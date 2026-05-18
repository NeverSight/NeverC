/*===- DJB.c - DJB Hash -----------------------------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#include "include/csupport/ld_lj_lb.h"
#include "include/csupport/lunicode_lcase_lfold.h"
#include <assert.h>
#include <stdbool.h>

typedef unsigned int UTF32;
typedef unsigned char UTF8;

typedef enum {
  cvt_conversionOK,
  cvt_sourceExhausted,
  cvt_targetExhausted,
  cvt_sourceIllegal
} CvtConversionResult;

typedef enum {
  cvt_strictConversion = 0,
  cvt_lenientConversion
} CvtConversionFlags;

CvtConversionResult ConvertUTF8toUTF32(const UTF8 **sourceStart,
                                       const UTF8 *sourceEnd,
                                       UTF32 **targetStart, UTF32 *targetEnd,
                                       CvtConversionFlags flags);
CvtConversionResult ConvertUTF32toUTF8(const UTF32 **sourceStart,
                                       const UTF32 *sourceEnd,
                                       UTF8 **targetStart, UTF8 *targetEnd,
                                       CvtConversionFlags flags);

#define UNI_MAX_UTF8_BYTES_PER_CODE_POINT 4

static UTF32 chop_one_utf32(const char **buf, size_t *len) {
  UTF32 c;
  const UTF8 *begin8 = (const UTF8 *)*buf;
  const UTF8 *begin8_start = begin8;
  UTF32 *begin32 = &c;

  assert(*len > 0);
  ConvertUTF8toUTF32(&begin8, (const UTF8 *)(*buf + *len), &begin32, &c + 1,
                     cvt_lenientConversion);
  size_t consumed = (size_t)(begin8 - begin8_start);
  *buf += consumed;
  *len -= consumed;
  return c;
}

static void to_utf8(UTF32 c, UTF8 *storage, const char **out_data,
                    size_t *out_len) {
  const UTF32 *begin32 = &c;
  UTF8 *begin8 = storage;

  ConvertUTF32toUTF8(&begin32, &c + 1, &begin8,
                     storage + UNI_MAX_UTF8_BYTES_PER_CODE_POINT,
                     cvt_strictConversion);
  *out_data = (const char *)storage;
  *out_len = (size_t)(begin8 - storage);
}

static UTF32 fold_char_dwarf(UTF32 c) {
  if (c == 0x130 || c == 0x131)
    return 'i';
  return (UTF32)csupport_fold_char_simple((int)c);
}

static bool fast_case_folding_djb_hash(const char *data, size_t len,
                                       uint32_t h, uint32_t *result) {
  bool all_ascii = true;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)data[i];
    h = h * 33 + ('A' <= c && c <= 'Z' ? c - 'A' + 'a' : c);
    all_ascii &= (c <= 0x7f);
  }
  if (all_ascii) {
    *result = h;
    return true;
  }
  return false;
}

static uint32_t case_folding_djb_hash_impl(const char *data, size_t len,
                                           uint32_t h) {
  uint32_t fast_result;
  if (fast_case_folding_djb_hash(data, len, h, &fast_result))
    return fast_result;

  UTF8 storage[UNI_MAX_UTF8_BYTES_PER_CODE_POINT];
  const char *ptr = data;
  size_t remaining = len;
  while (remaining > 0) {
    UTF32 c = fold_char_dwarf(chop_one_utf32(&ptr, &remaining));
    const char *folded_data;
    size_t folded_len;
    to_utf8(c, storage, &folded_data, &folded_len);
    h = csupport_djb_hash(folded_data, folded_len, h);
  }
  return h;
}

uint32_t csupport_case_folding_djb_hash(csupport_string_ref_t buffer,
                                        uint32_t h) {
  return case_folding_djb_hash_impl(buffer.data, buffer.length, h);
}

uint32_t csupport_case_folding_djb_hash_raw(const char *data, size_t length,
                                            uint32_t h) {
  return case_folding_djb_hash_impl(data, length, h);
}
