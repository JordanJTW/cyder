#include "stack_helpers.h"

#include "core/endian_helpers.h"

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
  auto ptr = TRY(Pop<Ptr>(stack_ptr_reg));
  auto length = TRY(kSystemMemory.Copy<uint8_t>(ptr));
  return absl::string_view(
      reinterpret_cast<const char*>(kSystemMemory.raw_ptr()) + ptr + 1, length);
}

template <>
absl::StatusOr<Rect> PopRef(m68k_register_t stack_ptr_reg) {
  auto ptr = TRY(Pop<Ptr>(stack_ptr_reg));
  Rect rect;
  rect.top = be16toh(TRY(kSystemMemory.Copy<Integer>(ptr)));
  rect.left = be16toh(TRY(kSystemMemory.Copy<Integer>(ptr + 2)));
  rect.bottom = be16toh(TRY(kSystemMemory.Copy<Integer>(ptr + 4)));
  rect.right = be16toh(TRY(kSystemMemory.Copy<Integer>(ptr + 6)));
  return rect;
}

template <>
absl::Status Push(uint16_t value, m68k_register_t stack_ptr_reg) {
  return internal::Push<uint16_t>(htobe16(value), stack_ptr_reg);
}

template <>
absl::Status Push(uint32_t value, m68k_register_t stack_ptr_reg) {
  return internal::Push<uint32_t>(htobe32(value), stack_ptr_reg);
}

template <>
absl::Status TrapReturn(int16_t value) {
  return internal::TrapReturn<int16_t>(htobe16(value));
}
template <>
absl::Status TrapReturn(uint16_t value) {
  return internal::TrapReturn<uint16_t>(htobe16(value));
}
template <>
absl::Status TrapReturn(uint32_t value) {
  return internal::TrapReturn<uint32_t>(htobe32(value));
}
