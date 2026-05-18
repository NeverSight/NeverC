#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Unicode.h"
#include "llvm/Support/UnicodeCharRanges.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#ifdef __AVX512BW__
#include <immintrin.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include "UnicodeCharRanges.inc"

using namespace neverc;

#include "SourceScannerSIMDHelpers.inc"

namespace {
inline CharSourceRange buildCharRange(SourceScanner &L, const char *Begin,
                                      const char *End) {
  return CharSourceRange::getCharRange(L.getSourceLocation(Begin),
                                       L.getSourceLocation(End));
}

bool isUnicodeSpaceChar(uint32_t Codepoint) {
  static const llvm::sys::UnicodeCharSet UnicodeWhitespaceChars(
      UnicodeWhitespaceCharRanges);
  return UnicodeWhitespaceChars.contains(Codepoint);
}
} // namespace

// ===----------------------------------------------------------------------===
// Comments & whitespace
// ===----------------------------------------------------------------------===

__attribute__((hot, flatten)) bool
SourceScanner::consumeSpacing(Token &Result, const char *CurPtr,
                              bool &TokAtPhysicalStartOfLine) {
  bool SawNewline = isVerticalWhitespace(CurPtr[-1]);

  const char *lastNewLine = nullptr;
  auto setLastNewLine = [&](const char *Ptr) {
    lastNewLine = Ptr;
    if (!NewLinePtr)
      NewLinePtr = Ptr;
  };
  if (SawNewline)
    setLastNewLine(CurPtr - 1);

  while (true) {
    if (isHorizontalWhitespace(*CurPtr)) {
      ++CurPtr;
      if (LLVM_UNLIKELY(isHorizontalWhitespace(*CurPtr)))
        CurPtr = skipWhitespaceRun(CurPtr, BufferEnd);
    }

    if (LLVM_LIKELY(!isVerticalWhitespace(*CurPtr)))
      break;

    if (LLVM_UNLIKELY(ParsingDirective)) {
      BufferPtr = CurPtr;
      return false;
    }

    if (*CurPtr == '\n')
      setLastNewLine(CurPtr);
    SawNewline = true;
    ++CurPtr;
  }

  if (LLVM_UNLIKELY(isKeepWhitespaceMode())) {
    emitToken(Result, CurPtr, tok::unknown);
    if (SawNewline) {
      IsAtStartOfLine = true;
      IsAtPhysicalStartOfLine = true;
    }
    return true;
  }

  char PrevChar = CurPtr[-1];
  bool HasLeadingSpace = !isVerticalWhitespace(PrevChar);

  Result.setFlagValue(Token::LeadingSpace, HasLeadingSpace);
  if (SawNewline) {
    Result.setFlag(Token::StartOfLine);
    TokAtPhysicalStartOfLine = true;

    if (NewLinePtr && lastNewLine && NewLinePtr != lastNewLine && PP) {
      if (auto *Handler = PP->getEmptylineHandler())
        Handler->ProcessEmptyline(
            SourceRange(getSourceLocationFast(NewLinePtr + 1, 1),
                        getSourceLocationFast(lastNewLine, 1)));
    }
  }

  BufferPtr = CurPtr;
  return false;
}

