#include "neverc/Foundation/Core/DirectoryEntry.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/VarArgExpansion.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <utility>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;
// ===----------------------------------------------------------------------===
// Directive utilities & macro validation
// ===----------------------------------------------------------------------===

MacroRecord *PrepEngine::AllocMacroRecord(SourceLocation L) {
  static_assert(std::is_trivially_destructible_v<MacroRecord>, "");
  return new (BP) MacroRecord(L);
}

DefMacroDirective *PrepEngine::AllocDefDirective(MacroRecord *MI,
                                                 SourceLocation Loc) {
  return new (BP) DefMacroDirective(MI, Loc);
}

UndefMacroDirective *PrepEngine::AllocUndefDirective(SourceLocation UndefLoc) {
  return new (BP) UndefMacroDirective(UndefLoc);
}

SourceRange PrepEngine::DiscardDirectiveLine() {
  Token Tmp;
  SourceRange Res;

  LexWithoutExpansion(Tmp);
  Res.setBegin(Tmp.getLocation());
  while (Tmp.isNot(tok::eod)) {
    assert(Tmp.isNot(tok::eof) && "EOF seen while discarding directive tokens");
    LexWithoutExpansion(Tmp);
  }
  Res.setEnd(Tmp.getLocation());
  return Res;
}

namespace {
enum ReservedIdDiag {
  RID_NoWarn,       //> Not a reserved identifier
  RID_KeywordDef,   //> Macro hides keyword, enabled by default
  RID_ReservedMacro //> #define or #undef of reserved id, disabled by default
};

enum ConditionalBranchDiag { CBD_Elif, CBD_Elifdef, CBD_Elifndef };
bool isFeatureTestMacro(llvm::StringRef MacroName) {
  if (MacroName.size() < 9 || MacroName[0] != '_')
    return false;
  static constexpr llvm::StringLiteral Sorted[] = {
      "_ATFILE_SOURCE",
      "_BSD_SOURCE",
      "_CRT_NONSTDC_NO_WARNINGS",
      "_CRT_SECURE_NO_WARNINGS",
      "_FILE_OFFSET_BITS",
      "_FORTIFY_SOURCE",
      "_GNU_SOURCE",
      "_ISOC11_SOURCE",
      "_ISOC95_SOURCE",
      "_ISOC99_SOURCE",
      "_LARGEFILE64_SOURCE",
      "_POSIX_C_SOURCE",
      "_REENTRANT",
      "_SVID_SOURCE",
      "_THREAD_SAFE",
      "_XOPEN_SOURCE",
      "_XOPEN_SOURCE_EXTENDED",
      "__STDC_FORMAT_MACROS",
  };
  auto It = std::lower_bound(
      std::begin(Sorted), std::end(Sorted), MacroName,
      [](llvm::StringLiteral A, llvm::StringRef B) { return A < B; });
  return It != std::end(Sorted) && *It == MacroName;
}

bool isIntrinsicLangMacro(const SourceManager &SourceMgr, const MacroRecord *MI,
                          const llvm::StringRef MacroName) {
  if (MI->isBuiltinMacro())
    return true;
  if (!SourceMgr.isWrittenInBuiltinFile(MI->getDefinitionLoc()))
    return false;
  return MacroName.starts_with("__STDC");
}

ReservedIdDiag categorizeMacroDefinition(PrepEngine &PP, IdentifierInfo *II) {
  const LangOptions &Lang = PP.getLangOpts();
  llvm::StringRef Text = II->getName();
  if (isReservedInAllContexts(II->isReserved(Lang)))
    return isFeatureTestMacro(Text) ? RID_NoWarn : RID_ReservedMacro;
  if (II->isKeyword(Lang))
    return RID_KeywordDef;
  return RID_NoWarn;
}

ReservedIdDiag classifyUndefTarget(PrepEngine &PP, IdentifierInfo *II) {
  const LangOptions &Lang = PP.getLangOpts();
  if (isReservedInAllContexts(II->isReserved(Lang)))
    return RID_ReservedMacro;
  return RID_NoWarn;
}

bool detectIncludeCaseDiff(llvm::StringRef Include) {
  if (::llvm::sys::path::begin(Include)->equals_insensitive("boost"))
    return true;

  static constexpr size_t MaxStdHeaderNameLen = 18u;
  if (Include.size() > MaxStdHeaderNameLen)
    return false;

  llvm::SmallString<32> LowerInclude{Include};
  char *Data = LowerInclude.data();
  size_t Len = LowerInclude.size();
  size_t I = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
  if (Len >= 16) {
    uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Data));
    uint8x16_t Hi = vcgtq_u8(V, vdupq_n_u8(0x7f));
    if (vmaxvq_u8(Hi) != 0)
      return false;
    uint8x16_t IsUpper =
        vandq_u8(vcgeq_u8(V, vdupq_n_u8('A')), vcleq_u8(V, vdupq_n_u8('Z')));
    uint8x16_t ToLower = vandq_u8(IsUpper, vdupq_n_u8('A' ^ 'a'));
    V = veorq_u8(V, ToLower);
    uint8x16_t IsBSlash = vceqq_u8(V, vdupq_n_u8('\\'));
    V = vbslq_u8(IsBSlash, vdupq_n_u8('/'), V);
    vst1q_u8(reinterpret_cast<uint8_t *>(Data), V);
    I = 16;
  }
