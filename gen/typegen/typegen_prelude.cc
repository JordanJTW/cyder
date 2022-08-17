#include "typegen_prelude.h"

template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t offset) {
  auto length = TRY(region.Copy<uint8_t>(offset));
  auto data = reinterpret_cast<const char*>(region.raw_ptr()) + offset + 1;
  return absl::string_view(data, length);
}

template <>
absl::StatusOr<std::string> ReadType(const core::MemoryRegion& region,
                                     size_t offset) {
  auto length = TRY(region.Copy<uint8_t>(offset));
  auto data = reinterpret_cast<const char*>(region.raw_ptr()) + offset + 1;
  return std::string(data, length);
}

template <>
absl::Status WriteType(const std::string& type,
                       core::MemoryRegion& region,
                       size_t offset) {
  CHECK_LT(type.size(), 255u);
  RETURN_IF_ERROR(region.Write<uint8_t>(offset, type.size()));
  RETURN_IF_ERROR(region.Write(type.data(), offset + 1, type.size()));
  return absl::OkStatus();
}

template <>
absl::Status WriteType(const absl::string_view& type,
                       core::MemoryRegion& region,
                       size_t offset) {
  CHECK_LT(type.size(), 255u);
  RETURN_IF_ERROR(region.Write<uint8_t>(offset, type.size()));
  RETURN_IF_ERROR(region.Write(type.data(), offset + 1, type.size()));
  return absl::OkStatus();
}