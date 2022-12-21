#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emu/graphics/bitmap_image.h"
#include "emu/menu_popup.h"
#include "emu/mouse_listener.h"
#include "emu/native_bridge.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class MenuManager : public MouseListener {
 public:
  MenuManager(graphics::BitmapImage& screen, NativeBridge& native_bridge);

  void InsertMenu(MenuResource menu, std::vector<MenuItemResource> items);
  void DrawMenuBar() const;

  bool IsInMenuBar(Point point) const;

  void NativeMenuSelect(int x,
                        int y,
                        std::function<void(uint32_t)> on_selected);

  // MouseListener implementation:
  void OnMouseMove(int x, int y);
  void OnMouseUp(int x, int y);

 private:
  graphics::BitmapImage& screen_;
  NativeBridge& native_bridge_;
  std::vector<MenuResource> menus_;
  std::map<uint16_t, std::vector<MenuItemResource>> menu_items_;

  std::function<void(uint32_t)> on_selected_;
  std::unique_ptr<MenuPopUp> popup_menu_;
};

}  // namespace cyder
