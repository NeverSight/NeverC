#include "neverc/Scan/ExpansionLexer.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/VarArgExpansion.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstring>
#include <optional>

#ifdef __SSE2__
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;

namespace {
constexpr unsigned PrefetchAhead = 4;
} // namespace

// ===----------------------------------------------------------------------===
// Initialization & cleanup
// ===----------------------------------------------------------------------===

void ExpansionLexer::Init(Token &Tok, SourceLocation ELEnd, MacroRecord *MI,
                          MacroArgStorage *Actuals) {
  destroy();

  Macro = MI;
  ActualArgs = Actuals;
  CurTokenIdx = 0;

  ExpandLocStart = Tok.getLocation();
  ExpandLocEnd = ELEnd;
  AtStartOfLine = Tok.isAtStartOfLine();
  HasLeadingSpace = Tok.hasLeadingSpace();
  NextTokGetsSpace = false;
  Tokens = &*Macro->tokens_begin();
  OwnsTokens = false;
  DisableMacroExpansion = false;
  IsReinject = false;
  NumTokens = Macro->tokens_end() - Macro->tokens_begin();
  MacroExpansionStart = SourceLocation();

  SourceManager &SM = PP.getSourceManager();
  MacroStartSLocOffset = SM.getNextLocalOffset();

  if (NumTokens > 0) {
    assert(Tokens[0].getLocation().isValid());
    assert((Tokens[0].getLocation().isFileID() || Tokens[0].is(tok::comment)) &&
           "Macro defined in macro?");
    assert(ExpandLocStart.isValid());

    MacroDefStart = SM.getExpansionLoc(Tokens[0].getLocation());
    MacroDefLength = Macro->getDefinitionLength(SM);
    MacroExpansionStart = SM.createExpansionLoc(MacroDefStart, ExpandLocStart,
                                                ExpandLocEnd, MacroDefLength);
  }

  if (Macro->isFunctionLike() && Macro->getNumParams())
    substituteFunctionParams();

  Macro->DisableMacro();
}

void ExpansionLexer::Init(const Token *TokArray, unsigned NumToks,
                          bool disableMacroExpansion, bool ownsTokens,
                          bool isReinject) {
  assert(!isReinject || disableMacroExpansion);
  destroy();

  Macro = nullptr;
  ActualArgs = nullptr;
  Tokens = TokArray;
  OwnsTokens = ownsTokens;
  DisableMacroExpansion = disableMacroExpansion;
  IsReinject = isReinject;
  NumTokens = NumToks;
  CurTokenIdx = 0;
  ExpandLocStart = ExpandLocEnd = SourceLocation();
  AtStartOfLine = false;
  HasLeadingSpace = false;
  NextTokGetsSpace = false;
  MacroExpansionStart = SourceLocation();

  if (NumToks != 0) {
    AtStartOfLine = TokArray[0].isAtStartOfLine();
    HasLeadingSpace = TokArray[0].hasLeadingSpace();
  }
}

void ExpansionLexer::destroy() {
  if (OwnsTokens) {
    delete[] Tokens;
    Tokens = nullptr;
    OwnsTokens = false;
  }

  if (ActualArgs)
    ActualArgs->destroy(PP);
}

// ===----------------------------------------------------------------------===
// Parameter substitution
// ===----------------------------------------------------------------------===

bool ExpansionLexer::tryOmitCommaForVarargs(
    llvm::SmallVectorImpl<Token> &ResultToks, bool HasPasteOperator,
    MacroRecord *Macro, unsigned MacroArgNo, PrepEngine &PP) {
  if (!Macro->isVariadic() || MacroArgNo != Macro->getNumParams() - 1)
    return false;

  if (!HasPasteOperator && !PP.getLangOpts().MSVCCompat)
    return false;

  if (PP.getLangOpts().C99 && !PP.getLangOpts().GNUMode &&
      Macro->getNumParams() < 2)
    return false;

  if (ResultToks.empty() || !ResultToks.back().is(tok::comma))
    return false;

  if (HasPasteOperator)
    PP.Diag(ResultToks.back().getLocation(), diag::ext_paste_comma);

  ResultToks.pop_back();

  if (!ResultToks.empty()) {
    if (ResultToks.back().is(tok::hashhash))
      ResultToks.pop_back();
    ResultToks.back().setFlag(Token::CommaAfterElided);
  }

  NextTokGetsSpace = false;
  return true;
}

