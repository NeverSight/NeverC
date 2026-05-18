#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Unicode.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef __AVX512BW__
#include <immintrin.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
const char *scanToSeparator(const char *Ptr, const char *End) {
#ifdef __AVX512BW__
  {
    const __m512i VS = _mm512_set1_epi8('\'');
    while (Ptr + 64 <= End) {
      __m512i V = _mm512_loadu_si512(Ptr);
      __mmask64 Hit = _mm512_cmpeq_epi8_mask(V, VS);
      if (Hit)
        return Ptr + _tzcnt_u64(Hit);
      Ptr += 64;
    }
  }
#endif
#if defined(__AVX2__) || defined(__AVX512BW__)
  {
    const __m256i VS = _mm256_set1_epi8('\'');
    while (Ptr + 32 <= End) {
      __m256i V = _mm256_loadu_si256((const __m256i *)Ptr);
      unsigned Hit =
          static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(V, VS)));
      if (Hit)
        return Ptr + llvm::countr_zero(Hit);
      Ptr += 32;
    }
  }
#elif defined(__SSE2__)
  {
    const __m128i VS = _mm_set1_epi8('\'');
    while (Ptr + 16 <= End) {
      __m128i V = _mm_loadu_si128((const __m128i *)Ptr);
      unsigned Hit =
          static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(V, VS)));
      if (Hit)
        return Ptr + llvm::countr_zero(Hit);
      Ptr += 16;
    }
  }
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t VS = vdupq_n_u8('\'');
    while (Ptr + 32 <= End) {
      uint8x16_t V0 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr));
      uint8x16_t V1 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr + 16));
      uint8x16_t H0 = vceqq_u8(V0, VS);
      uint8x16_t H1 = vceqq_u8(V1, VS);
      if (vmaxvq_u8(vorrq_u8(H0, H1))) {
        if (vmaxvq_u8(H0)) {
          uint64x2_t As64 = vreinterpretq_u64_u8(H0);
          uint64_t Lo = vgetq_lane_u64(As64, 0);
          if (Lo)
            return Ptr + (__builtin_ctzll(Lo) >> 3);
          return Ptr + 8 + (__builtin_ctzll(vgetq_lane_u64(As64, 1)) >> 3);
        }
        uint64x2_t As64 = vreinterpretq_u64_u8(H1);
        uint64_t Lo = vgetq_lane_u64(As64, 0);
        if (Lo)
          return Ptr + 16 + (__builtin_ctzll(Lo) >> 3);
        return Ptr + 24 + (__builtin_ctzll(vgetq_lane_u64(As64, 1)) >> 3);
      }
      Ptr += 32;
    }
    if (Ptr + 16 <= End) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr));
      uint8x16_t Hit = vceqq_u8(V, VS);
      if (vmaxvq_u8(Hit)) {
        uint64x2_t As64 = vreinterpretq_u64_u8(Hit);
        uint64_t Lo = vgetq_lane_u64(As64, 0);
        if (Lo)
          return Ptr + (__builtin_ctzll(Lo) >> 3);
        return Ptr + 8 + (__builtin_ctzll(vgetq_lane_u64(As64, 1)) >> 3);
      }
      Ptr += 16;
    }
  }
#else
  {
    constexpr uint64_t Broadcast = 0x0101010101010101ULL;
    constexpr uint64_t HiBitMask = 0x8080808080808080ULL;
    auto hasZeroByte = [](uint64_t V) -> uint64_t {
      return (V - 0x0101010101010101ULL) & ~V & 0x8080808080808080ULL;
    };
    while (Ptr + 16 <= End) {
      uint64_t W0, W1;
      __builtin_memcpy(&W0, Ptr, 8);
      __builtin_memcpy(&W1, Ptr + 8, 8);
      uint64_t S0 = hasZeroByte(W0 ^ (Broadcast * '\''));
      if (S0)
        return Ptr + (__builtin_ctzll(S0 & HiBitMask) >> 3);
      uint64_t S1 = hasZeroByte(W1 ^ (Broadcast * '\''));
      if (S1)
        return Ptr + 8 + (__builtin_ctzll(S1 & HiBitMask) >> 3);
      Ptr += 16;
    }
    if (Ptr + 8 <= End) {
      uint64_t Word;
      __builtin_memcpy(&Word, Ptr, 8);
      uint64_t Stops = hasZeroByte(Word ^ (Broadcast * '\''));
      if (Stops)
        return Ptr + (__builtin_ctzll(Stops & HiBitMask) >> 3);
      Ptr += 8;
    }
  }
