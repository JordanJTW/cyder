// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emu/graphics/bitmap_image.h"
#include "emu/menu_popup.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class MenuManager {
 public:
  MenuManager(graphics::BitmapImage& screen);

  void InsertMenu(MenuResource menu, std::vector<MenuItemResource> items);
  void DrawMenuBar() const;

  bool IsInMenuBar(const Point& point) const;

  using OnSelectedFunc = std::function<void(uint32_t)>;
  void MenuSelect(const Point& start, OnSelectedFunc on_selected);

 private:
  graphics::BitmapImage& screen_;
  std::vector<MenuResource> menus_;
  std::map<uint16_t, std::vector<MenuItemResource>> menu_items_;

  std::unique_ptr<MenuPopUp> popup_menu_;
};

}  // namespace cyder
