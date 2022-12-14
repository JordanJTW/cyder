#include "emu/graphics/bitmap_image.h"

#include <bitset>
#include <fstream>

#include "core/logging.h"
#include "emu/graphics/copybits.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace graphics {
namespace {

static const bool kVerbose = false;

inline uint8_t RotateByteRight(uint8_t byte, uint16_t shift) {
  return (byte >> shift) | (byte << (CHAR_BIT - shift));
}

}  // namespace

BitmapImage::BitmapImage(int width, int height)
    : width_(width),
      height_(height),
      bitmap_size_(PixelWidthToBytes(width) * height),
      bitmap_(absl::make_unique<uint8_t[]>(bitmap_size_)) {
  std::memset(bitmap_.get(), 0, bitmap_size_);
  clip_rect_ = NewRect(0, 0, width, height);
}

BitmapImage::~BitmapImage() = default;

void BitmapImage::FillRect(const Rect& rect,
                           const uint8_t pattern[8],
                           FillMode mode) {
  // The pattern should align with the left side of the |rect| but may not
  // be byte aligned so we rotate the byte right by the offset to compensate
  int16_t pattern_offset = std::max((int16_t)0, rect.left) % 8;

  int16_t height = RectHeight(rect);
  for (int16_t row = 0; row < height; ++row) {
    uint8_t swatch = RotateByteRight(pattern[row % 8], pattern_offset);
    FillRow(rect.top + row, rect.left, rect.right, swatch, mode);
  }
}

void BitmapImage::FillEllipse(const Rect& rect, const uint8_t pattern[8]) {
  int half_width = RectWidth(rect) / 2;
  int half_height = RectHeight(rect) / 2;
  int origin_x = rect.left + half_width;
  int origin_y = rect.top + half_height;

  // Pre-calculate squares for formula below
  int hh = half_height * half_height;
  int ww = half_width * half_width;
  int hhww = hh * ww;

  int last_offset = half_width;
  int slope_dx = 0;

  // The pattern should align with the left side of the |rect| but may not
  // be byte aligned so we rotate the byte right by the offset to compensate
  int16_t pattern_offset = std::max((int16_t)0, rect.left) % 8;

  // Fill the horrizontal center row of the ellipse.
  // The rest of the ellipse is mirrored over this central line.
  FillRow(origin_y, rect.left, rect.right,
          RotateByteRight(pattern[half_height % 8], pattern_offset));

  for (int row = 1; row <= half_height; ++row) {
    // Calculate the new offset from the vertical center for each row
    // exploiting the fact that each new row will differ from the last
    // by at least the same slope line as the last (give or take 1 to
    // account for integer math).
    int offset = last_offset - (slope_dx - 1);
    for (; offset > 0; --offset) {
      if (offset * offset * hh + row * row * ww <= hhww)
        break;
    }

    // Fill rows mirrored over center line taking care to ensure the
    // fill pattern starts at the top of the ellipse and follows down.
    FillRow(origin_y - row, origin_x - offset, origin_x + offset,
            RotateByteRight(pattern[(half_height - row) % 8], pattern_offset));
    FillRow(origin_y + row, origin_x - offset, origin_x + offset,
            RotateByteRight(pattern[(half_height + row) % 8], pattern_offset));

    slope_dx = last_offset - offset;
    last_offset = offset;
  }
}

