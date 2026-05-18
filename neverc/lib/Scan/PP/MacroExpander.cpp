#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Attr/Attributes.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/MSVCMacroRebuilding.h"
#include <cassert>
#include <cstddef>
#include <ctime>
#include <utility>

using namespace neverc;
// ===----------------------------------------------------------------------===
// Macro directive management
// ===----------------------------------------------------------------------===

MacroDirective *
PrepEngine::getLocalMacroDirectiveHistory(const IdentifierInfo *II) const {
  if (!II->hadMacroDefinition())
    return nullptr;
  auto Pos = Macros.find(II);
  return Pos == Macros.end() ? nullptr : Pos->second.getLatest();
}

void PrepEngine::appendMacroDirective(IdentifierInfo *II, MacroDirective *MD) {
  assert(MD && "MacroDirective should be non-zero!");
  assert(!MD->getPrevious() && "Already attached to a MacroDirective history.");

  MacroState &StoredMD = Macros[II];
  auto *OldMD = StoredMD.getLatest();
  MD->setPrevious(OldMD);
  StoredMD.setLatest(MD);

  II->setHasMacroDefinition(MD->isDefined());
}

void PrepEngine::setLoadedMacroDirective(IdentifierInfo *II, MacroDirective *ED,
                                         MacroDirective *MD) {
  assert(II && MD);
  MacroState &StoredMD = Macros[II];

  if (auto *OldMD = StoredMD.getLatest()) {
    assert(OldMD->getMacroRecord()->isBuiltinMacro() &&
           "only built-ins should have an entry here");
    assert(!OldMD->getPrevious() && "builtin should only have a single entry");
    ED->setPrevious(OldMD);
    StoredMD.setLatest(MD);
  } else {
    StoredMD = MD;
  }

  II->setHasMacroDefinition(MD->isDefined());
}

void PrepEngine::traceMacroRecord(const IdentifierInfo *II) {
  const MacroState *State = nullptr;
  auto Pos = Macros.find(II);
  if (Pos != Macros.end())
    State = &Pos->second;

  llvm::errs() << "MacroState " << State << " " << II->getNameStart() << "\n";

  for (auto *MD = State ? State->getLatest() : nullptr; MD;
       MD = MD->getPrevious()) {
    llvm::errs() << " ";
    MD->dump();
  }
}

// ===----------------------------------------------------------------------===
// Intrinsic macro registration
// ===----------------------------------------------------------------------===

namespace {
IdentifierInfo *createIntrinsicMacro(PrepEngine &PP, llvm::StringRef Name) {
  IdentifierInfo *Id = PP.getIdentifierInfo(Name);
  MacroRecord *MI = PP.AllocMacroRecord(SourceLocation());
  MI->setIsBuiltinMacro();
  PP.appendDefMacroDirective(Id, MI);
  return Id;
}
} // namespace

