#include "neverc/Scan/SourceScanner.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/IncludeGuardOpt.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/Unicode.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
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

using namespace neverc;

#include "SourceScannerSIMDHelpers.inc"

// ===----------------------------------------------------------------------===
// Construction & initialization
// ===----------------------------------------------------------------------===

void SourceScanner::anchor() {}

void SourceScanner::initScanState(const char *BufStart, const char *BufPtr,
                                  const char *BufEnd) {
  BufferStart = BufStart;
  BufferPtr = BufPtr;
  BufferEnd = BufEnd;

  assert(BufEnd[0] == 0 && "Input buffer must be null-terminated");

  if (BufferStart == BufferPtr) {
    llvm::StringRef Buf(BufferStart, BufferEnd - BufferStart);
    size_t BOMLength = llvm::StringSwitch<size_t>(Buf)
                           .StartsWith("\xEF\xBB\xBF", 3)
                           .Default(0);
    BufferPtr += BOMLength;
  }

  Is_PragmaLexer = false;
  CurrentConflictMarkerState = CMK_None;
  IsAtStartOfLine = true;
  IsAtPhysicalStartOfLine = true;
  HasLeadingSpace = false;
  HasLeadingEmptyMacro = false;
  ParsingDirective = false;
  ParsingFilename = false;
  LexingRawMode = false;
  ExtendedTokenMode = 0;

  NewLinePtr = nullptr;
}

SourceScanner::SourceScanner(FileID FID, const llvm::MemoryBufferRef &InputFile,
                             PrepEngine &PP, bool IsFirstIncludeOfFile)
    : LexerCore(&PP, FID),
      FileLoc(PP.getSourceManager().getLocForStartOfFile(FID)),
      LangOpts(PP.getLangOpts()), LineComment(LangOpts.LineComment),
      IsFirstTimeLexingFile(IsFirstIncludeOfFile) {
  initScanState(InputFile.getBufferStart(), InputFile.getBufferStart(),
                InputFile.getBufferEnd());

  resetExtendedTokenMode();
}

SourceScanner::SourceScanner(SourceLocation fileloc,
                             const LangOptions &langOpts, const char *BufStart,
                             const char *BufPtr, const char *BufEnd,
                             bool IsFirstIncludeOfFile)
    : FileLoc(fileloc), LangOpts(langOpts), LineComment(LangOpts.LineComment),
      IsFirstTimeLexingFile(IsFirstIncludeOfFile) {
  initScanState(BufStart, BufPtr, BufEnd);

  LexingRawMode = true;
}

SourceScanner::SourceScanner(FileID FID, const llvm::MemoryBufferRef &FromFile,
                             const SourceManager &SM,
                             const LangOptions &langOpts,
                             bool IsFirstIncludeOfFile)
    : SourceScanner(SM.getLocForStartOfFile(FID), langOpts,
                    FromFile.getBufferStart(), FromFile.getBufferStart(),
                    FromFile.getBufferEnd(), IsFirstIncludeOfFile) {}

void SourceScanner::resetExtendedTokenMode() {
  assert(PP && "Cannot reset token mode without a preprocessor");
  if (LangOpts.TraditionalCPP)
    SetKeepWhitespaceMode(true);
  else
    SetCommentRetentionState(PP->getCommentRetentionState());
}

SourceScanner *SourceScanner::CreatePragmaScanner(
    SourceLocation SpellingLoc, SourceLocation ExpansionLocStart,
    SourceLocation ExpansionLocEnd, unsigned TokLen, PrepEngine &PP) {
  SourceManager &SM = PP.getSourceManager();

  FileID SpellingFID = SM.getFileID(SpellingLoc);
  llvm::MemoryBufferRef InputFile = SM.getBufferOrFake(SpellingFID);
  auto *L = new SourceScanner(SpellingFID, InputFile, PP);

  const char *StrData = SM.getCharacterData(SpellingLoc);
  L->BufferPtr = StrData;
  L->BufferEnd = StrData + TokLen;
  assert(L->BufferEnd[0] == 0 && "Buffer is not nul terminated!");

  L->FileLoc =
      SM.createExpansionLoc(SM.getLocForStartOfFile(SpellingFID),
                            ExpansionLocStart, ExpansionLocEnd, TokLen);

  L->ParsingDirective = true;
  L->Is_PragmaLexer = true;
  return L;
}

