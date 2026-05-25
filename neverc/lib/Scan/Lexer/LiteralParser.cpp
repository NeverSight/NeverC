#include "neverc/Scan/LiteralParser.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LexDiag.h"
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

// ===----------------------------------------------------------------------===
// SIMD scan helpers & escape parsing
// ===----------------------------------------------------------------------===

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
const char *scanToBackslash(const char *Ptr, const char *End) {
#ifdef __AVX512BW__
  {
    const __m512i VBS = _mm512_set1_epi8('\\');
    while (Ptr + 64 <= End) {
      __m512i V = _mm512_loadu_si512(Ptr);
      __mmask64 Hit = _mm512_cmpeq_epi8_mask(V, VBS);
      if (Hit)
        return Ptr + _tzcnt_u64(Hit);
      Ptr += 64;
    }
  }
#endif
#if defined(__AVX2__) || defined(__AVX512BW__)
  {
    const __m256i VBS = _mm256_set1_epi8('\\');
    while (Ptr + 32 <= End) {
      __m256i V = _mm256_loadu_si256((const __m256i *)Ptr);
      unsigned Hit = static_cast<unsigned>(
          _mm256_movemask_epi8(_mm256_cmpeq_epi8(V, VBS)));
      if (Hit)
        return Ptr + llvm::countr_zero(Hit);
      Ptr += 32;
    }
  }
#elif defined(__SSE2__)
  {
    const __m128i VBS = _mm_set1_epi8('\\');
    while (Ptr + 16 <= End) {
      __m128i V = _mm_loadu_si128((const __m128i *)Ptr);
      unsigned Hit =
          static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(V, VBS)));
      if (Hit)
        return Ptr + llvm::countr_zero(Hit);
      Ptr += 16;
    }
  }
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t VBS = vdupq_n_u8('\\');
    while (Ptr + 32 <= End) {
      uint8x16_t V0 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr));
      uint8x16_t V1 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr + 16));
      uint8x16_t H0 = vceqq_u8(V0, VBS);
      uint8x16_t H1 = vceqq_u8(V1, VBS);
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
      uint8x16_t Hit = vceqq_u8(V, VBS);
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
      uint64_t S0 = hasZeroByte(W0 ^ (Broadcast * '\\'));
      if (S0)
        return Ptr + (__builtin_ctzll(S0 & HiBitMask) >> 3);
      uint64_t S1 = hasZeroByte(W1 ^ (Broadcast * '\\'));
      if (S1)
        return Ptr + 8 + (__builtin_ctzll(S1 & HiBitMask) >> 3);
      Ptr += 16;
    }
    if (Ptr + 8 <= End) {
      uint64_t Word;
      __builtin_memcpy(&Word, Ptr, 8);
      uint64_t Stops = hasZeroByte(Word ^ (Broadcast * '\\'));
      if (Stops)
        return Ptr + (__builtin_ctzll(Stops & HiBitMask) >> 3);
      Ptr += 8;
    }
  }
#endif
  while (Ptr != End && *Ptr != '\\')
    ++Ptr;
  return Ptr;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
unsigned getCharacterBitWidth(tok::TokenKind kind, const TargetInfo &Target) {
  switch (kind) {
  default:
    llvm_unreachable("Unknown token type!");
  case tok::char_constant:
  case tok::string_literal:
  case tok::utf8_char_constant:
  case tok::utf8_string_literal:
    return Target.getCharWidth();
  case tok::wide_char_constant:
  case tok::wide_string_literal:
    return Target.getWCharWidth();
  case tok::utf16_char_constant:
  case tok::utf16_string_literal:
    return Target.getChar16Width();
  case tok::utf32_char_constant:
  case tok::utf32_string_literal:
    return Target.getChar32Width();
  }
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
unsigned getEncodingPrefixLength(tok::TokenKind kind) {
  constexpr unsigned PrefixLenTable[] = {
      [tok::char_constant - tok::char_constant] = 0,
      [tok::wide_char_constant - tok::char_constant] = 1,
      [tok::utf8_char_constant - tok::char_constant] = 2,
      [tok::utf16_char_constant - tok::char_constant] = 1,
      [tok::utf32_char_constant - tok::char_constant] = 1,
      [tok::string_literal - tok::char_constant] = 0,
      [tok::wide_string_literal - tok::char_constant] = 1,
      [tok::utf8_string_literal - tok::char_constant] = 2,
      [tok::utf16_string_literal - tok::char_constant] = 1,
      [tok::utf32_string_literal - tok::char_constant] = 1,
  };
  unsigned Idx =
      static_cast<unsigned>(kind) - static_cast<unsigned>(tok::char_constant);
  assert(Idx < std::size(PrefixLenTable) && "Unknown token type!");
  return PrefixLenTable[Idx];
}
} // namespace

namespace {
CharSourceRange makeCharSourceRange(const LangOptions &Features,
                                    FullSourceLoc TokLoc, const char *TokBegin,
                                    const char *TokRangeBegin,
                                    const char *TokRangeEnd) {
  SourceLocation Begin = SourceScanner::AdvanceToTokenCharacter(
      TokLoc, TokRangeBegin - TokBegin, TokLoc.getManager(), Features);
  SourceLocation End = SourceScanner::AdvanceToTokenCharacter(
      Begin, TokRangeEnd - TokRangeBegin, TokLoc.getManager(), Features);
  return CharSourceRange::getCharRange(Begin, End);
}

DiagnosticBuilder Diag(DiagnosticsEngine *Diags, const LangOptions &Features,
                       FullSourceLoc TokLoc, const char *TokBegin,
                       const char *TokRangeBegin, const char *TokRangeEnd,
                       unsigned DiagID) {
  SourceLocation Begin = SourceScanner::AdvanceToTokenCharacter(
      TokLoc, TokRangeBegin - TokBegin, TokLoc.getManager(), Features);
  return Diags->Report(Begin, DiagID) << makeCharSourceRange(
             Features, TokLoc, TokBegin, TokRangeBegin, TokRangeEnd);
}
} // namespace