void ExpansionLexer::genVAOptStringified(
    llvm::SmallVectorImpl<Token> &ResultToks, const VAOptExpansionContext &VCtx,
    const SourceLocation VAOPTClosingParenLoc) {
  const int NumToksPriorToVAOpt = VCtx.getNumberOfTokensPriorToVAOpt();
  const unsigned int NumVAOptTokens = ResultToks.size() - NumToksPriorToVAOpt;
  Token *const VAOPTTokens =
      NumVAOptTokens ? &ResultToks[NumToksPriorToVAOpt] : nullptr;

  llvm::SmallVector<Token, 64> ConcatenatedVAOPTResultToks;
  // Perform token pasting (concatenation) prior to stringization.
  for (unsigned int CurTokenIdx = 0; CurTokenIdx != NumVAOptTokens;
       ++CurTokenIdx) {
    if (VAOPTTokens[CurTokenIdx].is(tok::hashhash)) {
      assert(CurTokenIdx != 0 &&
             "Can not have __VAOPT__ contents begin with a ##");
      Token &LHS = VAOPTTokens[CurTokenIdx - 1];
      pasteTokens(LHS, llvm::ArrayRef(VAOPTTokens, NumVAOptTokens),
                  CurTokenIdx);
      // Replace the token prior to the first ## in this iteration.
      ConcatenatedVAOPTResultToks.back() = LHS;
      if (CurTokenIdx == NumVAOptTokens)
        break;
    }
    ConcatenatedVAOPTResultToks.push_back(VAOPTTokens[CurTokenIdx]);
  }

  ConcatenatedVAOPTResultToks.push_back(VCtx.getEOFTok());
  const SourceLocation ExpansionLocStartWithinMacro =
      getExpansionLocForMacroDefLoc(VCtx.getVAOptLoc());
  const SourceLocation ExpansionLocEndWithinMacro =
      getExpansionLocForMacroDefLoc(VAOPTClosingParenLoc);

  Token StringifiedVAOPT = MacroArgStorage::EscapeArgToLiteral(
      &ConcatenatedVAOPTResultToks[0], PP, VCtx.hasCharifyBefore() /*Charify*/,
      ExpansionLocStartWithinMacro, ExpansionLocEndWithinMacro);

  if (VCtx.getLeadingSpaceForStringifiedToken())
    StringifiedVAOPT.setFlag(Token::LeadingSpace);

  StringifiedVAOPT.setFlag(Token::StringifiedInMacro);
  ResultToks.resize(NumToksPriorToVAOpt + 1);
  ResultToks.back() = StringifiedVAOPT;
}

bool ExpansionLexer::needsParamSubstitution() const {
  const unsigned E = NumTokens;
  if (LLVM_UNLIKELY(E == 0))
    return false;

  const bool IsVariadic = Macro->isVariadic();
  const unsigned NParams = Macro->getNumParams();

  // Two-token lookahead batch: check token kinds of adjacent pairs
  // to reduce branch misprediction on the hash/hashat test.
  unsigned I = 0;
  for (; I + 2 <= E; I += 2) {
    if (LLVM_LIKELY(I + PrefetchAhead + 2 < E))
      __builtin_prefetch(&Tokens[I + PrefetchAhead + 2], 0, 1);

    const Token &T0 = Tokens[I];
    const Token &T1 = Tokens[I + 1];

    // Batch the stringification-operator check: if either token in
    // the pair is hash/hashat we need substitution.
    unsigned K0 = static_cast<unsigned>(T0.getKind());
    unsigned K1 = static_cast<unsigned>(T1.getKind());
    unsigned HashKind = static_cast<unsigned>(tok::hash);
    unsigned HashAtKind = static_cast<unsigned>(tok::hashat);
    if (LLVM_UNLIKELY((K0 == HashKind) | (K0 == HashAtKind) | (K1 == HashKind) |
                      (K1 == HashAtKind)))
      return true;

    const IdentifierInfo *II0 = T0.getIdentifierInfo();
    const IdentifierInfo *II1 = T1.getIdentifierInfo();

    if (II0 && NParams != 0 && Macro->getParameterNum(II0) != -1)
      return true;
    if (II1 && NParams != 0 && Macro->getParameterNum(II1) != -1)
      return true;
    if (LLVM_UNLIKELY(IsVariadic)) {
      if ((II0 && II0->isStr("__VA_OPT__")) ||
          (II1 && II1->isStr("__VA_OPT__")))
        return true;
    }
  }
  // Handle trailing odd token.
  if (I < E) {
    const Token &T = Tokens[I];
    if (LLVM_UNLIKELY(T.isOneOf(tok::hash, tok::hashat)))
      return true;
    const IdentifierInfo *II = T.getIdentifierInfo();
    if (II) {
      if (NParams != 0 && Macro->getParameterNum(II) != -1)
        return true;
      if (LLVM_UNLIKELY(IsVariadic && II->isStr("__VA_OPT__")))
        return true;
    }
  }
  return false;
}

