// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/trap/trap_dispatcher.h"

#include <SDL.h>

#include <cstdint>
#include <iomanip>
#include <tuple>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/controls/control_manager.h"
#include "emu/debug/debugger.h"
#include "emu/dialog/dialog_manager.h"
#include "emu/font/font.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/pict_v1.h"
#include "emu/graphics/quickdraw.h"
#include "emu/memory/memory_helpers.h"
#include "emu/memory/memory_manager.h"
#include "emu/memory/memory_map.h"
#include "emu/rsrc/resource.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_helpers.h"
#include "gen/global_names.h"
#include "gen/trap_names.h"
#include "gen/typegen/typegen_prelude.h"
#include "third_party/abseil-cpp/absl/flags/declare.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/musashi/src/m68k.h"

ABSL_DECLARE_FLAG(/*type=*/bool, exit_on_idle);

extern bool single_step;

constexpr bool kVerboseLogTraps = false;
constexpr bool kDummyTraps = false;
constexpr bool kSupportColorQD = false;

char save_buffer[2048];
int f_offset = 0;

#define LOG_TRAP() LOG_IF(INFO, kVerboseLogTraps) << "TRAP "

#define LOG_DUMMY()                                \
  LOG_IF(WARNING, kVerboseLogTraps || kDummyTraps) \
      << COLOR(88) << "TRAP " << COLOR_RESET()

