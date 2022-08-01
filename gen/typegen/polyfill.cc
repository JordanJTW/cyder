// This file implements pieces of typegen that are difficult to generate

#include "generated_types.h"

#include "emu/memory/memory_map.h"

template <>
absl::StatusOr<absl::string_view> ReadType(const core::MemoryRegion& region,
                                           size_t ptr) {
  auto length = TRY(cyder::memory::kSystemMemory.Copy<uint8_t>(ptr));
  return absl::string_view(
      reinterpret_cast<const char*>(cyder::memory::kSystemMemory.raw_ptr()) +
          ptr + 1,
      length);
}
