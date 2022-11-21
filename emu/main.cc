#include <SDL.h>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <iomanip>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/event_manager.h"
#include "emu/graphics/bitmap_screen.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/memory/memory_manager.h"
#include "emu/memory/memory_map.h"
#include "emu/menu_manager.h"
#include "emu/segment_loader.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_manager.h"
#include "gen/global_names.h"
#include "gen/trap_names.h"
#include "resource_file.h"
#include "resource_manager.h"
#include "third_party/musashi/src/m68k.h"

ABSL_FLAG(bool,
          disassemble,
          /*default_value=*/false,
          "Disassemble the m68k instructions being executed");

ABSL_FLAG(std::string,
          system_file,
          /*default_value=*/"",
          "A Macintosh System (1-5) resource file to pull from");

constexpr bool memory_write_log = false;

constexpr size_t break_on_line = 0;

bool single_step = false;
bool breakpoint = false;

constexpr SDL_Color kOnColor = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr SDL_Color kOffColor = {0x00, 0x00, 0x00, 0xFF};

constexpr size_t kScreenWidth = 512;
constexpr size_t kScreenHeight = 384;
constexpr size_t kScaleFactor = 1;

constexpr uint8_t kGreyPattern[8] = {0xAA, 0x55, 0xAA, 0x55,
                                     0xAA, 0x55, 0xAA, 0x55};

typedef std::function<absl::Status(uint32_t)> on_address_callback_t;

on_address_callback_t on_emulated_subroutine = nullptr;

absl::Status HandleException(unsigned int address) {
  CHECK_LT(address, 0x100) << "Address 0x" << std::hex << address
                           << " is outside of the IVT";

  switch (address) {
    case 0x28:
      return absl::OkStatus();
    default:
      return absl::UnimplementedError(
          absl::StrCat("Exception occured with no handler: ", address));
  }
}

unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(cyder::memory::kSystemMemory.Read<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return MUST(cyder::memory::kSystemMemory.Read<uint16_t>(address));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return MUST(cyder::memory::kSystemMemory.Read<uint32_t>(address));
}

unsigned int m68k_read_memory_8(unsigned int address) {
  cyder::memory::CheckReadAccess(address);
  return MUST(cyder::memory::kSystemMemory.Read<uint8_t>(address));
}
unsigned int m68k_read_memory_16(unsigned int address) {
  cyder::memory::CheckReadAccess(address);

  if (address >= cyder::memory::kLastEmulatedSubroutineAddress &&
      address < cyder::memory::kSystemMemorySize) {
    CHECK(on_emulated_subroutine)
        << "No emulated subroutine callback registered";
    auto status = on_emulated_subroutine(address);
    CHECK(status.ok()) << std::move(status).message();
  }

  return MUST(cyder::memory::kSystemMemory.Read<uint16_t>(address));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  cyder::memory::CheckReadAccess(address);

  if (address < cyder::memory::kInterruptVectorTableEnd) {
    auto status = HandleException(address);
    CHECK(status.ok()) << std::move(status).message();
  }

  return MUST(cyder::memory::kSystemMemory.Read<uint32_t>(address));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint8_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint16_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint32_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
}

cyder::memory::MemoryManager* memory_manager_ptr;