void PrepEngine::InitIntrinsicMacros() {
  auto Reg = [this](llvm::StringRef Name,
                    IntrinsicMacroKind Kind) -> IdentifierInfo * {
    IdentifierInfo *II = createIntrinsicMacro(*this, Name);
    IntrinsicMacroMap[II] = Kind;
    return II;
  };

  //[MSVC Compatibility]
  if (getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat) {
    Ident__FUNCTION__ = Reg("__FUNCTION__", IntrinsicMacroKind::IMK_Function);
    Ident__LINE__ = Reg("__LINE__", IntrinsicMacroKind::IMK_Line);
  } else {
    Ident__LINE__ = Reg("__LINE__", IntrinsicMacroKind::IMK_Line);
    Ident__FUNCTION__ = Ident__LINE__;
  }
  Ident__RANDOM__NUMERIC__ =
      Reg("__RANDOM__NUMERIC__", IntrinsicMacroKind::IMK_RandomNumeric);
  Ident__FILE__ = Reg("__FILE__", IntrinsicMacroKind::IMK_File);
  Ident__DATE__ = Reg("__DATE__", IntrinsicMacroKind::IMK_Date);
  Ident__TIME__ = Reg("__TIME__", IntrinsicMacroKind::IMK_Time);
  Ident__COUNTER__ = Reg("__COUNTER__", IntrinsicMacroKind::IMK_Counter);
  Ident_Pragma = Reg("_Pragma", IntrinsicMacroKind::IMK_Pragma);
  Ident__FLT_EVAL_METHOD__ =
      Reg("__FLT_EVAL_METHOD__", IntrinsicMacroKind::IMK_FltEvalMethod);

  // GCC Extensions.
  Ident__BASE_FILE__ = Reg("__BASE_FILE__", IntrinsicMacroKind::IMK_BaseFile);
  Ident__INCLUDE_LEVEL__ =
      Reg("__INCLUDE_LEVEL__", IntrinsicMacroKind::IMK_IncludeLevel);
  Ident__TIMESTAMP__ = Reg("__TIMESTAMP__", IntrinsicMacroKind::IMK_Timestamp);

  // Microsoft Extensions.
  if (getLangOpts().MicrosoftExt) {
    Ident__identifier =
        Reg("__identifier", IntrinsicMacroKind::IMK_MsIdentifier);
    Ident__pragma = Reg("__pragma", IntrinsicMacroKind::IMK_MsPragma);
  } else {
    Ident__identifier = nullptr;
    Ident__pragma = nullptr;
  }

  // NeverC Extensions.
  Ident__FILE_NAME__ = Reg("__FILE_NAME__", IntrinsicMacroKind::IMK_FileName);
  Ident__has_feature = Reg("__has_feature", IntrinsicMacroKind::IMK_HasFeature);
  Ident__has_extension =
      Reg("__has_extension", IntrinsicMacroKind::IMK_HasExtension);
  Ident__has_builtin = Reg("__has_builtin", IntrinsicMacroKind::IMK_HasBuiltin);
  Ident__has_constexpr_builtin = Reg(
      "__has_constexpr_builtin", IntrinsicMacroKind::IMK_HasConstexprBuiltin);
  Ident__has_attribute =
      Reg("__has_attribute", IntrinsicMacroKind::IMK_HasAttribute);
  Ident__has_c_attribute =
      Reg("__has_c_attribute", IntrinsicMacroKind::IMK_HasCAttribute);

  Ident__has_declspec =
      Reg("__has_declspec_attribute", IntrinsicMacroKind::IMK_HasDeclspec);
  Ident__has_include = Reg("__has_include", IntrinsicMacroKind::IMK_HasInclude);
  Ident__has_include_next =
      Reg("__has_include_next", IntrinsicMacroKind::IMK_HasIncludeNext);
  Ident__has_warning = Reg("__has_warning", IntrinsicMacroKind::IMK_HasWarning);
  Ident__is_identifier =
      Reg("__is_identifier", IntrinsicMacroKind::IMK_IsIdentifier);
  Ident__is_target_arch =
      Reg("__is_target_arch", IntrinsicMacroKind::IMK_IsTargetArch);
  Ident__is_target_vendor =
      Reg("__is_target_vendor", IntrinsicMacroKind::IMK_IsTargetVendor);
  Ident__is_target_os =
      Reg("__is_target_os", IntrinsicMacroKind::IMK_IsTargetOS);
  Ident__is_target_environment = Reg(
      "__is_target_environment", IntrinsicMacroKind::IMK_IsTargetEnvironment);
}

// ===----------------------------------------------------------------------===
// Macro expansion
// ===----------------------------------------------------------------------===

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isTrivialSingleTokenExpansion(const MacroRecord *MI,
                                   const IdentifierInfo *MacroIdent,
                                   PrepEngine &PP) {
  IdentifierInfo *II = MI->getReplacementToken(0).getIdentifierInfo();
  if (LLVM_LIKELY(!II))
    return true;

  if (auto *ExpansionMI = PP.getMacroRecord(II))
    if (ExpansionMI->isEnabled() && II != MacroIdent)
      return false;

  if (MI->isObjectLike())
    return true;

  const auto Params = MI->params();
  const unsigned NP = Params.size();
  for (unsigned i = 0; i < NP; ++i)
    if (Params[i] == II)
      return false;
  return true;
}
} // namespace

bool PrepEngine::ProbeLeftParen() {
  unsigned Val;
  if (CurLexer)
    Val = CurLexer->probeLeftParen();
  else
    Val = CurExpansionLexer->isNextTokenLParen();

  if (LLVM_UNLIKELY(Val == 2)) {
    if (CurPPLexer)
      return false;
    for (const LexerFrame &Entry : llvm::reverse(IncludeMacroStack)) {
      if (Entry.TheLexer)
        Val = Entry.TheLexer->probeLeftParen();
      else
        Val = Entry.TheExpansionLexer->isNextTokenLParen();

      if (Val != 2)
        break;

      if (Entry.ThePPLexer)
        return false;
    }
  }

  return Val == 1;
}

