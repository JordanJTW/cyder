#include "memory_manager.h"

#include <iomanip>

#include "core/logging.h"
#include "core/memory_region.h"
#include "memory_map.h"

extern core::MemoryRegion kSystemMemory;

Handle MemoryManager::Allocate(size_t size, std::string tag) {
  size_t offset = kHeapStart + heap_offset_;
  LOG(INFO) << "Alloc: '" << tag << "'";
  LOG(INFO) << "Handle " << (handle_offset_ / sizeof(Handle)) << " @ "
            << std::hex << offset << " size: " << size;
  heap_offset_ += size;
  LOG(INFO) << "Memory used: " << heap_offset_ << " / "
            << (kHeapEnd - kHeapStart);
  Handle handle = kHeapStart + handle_offset_;
  handle_offset_ += sizeof(Handle);
  LOG(INFO) << "Handles used: " << handle_offset_ / sizeof(Handle);
  CHECK(kSystemMemory.Write<uint32_t>(handle, htobe32(offset)).ok());

  HandleData metadata;
  metadata.tag = std::move(tag);
  metadata.start = offset;
  metadata.end = offset + size;
  metadata.size = size;

  handle_to_data_.insert({handle, std::move(metadata)});
  return handle;
}

bool MemoryManager::Deallocate(Handle handle) {
  auto entry = handle_to_data_.find(handle);
  if (entry == handle_to_data_.cend()) {
    LOG(ERROR) << "Handle was already deallocated...";
    return false;
  }

  LOG(INFO) << "Dealloc: '" << entry->second.tag << "'";
  handle_to_data_.erase(handle);
  return true;
}

std::string MemoryManager::GetTag(Handle handle) {
  auto entry = handle_to_data_.find(handle);
  if (entry == handle_to_data_.cend()) {
    return {};
  }
  return entry->second.tag;
}

Handle MemoryManager::GetHandleThatContains(uint32_t address) {
  for (const auto& entry : handle_to_data_) {
    if (address < entry.second.end && address >= entry.second.start) {
      return entry.first;
    }
  }
  return 0;
}

uint32_t MemoryManager::GetHandleSize(Handle handle) {
  auto entry = handle_to_data_.find(handle);
  if (entry == handle_to_data_.cend()) {
    return {};
  }
  return entry->second.size;
}
