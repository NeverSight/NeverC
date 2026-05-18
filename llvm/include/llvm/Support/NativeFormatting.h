//===- NativeFormatting.h - Low level formatting helpers ---------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_NATIVEFORMATTING_H
#define LLVM_SUPPORT_NATIVEFORMATTING_H

#include <optional>
#include <stdint.h>

namespace llvm {
class raw_ostream;
enum class FloatStyle { Exponent, ExponentUpper, Fixed, Percent };
enum class IntegerStyle {
  Integer,
  Number,
};
enum class HexPrintStyle { Upper, Lower, PrefixUpper, PrefixLower };

size_t getDefaultPrecision(FloatStyle Style);

bool isPrefixedHexStyle(HexPrintStyle S);

void write_integer(raw_ostream &S, unsigned int N, size_t MinDigits,
                   IntegerStyle Style);
void write_integer(raw_ostream &S, int N, size_t MinDigits, IntegerStyle Style);
void write_integer(raw_ostream &S, unsigned long N, size_t MinDigits,
                   IntegerStyle Style);
void write_integer(raw_ostream &S, long N, size_t MinDigits,
                   IntegerStyle Style);
void write_integer(raw_ostream &S, unsigned long long N, size_t MinDigits,
                   IntegerStyle Style);
void write_integer(raw_ostream &S, long long N, size_t MinDigits,
                   IntegerStyle Style);

void write_hex(raw_ostream &S, uint64_t N, HexPrintStyle Style,
               size_t Width = SIZE_MAX);
void write_double(raw_ostream &S, double D, FloatStyle Style,
                  size_t Precision = SIZE_MAX);

// === Inline implementations (moved from cpp_bridge.cpp) ===

#if defined(_WIN32) && !defined(__MINGW32__)
#include <float.h>
#endif

inline static void write_integer_via_c(raw_ostream &S, uint64_t N,
                                       size_t MinDigits, IntegerStyle Style,
                                       bool IsNegative) {
  char buf[192];
  int len =
      csupport_format_integer_to_buf(buf, sizeof(buf), N, MinDigits,
                                     Style == IntegerStyle::Number, IsNegative);
  S.write(buf, len);
}

inline void write_integer(raw_ostream &S, unsigned int N, size_t MinDigits,
                          IntegerStyle Style) {
  write_integer_via_c(S, N, MinDigits, Style, false);
}

inline void write_integer(raw_ostream &S, int N, size_t MinDigits,
                          IntegerStyle Style) {
  if (N >= 0) {
    write_integer_via_c(S, (unsigned)N, MinDigits, Style, false);
    return;
  }
  write_integer_via_c(S, (uint64_t)(-(int64_t)N), MinDigits, Style, true);
}

inline void write_integer(raw_ostream &S, unsigned long N, size_t MinDigits,
                          IntegerStyle Style) {
  write_integer_via_c(S, N, MinDigits, Style, false);
}

inline void write_integer(raw_ostream &S, long N, size_t MinDigits,
                          IntegerStyle Style) {
  if (N >= 0) {
    write_integer_via_c(S, (unsigned long)N, MinDigits, Style, false);
    return;
  }
  write_integer_via_c(S, (uint64_t)(-(int64_t)N), MinDigits, Style, true);
}

inline void write_integer(raw_ostream &S, unsigned long long N,
                          size_t MinDigits, IntegerStyle Style) {
  write_integer_via_c(S, N, MinDigits, Style, false);
}

inline void write_integer(raw_ostream &S, long long N, size_t MinDigits,
                          IntegerStyle Style) {
  if (N >= 0) {
    write_integer_via_c(S, (unsigned long long)N, MinDigits, Style, false);
    return;
  }
  write_integer_via_c(S, (uint64_t)(-N), MinDigits, Style, true);
}

inline void write_hex(raw_ostream &S, uint64_t N, HexPrintStyle Style,
                      size_t Width) {
  bool Prefix = (Style == HexPrintStyle::PrefixLower ||
                 Style == HexPrintStyle::PrefixUpper);
  bool Upper =
      (Style == HexPrintStyle::Upper || Style == HexPrintStyle::PrefixUpper);
  char buf[192];
  int len = csupport_format_hex_to_buf(buf, sizeof(buf), N, Upper, Prefix,
                                       (Width == SIZE_MAX) ? 0u : Width);
  S.write(buf, len);
}

inline void write_double(raw_ostream &S, double N, FloatStyle Style,
                         size_t Precision) {
  size_t Prec =
      (Precision == SIZE_MAX) ? getDefaultPrecision(Style) : Precision;
  char buf[64];
  int len =
      csupport_format_double_ex(buf, sizeof(buf), N, (int)(Style), (int)Prec);
  if (len > 0)
    S.write(buf, len);
}

inline bool isPrefixedHexStyle(HexPrintStyle S) {
  return (S == HexPrintStyle::PrefixLower || S == HexPrintStyle::PrefixUpper);
}

inline size_t getDefaultPrecision(FloatStyle Style) {
  return csupport_default_float_precision((int)(Style));
}

} // namespace llvm

#endif