#endif
  while (Ptr != End && *Ptr != '\'')
    ++Ptr;
  return Ptr;
}
} // namespace

// ===----------------------------------------------------------------------===
// NumericLiteralParser
// ===----------------------------------------------------------------------===

NumericLiteralParser::NumericLiteralParser(llvm::StringRef TokSpelling,
                                           SourceLocation TokLoc,
                                           const SourceManager &SM,
                                           const LangOptions &LangOpts,
                                           const TargetInfo &Target,
                                           DiagnosticsEngine &Diags)
    : SM(SM), LangOpts(LangOpts), Diags(Diags),
      ThisTokBegin(TokSpelling.begin()), ThisTokEnd(TokSpelling.end()) {

  s = DigitsBegin = ThisTokBegin;
  saw_exponent = false;
  saw_period = false;
  saw_fixed_point_suffix = false;
  isLong = false;
  isUnsigned = false;
  isLongLong = false;
  isSizeT = false;
  isHalf = false;
  isFloat = false;
  isImaginary = false;
  isFloat16 = false;
  isFloat128 = false;
  MicrosoftInteger = 0;
  isFract = false;
  isAccum = false;
  hadError = false;
  isBitInt = false;

  if (LLVM_UNLIKELY(isPreprocessingNumberBody(*ThisTokEnd))) {
    Diags.Report(TokLoc, diag::err_lexing_numeric);
    hadError = true;
    return;
  }

  if (*s == '0') { // parse radix
    parseNumberStartingWithZero(TokLoc);
    if (LLVM_UNLIKELY(hadError))
      return;
  } else { // the first digit is non-zero
    radix = 10;
    s = skipDigits(s);
    if (LLVM_LIKELY(s == ThisTokEnd)) {
      // Done.
    } else {
      parseDecimalOrOctalCommon(TokLoc);
      if (LLVM_UNLIKELY(hadError))
        return;
    }
  }

  SuffixBegin = s;
  checkSeparator(TokLoc, s, CSK_AfterDigits);

  if (LLVM_LIKELY(s == ThisTokEnd))
    return;

  // Pass 1: Build a presence bitmap of suffix character categories.
  // For the most common single-suffix cases (u, l, ll, f), we can
  // return immediately without running the full validation switch.
  {
    unsigned SuffixBits = 0;
    enum : unsigned { SC_U = 1, SC_L = 2, SC_F = 4, SC_Other = 8 };
    for (const char *p = s; p != ThisTokEnd; ++p) {
      switch (*p) {
      case 'u':
      case 'U':
        SuffixBits |= SC_U;
        break;
      case 'l':
      case 'L':
        SuffixBits |= SC_L;
        break;
      case 'f':
      case 'F':
        SuffixBits |= SC_F;
        break;
      default:
        SuffixBits |= SC_Other;
        break;
      }
    }

    if (LLVM_LIKELY(!(SuffixBits & SC_Other))) {
      const unsigned SuffixLen = ThisTokEnd - s;
      bool FP = isFloatingLiteral();
      if (SuffixBits == SC_U && SuffixLen == 1 && !FP) {
        isUnsigned = true;
        s = ThisTokEnd;
        return;
      }
      if (SuffixBits == SC_L && SuffixLen == 1) {
        isLong = true;
        s = ThisTokEnd;
        return;
      }
      if (SuffixBits == SC_L && SuffixLen == 2 && s[0] == s[1] && !FP) {
        isLongLong = true;
        s = ThisTokEnd;
        return;
      }
      if (SuffixBits == SC_F && SuffixLen == 1 && FP) {
        isFloat = true;
        s = ThisTokEnd;
        return;
      }
    }
  }

  // Pass 2: Full validation for complex or uncommon suffixes.
  if (LangOpts.FixedPoint) {
    for (const char *c = s; c != ThisTokEnd; ++c) {
      if (*c == 'r' || *c == 'k' || *c == 'R' || *c == 'K') {
        saw_fixed_point_suffix = true;
        break;
      }
    }
  }

  bool isFixedPointConstant = isFixedPointLiteral();
  bool isFPConstant = isFloatingLiteral();
  bool HasSize = false;

  {
    unsigned SuffixLen = ThisTokEnd - s;
    if (SuffixLen == 0) {
      s = ThisTokEnd;
    } else if (LLVM_LIKELY(SuffixLen == 1)) {
      switch (*s) {
      case 'u':
      case 'U':
        if (!isFPConstant && !isFixedPointConstant) {
          isUnsigned = true;
          s = ThisTokEnd;
        }
        break;
      case 'l':
      case 'L':
        isLong = true;
        HasSize = true;
        s = ThisTokEnd;
        break;
      case 'f':
      case 'F':
        if (isFPConstant) {
          isFloat = true;
          HasSize = true;
          s = ThisTokEnd;
        }
        break;
      default:
        break;
      }
    } else if (SuffixLen == 2 && !isFPConstant && !isFixedPointConstant) {
      char C0 = s[0], C1 = s[1];
      if ((C0 == 'u' || C0 == 'U') && (C1 == 'l' || C1 == 'L')) {
        isUnsigned = true;
        isLong = true;
        HasSize = true;
        s = ThisTokEnd;
      } else if ((C0 == 'l' || C0 == 'L') && (C1 == 'u' || C1 == 'U')) {
        isUnsigned = true;
        isLong = true;
        HasSize = true;
        s = ThisTokEnd;
      } else if (C0 == C1 && (C0 == 'l' || C0 == 'L')) {
        isLongLong = true;
        HasSize = true;
        s = ThisTokEnd;
      }
    } else if (SuffixLen == 3 && !isFPConstant && !isFixedPointConstant) {
      char C0 = s[0], C1 = s[1], C2 = s[2];
      if ((C0 == 'u' || C0 == 'U') && C1 == C2 && (C1 == 'l' || C1 == 'L')) {
        isUnsigned = true;
        isLongLong = true;
        HasSize = true;
        s = ThisTokEnd;
      } else if (C0 == C1 && (C0 == 'l' || C0 == 'L') &&
                 (C2 == 'u' || C2 == 'U')) {
        isUnsigned = true;
        isLongLong = true;
        HasSize = true;
        s = ThisTokEnd;
      }
    }
  }

  for (; s != ThisTokEnd; ++s) {
    switch (*s) {
    case 'R':
    case 'r':
      if (!LangOpts.FixedPoint)
        break;
      if (isFract || isAccum)
        break;
      if (!(saw_period || saw_exponent))
        break;
      isFract = true;
      continue;
    case 'K':
    case 'k':
      if (!LangOpts.FixedPoint)
        break;
      if (isFract || isAccum)
        break;
      if (!(saw_period || saw_exponent))
        break;
      isAccum = true;
      continue;
    case 'h': // FP Suffix for "half".
    case 'H':
      // h or H suffix for half type.
      if (!LangOpts.FixedPoint)
        break;
      if (isIntegerLiteral())
        break; // Error for integer constant.
      if (HasSize)
        break;
      HasSize = true;
      isHalf = true;
      continue; // Success.
    case 'f':   // FP Suffix for "float"
    case 'F':
      if (!isFPConstant)
        break; // Error for integer constant.
      if (HasSize)
        break;
      HasSize = true;

      // f16 suffix for _Float16 type.
      if (Target.hasFloat16Type() && s + 2 < ThisTokEnd && s[1] == '1' &&
          s[2] == '6') {
        s += 2; // success, eat up 2 characters.
        isFloat16 = true;
        continue;
      }

      isFloat = true;
      continue; // Success.
    case 'q':   // FP Suffix for "__float128"
    case 'Q':
      if (!isFPConstant)
        break; // Error for integer constant.
      if (HasSize)
        break;
      HasSize = true;
      isFloat128 = true;
      continue; // Success.
    case 'u':
    case 'U':
      if (isFPConstant)
        break; // Error for floating constant.
      if (isUnsigned)
        break; // Cannot be repeated.
      isUnsigned = true;
      continue; // Success.
    case 'l':
    case 'L':
      if (HasSize)
        break;
      HasSize = true;

      // Check for long long.  The L's need to be adjacent and the same case.
      if (s[1] == s[0]) {
        assert(s + 1 < ThisTokEnd && "didn't maximally munch?");
        if (isFPConstant)
          break; // long long invalid for floats.
        isLongLong = true;
        ++s; // Eat both of them.
      } else {
        isLong = true;
      }
      continue; // Success.
    case 'z':
    case 'Z':
      if (isFPConstant)
        break; // Invalid for floats.
      if (HasSize)
        break;
      HasSize = true;
      isSizeT = true;
      continue;
    case 'i':
    case 'I':
      if (LangOpts.MicrosoftExt && !isFPConstant) {
        // Allow i8, i16, i32, and i64. First, look ahead and check if
        // suffixes are Microsoft integers and not the imaginary unit.
        uint8_t Bits = 0;
        size_t ToSkip = 0;
        switch (s[1]) {
        case '8': // i8 suffix
          Bits = 8;
          ToSkip = 2;
          break;
        case '1':
          if (s[2] == '6') { // i16 suffix
            Bits = 16;
            ToSkip = 3;
          }
          break;
        case '3':
          if (s[2] == '2') { // i32 suffix
            Bits = 32;
            ToSkip = 3;
          }
          break;
        case '6':
          if (s[2] == '4') { // i64 suffix
            Bits = 64;
            ToSkip = 3;
          }
          break;
        default:
          break;
        }
        if (Bits) {
          if (HasSize)
            break;
          HasSize = true;
          MicrosoftInteger = Bits;
          s += ToSkip;
          assert(s <= ThisTokEnd && "didn't maximally munch?");
          break;
        }
      }
      [[fallthrough]];
    case 'j':
    case 'J':
      if (isImaginary)
        break; // Cannot be repeated.
      isImaginary = true;
      continue; // Success.
    case 'w':
    case 'W':
      if (isFPConstant)
        break; // Invalid for floats.
      if (HasSize)
        break; // Invalid if we already have a size for the literal.

      // wb and WB are allowed, but a mixture of cases like Wb or wB is not.
      if ((s[0] == 'w' && s[1] == 'b') || (s[0] == 'W' && s[1] == 'B')) {
        isBitInt = true;
        HasSize = true;
        ++s;      // Skip both characters (2nd char skipped on continue).
        continue; // Success.
      }
    }
    // If we reached here, there was an error.
    break;
  }

  if (s != ThisTokEnd) {
    Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                     TokLoc, SuffixBegin - ThisTokBegin, SM, LangOpts),
                 diag::err_invalid_suffix_constant)
        << llvm::StringRef(SuffixBegin, ThisTokEnd - SuffixBegin)
        << (isFixedPointConstant ? 2 : isFPConstant);
    hadError = true;
  }

  if (!hadError && saw_fixed_point_suffix) {
    assert(isFract || isAccum);
  }
}

