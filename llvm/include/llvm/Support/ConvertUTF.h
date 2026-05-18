/*===--- ConvertUTF.h - Universal Character Names conversions ---------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *==------------------------------------------------------------------------==*/
/*
 * Copyright © 1991-2015 Unicode, Inc. All rights reserved.
 * Distributed under the Terms of Use in
 * http://www.unicode.org/copyright.html.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of the Unicode data files and any associated documentation
 * (the "Data Files") or Unicode software and any associated documentation
 * (the "Software") to deal in the Data Files or Software
 * without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, and/or sell copies of
 * the Data Files or Software, and to permit persons to whom the Data Files
 * or Software are furnished to do so, provided that
 * (a) this copyright and permission notice appear with all copies
 * of the Data Files or Software,
 * (b) this copyright and permission notice appear in associated
 * documentation, and
 * (c) there is clear notice in each modified Data File or in the Software
 * as well as in the documentation associated with the Data File(s) or
 * Software that the data or software has been modified.
 *
 * THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
 * ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT OF THIRD PARTY RIGHTS.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
 * NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
 * DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THE DATA FILES OR SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder
 * shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in these Data Files or Software without prior
 * written authorization of the copyright holder.
 */

/* ---------------------------------------------------------------------

    Conversions between UTF32, UTF-16, and UTF-8.  Header file.

    Several funtions are included here, forming a complete set of
    conversions between the three formats.  UTF-7 is not included
    here, but is handled in a separate source file.

    Each of these routines takes pointers to input buffers and output
    buffers.  The input buffers are const.

    Each routine converts the text between *sourceStart and sourceEnd,
    putting the result into the buffer between *targetStart and
    targetEnd. Note: the end pointers are *after* the last item: e.g.
    *(sourceEnd - 1) is the last item.

    The return result indicates whether the conversion was successful,
    and if not, whether the problem was in the source or target buffers.
    (Only the first encountered problem is indicated.)

    After the conversion, *sourceStart and *targetStart are both
    updated to point to the end of last text successfully converted in
    the respective buffers.

    Input parameters:
        sourceStart - pointer to a pointer to the source buffer.
                The contents of this are modified on return so that
                it points at the next thing to be converted.
        targetStart - similarly, pointer to pointer to the target buffer.
        sourceEnd, targetEnd - respectively pointers to the ends of the
                two buffers, for overflow checking only.

    These conversion functions take a ConversionFlags argument. When this
    flag is set to strict, both irregular sequences and isolated surrogates
    will cause an error.  When the flag is set to lenient, both irregular
    sequences and isolated surrogates are converted.

    Whether the flag is strict or lenient, all illegal sequences will cause
    an error return. This includes sequences such as: <F4 90 80 80>, <C0 80>,
    or <A0> in UTF-8, and values above 0x10FFFF in UTF-32. Conformant code
    must check for illegal sequences.

    When the flag is set to lenient, characters over 0x10FFFF are converted
    to the replacement character; otherwise (when the flag is set to strict)
    they constitute an error.

    Output parameters:
        The value "sourceIllegal" is returned from some routines if the input
        sequence is malformed.  When "sourceIllegal" is returned, the source
        value will point to the illegal value that caused the problem. E.g.,
        in UTF-8 when a sequence is malformed, it points to the start of the
        malformed sequence.

    Author: Mark E. Davis, 1994.
    Rev History: Rick McGowan, fixes & updates May 2001.
         Fixes & updates, Sept 2001.

------------------------------------------------------------------------ */

#ifndef LLVM_SUPPORT_CONVERTUTF_H
#define LLVM_SUPPORT_CONVERTUTF_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SwapByteOrder.h"
#include <stddef.h>
#include <string>

#if defined(_WIN32)
#include <system_error>
#endif

// Wrap everything in namespace llvm so that programs can link with llvm and
// their own version of the unicode libraries.

