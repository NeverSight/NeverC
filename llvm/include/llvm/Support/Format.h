//===- Format.h - Efficient printf-style formatting for streams -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the format() function, which can be used with other
// LLVM subsystems to provide printf-style formatting.  This gives all the power
// and risk of printf.  This can be used like this (with raw_ostreams as an
// example):
//
//    OS << "mynumber: " << format("%4.5f", 1234.412) << '\n';
//
// Or if you prefer:
//
//  OS << format("mynumber: %4.5f\n", 1234.412);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMAT_H
#define LLVM_SUPPORT_FORMAT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include <assert.h>
#include <optional>
#include <stdio.h>
#include <tuple>
#include <utility>

namespace llvm {

/// This is a helper class used for handling formatted output.  It is the
/// abstract base class of a templated derived class.
class format_object_base {
protected:
  const char *Fmt;
  ~format_object_base() = default; // Disallow polymorphic deletion.
  format_object_base(const format_object_base &) = default;
  virtual void home(); // Out of line virtual method.

  /// Call snprintf() for this object, on the given buffer and size.
  virtual int snprint(char *Buffer, unsigned BufferSize) const = 0;

public:
  format_object_base(const char *fmt) : Fmt(fmt) {}

  /// Format the object into the specified buffer.  On success, this returns
  /// the length of the formatted string.  If the buffer is too small, this
  /// returns a length to retry with, which will be larger than BufferSize.
  unsigned print(char *Buffer, unsigned BufferSize) const {
    assert(BufferSize && "Invalid buffer size!");

    // Print the string, leaving room for the terminating null.
    int N = snprint(Buffer, BufferSize);

    // VC++ and old GlibC return negative on overflow, just double the size.
    if (N < 0)
      return BufferSize * 2;

    // Other implementations yield number of bytes needed, not including the
    // final '\0'.
    if (unsigned(N) >= BufferSize)
      return N + 1;

    // Otherwise N is the length of output (not including the final '\0').
    return N;
  }
};

/// These are templated helper classes used by the format function that
/// capture the object to be formatted and the format string. When actually
/// printed, this synthesizes the string into a temporary buffer provided and
/// returns whether or not it is big enough.

// Helper to validate that format() parameters are scalars or pointers.
template <typename... Args> struct validate_format_parameters;
template <typename Arg, typename... Args>
struct validate_format_parameters<Arg, Args...> {
  static_assert(std::is_scalar_v<Arg>,
                "format can't be used with non fundamental / non pointer type");
  validate_format_parameters() { validate_format_parameters<Args...>(); }
};
template <> struct validate_format_parameters<> {};

template <typename... Ts>
class format_object final : public format_object_base {
  std::tuple<Ts...> Vals;

  template <std::size_t... Is>
  int snprint_tuple(char *Buffer, unsigned BufferSize,
                    std::index_sequence<Is...>) const {
#ifdef _MSC_VER
    return _snprintf(Buffer, BufferSize, Fmt, std::get<Is>(Vals)...);
#else
    return snprintf(Buffer, BufferSize, Fmt, std::get<Is>(Vals)...);
#endif
  }

public:
  format_object(const char *fmt, const Ts &...vals)
      : format_object_base(fmt), Vals(vals...) {
    validate_format_parameters<Ts...>();
  }