void NumericLiteralParser::parseDecimalOrOctalCommon(SourceLocation TokLoc) {
  assert((radix == 8 || radix == 10) && "Unexpected radix");

  // If we have a hex digit other than 'e' (which denotes a FP exponent) then
  // the code is using an incorrect base.
  if (isHexDigit(*s) && *s != 'e' && *s != 'E') {
    Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                     TokLoc, s - ThisTokBegin, SM, LangOpts),
                 diag::err_invalid_digit)
        << llvm::StringRef(s, 1) << (radix == 8 ? 1 : 0);
    hadError = true;
    return;
  }

  if (*s == '.') {
    checkSeparator(TokLoc, s, CSK_AfterDigits);
    s++;
    radix = 10;
    saw_period = true;
    checkSeparator(TokLoc, s, CSK_BeforeDigits);
    s = skipDigits(s); // Skip suffix.
  }
  if (*s == 'e' || *s == 'E') { // exponent
    checkSeparator(TokLoc, s, CSK_AfterDigits);
    const char *Exponent = s;
    s++;
    radix = 10;
    saw_exponent = true;
    if (s != ThisTokEnd && (*s == '+' || *s == '-'))
      s++; // sign
    const char *first_non_digit = skipDigits(s);
    if (containsDigits(s, first_non_digit)) {
      checkSeparator(TokLoc, s, CSK_BeforeDigits);
      s = first_non_digit;
    } else {
      if (!hadError) {
        Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                         TokLoc, Exponent - ThisTokBegin, SM, LangOpts),
                     diag::err_exponent_has_no_digits);
        hadError = true;
      }
      return;
    }
  }
}

