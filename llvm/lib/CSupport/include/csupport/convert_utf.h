/*===-- csupport/convert_utf.h - UTF conversion C ABI -----------*- C -*-===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions. See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef CSUPPORT_CONVERT_UTF_H
#define CSUPPORT_CONVERT_UTF_H

/* Shared by the C implementation and every C/C++ caller so the integer and
 * enum types at this ABI boundary cannot drift independently. */
typedef unsigned int csupport_utf32_t;
typedef unsigned short csupport_utf16_t;
typedef unsigned char csupport_utf8_t;
typedef unsigned char csupport_boolean_t;

typedef enum {
  CSUPPORT_CONVERSION_OK,
  CSUPPORT_SOURCE_EXHAUSTED,
  CSUPPORT_TARGET_EXHAUSTED,
  CSUPPORT_SOURCE_ILLEGAL
} csupport_conversion_result_t;

typedef enum {
  CSUPPORT_STRICT_CONVERSION = 0,
  CSUPPORT_LENIENT_CONVERSION
} csupport_conversion_flags_t;

#ifdef __cplusplus
extern "C" {
#endif

csupport_conversion_result_t ConvertUTF8toUTF16(
    const csupport_utf8_t **source_start, const csupport_utf8_t *source_end,
    csupport_utf16_t **target_start, csupport_utf16_t *target_end,
    csupport_conversion_flags_t flags);
csupport_conversion_result_t ConvertUTF8toUTF32Partial(
    const csupport_utf8_t **source_start, const csupport_utf8_t *source_end,
    csupport_utf32_t **target_start, csupport_utf32_t *target_end,
    csupport_conversion_flags_t flags);
csupport_conversion_result_t ConvertUTF8toUTF32(
    const csupport_utf8_t **source_start, const csupport_utf8_t *source_end,
    csupport_utf32_t **target_start, csupport_utf32_t *target_end,
    csupport_conversion_flags_t flags);
csupport_conversion_result_t
ConvertUTF16toUTF8(const csupport_utf16_t **source_start,
                   const csupport_utf16_t *source_end,
                   csupport_utf8_t **target_start, csupport_utf8_t *target_end,
                   csupport_conversion_flags_t flags);
csupport_conversion_result_t
ConvertUTF32toUTF8(const csupport_utf32_t **source_start,
                   const csupport_utf32_t *source_end,
                   csupport_utf8_t **target_start, csupport_utf8_t *target_end,
                   csupport_conversion_flags_t flags);
csupport_conversion_result_t ConvertUTF16toUTF32(
    const csupport_utf16_t **source_start, const csupport_utf16_t *source_end,
    csupport_utf32_t **target_start, csupport_utf32_t *target_end,
    csupport_conversion_flags_t flags);
csupport_conversion_result_t ConvertUTF32toUTF16(
    const csupport_utf32_t **source_start, const csupport_utf32_t *source_end,
    csupport_utf16_t **target_start, csupport_utf16_t *target_end,
    csupport_conversion_flags_t flags);

csupport_boolean_t isLegalUTF8Sequence(const csupport_utf8_t *source,
                                       const csupport_utf8_t *source_end);
csupport_boolean_t isLegalUTF8String(const csupport_utf8_t **source,
                                     const csupport_utf8_t *source_end);
unsigned getUTF8SequenceSize(const csupport_utf8_t *source,
                             const csupport_utf8_t *source_end);
unsigned getNumBytesForUTF8(csupport_utf8_t first_byte);

#ifdef __cplusplus
}
#endif

#endif
