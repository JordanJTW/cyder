#include "emu/menu_popup.h"

#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace {

constexpr size_t kMenuItemHeight = 12u;
constexpr size_t kMenuItemGlyphWidth = 8u;
constexpr size_t kMenuItemPaddingWidth = 10u;
constexpr size_t kMenuItemPaddingHeight = 2u;

constexpr uint8_t kMenuPopUpPattern[8] = {0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kHighLightPattern[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                          0xFF, 0xFF, 0xFF, 0xFF};

Rect GetPopUpBounds(const MenuResource& menu, const Rect& anchor_rect) {
  int width = 0, height = 0;
  for (const auto& item : menu.items) {
    height += kMenuItemHeight;
    width = std::max(width, int(item.title.size() * kMenuItemGlyphWidth));
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
  return menu.state_bit_field & ((item_index + 1) << 1);
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
                     Rect anchor_rect)
    : screen_(screen),
      menu_(std::move(menu)),
      anchor_rect_(std::move(anchor_rect)),
      anchor_hilite_(anchor_rect_, screen_),
      popup_rect_(GetPopUpBounds(menu_, anchor_rect_)),
      saved_bitmap_(RectWidth(popup_rect_), RectHeight(popup_rect_)) {
  saved_bitmap_.CopyBitmap(screen_, popup_rect_, NormalizeRect(popup_rect_));

  screen_.FillRect(popup_rect_, kMenuPopUpPattern);
  screen_.FrameRect(popup_rect_, kHighLightPattern);

  int y_offset = popup_rect_.top;
  for (const auto& item : menu_.items) {
    if (IsMenuItemSeparator(item)) {
      // Draw a grey line _without_ overwritting the frame around the menu
      screen_.FillRow(y_offset + (kMenuItemHeight / 2), popup_rect_.left + 1,
                      popup_rect_.right - 1, /*pattern=*/0xAA);
    } else {
      DrawString(screen_, item.title, popup_rect_.left + kMenuItemPaddingWidth,
                 y_offset + kMenuItemPaddingHeight);
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
