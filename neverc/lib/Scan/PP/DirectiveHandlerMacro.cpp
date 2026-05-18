#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/VarArgExpansion.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;

namespace {
bool isIntrinsicLangMacro(const SourceManager &SourceMgr, const MacroRecord *MI,
                          const llvm::StringRef MacroName) {
  if (MI->isBuiltinMacro())
    return true;
  if (!SourceMgr.isWrittenInBuiltinFile(MI->getDefinitionLoc()))
    return false;
  return MacroName.starts_with("__STDC");
}
} // namespace

// ===----------------------------------------------------------------------===
// Macro directive handling
// ===----------------------------------------------------------------------===

bool PrepEngine::ParseMacroParamList(MacroRecord *MI, Token &Tok) {
  llvm::SmallVector<IdentifierInfo *, 32> Parameters;

  while (true) {
    LexRawSkipComments(Tok);
    switch (Tok.getKind()) {
    case tok::r_paren:
      if (Parameters.empty())
        return false;
      Diag(Tok, diag::err_pp_expected_ident_in_arg_list);
      return true;
    case tok::ellipsis:
      if (!LangOpts.C99)
        Diag(Tok, diag::ext_variadic_macro);
      LexRawSkipComments(Tok);
      if (Tok.isNot(tok::r_paren)) {
        Diag(Tok, diag::err_pp_missing_rparen_in_macro_def);
        return true;
      }
      Parameters.push_back(Ident__VA_ARGS__);
      MI->setIsC99Varargs();
      MI->setParameterList(Parameters, BP);
      return false;
    case tok::eod:
      Diag(Tok, diag::err_pp_missing_rparen_in_macro_def);
      return true;
    default:
      IdentifierInfo *II = Tok.getIdentifierInfo();
      if (!II) {
        Diag(Tok, diag::err_pp_invalid_tok_in_arg_list);
        return true;
      }

      if (llvm::is_contained(Parameters, II)) {
        Diag(Tok, diag::err_pp_duplicate_name_in_arg_list) << II;
        return true;
      }

      Parameters.push_back(II);
      LexRawSkipComments(Tok);

      switch (Tok.getKind()) {
      default:
        Diag(Tok, diag::err_pp_expected_comma_in_arg_list);
        return true;
      case tok::r_paren:
        MI->setParameterList(Parameters, BP);
        return false;
      case tok::comma:
        break;
      case tok::ellipsis:
        Diag(Tok, diag::ext_named_variadic_macro);
        LexRawSkipComments(Tok);
        if (Tok.isNot(tok::r_paren)) {
          Diag(Tok, diag::err_pp_missing_rparen_in_macro_def);
          return true;
        }

        MI->setIsGNUVarargs();
        MI->setParameterList(Parameters, BP);
        return false;
      }
    }
  }
}

namespace {
bool isKeywordRedefinition(Token &MacroName, MacroRecord *MI,
                           const LangOptions &LOptions) {
  if (MI->getNumTokens() == 1) {
    const Token &Value = MI->getReplacementToken(0);

    if (MacroName.getKind() == Value.getKind())
      return true;

    // Keyword remapped to underscore-decorated variant (e.g. inline ->
    // __inline).
    llvm::StringRef MacroText = MacroName.getIdentifierInfo()->getName();
    if (IdentifierInfo *II = Value.getIdentifierInfo()) {
      if (!II->isKeyword(LOptions))
        return false;
      llvm::StringRef ValueText = II->getName();
      llvm::StringRef TrimmedValue = ValueText;
      if (!ValueText.starts_with("__")) {
        if (ValueText.starts_with("_"))
          TrimmedValue = TrimmedValue.drop_front(1);
        else
          return false;
      } else {
        TrimmedValue = TrimmedValue.drop_front(2);
        if (TrimmedValue.ends_with("__"))
          TrimmedValue = TrimmedValue.drop_back(2);
      }
      return TrimmedValue.equals(MacroText);
    } else {
      return false;
    }
  }

  // #define inline
  return MacroName.isOneOf(tok::kw_extern, tok::kw_inline, tok::kw_static,
                           tok::kw_const) &&
         MI->getNumTokens() == 0;
}
} // namespace

