#pragma once

#include <functional>
#include <string>
#include <vector>

#include "emu/graphics/bitmap_screen.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class MenuManager {
 public:
  MenuManager(graphics::BitmapScreen& screen);

  void InsertMenu(MenuResource menu);
  void DrawMenuBar() const;

  bool IsInMenuBar(Point point) const;

  void NativeMenuSelect(int x,
                        int y,
                        std::function<void(uint32_t)> on_selected);

  void OnMouseMove(int x, int y);
  void OnMouseUp(int x, int y);

 private:
  uint32_t GetSelected(int menu_index, uint16_t item_index);

  graphics::BitmapScreen& screen_;
  std::vector<MenuResource> menus_;

  std::function<void(uint32_t)> on_selected_;
  std::unique_ptr<uint8_t[]> previous_bitmap_;
  Rect previous_rect_;
};

}  // namespace cyder