__attribute__((hot)) bool
PrepEngine::BeginMacroExpansion(Token &Identifier, const MacroDefinition &M) {
  checkMacroWarnings(Identifier);

  MacroRecord *MI = M.getMacroRecord();

  if (CurPPLexer)
    CurPPLexer->MIOpt.expandedMacro();

  if (LLVM_UNLIKELY(MI->isBuiltinMacro())) {
    if (Callbacks)
      Callbacks->MacroExpands(Identifier, M, Identifier.getLocation(),
                              /*Args=*/nullptr);
    ExpandIntrinsicMacro(Identifier);
    return true;
  }

  MacroArgStorage *Args = nullptr;
  SourceLocation ExpansionEnd = Identifier.getLocation();

  if (MI->isFunctionLike()) {
    InMacroArgStorage = true;
    ArgMacro = &Identifier;

    Args = CollectMacroArgs(Identifier, MI, ExpansionEnd);

    InMacroArgStorage = false;
    ArgMacro = nullptr;

    if (!Args)
      return true;

    ++NumFnMacroExpanded;
  } else {
    ++NumMacroExpanded;
  }

  setMacroUsed(MI);

  SourceLocation ExpandLoc = Identifier.getLocation();
  SourceRange ExpansionRange(ExpandLoc, ExpansionEnd);

  if (LLVM_UNLIKELY(Callbacks != nullptr)) {
    if (LLVM_UNLIKELY(InMacroArgStorage)) {
      DelayedMacroExpandsCallbacks.push_back(
          DeferredExpansion(Identifier, M, ExpansionRange));
    } else {
      Callbacks->MacroExpands(Identifier, M, ExpansionRange, Args);
      if (LLVM_UNLIKELY(!DelayedMacroExpandsCallbacks.empty())) {
        for (const DeferredExpansion &Info : DelayedMacroExpandsCallbacks) {
          Callbacks->MacroExpands(Info.Tok, Info.MD, Info.Range,
                                  /*Args=*/nullptr);
        }
        DelayedMacroExpandsCallbacks.clear();
      }
    }
  }

  if (LLVM_UNLIKELY(M.isAmbiguous())) {
    Diag(Identifier, diag::warn_pp_ambiguous_macro)
        << Identifier.getIdentifierInfo();
    Diag(MI->getDefinitionLoc(), diag::note_pp_ambiguous_macro_chosen)
        << Identifier.getIdentifierInfo();
    M.forAllDefinitions([&](const MacroRecord *OtherMI) {
      if (OtherMI != MI)
        Diag(OtherMI->getDefinitionLoc(), diag::note_pp_ambiguous_macro_other)
            << Identifier.getIdentifierInfo();
    });
  }

  if (LLVM_UNLIKELY(MI->getNumTokens() == 0)) {
    if (Args)
      Args->destroy(*this);
    Identifier.setFlag(Token::LeadingEmptyMacro);
    CarryLineFlags(Identifier);
    ++NumFastMacroExpanded;
    return false;
  } else if (MI->getNumTokens() == 1 &&
             isTrivialSingleTokenExpansion(MI, Identifier.getIdentifierInfo(),
                                           *this)) {
    if (Args)
      Args->destroy(*this);

    bool isAtStartOfLine = Identifier.isAtStartOfLine();
    bool hasLeadingSpace = Identifier.hasLeadingSpace();

    Identifier = MI->getReplacementToken(0);

    Identifier.setFlagValue(Token::StartOfLine, isAtStartOfLine);
    Identifier.setFlagValue(Token::LeadingSpace, hasLeadingSpace);

    SourceLocation Loc =
        SourceMgr.createExpansionLoc(Identifier.getLocation(), ExpandLoc,
                                     ExpansionEnd, Identifier.getLength());
    Identifier.setLocation(Loc);

    if (IdentifierInfo *NewII = Identifier.getIdentifierInfo()) {
      if (MacroRecord *NewMI = getMacroRecord(NewII))
        if (!NewMI->isEnabled() || NewMI == MI) {
          Identifier.setFlag(Token::DisableExpand);
          if (NewMI != MI || MI->isFunctionLike())
            Diag(Identifier, diag::pp_disabled_macro_expansion);
        }
    }

    ++NumFastMacroExpanded;
    return true;
  }

  PushMacroContext(Identifier, ExpansionEnd, MI, Args);
  return false;
}

// ===----------------------------------------------------------------------===
// Argument collection
// ===----------------------------------------------------------------------===

