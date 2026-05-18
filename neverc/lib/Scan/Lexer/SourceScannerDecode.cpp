#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Unicode.h"
#include "llvm/Support/UnicodeCharRanges.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

#include "UnicodeCharRanges.inc"

using namespace neverc;

// ===----------------------------------------------------------------------===
// Trigraph tables (shared with line splicing below)
// ===----------------------------------------------------------------------===

namespace {
constexpr char TrigraphTable[128] = {
    ['='] = '#', [')'] = ']',  ['('] = '[', ['!'] = '|', ['\''] = '^',
    ['>'] = '}', ['/'] = '\\', ['<'] = '{', ['-'] = '~',
};

LLVM_ATTRIBUTE_ALWAYS_INLINE
char decodeTrigraph(char Letter) {
  auto U = static_cast<unsigned char>(Letter);
  return (U < 128) ? TrigraphTable[U] : '\0';
}

char processTrigraphSeq(const char *CP, SourceScanner *L, bool Trigraphs) {
  char Res = decodeTrigraph(*CP);
  if (!Res)
    return Res;

  if (!Trigraphs) {
    if (L && !L->isLexingRawMode())
      L->Diag(CP - 2, diag::trigraph_ignored);
    return 0;
  }

  if (L && !L->isLexingRawMode())
    L->Diag(CP - 2, diag::trigraph_converted) << llvm::StringRef(&Res, 1);
  return Res;
}
} // namespace

// ===----------------------------------------------------------------------===
// Line splicing & character decoding
// ===----------------------------------------------------------------------===

unsigned SourceScanner::probeLineSplice(const char *Ptr) {
  unsigned Size = 0;
  while (isWhitespace(Ptr[Size])) {
    ++Size;

    if (Ptr[Size - 1] != '\n' && Ptr[Size - 1] != '\r')
      continue;

    if ((Ptr[Size] == '\r' || Ptr[Size] == '\n') && Ptr[Size - 1] != Ptr[Size])
      ++Size;

    return Size;
  }

  return 0;
}

const char *SourceScanner::skipSplices(const char *P) {
  while (true) {
    const char *AfterEscape;
    if (*P == '\\') {
      AfterEscape = P + 1;
    } else if (*P == '?') {
      if (P[1] != '?' || P[2] != '/')
        return P;
      AfterEscape = P + 3;
    } else {
      return P;
    }

    unsigned NewLineSize = SourceScanner::probeLineSplice(AfterEscape);
    if (NewLineSize == 0)
      return P;
    P = AfterEscape + NewLineSize;
  }
}

std::optional<Token> SourceScanner::peekNextToken(SourceLocation Loc,
                                                  const SourceManager &SM,
                                                  const LangOptions &LangOpts) {
  if (Loc.isMacroID()) {
    if (!SourceScanner::isAtEndOfMacroExpansion(Loc, SM, LangOpts, &Loc))
      return std::nullopt;
  }
  Loc = SourceScanner::getLocForEndOfToken(Loc, 0, SM, LangOpts);

  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  bool InvalidTemp = false;
  llvm::StringRef File = SM.getBufferData(LocInfo.first, &InvalidTemp);
  if (InvalidTemp)
    return std::nullopt;

  const char *TokenBegin = File.data() + LocInfo.second;

  SourceScanner lexer(SM.getLocForStartOfFile(LocInfo.first), LangOpts,
                      File.begin(), TokenBegin, File.end());
  Token Tok;
  lexer.LexFromRawLexer(Tok);
  return Tok;
}

SourceLocation SourceScanner::locateAfterToken(
    SourceLocation Loc, tok::TokenKind TKind, const SourceManager &SM,
    const LangOptions &LangOpts, bool SkipTrailingWhitespaceAndNewLine) {
  std::optional<Token> Tok = peekNextToken(Loc, SM, LangOpts);
  if (!Tok || Tok->isNot(TKind))
    return {};
  SourceLocation TokenLoc = Tok->getLocation();

  unsigned NumWhitespaceChars = 0;
  if (SkipTrailingWhitespaceAndNewLine) {
    const char *TokenEnd = SM.getCharacterData(TokenLoc) + Tok->getLength();
    unsigned char C = *TokenEnd;
    while (isHorizontalWhitespace(C)) {
      C = *(++TokenEnd);
      NumWhitespaceChars++;
    }

    if (C == '\n' || C == '\r') {
      char PrevC = C;
      C = *(++TokenEnd);
      NumWhitespaceChars++;
      if ((C == '\n' || C == '\r') && C != PrevC)
        NumWhitespaceChars++;
    }
  }

  return TokenLoc.getLocWithOffset(Tok->getLength() + NumWhitespaceChars);
}

