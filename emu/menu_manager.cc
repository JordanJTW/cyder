#include "emu/menu_manager.h"

#include "absl/strings/str_join.h"
#include "core/logging.h"
#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace {

constexpr uint8_t kMenuPattern[8] = {0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00};

constexpr int kMenuBarHeight = 20;
constexpr int kMenuBarWidthPadding = 3;
constexpr int kMenuBarItemWidthPadding = 3;
constexpr int kMenuBarItemHeightPadding = 6;

}  // namespace

MenuManager::MenuManager(graphics::BitmapImage& screen,
                         NativeBridge& native_bridge)
    : screen_(screen), native_bridge_(native_bridge) {}

void MenuManager::InsertMenu(MenuResource menu,
                             std::vector<MenuItemResource> menu_items) {
  menu_items_[menu.id] = std::move(menu_items);
  menus_.push_back(std::move(menu));
}

void MenuManager::DrawMenuBar() const {
  auto menu_rect = NewRect(0, 0, screen_.width(), kMenuBarHeight);
  screen_.FillRect(menu_rect, kMenuPattern);

  int x_offset = kMenuBarWidthPadding;
  for (auto& menu : menus_) {
    DrawString(screen_, menu.title, x_offset + kMenuBarItemWidthPadding,
               kMenuBarItemHeightPadding);
    x_offset += (menu.title.size() * 8) + (kMenuBarItemWidthPadding * 2);
  }
}

bool MenuManager::IsInMenuBar(Point point) const {
  return point.y < kMenuBarHeight;
}

void MenuManager::NativeMenuSelect(int x,
                                   int y,
                                   std::function<void(uint32_t)> on_selected) {
  on_selected_ = std::move(on_selected);
  native_bridge_.StartNativeMouseControl(this);
  OnMouseMove(x, y);
}

void MenuManager::OnMouseMove(int x, int y) {
  if (!on_selected_) {
    return;
  }

  int x_offset = kMenuBarWidthPadding;
  for (auto& menu : menus_) {
    int menu_bar_item_width =
        (menu.title.size() * 8) + (kMenuBarItemWidthPadding * 2);

    int next_x_offset = x_offset + menu_bar_item_width;
    if (x > x_offset && x < next_x_offset && y < kMenuBarHeight) {
      if (popup_menu_ && popup_menu_->id() == menu.id) {
        return;
      }

      // Needs to be cleared first so that the background bitmap is restored
      // in the RAII-types destructor before we create a new one.
      popup_menu_.reset();

      popup_menu_ = absl::make_unique<MenuPopUp>(
          screen_, menu, menu_items_[menu.id],
          NewRect(x_offset, 0, menu_bar_item_width, kMenuBarHeight));
      break;
    }
    x_offset = next_x_offset;
  }
  if (popup_menu_)
    popup_menu_->GetHoveredMenuItem(x, y);
}

void MenuManager::OnMouseUp(int x, int y) {
  if (!on_selected_) {
    return;
  }

  auto on_selected = std::move(on_selected_);
  on_selected_ = nullptr;

  // Once the mouse is released all popups should disappear and selection
  // should be finished. Transferring ownership of RAII-type |popup_menu_|
  // ensures it is _always_ cleaned up no matter which return.
  auto popup_menu = std::move(popup_menu_);

  if (!popup_menu) {
    return on_selected(0);
  }

  uint16_t item_index = popup_menu->GetHoveredMenuItem(x, y);
  if (item_index == MenuPopUp::kNoMenuItem) {
    return on_selected(0);
  }

  on_selected(popup_menu->id() << 16 | item_index);
}

}  // namespace cyder