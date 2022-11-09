#pragma once

#include <cstddef>
#include <cstdint>

#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {
namespace graphics {

// Represents a physical bitmap (1 bit-per-pixel) display to be drawn to.
// All coordinates are mapped from the upper-left hand corner at (0, 0).
class BitmapScreen {
 public:
  BitmapScreen(int width, int height);
  ~BitmapScreen();

  // Fill |rect| with the given bit |pattern|
  void FillRect(const Rect& rect, const uint8_t pattern[8]);

  // Fill an ellipse contained within |rect| with bit |pattern|
  void FillEllipse(const Rect& rect, const uint8_t pattern[8]);

  // Fill the pixels from |start| to |end| on the given |row| with bit |pattern|
  void FillRow(int row, int16_t start, int16_t end, uint8_t pattern);

  // Copy a bit image from |src| with dimensions |src_rect| to |dst_rect|
  void CopyBits(const uint8_t* src, const Rect& src_rect, const Rect& dst_rect);
  void PrintBitmap() const;

  const uint8_t* const bits() const { return bitmap_; }

 private:
  const int width_;
  const int height_;
  const int bitmap_size_;
  uint8_t* const bitmap_;
};

}  // namespace graphics
}  // namespace cyder
