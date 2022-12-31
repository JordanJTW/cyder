// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>

// Common user-defined literal operators
// Docs: https://docs.microsoft.com/en-us/cpp/cpp/user-defined-literals-cpp

// Memory units:
constexpr size_t operator"" _kb(unsigned long long int value) {
  return value * 1024;
}
constexpr size_t operator"" _mb(unsigned long long int value) {
  return value * 1024_kb;
}