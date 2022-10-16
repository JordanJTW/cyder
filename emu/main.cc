#include <SDL.h>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <iomanip>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "core/endian_helpers.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "gen/global_names.h"
#include "gen/trap_names.h"
#include "memory/memory_manager.h"
#include "memory/memory_map.h"
#include "memory/stack_monitor.h"
#include "resource_file.h"
#include "resource_manager.h"
#include "segment_loader.h"
#include "third_party/musashi/src/m68k.h"
#include "trap/trap_manager.h"

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
  return MUST(cyder::memory::kSystemMemory.Copy<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return be16toh(MUST(cyder::memory::kSystemMemory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return be32toh(MUST(cyder::memory::kSystemMemory.Copy<uint32_t>(address)));
}

unsigned int m68k_read_memory_8(unsigned int address) {
  cyder::memory::CheckReadAccess(address);
  return MUST(cyder::memory::kSystemMemory.Copy<uint8_t>(address));
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

  return be16toh(MUST(cyder::memory::kSystemMemory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  cyder::memory::CheckReadAccess(address);

  if (address < cyder::memory::kInterruptVectorTableEnd) {
    auto status = HandleException(address);
    CHECK(status.ok()) << std::move(status).message();
  }

  return be32toh(MUST(cyder::memory::kSystemMemory.Copy<uint32_t>(address)));
}

cyder::memory::StackMonitor stack_monitor;

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint8_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
  stack_monitor.MaybeHandleWrite(address, value, /*size=*/1);
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint16_t>(address, htobe16(value))
            .ok())
      << " unable to write " << std::hex << value << " to " << address;
  stack_monitor.MaybeHandleWrite(address, value, /*size=*/2);
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint32_t>(address, htobe32(value))
            .ok())
      << " unable to write " << std::hex << value << " to " << address;
  stack_monitor.MaybeHandleWrite(address, value, /*size=*/4);
}

cyder::memory::MemoryManager* memory_manager_ptr;

void cpu_instr_callback(unsigned int pc) {
  stack_monitor.UpdateStackState();

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

using cyder::ResourceManager;
using cyder::SegmentLoader;
using cyder::memory::kSystemMemory;
using cyder::memory::MemoryManager;
using cyder::trap::TrapManager;
using rsrcloader::ResourceFile;

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

  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window* window = SDL_CreateWindow("Cyder", SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED, 512, 384, 0);

  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  TrapManager trap_manager(memory_manager, resource_manager, segment_loader,
                           renderer);
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
      size_t address = be32toh(MUST(kSystemMemory.Copy<uint32_t>(handle)));
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

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      GlobalVars::ApplLimit, htobe(cyder::memory::kHeapEnd)));
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      GlobalVars::CurrentA5, htobe(cyder::memory::GetA5WorldPosition())));

  // Sets the size of the name to 0 so it is not read:
  // TODO: Store the application name here as a Pascal string
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(GlobalVars::CurApName, 0));

  // Assembly: RTS
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerEntryAddress, htobe16(0x4E75)));
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      0x28, htobe32(cyder::memory::kTrapManagerEntryAddress)));

  // Assembly: TST.W D0
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerExitAddress, htobe16(0x4A40)));
  // Assembly: RTS
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerExitAddress + 2, htobe16(0x4E75)));

  for (int i = 0; i < 1024; ++i) {
    RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
        cyder::memory::kBaseToolboxTrapAddress + (i * sizeof(uint16_t)),
        htobe16(0x4E75)));
  }
  for (int i = 0; i < 256; ++i) {
    RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
        cyder::memory::kBaseSystemTrapAddress + (i * sizeof(uint16_t)),
        htobe16(0x4E75)));
  }

  // FIXME: Create a more accurate AppParmHandle for the current application
  Handle handle = memory_manager.AllocateHandle(10, "AppParam");
  auto app_param = memory_manager.GetRegionForHandle(handle);
  RETURN_IF_ERROR(app_param.Write<Integer>(/*offset=*/0, /*vRefNum=*/0));
  RETURN_IF_ERROR(app_param.Write<uint32_t>(/*offset=*/2,
                                            /*fType=*/htobe<uint32_t>('ABCD')));
  RETURN_IF_ERROR(app_param.Write<Integer>(/*offset=*/6, /*versNum=*/0));
  RETURN_IF_ERROR(app_param.Write<uint8_t>(/*offset=*/8, /*fName=*/1));
  RETURN_IF_ERROR(app_param.Write<uint8_t>(/*offset=*/9, /*fName=*/'J'));
  RETURN_IF_ERROR(
      kSystemMemory.Write<Ptr>(GlobalVars::AppParmHandle, htobe(handle)));

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      GlobalVars::CurStackBase, htobe32(cyder::memory::kStackStart)));

  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_TARGET, 512, 384);

  SDL_Event event;
  bool should_exit = false;
  while (!should_exit) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    if (!single_step) {
      SDL_SetRenderTarget(renderer, texture);
      m68k_execute(100000);
      SDL_SetRenderTarget(renderer, NULL);
    }

    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_KEYDOWN:
          single_step = false;
          break;
        case SDL_QUIT:
          should_exit = true;
          break;
      }
    }
    PrintFrameTiming();
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return absl::OkStatus();
}
