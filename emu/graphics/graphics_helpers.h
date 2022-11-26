#pragma once

#include <climits>

namespace cyder {

// Get the number of bytes needed to represent |width_px| at 1 bit-per-pixel
inline int PixelWidthToBytes(int width_px) {
  int width_bytes = width_px / CHAR_BIT;
  // Add one byte to capture the remaining pixels if needed
  return width_px % CHAR_BIT ? width_bytes + 1 : width_bytes;
}

// Get the number of bytes needed to represent a PICT v1 with frame |rect|
inline int FrameRectToBytes(const Rect& rect) {
  return rect.bottom * PixelWidthToBytes(rect.right);
}

// Offset |rect| by the given offsets
inline Rect OffsetRect(Rect rect, int16_t offset_x, int16_t offset_y) {
  rect.left += offset_x;
  rect.right += offset_x;
  rect.top += offset_y;
  rect.bottom += offset_y;
  return rect;
}

// Normalize the |rect| so that its origin is at (0, 0) with the same dimensions
inline Rect NormalizeRect(Rect rect) {
  return OffsetRect(rect, -rect.left, -rect.top);
}

// Return a rectangle which is just large enough to contain the provided rects
inline Rect UnionRect(Rect r1, Rect r2) {
  Rect urect;
  urect.top = std::min(r1.top, r2.top);
  urect.bottom = std::max(r1.bottom, r2.bottom);
  urect.left = std::min(r1.left, r2.left);
  urect.right = std::max(r1.right, r2.right);
  return urect;
}

inline int16_t RectWidth(const Rect& rect) {
  return rect.right - rect.left;
}

inline int16_t RectHeight(const Rect& rect) {
  return rect.bottom - rect.top;
}

inline Rect NewRect(int16_t x, int16_t y, int16_t width, int16_t height) {
  Rect rect;
  rect.top = y;
  rect.bottom = y + height;
  rect.left = x;
  rect.right = x + width;
  return rect;
}

}  // namespace cyder