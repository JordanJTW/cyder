#pragma once

#include <cstddef>
#include <map>
#include <sstream>
#include <string>

#include "core/memory_region.h"
#include "emu/system_types.h"

namespace cyder {
namespace memory {

class MemoryManager {
 public:
  static constexpr size_t kHeapHandleOffset{512};

  Ptr Allocate(uint32_t size);
  Handle AllocateHandle(uint32_t size, std::string tag);
  Handle AllocateHandleForRegion(const core::MemoryRegion& region,
                                 std::string tag);
  bool Deallocate(Handle handle);

  std::string GetTag(Handle handle);
  Handle GetHandleThatContains(uint32_t address);

  core::MemoryRegion GetRegionForHandle(Handle handle);

  uint32_t GetHandleSize(Handle handle);

  std::string LogHandles() {
    std::stringstream os;
    for (const auto& handle_pair : handle_to_metadata_) {
      os << "\t0x" << std::hex << handle_pair.first << " -> 0x"
         << handle_pair.second.start;
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