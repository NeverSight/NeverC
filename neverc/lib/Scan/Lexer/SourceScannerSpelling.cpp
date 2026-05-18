#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

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

#if defined(__aarch64__) && defined(__ARM_NEON)
namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
inline unsigned neonFirstSetByte(uint8x16_t Mask) {
  uint64x2_t AsU64 = vreinterpretq_u64_u8(Mask);
  uint64_t Lo = vgetq_lane_u64(AsU64, 0);
  uint64_t Hi = vgetq_lane_u64(AsU64, 1);
  if (Lo)
    return __builtin_ctzll(Lo) >> 3;
  return 8 + (__builtin_ctzll(Hi) >> 3);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline bool neonAnySet(uint8x16_t V) { return vmaxvq_u8(V) != 0; }
} // namespace
#endif

using namespace neverc;

// ===----------------------------------------------------------------------===
// Token spelling & measurement
// ===----------------------------------------------------------------------===

namespace {
size_t resolveTokenChars(const Token &Tok, const char *BufPtr,
                         const LangOptions &LangOpts, char *Spelling) {
  assert(Tok.needsCleaning() && "resolveTokenChars called on simple token");

  size_t Length = 0;
  const char *BufEnd = BufPtr + Tok.getLength();

  if (tok::isStringLiteral(Tok.getKind())) {
    while (BufPtr < BufEnd) {
      auto CharAndSize = SourceScanner::getCharAndSizeNoWarn(BufPtr, LangOpts);
      Spelling[Length++] = CharAndSize.Char;
      BufPtr += CharAndSize.Size;

      if (Spelling[Length - 1] == '"')
        break;
    }

    // Raw body: skip trigraph/splice decode.
    if (Length >= 2 && Spelling[Length - 2] == 'R' &&
        Spelling[Length - 1] == '"') {
      const char *RawEnd = BufEnd;
      do
        --RawEnd;
      while (*RawEnd != '"');
      size_t RawLength = RawEnd - BufPtr + 1;

      memcpy(Spelling + Length, BufPtr, RawLength);
      Length += RawLength;
      BufPtr += RawLength;
    }
  }

  while (BufPtr < BufEnd) {
    const char *DirtyPos = BufPtr;
#if defined(__AVX2__) || defined(__AVX512BW__)
    {
      const __m256i VBS = _mm256_set1_epi8('\\');
      const __m256i VQM = _mm256_set1_epi8('?');
      while (DirtyPos + 32 <= BufEnd) {
        __m256i V = _mm256_loadu_si256((const __m256i *)DirtyPos);
        unsigned Hit =
            static_cast<unsigned>(_mm256_movemask_epi8(_mm256_or_si256(
                _mm256_cmpeq_epi8(V, VBS), _mm256_cmpeq_epi8(V, VQM))));
        if (Hit != 0) {
          DirtyPos += llvm::countr_zero(Hit);
          goto found_dirty;
        }
        DirtyPos += 32;
      }
    }
#elif defined(__SSE2__)
    {
      const __m128i VBS = _mm_set1_epi8('\\');
      const __m128i VQM = _mm_set1_epi8('?');
      while (DirtyPos + 16 <= BufEnd) {
        __m128i V = _mm_loadu_si128((const __m128i *)DirtyPos);
        unsigned Hit = static_cast<unsigned>(_mm_movemask_epi8(
            _mm_or_si128(_mm_cmpeq_epi8(V, VBS), _mm_cmpeq_epi8(V, VQM))));
        if (Hit != 0) {
          DirtyPos += llvm::countr_zero(Hit);
          goto found_dirty;
        }
        DirtyPos += 16;
      }
    }
#elif defined(__aarch64__) && defined(__ARM_NEON)
    {
      const uint8x16_t VBS = vdupq_n_u8('\\');
      const uint8x16_t VQM = vdupq_n_u8('?');
      while (DirtyPos + 16 <= BufEnd) {
        uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(DirtyPos));
        uint8x16_t Hit = vorrq_u8(vceqq_u8(V, VBS), vceqq_u8(V, VQM));
        if (neonAnySet(Hit)) {
          DirtyPos += neonFirstSetByte(Hit);
          goto found_dirty;
        }
        DirtyPos += 16;
      }
    }
#endif
    while (DirtyPos < BufEnd && *DirtyPos != '\\' && *DirtyPos != '?')
      ++DirtyPos;

  found_dirty:
    if (DirtyPos > BufEnd)
      DirtyPos = BufEnd;

    size_t RunLen = DirtyPos - BufPtr;
    if (RunLen > 0) {
      memcpy(Spelling + Length, BufPtr, RunLen);
      Length += RunLen;
      BufPtr = DirtyPos;
    }

    if (BufPtr >= BufEnd)
      break;

    auto CharAndSize = SourceScanner::getCharAndSizeNoWarn(BufPtr, LangOpts);
    Spelling[Length++] = CharAndSize.Char;
    BufPtr += CharAndSize.Size;
  }

