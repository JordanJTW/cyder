#pragma once

#include <memory>

#include "emu/graphics/bitmap_screen.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

// RAII class to invert |rect| on |screen| and undo the change on destructor
class AutoHiliteRect {
 public:
  AutoHiliteRect(Rect rect, graphics::BitmapScreen& screen);
  ~AutoHiliteRect();

 private:
  const Rect rect_;
  graphics::BitmapScreen& screen_;
};

// RAII class representing an on-screen |menu| pop-up anchored to an item in
// the menu bar (represented by |anchor_rect|). On destructor, screen will
// be restored to what was present under the pop-up on construction.
class MenuPopUp {
 public:
  MenuPopUp(graphics::BitmapScreen& screen,
            MenuResource menu,
            Rect anchor_rect);
  ~MenuPopUp();

  // Gets the index (1-based) of the item under the global screen coordinates
  // (|x|, |y|) or |kNoMenuItem|. Hilites the currently hovered menu item.
  static constexpr uint16_t kNoMenuItem = 0;
  uint16_t GetHoveredMenuItem(int x, int y);

  uint16_t id() const { return menu_.id; }

 private:
  graphics::BitmapScreen& screen_;
  MenuResource menu_;
  const Rect anchor_rect_;
  AutoHiliteRect anchor_hilite_;
  const Rect popup_rect_;

  std::unique_ptr<uint8_t[]> saved_bitmap_;
  std::unique_ptr<AutoHiliteRect> hovered_rect_;
};

}  // namespace cyder