namespace llvm {

/* ---------------------------------------------------------------------
    The following 4 definitions are compiler-specific.
    The C standard does not guarantee that wchar_t has at least
    16 bits, so wchar_t is no less portable than unsigned short!
    All should be unsigned values to avoid sign extension during
    bit mask & shift operations.
------------------------------------------------------------------------ */

typedef unsigned int UTF32;    /* at least 32 bits */
typedef unsigned short UTF16;  /* at least 16 bits */
typedef unsigned char UTF8;    /* typically 8 bits */
typedef unsigned char Boolean; /* 0 or 1 */

/* Some fundamental constants */
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32 (UTF32)0x0010FFFF

#define UNI_MAX_UTF8_BYTES_PER_CODE_POINT 4

#define UNI_UTF16_BYTE_ORDER_MARK_NATIVE 0xFEFF
#define UNI_UTF16_BYTE_ORDER_MARK_SWAPPED 0xFFFE

#define UNI_UTF32_BYTE_ORDER_MARK_NATIVE 0x0000FEFF
#define UNI_UTF32_BYTE_ORDER_MARK_SWAPPED 0xFFFE0000

typedef enum {
  conversionOK,    /* conversion successful */
  sourceExhausted, /* partial character in source, but hit end */
  targetExhausted, /* insuff. room in target for conversion */
  sourceIllegal    /* source sequence is illegal/malformed */
} ConversionResult;

typedef enum { strictConversion = 0, lenientConversion } ConversionFlags;

ConversionResult ConvertUTF8toUTF16(const UTF8 **sourceStart,
                                    const UTF8 *sourceEnd, UTF16 **targetStart,
                                    UTF16 *targetEnd, ConversionFlags flags);

/**
 * Convert a partial UTF8 sequence to UTF32.  If the sequence ends in an
 * incomplete code unit sequence, returns \c sourceExhausted.
 */
ConversionResult ConvertUTF8toUTF32Partial(const UTF8 **sourceStart,
                                           const UTF8 *sourceEnd,
                                           UTF32 **targetStart,
                                           UTF32 *targetEnd,
                                           ConversionFlags flags);

/**
 * Convert a partial UTF8 sequence to UTF32.  If the sequence ends in an
 * incomplete code unit sequence, returns \c sourceIllegal.
 */
ConversionResult ConvertUTF8toUTF32(const UTF8 **sourceStart,
                                    const UTF8 *sourceEnd, UTF32 **targetStart,
                                    UTF32 *targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF8(const UTF16 **sourceStart,
                                    const UTF16 *sourceEnd, UTF8 **targetStart,
                                    UTF8 *targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF32toUTF8(const UTF32 **sourceStart,
                                    const UTF32 *sourceEnd, UTF8 **targetStart,
                                    UTF8 *targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF32(const UTF16 **sourceStart,
                                     const UTF16 *sourceEnd,
                                     UTF32 **targetStart, UTF32 *targetEnd,
                                     ConversionFlags flags);

ConversionResult ConvertUTF32toUTF16(const UTF32 **sourceStart,
                                     const UTF32 *sourceEnd,
                                     UTF16 **targetStart, UTF16 *targetEnd,
                                     ConversionFlags flags);

Boolean isLegalUTF8Sequence(const UTF8 *source, const UTF8 *sourceEnd);

Boolean isLegalUTF8String(const UTF8 **source, const UTF8 *sourceEnd);

unsigned getUTF8SequenceSize(const UTF8 *source, const UTF8 *sourceEnd);

unsigned getNumBytesForUTF8(UTF8 firstByte);

/*************************************************************************/
/* Below are LLVM-specific wrappers of the functions above. */

template <typename T> class ArrayRef;
template <typename T> class SmallVectorImpl;
class StringRef;

/**
 * Convert an UTF8 StringRef to UTF8, UTF16, or UTF32 depending on
 * WideCharWidth. The converted data is written to ResultPtr, which needs to
 * point to at least WideCharWidth * (Source.Size() + 1) bytes. On success,
 * ResultPtr will point one after the end of the copied string. On failure,
 * ResultPtr will not be changed, and ErrorPtr will be set to the location of
 * the first character which could not be converted.
 * \return true on success.
 */
bool ConvertUTF8toWide(unsigned WideCharWidth, llvm::StringRef Source,
                       char *&ResultPtr, const UTF8 *&ErrorPtr);

/**
 * Converts a UTF-8 StringRef to a wide character buffer.
 * \return true on success.
 */
bool ConvertUTF8toWide(llvm::StringRef Source,
                       SmallVectorImpl<wchar_t> &Result);

/**
 * Converts a UTF-8 C-string to a wide character buffer.
 * \return true on success.
 */
bool ConvertUTF8toWide(const char *Source, SmallVectorImpl<wchar_t> &Result);

/**
 * Converts a wide character buffer to a UTF-8 encoded string buffer.
 * \return true on success.
 */
bool convertWideToUTF8(const wchar_t *Source, size_t SourceLen,
                       SmallVectorImpl<char> &Result);

/**
 * Convert an Unicode code point to UTF8 sequence.
 *
 * \param Source a Unicode code point.
 * \param [in,out] ResultPtr pointer to the output buffer, needs to be at least
 * \c UNI_MAX_UTF8_BYTES_PER_CODE_POINT bytes.  On success \c ResultPtr is
 * updated one past end of the converted sequence.
 *
 * \returns true on success.
 */
bool ConvertCodePointToUTF8(unsigned Source, char *&ResultPtr);

/**
 * Convert the first UTF8 sequence in the given source buffer to a UTF32
 * code point.
 *
 * \param [in,out] source A pointer to the source buffer. If the conversion
 * succeeds, this pointer will be updated to point to the byte just past the
 * end of the converted sequence.
 * \param sourceEnd A pointer just past the end of the source buffer.
 * \param [out] target The converted code
 * \param flags Whether the conversion is strict or lenient.
 *
 * \returns conversionOK on success
 *
 * \sa ConvertUTF8toUTF32
 */
inline ConversionResult convertUTF8Sequence(const UTF8 **source,
                                            const UTF8 *sourceEnd,
                                            UTF32 *target,
                                            ConversionFlags flags) {
  if (*source == sourceEnd)
    return sourceExhausted;
  unsigned size = getNumBytesForUTF8(**source);
  if ((ptrdiff_t)size > sourceEnd - *source)
    return sourceExhausted;
  return ConvertUTF8toUTF32(source, *source + size, &target, target + 1, flags);
}

/**
 * Returns true if a blob of text starts with a UTF-16 big or little endian byte
 * order mark.
 */
bool hasUTF16ByteOrderMark(ArrayRef<char> SrcBytes);

/**
 * Converts a stream of raw bytes assumed to be UTF16 into a UTF8 std::string.
 *
 * \param [in] SrcBytes A buffer of what is assumed to be UTF-16 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF16ToUTF8String(ArrayRef<char> SrcBytes,
                              SmallVectorImpl<char> &Out);

/**
 * Converts a UTF16 string into a UTF8 string.
 *
 * \param [in] Src A buffer of UTF-16 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF16ToUTF8String(ArrayRef<UTF16> Src, SmallVectorImpl<char> &Out);

/**
 * Converts a stream of raw bytes assumed to be UTF32 into a UTF8 string.
 *
 * \param [in] SrcBytes A buffer of what is assumed to be UTF-32 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF32ToUTF8String(ArrayRef<char> SrcBytes,
                              SmallVectorImpl<char> &Out);

/**
 * Converts a UTF32 string into a UTF8 string.
 *
 * \param [in] Src A buffer of UTF-32 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
bool convertUTF32ToUTF8String(ArrayRef<UTF32> Src, SmallVectorImpl<char> &Out);

/**
 * Converts a UTF-8 string into a UTF-16 string with native endianness.
 *
 * \returns true on success
 */
bool convertUTF8ToUTF16String(StringRef SrcUTF8,
                              SmallVectorImpl<UTF16> &DstUTF16);

/**
 * Converts a GBK string into a UTF-16 string.
 *
 * \returns true on success
 */
bool convertGBKToUTF8String(StringRef SrcGBK, SmallVectorImpl<char> &Out);

#if defined(_WIN32)
namespace sys {
namespace windows {
std::error_code UTF8ToUTF16(StringRef utf8, SmallVectorImpl<wchar_t> &utf16);
/// Convert to UTF16 from the current code page used in the system
std::error_code CurCPToUTF16(StringRef utf8, SmallVectorImpl<wchar_t> &utf16);
std::error_code UTF16ToUTF8(const wchar_t *utf16, size_t utf16_len,
                            SmallVectorImpl<char> &utf8);
/// Convert from UTF16 to the current code page used in the system
std::error_code UTF16ToCurCP(const wchar_t *utf16, size_t utf16_len,
                             SmallVectorImpl<char> &utf8);
} // namespace windows
} // namespace sys
#endif

} /* end namespace llvm */

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

namespace llvm {

inline bool ConvertUTF8toWide(unsigned WideCharWidth, llvm::StringRef Source,
                              char *&ResultPtr, const UTF8 *&ErrorPtr) {
  assert(WideCharWidth == 1 || WideCharWidth == 2 || WideCharWidth == 4);
  ConversionResult result = conversionOK;
  if (WideCharWidth == 1) {
    const UTF8 *Pos = (const UTF8 *)(Source.begin());
    if (!isLegalUTF8String(&Pos, (const UTF8 *)(Source.end()))) {
      result = sourceIllegal;
      ErrorPtr = Pos;
    } else {
      memcpy(ResultPtr, Source.data(), Source.size());
      ResultPtr += Source.size();
    }
  } else if (WideCharWidth == 2) {
    const UTF8 *sourceStart = (const UTF8 *)Source.data();
    UTF16 *targetStart = (UTF16 *)(ResultPtr);
    ConversionFlags flags = strictConversion;
    result =
        ConvertUTF8toUTF16(&sourceStart, sourceStart + Source.size(),
                           &targetStart, targetStart + Source.size(), flags);
    if (result == conversionOK)
      ResultPtr = (char *)(targetStart);
    else
      ErrorPtr = sourceStart;
  } else if (WideCharWidth == 4) {
    const UTF8 *sourceStart = (const UTF8 *)Source.data();
    UTF32 *targetStart = (UTF32 *)(ResultPtr);
    ConversionFlags flags = strictConversion;
    result =
        ConvertUTF8toUTF32(&sourceStart, sourceStart + Source.size(),
                           &targetStart, targetStart + Source.size(), flags);
    if (result == conversionOK)
      ResultPtr = (char *)(targetStart);
    else
      ErrorPtr = sourceStart;
  }
  assert((result != targetExhausted) &&
         "ConvertUTF8toUTFXX exhausted target buffer");
  return result == conversionOK;
}

inline bool ConvertCodePointToUTF8(unsigned Source, char *&ResultPtr) {
  const UTF32 *SourceStart = &Source;
  const UTF32 *SourceEnd = SourceStart + 1;
  UTF8 *TargetStart = (UTF8 *)(ResultPtr);
  UTF8 *TargetEnd = TargetStart + 4;
  ConversionResult CR = ConvertUTF32toUTF8(
      &SourceStart, SourceEnd, &TargetStart, TargetEnd, strictConversion);
  if (CR != conversionOK)
    return false;

  ResultPtr = (char *)(TargetStart);
  return true;
}

inline bool hasUTF16ByteOrderMark(ArrayRef<char> S) {
  return (S.size() >= 2 && ((S[0] == '\xff' && S[1] == '\xfe') ||
                            (S[0] == '\xfe' && S[1] == '\xff')));
}

inline bool convertUTF16ToUTF8String(ArrayRef<char> SrcBytes,
                                     SmallVectorImpl<char> &Out) {
  assert(Out.empty());

  if (SrcBytes.size() % 2)
    return false;

  if (SrcBytes.empty())
    return true;

  const UTF16 *Src = (const UTF16 *)(SrcBytes.begin());
  const UTF16 *SrcEnd = (const UTF16 *)(SrcBytes.end());

  assert((uintptr_t)Src % sizeof(UTF16) == 0);

  SmallVector<UTF16, 256> ByteSwapped;
  if (Src[0] == UNI_UTF16_BYTE_ORDER_MARK_SWAPPED) {
    ByteSwapped.insert(ByteSwapped.end(), Src, SrcEnd);
    for (UTF16 &I : ByteSwapped)
      I = llvm::byteswap<uint16_t>(I);
    Src = &ByteSwapped[0];
    SrcEnd = &ByteSwapped[ByteSwapped.size() - 1] + 1;
  }

  if (Src[0] == UNI_UTF16_BYTE_ORDER_MARK_NATIVE)
    Src++;

  Out.resize(SrcBytes.size() * UNI_MAX_UTF8_BYTES_PER_CODE_POINT + 1);
  UTF8 *Dst = (UTF8 *)(&Out[0]);
  UTF8 *DstEnd = Dst + Out.size();

  ConversionResult CR =
      ConvertUTF16toUTF8(&Src, SrcEnd, &Dst, DstEnd, strictConversion);
  assert(CR != targetExhausted);

  if (CR != conversionOK) {
    Out.clear();
    return false;
  }

  Out.resize((char *)(Dst) - &Out[0]);
  Out.push_back(0);
  Out.pop_back();
  return true;
}

inline bool convertUTF16ToUTF8String(ArrayRef<UTF16> Src,
                                     SmallVectorImpl<char> &Out) {
  return convertUTF16ToUTF8String(
      llvm::ArrayRef<char>((const char *)(Src.data()),
                           Src.size() * sizeof(UTF16)),
      Out);
}

inline bool convertUTF32ToUTF8String(ArrayRef<char> SrcBytes,
                                     SmallVectorImpl<char> &Out) {
  assert(Out.empty());

  if (SrcBytes.size() % 4)
    return false;

  if (SrcBytes.empty())
    return true;

  const UTF32 *Src = (const UTF32 *)(SrcBytes.begin());
  const UTF32 *SrcEnd = (const UTF32 *)(SrcBytes.end());

  assert((uintptr_t)Src % sizeof(UTF32) == 0);

  SmallVector<UTF32, 256> ByteSwapped;
  if (Src[0] == UNI_UTF32_BYTE_ORDER_MARK_SWAPPED) {
    ByteSwapped.insert(ByteSwapped.end(), Src, SrcEnd);
    for (UTF32 &I : ByteSwapped)
      I = llvm::byteswap<uint32_t>(I);
    Src = &ByteSwapped[0];
    SrcEnd = &ByteSwapped[ByteSwapped.size() - 1] + 1;
  }

  if (Src[0] == UNI_UTF32_BYTE_ORDER_MARK_NATIVE)
    Src++;

  Out.resize(SrcBytes.size() * UNI_MAX_UTF8_BYTES_PER_CODE_POINT + 1);
  UTF8 *Dst = (UTF8 *)(&Out[0]);
  UTF8 *DstEnd = Dst + Out.size();

  ConversionResult CR =
      ConvertUTF32toUTF8(&Src, SrcEnd, &Dst, DstEnd, strictConversion);
  assert(CR != targetExhausted);

  if (CR != conversionOK) {
    Out.clear();
    return false;
  }

  Out.resize((char *)(Dst) - &Out[0]);
  Out.push_back(0);
  Out.pop_back();
  return true;
}

inline bool convertUTF32ToUTF8String(ArrayRef<UTF32> Src,
                                     SmallVectorImpl<char> &Out) {
  return convertUTF32ToUTF8String(
      llvm::ArrayRef<char>((const char *)(Src.data()),
                           Src.size() * sizeof(UTF32)),
      Out);
}

inline bool convertUTF8ToUTF16String(StringRef SrcUTF8,
                                     SmallVectorImpl<UTF16> &DstUTF16) {
  assert(DstUTF16.empty());

  if (SrcUTF8.empty()) {
    DstUTF16.push_back(0);
    DstUTF16.pop_back();
    return true;
  }

  const UTF8 *Src = (const UTF8 *)(SrcUTF8.begin());
  const UTF8 *SrcEnd = (const UTF8 *)(SrcUTF8.end());

  DstUTF16.resize(SrcUTF8.size() + 1);
  UTF16 *Dst = &DstUTF16[0];
  UTF16 *DstEnd = Dst + DstUTF16.size();

  ConversionResult CR =
      ConvertUTF8toUTF16(&Src, SrcEnd, &Dst, DstEnd, strictConversion);
  assert(CR != targetExhausted);

  if (CR != conversionOK) {
    DstUTF16.clear();
    return false;
  }

  DstUTF16.resize(Dst - &DstUTF16[0]);
  DstUTF16.push_back(0);
  DstUTF16.pop_back();
  return true;
}

} // namespace llvm
extern "C" {
int csupport_has_gbk(const char *data, size_t len);
int csupport_convert_gbk_to_utf8(const char *src, size_t src_len, char *dst,
                                 size_t dst_cap, size_t *out_len);
}
namespace llvm {

inline bool convertGBKToUTF8String(StringRef SrcGBK,
                                   SmallVectorImpl<char> &Out) {
  if (!csupport_has_gbk(SrcGBK.data(), SrcGBK.size())) {
    return false;
  }

  char buf[4096];
  size_t written = 0;
  size_t cap = sizeof(buf);
  char *heap = 0;
  if (SrcGBK.size() * 3 > cap) {
    cap = SrcGBK.size() * 3 + 1;
    heap = (char *)malloc(cap);
    if (!heap)
      return false;
  }
  char *dst = heap ? heap : buf;
  if (!csupport_convert_gbk_to_utf8(SrcGBK.data(), SrcGBK.size(), dst, cap,
                                    &written)) {
    free(heap);
    return false;
  }
  Out.assign(dst, dst + written);
  free(heap);
  return true;
}

static_assert(sizeof(wchar_t) == 1 || sizeof(wchar_t) == 2 ||
                  sizeof(wchar_t) == 4,
              "Expected wchar_t to be 1, 2, or 4 bytes");

inline bool ConvertUTF8toWide(llvm::StringRef Source,
                              SmallVectorImpl<wchar_t> &Result) {
  Result.resize(Source.size() + 1);
  char *ResultPtr = (char *)(&Result[0]);
  const UTF8 *ErrorPtr;
  if (!ConvertUTF8toWide(sizeof(wchar_t), Source, ResultPtr, ErrorPtr)) {
    Result.clear();
    return false;
  }
  Result.resize((wchar_t *)(ResultPtr) - &Result[0]);
  return true;
}

inline bool ConvertUTF8toWide(const char *Source,
                              SmallVectorImpl<wchar_t> &Result) {
  if (!Source) {
    Result.clear();
    return true;
  }
  return ConvertUTF8toWide(llvm::StringRef(Source), Result);
}

inline bool convertWideToUTF8(const wchar_t *Source, size_t SourceLen,
                              SmallVectorImpl<char> &Result) {
  if (sizeof(wchar_t) == 1) {
    const UTF8 *Start = (const UTF8 *)(Source);
    const UTF8 *End = (const UTF8 *)(Source + SourceLen);
    if (!isLegalUTF8String(&Start, End))
      return false;
    Result.resize(SourceLen);
    memcpy(Result.data(), Source, SourceLen);
    return true;
  } else if (sizeof(wchar_t) == 2) {
    return convertUTF16ToUTF8String(
        llvm::ArrayRef<UTF16>((const UTF16 *)(Source), SourceLen), Result);
  } else if (sizeof(wchar_t) == 4) {
    const UTF32 *Start = (const UTF32 *)(Source);
    const UTF32 *End = (const UTF32 *)(Source + SourceLen);
    Result.resize(UNI_MAX_UTF8_BYTES_PER_CODE_POINT * SourceLen);
    UTF8 *ResultPtr = (UTF8 *)(Result.data());
    UTF8 *ResultEnd = (UTF8 *)(Result.data() + Result.size());
    if (ConvertUTF32toUTF8(&Start, End, &ResultPtr, ResultEnd,
                           strictConversion) == conversionOK) {
      Result.resize((char *)(ResultPtr)-Result.data());
      return true;
    } else {
      Result.clear();
      return false;
    }
  } else {
    llvm_unreachable(
        "Control should never reach this point; see static_assert further up");
  }
}

} // end namespace llvm

extern "C" {
typedef unsigned int CU32;
typedef unsigned short CU16;
typedef unsigned char CU8;
typedef enum { cOK = 0, cSrcExh, cTgtExh, cSrcIll } CConvRes;
typedef enum { cStrict = 0, cLenient } CConvFlag;
CConvRes ConvertUTF32toUTF16(const CU32 **, const CU32 *, CU16 **, CU16 *,
                             CConvFlag);
CConvRes ConvertUTF16toUTF32(const CU16 **, const CU16 *, CU32 **, CU32 *,
                             CConvFlag);
CConvRes ConvertUTF16toUTF8(const CU16 **, const CU16 *, CU8 **, CU8 *,
                            CConvFlag);
CConvRes ConvertUTF32toUTF8(const CU32 **, const CU32 *, CU8 **, CU8 *,
                            CConvFlag);
CConvRes ConvertUTF8toUTF16(const CU8 **, const CU8 *, CU16 **, CU16 *,
                            CConvFlag);
CConvRes ConvertUTF8toUTF32Partial(const CU8 **, const CU8 *, CU32 **, CU32 *,
                                   CConvFlag);
CConvRes ConvertUTF8toUTF32(const CU8 **, const CU8 *, CU32 **, CU32 *,
                            CConvFlag);
int isLegalUTF8Sequence(const CU8 *, const CU8 *);
int isLegalUTF8String(const CU8 **, const CU8 *);
unsigned getNumBytesForUTF8(CU8);
unsigned getUTF8SequenceSize(const CU8 *, const CU8 *);
}

namespace llvm {
inline ConversionResult ConvertUTF32toUTF16(const UTF32 **a, const UTF32 *b,
                                            UTF16 **c, UTF16 *d,
                                            ConversionFlags f) {
  return (ConversionResult)::ConvertUTF32toUTF16(
      (const CU32 **)a, (const CU32 *)b, (CU16 **)c, (CU16 *)d, (CConvFlag)f);
}
inline ConversionResult ConvertUTF16toUTF32(const UTF16 **a, const UTF16 *b,
                                            UTF32 **c, UTF32 *d,
                                            ConversionFlags f) {
  return (ConversionResult)::ConvertUTF16toUTF32(
      (const CU16 **)a, (const CU16 *)b, (CU32 **)c, (CU32 *)d, (CConvFlag)f);
}
inline ConversionResult ConvertUTF16toUTF8(const UTF16 **a, const UTF16 *b,
                                           UTF8 **c, UTF8 *d,
                                           ConversionFlags f) {
  return (ConversionResult)::ConvertUTF16toUTF8(
      (const CU16 **)a, (const CU16 *)b, c, d, (CConvFlag)f);
}
inline ConversionResult ConvertUTF32toUTF8(const UTF32 **a, const UTF32 *b,
                                           UTF8 **c, UTF8 *d,
                                           ConversionFlags f) {
  return (ConversionResult)::ConvertUTF32toUTF8(
      (const CU32 **)a, (const CU32 *)b, c, d, (CConvFlag)f);
}
inline ConversionResult ConvertUTF8toUTF16(const UTF8 **a, const UTF8 *b,
                                           UTF16 **c, UTF16 *d,
                                           ConversionFlags f) {
  return (ConversionResult)::ConvertUTF8toUTF16(a, b, (CU16 **)c, (CU16 *)d,
                                                (CConvFlag)f);
}
inline ConversionResult ConvertUTF8toUTF32Partial(const UTF8 **a, const UTF8 *b,
                                                  UTF32 **c, UTF32 *d,
                                                  ConversionFlags f) {
  return (ConversionResult)::ConvertUTF8toUTF32Partial(a, b, (CU32 **)c,
                                                       (CU32 *)d, (CConvFlag)f);
}
inline ConversionResult ConvertUTF8toUTF32(const UTF8 **a, const UTF8 *b,
                                           UTF32 **c, UTF32 *d,
                                           ConversionFlags f) {
  return (ConversionResult)::ConvertUTF8toUTF32(a, b, (CU32 **)c, (CU32 *)d,
                                                (CConvFlag)f);
}
inline Boolean isLegalUTF8Sequence(const UTF8 *a, const UTF8 *b) {
  return ::isLegalUTF8Sequence(a, b);
}
inline Boolean isLegalUTF8String(const UTF8 **a, const UTF8 *b) {
  return ::isLegalUTF8String(a, b);
}
inline unsigned getNumBytesForUTF8(UTF8 f) { return ::getNumBytesForUTF8(f); }
inline unsigned getUTF8SequenceSize(const UTF8 *a, const UTF8 *b) {
  return ::getUTF8SequenceSize(a, b);
}
} // end namespace llvm

#endif