  int snprint(char *Buffer, unsigned BufferSize) const override {
    return snprint_tuple(Buffer, BufferSize, std::index_sequence_for<Ts...>());
  }
};

/// These are helper functions used to produce formatted output.  They use
/// template type deduction to construct the appropriate instance of the
/// format_object class to simplify their construction.
///
/// This is typically used like:
/// \code
///   OS << format("%0.4f", myfloat) << '\n';
/// \endcode

template <typename... Ts>
inline format_object<Ts...> format(const char *Fmt, const Ts &...Vals) {
  return format_object<Ts...>(Fmt, Vals...);
}

/// This is a helper class for left_justify, right_justify, and center_justify.
class FormattedString {
public:
  enum Justification { JustifyNone, JustifyLeft, JustifyRight, JustifyCenter };
  FormattedString(StringRef S, unsigned W, Justification J)
      : Str(S), Width(W), Justify(J) {}

private:
  StringRef Str;
  unsigned Width;
  Justification Justify;
  friend class raw_ostream;
};

/// left_justify - append spaces after string so total output is
/// \p Width characters.  If \p Str is larger that \p Width, full string
/// is written with no padding.
inline FormattedString left_justify(StringRef Str, unsigned Width) {
  return FormattedString(Str, Width, FormattedString::JustifyLeft);
}

/// right_justify - add spaces before string so total output is
/// \p Width characters.  If \p Str is larger that \p Width, full string
/// is written with no padding.
inline FormattedString right_justify(StringRef Str, unsigned Width) {
  return FormattedString(Str, Width, FormattedString::JustifyRight);
}

/// center_justify - add spaces before and after string so total output is
/// \p Width characters.  If \p Str is larger that \p Width, full string
/// is written with no padding.
inline FormattedString center_justify(StringRef Str, unsigned Width) {
  return FormattedString(Str, Width, FormattedString::JustifyCenter);
}

/// This is a helper class used for format_hex() and format_decimal().
class FormattedNumber {
  uint64_t HexValue;
  int64_t DecValue;
  unsigned Width;
  bool Hex;
  bool Upper;
  bool HexPrefix;
  friend class raw_ostream;

public:
  FormattedNumber(uint64_t HV, int64_t DV, unsigned W, bool H, bool U,
                  bool Prefix)
      : HexValue(HV), DecValue(DV), Width(W), Hex(H), Upper(U),
        HexPrefix(Prefix) {}
};

/// format_hex - Output \p N as a fixed width hexadecimal. If number will not
/// fit in width, full number is still printed.  Examples:
///   OS << format_hex(255, 4)              => 0xff
///   OS << format_hex(255, 4, true)        => 0xFF
///   OS << format_hex(255, 6)              => 0x00ff
///   OS << format_hex(255, 2)              => 0xff
inline FormattedNumber format_hex(uint64_t N, unsigned Width,
                                  bool Upper = false) {
  assert(Width <= 18 && "hex width must be <= 18");
  return FormattedNumber(N, 0, Width, true, Upper, true);
}

/// format_hex_no_prefix - Output \p N as a fixed width hexadecimal. Does not
/// prepend '0x' to the outputted string.  If number will not fit in width,
/// full number is still printed.  Examples:
///   OS << format_hex_no_prefix(255, 2)              => ff
///   OS << format_hex_no_prefix(255, 2, true)        => FF
///   OS << format_hex_no_prefix(255, 4)              => 00ff
///   OS << format_hex_no_prefix(255, 1)              => ff
inline FormattedNumber format_hex_no_prefix(uint64_t N, unsigned Width,
                                            bool Upper = false) {
  assert(Width <= 16 && "hex width must be <= 16");
  return FormattedNumber(N, 0, Width, true, Upper, false);
}

/// format_decimal - Output \p N as a right justified, fixed-width decimal. If
/// number will not fit in width, full number is still printed.  Examples:
///   OS << format_decimal(0, 5)     => "    0"
///   OS << format_decimal(255, 5)   => "  255"
///   OS << format_decimal(-1, 3)    => " -1"
///   OS << format_decimal(12345, 3) => "12345"
inline FormattedNumber format_decimal(int64_t N, unsigned Width) {
  return FormattedNumber(0, N, Width, false, false, false);
}

class FormattedBytes {
  ArrayRef<uint8_t> Bytes;

  // If not std::nullopt, display offsets for each line relative to starting
  // value.
  std::optional<uint64_t> FirstByteOffset;
  uint32_t IndentLevel;  // Number of characters to indent each line.
  uint32_t NumPerLine;   // Number of bytes to show per line.
  uint8_t ByteGroupSize; // How many hex bytes are grouped without spaces
  bool Upper;            // Show offset and hex bytes as upper case.
  bool ASCII;            // Show the ASCII bytes for the hex bytes to the right.
  friend class raw_ostream;

public:
  FormattedBytes(ArrayRef<uint8_t> B, uint32_t IL, std::optional<uint64_t> O,
                 uint32_t NPL, uint8_t BGS, bool U, bool A)
      : Bytes(B), FirstByteOffset(O), IndentLevel(IL), NumPerLine(NPL),
        ByteGroupSize(BGS), Upper(U), ASCII(A) {

    if (ByteGroupSize > NumPerLine)
      ByteGroupSize = NumPerLine;
  }
};

inline FormattedBytes
format_bytes(ArrayRef<uint8_t> Bytes,
             std::optional<uint64_t> FirstByteOffset = std::nullopt,
             uint32_t NumPerLine = 16, uint8_t ByteGroupSize = 4,
             uint32_t IndentLevel = 0, bool Upper = false) {
  return FormattedBytes(Bytes, IndentLevel, FirstByteOffset, NumPerLine,
                        ByteGroupSize, Upper, false);
}

inline FormattedBytes
format_bytes_with_ascii(ArrayRef<uint8_t> Bytes,
                        std::optional<uint64_t> FirstByteOffset = std::nullopt,
                        uint32_t NumPerLine = 16, uint8_t ByteGroupSize = 4,
                        uint32_t IndentLevel = 0, bool Upper = false) {
  return FormattedBytes(Bytes, IndentLevel, FirstByteOffset, NumPerLine,
                        ByteGroupSize, Upper, true);
}

} // end namespace llvm