namespace {
struct SimpleEscapeLookup {
  bool Table[256] = {};
  constexpr SimpleEscapeLookup() {
    Table[(unsigned char)'\''] = true;
    Table[(unsigned char)'"'] = true;
    Table[(unsigned char)'?'] = true;
    Table[(unsigned char)'\\'] = true;
    Table[(unsigned char)'a'] = true;
    Table[(unsigned char)'b'] = true;
    Table[(unsigned char)'f'] = true;
    Table[(unsigned char)'n'] = true;
    Table[(unsigned char)'r'] = true;
    Table[(unsigned char)'t'] = true;
    Table[(unsigned char)'v'] = true;
  }
};
constexpr SimpleEscapeLookup SimpleEscapeTable;
} // namespace

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isSimpleEscapeChar(char Escape) {
  return SimpleEscapeTable.Table[(unsigned char)Escape];
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
unsigned parseCharEscape(const char *ThisTokBegin, const char *&ThisTokBuf,
                         const char *ThisTokEnd, bool &HadError,
                         FullSourceLoc Loc, unsigned CharWidth,
                         DiagnosticsEngine *Diags, const LangOptions &Features,
                         StringLiteralEvalMethod EvalMethod) {
  const char *EscapeBegin = ThisTokBuf;
  bool Delimited = false;
  bool EndDelimiterFound = false;

  static constexpr auto makeEscapeTable = []() constexpr {
    std::array<unsigned, 256> T = {};
    for (auto &v : T)
      v = 256;
    T['\\'] = '\\';
    T['\''] = '\'';
    T['"'] = '"';
    T['?'] = '?';
    T['a'] = 7;
    T['b'] = 8;
    T['e'] = 27;
    T['E'] = 27;
    T['f'] = 12;
    T['n'] = 10;
    T['r'] = 13;
    T['t'] = 9;
    T['v'] = 11;
    return T;
  };
  static constexpr auto EscapeValueTable = makeEscapeTable();

  ++ThisTokBuf;
  unsigned ResultChar = *ThisTokBuf++;
  char Escape = ResultChar;

  unsigned TableVal = EscapeValueTable[static_cast<unsigned char>(ResultChar)];
  if (LLVM_LIKELY(TableVal != 256)) {
    if (LLVM_UNLIKELY((ResultChar == 'e' || ResultChar == 'E') && Diags))
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_nonstandard_escape)
          << std::string(1, static_cast<char>(ResultChar));
    ResultChar = TableVal;
  } else
    switch (ResultChar) {
    case 'x': { // Hex escape.
      ResultChar = 0;
      if (ThisTokBuf != ThisTokEnd && *ThisTokBuf == '{') {
        Delimited = true;
        ThisTokBuf++;
        if (*ThisTokBuf == '}') {
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_delimited_escape_empty);
          return ResultChar;
        }
      } else if (ThisTokBuf == ThisTokEnd || !isHexDigit(*ThisTokBuf)) {
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_hex_escape_no_digits)
              << "x";
        return ResultChar;
      }

      // Hex escapes are a maximal series of hex digits.
      bool Overflow = false;
      for (; ThisTokBuf != ThisTokEnd; ++ThisTokBuf) {
        if (Delimited && *ThisTokBuf == '}') {
          ThisTokBuf++;
          EndDelimiterFound = true;
          break;
        }
        int CharVal = llvm::hexDigitValue(*ThisTokBuf);
        if (CharVal == -1) {
          // Non delimited hex escape sequences stop at the first non-hex digit.
          if (!Delimited)
            break;
          HadError = true;
          if (Diags)
            Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
                 diag::err_delimited_escape_invalid)
                << llvm::StringRef(ThisTokBuf, 1);
          continue;
        }
        // About to shift out a digit?
        if (ResultChar & 0xF0000000)
          Overflow = true;
        ResultChar <<= 4;
        ResultChar |= CharVal;
      }
      // See if any bits will be truncated when evaluated as a character.
      if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
        Overflow = true;
        ResultChar &= ~0U >> (32 - CharWidth);
      }

      // Check for overflow.
      if (!HadError && Overflow) { // Too many digits to fit in
        HadError = true;
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_escape_too_large)
              << 0;
      }
      break;
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
      // Octal escapes.
      --ThisTokBuf;
      ResultChar = 0;

      // Octal escapes are a series of octal digits with maximum length 3.
      // "\0123" is a two digit sequence equal to "\012" "3".
      unsigned NumDigits = 0;
      do {
        ResultChar <<= 3;
        ResultChar |= *ThisTokBuf++ - '0';
        ++NumDigits;
      } while (ThisTokBuf != ThisTokEnd && NumDigits < 3 &&
               ThisTokBuf[0] >= '0' && ThisTokBuf[0] <= '7');

      // Check for overflow.  Reject '\777', but not L'\777'.
      if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_escape_too_large)
              << 1;
        ResultChar &= ~0U >> (32 - CharWidth);
      }
      break;
    }
    case 'o': {
      bool Overflow = false;
      if (ThisTokBuf == ThisTokEnd || *ThisTokBuf != '{') {
        HadError = true;
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_delimited_escape_missing_brace)
              << "o";

        break;
      }
      ResultChar = 0;
      Delimited = true;
      ++ThisTokBuf;
      if (*ThisTokBuf == '}') {
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_delimited_escape_empty);
        return ResultChar;
      }

      while (ThisTokBuf != ThisTokEnd) {
        if (*ThisTokBuf == '}') {
          EndDelimiterFound = true;
          ThisTokBuf++;
          break;
        }
        if (*ThisTokBuf < '0' || *ThisTokBuf > '7') {
          HadError = true;
          if (Diags)
            Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
                 diag::err_delimited_escape_invalid)
                << llvm::StringRef(ThisTokBuf, 1);
          ThisTokBuf++;
          continue;
        }
        // Check if one of the top three bits is set before shifting them out.
        if (ResultChar & 0xE0000000)
          Overflow = true;

        ResultChar <<= 3;
        ResultChar |= *ThisTokBuf++ - '0';
      }
      // Check for overflow.  Reject '\777', but not L'\777'.
      if (!HadError &&
          (Overflow || (CharWidth != 32 && (ResultChar >> CharWidth) != 0))) {
        HadError = true;
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_escape_too_large)
              << 1;
        ResultChar &= ~0U >> (32 - CharWidth);
      }
      break;
    }
      // Otherwise, these are not valid escapes.
    case '(':
    case '{':
    case '[':
    case '%':
      // GCC accepts these as extensions.  We warn about them as such though.
      if (Diags)
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::ext_nonstandard_escape)
            << std::string(1, ResultChar);
      break;
    default:
      if (!Diags)
        break;

      if (isPrintable(ResultChar))
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::ext_unknown_escape)
            << std::string(1, ResultChar);
      else
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::ext_unknown_escape)
            << "x" + llvm::utohexstr(ResultChar);
      break;
    }

  if (Delimited && Diags) {
    if (!EndDelimiterFound)
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::err_expected)
          << tok::r_brace;
    else if (!HadError) {
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_delimited_escape_sequence)
          << /*delimited*/ 0;
    }
  }

  if (EvalMethod == StringLiteralEvalMethod::Unevaluated &&
      !isSimpleEscapeChar(Escape)) {
    Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
         diag::err_unevaluated_string_invalid_escape_sequence)
        << llvm::StringRef(EscapeBegin, ThisTokBuf - EscapeBegin);
    HadError = true;
  }

  return ResultChar;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
