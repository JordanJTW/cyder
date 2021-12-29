#pragma once

#include <stddef.h>
#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "core/status_helpers.h"

namespace rsrcloader {

class MemoryRegion final {
 public:
  MemoryRegion(void* data, size_t size);

  absl::StatusOr<MemoryRegion> Create(std::string name, size_t offset) const;

  template <typename T>
  absl::StatusOr<T> Copy(size_t offset) const {
    T value;
    RETURN_IF_ERROR(Copy(&value, offset, sizeof(T)));
    return std::move(value);
  }

  absl::Status Copy(void* dest, size_t offset, size_t length) const;

  const uint8_t* const raw_ptr() const { return data_; }

 private:
  MemoryRegion(std::string name, const uint8_t* const data, size_t size);

  const std::string name_;
  const uint8_t* const data_;
  const size_t size_;
};

}  // namespace rsrcloader