  assert(Length < Tok.getLength() &&
         "NeedsCleaning flag set on token that didn't need cleaning!");
  return Length;
}
} // namespace

llvm::StringRef SourceScanner::getSpelling(SourceLocation loc,
                                           llvm::SmallVectorImpl<char> &buffer,
                                           const SourceManager &SM,
                                           const LangOptions &options,
                                           bool *invalid) {
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);

  bool invalidTemp = false;
  llvm::StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp) {
    if (invalid)
      *invalid = true;
    return {};
  }

  const char *tokenBegin = file.data() + locInfo.second;

  SourceScanner lexer(SM.getLocForStartOfFile(locInfo.first), options,
                      file.begin(), tokenBegin, file.end());
  Token token;
  lexer.LexFromRawLexer(token);

  unsigned length = token.getLength();

  if (LLVM_LIKELY(!token.needsCleaning()))
    return llvm::StringRef(tokenBegin, length);

  buffer.resize(length);
  buffer.resize(resolveTokenChars(token, tokenBegin, options, buffer.data()));
  return llvm::StringRef(buffer.data(), buffer.size());
}

std::string SourceScanner::getSpelling(const Token &Tok,
                                       const SourceManager &SourceMgr,
                                       const LangOptions &LangOpts,
                                       bool *Invalid) {
  assert((int)Tok.getLength() >= 0 && "Token character range is bogus!");

  bool CharDataInvalid = false;
  const char *TokStart =
      SourceMgr.getCharacterData(Tok.getLocation(), &CharDataInvalid);
  if (Invalid)
    *Invalid = CharDataInvalid;
  if (CharDataInvalid)
    return {};

  if (LLVM_LIKELY(!Tok.needsCleaning()))
    return std::string(TokStart, TokStart + Tok.getLength());

  std::string Result;
  Result.resize(Tok.getLength());
  Result.resize(resolveTokenChars(Tok, TokStart, LangOpts, &*Result.begin()));
  return Result;
}

unsigned SourceScanner::getSpelling(const Token &Tok, const char *&Buffer,
                                    const SourceManager &SourceMgr,
                                    const LangOptions &LangOpts,
                                    bool *Invalid) {
  assert((int)Tok.getLength() >= 0 && "Token character range is bogus!");

  const char *TokStart = nullptr;
  if (Tok.is(tok::raw_identifier))
    TokStart = Tok.getRawIdentifier().data();
  else if (!Tok.hasUCN()) {
    if (const IdentifierInfo *II = Tok.getIdentifierInfo()) {
      Buffer = II->getNameStart();
      return II->getLength();
    }
  }

  if (Tok.isLiteral())
    TokStart = Tok.getLiteralData();

  if (!TokStart) {
    bool CharDataInvalid = false;
    TokStart = SourceMgr.getCharacterData(Tok.getLocation(), &CharDataInvalid);
    if (Invalid)
      *Invalid = CharDataInvalid;
    if (CharDataInvalid) {
      Buffer = "";
      return 0;
    }
  }

  if (LLVM_LIKELY(!Tok.needsCleaning())) {
    Buffer = TokStart;
    return Tok.getLength();
  }

  return resolveTokenChars(Tok, TokStart, LangOpts, const_cast<char *>(Buffer));
}

unsigned SourceScanner::measureTokenLength(SourceLocation Loc,
                                           const SourceManager &SM,
                                           const LangOptions &LangOpts) {
  Token TheTok;
  if (scanRawToken(Loc, TheTok, SM, LangOpts))
    return 0;
  return TheTok.getLength();
}