namespace {
enum BracketKind { BK_Brace, BK_Paren };

bool areBracesBalanced(const llvm::SmallVectorImpl<Token> &Tokens) {
  const unsigned N = Tokens.size();
  if (LLVM_UNLIKELY(N == 0))
    return true;

  int ParenDepth = 0;
  int BraceDepth = 0;
  llvm::SmallVector<uint8_t, 32> NestOrder;

  const Token *Data = Tokens.data();
  unsigned i = 0;

  for (; i + 4 <= N; i += 4) {
    __builtin_prefetch(Data + i + 8, 0, 1);
    for (unsigned j = 0; j < 4; ++j) {
      const auto K = Data[i + j].getKind();
      if (LLVM_LIKELY(K != tok::l_paren && K != tok::r_paren &&
                      K != tok::l_brace && K != tok::r_brace))
        continue;
      switch (K) {
      case tok::l_paren:
        ++ParenDepth;
        NestOrder.push_back(0);
        break;
      case tok::r_paren:
        if (LLVM_UNLIKELY(NestOrder.empty() || NestOrder.back() != 0))
          return false;
        --ParenDepth;
        NestOrder.pop_back();
        break;
      case tok::l_brace:
        ++BraceDepth;
        NestOrder.push_back(1);
        break;
      case tok::r_brace:
        if (LLVM_UNLIKELY(NestOrder.empty() || NestOrder.back() != 1))
          return false;
        --BraceDepth;
        NestOrder.pop_back();
        break;
      default:
        break;
      }
    }
  }

  for (; i < N; ++i) {
    const auto K = Data[i].getKind();
    switch (K) {
    case tok::l_paren:
      ++ParenDepth;
      NestOrder.push_back(0);
      break;
    case tok::r_paren:
      if (LLVM_UNLIKELY(NestOrder.empty() || NestOrder.back() != 0))
        return false;
      --ParenDepth;
      NestOrder.pop_back();
      break;
    case tok::l_brace:
      ++BraceDepth;
      NestOrder.push_back(1);
      break;
    case tok::r_brace:
      if (LLVM_UNLIKELY(NestOrder.empty() || NestOrder.back() != 1))
        return false;
      --BraceDepth;
      NestOrder.pop_back();
      break;
    default:
      break;
    }
  }

  return (ParenDepth | BraceDepth) == 0;
}

bool rewriteBracedMacroArgs(PrepEngine &PP,
                            llvm::SmallVectorImpl<Token> &OldTokens,
                            llvm::SmallVectorImpl<Token> &NewTokens,
                            unsigned &NumArgs,
                            llvm::SmallVectorImpl<SourceRange> &ParenHints,
                            llvm::SmallVectorImpl<SourceRange> &InitLists) {
  if (!areBracesBalanced(OldTokens))
    return false;

  unsigned Braces = 0;

  llvm::SmallVectorImpl<Token>::iterator ArgStartIterator = OldTokens.begin();
  llvm::SmallVectorImpl<Token>::iterator ClosingBrace = OldTokens.end();
  NumArgs = 0;
  Token TempToken;
  bool FoundSeparatorToken = false;
  for (llvm::SmallVectorImpl<Token>::iterator I = OldTokens.begin(),
                                              E = OldTokens.end();
       I != E; ++I) {
    if (I->is(tok::l_brace)) {
      ++Braces;
    } else if (I->is(tok::r_brace)) {
      --Braces;
      if (Braces == 0 && ClosingBrace == E && FoundSeparatorToken)
        ClosingBrace = I;
    } else if (I->is(tok::eof)) {
      if (Braces != 0) {
        // Inside braces: treat separator as comma in braced list.
        FoundSeparatorToken = true;
        I->setKind(tok::comma);
        I->setLength(1);
      } else {
        ++NumArgs;

        if (FoundSeparatorToken && ArgStartIterator->is(tok::l_brace)) {
          InitLists.push_back(
              SourceRange(ArgStartIterator->getLocation(),
                          PP.getLocForEndOfToken(ClosingBrace->getLocation())));
          ClosingBrace = E;
        }

        if (FoundSeparatorToken) {
          TempToken.startToken();
          TempToken.setKind(tok::l_paren);
          TempToken.setLocation(ArgStartIterator->getLocation());
          TempToken.setLength(0);
          NewTokens.push_back(TempToken);
        }

        NewTokens.insert(NewTokens.end(), ArgStartIterator, I);

        if (FoundSeparatorToken) {
          SourceLocation Loc = PP.getLocForEndOfToken((I - 1)->getLocation());
          TempToken.startToken();
          TempToken.setKind(tok::r_paren);
          TempToken.setLocation(Loc);
          TempToken.setLength(0);
          NewTokens.push_back(TempToken);
          ParenHints.push_back(
              SourceRange(ArgStartIterator->getLocation(), Loc));
        }

        NewTokens.push_back(*I);
        ArgStartIterator = I + 1;
        FoundSeparatorToken = false;
      }
    }
  }

  return !ParenHints.empty() && InitLists.empty();
}
} // namespace