#include "csupport/raw_ostream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

inline void format_object_base::home() {}

inline raw_ostream &raw_ostream::operator<<(const format_object_base &Fmt) {
  size_t NextBufferSize = 127;
  size_t BufferBytesLeft = OutBufEnd - OutBufCur;
  if (BufferBytesLeft > 3) {
    size_t BytesUsed = Fmt.print(OutBufCur, BufferBytesLeft);
    if (BytesUsed <= BufferBytesLeft) {
      OutBufCur += BytesUsed;
      return *this;
    }
    NextBufferSize = BytesUsed;
  }
  SmallVector<char, 128> V;
  while (true) {
    V.resize(NextBufferSize);
    size_t BytesUsed = Fmt.print(V.data(), NextBufferSize);
    if (BytesUsed <= NextBufferSize)
      return write(V.data(), BytesUsed);
    assert(BytesUsed > NextBufferSize && "Didn't grow buffer!?");
    NextBufferSize = BytesUsed;
  }
}

inline raw_ostream &raw_ostream::operator<<(const FormattedString &FS) {
  char buf[4096];
  size_t n =
      csupport_format_justified(buf, sizeof(buf), FS.Str.data(), FS.Str.size(),
                                FS.Width, (int)FS.Justify);
  write(buf, n);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(const FormattedNumber &FN) {
  char buf[128];
  size_t n;
  if (FN.Hex) {
    n = csupport_format_number_hex(buf, sizeof(buf), FN.HexValue, FN.Width,
                                   FN.Upper, FN.HexPrefix);
  } else {
    n = csupport_format_number_decimal(buf, sizeof(buf), FN.DecValue, FN.Width);
  }
  write(buf, n);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(const FormattedBytes &FB) {
  if (FB.Bytes.empty())
    return *this;
  size_t LineIndex = 0;
  auto Bytes = FB.Bytes;
  const size_t Size = Bytes.size();
  HexPrintStyle HPS = FB.Upper ? HexPrintStyle::Upper : HexPrintStyle::Lower;
  uint64_t OffsetWidth = 0;
  if (FB.FirstByteOffset) {
    size_t Lines = Size / FB.NumPerLine;
    uint64_t MaxOffset = *FB.FirstByteOffset + Lines * FB.NumPerLine;
    unsigned Power = 0;
    if (MaxOffset > 0)
      Power = Log2_64_Ceil(MaxOffset);
    uint64_t Aligned = alignTo(Power, 4) / 4;
    OffsetWidth = 4 > Aligned ? 4 : Aligned;
  }
  unsigned NumByteGroups =
      alignTo(FB.NumPerLine, FB.ByteGroupSize) / FB.ByteGroupSize;
  unsigned BlockCharWidth = FB.NumPerLine * 2 + NumByteGroups - 1;
  while (!Bytes.empty()) {
    indent(FB.IndentLevel);
    if (FB.FirstByteOffset) {
      ::llvm::write_hex(*this, *FB.FirstByteOffset + LineIndex, HPS,
                        OffsetWidth);
      *this << ": ";
    }
    auto Line = Bytes.take_front(FB.NumPerLine);
    size_t CharsPrinted = 0;
    for (size_t I = 0; I < Line.size(); ++I, CharsPrinted += 2) {
      if (I && (I % FB.ByteGroupSize) == 0) {
        ++CharsPrinted;
        *this << " ";
      }
      ::llvm::write_hex(*this, Line[I], HPS, 2);
    }
    if (FB.ASCII) {
      assert(BlockCharWidth >= CharsPrinted);
      indent(BlockCharWidth - CharsPrinted + 2);
      *this << "|";
      for (uint8_t Byte : Line)
        *this << ((Byte >= 0x20 && Byte < 0x7f) ? (char)Byte : '.');
      *this << '|';
    }
    Bytes = Bytes.drop_front(Line.size());
    LineIndex += Line.size();
    if (LineIndex < Size)
      *this << '\n';
  }
  return *this;
}

} // namespace llvm

#endif
