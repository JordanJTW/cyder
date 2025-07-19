// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/menu_manager.h"

#include "absl/strings/str_join.h"
#include "core/logging.h"
#include "emu/event_manager.h"
#include "emu/font/font.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace {

constexpr uint8_t kMenuPattern[8] = {0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kMenuIcon[32] = {
    0x00, 0x00, 0x00, 0x00, 0x07, 0xFE, 0x37, 0xFE, 0x3F, 0xFE, 0x37,
    0x9E, 0x37, 0x6E, 0x37, 0x7E, 0x37, 0x6E, 0x37, 0x9E, 0x3F, 0xFE,
    0x37, 0xFE, 0x07, 0xFE, 0x03, 0xFC, 0x01, 0xF8, 0x00, 0x00};
constexpr Rect kMenuIconRect = {0, 0, 16, 16};

constexpr int kMenuBarHeight = 20;
constexpr int kMenuBarWidthPadding = 6;
constexpr int kMenuBarItemWidthPadding = 4;
constexpr int kMenuBarItemHeightPadding = 6;

bool IsAppleMenu(const MenuResource& menu) {
  // The Apple menu's title should be just the "Apple" (code 0x14)
  return menu.title == "\x14";
}

}  // namespace

MenuManager::MenuManager(graphics::BitmapImage& screen) : screen_(screen) {}

void MenuManager::InsertMenu(MenuResource menu,
                             std::vector<MenuItemResource> menu_items) {
  // TODO: Should a duplicate inserted menu be ignored or replace existing menu?
  if (menu_items_.find(menu.id) != menu_items_.cend())
    return;

  menu_items_[menu.id] = std::move(menu_items);
  menus_.push_back(std::move(menu));
}

void MenuManager::DrawMenuBar() const {
  auto menu_rect = NewRect(0, 0, screen_.width(), kMenuBarHeight);
  screen_.FillRect(menu_rect, kMenuPattern);

  int x_offset = kMenuBarWidthPadding;
  for (auto& menu : menus_) {
    if (IsAppleMenu(menu)) {
      screen_.CopyBits(
          kMenuIcon, kMenuIconRect, kMenuIconRect,
          OffsetRect(kMenuIconRect, x_offset + kMenuBarItemWidthPadding,
                     (kMenuBarHeight - RectHeight(kMenuIconRect)) / 2));
      x_offset += RectWidth(kMenuIconRect) + (kMenuBarItemWidthPadding * 2);
    } else {
      SystemFont().DrawString(screen_, menu.title,
                              x_offset + kMenuBarItemWidthPadding,
                              kMenuBarItemHeightPadding);
      x_offset += (menu.title.size() * 8) + (kMenuBarItemWidthPadding * 2);
    }
  }
}

bool MenuManager::IsInMenuBar(const Point& point) const {
  return point.y < kMenuBarHeight;
}

uint32_t MenuManager::MenuSelect(const Point& start) {
  // Draws the menu bar with the mouse at |pt|.
  auto update_menu_bar = [this](const Point& pt) {
    int x_offset = kMenuBarWidthPadding;
    for (MenuResource& menu : menus_) {
      int menu_bar_item_width = (IsAppleMenu(menu) ? RectWidth(kMenuIconRect)
                                                   : menu.title.size() * 8) +
                                (kMenuBarItemWidthPadding * 2);

      int next_x_offset = x_offset + menu_bar_item_width;
      if (pt.x > x_offset && pt.x < next_x_offset && pt.y < kMenuBarHeight) {
        if (popup_menu_ && popup_menu_->id() == menu.id) {
          break;
        }

        // Needs to be cleared first so that the background bitmap is
        // restored in the RAII-types destructor before we create a new
        // one.
        popup_menu_.reset();

        popup_menu_ = absl::make_unique<MenuPopUp>(
            screen_, menu, menu_items_[menu.id],
            NewRect(x_offset, 0, menu_bar_item_width, kMenuBarHeight));
        break;
      }
      x_offset = next_x_offset;
    }
    if (popup_menu_)
      popup_menu_->GetHoveredMenuItem(pt.x, pt.y);
  };

  update_menu_bar(start);

  auto mouse_move_enabler = EventManager::the().EnableMouseMove();
  while (true) {
    EventRecord record =
        EventManager::the().GetNextEvent(1 << kMouseMove | 1 << kMouseUp);
    switch (record.what) {
      case kMouseMove: {
        update_menu_bar(record.where);
        break;
      }

      case kMouseUp: {
        // Once the mouse is released all popups should disappear and
        // selection should be finished. Transferring ownership of RAII-type
        // |popup_menu_| ensures it is _always_ cleaned up no matter which
        // return.
        std::unique_ptr<MenuPopUp> popup_menu = std::move(popup_menu_);

        if (!popup_menu) {
          return 0;
        }

        uint16_t item_index =
            popup_menu->GetHoveredMenuItem(record.where.x, record.where.y);
        if (item_index == MenuPopUp::kNoMenuItem) {
          return 0;
        }

        return (popup_menu->id() << 16 | item_index);
      }
    }
  }

  // Queue the `start` point to trigger the inital draw.
  EventManager::the().OnMouseMove(start.x, start.y);
}

}  // namespace cyder