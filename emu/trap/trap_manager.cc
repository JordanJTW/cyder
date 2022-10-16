#include "trap_manager.h"

#include <SDL.h>

#include <cstdint>
#include <iomanip>
#include <tuple>

#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/graphics/grafport_types.h"
#include "emu/graphics/quickdraw.h"
#include "emu/memory/memory_map.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_helpers.h"
#include "gen/trap_names.h"
#include "gen/typegen/typegen_prelude.h"
#include "resource.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace trap {
namespace {

using rsrcloader::GetTypeName;

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

absl::Status HandleLoadSegmentTrap(SegmentLoader& segment_loader,
                                   Ptr& return_address) {
  uint16_t load_segment = TRY(Pop<uint16_t>());
  LOG(INFO) << "TRAP LoadSeg(" << load_segment << ")";
  TRY(segment_loader.Load(load_segment));
  // The segment loader modifies the six byte entry for this segment in the
  // table so return to the begining of the entry (4 bytes behind the 2 byte
  // trap being executed):
  return_address -= 6;
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
    case memory::kTrapManagerExitAddress:
      return PerformTrapExit();
    // FIXME: This should not be necessary; remove this hack...
    case (memory::kTrapManagerExitAddress + 2):
      return absl::OkStatus();
    default:
      if (address < memory::kTrapManagerExitAddress &&
          address >= memory::kBaseSystemTrapAddress) {
        uint16_t trap_index =
            (address - memory::kBaseSystemTrapAddress) / sizeof(uint16_t);
        return PerformTrapDispatch(trap_index, false);
      } else if (address < memory::kBaseSystemTrapAddress &&
                 address >= memory::kBaseToolboxTrapAddress) {
        uint16_t trap_index =
            (address - memory::kBaseToolboxTrapAddress) / sizeof(uint16_t);
        return PerformTrapDispatch(trap_index, true);
      }

      return absl::UnimplementedError(
          absl::StrCat("No subroutine registered for address: 0x",
                       absl::Hex(address, absl::kZeroPad8)));
  }
}

absl::Status TrapManager::PerformTrapEntry() {
  /*status_register=*/TRY(Pop<uint16_t>());
  auto instruction_ptr = TRY(Pop<uint32_t>());

  uint16_t trap_op =
      be16toh(TRY(memory::kSystemMemory.Copy<uint16_t>(instruction_ptr)));

  if (IsAutoPopSet(trap_op)) {
    // Clear the bit so that GetTrapName() works below
    // FIXME: GetTrapName() should ignore flags in the A-Trap
    trap_op = trap_op & ~(1 << 10);
    // If the "auto-pop bit" is set then the program has called a trap
    // indirectly (through a "glue subroutine"). We should return to
    // the JSR address instead of the instruction after the A-Trap.
    instruction_ptr = TRY(Pop<uint32_t>());
  } else {
    // `instruction_ptr` points to the address of the instruction that
    // triggered the trap. When we return from handling the trap return
    // to the instruction past the 16-bit A-Trap (i.e. + 2).
    instruction_ptr += 2;
  }

  LOG(INFO) << COLOR(160) << "A-Line Exception "
            << (IsToolbox(trap_op) ? "Toolbox" : "OS")
            << "::" << GetTrapName(trap_op) << " (0x" << std::hex << trap_op
            << ") Index: " << std::dec << ExtractIndex(trap_op)
            << COLOR_RESET();

  RETURN_IF_ERROR(Push<uint32_t>(instruction_ptr));

  if (IsSystem(trap_op)) {
    if (ShouldSaveA0(trap_op)) {
      RETURN_IF_ERROR(
          Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_A0)));
    }
    RETURN_IF_ERROR(
        Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_A1)));
    RETURN_IF_ERROR(
        Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_D1)));
    RETURN_IF_ERROR(
        Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_D2)));

    m68k_set_reg(M68K_REG_D1, trap_op);
  }

  uint32_t dispatch_address = GetTrapAddress(trap_op);
  RETURN_IF_ERROR(Push<uint32_t>(dispatch_address));
  return absl::OkStatus();
}

absl::Status TrapManager::PerformTrapExit() {
  uint32_t trap_op = m68k_get_reg(/*context=*/NULL, M68K_REG_D1);

  m68k_set_reg(M68K_REG_D2, TRY(Pop<uint32_t>()));
  m68k_set_reg(M68K_REG_D1, TRY(Pop<uint32_t>()));
  m68k_set_reg(M68K_REG_A1, TRY(Pop<uint32_t>()));
  if (ShouldSaveA0(trap_op)) {
    m68k_set_reg(M68K_REG_A0, TRY(Pop<uint32_t>()));
  }
  return absl::OkStatus();
}

