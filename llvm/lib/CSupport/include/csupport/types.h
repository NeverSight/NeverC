/*===-- csupport/types.h - C equivalents for LLVM Support types --*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_TYPES_H
#define CSUPPORT_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* MSVC compatibility for GCC/Clang builtins */
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_BitScanReverse, _BitScanReverse64)
#pragma intrinsic(_BitScanForward, _BitScanForward64)
#pragma intrinsic(__popcnt64)

static __forceinline int csupport_compat_clzll(unsigned __int64 x) {
  unsigned long index;
  _BitScanReverse64(&index, x);
  return 63 - (int)index;
}
static __forceinline int csupport_compat_ctzll(unsigned __int64 x) {
  unsigned long index;
  _BitScanForward64(&index, x);
  return (int)index;
}
static __forceinline int csupport_compat_clz(unsigned int x) {
  unsigned long index;
  _BitScanReverse(&index, x);
  return 31 - (int)index;
}
#define __builtin_clzll(x) csupport_compat_clzll(x)
#define __builtin_ctzll(x) csupport_compat_ctzll(x)
#define __builtin_clz(x) csupport_compat_clz(x)
#define __builtin_popcountll(x) ((int)__popcnt64(x))
#define CSUPPORT_FALLTHROUGH
#else
#define CSUPPORT_FALLTHROUGH __attribute__((fallthrough))
#endif /* _MSC_VER */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const uint8_t *data;
  size_t size;
} csupport_byte_array_ref_t;

typedef struct {
  uint8_t *data;
  size_t size;
} csupport_mutable_byte_array_ref_t;

typedef struct {
  const char *data;
  size_t length;
} csupport_string_ref_t;

typedef struct {
  char *data;
  size_t length;
} csupport_mutable_string_ref_t;

typedef int csupport_error_t;
#define CSUPPORT_SUCCESS 0
#define CSUPPORT_ERROR_INVALID_INPUT 1
#define CSUPPORT_ERROR_BUFFER_TOO_SMALL 2
#define CSUPPORT_ERROR_OUT_OF_MEMORY 3

static inline csupport_byte_array_ref_t
csupport_byte_array_ref(const uint8_t *data, size_t size) {
  csupport_byte_array_ref_t ref;
  ref.data = data;
  ref.size = size;
  return ref;
}

static inline csupport_string_ref_t csupport_string_ref(const char *data,
                                                        size_t length) {
  csupport_string_ref_t ref;
  ref.data = data;
  ref.length = length;
  return ref;
}

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_TYPES_H */
