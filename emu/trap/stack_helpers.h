// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <type_traits>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "emu/base_types.h"
#include "gen/typegen/typegen_prelude.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace memory {
extern core::MemoryRegion kSystemMemory;
}  // namespace memory

namespace trap {

// Pops `T` off of the stack
template <typename T>
T Pop() {
  // Special case for bools. Stored in a byte but word aligned on stack.
  if constexpr (std::is_same<T, bool>::value) {
    return (Pop<uint16_t>() & 0x0100) ? true : false;
  }

  static_assert(std::is_integral<T>::value,
                "Only integers are stored on the stack (see PopRef<>)");
  Ptr current_stack = m68k_get_reg(NULL, M68K_REG_SP);
  T value = MUST(memory::kSystemMemory.Read<T>(current_stack));
  m68k_set_reg(M68K_REG_SP, current_stack + sizeof(T));
  return value;
}

// Peek `T` at `offset` index of the stack
template <typename T>
absl::StatusOr<T> Peek(size_t offset = 0) {
  static_assert(std::is_integral<T>::value,
                "Only integers are stored on the stack");
  Ptr current_stack = m68k_get_reg(NULL, M68K_REG_SP);
  return TRY(memory::kSystemMemory.Read<T>(current_stack + offset));
}

// Pops type `T` off of the stack.
// Types <= 4 bytes in size are stored directly on the stack.
// Reference:
// http://0.0.0.0:8000/docs/mac/OSUtilities/OSUtilities-170.html#HEADING170-115
// FIXME: Add check to template that size of `T` is <= to 4 bytes?
template <typename T>
T PopType() {
  Ptr current_stack = m68k_get_reg(NULL, M68K_REG_SP);
  T value = MUST(ReadType<T>(memory::kSystemMemory, current_stack));
  m68k_set_reg(M68K_REG_SP, current_stack + T::fixed_size);
  return value;
}

// Pops pointer to `T` off of the stack and returns the dereferenced value
template <typename T>
T PopRef() {
  // AND-ing here accounts for non 32-bit clean systems where the OS stored
  // flags in the upper byte of a pointer.
  // TODO: Allow for 32-bit clean behavior and handle this more consistently
  auto ptr = Pop<Ptr>() & 0x00FFFFFF;
  return MUST(ReadType<T>(memory::kSystemMemory, ptr));
}

template <typename T>
Var<T> PopVar() {
  auto ptr = Pop<Ptr>();
  if constexpr (std::is_integral<T>::value) {
    return Var<T>{ptr, MUST(memory::kSystemMemory.Read<T>(ptr))};
  } else {
    return Var<T>{ptr, MUST(ReadType<T>(memory::kSystemMemory, ptr))};
  }
}

// Pushes `T` on to the stack
template <typename T>
void Push(T value) {
  // Special case for bools. Stored in a byte but word aligned on stack.
  if constexpr (std::is_same<T, bool>::value) {
    Push<uint16_t>(value ? 0x0100 : 0x0000);
    return;
  }

  static_assert(std::is_integral<T>::value,
                "Only integers are stored on the stack");
  Ptr new_stack_ptr = m68k_get_reg(NULL, M68K_REG_SP) - sizeof(T);
  CHECK_OK(memory::kSystemMemory.Write<T>(new_stack_ptr, value));
  m68k_set_reg(M68K_REG_SP, new_stack_ptr);
}

// Function results are returned by value or by address on the stack.
// Space for the function result is allocated by the caller before the
// parameters are pushed. The caller is responsible for removing the
// result from the stack after the call.
// Link:
// https://dev.os9.ca/techpubs/mac/OSUtilities/OSUtilities-170.html#HEADING170-121
template <typename T>
absl::Status TrapReturn(T value) {
  // Special case for bools. Stored in a byte but word aligned on stack.
  if constexpr (std::is_same<T, bool>::value) {
    Ptr current_stack = m68k_get_reg(NULL, M68K_REG_SP);
    RETURN_IF_ERROR(memory::kSystemMemory.Write<uint16_t>(
        current_stack, value ? 0x0100 : 0x0000));
  } else if constexpr (std::is_integral<T>::value) {
    Ptr current_stack = m68k_get_reg(NULL, M68K_REG_SP);
    RETURN_IF_ERROR(memory::kSystemMemory.Write<T>(current_stack, value));
  } else {
    Ptr current_stack = m68k_get_reg(NULL, M68K_REG_SP);
    RETURN_IF_ERROR(WriteType<T>(value, memory::kSystemMemory, current_stack));
  }
  return absl::OkStatus();
}

}  // namespace trap
}  // namespace cyder