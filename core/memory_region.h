#pragma once

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "status_helpers.h"

namespace core {

// Represents a region of memory and allows safe access. Each MemoryRegion is a
// subset of its parent and allows `offset`s to be relative to each region.
class MemoryRegion final {
 public:
  // Constructs a "base" region to access [`data` : `data` + `size`)
  // `size` is the maximum size that any sub-region can occupy
  MemoryRegion(void* data, size_t size);

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

  template <typename T>
  absl::StatusOr<T> Copy(size_t offset) const {
    T value;
    RETURN_IF_ERROR(Copy(&value, offset, sizeof(T)));
    return std::move(value);
  }

  // Copies `length` bytes from `offset` to `dest`. Returns
  // absl::OutOfRangeError if `offset` + `length` overflows the "base" region.
  absl::Status Copy(void* dest, size_t offset, size_t length) const;

  // The offset of this region within "base"
  size_t base_offset() const { return base_offset_; }
  // The expected size of a region
  size_t size() const { return size_; }

  const uint8_t* const raw_ptr() const { return data_; }

 private:
  MemoryRegion(std::string name,
               const uint8_t* const data,
               size_t size,
               size_t maximum_size,
               size_t base_offset);

  const std::string name_;
  const uint8_t* const data_;
  const size_t size_;

  const size_t maximum_size_;
  const size_t base_offset_;
};

std::ostream& operator<<(std::ostream&, const MemoryRegion&);

}  // namespace core