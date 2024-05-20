// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <list>

#include "emu/event_manager.h"
#include "emu/graphics/bitmap_image.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/memory/memory_manager.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

using WindowPtr = Ptr;

class WindowManager {
 public:
  WindowManager(EventManager& event_manager,
                graphics::BitmapImage& screen,
                memory::MemoryManager& memory);

  static WindowManager& the();

  absl::StatusOr<WindowRecord> NewWindowRecord(const Rect& bounds_rect,
                                               std::string title,
                                               bool is_visible,
                                               bool has_close,
                                               int16_t window_definition_id,
                                               WindowPtr behind_window,
                                               uint32_t reference_constant);

  absl::StatusOr<WindowPtr> NewWindow(Ptr window_storage,
                                      const Rect& bounds_rect,
                                      std::string title,
                                      bool is_visible,
                                      bool has_close,
                                      int16_t window_definition_id,
                                      WindowPtr behind_window,
                                      uint32_t reference_constant);

  void DisposeWindow(WindowPtr window_ptr);

  void DragWindow(WindowPtr window_ptr, const Point& start);

  enum MoveType {
    Relative,
    Absolute,
  };
  void MoveWindow(WindowPtr window_ptr,
                  MoveType move_type,
                  const Point& location,
                  bool bring_to_front);

  void SelectWindow(WindowPtr window_ptr);

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

  absl::Status ShowWindow(WindowPtr window);

 private:
  // Reorders the window linked list so that |window_pt| comes first
  void MoveToFront(Ptr window_ptr);
  // Invalidate windows back to front using the painter's algorithm
  void InvalidateWindows() const;
  // Repaint the desktop pattern where the window (and its frame) are
  void RepaintDesktopOverWindow(const WindowRecord& window);

  Rect GetRegionRect(Handle handle) const;

  EventManager& event_manager_;
  graphics::BitmapImage& screen_;
  memory::MemoryManager& memory_;

  std::list<Ptr> window_list_;

  Rect outline_rect_;
};

void DrawWindowFrame(const WindowRecord& window, graphics::BitmapImage& screen);
void UpdateWindowRegions(WindowRecord& record,
                         const memory::MemoryManager& memory);

}  // namespace cyder
