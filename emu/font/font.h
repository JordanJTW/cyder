#pragma once

#include "emu/graphics/bitmap_image.h"

#include <cstdint>

namespace cyder {

// Represents a bitmap Font that can be drawn to the screen.
class Font {
 public:
  virtual ~Font() = default;

  // Draws |string| to |image| with the upper-left corner at (x, y).
  // Returns the length in pixels of the |string|.
  virtual int DrawString(graphics::BitmapImage& image,
                         absl::string_view string,
                         int x,
                         int y) = 0;

  // Draws |ch| to |image| with the upper-left corner at (x, y).
  // Returns the width of the glyph in pixels.
  virtual int DrawChar(graphics::BitmapImage& image, char ch, int x, int y) = 0;

  // Gets the width of |ch| in pixels as drawn by this font.
  virtual int GetCharWidth(char ch) = 0;
};

// The font used by native functions to draw to the screen.
Font& SystemFont();

// A built-in fixed width 8x8 font which does not rely on any resources.
Font& BuiltInFont();

// Loads a font from resources with the font-family `font_type`.
Font& GetFont(int16_t font_type);

}  // namespace cyder
