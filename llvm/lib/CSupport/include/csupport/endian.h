/*===-- csupport/endian.h - Byte order utilities ----------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_ENDIAN_H
#define CSUPPORT_ENDIAN_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint16_t csupport_byteswap16(uint16_t x) {
  return (uint16_t)((x >> 8) | (x << 8));
}

static inline uint32_t csupport_byteswap32(uint32_t x) {
#if __has_builtin(__builtin_bswap32)
  return __builtin_bswap32(x);
#else
  return ((x >> 24) & 0xFFU) | ((x >> 8) & 0xFF00U) | ((x << 8) & 0xFF0000U) |
         ((x << 24) & 0xFF000000U);
#endif
}

static inline uint64_t csupport_byteswap64(uint64_t x) {
#if __has_builtin(__builtin_bswap64)
  return __builtin_bswap64(x);
#else
  return ((x >> 56) & 0xFFULL) | ((x >> 40) & 0xFF00ULL) |
         ((x >> 24) & 0xFF0000ULL) | ((x >> 8) & 0xFF000000ULL) |
         ((x << 8) & 0xFF00000000ULL) | ((x << 24) & 0xFF0000000000ULL) |
         ((x << 40) & 0xFF000000000000ULL) |
         ((x << 56) & 0xFF00000000000000ULL);
#endif
}

static inline uint32_t csupport_read32le(const void *p) {
  uint32_t val;
  memcpy(&val, p, sizeof(val));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  val = csupport_byteswap32(val);
#endif
  return val;
}

static inline uint64_t csupport_read64le(const void *p) {
  uint64_t val;
  memcpy(&val, p, sizeof(val));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  val = csupport_byteswap64(val);
#endif
  return val;
}

static inline void csupport_write32le(void *p, uint32_t val) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  val = csupport_byteswap32(val);
#endif
  memcpy(p, &val, sizeof(val));
}

static inline void csupport_write64le(void *p, uint64_t val) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  val = csupport_byteswap64(val);
#endif
  memcpy(p, &val, sizeof(val));
}

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_ENDIAN_H */
