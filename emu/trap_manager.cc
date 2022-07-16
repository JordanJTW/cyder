#include "trap_manager.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <iomanip>
#include <tuple>

#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "memory_map.h"
#include "resource.h"
#include "stack_helpers.h"
#include "system_types.h"
#include "third_party/musashi/src/m68k.h"
#include "trap_helpers.h"
#include "trap_names.h"

using rsrcloader::GetTypeName;
using rsrcloader::ResId;
using rsrcloader::ResType;

// RAII class to capture the return address on the stack while processing an
// exception, modify it, then push it back on to the stack.
class ExceptionReturn {
 public:
  ExceptionReturn()
      : status_(MUST(Pop<uint16_t>())),
        return_address_(MUST(Pop<uint32_t>())) {}

  ~ExceptionReturn() {
    CHECK(Push<uint32_t>(return_address_ + return_offset_).ok());
    CHECK(Push<uint16_t>(status_).ok());
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

TrapManager::TrapManager(MemoryManager& memory_manager,
                         rsrcloader::ResourceFile& resource_file,
                         SegmentLoader& segment_loader,
                         SDL_Renderer* renderer)
    : memory_manager_(memory_manager),
      resource_file_(resource_file),
      segment_loader_(segment_loader),
      renderer_(renderer) {
  CHECK(renderer);
}

absl::Status TrapManager::HandleALineTrap(uint16_t trap) {
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

  ExceptionReturn stack_return;

  switch (trap) {
    case Trap::LoadSeg: {
      uint16_t load_segment = TRY(Pop<uint16_t>());
      LOG(INFO) << "TRAP LoadSeg(" << load_segment << ")";
      RETURN_IF_ERROR(segment_loader_.Load(load_segment));
      // The segment loader modifies the six byte entry for this segment in the
      // table so return to the begining of the entry (4 bytes behind the 2 byte
      // trap being executed):
      stack_return.SetReturnOffset(-4);
      return absl::OkStatus();
    }
    case Trap::Get1NamedResource: {
      auto name = TRY(PopRef<absl::string_view>());
      ResType type = TRY(Pop<ResType>());

      LOG(INFO) << "TRAP Get1NamedResource(theType: '"
                << rsrcloader::GetTypeName(type) << "', name: \"" << name
                << "\")";
      return absl::UnimplementedError("");
    }
    case Trap::GetResource: {
      auto id = TRY(Pop<ResId>());
      auto type = TRY(Pop<ResType>());
      LOG(INFO) << "TRAP GetResource(theType: '" << GetTypeName(type)
                << "', theID: " << id << ")";

      auto* resource = resource_file_.FindByTypeAndId(type, id);
      // FIXME: Set ResError in D0 and call ResErrorProc
      // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-35.html#MARKER-9-220
      CHECK(resource) << "Resource not found";

      LOG(INFO) << "Attributes: "
                << static_cast<int>(resource->GetAttributes());
      auto handle = memory_manager_.AllocateHandleForRegion(
          resource->GetData(),
          absl::StrCat("Resource[", GetTypeName(type), ":", id, "]"));

      return TrapReturn<uint32_t>(handle);
    }
    case Trap::LoadResource: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP LoadResource(theResource: 0x" << std::hex << handle
                << ")";
      return absl::OkStatus();
    }
    case Trap::ReleaseResource: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP ReleaseResource(theResource: 0x" << std::hex << handle
                << ")";
      return absl::OkStatus();
    }
    case Trap::SizeRsrc: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP GetResourceSizeOnDisk(theResource: 0x" << std::hex
                << handle << ")";
      // FIXME: This should read the size from disk not memory i.e. from
      // ResourceManager
      // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-82.html
      auto handle_size = memory_manager_.GetHandleSize(handle);
      LOG(INFO) << "Handle size: " << handle_size;
      return TrapReturn<uint32_t>(handle_size);
    }
    case Trap::GetResAttrs: {
      auto handle = TRY(Pop<uint32_t>());
      LOG(INFO) << "TRAP GetResAttrs(theResource: 0x" << std::hex << handle
                << ")";
      // FIXME: Load the actual attributes from the resource...
      uint16_t attrs = 8;
      return TrapReturn<uint16_t>(attrs);
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
        RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(
            dest_ptr + i, TRY(kSystemMemory.Copy<uint8_t>(source_ptr + i))));
      }
      // Return result code "noErr"
      m68k_set_reg(M68K_REG_D0, 0);
      return absl::OkStatus();
    }
    case Trap::GetHandleSize: {
      uint32_t handle = m68k_get_reg(NULL, M68K_REG_A0);
      LOG(INFO) << "TRAP GetHandleSize(handle: 0x" << std::hex << handle << ")";
      m68k_set_reg(M68K_REG_D0, memory_manager_.GetHandleSize(handle));
      return absl::OkStatus();
    }
    case Trap::GetOSTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP GetOSTrapAddress(trap: '"
                << GetTrapNameBySystemIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, 0x3000);
      return absl::OkStatus();
    }
    case Trap::GetToolBoxTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP GetToolBoxTrapAddress(trap: '"
                << GetTrapNameByToolboxIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, 0x3000);
      return absl::OkStatus();
    }
    case Trap::GetTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP GetTrapAddress(trap: '" << GetTrapName(trap_index)
                << "')";
      m68k_set_reg(M68K_REG_A0, 0x3000);
      return absl::OkStatus();
    }
    case Trap::SetTrapAddress: {
      uint32_t trap_addr = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP SetTrapAddress(trapAddr: 0x" << std::hex << trap_addr
                << ", trap: '" << GetTrapName(trap_index) << "')";
      return absl::OkStatus();
    }
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
      auto qLink = TRY(kSystemMemory.Copy<Ptr>(ptr));
      auto qType = TRY(kSystemMemory.Copy<uint8_t>(ptr + 4));
      auto ioTrap = TRY(kSystemMemory.Copy<uint8_t>(ptr + 5));
      auto ioCmdAddr = TRY(kSystemMemory.Copy<Ptr>(ptr + 6));
      auto ioCompletion = TRY(kSystemMemory.Copy<Ptr>(ptr + 10));
      auto ioResult = TRY(kSystemMemory.Copy<int16_t>(ptr + 14));
      auto ioNamePtr = TRY(kSystemMemory.Copy<Ptr>(ptr + 16));
      auto ioVRefNum = TRY(kSystemMemory.Copy<uint8_t>(ptr + 20));
      auto ioRefNum = TRY(kSystemMemory.Copy<uint8_t>(ptr + 21));
      auto ioVersNum = TRY(kSystemMemory.Copy<char>(ptr + 22));
      auto ioPermssn = TRY(kSystemMemory.Copy<char>(ptr + 23));
      auto ioMisc = TRY(kSystemMemory.Copy<Ptr>(ptr + 24));
      auto ioBuffer = TRY(kSystemMemory.Copy<Ptr>(ptr + 28));
      auto ioReqCount = TRY(kSystemMemory.Copy<uint16_t>(ptr + 32));
      auto ioActCount = TRY(kSystemMemory.Copy<uint16_t>(ptr + 34));
      auto ioPosMode = TRY(kSystemMemory.Copy<uint8_t>(ptr + 36));
      auto ioPosOffset = TRY(kSystemMemory.Copy<uint16_t>(ptr + 37));

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
    case Trap::InitGraf: {
      auto globalPtr = TRY(Pop<Ptr>());
      LOG(INFO) << "TRAP InitGraf(globalPtr: 0x" << std::hex << globalPtr
                << ")";
      return absl::OkStatus();
    }
    case Trap::OpenPort: {
      auto thePortPtr = TRY(Pop<GrafPtr>());
      LOG(INFO) << "TRAP OpenPort(port: 0x" << std::hex << thePortPtr << ")";
      return absl::OkStatus();
    }
    case Trap::HideCursor: {
      LOG(INFO) << "TRAP HideCursor()";
      return absl::OkStatus();
    }
    case Trap::PaintRect: {
      auto rect = TRY(PopRef<Rect>());
      LOG(INFO) << "TRAP PaintRect(rect: " << rect << ")";
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      DrawRect(renderer_, rect, {0, 0, 0});
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
      auto rect = TRY(PopRef<Rect>());
      LOG(INFO) << "TRAP PaintOval(rect: " << rect << ")";
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      DrawRect(renderer_, rect, {0, 0, 0});
      return absl::OkStatus();
    }
    case Trap::EraseOval: {
      auto rect = TRY(PopRef<Rect>());
      LOG(INFO) << "TRAP EraseOval(rect: " << rect << ")";
      // FIXME: Clear with the color set for QuickDraw (A5 World?)
      DrawRect(renderer_, rect, {0xFF, 0xBF, 0x00});
      return absl::OkStatus();
    }
    case Trap::SysBeep: {
      uint16_t duration = TRY(Pop<Integer>());
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