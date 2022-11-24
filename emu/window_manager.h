#pragma once

#include "emu/event_manager.h"
#include "emu/graphics/bitmap_screen.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/mouse_listener.h"
#include "emu/native_bridge.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class WindowManager : public MouseListener {
 public:
  WindowManager(NativeBridge& native_bridge,
                EventManager& event_manager,
                graphics::BitmapScreen& screen);

  void NativeDragWindow(Ptr window_ptr, int x, int y);

  // MouseListener implementation:
  void OnMouseMove(int x, int y);
  void OnMouseUp(int x, int y);

 private:
  NativeBridge& native_bridge_;
  EventManager& event_manager_;
  graphics::BitmapScreen& screen_;

  Ptr target_window_ptr_{0};
  WindowRecord target_window_;
};

}  // namespace cyder