bool ExpansionLexer::processVAOpt(
    unsigned &I, llvm::SmallVectorImpl<Token> &ResultToks,
    VAOptExpansionContext &VCtx, bool &MadeChange,
    std::optional<bool> &CalledWithVariadicArguments) {
  const Token &CurTok = Tokens[I];

  if (VCtx.isVAOptToken(CurTok)) {
    MadeChange = true;
    assert(Tokens[I + 1].is(tok::l_paren) &&
           "__VA_OPT__ must be followed by '('");
    ++I;
    VCtx.sawVAOptFollowedByOpeningParens(CurTok.getLocation(),
                                         ResultToks.size());
    return true;
  }

  if (!VCtx.isInVAOpt())
    return false;

  if (Tokens[I].is(tok::l_paren))
    VCtx.sawOpeningParen(Tokens[I].getLocation());

  if (!Tokens[I].is(tok::r_paren) || !VCtx.sawClosingParen()) {
    if (!CalledWithVariadicArguments)
      CalledWithVariadicArguments =
          ActualArgs->invokedWithVariadicArgument(Macro, PP);
    if (!*CalledWithVariadicArguments)
      return true;
    return false;
  }

  if (VCtx.hasStringifyOrCharifyBefore()) {
    genVAOptStringified(ResultToks, VCtx, Tokens[I].getLocation());
  } else if (ResultToks.size() == VCtx.getNumberOfTokensPriorToVAOpt()) {
    if (ResultToks.size() && ResultToks.back().is(tok::hashhash)) {
      ResultToks.pop_back();
    } else if ((I + 1 != NumTokens) && Tokens[I + 1].is(tok::hashhash)) {
      ++I;
    }
  } else {
    if (VCtx.beginsWithPlaceholder()) {
      assert(VCtx.getNumberOfTokensPriorToVAOpt() > 0 &&
             ResultToks.size() >= VCtx.getNumberOfTokensPriorToVAOpt() &&
             ResultToks[VCtx.getNumberOfTokensPriorToVAOpt() - 1].is(
                 tok::hashhash) &&
             "no token paste before __VA_OPT__");
      ResultToks.erase(ResultToks.begin() +
                       VCtx.getNumberOfTokensPriorToVAOpt() - 1);
    }
    if (VCtx.endsWithPlaceholder() && I + 1 != NumTokens &&
        Tokens[I + 1].is(tok::hashhash)) {
      ++I;
    }
  }
  VCtx.reset();
  return true;
}

