#pragma once

#include <functional>
#include <string>
#include <vector>

#include "emu/graphics/bitmap_screen.h"
#include "emu/menu_popup.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class MenuManager {
 public:
  explicit MenuManager(graphics::BitmapScreen& screen);

  void InsertMenu(MenuResource menu);
  void DrawMenuBar() const;

  bool IsInMenuBar(Point point) const;

  void NativeMenuSelect(int x,
                        int y,
                        std::function<void(uint32_t)> on_selected);

  void OnMouseMove(int x, int y);
  void OnMouseUp(int x, int y);

 private:
  graphics::BitmapScreen& screen_;
  std::vector<MenuResource> menus_;

  std::function<void(uint32_t)> on_selected_;
  std::unique_ptr<MenuPopUp> popup_menu_;
};

}  // namespace cyder