void appendCodePoint(unsigned Codepoint, llvm::SmallVectorImpl<char> &Str) {
  char ResultBuf[4];
  char *ResultPtr = ResultBuf;
  if (llvm::ConvertCodePointToUTF8(Codepoint, ResultPtr))
    Str.append(ResultBuf, ResultPtr);
}
} // namespace

void neverc::expandUCNs(llvm::SmallVectorImpl<char> &Buf,
                        llvm::StringRef Input) {
  const char *I = Input.begin();
  const char *E = Input.end();

  while (I != E) {
    const char *BS = (const char *)memchr(I, '\\', E - I);
    if (!BS) {
      Buf.append(I, E);
      break;
    }
    if (BS > I)
      Buf.append(I, BS);
    I = BS;

    ++I;
    char Kind = *I;
    ++I;

    assert(Kind == 'u' || Kind == 'U' || Kind == 'N');
    uint32_t CodePoint = 0;

    if (Kind == 'u' && *I == '{') {
      for (++I; *I != '}'; ++I) {
        unsigned Value = llvm::hexDigitValue(*I);
        assert(Value != -1U);
        CodePoint <<= 4;
        CodePoint += Value;
      }
      appendCodePoint(CodePoint, Buf);
      ++I;
      continue;
    }

    if (Kind == 'N') {
      assert(*I == '{');
      ++I;
      const char *Delim = (const char *)memchr(I, '}', E - I);
      assert(Delim != nullptr);
      llvm::StringRef Name(I, Delim - I);
      CodePoint = llvm::sys::unicode::nameToCodepointStrict(Name);
      if (CodePoint == UINT32_MAX) {
        auto Res = llvm::sys::unicode::nameToCodepointLooseMatching(Name);
        CodePoint = Res.CodePoint;
      }
      assert(CodePoint != UINT32_MAX &&
             "could not find a codepoint that was previously found");
      appendCodePoint(CodePoint, Buf);
      I = Delim + 1;
      continue;
    }

    unsigned NumHexDigits;
    if (Kind == 'u')
      NumHexDigits = 4;
    else
      NumHexDigits = 8;

    assert(I + NumHexDigits <= E);

    for (unsigned N = 0; N != NumHexDigits; ++I, ++N) {
      unsigned Value = llvm::hexDigitValue(*I);
      assert(Value != -1U);

      CodePoint <<= 4;
      CodePoint += Value;
    }

    appendCodePoint(CodePoint, Buf);
  }
}

bool neverc::isFunctionLocalStringLiteralMacro(tok::TokenKind K,
                                               const LangOptions &LO) {
  return LO.MicrosoftExt &&
         (K == tok::kw___FUNCTION__ || K == tok::kw_L__FUNCTION__ ||
          K == tok::kw___FUNCSIG__ || K == tok::kw_L__FUNCSIG__ ||
          K == tok::kw___FUNCDNAME__);
}

bool neverc::tokenIsLikeStringLiteral(const Token &Tok, const LangOptions &LO) {
  return tok::isStringLiteral(Tok.getKind()) ||
         isFunctionLocalStringLiteralMacro(Tok.getKind(), LO);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool parseNumericUCN(const char *ThisTokBegin, const char *&ThisTokBuf,
                     const char *ThisTokEnd, uint32_t &UcnVal,
                     unsigned short &UcnLen, bool &Delimited, FullSourceLoc Loc,
                     DiagnosticsEngine *Diags, const LangOptions &Features,
                     bool in_char_string_literal = false) {
  const char *UcnBegin = ThisTokBuf;
  bool HasError = false;
  bool EndDelimiterFound = false;

  // Skip the '\u' char's.
  ThisTokBuf += 2;
  Delimited = false;
  if (UcnBegin[1] == 'u' && in_char_string_literal &&
      ThisTokBuf != ThisTokEnd && *ThisTokBuf == '{') {
    Delimited = true;
    ThisTokBuf++;
  } else if (ThisTokBuf == ThisTokEnd || !isHexDigit(*ThisTokBuf)) {
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_hex_escape_no_digits)
          << llvm::StringRef(&ThisTokBuf[-1], 1);
    return false;
  }
  UcnLen = (ThisTokBuf[-1] == 'u' ? 4 : 8);

  bool Overflow = false;
  unsigned short Count = 0;
  for (; ThisTokBuf != ThisTokEnd && (Delimited || Count != UcnLen);
       ++ThisTokBuf) {
    if (Delimited && *ThisTokBuf == '}') {
      ++ThisTokBuf;
      EndDelimiterFound = true;
      break;
    }
    int CharVal = llvm::hexDigitValue(*ThisTokBuf);
    if (CharVal == -1) {
      HasError = true;
      if (!Delimited)
        break;
      if (Diags) {
        Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
             diag::err_delimited_escape_invalid)
            << llvm::StringRef(ThisTokBuf, 1);
      }
      Count++;
      continue;
    }
    if (UcnVal & 0xF0000000) {
      Overflow = true;
      continue;
    }
    UcnVal <<= 4;
    UcnVal |= CharVal;
    Count++;
  }

  if (Overflow) {
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_escape_too_large)
          << 0;
    return false;
  }

  if (Delimited && !EndDelimiterFound) {
    if (Diags) {
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_expected)
          << tok::r_brace;
    }
    return false;
  }

  // If we didn't consume the proper number of digits, there is a problem.
  if (Count == 0 || (!Delimited && Count != UcnLen)) {
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           Delimited ? diag::err_delimited_escape_empty
                     : diag::err_ucn_escape_incomplete);
    return false;
  }
  return !HasError;
}

