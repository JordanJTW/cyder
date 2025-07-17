#pragma once

#include <cstdint>
#include <iomanip>

using Ptr = uint32_t;
// An indirect reference to a pointer (pointer to pointer)
using Handle = Ptr;
using Integer = uint16_t;
// A four character string identifier used through-out Mac OS
using OSType = uint32_t;

template <typename T>
struct Var {
  Ptr ptr;
  T current_value;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, const Var<T>& var) {
  return os << "0x" << std::hex << var.ptr << std::dec
            << " [value: " << var.current_value << "]";
}