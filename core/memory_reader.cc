#include "memory_reader.h"

#include <cstdint>

#include "absl/status/status.h"

namespace core {

MemoryReader::MemoryReader(const MemoryRegion& region) : region_(region) {}

absl::StatusOr<absl::string_view> MemoryReader::NextString(
    absl::optional<size_t> fixed_size) {
  auto length = TRY(Next<uint8_t>());
  if (fixed_size.has_value() && fixed_size.value() < length) {
    return absl::FailedPreconditionError(absl::StrCat(
        "String has a length of ", length,
        " which is greater than its fixed size (", fixed_size.value(), ")"));
  }
  auto data = reinterpret_cast<const char*>(region_.raw_ptr()) + offset_;
  offset_ += fixed_size.value_or(length);
  return absl::string_view(data, length);
}

absl::StatusOr<MemoryRegion> MemoryReader::NextRegion(std::string name,
                                                      size_t length) {
  auto region = TRY(region_.Create(std::move(name), offset_, length));
  offset_ += length;
  return region;
}

void MemoryReader::OffsetTo(size_t new_offset) {
  offset_ = new_offset;
}

void MemoryReader::AlignTo(size_t block_size) {
  if (offset_ % block_size != 0) {
    OffsetTo(((offset_ / block_size) + 1 * block_size));
  }
}

}  // namespace core