// Slow path: decode trigraphs and escaped newlines, emitting diagnostics.
SourceScanner::SizedChar SourceScanner::decodeCharSlow(const char *Ptr,
                                                       Token *Tok) {
  unsigned Size = 0;
  if (Ptr[0] == '\\') {
    ++Size;
    ++Ptr;
  Slash:
    if (!isWhitespace(Ptr[0]))
      return {'\\', Size};

    if (unsigned EscapedNewLineSize = probeLineSplice(Ptr)) {
      if (Tok)
        Tok->setFlag(Token::NeedsCleaning);
      if (Ptr[0] != '\n' && Ptr[0] != '\r' && Tok && !isLexingRawMode())
        Diag(Ptr, diag::backslash_newline_space);

      Size += EscapedNewLineSize;
      Ptr += EscapedNewLineSize;

      auto CharAndSize = decodeCharSlow(Ptr, Tok);
      CharAndSize.Size += Size;
      return CharAndSize;
    }

    return {'\\', Size};
  }

  if (Ptr[0] == '?' && Ptr[1] == '?') {
    if (char C = processTrigraphSeq(Ptr + 2, Tok ? this : nullptr,
                                    LangOpts.Trigraphs)) {
      if (Tok)
        Tok->setFlag(Token::NeedsCleaning);
      Ptr += 3;
      Size += 3;
      if (C == '\\')
        goto Slash;
      return {C, Size};
    }
  }

  return {*Ptr, Size + 1u};
}

// Silent variant: same logic, no diagnostics.
SourceScanner::SizedChar
SourceScanner::decodeCharSlowSilent(const char *Ptr,
                                    const LangOptions &LangOpts) {
  unsigned Size = 0;
  if (Ptr[0] == '\\') {
    ++Size;
    ++Ptr;
  Slash:
    if (!isWhitespace(Ptr[0]))
      return {'\\', Size};

    if (unsigned EscapedNewLineSize = probeLineSplice(Ptr)) {
      Size += EscapedNewLineSize;
      Ptr += EscapedNewLineSize;
      auto CharAndSize = decodeCharSlowSilent(Ptr, LangOpts);
      CharAndSize.Size += Size;
      return CharAndSize;
    }

    return {'\\', Size};
  }

  if (LangOpts.Trigraphs && Ptr[0] == '?' && Ptr[1] == '?') {
    if (char C = decodeTrigraph(Ptr[2])) {
      Ptr += 3;
      Size += 3;
      if (C == '\\')
        goto Slash;
      return {C, Size};
    }
  }

  return {*Ptr, Size + 1u};
}

void SourceScanner::setByteOffset(unsigned Offset, bool StartOfLine) {
  BufferPtr = BufferStart + Offset;
  if (BufferPtr > BufferEnd)
    BufferPtr = BufferEnd;
  IsAtStartOfLine = StartOfLine;
  IsAtPhysicalStartOfLine = StartOfLine;
}

// ===----------------------------------------------------------------------===
// Unicode & identifier classification
// ===----------------------------------------------------------------------===