MacroRecord *
PrepEngine::ParseMacroBody(const Token &MacroNameTok,
                           const bool ImmediatelyAfterHeaderGuard) {

  Token LastTok = MacroNameTok;
  MacroRecord *const MI = AllocMacroRecord(MacroNameTok.getLocation());

  Token Tok;
  LexWithoutExpansion(Tok);

  auto _ = llvm::make_scope_exit([&]() {
    if (CurLexer->ParsingDirective)
      DiscardDirectiveLine();
  });

  VarArgScopeGuard VarArgScopeGuard(*this);

  if (Tok.is(tok::eod)) {
    if (ImmediatelyAfterHeaderGuard) {
      CurPPLexer->MIOpt.setDefinedMacro(MacroNameTok.getIdentifierInfo(),
                                        MacroNameTok.getLocation());
    }
  } else if (Tok.hasLeadingSpace()) {
    Tok.clearFlag(Token::LeadingSpace);
  } else if (Tok.is(tok::l_paren)) {
    MI->setIsFunctionLike();
    if (ParseMacroParamList(MI, LastTok))
      return nullptr;

    if (MI->isC99Varargs()) {
      VarArgScopeGuard.enterScope();
    }

    LexWithoutExpansion(Tok);
  } else if (LangOpts.C99) {
    Diag(Tok, diag::ext_c99_whitespace_required_after_macro_name);
  } else {
    if (Tok.is(tok::unknown))
      Diag(Tok, diag::ext_missing_whitespace_after_macro_name);
    else
      Diag(Tok, diag::warn_missing_whitespace_after_macro_name);
  }

  if (!Tok.is(tok::eod))
    LastTok = Tok;

  llvm::SmallVector<Token, 16> Tokens;

  if (MI->isObjectLike()) {
    while (Tok.isNot(tok::eod)) {
      LastTok = Tok;
      Tokens.push_back(Tok);
      LexWithoutExpansion(Tok);
    }
  } else {
    VAOptDefinitionContext VAOCtx(*this);

    while (Tok.isNot(tok::eod)) {
      LastTok = Tok;

      if (!Tok.isOneOf(tok::hash, tok::hashat, tok::hashhash)) {
        Tokens.push_back(Tok);

        if (VAOCtx.isVAOptToken(Tok)) {
          if (VAOCtx.isInVAOpt()) {
            Diag(Tok, diag::err_pp_vaopt_nested_use);
            return nullptr;
          }
          LexWithoutExpansion(Tok);
          if (Tok.isNot(tok::l_paren)) {
            Diag(Tok, diag::err_pp_missing_lparen_in_vaopt_use);
            return nullptr;
          }
          Tokens.push_back(Tok);
          VAOCtx.sawVAOptFollowedByOpeningParens(Tok.getLocation());
          LexWithoutExpansion(Tok);
          if (Tok.is(tok::hashhash)) {
            Diag(Tok, diag::err_vaopt_paste_at_start);
            return nullptr;
          }
          continue;
        } else if (VAOCtx.isInVAOpt()) {
          if (Tok.is(tok::r_paren)) {
            if (VAOCtx.sawClosingParen()) {
              assert(Tokens.size() >= 3 &&
                     "Must have seen at least __VA_OPT__( "
                     "and a subsequent tok::r_paren");
              if (Tokens[Tokens.size() - 2].is(tok::hashhash)) {
                Diag(Tok, diag::err_vaopt_paste_at_end);
                return nullptr;
              }
            }
          } else if (Tok.is(tok::l_paren)) {
            VAOCtx.sawOpeningParen(Tok.getLocation());
          }
        }
        LexWithoutExpansion(Tok);
        continue;
      }

      // Traditional CPP: no stringification or pasting.
      if (getLangOpts().TraditionalCPP) {
        Tok.setKind(tok::unknown);
        Tokens.push_back(Tok);
        LexWithoutExpansion(Tok);
        continue;
      }

      if (Tok.is(tok::hashhash)) {
        LexWithoutExpansion(Tok);

        if (Tok.is(tok::eod)) {
          Tokens.push_back(LastTok);
          break;
        }

        if (!Tokens.empty() && Tok.getIdentifierInfo() == Ident__VA_ARGS__ &&
            Tokens[Tokens.size() - 1].is(tok::comma))
          MI->setHasCommaPasting();

        Tokens.push_back(LastTok);
        continue;
      }

      // Stringization: # must precede a macro parameter or __VA_OPT__.
      LexWithoutExpansion(Tok);
      if (!VAOCtx.isVAOptToken(Tok) &&
          (Tok.getIdentifierInfo() == nullptr ||
           MI->getParameterNum(Tok.getIdentifierInfo()) == -1)) {

        // In asm mode, '#' is often a comment char; downgrade to unknown.
        if (getLangOpts().AsmPreprocessor && Tok.isNot(tok::eod)) {
          LastTok.setKind(tok::unknown);
          Tokens.push_back(LastTok);
          continue;
        } else {
          Diag(Tok, diag::err_pp_stringize_not_parameter)
              << LastTok.is(tok::hashat);
          return nullptr;
        }
      }

      Tokens.push_back(LastTok);

      if (!VAOCtx.isVAOptToken(Tok)) {
        Tokens.push_back(Tok);
        LastTok = Tok;
        LexWithoutExpansion(Tok);
      }
    }
    if (VAOCtx.isInVAOpt()) {
      assert(Tok.is(tok::eod) && "Must be at End Of preprocessing Directive");
      Diag(Tok, diag::err_pp_expected_after)
          << LastTok.getKind() << tok::r_paren;
      Diag(VAOCtx.getUnmatchedOpeningParenLoc(), diag::note_matching)
          << tok::l_paren;
      return nullptr;
    }
  }
  MI->setDefinitionEndLoc(LastTok.getLocation());

  MI->setTokens(Tokens, BP);
  return MI;
}

