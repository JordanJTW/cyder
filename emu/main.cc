#include <SDL2/SDL.h>

#include <iomanip>
#include <tuple>

#include "absl/status/status.h"
#include "core/endian_helpers.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "memory_manager.h"
#include "memory_map.h"
#include "resource.h"
#include "resource_file.h"
#include "segment_loader.h"
#include "stack_helpers.h"
#include "system_types.h"
#include "third_party/musashi/src/m68k.h"
#include "trap_helpers.h"
#include "trap_names.h"

using rsrcloader::GetTypeName;
using rsrcloader::ResId;
using rsrcloader::ResType;

constexpr bool disassemble_log = false;
constexpr bool memory_write_log = false;

constexpr size_t kGlobalAppNameAddr = 0x910;
constexpr size_t kCurrentStackBase = 0x908;

bool single_step = false;

extern core::MemoryRegion kSystemMemory;

typedef std::function<void(uint32_t)> on_exception_callback_t;

on_exception_callback_t on_exception_callback = nullptr;

SDL_Renderer* renderer;

void DrawRect(const Rect& rect,
              const std::tuple<uint8_t, uint8_t, uint8_t>& color) {
  SDL_SetRenderDrawColor(renderer, std::get<0>(color), std::get<1>(color),
                         std::get<2>(color), 255);

  int width = (rect.right - rect.left);
  int height = (rect.bottom - rect.top);

  if (width < 0 || height < 0)
    return;

  SDL_Rect sdl_rect = {
      .x = rect.left,
      .y = rect.top,
      .w = width,
      .h = height,
  };
  SDL_RenderFillRect(renderer, &sdl_rect);
}

// RAII class to capture the return address on the stack while processing an
// exception, modify it, then push it back on to the stack.
class ExceptionReturn {
 public:
  ExceptionReturn()
      : status_(MUST(Pop<uint16_t>(M68K_REG_SP))),
        return_address_(MUST(Pop<uint32_t>(M68K_REG_SP))) {}

  ~ExceptionReturn() {
    CHECK(Push<uint32_t>(return_address_ + return_offset_, M68K_REG_SP).ok());
    CHECK(Push<uint16_t>(status_, M68K_REG_SP).ok());
  }

  void SetReturnOffset(int16_t return_offset) {
    return_offset_ = return_offset;
  }

 private:
  // By default skip past the instruction that triggered the exception when
  // returning (the instruction should be 2-bytes wide).
  int16_t return_offset_{2};

  uint16_t status_;
  uint32_t return_address_;
};