void BitmapImage::FillRow(int row,
                          int16_t start,
                          int16_t end,
                          uint8_t pattern,
                          FillMode mode) {
  static const unsigned char kMask[] = {0b11111111, 0b01111111, 0b00111111,
                                        0b00011111, 0b00001111, 0b00000111,
                                        0b00000011, 0b00000001, 0b00000000};

  // Handle clipping for the shapes. If the requested |row| is outside of the
  // clip region then ignore it. Ensure |start| and |end| are bound to the
  // extents of the clip region and ignore negative sized rows.
  if (row < clip_rect_.top || row >= clip_rect_.bottom) {
    return;
  }

  start = std::max(start, clip_rect_.left);
  end = std::min(end, clip_rect_.right);

  if (start >= end) {
    return;
  }

  int start_byte = row * PixelWidthToBytes(width_) + (start / CHAR_BIT);

  LOG_IF(INFO, kVerbose) << "Start byte: " << start_byte;

  int remaining_pixels = (end - start);

  // Sets the given |index| to the pattern AND'd with |mask|.
  auto set_index_with_mask = [&](int index, uint8_t mask) {
    using FillMode = BitmapImage::FillMode;

    switch (mode) {
      case FillMode::Copy:
        // Clear the pixels to be written
        bitmap_[index] &= ~mask;
        // Draw the masked pattern
        bitmap_[index] |= (mask & pattern);
        break;
      case FillMode::XOr:
        bitmap_[index] ^= (mask & pattern);
        break;
      case FillMode::NotXOr:
        bitmap_[index] ^= (mask & ~pattern);
        break;
    }
  };

  // Handles a |start| offset which is not byte aligned. Once this is handled
  // the next pixel is gauranteed to appear at the start of the next byte.
  uint8_t start_offset = start % CHAR_BIT;
  if (start_offset) {
    CHECK_LT(start_byte, bitmap_size_)
        << "Attemping to draw outside array bounds";

    // Handles a corner case where the |start| and |end| both occur in the
    // middle of the start byte.
    int byte_aligned_size = start_offset + (end - start);
    if (byte_aligned_size <= CHAR_BIT) {
      // Calculate a mask so that only the bits in the middle are set
      uint8_t byte_mask = kMask[start_offset] & ~kMask[byte_aligned_size];
      set_index_with_mask(start_byte, byte_mask);
      return;
    }

    LOG_IF(INFO, kVerbose) << "Start inset: " << (int)start_offset
                           << " Mask: " << std::bitset<8>(kMask[start_offset]);

    set_index_with_mask(start_byte, kMask[start_offset]);
    remaining_pixels -= (CHAR_BIT - start_offset);
    start_byte += 1;
  }

  // Now we are byte aligned so try to write out as many full bytes as we can
  int full_bytes = remaining_pixels / CHAR_BIT;
  if (full_bytes) {
    CHECK_LT(start_byte + full_bytes - 1, bitmap_size_)
        << "Attemping to draw outside array bounds";

    LOG_IF(INFO, kVerbose) << "Full bytes: " << full_bytes;
    switch (mode) {
      case FillMode::Copy:
        std::memset(bitmap_.get() + start_byte, pattern, full_bytes);
        break;
      case FillMode::XOr:
        for (int i = 0; i < full_bytes; ++i) {
          bitmap_[start_byte + i] ^= pattern;
        }
        break;
      case FillMode::NotXOr:
        for (int i = 0; i < full_bytes; ++i) {
          bitmap_[start_byte + i] ^= ~pattern;
        }
        break;
    }
  }

  // Handle any left over pixels which do not consume a full byte
  uint8_t end_outset = remaining_pixels % CHAR_BIT;
  if (end_outset) {
    CHECK_LT(start_byte + full_bytes, bitmap_size_)
        << "Attemping to draw outside array bounds";

    set_index_with_mask(start_byte + full_bytes, ~kMask[end_outset]);
    LOG_IF(INFO, kVerbose) << "End outset: " << (int)end_outset
                           << " Mask: " << std::bitset<8>(~kMask[end_outset]);
  }
}

