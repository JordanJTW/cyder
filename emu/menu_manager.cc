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

}  // namespace cyder