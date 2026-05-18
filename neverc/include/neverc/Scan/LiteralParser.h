#ifndef NEVERC_LEX_LITERALPARSER_H
#define NEVERC_LEX_LITERALPARSER_H

#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace neverc {

class DiagnosticsEngine;
class PrepEngine;
class Token;
class SourceLocation;
class TargetInfo;
class SourceManager;
class LangOptions;

void expandUCNs(llvm::SmallVectorImpl<char> &Buf, llvm::StringRef Input);

bool isFunctionLocalStringLiteralMacro(tok::TokenKind K, const LangOptions &LO);

bool tokenIsLikeStringLiteral(const Token &Tok, const LangOptions &LO);

class NumericLiteralParser {
  const SourceManager &SM;
  const LangOptions &LangOpts;
  DiagnosticsEngine &Diags;

  const char *const ThisTokBegin;
  const char *const ThisTokEnd;
  const char *DigitsBegin, *SuffixBegin; // markers
  const char *s;                         // cursor

  unsigned radix;

  bool saw_exponent, saw_period, saw_fixed_point_suffix;

public:
  NumericLiteralParser(llvm::StringRef TokSpelling, SourceLocation TokLoc,
                       const SourceManager &SM, const LangOptions &LangOpts,
                       const TargetInfo &Target, DiagnosticsEngine &Diags);
  bool hadError : 1;
  bool isUnsigned : 1;
  bool isLong : 1; // This is *not* set for long long.
  bool isLongLong : 1;
  bool isSizeT : 1;         // 1z, 1uz (C23)
  bool isHalf : 1;          // 1.0h
  bool isFloat : 1;         // 1.0f
  bool isImaginary : 1;     // 1.0i
  bool isFloat16 : 1;       // 1.0f16
  bool isFloat128 : 1;      // 1.0q
  bool isFract : 1;         // 1.0hr/r/lr/uhr/ur/ulr
  bool isAccum : 1;         // 1.0hk/k/lk/uhk/uk/ulk
  bool isBitInt : 1;        // 1wb, 1uwb (C23)
  uint8_t MicrosoftInteger; // Microsoft suffix extension i8, i16, i32, or i64.

  bool isFixedPointLiteral() const {
    return (saw_period || saw_exponent) && saw_fixed_point_suffix;
  }

  bool isIntegerLiteral() const {
    return !saw_period && !saw_exponent && !isFixedPointLiteral();
  }
  bool isFloatingLiteral() const {
    return (saw_period || saw_exponent) && !isFixedPointLiteral();
  }

  unsigned getRadix() const { return radix; }

  bool getIntegerValue(llvm::APInt &Val);

  llvm::APFloat::opStatus getFloatValue(llvm::APFloat &Result);

  bool getFixedPointValue(llvm::APInt &StoreVal, unsigned Scale);

  llvm::StringRef getLiteralDigits() const {
    assert(!hadError && "cannot reliably get the literal digits with an error");
    return llvm::StringRef(DigitsBegin, SuffixBegin - DigitsBegin);
  }

private:
  void parseNumberStartingWithZero(SourceLocation TokLoc);
  void parseDecimalOrOctalCommon(SourceLocation TokLoc);

  static bool isDigitSeparator(char C) { return C == '\''; }

  bool containsDigits(const char *Start, const char *End) {
    return Start != End && (Start + 1 != End || !isDigitSeparator(Start[0]));
  }

  enum CheckSeparatorKind { CSK_BeforeDigits, CSK_AfterDigits };

