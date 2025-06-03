// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/endian_helpers.h"
#include "core/memory_reader.h"
#include "core/memory_region.h"

// Represents 3 bytes (often packed with a byte).
// This is just used to make clear when a `u24` is used.
using uint24_t = uint32_t;

// A four character string identifier used through-out Mac OS
using OSType = uint32_t;

using Ptr = uint32_t;  // A pointer to a memory location, often used in Mac OS.
using Handle = Ptr;  // A handle is a pointer to a memory location.

struct Field {
  const size_t offset;
  const size_t size;

  Field operator+(const Field& other) const;
};

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

std::string OSTypeName(uint32_t os_type);