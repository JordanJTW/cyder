#include "emu/trap/trap_manager.h"

#include <SDL.h>

#include <cstdint>
#include <iomanip>
#include <tuple>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/pict_v1.h"
#include "emu/graphics/quickdraw.h"
#include "emu/memory/memory_helpers.h"
#include "emu/memory/memory_map.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_helpers.h"
#include "gen/global_names.h"
#include "gen/trap_names.h"
#include "gen/typegen/typegen_prelude.h"
#include "resource.h"
#include "third_party/musashi/src/m68k.h"

extern bool single_step;

constexpr bool kVerboseLogTraps = false;

#define LOG_TRAP() LOG_IF(INFO, kVerboseLogTraps) << "TRAP "

namespace cyder {
namespace trap {
namespace {

using rsrcloader::GetTypeName;

constexpr uint8_t kBackgroundPattern[8] = {0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kForegroundPattern[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                           0xFF, 0xFF, 0xFF, 0xFF};

absl::Status HandleLoadSegmentTrap(SegmentLoader& segment_loader,
                                   Ptr& return_address) {
  uint16_t load_segment = TRY(Pop<uint16_t>());
  LOG_TRAP() << "LoadSeg(" << load_segment << ")";
  TRY(segment_loader.Load(load_segment));
  // The segment loader modifies the six byte entry for this segment in the
  // table so return to the begining of the entry (4 bytes behind the 2 byte
  // trap being executed):
  return_address -= 6;
  return absl::OkStatus();
}

absl::Status WithPort(std::function<absl::Status(GrafPort& the_port)> cb) {
  return WithType<GrafPort>(TRY(port::GetThePort()), std::move(cb));
}

}  // namespace

TrapManager::TrapManager(memory::MemoryManager& memory_manager,
                         ResourceManager& resource_manager,
                         SegmentLoader& segment_loader,
                         EventManager& event_manager,
                         MenuManager& menu_manager,
                         WindowManager& window_manager,
                         graphics::BitmapImage& bitmap_screen)
    : memory_manager_(memory_manager),
      resource_manager_(resource_manager),
      segment_loader_(segment_loader),
      event_manager_(event_manager),
      menu_manager_(menu_manager),
      window_manager_(window_manager),
      screen_(bitmap_screen) {}

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

  uint16_t trap_op = TRY(memory::kSystemMemory.Read<uint16_t>(instruction_ptr));

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

  LOG_IF(INFO, kVerboseLogTraps)
      << COLOR(160) << "A-Line Exception "
      << (IsToolbox(trap_op) ? "Toolbox" : "OS") << "::" << GetTrapName(trap_op)
      << " (0x" << std::hex << trap_op << ") Index: " << std::dec
      << ExtractIndex(trap_op) << COLOR_RESET();

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

