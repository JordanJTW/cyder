#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "resource_file.h"
#include "resource_group.h"

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
  static absl::StatusOr<SegmentLoader> Create(rsrcloader::ResourceFile&);

  // Loads the segment with the given ID into the heap in kSystemMemory and
  // updates the jump-table entries to point to the newly loaded code.
  absl::Status Load(uint16_t);

  // The initial program counter to jump to the entry point:
  size_t entry_point() const { return initial_program_counter_; }

 private:
  SegmentLoader(InMemoryTableHeader,
                rsrcloader::ResourceGroup&,
                uint32_t initial_program_counter);

  const InMemoryTableHeader table_header_;
  rsrcloader::ResourceGroup& code_resources_;
  const uint32_t initial_program_counter_;

  size_t heap_offset_{0};
};