// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "endian_helpers.h"
#include "status_helpers.h"

namespace core {

// Represents a region of memory and allows safe access. Each MemoryRegion is a
// subset of its parent and allows `offset`s to be relative to each region.
class MemoryRegion final {
 public:
  // Constructs a "base" region to access [`data` : `data` + `size`)
  // `size` is the maximum size that any sub-region can occupy
  MemoryRegion(void* const data, size_t size);

  // Creates a new MemoryRegion representing a subset of the parent region from
  // [`offset`, `parent size`).
  //
  // Regions _must_ be within the "base" region. If an `offset` would overflow
  // "base" then absl::OutOfRangeError is returned. `offset` is always relative
  // to the parent region.
  absl::StatusOr<MemoryRegion> Create(size_t offset) const;
  absl::StatusOr<MemoryRegion> Create(std::string name, size_t offset) const;

  // Creates a new MemoryRegion representing a subset of the parent region from
  // [`offset`, `size`). Regions _must_ be within the "base" region.
  //
  // If an `offset` + `size` would overflow "base" then absl::OutOfRangeError is
  // returned. If an `offset` + `size` would overflow the parent `size` then an
  // error is logged. If `size` == 0 then it is the same as Create(offset).
  // `offset` is always relative to the parent region.
  absl::StatusOr<MemoryRegion> Create(std::string name,
                                      size_t offset,
                                      size_t size) const;

  template <typename Type>
  absl::StatusOr<Type> Read(size_t offset) const {
    return betoh<Type>(TRY(ReadRaw<Type>(offset)));
  }

  template <typename Type>
  absl::StatusOr<Type> ReadRaw(size_t offset) const {
    Type value;
    RETURN_IF_ERROR(ReadRaw(&value, offset, sizeof(Type)));
    return std::move(value);
  }

  // Copies `length` bytes from `offset` to `dest`. Returns
  // absl::OutOfRangeError if `offset` + `length` overflows the "base" region.
  absl::Status ReadRaw(void* dest, size_t offset, size_t length) const;

  template <typename Type>
  absl::Status Write(size_t offset, Type data) {
    return WriteRaw<Type>(offset, htobe<Type>(data));
  }

  template <typename Type>
  absl::Status WriteRaw(size_t offset, Type data) {
    return WriteRaw(&data, offset, sizeof(Type));
  }

  // Writes `length` bytes from `src` to `offset`
  absl::Status WriteRaw(const void* src, size_t offset, size_t length);

  // The offset of this region within "base"
  size_t base_offset() const { return base_offset_; }
  // The expected size of a region
  size_t size() const { return size_; }

  const uint8_t* const raw_ptr() const { return data_; }

 private:
  MemoryRegion(std::string name,
               uint8_t* const data,
               size_t size,
               size_t maximum_size,
               size_t base_offset);

  absl::Status CheckSafeAccess(const std::string& access_type,
                               size_t offset,
                               size_t size) const;

  const std::string name_;
  uint8_t* const data_;
  const size_t size_;

  const size_t maximum_size_;
  const size_t base_offset_;
};

std::ostream& operator<<(std::ostream&, const MemoryRegion&);

}  // namespace core