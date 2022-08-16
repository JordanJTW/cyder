#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/endian_helpers.h"
#include "core/memory_region.h"

template <typename Type>
absl::StatusOr<Type> ReadType(const core::MemoryRegion& region, size_t ptr);
template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t ptr);
template <>
absl::StatusOr<std::string> ReadType(const core::MemoryRegion& region,
                                     size_t ptr);

template <typename Type>
absl::StatusOr<Type> CopyWithWidth(const core::MemoryRegion& region,
                                   size_t offset,
                                   size_t width) {
  Type return_value;
  RETURN_IF_ERROR(region.Copy(&return_value, offset, width));
  return_value <<= ((sizeof(Type) - width) * 8);
  return betoh(return_value);
}