void ExpansionLexer::substituteFunctionParams() {
  if (!needsParamSubstitution())
    return;

  const unsigned E = NumTokens;
  const unsigned NParams = Macro->getNumParams();
  const bool MSVCMode =
      PP.getLangOpts().MSVCCompat || PP.getLangOpts().MicrosoftExt;

  llvm::SmallVector<Token, 128> ResultToks;
  ResultToks.reserve(E + NParams * 4);

  bool MadeChange = false;

  std::optional<bool> CalledWithVariadicArguments;

  VAOptExpansionContext VCtx(PP);

  for (unsigned I = 0; I != E; ++I) {
    if (LLVM_LIKELY(I + PrefetchAhead < E))
      __builtin_prefetch(&Tokens[I + PrefetchAhead], 0, 1);

    const Token &CurTok = Tokens[I];
    if (I != 0 && !Tokens[I - 1].is(tok::hashhash) && CurTok.hasLeadingSpace())
      NextTokGetsSpace = true;

    if (processVAOpt(I, ResultToks, VCtx, MadeChange,
                     CalledWithVariadicArguments))
      continue;

    if (LLVM_UNLIKELY(CurTok.isOneOf(tok::hash, tok::hashat))) {
      int ArgNo = Macro->getParameterNum(Tokens[I + 1].getIdentifierInfo());
      assert((ArgNo != -1 || VCtx.isVAOptToken(Tokens[I + 1])) &&
             "Token following # is not an argument or __VA_OPT__!");

      if (ArgNo == -1) {
        VCtx.sawHashOrHashAtBefore(NextTokGetsSpace, CurTok.is(tok::hashat));
        continue;
      }
      SourceLocation ExpansionLocStart =
          getExpansionLocForMacroDefLoc(CurTok.getLocation());
      SourceLocation ExpansionLocEnd =
          getExpansionLocForMacroDefLoc(Tokens[I + 1].getLocation());

      bool Charify = CurTok.is(tok::hashat);
      const Token *UnexpArg = ActualArgs->getUnexpArgument(ArgNo);
      Token Res = MacroArgStorage::EscapeArgToLiteral(
          UnexpArg, PP, Charify, ExpansionLocStart, ExpansionLocEnd);
      Res.setFlag(Token::StringifiedInMacro);

      if (NextTokGetsSpace)
        Res.setFlag(Token::LeadingSpace);

      ResultToks.push_back(Res);
      MadeChange = true;
      ++I;
      NextTokGetsSpace = false;
      continue;
    }

    bool NonEmptyPasteBefore =
        !ResultToks.empty() && ResultToks.back().is(tok::hashhash);
    bool PasteBefore = I != 0 && Tokens[I - 1].is(tok::hashhash);
    bool PasteAfter = I + 1 != E && Tokens[I + 1].is(tok::hashhash);
    bool RParenAfter = I + 1 != E && Tokens[I + 1].is(tok::r_paren);
    bool NumericConstantBeforeHashHash =
        MSVCMode && (I >= 2) && Tokens[I - 1].is(tok::hashhash) &&
        Tokens[I - 2].is(tok::numeric_constant);
    assert((!NonEmptyPasteBefore || PasteBefore || VCtx.isInVAOpt()) &&
           "unexpected ## in ResultToks");

    IdentifierInfo *II = CurTok.getIdentifierInfo();
    int ArgNo = II ? Macro->getParameterNum(II) : -1;
    if (LLVM_LIKELY(ArgNo == -1)) {
      ResultToks.push_back(CurTok);

      if (NextTokGetsSpace) {
        ResultToks.back().setFlag(Token::LeadingSpace);
        NextTokGetsSpace = false;
      } else if (PasteBefore && !NonEmptyPasteBefore)
        ResultToks.back().clearFlag(Token::LeadingSpace);

      continue;
    }

    MadeChange = true;

    if (!PasteBefore && ActualArgs->isVarargsElidedUse() &&
        tryOmitCommaForVarargs(ResultToks,
                               /*HasPasteOperator=*/false, Macro, ArgNo, PP))
      continue;

    bool GotoPreExpand = !PasteBefore && !PasteAfter;
    if (!GotoPreExpand && NumericConstantBeforeHashHash)
      GotoPreExpand = true;
    if (GotoPreExpand) {
      const Token *ResultArgToks;
      const Token *ArgTok = ActualArgs->getUnexpArgument(ArgNo);
      if (ActualArgs->ArgNeedsPreexpansion(ArgTok, PP))
        ResultArgToks = &ActualArgs->getPreExpArgument(ArgNo, PP)[0];
      else
        ResultArgToks = ArgTok; // Use non-preexpanded tokens.

      if (ResultArgToks->isNot(tok::eof)) {
        size_t FirstResult = ResultToks.size();
        unsigned NumToks = MacroArgStorage::getArgLength(ResultArgToks);
        ResultToks.append(ResultArgToks, ResultArgToks + NumToks);

        if (PP.getLangOpts().MSVCCompat && NumToks == 1 &&
            ResultToks.back().is(tok::comma))
          ResultToks.back().setFlag(Token::IgnoredComma);

        for (Token &Tok : llvm::drop_begin(ResultToks, FirstResult))
          if (Tok.is(tok::hashhash))
            Tok.setKind(tok::unknown);

        if (ExpandLocStart.isValid()) {
          updateLocForMacroArgTokens(CurTok.getLocation(),
                                     ResultToks.begin() + FirstResult,
                                     ResultToks.end());
        }

        ResultToks[FirstResult].setFlagValue(Token::LeadingSpace,
                                             NextTokGetsSpace);
        ResultToks[FirstResult].setFlagValue(Token::StartOfLine, false);
        NextTokGetsSpace = false;
      } else {
        if (NonEmptyPasteBefore) {
          assert(VCtx.isInVAOpt() && "should only happen inside a __VA_OPT__");
          VCtx.hasPlaceholderAfterHashhashAtStart();
        } else if (RParenAfter)
          VCtx.hasPlaceholderBeforeRParen();
      }
      continue;
    }

    const Token *ArgToks = ActualArgs->getUnexpArgument(ArgNo);
    unsigned NumToks = MacroArgStorage::getArgLength(ArgToks);
    if (NumToks) {
      bool VaArgsPseudoPaste = false;
      // GNU ", ## __VA_ARGS__" extension.
      if (NonEmptyPasteBefore && ResultToks.size() >= 2 &&
          ResultToks[ResultToks.size() - 2].is(tok::comma) &&
          (unsigned)ArgNo == Macro->getNumParams() - 1 && Macro->isVariadic()) {
        VaArgsPseudoPaste = true;
        PP.Diag(ResultToks.pop_back_val().getLocation(), diag::ext_paste_comma);
      }

      ResultToks.append(ArgToks, ArgToks + NumToks);

      for (Token &Tok :
           llvm::make_range(ResultToks.end() - NumToks, ResultToks.end())) {
        if (Tok.is(tok::hashhash))
          Tok.setKind(tok::unknown);
      }

      if (ExpandLocStart.isValid()) {
        updateLocForMacroArgTokens(
            CurTok.getLocation(), ResultToks.end() - NumToks, ResultToks.end());
      }

      if (!VaArgsPseudoPaste) {
        ResultToks[ResultToks.size() - NumToks].setFlagValue(Token::StartOfLine,
                                                             false);
        ResultToks[ResultToks.size() - NumToks].setFlagValue(
            Token::LeadingSpace, NextTokGetsSpace);
      }

      NextTokGetsSpace = false;
      continue;
    }

    if (PasteAfter) {
      ++I;
      continue;
    }

    if (RParenAfter && !NonEmptyPasteBefore)
      VCtx.hasPlaceholderBeforeRParen();

    assert(PasteBefore);
    if (NonEmptyPasteBefore) {
      assert(ResultToks.back().is(tok::hashhash));
      if (!VCtx.isInVAOpt() ||
          ResultToks.size() > VCtx.getNumberOfTokensPriorToVAOpt())
        ResultToks.pop_back();
      else
        VCtx.hasPlaceholderAfterHashhashAtStart();
    }

    if (ActualArgs->isVarargsElidedUse())
      tryOmitCommaForVarargs(ResultToks,
                             /*HasPasteOperator=*/true, Macro, ArgNo, PP);
  }

  if (MadeChange) {
    assert(!OwnsTokens && "This would leak if we already own the token list");
    NumTokens = ResultToks.size();
    Tokens = PP.stashExpandedTokens(this, ResultToks);
    OwnsTokens = false;
  }
}