#elif defined(__AVX2__)
  if (Len >= 16) {
    __m128i V = _mm_loadu_si128((const __m128i *)Data);
    __m128i HiBits = _mm_and_si128(V, _mm_set1_epi8((char)0x80));
    if (_mm_movemask_epi8(HiBits) != 0)
      return false;
    const __m128i CaseBit = _mm_set1_epi8('A' ^ 'a');
    __m128i Biased = _mm_xor_si128(V, _mm_set1_epi8((char)0x80u));
    __m128i IsUpper = _mm_and_si128(
        _mm_cmpgt_epi8(Biased, _mm_set1_epi8((char)(('A' - 1) ^ 0x80u))),
        _mm_cmpgt_epi8(_mm_set1_epi8((char)(('Z' + 1) ^ 0x80u)), Biased));
    V = _mm_xor_si128(V, _mm_and_si128(IsUpper, CaseBit));
    __m128i IsBSlash = _mm_cmpeq_epi8(V, _mm_set1_epi8('\\'));
    V = _mm_blendv_epi8(V, _mm_set1_epi8('/'), IsBSlash);
    _mm_storeu_si128((__m128i *)Data, V);
    I = 16;
  }
#elif defined(__SSE2__)
  if (Len >= 16) {
    __m128i V = _mm_loadu_si128((const __m128i *)Data);
    __m128i HiBits = _mm_and_si128(V, _mm_set1_epi8((char)0x80));
    if (_mm_movemask_epi8(HiBits) != 0)
      return false;
    __m128i IsUpper = _mm_and_si128(_mm_cmpgt_epi8(V, _mm_set1_epi8('A' - 1)),
                                    _mm_cmpgt_epi8(_mm_set1_epi8('Z' + 1), V));
    __m128i ToLower = _mm_and_si128(IsUpper, _mm_set1_epi8('A' ^ 'a'));
    V = _mm_xor_si128(V, ToLower);
    __m128i IsBSlash = _mm_cmpeq_epi8(V, _mm_set1_epi8('\\'));
    V = _mm_or_si128(_mm_andnot_si128(IsBSlash, V),
                     _mm_and_si128(IsBSlash, _mm_set1_epi8('/')));
    _mm_storeu_si128((__m128i *)Data, V);
    I = 16;
  }
