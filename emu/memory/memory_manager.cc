// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/memory/memory_manager.h"

#include <iomanip>

#include "core/logging.h"
#include "core/memory_region.h"
#include "emu/memory/memory_map.h"
#include "gen/global_names.h"

namespace cyder {
namespace memory {

namespace {

MemoryManager* s_instance;

}  // namespace

extern core::MemoryRegion kSystemMemory;

MemoryManager::MemoryManager() {
  s_instance = this;
}

// static
MemoryManager& MemoryManager::the() {
  return *s_instance;
}

Ptr MemoryManager::Allocate(uint32_t size) {
  size_t ptr = kHeapStart + heap_offset_;
  LOG(INFO) << "Allocate " << size << " bytes at 0x" << std::hex << ptr;
  heap_offset_ += size;
  LOG(INFO) << "Memory used: " << heap_offset_ << " / "
            << (kHeapEnd - kHeapStart);
  CHECK_LT(heap_offset_, kHeapEnd);
  return ptr;
}

Handle MemoryManager::AllocateHandle(uint32_t size, std::string tag) {
  Ptr block = Allocate(size);
  Handle handle = kHeapStart + handle_offset_;

  LOG(INFO) << "Handle " << (handle_offset_ / sizeof(Handle)) << " ["
            << std::hex << handle << "] for '" << tag << "'";

  CHECK_LT(handle_offset_, kHeapHandleOffset);

  handle_offset_ += sizeof(Handle);
  LOG(INFO) << "Handles used: " << handle_offset_ / sizeof(Handle);

  CHECK(kSystemMemory.Write<uint32_t>(handle, block).ok());

  HandleMetadata metadata;
  metadata.tag = std::move(tag);
  metadata.start = block;
  metadata.end = block + size;
  metadata.size = size;

  handle_to_metadata_.insert({handle, std::move(metadata)});
  return handle;
}

Handle MemoryManager::AllocateHandleForRegion(const core::MemoryRegion& region,
                                              std::string tag) {
  Handle handle = AllocateHandle(region.size(), tag);
  size_t load_addr = MUST(kSystemMemory.Read<uint32_t>(handle));

  for (size_t i = 0; i < region.size(); ++i) {
    CHECK(kSystemMemory
              .Write<uint8_t>(load_addr + i, MUST(region.Read<uint8_t>(i)))
              .ok());
  }
  return handle;
}

Ptr MemoryManager::GetPtrForHandle(Handle handle) const {
  auto entry = handle_to_metadata_.find(handle);
  CHECK(entry != handle_to_metadata_.cend())
      << "Handle (0x" << std::hex << handle << ") can not be found.";

  auto current_ptr = MUST(kSystemMemory.Read<uint32_t>(entry->first));

  const HandleMetadata& metadata = entry->second;
  CHECK_EQ(current_ptr, metadata.start);

  return current_ptr;
}

core::MemoryRegion MemoryManager::GetRegionForHandle(Handle handle) const {
  auto entry = handle_to_metadata_.find(handle);
  CHECK(entry != handle_to_metadata_.cend())
      << "Handle (0x" << std::hex << handle << ") can not be found.";

  const HandleMetadata& metadata = entry->second;
  auto current_ptr = MUST(kSystemMemory.Read<uint32_t>(entry->first));
  CHECK_EQ(current_ptr, metadata.start);

  return MUST(kSystemMemory.Create(absl::StrCat("Handle[", metadata.tag, "]"),
                                   metadata.start, metadata.size));
}

bool MemoryManager::Deallocate(Handle handle) {
  auto entry = handle_to_metadata_.find(handle);
  if (entry == handle_to_metadata_.cend()) {
    LOG(ERROR) << "Handle was already deallocated...";
    return false;
  }

  LOG(INFO) << "Dealloc: '" << entry->second.tag << "'";
  handle_to_metadata_.erase(handle);
  return true;
}

std::string MemoryManager::GetTag(Handle handle) const {
  auto entry = handle_to_metadata_.find(handle);
  if (entry == handle_to_metadata_.cend()) {
    return {};
  }
  return entry->second.tag;
}

Handle MemoryManager::GetHandleThatContains(uint32_t address) const {
  for (const auto& entry : handle_to_metadata_) {
    if (address < entry.second.end && address >= entry.second.start) {
      return entry.first;
    }
  }
  return 0;
}

uint32_t MemoryManager::GetHandleSize(Handle handle) const {
  auto entry = handle_to_metadata_.find(handle);
  if (entry == handle_to_metadata_.cend()) {
    NOTREACHED() << "Handle 0x" << std::hex << handle << " does not exist!";
    return {};
  }
  return entry->second.size;
}

bool MemoryManager::SetApplLimit(Ptr last_addr) {
  if (last_addr >= kHeapEnd) {
    LOG(WARNING) << "Requested more heap memory than available";
    return false;
  }
  auto status = kSystemMemory.Write<Ptr>(GlobalVars::ApplLimit, last_addr);
  CHECK(status.ok()) << std::move(status).message();
  return true;
}

uint32_t MemoryManager::GetFreeMemorySize() const {
  return kHeapEnd - heap_offset_;
}

}  // namespace memory
}  // namespace cyder