absl::Status TrapManager::PerformTrapDispatch(uint16_t trap_index,
                                              bool is_toolbox) {
  uint16_t trap_op = 0xA000 | trap_index;
  if (is_toolbox) {
    trap_op |= (1 << 11);
  } else {
    trap_op = m68k_get_reg(/*context=*/NULL, M68K_REG_D1);
    CHECK_EQ(trap_index, ExtractIndex(trap_op));
  }

  // This must be removed from the stack so the arguments are at the top:
  Ptr return_address = TRY(Pop<Ptr>());

  // Handle _LoadSeg specially since it needs to modify the return address.
  absl::Status status = absl::OkStatus();
  if (Trap::LoadSeg == trap_op) {
    status = HandleLoadSegmentTrap(segment_loader_, return_address);
  } else {
    status = IsToolbox(trap_op) ? DispatchNativeToolboxTrap(trap_op)
                                : DispatchNativeSystemTrap(trap_op);
  }
  RETURN_IF_ERROR(Push<Ptr>(return_address));
  if (IsSystem(trap_op)) {
    RETURN_IF_ERROR(Push<Ptr>(memory::kTrapManagerExitAddress));
  }
  return status;
}

uint32_t TrapManager::GetTrapAddress(uint16_t trap) {
  if (trap == Trap::Unimplemented) {
    return 0;
  }

  auto patch_address = patch_trap_addresses_.find(trap);
  if (patch_address != patch_trap_addresses_.cend()) {
    return patch_address->second;
  } else {
    if (IsToolbox(trap)) {
      return memory::kBaseToolboxTrapAddress +
             ExtractIndex(trap) * sizeof(uint16_t);
    } else {
      return memory::kBaseSystemTrapAddress +
             ExtractIndex(trap) * sizeof(uint16_t);
    }
  }
}

