// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/menu_popup.h"

#include "absl/strings/str_format.h"
#include "emu/font/font.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace {

constexpr int16_t kMenuItemHeight = 12u;
constexpr int16_t kMenuItemGlyphWidth = 8u;
constexpr int16_t kMenuItemPaddingWidth = 6u;
constexpr int16_t kMenuItemPaddingHeight = 2u;

constexpr uint8_t kMenuPopUpPattern[8] = {0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kHighLightPattern[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                          0xFF, 0xFF, 0xFF, 0xFF};

// Minimum allowed distance between a menu item title and its keyboard shortcut
constexpr int kMinimumShortcutPadding = 12;

Rect GetPopUpBounds(const std::vector<MenuItemResource>& menu_items,
                    const Rect& anchor_rect) {
  int width = 0, height = 0;
  for (const auto& item : menu_items) {
    height += kMenuItemHeight;
    int item_width = item.title.size() * kMenuItemGlyphWidth;
    if (item.keyboard_shortcut != 0) {
      // The keyboard shortcut needs to maintain a minimum distance from
      // the longest item title and will always be displayed as two
      // characters: "⌘{item.keyboard_shortcut}"
      item_width += kMinimumShortcutPadding + (kMenuItemGlyphWidth * 2);
    }
    width = std::max(width, item_width);
  }
  return NewRect(anchor_rect.left, anchor_rect.bottom,
                 width + kMenuItemPaddingWidth * 2, height);
}

Rect GetMenuItemBounds(const Rect& popup_rect, int item_index) {
  Rect menu_item;
  menu_item.top = popup_rect.top + item_index * kMenuItemHeight;
  menu_item.bottom = menu_item.top + kMenuItemHeight;
  menu_item.left = popup_rect.left;
  menu_item.right = popup_rect.right;
  return menu_item;
}

bool IsMenuItemEnabled(const MenuResource& menu, int item_index) {
  // Contains if the `menu` itself is enabled then each of its items in order
  return menu.state_bit_field >> (item_index + 1) & 1;
}

bool IsMenuItemSeparator(const MenuItemResource& item) {
  // MenuItem's with a title which starts with "-" appear to be separators
  return !item.title.empty() && item.title[0] == '-';
}

}  // namespace

AutoHiliteRect::AutoHiliteRect(Rect rect, graphics::BitmapImage& screen)
    : rect_(std::move(rect)), screen_(screen) {
  screen_.FillRect(rect_, kHighLightPattern,
                   graphics::BitmapImage::FillMode::XOr);
}
AutoHiliteRect::~AutoHiliteRect() {
  screen_.FillRect(rect_, kHighLightPattern,
                   graphics::BitmapImage::FillMode::XOr);
}

MenuPopUp::MenuPopUp(graphics::BitmapImage& screen,
                     MenuResource menu,
                     std::vector<MenuItemResource> menu_items,
                     Rect anchor_rect)
    : screen_(screen),
      menu_(std::move(menu)),
      menu_items_(std::move(menu_items)),
      anchor_rect_(std::move(anchor_rect)),
      anchor_hilite_(anchor_rect_, screen_),
      popup_rect_(GetPopUpBounds(menu_items_, anchor_rect_)),
      saved_bitmap_(RectWidth(popup_rect_), RectHeight(popup_rect_)) {
  saved_bitmap_.CopyBitmap(screen_, popup_rect_, NormalizeRect(popup_rect_));

  screen_.FillRect(popup_rect_, kMenuPopUpPattern);
  screen_.FrameRect(popup_rect_, kHighLightPattern);

  int y_offset = popup_rect_.top;
  for (const auto& item : menu_items_) {
    if (IsMenuItemSeparator(item)) {
      // Draw a grey line _without_ overwritting the frame around the menu
      screen_.FillRow(y_offset + (kMenuItemHeight / 2), popup_rect_.left + 1,
                      popup_rect_.right - 1, /*pattern=*/0xAA);
    } else {
      SystemFont().DrawString(screen_, item.title,
                              popup_rect_.left + kMenuItemPaddingWidth,
                              y_offset + kMenuItemPaddingHeight);
      if (item.keyboard_shortcut != 0) {
        // See GetPopUpBounds() above for an explanation of this math :P
        SystemFont().DrawString(
            screen_, absl::StrFormat("&%c", (char)item.keyboard_shortcut),
            popup_rect_.right - kMenuItemPaddingWidth -
                (kMenuItemGlyphWidth * 2),
            y_offset + kMenuItemPaddingHeight);
      }
    }
    y_offset += kMenuItemHeight;
  }
}

MenuPopUp::~MenuPopUp() {
  // We do not want to invert this rect once |saved_bitmap_| is restored...
  hovered_rect_.reset();
  screen_.CopyBitmap(saved_bitmap_, NormalizeRect(popup_rect_), popup_rect_);
}

uint16_t MenuPopUp::GetHoveredMenuItem(int x, int y) {
  if (x < popup_rect_.left || x >= popup_rect_.right)
    return kNoMenuItem;

  if (y < popup_rect_.top || y >= popup_rect_.bottom)
    return kNoMenuItem;

  uint16_t item_index = (y - popup_rect_.top) / (kMenuItemHeight);

  if (!IsMenuItemEnabled(menu_, item_index)) {
    hovered_rect_.reset();
    return kNoMenuItem;
  }

  hovered_rect_ = absl::make_unique<AutoHiliteRect>(
      GetMenuItemBounds(popup_rect_, item_index), screen_);
  return (item_index + 1);
}

}  // namespace cyder