absl::Status HandleALineTrap(SegmentLoader& segment_loader,
                             rsrcloader::ResourceFile& current_rsrc,
                             MemoryManager& memory_manager,
                             uint16_t trap,
                             ExceptionReturn& stack_return) {
  LOG(INFO) << "\u001b[38;5;160m"
            << "A-Line Exception " << (trap::IsToolbox(trap) ? "Toolbox" : "OS")
            << "::" << GetTrapName(trap) << " (0x" << std::hex << trap
            << ") Index: " << std::dec << trap::ExtractIndex(trap)
            << "\u001b[0m";

  CHECK(!trap::IsAutoPopSet(trap));
  if (trap::IsSystem(trap))
    LOG(INFO) << "Should return A0? "
              << (trap::IsReturnA0(trap) ? "true" : "false")
              << " Flags: " << trap::ExtractFlags(trap);

  switch (trap) {
    case Trap::LoadSeg: {
      uint16_t load_segment = TRY(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "TRAP LoadSeg(" << load_segment << ")";
      RETURN_IF_ERROR(segment_loader.Load(load_segment));
      // The segment loader modifies the six byte entry for this segment in the
      // table so return to the begining of the entry (4 bytes behind the 2 byte
      // trap being executed):
      stack_return.SetReturnOffset(-4);
      return absl::OkStatus();
    }
    case Trap::Get1NamedResource: {
      auto name = TRY(PopRef<absl::string_view>(M68K_REG_USP));
      ResType type = TRY(Pop<ResType>(M68K_REG_USP));

      LOG(INFO) << "TRAP Get1NamedResource(theType: '"
                << rsrcloader::GetTypeName(type) << "', name: \"" << name
                << "\")";
      return absl::UnimplementedError("");
    }
    case Trap::GetResource: {
      auto id = TRY(Pop<ResId>(M68K_REG_USP));
      auto type = TRY(Pop<ResType>(M68K_REG_USP));
      LOG(INFO) << "TRAP GetResource(theType: '" << GetTypeName(type)
                << "', theID: " << id << ")";

      auto* resource = current_rsrc.FindByTypeAndId(type, id);
      // FIXME: Set ResError in D0 and call ResErrorProc
      // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-35.html#MARKER-9-220
      CHECK(resource) << "Resource not found";

      LOG(INFO) << "Attributes: "
                << static_cast<int>(resource->GetAttributes());
      auto handle = memory_manager.AllocateHandleForRegion(
          resource->GetData(),
          absl::StrCat("Resource[", GetTypeName(type), ":", id, "]"));

      return TrapReturn<uint32_t>(handle);
    }
    case Trap::SizeRsrc: {
      auto handle = TRY(Pop<uint32_t>(M68K_REG_USP));
      LOG(INFO) << "TRAP GetResourceSizeOnDisk(theResource: 0x" << std::hex
                << handle << ")";
      // FIXME: This should read the size from disk not memory i.e. from
      // ResourceManager
      // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-82.html
      auto handle_size = memory_manager.GetHandleSize(handle);
      LOG(INFO) << "Handle size: " << handle_size;
      return TrapReturn<uint32_t>(handle_size);
    }
    case Trap::InitGraf: {
      auto globalPtr = TRY(Pop<Ptr>(M68K_REG_USP));
      LOG(INFO) << "TRAP InitGraf(globalPtr: 0x" << std::hex << globalPtr
                << ")";
      return absl::OkStatus();
    }
    case Trap::OpenPort: {
      auto thePortPtr = TRY(Pop<GrafPtr>(M68K_REG_USP));
      LOG(INFO) << "TRAP OpenPort(port: 0x" << std::hex << thePortPtr << ")";
      return absl::OkStatus();
    }
    case Trap::HideCursor: {
      LOG(INFO) << "TRAP HideCursor()";
      return absl::OkStatus();
    }
    case Trap::PaintRect: {
      auto rect = TRY(PopRef<Rect>(M68K_REG_USP));
      LOG(INFO) << "TRAP PaintRect(rect: " << rect << ")";
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      DrawRect(rect, {0, 0, 0});
      return absl::OkStatus();
    }
    case Trap::Random: {
      LOG(INFO) << "TRAP Random()";
      // FIXME: Use the same algorithm used in Mac OS to generate rand()
      RETURN_IF_ERROR(TrapReturn<int16_t>(0xFFFF * rand()));
      return absl::OkStatus();
    }
    case Trap::Button: {
      LOG(INFO) << "TRAP Button()";
      RETURN_IF_ERROR(TrapReturn<uint16_t>(0x0000));
      return absl::OkStatus();
    }
    case Trap::PaintOval: {
      auto rect = TRY(PopRef<Rect>(M68K_REG_USP));
      LOG(INFO) << "TRAP PaintOval(rect: " << rect << ")";
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      DrawRect(rect, {0, 0, 0});
      return absl::OkStatus();
    }
    case Trap::EraseOval: {
      auto rect = TRY(PopRef<Rect>(M68K_REG_USP));
      LOG(INFO) << "TRAP EraseOval(rect: " << rect << ")";
      // FIXME: Clear with the color set for QuickDraw (A5 World?)
      DrawRect(rect, {0xFF, 0xBF, 0x00});
      return absl::OkStatus();
    }
    case Trap::SysBeep: {
      uint16_t duration = TRY(Pop<Integer>(M68K_REG_USP));
      LOG(INFO) << "TRAP SysBeep(duration: " << duration << ")";
      return absl::OkStatus();
    }
    case Trap::ExitToShell:
      LOG(INFO) << "TRAP ExitToShell()";
      exit(0);
      return absl::OkStatus();
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Reached unimplemented trap: '", GetTrapName(trap), "'"));
  }
}

absl::Status HandleException(SegmentLoader& segment_loader,
                             rsrcloader::ResourceFile& current_rsrc,
                             MemoryManager& memory_manager,
                             unsigned int address) {
  CHECK_LT(address, 0x100) << "Address 0x" << std::hex << address
                           << " is outside of the IVT";

  switch (address) {
    // 1010 Instruction Trap Handler
    case 0x28: {
      uint16_t trap_op = be16toh(
          TRY(kSystemMemory.Copy<uint16_t>(m68k_get_reg(NULL, M68K_REG_PPC))));

      ExceptionReturn exception_return;
      RETURN_IF_ERROR(HandleALineTrap(segment_loader, current_rsrc,
                                      memory_manager, trap_op,
                                      exception_return));
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError(
          absl::StrCat("Exception occured with no handler: ", address));
  }
}

unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(kSystemMemory.Copy<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return be16toh(MUST(kSystemMemory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return be32toh(MUST(kSystemMemory.Copy<uint32_t>(address)));
}

unsigned int m68k_read_memory_8(unsigned int address) {
  CheckReadAccess(address);
  return MUST(kSystemMemory.Copy<uint8_t>(address));
}
unsigned int m68k_read_memory_16(unsigned int address) {
  CheckReadAccess(address);
  return be16toh(MUST(kSystemMemory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  CheckReadAccess(address);

  if (address < kInterruptVectorTableEnd) {
    if (on_exception_callback) {
      on_exception_callback(address);
    }
    return kExceptionReturnAddr;
  }

  return be32toh(MUST(kSystemMemory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(kSystemMemory.Write<uint8_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(kSystemMemory.Write<uint16_t>(address, htobe16(value)).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(kSystemMemory.Write<uint32_t>(address, htobe32(value)).ok())
      << " unable to write " << std::hex << value << " to " << address;
}

MemoryManager* memory_manager_ptr;

void cpu_instr_callback(unsigned int pc) {
  CHECK(pc != 0) << "Reset";
  if (disassemble_log) {
#define REG(name) " " << #name << ": 0x" << m68k_get_reg(NULL, M68K_REG_##name)

    LOG(INFO) << "\u001b[38;5;240m" << std::hex << REG(A0) << REG(A1) << REG(A2)
              << REG(A3) << REG(A4) << REG(A5) << REG(A6) << REG(A7) << REG(USP)
              << REG(ISP) << "\u001b[0m";
    LOG(INFO) << "\u001b[38;5;240m" << std::hex << REG(D0) << REG(D1) << REG(D2)
              << REG(D3) << REG(D4) << REG(D5) << REG(D6) << REG(D7)
              << "\u001b[0m";

    LOG(INFO) << "\u001b[38;5;240m"
              << "Handles: " << memory_manager_ptr->LogHandles() << "\u001b[0m";
#undef REG

    char buffer[255];
    size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
    Handle handle = memory_manager_ptr->GetHandleThatContains(pc);
    std::string tag = (handle == 0) ? "" : memory_manager_ptr->GetTag(handle);
    LOG(INFO) << std::hex << pc << " (" << tag << "): " << buffer;
  }
  CHECK(m68k_get_reg(NULL, M68K_REG_USP) <= kUserStackStart);
  CHECK(m68k_get_reg(NULL, M68K_REG_ISP) <= kInterruptStackStart);
  if (single_step)
    m68k_end_timeslice();
}

void PrintFrameTiming(std::ostream& os = std::cout, float period = 2.0f) {
  static unsigned int frames = 0;
  frames++;
  static auto start = std::chrono::steady_clock::now();
  auto end = std::chrono::steady_clock::now();

  float seconds =
      std::chrono::duration_cast<std::chrono::duration<float> >(end - start)
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

absl::Status Main(const core::Args& args) {
  auto file =
      TRY(rsrcloader::ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

  MemoryManager memory_manager;
  memory_manager_ptr = &memory_manager;

  auto segment_loader = TRY(SegmentLoader::Create(*file, memory_manager));
  RETURN_IF_ERROR(segment_loader.Load(1));

  size_t pc = segment_loader.entry_point();
  LOG(INFO) << "Initialize PC: " << std::hex << pc;
  LOG(INFO) << "Memory Map: " << MemoryMapToStr();

  on_exception_callback = [&](uint32_t address) {
    auto status =
        HandleException(segment_loader, *file, memory_manager, address);
    CHECK(status.ok()) << std::move(status).message();
  };

  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_PC, pc);
  m68k_set_reg(M68K_REG_A5, GetA5WorldPosition());
  m68k_set_reg(M68K_REG_USP, kUserStackStart);
  m68k_set_reg(M68K_REG_ISP, kInterruptStackStart);

  // Sets the size of the name to 0 so it is not read:
  // TODO: Store the application name here as a Pascal string
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(kGlobalAppNameAddr, 0));
  // Stores the 'RTE' op-code at the exception return address to
  // jump back from the exception handler back to user code:
  RETURN_IF_ERROR(
      kSystemMemory.Write<uint16_t>(kExceptionReturnAddr, htobe16(0x4E73)));

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(kCurrentStackBase,
                                                htobe32(kUserStackStart)));

  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window* window = SDL_CreateWindow("Cyder", SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED, 512, 384, 0);

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

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
