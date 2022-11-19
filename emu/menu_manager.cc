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

}  // namespace

MenuManager::MenuManager(graphics::BitmapScreen& screen) : screen_(screen) {}

void MenuManager::InsertMenu(MenuResource menu) {
  menus_.push_back(std::move(menu));
}

void MenuManager::DrawMenuBar() const {
  auto menu_rect = NewRect(0, 0, screen_.width(), kMenuBarHeight);
  screen_.FillRect(menu_rect, kMenuPattern);

  int x_offset = 6;
  for (auto& menu : menus_) {
    DrawString(screen_, menu.title, x_offset, 6);
    x_offset += (menu.title.size() * 8) + 6;
  }
}

bool MenuManager::IsInMenuBar(Point point) const {
  return point.y < kMenuBarHeight;
}

uint32_t MenuManager::GetSelected(int menu_index, uint16_t item_index) {
  return menus_[menu_index].id << 16 | item_index;
}

void MenuManager::NativeMenuSelect(int x,
                                   int y,
                                   std::function<void(uint32_t)> on_selected) {
  on_selected_ = std::move(on_selected);
  OnMouseMove(x, y);
}

void MenuManager::OnMouseMove(int x, int y) {
  if (!on_selected_ || y > kMenuBarHeight)
    return;

  int x_offset = 6;
  absl::optional<MenuResource> selected;
  for (auto& menu : menus_) {
    int new_offset = x_offset + (menu.title.size() * 8);
    if (x > x_offset && x < new_offset) {
      selected = menu;
      break;
    }
    x_offset = new_offset + 6;
  }

  if (!selected.has_value()) {
    DrawMenuBar();
    screen_.CopyBits(previous_bitmap_.get(), NormalizeRect(previous_rect_),
                     previous_rect_);
    return;
  }

  int width = 0, height = 0;
  for (const auto& item : selected->items) {
    height += 10;
    width = std::max(width, int(item.title.size() * 8));
  }

  screen_.CopyBits(previous_bitmap_.get(), NormalizeRect(previous_rect_),
                   previous_rect_);

  previous_rect_ = NewRect(x_offset, kMenuBarHeight, width, height);
  previous_bitmap_ = std::unique_ptr<uint8_t[]>(
      new uint8_t[height * PixelWidthToBytes(width)]);
  for (int x = 0; x < PixelWidthToBytes(width); ++x) {
    for (int y = 0; y < height; ++y) {
      previous_bitmap_[y * PixelWidthToBytes(width) + x] =
          screen_.bits()[(kMenuBarHeight + y) *
                             PixelWidthToBytes(screen_.width()) +
                         x + x_offset];
    }
  }

  screen_.FillRect(previous_rect_, kMenuPattern);

  int y_offset = kMenuBarHeight;
  for (const auto& item : selected->items) {
    DrawString(screen_, item.title, x_offset, y_offset);
    y_offset += 10;
  }

  auto string = absl::StrCat("(x: ", x, ", y: ", y, "): ", selected->id);
  DrawString(screen_, string, screen_.width() - (string.length() * 8) - 6, 6);
}

void MenuManager::OnMouseUp(int x, int y) {
  if (!on_selected_)
    return;

  if (y > kMenuBarHeight) {
    screen_.CopyBits(previous_bitmap_.get(), NormalizeRect(previous_rect_),
                     previous_rect_);
    auto on_selected = std::move(on_selected_);
    return on_selected(0);
  }

  auto on_selected = std::move(on_selected_);
  int x_offset = 6;
  for (auto& menu : menus_) {
    int new_offset = x_offset + (menu.title.size() * 8);
    if (x > x_offset && x < new_offset) {
      screen_.CopyBits(previous_bitmap_.get(), NormalizeRect(previous_rect_),
                       previous_rect_);
      return on_selected(menu.id << 16 | 1);
    }
    x_offset = new_offset + 6;
  }
  screen_.CopyBits(previous_bitmap_.get(), NormalizeRect(previous_rect_),
                   previous_rect_);
  on_selected(0);
}

}  // namespace cyder