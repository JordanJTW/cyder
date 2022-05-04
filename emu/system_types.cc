#include "system_types.h"

std::ostream& operator<<(std::ostream& os, const Rect& rect) {
  return os << "[" << rect.top << ", " << rect.left << ", " << rect.bottom
            << ", " << rect.right << "]";
}
