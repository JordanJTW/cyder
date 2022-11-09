#include "emu/graphics/bitmap_screen.h"

#include <bitset>

#include "core/logging.h"
#include "emu/graphics/copybits.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace graphics {
namespace {

static const bool kVerbose = false;

}  // namespace

BitmapScreen::BitmapScreen(int width, int height)
    : width_(width),
      height_(height),
      bitmap_size_(PixelWidthToBytes(width) * height),
      bitmap_(new uint8_t[bitmap_size_]) {
  std::memset(bitmap_, 0, bitmap_size_);
  clip_rect_ = NewRect(0, 0, width, height);
}

BitmapScreen::~BitmapScreen() {
  delete[] bitmap_;
}

void BitmapScreen::FillRect(const Rect& rect, const uint8_t pattern[8]) {
  for (int16_t row = rect.top; row < rect.bottom; ++row) {
    FillRow(row, rect.left, rect.right, pattern[row % 8]);
  }
}

void BitmapScreen::FillEllipse(const Rect& rect, const uint8_t pattern[8]) {
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

  // Fill the horrizontal center row of the ellipse.
  // The rest of the ellipse is mirrored over this central line.
  FillRow(origin_y, rect.left, rect.right, pattern[half_height % 8]);

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
            pattern[(half_height - row) % 8]);
    FillRow(origin_y + row, origin_x - offset, origin_x + offset,
            pattern[(half_height + row) % 8]);

    slope_dx = last_offset - offset;
    last_offset = offset;
  }
}

void BitmapScreen::FillRow(int row,
                           int16_t start,
                           int16_t end,
                           uint8_t pattern) {
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
      // Calculate a mask so that only the bits in the middle are on
      uint8_t byte_mask = kMask[start_offset] & ~kMask[byte_aligned_size];
      // Clear the pixels to be written to 0
      bitmap_[start_byte] &= ~byte_mask;
      // Draw the masked pattern
      bitmap_[start_byte] |= (byte_mask & pattern);
      return;
    }

    LOG_IF(INFO, kVerbose) << "Start inset: " << (int)start_offset
                           << " Mask: " << std::bitset<8>(kMask[start_offset]);

    // Clear the pixels to be written to 0
    bitmap_[start_byte] &= ~kMask[start_offset];
    // Draw the masked pattern
    bitmap_[start_byte] |= (kMask[start_offset] & pattern);
    remaining_pixels -= (CHAR_BIT - start_offset);
    start_byte += 1;
  }

  // Now we are byte aligned so try to write out as many full bytes as we can
  int full_bytes = remaining_pixels / CHAR_BIT;
  if (full_bytes) {
    CHECK_LT(start_byte + full_bytes - 1, bitmap_size_)
        << "Attemping to draw outside array bounds";

    LOG_IF(INFO, kVerbose) << "Full bytes: " << full_bytes;
    std::memset(bitmap_ + start_byte, pattern, full_bytes);
  }

  // Handle any left over pixels which do not consume a full byte
  uint8_t end_outset = remaining_pixels % CHAR_BIT;
  if (end_outset) {
    CHECK_LT(start_byte + full_bytes, bitmap_size_)
        << "Attemping to draw outside array bounds";

    // Clear the pixels to be written to 0
    bitmap_[start_byte + full_bytes] &= kMask[end_outset];
    // Draw the masked pattern
    bitmap_[start_byte + full_bytes] |= (~kMask[end_outset] & pattern);
    LOG_IF(INFO, kVerbose) << "End outset: " << (int)end_outset
                           << " Mask: " << std::bitset<8>(~kMask[end_outset]);
  }
}

void BitmapScreen::CopyBits(const uint8_t* src,
                            const Rect& src_rect,
                            const Rect& dst_rect) {
  int16_t height = RectHeight(dst_rect);
  int16_t width = RectWidth(dst_rect);

  // FIXME: Allow scaling between source/destination rects
  CHECK(height == RectHeight(src_rect) && width == RectWidth(src_rect))
      << "Source and destination MUST have the same dimensions";

  for (int row = 0; row < height; ++row) {
    int src_row_offset = PixelWidthToBytes(width) * row;
    int dst_row_offset = PixelWidthToBytes(width_) * (dst_rect.top + row);

    bitarray_copy(src + src_row_offset, /*src_offset=*/0, width,
                  bitmap_ + dst_row_offset, dst_rect.left);
  }
}

void BitmapScreen::PrintBitmap() const {
  for (int i = 0; i < bitmap_size_; ++i) {
    std::cout << std::bitset<8>(bitmap_[i]);
    if ((i + 1) % PixelWidthToBytes(width_) == 0) {
      std::cout << std::endl;
    }
  }
}

}  // namespace graphics
}  // namespace cyder