MacroArgStorage *PrepEngine::CollectMacroArgs(Token &MacroName, MacroRecord *MI,
                                              SourceLocation &MacroEnd) {
  unsigned NumFixedArgsLeft = MI->getNumParams();
  const bool isVariadic = MI->isVariadic();
  const bool MSVCMode = getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat;

  Token Tok;
  LexWithoutExpansion(Tok);
  assert(Tok.is(tok::l_paren) && "Error computing l-paren-ness?");

  llvm::SmallVector<Token, 64> ArgTokens;
  ArgTokens.reserve(NumFixedArgsLeft * 8);
  bool FoundElidedComma = false;

  SourceLocation TooManyArgsLoc;

  unsigned NumActuals = 0;
  while (Tok.isNot(tok::r_paren)) {
    assert(Tok.isOneOf(tok::l_paren, tok::comma) &&
           "only expect argument separators here");

    size_t ArgTokenStart = ArgTokens.size();
    SourceLocation ArgStartLoc = Tok.getLocation();

    unsigned NumParens = 0;

    while (true) {
      LexWithoutExpansion(Tok);

      if (LLVM_UNLIKELY(Tok.isOneOf(tok::eof, tok::eod))) {
        Diag(MacroName, diag::err_unterm_macro_invoc);
        Diag(MI->getDefinitionLoc(), diag::note_macro_here)
            << MacroName.getIdentifierInfo();
        MacroName = Tok;
        return nullptr;
      } else if (Tok.is(tok::r_paren)) {
        if (NumParens-- == 0) {
          MacroEnd = Tok.getLocation();
          if (!ArgTokens.empty() && ArgTokens.back().commaAfterElided()) {
            FoundElidedComma = true;
          }
          break;
        }
      } else if (Tok.is(tok::l_paren)) {
        ++NumParens;
      } else if (Tok.is(tok::comma)) {
        if (Tok.getFlags() & Token::IgnoredComma) {
          // MSVC compat: IgnoredComma suppresses separation once.
          Tok.clearFlag(Token::IgnoredComma);
        } else if (NumParens == 0) {
          if (!isVariadic)
            break;
          if (NumFixedArgsLeft > 1)
            break;
        }
      } else if (Tok.is(tok::comment) && !KeepMacroComments) {
        continue;
      } else if (!Tok.isAnnotation() && Tok.getIdentifierInfo() != nullptr) {
        // Mark tokens referencing disabled macros to prevent re-expansion.
        if (MacroRecord *MI = getMacroRecord(Tok.getIdentifierInfo()))
          if (!MI->isEnabled())
            Tok.setFlag(Token::DisableExpand);
      }

      ArgTokens.push_back(Tok);
    }

    if (ArgTokens.empty() && Tok.getKind() == tok::r_paren)
      break;

    if (!isVariadic && NumFixedArgsLeft == 0 && TooManyArgsLoc.isInvalid()) {
      if (ArgTokens.size() != ArgTokenStart)
        TooManyArgsLoc = ArgTokens[ArgTokenStart].getLocation();
      else
        TooManyArgsLoc = ArgStartLoc;
    }

    if (ArgTokens.size() == ArgTokenStart && !getLangOpts().C99)
      Diag(Tok, diag::ext_empty_fnmacro_arg);

    Token EOFTok;
    EOFTok.startToken();
    EOFTok.setKind(tok::eof);
    EOFTok.setLocation(Tok.getLocation());
    EOFTok.setLength(0);
    ArgTokens.push_back(EOFTok);
    ++NumActuals;
    if (NumFixedArgsLeft != 0)
      --NumFixedArgsLeft;
  }

  unsigned MinArgsExpected = MI->getNumParams();

  if (!isVariadic && NumActuals > MinArgsExpected) {
    if (MSVCMode) {
      Diag(TooManyArgsLoc, diag::ext_too_many_args_in_macro_invoc_msvc)
          << MacroName.getIdentifierInfo();
      NumActuals = MinArgsExpected;
    } else {
      Diag(TooManyArgsLoc, diag::err_too_many_args_in_macro_invoc);
      Diag(MI->getDefinitionLoc(), diag::note_macro_here)
          << MacroName.getIdentifierInfo();

      llvm::SmallVector<Token, 4> FixedArgTokens;
      unsigned FixedNumArgs = 0;
      llvm::SmallVector<SourceRange, 4> ParenHints, InitLists;
      if (!rewriteBracedMacroArgs(*this, ArgTokens, FixedArgTokens,
                                  FixedNumArgs, ParenHints, InitLists)) {
        if (!InitLists.empty()) {
          DiagnosticBuilder DB = Diag(
              MacroName, diag::note_init_list_at_beginning_of_macro_argument);
          for (SourceRange Range : InitLists)
            DB << Range;
        }
        return nullptr;
      }
      if (FixedNumArgs != MinArgsExpected)
        return nullptr;

      DiagnosticBuilder DB =
          Diag(MacroName, diag::note_suggest_parens_for_macro);
      for (SourceRange ParenLocation : ParenHints) {
        DB << FixItHint::CreateInsertion(ParenLocation.getBegin(), "(");
        DB << FixItHint::CreateInsertion(ParenLocation.getEnd(), ")");
      }
      ArgTokens.swap(FixedArgTokens);
      NumActuals = FixedNumArgs;
    }
  }

  bool isVarargsElided = false;

  if (NumActuals < MinArgsExpected) {
    if (NumActuals == 0 && MinArgsExpected == 1) {
      isVarargsElided = MI->isVariadic();
    } else if ((FoundElidedComma || MI->isVariadic()) &&
               (NumActuals + 1 == MinArgsExpected ||
                (NumActuals == 0 && MinArgsExpected == 2))) {
      if (!MI->hasCommaPasting()) {
        Diag(Tok, diag::ext_missing_varargs_arg);
        Diag(MI->getDefinitionLoc(), diag::note_macro_here)
            << MacroName.getIdentifierInfo();
      }

      isVarargsElided = true;
    } else {
      Diag(Tok, diag::err_too_few_args_in_macro_invoc);
      Diag(MI->getDefinitionLoc(), diag::note_macro_here)
          << MacroName.getIdentifierInfo();
      return nullptr;
    }

    SourceLocation EndLoc = Tok.getLocation();
    Tok.startToken();
    Tok.setKind(tok::eof);
    Tok.setLocation(EndLoc);
    Tok.setLength(0);
    ArgTokens.push_back(Tok);

    if (NumActuals == 0 && MinArgsExpected == 2)
      ArgTokens.push_back(Tok);

  } else if (NumActuals > MinArgsExpected && !MI->isVariadic()) {
    Diag(MacroName, diag::err_too_many_args_in_macro_invoc);
    Diag(MI->getDefinitionLoc(), diag::note_macro_here)
        << MacroName.getIdentifierInfo();
    return nullptr;
  }

  return MacroArgStorage::create(MI, ArgTokens, isVarargsElided, *this);
}

