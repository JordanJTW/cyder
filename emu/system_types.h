#pragma once

#include <cstdint>
#include <ostream>

typedef uint32_t Ptr;
typedef Ptr Handle;
typedef uint16_t Integer;

typedef Ptr GrafPtr;

typedef struct {
  Integer top;
  Integer left;
  Integer bottom;
  Integer right;
} Rect;

std::ostream& operator<<(std::ostream&, const Rect&);
