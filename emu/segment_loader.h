#pragma once

#include <cstdint>

#include "absl/status/statusor.h"
#include "memory_manager.h"
#include "resource_group.h"
#include "resource_manager.h"

// The in memory representation of the jump-table header structure from:
// http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/runtimehtml/RTArch-118.html#MARKER-9-35
struct InMemoryTableHeader {
  uint32_t above_a5;
  uint32_t below_a5;
  uint32_t table_size;
  uint32_t table_offset;
};

// Loads 'CODE' segments into kSystemMemory
class SegmentLoader final {
 public:
  // Parses 'CODE' segment 0 and loads the jump-table into kSystemMemory,
  // calculating the initial program counter (the first subroutine offset of the
  // first 'CODE' segment), and creates a new SegmentLoader.
  static absl::StatusOr<SegmentLoader> Create(rsrcloader::ResourceManager&,
                                              MemoryManager&);

  // Loads the segment with the given ID into the heap in kSystemMemory and
  // updates the jump-table entries to point to the newly loaded code.
  absl::StatusOr<Ptr> Load(uint16_t);

 private:
  SegmentLoader(MemoryManager&,
                rsrcloader::ResourceManager&,
                InMemoryTableHeader);

  MemoryManager& memory_manager_;
  rsrcloader::ResourceManager& resource_manager_;

  const InMemoryTableHeader table_header_;
};