// ===----------------------------------------------------------------------===
// Lexing
// ===----------------------------------------------------------------------===

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isWideLiteralConcat(const Token &FirstTok, const Token &SecondTok) {
  return FirstTok.is(tok::identifier) &&
         FirstTok.getIdentifierInfo()->isStr("L") && SecondTok.isLiteral() &&
         SecondTok.stringifiedInMacro();
}
} // namespace

__attribute__((hot)) bool ExpansionLexer::Lex(Token &Tok) {
  if (LLVM_UNLIKELY(isAtEnd())) {
    if (Macro)
      Macro->EnableMacro();

    Tok.startToken();
    Tok.setFlagValue(Token::StartOfLine, AtStartOfLine);
    Tok.setFlagValue(Token::LeadingSpace, HasLeadingSpace || NextTokGetsSpace);
    if (CurTokenIdx == 0)
      Tok.setFlag(Token::LeadingEmptyMacro);
    return PP.FinalizeExpansion(Tok);
  }

  SourceManager &SM = PP.getSourceManager();

  const bool isFirstToken = CurTokenIdx == 0;
  Tok = Tokens[CurTokenIdx++];

  if (LLVM_LIKELY(!isAtEnd())) {
    __builtin_prefetch(&Tokens[CurTokenIdx], 0, 3);
    if (LLVM_LIKELY(CurTokenIdx + PrefetchAhead < NumTokens))
      __builtin_prefetch(&Tokens[CurTokenIdx + PrefetchAhead], 0, 1);
  }

  if (LLVM_UNLIKELY(IsReinject))
    Tok.setFlag(Token::IsReinjected);

  bool TokenIsFromPaste = false;

  if (LLVM_UNLIKELY(!isAtEnd() && Macro)) {
    const Token &Next = Tokens[CurTokenIdx];
    if (LLVM_UNLIKELY(
            Next.is(tok::hashhash) ||
            (PP.getLangOpts().MSVCCompat && isWideLiteralConcat(Tok, Next)))) {
      if (pasteTokens(Tok))
        return true;
      TokenIsFromPaste = true;
    }
  }

  if (LLVM_LIKELY(ExpandLocStart.isValid()) &&
      SM.isBeforeInSLocAddrSpace(Tok.getLocation(), MacroStartSLocOffset)) {
    SourceLocation instLoc;
    if (LLVM_UNLIKELY(Tok.is(tok::comment))) {
      instLoc = SM.createExpansionLoc(Tok.getLocation(), ExpandLocStart,
                                      ExpandLocEnd, Tok.getLength());
    } else {
      instLoc = getExpansionLocForMacroDefLoc(Tok.getLocation());
    }

    Tok.setLocation(instLoc);
  }

  if (LLVM_UNLIKELY(isFirstToken)) {
    Tok.setFlagValue(Token::StartOfLine, AtStartOfLine);
    Tok.setFlagValue(Token::LeadingSpace, HasLeadingSpace);
  } else {
    if (AtStartOfLine)
      Tok.setFlag(Token::StartOfLine);
    if (HasLeadingSpace)
      Tok.setFlag(Token::LeadingSpace);
  }
  AtStartOfLine = false;
  HasLeadingSpace = false;

  IdentifierInfo *II;
  if (LLVM_LIKELY(!Tok.isAnnotation()) &&
      LLVM_LIKELY((II = Tok.getIdentifierInfo()) != nullptr)) {
    Tok.setKind(II->getTokenID());

    if (LLVM_UNLIKELY(II->isPoisoned() && TokenIsFromPaste))
      PP.DiagPoisonedIdent(Tok);

    if (LLVM_UNLIKELY(!DisableMacroExpansion &&
                      II->needsIdentifierProcessing()))
      return PP.ResolveIdentifier(Tok);
  }

  return true;
}

