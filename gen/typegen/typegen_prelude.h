#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/memory_region.h"

template <typename Type>
absl::StatusOr<Type> ReadType(const core::MemoryRegion& region, size_t ptr);
template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t ptr);
template <>
absl::StatusOr<std::string> ReadType(const core::MemoryRegion& region,
                                     size_t ptr);