bool SourceScanner::scanRawToken(SourceLocation Loc, Token &Result,
                                 const SourceManager &SM,
                                 const LangOptions &LangOpts,
                                 bool IgnoreWhiteSpace) {
  Loc = SM.getExpansionLoc(Loc);
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  bool Invalid = false;
  llvm::StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
  if (Invalid)
    return true;

  const char *StrData = Buffer.data() + LocInfo.second;

  if (!IgnoreWhiteSpace && isWhitespace(StrData[0]))
    return true;

  SourceScanner TheLexer(SM.getLocForStartOfFile(LocInfo.first), LangOpts,
                         Buffer.begin(), StrData, Buffer.end());
  TheLexer.SetCommentRetentionState(true);
  TheLexer.LexFromRawLexer(Result);
  return false;
}

namespace {
const char *findPhysicalLineBegin(llvm::StringRef Buffer, unsigned Offset) {
  const char *BufStart = Buffer.data();
  if (Offset >= Buffer.size())
    return nullptr;

  const char *LexStart = BufStart + Offset;

#ifdef __AVX512BW__
  {
    const __m512i VNL = _mm512_set1_epi8('\n');
    const __m512i VCR = _mm512_set1_epi8('\r');
    while (LexStart - BufStart >= 64) {
      LexStart -= 64;
      __m512i V = _mm512_loadu_si512(LexStart);
      __mmask64 Hits =
          _mm512_cmpeq_epi8_mask(V, VNL) | _mm512_cmpeq_epi8_mask(V, VCR);
      if (Hits != 0) {
        unsigned LastBit = 63 - __builtin_clzll(Hits);
        const char *Candidate = LexStart + LastBit;
        if (!SourceScanner::isEscapedNewline(BufStart, Candidate))
          return Candidate + 1;
        LexStart = Candidate;
        goto scalar_fallback_512;
      }
    }
  }
scalar_fallback_512:
#endif
#if defined(__AVX2__) || defined(__AVX512BW__)
{
  const __m256i VNL = _mm256_set1_epi8('\n');
  const __m256i VCR = _mm256_set1_epi8('\r');
  while (LexStart - BufStart >= 32) {
    LexStart -= 32;
    __m256i V = _mm256_loadu_si256((const __m256i *)LexStart);
    unsigned Hits = static_cast<unsigned>(_mm256_movemask_epi8(
        _mm256_or_si256(_mm256_cmpeq_epi8(V, VNL), _mm256_cmpeq_epi8(V, VCR))));
    if (Hits != 0) {
      unsigned LastBit = 31 - __builtin_clz(Hits);
      const char *Candidate = LexStart + LastBit;
      if (!SourceScanner::isEscapedNewline(BufStart, Candidate))
        return Candidate + 1;
      LexStart = Candidate;
      goto scalar_fallback;
    }
  }
}
scalar_fallback:
#elif defined(__SSE2__)
  {
    const __m128i VNL = _mm_set1_epi8('\n');
    const __m128i VCR = _mm_set1_epi8('\r');
    while (LexStart - BufStart >= 16) {
      LexStart -= 16;
      __m128i V = _mm_loadu_si128((const __m128i *)LexStart);
      unsigned Hits = static_cast<unsigned>(_mm_movemask_epi8(
          _mm_or_si128(_mm_cmpeq_epi8(V, VNL), _mm_cmpeq_epi8(V, VCR))));
      if (Hits != 0) {
        unsigned LastBit = 31 - __builtin_clz(Hits);
        const char *Candidate = LexStart + LastBit;
        if (!SourceScanner::isEscapedNewline(BufStart, Candidate))
          return Candidate + 1;
        LexStart = Candidate;
        goto scalar_path;
      }
    }
  }
scalar_path:
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t VNL = vdupq_n_u8('\n');
    const uint8x16_t VCR = vdupq_n_u8('\r');
    while (LexStart - BufStart >= 16) {
      LexStart -= 16;
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(LexStart));
      uint8x16_t Hits = vorrq_u8(vceqq_u8(V, VNL), vceqq_u8(V, VCR));
      if (neonAnySet(Hits)) {
        uint64x2_t As64 = vreinterpretq_u64_u8(Hits);
        uint64_t Hi = vgetq_lane_u64(As64, 1);
        uint64_t Lo = vgetq_lane_u64(As64, 0);
        unsigned LastBit;
        if (Hi)
          LastBit = 8 + (63 - __builtin_clzll(Hi)) / 8;
        else
          LastBit = (63 - __builtin_clzll(Lo)) / 8;
        const char *Candidate = LexStart + LastBit;
        if (!SourceScanner::isEscapedNewline(BufStart, Candidate))
          return Candidate + 1;
        LexStart = Candidate;
        break;
      }
    }
  }