__attribute__((hot, flatten)) bool
SourceScanner::consumeLineComment(Token &Result, const char *CurPtr,
                                  bool &TokAtPhysicalStartOfLine) {
  if (!LineComment) {
    if (!isLexingRawMode())
      Diag(BufferPtr, diag::ext_line_comment);
    LineComment = true;
  }

  bool UnicodeDecodingAlreadyDiagnosed = false;

  char C;
  while (true) {
    CurPtr = scanToLineBreak(CurPtr, BufferEnd);
    UnicodeDecodingAlreadyDiagnosed = false;
    C = *CurPtr;

    if (!isASCII(C)) {
      unsigned Length = llvm::getUTF8SequenceSize(
          (const llvm::UTF8 *)CurPtr, (const llvm::UTF8 *)BufferEnd);
      if (Length == 0) {
        if (!UnicodeDecodingAlreadyDiagnosed && !isLexingRawMode())
          Diag(CurPtr, diag::warn_invalid_utf8_in_comment);
        UnicodeDecodingAlreadyDiagnosed = true;
        ++CurPtr;
      } else {
        UnicodeDecodingAlreadyDiagnosed = false;
        CurPtr += Length;
      }
      continue;
    }

    const char *NextLine = CurPtr;
    if (C != 0) {
      const char *EscapePtr = CurPtr - 1;
      bool HasSpace = false;
      while (isHorizontalWhitespace(*EscapePtr)) {
        --EscapePtr;
        HasSpace = true;
      }

      if (*EscapePtr == '\\')
        CurPtr = EscapePtr;
      else if (EscapePtr[0] == '/' && EscapePtr[-1] == '?' &&
               EscapePtr[-2] == '?' && LangOpts.Trigraphs)
        CurPtr = EscapePtr - 2;
      else
        break;

      if (HasSpace && !isLexingRawMode())
        Diag(EscapePtr, diag::backslash_newline_space);
    }

    const char *OldPtr = CurPtr;
    bool OldRawMode = isLexingRawMode();
    LexingRawMode = true;
    C = readChar(CurPtr, Result);
    LexingRawMode = OldRawMode;

    if (C != 0 && CurPtr == OldPtr + 1) {
      CurPtr = NextLine;
      break;
    }

    if (CurPtr != OldPtr + 1 && C != '/' &&
        (CurPtr == BufferEnd + 1 || CurPtr[0] != '/')) {
      for (; OldPtr != CurPtr; ++OldPtr)
        if (OldPtr[0] == '\n' || OldPtr[0] == '\r') {
          if (isWhitespace(C)) {
            const char *ForwardPtr = CurPtr;
            while (isWhitespace(*ForwardPtr))
              ++ForwardPtr;
            if (ForwardPtr[0] == '/' && ForwardPtr[1] == '/')
              break;
          }

          if (!isLexingRawMode())
            Diag(OldPtr - 1, diag::ext_multi_line_line_comment);
          break;
        }
    }

    if (C == '\r' || C == '\n' || CurPtr == BufferEnd + 1) {
      --CurPtr;
      break;
    }
  }
  if (PP && !isLexingRawMode() &&
      PP->DispatchComment(Result,
                          SourceRange(getSourceLocationFast(BufferPtr, 1),
                                      getSourceLocationFast(CurPtr, 1)))) {
    BufferPtr = CurPtr;
    return true; // A token has to be returned.
  }

  if (inKeepCommentMode())
    return finalizeLineComment(Result, CurPtr);

  if (ParsingDirective || CurPtr == BufferEnd) {
    BufferPtr = CurPtr;
    return false;
  }

  NewLinePtr = CurPtr++;

  Result.setFlag(Token::StartOfLine);
  TokAtPhysicalStartOfLine = true;
  Result.clearFlag(Token::LeadingSpace);
  BufferPtr = CurPtr;
  return false;
}

bool SourceScanner::finalizeLineComment(Token &Result, const char *CurPtr) {
  emitToken(Result, CurPtr, tok::comment);

  if (!ParsingDirective || LexingRawMode)
    return true;

  bool Invalid = false;
  std::string Spelling = PP->getSpelling(Result, &Invalid);
  if (Invalid)
    return true;

  assert(Spelling[0] == '/' && Spelling[1] == '/' && "Not line comment?");
  Spelling[1] = '*'; // Change prefix to "/*".
  Spelling += "*/";  // add suffix.

  Result.setKind(tok::comment);
  PP->WriteScratch(Spelling, Result, Result.getLocation(),
                   Result.getLocation());
  return true;
}