#endif
  for (; I < Len; ++I) {
    char &Ch = Data[I];
    if (static_cast<unsigned char>(Ch) > 0x7f)
      return false;
    Ch |= ('A' ^ 'a') * (Ch >= 'A' && Ch <= 'Z');
    if (::llvm::sys::path::is_separator(Ch))
      Ch = '/';
  }

  static const llvm::StringSet<> KnownHeaders = [] {
    llvm::StringSet<> S;
    for (llvm::StringRef H : {
             "assert.h",
             "complex.h",
             "ctype.h",
             "errno.h",
             "fenv.h",
             "float.h",
             "inttypes.h",
             "iso646.h",
             "limits.h",
             "locale.h",
             "math.h",
             "setjmp.h",
             "signal.h",
             "stdalign.h",
             "stdarg.h",
             "stdatomic.h",
             "stdbool.h",
             "stdckdint.h",
             "stddef.h",
             "stdint.h",
             "stdio.h",
             "stdlib.h",
             "stdnoreturn.h",
             "string.h",
             "tgmath.h",
             "threads.h",
             "time.h",
             "uchar.h",
             "wchar.h",
             "wctype.h",
             "cassert",
             "ccomplex",
             "cctype",
             "cerrno",
             "cfenv",
             "cfloat",
             "cinttypes",
             "ciso646",
             "climits",
             "clocale",
             "cmath",
             "csetjmp",
             "csignal",
             "cstdalign",
             "cstdarg",
             "cstdbool",
             "cstddef",
             "cstdint",
             "cstdio",
             "cstdlib",
             "cstring",
             "ctgmath",
             "ctime",
             "cuchar",
             "cwchar",
             "cwctype",
             "algorithm",
             "fstream",
             "list",
             "regex",
             "thread",
             "array",
             "functional",
             "locale",
             "scoped_allocator",
             "tuple",
             "atomic",
             "future",
             "map",
             "set",
             "type_traits",
             "bitset",
             "initializer_list",
             "memory",
             "shared_mutex",
             "typeindex",
             "chrono",
             "iomanip",
             "mutex",
             "sstream",
             "typeinfo",
             "codecvt",
             "ios",
             "new",
             "stack",
             "unordered_map",
             "complex",
             "iosfwd",
             "numeric",
             "stdexcept",
             "unordered_set",
             "condition_variable",
             "iostream",
             "ostream",
             "streambuf",
             "utility",
             "deque",
             "istream",
             "queue",
             "string",
             "valarray",
             "exception",
             "iterator",
             "random",
             "strstream",
             "vector",
             "forward_list",
             "limits",
             "ratio",
             "system_error",
             "aio.h",
             "arpa/inet.h",
             "cpio.h",
             "dirent.h",
             "dlfcn.h",
             "fcntl.h",
             "fmtmsg.h",
             "fnmatch.h",
             "ftw.h",
             "glob.h",
             "grp.h",
             "iconv.h",
             "langinfo.h",
             "libgen.h",
             "monetary.h",
             "mqueue.h",
             "ndbm.h",
             "net/if.h",
             "netdb.h",
             "netinet/in.h",
             "netinet/tcp.h",
             "nl_types.h",
             "poll.h",
             "pthread.h",
             "pwd.h",
             "regex.h",
             "sched.h",
             "search.h",
             "semaphore.h",
             "spawn.h",
             "strings.h",
             "stropts.h",
             "sys/ipc.h",
             "sys/mman.h",
             "sys/msg.h",
             "sys/resource.h",
             "sys/select.h",
             "sys/sem.h",
             "sys/shm.h",
             "sys/socket.h",
             "sys/stat.h",
             "sys/statvfs.h",
             "sys/time.h",
             "sys/times.h",
             "sys/types.h",
             "sys/uio.h",
             "sys/un.h",
             "sys/utsname.h",
             "sys/wait.h",
             "syslog.h",
             "tar.h",
             "termios.h",
             "trace.h",
             "ulimit.h",
             "unistd.h",
             "utime.h",
             "utmpx.h",
             "wordexp.h",
         })
      S.insert(H);
    return S;
  }();
  return KnownHeaders.count(LowerInclude);
}

std::optional<llvm::StringRef>
findSimilarStr(llvm::StringRef LHS,
               llvm::ArrayRef<llvm::StringRef> Candidates) {
  for (llvm::StringRef C : Candidates) {
    if (LHS.equals_insensitive(C)) {
      return C;
    }
  }

  size_t Length = LHS.size();
  size_t MaxDist = Length < 3 ? Length - 1 : Length / 3;

  std::optional<std::pair<llvm::StringRef, size_t>> SimilarStr;
  for (llvm::StringRef C : Candidates) {
    size_t CurDist = LHS.edit_distance(C, true);
    if (CurDist <= MaxDist) {
      if (!SimilarStr || CurDist < SimilarStr->second)
        SimilarStr = {C, CurDist};
    }
  }

  return SimilarStr ? std::optional(SimilarStr->first) : std::nullopt;
}
} // namespace

bool PrepEngine::validateMacroName(Token &MacroNameTok, MacroUse isDefineUndef,
                                   bool *ShadowFlag) {
  if (MacroNameTok.is(tok::eod))
    return Diag(MacroNameTok, diag::err_pp_missing_macro_name);

  IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
  if (!II)
    return true;

  if ((isDefineUndef != MU_Other) && II->getPPKeywordID() == tok::pp_defined)
    return Diag(MacroNameTok, diag::err_defined_macro_name);

  SourceLocation MacroNameLoc = MacroNameTok.getLocation();
  if (ShadowFlag)
    *ShadowFlag = false;
  if (!SourceMgr.isInSystemHeader(MacroNameLoc) &&
      (SourceMgr.getBufferName(MacroNameLoc) != "<built-in>")) {
    ReservedIdDiag D = RID_NoWarn;
    if (isDefineUndef == MU_Define) {
      D = categorizeMacroDefinition(*this, II);
    } else if (isDefineUndef == MU_Undef)
      D = classifyUndefTarget(*this, II);
    if (D == RID_KeywordDef) {
      if (ShadowFlag)
        *ShadowFlag = true;
    }
    if (D == RID_ReservedMacro)
      Diag(MacroNameTok, diag::warn_pp_macro_is_reserved_id);
  }

  return false;
}

