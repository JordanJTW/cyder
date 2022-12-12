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
  Rect rect;
  rect.top = std::min(r1.top, r2.top);
  rect.bottom = std::max(r1.bottom, r2.bottom);
  rect.left = std::min(r1.left, r2.left);
  rect.right = std::max(r1.right, r2.right);
  return rect;
}

// Return a rectangle which represents the intersection of two rects or
// (t:0, l:0, b:0, r:0) if there is no intersection
inline Rect IntersectRect(Rect r1, Rect r2) {
  Rect rect;
  rect.top = std::max(r1.top, r2.top);
  rect.bottom = std::min(r1.bottom, r2.bottom);
  rect.left = std::max(r1.left, r2.left);
  rect.right = std::min(r1.right, r2.right);

  if (rect.top >= rect.bottom || rect.left >= rect.right) {
    rect.top = 0;
    rect.bottom = 0;
    rect.left = 0;
    rect.right = 0;
  }
  return rect;
}

// Return True if |rect| is set to (t:0, l:0, b:0, r:0) and False otherwise
inline bool IsZeroRect(const Rect& rect) {
  return rect.top == 0 && rect.bottom == 0 && rect.left == 0 && rect.right == 0;
}

// Return True if |parent| contains |child|, otherwise return False
inline bool ContainsRect(const Rect& parent, const Rect& child) {
  return parent.top <= child.top && parent.left <= child.left &&
         parent.bottom >= child.bottom && parent.right >= child.right;
}

inline bool EqualRect(const Rect& r1, const Rect& r2) {
  return r1.top == r2.top && r1.left == r2.left && r1.bottom == r2.bottom &&
         r1.right == r2.right;
}

inline bool PointInRect(const Point& pt, const Rect& rect) {
  return pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top &&
         pt.y < rect.bottom;
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