namespace {
bool isLineSplicedBlockEnd(const char *CurPtr, SourceScanner *L,
                           bool Trigraphs) {
  assert(CurPtr[0] == '\n' || CurPtr[0] == '\r');

  const char *TrigraphPos = nullptr;
  const char *SpacePos = nullptr;

  while (true) {
    --CurPtr;

    if (CurPtr[0] == '\n' || CurPtr[0] == '\r') {
      if (CurPtr[0] == CurPtr[1])
        return false;
      --CurPtr;
    }

    while (isHorizontalWhitespace(*CurPtr) || *CurPtr == 0) {
      SpacePos = CurPtr;
      --CurPtr;
    }

    if (*CurPtr == '\\') {
      --CurPtr;
    } else if (CurPtr[0] == '/' && CurPtr[-1] == '?' && CurPtr[-2] == '?') {
      TrigraphPos = CurPtr - 2;
      CurPtr -= 3;
    } else {
      return false;
    }

    if (*CurPtr == '*')
      break;
    if (*CurPtr != '\n' && *CurPtr != '\r')
      return false;
  }

  if (TrigraphPos) {
    if (!Trigraphs) {
      if (!L->isLexingRawMode())
        L->Diag(TrigraphPos, diag::trigraph_ignored_block_comment);
      return false;
    }
    if (!L->isLexingRawMode())
      L->Diag(TrigraphPos, diag::trigraph_ends_block_comment);
  }

  if (!L->isLexingRawMode())
    L->Diag(CurPtr + 1, diag::escaped_newline_block_comment_end);

  if (SpacePos && !L->isLexingRawMode())
    L->Diag(SpacePos, diag::backslash_newline_space);

  return true;
}
} // namespace

__attribute__((hot)) bool
SourceScanner::consumeBlockComment(Token &Result, const char *CurPtr,
                                   bool &TokAtPhysicalStartOfLine) {
  unsigned CharSize;
  unsigned char C = peekCharSize(CurPtr, CharSize);
  CurPtr += CharSize;
  if (C == 0 && CurPtr == BufferEnd + 1) {
    if (!isLexingRawMode())
      Diag(BufferPtr, diag::err_unterminated_block_comment);
    --CurPtr;

    if (isKeepWhitespaceMode()) {
      emitToken(Result, CurPtr, tok::unknown);
      return true;
    }

    BufferPtr = CurPtr;
    return false;
  }

  if (C == '/')
    C = *CurPtr++;

  bool UnicodeDecodingAlreadyDiagnosed = false;

  while (true) {
    if (CurPtr + 16 <= BufferEnd && C != '/' && C != '\0' && isASCII(C)) {
      __builtin_prefetch(CurPtr + 64, 0, 2);
      CurPtr = scanBlockBody(CurPtr, BufferEnd);
      UnicodeDecodingAlreadyDiagnosed = false;
      C = *CurPtr++;
    }

    while (C != '/' && C != '\0') {
      if (isASCII(C)) {
        UnicodeDecodingAlreadyDiagnosed = false;
        C = *CurPtr++;
        continue;
      }
      unsigned Length = llvm::getUTF8SequenceSize(
          (const llvm::UTF8 *)CurPtr - 1, (const llvm::UTF8 *)BufferEnd);
      if (Length == 0) {
        if (!UnicodeDecodingAlreadyDiagnosed && !isLexingRawMode())
          Diag(CurPtr - 1, diag::warn_invalid_utf8_in_comment);
        UnicodeDecodingAlreadyDiagnosed = true;
      } else {
        UnicodeDecodingAlreadyDiagnosed = false;
        CurPtr += Length - 1;
      }
      C = *CurPtr++;
    }

    if (C == '/') {
      if (LLVM_LIKELY(CurPtr[-2] == '*'))
        break;

      if (LLVM_UNLIKELY(CurPtr[-2] == '\n' || CurPtr[-2] == '\r')) {
        if (isLineSplicedBlockEnd(CurPtr - 2, this, LangOpts.Trigraphs))
          break;
      }
      if (LLVM_UNLIKELY(CurPtr[0] == '*' && CurPtr[1] != '/')) {
        if (!isLexingRawMode())
          Diag(CurPtr - 1, diag::warn_nested_block_comment);
      }
    } else if (C == 0 && CurPtr == BufferEnd + 1) {
      if (!isLexingRawMode())
        Diag(BufferPtr, diag::err_unterminated_block_comment);
      --CurPtr;

      if (isKeepWhitespaceMode()) {
        emitToken(Result, CurPtr, tok::unknown);
        return true;
      }

      BufferPtr = CurPtr;
      return false;
    }

    C = *CurPtr++;
  }

  if (PP && !isLexingRawMode() &&
      PP->DispatchComment(Result,
                          SourceRange(getSourceLocationFast(BufferPtr, 1),
                                      getSourceLocationFast(CurPtr, 1)))) {
    BufferPtr = CurPtr;
    return true;
  }

  if (inKeepCommentMode()) {
    emitToken(Result, CurPtr, tok::comment);
    return true;
  }

  if (isHorizontalWhitespace(*CurPtr)) {
    consumeSpacing(Result, CurPtr + 1, TokAtPhysicalStartOfLine);
    return false;
  }

  BufferPtr = CurPtr;
  Result.setFlag(Token::LeadingSpace);
  return false;
}

