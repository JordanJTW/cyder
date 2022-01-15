#pragma once

#include "absl/status/statusor.h"
#include "core/memory_region.h"
#include "third_party/musashi/src/m68k.h"

extern core::MemoryRegion kSystemMemory;

template <typename T>
absl::StatusOr<T> BasePop(m68k_register_t stack_ptr_reg) {
  uint32_t stack_ptr = m68k_get_reg(NULL, stack_ptr_reg);
  T value = TRY(kSystemMemory.Copy<T>(stack_ptr));
  m68k_set_reg(stack_ptr_reg, stack_ptr + sizeof(T));
  return value;
}

template <typename T>
absl::StatusOr<T> Pop(m68k_register_t stack_ptr_reg) {
  return BasePop<T>(stack_ptr_reg);
}

template <>
absl::StatusOr<uint16_t> Pop(m68k_register_t stack_ptr_reg) {
  return be16toh(TRY(BasePop<uint16_t>(stack_ptr_reg)));
}

template <>
absl::StatusOr<uint32_t> Pop(m68k_register_t stack_ptr_reg) {
  return be32toh(TRY(BasePop<uint32_t>(stack_ptr_reg)));
}

template <typename T>
absl::Status BasePush(T value, m68k_register_t stack_ptr_reg) {
  uint32_t new_stack_ptr = m68k_get_reg(NULL, stack_ptr_reg) - sizeof(T);
  RETURN_IF_ERROR(kSystemMemory.Write<T>(new_stack_ptr, value));
  m68k_set_reg(stack_ptr_reg, new_stack_ptr);
  return absl::OkStatus();
}

template <typename T>
absl::Status Push(T value, m68k_register_t stack_ptr_reg) {
  return BasePush<T>(value, stack_ptr_reg);
}

template <>
absl::Status Push(uint16_t value, m68k_register_t stack_ptr_reg) {
  return BasePush<uint16_t>(htobe16(value), stack_ptr_reg);
}

template <>
absl::Status Push(uint32_t value, m68k_register_t stack_ptr_reg) {
  return BasePush<uint32_t>(htobe32(value), stack_ptr_reg);
}