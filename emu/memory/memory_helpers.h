// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <functional>

#include "absl/status/status.h"
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

}  // namespace cyder