void SourceScanner::drainDirectiveLine(llvm::SmallVectorImpl<char> *Result) {
  assert(ParsingDirective && ParsingFilename == false &&
         "Must be in a preprocessing directive!");
  Token Tmp;
  Tmp.startToken();

  const char *CurPtr = BufferPtr;
  while (true) {
    char Char = readChar(CurPtr, Tmp);
    switch (Char) {
    default:
      if (Result)
        Result->push_back(Char);
      break;
    case 0:
      if (CurPtr - 1 != BufferEnd) {
        if (Result)
          Result->push_back(Char);
        break;
      }
      [[fallthrough]];
    case '\r':
    case '\n':
      assert(CurPtr[-1] == Char && "Trigraphs for newline?");
      BufferPtr = CurPtr - 1;
      Lex(Tmp);
      assert(Tmp.is(tok::eod) && "Unexpected token!");
      return;
    }
  }
}

// ===----------------------------------------------------------------------===
// Buffer management & conflict markers
// ===----------------------------------------------------------------------===

__attribute__((cold, noinline)) bool
SourceScanner::onBufferTerminator(Token &Result, const char *CurPtr) {
  if (ParsingDirective) {
    ParsingDirective = false;
    emitToken(Result, CurPtr, tok::eod);
    if (PP)
      resetExtendedTokenMode();
    return true;
  }

  if (isLexingRawMode()) {
    Result.startToken();
    BufferPtr = BufferEnd;
    emitToken(Result, BufferEnd, tok::eof);
    return true;
  }

  while (!ConditionalStack.empty()) {
    PP->Diag(ConditionalStack.back().IfLoc,
             diag::err_pp_unterminated_conditional);
    ConditionalStack.pop_back();
  }

  // Pedantic: source files must end with a newline.
  if (CurPtr != BufferStart && (CurPtr[-1] != '\n' && CurPtr[-1] != '\r')) {
    SourceLocation EndLoc = getSourceLocation(BufferEnd);
    Diag(BufferEnd, diag::ext_no_newline_eof)
        << FixItHint::CreateInsertion(EndLoc, "\n");
  }

  BufferPtr = CurPtr;
  return PP->FinalizeSourceEnd(Result, isPragmaLexer());
}

unsigned SourceScanner::probeLeftParen() {
  assert(!LexingRawMode && "How can we expand a macro from a skipping buffer?");

  LexingRawMode = true;
  const char *TmpBufferPtr = BufferPtr;
  bool inPPDirectiveMode = ParsingDirective;
  bool atStartOfLine = IsAtStartOfLine;
  bool atPhysicalStartOfLine = IsAtPhysicalStartOfLine;
  bool leadingSpace = HasLeadingSpace;

  Token Tok;
  Lex(Tok);

  BufferPtr = TmpBufferPtr;
  ParsingDirective = inPPDirectiveMode;
  HasLeadingSpace = leadingSpace;
  IsAtStartOfLine = atStartOfLine;
  IsAtPhysicalStartOfLine = atPhysicalStartOfLine;
  LexingRawMode = false;

  if (Tok.is(tok::eof))
    return 2;
  return Tok.is(tok::l_paren);
}

