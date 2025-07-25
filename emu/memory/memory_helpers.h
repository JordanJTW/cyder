// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>

#include "absl/status/status.h"
#include "emu/memory/memory_manager.h"
#include "emu/memory/memory_map.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

template <typename Type>
absl::Status WithType(Ptr ptr, std::function<absl::Status(Type& type)> cb) {
  auto type = TRY(ReadType<Type>(memory::kSystemMemory, ptr));
  RETURN_IF_ERROR(cb(type));
  return WriteType<Type>(type, memory::kSystemMemory, ptr);
}

template <typename Type>
absl::Status WithType(Ptr ptr,
                      std::function<absl::Status(const Type& type)> cb) {
  auto type = TRY(ReadType<Type>(memory::kSystemMemory, ptr));
  RETURN_IF_ERROR(cb(type));
  return absl::OkStatus();
}

template <typename Type>
absl::Status WithHandleToType(Handle handle,
                              std::function<absl::Status(Type& type)> cb) {
  auto ptr = TRY(memory::kSystemMemory.Read<Ptr>(handle));
  return WithType(ptr, std::move(cb));
}

template <typename Type>
absl::StatusOr<Type> ReadHandleToType(Handle handle) {
  auto ptr = TRY(memory::kSystemMemory.Read<Ptr>(handle));
  return ReadType<Type>(memory::kSystemMemory, ptr);
}

inline region::Region ReadRegionFromHandle(Handle handle) {
  core::MemoryRegion region_for_handle =
      memory::MemoryManager::the().GetRegionForHandle(handle);

  auto region = MUST(ReadType<Region>(region_for_handle, 0));
  return {
      .rect = region.bounding_box,
      // NOTE: This differs from the documentation in that |region_size| is the
      //       size of just the data (not including |Region::fixed_size|).
      .data = MUST(memory::kSystemMemory.Create(
          "region_data", region_for_handle.base_offset() + Region::fixed_size,
          region.region_size)),
  };
}

inline Handle AllocateHandleToRegion(const region::OwnedRegion& region) {
  int16_t data_size = region.owned_data.size() * sizeof(int16_t);

  auto handle = memory::MemoryManager::the().AllocateHandle(
      Region::fixed_size + data_size, "Region");
  core::MemoryRegion region_for_handle =
      memory::MemoryManager::the().GetRegionForHandle(handle);

  CHECK_OK(region_for_handle.Write<int16_t>(/*offset=*/0, data_size));
  CHECK_OK(WriteType<Rect>(region.rect, region_for_handle, /*offset=*/2));
  size_t offset = Region::fixed_size;
  for (const int16_t& value : region.owned_data) {
    CHECK_OK(region_for_handle.Write<int16_t>(offset, value));
    offset += sizeof(int16_t);
  }
  return handle;
}
}  // namespace cyder