void cpu_instr_callback(unsigned int pc) {
  if (pc == break_on_line) {
    LOG(INFO) << "Breakpoint!";
    breakpoint = true;
  }

  CHECK(pc != 0) << "Reset";
  if (absl::GetFlag(FLAGS_disassemble) || breakpoint) {
#define REG(name)                                                    \
  COLOR(240) << std::hex << " " << #name << ": 0x"                   \
             << m68k_get_reg(NULL, M68K_REG_##name) << COLOR_RESET() \
             << std::dec

    LOG(INFO) << REG(A0) << REG(A1) << REG(A2) << REG(A3) << REG(A4) << REG(A5)
              << REG(A6) << REG(A7);
    LOG(INFO) << REG(D0) << REG(D1) << REG(D2) << REG(D3) << REG(D4) << REG(D5)
              << REG(D6) << REG(D7);

    LOG(INFO) << COLOR(240) << "Handles: " << memory_manager_ptr->LogHandles()
              << COLOR_RESET();

    cyder::memory::debug::LogStack();
#undef REG

    char buffer[255];
    m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
    Handle handle = memory_manager_ptr->GetHandleThatContains(pc);
    std::string tag = (handle == 0) ? "" : memory_manager_ptr->GetTag(handle);
    LOG(INFO) << std::hex << pc << " (" << tag << "): " << buffer;
  }
  CHECK(m68k_get_reg(NULL, M68K_REG_ISP) <= cyder::memory::kStackStart);

  single_step = breakpoint;

  if (single_step)
    m68k_end_timeslice();
}

void PrintFrameTiming(std::ostream& os = std::cout, float period = 2.0f) {
  static unsigned int frames = 0;
  frames++;
  static auto start = std::chrono::steady_clock::now();
  auto end = std::chrono::steady_clock::now();

  float seconds =
      std::chrono::duration_cast<std::chrono::duration<float>>(end - start)
          .count();
  if (seconds > period) {
    float spf = seconds / frames;
    os << frames << " frames in " << std::setprecision(1) << std::fixed
       << seconds << " seconds = " << std::setprecision(1) << std::fixed
       << 1.0f / spf << " FPS (" << std::setprecision(3) << std::fixed
       << spf * 1000.0f << " ms/frame)\n";
    frames = 0;
    start = end;
  }
}

using cyder::EventManager;
using cyder::MenuManager;
using cyder::ResourceManager;
using cyder::SegmentLoader;
using cyder::graphics::BitmapScreen;
using cyder::memory::kSystemMemory;
using cyder::memory::MemoryManager;
using cyder::trap::TrapManager;
using rsrcloader::ResourceFile;

SDL_Surface* const MakeSurface(const BitmapScreen& screen) {
  static SDL_Surface* const surface = SDL_CreateRGBSurfaceWithFormat(
      SDL_SWSURFACE, kScreenWidth, kScreenHeight, 1, SDL_PIXELFORMAT_INDEX1MSB);
  surface->pixels = const_cast<uint8_t*>(screen.bits());

  static SDL_Color colors[2] = {kOnColor, kOffColor};
  SDL_SetPaletteColors(surface->format->palette, colors, 0, 2);
  return surface;
}

absl::Status Main(const core::Args& args) {
  auto file = TRY(ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

  MemoryManager memory_manager;
  memory_manager_ptr = &memory_manager;

  ResourceManager resource_manager(memory_manager, *file);

  auto segment_loader =
      TRY(SegmentLoader::Create(memory_manager, resource_manager));

  size_t pc = TRY(segment_loader.Load(1));
  LOG(INFO) << "Initialize PC: " << std::hex << pc;
  LOG(INFO) << "Memory Map: " << cyder::memory::MemoryMapToStr();

  BitmapScreen screen(kScreenWidth, kScreenHeight);

  MenuManager menu_manager(screen);

  // Draw classic Mac OS grey baground pattern
  auto screen_rect = NewRect(0, 0, kScreenWidth, kScreenHeight);
  screen.FillRect(screen_rect, kGreyPattern);

  SDL_Init(SDL_INIT_VIDEO);

  int window_width = int(kScreenWidth * kScaleFactor);
  int window_height = int(kScreenHeight * kScaleFactor);

  SDL_Window* window = SDL_CreateWindow(
      "Cyder", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width,
      window_height, SDL_WINDOW_ALLOW_HIGHDPI);

  CHECK(window) << "Failing to create window: " << SDL_GetError();

  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);

  CHECK(renderer) << "Failing to create renderer: " << SDL_GetError();

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);

  EventManager event_manager;

  TrapManager trap_manager(memory_manager, resource_manager, segment_loader,
                           event_manager, menu_manager, screen);
  on_emulated_subroutine = std::bind(&TrapManager::DispatchEmulatedSubroutine,
                                     &trap_manager, std::placeholders::_1);

  auto system_path = absl::GetFlag(FLAGS_system_file);
  if (!system_path.empty()) {
    auto system = TRY(ResourceFile::Load(system_path));
    if (auto* version = system->FindByTypeAndId('STR ', 0)) {
      LOG(INFO) << "Using System: " << system_path;
      LOG(INFO) << TRY(ReadType<absl::string_view>(version->GetData(), 0));
    }

    if (auto* pack4 = system->FindByTypeAndId('PACK', 4)) {
      LOG(INFO) << "Loading PACK4 into memory";
      Handle handle =
          memory_manager.AllocateHandleForRegion(pack4->GetData(), "PACK4");
      size_t address = MUST(kSystemMemory.Read<uint32_t>(handle));
      trap_manager.SetTrapAddress(Trap::Pack4, address);
    }
  }

  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_PC, pc);
  m68k_set_reg(M68K_REG_A5, cyder::memory::GetA5WorldPosition());
  m68k_set_reg(M68K_REG_SP, cyder::memory::kStackStart);

  // Mac OS _always_ runs in supervisor mode so set the SR
  // Link: https://en.wikibooks.org/wiki/68000_Assembly/Registers
  uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
  m68k_set_reg(M68K_REG_SR, sr | (1 << 13));

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(GlobalVars::ApplLimit,
                                                cyder::memory::kHeapEnd));
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      GlobalVars::CurrentA5, cyder::memory::GetA5WorldPosition()));

  // Sets the size of the name to 0 so it is not read:
  // TODO: Store the application name here as a Pascal string
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(GlobalVars::CurApName, 0));

  // Assembly: RTS
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerEntryAddress, 0x4E75));
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      0x28, cyder::memory::kTrapManagerEntryAddress));

  // Assembly: TST.W D0
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerExitAddress, 0x4A40));
  // Assembly: RTS
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerExitAddress + 2, 0x4E75));

  for (int i = 0; i < 1024; ++i) {
    RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
        cyder::memory::kBaseToolboxTrapAddress + (i * sizeof(uint16_t)),
        0x4E75));
  }
  for (int i = 0; i < 256; ++i) {
    RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
        cyder::memory::kBaseSystemTrapAddress + (i * sizeof(uint16_t)),
        0x4E75));
  }

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(GlobalVars::CurStackBase,
                                                cyder::memory::kStackStart));

  RETURN_IF_ERROR(cyder::trap::Push<uint32_t>(
      cyder::memory::kBaseToolboxTrapAddress +
      (Trap::ExitToShell & 0x03FF) * sizeof(uint16_t)));
  SDL_Event event;
  bool should_exit = false;
  while (!should_exit) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    if (!single_step) {
      m68k_execute(100000);
    }

    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(renderer, MakeSurface(screen));
    CHECK(texture) << "Failed to create texture: " << SDL_GetError();

    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_DestroyTexture(texture);

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_KEYDOWN:
          single_step = false;
          break;
        case SDL_MOUSEBUTTONDOWN:
          event_manager.QueueMouseDown(event.button.x, event.button.y);
          break;
        case SDL_MOUSEMOTION:
          menu_manager.OnMouseMove(event.motion.x, event.motion.y);
          break;
        case SDL_MOUSEBUTTONUP:
          menu_manager.OnMouseUp(event.button.x, event.button.y);
          break;
        case SDL_QUIT:
          should_exit = true;
          break;
      }
    }
    PrintFrameTiming();
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return absl::OkStatus();
}