#endif

  for (; LexStart != BufStart; --LexStart) {
    if (isVerticalWhitespace(LexStart[0]) &&
        !SourceScanner::isEscapedNewline(BufStart, LexStart)) {
      ++LexStart;
      break;
    }
  }
  return LexStart;
}

SourceLocation backtrackToTokenOrigin(SourceLocation Loc,
                                      const SourceManager &SM,
                                      const LangOptions &LangOpts) {
  assert(Loc.isFileID());
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  if (LocInfo.first.isInvalid())
    return Loc;

  bool Invalid = false;
  llvm::StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
  if (Invalid)
    return Loc;

  const char *StrData = Buffer.data() + LocInfo.second;
  const char *LexStart = findPhysicalLineBegin(Buffer, LocInfo.second);
  if (!LexStart || LexStart == StrData)
    return Loc;

  SourceLocation LexerStartLoc = Loc.getLocWithOffset(-LocInfo.second);
  SourceScanner TheLexer(LexerStartLoc, LangOpts, Buffer.data(), LexStart,
                         Buffer.end());
  TheLexer.SetCommentRetentionState(true);

  Token TheTok;
  do {
    TheLexer.LexFromRawLexer(TheTok);

    if (TheLexer.getBufferLocation() > StrData) {
      if (TheLexer.getBufferLocation() - TheTok.getLength() <= StrData)
        return TheTok.getLocation();
      break;
    }
  } while (TheTok.getKind() != tok::eof);

  return Loc;
}
} // namespace

SourceLocation SourceScanner::locateTokenOrigin(SourceLocation Loc,
                                                const SourceManager &SM,
                                                const LangOptions &LangOpts) {
  if (Loc.isFileID())
    return backtrackToTokenOrigin(Loc, SM, LangOpts);

  if (!SM.isMacroArgExpansion(Loc))
    return Loc;

  SourceLocation FileLoc = SM.getSpellingLoc(Loc);
  SourceLocation BeginFileLoc = backtrackToTokenOrigin(FileLoc, SM, LangOpts);
  std::pair<FileID, unsigned> FileLocInfo = SM.getDecomposedLoc(FileLoc);
  std::pair<FileID, unsigned> BeginFileLocInfo =
      SM.getDecomposedLoc(BeginFileLoc);
  assert(FileLocInfo.first == BeginFileLocInfo.first &&
         FileLocInfo.second >= BeginFileLocInfo.second);
  return Loc.getLocWithOffset(BeginFileLocInfo.second - FileLocInfo.second);
}

unsigned SourceScanner::getTokenPrefixLength(SourceLocation TokStart,
                                             unsigned CharNo,
                                             const SourceManager &SM,
                                             const LangOptions &LangOpts) {
  bool Invalid = false;
  const char *TokPtr = SM.getCharacterData(TokStart, &Invalid);

  if (Invalid || (CharNo == 0 && SourceScanner::isTrivialCharUnit(*TokPtr)))
    return 0;

  unsigned PhysOffset = 0;

  while (SourceScanner::isTrivialCharUnit(*TokPtr)) {
    if (CharNo == 0)
      return PhysOffset;
    ++TokPtr;
    --CharNo;
    ++PhysOffset;
  }

  for (; CharNo; --CharNo) {
    auto CharAndSize = SourceScanner::getCharAndSizeNoWarn(TokPtr, LangOpts);
    TokPtr += CharAndSize.Size;
    PhysOffset += CharAndSize.Size;
  }

  if (!SourceScanner::isTrivialCharUnit(*TokPtr))
    PhysOffset += SourceScanner::skipSplices(TokPtr) - TokPtr;

  return PhysOffset;
}

