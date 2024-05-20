// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>
#include <string>
#include <type_traits>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "endian_helpers.h"
#include "memory_region.h"
#include "status_helpers.h"

// Read `Type` from |region| at |offset|
//
// It is expected that specializations of this template will use MemoryReader
// to create convience methods to read structs from memory. Notably, typegen
// will auto-create ReadType specializations based on struct definitions :)
template <typename Type>
absl::StatusOr<Type> ReadType(const core::MemoryRegion& region,
                              size_t offset = 0);

namespace core {

// Reads data of various types sequentially from a MemoryRegion.
class MemoryReader final {
 public:
  MemoryReader(const MemoryRegion& region, size_t offset = 0);

  // Read the next integer from the MemoryRegion.
  template <typename IntegerType,
            typename std::enable_if<std::is_integral<IntegerType>::value,
                                    bool>::type = true>
  absl::StatusOr<IntegerType> Next() {
    IntegerType value = TRY(region_.Read<IntegerType>(offset_));
    offset_ += sizeof(value);
    return std::move(value);
  }

  // Peek (do not increase offset) the next integer from the MemoryRegion.
  template <typename IntegerType,
            typename std::enable_if<std::is_integral<IntegerType>::value,
                                    bool>::type = true>
  absl::StatusOr<IntegerType> Peek() {
    return region_.Read<IntegerType>(offset_);
  }

  // Read the next ReadType<> `Type` from the MemoryRegion.
  // FIXME: Ensure the templated `Type` has a size() method and is not a string
  template <typename Type>
  absl::StatusOr<Type> NextType() {
    auto type = TRY(ReadType<Type>(region_, offset_));
    offset_ += type.size();
    return type;
  }

  // Read the next Pascal style string (a byte length |n| followed by |n| chars)
  // from the MemoryRegion. If |fixed_length| is provided then |n| must be less
  // than |fixed_length| and |fixed_length| will _always_ be added to offset.
  absl::StatusOr<absl::string_view> NextString(
      absl::optional<size_t> fixed_size = absl::nullopt);

  // Create a new MemoryRegion from the current offset with the given |length|.
  absl::StatusOr<core::MemoryRegion> NextRegion(std::string name,
                                                size_t length);

  // Update the offset that the next read should start from.
  void OffsetTo(size_t new_offset);

  // Move the offset forward by |skip_bytes|.
  void SkipNext(size_t skip_bytes);

  // Move the offset to align with the start of the next block.
  void AlignTo(size_t block_size);

  // Return whether or not there is still memory to be read.
  bool HasNext() const;

  // Returns the current `offset` being read from.
  size_t offset() const { return offset_; }

 private:
  const MemoryRegion region_;
  size_t offset_;
};

}  // namespace core