namespace {
bool isUnicodeSpaceChar(uint32_t Codepoint) {
  static const llvm::sys::UnicodeCharSet UnicodeWhitespaceChars(
      UnicodeWhitespaceCharRanges);
  return UnicodeWhitespaceChars.contains(Codepoint);
}

llvm::SmallString<5> formatCodepointHex(uint32_t C) {
  llvm::SmallString<5> CharBuf;
  llvm::raw_svector_ostream CharOS(CharBuf);
  llvm::write_hex(CharOS, C, llvm::HexPrintStyle::Upper, 4);
  return CharBuf;
}

// Math notation profile (Unicode L2/2022/22230).
bool isMathNotationIdChar(uint32_t C, const LangOptions &LangOpts, bool IsStart,
                          bool &IsExtension) {
  static const llvm::sys::UnicodeCharSet MathStartChars(
      MathematicalNotationProfileIDStartRanges);
  static const llvm::sys::UnicodeCharSet MathContinueChars(
      MathematicalNotationProfileIDContinueRanges);
  if (MathStartChars.contains(C) ||
      (!IsStart && MathContinueChars.contains(C))) {
    IsExtension = true;
    return true;
  }
  return false;
}

bool isAllowedIdentContinue(uint32_t C, const LangOptions &LangOpts,
                            bool &IsExtension) {
  if (LangOpts.AsmPreprocessor) {
    return false;
  } else if (LangOpts.DollarIdents && '$' == C) {
    return true;
  } else if (LangOpts.C23) {
    // XID_Continue must check both tables; '_' is special-cased.
    static const llvm::sys::UnicodeCharSet XIDStartChars(XIDStartRanges);
    static const llvm::sys::UnicodeCharSet XIDContinueChars(XIDContinueRanges);
    if (C == '_' || XIDStartChars.contains(C) || XIDContinueChars.contains(C))
      return true;
    return isMathNotationIdChar(C, LangOpts, /*IsStart=*/false, IsExtension);
  } else if (LangOpts.C11) {
    static const llvm::sys::UnicodeCharSet C11AllowedIDChars(
        C11AllowedIDCharRanges);
    return C11AllowedIDChars.contains(C);
  } else {
    static const llvm::sys::UnicodeCharSet C99AllowedIDChars(
        C99AllowedIDCharRanges);
    return C99AllowedIDChars.contains(C);
  }
}

bool isAllowedIdentStart(uint32_t C, const LangOptions &LangOpts,
                         bool &IsExtension) {
  assert(C > 0x7F && "isAllowedIdentStart called with an ASCII codepoint");
  IsExtension = false;
  if (LangOpts.AsmPreprocessor) {
    return false;
  }
  if (LangOpts.C23) {
    static const llvm::sys::UnicodeCharSet XIDStartChars(XIDStartRanges);
    if (XIDStartChars.contains(C))
      return true;
    return isMathNotationIdChar(C, LangOpts, /*IsStart=*/true, IsExtension);
  }
  if (!isAllowedIdentContinue(C, LangOpts, IsExtension))
    return false;
  if (LangOpts.C11) {
    static const llvm::sys::UnicodeCharSet C11DisallowedInitialIDChars(
        C11DisallowedInitialIDCharRanges);
    return !C11DisallowedInitialIDChars.contains(C);
  }
  static const llvm::sys::UnicodeCharSet C99DisallowedInitialIDChars(
      C99DisallowedInitialIDCharRanges);
  return !C99DisallowedInitialIDChars.contains(C);
}

LLVM_ATTRIBUTE_NOINLINE
void diagMathNotationInIdent(DiagnosticsEngine &Diags, uint32_t C,
                             CharSourceRange Range) {

  static const llvm::sys::UnicodeCharSet MathStartChars(
      MathematicalNotationProfileIDStartRanges);
  static const llvm::sys::UnicodeCharSet MathContinueChars(
      MathematicalNotationProfileIDContinueRanges);

  (void)MathStartChars;
  (void)MathContinueChars;
  assert((MathStartChars.contains(C) || MathContinueChars.contains(C)) &&
         "Unexpected mathematical notation codepoint");
  Diags.Report(Range.getBegin(), diag::ext_mathematical_notation)
      << formatCodepointHex(C) << Range;
}
} // namespace

