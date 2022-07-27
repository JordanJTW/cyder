#include <SDL2/SDL.h>

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
#include "global_names.h"
#include "memory/memory_manager.h"
#include "memory/memory_map.h"
#include "resource_file.h"
#include "resource_manager.h"
#include "segment_loader.h"
#include "third_party/musashi/src/m68k.h"
#include "trap/trap_manager.h"

ABSL_FLAG(bool,
          disassemble,
          /*default_value=*/false,
          "Disassemble the m68k instructions being executed");

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

  if (address > cyder::memory::kLastEmulatedSubroutineAddress) {
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
  CHECK(cyder::memory::kSystemMemory.Write<uint16_t>(address, htobe16(value))
            .ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(cyder::memory::kSystemMemory.Write<uint32_t>(address, htobe32(value))
            .ok())
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
#define REG(name) " " << #name << ": 0x" << m68k_get_reg(NULL, M68K_REG_##name)

    LOG(INFO) << "\u001b[38;5;240m" << std::hex << REG(A0) << REG(A1) << REG(A2)
              << REG(A3) << REG(A4) << REG(A5) << REG(A6) << REG(A7)
              << "\u001b[0m";
    LOG(INFO) << "\u001b[38;5;240m" << std::hex << REG(D0) << REG(D1) << REG(D2)
              << REG(D3) << REG(D4) << REG(D5) << REG(D6) << REG(D7)
              << "\u001b[0m";

    LOG(INFO) << "\u001b[38;5;240m"
              << "Handles: " << memory_manager_ptr->LogHandles() << "\u001b[0m";

    Ptr stack_head = m68k_get_reg(NULL, M68K_REG_SP);
    uint32_t stack_length = cyder::memory::kStackStart - stack_head;
    LOG(INFO) << "\u001b[38;5;240mStack:\n"
              << *cyder::memory::kSystemMemory.Create("Stack", stack_head,
                                                      stack_length)
              << "\u001b[0m";

    LOG(INFO) << "\u001b[38;5;240mStatus: "
              << std::bitset<16>(m68k_get_reg(NULL, M68K_REG_SR)) << REG(SP)
              << "\u001b[0m";
#undef REG

    char buffer[255];
    size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
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

absl::Status Main(const core::Args& args) {
  auto file =
      TRY(rsrcloader::ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

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

  // Sets the size of the name to 0 so it is not read:
  // TODO: Store the application name here as a Pascal string
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(GlobalVars::CurApName, 0));

  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerEntryAddress, htobe16(0x4E75)));
  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerDispatchAddress, htobe16(0x4E75)));
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      0x28, htobe32(cyder::memory::kTrapManagerEntryAddress)));

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
