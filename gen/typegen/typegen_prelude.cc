#include "typegen_prelude.h"

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