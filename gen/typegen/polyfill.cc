// This file implements pieces of typegen that are difficult to generate

#include "generated_types.h"

#include <string>

#include "emu/memory/memory_map.h"

template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t ptr) {
  auto length = TRY(region.Copy<uint8_t>(ptr));
  auto data = reinterpret_cast<const char*>(region.raw_ptr()) + ptr + 1;
  return absl::string_view(data, length);
}

template <>
absl::StatusOr<std::string> ReadType(const core::MemoryRegion& region,
                                     size_t ptr) {
  auto length = TRY(region.Copy<uint8_t>(ptr));
  auto data = reinterpret_cast<const char*>(region.raw_ptr()) + ptr + 1;
  return std::string(data, length);
}