void NumericLiteralParser::checkSeparator(SourceLocation TokLoc,
                                          const char *Pos,
                                          CheckSeparatorKind IsAfterDigits) {
  if (IsAfterDigits == CSK_AfterDigits) {
    if (Pos == ThisTokBegin)
      return;
    --Pos;
  } else if (Pos == ThisTokEnd)
    return;

  if (isDigitSeparator(*Pos)) {
    Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                     TokLoc, Pos - ThisTokBegin, SM, LangOpts),
                 diag::err_digit_separator_not_between_digits)
        << IsAfterDigits;
    hadError = true;
  }
}

void NumericLiteralParser::parseNumberStartingWithZero(SourceLocation TokLoc) {
  assert(s[0] == '0' && "parseNumberStartingWithZero requires a leading zero");
  s++;

  int c1 = s[0];
  if ((c1 == 'x' || c1 == 'X') && (isHexDigit(s[1]) || s[1] == '.')) {
    s++;
    assert(s < ThisTokEnd && "didn't maximally munch?");
    radix = 16;
    DigitsBegin = s;
    s = skipHexDigits(s);
    bool HasSignificandDigits = containsDigits(DigitsBegin, s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (*s == '.') {
      s++;
      saw_period = true;
      const char *floatDigitsBegin = s;
      s = skipHexDigits(s);
      if (containsDigits(floatDigitsBegin, s))
        HasSignificandDigits = true;
      if (HasSignificandDigits)
        checkSeparator(TokLoc, floatDigitsBegin, CSK_BeforeDigits);
    }

    if (!HasSignificandDigits) {
      Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                       TokLoc, s - ThisTokBegin, SM, LangOpts),
                   diag::err_hex_constant_requires)
          << 0 << 1;
      hadError = true;
      return;
    }

    // A binary exponent can appear with or with a '.'. If dotted, the
    // binary exponent is required.
    if (*s == 'p' || *s == 'P') {
      checkSeparator(TokLoc, s, CSK_AfterDigits);
      const char *Exponent = s;
      s++;
      saw_exponent = true;
      if (s != ThisTokEnd && (*s == '+' || *s == '-'))
        s++; // sign
      const char *first_non_digit = skipDigits(s);
      if (!containsDigits(s, first_non_digit)) {
        if (!hadError) {
          Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                           TokLoc, Exponent - ThisTokBegin, SM, LangOpts),
                       diag::err_exponent_has_no_digits);
          hadError = true;
        }
        return;
      }
      checkSeparator(TokLoc, s, CSK_BeforeDigits);
      s = first_non_digit;

      if (!LangOpts.HexFloats)
        Diags.Report(TokLoc, diag::ext_hex_constant_invalid);
    } else if (saw_period) {
      Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                       TokLoc, s - ThisTokBegin, SM, LangOpts),
                   diag::err_hex_constant_requires)
          << 0 << 0;
      hadError = true;
    }
    return;
  }
  if ((c1 == 'b' || c1 == 'B') && (s[1] == '0' || s[1] == '1')) {
    // Binary literal (0b/0B prefix).
    Diags.Report(TokLoc, diag::ext_binary_literal);
    ++s;
    assert(s < ThisTokEnd && "didn't maximally munch?");
    radix = 2;
    DigitsBegin = s;
    s = skipBinaryDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (isHexDigit(*s)) {
      Diags.Report(SourceScanner::AdvanceToTokenCharacter(
                       TokLoc, s - ThisTokBegin, SM, LangOpts),
                   diag::err_invalid_digit)
          << llvm::StringRef(s, 1) << 2;
      hadError = true;
    }
    // Other suffixes will be diagnosed by the caller.
    return;
  }

  // For now, the radix is set to 8. If we discover that we have a
  // floating point constant, the radix will change to 10. Octal floating
  // point constants are not permitted (only decimal and hexadecimal).
  radix = 8;
  const char *PossibleNewDigitStart = s;
  s = skipOctalDigits(s);
  // When the value is 0 followed by a suffix (like 0wb), we want to leave 0
  // as the start of the digits. So if skipping octal digits does not skip
  // anything, we leave the digit start where it was.
  if (s != PossibleNewDigitStart)
    DigitsBegin = PossibleNewDigitStart;

  if (s == ThisTokEnd)
    return; // Done, simple octal number like 01234

  // If we have some other non-octal digit that *is* a decimal digit, see if
  // this is part of a floating point number like 094.123 or 09e1.
  if (isDigit(*s)) {
    const char *EndDecimal = skipDigits(s);
    if (EndDecimal[0] == '.' || EndDecimal[0] == 'e' || EndDecimal[0] == 'E') {
      s = EndDecimal;
      radix = 10;
    }
  }

  parseDecimalOrOctalCommon(TokLoc);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool canFitIn64Bits(unsigned Radix, unsigned NumDigits) {
  switch (Radix) {
  case 2:
    return NumDigits <= 64;
  case 8:
    return NumDigits <= 64 / 3; // Digits are groups of 3 bits.
  case 10:
    return NumDigits <= 19; // floor(log10(2^64))
  case 16:
    return NumDigits <= 64 / 4; // Digits are groups of 4 bits.
  default:
    llvm_unreachable("impossible Radix");
  }
}
} // namespace

