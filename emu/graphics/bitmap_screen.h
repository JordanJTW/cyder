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

  // Get/set the global clip |rect| within which drawing is allowed
  const Rect& GetClipRect() const { return clip_rect_; }
  void SetClipRect(const Rect& rect) { clip_rect_ = rect; }

  enum class FillMode { Copy, XOr };

  // Fill |rect| with the given bit |pattern|
  void FillRect(const Rect& rect,
                const uint8_t pattern[8],
                FillMode mode = FillMode::Copy);

  // Fill an ellipse contained within |rect| with bit |pattern|
  void FillEllipse(const Rect& rect, const uint8_t pattern[8]);

  // Fill the pixels from |start| to |end| on the given |row| with bit |pattern|
  void FillRow(int row,
               int16_t start,
               int16_t end,
               uint8_t pattern,
               FillMode mode = FillMode::Copy);

  // Draw a 1-pixel border of |pattern[0]| within |rect|
  // FIXME: Support various "pen" widths and account for clipping
  void FrameRect(const Rect& rect, const uint8_t pattern[8]);

  // Copy a bit image from |src| with dimensions |src_rect| to |dst_rect|
  void CopyBits(const uint8_t* src, const Rect& src_rect, const Rect& dst_rect);
  void PrintBitmap() const;

  int height() const { return height_; }
  int width() const { return width_; }

  const uint8_t* const bits() const { return bitmap_.get(); }

 private:
  const int width_;
  const int height_;
  const int bitmap_size_;
  const std::unique_ptr<uint8_t[]> bitmap_;

  Rect clip_rect_;
};

}  // namespace graphics
}  // namespace cyder