namespace {
const char *locateConflictEnd(const char *CurPtr, const char *BufferEnd,
                              ConflictMarkerKind CMK) {
  const char *Terminator = CMK == CMK_Perforce ? "<<<<\n" : ">>>>>>>";
  size_t TermLen = CMK == CMK_Perforce ? 5 : 7;
  auto RestOfBuffer =
      llvm::StringRef(CurPtr, BufferEnd - CurPtr).substr(TermLen);
  size_t Pos = RestOfBuffer.find(Terminator);
  while (Pos != llvm::StringRef::npos) {
    if (Pos == 0 ||
        (RestOfBuffer[Pos - 1] != '\r' && RestOfBuffer[Pos - 1] != '\n')) {
      RestOfBuffer = RestOfBuffer.substr(Pos + TermLen);
      Pos = RestOfBuffer.find(Terminator);
      continue;
    }
    return RestOfBuffer.data() + Pos;
  }
  return nullptr;
}
} // namespace

__attribute__((cold, noinline)) bool
SourceScanner::matchConflictBegin(const char *CurPtr) {
  if (CurPtr != BufferStart && CurPtr[-1] != '\n' && CurPtr[-1] != '\r')
    return false;

  if (!llvm::StringRef(CurPtr, BufferEnd - CurPtr).starts_with("<<<<<<<") &&
      !llvm::StringRef(CurPtr, BufferEnd - CurPtr).starts_with(">>>> "))
    return false;

  if (CurrentConflictMarkerState || isLexingRawMode())
    return false;

  ConflictMarkerKind Kind = *CurPtr == '<' ? CMK_Normal : CMK_Perforce;

  if (locateConflictEnd(CurPtr, BufferEnd, Kind)) {
    Diag(CurPtr, diag::err_conflict_marker);
    CurrentConflictMarkerState = Kind;

    while (*CurPtr != '\r' && *CurPtr != '\n') {
      assert(CurPtr != BufferEnd && "Didn't find end of line");
      ++CurPtr;
    }
    BufferPtr = CurPtr;
    return true;
  }

  return false;
}

__attribute__((cold, noinline)) bool
SourceScanner::matchConflictClose(const char *CurPtr) {
  if (CurPtr != BufferStart && CurPtr[-1] != '\n' && CurPtr[-1] != '\r')
    return false;

  if (!CurrentConflictMarkerState || isLexingRawMode())
    return false;

  for (unsigned i = 1; i != 4; ++i)
    if (CurPtr[i] != CurPtr[0])
      return false;

  if (const char *End =
          locateConflictEnd(CurPtr, BufferEnd, CurrentConflictMarkerState)) {
    CurPtr = End;
    while (CurPtr != BufferEnd && *CurPtr != '\r' && *CurPtr != '\n')
      ++CurPtr;
    BufferPtr = CurPtr;
    CurrentConflictMarkerState = CMK_None;
    return true;
  }

  return false;
}