SourceLocation SourceScanner::getLocForEndOfToken(SourceLocation Loc,
                                                  unsigned Offset,
                                                  const SourceManager &SM,
                                                  const LangOptions &LangOpts) {
  if (Loc.isInvalid())
    return {};

  if (Loc.isMacroID()) {
    if (Offset > 0 || !isAtEndOfMacroExpansion(Loc, SM, LangOpts, &Loc))
      return {}; // Points inside the macro expansion.
  }

  unsigned Len = SourceScanner::measureTokenLength(Loc, SM, LangOpts);
  if (Len > Offset)
    Len = Len - Offset;
  else
    return Loc;

  return Loc.getLocWithOffset(Len);
}

bool SourceScanner::isAtStartOfMacroExpansion(SourceLocation loc,
                                              const SourceManager &SM,
                                              const LangOptions &LangOpts,
                                              SourceLocation *MacroBegin) {
  assert(loc.isValid() && loc.isMacroID() && "Expected a valid macro loc");

  SourceLocation expansionLoc;
  if (!SM.isAtStartOfImmediateMacroExpansion(loc, &expansionLoc))
    return false;

  if (expansionLoc.isFileID()) {
    if (MacroBegin)
      *MacroBegin = expansionLoc;
    return true;
  }

  return isAtStartOfMacroExpansion(expansionLoc, SM, LangOpts, MacroBegin);
}

bool SourceScanner::isAtEndOfMacroExpansion(SourceLocation loc,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts,
                                            SourceLocation *MacroEnd) {
  assert(loc.isValid() && loc.isMacroID() && "Expected a valid macro loc");

  SourceLocation spellLoc = SM.getSpellingLoc(loc);
  unsigned tokLen = measureTokenLength(spellLoc, SM, LangOpts);
  if (tokLen == 0)
    return false;

  SourceLocation afterLoc = loc.getLocWithOffset(tokLen);
  SourceLocation expansionLoc;
  if (!SM.isAtEndOfImmediateMacroExpansion(afterLoc, &expansionLoc))
    return false;

  if (expansionLoc.isFileID()) {
    if (MacroEnd)
      *MacroEnd = expansionLoc;
    return true;
  }

  return isAtEndOfMacroExpansion(expansionLoc, SM, LangOpts, MacroEnd);
}

namespace {
CharSourceRange buildFileCharRange(CharSourceRange Range,
                                   const SourceManager &SM,
                                   const LangOptions &LangOpts) {
  SourceLocation Begin = Range.getBegin();
  SourceLocation End = Range.getEnd();
  assert(Begin.isFileID() && End.isFileID());
  if (Range.isTokenRange()) {
    End = SourceScanner::getLocForEndOfToken(End, 0, SM, LangOpts);
    if (End.isInvalid())
      return {};
  }

  FileID FID;
  unsigned BeginOffs;
  std::tie(FID, BeginOffs) = SM.getDecomposedLoc(Begin);
  if (FID.isInvalid())
    return {};

  unsigned EndOffs;
  if (!SM.isInFileID(End, FID, &EndOffs) || BeginOffs > EndOffs)
    return {};

  return CharSourceRange::getCharRange(Begin, End);
}
} // namespace

namespace {
bool isExpansionTokenRange(const SourceLocation Loc, const SourceManager &SM) {
  return SM.getSLocEntry(SM.getFileID(Loc))
      .getExpansion()
      .isExpansionTokenRange();
}
} // namespace