  void checkSeparator(SourceLocation TokLoc, const char *Pos,
                      CheckSeparatorKind IsAfterDigits);

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  static bool allAllowedNEON(uint8x16_t Mask) {
    return vminvq_u8(Mask) == 0xff;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  static uint8x16_t isDecimalOrSeparatorNEON(uint8x16_t V) {
    uint8x16_t IsDigit =
        vandq_u8(vcgeq_u8(V, vdupq_n_u8('0')), vcleq_u8(V, vdupq_n_u8('9')));
    return vorrq_u8(IsDigit, vceqq_u8(V, vdupq_n_u8('\'')));
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  static uint8x16_t isHexOrSeparatorNEON(uint8x16_t V) {
    uint8x16_t IsDec = isDecimalOrSeparatorNEON(V);
    uint8x16_t Lower = vorrq_u8(V, vdupq_n_u8(0x20));
    uint8x16_t IsHexAlpha = vandq_u8(vcgeq_u8(Lower, vdupq_n_u8('a')),
                                     vcleq_u8(Lower, vdupq_n_u8('f')));
    return vorrq_u8(IsDec, IsHexAlpha);
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  static uint8x16_t isOctalOrSeparatorNEON(uint8x16_t V) {
    uint8x16_t IsOct =
        vandq_u8(vcgeq_u8(V, vdupq_n_u8('0')), vcleq_u8(V, vdupq_n_u8('7')));
    return vorrq_u8(IsOct, vceqq_u8(V, vdupq_n_u8('\'')));
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  static uint8x16_t isBinaryOrSeparatorNEON(uint8x16_t V) {
    uint8x16_t IsBin =
        vorrq_u8(vceqq_u8(V, vdupq_n_u8('0')), vceqq_u8(V, vdupq_n_u8('1')));
    return vorrq_u8(IsBin, vceqq_u8(V, vdupq_n_u8('\'')));
  }
#endif

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const char *skipHexDigits(const char *ptr) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    while (LLVM_LIKELY(ptr + 16 <= ThisTokEnd)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(ptr));
      if (!allAllowedNEON(isHexOrSeparatorNEON(V)))
        break;
      ptr += 16;
    }
#endif
    while (LLVM_LIKELY(ptr != ThisTokEnd)) {
      unsigned char C = static_cast<unsigned char>(*ptr);
      unsigned IsHex = (C - '0' < 10u) | ((C | 0x20) - 'a' < 6u);
      unsigned IsSep = C == '\'';
      if (LLVM_LIKELY(IsHex | IsSep))
        ++ptr;
      else
        break;
    }
    return ptr;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const char *skipOctalDigits(const char *ptr) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    while (LLVM_LIKELY(ptr + 16 <= ThisTokEnd)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(ptr));
      if (!allAllowedNEON(isOctalOrSeparatorNEON(V)))
        break;
      ptr += 16;
    }
#endif
    while (LLVM_LIKELY(ptr != ThisTokEnd)) {
      unsigned char C = static_cast<unsigned char>(*ptr);
      if (LLVM_LIKELY((C - '0' < 8u) | (C == '\'')))
        ++ptr;
      else
        break;
    }
    return ptr;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const char *skipDigits(const char *ptr) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    while (LLVM_LIKELY(ptr + 16 <= ThisTokEnd)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(ptr));
      if (!allAllowedNEON(isDecimalOrSeparatorNEON(V)))
        break;
      ptr += 16;
    }
#endif
    while (LLVM_LIKELY(ptr != ThisTokEnd)) {
      unsigned char C = static_cast<unsigned char>(*ptr);
      if (LLVM_LIKELY((C - '0' < 10u) | (C == '\'')))
        ++ptr;
      else
        break;
    }
    return ptr;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const char *skipBinaryDigits(const char *ptr) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    while (LLVM_LIKELY(ptr + 16 <= ThisTokEnd)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(ptr));
      if (!allAllowedNEON(isBinaryOrSeparatorNEON(V)))
        break;
      ptr += 16;
    }
#endif
    while (LLVM_LIKELY(ptr != ThisTokEnd)) {
      unsigned char C = static_cast<unsigned char>(*ptr);
      if (LLVM_LIKELY((C == '0') | (C == '1') | (C == '\'')))
        ++ptr;
      else
        break;
    }
    return ptr;
  }
};

class CharLiteralParser {
  uint64_t Value;
  tok::TokenKind Kind;
  bool IsMultiChar;
  bool HadError;

public:
  CharLiteralParser(const char *begin, const char *end, SourceLocation Loc,
                    PrepEngine &PP, tok::TokenKind kind);

  bool hadError() const { return HadError; }
  bool isOrdinary() const { return Kind == tok::char_constant; }
  bool isWide() const { return Kind == tok::wide_char_constant; }
  bool isUTF8() const { return Kind == tok::utf8_char_constant; }
  bool isUTF16() const { return Kind == tok::utf16_char_constant; }
  bool isUTF32() const { return Kind == tok::utf32_char_constant; }
  bool isMultiChar() const { return IsMultiChar; }
  uint64_t getValue() const { return Value; }
};

enum class StringLiteralEvalMethod {
  Evaluated,
  Unevaluated,
};

class StringLiteralParser {
  const SourceManager &SM;
  const LangOptions &Features;
  const TargetInfo &Target;
  DiagnosticsEngine *Diags;

  unsigned MaxTokenLength;
  unsigned SizeBound;
  unsigned CharByteWidth;
  tok::TokenKind Kind;
  llvm::SmallString<512> ResultBuf;
  char *ResultPtr; // cursor
  StringLiteralEvalMethod EvalMethod;

public:
  StringLiteralParser(llvm::ArrayRef<Token> StringToks, PrepEngine &PP,
                      StringLiteralEvalMethod StringMethod =
                          StringLiteralEvalMethod::Evaluated);
  StringLiteralParser(llvm::ArrayRef<Token> StringToks, const SourceManager &sm,
                      const LangOptions &features, const TargetInfo &target,
                      DiagnosticsEngine *diags = nullptr)
      : SM(sm), Features(features), Target(target), Diags(diags),
        MaxTokenLength(0), SizeBound(0), CharByteWidth(0), Kind(tok::unknown),
        ResultPtr(ResultBuf.data()),
        EvalMethod(StringLiteralEvalMethod::Evaluated), hadError(false) {
    init(StringToks);
  }

  bool hadError;

  llvm::StringRef getString() const {
    return llvm::StringRef(ResultBuf.data(), getStringLength());
  }
  unsigned getStringLength() const { return ResultPtr - ResultBuf.data(); }

  unsigned getNumStringChars() const {
    return getStringLength() / CharByteWidth;
  }
  unsigned getOffsetOfStringByte(const Token &TheTok, unsigned ByteNo) const;

  bool isOrdinary() const { return Kind == tok::string_literal; }
  bool isWide() const { return Kind == tok::wide_string_literal; }
  bool isUTF8() const { return Kind == tok::utf8_string_literal; }
  bool isUTF16() const { return Kind == tok::utf16_string_literal; }
  bool isUTF32() const { return Kind == tok::utf32_string_literal; }
  bool isUnevaluated() const {
    return EvalMethod == StringLiteralEvalMethod::Unevaluated;
  }

private:
  void init(llvm::ArrayRef<Token> StringToks);
  bool copyStringFragment(const Token &Tok, const char *TokBegin,
                          llvm::StringRef Fragment);
  void diagnoseLexingError(SourceLocation Loc);
};

} // end namespace neverc

#endif