// ===----------------------------------------------------------------------===
// Macro definition & conditional directives
// ===----------------------------------------------------------------------===

void PrepEngine::ExecDefine(Token &DefineTok,
                            const bool ImmediatelyAfterHeaderGuard) {
  ++NumDefined;

  Token MacroNameTok;
  bool MacroShadowsKeyword;
  ValidateMacroNameTok(MacroNameTok, MU_Define, &MacroShadowsKeyword);

  if (MacroNameTok.is(tok::eod))
    return;

  IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
  if (LLVM_UNLIKELY(!II->hasMacroDefinition() && II->hadMacroDefinition() &&
                    II->isFinal()))
    warnFinalMacro(MacroNameTok, /*IsUndef=*/false);

  if (CurLexer)
    CurLexer->SetCommentRetentionState(KeepMacroComments);

  MacroRecord *const MI =
      ParseMacroBody(MacroNameTok, ImmediatelyAfterHeaderGuard);

  if (!MI)
    return;

  if (MacroShadowsKeyword &&
      !isKeywordRedefinition(MacroNameTok, MI, getLangOpts())) {
    Diag(MacroNameTok, diag::warn_pp_macro_hides_keyword);
  }
  unsigned NumTokens = MI->getNumTokens();
  if (NumTokens != 0) {
    if (MI->getReplacementToken(0).is(tok::hashhash)) {
      Diag(MI->getReplacementToken(0), diag::err_paste_at_start);
      return;
    }
    if (MI->getReplacementToken(NumTokens - 1).is(tok::hashhash)) {
      Diag(MI->getReplacementToken(NumTokens - 1), diag::err_paste_at_end);
      return;
    }
  }

  if (const MacroRecord *OtherMI = getMacroRecord(II)) {
    if (II->isFinal())
      warnFinalMacro(MacroNameTok, /*IsUndef=*/false);

    if (!getDiagnostics().getSuppressSystemWarnings() ||
        !SourceMgr.isInSystemHeader(DefineTok.getLocation())) {

      if (!OtherMI->isUsed() && OtherMI->isWarnIfUnused())
        Diag(OtherMI->getDefinitionLoc(), diag::pp_macro_not_used);

      if (isIntrinsicLangMacro(SourceMgr, OtherMI, II->getName()))
        Diag(MacroNameTok, diag::ext_pp_redef_builtin_macro);
      else if (!OtherMI->isAllowRedefinitionsWithoutWarning() &&
               !MI->isIdenticalTo(*OtherMI, *this,
                                  /*Syntactic=*/LangOpts.MicrosoftExt)) {
        Diag(MI->getDefinitionLoc(), diag::ext_pp_macro_redef) << II;
        Diag(OtherMI->getDefinitionLoc(), diag::note_previous_definition);
      }
    }
    if (OtherMI->isWarnIfUnused())
      WarnUnusedMacroLocs.erase(OtherMI->getDefinitionLoc());
  }

  DefMacroDirective *MD = appendDefMacroDirective(II, MI);

  assert(!MI->isUsed());
  if (getSourceManager().isInMainFile(MI->getDefinitionLoc()) &&
      !Diags->isIgnored(diag::pp_macro_not_used, MI->getDefinitionLoc()) &&
      !MacroExpansionInDirectivesOverride &&
      getSourceManager().getFileID(MI->getDefinitionLoc()) !=
          getPredefinesFileID()) {
    MI->setIsWarnIfUnused(true);
    WarnUnusedMacroLocs.insert(MI->getDefinitionLoc());
  }

  if (Callbacks)
    Callbacks->MacroDefined(MacroNameTok, MD);

  if (getLangOpts().MSVCCompat && II->isStr("assert")) {
    const char *StaticAssertKW = tok::getKeywordSpelling(tok::kw_static_assert);
    if (!isMacroDefined(StaticAssertKW)) {
      MacroRecord *MI = AllocMacroRecord(SourceLocation());

      const char *UnderscoreStaticAssertKW =
          tok::getKeywordSpelling(tok::kw__Static_assert);
      IdentifierInfo *UnderscoreII =
          getIdentifierInfo(UnderscoreStaticAssertKW);

      Token Tok;
      Tok.startToken();
      Tok.setKind(tok::kw__Static_assert);
      Tok.setIdentifierInfo(UnderscoreII);
      MI->setTokens({Tok}, BP);
      (void)appendDefMacroDirective(getIdentifierInfo(StaticAssertKW), MI);
    }
  }
}