void PrepEngine::ValidateMacroNameTok(Token &MacroNameTok,
                                      MacroUse isDefineUndef,
                                      bool *ShadowFlag) {
  LexWithoutExpansion(MacroNameTok);

  if (!validateMacroName(MacroNameTok, isDefineUndef, ShadowFlag))
    return;

  if (MacroNameTok.isNot(tok::eod)) {
    MacroNameTok.setKind(tok::eod);
    DiscardDirectiveLine();
  }
}

SourceLocation PrepEngine::VerifyDirectiveEnd(const char *DirType,
                                              bool EnableMacros) {
  Token Tmp;
  if (EnableMacros)
    Lex(Tmp);
  else
    LexWithoutExpansion(Tmp);

  while (Tmp.is(tok::comment))
    LexWithoutExpansion(Tmp);

  if (Tmp.is(tok::eod))
    return Tmp.getLocation();

  FixItHint Hint;
  if ((LangOpts.GNUMode || LangOpts.C99) && !CurExpansionLexer)
    Hint = FixItHint::CreateInsertion(Tmp.getLocation(), "//");
  Diag(Tmp, diag::ext_pp_extra_tokens_at_eol) << DirType << Hint;
  return DiscardDirectiveLine().getEnd();
}

void PrepEngine::SuggestDirectiveTypo(const Token &Tok,
                                      llvm::StringRef Directive) const {
  if (getLangOpts().AsmPreprocessor)
    return;

  llvm::SmallVector<llvm::StringRef, 8> Candidates = {
      tok::getPPKeywordSpelling(tok::pp_if),
      tok::getPPKeywordSpelling(tok::pp_ifdef),
      tok::getPPKeywordSpelling(tok::pp_ifndef),
      tok::getPPKeywordSpelling(tok::pp_elif),
      tok::getPPKeywordSpelling(tok::pp_else),
      tok::getPPKeywordSpelling(tok::pp_endif)};
  if (LangOpts.C23)
    Candidates.insert(Candidates.end(),
                      {tok::getPPKeywordSpelling(tok::pp_elifdef),
                       tok::getPPKeywordSpelling(tok::pp_elifndef)});

  if (std::optional<llvm::StringRef> Sugg =
          findSimilarStr(Directive, Candidates)) {
    // Directive cannot be coming from macro.
    assert(Tok.getLocation().isFileID());
    CharSourceRange DirectiveRange = CharSourceRange::getCharRange(
        Tok.getLocation(),
        Tok.getLocation().getLocWithOffset(Directive.size()));
    llvm::StringRef SuggValue = *Sugg;

    auto Hint = FixItHint::CreateReplacement(DirectiveRange, SuggValue);
    Diag(Tok, diag::warn_pp_invalid_directive) << 1 << SuggValue << Hint;
  }
}