// ===----------------------------------------------------------------------===
// Token pasting
// ===----------------------------------------------------------------------===

bool ExpansionLexer::pasteTokens(Token &Tok) {
  return pasteTokens(Tok, llvm::ArrayRef(Tokens, NumTokens), CurTokenIdx);
}

namespace {
bool handleMSVCPasteCompat(const LangOptions &LO, const Token &LHS,
                           const Token &RHS, bool IsInvalid,
                           bool &PasteResult) {
  if (LLVM_LIKELY(!LO.MSVCCompat && !LO.MicrosoftExt))
    return false;
  if (!IsInvalid) {
    if (LHS.is(tok::kw___FUNCTION__) && RHS.is(tok::string_literal)) {
      PasteResult = false;
      return true;
    }
    if ((LHS.is(tok::period) || LHS.is(tok::arrow)) && RHS.isAnyIdentifier()) {
      PasteResult = true;
      return true;
    }
    if (LHS.is(tok::string_literal) && RHS.is(tok::string_literal)) {
      PasteResult = true;
      return true;
    }
  } else {
    if (LHS.is(tok::amp)) {
      PasteResult = true;
      return true;
    }
    if (LHS.isAnyIdentifier() && RHS.is(tok::string_literal)) {
      PasteResult = false;
      return true;
    }
  }
  return false;
}
} // namespace

