#include "emu/menu_popup.h"

#include "emu/graphics/copybits.h"
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

}  // namespace

AutoHiliteRect::AutoHiliteRect(Rect rect, graphics::BitmapScreen& screen)
    : rect_(std::move(rect)), screen_(screen) {
  screen_.FillRect(rect_, kHighLightPattern,
                   graphics::BitmapScreen::FillMode::XOr);
}
AutoHiliteRect::~AutoHiliteRect() {
  screen_.FillRect(rect_, kHighLightPattern,
                   graphics::BitmapScreen::FillMode::XOr);
}

MenuPopUp::MenuPopUp(graphics::BitmapScreen& screen,
                     MenuResource menu,
                     Rect anchor_rect)
    : screen_(screen),
      menu_(std::move(menu)),
      anchor_rect_(std::move(anchor_rect)),
      anchor_hilite_(anchor_rect_, screen_),
      popup_rect_(GetPopUpBounds(menu_, anchor_rect_)),
      saved_bitmap_(absl::make_unique<uint8_t[]>(
          RectHeight(popup_rect_) *
          PixelWidthToBytes(RectWidth(popup_rect_)))) {
  int width = RectWidth(popup_rect_);
  int height = RectHeight(popup_rect_);

  // FIXME: Make BitmapScreen more generic and allow copying to/from?
  for (int row = 0; row < height; ++row) {
    int src_row_offset =
        PixelWidthToBytes(screen_.width()) * (popup_rect_.top + row);
    int dst_row_offset = PixelWidthToBytes(width) * row;

    bitarray_copy(screen_.bits() + src_row_offset,
                  /*src_offset=*/popup_rect_.left,
                  /*src_length=*/width, saved_bitmap_.get() + dst_row_offset,
                  /*dst_offset=*/0);
  }

  screen_.FillRect(popup_rect_, kMenuPopUpPattern);
  screen_.FrameRect(popup_rect_, kHighLightPattern);

  int y_offset = popup_rect_.top;
  for (const auto& item : menu_.items) {
    DrawString(screen_, item.title, popup_rect_.left + kMenuItemPaddingWidth,
               y_offset + kMenuItemPaddingHeight);
    y_offset += kMenuItemHeight;
  }
}

MenuPopUp::~MenuPopUp() {
  screen_.CopyBits(saved_bitmap_.get(), NormalizeRect(popup_rect_),
                   popup_rect_);
}

uint16_t MenuPopUp::GetHoveredMenuItem(int x, int y) {
  if (x < popup_rect_.left || x > popup_rect_.right)
    return kNoMenuItem;

  if (y < popup_rect_.top || y > popup_rect_.bottom)
    return kNoMenuItem;

  uint16_t item_index = (y - popup_rect_.top) / (kMenuItemHeight);
  hovered_rect_ = absl::make_unique<AutoHiliteRect>(
      GetMenuItemBounds(popup_rect_, item_index), screen_);
  return (item_index + 1);
}

}  // namespace cyder