namespace cyder {
namespace trap {
namespace {

constexpr Pattern kForegroundPattern = {
    .bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

absl::Status WithPort(std::function<absl::Status(GrafPort& the_port)> cb) {
  return WithType<GrafPort>(TRY(port::GetThePort()), std::move(cb));
}

absl::Status InPort(
    std::function<absl::Status(GrafPort& the_port,
                               graphics::BitmapImage& bitmap)> cb) {
  return WithPort([&cb](GrafPort& the_port) {
    graphics::BitmapImage image = graphics::ThePortImage();
    auto clip_region = MUST(ReadHandleToType<Region>(the_port.clip_region));
    graphics::TempClipRect _(image, OffsetRect(clip_region.bounding_box,
                                               -the_port.port_bits.bounds.left,
                                               -the_port.port_bits.bounds.top));
    return cb(the_port, image);
  });
}

absl::Status WithWindow(
    std::function<absl::Status(WindowRecord& the_window)> cb) {
  return WithType<WindowRecord>(TRY(port::GetThePort()), std::move(cb));
}

void SaveScreenShotAndExit() {
  auto globals = MUST(port::GetQDGlobals());
  graphics::BitmapImage(
      globals.screen_bits,
      memory::kSystemMemory.raw_mutable_ptr() + globals.screen_bits.base_addr)
      .SaveBitmap("/tmp/screenshot.bmp");
  LOG(INFO) << "Saved screenshot to: /tmp/screenshot.bmp";
  exit(0);
}

}  // namespace

TrapDispatcherImpl::TrapDispatcherImpl(memory::MemoryManager& memory_manager,
                                       ResourceManager& resource_manager,
                                       EventManager& event_manager,
                                       MenuManager& menu_manager,
                                       WindowManager& window_manager,
                                       BitMap& screen_bits)
    : memory_manager_(memory_manager),
      resource_manager_(resource_manager),
      event_manager_(event_manager),
      menu_manager_(menu_manager),
      window_manager_(window_manager),
      screen_bits_(screen_bits) {}

absl::Status TrapDispatcherImpl::Dispatch(uint16_t trap_op) {
  if (IsToolbox(trap_op)) {
    CHECK_OK(DispatchNativeToolboxTrap(trap_op))
        << "Failed to dispatch Toolbox::" << GetTrapName(trap_op) << " (0x"
        << std::hex << trap_op << ") Index: " << std::dec
        << ExtractIndex(trap_op);
  } else {
    CHECK_OK(DispatchNativeSystemTrap(trap_op))
        << "Failed to dispatch System::" << GetTrapName(trap_op) << " (0x"
        << std::hex << trap_op << ") Index: " << std::dec
        << ExtractIndex(trap_op);
  }
  return absl::OkStatus();
}

absl::Status TrapDispatcherImpl::DispatchNativeSystemTrap(uint16_t trap) {
  CHECK(IsSystem(trap));

  switch (trap) {
    // ===================  MemoryManager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-103.html
    case Trap::BlockMoveData:
    case Trap::BlockMove: {
      uint32_t source_ptr = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t dest_ptr = m68k_get_reg(NULL, M68K_REG_A1);
      uint32_t byte_count = m68k_get_reg(NULL, M68K_REG_D0);

      LOG_TRAP() << "BlockMove(sourcePtr: 0x" << std::hex << source_ptr
                 << ", destPtr: 0x" << dest_ptr << ", byteCount: " << byte_count
                 << ")";

      // FIXME: Allow for more efficient copies in system memory (memcpy)?
      for (size_t i = 0; i < byte_count; ++i) {
        RETURN_IF_ERROR(memory::kSystemMemory.Write<uint8_t>(
            dest_ptr + i,
            TRY(memory::kSystemMemory.Read<uint8_t>(source_ptr + i))));
      }
      // Return result code "noErr"
      m68k_set_reg(M68K_REG_D0, 0);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-79.html
    case Trap::DisposePtr: {
      uint32_t ptr = m68k_get_reg(NULL, M68K_REG_A0);

      LOG_TRAP() << "DisposePtr(ptr: 0x" << std::hex << ptr << ")";

      uint32_t status = 0;
      m68k_set_reg(M68K_REG_D0, status);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-81.html
    case Trap::GetHandleSize: {
      uint32_t handle = m68k_get_reg(NULL, M68K_REG_A0);
      LOG_TRAP() << "GetHandleSize(handle: 0x" << std::hex << handle << ")";
      m68k_set_reg(M68K_REG_D0, memory_manager_.GetHandleSize(handle));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-75.html
    case Trap::NewPtr:
    // All allocated pointers are cleared (and never reallocated)
    case Trap::NewPtrClear:
    case Trap::NewPtrSysClear:
    // FIXME: Should "SYS" pointers be allocated differently?
    case Trap::NewPtrSys: {
      // D0 seems to contain the argument in a sample program...
      // but the documentation says it should be in A0.
      uint32_t logical_size = m68k_get_reg(NULL, M68K_REG_D0);
      LOG_TRAP() << "NewPtr(logicalSize: " << logical_size << ")";
      if (memory_manager_.HasSpaceForAllocation(logical_size)) {
        auto ptr = memory_manager_.Allocate(logical_size);
        m68k_set_reg(M68K_REG_A0, ptr);
        m68k_set_reg(M68K_REG_D0, 0 /* noErr */);
      } else {
        m68k_set_reg(M68K_REG_D0, -108 /* memFullErr */);
      }
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-21.html
    case Trap::NewHandle:
    case Trap::NewHandleClear: {
      // D0 seems to contain the argument in a sample program...
      // but the documentation says it should be in A0.
      uint32_t logical_size = m68k_get_reg(NULL, M68K_REG_D0);
      LOG_TRAP() << "NewHandle(logicalSize: " << logical_size << ")";
      if (memory_manager_.HasSpaceForAllocation(logical_size)) {
        auto handle = memory_manager_.AllocateHandle(logical_size, "NewHandle");
        m68k_set_reg(M68K_REG_A0, handle);
        m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      } else {
        m68k_set_reg(M68K_REG_D0, -108 /* memFullErr */);
      }
      return absl::OkStatus();
    }
    // Link: https://dev.os9.ca/techpubs/mac/Memory/Memory-97.html
    case Trap::RecoverHandle: {
      uint32_t ptr = m68k_get_reg(NULL, M68K_REG_A0);
      m68k_set_reg(M68K_REG_A0, memory_manager_.RecoverHandle(ptr));
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-31.html
    case Trap::HLock:
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-32.html
    case Trap::HUnlock:
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-33.html
    case Trap::HPurge:
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-34.html
    case Trap::HNoPurge: {
      // MemoryManager currently does not move or purge blocks so assume success
      m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-64.html
    case Trap::MaxApplZone:
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-65.html
    case Trap::MoreMasters: {
      // Memory manager currently assumes max heap already and
      // heap fragmentation is not a huge concern right now.
      m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-125.html
    case Trap::SetGrowZone:
      m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      return absl::OkStatus();

    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-19.html
    case Trap::SetApplLimit: {
      uint32_t zone_limit = m68k_get_reg(NULL, M68K_REG_A0);
      LOG_TRAP() << "SetApplLimit(zoneLimit: 0x" << std::hex << zone_limit
                 << ")";
      bool success = memory_manager_.SetApplLimit(zone_limit);
      int32_t result_code = success ? /*noErr*/ 0 : /*memFullErr*/ -108;
      m68k_set_reg(M68K_REG_D0, result_code);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-110.html
    case Trap::FreeMem: {
      LOG_TRAP() << "FreeMem()";
      m68k_set_reg(M68K_REG_D0, memory_manager_.GetFreeMemorySize());
      return absl::OkStatus();
    }

    // =====================  Event Manager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-56.html
    case Trap::FlushEvents: {
      uint32_t arguments = m68k_get_reg(NULL, M68K_REG_D0);
      uint16_t eventMask = arguments & 0xFFFF;
      uint16_t stopMask = (2 >> arguments) & 0xFFFF;

      LOG_DUMMY() << "FlushEvents(eventMask: 0x" << std::hex
                  << std::setfill('0') << std::setw(4) << eventMask
                  << ", stopMask: 0x" << std::setfill('0') << std::setw(4)
                  << stopMask << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-67.html
    case Trap::PostEvent: {
      uint16_t event_number = m68k_get_reg(NULL, M68K_REG_A0) & 0xFFFF;
      uint32_t event_message = m68k_get_reg(NULL, M68K_REG_D0);
      LOG_TRAP() << "PostEvent(eventNum: " << event_number
                 << ", eventMsg: " << event_message << ")";

      event_manager_.QueueRawEvent(event_number, event_message);

      m68k_set_reg(M68K_REG_D0, 0 /*noErr*/);
      return absl::OkStatus();
    }

    // =====================  File Manager  ========================

    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-178.html
    case Trap::GetVolInfo: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<HVolumeParamType>(ptr, [](HVolumeParamType& param) {
        LOG_DUMMY() << "GetVolInfoSync(paramBlock: " << param << ")";

        param.header.ioResult = 0 /*noErr*/;
        // This is the only information needed to make MacPaint happy :^)
        param.ioVFrBlk = 16;       // The number of unused allocation blocks
        param.ioVAlBlkSiz = 1024;  // The size of allocation blocks

        auto filename = TRY(ReadType<absl::string_view>(
            memory::kSystemMemory, param.header.ioNamePtr));

        LOG(INFO) << "GetVolInfo\n<-- ioResult: " << std::dec
                  << param.header.ioResult << "\n<-> ioNamePtr: 0x" << std::hex
                  << param.header.ioNamePtr << " [" << filename << "]"
                  << std::dec << "\n<-> ioVRefNum: " << param.header.ioVRefNum
                  << "\n--> ioVolIndex: " << param.ioVolIndex
                  << "\n<-- ioVCrDate: " << param.ioVCrDate
                  << "\n<-- ioVLsMod: " << param.ioVLsMod
                  << "\n<-- ioVAtrb: " << param.ioVAtrb
                  << "\n<-- ioVNmFls: " << param.ioVNmFls
                  << "\n<-- ioVBitMap: " << param.ioVBitMap
                  << "\n<-- ioVAllocPtr: " << param.ioVAllocPtr
                  << "\n<-- ioVNmAlBlks: " << param.ioVNmAlBlks
                  << "\n<-- ioVAlBlkSiz: " << param.ioVAlBlkSiz
                  << "\n<-- ioVClpSiz: " << param.ioVClpSiz
                  << "\n<-- ioAlBlSt: " << param.ioAlBlSt
                  << "\n<-- ioVNxtCNID: " << param.ioVNxtCNID
                  << "\n<-- ioVFrBlk: " << param.ioVFrBlk
                  << "\n<-- ioVSigWord: " << param.ioVSigWord
                  << "\n<-- ioVDrvInfo: " << param.ioVDrvInfo
                  << "\n<-- ioVDRefNum: " << param.ioVDRefNum
                  << "\n<-- ioVFSID: " << param.ioVFSID
                  << "\n<-- ioVBkUp: " << param.ioVBkUp
                  << "\n<-- ioVSeqNum: " << param.ioVSeqNum
                  << "\n<-- ioVWrCnt: " << param.ioVWrCnt
                  << "\n<-- ioVFilCnt: " << param.ioVFilCnt
                  << "\n<-- ioVDirCnt: " << param.ioVDirCnt
                  << "\n<-- ioVFndrInfo: [1..8]";

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }

    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-234.html
    case Trap::Create: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<FileParamType>(ptr, [](FileParamType& param) {
        LOG_DUMMY() << "CreateSync(paramBlock: " << param << ")";

        param.header.ioResult = 0 /*noErr*/;

        auto filename = TRY(ReadType<absl::string_view>(
            memory::kSystemMemory, param.header.ioNamePtr));

        LOG(INFO) << "Create\n--> ioCompletion: 0x" << std::hex
                  << param.header.ioCompletion << "\n<-- ioResult: " << std::dec
                  << param.header.ioResult << "\n--> ioNamePtr: 0x" << std::hex
                  << param.header.ioNamePtr << " [" << filename << "]"
                  << std::dec << "\n--> ioVRefNum: " << param.header.ioVRefNum
                  << "\n--> ioDirId: " << param.ioDirID;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-232.html
    case Trap::Open: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<IOParamType>(ptr, [](IOParamType& param) {
        LOG_DUMMY() << "OpenSync(paramBlock: " << param << ")";

        absl::string_view filename = TRY(ReadType<absl::string_view>(
            memory::kSystemMemory, param.header.ioNamePtr));

        param.ioRefNum = 0;
        param.header.ioResult = 0 /*noErr*/;

        // Documentation shows that ioDirID should also be an input parameter
        // but that is only part of the HFileParam Record and the other fields
        // more closely match the IOParam Record so we will ignore it for now.
        LOG(INFO) << "Open\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioNamePtr: 0x" << std::hex << param.header.ioNamePtr
                  << " [" << filename << "]\n--> ioVRefNum: " << std::dec
                  << param.header.ioVRefNum
                  << "\n--> ioRefNum: " << static_cast<int>(param.ioRefNum)
                  << "\n--> ioPermsission: "
                  << static_cast<int>(param.ioPermssn);

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-240.html
    case Trap::GetFileInfo: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<FileParamType>(ptr, [](FileParamType& param) {
        LOG_DUMMY() << "GetFileInfoSync(paramBlock: " << param << ")";

        auto filename = TRY(ReadType<absl::string_view>(
            memory::kSystemMemory, param.header.ioNamePtr));

        param.header.ioResult = 0 /*noErr*/;

        LOG(INFO) << "GetFileInfo\n<-- ioResult: " << param.header.ioResult
                  << "\n<-> ioNamePtr: 0x" << std::hex << param.header.ioNamePtr
                  << " [" << filename << "]\n--> ioVRefNum: " << std::dec
                  << param.header.ioVRefNum
                  << "\n<-- ioFRefNum: " << param.ioFRefNum
                  << "\n--> ioFDirIndex: " << param.ioFDirIndex
                  << "\n<-- ioFlAttrib: " << static_cast<int>(param.ioFlAttrib)
                  << "\n<-- ioFlFndrInfo: " << param.ioFlFndrInfo
                  << "\n<-> ioDirID: " << param.ioDirID
                  << "\n<-- ioFlStBlk: " << param.ioFlStBlk
                  << "\n<-- ioFlLgLen: " << param.ioFlLgLen
                  << "\n<-- ioFlPyLen: " << param.ioFlPyLen
                  << "\n<-- ioFlRStBlk: " << param.ioFlRStBlk
                  << "\n<-- ioFlRLgLen: " << param.ioFlRLgLen
                  << "\n<-- ioFlRPyLen: " << param.ioFlRPyLen
                  << "\n<-- ioFlCrDat: " << param.ioFlCrDat
                  << "\n<-- ioFlMdDat: " << param.ioFlMdDat;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-241.html
    case Trap::SetFileInfo: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);
      return WithType<FileParamType>(ptr, [](FileParamType& param) {
        LOG_DUMMY() << "SetFileInfoSync(paramBlock: " << param << ")";

        auto filename = TRY(ReadType<absl::string_view>(
            memory::kSystemMemory, param.header.ioNamePtr));

        param.header.ioResult = 0 /*noErr*/;

        LOG(INFO) << "SetFileInfo\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioNamePtr: 0x" << std::hex << param.header.ioNamePtr
                  << " [" << filename << "]\n--> ioVRefNum: " << std::dec
                  << param.header.ioVRefNum
                  << "\n--> ioFlFndrInfo: " << param.ioFlFndrInfo
                  << "\n--> ioDirID: " << param.ioDirID
                  << "\n--> ioFlCrDat: " << param.ioFlCrDat
                  << "\n--> ioFlMdDat: " << param.ioFlMdDat;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-149.html
    case Trap::SetEOF: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<IOParamType>(ptr, [](IOParamType& param) {
        LOG_DUMMY() << "SetEofSync(paramBlock: " << param << ")";

        param.header.ioResult = 0 /*noErr*/;

        LOG(INFO) << "SetEOF\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioRefNum: " << static_cast<int>(param.ioRefNum)
                  << "\n--> ioMisc: 0x" << std::hex << param.ioMisc;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-151.html
    case Trap::Allocate: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<IOParamType>(ptr, [](IOParamType& param) {
        LOG_DUMMY() << "AllocateSync(paramBlock: " << param << ")";

        param.header.ioResult = 0 /*noErr*/;
        param.ioActCount = param.ioReqCount;

        LOG(INFO) << "Allocate\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioRefNum: " << static_cast<int>(param.ioRefNum)
                  << "\n--> ioReqCount: " << std::dec << param.ioReqCount
                  << "\n<-- ioActCount: " << param.ioActCount;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-141.html
    case Trap::Read: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<IOParamType>(ptr, [](IOParamType& param) {
        LOG_DUMMY() << "ReadSync(paramBlock: " << param << ")";

        LOG(INFO) << "Read\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioRefNum: " << static_cast<int>(param.ioRefNum)
                  << "\n--> ioBuffer: 0x" << std::hex << param.ioBuffer
                  << "\n--> ioReqCount: " << std::dec << param.ioReqCount
                  << "\n<-- ioActCount: " << param.ioActCount
                  << "\n--> ioPosMode: " << static_cast<int>(param.ioPosMode)
                  << "\n<-> ioPosOffset: " << param.ioPosOffset;

        for (size_t i = 0; i < param.ioReqCount; ++i) {
          RETURN_IF_ERROR(memory::kSystemMemory.Write<uint8_t>(
              param.ioBuffer + i, save_buffer[i + f_offset]));
        }
        f_offset += param.ioReqCount;

        param.ioActCount = param.ioReqCount;
        param.header.ioResult = 0 /*noErr*/;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-142.html
    case Trap::Write: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<IOParamType>(ptr, [](IOParamType& param) {
        LOG_DUMMY() << "WriteSync(paramBlock: " << param << ")";

        param.ioActCount = param.ioReqCount;
        param.header.ioResult = 0 /*noErr*/;

        LOG(INFO) << "Write\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioRefNum: " << static_cast<int>(param.ioRefNum)
                  << "\n--> ioBuffer: 0x" << std::hex << param.ioBuffer
                  << "\n--> ioReqCount: " << std::dec << param.ioReqCount
                  << "\n<-- ioActCount: " << param.ioActCount
                  << "\n--> ioPosMode: " << static_cast<int>(param.ioPosMode)
                  << "\n<-> ioPosOffset: " << param.ioPosOffset;

        for (size_t i = 0; i < param.ioReqCount; ++i) {
          save_buffer[i + f_offset] =
              TRY(memory::kSystemMemory.Read<uint8_t>(param.ioBuffer + i));
        }
        f_offset += param.ioReqCount;

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-141.html
    case Trap::Close: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);

      return WithType<IOParamType>(ptr, [](IOParamType& param) {
        LOG_DUMMY() << "CloseSync(paramBlock: " << param << ")";

        param.header.ioResult = 0 /*noErr*/;

        LOG(INFO) << "Close\n<-- ioResult: " << param.header.ioResult
                  << "\n--> ioRefNum: " << static_cast<int>(param.ioRefNum);

        m68k_set_reg(M68K_REG_D0, param.header.ioResult);
        return absl::OkStatus();
      });
    }

    // =====================  OS Utilities  =======================

    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-106.html
    case Trap::ReadDateTime: {
      Ptr time_var = m68k_get_reg(NULL, M68K_REG_A0);
      LOG_TRAP() << "ReadDateTime(VAR time: 0x" << std::hex << time_var << ")";
      // Time is set with the current time before each emulation time slice.
      RETURN_IF_ERROR(memory::kSystemMemory.Write<uint32_t>(
          time_var,
          TRY(memory::kSystemMemory.Read<uint32_t>(GlobalVars::Time))));
      m68k_set_reg(M68K_REG_D0, 0 /*noErr*/);
      return absl::OkStatus();
    }

    // ======================== Gestalt Manager ========================

    // Link: https://dev.os9.ca/techpubs/mac/OSUtilities/OSUtilities-19.html
    case Trap::SysEnvirons: {
      Integer version_requested = m68k_get_reg(NULL, M68K_REG_D0);
      Ptr var_the_world = m68k_get_reg(NULL, M68K_REG_A0);
      LOG_TRAP() << "SysEnvirons(versionRequested: " << version_requested
                 << ", VAR theWorld: 0x" << std::hex << var_the_world << ")";

      return WithType<SysEnvRecord>(var_the_world, [](SysEnvRecord& record) {
        record.hasColorQD = kSupportColorQD ? 0x01 : 0x00;
        m68k_set_reg(M68K_REG_D0, 0 /*noErr*/);
        return absl::OkStatus();
      });
    }

    default:
      return absl::UnimplementedError(
          absl::StrCat("Unimplemented system trap: '", GetTrapName(trap), "'"));
  }
}

absl::Status TrapDispatcherImpl::DispatchNativeToolboxTrap(uint16_t trap) {
  CHECK(IsToolbox(trap));

  switch (trap) {
    // =================  Event Manager  ==================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-73.html
    case Trap::Button: {
      LOG_TRAP() << "Button()";
      return TrapReturn<bool>(event_manager_.HasMouseEvent(kMouseDown));
    }
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-74.html
    case Trap::StillDown: {
      LOG_TRAP() << "StillDown()";
      return TrapReturn<bool>(!event_manager_.HasMouseEvent(kMouseUp));
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-72.html
    case Trap::GetMouse: {
      auto mouse_location_var = Pop<Ptr>();

      LOG_TRAP() << "GetMouse(VAR mouseLoc: 0x" << std::hex
                 << mouse_location_var << ")";

      int mouse_x = 0, mouse_y = 0;
      SDL_GetMouseState(&mouse_x, &mouse_y);

      // FIXME: Ensure it is safe to do these implicit bit-narrowing casts
      Point mouse_location;
      mouse_location.x = mouse_x;
      mouse_location.y = mouse_y;

      return WithPort([&](GrafPort& the_port) {
        mouse_location = port::GlobalToLocal(the_port, mouse_location);
        return WriteType<Point>(mouse_location, memory::kSystemMemory,
                                mouse_location_var);
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-51.html
    case Trap::WaitNextEvent: {
      auto mouse_region = Pop<Handle>();
      auto sleep = Pop<uint32_t>();
      auto the_event_var = Pop<Ptr>();
      auto event_mask = Pop<uint16_t>();

      LOG_TRAP() << "WaitNextEvent(eventMask: " << std::bitset<16>(event_mask)
                 << ", VAR theEvent: 0x" << std::hex << the_event_var
                 << ", sleep: " << std::dec << sleep << ", mouseRgn: 0x"
                 << std::hex << mouse_region << ")";

      if (absl::GetFlag(FLAGS_exit_on_idle) &&
          !event_manager_.has_window_events()) {
        SaveScreenShotAndExit();
      }

      EventRecord event = event_manager_.WaitNextEvent(event_mask, sleep);
      Debugger::Instance().OnEvent(event.what);

      RETURN_IF_ERROR(WriteType<EventRecord>(
          std::move(event), memory::kSystemMemory, the_event_var));

      if (event.what == kWindowUpdate) {
        RETURN_IF_ERROR(
            WithType<WindowRecord>(event.message, [&](WindowRecord& window) {
              Ptr wm_port_ptr =
                  TRY(memory::kSystemMemory.Read<Handle>(GlobalVars::WMgrPort));
              graphics::BitmapImage image = graphics::PortImageFor(wm_port_ptr);
              DrawWindowFrame(window, image);
              return absl::OkStatus();
            }));
      }

      return TrapReturn<bool>(event.what != 0);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-53.html
    case Trap::GetNextEvent: {
      auto the_event_var = Pop<Ptr>();
      auto event_mask = Pop<uint16_t>();

      LOG_TRAP() << "GetNextEvent(eventMask: " << std::bitset<16>(event_mask)
                 << ", VAR theEvent: 0x" << std::hex << the_event_var << ")";

      if (absl::GetFlag(FLAGS_exit_on_idle) &&
          !event_manager_.has_window_events()) {
        SaveScreenShotAndExit();
      }

      auto event = event_manager_.GetNextEvent(event_mask);

      Debugger::Instance().OnEvent(event.what);

      RETURN_IF_ERROR(WriteType<EventRecord>(
          std::move(event), memory::kSystemMemory, the_event_var));

      if (event.what == kWindowUpdate) {
        RETURN_IF_ERROR(
            WithType<WindowRecord>(event.message, [&](WindowRecord& window) {
              Ptr wm_port_ptr =
                  TRY(memory::kSystemMemory.Read<Handle>(GlobalVars::WMgrPort));
              graphics::BitmapImage image = graphics::PortImageFor(wm_port_ptr);
              DrawWindowFrame(window, image);
              return absl::OkStatus();
            }));
      }

      return TrapReturn<bool>(event.what != 0);
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-77.html
    case Trap::GetKeys: {
      Ptr var_the_keys = Pop<Ptr>();
      LOG_DUMMY() << "GetKeys(VAR theKeys: 0x" << std::hex << var_the_keys
                  << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-80.html
    case Trap::TickCount: {
      return TrapReturn<uint32_t>(event_manager_.NowTicks());
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-58.html
    case Trap::SystemTask: {
      return absl::OkStatus();
    }

    // ===================  Menu Manager  ======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-124.html
    case Trap::GetNewMBar: {
      auto menu_bar_id = Pop<Integer>();
      LOG_TRAP() << "GetNewMBar(menuBarID: " << menu_bar_id << ")";
      Handle handle = resource_manager_.GetResource('MBAR', menu_bar_id);
      // TODO: This should return a handle to a MenuList not the resource.
      return TrapReturn<Handle>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-118.html
    // NOTE: It appears that _GetRMenu was renamed to _GetMenu at some point
    case Trap::GetRMenu: {
      auto menu_id = Pop<Integer>();
      LOG_TRAP() << "GetRMenu(menuID: " << menu_id << ")";

      Handle handle = resource_manager_.GetResource('MENU', menu_id);
      // TODO: This should return a handle to a MenuRecord not the resource.
      return TrapReturn<Handle>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-120.html
    case Trap::InsertMenu: {
      auto before_id = Pop<uint16_t>();
      auto the_menu = Pop<Handle>();
      LOG_TRAP() << "InsertMenu(beforeId: " << before_id << ", theMenu: 0x"
                 << std::hex << the_menu << ")";

      core::MemoryReader reader(memory_manager_.GetRegionForHandle(the_menu));

      auto menu = TRY(reader.NextType<MenuResource>());
      // The MenuResource ends with a null-terminated list of MenuItems
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-183.html
      std::vector<MenuItemResource> menu_items;
      while (reader.HasNext() && TRY(reader.Peek<uint8_t>()) != 0) {
        menu_items.push_back(TRY(reader.NextType<MenuItemResource>()));
      }

      menu_manager_.InsertMenu(std::move(menu), std::move(menu_items));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-127.html
    case Trap::SetMenuBar: {
      auto menu_list_handle = Pop<Handle>();
      LOG_TRAP() << "SetMenuBar(menuList: 0x" << menu_list_handle << ")";

      core::MemoryReader menu_bar_reader(
          memory_manager_.GetRegionForHandle(menu_list_handle));

      // MenuBarResource: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-184.html
      auto menu_count = TRY(menu_bar_reader.Next<uint16_t>());
      // FIXME: The MENUs should not need to be loaded _again_ (see GetNewMBar)
      for (int i = 0; i < menu_count; ++i) {
        auto id = TRY(menu_bar_reader.Next<uint16_t>());
        Handle menu_handle = resource_manager_.GetResource('MENU', id);

        core::MemoryReader reader(
            memory_manager_.GetRegionForHandle(menu_handle));

        auto menu = TRY(reader.NextType<MenuResource>());
        // The MenuResource ends with a null-terminated list of MenuItems
        // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-183.html
        std::vector<MenuItemResource> menu_items;
        while (reader.HasNext() && TRY(reader.Peek<uint8_t>()) != 0) {
          menu_items.push_back(TRY(reader.NextType<MenuItemResource>()));
        }

        menu_manager_.InsertMenu(std::move(menu), std::move(menu_items));
      }
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-147.html
    case Trap::AppendResMenu: {
      auto the_type = Pop<ResType>();
      auto the_menu = Pop<Handle>();
      LOG_DUMMY() << "AppendResMenu(theMenu: 0x" << std::hex << the_menu
                  << ", theType: " << OSTypeName(the_type) << ")";

      core::MemoryReader reader(memory_manager_.GetRegionForHandle(the_menu));

      auto menu = TRY(reader.NextType<MenuResource>());
      // The MenuResource ends with a null-terminated list of MenuItems
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-183.html
      std::vector<MenuItemResource> menu_items;
      size_t size = menu.size();
      while (reader.HasNext() && TRY(reader.Peek<uint8_t>()) != 0) {
        auto menu_item = TRY(reader.NextType<MenuItemResource>());
        menu_items.push_back(menu_item);
        size += menu_item.size();
      }

      std::vector<std::pair<ResId, std::string>> ids_and_names =
          resource_manager_.GetIdsForType(the_type);

      if (ids_and_names.empty())
        return absl::OkStatus();

      for (auto& [id, name] : ids_and_names) {
        if (name.empty())
          continue;

        MenuItemResource item;
        item.title = name;
        menu_items.push_back(item);
        size += item.size();
      }

      Ptr new_location = memory_manager_.Allocate(size);
      RETURN_IF_ERROR(
          WriteType<MenuResource>(menu, memory::kSystemMemory, new_location));
      size_t offset = menu.size();
      for (MenuItemResource& item : menu_items) {
        RETURN_IF_ERROR(
            WriteType(item, memory::kSystemMemory, new_location + offset));
        offset += item.size();
      }

      memory_manager_.UpdateHandle(the_menu, new_location, offset);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-130.html
    case Trap::DrawMenuBar: {
      LOG_TRAP() << "DrawMenuBar()";
      menu_manager_.DrawMenuBar();
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-152.html
    case Trap::GetMenuItemText: {
      auto item_string_var = Pop<Ptr>();
      auto item = Pop<uint16_t>();
      auto the_menu = Pop<Handle>();

      CHECK_GT(item, 0) << "Menu item is not expected to be 0-indexed";

      LOG_TRAP() << "GetMenuItemText(theMenu: 0x" << std::hex << the_menu
                 << ", item: " << std::dec << item << ", VAR itemString: 0x"
                 << std::hex << item_string_var << ")";

      core::MemoryReader reader(memory_manager_.GetRegionForHandle(the_menu));

      auto menu = TRY(reader.NextType<MenuResource>());
      // The MenuResource ends with a null-terminated list of MenuItems
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-183.html
      uint16_t item_index = 0;
      while (reader.HasNext() && TRY(reader.Peek<uint8_t>()) != 0) {
        auto menu_item = TRY(reader.NextType<MenuItemResource>());

        if (++item_index == item) {
          RETURN_IF_ERROR(WriteType<absl::string_view>(
              menu_item.title, memory::kSystemMemory, item_string_var));
          return absl::OkStatus();
        }
      }

      // The documentation does not describe what should happen
      NOTREACHED() << "GetMenuItemText received an invalid index!";
      return absl::OkStatus();
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-166.html
    case Trap::CountMItems: {
      auto the_menu = Pop<Handle>();
      LOG_DUMMY() << "CountMItems(theMenu: 0x" << std::hex << the_menu << ")";
      core::MemoryReader reader(memory_manager_.GetRegionForHandle(the_menu));
      auto menu = TRY(reader.NextType<MenuResource>());

      uint16_t count = 0;
      while (reader.HasNext() && TRY(reader.Peek<uint8_t>()) != 0) {
        MUST(reader.NextType<MenuItemResource>());
        ++count;
      }
      return TrapReturn<uint16_t>(count);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-133.html
    case Trap::MenuSelect: {
      auto start_pt = PopType<Point>();

      LOG_TRAP() << "MenuSelect(startPt: " << start_pt << ")";

      uint32_t selected = menu_manager_.MenuSelect(start_pt);
      return TrapReturn<uint32_t>(selected);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-136.html
    case Trap::HiliteMenu: {
      auto menu_id = Pop<Integer>();
      LOG_DUMMY() << "HiliteMenu(menuId: " << menu_id << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-150.html
    case Trap::EnableItem: {
      auto item = Pop<int16_t>();
      auto the_menu_handle = Pop<Handle>();
      LOG_DUMMY() << "EnableItem(theMenu: 0x" << std::hex << the_menu_handle
                  << ", item: " << std::dec << item << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-151.html
    case Trap::DisableItem: {
      auto item = Pop<int16_t>();
      auto the_menu_handle = Pop<Handle>();
      LOG_DUMMY() << "DisableItem(theMenu: 0x" << std::hex << the_menu_handle
                  << ", item: " << std::dec << item << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-158.html
    case Trap::CheckItem: {
      auto checked = Pop<bool>();
      auto item = Pop<int16_t>();
      auto the_menu_handle = Pop<Handle>();
      LOG_DUMMY() << "CheckItem(theMenu: 0x" << std::hex << the_menu_handle
                  << std::dec << ", item: " << item
                  << ", checked: " << (checked ? "True" : "False");
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-141.html
    case Trap::GetMenuHandle: {
      auto menu_id = Pop<uint16_t>();
      LOG_TRAP() << "GetMenuHandle(menuID: " << menu_id << ")";
      // FIXME: This should return the application's menu and not the resource
      Handle handle = resource_manager_.GetResource('MENU', menu_id);
      auto menu = TRY(memory_manager_.ReadTypeFromHandle<MenuResource>(handle));
      return TrapReturn<Handle>(handle);
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-139.html
    case Trap::SysEdit: {
      auto edit_cmd = Pop<Integer>();
      LOG_TRAP() << "SystemEdit(editCmd: " << edit_cmd << ")";
      // Always return false since Cyder does not support desk accessories :^)
      return TrapReturn<bool>(false);
    }

    // =================  Process Manager  ====================

    // Link: http://0.0.0.0:8000/docs/mac/Processes/Processes-51.html
    case Trap::ExitToShell: {
      LOG_TRAP() << "ExitToShell()";
      LOG(INFO) << "Have a nice day! 🐙";
      exit(0);
      return absl::OkStatus();
    }

    // ====================  QuickDraw  ======================

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-36.html
    case Trap::GetPort: {
      auto port_var = Pop<Ptr>();
      LOG_TRAP() << "GetPort(VAR port: 0x" << std::hex << port_var << ")";

      RETURN_IF_ERROR(
          memory::kSystemMemory.Write<Ptr>(port_var, TRY(port::GetThePort())));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-37.html
    case Trap::SetPort: {
      auto port = Pop<Ptr>();
      LOG_TRAP() << "SetPort(port: 0x" << std::hex << port << ")";

      RETURN_IF_ERROR(port::SetThePort(port));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-32.html
    case Trap::OpenPort: {
      auto the_port = Pop<GrafPtr>();
      LOG_TRAP() << "OpenPort(port: 0x" << std::hex << the_port << ")";
      RETURN_IF_ERROR(port::SetThePort(the_port));
      return WithType<GrafPort>(the_port, [](GrafPort& port) {
        port::InitPort(port);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-47.html
    case Trap::SetPortBits: {
      auto bitmap = PopRef<BitMap>();
      LOG_TRAP() << "SetPortBits(bitmap: " << bitmap << ")";
      return WithPort([&](GrafPort& port) {
        port.port_bits = std::move(bitmap);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-392.html
    case Trap::HideCursor: {
      LOG_DUMMY() << "HideCursor()";
      return absl::OkStatus();
    }
    // Link: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-40.html
    case Trap::SetOrigin: {
      Point origin = PopType<Point>();
      LOG_TRAP() << "SetOrigin(h,v: " << origin << ")";
      return WithPort([&](GrafPort& port) {
        // Normalize the portRect and portBits.bounds as if the origin was (0,0)
        Rect normalizedPortRect = NormalizeRect(port.port_rect);
        Rect normalizedBounds = OffsetRect(
            port.port_bits.bounds, -port.port_rect.left, -port.port_rect.top);

        // Recalculate both with the new origin
        port.port_rect = OffsetRect(normalizedPortRect, origin.x, origin.y);
        port.port_bits.bounds =
            OffsetRect(normalizedBounds, origin.x, origin.y);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-98.html
    case Trap::PaintRect: {
      auto rect = PopRef<Rect>();
      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillRect(port::LocalToGlobal(port, rect), port.pen_pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-99.html
    case Trap::FillRect: {
      auto pattern = PopRef<Pattern>();
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "FillRect(rect: " << rect << ", pat: " << pattern << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillRect(port::LocalToGlobal(port, rect), pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link:https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-111.html
    case Trap::FillOval: {
      auto pattern = PopRef<Pattern>();
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "FillOval(rect: " << rect << ", pat: " << pattern << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillEllipse(port::LocalToGlobal(port, rect), pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-107.html
    case Trap::InverRoundRect: {
      auto oval_height = Pop<Integer>();
      auto oval_width = Pop<Integer>();
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "InverRoundRect(rect: " << rect
                 << ", ovalWidth: " << oval_width
                 << ", ovalHeight: " << oval_height << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        // FIXME: Use a proper rounded rectangle here.
        image.FillRect(port::LocalToGlobal(port, rect), graphics::kBlackPattern,
                       graphics::BitmapImage::FillMode::NotXOr);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-97.html
    case Trap::FrameRect: {
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "FrameRect(rect: " << rect << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        // FIXME: Respect the pen size when framing rects
        image.FrameRect(port::LocalToGlobal(port, rect), port.pen_pattern.bytes,
                        ConvertMode(port.pattern_mode));
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-100.html
    case Trap::EraseRect: {
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "EraseRect(rect: " << rect << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillRect(port::LocalToGlobal(port, rect),
                       port.back_pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-103.html
    case Trap::FrameRoundRect: {
      auto oval_height = Pop<uint16_t>();
      auto oval_width = Pop<uint16_t>();
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "FrameRoundRect(rect: " << rect
                 << ", ovalWidth: " << oval_width
                 << ", ovalHeight: " << oval_height << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        if (port.region_save) {
          // TODO: Implement proper Region support!
          return WithType<Region>(port.region_save, [&](Region& region) {
            region.region_size = 10;
            region.bounding_box = rect;
            return absl::OkStatus();
          });
        }

        // TODO: Respect the pen size when framing rects
        // TODO: Implement support for rounded rects i.e. squircles
        image.FrameRect(port::LocalToGlobal(port, rect), port.pen_pattern.bytes,
                        ConvertMode(port.pattern_mode));
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-110.html
    case Trap::PaintOval: {
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "PaintOval(rect: " << rect << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillEllipse(port::LocalToGlobal(port, rect),
                          port.fill_pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-112.html
    case Trap::EraseOval: {
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "EraseOval(rect: " << rect << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillEllipse(port::LocalToGlobal(port, rect),
                          port.back_pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-86.html
    case Trap::SetRect: {
      auto bottom = Pop<uint16_t>();
      auto right = Pop<uint16_t>();
      auto top = Pop<uint16_t>();
      auto left = Pop<uint16_t>();
      auto rect_ptr = Pop<Ptr>();
      LOG_TRAP() << "SetRect(r: 0x" << std::hex << rect_ptr << std::dec
                 << ", top: " << top << ", left: " << left
                 << ", bottom: " << bottom << ", right: " << right << ")";
      struct Rect rect;
      rect.left = left;
      rect.top = top;
      rect.right = right;
      rect.bottom = bottom;
      RETURN_IF_ERROR(WriteType<Rect>(rect, memory::kSystemMemory, rect_ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-51.html
    case Trap::AddPt: {
      auto dst_pt_var = Pop<Ptr>();
      auto src_pt = PopType<Point>();

      return WithType<Point>(dst_pt_var, [&](Point& dst_pt) {
        LOG_TRAP() << "AddPt(srcPt: " << src_pt << ", VAR dstPt: " << dst_pt
                   << " @ 0x" << std::hex << dst_pt_var << ")";
        dst_pt.x += src_pt.x;
        dst_pt.y += src_pt.y;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-54.html
    case Trap::SetPt: {
      auto v = Pop<uint16_t>();
      auto h = Pop<uint16_t>();
      auto pt_var = Pop<Ptr>();
      LOG_TRAP() << "SetPt(VAR pt: 0x" << std::hex << pt_var
                 << ", h: " << std::dec << h << ", v: " << v << ")";

      return WithType<Point>(pt_var, [&](Point& pt) {
        pt.x = h;
        pt.y = v;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-88.html
    case Trap::InsetRect: {
      auto dv = Pop<int16_t>();
      auto dh = Pop<int16_t>();
      auto rect_var = Pop<Ptr>();

      return WithType<Rect>(rect_var, [&](Rect& rect) {
        LOG_TRAP() << "InsetRect(VAR r: " << rect << " @ 0x" << std::hex
                   << rect_var << std::dec << ", dh: " << dh << ", dv: " << dv
                   << ")";
        rect = InsetRect(rect, dh, dv);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-87.html
    case Trap::OffsetRect: {
      auto dv = Pop<int16_t>();
      auto dh = Pop<int16_t>();
      auto rect_var = Pop<Ptr>();

      return WithType<Rect>(rect_var, [&](Rect& rect) {
        LOG_TRAP() << "OffsetRect(r: " << rect << " @ 0x" << std::hex
                   << rect_var << std::dec << ", dh: " << dh << ", dv: " << dv
                   << ")";
        rect = OffsetRect(rect, dh, dv);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-93.html
    case Trap::PtToAngle: {
      auto angle_var = Pop<Ptr>();
      auto pt = PopType<Point>();
      auto rect = PopRef<Rect>();

      LOG_TRAP() << "PtToAngle(rect: " << rect << ", pt: " << pt
                 << ", VAR angle: 0x" << std::hex << angle_var << ")";

      auto offset = TRY(port::GetLocalToGlobalOffset());

      int16_t width = rect.right - rect.left;
      int16_t height = rect.bottom - rect.top;
      int16_t center_x = rect.left + (width / 2) + offset.x;
      int16_t center_y = rect.top + (height / 2) + offset.y;

      double result =
          std::atan2(pt.y - center_y, pt.x - center_x) * 180 / 3.14159265;

      int16_t angle = (360 + result);
      angle = (angle + 90) % 360;

      RETURN_IF_ERROR(memory::kSystemMemory.Write<Integer>(angle_var, angle));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-49.html
    case Trap::GlobalToLocal: {
      auto pt_var = Pop<Ptr>();
      auto pt = TRY(ReadType<Point>(memory::kSystemMemory, pt_var));

      LOG_TRAP() << "GlobalToLocal(VAR pt: " << pt << " @ 0x" << std::hex
                 << pt_var << ")";

      auto offset = TRY(port::GetLocalToGlobalOffset());
      pt.x -= offset.x;
      pt.y -= offset.y;

      RETURN_IF_ERROR(WriteType<Point>(pt, memory::kSystemMemory, pt_var));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-50.html
    case Trap::LocalToGlobal: {
      auto pt_var = Pop<Ptr>();
      auto pt = TRY(ReadType<Point>(memory::kSystemMemory, pt_var));

      LOG_TRAP() << "LocalToGlobal(VAR pt: " << pt << " @ 0x" << std::hex
                 << pt_var << ")";

      auto offset = TRY(port::GetLocalToGlobalOffset());
      pt.x += offset.x;
      pt.y += offset.y;

      RETURN_IF_ERROR(WriteType<Point>(pt, memory::kSystemMemory, pt_var));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-81.html
    case Trap::MoveTo: {
      auto v = Pop<Integer>();
      auto h = Pop<Integer>();
      LOG_TRAP() << "MoveTo(h: " << h << ", v: " << v << ")";

      return WithPort([h, v](GrafPort& port) {
        port.pen_location.x = h;
        port.pen_location.y = v;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-82.html
    case Trap::Move: {
      auto dv = Pop<Integer>();
      auto dh = Pop<Integer>();
      LOG_TRAP() << "Move(dh: " << dh << ", dv: " << dv << ")";

      return WithPort([dh, dv](GrafPort& port) {
        port.pen_location.x += dh;
        port.pen_location.y += dv;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-74.html
    case Trap::PenSize: {
      auto height = Pop<Integer>();
      auto width = Pop<Integer>();
      LOG_TRAP() << "PenSize(width: " << width << ", height: " << height << ")";

      return WithPort([width, height](GrafPort& port) {
        port.pen_size.x = width;
        port.pen_size.y = height;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-101.html
    case Trap::InverRect: {
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "InvertRect(r: " << rect << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillRect(port::LocalToGlobal(port, rect),
                       kForegroundPattern.bytes,
                       graphics::BitmapImage::FillMode::XOr);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-89.html
    case Trap::SectRect: {
      auto dst_rect_var = Pop<Ptr>();
      auto src2 = PopRef<Rect>();
      auto src1 = PopRef<Rect>();

      LOG_TRAP() << "SectRect(src1: " << src1 << ", src2: " << src2
                 << ", VAR dstRect: 0x" << dst_rect_var << ")";

      auto rect = IntersectRect(src1, src2);
      RETURN_IF_ERROR(
          WriteType<Rect>(rect, memory::kSystemMemory, dst_rect_var));
      return TrapReturn<bool>(IsZeroRect(rect));
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-94.html
    case Trap::EqualRect: {
      auto rect2 = PopRef<Rect>();
      auto rect1 = PopRef<Rect>();
      LOG_TRAP() << "EqualRect(rect1: " << rect1 << ", rect2: " << rect2 << ")";
      return TrapReturn<bool>(EqualRect(rect1, rect2));
    }
    // Link: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-55.html
    case Trap::EqualPt: {
      auto pt2 = PopType<Point>();
      auto pt1 = PopType<Point>();
      LOG_TRAP() << "EqualPt(p1:" << pt1 << ", pt2: " << pt2 << ")";
      return TrapReturn<bool>(pt1.x == pt2.x && pt1.y == pt2.y);
    }
    // Link: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-92.html
    case Trap::Pt2Rect: {
      auto var_dest_rect = PopVar<Rect>();
      auto pt2 = PopType<Point>();
      auto pt1 = PopType<Point>();

      LOG_TRAP() << "Pt2Rect(pt1: " << pt1 << ", pt2: " << pt2
                 << ", VAR dstRect: " << var_dest_rect << ")";

#define MAX(a, b) a > b ? a : b
#define MIN(a, b) a < b ? a : b

      return WithType<Rect>(var_dest_rect.ptr, [&](Rect& rect) {
        rect = Rect{.top = MIN(pt1.y, pt2.y),
                    .left = MIN(pt1.x, pt2.x),
                    .bottom = MAX(pt1.y, pt2.y),
                    .right = MAX(pt1.x, pt2.x)};
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-384.html
    case Trap::GetCursor: {
      auto cursor_id = Pop<uint16_t>();
      LOG_TRAP() << "GetCursor(cursorID: " << cursor_id << ")";
      static auto kEmptyCursorHandle =
          TRY(memory_manager_.NewHandleFor<Cursor>({}, "EmptyCursor"));
      return TrapReturn<Handle>(kEmptyCursorHandle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-385.html
    case Trap::SetCursor: {
      auto crsr = PopRef<Cursor>();
      LOG_DUMMY() << "SetCursor(crsr: " << crsr << ")";
      // TODO: Allow alternative cursors to be displayed with SDL2
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-262.html
    case Trap::InvalRect: {
      auto bad_rect = PopRef<Rect>();
      LOG_TRAP() << "InvalRect(badRect: " << bad_rect << ")";
      // FIXME: Implement this once "update regions" are supported
      RETURN_IF_ERROR(WithWindow([bad_rect](WindowRecord& the_window) {
        return WithHandleToType<Region>(the_window.update_region,
                                        [&](Region& region) {
                                          region.bounding_box = bad_rect;
                                          return absl::OkStatus();
                                        });
      }));
      event_manager_.QueueWindowUpdate(TRY(port::GetThePort()));
      return absl::OkStatus();
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-264.html
    case Trap::ValidRect: {
      auto goodRect = PopRef<Rect>();
      LOG_DUMMY() << "ValidRect(goodRect: " << goodRect << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-43.html
    case Trap::GetClip: {
      auto rgn = Pop<Handle>();
      LOG_DUMMY() << "GetClip(rgn: 0x" << std::hex << rgn << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-44.html
    case Trap::SetClip: {
      auto rgn = Pop<Handle>();
      LOG_DUMMY() << "SetClip(rgn: 0x" << std::hex << rgn << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-45.html
    case Trap::ClipRect: {
      auto r = PopRef<Rect>();
      LOG_DUMMY() << "ClipRect(r: " << r << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-132.html
    case Trap::NewRgn: {
      LOG_DUMMY() << "NewRgn()";
      return TrapReturn<Handle>(
          TRY(memory_manager_.NewHandleFor<Region>({}, "NewRgn")));
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-135.html
    case Trap::DisposeRgn: {
      auto rgn = Pop<Handle>();
      LOG_TRAP() << "DisposeRgn(rgn: 0x" << std::hex << rgn << ")";
      // TODO: Implement this once Memory Manager supports freeing memory
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-71.html
    case Trap::GetPen: {
      auto pt_var = Pop<Ptr>();
      LOG_TRAP() << "GetPen(VAR pt: 0x" << std::hex << pt_var << ")";

      return WithPort([&](const GrafPort& port) {
        return WithType<Point>(pt_var, [&](Point& pt) {
          pt.x = port.pen_location.x;
          pt.y = port.pen_location.y;
          return absl::OkStatus();
        });
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-75.html
    case Trap::PenMode: {
      auto mode = Pop<Integer>();
      LOG_TRAP() << "PenMode(mode: " << mode << ")";
      return WithPort([&](GrafPort& port) {
        port.pattern_mode = mode;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-76.html
    case Trap::PenPat: {
      auto pat = PopRef<Pattern>();
      LOG_TRAP() << "PenPat(pat: " << pat << ")";
      return WithPort([&](GrafPort& port) {
        port.pen_pattern = pat;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-77.html
    case Trap::PenNormal: {
      LOG_TRAP() << "PenNormal()";
      return WithPort([&](GrafPort& port) {
        static Point kDefaultSize = {.y = 1, .x = 1};
        port.pen_size = kDefaultSize;
        static Pattern kDefaultPattern = kForegroundPattern;
        port.pen_pattern = kDefaultPattern;
        // PenMode constants are documented here:
        // http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-75.html#HEADING75-0
        port.pattern_mode = 8 /*patCopy*/;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-83.html
    case Trap::LineTo: {
      auto v = Pop<int16_t>();
      auto h = Pop<int16_t>();
      LOG_TRAP() << "LineTo(h: " << h << ", v: " << v << ")";
      // TODO: Support drawing arbitrarily sloped lines...
      // CHECK(dv == 0) << "Only horizontal lines are supported currently";
      return InPort([&](GrafPort& port, graphics::BitmapImage& image) {
        image.FillRow(port.pen_location.y - port.port_bits.bounds.top,
                      port.pen_location.x - port.port_bits.bounds.left,
                      h - port.port_bits.bounds.left,
                      port.pen_pattern.bytes[0]);
        port.pen_location.x = h;
        port.pen_location.y = v;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-84.html
    case Trap::Line: {
      auto dv = Pop<int16_t>();
      auto dh = Pop<int16_t>();
      LOG_TRAP() << "Line(dh: " << dh << ", dv: " << dv << ")";
      // TODO: Support drawing arbitrarily sloped lines...
      // CHECK(dv == 0) << "Only horizontal lines are supported currently";
      return InPort([&](GrafPort& port, graphics::BitmapImage& image) {
        image.FillRow(port.pen_location.y - port.port_bits.bounds.top,
                      port.pen_location.x - port.port_bits.bounds.left,
                      port.pen_location.x - port.port_bits.bounds.left + dh,
                      port.pen_pattern.bytes[0]);
        port.pen_location.x += dh;
        port.pen_location.y += dv;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-91.html
    case Trap::PtInRect: {
      auto r = PopRef<Rect>();
      auto pt = PopType<Point>();
      LOG_TRAP() << "PtInRect(pt: " << pt << ", r: " << r << ")";
      return TrapReturn<bool>(PointInRect(pt, r));
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-146.html
    case Trap::PtInRgn: {
      auto rgn_handle = Pop<Handle>();
      auto pt = PopType<Point>();
      LOG_TRAP() << "PtInRgn(pt: " << pt << ", rgn: 0x" << std::hex
                 << rgn_handle << ")";

      return WithHandleToType<Region>(rgn_handle, [&](const Region& region) {
        CHECK_EQ(region.region_size, 10u) << "Only rect regions are supported!";
        return TrapReturn<bool>(PointInRect(pt, region.bounding_box));
      });
    }
      // Link: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-147.html
    case Trap::RectInRgn: {
      auto rgn_handle = Pop<Handle>();
      auto rect = PopRef<Rect>();
      LOG_TRAP() << "RectInRgn(rect: " << rect << ", rgn: 0x" << std::hex
                 << rgn_handle << ")";

      return WithHandleToType<Region>(rgn_handle, [&](const Region& region) {
        CHECK_EQ(region.region_size, 10u) << "Only rect regions are supported!";
        return TrapReturn<bool>(RectInRect(rect, region.bounding_box));
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-133.html
    case Trap::OpenRgn: {
      return WithPort([this](GrafPort& port) {
        // TODO: "region_save" should probably be a BitmapImage we draw to
        //       and then use to consturct the final Region in CloseRgn
        port.region_save =
            memory_manager_.AllocateHandle(Region::fixed_size, "OpenRgn");
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-134.html
    case Trap::CloseRgn: {
      auto dst_rgn = Pop<Handle>();
      LOG_TRAP() << "CloseRgn(dstRgn: 0x" << std::hex << dst_rgn << ")";

      return WithPort([&](GrafPort& port) {
        return WithType<Region>(port.region_save, [&](const Region& region) {
          return memory_manager_.WriteTypeToHandle<Region>(region, dst_rgn);
        });
        port.region_save = 0;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-153.html
    case Trap::FillRgn: {
      auto pattern = PopRef<Pattern>();
      auto region_handle = Pop<Handle>();

      auto region_ptr = memory_manager_.GetPtrForHandle(region_handle);
      auto region = TRY(ReadType<Region>(memory::kSystemMemory, region_ptr));

      LOG_TRAP() << "FillRgn(region: " << region << " @ 0x" << std::hex
                 << region_handle << ", pattern: " << pattern << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.FillRect(port::LocalToGlobal(port, region.bounding_box),
                       pattern.bytes);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-350.html
    case Trap::DrawPicture: {
      auto dst_rect = PopRef<Rect>();
      auto my_picture = Pop<Handle>();

      LOG_TRAP() << "DrawPicture(myPicture: 0x" << std::hex << my_picture
                 << ", dstRect: " << std::dec << dst_rect << ")";

      auto pict_data = memory_manager_.GetRegionForHandle(my_picture);

      auto pict_frame = TRY(graphics::GetPICTFrame(pict_data));

      size_t picture_size =
          PixelWidthToBytes(pict_frame.right) * pict_frame.bottom;
      auto picture = std::make_unique<uint8_t[]>(picture_size);
      std::memset(picture.get(), 0, picture_size);

      RETURN_IF_ERROR(
          graphics::ParsePICTv1(pict_data, /*output=*/picture.get()));

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.CopyBits(picture.get(), pict_frame, pict_frame,
                       port::LocalToGlobal(port, dst_rect));
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-351.html
    case Trap::GetPicture: {
      auto pict_id = Pop<Integer>();
      LOG_TRAP() << "GetPicture(picId: " << pict_id << ")";

      Handle handle = resource_manager_.GetResource('PICT', pict_id);
      return TrapReturn<Handle>(handle);
    }

    // ================== Resource Manager ==================

    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-53.html
    case Trap::Get1NamedResource: {
      auto name = PopRef<absl::string_view>();
      ResType type = Pop<ResType>();

      LOG_TRAP() << "Get1NamedResource(theType: '" << OSTypeName(type)
                 << "', name: \"" << name << "\")";

      Handle handle = resource_manager_.GetResourseByName(type, name);
      return TrapReturn<uint32_t>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-50.html
    case Trap::GetResource: {
      auto id = Pop<ResId>();
      auto type = Pop<ResType>();

      LOG_TRAP() << "GetResource(theType: '" << OSTypeName(type)
                 << "', theID: " << id << ")";

      Handle handle = resource_manager_.GetResource(type, id);
      return TrapReturn<uint32_t>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-56.html
    case Trap::LoadResource: {
      auto handle = Pop<uint32_t>();
      LOG_DUMMY() << "LoadResource(theResource: 0x" << std::hex << handle
                  << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-85.html
    case Trap::ReleaseResource: {
      auto handle = Pop<uint32_t>();
      LOG_TRAP() << "ReleaseResource(theResource: 0x" << std::hex << handle
                 << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-82.html
    case Trap::SizeRsrc: {
      auto handle = Pop<uint32_t>();
      LOG_TRAP() << "GetResourceSizeOnDisk(theResource: 0x" << std::hex
                 << handle << ")";
      // FIXME: This should read the size from disk not memory
      auto handle_size = memory_manager_.GetHandleSize(handle);
      return TrapReturn<uint32_t>(handle_size);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-60.html
    case Trap::GetResAttrs: {
      auto handle = Pop<uint32_t>();
      LOG_DUMMY() << "GetResAttrs(theResource: 0x" << std::hex << handle << ")";
      // FIXME: Load the actual attributes from the resource...
      uint16_t attrs = 8;
      return TrapReturn<uint16_t>(attrs);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-63.html
    case Trap::ChangedResource: {
      auto the_resource = Pop<Handle>();
      LOG_DUMMY() << "ChangedResource(theResource: 0x" << std::hex
                  << the_resource << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-67.html
    case Trap::WriteResource: {
      auto the_resource = Pop<Handle>();
      LOG_DUMMY() << "WriteResource(theResource: 0x" << std::hex << the_resource
                  << ")";
      return absl::OkStatus();
    }

    // =====================  Initializers  =====================

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-30.html
    case Trap::InitGraf: {
      auto global_ptr = Pop<Ptr>();
      LOG_TRAP() << "InitGraf(globalPtr: 0x" << std::hex << global_ptr << ")";

      uint32_t a5_world = m68k_get_reg(/*context=*/NULL, M68K_REG_A5);
      RETURN_IF_ERROR(
          memory::kSystemMemory.Write<uint32_t>(a5_world, global_ptr));

      QDGlobals qd_globals;
      qd_globals.screen_bits = screen_bits_;
      qd_globals.grey = {
          .bytes = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}};
      qd_globals.white = {.bytes = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}};

      // `globalPtr` accounts for the size of `thePort` so must be added here
      size_t qd_ptr = global_ptr - QDGlobals::fixed_size + sizeof(Ptr);
      RETURN_IF_ERROR(
          WriteType<QDGlobals>(qd_globals, memory::kSystemMemory, qd_ptr));

      RESTRICT_FIELD_ACCESS(QDGlobals, qd_ptr, QDGlobalsFields::random_seed,
                            QDGlobalsFields::screen_bits,
                            QDGlobalsFields::the_port);

      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-222.html
    case Trap::InitFonts: {
      LOG_TRAP() << "InitFonts()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-223.html
    case Trap::InitWindows: {
      LOG_TRAP() << "InitWindows()";

      GrafPort port;
      // TODO: Verify that "whole screen" in the docs includes the menu bar?
      port::InitPort(port);

      auto ptr = memory_manager_.Allocate(GrafPort::fixed_size);
      RETURN_IF_ERROR(WriteType<GrafPort>(port, memory::kSystemMemory, ptr));

      RESTRICT_FIELD_ACCESS(GrafPort, ptr,
                            GrafPortFields::port_bits + BitMapFields::bounds);

      RETURN_IF_ERROR(
          memory::kSystemMemory.Write<Ptr>(GlobalVars::WMgrPort, ptr));

      RETURN_IF_ERROR(port::SetThePort(ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-114.html
    case Trap::InitMenus: {
      LOG_TRAP() << "InitMenus()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-69.html
    case Trap::TEInit: {
      LOG_TRAP() << "TEInit()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-391.html
    case Trap::InitDialogs: {
      auto resumeProc = Pop<Ptr>();
      CHECK(resumeProc == 0) << "System 7 should always pass null (0)";
      LOG_TRAP() << "InitDialogs(0x" << std::hex << resumeProc << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-381.html
    case Trap::InitCursor: {
      LOG_TRAP() << "InitCursor()";
      return absl::OkStatus();
    }

    // ====================  Window Manager  =====================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-226.html
    case Trap::GetNewWindow: {
      auto behind_window = Pop<Ptr>();
      auto window_storage = Pop<Ptr>();
      auto window_id = Pop<Integer>();
      LOG_TRAP() << "GetNewWindow(id: " << window_id << ", wStorage: 0x"
                 << std::hex << window_storage << ", behind: 0x"
                 << behind_window << ")";

      auto resource_handle = resource_manager_.GetResource('WIND', window_id);
      auto resource_region =
          memory_manager_.GetRegionForHandle(resource_handle);
      auto resource = TRY(ReadType<WIND>(resource_region, /*offset=*/0));

      window_storage = TRY(window_manager_.NewWindow(
          window_storage, resource.initial_rect, resource.title,
          resource.is_visible != 0, resource.has_close != 0,
          resource.window_definition_id, behind_window,
          resource.reference_constant));

      // Focus (activate) and update the most recently created window
      event_manager_.QueueWindowActivate(window_storage, ActivateState::ON);
      event_manager_.QueueWindowUpdate(window_storage);

      return TrapReturn<Ptr>(window_storage);
    }
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-228.html
    case Trap::NewCWindow:
    case Trap::NewWindow: {
      auto reference_constant = Pop<uint32_t>();
      auto go_away_flag = Pop<bool>();
      auto behind_window = Pop<Ptr>();
      auto window_definition_id = Pop<int16_t>();
      auto visible = Pop<bool>();
      auto title = PopRef<std::string>();
      auto bounds_rect = PopRef<Rect>();
      auto window_storage = Pop<Ptr>();

      LOG_TRAP() << "NewWindow(wStorage: 0x" << std::hex << window_storage
                 << ", boundsRect: " << std::dec << bounds_rect << ", title: '"
                 << title << "', visible: " << (visible ? "True" : "False")
                 << ", theProc: 0x" << std::hex << window_definition_id
                 << ", behind: 0x" << behind_window
                 << ", goAwayFlog: " << (go_away_flag ? "True" : "False")
                 << ", refCon: 0x" << reference_constant << ")";

      window_storage = TRY(window_manager_.NewWindow(
          window_storage, bounds_rect, title, visible != 0, go_away_flag != 0,
          window_definition_id, behind_window, reference_constant));

      // Focus (activate) and update the most recently created window
      event_manager_.QueueWindowActivate(window_storage, ActivateState::ON);
      event_manager_.QueueWindowUpdate(window_storage);

      return TrapReturn<Ptr>(window_storage);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-258.html
    case Trap::DisposeWindow: {
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "DisposeWindow(theWindow: 0x" << std::hex << the_window
                 << ")";
      window_manager_.DisposeWindow(the_window);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-243.html
    case Trap::FrontWindow: {
      LOG_TRAP() << "FrontWindow()";
      return TrapReturn<Ptr>(window_manager_.GetFrontWindow());
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-242.html
    case Trap::FindWindow: {
      auto the_window_var = Pop<Ptr>();
      auto the_point = PopType<Point>();

      LOG_TRAP() << "FindWindow(thePoint: " << the_point
                 << ", VAR theWindow: 0x" << std::hex << the_window_var << ")";

      if (menu_manager_.IsInMenuBar(the_point)) {
        return TrapReturn<int16_t>(1 /*inMenuBar*/);
      }

      Ptr target_window = 0;
      // FIXME: Check other regions according to docs
      switch (window_manager_.GetWindowAt(the_point, target_window)) {
        case WindowManager::RegionType::Drag:
          RETURN_IF_ERROR(
              memory::kSystemMemory.Write<Ptr>(the_window_var, target_window));
          return TrapReturn<int16_t>(4 /*inDrag*/);
        case WindowManager::RegionType::Content:
          RETURN_IF_ERROR(
              memory::kSystemMemory.Write<Ptr>(the_window_var, target_window));
          return TrapReturn<int16_t>(3 /*inContent*/);
        case WindowManager::RegionType::None:
          return TrapReturn<int16_t>(0 /*inDesk*/);
      }
    }
    // TODO: Set in the Port when Color QuickDraw is implemented
    case Trap::RGBForeColor: {
      auto rgb = PopType<RGBColor>();

      LOG_TRAP() << "RGBForeColor(red: " << rgb.red << ", green: " << rgb.green
                 << ", blue: " << rgb.blue << ")";

      return WithPort([&](GrafPort& port) {
        // port.fore_color = {red, green, blue};
        return absl::OkStatus();
      });
    }
    case Trap::InvertColor: {
      auto rgb = PopType<RGBColor>();

      LOG_TRAP() << "InvertColor(red: " << rgb.red << ", green: " << rgb.green
                 << ", blue: " << rgb.blue << ")";

      rgb.red = 0xFFFF - rgb.red;
      rgb.green = 0xFFFF - rgb.green;
      rgb.blue = 0xFFFF - rgb.blue;

      return TrapReturn<RGBColor>(rgb);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-270.html
    case Trap::GetWRefCon: {
      auto the_window = PopRef<WindowRecord>();
      LOG_TRAP() << "GetWRefCon(theWindow: " << the_window << ")";
      return TrapReturn<uint32_t>(the_window.reference_constant);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-269.html
    case Trap::SetWRefCon: {
      auto data = Pop<uint32_t>();
      auto window_ptr = Pop<Ptr>();

      LOG_TRAP() << "SetWRefCon(theWindow: 0x" << std::hex << window_ptr
                 << ", data: " << std::dec << data << ")";

      return WithType<WindowRecord>(window_ptr, [&](WindowRecord& window) {
        window.reference_constant = data;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-276.html
    case Trap::GetWMgrPort: {
      auto port_var = Pop<Ptr>();

      LOG_TRAP() << "GetWMgrPort(VAR wPort: 0x" << std::hex << port_var << ")";

      RETURN_IF_ERROR(memory::kSystemMemory.Write<Ptr>(
          port_var,
          TRY(memory::kSystemMemory.Read<Ptr>(GlobalVars::WMgrPort))));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-245.html
    case Trap::DragWindow: {
      auto bounds_rect = PopRef<Rect>();
      auto start_pt = PopType<Point>();
      auto the_window = Pop<Ptr>();

      LOG_TRAP() << "DragWindow(theWindow: 0x" << std::hex << the_window
                 << ", startPt: " << std::dec << start_pt
                 << ", boundsRect: " << bounds_rect << ")";

      window_manager_.DragWindow(the_window, start_pt);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-246.html
    case Trap::MoveWindow: {
      auto front = Pop<bool>();
      auto v_global = Pop<int16_t>();
      auto h_global = Pop<int16_t>();
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "MoveWindow(theWindow: 0x" << std::hex << the_window
                 << std::dec << ", hGlobal: " << h_global
                 << ", vGlobal: " << v_global
                 << ", front: " << (front ? "True" : "False") << ")";
      window_manager_.MoveWindow(the_window, WindowManager::MoveType::Absolute,
                                 {v_global, h_global},
                                 /*bring_to_front=*/front != 0);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-247.html
    case Trap::DragGreyRgn: {
      auto action_proc = Pop<Ptr>();
      auto axis = Pop<uint16_t>();
      auto slop_rect = PopRef<Rect>();
      auto limit_rect = PopRef<Rect>();
      auto start_pt = PopType<Point>();
      auto the_rgn = Pop<Handle>();
      LOG_TRAP() << "DragGreyRgn(theRgn: 0x" << std::hex << the_rgn
                 << ", startPt: " << std::dec << start_pt
                 << ", limitRect: " << limit_rect << ", slopRect: " << slop_rect
                 << ", axis: " << axis << ", actionProc: 0x" << std::hex
                 << action_proc << ")";

      auto region = TRY(memory_manager_.ReadTypeFromHandle<Region>(the_rgn));

      Point pt = window_manager_.DragGrayRegion(region, start_pt);
      return TrapReturn<uint32_t>(pt.y << 16 | pt.x);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-230.html
    case Trap::SetWTitle: {
      auto title = PopRef<absl::string_view>();
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "SetWTitle(theWindow: 0x" << std::hex << the_window
                 << ", title: '" << title << "')";

      auto handle =
          memory_manager_.AllocateHandle(title.size() + 1, "SetWTitle");
      auto memory = memory_manager_.GetRegionForHandle(handle);
      RETURN_IF_ERROR(
          WriteType<absl::string_view>(title, memory, /*offset=*/0));

      return WithType<WindowRecord>(the_window, [&](WindowRecord& window) {
        window.title_handle = handle;
        window.title_width = title.size() * 8;  // Assumes fixed-width 8x8 font

        Ptr wm_port_ptr =
            TRY(memory::kSystemMemory.Read<Handle>(GlobalVars::WMgrPort));
        RETURN_IF_ERROR(port::SetThePort(wm_port_ptr));
        graphics::BitmapImage image = graphics::ThePortImage();
        DrawWindowFrame(window, image);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-234.html
    case Trap::SelectWindow: {
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "SelectWindow(theWindow: 0x" << std::hex << the_window
                 << ")";
      window_manager_.SelectWindow(the_window);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-235.html
    case Trap::ShowWindow: {
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "ShowWindow(theWindow: 0x" << std::hex << the_window << ")";
      return window_manager_.ShowWindow(the_window);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-260.html
    case Trap::BeginUpDate: {
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "BeginUpdate(theWindow: 0x" << std::hex << the_window
                 << ")";

      return WithType<WindowRecord>(
          the_window, [this](WindowRecord& window_record) {
            previous_clip_region_ = window_record.port.clip_region;
            window_record.port.clip_region = window_record.update_region;
            return absl::OkStatus();
          });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-260.html
    case Trap::EndUpDate: {
      auto the_window = Pop<Ptr>();
      LOG_TRAP() << "EndUpdate(theWindow: 0x" << std::hex << the_window << ")";

      return WithType<WindowRecord>(
          the_window, [this](WindowRecord& window_record) {
            window_record.port.clip_region = previous_clip_region_;
            return WithHandleToType<Region>(
                window_record.update_region, [](Region& update_region) {
                  update_region.bounding_box = Rect{0, 0, 0, 0};
                  return absl::OkStatus();
                });
          });
    }

    // ======================  Text Manager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-149.html
    case Trap::TextFont: {
      auto font = Pop<Integer>();
      LOG_TRAP() << "TextFont(font: " << font << ")";
      return WithPort([&](GrafPort& the_port) {
        the_port.text_font = font;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-150.html
    case Trap::TextFace: {
      auto face = Pop<Integer>();
      LOG_DUMMY() << "TextFace(face: " << face << ")";
      // We only support one bitmap font but nice of it to ask... :P
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-152.html
    case Trap::TextSize: {
      auto size = Pop<Integer>();
      LOG_DUMMY() << "TextSize(size: " << size << ")";
      // We only support one bitmap font but nice of it to ask... :P
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-162.html
    case Trap::CharWidth: {
      auto ch = Pop<Integer>();
      LOG_TRAP() << "CharWidth(ch: '" << (char)ch << "')";
      return TrapReturn<Integer>(8 /*8x8 bitmap font*/);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-157.html
    case Trap::DrawChar: {
      auto ch = Pop<Integer>();
      LOG_TRAP() << "DrawChar(ch: '" << (char)ch << "')";

      return InPort([&](GrafPort& port, graphics::BitmapImage& image) {
        port.pen_location.x +=
            GetFont(port.text_font)
                .DrawChar(image, ch,
                          port.pen_location.x - port.port_bits.bounds.left,
                          port.pen_location.y - port.port_bits.bounds.top);
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-158.html
    case Trap::DrawString: {
      auto str = PopRef<std::string>();
      LOG_TRAP() << "DrawString(str: " << str << ")";

      return InPort([&](GrafPort& port, graphics::BitmapImage& image) {
        // TODO: Remove the -8 y-offset. It looks like maybe QuickDraw expects
        //       fonts drawn from the bottom-left corner and Cyder's code
        //       expects them in the top-left... needs more investigation.
        int width =
            GetFont(port.text_font)
                .DrawString(
                    image, str,
                    port.pen_location.x - port.port_bits.bounds.left,
                    port.pen_location.y - port.port_bits.bounds.top - 8);
        port.pen_location.x += width;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-303.html
    case Trap::GetString: {
      auto string_id = Pop<Integer>();
      LOG_TRAP() << "GetString(stringID: " << string_id << ")";

      Handle handle = resource_manager_.GetResource('STR ', string_id);
      return TrapReturn<Handle>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-155.html
    case Trap::GetFontInfo: {
      auto info_var = Pop<Ptr>();
      LOG_TRAP() << "GetFontInfo(VAR info: 0x" << std::hex << info_var << ")";

      return WithType<FontInfo>(info_var, [&](FontInfo& info) {
        // Only a fixed width 8x8 bitmap font is currently supported :P
        info.ascent = 8;
        info.descent = 0;
        info.widMax = 8;
        info.leading = 0;
        return absl::OkStatus();
      });
    }
    // Link: https://dev.os9.ca/techpubs/mac/Text/Text-224.html
    case Trap::GetFontName: {
      auto theName = PopVar<std::string>();
      auto familyId = Pop<uint16_t>();
      LOG_DUMMY() << "GetFontName(familyId: " << familyId << ", VAR theName: '"
                  << theName << "')";
      return absl::OkStatus();
    }
    // Link: https://dev.os9.ca/techpubs/mac/Text/Text-225.html
    case Trap::GetFontNum: {
      auto familyId = PopVar<uint16_t>();
      auto theName = PopRef<std::string>();
      LOG_DUMMY() << "GetFontNum(theName: '" << theName
                  << "', VAR familyId: " << familyId << ")";
      return absl::OkStatus();
    }
    // Link: https://dev.os9.ca/techpubs/mac/Text/Text-226.html
    case Trap::RealFont: {
      auto size = Pop<uint16_t>();
      auto fontNum = Pop<uint16_t>();
      LOG_DUMMY() << "RealFont(fontNum: " << fontNum << ", size: " << size
                  << ")";
      return TrapReturn<bool>(false);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-163.html
    case Trap::StringWidth: {
      auto str = PopRef<absl::string_view>();
      LOG_TRAP() << "StringWidth(s: '" << str << "')";
      // Only a fixed width 8x8 bitmap font is currently supported :P
      return TrapReturn<Integer>(str.size() * 8);
    }

    // =========================  TextEdit  ==========================

    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-89.html
    case Trap::TETextBox: {
      auto align = Pop<int16_t>();
      auto box = PopRef<Rect>();
      auto length = Pop<uint32_t>();
      auto text_ptr = Pop<Ptr>();
      LOG_TRAP() << "TETextBox(text: 0x" << std::hex << text_ptr
                 << ", length: " << std::dec << length << ", box: " << box
                 << ", align: " << align << ")";

      box = TRY(port::ConvertLocalToGlobal(box));

      auto text = absl::string_view(
          reinterpret_cast<const char*>(memory::kSystemMemory.raw_ptr()) +
              text_ptr,
          length);

      // Only a 8x8 bitmap font is currently supported so assume 8px wide chars
      auto length_px = text.size() * 8;
      // Alignment Constants Link:
      //   http://0.0.0.0:8000/docs/mac/Text/Text-125.html#MARKER-9-577

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        switch (align) {
          case 1: {  // teCenter
            int offset_x = (RectWidth(box) - length_px) / 2;
            int offset_y = (RectHeight(box) - 8) / 2;
            GetFont(port.text_font)
                .DrawString(image, text, box.left + offset_x,
                            box.top + offset_y);
            break;
          }
          case -1:  // teFlushRight
            GetFont(port.text_font)
                .DrawString(image, text, box.right - length_px, box.top);
            break;
          case 0:   // teFlushDefault (assume Left-to-Right script)
          case -2:  // teFlushLeft
            GetFont(port.text_font).DrawString(image, text, box.left, box.top);
            break;
        }
        return absl::OkStatus();
      });
    }

    // ======================  Dialog Manager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-427.html
    case Trap::IsDialogEvent: {
      auto the_event = PopRef<EventRecord>();
      LOG_TRAP() << "IsDialogEvent(theEvent: " << the_event << ")";
      return TrapReturn<bool>(TRY(dialog::IsDialogEvent(std::move(the_event))));
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-417.html
    case Trap::ParamText: {
      auto param0 = PopRef<absl::string_view>();
      auto param1 = PopRef<absl::string_view>();
      auto param2 = PopRef<absl::string_view>();
      auto param3 = PopRef<absl::string_view>();
      LOG_TRAP() << "ParamText(param0: '" << param0 << "', param1: '" << param1
                 << "', param2: '" << param2 << "', param3: '" << param3
                 << "')";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-402.html
    case Trap::GetNewDialog: {
      WindowPtr behind = Pop<WindowPtr>();
      Ptr d_storage = Pop<Ptr>();
      Integer dialog_id = Pop<Integer>();

      LOG_TRAP() << "GetNewDialog(dialogId: " << dialog_id << ", dStorage: 0x"
                 << std::hex << d_storage << ", behind: 0x" << behind << ")";

      dialog::DialogPtr dialog_ptr =
          TRY(dialog::GetNewDialog(dialog_id, d_storage, behind));

      return TrapReturn<dialog::DialogPtr>(dialog_ptr);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-408.html
    case Trap::GetDialogItem: {
      Var<Rect> box = PopVar<Rect>();
      Var<Handle> item = PopVar<Handle>();
      Var<Integer> item_type = PopVar<Integer>();
      Integer item_no = Pop<Integer>();
      dialog::DialogPtr the_dialog = Pop<dialog::DialogPtr>();

      LOG_TRAP() << "GetDialogItem(theDialog: 0x" << std::hex << the_dialog
                 << ", itemNo: " << std::dec << item_no
                 << ", VAR itemType: " << item_type << ", VAR item: " << item
                 << ", VAR box: " << box << ")";

      return dialog::GetDialogItem(the_dialog, item_no, std::move(item_type),
                                   std::move(item), std::move(box));
    }
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-409.html
    case Trap::SetDialogItem: {
      Rect box = PopRef<Rect>();
      Handle item = Pop<Handle>();
      Integer item_type = Pop<Integer>();
      Integer item_no = Pop<Integer>();
      dialog::DialogPtr the_dialog = Pop<dialog::DialogPtr>();

      LOG_TRAP() << "SetDialogItem(theDialog: 0x" << std::hex << the_dialog
                 << ", itemNo: " << std::dec << item_no
                 << ", itemType: " << item_type << ", item: 0x" << std::hex
                 << item << ", box: " << box << ")";

      return dialog::SetDialogItem(the_dialog, item_no, item_type, item,
                                   std::move(box));
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-426.html
    case Trap::ModalDialog: {
      Var<Integer> item_hit = PopVar<Integer>();
      Ptr filter_proc = Pop<Ptr>();

      LOG_TRAP() << "ModalDialog(filterProc: 0x" << std::hex << filter_proc
                 << ", VAR itemHit: " << item_hit << ")";

      return dialog::ModalDialog(filter_proc, std::move(item_hit));
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-428.html
    case Trap::DialogSelect: {
      Ptr var_item_hit = Pop<Ptr>();
      Ptr var_the_dialog = Pop<Ptr>();
      EventRecord event_record = PopRef<EventRecord>();

      LOG_TRAP() << "DialogSelect(eventRecord: " << event_record
                 << ", VAR theDialog: 0x" << std::hex << var_the_dialog
                 << ", VAR itemHit: 0x" << var_item_hit << ")";

      return TrapReturn<bool>(false);
    }
    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-406.html
    case Trap::DisposeDialog: {
      auto the_dialog = Pop<dialog::DialogPtr>();
      LOG_TRAP() << "DisposeDialog(theDialog: 0x" << std::hex << the_dialog
                 << ")";
      window_manager_.DisposeWindow(the_dialog);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-396.html
    case Trap::StopAlert: {
      auto filter_proc = Pop<Ptr>();
      auto alert_id = Pop<uint16_t>();
      LOG_TRAP() << "StopAlert(alertID: " << alert_id << ", filterProc: 0x"
                 << std::hex << filter_proc << ")";
      return TrapReturn<int16_t>(-1);
    }

    // ======================  Icon Utilities  =======================

    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-282.html
    case Trap::PlotIcon: {
      auto the_handle = Pop<Handle>();
      auto the_rect = PopRef<Rect>();

      auto icon_ptr = TRY(memory::kSystemMemory.Read<Handle>(the_handle));

      LOG_TRAP() << "PlotIcon(theRect: " << the_rect << ", theHandle: 0x"
                 << std::hex << the_handle << ")";

      return InPort([&](const GrafPort& port, graphics::BitmapImage& image) {
        image.CopyBits(memory::kSystemMemory.raw_ptr() + icon_ptr,
                       NewRect(0, 0, 32, 32), NewRect(0, 0, 32, 32),
                       port::LocalToGlobal(port, the_rect));
        return absl::OkStatus();
      });
    }

    // ==============  Date, Time, and Measurement Utilities  ===============

    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-113.html
    case Trap::SecondsToDate: {
      uint32_t number_of_seconds = m68k_get_reg(NULL, M68K_REG_D0);
      Ptr record_ptr = m68k_get_reg(NULL, M68K_REG_A0);

      static auto kLocalTimeZone = absl::LocalTimeZone();

      return WithType<DateTimeRec>(record_ptr, [&](DateTimeRec& record) {
        auto breakdown = kLocalTimeZone.At(
            absl::FromUnixSeconds(number_of_seconds - 2082844800));

        record.day = breakdown.cs.day();
        record.month = breakdown.cs.month();
        record.year = breakdown.cs.year();
        // TODO: Absiel has removed  the weekday field from CivilTime find a
        //       way to calculate it on our own.
        // record.dayOfWeek = breakdown.cs.weekday();
        record.hour = breakdown.cs.hour();
        record.minute = breakdown.cs.minute();
        record.second = breakdown.cs.second();
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-114.html
    case Trap::DateToSeconds: {
      Ptr record_ptr = m68k_get_reg(NULL, M68K_REG_A0);
      return WithType<DateTimeRec>(record_ptr, [&](const DateTimeRec& record) {
        absl::CivilSecond civil_time(record.year, record.month, record.day,
                                     record.hour, record.minute, record.second);
        absl::Time time = absl::FromCivil(civil_time, absl::LocalTimeZone());
        m68k_set_reg(M68K_REG_D0, absl::ToUnixSeconds(time) + 2082844800);
        return absl::OkStatus();
      });
    }

    // ==================  Math and Logical Utilities  ====================

    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-56.html
    case Trap::HiWord: {
      auto x = Pop<uint32_t>();
      LOG_TRAP() << "HiWord(x: " << x << ")";
      return TrapReturn<uint16_t>((x >> 16) & 0xFFFF);
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-57.html
    case Trap::LoWord: {
      auto x = Pop<uint32_t>();
      LOG_TRAP() << "LoWord(x: " << x << ")";
      return TrapReturn<uint16_t>(x & 0xFFFF);
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-58.html
    case Trap::StuffHex: {
      auto s = PopRef<std::string>();
      auto thing_ptr = Pop<Ptr>();
      LOG_TRAP() << "StuffHex(thingPtr: 0x" << std::hex << thing_ptr << ", s: '"
                 << s << "')";
      for (size_t i = 0; i < s.length() / 2; ++i) {
        auto hex = s.substr(i * 2, 2);
        // TODO: Error check? Documentation implies it will always be valid
        auto value = strtol(hex.c_str(), NULL, 16);
        RETURN_IF_ERROR(
            memory::kSystemMemory.Write<uint8_t>(thing_ptr + i, value));
      }
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-63.html
    case Trap::Random: {
      LOG_TRAP() << "Random()";
      // FIXME: Use the same algorithm used in Mac OS to generate rand()
      RETURN_IF_ERROR(TrapReturn<int16_t>(0xFFFF * rand()));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-88.html
    case Trap::FixRatio: {
      auto denom = Pop<uint16_t>();
      auto numer = Pop<uint16_t>();
      LOG_TRAP() << "FixRatio(numer: " << numer << ", denom: " << denom << ")";
      return TrapReturn<uint32_t>((static_cast<uint32_t>(numer) << 16) / denom);
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-68.html
    case Trap::FixMul: {
      uint64_t v2 = Pop<uint32_t>();
      uint64_t v1 = Pop<uint32_t>();
      LOG_TRAP() << "FixMul(v1: " << v1 << ", v2: " << v2 << ")";
      uint64_t result = v1 * v2;
      return TrapReturn<uint32_t>(result >> 16);
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-89.html
    case Trap::FixRound: {
      uint32_t v = Pop<uint32_t>();
      LOG_TRAP() << "FixRound(v: " << v << ")";
      // Separate integer and fractional values
      uint16_t integer = v >> 16;
      uint16_t fractional = v & 0xFFFF;
      // Round up only if `fractional` is greater than 0.5
      if (fractional > 32767) {
        integer = integer + 1;
      }
      return TrapReturn<uint16_t>(integer);
    }
    // Mathematical and Logical Utilities (3-28)
    // https://dev.os9.ca/techpubs/mac/pdf/Operating_System_Utilities/MLU.pdf
    case Trap::BitTst: {
      auto bitNum = Pop<uint32_t>();
      auto bytePtr = Pop<Ptr>();

      LOG_TRAP() << "BitTst(bytePtr: 0x" << std::hex << bytePtr
                 << ", bitNum: " << std::dec << bitNum << ")";

      uint32_t byteOffset = bitNum / 8;
      uint32_t bitInByte = bitNum % 8;
      bool result = (memory::kSystemMemory.raw_ptr()[bytePtr + byteOffset] &
                     (1 << (7 - bitInByte))) != 0;
      return TrapReturn<bool>(result);
    }
    // Mathematical and Logical Utilities (3-30)
    // https://dev.os9.ca/techpubs/mac/pdf/Operating_System_Utilities/MLU.pdf
    case Trap::BitAnd: {
      auto v2 = Pop<uint32_t>();
      auto v1 = Pop<uint32_t>();

      LOG_TRAP() << "BitAnd(value1: " << v1 << ", value2: " << v2 << ")";

      return TrapReturn<uint32_t>(v1 & v2);
    }
    // Mathematical and Logical Utilities (3-30)
    // https://dev.os9.ca/techpubs/mac/pdf/Operating_System_Utilities/MLU.pdf
    case Trap::BitShift: {
      auto count = Pop<int16_t>();
      auto value = Pop<uint32_t>();

      LOG_TRAP() << "BitShift(value: " << value << ", count: " << count << ")";

      if (count < 0) {
        return TrapReturn<uint32_t>(value >> -count);
      } else {
        return TrapReturn<uint32_t>(value << count);
      }
    }
    // Mathematical and Logical Utilities (3-30)
    // https://dev.os9.ca/techpubs/mac/pdf/Operating_System_Utilities/MLU.pdf
    case Trap::BitSet: {
      auto bitNum = Pop<uint32_t>();
      auto bytePtr = Pop<Ptr>();

      LOG_TRAP() << "BitSet(bytePtr: 0x" << std::hex << bytePtr
                 << ", bitNum: " << std::dec << bitNum << ")";

      uint32_t byteOffset = bitNum / 8;
      uint32_t bitInByte = bitNum % 8;
      const_cast<uint8_t*>(
          memory::kSystemMemory.raw_ptr())[bytePtr + byteOffset] |=
          (1 << (7 - bitInByte));
      return absl::OkStatus();
    }

    // ======================  Sound Manager  ========================

    // Link: http://0.0.0.0:8000/docs/mac/Sound/Sound-97.html
    case Trap::SndNewChannel: {
      auto user_routine = Pop<Ptr>();
      auto init = Pop<uint32_t>();
      auto synth = Pop<uint16_t>();
      auto chan_var = Pop<Ptr>();
      LOG_DUMMY() << "SndNewChannel(VAR chan: 0x" << std::hex << chan_var
                  << ", synth: " << std::dec << synth << ", init: " << init
                  << ", userRoutine: 0x" << std::hex << user_routine << ")";
      return TrapReturn<uint16_t>(-204 /*resProblem*/);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Sound/Sound-35.html
    case Trap::SndPlay: {
      auto async = Pop<bool>();
      auto snd_hdl = Pop<Handle>();
      auto chan = Pop<Ptr>();
      LOG_DUMMY() << "SndPlay(chan: 0x" << std::hex << chan << ", sndHdl: 0x"
                  << snd_hdl << ", async: " << (async ? "True" : "False")
                  << ")";
      return TrapReturn<uint16_t>(-201 /*notEnoughHardwareErr*/);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Sound/Sound-98.html
    case Trap::SndDisposeChannel: {
      auto quiet_now = Pop<bool>();
      auto chan = Pop<Ptr>();
      LOG_DUMMY() << "SndDisposeChannel(chan: 0x" << std::hex << chan
                  << ", quietNow: " << (quiet_now ? "True" : "False") << ")";
      return TrapReturn<uint16_t>(0 /*noErr*/);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Sound/Sound-90.html
    case Trap::SysBeep: {
      uint16_t duration = Pop<Integer>();
      LOG_DUMMY() << "SysBeep(duration: " << duration << ")";
      return absl::OkStatus();
    }

    // ======================== Scrap Manager ========================

    // Link: https://dev.os9.ca/techpubs/mac/MoreToolbox/MoreToolbox-135.html
    case Trap::UnloadScrap: {
      LOG_TRAP() << "UnloadScrap()";
      return TrapReturn<uint32_t>(0 /*noErr*/);
    }

    // ===========================  _Pack#  =============================

    // Link:
    // http://www.bitsavers.org/pdf/apple/mac/Inside_Macintosh_Vol_2_1984.pdf
    case Trap::Pack3: {
      // IMV2(25): The macros for calling the Standard File Package routines
      // push one of the following routine selectors onto the stack and then
      // invoke Pack3:
      auto selector = Pop<Integer>();
      switch (selector) {
        case 1:  // IMV2(26): SFPutFile
        {
          Var<SFReply> reply = PopVar<SFReply>();
          Ptr dlg_hook = Pop<Ptr>();
          auto orig_name = PopRef<absl::string_view>();
          auto prompt = PopRef<absl::string_view>();
          auto where = PopType<Point>();
          LOG_TRAP() << "_Pack3 SFPutFile(where: " << where << ", prompt: '"
                     << prompt << "', origName: '" << orig_name
                     << "', dlgHook: 0x" << std::hex << dlg_hook
                     << ", VAR reply: 0x" << reply << ")";

          SFReply reply_value;
          reply_value.good = true;
          return WriteType<SFReply>(std::move(reply_value),
                                    memory::kSystemMemory, reply.ptr);
        }
        case 2:  // IMV2(30): SFGetFile
        {
          Var<SFReply> reply = PopVar<SFReply>();
          Ptr dlg_hook = Pop<Ptr>();
          Ptr type_list_ptr = Pop<Ptr>();
          int16_t num_types = Pop<int16_t>();
          Ptr file_filter_proc = Pop<Ptr>();
          auto prompt = PopRef<absl::string_view>();
          auto where = PopType<Point>();

          using OSType = uint32_t;
          OSType type_list[4];  // IMV2(31): This array is declared for a
                                // reasonable maximum number of types (four).
                                // If you need to specify more than four
                                // types, declare your own array type with the
                                // desired number of entries (and use the @
                                // operator to pass a pointer to it).
          std::stringstream type_list_str;

          // IMV2(31): Pass -1 for numTypes to display all types of files;
          // otherwise, pass the number of file types you want to display.
          if (num_types != -1) {
            for (int i = 0; i < num_types; ++i) {
              type_list[i] =
                  TRY(memory::kSystemMemory.Read<uint32_t>(type_list_ptr));
              type_list_str << OSTypeName(type_list[i]) << ", ";
              type_list_ptr += sizeof(uint32_t);
            }
          }

          LOG_TRAP() << "_Pack3 SFGetFile(where: " << where << ", prompt: '"
                     << prompt << "', file_filter_proc: 0x" << std::hex
                     << file_filter_proc << ", numTypes: " << std::dec
                     << num_types << ", typeList: [" << type_list_str.str()
                     << "]" << ", dlgHook: 0x" << std::hex << dlg_hook
                     << ", VAR reply: 0x" << reply << ")";
          SFReply reply_value;
          reply_value.good = false;
          return WriteType<SFReply>(std::move(reply_value),
                                    memory::kSystemMemory, reply.ptr);
        }
        case 3:  // IMV2(30): SFPPutFile
          return absl::UnimplementedError("_Pack3 SFPPutFile is unimplemented");
        case 4:  // SFPGetFile
          return absl::UnimplementedError("_Pack3 SFPGetFile is unimplemented");
      }
      LOG(FATAL) << "Unknown _Pack3 routine selector: " << selector;
    }

    // ========================  Control Manager  ==========================

    // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-323.html
    case Trap::GetNewControl: {
      auto owner = Pop<WindowPtr>();
      auto control_id = Pop<uint16_t>();
      LOG_TRAP() << "GetNewControl(control_id: 0x" << std::hex << control_id
                 << ", owner: 0x" << owner << std::dec << ")";
      Handle handle_to_control = TRY(control::GetNewControl(control_id, owner));
      return TrapReturn<Handle>(handle_to_control);
    }

    // ========================  Segment Manager  ==========================

    // Link: https://dev.os9.ca/techpubs/mac/Processes/Processes-144.html
    case Trap::UnLoadSeg: {
      auto routineAddr = Pop<Ptr>();
      LOG_DUMMY() << "UnloadSeg(routineAddr: 0x" << std::hex << routineAddr
                  << ")";
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Unimplemented Toolbox trap: '", GetTrapName(trap), "'"));
  }
}

}  // namespace trap
}  // namespace cyder