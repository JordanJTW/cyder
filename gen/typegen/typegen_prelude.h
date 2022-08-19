#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/endian_helpers.h"
#include "core/memory_region.h"

// Represents 3 bytes (often packed with a byte).
// This is just used to make clear when a `u24` is used.
using uint24_t = uint32_t;

template <typename Type>
absl::StatusOr<Type> ReadType(const core::MemoryRegion& region, size_t offset);
template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t offset);
template <>
absl::StatusOr<std::string> ReadType(const core::MemoryRegion& region,
                                     size_t offset);

absl::StatusOr<uint32_t> CopyU24(const core::MemoryRegion& region,
                                 size_t offset);

absl::Status WriteU24(uint24_t value,
                      core::MemoryRegion& region,
                      size_t offset);

template <typename Type>
absl::Status WriteType(const Type& type,
                       core::MemoryRegion& region,
                       size_t offset);
template <>
absl::Status WriteType(const std::string& type,
                       core::MemoryRegion& region,
                       size_t offset);
template <>
absl::Status WriteType(const absl::string_view& type,
                       core::MemoryRegion& region,
                       size_t offset);