bool ExpansionLexer::pasteTokens(Token &LHSTok,
                                 llvm::ArrayRef<Token> TokenStream,
                                 unsigned int &CurIdx) {
  assert(CurIdx > 0 && "## can not be the first token within tokens");
  assert((TokenStream[CurIdx].is(tok::hashhash) ||
          (PP.getLangOpts().MSVCCompat &&
           isWideLiteralConcat(LHSTok, TokenStream[CurIdx]))) &&
         "Token at this Index must be ## or part of the MSVC 'L "
         "#macro-arg' pasting pair");

  if (PP.getLangOpts().MicrosoftExt && (CurIdx >= 2) &&
      TokenStream[CurIdx - 2].is(tok::hashhash))
    LHSTok.clearFlag(Token::LeadingSpace);

  llvm::SmallString<128> Buffer;
  const char *ResultTokStrPtr = nullptr;
  SourceLocation StartLoc = LHSTok.getLocation();
  SourceLocation PasteOpLoc;

  auto IsAtEnd = [&TokenStream, &CurIdx] {
    return TokenStream.size() == CurIdx;
  };

  do {
    PasteOpLoc = TokenStream[CurIdx].getLocation();
    if (TokenStream[CurIdx].is(tok::hashhash))
      ++CurIdx;
    assert(!IsAtEnd() && "No token on the RHS of a paste operator!");

    const Token &RHS = TokenStream[CurIdx];
    Buffer.resize(LHSTok.getLength() + RHS.getLength());

    const char *BufPtr = &Buffer[0];
    bool Invalid = false;
    unsigned LHSLen = PP.getSpelling(LHSTok, BufPtr, &Invalid);
    if (BufPtr != &Buffer[0])
      memcpy(&Buffer[0], BufPtr, LHSLen);
    if (Invalid)
      return true;

    BufPtr = Buffer.data() + LHSLen;
    unsigned RHSLen = PP.getSpelling(RHS, BufPtr, &Invalid);
    if (Invalid)
      return true;
    if (RHSLen && BufPtr != &Buffer[LHSLen])
      memcpy(&Buffer[LHSLen], BufPtr, RHSLen);

    Buffer.resize(LHSLen + RHSLen);

    Token ResultTokTmp;
    ResultTokTmp.startToken();
    ResultTokTmp.setKind(tok::string_literal);
    PP.WriteScratch(Buffer, ResultTokTmp);
    SourceLocation ResultTokLoc = ResultTokTmp.getLocation();
    ResultTokStrPtr = ResultTokTmp.getLiteralData();

    Token Result;
    {
      bool PasteResult;
      if (handleMSVCPasteCompat(PP.getLangOpts(), LHSTok, RHS,
                                /*IsInvalid=*/false, PasteResult))
        return PasteResult;
    }
    if (LHSTok.isAnyIdentifier() && RHS.isAnyIdentifier()) {
      // Fast path: id+id = id, skip re-lexing.
      PP.TallyTokenPaste(true);
      Result.startToken();
      Result.setKind(tok::raw_identifier);
      Result.setRawIdentifierData(ResultTokStrPtr);
      Result.setLocation(ResultTokLoc);
      Result.setLength(LHSLen + RHSLen);
    } else {
      PP.TallyTokenPaste(false);

      assert(ResultTokLoc.isFileID() &&
             "Should be a raw location into scratch buffer");
      SourceManager &SourceMgr = PP.getSourceManager();
      FileID LocFileID = SourceMgr.getFileID(ResultTokLoc);

      bool Invalid = false;
      const char *ScratchBufStart =
          SourceMgr.getBufferData(LocFileID, &Invalid).data();
      if (Invalid)
        return false;

      SourceScanner TL(SourceMgr.getLocForStartOfFile(LocFileID),
                       PP.getLangOpts(), ScratchBufStart, ResultTokStrPtr,
                       ResultTokStrPtr + LHSLen + RHSLen);

      bool isInvalid = !TL.LexFromRawLexer(Result);
      isInvalid |= Result.is(tok::eof);

      if (isInvalid) {
        {
          bool PasteResult;
          if (handleMSVCPasteCompat(PP.getLangOpts(), LHSTok, RHS,
                                    /*IsInvalid=*/true, PasteResult))
            return PasteResult;
        }
        SourceManager &SM = PP.getSourceManager();
        SourceLocation Loc =
            SM.createExpansionLoc(PasteOpLoc, ExpandLocStart, ExpandLocEnd, 2);

        if (PP.getLangOpts().MicrosoftExt && LHSTok.is(tok::slash) &&
            RHS.is(tok::slash)) {
          skipPastedComment(LHSTok, Loc);
          return true;
        }

        if (!PP.getLangOpts().AsmPreprocessor) {
          PP.Diag(Loc, PP.getLangOpts().MicrosoftExt ? diag::ext_pp_bad_paste_ms
                                                     : diag::err_pp_bad_paste)
              << Buffer;
        }

        break;
      }

      if (Result.is(tok::hashhash))
        Result.setKind(tok::unknown);
    }

    Result.setFlagValue(Token::StartOfLine, LHSTok.isAtStartOfLine());
    Result.setFlagValue(Token::LeadingSpace, LHSTok.hasLeadingSpace());
    ++CurIdx;
    LHSTok = Result;
  } while (!IsAtEnd() && TokenStream[CurIdx].is(tok::hashhash));

  SourceLocation EndLoc = TokenStream[CurIdx - 1].getLocation();

  SourceManager &SM = PP.getSourceManager();
  if (StartLoc.isFileID())
    StartLoc = getExpansionLocForMacroDefLoc(StartLoc);
  if (EndLoc.isFileID())
    EndLoc = getExpansionLocForMacroDefLoc(EndLoc);
  FileID MacroFID = SM.getFileID(MacroExpansionStart);
  while (SM.getFileID(StartLoc) != MacroFID)
    StartLoc = SM.getImmediateExpansionRange(StartLoc).getBegin();
  while (SM.getFileID(EndLoc) != MacroFID)
    EndLoc = SM.getImmediateExpansionRange(EndLoc).getEnd();

  LHSTok.setLocation(SM.createExpansionLoc(LHSTok.getLocation(), StartLoc,
                                           EndLoc, LHSTok.getLength()));

  // Raw-mode paste skips identifier resolution; do it now.
  if (LHSTok.is(tok::raw_identifier)) {
    PP.ResolveRawIdent(LHSTok);
  }
  return false;
}

