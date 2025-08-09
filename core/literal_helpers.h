// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>
#include <cstdint>

// Common user-defined literal operators
// Docs: https://docs.microsoft.com/en-us/cpp/cpp/user-defined-literals-cpp

// Memory units:
constexpr size_t operator""_kb(unsigned long long int value) {
  return value * 1024;
}
constexpr size_t operator""_mb(unsigned long long int value) {
  return value * 1024_kb;
}

// User-defined literal operators for various integer types:
constexpr std::uint8_t operator""_u8(unsigned long long value) {
  return static_cast<std::uint8_t>(value);
}
constexpr std::uint16_t operator""_u16(unsigned long long value) {
  return static_cast<std::uint16_t>(value);
}
constexpr std::uint32_t operator""_u32(unsigned long long value) {
  return static_cast<std::uint32_t>(value);
}
constexpr std::int8_t operator""_i8(unsigned long long value) {
  return static_cast<std::int8_t>(value);
}
constexpr std::int16_t operator""_i16(unsigned long long value) {
  return static_cast<std::int16_t>(value);
}
constexpr std::int32_t operator""_i32(unsigned long long value) {
  return static_cast<std::int32_t>(value);
}