Token *PrepEngine::stashExpandedTokens(ExpansionLexer *tokLexer,
                                       llvm::ArrayRef<Token> tokens) {
  assert(tokLexer);
  if (tokens.empty())
    return nullptr;

  size_t newIndex = MacroExpandedTokens.size();
  bool cacheNeedsToGrow = tokens.size() > MacroExpandedTokens.capacity() -
                                              MacroExpandedTokens.size();
  MacroExpandedTokens.append(tokens.begin(), tokens.end());

  if (cacheNeedsToGrow) {
    // Go through all the ExpansionLexers whose 'Tokens' pointer points in the
    // buffer and update the pointers to the (potential) new buffer array.
    for (const auto &Lexer : MacroExpandingLexersStack) {
      ExpansionLexer *prevLexer;
      size_t tokIndex;
      std::tie(prevLexer, tokIndex) = Lexer;
      prevLexer->Tokens = MacroExpandedTokens.data() + tokIndex;
    }
  }

  MacroExpandingLexersStack.push_back(std::make_pair(tokLexer, newIndex));
  return MacroExpandedTokens.data() + newIndex;
}

void PrepEngine::unstashLastLexerTokens() {
  assert(!MacroExpandingLexersStack.empty());
  size_t tokIndex = MacroExpandingLexersStack.back().second;
  assert(tokIndex < MacroExpandedTokens.size());
  MacroExpandedTokens.resize(tokIndex);
  MacroExpandingLexersStack.pop_back();
}