std::optional<uint32_t> SourceScanner::tryReadNumericUCN(const char *&StartPtr,
                                                         const char *SlashLoc,
                                                         Token *Result) {
  unsigned CharSize;
  char Kind = peekCharSize(StartPtr, CharSize);
  assert((Kind == 'u' || Kind == 'U') && "expected a UCN");

  unsigned NumHexDigits;
  if (Kind == 'u')
    NumHexDigits = 4;
  else if (Kind == 'U')
    NumHexDigits = 8;

  bool Delimited = false;
  bool FoundEndDelimiter = false;
  unsigned Count = 0;
  bool Diagnose = Result && !isLexingRawMode();

  if (!LangOpts.C99) {
    if (Diagnose)
      Diag(SlashLoc, diag::warn_ucn_not_valid_in_c89);
    return std::nullopt;
  }

  const char *CurPtr = StartPtr + CharSize;
  const char *KindLoc = &CurPtr[-1];

  uint32_t CodePoint = 0;
  while (Count != NumHexDigits || Delimited) {
    char C = peekCharSize(CurPtr, CharSize);
    if (!Delimited && Count == 0 && C == '{') {
      Delimited = true;
      CurPtr += CharSize;
      continue;
    }

    if (Delimited && C == '}') {
      CurPtr += CharSize;
      FoundEndDelimiter = true;
      break;
    }

    unsigned Value = llvm::hexDigitValue(C);
    if (Value == -1U) {
      if (!Delimited)
        break;
      if (Diagnose)
        Diag(SlashLoc, diag::warn_delimited_ucn_incomplete)
            << llvm::StringRef(KindLoc, 1);
      return std::nullopt;
    }

    if (CodePoint & 0xF000'0000) {
      if (Diagnose)
        Diag(KindLoc, diag::err_escape_too_large) << 0;
      return std::nullopt;
    }

    CodePoint <<= 4;
    CodePoint |= Value;
    CurPtr += CharSize;
    Count++;
  }

  if (Count == 0) {
    if (Diagnose)
      Diag(SlashLoc, FoundEndDelimiter ? diag::warn_delimited_ucn_empty
                                       : diag::warn_ucn_escape_no_digits)
          << llvm::StringRef(KindLoc, 1);
    return std::nullopt;
  }

  if (Delimited && Kind == 'U') {
    if (Diagnose)
      Diag(SlashLoc, diag::err_hex_escape_no_digits)
          << llvm::StringRef(KindLoc, 1);
    return std::nullopt;
  }

  if (!Delimited && Count != NumHexDigits) {
    if (Diagnose) {
      Diag(SlashLoc, diag::warn_ucn_escape_incomplete);
      if (Count == 4 && NumHexDigits == 8) {
        CharSourceRange URange = buildCharRange(*this, KindLoc, KindLoc + 1);
        Diag(KindLoc, diag::note_ucn_four_not_eight)
            << FixItHint::CreateReplacement(URange, "u");
      }
    }
    return std::nullopt;
  }

  if (Delimited && PP) {
    Diag(SlashLoc, diag::ext_delimited_escape_sequence) << /*delimited*/ 0 << 0;
  }

  if (Result) {
    Result->setFlag(Token::HasUCN);
    if (CurPtr - StartPtr == (ptrdiff_t)(Count + 1 + (Delimited ? 2 : 0)))
      StartPtr = CurPtr;
    else
      while (StartPtr != CurPtr)
        (void)readChar(StartPtr, *Result);
  } else {
    StartPtr = CurPtr;
  }
  return CodePoint;
}