void SourceScanner::seek(unsigned Offset, bool IsAtStartOfLine) {
  this->IsAtPhysicalStartOfLine = IsAtStartOfLine;
  this->IsAtStartOfLine = IsAtStartOfLine;
  assert((BufferStart + Offset) <= BufferEnd);
  BufferPtr = BufferStart + Offset;
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
size_t countEscapeCharsInSpan(const char *Data, size_t Len, char Quote) {
  size_t Extra = 0;
  size_t i = 0;

#ifdef __AVX512BW__
  {
    const __m512i VBS = _mm512_set1_epi8('\\');
    const __m512i VQ = _mm512_set1_epi8(Quote);
    const __m512i VNL = _mm512_set1_epi8('\n');
    const __m512i VCR = _mm512_set1_epi8('\r');
    for (; i + 64 <= Len; i += 64) {
      __m512i V = _mm512_loadu_si512(Data + i);
      __mmask64 Hit =
          _mm512_cmpeq_epi8_mask(V, VBS) | _mm512_cmpeq_epi8_mask(V, VQ) |
          _mm512_cmpeq_epi8_mask(V, VNL) | _mm512_cmpeq_epi8_mask(V, VCR);
      Extra += __builtin_popcountll(Hit);
    }
  }
#endif
#if defined(__AVX2__) || defined(__AVX512BW__)
  {
    const __m256i VBS = _mm256_set1_epi8('\\');
    const __m256i VQ = _mm256_set1_epi8(Quote);
    const __m256i VNL = _mm256_set1_epi8('\n');
    const __m256i VCR = _mm256_set1_epi8('\r');
    for (; i + 32 <= Len; i += 32) {
      __m256i V = _mm256_loadu_si256((const __m256i *)(Data + i));
      __m256i Hit = _mm256_or_si256(
          _mm256_or_si256(_mm256_cmpeq_epi8(V, VBS), _mm256_cmpeq_epi8(V, VQ)),
          _mm256_or_si256(_mm256_cmpeq_epi8(V, VNL),
                          _mm256_cmpeq_epi8(V, VCR)));
      Extra += __builtin_popcount(_mm256_movemask_epi8(Hit));
    }
  }
#elif defined(__SSE2__)
  {
    const __m128i VBS = _mm_set1_epi8('\\');
    const __m128i VQ = _mm_set1_epi8(Quote);
    const __m128i VNL = _mm_set1_epi8('\n');
    const __m128i VCR = _mm_set1_epi8('\r');
    for (; i + 16 <= Len; i += 16) {
      __m128i V = _mm_loadu_si128((const __m128i *)(Data + i));
      __m128i Hit = _mm_or_si128(
          _mm_or_si128(_mm_cmpeq_epi8(V, VBS), _mm_cmpeq_epi8(V, VQ)),
          _mm_or_si128(_mm_cmpeq_epi8(V, VNL), _mm_cmpeq_epi8(V, VCR)));
      Extra +=
          __builtin_popcount(static_cast<unsigned>(_mm_movemask_epi8(Hit)));
    }
  }
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t VBS = vdupq_n_u8('\\');
    const uint8x16_t VQ = vdupq_n_u8(static_cast<uint8_t>(Quote));
    const uint8x16_t VNL = vdupq_n_u8('\n');
    const uint8x16_t VCR = vdupq_n_u8('\r');
    uint16x8_t Acc = vdupq_n_u16(0);
    unsigned BatchCount = 0;
    for (; i + 16 <= Len; i += 16) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Data + i));
      uint8x16_t Hit = vorrq_u8(vorrq_u8(vceqq_u8(V, VBS), vceqq_u8(V, VQ)),
                                vorrq_u8(vceqq_u8(V, VNL), vceqq_u8(V, VCR)));
      Acc = vaddq_u16(Acc, vpaddlq_u8(vandq_u8(Hit, vdupq_n_u8(1))));
      if (++BatchCount == 255) {
        Extra += vaddvq_u16(Acc);
        Acc = vdupq_n_u16(0);
        BatchCount = 0;
      }
    }
    Extra += vaddvq_u16(Acc);
  }