void PrepEngine::ExecUndef() {
  ++NumUndefined;

  Token MacroNameTok;
  ValidateMacroNameTok(MacroNameTok, MU_Undef);

  if (MacroNameTok.is(tok::eod))
    return;
  VerifyDirectiveEnd(tok::getPPKeywordSpelling(tok::pp_undef));

  // Okay, we have a valid identifier to undef.
  auto *II = MacroNameTok.getIdentifierInfo();
  auto MD = getMacroDefinition(II);
  UndefMacroDirective *Undef = nullptr;

  if (II->isFinal())
    warnFinalMacro(MacroNameTok, /*IsUndef=*/true);

  // If the macro is not defined, this is a noop undef.
  if (const MacroRecord *MI = MD.getMacroRecord()) {
    if (!MI->isUsed() && MI->isWarnIfUnused())
      Diag(MI->getDefinitionLoc(), diag::pp_macro_not_used);

    // Warn on undef of builtin macros, but allow as extension.
    if (isIntrinsicLangMacro(SourceMgr, MI, II->getName()))
      Diag(MacroNameTok, diag::ext_pp_undef_builtin_macro);

    if (MI->isWarnIfUnused())
      WarnUnusedMacroLocs.erase(MI->getDefinitionLoc());

    Undef = AllocUndefDirective(MacroNameTok.getLocation());
  }

  // If the callbacks want to know, tell them about the macro #undef.
  // Note: no matter if the macro was defined or not.
  if (Callbacks)
    Callbacks->MacroUndefined(MacroNameTok, MD, Undef);

  if (Undef)
    appendMacroDirective(II, Undef);
}
