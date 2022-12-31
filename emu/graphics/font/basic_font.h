// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <string>

#include "emu/graphics/bitmap_image.h"
#include "emu/graphics/graphics_helpers.h"

extern uint8_t basic_font[128][8];

struct GlyphMetric {
  int start;
  int width;
  int follow;
};

extern struct GlyphMetric glyph_metrics[128];

namespace cyder {

// Draw |string| to |image| with the upper-left corner at (x, y).
// Returns the length in pixels of the |string|.
int DrawString(graphics::BitmapImage& image,
               absl::string_view string,
               int x,
               int y);

}  // namespace cyder
