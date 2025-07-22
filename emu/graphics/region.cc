#include "emu/graphics/region.h"

#include <cmath>

#include "absl/strings/str_join.h"
#include "core/memory_reader.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/graphics/graphics_helpers.h"
#include "gen/typegen/typegen_prelude.h"

struct Range {
  int16_t start;
  int16_t end;

  static constexpr size_t fixed_size = 4;
  size_t size() const { return fixed_size; }
};

template <>
absl::StatusOr<Range> ReadType(const core::MemoryRegion& region,
                               size_t offset) {
  return Range{.start = TRY(region.Read<int16_t>(offset)),
               .end = TRY(region.Read<int16_t>(offset + 2))};
}

std::ostream& operator<<(std::ostream& os, const Range& obj) {
  return os << "[" << obj.start << ", " << obj.end << ")";
}

namespace cyder {
namespace region {

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

std::vector<int16_t> Union(const core::MemoryRegion& d1,
                           const core::MemoryRegion& d2) {
  core::MemoryReader r1(d1);
  core::MemoryReader r2(d2);

  struct Range union_range = {0, 0};
  bool has_range = false;

  std::vector<int16_t> output;
  while (r1.HasNext() || r2.HasNext()) {
    struct Range next_range =
        !r2.HasNext() || (r1.HasNext() &&
                          MUST(r1.Peek<int16_t>()) < MUST(r2.Peek<int16_t>()))
            ? MUST(r1.NextType<Range>())
            : MUST(r2.NextType<Range>());

    if (!has_range) {
      union_range = std::move(next_range);
      has_range = true;
      continue;
    }

    if (next_range.start <= union_range.end) {
      union_range.end = MAX(next_range.end, union_range.end);
      continue;
    }

    output.push_back(union_range.start);
    output.push_back(union_range.end);
    union_range = std::move(next_range);
  }

  if (has_range) {
    output.push_back(union_range.start);
    output.push_back(union_range.end);
  }
  return output;
}

std::vector<int16_t> Offset(const core::MemoryRegion& data, int16_t offset) {
  core::MemoryReader reader(data);

  std::vector<int16_t> output;
  while (reader.HasNext()) {
    Range range = MUST(reader.NextType<Range>());
    output.push_back(range.start + offset);
    output.push_back(range.end + offset);
  }
  return output;
}

std::vector<int16_t> Intersect(const core::MemoryRegion& d1,
                               const core::MemoryRegion& d2) {
  core::MemoryReader r1(d1);
  core::MemoryReader r2(d2);

  Range v1, v2;
  std::vector<int16_t> output;
  while (r1.HasNext() && r2.HasNext()) {
    auto v1 = MUST(r1.PeekType<Range>());
    auto v2 = MUST(r2.PeekType<Range>());

    Range intersect = {.start = MAX(v1.start, v2.start),
                       .end = MIN(v1.end, v2.end)};

    if (intersect.start < intersect.end) {
      output.push_back(intersect.start);
      output.push_back(intersect.end);
    }

    if (v1.end < v2.end) {
      r1.SkipNext(Range::fixed_size);
    } else {
      r2.SkipNext(Range::fixed_size);
    }
  }

  return output;
}

std::vector<int16_t> Subtract(const core::MemoryRegion& d1,
                              const core::MemoryRegion& d2) {
  core::MemoryReader r1(d1);
  core::MemoryReader r2(d2);

  auto try_load_next_overlap = [&r2]() {
    return r2.HasNext() ? std::make_optional(MUST(r2.NextType<Range>()))
                        : std::nullopt;
  };

  std::optional<Range> potential_overlap = try_load_next_overlap();

  std::vector<int16_t> result;
  while (r1.HasNext()) {
    Range a = MUST(r1.NextType<Range>());
    int current_start = a.start;

    // Skip B ranges that end before A starts
    while (potential_overlap && potential_overlap->end <= a.start) {
      potential_overlap = try_load_next_overlap();
    }

    while (potential_overlap && potential_overlap->start < a.end) {
      if (current_start < potential_overlap->start) {
        result.push_back(current_start);
        result.push_back(MIN(potential_overlap->start, a.end));
      }

      current_start = MAX(current_start, potential_overlap->end);

      if (current_start >= a.end)
        break;

      potential_overlap = try_load_next_overlap();
    }

    if (current_start < a.end) {
      result.push_back(current_start);
      result.push_back(a.end);
    }
  }

  return result;
}

OwnedRegion NewRectRegion(int16_t x, int16_t y, int16_t width, int16_t height) {
  OwnedRegion result;
  result.owned_data.push_back(y);
  result.owned_data.push_back(2);
  result.owned_data.push_back(x);
  result.owned_data.push_back(x + width);
  result.owned_data.push_back(y + height);
  result.owned_data.push_back(0);
  result.rect = NewRect(x, y, width, height);
  return result;
}

struct Scanline {
  int16_t y;
  // A byte offset to the start of the scanline data (ranges) in a MemoryRegion
  size_t offset = 0;
  // The length of the scanline data in bytes
  size_t length = 0;
};

void AdvanceScanline(core::MemoryReader& reader, Scanline& scanline) {
  if (!reader.HasNext())
    return;

  scanline.y = MUST(reader.Next<int16_t>());
  auto count = MUST(reader.Next<int16_t>());

  scanline.offset = reader.offset();
  scanline.length = count * sizeof(int16_t);
  reader.SkipNext(scanline.length);
}

void WriteScanline(int16_t y,
                   std::vector<int16_t>& data,
                   std::vector<int16_t>& output) {
  output.push_back(y);
  output.push_back(static_cast<int16_t>(data.size()));
  output.insert(output.end(), data.begin(), data.end());
}

typedef std::vector<int16_t> OpFunction(const core::MemoryRegion&,
                                        const core::MemoryRegion&);

OwnedRegion RegionOp(const Region& r1, const Region& r2, OpFunction* op) {
  core::MemoryReader read1(r1.data), read2(r2.data);

  Scanline line1, line2;
  std::vector<int16_t> lastWritten;

  Rect rect = {INT16_MAX, INT16_MAX, INT16_MIN, INT16_MIN};

  std::vector<int16_t> output;
  while (read1.HasNext() || read2.HasNext()) {
    int16_t currentY;

    if (read1.HasNext() &&
        (!read2.HasNext() ||
         MUST(read1.Peek<int16_t>()) < MUST(read2.Peek<int16_t>()))) {
      AdvanceScanline(read1, line1);
      currentY = line1.y;
    } else if (read2.HasNext() &&
               (!read1.HasNext() ||
                MUST(read2.Peek<int16_t>()) < MUST(read1.Peek<int16_t>()))) {
      AdvanceScanline(read2, line2);
      currentY = line2.y;
    } else {  // yA == yB
      AdvanceScanline(read1, line1);
      AdvanceScanline(read2, line2);
      currentY = line1.y;
    }

    std::vector<int16_t> toWrite =
        op(MUST(r1.data.Create("line1", line1.offset, line1.length)),
           MUST(r2.data.Create("line2", line2.offset, line2.length)));

    // Only write if different from last written line
    if (toWrite != lastWritten) {
      WriteScanline(currentY, toWrite, output);
      lastWritten = toWrite;
    }

    // Update the bounding rect
    if (!toWrite.empty()) {
      rect.top = MIN(currentY, rect.top);
      rect.left = MIN(toWrite[0], rect.left);
      rect.right = MAX(toWrite.back(), rect.right);
    }
    rect.bottom = currentY;
  }
  return {.rect = std::move(rect), .owned_data = std::move(output)};
}

OwnedRegion Subtract(const Region& r1, const Region& r2) {
  return RegionOp(r1, r2, Subtract);
}
OwnedRegion Intersect(const Region& r1, const Region& r2) {
  return RegionOp(r1, r2, Intersect);
}
OwnedRegion Union(const Region& r1, const Region& r2) {
  return RegionOp(r1, r2, Union);
}

Region ConvertRegion(OwnedRegion& region, bool is_big_endian) {
  return {.rect = region.rect,
          .data = core::MemoryRegion(
              reinterpret_cast<uint8_t*>(region.owned_data.data()),
              region.owned_data.size() * sizeof(int16_t), is_big_endian)};
}

}  // namespace region
}  // namespace cyder