CharSourceRange SourceScanner::makeFileCharRange(CharSourceRange Range,
                                                 const SourceManager &SM,
                                                 const LangOptions &LangOpts) {
  SourceLocation Begin = Range.getBegin();
  SourceLocation End = Range.getEnd();
  if (Begin.isInvalid() || End.isInvalid())
    return {};

  if (Begin.isFileID() && End.isFileID())
    return buildFileCharRange(Range, SM, LangOpts);

  if (Begin.isMacroID() && End.isFileID()) {
    if (!isAtStartOfMacroExpansion(Begin, SM, LangOpts, &Begin))
      return {};
    Range.setBegin(Begin);
    return buildFileCharRange(Range, SM, LangOpts);
  }

  if (Begin.isFileID() && End.isMacroID()) {
    if (Range.isTokenRange()) {
      if (!isAtEndOfMacroExpansion(End, SM, LangOpts, &End))
        return {};
      Range.setTokenRange(isExpansionTokenRange(Range.getEnd(), SM));
    } else if (!isAtStartOfMacroExpansion(End, SM, LangOpts, &End))
      return {};
    Range.setEnd(End);
    return buildFileCharRange(Range, SM, LangOpts);
  }

  assert(Begin.isMacroID() && End.isMacroID());
  SourceLocation MacroBegin, MacroEnd;
  if (isAtStartOfMacroExpansion(Begin, SM, LangOpts, &MacroBegin) &&
      ((Range.isTokenRange() &&
        isAtEndOfMacroExpansion(End, SM, LangOpts, &MacroEnd)) ||
       (Range.isCharRange() &&
        isAtStartOfMacroExpansion(End, SM, LangOpts, &MacroEnd)))) {
    Range.setBegin(MacroBegin);
    Range.setEnd(MacroEnd);
    if (Range.isTokenRange())
      Range.setTokenRange(isExpansionTokenRange(End, SM));
    return buildFileCharRange(Range, SM, LangOpts);
  }

  bool Invalid = false;
  const SrcMgr::SLocEntry &BeginEntry =
      SM.getSLocEntry(SM.getFileID(Begin), &Invalid);
  if (Invalid)
    return {};

  if (BeginEntry.getExpansion().isMacroArgExpansion()) {
    const SrcMgr::SLocEntry &EndEntry =
        SM.getSLocEntry(SM.getFileID(End), &Invalid);
    if (Invalid)
      return {};

    if (EndEntry.getExpansion().isMacroArgExpansion() &&
        BeginEntry.getExpansion().getExpansionLocStart() ==
            EndEntry.getExpansion().getExpansionLocStart()) {
      Range.setBegin(SM.getImmediateSpellingLoc(Begin));
      Range.setEnd(SM.getImmediateSpellingLoc(End));
      return makeFileCharRange(Range, SM, LangOpts);
    }
  }

  return {};
}

llvm::StringRef SourceScanner::getSourceText(CharSourceRange Range,
                                             const SourceManager &SM,
                                             const LangOptions &LangOpts,
                                             bool *Invalid) {
  Range = makeFileCharRange(Range, SM, LangOpts);
  if (Range.isInvalid()) {
    if (Invalid)
      *Invalid = true;
    return {};
  }

  std::pair<FileID, unsigned> beginInfo = SM.getDecomposedLoc(Range.getBegin());
  if (beginInfo.first.isInvalid()) {
    if (Invalid)
      *Invalid = true;
    return {};
  }

  unsigned EndOffs;
  if (!SM.isInFileID(Range.getEnd(), beginInfo.first, &EndOffs) ||
      beginInfo.second > EndOffs) {
    if (Invalid)
      *Invalid = true;
    return {};
  }

  bool invalidTemp = false;
  llvm::StringRef file = SM.getBufferData(beginInfo.first, &invalidTemp);
  if (invalidTemp) {
    if (Invalid)
      *Invalid = true;
    return {};
  }

  if (Invalid)
    *Invalid = false;
  return file.substr(beginInfo.second, EndOffs - beginInfo.second);
}

llvm::StringRef SourceScanner::getImmediateMacroName(
    SourceLocation Loc, const SourceManager &SM, const LangOptions &LangOpts) {
  assert(Loc.isMacroID() && "Only reasonable to call this on macros");

  while (true) {
    FileID FID = SM.getFileID(Loc);
    const SrcMgr::SLocEntry *E = &SM.getSLocEntry(FID);
    const SrcMgr::ExpansionInfo &Expansion = E->getExpansion();
    Loc = Expansion.getExpansionLocStart();
    if (!Expansion.isMacroArgExpansion())
      break;

    Loc = SM.getImmediateExpansionRange(Loc).getBegin();
    SourceLocation SpellLoc = Expansion.getSpellingLoc();
    if (SpellLoc.isFileID())
      break;

    FileID MacroFID = SM.getFileID(Loc);
    if (SM.isInFileID(SpellLoc, MacroFID))
      break;

    Loc = SpellLoc;
  }

  Loc = SM.getSpellingLoc(Loc);
  std::pair<FileID, unsigned> ExpansionInfo = SM.getDecomposedLoc(Loc);
  unsigned MacroTokenLength =
      SourceScanner::measureTokenLength(Loc, SM, LangOpts);
  llvm::StringRef ExpansionBuffer = SM.getBufferData(ExpansionInfo.first);
  return ExpansionBuffer.substr(ExpansionInfo.second, MacroTokenLength);
}