void PrepEngine::SkipExcludedBlock(SourceLocation HashTokenLoc,
                                   SourceLocation IfTokenLoc,
                                   bool FoundNonSkipPortion, bool FoundElse,
                                   SourceLocation ElseLoc) {
  assert(!SkippingExcludedConditionalBlock &&
         "calling SkipExcludedBlock recursively");
  llvm::SaveAndRestore SARSkipping(SkippingExcludedConditionalBlock, true);

  ++NumSkipped;
  assert(!CurExpansionLexer &&
         "Conditional PP block cannot appear in a macro!");
  assert(CurPPLexer && "Conditional PP block must be in a file!");
  assert(CurLexer && "Conditional PP block but no current lexer set!");

  CurPPLexer->pushConditionalLevel(IfTokenLoc, /*isSkipping*/ false,
                                   FoundNonSkipPortion, FoundElse);

  CurPPLexer->LexingRawMode = true;
  Token Tok;
  SourceLocation endLoc;
  const llvm::StringRef PPIf = tok::getPPKeywordSpelling(tok::pp_if);
  const llvm::StringRef PPIfdef = tok::getPPKeywordSpelling(tok::pp_ifdef);
  const llvm::StringRef PPIfndef = tok::getPPKeywordSpelling(tok::pp_ifndef);
  const llvm::StringRef PPEndif = tok::getPPKeywordSpelling(tok::pp_endif);
  const llvm::StringRef PPElse = tok::getPPKeywordSpelling(tok::pp_else);
  const llvm::StringRef PPElif = tok::getPPKeywordSpelling(tok::pp_elif);
  const llvm::StringRef PPElifdef = tok::getPPKeywordSpelling(tok::pp_elifdef);
  const llvm::StringRef PPElifndef =
      tok::getPPKeywordSpelling(tok::pp_elifndef);

  struct SkippingRangeStateTy {
    PrepEngine &PP;

    const char *BeginPtr = nullptr;
    unsigned *SkipRangePtr = nullptr;

    SkippingRangeStateTy(PrepEngine &PP) : PP(PP) {}

    void beginLexPass() {
      if (BeginPtr)
        return;
      BeginPtr = PP.CurLexer->getBufferLocation();
      SkipRangePtr = &PP.RecordedSkippedRanges[BeginPtr];
      if (*SkipRangePtr) {
        PP.CurLexer->seek(PP.CurLexer->getCurrentBufferOffset() + *SkipRangePtr,
                          /*IsAtStartOfLine*/ true);
      }
    }

    void endLexPass(const char *Hashptr) {
      assert(BeginPtr && "endLexPass without beginLexPass");
      if (!*SkipRangePtr) {
        *SkipRangePtr = Hashptr - BeginPtr;
      }
      assert(*SkipRangePtr == Hashptr - BeginPtr);
      BeginPtr = nullptr;
      SkipRangePtr = nullptr;
    }
  } SkippingRangeState(*this);

  while (true) {
    SkippingRangeState.beginLexPass();
    while (true) {
      CurLexer->Lex(Tok);

      // HandleBufferEnd diagnoses unterminated conditionals.
      if (Tok.is(tok::eof))
        break;

      // If this token is not a preprocessor directive, just skip it.
      if (Tok.isNot(tok::hash) || !Tok.isAtStartOfLine())
        continue;

      break;
    }
    if (Tok.is(tok::eof))
      break;

    // We just parsed a # character at the start of a line, so we're in
    // directive mode.  Tell the lexer this so any newlines we see will be
    // converted into an EOD token (this terminates the macro).
    CurPPLexer->ParsingDirective = true;
    if (CurLexer)
      CurLexer->SetKeepWhitespaceMode(false);

    assert(Tok.is(tok::hash));
    const char *Hashptr = CurLexer->getBufferLocation() - Tok.getLength();
    assert(CurLexer->getSourceLocation(Hashptr) == Tok.getLocation());

    // Read the next token, the directive flavor.
    LexWithoutExpansion(Tok);

    // If this isn't an identifier directive (e.g. is "# 1\n" or "#\n", or
    // something bogus), skip it.
    if (Tok.isNot(tok::raw_identifier)) {
      CurPPLexer->ParsingDirective = false;
      // Restore comment saving mode.
      if (CurLexer)
        CurLexer->resetExtendedTokenMode();
      continue;
    }

    // If the first letter isn't i or e, it isn't intesting to us.  We know that
    // this is safe in the face of spelling differences, because there is no way
    // to spell an i/e in a strange way that is another letter.  Skipping this
    // allows us to avoid looking up the identifier info for #define/#undef and
    // other common directives.
    llvm::StringRef RI = Tok.getRawIdentifier();

    const char FirstChar = RI[0];
    if (LLVM_LIKELY(static_cast<unsigned>(FirstChar - 'a') < 26u &&
                    FirstChar != 'i' && FirstChar != 'e')) {
      CurPPLexer->ParsingDirective = false;
      if (CurLexer)
        CurLexer->resetExtendedTokenMode();
      continue;
    }

    // Get the identifier name without trigraphs or embedded newlines.  Note
    // that we can't use Tok.getIdentifierInfo() because its lookup is disabled
    // when skipping.
    char DirectiveBuf[20];
    llvm::StringRef Directive;
    if (!Tok.needsCleaning() && RI.size() < 20) {
      Directive = RI;
    } else {
      std::string DirectiveStr = getSpelling(Tok);
      size_t IdLen = DirectiveStr.size();
      if (IdLen >= 20) {
        CurPPLexer->ParsingDirective = false;
        // Restore comment saving mode.
        if (CurLexer)
          CurLexer->resetExtendedTokenMode();
        continue;
      }
      memcpy(DirectiveBuf, &DirectiveStr[0], IdLen);
      Directive = llvm::StringRef(DirectiveBuf, IdLen);
    }

    if (Directive == PPIf || Directive == PPIfdef || Directive == PPIfndef) {
      // We know the entire #if/#ifdef/#ifndef block will be skipped, don't
      // bother parsing the condition.
      DiscardDirectiveLine();
      CurPPLexer->pushConditionalLevel(Tok.getLocation(),
                                       /*wasskipping*/ true,
                                       /*foundnonskip*/ false,
                                       /*foundelse*/ false);
    } else if (Directive.starts_with(PPIf)) {
      SuggestDirectiveTypo(Tok, Directive);
    } else if (Directive[0] == 'e') {
      if (Directive == PPEndif) {
        PPConditionalInfo CondInfo;
        CondInfo.WasSkipping = true; // Silence bogus warning.
        bool InCond = CurPPLexer->popConditionalLevel(CondInfo);
        (void)InCond; // Silence warning in no-asserts mode.
        assert(!InCond && "Can't be skipping if not in a conditional!");

        // If we popped the outermost skipping block, we're done skipping!
        if (!CondInfo.WasSkipping) {
          SkippingRangeState.endLexPass(Hashptr);
          // Restore the value of LexingRawMode so that trailing comments
          // are handled correctly, if we've reached the outermost block.
          CurPPLexer->LexingRawMode = false;
          endLoc = VerifyDirectiveEnd(PPEndif.data());
          CurPPLexer->LexingRawMode = true;
          if (Callbacks)
            Callbacks->Endif(Tok.getLocation(), CondInfo.IfLoc);
          break;
        } else {
          DiscardDirectiveLine();
        }
      } else if (Directive == PPElse) {
        // #else directive in a skipping conditional.  If not in some other
        // skipping conditional, and if #else hasn't already been seen, enter it
        // as a non-skipping conditional.
        PPConditionalInfo &CondInfo = CurPPLexer->peekConditionalLevel();

        if (!CondInfo.WasSkipping)
          SkippingRangeState.endLexPass(Hashptr);

        // If this is a #else with a #else before it, report the error.
        if (CondInfo.FoundElse)
          Diag(Tok, diag::pp_err_else_after_else);

        // Note that we've seen a #else in this conditional.
        CondInfo.FoundElse = true;

        // If the conditional is at the top level, and the #if block wasn't
        // entered, enter the #else block now.
        if (!CondInfo.WasSkipping && !CondInfo.FoundNonSkip) {
          CondInfo.FoundNonSkip = true;
          // Restore the value of LexingRawMode so that trailing comments
          // are handled correctly.
          CurPPLexer->LexingRawMode = false;
          endLoc = VerifyDirectiveEnd(PPElse.data());
          CurPPLexer->LexingRawMode = true;
          if (Callbacks)
            Callbacks->Else(Tok.getLocation(), CondInfo.IfLoc);
          break;
        } else {
          DiscardDirectiveLine();
        }
      } else if (Directive == PPElif) {
        PPConditionalInfo &CondInfo = CurPPLexer->peekConditionalLevel();

        if (!CondInfo.WasSkipping)
          SkippingRangeState.endLexPass(Hashptr);

        // If this is a #elif with a #else before it, report the error.
        if (CondInfo.FoundElse)
          Diag(Tok, diag::pp_err_elif_after_else) << CBD_Elif;

        // If this is in a skipping block or if we're already handled this #if
        // block, don't bother parsing the condition.
        if (CondInfo.WasSkipping || CondInfo.FoundNonSkip) {
          DiscardDirectiveLine();
        } else {
          // Restore the value of LexingRawMode so that identifiers are
          // looked up, etc, inside the #elif expression.
          assert(CurPPLexer->LexingRawMode && "We have to be skipping here!");
          CurPPLexer->LexingRawMode = false;
          IdentifierInfo *IfNDefMacro = nullptr;
          CondEvalOutcome DER = EvalCondExpr(IfNDefMacro);
          if (!CurPPLexer)
            return;
          const bool CondValue = DER.Conditional;
          CurPPLexer->LexingRawMode = true;
          if (Callbacks) {
            Callbacks->Elif(
                Tok.getLocation(), DER.ExprRange,
                (CondValue ? PrepObserver::CVK_True : PrepObserver::CVK_False),
                CondInfo.IfLoc);
          }
          // If this condition is true, enter it!
          if (CondValue) {
            CondInfo.FoundNonSkip = true;
            break;
          }
        }
      } else if (Directive == PPElifdef || Directive == PPElifndef) {
        bool IsElifDef = Directive == PPElifdef;
        PPConditionalInfo &CondInfo = CurPPLexer->peekConditionalLevel();
        Token DirectiveToken = Tok;

        if (!CondInfo.WasSkipping)
          SkippingRangeState.endLexPass(Hashptr);

        // Warn if using `#elifdef` & `#elifndef` in not C23 mode even
        // if this branch is in a skipping block.
        unsigned DiagID = LangOpts.C23 ? diag::warn_c23_compat_pp_directive
                                       : diag::ext_c23_pp_directive;
        Diag(Tok, DiagID) << (IsElifDef ? CBD_Elifdef : CBD_Elifndef);

        // If this is a #elif with a #else before it, report the error.
        if (CondInfo.FoundElse)
          Diag(Tok, diag::pp_err_elif_after_else)
              << (IsElifDef ? CBD_Elifdef : CBD_Elifndef);

        // If this is in a skipping block or if we're already handled this #if
        // block, don't bother parsing the condition.
        if (CondInfo.WasSkipping || CondInfo.FoundNonSkip) {
          DiscardDirectiveLine();
        } else {
          // Restore the value of LexingRawMode so that identifiers are
          // looked up, etc, inside the #elif[n]def expression.
          assert(CurPPLexer->LexingRawMode && "We have to be skipping here!");
          CurPPLexer->LexingRawMode = false;
          Token MacroNameTok;
          ValidateMacroNameTok(MacroNameTok);
          CurPPLexer->LexingRawMode = true;

          // If the macro name token is tok::eod, there was an error that was
          // already reported.
          if (MacroNameTok.is(tok::eod)) {
            // Skip code until we get to #endif.  This helps with recovery by
            // not emitting an error when the #endif is reached.
            continue;
          }

          checkMacroWarnings(MacroNameTok);

          VerifyDirectiveEnd(IsElifDef
                                 ? tok::getPPKeywordSpelling(tok::pp_elifdef)
                                 : tok::getPPKeywordSpelling(tok::pp_elifndef));

          IdentifierInfo *MII = MacroNameTok.getIdentifierInfo();
          auto MD = getMacroDefinition(MII);
          MacroRecord *MI = MD.getMacroRecord();

          if (Callbacks) {
            if (IsElifDef) {
              Callbacks->Elifdef(DirectiveToken.getLocation(), MacroNameTok,
                                 MD);
            } else {
              Callbacks->Elifndef(DirectiveToken.getLocation(), MacroNameTok,
                                  MD);
            }
          }
          // If this condition is true, enter it!
          if (static_cast<bool>(MI) == IsElifDef) {
            CondInfo.FoundNonSkip = true;
            break;
          }
        }
      } else {
        SuggestDirectiveTypo(Tok, Directive);
      }
    } else {
      SuggestDirectiveTypo(Tok, Directive);
    }

    CurPPLexer->ParsingDirective = false;
    // Restore comment saving mode.
    if (CurLexer)
      CurLexer->resetExtendedTokenMode();
  }

  // Finally, if we are out of the conditional (saw an #endif or ran off the end
  // of the file, just stop skipping and return to lexing whatever came after
  // the #if block.
  CurPPLexer->LexingRawMode = false;

  if (Callbacks && Tok.isNot(tok::eof))
    Callbacks->SourceRangeSkipped(
        SourceRange(HashTokenLoc, endLoc.isValid()
                                      ? endLoc
                                      : CurPPLexer->getSourceLocation()),
        Tok.getLocation());
}