std::optional<uint32_t> SourceScanner::tryReadNamedUCN(const char *&StartPtr,
                                                       const char *SlashLoc,
                                                       Token *Result) {
  unsigned CharSize;
  bool Diagnose = Result && !isLexingRawMode();

  char C = peekCharSize(StartPtr, CharSize);
  assert(C == 'N' && "expected \\N{...}");

  const char *CurPtr = StartPtr + CharSize;
  const char *KindLoc = &CurPtr[-1];

  C = peekCharSize(CurPtr, CharSize);
  if (C != '{') {
    if (Diagnose)
      Diag(SlashLoc, diag::warn_ucn_escape_incomplete);
    return std::nullopt;
  }
  CurPtr += CharSize;
  const char *StartName = CurPtr;
  bool FoundEndDelimiter = false;
  llvm::SmallVector<char, 30> Buffer;
  while (C) {
    C = peekCharSize(CurPtr, CharSize);
    CurPtr += CharSize;
    if (C == '}') {
      FoundEndDelimiter = true;
      break;
    }

    if (isVerticalWhitespace(C))
      break;
    Buffer.push_back(C);
  }

  if (!FoundEndDelimiter || Buffer.empty()) {
    if (Diagnose)
      Diag(SlashLoc, FoundEndDelimiter ? diag::warn_delimited_ucn_empty
                                       : diag::warn_delimited_ucn_incomplete)
          << llvm::StringRef(KindLoc, 1);
    return std::nullopt;
  }

  llvm::StringRef Name(Buffer.data(), Buffer.size());
  char32_t Match = llvm::sys::unicode::nameToCodepointStrict(Name);
  llvm::sys::unicode::LooseMatchingResult LooseMatch{UINT32_MAX, {}};
  if (Match == UINT32_MAX) {
    LooseMatch = llvm::sys::unicode::nameToCodepointLooseMatching(Name);
    if (Diagnose) {
      Diag(StartName, diag::err_invalid_ucn_name)
          << llvm::StringRef(Buffer.data(), Buffer.size())
          << buildCharRange(*this, StartName, CurPtr - CharSize);
      if (LooseMatch.CodePoint != UINT32_MAX) {
        Diag(StartName, diag::note_invalid_ucn_name_loose_matching)
            << FixItHint::CreateReplacement(
                   buildCharRange(*this, StartName, CurPtr - CharSize),
                   LooseMatch.Name);
      }
    }
  }

  if (Diagnose && Match != UINT32_MAX)
    Diag(SlashLoc, diag::ext_delimited_escape_sequence) << /*named*/ 1 << 0;

  if (LooseMatch.CodePoint != UINT32_MAX && Diagnose)
    Match = LooseMatch.CodePoint;

  if (Result) {
    Result->setFlag(Token::HasUCN);
    if (CurPtr - StartPtr == (ptrdiff_t)(Buffer.size() + 3))
      StartPtr = CurPtr;
    else
      while (StartPtr != CurPtr)
        (void)readChar(StartPtr, *Result);
  } else {
    StartPtr = CurPtr;
  }
  return Match != UINT32_MAX ? std::optional<uint32_t>(Match) : std::nullopt;
}

uint32_t SourceScanner::tryReadUCN(const char *&StartPtr, const char *SlashLoc,
                                   Token *Result) {

  unsigned CharSize;
  std::optional<uint32_t> CodePointOpt;
  char Kind = peekCharSize(StartPtr, CharSize);
  if (Kind == 'u' || Kind == 'U')
    CodePointOpt = tryReadNumericUCN(StartPtr, SlashLoc, Result);
  else if (Kind == 'N')
    CodePointOpt = tryReadNamedUCN(StartPtr, SlashLoc, Result);

  if (!CodePointOpt)
    return 0;

  uint32_t CodePoint = *CodePointOpt;

  if (LangOpts.AsmPreprocessor)
    return CodePoint;

  if (CodePoint < 0xA0) {
    if (Result && PP) {
      if (CodePoint < 0x20 || CodePoint >= 0x7F)
        Diag(BufferPtr, diag::err_ucn_control_character);
      else {
        char C = static_cast<char>(CodePoint);
        Diag(BufferPtr, diag::err_ucn_escape_basic_scs)
            << llvm::StringRef(&C, 1);
      }
    }

    return 0;
  } else if (CodePoint >= 0xD800 && CodePoint <= 0xDFFF) {
    if (Result && PP) {
      Diag(BufferPtr, diag::err_ucn_escape_invalid);
    }
    return 0;
  }

  return CodePoint;
}

__attribute__((cold, noinline)) bool
SourceScanner::checkUnicodeWhitespace(Token &Result, uint32_t C,
                                      const char *CurPtr) {
  if (!isLexingRawMode() && !PP->isPreprocessedOutput() &&
      isUnicodeSpaceChar(C)) {
    Diag(BufferPtr, diag::ext_unicode_whitespace)
        << buildCharRange(*this, BufferPtr, CurPtr);

    Result.setFlag(Token::LeadingSpace);
    return true;
  }
  return false;
}
