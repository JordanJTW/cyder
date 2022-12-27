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

}  // namespace cyder