void reportInvalidCharName(DiagnosticsEngine *Diags,
                           const LangOptions &Features, FullSourceLoc Loc,
                           const char *TokBegin, const char *TokRangeBegin,
                           const char *TokRangeEnd, llvm::StringRef Name) {

  Diag(Diags, Features, Loc, TokBegin, TokRangeBegin, TokRangeEnd,
       diag::err_invalid_ucn_name)
      << Name;

  namespace u = llvm::sys::unicode;

  auto Res = u::nameToCodepointLooseMatching(Name);
  if (Res.CodePoint != UINT32_MAX) {
    Diag(Diags, Features, Loc, TokBegin, TokRangeBegin, TokRangeEnd,
         diag::note_invalid_ucn_name_loose_matching)
        << FixItHint::CreateReplacement(
               makeCharSourceRange(Features, Loc, TokBegin, TokRangeBegin,
                                   TokRangeEnd),
               Res.Name);
    return;
  }

  unsigned Distance = 0;
  llvm::SmallVector<u::MatchForCodepointName> Matches =
      u::nearestMatchesForCodepointName(Name, 5);
  assert(!Matches.empty() && "No unicode characters found");

  for (const auto &Match : Matches) {
    if (Distance == 0)
      Distance = Match.Distance;
    if (std::max(Distance, Match.Distance) -
            std::min(Distance, Match.Distance) >
        3)
      break;
    Distance = Match.Distance;

    llvm::SmallString<8> Str;
    llvm::UTF32 V = Match.Value;
    bool Converted =
        llvm::convertUTF32ToUTF8String(llvm::ArrayRef<llvm::UTF32>(&V, 1), Str);
    (void)Converted;
    assert(Converted && "Found a match wich is not a unicode character");

    Diag(Diags, Features, Loc, TokBegin, TokRangeBegin, TokRangeEnd,
         diag::note_invalid_ucn_name_candidate)
        << Match.Name << llvm::utohexstr(Match.Value) << Str
        << FixItHint::CreateReplacement(
               makeCharSourceRange(Features, Loc, TokBegin, TokRangeBegin,
                                   TokRangeEnd),
               Match.Name);
  }
}

bool parseNamedUCN(const char *ThisTokBegin, const char *&ThisTokBuf,
                   const char *ThisTokEnd, uint32_t &UcnVal,
                   unsigned short &UcnLen, FullSourceLoc Loc,
                   DiagnosticsEngine *Diags, const LangOptions &Features) {
  const char *UcnBegin = ThisTokBuf;
  assert(UcnBegin[0] == '\\' && UcnBegin[1] == 'N');
  ThisTokBuf += 2;
  if (ThisTokBuf == ThisTokEnd || *ThisTokBuf != '{') {
    if (Diags) {
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_delimited_escape_missing_brace)
          << llvm::StringRef(&ThisTokBuf[-1], 1);
    }
    return false;
  }
  ThisTokBuf++;
  const char *ClosingBrace = std::find_if(ThisTokBuf, ThisTokEnd, [](char C) {
    return C == '}' || isVerticalWhitespace(C);
  });
  bool Incomplete = ClosingBrace == ThisTokEnd;
  bool Empty = ClosingBrace == ThisTokBuf;
  if (Incomplete || Empty) {
    if (Diags) {
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           Incomplete ? diag::err_ucn_escape_incomplete
                      : diag::err_delimited_escape_empty)
          << llvm::StringRef(&UcnBegin[1], 1);
    }
    ThisTokBuf = ClosingBrace == ThisTokEnd ? ClosingBrace : ClosingBrace + 1;
    return false;
  }
  llvm::StringRef Name(ThisTokBuf, ClosingBrace - ThisTokBuf);
  ThisTokBuf = ClosingBrace + 1;
  char32_t Res = llvm::sys::unicode::nameToCodepointStrict(Name);
  if (Res == UINT32_MAX) {
    if (Diags)
      reportInvalidCharName(Diags, Features, Loc, ThisTokBegin, &UcnBegin[3],
                            ClosingBrace, Name);
    return false;
  }
  UcnVal = Res;
  UcnLen = UcnVal > 0xFFFF ? 8 : 4;
  return true;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool parseUCNEscape(const char *ThisTokBegin, const char *&ThisTokBuf,
                    const char *ThisTokEnd, uint32_t &UcnVal,
                    unsigned short &UcnLen, FullSourceLoc Loc,
                    DiagnosticsEngine *Diags, const LangOptions &Features,
                    bool in_char_string_literal = false) {

  bool HasError;
  const char *UcnBegin = ThisTokBuf;
  bool IsDelimitedEscapeSequence = false;
  bool IsNamedEscapeSequence = false;
  if (ThisTokBuf[1] == 'N') {
    IsNamedEscapeSequence = true;
    HasError = !parseNamedUCN(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal,
                              UcnLen, Loc, Diags, Features);
  } else {
    HasError = !parseNumericUCN(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal,
                                UcnLen, IsDelimitedEscapeSequence, Loc, Diags,
                                Features, in_char_string_literal);
  }
  if (HasError)
    return false;

  // Reject surrogate pairs and out-of-range code points.
  if ((0xD800 <= UcnVal && UcnVal <= 0xDFFF) || // surrogate codepoints
      UcnVal > 0x10FFFF) {                      // maximum legal UTF32 value
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_ucn_escape_invalid);
    return false;
  }

  // C23 allows UCNs that refer to control characters and basic source
  // characters inside character and string literals (with compatibility
  // warnings).
  if (UcnVal < 0xa0 &&
      // $, @, ` are allowed in all language modes
      (UcnVal != 0x24 && UcnVal != 0x40 && UcnVal != 0x60)) {
    bool IsError = (!Features.C23 || !in_char_string_literal);
    if (Diags) {
      char BasicSCSChar = UcnVal;
      if (UcnVal >= 0x20 && UcnVal < 0x7f)
        Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
             IsError ? diag::err_ucn_escape_basic_scs
                     : diag::warn_c23_compat_literal_ucn_escape_basic_scs)
            << llvm::StringRef(&BasicSCSChar, 1);
      else
        Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
             IsError ? diag::err_ucn_control_character
                     : diag::warn_c23_compat_literal_ucn_control_character);
    }
    if (IsError)
      return false;
  }

  if (!Features.C99 && Diags)
    Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
         diag::warn_ucn_not_valid_in_c89_literal);

  if ((IsDelimitedEscapeSequence || IsNamedEscapeSequence) && Diags)
    Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
         diag::ext_delimited_escape_sequence)
        << (IsNamedEscapeSequence ? 1 : 0);

  return true;
}

