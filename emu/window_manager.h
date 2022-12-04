#pragma once

#include "emu/event_manager.h"
#include "emu/graphics/bitmap_image.h"
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
                graphics::BitmapImage& screen,
                memory::MemoryManager& memory);

  absl::StatusOr<Ptr> NewWindow(Ptr window_storage,
                                const Rect& bounds_rect,
                                std::string title,
                                bool is_visible,
                                bool has_close,
                                int16_t window_definition_id,
                                Ptr behind_window,
                                uint32_t reference_constant);

  void NativeDragWindow(Ptr window_ptr, int x, int y);

  bool CheckIsWindowDrag(const WindowRecord& window, int x, int y) const;

  enum class RegionType {
    None,
    Drag,
  };
  RegionType GetWindowAt(const Point& mouse, Ptr& target_window) const;

  // MouseListener implementation:
  void OnMouseMove(int x, int y);
  void OnMouseUp(int x, int y);

 private:
  void AddWindowToFront(Ptr window_ptr);
  // Reorders the window linked list so that |window_pt| comes first 
  void MoveToFront(Ptr window_ptr);
  // Invalidate windows back to front using the painter's algorithm
  void InvalidateWindows() const;
  void InvalidateWindowsImpl(Ptr window_ptr) const;

  NativeBridge& native_bridge_;
  EventManager& event_manager_;
  graphics::BitmapImage& screen_;
  memory::MemoryManager& memory_;

  Ptr front_window_{0};
  Ptr target_window_ptr_{0};
  WindowRecord target_window_;
  Point target_offset_;

  Rect outline_rect_;
  std::unique_ptr<graphics::BitmapImage> saved_bitmap_;
};

void SetStructRegionAndDrawFrame(graphics::BitmapImage& screen,
                                 WindowRecord& record,
                                 const memory::MemoryManager& memory);

}  // namespace cyder
