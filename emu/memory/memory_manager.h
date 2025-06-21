// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>
#include <map>
#include <sstream>
#include <string>

#include "core/memory_region.h"
#include "emu/memory/memory_map.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {
namespace memory {

class MemoryManager {
 public:
  static constexpr size_t kHeapHandleOffset{4096};

  MemoryManager();

  static MemoryManager& the();

  Ptr Allocate(uint32_t size);
  Handle AllocateHandle(uint32_t size, std::string tag);
  Handle AllocateHandleForRegion(const core::MemoryRegion& region,
                                 std::string tag);
  bool Deallocate(Handle handle);

  bool HasSpaceForAllocation(uint32_t size) const {
    return heap_offset_ + size < kHeapEnd;
  }

  std::string GetTag(Handle handle) const;
  Handle GetHandleThatContains(uint32_t address) const;

  Ptr GetPtrForHandle(Handle handle) const;

  core::MemoryRegion GetRegionForHandle(Handle handle) const;

  uint32_t GetHandleSize(Handle handle) const;

  void UpdateHandle(Handle handle, uint32_t new_address, uint32_t new_size);

  bool SetApplLimit(Ptr last_addr);
  uint32_t GetFreeMemorySize() const;

  Handle RecoverHandle(Ptr ptr);

  template <typename Type>
  absl::StatusOr<Type> ReadTypeFromHandle(Handle handle) const {
    auto memory_region = GetRegionForHandle(handle);
    return ReadType<Type>(memory_region, /*offset=*/0);
  }

  template <typename Type>
  absl::Status WriteTypeToHandle(Type type, Handle handle) const {
    auto memory_region = GetRegionForHandle(handle);
    return WriteType<Type>(type, memory_region, /*offset=*/0);
  }

  template <typename Type>
  absl::StatusOr<Handle> NewHandleFor(const Type& type, std::string tag) {
    auto handle = AllocateHandle(type.size(), std::move(tag));
    auto region_for_handle = GetRegionForHandle(handle);
    RETURN_IF_ERROR(WriteType<Type>(type, region_for_handle, /*offset=*/0));
    return handle;
  }

  std::string LogHandles() {
    std::stringstream os;
    for (const auto& handle_pair : handle_to_metadata_) {
      os << "\n0x" << std::hex << handle_pair.first << " -> 0x"
         << handle_pair.second.start << " (" << handle_pair.second.tag << ")";
    }
    return os.str();
  }

 private:
  size_t heap_offset_{kHeapHandleOffset};
  size_t handle_offset_{0};

  struct HandleMetadata {
    std::string tag;
    uint32_t start;
    uint32_t end;
    uint32_t size;
  };

  std::map<Handle, HandleMetadata> handle_to_metadata_;
};

}  // namespace memory
}  // namespace cyder