int calcUCNByteLength(const char *ThisTokBegin, const char *&ThisTokBuf,
                      const char *ThisTokEnd, unsigned CharByteWidth,
                      const LangOptions &Features, bool &HadError) {
  // UTF-32: 4 bytes per escape.
  if (CharByteWidth == 4)
    return 4;

  uint32_t UcnVal = 0;
  unsigned short UcnLen = 0;
  FullSourceLoc Loc;

  if (!parseUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal, UcnLen, Loc,
                      nullptr, Features, true)) {
    HadError = true;
    return 0;
  }

  // UTF-16: 2 bytes for BMP, 4 bytes otherwise.
  if (CharByteWidth == 2)
    return UcnVal <= 0xFFFF ? 2 : 4;

  // UTF-8.
  if (UcnVal < 0x80)
    return 1;
  if (UcnVal < 0x800)
    return 2;
  if (UcnVal < 0x10000)
    return 3;
  return 4;
}

void writeUCNEscape(const char *ThisTokBegin, const char *&ThisTokBuf,
                    const char *ThisTokEnd, char *&ResultBuf, bool &HadError,
                    FullSourceLoc Loc, unsigned CharByteWidth,
                    DiagnosticsEngine *Diags, const LangOptions &Features) {
  typedef uint32_t UTF32;
  UTF32 UcnVal = 0;
  unsigned short UcnLen = 0;
  if (!parseUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal, UcnLen, Loc,
                      Diags, Features, true)) {
    HadError = true;
    return;
  }

  assert((CharByteWidth == 1 || CharByteWidth == 2 || CharByteWidth == 4) &&
         "only character widths of 1, 2, or 4 bytes supported");

  (void)UcnLen;
  assert((UcnLen == 4 || UcnLen == 8) && "only ucn length of 4 or 8 supported");

  if (CharByteWidth == 4) {
    llvm::UTF32 *ResultPtr = reinterpret_cast<llvm::UTF32 *>(ResultBuf);
    *ResultPtr = UcnVal;
    ResultBuf += 4;
    return;
  }

  if (CharByteWidth == 2) {
    llvm::UTF16 *ResultPtr = reinterpret_cast<llvm::UTF16 *>(ResultBuf);

    if (UcnVal <= (UTF32)0xFFFF) {
      *ResultPtr = UcnVal;
      ResultBuf += 2;
      return;
    }

    // Convert to UTF16.
    UcnVal -= 0x10000;
    *ResultPtr = 0xD800 + (UcnVal >> 10);
    *(ResultPtr + 1) = 0xDC00 + (UcnVal & 0x3FF);
    ResultBuf += 4;
    return;
  }

  assert(CharByteWidth == 1 && "UTF-8 encoding is only for 1 byte characters");

  typedef uint8_t UTF8;

  unsigned short bytesToWrite = 0;
  if (UcnVal < (UTF32)0x80)
    bytesToWrite = 1;
  else if (UcnVal < (UTF32)0x800)
    bytesToWrite = 2;
  else if (UcnVal < (UTF32)0x10000)
    bytesToWrite = 3;
  else
    bytesToWrite = 4;

  const unsigned byteMask = 0xBF;
  const unsigned byteMark = 0x80;

  // Once the bits are split out into bytes of UTF8, this is a mask OR-ed
  // into the first byte, depending on how many bytes follow.
  static const UTF8 firstByteMark[5] = {0x00, 0x00, 0xC0, 0xE0, 0xF0};
  // Finally, we write the bytes into ResultBuf.
  ResultBuf += bytesToWrite;
  switch (bytesToWrite) { // note: everything falls through.
  case 4:
    *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask);
    UcnVal >>= 6;
    [[fallthrough]];
  case 3:
    *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask);
    UcnVal >>= 6;
    [[fallthrough]];
  case 2:
    *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask);
    UcnVal >>= 6;
    [[fallthrough]];
  case 1:
    *--ResultBuf = (UTF8)(UcnVal | firstByteMark[bytesToWrite]);
  }
  // Update the buffer.
  ResultBuf += bytesToWrite;
}
} // namespace

// Parses character constant tokens.
// ===----------------------------------------------------------------------===
// CharLiteralParser
// ===----------------------------------------------------------------------===

