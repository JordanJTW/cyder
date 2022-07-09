#pragma once

#include <type_traits>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/endian_helpers.h"
#include "core/memory_region.h"
#include "system_types.h"
#include "third_party/musashi/src/m68k.h"

extern core::MemoryRegion kSystemMemory;

// Pops `T` off of the stack pointed to by `stack_ptr_reg`
template <typename T>
absl::StatusOr<T> Pop(m68k_register_t stack_ptr_reg) {
  static_assert(std::is_integral<T>::value,
                "Only integers are stored on the stack (see PopRef<>)");
  Ptr current_stack = m68k_get_reg(NULL, stack_ptr_reg);
  T value = TRY(kSystemMemory.Copy<T>(current_stack));
  m68k_set_reg(stack_ptr_reg, current_stack + sizeof(T));
  return betoh<T>(value);
}

// Pops pointer to `T` off of the stack pointed to by
// `stack_ptr_reg` and returns the dereferenced value
template <typename T>
absl::StatusOr<T> PopRef(m68k_register_t stack_ptr_reg);
template <>
absl::StatusOr<absl::string_view> PopRef(m68k_register_t stack_ptr_reg);
template <>
absl::StatusOr<Rect> PopRef(m68k_register_t stack_ptr_reg);

// Pushes `T` on to the stack pointed to by `stack_ptr_reg`
template <typename T>
absl::Status Push(T value, m68k_register_t stack_ptr_reg) {
  static_assert(std::is_integral<T>::value,
                "Only integers are stored on the stack");
  Ptr new_stack_ptr = m68k_get_reg(NULL, stack_ptr_reg) - sizeof(T);
  RETURN_IF_ERROR(kSystemMemory.Write<T>(new_stack_ptr, htobe<T>(value)));
  m68k_set_reg(stack_ptr_reg, new_stack_ptr);
  return absl::OkStatus();
}

// Function results are returned by value or by address on the stack.
// Space for the function result is allocated by the caller before the
// parameters are pushed. The caller is responsible for removing the
// result from the stack after the call.
// Link:
// https://dev.os9.ca/techpubs/mac/OSUtilities/OSUtilities-170.html#HEADING170-121
template <typename T>
absl::Status TrapReturn(T value) {
  static_assert(std::is_integral<T>::value,
                "Only integers are stored on the stack");
  Ptr current_stack = m68k_get_reg(NULL, M68K_REG_USP);
  RETURN_IF_ERROR(kSystemMemory.Write<T>(current_stack, htobe<T>(value)));
  return absl::OkStatus();
}
