// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/trap/trap_manager.h"

#include <cstdint>
#include <iomanip>
#include <tuple>

#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/emulator.h"
#include "emu/memory/memory_helpers.h"
#include "emu/memory/memory_map.h"
#include "emu/segment_loader.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_helpers.h"
#include "gen/global_names.h"
#include "gen/trap_names.h"
#include "third_party/musashi/src/m68k.h"

constexpr bool kVerboseLogTraps = false;

#define LOG_TRAP() LOG_IF(INFO, kVerboseLogTraps) << "TRAP "

namespace cyder {
namespace trap {
namespace {

void HandleLoadSegmentTrap(SegmentLoader& segment_loader, Ptr& return_address) {
  uint16_t load_segment = Pop<uint16_t>();
  LOG_TRAP() << "LoadSeg(" << load_segment << ")";
  MUST(segment_loader.Load(load_segment));
  // The segment loader modifies the six byte entry for this segment in the
  // table so return to the begining of the entry (4 bytes behind the 2 byte
  // trap being executed):
  return_address -= 6;
}

}  // namespace

TrapManager::TrapManager(SegmentLoader& segment_loader,
                         TrapDispatcher& trap_dispatcher)
    : segment_loader_(segment_loader), trap_dispatcher_(trap_dispatcher) {
  Emulator::Instance().RegisterATrapHandler([this]() {
    // The PC and SR have already been restored at this point (due to RTE).
    auto func_address = PerformTrapEntry();
    m68k_set_reg(M68K_REG_PC, func_address);
  });

  // Sets up native Toolbox trap handler function stubs. These allow chaining
  // from patched traps to the native trap handlers.
  for (int i = 0; i < 1024; ++i) {
    Emulator::Instance().RegisterNativeFunction(
        memory::kBaseToolboxTrapAddress + (i * sizeof(uint16_t)), [this, i]() {
          PerformTrapDispatch(0xA000 | i, /*is_toolbox=*/true);
          ReturnSubroutine();
        });
  }

  // Sets up native System trap handler function stubs. These allow chaining
  // from patched traps to the native trap handlers.
  for (int i = 0; i < 256; ++i) {
    Emulator::Instance().RegisterNativeFunction(
        memory::kBaseSystemTrapAddress + (i * sizeof(uint16_t)), [this, i]() {
          PerformTrapDispatch(0xA000 | i, /*is_toolbox=*/false);
          ReturnSubroutine();
        });
  }

  // Sets up the A-Trap exit handler. It is a mix of emulated and native code.
  CHECK_OK(memory::kSystemMemory.Write<uint16_t>(
      cyder::memory::kTrapManagerExitAddress, 0x4A40 /* TST.W D0 */));
  // Ensure the native funtion occures after the `TST.W D0` instruction.
  Emulator::Instance().RegisterNativeFunction(
      memory::kTrapManagerExitAddress + 2, [this]() {
        CHECK_OK(PerformTrapExit());
        ReturnSubroutine();
      });
}

uint32_t TrapManager::PerformTrapEntry() {
  // The emulator calls native functions AFTER the instruction has been
  // executed. This means that the instruction pointer is already at A-Trap
  // instruction that triggered the trap.
  uint32_t ip = m68k_get_reg(/*context=*/NULL, M68K_REG_PC);
  uint16_t trap_op = MUST(memory::kSystemMemory.Read<uint16_t>(ip));

  if (IsAutoPopSet(trap_op)) {
    // Clear the bit so that GetTrapName() works below
    // FIXME: GetTrapName() should ignore flags in the A-Trap
    trap_op = trap_op & ~(1 << 10);
    // If the "auto-pop bit" is set then the program has called a trap
    // indirectly (through a "glue subroutine"). We should return to
    // the JSR address instead of the instruction after the A-Trap.
    ip = Pop<uint32_t>();
  } else {
    // `ip` points to the address of the instruction that triggered the trap.
    // When we return from handling the trap return to the instruction past the
    // 16-bit A-Trap (i.e. + 2).
    ip += 2;
  }

  LOG_IF(INFO, kVerboseLogTraps)
      << COLOR(160) << "A-Line Exception "
      << (IsToolbox(trap_op) ? "Toolbox" : "OS") << "::" << GetTrapName(trap_op)
      << " (0x" << std::hex << trap_op << ") Index: " << std::dec
      << ExtractIndex(trap_op) << COLOR_RESET();

  CHECK_OK(Push<uint32_t>(ip));

  if (IsSystem(trap_op)) {
    if (ShouldSaveA0(trap_op)) {
      CHECK_OK(Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_A0)));
    }
    CHECK_OK(Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_A1)));
    CHECK_OK(Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_D1)));
    CHECK_OK(Push<uint32_t>(m68k_get_reg(/*context=*/NULL, M68K_REG_D2)));

    m68k_set_reg(M68K_REG_D1, trap_op);
  }

  auto patch_address = patch_trap_addresses_.find(trap_op);
  if (patch_address != patch_trap_addresses_.cend()) {
    LOG(INFO) << "Patched trap: '" << GetTrapName(trap_op) << "' (0x"
              << std::hex << trap_op << ") -> 0x" << patch_address->second;
    // Patched system traps still need to execute `PerformTrapExit()` to restore
    // the registes (and stack). Pushing the exit address emulates a `JSR` so
    // that TrapManager can complete the rest of its logic (the same as native).
    if (IsSystem(trap_op))
      CHECK_OK(Push<uint32_t>(memory::kTrapManagerExitAddress));

    // A patched trap address should end with an `RTS` instruction. So the PC
    // will be reset to `ip` when the patched trap address returns.
    return patch_address->second;
  }

  PerformTrapDispatch(ExtractIndex(trap_op), IsToolbox(trap_op));

  if (IsSystem(trap_op)) {
    return memory::kTrapManagerExitAddress;
  }

  uint32_t return_address = Pop<uint32_t>();
  CHECK_EQ(return_address, ip) << "Expected return address to be 0x" << std::hex
                               << ip << " but got 0x" << return_address;
  return return_address;
}

