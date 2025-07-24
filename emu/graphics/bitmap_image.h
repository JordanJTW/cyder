// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/region.h"

namespace cyder {
namespace graphics {

extern uint8_t kBlackPattern[8];

// Represents a bitmap (1 bit-per-pixel) image.
// All coordinates are mapped from the upper-left hand corner at (0, 0).
class BitmapImage {
 public:
  using Scanline = std::vector<std::pair<int16_t, int16_t>>;

  // Constructs a BitmapImage pointing to `bitmap` in emulated memory
  // `memory_ptr` should be a ptr to the start of bitmap memory (on host)
  BitmapImage(BitMap bitmap, uint8_t* memory_ptr);
  // Allocates internal storage for a Bitmap with `width` and `height`
  BitmapImage(int width, int height);
  ~BitmapImage();

  // Get/set the global clip |region| within which drawing is allowed
  const region::OwnedRegion CopyClipRegion() const { return clip_region_; }
  void SetClipRegion(const region::Region& clip_region);

  enum class FillMode { Copy, XOr, NotXOr };

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
               Scanline scanline,
               uint8_t pattern,
               FillMode mode = FillMode::Copy);

  void FillRow(int row,
               int16_t start,
               int16_t end,
               uint8_t pattern,
               FillMode mode = FillMode::Copy);

  void FillRegion(const region::OwnedRegion& region,
                  const uint8_t pattern[8],
                  FillMode mode = FillMode::Copy);

  // Draw a 1-pixel border of |pattern[0]| within |rect|
  // FIXME: Support various "pen" widths and account for clipping
  void FrameRect(const Rect& rect,
                 const uint8_t pattern[8],
                 FillMode mode = FillMode::Copy);

  // Given a |src| bitmap image with dimensions |src_dims|, copy the area
  // |src_rect| to |dst_rect| with-in the current bitmap
  void CopyBits(const uint8_t* src,
                const Rect& src_dims,
                const Rect& src_rect,
                const Rect& dst_rect);

  void CopyBitmap(const BitmapImage& bitmap,
                  const Rect& src_rect,
                  const Rect& dst_rect);

  void PrintBitmap() const;
  void SaveBitmap(const std::string& path) const;

  int height() const { return height_; }
  int width() const { return width_; }

  const uint8_t* const bits() const { return bitmap_; }

 private:
  void _FillRow(int row,
                int16_t start,
                int16_t end,
                uint8_t pattern,
                FillMode mode = FillMode::Copy);

  const int width_;
  const int height_;
  const int bitmap_size_;
  const std::unique_ptr<uint8_t[]> bitmap_storage_;

  uint8_t* const bitmap_;

  region::OwnedRegion clip_region_;
};

// RAII class to temporarily override the clip rect then restore it
class TempClipRect final {
 public:
  TempClipRect(BitmapImage& screen, const region::Region& clip_region)
      : screen_(screen), saved_clip_region_(screen_.CopyClipRegion()) {
    screen_.SetClipRegion(clip_region);
  }
  ~TempClipRect() {
    screen_.SetClipRegion(region::ConvertRegion(saved_clip_region_));
  }

 private:
  BitmapImage& screen_;
  region::OwnedRegion saved_clip_region_;
};

}  // namespace graphics
}  // namespace cyder
