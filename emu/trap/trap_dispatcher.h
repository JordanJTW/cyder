// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

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

class TrapDispatcher {
 public:
  virtual ~TrapDispatcher() = default;

  virtual absl::Status Dispatch(uint16_t trap) = 0;
};

// Handles dispatching from emulated A-Traps to native code
class TrapDispatcherImpl : public TrapDispatcher {
 public:
  TrapDispatcherImpl(memory::MemoryManager& memory_manager,
                     ResourceManager& resource_manager,
                     EventManager& event_manager,
                     MenuManager& menu_manager,
                     WindowManager& window_manager,
                     BitMap& screen_bits);
  ~TrapDispatcherImpl() override = default;

  // TrapDispatcher implementation:
  absl::Status Dispatch(uint16_t trap) override;

 private:
  absl::Status DispatchNativeSystemTrap(uint16_t trap);
  absl::Status DispatchNativeToolboxTrap(uint16_t trap);

  memory::MemoryManager& memory_manager_;
  ResourceManager& resource_manager_;
  EventManager& event_manager_;
  MenuManager& menu_manager_;
  WindowManager& window_manager_;
  BitMap screen_bits_;

  Handle previous_clip_region_;
};

}  // namespace trap
}  // namespace cyder