CharLiteralParser::CharLiteralParser(const char *begin, const char *end,
                                     SourceLocation Loc, PrepEngine &PP,
                                     tok::TokenKind kind) {
  // At this point we know that the character matches the regex "(L|u|U)?'.*'".
  HadError = false;

  Kind = kind;

  const char *TokBegin = begin;

  // Skip over wide character determinant.
  if (Kind != tok::char_constant)
    ++begin;
  if (Kind == tok::utf8_char_constant)
    ++begin;

  // Skip over the entry quote.
  if (begin[0] != '\'') {
    PP.Diag(Loc, diag::err_lexing_char);
    HadError = true;
    return;
  }

  ++begin;

  // Trim the ending quote.
  assert(end != begin && "Invalid token lexed");
  --end;

  assert(PP.getTargetInfo().getCharWidth() == 8 && "Assumes char is 8 bits");
  assert(PP.getTargetInfo().getIntWidth() <= 64 &&
         (PP.getTargetInfo().getIntWidth() & 7) == 0 &&
         "Assumes sizeof(int) on target is <= 64 and a multiple of char");
  assert(PP.getTargetInfo().getWCharWidth() <= 64 &&
         "Assumes sizeof(wchar) on target is <= 64");

  llvm::SmallVector<uint32_t, 4> codepoint_buffer;
  codepoint_buffer.resize(end - begin);
  uint32_t *buffer_begin = &codepoint_buffer.front();
  uint32_t *buffer_end = buffer_begin + codepoint_buffer.size();

  // Unicode escapes representing characters that cannot be correctly
  // represented in a single code unit are disallowed in character literals
  // by this implementation.
  uint32_t largest_character_for_kind;
  if (tok::wide_char_constant == Kind) {
    largest_character_for_kind =
        0xFFFFFFFFu >> (32 - PP.getTargetInfo().getWCharWidth());
  } else if (tok::utf8_char_constant == Kind) {
    largest_character_for_kind = 0x7F;
  } else if (tok::utf16_char_constant == Kind) {
    largest_character_for_kind = 0xFFFF;
  } else if (tok::utf32_char_constant == Kind) {
    largest_character_for_kind = 0x10FFFF;
  } else {
    largest_character_for_kind = 0x7Fu;
  }

  while (begin != end) {
    if (begin[0] != '\\') {
      char const *start = begin;
      begin = scanToBackslash(begin, end);

      char const *tmp_in_start = start;
      uint32_t *tmp_out_start = buffer_begin;
      llvm::ConversionResult res = llvm::ConvertUTF8toUTF32(
          reinterpret_cast<llvm::UTF8 const **>(&start),
          reinterpret_cast<llvm::UTF8 const *>(begin), &buffer_begin,
          buffer_end, llvm::strictConversion);
      if (res != llvm::conversionOK) {
        // If we see bad encoding for unprefixed character literals, warn and
        // simply copy the byte values, for compatibility with gcc and
        // older versions.
        bool NoErrorOnBadEncoding = isOrdinary();
        unsigned Msg = diag::err_bad_character_encoding;
        if (NoErrorOnBadEncoding)
          Msg = diag::warn_bad_character_encoding;
        PP.Diag(Loc, Msg);
        if (NoErrorOnBadEncoding) {
          start = tmp_in_start;
          buffer_begin = tmp_out_start;
          for (; start != begin; ++start, ++buffer_begin)
            *buffer_begin = static_cast<uint8_t>(*start);
        } else {
          HadError = true;
        }
      } else {
        for (; tmp_out_start < buffer_begin; ++tmp_out_start) {
          if (*tmp_out_start > largest_character_for_kind) {
            HadError = true;
            PP.Diag(Loc, diag::err_character_too_large);
          }
        }
      }

      continue;
    }
    // Is this a Universal Character Name escape?
    if (begin[1] == 'u' || begin[1] == 'U' || begin[1] == 'N') {
      unsigned short UcnLen = 0;
      if (!parseUCNEscape(TokBegin, begin, end, *buffer_begin, UcnLen,
                          FullSourceLoc(Loc, PP.getSourceManager()),
                          &PP.getDiagnostics(), PP.getLangOpts(), true)) {
        HadError = true;
      } else if (*buffer_begin > largest_character_for_kind) {
        HadError = true;
        PP.Diag(Loc, diag::err_character_too_large);
      }

      ++buffer_begin;
      continue;
    }
    unsigned CharWidth = getCharacterBitWidth(Kind, PP.getTargetInfo());
    uint64_t result =
        parseCharEscape(TokBegin, begin, end, HadError,
                        FullSourceLoc(Loc, PP.getSourceManager()), CharWidth,
                        &PP.getDiagnostics(), PP.getLangOpts(),
                        StringLiteralEvalMethod::Evaluated);
    *buffer_begin++ = result;
  }

  unsigned NumCharsSoFar = buffer_begin - &codepoint_buffer.front();

  if (NumCharsSoFar > 1) {
    if (isOrdinary() && NumCharsSoFar == 4)
      PP.Diag(Loc, diag::warn_four_char_character_literal);
    else if (isOrdinary())
      PP.Diag(Loc, diag::warn_multichar_character_literal);
    else {
      PP.Diag(Loc, diag::err_multichar_character_literal) << (isWide() ? 0 : 1);
      HadError = true;
    }
    IsMultiChar = true;
  } else {
    IsMultiChar = false;
  }

  llvm::APInt LitVal(PP.getTargetInfo().getIntWidth(), 0);

  bool multi_char_too_long = false;
  if (isOrdinary() && isMultiChar()) {
    LitVal = 0;
    for (size_t i = 0; i < NumCharsSoFar; ++i) {
      // check for enough leading zeros to shift into
      multi_char_too_long |= (LitVal.countl_zero() < 8);
      LitVal <<= 8;
      LitVal = LitVal + (codepoint_buffer[i] & 0xFF);
    }
  } else if (NumCharsSoFar > 0) {
    // otherwise just take the last character
    LitVal = buffer_begin[-1];
  }

  if (!HadError && multi_char_too_long) {
    PP.Diag(Loc, diag::warn_char_constant_too_large);
  }

  Value = LitVal.getZExtValue();

  // Sign-extend single narrow chars when target char type is signed.
  if (isOrdinary() && NumCharsSoFar == 1 && (Value & 128) &&
      PP.getLangOpts().CharIsSigned)
    Value = (signed char)Value;
}

// Parses string literal tokens.
// ===----------------------------------------------------------------------===
// StringLiteralParser
// ===----------------------------------------------------------------------===

StringLiteralParser::StringLiteralParser(llvm::ArrayRef<Token> StringToks,
                                         PrepEngine &PP,
                                         StringLiteralEvalMethod EvalMethod)
    : SM(PP.getSourceManager()), Features(PP.getLangOpts()),
      Target(PP.getTargetInfo()), Diags(&PP.getDiagnostics()),
      MaxTokenLength(0), SizeBound(0), CharByteWidth(0), Kind(tok::unknown),
      ResultPtr(ResultBuf.data()), EvalMethod(EvalMethod), hadError(false) {
  init(StringToks);
}