llvm::StringRef SourceScanner::getImmediateMacroNameForDiagnostics(
    SourceLocation Loc, const SourceManager &SM, const LangOptions &LangOpts) {
  assert(Loc.isMacroID() && "Only reasonable to call this on macros");
  while (SM.isMacroArgExpansion(Loc))
    Loc = SM.getImmediateExpansionRange(Loc).getBegin();

  SourceLocation SpellLoc = SM.getSpellingLoc(Loc);
  if (!SpellLoc.isFileID() || SM.isWrittenInScratchSpace(SpellLoc))
    return {};

  Loc = SM.getSpellingLoc(SM.getImmediateExpansionRange(Loc).getBegin());
  std::pair<FileID, unsigned> ExpansionInfo = SM.getDecomposedLoc(Loc);
  unsigned MacroTokenLength =
      SourceScanner::measureTokenLength(Loc, SM, LangOpts);
  llvm::StringRef ExpansionBuffer = SM.getBufferData(ExpansionInfo.first);
  return ExpansionBuffer.substr(ExpansionInfo.second, MacroTokenLength);
}

bool SourceScanner::isAsciiIdentifierContinueChar(char c,
                                                  const LangOptions &LangOpts) {
  return isAsciiIdentifierContinue(c, LangOpts.DollarIdents);
}

bool SourceScanner::isEscapedNewline(const char *BufferStart, const char *Str) {
  assert(isVerticalWhitespace(Str[0]));
  if (Str - 1 < BufferStart)
    return false;

  if ((Str[0] == '\n' && Str[-1] == '\r') ||
      (Str[0] == '\r' && Str[-1] == '\n')) {
    if (Str - 2 < BufferStart)
      return false;
    --Str;
  }
  --Str;

  while (Str > BufferStart && isHorizontalWhitespace(*Str))
    --Str;

  return *Str == '\\';
}

llvm::StringRef SourceScanner::getIndentationForLine(SourceLocation Loc,
                                                     const SourceManager &SM) {
  if (Loc.isInvalid() || Loc.isMacroID())
    return {};
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  if (LocInfo.first.isInvalid())
    return {};
  bool Invalid = false;
  llvm::StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
  if (Invalid)
    return {};
  const char *Line = findPhysicalLineBegin(Buffer, LocInfo.second);
  if (!Line)
    return {};
  llvm::StringRef Rest = Buffer.substr(Line - Buffer.data());
  size_t NumWhitespaceChars = Rest.find_first_not_of(" \t");
  return NumWhitespaceChars == llvm::StringRef::npos
             ? ""
             : Rest.take_front(NumWhitespaceChars);
}

// Resolve location inside a _Pragma mapped buffer (cold path).
namespace {
LLVM_ATTRIBUTE_NOINLINE SourceLocation computeExpansionLoc(
    PrepEngine &PP, SourceLocation FileLoc, unsigned CharNo, unsigned TokLen);
} // namespace
namespace {
SourceLocation computeExpansionLoc(PrepEngine &PP, SourceLocation FileLoc,
                                   unsigned CharNo, unsigned TokLen) {
  assert(FileLoc.isMacroID() && "Must be a macro expansion");

  SourceManager &SM = PP.getSourceManager();
  SourceLocation SpellingLoc = SM.getSpellingLoc(FileLoc);
  SpellingLoc = SpellingLoc.getLocWithOffset(CharNo);
  CharSourceRange II = SM.getImmediateExpansionRange(FileLoc);
  return SM.createExpansionLoc(SpellingLoc, II.getBegin(), II.getEnd(), TokLen);
}
} // namespace

SourceLocation SourceScanner::getSourceLocation(const char *Loc,
                                                unsigned TokLen) const {
  assert(Loc >= BufferStart && Loc <= BufferEnd &&
         "Location out of range for this buffer!");

  unsigned CharNo = Loc - BufferStart;
  if (FileLoc.isFileID())
    return FileLoc.getLocWithOffset(CharNo);

  assert(PP && "This doesn't work on raw lexers");
  return computeExpansionLoc(*PP, FileLoc, CharNo, TokLen);
}

DiagnosticBuilder SourceScanner::Diag(const char *Loc, unsigned DiagID) const {
  return PP->Diag(getSourceLocation(Loc), DiagID);
}
