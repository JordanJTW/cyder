#include "trap_manager.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <iomanip>
#include <tuple>

#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/memory/memory_map.h"
#include "emu/system_types.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_helpers.h"
#include "resource.h"
#include "third_party/musashi/src/m68k.h"
#include "trap_names.h"

namespace cyder {
namespace trap {
namespace {

using rsrcloader::GetTypeName;
using rsrcloader::ResId;
using rsrcloader::ResType;

void DrawRect(SDL_Renderer* renderer,
              const Rect& rect,
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

template <typename T>
class RestorePop {
 public:
  RestorePop() : value(MUST(Pop<T>())) {}
  ~RestorePop() { CHECK(Push<T>(value).ok()); }

  T value;
};

absl::Status HandleLoadSegmentTrap(SegmentLoader& segment_loader,
                                   RestorePop<Ptr>& return_address) {
  uint16_t load_segment = TRY(Pop<uint16_t>());
  LOG(INFO) << "TRAP LoadSeg(" << load_segment << ")";
  TRY(segment_loader.Load(load_segment));
  // The segment loader modifies the six byte entry for this segment in the
  // table so return to the begining of the entry (4 bytes behind the 2 byte
  // trap being executed):
  return_address.value -= 6;
  return absl::OkStatus();
}

}  // namespace

TrapManager::TrapManager(memory::MemoryManager& memory_manager,
                         ResourceManager& resource_manager,
                         SegmentLoader& segment_loader,
                         SDL_Renderer* renderer)
    : memory_manager_(memory_manager),
      resource_manager_(resource_manager),
      segment_loader_(segment_loader),
      renderer_(renderer) {
  CHECK(renderer);
}

absl::Status TrapManager::DispatchEmulatedSubroutine(uint32_t address) {
  switch (address) {
    case memory::kTrapManagerEntryAddress:
      return PerformTrapEntry();
    case memory::kTrapManagerDispatchAddress:
      return PerformTrapDispatch();
    default:
      return absl::UnimplementedError(
          absl::StrCat("No subroutine registered for address: ", address));
  }
}

absl::Status TrapManager::PerformTrapEntry() {
  auto status_register = TRY(Pop<uint16_t>());
  auto instruction_ptr = TRY(Pop<uint32_t>());

  uint16_t trap_op =
      be16toh(TRY(memory::kSystemMemory.Copy<uint16_t>(instruction_ptr)));

  LOG(INFO) << "\u001b[38;5;160m"
            << "A-Line Exception " << (IsToolbox(trap_op) ? "Toolbox" : "OS")
            << "::" << GetTrapName(trap_op) << " (0x" << std::hex << trap_op
            << ") Index: " << std::dec << ExtractIndex(trap_op) << "\u001b[0m";

  CHECK(!IsAutoPopSet(trap_op));

  // `instruction_ptr` points to the address of the instruction that triggered
  // the trap. When we return from handling the trap return to the instruction
  // past the 16-bit A-Line Trap (i.e. + 2).
  RETURN_IF_ERROR(Push<uint32_t>(instruction_ptr + 2));

  uint32_t dispatch_address = GetTrapAddress(trap_op);
  RETURN_IF_ERROR(Push<uint32_t>(dispatch_address));
  return absl::OkStatus();
}

absl::Status TrapManager::PerformTrapExit() {
  return absl::OkStatus();
}

absl::Status TrapManager::PerformTrapDispatch() {
  // The return address should be at the top of the stack which is +2 past the
  // address of the instruction that caused the trap (see above).
  RestorePop<Ptr> return_address;
  Ptr trap_address = return_address.value - 2;

  auto trap = be16toh(TRY(memory::kSystemMemory.Copy<uint16_t>(trap_address)));

  // Handle _LoadSeg specially since it needs to modify the return address.
  if (Trap::LoadSeg == trap) {
    return HandleLoadSegmentTrap(segment_loader_, return_address);
  }
  return IsToolbox(trap) ? DispatchNativeToolboxTrap(trap)
                         : DispatchNativeSystemTrap(trap);
}

uint32_t TrapManager::GetTrapAddress(uint16_t trap) {
  if (trap == Trap::Unimplemented) {
    return 0;
  }

  auto patch_address = patch_trap_addresses_.find(trap);
  if (patch_address != patch_trap_addresses_.cend()) {
    return patch_address->second;
  } else {
    return memory::kTrapManagerDispatchAddress;
  }
}

absl::Status TrapManager::DispatchNativeSystemTrap(uint16_t trap) {
  CHECK(IsSystem(trap));

  switch (trap) {
    // ===================  MemoryManager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-103.html
    case Trap::BlockMove: {
      uint32_t source_ptr = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t dest_ptr = m68k_get_reg(NULL, M68K_REG_A1);
      uint32_t byte_count = m68k_get_reg(NULL, M68K_REG_D0);

      LOG(INFO) << "TRAP BlockMove(sourcePtr: 0x" << std::hex << source_ptr
                << ", destPtr: 0x" << dest_ptr << ", byteCount: " << byte_count
                << ")";

      // FIXME: Allow for more efficient copies in system memory (memcpy)?
      for (size_t i = 0; i < byte_count; ++i) {
        RETURN_IF_ERROR(memory::kSystemMemory.Write<uint8_t>(
            dest_ptr + i,
            TRY(memory::kSystemMemory.Copy<uint8_t>(source_ptr + i))));
      }
      // Return result code "noErr"
      m68k_set_reg(M68K_REG_D0, 0);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-79.html
    case Trap::DisposePtr: {
      uint32_t ptr = m68k_get_reg(NULL, M68K_REG_A0);

      LOG(INFO) << "TRAP DisposePtr(ptr: 0x" << std::hex << ptr << ")";

      uint32_t status = 0;
      m68k_set_reg(M68K_REG_D0, status);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-81.html
    case Trap::GetHandleSize: {
      uint32_t handle = m68k_get_reg(NULL, M68K_REG_A0);
      LOG(INFO) << "TRAP GetHandleSize(handle: 0x" << std::hex << handle << ")";
      m68k_set_reg(M68K_REG_D0, memory_manager_.GetHandleSize(handle));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-75.html
    case Trap::NewPtr:
    // FIXME: Should "SYS" pointers be allocated differently?
    case Trap::NewPtrSys: {
      // D0 seems to contain the argument in a sample program...
      // but the documentation says it should be in A0.
      uint32_t logical_size = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP NewPtr(logicalSize: " << logical_size << ")";
      auto ptr = memory_manager_.Allocate(logical_size);
      m68k_set_reg(M68K_REG_A0, ptr);
      return absl::OkStatus();
    }

    // =======================  Trap Manager  ====================

    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-175.html
    case Trap::GetOSTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP GetOSTrapAddress(trap: '"
                << GetTrapNameBySystemIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-176.html
    case Trap::GetToolBoxTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP GetToolBoxTrapAddress(trap: '"
                << GetTrapNameByToolboxIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-185.html
    case Trap::GetTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP GetTrapAddress(trap: '" << GetTrapName(trap_index)
                << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-186.html
    case Trap::SetTrapAddress: {
      uint32_t trap_address = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFF;
      LOG(INFO) << "TRAP SetTrapAddress(trapAddr: 0x" << std::hex
                << trap_address << ", trap: '" << GetTrapName(trap_index)
                << "')";

      if (trap_address == memory::kTrapManagerDispatchAddress) {
        patch_trap_addresses_.erase(trap_index);
        return absl::OkStatus();
      }

      patch_trap_addresses_[trap_index] = trap_address;

      for (const auto& pair : patch_trap_addresses_) {
        LOG(INFO) << "Patch trap: 0x" << std::hex << pair.first << " -> 0x"
                  << pair.second;
      }
      return absl::OkStatus();
    }

    // =====================  Event Manager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-56.html
    case Trap::FlushEvents: {
      uint32_t arguments = m68k_get_reg(NULL, M68K_REG_D0);
      uint16_t eventMask = arguments & 0xFFFF;
      uint16_t stopMask = (2 >> arguments) & 0xFFFF;

      LOG(INFO) << "TRAP FlushEvents(eventMask: 0x" << std::hex
                << std::setfill('0') << std::setw(4) << eventMask
                << ", stopMask: 0x" << std::setfill('0') << std::setw(4)
                << stopMask << ")";
      return absl::OkStatus();
    }

    // =====================  File Manager  ========================

    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-232.html#HEADING232-0
    case Trap::Open: {
      // Link:
      // https://dev.os9.ca/techpubs/mac/Files/Files-301A.html#HEADING301-362
      // typedef struct IOParam {
      //  QElemPtr   qLink;         /* next queue entry */
      //  short      qType;         /* queue type */
      //  short      ioTrap;        /* routine trap */
      //  Ptr        ioCmdAddr;     /* routine address */
      //  ProcPtr    ioCompletion;  /* completion routine address */
      //  OSErr      ioResult;      /* result code */
      //  StringPtr  ioNamePtr;     /* pointer to driver name */
      //  short      ioVRefNum;     /* volume reference or drive number * /
      //  short      ioRefNum;      /* driver reference number */
      //  char       ioVersNum;     /* not used by the Device Manager * /
      //  char       ioPermssn;     /* read/write permission */
      //  Ptr        ioMisc;        /* not used by the Device Manager * /
      //  Ptr        ioBuffer;      /* pointer to data buffer */
      //  long       ioReqCount;    /* requested number of bytes */
      //  long       ioActCount;    /* actual number of bytes completed * /
      //  short      ioPosMode;     /* positioning mode */
      //  long       ioPosOffset;   /* positioning offset */
      // } IOParam;
      uint32_t ptr = m68k_get_reg(NULL, M68K_REG_A0);
      LOG(INFO) << "TRAP Open(ptr: 0x" << std::hex << ptr << ")";
      auto qLink = TRY(memory::kSystemMemory.Copy<Ptr>(ptr));
      auto qType = TRY(memory::kSystemMemory.Copy<uint8_t>(ptr + 4));
      auto ioTrap = TRY(memory::kSystemMemory.Copy<uint8_t>(ptr + 5));
      auto ioCmdAddr = TRY(memory::kSystemMemory.Copy<Ptr>(ptr + 6));
      auto ioCompletion = TRY(memory::kSystemMemory.Copy<Ptr>(ptr + 10));
      auto ioResult = TRY(memory::kSystemMemory.Copy<int16_t>(ptr + 14));
      auto ioNamePtr = TRY(memory::kSystemMemory.Copy<Ptr>(ptr + 16));
      auto ioVRefNum = TRY(memory::kSystemMemory.Copy<uint8_t>(ptr + 20));
      auto ioRefNum = TRY(memory::kSystemMemory.Copy<uint8_t>(ptr + 21));
      auto ioVersNum = TRY(memory::kSystemMemory.Copy<char>(ptr + 22));
      auto ioPermssn = TRY(memory::kSystemMemory.Copy<char>(ptr + 23));
      auto ioMisc = TRY(memory::kSystemMemory.Copy<Ptr>(ptr + 24));
      auto ioBuffer = TRY(memory::kSystemMemory.Copy<Ptr>(ptr + 28));
      auto ioReqCount = TRY(memory::kSystemMemory.Copy<uint16_t>(ptr + 32));
      auto ioActCount = TRY(memory::kSystemMemory.Copy<uint16_t>(ptr + 34));
      auto ioPosMode = TRY(memory::kSystemMemory.Copy<uint8_t>(ptr + 36));
      auto ioPosOffset = TRY(memory::kSystemMemory.Copy<uint16_t>(ptr + 37));

#define FIELD(name) "\n\t" << #name << ": " << (static_cast<int>(name)) << ", "
      LOG(INFO) << "IOParam: {" << FIELD(qLink) << FIELD(qType) << FIELD(ioTrap)
                << FIELD(ioCmdAddr) << FIELD(ioCompletion) << FIELD(ioResult)
                << FIELD(ioNamePtr) << FIELD(ioVRefNum) << FIELD(ioRefNum)
                << FIELD(ioVersNum) << FIELD(ioPermssn) << FIELD(ioMisc)
                << FIELD(ioBuffer) << FIELD(ioReqCount) << FIELD(ioActCount)
                << FIELD(ioPosMode) << FIELD(ioPosOffset) << "\n}";
#undef FIELD
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError(
          absl::StrCat("Unimplemented system trap: '", GetTrapName(trap), "'"));
  }
}

absl::Status TrapManager::DispatchNativeToolboxTrap(uint16_t trap) {
  CHECK(IsToolbox(trap));

  switch (trap) {
    // =================  Event Manager  ==================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-73.html
    case Trap::Button: {
      LOG(INFO) << "TRAP Button()";
      RETURN_IF_ERROR(TrapReturn<uint16_t>(0x0000));
      return absl::OkStatus();
    }

    // =================  Process Manager  ====================

    // Link: http://0.0.0.0:8000/docs/mac/Processes/Processes-51.html
    case Trap::ExitToShell: {
      LOG(INFO) << "TRAP ExitToShell()";
      exit(0);
      return absl::OkStatus();
    }

    // ====================  QuickDraw  ======================

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-32.html
    case Trap::OpenPort: {
      auto thePortPtr = TRY(Pop<GrafPtr>());
      LOG(INFO) << "TRAP OpenPort(port: 0x" << std::hex << thePortPtr << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-392.html
    case Trap::HideCursor: {
      LOG(INFO) << "TRAP HideCursor()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-98.html
    case Trap::PaintRect: {
      auto rect = TRY(PopRef<Rect>());
      LOG(INFO) << "TRAP PaintRect(rect: " << rect << ")";
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      DrawRect(renderer_, rect, {0, 0, 0});
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-63.html
    case Trap::Random: {
      LOG(INFO) << "TRAP Random()";
      // FIXME: Use the same algorithm used in Mac OS to generate rand()
      RETURN_IF_ERROR(TrapReturn<int16_t>(0xFFFF * rand()));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-110.html
    case Trap::PaintOval: {
      auto rect = TRY(PopRef<Rect>());
      LOG(INFO) << "TRAP PaintOval(rect: " << rect << ")";
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      DrawRect(renderer_, rect, {0, 0, 0});
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-112.html
    case Trap::EraseOval: {
      auto rect = TRY(PopRef<Rect>());
      LOG(INFO) << "TRAP EraseOval(rect: " << rect << ")";
      // FIXME: Clear with the color set for QuickDraw (A5 World?)
      DrawRect(renderer_, rect, {0xFF, 0xBF, 0x00});
      return absl::OkStatus();
    }

    // ================== Resource Manager ==================

    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-53.html
    case Trap::Get1NamedResource: {
      auto name = TRY(PopRef<absl::string_view>());
      ResType type = TRY(Pop<ResType>());

      LOG(INFO) << "TRAP Get1NamedResource(theType: '"
                << rsrcloader::GetTypeName(type) << "', name: \"" << name
                << "\")";

      Handle handle = resource_manager_.GetResourseByName(type, name);
      return TrapReturn<uint32_t>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-50.html
    case Trap::GetResource: {
      auto id = TRY(Pop<ResId>());
      auto type = TRY(Pop<ResType>());

      LOG(INFO) << "TRAP GetResource(theType: '" << GetTypeName(type)
                << "', theID: " << id << ")";

      Handle handle = resource_manager_.GetResource(type, id);
      return TrapReturn<uint32_t>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-56.html
    case Trap::LoadResource: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP LoadResource(theResource: 0x" << std::hex << handle
                << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-85.html
    case Trap::ReleaseResource: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP ReleaseResource(theResource: 0x" << std::hex << handle
                << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-82.html
    case Trap::SizeRsrc: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP GetResourceSizeOnDisk(theResource: 0x" << std::hex
                << handle << ")";
      // FIXME: This should read the size from disk not memory
      auto handle_size = memory_manager_.GetHandleSize(handle);
      return TrapReturn<uint32_t>(handle_size);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-60.html
    case Trap::GetResAttrs: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP GetResAttrs(theResource: 0x" << std::hex << handle
                << ")";
      // FIXME: Load the actual attributes from the resource...
      uint16_t attrs = 8;
      return TrapReturn<uint16_t>(attrs);
    }

    // ====================  Sound Manager  =====================

    // Link: http://0.0.0.0:8000/docs/mac/Sound/Sound-90.html
    case Trap::SysBeep: {
      uint16_t duration = TRY(Pop<Integer>());
      LOG(INFO) << "TRAP SysBeep(duration: " << duration << ")";
      return absl::OkStatus();
    }

    // =====================  Initializers  =====================

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-30.html
    case Trap::InitGraf: {
      auto globalPtr = TRY(Pop<Ptr>());
      LOG(INFO) << "TRAP InitGraf(globalPtr: 0x" << std::hex << globalPtr
                << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-222.html
    case Trap::InitFonts: {
      LOG(INFO) << "TRAP InitFonts()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-223.html
    case Trap::InitWindows: {
      LOG(INFO) << "TRAP InitWindows()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-114.html
    case Trap::InitMenus: {
      LOG(INFO) << "TRAP InitMenus()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-69.html
    case Trap::TEInit: {
      LOG(INFO) << "TRAP TEInit()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-391.html
    case Trap::InitDialogs: {
      auto resumeProc = TRY(Pop<Ptr>());
      CHECK(resumeProc == 0) << "System 7 should always pass null (0)";
      LOG(INFO) << "TRAP InitDialogs(0x" << std::hex << resumeProc << ")";
      return absl::OkStatus();
    }
      // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-381.html
    case Trap::InitCursor: {
      LOG(INFO) << "TRAP InitCursor()";
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Unimplemented Toolbox trap: '", GetTrapName(trap), "'"));
  }
}

}  // namespace trap
}  // namespace cyder