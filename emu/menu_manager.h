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
 private:
  graphics::BitmapScreen& screen_;
  std::vector<MenuResource> menus_;
};

}  // namespace cyder