void StringLiteralParser::init(llvm::ArrayRef<Token> StringToks) {
  if (StringToks.empty() || StringToks[0].getLength() < 2)
    return diagnoseLexingError(SourceLocation());
  assert(!StringToks.empty() && "expected at least one token");
  MaxTokenLength = StringToks[0].getLength();
  assert(StringToks[0].getLength() >= 2 && "literal token is invalid!");
  SizeBound = StringToks[0].getLength() - 2; // -2 for "".
  hadError = false;

  // Determines the kind of string from the prefix
  Kind = tok::string_literal;

  // Common case: single string fragment (no adjacent-literal concatenation).
  for (const Token &Tok : StringToks) {
    if (Tok.getLength() < 2)
      return diagnoseLexingError(Tok.getLocation());

    assert(Tok.getLength() >= 2 && "literal token is invalid!");
    SizeBound += Tok.getLength() - 2;

    if (Tok.getLength() > MaxTokenLength)
      MaxTokenLength = Tok.getLength();

    if (isUnevaluated() && Tok.getKind() != tok::string_literal) {
      if (Diags) {
        SourceLocation PrefixEndLoc = SourceScanner::AdvanceToTokenCharacter(
            Tok.getLocation(), getEncodingPrefixLength(Tok.getKind()), SM,
            Features);
        CharSourceRange Range =
            CharSourceRange::getCharRange({Tok.getLocation(), PrefixEndLoc});
        llvm::StringRef Prefix(SM.getCharacterData(Tok.getLocation()),
                               getEncodingPrefixLength(Tok.getKind()));
        Diags->Report(Tok.getLocation(), diag::warn_unevaluated_string_prefix)
            << Prefix << FixItHint::CreateRemoval(Range);
      }
    } else if (Tok.isNot(Kind) && Tok.isNot(tok::string_literal)) {
      if (isOrdinary()) {
        Kind = Tok.getKind();
      } else {
        if (Diags)
          Diags->Report(Tok.getLocation(), diag::err_unsupported_string_concat);
        hadError = true;
      }
    }
  }

  ++SizeBound;
  CharByteWidth = getCharacterBitWidth(Kind, Target);
  assert((CharByteWidth & 7) == 0 && "Assumes character size is byte multiple");
  CharByteWidth /= 8;

  // The output buffer size needs to be large enough to hold wide characters.
  // This is a worst-case assumption which basically corresponds to L"" "long".
  SizeBound *= CharByteWidth;

  // Size the temporary buffer to hold the result string data.
  ResultBuf.resize(SizeBound);

  // Likewise, but for each string piece.
  llvm::SmallString<512> TokenBuf;
  TokenBuf.resize(MaxTokenLength);

  ResultPtr = &ResultBuf[0]; // Next byte to fill in.

  for (unsigned i = 0, e = StringToks.size(); i != e; ++i) {
    const char *ThisTokBuf = &TokenBuf[0];
    // We know
    // that ThisTokBuf points to a buffer that is big enough for the whole token
    // and 'spelled' tokens can only shrink.
    bool StringInvalid = false;
    unsigned ThisTokLen = SourceScanner::getSpelling(
        StringToks[i], ThisTokBuf, SM, Features, &StringInvalid);
    if (StringInvalid)
      return diagnoseLexingError(StringToks[i].getLocation());

    const char *ThisTokBegin = ThisTokBuf;
    const char *ThisTokEnd = ThisTokBuf + ThisTokLen;

    // Strip the end quote.
    --ThisTokEnd;

    // Skip marker for wide or unicode strings.
    if (ThisTokBuf[0] == 'L' || ThisTokBuf[0] == 'u' || ThisTokBuf[0] == 'U') {
      ++ThisTokBuf;
      // Skip 8 of u8 marker for utf8 strings.
      if (ThisTokBuf[0] == '8')
        ++ThisTokBuf;
    }
    if (ThisTokBuf[0] == 'R') {
      if (ThisTokBuf[1] != '"')
        return diagnoseLexingError(StringToks[i].getLocation());
      ThisTokBuf += 2; // skip R"

      // Delimiter (d-char-sequence): at most 16 characters before `(`.
      constexpr unsigned MaxRawStrDelimLen = 16;

      const char *Prefix = ThisTokBuf;
      while (static_cast<unsigned>(ThisTokBuf - Prefix) < MaxRawStrDelimLen &&
             ThisTokBuf[0] != '(')
        ++ThisTokBuf;
      if (ThisTokBuf[0] != '(')
        return diagnoseLexingError(StringToks[i].getLocation());
      ++ThisTokBuf; // skip '('

      // Remove same number of characters from the end
      ThisTokEnd -= ThisTokBuf - Prefix;
      if (ThisTokEnd < ThisTokBuf)
        return diagnoseLexingError(StringToks[i].getLocation());

      // Newlines in the raw string body are preserved in the execution literal.
      llvm::StringRef RemainingTokenSpan(ThisTokBuf, ThisTokEnd - ThisTokBuf);
      while (!RemainingTokenSpan.empty()) {
        // Split the string literal on \r\n boundaries.
        size_t CRLFPos = RemainingTokenSpan.find("\r\n");
        llvm::StringRef BeforeCRLF = RemainingTokenSpan.substr(0, CRLFPos);
        llvm::StringRef AfterCRLF = RemainingTokenSpan.substr(CRLFPos);

        // Copy everything before the \r\n sequence into the string literal.
        if (copyStringFragment(StringToks[i], ThisTokBegin, BeforeCRLF))
          hadError = true;

        // Point into the \n inside the \r\n sequence and operate on the
        // remaining portion of the literal.
        RemainingTokenSpan = AfterCRLF.substr(1);
      }
    } else {
      if (ThisTokBuf[0] != '"')
        return diagnoseLexingError(StringToks[i].getLocation());
      ++ThisTokBuf; // skip "

      while (ThisTokBuf != ThisTokEnd) {
        if (ThisTokBuf[0] != '\\') {
          const char *InStart = ThisTokBuf;
          ThisTokBuf = scanToBackslash(ThisTokBuf, ThisTokEnd);

          if (copyStringFragment(
                  StringToks[i], ThisTokBegin,
                  llvm::StringRef(InStart, ThisTokBuf - InStart)))
            hadError = true;
          continue;
        }
        // Is this a Universal Character Name escape?
        if (ThisTokBuf[1] == 'u' || ThisTokBuf[1] == 'U' ||
            ThisTokBuf[1] == 'N') {
          writeUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, ResultPtr,
                         hadError,
                         FullSourceLoc(StringToks[i].getLocation(), SM),
                         CharByteWidth, Diags, Features);
          continue;
        }
        // Otherwise, this is a non-UCN escape character.  Process it.
        unsigned ResultChar =
            parseCharEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, hadError,
                            FullSourceLoc(StringToks[i].getLocation(), SM),
                            CharByteWidth * 8, Diags, Features, EvalMethod);

        if (CharByteWidth == 4) {
          llvm::UTF32 *ResultWidePtr =
              reinterpret_cast<llvm::UTF32 *>(ResultPtr);
          *ResultWidePtr = ResultChar;
          ResultPtr += 4;
        } else if (CharByteWidth == 2) {
          llvm::UTF16 *ResultWidePtr =
              reinterpret_cast<llvm::UTF16 *>(ResultPtr);
          *ResultWidePtr = ResultChar & 0xFFFF;
          ResultPtr += 2;
        } else {
          assert(CharByteWidth == 1 && "Unexpected char width");
          *ResultPtr++ = ResultChar & 0xFF;
        }
      }
    }
  }

  if (Diags) {
    unsigned MaxChars = Features.C99 ? 4095 : 509;

    if (getNumStringChars() > MaxChars)
      Diags->Report(StringToks.front().getLocation(), diag::ext_string_too_long)
          << getNumStringChars() << MaxChars << (Features.C99 ? 1 : 0)
          << SourceRange(StringToks.front().getLocation(),
                         StringToks.back().getLocation());
  }
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
const char *advanceToValidUTF8(const char *Err, const char *End) {
  if (Err == End)
    return End;
  End = Err + std::min<unsigned>(llvm::getNumBytesForUTF8(*Err), End - Err);
  while (++Err != End && (*Err & 0xC0) == 0x80)
    ;
  return Err;
}
} // namespace