OptionalFileEntryRef
PrepEngine::getHeaderToIncludeForDiagnostics(SourceLocation IncLoc,
                                             SourceLocation Loc) {
  auto &SM = getSourceManager();
  while (!Loc.isInvalid() && !SM.isInMainFile(Loc)) {
    auto ID = SM.getFileID(SM.getExpansionLoc(Loc));
    auto FE = SM.getFileEntryRefForID(ID);
    if (!FE)
      break;

    if (getIncludeResolver().isFileMultipleIncludeGuarded(*FE))
      return *FE;

    Loc = SM.getIncludeLoc(ID);
  }

  return std::nullopt;
}

OptionalFileEntryRef PrepEngine::ResolveInclude(
    SourceLocation FilenameLoc, llvm::StringRef Filename, bool isAngled,
    ConstSearchDirIterator FromDir, const FileEntry *FromFile,
    ConstSearchDirIterator *CurDirArg, llvm::SmallVectorImpl<char> *SearchPath,
    llvm::SmallVectorImpl<char> *RelativePath, bool *IsMapped,
    bool *IsFrameworkFound, bool SkipCache, bool OpenFile, bool CacheFailures) {
  ConstSearchDirIterator CurDirLocal = nullptr;
  ConstSearchDirIterator &CurDir = CurDirArg ? *CurDirArg : CurDirLocal;

  // If the header lookup mechanism may be relative to the current inclusion
  // stack, record the parent #includes.
  llvm::SmallVector<std::pair<OptionalFileEntryRef, DirectoryEntryRef>, 16>
      Includers;
  if (!FromDir && !FromFile) {
    FileID FID = getCurrentFileLexer()->getFileID();
    OptionalFileEntryRef FileEnt = SourceMgr.getFileEntryRefForID(FID);

    // If there is no file entry associated with this file, it must be the
    // predefines buffer or the module includes buffer. Any other file is not
    // lexed with a normal lexer, so it won't be scanned for preprocessor
    // directives.
    //
    // If we have the predefines buffer, resolve #include references (which come
    // from the -include command line argument) from the current working
    // directory instead of relative to the main file.
    //
    // If we have the module includes buffer, resolve #include references (which
    // come from header declarations in the module map) relative to the module
    // map file.
    if (!FileEnt) {
      if (FID == SourceMgr.getMainFileID() && MainFileDir) {
        Includers.push_back(std::make_pair(std::nullopt, *MainFileDir));
      } else if ((FileEnt = SourceMgr.getFileEntryRefForID(
                      SourceMgr.getMainFileID()))) {
        auto CWD = FileMgr.getOptionalDirectoryRef(".");
        Includers.push_back(std::make_pair(*FileEnt, *CWD));
      }
    } else {
      Includers.push_back(std::make_pair(*FileEnt, FileEnt->getDir()));
    }

    // MSVC searches the current include stack from top to bottom for
    // headers included by quoted include directives.
    // See: http://msdn.microsoft.com/en-us/library/36k2cdd4.aspx
    if (LangOpts.MSVCCompat && !isAngled) {
      for (LexerFrame &ISEntry : llvm::reverse(IncludeMacroStack)) {
        if (IsFileLexer(ISEntry))
          if ((FileEnt = ISEntry.ThePPLexer->getFileEntry()))
            Includers.push_back(std::make_pair(*FileEnt, FileEnt->getDir()));
      }
    }
  }

  CurDir = CurDirLookup;

  if (FromFile) {
    // We're supposed to start looking from after a particular file. Search
    // the include path until we find that file or run out of files.
    ConstSearchDirIterator TmpCurDir = CurDir;
    ConstSearchDirIterator TmpFromDir = nullptr;
    while (OptionalFileEntryRef FE = HeaderInfo.ResolveInclude(
               Filename, FilenameLoc, isAngled, TmpFromDir, &TmpCurDir,
               Includers, SearchPath, RelativePath,
               /*IsMapped=*/nullptr,
               /*IsFrameworkFound=*/nullptr, SkipCache)) {
      // Keep looking as if this file did a #include_next.
      TmpFromDir = TmpCurDir;
      ++TmpFromDir;
      if (&FE->getFileEntry() == FromFile) {
        // Found it.
        FromDir = TmpFromDir;
        CurDir = TmpCurDir;
        break;
      }
    }
  }

  // Do a standard file entry lookup.
  OptionalFileEntryRef FE = HeaderInfo.ResolveInclude(
      Filename, FilenameLoc, isAngled, FromDir, &CurDir, Includers, SearchPath,
      RelativePath, IsMapped, IsFrameworkFound, SkipCache, OpenFile,
      CacheFailures);
  if (FE)
    return FE;

  OptionalFileEntryRef CurFileEnt;
  // Otherwise, see if this is a subframework header.  If so, this is relative
  // to one of the headers on the #include stack.  Walk the list of the current
  // headers on the #include stack and pass them to HeaderInfo.
  if (IsFileLexer()) {
    if ((CurFileEnt = CurPPLexer->getFileEntry())) {
      if (OptionalFileEntryRef FE = HeaderInfo.ResolveSubframeworkHeader(
              Filename, *CurFileEnt, SearchPath, RelativePath)) {
        return FE;
      }
    }
  }

  for (LexerFrame &ISEntry : llvm::reverse(IncludeMacroStack)) {
    if (IsFileLexer(ISEntry)) {
      if ((CurFileEnt = ISEntry.ThePPLexer->getFileEntry())) {
        if (OptionalFileEntryRef FE = HeaderInfo.ResolveSubframeworkHeader(
                Filename, *CurFileEnt, SearchPath, RelativePath)) {
          return FE;
        }
      }
    }
  }

  return std::nullopt;
}
