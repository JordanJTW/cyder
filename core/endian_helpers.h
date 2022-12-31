// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstddef>
#include <type_traits>

// Cross-platform (Linux/macOS currently) header for accessing
// endian converter functions.

#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#else

#include <endian.h>

#endif  // __APPLE__

namespace internal {

template <typename IntegerType, size_t ByteWidth>
struct EndianSwap;

template <typename IntegerType>
struct EndianSwap<IntegerType, /*ByteWidth=*/1> {
  static IntegerType htobe(IntegerType value) { return value; }
  static IntegerType betoh(IntegerType value) { return value; }
};

template <typename IntegerType>
struct EndianSwap<IntegerType, /*ByteWidth=*/2> {
  static IntegerType htobe(IntegerType value) { return htobe16(value); }
  static IntegerType betoh(IntegerType value) { return be16toh(value); }
};

template <typename IntegerType>
struct EndianSwap<IntegerType, /*ByteWidth=*/4> {
  static IntegerType htobe(IntegerType value) { return htobe32(value); }
  static IntegerType betoh(IntegerType value) { return be32toh(value); }
};

template <typename IntegerType>
struct EndianSwap<IntegerType, /*ByteWidth=*/8> {
  static IntegerType htobe(IntegerType value) { return htobe64(value); }
  static IntegerType betoh(IntegerType value) { return be64toh(value); }
};

}  // namespace internal

// Converts `IntegerType` from Host endianness to big-endian by calling the
// appropriate htobeXX function based on the byte-width of `IntegerType`.
template <typename IntegerType,
          typename std::enable_if<std::is_integral<IntegerType>::value,
                                  bool>::type = true>
IntegerType htobe(IntegerType value) {
  return internal::EndianSwap<IntegerType, sizeof(IntegerType)>::htobe(value);
}

// Converts `IntegerType` from big-endian to the host endianness by calling the
// appropriate beXXtoh function based on the byte-width of `IntegerType`.
template <typename IntegerType,
          typename std::enable_if<std::is_integral<IntegerType>::value,
                                  bool>::type = true>
IntegerType betoh(IntegerType value) {
  return internal::EndianSwap<IntegerType, sizeof(IntegerType)>::betoh(value);
}