// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "memory_reader.h"

#include <cstdint>

#include "absl/strings/str_cat.h"
#include "absl/status/status.h"

namespace core {

MemoryReader::MemoryReader(const MemoryRegion& region, size_t offset)
    : region_(region), offset_(offset) {}

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

void MemoryReader::SkipNext(size_t skip_bytes) {
  offset_ += skip_bytes;
}

void MemoryReader::AlignTo(size_t block_size) {
  if (offset_ % block_size != 0) {
    OffsetTo(((offset_ / block_size) + 1 * block_size));
  }
}

bool MemoryReader::HasNext() const {
  return offset_ < region_.size();
}

}  // namespace core
