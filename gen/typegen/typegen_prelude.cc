// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "typegen_prelude.h"

Field Field::operator+(const Field& other) const {
  return Field{offset + other.offset, other.size};
}

template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t offset) {
  auto length = TRY(region.Read<uint8_t>(offset));
  auto data = reinterpret_cast<const char*>(region.raw_ptr()) + offset + 1;
  return absl::string_view(data, length);
}

template <>
absl::StatusOr<std::string> ReadType(const core::MemoryRegion& region,
                                     size_t offset) {
  auto length = TRY(region.Read<uint8_t>(offset));
  auto data = reinterpret_cast<const char*>(region.raw_ptr()) + offset + 1;
  return std::string(data, length);
}

template <>
absl::Status WriteType(const std::string& type,
                       core::MemoryRegion& region,
                       size_t offset) {
  CHECK_LT(type.size(), 255u);
  RETURN_IF_ERROR(region.Write<uint8_t>(offset, type.size()));
  RETURN_IF_ERROR(region.WriteRaw(type.data(), offset + 1, type.size()));
  return absl::OkStatus();
}

template <>
absl::Status WriteType(const absl::string_view& type,
                       core::MemoryRegion& region,
                       size_t offset) {
  CHECK_LT(type.size(), 255u);
  RETURN_IF_ERROR(region.Write<uint8_t>(offset, type.size()));
  RETURN_IF_ERROR(region.WriteRaw(type.data(), offset + 1, type.size()));
  return absl::OkStatus();
}

absl::StatusOr<uint32_t> CopyU24(const core::MemoryRegion& region,
                                 size_t offset) {
  uint32_t return_value;
  RETURN_IF_ERROR(region.ReadRaw(&return_value, offset, /*length=*/3));
  // The copy will place the `u24` right justified (like a little-endian value)
  // but it needs to be left justified with the LSB anchored correctly at '0'
  return_value <<= 8;
  return betoh(return_value);
}

absl::Status WriteU24(uint24_t value,
                      core::MemoryRegion& region,
                      size_t offset) {
  // See `CopyU24` for an explanation (this is just the reverse of that) :^)
  value = htobe(value) >> 8;
  RETURN_IF_ERROR(region.WriteRaw(&value, offset, /*length=*/3));
  return absl::OkStatus();
}

std::string OSTypeName(uint32_t os_type) {
  char type_name[4];
  // The type value is actually a 4 byte string so we must reverse it
  // back to big endian for the text to appear correctly
  uint32_t reversed_type = htobe(os_type);
  memcpy(type_name, &reversed_type, sizeof(uint32_t));
  return std::string(type_name, 4);
}