bool NumericLiteralParser::getIntegerValue(llvm::APInt &Val) {
  const unsigned NumDigits = SuffixBegin - DigitsBegin;
  if (canFitIn64Bits(radix, NumDigits)) {
    uint64_t N = 0;
    bool HasSeparator = memchr(DigitsBegin, '\'', NumDigits) != nullptr;
    if (LLVM_LIKELY(!HasSeparator)) {
      const char *Ptr = DigitsBegin;
      if (LLVM_LIKELY(radix == 10)) {
        const char *End = SuffixBegin;
        while (Ptr + 8 <= End) {
          N = N * 100000000ULL +
              static_cast<uint64_t>(Ptr[0] - '0') * 10000000 +
              static_cast<uint64_t>(Ptr[1] - '0') * 1000000 +
              static_cast<uint64_t>(Ptr[2] - '0') * 100000 +
              static_cast<uint64_t>(Ptr[3] - '0') * 10000 +
              (Ptr[4] - '0') * 1000 + (Ptr[5] - '0') * 100 +
              (Ptr[6] - '0') * 10 + (Ptr[7] - '0');
          Ptr += 8;
        }
        while (Ptr + 4 <= End) {
          N = N * 10000 + (Ptr[0] - '0') * 1000 + (Ptr[1] - '0') * 100 +
              (Ptr[2] - '0') * 10 + (Ptr[3] - '0');
          Ptr += 4;
        }
        while (Ptr != End)
          N = N * 10 + (*Ptr++ - '0');
      } else if (radix == 16) {
        while (Ptr + 8 <= SuffixBegin) {
          N = (N << 32) |
              (static_cast<uint64_t>(llvm::hexDigitValue(Ptr[0])) << 28) |
              (static_cast<uint64_t>(llvm::hexDigitValue(Ptr[1])) << 24) |
              (static_cast<uint64_t>(llvm::hexDigitValue(Ptr[2])) << 20) |
              (static_cast<uint64_t>(llvm::hexDigitValue(Ptr[3])) << 16) |
              (llvm::hexDigitValue(Ptr[4]) << 12) |
              (llvm::hexDigitValue(Ptr[5]) << 8) |
              (llvm::hexDigitValue(Ptr[6]) << 4) | llvm::hexDigitValue(Ptr[7]);
          Ptr += 8;
        }
        while (Ptr + 2 <= SuffixBegin) {
          N = (N << 8) | (llvm::hexDigitValue(Ptr[0]) << 4) |
              llvm::hexDigitValue(Ptr[1]);
          Ptr += 2;
        }
        for (; Ptr != SuffixBegin; ++Ptr)
          N = (N << 4) | llvm::hexDigitValue(*Ptr);
      } else if (radix == 2) {
        for (; Ptr != SuffixBegin; ++Ptr)
          N = (N << 1) | (*Ptr - '0');
      } else {
        for (; Ptr != SuffixBegin; ++Ptr)
          N = N * radix + llvm::hexDigitValue(*Ptr);
      }
    } else {
      for (const char *Ptr = DigitsBegin; Ptr != SuffixBegin; ++Ptr)
        if (!isDigitSeparator(*Ptr))
          N = N * radix + llvm::hexDigitValue(*Ptr);
    }
    Val = N;
    return Val.getZExtValue() != N;
  }

  Val = 0;
  const char *Ptr = DigitsBegin;

  llvm::APInt RadixVal(Val.getBitWidth(), radix);
  llvm::APInt CharVal(Val.getBitWidth(), 0);
  llvm::APInt OldVal = Val;

  bool OverflowOccurred = false;
  while (Ptr < SuffixBegin) {
    if (isDigitSeparator(*Ptr)) {
      ++Ptr;
      continue;
    }

    unsigned C = llvm::hexDigitValue(*Ptr++);

    // If this letter is out of bound for this radix, reject it.
    assert(C < radix && "NumericLiteralParser ctor should have rejected this");

    CharVal = C;

    // Add the digit to the value in the appropriate radix.  If adding in digits
    // made the value smaller, then this overflowed.
    OldVal = Val;

    // Multiply by radix, did overflow occur on the multiply?
    Val *= RadixVal;
    OverflowOccurred |= Val.udiv(RadixVal) != OldVal;

    // Add value, did overflow occur on the value?
    //   (a + b) ult b  <=> overflow
    Val += CharVal;
    OverflowOccurred |= Val.ult(CharVal);
  }
  return OverflowOccurred;
}