#endif

  for (; i < Len; ++i) {
    char C = Data[i];
    if (C == '\\' || C == Quote || C == '\n' || C == '\r')
      ++Extra;
  }
  return Extra;
}
} // namespace

template <typename T> static void insertStringEscapes(T &Str, char Quote) {
  size_t Len = Str.size();
  if (Len == 0)
    return;

  size_t ExtraApprox = countEscapeCharsInSpan(Str.data(), Len, Quote);
  if (ExtraApprox == 0)
    return;

  size_t Extra = 0;
  for (typename T::size_type i = 0, e = Len; i < e; ++i) {
    char C = Str[i];
    unsigned IsBS = static_cast<unsigned>(C == '\\');
    unsigned IsQ = static_cast<unsigned>(C == Quote);
    unsigned IsNL = static_cast<unsigned>(C == '\n');
    unsigned IsCR = static_cast<unsigned>(C == '\r');
    unsigned IsSpecial = IsBS | IsQ;
    unsigned IsLineEnd = IsNL | IsCR;
    if (LLVM_LIKELY(!(IsSpecial | IsLineEnd)))
      continue;
    if (IsSpecial) {
      ++Extra;
    } else {
      bool IsCRLF = (i + 1 < e) && (Str[i + 1] == '\n' || Str[i + 1] == '\r') &&
                    Str[i] != Str[i + 1];
      Extra += !IsCRLF;
      i += IsCRLF;
    }
  }

  if (Extra == 0)
    return;

  typename T::size_type OldSize = Str.size();
  Str.resize(OldSize + Extra);

  typename T::size_type Dst = Str.size();
  typename T::size_type Src = OldSize;
  while (Src > 0) {
    --Src;
    char C = Str[Src];
    if (C == '\\' || C == Quote) {
      Str[--Dst] = C;
      Str[--Dst] = '\\';
    } else if (C == '\n' || C == '\r') {
      if (Src > 0 && (Str[Src - 1] == '\n' || Str[Src - 1] == '\r') &&
          Str[Src - 1] != C) {
        Str[--Dst] = 'n';
        Str[--Dst] = '\\';
        --Src;
      } else {
        Str[--Dst] = 'n';
        Str[--Dst] = '\\';
      }
    } else {
      Str[--Dst] = C;
    }
  }
}

std::string SourceScanner::escapeStringLiteral(llvm::StringRef Str,
                                               bool Charify) {
  std::string Result(Str);
  char Quote = Charify ? '\'' : '"';
  insertStringEscapes(Result, Quote);
  return Result;
}

void SourceScanner::escapeStringLiteral(llvm::SmallVectorImpl<char> &Str) {
  insertStringEscapes(Str, '"');
}

// ===----------------------------------------------------------------------===
// Main lexing loop
// ===----------------------------------------------------------------------===

__attribute__((hot, flatten)) bool SourceScanner::Lex(Token &Result) {
  unsigned short InitFlags = 0;
  if (IsAtStartOfLine) {
    InitFlags = Token::StartOfLine;
    IsAtStartOfLine = false;
  }
  if (HasLeadingSpace) {
    InitFlags |= Token::LeadingSpace;
    HasLeadingSpace = false;
  }
  if (LLVM_UNLIKELY(HasLeadingEmptyMacro)) {
    InitFlags |= Token::LeadingEmptyMacro;
    HasLeadingEmptyMacro = false;
  }
  Result.startTokenFast(InitFlags);

  bool atPhysicalStartOfLine = IsAtPhysicalStartOfLine;
  IsAtPhysicalStartOfLine = false;
  bool produced = scanToken(Result, atPhysicalStartOfLine);
  assert((produced || !isLexingRawMode()) && "Raw lex must succeed");
  return produced;
}

bool SourceScanner::skipNonToken(Token &Result,
                                 bool &TokAtPhysicalStartOfLine) {
  for (;;) {
    const char *CurPtr = BufferPtr;
    if (CurPtr[0] == '/' && !inKeepCommentMode()) {
      char C1 = CurPtr[1];
      if (C1 == '/' && LineComment && !LangOpts.TraditionalCPP) {
        if (LLVM_UNLIKELY(consumeLineComment(Result, CurPtr + 2,
                                             TokAtPhysicalStartOfLine)))
          return true;
        continue;
      }
      if (C1 == '*') {
        if (LLVM_UNLIKELY(consumeBlockComment(Result, CurPtr + 2,
                                              TokAtPhysicalStartOfLine)))
          return true;
        continue;
      }
    }
    if (isHorizontalWhitespace(*CurPtr)) {
      Result.setFlag(Token::LeadingSpace);
      if (LLVM_UNLIKELY(
              consumeSpacing(Result, CurPtr, TokAtPhysicalStartOfLine)))
        return true;
      continue;
    }
    return false;
  }
}

