// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>

#include "absl/status/statusor.h"
#include "emu/memory/memory_manager.h"
#include "emu/rsrc/resource_group.h"
#include "emu/rsrc/resource_manager.h"
#include "emu/segment_types.tdef.h"

namespace cyder {

class SegmentLoader {
 public:
  virtual ~SegmentLoader() = default;

  // Loads the segment with the given ID into the heap in kSystemMemory and
  // updates the jump-table entries to point to the newly loaded code.
  virtual absl::StatusOr<Ptr> Load(uint16_t) = 0;
};

// Loads 'CODE' segments into kSystemMemory
class SegmentLoaderImpl : public SegmentLoader {
 public:
  // Parses 'CODE' segment 0 and loads the jump-table into kSystemMemory,
  // calculating the initial program counter (the first subroutine offset of the
  // first 'CODE' segment), and creates a new SegmentLoader.
  static absl::StatusOr<std::unique_ptr<SegmentLoader>> Create(
      memory::MemoryManager&,
      ResourceManager&);

  ~SegmentLoaderImpl() override = default;

  // SegmentLoader implementation:
  absl::StatusOr<Ptr> Load(uint16_t) override;

 private:
  SegmentLoaderImpl(memory::MemoryManager&,
                    ResourceManager&,
                    SegmentTableHeader);

  memory::MemoryManager& memory_manager_;
  ResourceManager& resource_manager_;

  const SegmentTableHeader table_header_;
};

}  // namespace cyder