void TrapManager::SetTrapAddress(uint16_t trap, uint32_t address) {
  if (address >= memory::kBaseToolboxTrapAddress &&
      address < memory::kTrapManagerExitAddress) {
    patch_trap_addresses_.erase(trap);
    return;
  }

  patch_trap_addresses_[trap] = address;

  for (const auto& pair : patch_trap_addresses_) {
    LOG(INFO) << "Patch trap: 0x" << std::hex << pair.first << " -> 0x"
              << pair.second;
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
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-21.html
    case Trap::NewHandle:
    case Trap::NewHandleClear: {
      // D0 seems to contain the argument in a sample program...
      // but the documentation says it should be in A0.
      uint32_t logical_size = m68k_get_reg(NULL, M68K_REG_D0);
      LOG(INFO) << "TRAP NewHandle(logicalSize: " << logical_size << ")";
      auto handle = memory_manager_.AllocateHandle(logical_size, "NewHandle");
      m68k_set_reg(M68K_REG_A0, handle);
      m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-64.html
    case Trap::MaxApplZone:
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-65.html
    case Trap::MoreMasters: {
      // Memory manager currently assumes max heap already and
      // heap fragmentation is not a huge concern right now.
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-125.html
    case Trap::SetGrowZone:
      m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      return absl::OkStatus();

    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-19.html
    case Trap::SetApplLimit: {
      uint32_t zone_limit = m68k_get_reg(NULL, M68K_REG_A0);
      LOG(INFO) << "TRAP SetApplLimit(zoneLimit: 0x" << std::hex << zone_limit
                << ")";
      bool success = memory_manager_.SetApplLimit(zone_limit);
      int32_t result_code = success ? /*noErr*/ 0 : /*memFullErr*/ -108;
      m68k_set_reg(M68K_REG_D0, result_code);
      return absl::OkStatus();
    }

    // =======================  Trap Manager  ====================

    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-175.html
    case Trap::GetOSTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = ExtractSystemIndex(trap_index & 0xFFFF);
      LOG(INFO) << "TRAP GetOSTrapAddress(trap: '"
                << GetTrapNameBySystemIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-176.html
    case Trap::GetToolBoxTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = ExtractToolboxIndex(trap_index & 0xFFFF);
      LOG(INFO) << "TRAP GetToolBoxTrapAddress(trap: '"
                << GetTrapNameByToolboxIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-185.html
    case Trap::GetTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = trap_index & 0xFFFF;  // A-Traps are always WORD sized
      if (trap_index < 0xFF) {
        // Any trap number from $00 to $4F, $54, or $57 is in the OS table
        bool is_system =
            trap_index < 0x4F || trap_index == 0x54 || trap_index == 0x57;
        if (!is_system) {
          // See the description of A-Traps in trap_helpers.h
          trap_index |= (1 << 11);  // Sets the "Toolbox Trap" bit
        }
        trap_index = 0xA000 | trap_index;
      }
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

      SetTrapAddress(trap_index, trap_address);
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
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);
      LOG(INFO) << "TRAP Open(ptr: 0x" << std::hex << ptr << ")";
      LOG(INFO) << "IOParam: "
                << TRY(ReadType<IOParam>(memory::kSystemMemory, ptr));
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

    // ===================  Menu Manager  ======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-124.html
    case Trap::GetNewMBar: {
      auto menu_bar_id = TRY(Pop<Integer>());
      LOG(INFO) << "TRAP GetNewMBar(menuBarID: " << menu_bar_id << ")";
      Handle handle = resource_manager_.GetResource('MBAR', menu_bar_id);

      auto menu_bar = TRY(ReadType<MenuBarResource>(
          memory_manager_.GetRegionForHandle(handle), 0));

      for (ResourceId id : menu_bar.menus) {
        LOG(INFO) << "Menu Resource ID: " << id;
        Handle menu_handle = resource_manager_.GetResource('MENU', id);

        auto menu = TRY(ReadType<MenuResource>(
            memory_manager_.GetRegionForHandle(menu_handle), 0));

        LOG(INFO) << "Menu {" << menu << "}";
        for (const auto& item : menu.items) {
          LOG(INFO) << "MenuItem {" << item << "}";
        }
      }
      return TrapReturn<Handle>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-118.html
    // NOTE: It appears that _GetRMenu was renamed to _GetMenu at some point
    case Trap::GetRMenu: {
      auto menu_id = TRY(Pop<Integer>());
      LOG(INFO) << "TRAP GetRMenu(menuID: " << menu_id << ")";

      Handle handle = resource_manager_.GetResource('MENU', menu_id);

      auto menu = TRY(ReadType<MenuResource>(
          memory_manager_.GetRegionForHandle(handle), 0));

      LOG(INFO) << "Menu {" << menu << "}";
      for (const auto& item : menu.items) {
        LOG(INFO) << "MenuItem {" << item << "}";
      }
      return TrapReturn<Handle>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-120.html
    case Trap::InsertMenu: {
      auto before_id = TRY(Pop<uint16_t>());
      auto the_menu = TRY(Pop<Handle>());
      LOG(INFO) << "TRAP InsertMenu(beforeId: " << before_id << ", theMenu: 0x"
                << std::hex << the_menu << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-147.html
    case Trap::AppendResMenu: {
      auto the_type = TRY(Pop<ResType>());
      auto the_menu = TRY(Pop<Handle>());
      LOG(INFO) << "TRAP AppendResMenu(theMenu: 0x" << std::hex << the_menu
                << ", theType: " << GetTypeName(the_type) << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-130.html
    case Trap::DrawMenuBar: {
      LOG(INFO) << "TRAP DrawMenuBar()";
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

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-36.html
    case Trap::GetPort: {
      auto port_var = TRY(Pop<Ptr>());
      LOG(INFO) << "TRAP GetPort(VAR port: 0x" << std::hex << port_var << ")";


      RETURN_IF_ERROR(memory::kSystemMemory.Write<Ptr>(
          port_var, htobe<Ptr>(TRY(port::GetThePort()))));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-37.html
    case Trap::SetPort: {
      auto port = TRY(Pop<Ptr>());
      LOG(INFO) << "TRAP SetPort(port: 0x" << std::hex << port << ")";

      RETURN_IF_ERROR(port::SetThePort(port));
      return absl::OkStatus();
    }
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
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-86.html
    case Trap::SetRect: {
      auto bottom = TRY(Pop<uint16_t>());
      auto right = TRY(Pop<uint16_t>());
      auto top = TRY(Pop<uint16_t>());
      auto left = TRY(Pop<uint16_t>());
      auto rect_ptr = TRY(Pop<Ptr>());
      LOG(INFO) << "TRAP SetRect(r: 0x" << std::hex << rect_ptr
                << ", left: " << std::dec << left << ", top: " << top
                << ", right: " << right << ", bottom: " << bottom << ")";
      struct Rect rect;
      rect.left = left;
      rect.top = top;
      rect.right = right;
      rect.bottom = bottom;
      RETURN_IF_ERROR(WriteType<Rect>(rect, memory::kSystemMemory, rect_ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-88.html
    case Trap::InsetRect: {
      auto dv = TRY(Pop<int16_t>());
      auto dh = TRY(Pop<int16_t>());
      auto rect_ptr = TRY(Pop<Ptr>());
      auto rect = TRY(ReadType<Rect>(memory::kSystemMemory, rect_ptr));
      LOG(INFO) << "TRAP InsetRect(r: { " << rect << " } @ 0x" << std::hex
                << rect_ptr << std::dec << ", dh: " << dh << ", dv: " << dv
                << ")";
      rect.left += dh;
      rect.right -= dh;
      rect.top += dv;
      rect.bottom -= dv;

      if (rect.right - rect.left < 0 || rect.bottom - rect.top < 0) {
        rect.left = 0;
        rect.top = 0;
        rect.right = 0;
        rect.bottom = 0;
      }

      RETURN_IF_ERROR(WriteType<Rect>(rect, memory::kSystemMemory, rect_ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-87.html
    case Trap::OffsetRect: {
      auto dv = TRY(Pop<int16_t>());
      auto dh = TRY(Pop<int16_t>());
      auto rect_ptr = TRY(Pop<Ptr>());
      auto rect = TRY(ReadType<Rect>(memory::kSystemMemory, rect_ptr));
      LOG(INFO) << "TRAP OffsetRect(r: { " << rect << " } @ 0x" << std::hex
                << rect_ptr << std::dec << ", dh: " << dh << ", dv: " << dv
                << ")";
      rect.left += dh;
      rect.right += dh;
      rect.top += dv;
      rect.bottom += dv;

      RETURN_IF_ERROR(WriteType<Rect>(rect, memory::kSystemMemory, rect_ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-93.html
    case Trap::PtToAngle: {
      auto angle_var = TRY(Pop<Ptr>());
      auto pt = TRY(PopType<Point>());
      auto r = TRY(PopRef<Rect>());

      LOG(INFO) << "TRAP PtToAngle(r: { " << r << " }, pt: { " << pt
                << " }, VAR angle: 0x" << std::hex << angle_var << ")";

      // FIXME: Calculate the angle in degrees from 0 to 359.
      Integer angle = 0;
      RETURN_IF_ERROR(memory::kSystemMemory.Write<Integer>(angle_var, angle));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-50.html
    case Trap::LocalToGlobal: {
      auto pt_var = TRY(Pop<Ptr>());
      auto pt = TRY(ReadType<Point>(memory::kSystemMemory, pt_var));

      LOG(INFO) << "TRAP LocalToGlobal(VAR pt: { " << pt << " } @ 0x"
                << std::hex << pt_var << ")";

      auto offset = TRY(port::GetLocalToGlobalOffset());
      pt.x += offset.x;
      pt.y += offset.y;

      RETURN_IF_ERROR(memory::kSystemMemory.Write<Point>(pt_var, pt));
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

      uint32_t a5_world = m68k_get_reg(/*context=*/NULL, M68K_REG_A5);
      RETURN_IF_ERROR(memory::kSystemMemory.Write<uint32_t>(
          a5_world, htobe<Ptr>(globalPtr)));

      Rect screen_bounds;
      screen_bounds.top = 0;
      screen_bounds.left = 0;
      screen_bounds.bottom = 384;
      screen_bounds.right = 512;

      // FIXME: Add WARNING if `screenBits.baseAddr` is accessed
      QDGlobals qd_globals;
      qd_globals.screen_bits.bounds = screen_bounds;

      // `globalPtr` accounts for the size of `thePort` so must be added here
      RETURN_IF_ERROR(WriteType<QDGlobals>(
          qd_globals, memory::kSystemMemory,
          globalPtr - QDGlobals::fixed_size + sizeof(Ptr)));

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

    case Trap::GetNewWindow: {
      auto behind_window = TRY(Pop<Ptr>());
      auto window_storage = TRY(Pop<Ptr>());
      auto window_id = TRY(Pop<Integer>());
      LOG(INFO) << "TRAP GetNewWindow(id: " << window_id << ", wStorage: 0x"
                << std::hex << window_storage << ", behind: 0x" << behind_window
                << ")";

      auto resource_handle = resource_manager_.GetResource('WIND', window_id);
      auto resource_region =
          memory_manager_.GetRegionForHandle(resource_handle);
      auto resource = TRY(ReadType<WIND>(resource_region, /*offset=*/0));
      LOG(INFO) << "WIND: { " << resource << " }";

      // FIXME: Fill in the rest of the WindowRecord (including GrafPort!)
      WindowRecord record;
      record.window_kind = 8 /*userKind*/;
      record.is_visible = (resource.is_visible != 0);
      record.has_close = (resource.has_close != 0);
      record.reference_constant = resource.reference_constant;

      RETURN_IF_ERROR(WriteType<WindowRecord>(record, memory::kSystemMemory,
                                              window_storage));

      memory::LogRegionAccess(window_storage, WindowRecord::fixed_size,
                              /*on_read=*/true, /*on_write=*/true,
                              "WindowRecord");
      return TrapReturn<Ptr>(window_storage);
    }

    default:
      return absl::UnimplementedError(absl::StrCat(
          "Unimplemented Toolbox trap: '", GetTrapName(trap), "'"));
  }
}

}  // namespace trap
}  // namespace cyder