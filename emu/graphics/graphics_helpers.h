#pragma once

#include <climits>

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