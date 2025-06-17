// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <optional>
#include <tuple>

#include "absl/status/status.h"
#include "emu/event_manager.h"
#include "emu/graphics/bitmap_image.h"
#include "emu/memory/memory_manager.h"
#include "emu/menu_manager.h"
#include "emu/rsrc/resource_manager.h"
#include "emu/segment_loader.h"
#include "emu/window_manager.h"
#include "gen/trap_names.h"

struct SDL_Renderer;

namespace cyder {
namespace trap {

// Handles dispatching from emulated A-Traps to native code
class TrapManager {
 public:
  TrapManager(memory::MemoryManager& memory_manager,
              ResourceManager& resource_manager,
              SegmentLoader& segment_loader,
              EventManager& event_manager,
              MenuManager& menu_manager,
              WindowManager& window_manager,
              BitMap& screen_bits);

  std::optional<Trap> DispatchEmulatedSubroutine(uint32_t address);

  absl::Status PerformTrapEntry();
  Trap PerformTrapDispatch(uint16_t trap_index, bool is_toolbox);
  absl::Status PerformTrapExit();

  void SetTrapAddress(uint16_t trap, uint32_t address);

 private:
  absl::Status DispatchNativeSystemTrap(uint16_t trap);
  absl::Status DispatchNativeToolboxTrap(uint16_t trap);

  uint32_t GetTrapAddress(uint16_t trap);

  memory::MemoryManager& memory_manager_;
  ResourceManager& resource_manager_;
  SegmentLoader& segment_loader_;
  EventManager& event_manager_;
  MenuManager& menu_manager_;
  WindowManager& window_manager_;
  BitMap screen_bits_;

  std::map<uint16_t, uint32_t> patch_trap_addresses_;
};

}  // namespace trap
}  // namespace cyder