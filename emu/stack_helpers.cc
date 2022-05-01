#include "stack_helpers.h"

#include "core/endian.h"

template <>
absl::StatusOr<uint16_t> Pop(m68k_register_t stack_ptr_reg) {
  return be16toh(TRY(internal::Pop<uint16_t>(stack_ptr_reg)));
}

template <>
absl::StatusOr<uint32_t> Pop(m68k_register_t stack_ptr_reg) {
  return be32toh(TRY(internal::Pop<uint32_t>(stack_ptr_reg)));
}

template <>
absl::StatusOr<absl::string_view> PopRef(m68k_register_t stack_ptr_reg) {
  size_t pstring_addr = TRY(Pop<uint32_t>(stack_ptr_reg));
  size_t pstring_len = TRY(kSystemMemory.Copy<uint8_t>(pstring_addr));
  return absl::string_view(
      reinterpret_cast<const char*>(kSystemMemory.raw_ptr()) + pstring_addr + 1,
      pstring_len);
}

template <>
absl::Status Push(uint16_t value, m68k_register_t stack_ptr_reg) {
  return internal::Push<uint16_t>(htobe16(value), stack_ptr_reg);
}

template <>
absl::Status Push(uint32_t value, m68k_register_t stack_ptr_reg) {
  return internal::Push<uint32_t>(htobe32(value), stack_ptr_reg);
}