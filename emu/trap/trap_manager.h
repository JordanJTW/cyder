// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <map>
#include <optional>

#include "absl/status/status.h"
#include "emu/memory/memory_manager.h"
#include "emu/rsrc/resource_file.h"
#include "emu/segment_loader.h"
#include "emu/trap/trap_dispatcher.h"

struct SDL_Renderer;

namespace cyder {
namespace trap {

// Handles dispatching from emulated A-Traps to native code
class TrapManager {
 public:
  TrapManager(SegmentLoader& segment_loader, TrapDispatcher& trap_dispatcher);

  // Patches PACK traps from the Mac OS `system_file` into the trap table.
  void PatchTrapsFromSystemFile(memory::MemoryManager& memory_manager,
                                rsrc::ResourceFile& system_file);

 private:
  // Gets the current address for the given `trap` handler.
  uint32_t GetTrapAddress(uint16_t trap);

  // Sets up the stack and calls the trap handler (either native or emulated).
  uint32_t PerformTrapEntry();
  // Handles restoring registers when returning from System traps.
  absl::Status PerformTrapExit();

  // Dispatches to a native trap handler for the given trap. Can be called from
  // emulated code (in the case of trap patches chaining to the original).
  void PerformTrapDispatch(uint16_t trap_index, bool is_toolbox);
  // Handles traps pertaining to the TrapManager itself. Prevents dependency
  // inversion with the TrapDispatcher.
  bool InternalDispatch(uint16_t trap_op);

  // Patches the trap `address` for the given `trap` number.
  void SetTrapAddress(uint16_t trap, uint32_t address);

  SegmentLoader& segment_loader_;
  TrapDispatcher& trap_dispatcher_;
  std::map<uint16_t, uint32_t> patch_trap_addresses_;
};

}  // namespace trap
}  // namespace cyder