llvm::APFloat::opStatus
NumericLiteralParser::getFloatValue(llvm::APFloat &Result) {
  using llvm::APFloat;

  unsigned n = std::min(SuffixBegin - ThisTokBegin, ThisTokEnd - ThisTokBegin);

  llvm::SmallString<16> Buffer;
  llvm::StringRef Str(ThisTokBegin, n);
  if (LLVM_UNLIKELY(Str.contains('\''))) {
    Buffer.reserve(n);
    const char *Ptr = Str.begin();
    const char *End = Str.end();
    while (Ptr != End) {
      const char *Next = scanToSeparator(Ptr, End);
      Buffer.append(Ptr, Next);
      Ptr = Next;
      if (Ptr != End)
        ++Ptr;
    }
    Str = Buffer;
  }

  auto StatusOrErr =
      Result.convertFromString(Str, APFloat::rmNearestTiesToEven);
  assert(StatusOrErr && "Invalid floating point representation");
  return !errorToBool(StatusOrErr.takeError()) ? *StatusOrErr
                                               : APFloat::opInvalidOp;
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isExponentChar(char c) { return (c | 0x20) == 'e' || (c | 0x20) == 'p'; }
} // namespace

bool NumericLiteralParser::getFixedPointValue(llvm::APInt &StoreVal,
                                              unsigned Scale) {
  assert(radix == 16 || radix == 10);

  // Find how many digits are needed to store the whole literal.
  unsigned NumDigits = SuffixBegin - DigitsBegin;
  if (saw_period)
    --NumDigits;

  // Initial scan of the exponent if it exists
  bool ExpOverflowOccurred = false;
  bool NegativeExponent = false;
  const char *ExponentBegin;
  uint64_t Exponent = 0;
  int64_t BaseShift = 0;
  if (saw_exponent) {
    const char *Ptr = DigitsBegin;

    while (!isExponentChar(*Ptr))
      ++Ptr;
    ExponentBegin = Ptr;
    ++Ptr;
    NegativeExponent = *Ptr == '-';
    if (NegativeExponent)
      ++Ptr;

    unsigned NumExpDigits = SuffixBegin - Ptr;
    if (canFitIn64Bits(radix, NumExpDigits)) {
      llvm::StringRef ExpStr(Ptr, NumExpDigits);
      llvm::APInt ExpInt(/*numBits=*/64, ExpStr, /*radix=*/10);
      Exponent = ExpInt.getZExtValue();
    } else {
      ExpOverflowOccurred = true;
    }

    if (NegativeExponent)
      BaseShift -= Exponent;
    else
      BaseShift += Exponent;
  }

  uint64_t NumBitsNeeded;
  if (radix == 10)
    NumBitsNeeded = 4 * (NumDigits + Exponent) + Scale;
  else
    NumBitsNeeded = 4 * NumDigits + Exponent + Scale;

  if (NumBitsNeeded > std::numeric_limits<unsigned>::max())
    ExpOverflowOccurred = true;
  llvm::APInt Val(static_cast<unsigned>(NumBitsNeeded), 0, /*isSigned=*/false);

  bool FoundDecimal = false;

  int64_t FractBaseShift = 0;
  const char *End = saw_exponent ? ExponentBegin : SuffixBegin;
  for (const char *Ptr = DigitsBegin; Ptr < End; ++Ptr) {
    if (*Ptr == '.') {
      FoundDecimal = true;
      continue;
    }

    // Normal reading of an integer
    unsigned C = llvm::hexDigitValue(*Ptr);
    assert(C < radix && "NumericLiteralParser ctor should have rejected this");

    Val *= radix;
    Val += C;

    if (FoundDecimal)
      // Keep track of how much we will need to adjust this value by from the
      // number of digits past the radix point.
      --FractBaseShift;
  }

  // For a radix of 16, we will be multiplying by 2 instead of 16.
  if (radix == 16)
    FractBaseShift *= 4;
  BaseShift += FractBaseShift;

  Val <<= Scale;

  uint64_t Base = (radix == 16) ? 2 : 10;
  if (BaseShift > 0) {
    for (int64_t i = 0; i < BaseShift; ++i) {
      Val *= Base;
    }
  } else if (BaseShift < 0) {
    for (int64_t i = BaseShift; i < 0 && !Val.isZero(); ++i)
      Val = Val.udiv(Base);
  }

  bool IntOverflowOccurred = false;
  auto MaxVal = llvm::APInt::getMaxValue(StoreVal.getBitWidth());
  if (Val.getBitWidth() > StoreVal.getBitWidth()) {
    IntOverflowOccurred |= Val.ugt(MaxVal.zext(Val.getBitWidth()));
    StoreVal = Val.trunc(StoreVal.getBitWidth());
  } else if (Val.getBitWidth() < StoreVal.getBitWidth()) {
    IntOverflowOccurred |= Val.zext(MaxVal.getBitWidth()).ugt(MaxVal);
    StoreVal = Val.zext(StoreVal.getBitWidth());
  } else {
    StoreVal = Val;
  }

  return IntOverflowOccurred || ExpOverflowOccurred;
}
