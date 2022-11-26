#pragma once

#include "emu/event_manager.h"
#include "emu/graphics/bitmap_screen.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/memory/memory_manager.h"
#include "emu/mouse_listener.h"
#include "emu/native_bridge.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class WindowManager : public MouseListener {
 public:
  WindowManager(NativeBridge& native_bridge,
                EventManager& event_manager,
                graphics::BitmapScreen& screen,
                const memory::MemoryManager& memory);

  void NativeDragWindow(Ptr window_ptr, int x, int y);

  bool CheckIsWindowDrag(Ptr window_ptr, int x, int y) const;

  // MouseListener implementation:
  void OnMouseMove(int x, int y);
  void OnMouseUp(int x, int y);

 private:
  NativeBridge& native_bridge_;
  EventManager& event_manager_;
  graphics::BitmapScreen& screen_;
  const memory::MemoryManager& memory_;

  Ptr target_window_ptr_{0};
  WindowRecord target_window_;
  Point target_offset_;
  
  std::unique_ptr<uint8_t[]> saved_bitmap_;
  Rect outline_rect_;
};

void SetStructRegionAndDrawFrame(graphics::BitmapScreen& screen,
                                 WindowRecord& record,
                                 const memory::MemoryManager& memory);

}  // namespace cyder
