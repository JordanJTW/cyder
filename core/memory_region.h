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

// Allows watching for reads/writes with-in a MemoryRegion (and sub-regions)
class MemoryWatcher {
 public:
  virtual void OnRead(size_t offset, size_t size) {}
  virtual void OnWrite(size_t offset, size_t size) {}
};

// Represents a region of memory and allows safe access. Each MemoryRegion is a
// subset of its parent and allows `offset`s to be relative to each region.
class MemoryRegion final {
 public:
  // Constructs a "base" region to access [`data` : `data` + `size`)
  // `size` is the maximum size that any sub-region can occupy
  MemoryRegion(void* const data, size_t size, bool is_big_endian = true);

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
    // Special case for bools, which are stored as a single byte
    // with the least significant bit set to true/false.
    if (std::is_same<Type, bool>::value) {
      uint8_t value;
      RETURN_IF_ERROR(ReadRaw(&value, offset, sizeof(uint8_t)));
      return value & 0x01 ? true : false;
    }

    static_assert(std::is_integral<Type>::value,
                  "Only integral/bool types can be read from MemoryRegion");
    Type value;
    RETURN_IF_ERROR(ReadRaw(&value, offset, sizeof(Type)));
    return is_big_endian_ ? betoh<Type>(value) : value;
  }

  // Copies `length` bytes from `offset` to `dest`. Returns
  // absl::OutOfRangeError if `offset` + `length` overflows the "base" region.
  absl::Status ReadRaw(void* dest, size_t offset, size_t length) const;

  template <typename Type>
  absl::Status Write(size_t offset, Type data) {
    // Special case for bools, which are stored as a single byte
    // with the least significant bit set to true/false.
    if (std::is_same<Type, bool>::value) {
      uint8_t value = data ? 0x01 : 0x00;
      return WriteRaw(&value, offset, sizeof(uint8_t));
    }

    static_assert(std::is_integral<Type>::value,
                  "Only integral/bool types can be written to MemoryRegion");
    const Type endian = is_big_endian_ ? htobe<Type>(data) : data;
    return WriteRaw(&endian, offset, sizeof(Type));
  }

  // Writes `length` bytes from `src` to `offset`
  absl::Status WriteRaw(const void* src, size_t offset, size_t length);

  // Sets `watcher` to track reads/writes to the MemoryRegion.
  // NOTE: This will track access across all regions associated with a "base".
  void SetWatcher(MemoryWatcher* watcher);

  // The offset of this region within "base"
  size_t base_offset() const { return base_offset_; }
  // The expected size of a region
  size_t size() const { return size_; }

  const uint8_t* const raw_ptr() const { return data_; }
  uint8_t* const raw_mutable_ptr() const { return data_; }

 private:
  struct SharedData {
    MemoryWatcher* watcher;
  };

  MemoryRegion(std::string name,
               uint8_t* const data,
               size_t size,
               size_t maximum_size,
               size_t base_offset,
               bool is_big_endian,
               std::shared_ptr<SharedData> shared_data);

  absl::Status CheckSafeAccess(const std::string& access_type,
                               size_t offset,
                               size_t size) const;

  const std::string name_;
  uint8_t* const data_;
  const size_t size_;

  const size_t maximum_size_;
  const size_t base_offset_;
  bool is_big_endian_;

  std::shared_ptr<SharedData> shared_data_;
};

std::ostream& operator<<(std::ostream&, const MemoryRegion&);

}  // namespace core