void BitmapImage::CopyBits(const uint8_t* src,
                           const Rect& src_dims,
                           const Rect& src_rect,
                           const Rect& dst_rect) {
  int16_t height = RectHeight(dst_rect);
  int16_t width = RectWidth(dst_rect);

  // FIXME: Allow scaling between source/destination rects
  CHECK(height == RectHeight(src_rect) && width == RectWidth(src_rect))
      << "Source and destination MUST have the same dimensions";

  // Calculate the number of pixels outside of |clip_rect_| on each side:
  Rect clip_offset;
  clip_offset.top = std::max(0, clip_rect_.top - dst_rect.top);
  clip_offset.bottom = std::max(0, dst_rect.bottom - clip_rect_.bottom);
  clip_offset.left = std::max(0, clip_rect_.left - dst_rect.left);
  clip_offset.right = std::max(0, dst_rect.right - clip_rect_.right);

  // Account for the portion of the bitmap outside of |clip_rect_|
  int16_t clipped_height = height - (clip_offset.top + clip_offset.bottom);
  int16_t clipped_width = width - (clip_offset.left + clip_offset.right);

  // Is there anything left to draw? :P
  if (clipped_height <= 0 || clipped_width <= 0) {
    return;
  }

  // TODO: What should happen if |src_rect| is outside of |src_dims|?
  int16_t src_width = RectWidth(src_dims);
  for (int row = 0; row < clipped_height; ++row) {
    int src_row_offset =
        PixelWidthToBytes(src_width) * (row + src_rect.top + clip_offset.top);
    int dst_row_offset =
        PixelWidthToBytes(width_) * (row + dst_rect.top + clip_offset.top);

    bitarray_copy(src + src_row_offset,
                  /*src_offset=*/src_rect.left + clip_offset.left,
                  /*src_length=*/clipped_width, bitmap_.get() + dst_row_offset,
                  /*dst_offset=*/dst_rect.left + clip_offset.left);
  }
}

void BitmapImage::FrameRect(const Rect& rect,
                            const uint8_t pattern[8],
                            FillMode mode) {
  constexpr uint16_t kWidth = 1u;
  // FIXME: Account for clipping, invalid |rect|s, and proper |pattern| support
  for (int row = rect.top; row < rect.top + kWidth; ++row) {
    FillRow(row, rect.left, rect.right, pattern[0], mode);
  }
  for (int row = rect.bottom - kWidth; row < rect.bottom; ++row) {
    FillRow(row, rect.left, rect.right, pattern[0], mode);
  }

  for (int row = rect.top + kWidth; row < rect.bottom - kWidth; ++row) {
    FillRow(row, rect.left, rect.left + kWidth, pattern[0], mode);
    FillRow(row, rect.right - kWidth, rect.right, pattern[0], mode);
  }
}

void BitmapImage::CopyBitmap(const BitmapImage& bitmap,
                             const Rect& src_rect,
                             const Rect& dst_rect) {
  auto src_dims = NewRect(0, 0, bitmap.width(), bitmap.height());
  CopyBits(bitmap.bits(), src_dims, src_rect, dst_rect);
}

void BitmapImage::PrintBitmap() const {
  for (int i = 0; i < bitmap_size_; ++i) {
    std::cout << std::bitset<8>(bitmap_[i]);
    if ((i + 1) % PixelWidthToBytes(width_) == 0) {
      std::cout << std::endl;
    }
  }
}

void BitmapImage::SaveBitmap(const std::string& path) const {
  std::ofstream icon;
  icon.open(path, std::ios::out);
  auto write_byte = [&](uint8_t byte, int length) {
    length = 7 - length;
    for (int i = 7; i >= length; --i) {
      icon << ((byte & (1 << i)) ? "1 " : "0 ");
    }
  };

  icon << "P1 " << width_ << " " << height_ << "\n";

  int byte_width = PixelWidthToBytes(width_);

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < byte_width; ++x) {
      int bit_length = width_ - 8 * x;
      if (bit_length > 7) {
        bit_length = 7;
      } else {
        bit_length -= 1;
      }
      write_byte(bitmap_[x + y * byte_width], bit_length);
    }
    icon << "\n";
  }
}

}  // namespace graphics
}  // namespace cyder