bool StringLiteralParser::copyStringFragment(const Token &Tok,
                                             const char *TokBegin,
                                             llvm::StringRef Fragment) {
  const llvm::UTF8 *ErrorPtrTmp;
  if (ConvertUTF8toWide(CharByteWidth, Fragment, ResultPtr, ErrorPtrTmp))
    return false;

  // If we see bad encoding for unprefixed string literals, warn and
  // simply copy the byte values, for compatibility with gcc and older
  // versions.
  bool NoErrorOnBadEncoding = isOrdinary();
  if (NoErrorOnBadEncoding) {
    memcpy(ResultPtr, Fragment.data(), Fragment.size());
    ResultPtr += Fragment.size();
  }

  if (Diags) {
    const char *ErrorPtr = reinterpret_cast<const char *>(ErrorPtrTmp);

    FullSourceLoc SourceLoc(Tok.getLocation(), SM);
    const DiagnosticBuilder &Builder =
        Diag(Diags, Features, SourceLoc, TokBegin, ErrorPtr,
             advanceToValidUTF8(ErrorPtr, Fragment.end()),
             NoErrorOnBadEncoding ? diag::warn_bad_string_encoding
                                  : diag::err_bad_string_encoding);

    const char *NextStart = advanceToValidUTF8(ErrorPtr, Fragment.end());
    llvm::StringRef NextFragment(NextStart, Fragment.end() - NextStart);

    // Decode into a dummy buffer.
    llvm::SmallString<512> Dummy;
    Dummy.reserve(Fragment.size() * CharByteWidth);
    char *Ptr = Dummy.data();

    while (!ConvertUTF8toWide(CharByteWidth, NextFragment, Ptr, ErrorPtrTmp)) {
      const char *ErrorPtr = reinterpret_cast<const char *>(ErrorPtrTmp);
      NextStart = advanceToValidUTF8(ErrorPtr, Fragment.end());
      Builder << makeCharSourceRange(Features, SourceLoc, TokBegin, ErrorPtr,
                                     NextStart);
      NextFragment = llvm::StringRef(NextStart, Fragment.end() - NextStart);
    }
  }
  return !NoErrorOnBadEncoding;
}

void StringLiteralParser::diagnoseLexingError(SourceLocation Loc) {
  hadError = true;
  if (Diags)
    Diags->Report(Loc, diag::err_lexing_string);
}

unsigned StringLiteralParser::getOffsetOfStringByte(const Token &Tok,
                                                    unsigned ByteNo) const {
  llvm::SmallString<32> SpellingBuffer;
  SpellingBuffer.resize(Tok.getLength());

  bool StringInvalid = false;
  const char *SpellingPtr = &SpellingBuffer[0];
  unsigned TokLen = SourceScanner::getSpelling(Tok, SpellingPtr, SM, Features,
                                               &StringInvalid);
  if (StringInvalid)
    return 0;

  const char *SpellingStart = SpellingPtr;
  const char *SpellingEnd = SpellingPtr + TokLen;

  if (SpellingPtr[0] == 'u' && SpellingPtr[1] == '8')
    SpellingPtr += 2;

  assert(SpellingPtr[0] != 'L' && SpellingPtr[0] != 'u' &&
         SpellingPtr[0] != 'U' && "Doesn't handle wide or utf strings yet");

  // For raw string literals, this is easy.
  if (SpellingPtr[0] == 'R') {
    assert(SpellingPtr[1] == '"' && "Should be a raw string literal!");
    // Skip 'R"'.
    SpellingPtr += 2;
    while (*SpellingPtr != '(') {
      ++SpellingPtr;
      assert(SpellingPtr < SpellingEnd && "Missing ( for raw string literal");
    }
    // Skip '('.
    ++SpellingPtr;
    return SpellingPtr - SpellingStart + ByteNo;
  }

  // Skip over the leading quote
  assert(SpellingPtr[0] == '"' && "Should be a string literal!");
  ++SpellingPtr;

  // Skip over bytes until we find the offset we're looking for.
  while (ByteNo) {
    assert(SpellingPtr < SpellingEnd && "Didn't find byte offset!");

    // Step over non-escapes simply.
    if (*SpellingPtr != '\\') {
      ++SpellingPtr;
      --ByteNo;
      continue;
    }

    // Otherwise, this is an escape character.  Advance over it.
    bool HadError = false;
    if (SpellingPtr[1] == 'u' || SpellingPtr[1] == 'U' ||
        SpellingPtr[1] == 'N') {
      const char *EscapePtr = SpellingPtr;
      unsigned Len = calcUCNByteLength(SpellingStart, SpellingPtr, SpellingEnd,
                                       1, Features, HadError);
      if (Len > ByteNo) {
        // ByteNo is somewhere within the escape sequence.
        SpellingPtr = EscapePtr;
        break;
      }
      ByteNo -= Len;
    } else {
      parseCharEscape(SpellingStart, SpellingPtr, SpellingEnd, HadError,
                      FullSourceLoc(Tok.getLocation(), SM), CharByteWidth * 8,
                      Diags, Features, StringLiteralEvalMethod::Evaluated);
      --ByteNo;
    }
    assert(!HadError && "unexpected use while string parse already failed");
  }

  return SpellingPtr - SpellingStart;
}
