// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <list>

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

  void DisposeWindow(Ptr window_ptr);

  void DragWindow(Ptr window_ptr, const Point& start);

  enum MoveType {
    Relative,
    Absolute,
  };
  void MoveWindow(Ptr window_ptr,
                  MoveType move_type,
                  const Point& location,
                  bool bring_to_front);

  void SelectWindow(Ptr window_ptr);

  void DragGrayRegion(const Region& region,
                      const Point& start,
                      std::function<void(const Point&)> on_drag_end);

  enum class RegionType {
    None,
    Drag,
    Content,
  };
  RegionType GetWindowAt(const Point& mouse, Ptr& target_window) const;

  Ptr GetFrontWindow() const;

  // MouseListener implementation:
  void OnMouseMove(const Point&);
  void OnMouseUp(const Point&);

 private:
  // Reorders the window linked list so that |window_pt| comes first
  void MoveToFront(Ptr window_ptr);
  // Invalidate windows back to front using the painter's algorithm
  void InvalidateWindows() const;
  // Repaint the desktop pattern where the window (and its frame) are
  void RepaintDesktopOverWindow(const WindowRecord& window);

  Rect GetRegionRect(Handle handle) const;

  NativeBridge& native_bridge_;
  EventManager& event_manager_;
  graphics::BitmapImage& screen_;
  memory::MemoryManager& memory_;

  Point target_offset_;

  std::list<Ptr> window_list_;

  Rect outline_rect_;
  Point start_pt_;
  std::unique_ptr<graphics::BitmapImage> saved_bitmap_;
  std::function<void(const Point&)> on_drag_end_;
};

void DrawWindowFrame(const WindowRecord& window, graphics::BitmapImage& screen);
void UpdateWindowRegions(WindowRecord& record,
                                 const memory::MemoryManager& memory);

}  // namespace cyder