namespace {
inline CharSourceRange buildCharRange(SourceScanner &L, const char *Begin,
                                      const char *End) {
  return CharSourceRange::getCharRange(L.getSourceLocation(Begin),
                                       L.getSourceLocation(End));
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void checkC99IdentCompat(DiagnosticsEngine &Diags, uint32_t C,
                         CharSourceRange Range, bool IsFirst) {
  if (!Diags.isIgnored(diag::warn_c99_compat_unicode_id, Range.getBegin())) {
    enum { CannotAppearInIdentifier = 0, CannotStartIdentifier };

    static const llvm::sys::UnicodeCharSet C99AllowedIDChars(
        C99AllowedIDCharRanges);
    static const llvm::sys::UnicodeCharSet C99DisallowedInitialIDChars(
        C99DisallowedInitialIDCharRanges);
    if (!C99AllowedIDChars.contains(C)) {
      Diags.Report(Range.getBegin(), diag::warn_c99_compat_unicode_id)
          << Range << CannotAppearInIdentifier;
    } else if (IsFirst && C99DisallowedInitialIDChars.contains(C)) {
      Diags.Report(Range.getBegin(), diag::warn_c99_compat_unicode_id)
          << Range << CannotStartIdentifier;
    }
  }
}

LLVM_ATTRIBUTE_NOINLINE
void warnConfusableGlyph(DiagnosticsEngine &Diags, uint32_t C,
                         CharSourceRange Range) {
  struct HomoglyphPair {
    uint32_t Character;
    char LooksLike;
    bool operator<(HomoglyphPair R) const { return Character < R.Character; }
  };
  static constexpr HomoglyphPair SortedHomoglyphs[] = {
      {U'\u00ad', 0},    // SOFT HYPHEN
      {U'\u01c3', '!'},  // LATIN LETTER RETROFLEX CLICK
      {U'\u037e', ';'},  // GREEK QUESTION MARK
      {U'\u200b', 0},    // ZERO WIDTH SPACE
      {U'\u200c', 0},    // ZERO WIDTH NON-JOINER
      {U'\u200d', 0},    // ZERO WIDTH JOINER
      {U'\u2060', 0},    // WORD JOINER
      {U'\u2061', 0},    // FUNCTION APPLICATION
      {U'\u2062', 0},    // INVISIBLE TIMES
      {U'\u2063', 0},    // INVISIBLE SEPARATOR
      {U'\u2064', 0},    // INVISIBLE PLUS
      {U'\u2212', '-'},  // MINUS SIGN
      {U'\u2215', '/'},  // DIVISION SLASH
      {U'\u2216', '\\'}, // SET MINUS
      {U'\u2217', '*'},  // ASTERISK OPERATOR
      {U'\u2223', '|'},  // DIVIDES
      {U'\u2227', '^'},  // LOGICAL AND
      {U'\u2236', ':'},  // RATIO
      {U'\u223c', '~'},  // TILDE OPERATOR
      {U'\ua789', ':'},  // MODIFIER LETTER COLON
      {U'\ufeff', 0},    // ZERO WIDTH NO-BREAK SPACE
      {U'\uff01', '!'},  // FULLWIDTH EXCLAMATION MARK
      {U'\uff03', '#'},  // FULLWIDTH NUMBER SIGN
      {U'\uff04', '$'},  // FULLWIDTH DOLLAR SIGN
      {U'\uff05', '%'},  // FULLWIDTH PERCENT SIGN
      {U'\uff06', '&'},  // FULLWIDTH AMPERSAND
      {U'\uff08', '('},  // FULLWIDTH LEFT PARENTHESIS
      {U'\uff09', ')'},  // FULLWIDTH RIGHT PARENTHESIS
      {U'\uff0a', '*'},  // FULLWIDTH ASTERISK
      {U'\uff0b', '+'},  // FULLWIDTH ASTERISK
      {U'\uff0c', ','},  // FULLWIDTH COMMA
      {U'\uff0d', '-'},  // FULLWIDTH HYPHEN-MINUS
      {U'\uff0e', '.'},  // FULLWIDTH FULL STOP
      {U'\uff0f', '/'},  // FULLWIDTH SOLIDUS
      {U'\uff1a', ':'},  // FULLWIDTH COLON
      {U'\uff1b', ';'},  // FULLWIDTH SEMICOLON
      {U'\uff1c', '<'},  // FULLWIDTH LESS-THAN SIGN
      {U'\uff1d', '='},  // FULLWIDTH EQUALS SIGN
      {U'\uff1e', '>'},  // FULLWIDTH GREATER-THAN SIGN
      {U'\uff1f', '?'},  // FULLWIDTH QUESTION MARK
      {U'\uff20', '@'},  // FULLWIDTH COMMERCIAL AT
      {U'\uff3b', '['},  // FULLWIDTH LEFT SQUARE BRACKET
      {U'\uff3c', '\\'}, // FULLWIDTH REVERSE SOLIDUS
      {U'\uff3d', ']'},  // FULLWIDTH RIGHT SQUARE BRACKET
      {U'\uff3e', '^'},  // FULLWIDTH CIRCUMFLEX ACCENT
      {U'\uff5b', '{'},  // FULLWIDTH LEFT CURLY BRACKET
      {U'\uff5c', '|'},  // FULLWIDTH VERTICAL LINE
      {U'\uff5d', '}'},  // FULLWIDTH RIGHT CURLY BRACKET
      {U'\uff5e', '~'},  // FULLWIDTH TILDE
      {0, 0}};
  auto Homoglyph =
      std::lower_bound(std::begin(SortedHomoglyphs),
                       std::end(SortedHomoglyphs) - 1, HomoglyphPair{C, '\0'});
  if (Homoglyph->Character == C) {
    if (Homoglyph->LooksLike) {
      const char LooksLikeStr[] = {Homoglyph->LooksLike, 0};
      Diags.Report(Range.getBegin(), diag::warn_utf8_symbol_homoglyph)
          << Range << formatCodepointHex(C) << LooksLikeStr;
    } else {
      Diags.Report(Range.getBegin(), diag::warn_utf8_symbol_zero_width)
          << Range << formatCodepointHex(C);
    }
  }
}

LLVM_ATTRIBUTE_NOINLINE
void diagBadIdentCodepoint(DiagnosticsEngine &Diags,
                           const LangOptions &LangOpts, uint32_t CodePoint,
                           CharSourceRange Range, bool IsFirst) {
  if (isASCII(CodePoint))
    return;

  bool IsExtension;
  bool IsIDStart = isAllowedIdentStart(CodePoint, LangOpts, IsExtension);
  bool IsIDContinue =
      IsIDStart || isAllowedIdentContinue(CodePoint, LangOpts, IsExtension);

  if ((IsFirst && IsIDStart) || (!IsFirst && IsIDContinue))
    return;

  bool InvalidOnlyAtStart = IsFirst && !IsIDStart && IsIDContinue;

  if (!IsFirst || InvalidOnlyAtStart) {
    Diags.Report(Range.getBegin(), diag::err_character_not_allowed_identifier)
        << Range << formatCodepointHex(CodePoint) << int(InvalidOnlyAtStart)
        << FixItHint::CreateRemoval(Range);
  } else {
    Diags.Report(Range.getBegin(), diag::err_character_not_allowed)
        << Range << formatCodepointHex(CodePoint)
        << FixItHint::CreateRemoval(Range);
  }
}
} // namespace

bool SourceScanner::tryConsumeIdentifierUCN(const char *&CurPtr, unsigned Size,
                                            Token &Result) {
  const char *UCNPtr = CurPtr + Size;
  uint32_t CodePoint = tryReadUCN(UCNPtr, CurPtr, /*Token=*/nullptr);
  if (CodePoint == 0) {
    return false;
  }
  bool IsExtension = false;
  if (!isAllowedIdentContinue(CodePoint, LangOpts, IsExtension)) {
    if (isASCII(CodePoint) || isUnicodeSpaceChar(CodePoint))
      return false;
    if (!isLexingRawMode() && !ParsingDirective && !PP->isPreprocessedOutput())
      diagBadIdentCodepoint(PP->getDiagnostics(), LangOpts, CodePoint,
                            buildCharRange(*this, CurPtr, UCNPtr),
                            /*IsFirst=*/false);

  } else if (!isLexingRawMode()) {
    if (IsExtension)
      diagMathNotationInIdent(PP->getDiagnostics(), CodePoint,
                              buildCharRange(*this, CurPtr, UCNPtr));

    checkC99IdentCompat(PP->getDiagnostics(), CodePoint,
                        buildCharRange(*this, CurPtr, UCNPtr),
                        /*IsFirst=*/false);
  }

  Result.setFlag(Token::HasUCN);
  if ((UCNPtr - CurPtr == 6 && CurPtr[1] == 'u') ||
      (UCNPtr - CurPtr == 10 && CurPtr[1] == 'U'))
    CurPtr = UCNPtr;
  else
    while (CurPtr != UCNPtr)
      (void)readChar(CurPtr, Result);
  return true;
}

bool SourceScanner::tryConsumeIdentifierUTF8Char(const char *&CurPtr,
                                                 Token &Result) {
  llvm::UTF32 CodePoint;

  unsigned FirstCodeUnitSize;
  peekCharSize(CurPtr, FirstCodeUnitSize);
  const char *CharStart = CurPtr + FirstCodeUnitSize - 1;
  const char *UnicodePtr = CharStart;

  llvm::ConversionResult ConvResult = llvm::convertUTF8Sequence(
      (const llvm::UTF8 **)&UnicodePtr, (const llvm::UTF8 *)BufferEnd,
      &CodePoint, llvm::strictConversion);
  if (ConvResult != llvm::conversionOK)
    return false;

  bool IsExtension = false;
  if (!isAllowedIdentContinue(static_cast<uint32_t>(CodePoint), LangOpts,
                              IsExtension)) {
    if (isASCII(CodePoint) || isUnicodeSpaceChar(CodePoint))
      return false;

    if (!isLexingRawMode() && !ParsingDirective && !PP->isPreprocessedOutput())
      diagBadIdentCodepoint(PP->getDiagnostics(), LangOpts, CodePoint,
                            buildCharRange(*this, CharStart, UnicodePtr),
                            /*IsFirst=*/false);
  } else if (!isLexingRawMode()) {
    if (IsExtension)
      diagMathNotationInIdent(PP->getDiagnostics(), CodePoint,
                              buildCharRange(*this, CharStart, UnicodePtr));
    checkC99IdentCompat(PP->getDiagnostics(), CodePoint,
                        buildCharRange(*this, CharStart, UnicodePtr),
                        /*IsFirst=*/false);
    warnConfusableGlyph(PP->getDiagnostics(), CodePoint,
                        buildCharRange(*this, CharStart, UnicodePtr));
  }

  // consumeChar sets NeedsCleaning and emits trailing-space warnings.
  consumeChar(CurPtr, FirstCodeUnitSize, Result);
  CurPtr = UnicodePtr;
  return true;
}

__attribute__((cold, noinline)) bool
SourceScanner::processUnicodeIdentStart(Token &Result, uint32_t C,
                                        const char *CurPtr) {
  bool IsExtension = false;
  if (isAllowedIdentStart(C, LangOpts, IsExtension)) {
    if (!isLexingRawMode() && !ParsingDirective &&
        !PP->isPreprocessedOutput()) {
      if (IsExtension)
        diagMathNotationInIdent(PP->getDiagnostics(), C,
                                buildCharRange(*this, BufferPtr, CurPtr));
      checkC99IdentCompat(PP->getDiagnostics(), C,
                          buildCharRange(*this, BufferPtr, CurPtr),
                          /*IsFirst=*/true);
      warnConfusableGlyph(PP->getDiagnostics(), C,
                          buildCharRange(*this, BufferPtr, CurPtr));
    }

    MIOpt.readToken();
    return scanIdentRest(Result, CurPtr);
  }

  if (!isLexingRawMode() && !ParsingDirective && !PP->isPreprocessedOutput() &&
      !isASCII(*BufferPtr) && !isUnicodeSpaceChar(C)) {
    diagBadIdentCodepoint(PP->getDiagnostics(), LangOpts, C,
                          buildCharRange(*this, BufferPtr, CurPtr),
                          /*IsStart*/ true);
    BufferPtr = CurPtr;
    return false;
  }

  MIOpt.readToken();
  emitToken(Result, CurPtr, tok::unknown);
  return true;
}