__attribute__((noinline)) int
SourceScanner::dispatchSlowPath(Token &Result, const char *CurPtr, char Char,
                                bool &TokAtPhysicalStartOfLine) {
  unsigned SizeTmp, SizeTmp2;
  tok::TokenKind Kind;

  switch (Char) {
  case 0:
    if (LLVM_UNLIKELY(CurPtr - 1 == BufferEnd))
      return onBufferTerminator(Result, CurPtr - 1) ? 1 : -1;

    if (LLVM_LIKELY(!isLexingRawMode()))
      Diag(CurPtr - 1, diag::null_in_file);
    Result.setFlag(Token::LeadingSpace);
    if (LLVM_UNLIKELY(consumeSpacing(Result, CurPtr, TokAtPhysicalStartOfLine)))
      return 1;
    return 0;

  case 26:
    if (LangOpts.MicrosoftExt) {
      if (!isLexingRawMode())
        Diag(CurPtr - 1, diag::ext_ctrl_z_eof_microsoft);
      return onBufferTerminator(Result, CurPtr - 1) ? 1 : -1;
    }
    Kind = tok::unknown;
    break;

  case '\r':
    if (CurPtr[0] == '\n')
      (void)readChar(CurPtr, Result);
    [[fallthrough]];
  case '\n':
    if (LLVM_UNLIKELY(ParsingDirective)) {
      ParsingDirective = false;
      if (PP)
        resetExtendedTokenMode();

      IsAtStartOfLine = true;
      IsAtPhysicalStartOfLine = true;
      NewLinePtr = CurPtr - 1;

      Kind = tok::eod;
      break;
    }

    Result.clearFlag(Token::LeadingSpace);

    if (LLVM_UNLIKELY(consumeSpacing(Result, CurPtr, TokAtPhysicalStartOfLine)))
      return 1;
    return 0;

  case ' ':
  case '\t':
  case '\f':
  case '\v':
    Result.setFlag(Token::LeadingSpace);
    if (LLVM_UNLIKELY(consumeSpacing(Result, CurPtr, TokAtPhysicalStartOfLine)))
      return 1;

    if (LLVM_UNLIKELY(skipNonToken(Result, TokAtPhysicalStartOfLine)))
      return 1;
    return 0;

  case '$':
    if (LLVM_UNLIKELY(LangOpts.DollarIdents)) {
      if (!isLexingRawMode())
        Diag(CurPtr - 1, diag::ext_dollar_in_identifier);
      MIOpt.readToken();
      return scanIdentRest(Result, CurPtr) ? 1 : -1;
    }

    Kind = tok::unknown;
    break;

  case '?':
    Kind = tok::question;
    break;
  case '[':
    Kind = tok::l_square;
    break;
  case ']':
    Kind = tok::r_square;
    break;
  case '(':
    Kind = tok::l_paren;
    break;
  case ')':
    Kind = tok::r_paren;
    break;
  case '{':
    Kind = tok::l_brace;
    break;
  case '}':
    Kind = tok::r_brace;
    break;
  case '.':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char >= '0' && Char <= '9') {
      MIOpt.readToken();

      return scanNumber(Result, consumeChar(CurPtr, SizeTmp, Result)) ? 1 : -1;
    } else if (Char == '.' && peekCharSize(CurPtr + SizeTmp, SizeTmp2) == '.') {
      Kind = tok::ellipsis;
      CurPtr =
          consumeChar(consumeChar(CurPtr, SizeTmp, Result), SizeTmp2, Result);
    } else {
      Kind = tok::period;
    }
    break;
  case '&':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '&') {
      Kind = tok::ampamp;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '=') {
      Kind = tok::ampequal;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      Kind = tok::amp;
    }
    break;
  case '*':
    if (peekCharSize(CurPtr, SizeTmp) == '=') {
      Kind = tok::starequal;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      Kind = tok::star;
    }
    break;
  case '+':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '+') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::plusplus;
    } else if (Char == '=') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::plusequal;
    } else {
      Kind = tok::plus;
    }
    break;
  case '-':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '-') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::minusminus;
    } else if (Char == '>') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::arrow;
    } else if (Char == '=') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::minusequal;
    } else {
      Kind = tok::minus;
    }
    break;
  case '~':
    Kind = tok::tilde;
    break;
  case '!':
    if (peekCharSize(CurPtr, SizeTmp) == '=') {
      Kind = tok::exclaimequal;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      Kind = tok::exclaim;
    }
    break;
  case '/':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '/') {
      bool TreatAsComment = LineComment && !LangOpts.TraditionalCPP;
      if (!TreatAsComment)
        if (!(PP && PP->isPreprocessedOutput()))
          TreatAsComment = peekCharSize(CurPtr + SizeTmp, SizeTmp2) != '*';

      if (TreatAsComment) {
        if (consumeLineComment(Result, consumeChar(CurPtr, SizeTmp, Result),
                               TokAtPhysicalStartOfLine))
          return 1;

        if (skipNonToken(Result, TokAtPhysicalStartOfLine))
          return 1;
        return 0;
      }
    }

    if (Char == '*') {
      if (consumeBlockComment(Result, consumeChar(CurPtr, SizeTmp, Result),
                              TokAtPhysicalStartOfLine))
        return 1;

      return 0;
    }

    if (Char == '=') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::slashequal;
    } else {
      Kind = tok::slash;
    }
    break;
  case '%':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Kind = tok::percentequal;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else if (LangOpts.Digraphs && Char == '>') {
      Kind = tok::r_brace; // '%>' -> '}'
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else if (LangOpts.Digraphs && Char == ':') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Char = peekCharSize(CurPtr, SizeTmp);
      if (Char == '%' && peekCharSize(CurPtr + SizeTmp, SizeTmp2) == ':') {
        Kind = tok::hashhash; // %:%: -> ##
        CurPtr =
            consumeChar(consumeChar(CurPtr, SizeTmp, Result), SizeTmp2, Result);
      } else if (Char == '@' && LangOpts.MicrosoftExt) {
        CurPtr = consumeChar(CurPtr, SizeTmp, Result);
        if (!isLexingRawMode())
          Diag(BufferPtr, diag::ext_charize_microsoft);
        Kind = tok::hashat;
      } else {
        if (TokAtPhysicalStartOfLine && !LexingRawMode && !Is_PragmaLexer) {
          emitToken(Result, CurPtr, tok::hash);
          PP->DispatchDirective(Result);
          return -1;
        }

        Kind = tok::hash;
      }
    } else {
      Kind = tok::percent;
    }
    break;
  case '<':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (ParsingFilename) {
      return scanAnglePath(Result, CurPtr) ? 1 : -1;
    } else if (Char == '<') {
      char After = peekCharSize(CurPtr + SizeTmp, SizeTmp2);
      if (After == '=') {
        Kind = tok::lesslessequal;
        CurPtr =
            consumeChar(consumeChar(CurPtr, SizeTmp, Result), SizeTmp2, Result);
      } else if (After == '<' && matchConflictBegin(CurPtr - 1)) {
        return 0;
      } else if (After == '<' && matchConflictClose(CurPtr - 1)) {
        return 0;
      } else {
        CurPtr = consumeChar(CurPtr, SizeTmp, Result);
        Kind = tok::lessless;
      }
    } else if (Char == '=') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::lessequal;
    } else if (LangOpts.Digraphs && Char == ':') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::l_square;
    } else if (LangOpts.Digraphs && Char == '%') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::l_brace;
    } else {
      Kind = tok::less;
    }
    break;
  case '>':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '=') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::greaterequal;
    } else if (Char == '>') {
      char After = peekCharSize(CurPtr + SizeTmp, SizeTmp2);
      if (After == '=') {
        CurPtr =
            consumeChar(consumeChar(CurPtr, SizeTmp, Result), SizeTmp2, Result);
        Kind = tok::greatergreaterequal;
      } else if (After == '>' && matchConflictBegin(CurPtr - 1)) {
        return 0;
      } else if (After == '>' && matchConflictClose(CurPtr - 1)) {
        return 0;
      } else {
        CurPtr = consumeChar(CurPtr, SizeTmp, Result);
        Kind = tok::greatergreater;
      }
    } else {
      Kind = tok::greater;
    }
    break;
  case '^':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '=') {
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
      Kind = tok::caretequal;
    } else {
      Kind = tok::caret;
    }
    break;
  case '|':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '=') {
      Kind = tok::pipeequal;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '|') {
      if (CurPtr[1] == '|' && matchConflictClose(CurPtr - 1))
        return 0;
      Kind = tok::pipepipe;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      Kind = tok::pipe;
    }
    break;
  case ':':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (LangOpts.Digraphs && Char == '>') {
      Kind = tok::r_square; // ':>' -> ']'
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == ':') {
      Kind = tok::coloncolon;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      Kind = tok::colon;
    }
    break;
  case ';':
    Kind = tok::semi;
    break;
  case '=':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '=') {
      if (CurPtr[1] == '=' && matchConflictClose(CurPtr - 1))
        return 0;
      Kind = tok::equalequal;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      Kind = tok::equal;
    }
    break;
  case ',':
    Kind = tok::comma;
    break;
  case '#':
    Char = peekCharSize(CurPtr, SizeTmp);
    if (Char == '#') {
      Kind = tok::hashhash;
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else if (Char == '@' && LangOpts.MicrosoftExt) {
      Kind = tok::hashat;
      if (!isLexingRawMode())
        Diag(BufferPtr, diag::ext_charize_microsoft);
      CurPtr = consumeChar(CurPtr, SizeTmp, Result);
    } else {
      if (TokAtPhysicalStartOfLine && !LexingRawMode && !Is_PragmaLexer) {
        emitToken(Result, CurPtr, tok::hash);
        PP->DispatchDirective(Result);
        return -1;
      }

      Kind = tok::hash;
    }
    break;

  case '@':
    Kind = tok::unknown;
    break;

  case '\\':
    if (!LangOpts.AsmPreprocessor) {
      if (uint32_t CodePoint = tryReadUCN(CurPtr, BufferPtr, &Result)) {
        if (checkUnicodeWhitespace(Result, CodePoint, CurPtr)) {
          if (consumeSpacing(Result, CurPtr, TokAtPhysicalStartOfLine))
            return 1;

          return 0;
        }

        return processUnicodeIdentStart(Result, CodePoint, CurPtr) ? 1 : -1;
      }
    }

    Kind = tok::unknown;
    break;

  default: {
    if (isASCII(Char)) {
      Kind = tok::unknown;
      break;
    }

    llvm::UTF32 CodePoint;
    --CurPtr;
    llvm::ConversionResult Status = llvm::convertUTF8Sequence(
        (const llvm::UTF8 **)&CurPtr, (const llvm::UTF8 *)BufferEnd, &CodePoint,
        llvm::strictConversion);
    if (Status == llvm::conversionOK) {
      if (checkUnicodeWhitespace(Result, CodePoint, CurPtr)) {
        if (consumeSpacing(Result, CurPtr, TokAtPhysicalStartOfLine))
          return 1;

        return 0;
      }
      return processUnicodeIdentStart(Result, CodePoint, CurPtr) ? 1 : -1;
    }

    if (isLexingRawMode() || ParsingDirective || PP->isPreprocessedOutput()) {
      ++CurPtr;
      Kind = tok::unknown;
      break;
    }

    Diag(CurPtr, diag::err_invalid_utf8);
    BufferPtr = CurPtr + 1;
    return 0;
  }
  }

  MIOpt.readToken();
  emitToken(Result, CurPtr, Kind);
  return 1;
}

