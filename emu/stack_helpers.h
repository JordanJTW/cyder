#pragma once

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "third_party/musashi/src/m68k.h"
#include "system_types.h"

extern core::MemoryRegion kSystemMemory;

// Pops `T` off of the stack pointed to by `stack_ptr_reg`
template <typename T>
absl::StatusOr<T> Pop(m68k_register_t stack_ptr_reg);
template <>
absl::StatusOr<uint16_t> Pop(m68k_register_t stack_ptr_reg);
template <>
absl::StatusOr<uint32_t> Pop(m68k_register_t stack_ptr_reg);

// Pops pointer to `T` off of the stack pointed to by
// `stack_ptr_reg` and returns the dereferenced value
template <typename T>
absl::StatusOr<T> PopRef(m68k_register_t stack_ptr_reg) {
  auto ptr = TRY(Pop<Ptr>(stack_ptr_reg));
  return *reinterpret_cast<const T*>(kSystemMemory.raw_ptr() + ptr);
}
template <>
absl::StatusOr<absl::string_view> PopRef(m68k_register_t stack_ptr_reg);

// Pushes `T` on to the stack pointed to by `stack_ptr_reg`
template <typename T>
absl::Status Push(T value, m68k_register_t stack_ptr_reg);
template <>
absl::Status Push(uint16_t value, m68k_register_t stack_ptr_reg);
template <>
absl::Status Push(uint32_t value, m68k_register_t stack_ptr_reg);

namespace internal {

template <typename T>
absl::StatusOr<T> Pop(m68k_register_t stack_ptr_reg) {
  uint32_t stack_ptr = m68k_get_reg(NULL, stack_ptr_reg);
  T value = TRY(kSystemMemory.Copy<T>(stack_ptr));
  m68k_set_reg(stack_ptr_reg, stack_ptr + sizeof(T));
  return value;
}

template <typename T>
absl::Status Push(T value, m68k_register_t stack_ptr_reg) {
  uint32_t new_stack_ptr = m68k_get_reg(NULL, stack_ptr_reg) - sizeof(T);
  RETURN_IF_ERROR(kSystemMemory.Write<T>(new_stack_ptr, value));
  m68k_set_reg(stack_ptr_reg, new_stack_ptr);
  return absl::OkStatus();
}

}  // namespace internal
