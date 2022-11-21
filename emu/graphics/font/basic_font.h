#pragma once

#include <cstdint>
#include <string>

#include "emu/graphics/bitmap_screen.h"
#include "emu/graphics/graphics_helpers.h"

extern uint8_t basic_font[128][8];

namespace cyder {

void DrawString(graphics::BitmapScreen& screen,
                const std::string& string,
                int x,
                int y);

}  // namespace cyder
