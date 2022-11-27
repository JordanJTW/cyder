#pragma once

#include <cstdint>
#include <string>

#include "emu/graphics/bitmap_image.h"
#include "emu/graphics/graphics_helpers.h"

extern uint8_t basic_font[128][8];

namespace cyder {

void DrawString(graphics::BitmapImage& image,
                const std::string& string,
                int x,
                int y);

}  // namespace cyder