unsigned ExpansionLexer::isNextTokenLParen() const {
  if (isAtEnd())
    return 2;
  return Tokens[CurTokenIdx].is(tok::l_paren);
}

bool ExpansionLexer::isParsingDirective() const {
  return Tokens[NumTokens - 1].is(tok::eod) && !isAtEnd();
}

void ExpansionLexer::skipPastedComment(Token &Tok, SourceLocation OpLoc) {
  PP.Diag(OpLoc, diag::ext_comment_paste_microsoft);
  assert(Macro && "Token streams can't paste comments");
  Macro->EnableMacro();

  PP.SkipPastedComment(Tok);
}

// ===----------------------------------------------------------------------===
// Source location mapping
// ===----------------------------------------------------------------------===

SourceLocation
ExpansionLexer::getExpansionLocForMacroDefLoc(SourceLocation loc) const {
  assert(ExpandLocStart.isValid() && MacroExpansionStart.isValid() &&
         "Not appropriate for token streams");
  assert(loc.isValid() && loc.isFileID());

  SourceManager &SM = PP.getSourceManager();
  assert(SM.isInSLocAddrSpace(loc, MacroDefStart, MacroDefLength) &&
         "Expected loc to come from the macro definition");

  SourceLocation::UIntTy relativeOffset = 0;
  SM.isInSLocAddrSpace(loc, MacroDefStart, MacroDefLength, &relativeOffset);
  return MacroExpansionStart.getLocWithOffset(relativeOffset);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
void batchUpdateExpansionLocs(SourceManager &SM, SourceLocation ExpandLoc,
                              Token *&begin_tokens, Token *end_tokens) {
  assert(begin_tokens + 1 < end_tokens);
  SourceLocation BeginLoc = begin_tokens->getLocation();
  static constexpr SourceLocation::UIntTy MaxDistance = 50;

  Token *PartEnd = begin_tokens + 1;
  const SourceLocation::UIntTy BeginRaw = BeginLoc.getRawEncoding();

  if (LLVM_LIKELY(BeginLoc.isFileID())) {
    SourceLocation::UIntTy PrevRaw = BeginRaw;
    while (PartEnd < end_tokens) {
      SourceLocation::UIntTy CurRaw = PartEnd->getLocation().getRawEncoding();
      if (LLVM_UNLIKELY(!PartEnd->getLocation().isFileID() ||
                        CurRaw - PrevRaw > MaxDistance))
        break;
      PrevRaw = CurRaw;
      ++PartEnd;
    }
  } else {
    FileID BeginFID = SM.getFileID(BeginLoc);
    SourceLocation::UIntTy LimitRaw =
        SM.getComposedLoc(BeginFID, SM.getFileIDSize(BeginFID))
            .getRawEncoding();
    SourceLocation::UIntTy PrevRaw = BeginRaw;
    while (PartEnd < end_tokens) {
      SourceLocation::UIntTy CurRaw = PartEnd->getLocation().getRawEncoding();
      if (CurRaw < BeginRaw || CurRaw > LimitRaw ||
          CurRaw - PrevRaw > MaxDistance)
        break;
      PrevRaw = CurRaw;
      ++PartEnd;
    }
  }
  assert(PartEnd > begin_tokens);

  SourceLocation::UIntTy FullLength =
      (PartEnd - 1)->getEndLoc().getRawEncoding() - BeginRaw;
  SourceLocation Expansion =
      SM.createMacroArgExpansionLoc(BeginLoc, ExpandLoc, FullLength);

  const SourceLocation::UIntTy ExpRaw = Expansion.getRawEncoding();
  for (Token *T = begin_tokens; T != PartEnd; ++T) {
    SourceLocation::UIntTy Off = T->getLocation().getRawEncoding() - BeginRaw;
    T->setLocation(SourceLocation::getFromRawEncoding(ExpRaw + Off));
  }
  begin_tokens = PartEnd;
}
} // namespace

void ExpansionLexer::updateLocForMacroArgTokens(SourceLocation ArgIdSpellLoc,
                                                Token *begin_tokens,
                                                Token *end_tokens) {
  SourceManager &SM = PP.getSourceManager();

  SourceLocation ExpandLoc = getExpansionLocForMacroDefLoc(ArgIdSpellLoc);

  while (begin_tokens < end_tokens) {
    if (end_tokens - begin_tokens == 1) {
      Token &Tok = *begin_tokens;
      Tok.setLocation(SM.createMacroArgExpansionLoc(
          Tok.getLocation(), ExpandLoc, Tok.getLength()));
      return;
    }

    batchUpdateExpansionLocs(SM, ExpandLoc, begin_tokens, end_tokens);
  }
}
