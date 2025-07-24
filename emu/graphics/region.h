#pragma once

#include "core/memory_region.h"
#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {
namespace region {

// A region already owned elsewhere (useful for emulator)
struct Region {
  Rect rect;
  core::MemoryRegion data;
};

// A region that owns its own memory (useful in native code/tests)
struct OwnedRegion {
  Rect rect;
  std::vector<int16_t> owned_data;
};

// Boolean set operations that process a single row of ranges.
std::vector<int16_t> Union(const core::MemoryRegion& v1,
                           const core::MemoryRegion& v2);
std::vector<int16_t> Intersect(const core::MemoryRegion& v1,
                               const core::MemoryRegion& v2);
std::vector<int16_t> Subtract(const core::MemoryRegion& v1,
                              const core::MemoryRegion& v2);
std::vector<int16_t> Offset(core::MemoryRegion& value, int16_t offset);

// Region operations that use the functions above to compute.
OwnedRegion Union(const Region& r1, const Region& r2);
OwnedRegion Intersect(const Region& r1, const Region& r2);
OwnedRegion Subtract(const Region& r1, const Region& r2);
OwnedRegion Offset(const Region& r1, int16_t dx, int16_t dy);

// Creates a new region representing a rectange at (x, y) with width and height
OwnedRegion NewRectRegion(int16_t x, int16_t y, int16_t width, int16_t height);
OwnedRegion NewRectRegion(const Rect& rect);

Region ConvertRegion(OwnedRegion& region);

std::ostream& operator<<(std::ostream& os, const Region& obj);
std::ostream& operator<<(std::ostream& os, const OwnedRegion obj);

// NewRgn
// OpenRgn
// CloseRgn
// DisposeRgn
// CopyRgn
// SetEmptyRgn
// SetRectRgn
// RectRgn
// OffsetRgn
// InsetRgn
// * SectRgn
// * UnionRgn
// * DiffRgn
// XorRgn
// PtInRgn
// RectInRgn
// EqualRgn
// EmptyRgn

}  // namespace region
}  // namespace cyder
