#pragma once

// Credit to Jamie (2010) from StackOverflow
// Link:
// https://stackoverflow.com/questions/3534535/whats-a-time-efficient-algorithm-to-copy-unaligned-bit-arrays
//
// Per https://stackoverflow.com/help/licensing, the code in this file (and
// only this file) is licensed under Creative Commons BY-SA 2.5:
// https://creativecommons.org/licenses/by-sa/2.5/legalcode

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define PREPARE_FIRST_COPY()                                                   \
  do {                                                                         \
    if (src_len >= (CHAR_BIT - dst_offset_modulo)) {                           \
      *dst &= mask[dst_offset_modulo];                                         \
      src_len -= CHAR_BIT - dst_offset_modulo;                                 \
    } else {                                                                   \
      *dst &= mask[dst_offset_modulo] | mask_xor[dst_offset_modulo + src_len]; \
      c &= mask[dst_offset_modulo + src_len];                                  \
      src_len = 0;                                                             \
    }                                                                          \
  } while (0)

static void bitarray_copy(const uint8_t* src_org,
                          int src_offset,
                          int src_len,
                          uint8_t* dst_org,
                          int dst_offset) {
  static const uint8_t mask[] = {0x00, 0x80, 0xc0, 0xe0, 0xf0,
                                 0xf8, 0xfc, 0xfe, 0xff};
  static const uint8_t mask_xor[] = {0xff, 0x7f, 0x3f, 0x1f, 0x0f,
                                     0x07, 0x03, 0x01, 0x00};

  if (src_len) {
    const uint8_t* src;
    uint8_t* dst;
    int src_offset_modulo, dst_offset_modulo;

    src = src_org + (src_offset / CHAR_BIT);
    dst = dst_org + (dst_offset / CHAR_BIT);

    src_offset_modulo = src_offset % CHAR_BIT;
    dst_offset_modulo = dst_offset % CHAR_BIT;

    if (src_offset_modulo == dst_offset_modulo) {
      int byte_len;
      int src_len_modulo;
      if (src_offset_modulo) {
        uint8_t c;

        c = mask_xor[dst_offset_modulo] & *src++;

        PREPARE_FIRST_COPY();
        *dst++ |= c;
      }

      byte_len = src_len / CHAR_BIT;
      src_len_modulo = src_len % CHAR_BIT;

      if (byte_len) {
        memcpy(dst, src, byte_len);
        src += byte_len;
        dst += byte_len;
      }
      if (src_len_modulo) {
        *dst &= mask_xor[src_len_modulo];
        *dst |= mask[src_len_modulo] & *src;
      }
    } else {
      int bit_diff_ls, bit_diff_rs;
      int byte_len;
      int src_len_modulo;
      uint8_t c;
      // Line things up on destination
      if (src_offset_modulo > dst_offset_modulo) {
        bit_diff_ls = src_offset_modulo - dst_offset_modulo;
        bit_diff_rs = CHAR_BIT - bit_diff_ls;

        c = *src++ << bit_diff_ls;
        c |= *src >> bit_diff_rs;
        c &= mask_xor[dst_offset_modulo];
      } else {
        bit_diff_rs = dst_offset_modulo - src_offset_modulo;
        bit_diff_ls = CHAR_BIT - bit_diff_rs;

        c = *src >> bit_diff_rs & mask_xor[dst_offset_modulo];
      }
      PREPARE_FIRST_COPY();
      *dst++ |= c;

      // Copy only shifting the source
      byte_len = src_len / CHAR_BIT;

      while (--byte_len >= 0) {
        c = *src++ << bit_diff_ls;
        c |= *src >> bit_diff_rs;
        *dst++ = c;
      }

      // Copy all remaining bits
      src_len_modulo = src_len % CHAR_BIT;
      if (src_len_modulo) {
        c = *src++ << bit_diff_ls;
        c |= *src >> bit_diff_rs;
        c &= mask[src_len_modulo];

        *dst &= mask_xor[src_len_modulo];
        *dst |= c;
      }
    }
  }
}