  LOG(INFO) << "Patch trap: '" << GetTrapName(trap) << "' (0x" << std::hex
            << trap << ") -> 0x" << address;
}

absl::Status TrapManager::DispatchNativeSystemTrap(uint16_t trap) {
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
    // FIXME: Should "SYS" pointers be allocated differently?
    case Trap::NewPtrSys: {
      // D0 seems to contain the argument in a sample program...
      // but the documentation says it should be in A0.
      uint32_t logical_size = m68k_get_reg(NULL, M68K_REG_D0);
      LOG_TRAP() << "NewPtr(logicalSize: " << logical_size << ")";
      auto ptr = memory_manager_.Allocate(logical_size);
      m68k_set_reg(M68K_REG_A0, ptr);
      // FIXME: Set `memFullErr` if memory could not be allocated
      m68k_set_reg(M68K_REG_D0, 0 /* noErr */);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Memory/Memory-21.html
    case Trap::NewHandle:
    case Trap::NewHandleClear: {
      // D0 seems to contain the argument in a sample program...
      // but the documentation says it should be in A0.
      uint32_t logical_size = m68k_get_reg(NULL, M68K_REG_D0);
      LOG_TRAP() << "NewHandle(logicalSize: " << logical_size << ")";
      auto handle = memory_manager_.AllocateHandle(logical_size, "NewHandle");
      m68k_set_reg(M68K_REG_A0, handle);
      m68k_set_reg(M68K_REG_D0, /*noErr*/ 0);
      return absl::OkStatus();
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

    // =======================  Trap Manager  ====================

    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-175.html
    case Trap::GetOSTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = ExtractSystemIndex(trap_index & 0xFFFF);
      LOG_TRAP() << "GetOSTrapAddress(trap: '"
                 << GetTrapNameBySystemIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-176.html
    case Trap::GetToolBoxTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = ExtractToolboxIndex(trap_index & 0xFFFF);
      LOG_TRAP() << "GetToolBoxTrapAddress(trap: '"
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
      LOG_TRAP() << "GetTrapAddress(trap: '" << GetTrapName(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-186.html
    case Trap::SetTrapAddress: {
      uint32_t trap_address = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFF;
      LOG_TRAP() << "SetTrapAddress(trapAddr: 0x" << std::hex << trap_address
                 << ", trap: '" << GetTrapName(trap_index) << "')";

      SetTrapAddress(trap_index, trap_address);
      return absl::OkStatus();
    }
      // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-180.html
    case Trap::SetToolBoxTrapAddress: {
      uint32_t trap_address = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFF;
      LOG_TRAP() << "SetToolboxTrapAddress(trapAddr: 0x" << std::hex
                 << trap_address << ", trap: '" << GetTrapName(trap_index)
                 << "')";

      // FIXME: This assumes the |trap_index| is a full A-Trap but only the
      //        index is also valid
      SetTrapAddress(trap_index, trap_address);
      return absl::OkStatus();
    }

    // =====================  Event Manager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-56.html
    case Trap::FlushEvents: {
      uint32_t arguments = m68k_get_reg(NULL, M68K_REG_D0);
      uint16_t eventMask = arguments & 0xFFFF;
      uint16_t stopMask = (2 >> arguments) & 0xFFFF;

      LOG_TRAP() << "FlushEvents(eventMask: 0x" << std::hex << std::setfill('0')
                 << std::setw(4) << eventMask << ", stopMask: 0x"
                 << std::setfill('0') << std::setw(4) << stopMask << ")";
      return absl::OkStatus();
    }

    // =====================  File Manager  ========================

    // Link: https://dev.os9.ca/techpubs/mac/Files/Files-232.html#HEADING232-0
    case Trap::Open: {
      Ptr ptr = m68k_get_reg(NULL, M68K_REG_A0);
      LOG_TRAP() << "Open(ptr: 0x" << std::hex << ptr << ")";
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
      LOG_TRAP() << "Button()";
      RETURN_IF_ERROR(TrapReturn<uint16_t>(0x0000));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-72.html
    case Trap::GetMouse: {
      auto mouse_location_var = TRY(Pop<Ptr>());

      LOG_TRAP() << "GetMouse(VAR mouseLoc: 0x" << std::hex
                 << mouse_location_var << ")";

      int mouse_x = 0, mouse_y = 0;
      SDL_GetMouseState(&mouse_x, &mouse_y);

      // FIXME: Ensure it is safe to do these implicit bit-narrowing casts
      Point mouse_location;
      mouse_location.x = mouse_x;
      mouse_location.y = mouse_y;

      RETURN_IF_ERROR(WriteType<Point>(mouse_location, memory::kSystemMemory,
                                       mouse_location_var));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-51.html
    case Trap::WaitNextEvent: {
      auto mouse_region = TRY(Pop<Handle>());
      auto sleep = TRY(Pop<uint32_t>());
      auto the_event_var = TRY(Pop<Ptr>());
      auto event_mask = TRY(Pop<uint16_t>());

      LOG_TRAP() << "WaitNextEvent(eventMask: " << std::bitset<16>(event_mask)
                 << ", VAR theEvent: 0x" << std::hex << the_event_var
                 << ", sleep: " << std::dec << sleep << ", mouseRgn: 0x"
                 << std::hex << mouse_region << ")";

      auto event = event_manager_.GetNextEvent();

      // FIXME: Monitor the field accesses with RESTRICT_FIELD_ACCESS.
      RETURN_IF_ERROR(WriteType<EventRecord>(
          std::move(event), memory::kSystemMemory, the_event_var));

      if (event.what == 6 /*updateEvt*/) {
        RETURN_IF_ERROR(
            WithType<WindowRecord>(event.message, [&](WindowRecord& window) {
              SetStructRegionAndDrawFrame(screen_, window, memory_manager_);
              return absl::OkStatus();
            }));
      }

      return TrapReturn<uint16_t>(event.what != 0 /*nullEvent*/ ? 0xFFFF
                                                                : 0x0000);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-53.html
    case Trap::GetNextEvent: {
      auto the_event_var = TRY(Pop<Ptr>());
      auto event_mask = TRY(Pop<uint16_t>());

      LOG_TRAP() << "GetNextEvent(eventMask: " << std::bitset<16>(event_mask)
                 << ", VAR theEvent: 0x" << std::hex << the_event_var << ")";

      auto event = event_manager_.GetNextEvent();

      // FIXME: Monitor the field accesses with RESTRICT_FIELD_ACCESS.
      RETURN_IF_ERROR(WriteType<EventRecord>(
          std::move(event), memory::kSystemMemory, the_event_var));

      if (event.what == 6 /*updateEvt*/) {
        RETURN_IF_ERROR(
            WithType<WindowRecord>(event.message, [&](WindowRecord& window) {
              SetStructRegionAndDrawFrame(screen_, window, memory_manager_);
              return absl::OkStatus();
            }));
      }

      return TrapReturn<uint16_t>(event.what != 0 /*nullEvent*/ ? 0xFFFF
                                                                : 0x0000);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-80.html
    case Trap::TickCount: {
      // FIXME: Read from GlobalVar::Ticks which should be set every 1/60th secs
      static absl::Time boot_time = absl::Now();
      auto elapsed_time_ms = absl::ToInt64Milliseconds(absl::Now() - boot_time);
      uint32_t elapsed_ticks = elapsed_time_ms / 16;
      return TrapReturn<uint32_t>(elapsed_ticks);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-58.html
    case Trap::SystemTask: {
      return absl::OkStatus();
    }

    // ===================  Menu Manager  ======================

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-124.html
    case Trap::GetNewMBar: {
      auto menu_bar_id = TRY(Pop<Integer>());
      LOG_TRAP() << "GetNewMBar(menuBarID: " << menu_bar_id << ")";
      Handle handle = resource_manager_.GetResource('MBAR', menu_bar_id);

      auto menu_bar = TRY(ReadType<MenuBarResource>(
          memory_manager_.GetRegionForHandle(handle), 0));

      // FIXME: Should return a MenuList with MENU Handles saved
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
      LOG_TRAP() << "GetRMenu(menuID: " << menu_id << ")";

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
      LOG_TRAP() << "InsertMenu(beforeId: " << before_id << ", theMenu: 0x"
                 << std::hex << the_menu << ")";

      auto menu = TRY(ReadType<MenuResource>(
          memory_manager_.GetRegionForHandle(the_menu), 0));
      menu_manager_.InsertMenu(menu);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-127.html
    case Trap::SetMenuBar: {
      auto menu_list_handle = TRY(Pop<Handle>());
      LOG_TRAP() << "SetMenuBar(menuList: 0x" << menu_list_handle << ")";

      auto menu_list = TRY(memory_manager_.ReadTypeFromHandle<MenuBarResource>(
          menu_list_handle));

      // FIXME: The MENUs should not need to be loaded _again_ (see GetNewMBar)
      for (ResourceId id : menu_list.menus) {
        LOG(INFO) << "Menu Resource ID: " << id;
        Handle menu_handle = resource_manager_.GetResource('MENU', id);

        auto menu = TRY(ReadType<MenuResource>(
            memory_manager_.GetRegionForHandle(menu_handle), 0));

        menu_manager_.InsertMenu(menu);

        LOG(INFO) << "Menu {" << menu << "}";
        for (const auto& item : menu.items) {
          LOG(INFO) << "MenuItem {" << item << "}";
        }
      }
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-147.html
    case Trap::AppendResMenu: {
      auto the_type = TRY(Pop<ResType>());
      auto the_menu = TRY(Pop<Handle>());
      LOG_TRAP() << "AppendResMenu(theMenu: 0x" << std::hex << the_menu
                 << ", theType: " << GetTypeName(the_type) << ")";
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
      auto item_string_var = TRY(Pop<Ptr>());
      auto item = TRY(Pop<uint16_t>());
      auto the_menu = TRY(Pop<Handle>());

      CHECK_GT(item, 0) << "Menu item is not expected to be 0-indexed";

      LOG_TRAP() << "GetMenuItemText(theMenu: 0x" << std::hex << the_menu
                 << ", item: " << std::dec << item << ", VAR itemString: 0x"
                 << std::hex << item_string_var << ")";

      auto menu = TRY(ReadType<MenuResource>(
          memory_manager_.GetRegionForHandle(the_menu), 0));

      RETURN_IF_ERROR(WriteType<absl::string_view>(
          menu.items[item - 1].title, memory::kSystemMemory, item_string_var));

      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-133.html
    case Trap::MenuSelect: {
      auto start_pt = TRY(PopType<Point>());

      LOG_TRAP() << "MenuSelect(startPt: { " << start_pt << " })";

      menu_manager_.NativeMenuSelect(
          start_pt.x, start_pt.y, [](uint32_t selected) {
            CHECK(TrapReturn<uint32_t>(selected).ok());
          });

      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-136.html
    case Trap::HiliteMenu: {
      auto menu_id = TRY(Pop<Integer>());
      LOG_TRAP() << "HiliteMenu(menuId: " << menu_id << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-150.html
    case Trap::EnableItem: {
      auto item = TRY(Pop<int16_t>());
      auto the_menu_handle = TRY(Pop<Handle>());
      LOG_TRAP() << "EnableItem(theMenu: 0x" << std::hex << the_menu_handle
                 << ", item: " << std::dec << item << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-151.html
    case Trap::DisableItem: {
      auto item = TRY(Pop<int16_t>());
      auto the_menu_handle = TRY(Pop<Handle>());
      LOG_TRAP() << "DisableItem(theMenu: 0x" << std::hex << the_menu_handle
                 << ", item: " << std::dec << item << ")";
      return absl::OkStatus();
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
      auto port_var = TRY(Pop<Ptr>());
      LOG_TRAP() << "GetPort(VAR port: 0x" << std::hex << port_var << ")";

      RETURN_IF_ERROR(
          memory::kSystemMemory.Write<Ptr>(port_var, TRY(port::GetThePort())));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-37.html
    case Trap::SetPort: {
      auto port = TRY(Pop<Ptr>());
      LOG_TRAP() << "SetPort(port: 0x" << std::hex << port << ")";

      RETURN_IF_ERROR(port::SetThePort(port));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-32.html
    case Trap::OpenPort: {
      auto thePortPtr = TRY(Pop<GrafPtr>());
      LOG_TRAP() << "OpenPort(port: 0x" << std::hex << thePortPtr << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-392.html
    case Trap::HideCursor: {
      LOG_TRAP() << "HideCursor()";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-98.html
    case Trap::PaintRect: {
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "PaintRect(rect: " << rect << ")";

      rect = TRY(port::ConvertLocalToGlobal(rect));

      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      screen_.FillRect(rect, kForegroundPattern);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-99.html
    case Trap::FillRect: {
      auto pat = TRY(PopRef<Pattern>());
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "FillRect(rect: " << rect << ", pat: { " << pat << " })";

      rect = TRY(port::ConvertLocalToGlobal(rect));

      // TODO: The way Pattern is defined in typegen is ugly...
      uint8_t pattern[8];
      std::memcpy(pattern, &pat.lower, 4);
      std::memcpy(pattern + 4, &pat.upper, 4);

      screen_.FillRect(rect, pattern);
      return absl::OkStatus();
    }
    case Trap::FrameRect: {
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "FrameRect(rect: " << rect << ")";

      rect = TRY(port::ConvertLocalToGlobal(rect));

      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      screen_.FrameRect(rect, kForegroundPattern);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-100.html
    case Trap::EraseRect: {
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "EraseRect(rect: " << rect << ")";

      rect = TRY(port::ConvertLocalToGlobal(rect));
      // FIXME: Clear with the color set for QuickDraw (A5 World?)
      screen_.FillRect(rect, kBackgroundPattern);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-63.html
    case Trap::Random: {
      LOG_TRAP() << "Random()";
      // FIXME: Use the same algorithm used in Mac OS to generate rand()
      RETURN_IF_ERROR(TrapReturn<int16_t>(0xFFFF * rand()));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-110.html
    case Trap::PaintOval: {
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "PaintOval(rect: " << rect << ")";

      rect = TRY(port::ConvertLocalToGlobal(rect));
      // FIXME: Paint with the color set for QuickDraw (A5 World?)
      screen_.FillEllipse(rect, kForegroundPattern);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-112.html
    case Trap::EraseOval: {
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "EraseOval(rect: " << rect << ")";

      rect = TRY(port::ConvertLocalToGlobal(rect));
      // FIXME: Clear with the color set for QuickDraw (A5 World?)
      screen_.FillEllipse(rect, kBackgroundPattern);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-86.html
    case Trap::SetRect: {
      auto bottom = TRY(Pop<uint16_t>());
      auto right = TRY(Pop<uint16_t>());
      auto top = TRY(Pop<uint16_t>());
      auto left = TRY(Pop<uint16_t>());
      auto rect_ptr = TRY(Pop<Ptr>());
      LOG_TRAP() << "SetRect(r: 0x" << std::hex << rect_ptr << std::dec
                 << "top: " << top << ", left: " << left
                 << ", bottom: " << bottom << ", right: " << right << ")";
      struct Rect rect;
      rect.left = left;
      rect.top = top;
      rect.right = right;
      rect.bottom = bottom;
      RETURN_IF_ERROR(WriteType<Rect>(rect, memory::kSystemMemory, rect_ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-54.html
    case Trap::SetPt: {
      auto v = TRY(Pop<uint16_t>());
      auto h = TRY(Pop<uint16_t>());
      auto pt_var = TRY(Pop<Ptr>());
      return WithType<Point>(pt_var, [&](Point& pt) {
        LOG_TRAP() << "SetPt(VAR pt: { " << pt << " } @ 0x" << std::hex
                   << pt_var << ", h: " << std::dec << h << ",v: " << v << ")";

        pt.x = h;
        pt.y = v;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-88.html
    case Trap::InsetRect: {
      auto dv = TRY(Pop<int16_t>());
      auto dh = TRY(Pop<int16_t>());
      auto rect_ptr = TRY(Pop<Ptr>());
      auto rect = TRY(ReadType<Rect>(memory::kSystemMemory, rect_ptr));
      LOG_TRAP() << "InsetRect(r: { " << rect << " } @ 0x" << std::hex
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
      LOG_TRAP() << "OffsetRect(r: { " << rect << " } @ 0x" << std::hex
                 << rect_ptr << std::dec << ", dh: " << dh << ", dv: " << dv
                 << ")";

      rect = OffsetRect(rect, dh, dv);
      RETURN_IF_ERROR(WriteType<Rect>(rect, memory::kSystemMemory, rect_ptr));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-93.html
    case Trap::PtToAngle: {
      auto angle_var = TRY(Pop<Ptr>());
      auto pt = TRY(PopType<Point>());
      auto rect = TRY(PopRef<Rect>());

      LOG_TRAP() << "PtToAngle(rect: { " << rect << " }, pt: { " << pt
                 << " }, VAR angle: 0x" << std::hex << angle_var << ")";

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
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-50.html
    case Trap::LocalToGlobal: {
      auto pt_var = TRY(Pop<Ptr>());
      auto pt = TRY(ReadType<Point>(memory::kSystemMemory, pt_var));

      LOG_TRAP() << "LocalToGlobal(VAR pt: { " << pt << " } @ 0x" << std::hex
                 << pt_var << ")";

      auto offset = TRY(port::GetLocalToGlobalOffset());
      pt.x += offset.x;
      pt.y += offset.y;

      RETURN_IF_ERROR(WriteType<Point>(pt, memory::kSystemMemory, pt_var));
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-81.html
    case Trap::MoveTo: {
      auto v = TRY(Pop<Integer>());
      auto h = TRY(Pop<Integer>());
      LOG_TRAP() << "MoveTo(h: " << h << ", v: " << v << ")";

      return WithPort([h, v](GrafPort& port) {
        port.pen_location.x = h;
        port.pen_location.y = v;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-101.html
    case Trap::InverRect: {
      auto rect = TRY(PopRef<Rect>());
      LOG_TRAP() << "InvertRect(r: { " << rect << " })";

      screen_.FillRect(TRY(port::ConvertLocalToGlobal(rect)),
                       kForegroundPattern,
                       graphics::BitmapImage::FillMode::XOr);

      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-89.html
    case Trap::SectRect: {
      auto dst_rect_var = TRY(Pop<Ptr>());
      auto src2 = TRY(PopRef<Rect>());
      auto src1 = TRY(PopRef<Rect>());

      LOG_TRAP() << "SectRect(src1: { " << src1 << " }, src2: { " << src2
                 << " }, VAR dstRect: 0x" << dst_rect_var << ")";

      auto rect = IntersectRect(src1, src2);
      RETURN_IF_ERROR(
          WriteType<Rect>(rect, memory::kSystemMemory, dst_rect_var));
      return TrapReturn<uint16_t>(IsZeroRect(rect) ? 0x0000 : 0xFFFF);
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-94.html
    case Trap::EqualRect: {
      auto rect2 = TRY(PopRef<Rect>());
      auto rect1 = TRY(PopRef<Rect>());
      LOG_TRAP() << "EqualRect(rect1: { " << rect1 << " }, rect2: { " << rect2
                 << " })";
      return TrapReturn<uint16_t>(EqualRect(rect1, rect2) ? 0xFFFF : 0x0000);
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-385.html
    case Trap::SetCursor: {
      auto crsr = TRY(PopRef<Cursor>());
      LOG_TRAP() << "SetCursor(crsr: { " << crsr << " })";
      // TODO: Allow alternative cursors to be displayed with SDL2
      return absl::OkStatus();
    }

    // ================== Resource Manager ==================

    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-53.html
    case Trap::Get1NamedResource: {
      auto name = TRY(PopRef<absl::string_view>());
      ResType type = TRY(Pop<ResType>());

      LOG_TRAP() << "Get1NamedResource(theType: '"
                 << rsrcloader::GetTypeName(type) << "', name: \"" << name
                 << "\")";

      Handle handle = resource_manager_.GetResourseByName(type, name);
      return TrapReturn<uint32_t>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-50.html
    case Trap::GetResource: {
      auto id = TRY(Pop<ResId>());
      auto type = TRY(Pop<ResType>());

      LOG_TRAP() << "GetResource(theType: '" << GetTypeName(type)
                 << "', theID: " << id << ")";

      Handle handle = resource_manager_.GetResource(type, id);
      return TrapReturn<uint32_t>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-56.html
    case Trap::LoadResource: {
      auto handle = TRY(Pop<uint32_t>());
      LOG_TRAP() << "LoadResource(theResource: 0x" << std::hex << handle << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-85.html
    case Trap::ReleaseResource: {
      auto handle = TRY(Pop<uint32_t>());
      LOG_TRAP() << "ReleaseResource(theResource: 0x" << std::hex << handle
                 << ")";
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-82.html
    case Trap::SizeRsrc: {
      auto handle = TRY(Pop<uint32_t>());
      LOG_TRAP() << "GetResourceSizeOnDisk(theResource: 0x" << std::hex
                 << handle << ")";
      // FIXME: This should read the size from disk not memory
      auto handle_size = memory_manager_.GetHandleSize(handle);
      return TrapReturn<uint32_t>(handle_size);
    }
    // Link: http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-60.html
    case Trap::GetResAttrs: {
      auto handle = TRY(Pop<uint32_t>());
      LOG_TRAP() << "GetResAttrs(theResource: 0x" << std::hex << handle << ")";
      // FIXME: Load the actual attributes from the resource...
      uint16_t attrs = 8;
      return TrapReturn<uint16_t>(attrs);
    }

    // ====================  Sound Manager  =====================

    // Link: http://0.0.0.0:8000/docs/mac/Sound/Sound-90.html
    case Trap::SysBeep: {
      uint16_t duration = TRY(Pop<Integer>());
      LOG_TRAP() << "SysBeep(duration: " << duration << ")";
      return absl::OkStatus();
    }

    // =====================  Initializers  =====================

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-30.html
    case Trap::InitGraf: {
      auto globalPtr = TRY(Pop<Ptr>());
      LOG_TRAP() << "InitGraf(globalPtr: 0x" << std::hex << globalPtr << ")";

      uint32_t a5_world = m68k_get_reg(/*context=*/NULL, M68K_REG_A5);
      RETURN_IF_ERROR(
          memory::kSystemMemory.Write<uint32_t>(a5_world, globalPtr));

      Rect screen_bounds;
      screen_bounds.top = 0;
      screen_bounds.left = 0;
      screen_bounds.bottom = 384;
      screen_bounds.right = 512;

      QDGlobals qd_globals;
      qd_globals.screen_bits.bounds = screen_bounds;
      qd_globals.grey.upper = 0xAA55AA55;
      qd_globals.grey.lower = 0xAA55AA55;

      // `globalPtr` accounts for the size of `thePort` so must be added here
      size_t qd_ptr = globalPtr - QDGlobals::fixed_size + sizeof(Ptr);
      RETURN_IF_ERROR(
          WriteType<QDGlobals>(qd_globals, memory::kSystemMemory, qd_ptr));

      RESTRICT_FIELD_ACCESS(
          QDGlobals, qd_ptr, QDGlobalsFields::random_seed,
          // TODO: Figure out a more elegant way to allow access to Rects
          QDGlobalsFields::screen_bits + BitMapFields::bounds,
          QDGlobalsFields::screen_bits + BitMapFields::bounds + 2,
          QDGlobalsFields::screen_bits + BitMapFields::bounds + 4,
          QDGlobalsFields::screen_bits + BitMapFields::bounds + 6,
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
      auto resumeProc = TRY(Pop<Ptr>());
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
      auto behind_window = TRY(Pop<Ptr>());
      auto window_storage = TRY(Pop<Ptr>());
      auto window_id = TRY(Pop<Integer>());
      LOG_TRAP() << "GetNewWindow(id: " << window_id << ", wStorage: 0x"
                 << std::hex << window_storage << ", behind: 0x"
                 << behind_window << ")";

      auto resource_handle = resource_manager_.GetResource('WIND', window_id);
      auto resource_region =
          memory_manager_.GetRegionForHandle(resource_handle);
      auto resource = TRY(ReadType<WIND>(resource_region, /*offset=*/0));
      LOG(INFO) << "WIND: { " << resource << " }";

      window_storage = TRY(window_manager_.NewWindow(
          window_storage, resource.initial_rect, resource.title,
          resource.is_visible != 0, resource.has_close != 0,
          resource.window_definition_id, behind_window,
          resource.reference_constant));

      // Focus (activate) and update the most recently created window
      event_manager_.QueueWindowActivate(window_storage);
      event_manager_.QueueWindowUpdate(window_storage);

      return TrapReturn<Ptr>(window_storage);
    }
      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-228.html
    case Trap::NewWindow: {
      auto reference_constant = TRY(Pop<uint32_t>());
      auto go_away_flag = TRY(Pop<uint16_t>());
      auto behind_window = TRY(Pop<Ptr>());
      auto window_definition_id = TRY(Pop<int16_t>());
      auto visible = TRY(Pop<uint16_t>());
      auto title = TRY(PopRef<std::string>());
      auto bounds_rect = TRY(PopRef<Rect>());
      auto window_storage = TRY(Pop<Ptr>());

      LOG_TRAP() << "NewWindow(wStorage: 0x" << std::hex << window_storage
                 << ", boundsRect: { " << std::dec << bounds_rect
                 << " }, title: '" << title
                 << "', visible: " << (visible ? "True" : "False")
                 << ", theProc: 0x" << std::hex << window_definition_id
                 << ", behind: 0x" << behind_window
                 << ", goAwayFlog: " << (go_away_flag ? "True" : "False")
                 << ", refCon: 0x" << reference_constant << ")";

      window_storage = TRY(window_manager_.NewWindow(
          window_storage, bounds_rect, title, visible != 0, go_away_flag != 0,
          window_definition_id, behind_window, reference_constant));

      // Focus (activate) and update the most recently created window
      event_manager_.QueueWindowActivate(window_storage);
      event_manager_.QueueWindowUpdate(window_storage);

      return TrapReturn<Ptr>(window_storage);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-258.html
    case Trap::DisposeWindow: {
      auto the_window = TRY(Pop<Ptr>());
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
      auto the_window_var = TRY(Pop<Ptr>());
      auto the_point = TRY(PopType<Point>());

      LOG_TRAP() << "FindWindow(thePoint: { " << the_point
                 << " }, VAR theWindow: 0x" << std::hex << the_window_var
                 << ")";

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
        case WindowManager::RegionType::None:
          return TrapReturn<int16_t>(0 /*inDesk*/);
      }
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-270.html
    case Trap::GetWRefCon: {
      auto the_window = TRY(PopRef<WindowRecord>());
      LOG_TRAP() << "GetWRefCon(theWindow: { " << std::hex << the_window
                 << " })";
      return TrapReturn<uint32_t>(the_window.reference_constant);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-269.html
    case Trap::SetWRefCon: {
      auto data = TRY(Pop<uint32_t>());
      auto the_window_ptr = TRY(Pop<Ptr>());
      auto the_window =
          TRY(ReadType<WindowRecord>(memory::kSystemMemory, the_window_ptr));

      LOG_TRAP() << "SetWRefCon(theWindow: { " << the_window << " } @ 0x"
                 << std::hex << the_window_ptr << ", data: " << std::dec << data
                 << ")";

      the_window.reference_constant = data;
      RETURN_IF_ERROR(WriteType<WindowRecord>(the_window, memory::kSystemMemory,
                                              the_window_ptr));
      return absl::OkStatus();
    }

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-260.html
    case Trap::BeginUpDate: {
      auto the_window = TRY(Pop<Ptr>());
      LOG_TRAP() << "BeginUpdate(theWindow: 0x" << std::hex << the_window
                 << ")";
      return absl::OkStatus();
    }

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-153.html
    case Trap::FillRgn: {
      auto pattern = TRY(PopRef<Pattern>());
      auto region_handle = TRY(Pop<Handle>());

      auto region_ptr = memory_manager_.GetPtrForHandle(region_handle);
      auto region = TRY(ReadType<Region>(memory::kSystemMemory, region_ptr));

      LOG_TRAP() << "FillRgn(region: { " << region << " } @ 0x" << std::hex
                 << region_handle << ", pattern: { " << pattern << " })";

      auto region_frame = TRY(port::ConvertLocalToGlobal(region.bounding_box));
      screen_.FillRect(region_frame, kBackgroundPattern);
      return absl::OkStatus();
    }

    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-350.html
    case Trap::DrawPicture: {
      auto dst_rect = TRY(PopRef<Rect>());
      auto my_picture = TRY(Pop<Handle>());

      LOG_TRAP() << "DrawPicture(myPicture: 0x" << std::hex << my_picture
                 << ", dstRect: { " << std::dec << dst_rect << " })";

      auto pict_data = memory_manager_.GetRegionForHandle(my_picture);

      auto pict_frame = TRY(graphics::GetPICTFrame(pict_data));

      size_t picture_size =
          PixelWidthToBytes(pict_frame.right) * pict_frame.bottom;
      uint8_t picture[picture_size];
      std::memset(picture, 0, picture_size);

      RETURN_IF_ERROR(graphics::ParsePICTv1(pict_data, /*output=*/picture));

      auto offset = TRY(port::GetLocalToGlobalOffset());
      auto target_rect = OffsetRect(dst_rect, offset.x, offset.y);

      screen_.CopyBits(picture, pict_frame, pict_frame, target_rect);
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/QuickDraw/QuickDraw-351.html
    case Trap::GetPicture: {
      auto pict_id = TRY(Pop<Integer>());
      LOG_TRAP() << "GetPicture(picId: " << pict_id << ")";

      Handle handle = resource_manager_.GetResource('PICT', pict_id);
      return TrapReturn<Handle>(handle);
    }

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-260.html
    case Trap::EndUpDate: {
      auto the_window = TRY(Pop<Ptr>());
      LOG_TRAP() << "EndUpdate(theWindow: 0x" << std::hex << the_window << ")";
      return absl::OkStatus();
    }

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-245.html
    case Trap::DragWindow: {
      auto bounds_rect = TRY(PopRef<Rect>());
      auto start_pt = TRY(PopType<Point>());
      auto the_window = TRY(Pop<Ptr>());

      LOG_TRAP() << "DragWindow(theWindow: 0x" << std::hex << the_window
                 << ", startPt: { " << std::dec << start_pt
                 << " }, boundsRect: { " << bounds_rect << " })";

      window_manager_.NativeDragWindow(the_window, start_pt.x, start_pt.y);
      return absl::OkStatus();
    }

    // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-402.html
    case Trap::GetNewDialog: {
      auto behind_window = TRY(Pop<Ptr>());
      auto dialog_storage = TRY(Pop<Ptr>());
      auto dialog_id = TRY(Pop<Integer>());

      LOG_TRAP() << "GetNewDialog(dialogId: " << dialog_id << ", dStorage: 0x"
                 << std::hex << dialog_storage << ", behind: 0x"
                 << behind_window << ")";

      auto dialog_handle = resource_manager_.GetResource('DLOG', dialog_id);
      auto dialog_resource =
          TRY(memory_manager_.ReadTypeFromHandle<DLOG>(dialog_handle));
      LOG(INFO) << "DLOG: { " << dialog_resource << " }";

      auto item_list_handle =
          resource_manager_.GetResource('DITL', dialog_resource.item_list_id);

      core::MemoryReader item_list(
          memory_manager_.GetRegionForHandle(item_list_handle));

      // Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-438.html
      uint16_t item_count = TRY(item_list.Next<uint16_t>());
      for (uint16_t i = 0; i <= item_count; ++i) {
        item_list.SkipNext(4);  // reserved
        LOG(INFO) << "Item #" << (i + 1);
        LOG(INFO) << "Rect: { " << TRY(item_list.NextType<Rect>()) << " }";
        LOG(INFO) << "Item Type: " << TRY(item_list.Next<uint8_t>());
        // Requires escaping due to carriage return ('\r') escape codes
        LOG(INFO) << "Text: "
                  << absl::CEscape(
                         TRY(item_list.NextType<absl::string_view>()));
        item_list.AlignTo(2);
      }

      return absl::UnimplementedError("WIP: partial implementation");
    }

    // ======================  Text Manager  =======================

    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-149.html
    case Trap::TextFont: {
      auto font = TRY(Pop<Integer>());
      LOG_TRAP() << "TextFont(font: " << font << ")";
      // We only support one bitmap font but nice of it to ask... :P
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-150.html
    case Trap::TextFace: {
      auto face = TRY(Pop<Integer>());
      LOG_TRAP() << "TextFace(face: " << face << ")";
      // We only support one bitmap font but nice of it to ask... :P
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-152.html
    case Trap::TextSize: {
      auto size = TRY(Pop<Integer>());
      LOG_TRAP() << "TextSize(size: " << size << ")";
      // We only support one bitmap font but nice of it to ask... :P
      return absl::OkStatus();
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-162.html
    case Trap::CharWidth: {
      // TODO: A single byte is based as a 16-bit value on the stack?
      auto ch = TRY(Pop<Integer>());
      LOG_TRAP() << "CharWidth(ch: '" << (char)ch << "')";
      return TrapReturn<Integer>(8 /*8x8 bitmap font*/);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-157.html
    case Trap::DrawChar: {
      // TODO: A single byte is based as a 16-bit value on the stack?
      auto ch = TRY(Pop<Integer>());
      LOG_TRAP() << "DrawChar(ch: '" << (char)ch << "')";

      return WithPort([&](GrafPort& port) {
        screen_.CopyBits(
            basic_font[ch], NewRect(0, 0, 8, 8), NewRect(0, 0, 8, 8),
            NewRect(port.pen_location.x - port.port_bits.bounds.left,
                    port.pen_location.y - port.port_bits.bounds.top - 8, 8, 8));
        port.pen_location.x += 8;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-158.html
    case Trap::DrawString: {
      auto str = TRY(PopRef<std::string>());
      LOG_TRAP() << "DrawString(str: " << str << ")";
      return WithPort([&](GrafPort& port) {
        graphics::TempClipRect _(
            screen_, OffsetRect(port.port_rect, -port.port_bits.bounds.left,
                                -port.port_bits.bounds.top));
        int width = DrawString(
            screen_, str, port.pen_location.x - port.port_bits.bounds.left,
            port.pen_location.y - port.port_bits.bounds.top - 8);
        port.pen_location.x += width;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-303.html
    case Trap::GetString: {
      auto string_id = TRY(Pop<Integer>());
      LOG_TRAP() << "GetString(stringID: " << string_id << ")";

      Handle handle = resource_manager_.GetResource('STR ', string_id);
      return TrapReturn<Handle>(handle);
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-155.html
    case Trap::GetFontInfo: {
      auto info_var = TRY(Pop<Ptr>());
      return WithType<FontInfo>(info_var, [&](FontInfo& info) {
        LOG_TRAP() << "GetFontInfo(VAR info: { " << info << " } @ 0x"
                   << std::hex << info_var << ")";
        // Only a fixed width 8x8 bitmap font is currently supported :P
        info.ascent = 8;
        info.descent = 0;
        info.widMax = 8;
        info.leading = 0;
        return absl::OkStatus();
      });
    }
    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-163.html
    case Trap::StringWidth: {
      auto str = TRY(PopRef<absl::string_view>());
      LOG_TRAP() << "StringWidth(s: '" << str << "')";
      // Only a fixed width 8x8 bitmap font is currently supported :P
      return TrapReturn<Integer>(str.size() * 8);
    }

    // =========================  TextEdit  ==========================

    // Link: http://0.0.0.0:8000/docs/mac/Text/Text-89.html
    case Trap::TETextBox: {
      auto align = TRY(Pop<int16_t>());
      auto box = TRY(PopRef<Rect>());
      auto length = TRY(Pop<uint32_t>());
      auto text_ptr = TRY(Pop<Ptr>());
      LOG_TRAP() << "TETextBox(text: 0x" << std::hex << text_ptr
                 << ", length: " << std::dec << length << ", box: { " << box
                 << " }, align: " << align << ")";

      box = TRY(port::ConvertLocalToGlobal(box));

      auto text = absl::string_view(
          reinterpret_cast<const char*>(memory::kSystemMemory.raw_ptr()) +
              text_ptr,
          length);

      // Only a 8x8 bitmap font is currently supported so assume 8px wide chars
      auto length_px = text.size() * 8;
      // Alignment Constants Link:
      //   http://0.0.0.0:8000/docs/mac/Text/Text-125.html#MARKER-9-577
      switch (align) {
        case 1: {  // teCenter
          int offset_x = (RectWidth(box) - length_px) / 2;
          int offset_y = (RectHeight(box) - 8) / 2;
          DrawString(screen_, text, box.left + offset_x, box.top + offset_y);
          break;
        }
        case -1:  // teFlushRight
          DrawString(screen_, text, box.right - length_px, box.top);
          break;
        case 0:   // teFlushDefault (assume Left-to-Right script)
        case -2:  // teFlushLeft
          DrawString(screen_, text, box.left, box.top);
          break;
      }
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Unimplemented Toolbox trap: '", GetTrapName(trap), "'"));
  }
}

}  // namespace trap
}  // namespace cyder