absl::Status TrapManager::PerformTrapExit() {
  uint32_t trap_op = m68k_get_reg(/*context=*/NULL, M68K_REG_D1);

  m68k_set_reg(M68K_REG_D2, Pop<uint32_t>());
  m68k_set_reg(M68K_REG_D1, Pop<uint32_t>());
  m68k_set_reg(M68K_REG_A1, Pop<uint32_t>());
  if (ShouldSaveA0(trap_op)) {
    m68k_set_reg(M68K_REG_A0, Pop<uint32_t>());
  }
  return absl::OkStatus();
}

void TrapManager::PerformTrapDispatch(uint16_t trap_index, bool is_toolbox) {
  uint16_t trap_op = 0xA000 | trap_index;
  if (is_toolbox) {
    trap_op |= (1 << 11);
  } else {
    trap_op = m68k_get_reg(/*context=*/NULL, M68K_REG_D1);
    CHECK_EQ(trap_index, ExtractIndex(trap_op));
  }

  // This must be removed from the stack so the arguments are at the top:
  Ptr return_address = Pop<Ptr>();

  // Handle _LoadSeg specially since it needs to modify the return address.
  if (Trap::LoadSeg == trap_op) {
    HandleLoadSegmentTrap(segment_loader_, return_address);
  } else if (InternalDispatch(trap_op)) {
    // If the trap pertains to the TrapManager itself it has been handled.
  } else {
    CHECK_OK(trap_dispatcher_.Dispatch(trap_op));
  }

  CHECK_OK(Push<Ptr>(return_address));
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

void TrapManager::PatchTrapsFromSystemFile(
    memory::MemoryManager& memory_manager,
    rsrc::ResourceFile& system_file) {
  if (const rsrc::Resource* version = system_file.FindByTypeAndId('STR ', 0)) {
    LOG(INFO) << "Loading PACK(s) from System: "
              << MUST(ReadType<absl::string_view>(version->GetData(), 0));
  }

  if (const rsrc::Resource* pack4 = system_file.FindByTypeAndId('PACK', 4)) {
    LOG(INFO) << "Loading PACK4 into memory";
    Handle handle =
        memory_manager.AllocateHandleForRegion(pack4->GetData(), "PACK4");
    size_t address = MUST(memory::kSystemMemory.Read<uint32_t>(handle));
    SetTrapAddress(Trap::Pack4, address);
  }

  if (const rsrc::Resource* pack7 = system_file.FindByTypeAndId('PACK', 7)) {
    LOG(INFO) << "Loading PACK7 into memory";
    Handle handle =
        memory_manager.AllocateHandleForRegion(pack7->GetData(), "PACK7");
    size_t address = MUST(memory::kSystemMemory.Read<uint32_t>(handle));
    SetTrapAddress(Trap::Pack7, address);
  }
}

bool TrapManager::InternalDispatch(uint16_t trap_op) {
  switch (trap_op) {
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-175.html
    case Trap::GetOSTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = ExtractSystemIndex(trap_index & 0xFFFF);
      LOG_TRAP() << "GetOSTrapAddress(trap: '"
                 << GetTrapNameBySystemIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return true;
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-176.html
    case Trap::GetToolBoxTrapAddress: {
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0);
      trap_index = ExtractToolboxIndex(trap_index & 0xFFFF);
      LOG_TRAP() << "GetToolBoxTrapAddress(trap: '"
                 << GetTrapNameByToolboxIndex(trap_index) << "')";
      m68k_set_reg(M68K_REG_A0, GetTrapAddress(trap_index));
      return true;
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
      return true;
    }
    // Link: http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-186.html
    case Trap::SetTrapAddress: {
      uint32_t trap_address = m68k_get_reg(NULL, M68K_REG_A0);
      uint32_t trap_index = m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFF;
      LOG_TRAP() << "SetTrapAddress(trapAddr: 0x" << std::hex << trap_address
                 << ", trap: '" << GetTrapName(trap_index) << "')";

      SetTrapAddress(trap_index, trap_address);
      return true;
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
      return true;
    }
    default:
      return false;
  }
}

}  // namespace trap
}  // namespace cyder