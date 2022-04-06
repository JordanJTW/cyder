#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "system_types.h"

class MemoryManager {
 public:
  Handle Allocate(size_t size, std::string tag);
  bool Deallocate(Handle handle);

  std::string GetTag(Handle handle);
  Handle GetHandleThatContains(uint32_t address);

  uint32_t GetHandleSize(Handle handle);

 private:
  size_t heap_offset_{4096};
  size_t handle_offset_{0};

  struct HandleData {
    std::string tag;
    uint32_t start;
    uint32_t end;
    uint32_t size;
  };

  std::map<Handle, HandleData> handle_to_data_;
};