__attribute__((hot, flatten)) bool
SourceScanner::scanToken(Token &Result, bool TokAtPhysicalStartOfLine) {
  for (;;) {
    assert(!Result.needsCleaning() && "Result needs cleaning");
    assert(!Result.hasPtrData() && "Result has not been reset");

    const char *CurPtr = BufferPtr;

    if (isHorizontalWhitespace(*CurPtr)) {
      ++CurPtr;
      if (LLVM_UNLIKELY(isHorizontalWhitespace(*CurPtr)))
        CurPtr = skipWhitespaceRun(CurPtr, BufferEnd);

      if (LLVM_UNLIKELY(isKeepWhitespaceMode())) {
        emitToken(Result, CurPtr, tok::unknown);
        return true;
      }

      BufferPtr = CurPtr;
      Result.setFlag(Token::LeadingSpace);
    }

    __builtin_prefetch(CurPtr + 64, 0, 3);

    char Char = readChar(CurPtr, Result);

    if (LLVM_LIKELY(!isVerticalWhitespace(Char)))
      NewLinePtr = nullptr;

    if (LLVM_LIKELY(isAsciiIdentifierStart(Char))) {
      MIOpt.readToken();

      if (LLVM_UNLIKELY(Char == 'u' || Char == 'U' || Char == 'L')) {
        unsigned SizeTmp, SizeTmp2;

        if (Char == 'u' && LangOpts.C11) {
          char Next = peekCharSize(CurPtr, SizeTmp);

          if (Next == '"')
            return scanStringLit(Result, consumeChar(CurPtr, SizeTmp, Result),
                                 tok::utf16_string_literal);

          if (Next == '\'')
            return scanCharLit(Result, consumeChar(CurPtr, SizeTmp, Result),
                               tok::utf16_char_constant);

          if (Next == '8') {
            char Next2 = peekCharSize(CurPtr + SizeTmp, SizeTmp2);

            if (Next2 == '"')
              return scanStringLit(
                  Result,
                  consumeChar(consumeChar(CurPtr, SizeTmp, Result), SizeTmp2,
                              Result),
                  tok::utf8_string_literal);
            if (Next2 == '\'' && LangOpts.C23)
              return scanCharLit(
                  Result,
                  consumeChar(consumeChar(CurPtr, SizeTmp, Result), SizeTmp2,
                              Result),
                  tok::utf8_char_constant);
          }
        } else if (Char == 'U' && LangOpts.C11) {
          char Next = peekCharSize(CurPtr, SizeTmp);

          if (Next == '"')
            return scanStringLit(Result, consumeChar(CurPtr, SizeTmp, Result),
                                 tok::utf32_string_literal);

          if (Next == '\'')
            return scanCharLit(Result, consumeChar(CurPtr, SizeTmp, Result),
                               tok::utf32_char_constant);
        } else {
          char Next = peekCharSize(CurPtr, SizeTmp);

          if (Next == '"')
            return scanStringLit(Result, consumeChar(CurPtr, SizeTmp, Result),
                                 tok::wide_string_literal);

          if (Next == '\'')
            return scanCharLit(Result, consumeChar(CurPtr, SizeTmp, Result),
                               tok::wide_char_constant);
        }
      }

      return scanIdentRest(Result, CurPtr);
    }

    if (Char >= '0' && Char <= '9') {
      MIOpt.readToken();
      return scanNumber(Result, CurPtr);
    }

    if (Char == '"') {
      MIOpt.readToken();
      return scanStringLit(Result, CurPtr,
                           ParsingFilename ? tok::header_name
                                           : tok::string_literal);
    }

    if (Char == '\'') {
      MIOpt.readToken();
      return scanCharLit(Result, CurPtr, tok::char_constant);
    }

    if (LLVM_LIKELY(isTrivialCharUnit(*CurPtr))) {
      const char Next = *CurPtr;
      switch (Char) {
      case ';':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::semi);
        return true;
      case ',':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::comma);
        return true;
      case '(':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::l_paren);
        return true;
      case ')':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::r_paren);
        return true;
      case '{':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::l_brace);
        return true;
      case '}':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::r_brace);
        return true;
      case '[':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::l_square);
        return true;
      case ']':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::r_square);
        return true;
      case '~':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::tilde);
        return true;
      case '?':
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::question);
        return true;

      case '=':
        if (LLVM_UNLIKELY(Next == '=' &&
                          !(CurPtr[1] == '=' && CurrentConflictMarkerState))) {
          MIOpt.readToken();
          emitToken(Result, CurPtr + 1, tok::equalequal);
          return true;
        }
        if (LLVM_LIKELY(Next != '=')) {
          MIOpt.readToken();
          emitToken(Result, CurPtr, tok::equal);
          return true;
        }
        break;
      case '*':
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::starequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::star);
        return true;
      case '!':
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::exclaimequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::exclaim);
        return true;
      case '^':
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::caretequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::caret);
        return true;
      case '+':
        MIOpt.readToken();
        if (Next == '+') {
          emitToken(Result, CurPtr + 1, tok::plusplus);
          return true;
        }
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::plusequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::plus);
        return true;
      case '-':
        MIOpt.readToken();
        if (Next == '>') {
          emitToken(Result, CurPtr + 1, tok::arrow);
          return true;
        }
        if (Next == '-') {
          emitToken(Result, CurPtr + 1, tok::minusminus);
          return true;
        }
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::minusequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::minus);
        return true;
      case '&':
        if (Next == '&') {
          MIOpt.readToken();
          emitToken(Result, CurPtr + 1, tok::ampamp);
          return true;
        }
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::ampequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::amp);
        return true;
      case '|':
        if (Next == '|') {
          if (LLVM_UNLIKELY(CurPtr[1] == '|' && CurrentConflictMarkerState))
            break;
          MIOpt.readToken();
          emitToken(Result, CurPtr + 1, tok::pipepipe);
          return true;
        }
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::pipeequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::pipe);
        return true;

      case '#':
        if (LLVM_UNLIKELY(Next == '#')) {
          MIOpt.readToken();
          emitToken(Result, CurPtr + 1, tok::hashhash);
          return true;
        }
        if (LLVM_UNLIKELY((Next == '@' && LangOpts.MicrosoftExt) ||
                          (TokAtPhysicalStartOfLine && !LexingRawMode &&
                           !Is_PragmaLexer)))
          break;
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::hash);
        return true;

      case '<':
        if (LLVM_UNLIKELY(ParsingFilename || Next == '<' ||
                          (LangOpts.Digraphs && (Next == ':' || Next == '%'))))
          break;
        MIOpt.readToken();
        if (Next == '=') {
          emitToken(Result, CurPtr + 1, tok::lessequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::less);
        return true;
      case '>':
        if (LLVM_UNLIKELY(Next == '>'))
          break;
        MIOpt.readToken();
        if (Next == '=') {
          emitToken(Result, CurPtr + 1, tok::greaterequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::greater);
        return true;
      case '.':
        if (LLVM_UNLIKELY(Next == '.' || (Next >= '0' && Next <= '9')))
          break;
        MIOpt.readToken();
        emitToken(Result, CurPtr, tok::period);
        return true;
      case ':':
        if (LLVM_UNLIKELY(LangOpts.Digraphs && Next == '>'))
          break;
        MIOpt.readToken();
        if (Next == ':') {
          emitToken(Result, CurPtr + 1, tok::coloncolon);
          return true;
        }
        emitToken(Result, CurPtr, tok::colon);
        return true;
      case '/':
        if (Next == '/') {
          if (LLVM_LIKELY(LineComment && !LangOpts.TraditionalCPP)) {
            Result.clearFlag(Token::NeedsCleaning);
            if (LLVM_UNLIKELY(consumeLineComment(Result, CurPtr + 1,
                                                 TokAtPhysicalStartOfLine)))
              return true;
            Result.clearFlag(Token::NeedsCleaning);
            continue;
          }
          break;
        }
        if (Next == '*') {
          Result.clearFlag(Token::NeedsCleaning);
          if (LLVM_UNLIKELY(consumeBlockComment(Result, CurPtr + 1,
                                                TokAtPhysicalStartOfLine)))
            return true;
          Result.clearFlag(Token::NeedsCleaning);
          continue;
        }
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::slashequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::slash);
        return true;
      case '%':
        if (LLVM_UNLIKELY(LangOpts.Digraphs && (Next == '>' || Next == ':')))
          break;
        MIOpt.readToken();
        if (LLVM_UNLIKELY(Next == '=')) {
          emitToken(Result, CurPtr + 1, tok::percentequal);
          return true;
        }
        emitToken(Result, CurPtr, tok::percent);
        return true;

      default:
        break;
      }
    }

    int action =
        dispatchSlowPath(Result, CurPtr, Char, TokAtPhysicalStartOfLine);
    if (LLVM_LIKELY(action > 0))
      return true;
    if (LLVM_UNLIKELY(action < 0))
      return false;
    Result.clearFlag(Token::NeedsCleaning);
  }
}
