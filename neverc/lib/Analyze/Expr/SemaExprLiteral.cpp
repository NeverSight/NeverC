//===- SemaExprLiteral.cpp - Literals, identifiers & generic selection ---===//
//
// Extracted from SemaExpr.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Analyze/SemaPluginHooks.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Literals, identifiers & generic selection
// ===----------------------------------------------------------------------===

ExprResult Sema::OnGenericSelectionExpr(
    SourceLocation KeyLoc, SourceLocation DefaultLoc, SourceLocation RParenLoc,
    bool PredicateIsExpr, void *ControllingExprOrType,
    llvm::ArrayRef<ParsedType> ArgTypes, llvm::ArrayRef<Expr *> ArgExprs) {
  unsigned NumAssocs = ArgTypes.size();
  assert(NumAssocs == ArgExprs.size());

  TypeSourceInfo **Types = new TypeSourceInfo *[NumAssocs];
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (ArgTypes[i])
      (void)GetTypeFromParser(ArgTypes[i], &Types[i]);
    else
      Types[i] = nullptr;
  }

  // If we have a controlling type, we need to convert it from a parsed type
  // into a semantic type and then pass that along.
  if (!PredicateIsExpr) {
    TypeSourceInfo *ControllingType;
    (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(ControllingExprOrType),
                            &ControllingType);
    assert(ControllingType && "couldn't get the type out of the parser");
    ControllingExprOrType = ControllingType;
  }

  ExprResult ER = CreateGenericSelectionExpr(
      KeyLoc, DefaultLoc, RParenLoc, PredicateIsExpr, ControllingExprOrType,
      llvm::ArrayRef(Types, NumAssocs), ArgExprs);
  delete[] Types;
  return ER;
}

ExprResult Sema::CreateGenericSelectionExpr(
    SourceLocation KeyLoc, SourceLocation DefaultLoc, SourceLocation RParenLoc,
    bool PredicateIsExpr, void *ControllingExprOrType,
    llvm::ArrayRef<TypeSourceInfo *> Types, llvm::ArrayRef<Expr *> Exprs) {
  unsigned NumAssocs = Types.size();
  assert(NumAssocs == Exprs.size());
  assert(ControllingExprOrType &&
         "Must have either a controlling expression or a controlling type");

  Expr *ControllingExpr = nullptr;
  TypeSourceInfo *ControllingType = nullptr;
  if (PredicateIsExpr) {
    // Decay and strip qualifiers for the controlling expression type, and
    // handle placeholder type replacement. See committee discussion from WG14
    // DR423.
    EnterExpressionEvaluationContext Unevaluated(
        *this, Sema::ExpressionEvaluationContext::Unevaluated);
    ExprResult R = DefaultFunctionArrayLvalueConversion(
        reinterpret_cast<Expr *>(ControllingExprOrType));
    if (R.isInvalid())
      return ExprError();
    ControllingExpr = R.get();
  } else {
    // The extension form uses the type directly rather than converting it.
    ControllingType = reinterpret_cast<TypeSourceInfo *>(ControllingExprOrType);
    if (!ControllingType)
      return ExprError();
  }

  bool TypeErrorFound = false;

  if (ControllingExpr && ControllingExpr->HasSideEffects(Context, false))
    Diag(ControllingExpr->getExprLoc(),
         diag::warn_side_effects_unevaluated_context);

  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (Types[i]) {
      // Generic association type must be a complete, non-VLA object type.
      unsigned D = 0;
      if (ControllingExpr && Types[i]->getType()->isIncompleteType())
        D = diag::err_assoc_type_incomplete;
      else if (ControllingExpr && !Types[i]->getType()->isObjectType())
        D = diag::err_assoc_type_nonobject;
      else if (Types[i]->getType()->isVariablyModifiedType())
        D = diag::err_assoc_type_variably_modified;
      else if (ControllingExpr) {
        unsigned Reason = 0;
        QualType QT = Types[i]->getType();
        if (QT->isArrayType())
          Reason = 1;
        else if (QT.hasQualifiers())
          Reason = 2;

        if (Reason)
          Diag(Types[i]->getTypeLoc().getBeginLoc(),
               diag::warn_unreachable_association)
              << QT << (Reason - 1);
      }

      if (D != 0) {
        Diag(Types[i]->getTypeLoc().getBeginLoc(), D)
            << Types[i]->getTypeLoc().getSourceRange() << Types[i]->getType();
        TypeErrorFound = true;
      }

      // No two generic associations may have compatible types.
      for (unsigned j = i + 1; j < NumAssocs; ++j)
        if (Types[j] && Context.typesAreCompatible(Types[i]->getType(),
                                                   Types[j]->getType())) {
          Diag(Types[j]->getTypeLoc().getBeginLoc(),
               diag::err_assoc_compatible_types)
              << Types[j]->getTypeLoc().getSourceRange() << Types[j]->getType()
              << Types[i]->getType();
          Diag(Types[i]->getTypeLoc().getBeginLoc(), diag::note_compat_assoc)
              << Types[i]->getTypeLoc().getSourceRange() << Types[i]->getType();
          TypeErrorFound = true;
        }
    }
  }
  if (TypeErrorFound)
    return ExprError();

  llvm::SmallVector<unsigned, 1> CompatIndices;
  unsigned DefaultIndex = -1U;
  // Look at the canonical type of the controlling expression in case it was a
  // deduced type like __auto_type. However, when issuing diagnostics, use the
  // type the user wrote in source rather than the canonical one.
  for (unsigned i = 0; i < NumAssocs; ++i) {
    if (!Types[i])
      DefaultIndex = i;
    else if (ControllingExpr &&
             Context.typesAreCompatible(
                 ControllingExpr->getType().getCanonicalType(),
                 Types[i]->getType()))
      CompatIndices.push_back(i);
    else if (ControllingType &&
             Context.typesAreCompatible(
                 ControllingType->getType().getCanonicalType(),
                 Types[i]->getType()))
      CompatIndices.push_back(i);
  }

  auto GetControllingRangeAndType = [](Expr *ControllingExpr,
                                       TypeSourceInfo *ControllingType) {
    // We strip parens here because the controlling expression is typically
    // parenthesized in macro definitions.
    if (ControllingExpr)
      ControllingExpr = ControllingExpr->IgnoreParens();

    SourceRange SR = ControllingExpr
                         ? ControllingExpr->getSourceRange()
                         : ControllingType->getTypeLoc().getSourceRange();
    QualType QT = ControllingExpr ? ControllingExpr->getType()
                                  : ControllingType->getType();

    return std::make_pair(SR, QT);
  };

  // Controlling expression must match at most one generic association type.
  if (CompatIndices.size() > 1) {
    auto P = GetControllingRangeAndType(ControllingExpr, ControllingType);
    SourceRange SR = P.first;
    Diag(SR.getBegin(), diag::err_generic_sel_multi_match)
        << SR << P.second << (unsigned)CompatIndices.size();
    for (unsigned I : CompatIndices) {
      Diag(Types[I]->getTypeLoc().getBeginLoc(), diag::note_compat_assoc)
          << Types[I]->getTypeLoc().getSourceRange() << Types[I]->getType();
    }
    return ExprError();
  }

  // Without a default, the controlling expression must match exactly one type.
  if (DefaultIndex == -1U && CompatIndices.size() == 0) {
    if ((ControllingExpr && ControllingExpr->containsErrors()) ||
        (ControllingType && ControllingType->getType()->containsErrors()))
      return ExprError();
    auto P = GetControllingRangeAndType(ControllingExpr, ControllingType);
    SourceRange SR = P.first;
    Diag(SR.getBegin(), diag::err_generic_sel_no_match) << SR << P.second;
    return ExprError();
  }

  // Result is the matching association, or the default if none matches.
  unsigned ResultIndex = CompatIndices.size() ? CompatIndices[0] : DefaultIndex;

  if (ControllingExpr) {
    return GenericSelectionExpr::Create(Context, KeyLoc, ControllingExpr, Types,
                                        Exprs, DefaultLoc, RParenLoc,
                                        ResultIndex);
  }
  return GenericSelectionExpr::Create(Context, KeyLoc, ControllingType, Types,
                                      Exprs, DefaultLoc, RParenLoc,
                                      ResultIndex);
}

namespace {
PredefinedIdentKind getPredefinedExprKind(tok::TokenKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("unexpected TokenKind");
  case tok::kw___func__:
    return PredefinedIdentKind::Func; // [C99 6.4.2.2]
  case tok::kw___FUNCTION__:
    return PredefinedIdentKind::Function;
  case tok::kw___FUNCDNAME__:
    return PredefinedIdentKind::FuncDName; // [MS]
  case tok::kw___FUNCSIG__:
    return PredefinedIdentKind::FuncSig; // [MS]
  case tok::kw_L__FUNCTION__:
    return PredefinedIdentKind::LFunction; // [MS]
  case tok::kw_L__FUNCSIG__:
    return PredefinedIdentKind::LFuncSig; // [MS]
  case tok::kw___PRETTY_FUNCTION__:
    return PredefinedIdentKind::PrettyFunction; // [GNU]
  }
}
} // namespace

namespace {
Decl *getPredefinedExprDecl(DeclContext *DC) {
  while (DC && !isa<FunctionDecl>(DC))
    DC = DC->getParent();
  return cast_or_null<Decl>(DC);
}
} // namespace

ExprResult Sema::OnUnevaluatedStringLiteral(llvm::ArrayRef<Token> StringToks) {
  // StringToks needs backing storage as it doesn't hold array elements itself
  std::vector<Token> ExpandedToks;
  if (getLangOpts().MicrosoftExt)
    StringToks = ExpandedToks = ExpandFunctionLocalPredefinedMacros(StringToks);

  StringLiteralParser Literal(StringToks, PP,
                              StringLiteralEvalMethod::Unevaluated);
  if (Literal.hadError)
    return ExprError();

  llvm::SmallVector<SourceLocation, 4> StringTokLocs;
  for (const Token &Tok : StringToks)
    StringTokLocs.push_back(Tok.getLocation());

  StringLiteral *Lit = StringLiteral::Create(
      Context, Literal.getString(), StringLiteralKind::Unevaluated, {},
      &StringTokLocs[0], StringTokLocs.size());

  return Lit;
}

std::vector<Token>
Sema::ExpandFunctionLocalPredefinedMacros(llvm::ArrayRef<Token> Toks) {
  // MSVC treats some predefined identifiers (e.g. __FUNCTION__) as function
  // local macros that expand to string literals that may be concatenated.
  // These macros are expanded here (in Sema), because StringLiteralParser
  // (in Lex) doesn't know the enclosing function (because it hasn't been
  // parsed yet).
  assert(getLangOpts().MicrosoftExt);

  // Note: Although function local macros are defined only inside functions,
  // we ensure a valid `CurrentDecl` even outside of a function. This allows
  // expansion of macros into empty string literals without additional checks.
  Decl *CurrentDecl = getPredefinedExprDecl(CurContext);
  if (!CurrentDecl)
    CurrentDecl = Context.getTranslationUnitDecl();

  std::vector<Token> ExpandedToks;
  ExpandedToks.reserve(Toks.size());
  for (const Token &Tok : Toks) {
    if (!isFunctionLocalStringLiteralMacro(Tok.getKind(), getLangOpts())) {
      assert(tok::isStringLiteral(Tok.getKind()));
      ExpandedToks.emplace_back(Tok);
      continue;
    }
    if (isa<TranslationUnitDecl>(CurrentDecl))
      Diag(Tok.getLocation(), diag::ext_predef_outside_function);
    // Escape predefined expression to string literal.
    Diag(Tok.getLocation(), diag::ext_string_literal_from_predefined)
        << Tok.getKind();
    llvm::SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    Token &Exp = ExpandedToks.emplace_back();
    Exp.startToken();
    if (Tok.getKind() == tok::kw_L__FUNCTION__ ||
        Tok.getKind() == tok::kw_L__FUNCSIG__) {
      OS << 'L';
      Exp.setKind(tok::wide_string_literal);
    } else {
      Exp.setKind(tok::string_literal);
    }
    OS << '"'
       << SourceScanner::escapeStringLiteral(PredefinedExpr::ComputeName(
              getPredefinedExprKind(Tok.getKind()), CurrentDecl))
       << '"';
    PP.WriteScratch(OS.str(), Exp, Tok.getLocation(), Tok.getEndLoc());
  }
  return ExpandedToks;
}

ExprResult Sema::OnStringLiteral(llvm::ArrayRef<Token> StringToks) {
  assert(!StringToks.empty() && "Must have at least one string!");

  // StringToks needs backing storage as it doesn't hold array elements itself
  std::vector<Token> ExpandedToks;
  if (getLangOpts().MicrosoftExt)
    StringToks = ExpandedToks = ExpandFunctionLocalPredefinedMacros(StringToks);

  StringLiteralParser Literal(StringToks, PP);
  if (Literal.hadError)
    return ExprError();

  llvm::SmallVector<SourceLocation, 4> StringTokLocs;
  for (const Token &Tok : StringToks)
    StringTokLocs.push_back(Tok.getLocation());

  QualType CharTy = Context.CharTy;
  StringLiteralKind Kind = StringLiteralKind::Ordinary;
  if (Literal.isWide()) {
    CharTy = Context.getWideCharType();
    Kind = StringLiteralKind::Wide;
  } else if (Literal.isUTF8()) {
    if (getLangOpts().Char8)
      CharTy = Context.Char8Ty;
    Kind = StringLiteralKind::UTF8;
  } else if (Literal.isUTF16()) {
    CharTy = Context.Char16Ty;
    Kind = StringLiteralKind::UTF16;
  } else if (Literal.isUTF32()) {
    CharTy = Context.Char32Ty;
    Kind = StringLiteralKind::UTF32;
  }

  QualType StrTy =
      Context.getStringLiteralArrayType(CharTy, Literal.getNumStringChars());

  // Pass &StringTokLocs[0], StringTokLocs.size() to factory!
  StringLiteral *Lit =
      StringLiteral::Create(Context, Literal.getString(), Kind, StrTy,
                            &StringTokLocs[0], StringTokLocs.size());
  return Lit;
}

DeclRefExpr *Sema::MakeDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                                   SourceLocation Loc) {
  DeclarationNameInfo NameInfo(D->getDeclName(), Loc);
  return MakeDeclRefExpr(D, Ty, VK, NameInfo);
}

NonOdrUseReason Sema::getNonOdrUseReasonInCurrentContext(ValueDecl *D) {
  // A declaration named in an unevaluated operand never constitutes an odr-use.
  if (isUnevaluatedContext())
    return NOUR_Unevaluated;

  // All remaining non-variable cases constitute an odr-use. For variables, we
  // need to wait and see how the expression is used.
  return NOUR_None;
}

DeclRefExpr *Sema::MakeDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                                   const DeclarationNameInfo &NameInfo,
                                   NamedDecl *FoundD) {
  DeclRefExpr *E = DeclRefExpr::Create(Context, D, NameInfo, Ty, VK, FoundD,
                                       getNonOdrUseReasonInCurrentContext(D));
  MarkDeclRefReferenced(E);

  bool IsCommonDecl = isa<VarDecl, FunctionDecl, EnumConstantDecl>(D);
  if (LLVM_UNLIKELY(!IsCommonDecl)) {
    const auto *FD = dyn_cast<FieldDecl>(D);
    if (const auto *IFD = dyn_cast<IndirectFieldDecl>(D))
      FD = IFD->getAnonField();
    if (FD) {
      UnusedPrivateFields.remove(FD);
      if (FD->isBitField())
        E->setObjectKind(OK_BitField);
    }
  }

  return E;
}

bool Sema::DiagnoseEmptyLookup(Scope *S, LookupResult &R,
                               CorrectionCandidateCallback &CCC,
                               TypoExpr **Out) {
  DeclarationName Name = R.getLookupName();

  const unsigned diagnostic = diag::err_undeclared_var_use;
  const unsigned diagnostic_suggest = diag::err_undeclared_var_use_suggest;

  // Correct here rather than through CorrectTypoDelayed.  A delayed TypoExpr
  // is only ever resolved by CorrectDelayedTyposInExpr, which this port leaves
  // as a no-op, so the correction would instead be flushed by
  // OnEndOfTranslationUnit -- long after CodeGen has run on the enclosing
  // declaration and tripped over the TypoExpr's dependent type.  That flush
  // also passes an empty TypoCorrection, so it drops the "did you mean"
  // suggestion.  Delaying buys nothing here in any case: it exists upstream to
  // let overload resolution pick the correction, and C has no overloading.
  if (Out)
    *Out = nullptr;
  TypoCorrection Corrected;
  if (S && (Corrected = CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S,
                                    CCC, CTK_ErrorRecovery))) {
    R.setLookupName(Corrected.getCorrection());

    bool AcceptableWithRecovery = false;
    bool AcceptableWithoutRecovery = false;
    NamedDecl *ND = Corrected.getFoundDecl();
    if (ND) {
      R.addDecl(ND);

      auto *UnderlyingND = ND->getUnderlyingDecl();
      AcceptableWithRecovery = isa<ValueDecl>(UnderlyingND);
      AcceptableWithoutRecovery = isa<TypeDecl>(UnderlyingND);
    } else {
      AcceptableWithoutRecovery = true;
    }

    if (AcceptableWithRecovery || AcceptableWithoutRecovery) {
      unsigned NoteID = Corrected.getCorrectionDeclAs<ImplicitParamDecl>()
                            ? diag::note_implicit_param_decl
                            : diag::note_previous_decl;
      diagnoseTypo(Corrected, PDiag(diagnostic_suggest) << Name, PDiag(NoteID),
                   AcceptableWithRecovery);

      // Tell the callee whether to try to recover.
      return !AcceptableWithRecovery;
    }
  }
  R.clear();

  // Give up, we can't recover.
  Diag(R.getNameLoc(), diagnostic) << Name;
  return true;
}

NEVERC_HOT ExprResult
Sema::OnIdExpression(Scope *S, UnqualifiedId &Id, bool HasTrailingLParen,
                     bool IsAddressOfOperand, CorrectionCandidateCallback *CCC,
                     bool IsInlineAsmIdentifier, Token *KeywordReplacement) {
  assert(!(IsAddressOfOperand && HasTrailingLParen) &&
         "cannot be direct & operand and have a trailing lparen");

  DeclarationNameInfo NameInfo = GetNameFromUnqualifiedId(Id);

  DeclarationName Name = NameInfo.getName();
  IdentifierInfo *II = Name.getAsIdentifierInfo();
  SourceLocation NameLoc = NameInfo.getLoc();

  LookupResult R(*this, NameInfo, ResolveOrdinary);
  if (CachedClassifyNameDecl && CachedClassifyNameLoc == NameLoc) {
    R.addDecl(CachedClassifyNameDecl);
    R.resolveKind();
    CachedClassifyNameDecl = nullptr;
  } else {
    CachedClassifyNameDecl = nullptr;
    LookupParsedName(R, S, /*AllowBuiltinCreation=*/true);
  }

  if (LLVM_UNLIKELY(R.isAmbiguous()))
    return ExprError();

  if (LLVM_UNLIKELY(R.empty()) && HasTrailingLParen && II &&
      getLangOpts().implicitFunctionsAllowed()) {
    NamedDecl *D = ImplicitlyDefineFunction(NameLoc, *II, S);
    if (D)
      R.addDecl(D);
  }

  if (R.empty()) {
    // Don't diagnose an empty lookup for inline assembly.
    if (IsInlineAsmIdentifier)
      return ExprError();

    // If this name wasn't predeclared and if this is not a function
    // call, diagnose the problem.
    TypoExpr *TE = nullptr;
    DefaultFilterCCC DefaultValidator(II);
    DefaultValidator.IsAddressOfOperand = IsAddressOfOperand;
    assert((!CCC || CCC->IsAddressOfOperand == IsAddressOfOperand) &&
           "Typo correction callback misconfigured");
    if (CCC) {
      // Make sure the callback knows what the typo being diagnosed is.
      CCC->setTypoName(II);
    }
    if (DiagnoseEmptyLookup(S, R, CCC ? *CCC : DefaultValidator, &TE)) {
      if (TE && KeywordReplacement) {
        auto &State = getTypoExprState(TE);
        auto BestTC = State.Consumer->getNextCorrection();
        if (BestTC.isKeyword()) {
          auto *II = BestTC.getCorrectionAsIdentifierInfo();
          if (State.DiagHandler)
            State.DiagHandler(BestTC);
          KeywordReplacement->startToken();
          KeywordReplacement->setKind(II->getTokenID());
          KeywordReplacement->setIdentifierInfo(II);
          KeywordReplacement->setLocation(
              BestTC.getCorrectionRange().getBegin());
          // Clean up the state associated with the TypoExpr, since it has
          // now been diagnosed (without a call to CorrectDelayedTyposInExpr).
          clearDelayedTypo(TE);
          // Signal that a correction to a keyword was performed by returning a
          // valid-but-null ExprResult.
          return (Expr *)nullptr;
        }
        State.Consumer->resetCorrectionStream();
      }
      return TE ? TE : ExprError();
    }

    assert(!R.empty() &&
           "DiagnoseEmptyLookup returned false but added no results");
  }

  assert(!R.empty());

  if (PluginHooks && R.isSingleResult()) {
    NamedDecl *Replacement = nullptr;
    switch (PluginHooks->analyzeDeclarationReference(
        *this, NameLoc, R.getFoundDecl(), Replacement)) {
    case SemaPluginOutcome::NotHandled:
      break;
    case SemaPluginOutcome::Handled:
      if (!Replacement)
        return ExprError();
      R.clear();
      R.addDecl(Replacement);
      R.resolveKind();
      break;
    case SemaPluginOutcome::Error:
      return ExprError();
    }
  }

  return FormDeclarationNameExpr(R);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool validateDeclForExpr(Sema &S, SourceLocation Loc, NamedDecl *D) {
  if (LLVM_UNLIKELY(D->isInvalidDecl()))
    return true;

  if (LLVM_UNLIKELY(isa<TypedefNameDecl>(D))) {
    S.Diag(Loc, diag::err_unexpected_typedef) << D->getDeclName();
    return true;
  }

  return false;
}
} // namespace

ExprResult Sema::FormDeclarationNameExpr(LookupResult &R) {
  // C++: preserve overloaded function sets until call (or address-of) resolution.
  if (getLangOpts().CPlusPlus && R.isOverloadedResult()) {
    llvm::SmallVector<NamedDecl *, 8> Decls;
    for (NamedDecl *D : R)
      Decls.push_back(D->getUnderlyingDecl());
    return UnresolvedLookupExpr::Create(Context, R.getLookupNameInfo(),
                                        /*RequiresADL=*/false, Decls);
  }
  return FormDeclarationNameExpr(R.getLookupNameInfo(), R.getFoundDecl(),
                                 R.getRepresentativeDecl());
}

NEVERC_HOT ExprResult Sema::FormDeclarationNameExpr(
    const DeclarationNameInfo &NameInfo, NamedDecl *D, NamedDecl *FoundD) {
  assert(D && "Cannot refer to a NULL declaration");

  SourceLocation Loc = NameInfo.getLoc();
  if (validateDeclForExpr(*this, Loc, D)) {
    // Recovery from invalid cases (e.g. D is an invalid Decl).
    // We use the dependent type for the RecoveryExpr to prevent bogus follow-up
    // diagnostics, as invalid decls use int as a fallback type.
    return CreateRecoveryExpr(NameInfo.getBeginLoc(), NameInfo.getEndLoc(), {});
  }

  if (LLVM_UNLIKELY(!isa<ValueDecl>(D))) {
    Diag(Loc, diag::err_ref_non_value) << D << SourceRange();
    Diag(D->getLocation(), diag::note_declared_at);
    return ExprError();
  }

  if (LLVM_UNLIKELY(CheckDeclUsage(D, Loc)))
    return ExprError();

  auto *VD = cast<ValueDecl>(D);

  if (LLVM_UNLIKELY(VD->isInvalidDecl()))
    return ExprError();

  if (LLVM_UNLIKELY(isa<IndirectFieldDecl>(VD)))
    return FormAnonymousStructUnionMemberReference(NameInfo.getLoc(),
                                                   cast<IndirectFieldDecl>(VD));

  QualType type = VD->getType();
  if (type.isNull())
    return ExprError();
  ExprValueKind valueKind;

  auto Kind = D->getKind();
  if (LLVM_LIKELY(Kind == Decl::Var || Kind == Decl::ParmVar ||
                  Kind == Decl::ImplicitParam)) {
    valueKind =
        (Kind == Decl::Var && !type.hasQualifiers() && type->isVoidType())
            ? VK_PRValue
            : VK_LValue;
  } else {
    valueKind = VK_PRValue;
    switch (Kind) {
#define ABSTRACT_DECL(kind)
#define VALUE(type, base)
#define DECL(type, base) case Decl::type:
#include "neverc/Tree/DeclNodes.td.h"
      llvm_unreachable("invalid value decl kind");

    case Decl::EnumConstant:
      break;

    case Decl::Field:
    case Decl::IndirectField:
      valueKind = VK_LValue;
      break;

    case Decl::Var:
    case Decl::ImplicitParam:
    case Decl::ParmVar:
      llvm_unreachable("handled above");

    case Decl::Function: {
      if (unsigned BID = cast<FunctionDecl>(VD)->getBuiltinID()) {
        if (!Context.BuiltinInfo.isDirectlyAddressable(BID)) {
          type = Context.BuiltinFnTy;
          break;
        }
      }

      const FunctionType *fty = type->castAs<FunctionType>();
      if (!cast<FunctionDecl>(VD)->hasPrototype() &&
          isa<FunctionProtoType>(fty))
        type = Context.getFunctionNoProtoType(fty->getReturnType(),
                                              fty->getExtInfo());
      break;
    }
    }
  }

  auto *E = MakeDeclRefExpr(VD, type, valueKind, NameInfo, FoundD);
  // NeverC AST consumers assume a DeclRefExpr refers to a valid decl. We
  // wrap a DeclRefExpr referring to an invalid decl with a dependent-type
  // RecoveryExpr to avoid follow-up semantic analysis (thus prevent bogus
  // diagnostics).
  if (VD->isInvalidDecl() && E)
    return CreateRecoveryExpr(E->getBeginLoc(), E->getEndLoc(), {E});
  return E;
}

namespace {
void utf8ToWideString(unsigned CharByteWidth, llvm::StringRef Source,
                      llvm::SmallString<32> &Target) {
  Target.resize(CharByteWidth * (Source.size() + 1));
  char *ResultPtr = &Target[0];
  const llvm::UTF8 *ErrorPtr;
  bool success =
      llvm::ConvertUTF8toWide(CharByteWidth, Source, ResultPtr, ErrorPtr);
  (void)success;
  assert(success);
  Target.resize(ResultPtr - &Target[0]);
}
} // namespace

ExprResult Sema::FormPredefinedExpr(SourceLocation Loc,
                                    PredefinedIdentKind IK) {
  Decl *currentDecl = getPredefinedExprDecl(CurContext);
  if (!currentDecl) {
    Diag(Loc, diag::ext_predef_outside_function);
    currentDecl = Context.getTranslationUnitDecl();
  }

  auto Str = PredefinedExpr::ComputeName(IK, currentDecl);
  unsigned Length = Str.length();

  llvm::APInt LengthI(32, Length + 1);
  QualType ResTy;
  StringLiteral *SL = nullptr;
  if (IK == PredefinedIdentKind::LFunction ||
      IK == PredefinedIdentKind::LFuncSig) {
    ResTy = Context.adjustStringLiteralBaseType(Context.WideCharTy.withConst());
    llvm::SmallString<32> RawChars;
    utf8ToWideString(Context.getTypeSizeInChars(ResTy).getQuantity(), Str,
                     RawChars);
    ResTy = Context.getConstantArrayType(ResTy, LengthI, nullptr,
                                         ArraySizeModifier::Normal,
                                         /*IndexTypeQuals*/ 0);
    SL = StringLiteral::Create(Context, RawChars, StringLiteralKind::Wide,
                               ResTy, Loc);
  } else {
    ResTy = Context.adjustStringLiteralBaseType(Context.CharTy.withConst());
    ResTy = Context.getConstantArrayType(ResTy, LengthI, nullptr,
                                         ArraySizeModifier::Normal,
                                         /*IndexTypeQuals*/ 0);
    SL = StringLiteral::Create(Context, Str, StringLiteralKind::Ordinary, ResTy,
                               Loc);
  }

  return PredefinedExpr::Create(Context, Loc, ResTy, IK, LangOpts.MicrosoftExt,
                                SL);
}

ExprResult Sema::OnPredefinedExpr(SourceLocation Loc, tok::TokenKind Kind) {
  return FormPredefinedExpr(Loc, getPredefinedExprKind(Kind));
}

ExprResult Sema::OnCharacterConstant(const Token &Tok) {
  llvm::SmallString<16> CharBuffer;
  bool Invalid = false;
  llvm::StringRef ThisTok = PP.getSpelling(Tok, CharBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  CharLiteralParser Literal(ThisTok.begin(), ThisTok.end(), Tok.getLocation(),
                            PP, Tok.getKind());
  if (Literal.hadError())
    return ExprError();

  QualType Ty;
  if (Literal.isWide())
    Ty = Context.WideCharTy; // L'x' -> wchar_t
  else if (Literal.isUTF8() && getLangOpts().C23)
    Ty = Context.UnsignedCharTy; // u8'x' -> unsigned char in C23
  else if (Literal.isUTF8() && getLangOpts().Char8)
    Ty = Context.Char8Ty; // u8'x' -> char8_t when it exists.
  else if (Literal.isUTF16())
    Ty = Context.Char16Ty; // u'x' -> char16_t
  else if (Literal.isUTF32())
    Ty = Context.Char32Ty; // U'x' -> char32_t
  else
    Ty = Context.IntTy; // 'x' -> int in C.

  CharacterLiteralKind Kind = CharacterLiteralKind::Ascii;
  if (Literal.isWide())
    Kind = CharacterLiteralKind::Wide;
  else if (Literal.isUTF16())
    Kind = CharacterLiteralKind::UTF16;
  else if (Literal.isUTF32())
    Kind = CharacterLiteralKind::UTF32;
  else if (Literal.isUTF8())
    Kind = CharacterLiteralKind::UTF8;

  return new (Context)
      CharacterLiteral(Literal.getValue(), Kind, Ty, Tok.getLocation());
}

ExprResult Sema::OnIntegerConstant(SourceLocation Loc, uint64_t Val) {
  unsigned IntSize = Context.getTargetInfo().getIntWidth();
  return IntegerLiteral::Create(Context, llvm::APInt(IntSize, Val),
                                Context.IntTy, Loc);
}

namespace {
Expr *formFloatingLiteral(Sema &S, NumericLiteralParser &Literal, QualType Ty,
                          SourceLocation Loc) {
  const llvm::fltSemantics &Format = S.Context.getFloatTypeSemantics(Ty);

  using llvm::APFloat;
  APFloat Val(Format);

  APFloat::opStatus result = Literal.getFloatValue(Val);

  // Overflow is always an error, but underflow is only an error if
  // we underflowed to zero (APFloat reports denormals as underflow).
  if ((result & APFloat::opOverflow) ||
      ((result & APFloat::opUnderflow) && Val.isZero())) {
    unsigned diagnostic;
    llvm::SmallString<20> buffer;
    if (result & APFloat::opOverflow) {
      diagnostic = diag::warn_float_overflow;
      APFloat::getLargest(Format).toString(buffer);
    } else {
      diagnostic = diag::warn_float_underflow;
      APFloat::getSmallest(Format).toString(buffer);
    }

    S.Diag(Loc, diagnostic)
        << Ty << llvm::StringRef(buffer.data(), buffer.size());
  }

  bool isExact = (result == APFloat::opOK);
  return FloatingLiteral::Create(S.Context, Val, isExact, Ty, Loc);
}
} // namespace

NEVERC_HOT ExprResult Sema::OnNumericConstant(const Token &Tok) {
  // Fast path: single digit is extremely common in real-world C code.
  if (Tok.getLength() == 1) {
    const char Val = PP.getSpellingOfSingleCharacterNumericConstant(Tok);
    return OnIntegerConstant(Tok.getLocation(), Val - '0');
  }

  // Extended fast path: small pure-decimal integers (2-9 digits, no suffix,
  // no cleaning) are extremely common (array sizes, bit widths, enum values).
  // Avoid constructing NumericLiteralParser + SmallString for these.
  if (unsigned Len = Tok.getLength();
      LLVM_LIKELY(Len <= 9 && !Tok.needsCleaning())) {
    if (const char *D = Tok.getLiteralData()) {
      bool AllDecimal = true;
      uint64_t Val = 0;
      for (unsigned I = 0; I < Len; ++I) {
        unsigned char C = static_cast<unsigned char>(D[I]);
        unsigned Digit = C - '0';
        if (LLVM_UNLIKELY(Digit > 9u)) {
          AllDecimal = false;
          break;
        }
        Val = Val * 10 + Digit;
      }
      if (LLVM_LIKELY(AllDecimal && D[0] != '0'))
        return OnIntegerConstant(Tok.getLocation(), Val);
    }
  }

  llvm::SmallString<128> SpellingBuffer;
  // NumericLiteralParser wants to overread by one character.  Add padding to
  // the buffer in case the token is copied to the buffer.  If getSpelling()
  // returns a llvm::StringRef to the memory buffer, it should have a null char
  // at the EOF, so it is also safe.
  SpellingBuffer.resize(Tok.getLength() + 1);
  bool Invalid = false;
  llvm::StringRef TokSpelling = PP.getSpelling(Tok, SpellingBuffer, &Invalid);
  if (Invalid)
    return ExprError();

  NumericLiteralParser Literal(TokSpelling, Tok.getLocation(),
                               PP.getSourceManager(), PP.getLangOpts(),
                               PP.getTargetInfo(), PP.getDiagnostics());
  if (Literal.hadError)
    return ExprError();

  Expr *Res;

  if (Literal.isFixedPointLiteral()) {
    QualType Ty;

    if (Literal.isAccum) {
      if (Literal.isHalf) {
        Ty = Context.ShortAccumTy;
      } else if (Literal.isLong) {
        Ty = Context.LongAccumTy;
      } else {
        Ty = Context.AccumTy;
      }
    } else if (Literal.isFract) {
      if (Literal.isHalf) {
        Ty = Context.ShortFractTy;
      } else if (Literal.isLong) {
        Ty = Context.LongFractTy;
      } else {
        Ty = Context.FractTy;
      }
    }

    if (Literal.isUnsigned)
      Ty = Context.getCorrespondingUnsignedType(Ty);

    bool isSigned = !Literal.isUnsigned;
    unsigned scale = Context.getFixedPointScale(Ty);
    unsigned bit_width = Context.getTypeInfo(Ty).Width;

    llvm::APInt Val(bit_width, 0, isSigned);
    bool Overflowed = Literal.getFixedPointValue(Val, scale);
    bool ValIsZero = Val.isZero() && !Overflowed;

    auto MaxVal = Context.getFixedPointMax(Ty).getValue();
    if (Literal.isFract && Val == MaxVal + 1 && !ValIsZero)
      // Clause 6.4.4 - The value of a constant shall be in the range of
      // representable values for its type, with exception for constants of a
      // fract type with a value of exactly 1; such a constant shall denote
      // the maximal value for the type.
      --Val;
    else if (Val.ugt(MaxVal) || Overflowed)
      Diag(Tok.getLocation(), diag::err_too_large_for_fixed_point);

    Res = FixedPointLiteral::CreateFromRawInt(Context, Val, Ty,
                                              Tok.getLocation(), scale);
  } else if (Literal.isFloatingLiteral()) {
    QualType Ty;
    if (Literal.isHalf) {
      Diag(Tok.getLocation(), diag::err_half_const_requires_fp16);
      return ExprError();
    } else if (Literal.isFloat)
      Ty = Context.FloatTy;
    else if (Literal.isLong)
      Ty = Context.LongDoubleTy;
    else if (Literal.isFloat16)
      Ty = Context.Float16Ty;
    else if (Literal.isFloat128)
      Ty = Context.Float128Ty;
    else
      Ty = Context.DoubleTy;

    Res = formFloatingLiteral(*this, Literal, Ty, Tok.getLocation());

  } else if (!Literal.isIntegerLiteral()) {
    return ExprError();
  } else {
    QualType Ty;

    // size_t literal suffix (z/uz): rejected in this compiler.
    if (Literal.isSizeT)
      Diag(Tok.getLocation(), diag::err_size_t_suffix);

    if (Literal.isBitInt)
      PP.Diag(Tok.getLocation(), getLangOpts().C23
                                     ? diag::warn_c23_compat_bitint_suffix
                                     : diag::ext_c23_bitint_suffix);

    // Get the value in the widest-possible width. What is "widest" depends on
    // whether the literal is a bit-precise integer or not. For a bit-precise
    // integer type, try to scan the source to determine how many bits are
    // needed to represent the value. This may seem a bit expensive, but trying
    // to get the integer value from an overly-wide APInt is *extremely*
    // expensive, so the naive approach of assuming
    // llvm::IntegerType::MAX_INT_BITS is a big performance hit.
    unsigned BitsNeeded =
        Literal.isBitInt ? llvm::APInt::getSufficientBitsNeeded(
                               Literal.getLiteralDigits(), Literal.getRadix())
                         : Context.getTargetInfo().getIntMaxTWidth();
    llvm::APInt ResultVal(BitsNeeded, 0);

    if (Literal.getIntegerValue(ResultVal)) {
      // If this value didn't fit into uintmax_t, error and force to ull.
      Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
          << /* Unsigned */ 1;
      Ty = Context.UnsignedLongLongTy;
      assert(Context.getTypeSize(Ty) == ResultVal.getBitWidth() &&
             "long long is not intmax_t?");
    } else {
      // If this value fits into a ULL, try to figure out what else it fits into
      // according to the rules of C99 6.4.4.1p5.

      // Octal, Hexadecimal, and integers with a U suffix are allowed to
      // be an unsigned int.
      bool AllowUnsigned = Literal.isUnsigned || Literal.getRadix() != 10;

      // Check from smallest to largest, picking the smallest type we can.
      unsigned Width = 0;

      // Microsoft specific integer suffixes are explicitly sized.
      if (Literal.MicrosoftInteger) {
        if (Literal.MicrosoftInteger == 8 && !Literal.isUnsigned) {
          Width = 8;
          Ty = Context.CharTy;
        } else {
          Width = Literal.MicrosoftInteger;
          Ty = Context.getIntTypeForBitwidth(Width,
                                             /*Signed=*/!Literal.isUnsigned);
        }
      }

      // Bit-precise integer literals are automagically-sized based on the
      // width required by the literal.
      if (Literal.isBitInt) {
        // The signed version has one more bit for the sign value. There are no
        // zero-width bit-precise integers, even if the literal value is 0.
        Width = std::max(ResultVal.getActiveBits(), 1u) +
                (Literal.isUnsigned ? 0u : 1u);

        // Diagnose if the width of the constant is larger than BITINT_MAXWIDTH,
        // and reset the type to the largest supported width.
        unsigned int MaxBitIntWidth =
            Context.getTargetInfo().getMaxBitIntWidth();
        if (Width > MaxBitIntWidth) {
          Diag(Tok.getLocation(), diag::err_integer_literal_too_large)
              << Literal.isUnsigned;
          Width = MaxBitIntWidth;
        }

        // Reset the result value to the smaller APInt and select the correct
        // type to be used. Note, we zext even for signed values because the
        // literal itself is always an unsigned value (a preceeding - is a
        // unary operator, not part of the literal).
        ResultVal = ResultVal.zextOrTrunc(Width);
        Ty = Context.getBitIntType(Literal.isUnsigned, Width);
      }

      // size_t literal suffix: pick size_t / ssize_t when in range.
      if (Literal.isSizeT) {
        assert(!Literal.MicrosoftInteger &&
               "size_t literals can't be Microsoft literals");
        unsigned SizeTSize = Context.getTargetInfo().getTypeWidth(
            Context.getTargetInfo().getSizeType());

        // Does it fit in size_t?
        if (ResultVal.isIntN(SizeTSize)) {
          // Does it fit in ssize_t?
          if (!Literal.isUnsigned && ResultVal[SizeTSize - 1] == 0)
            Ty = Context.getSignedSizeType();
          else if (AllowUnsigned)
            Ty = Context.getSizeType();
          Width = SizeTSize;
        }
      }

      if (Ty.isNull() && !Literal.isLong && !Literal.isLongLong &&
          !Literal.isSizeT) {
        // Are int/unsigned possibilities?
        unsigned IntSize = Context.getTargetInfo().getIntWidth();

        // Does it fit in a unsigned int?
        if (ResultVal.isIntN(IntSize)) {
          // Does it fit in a signed int?
          if (!Literal.isUnsigned && ResultVal[IntSize - 1] == 0)
            Ty = Context.IntTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedIntTy;
          Width = IntSize;
        }
      }

      // Are long/unsigned long possibilities?
      if (Ty.isNull() && !Literal.isLongLong && !Literal.isSizeT) {
        unsigned LongSize = Context.getTargetInfo().getLongWidth();

        // Does it fit in a unsigned long?
        if (ResultVal.isIntN(LongSize)) {
          // Does it fit in a signed long?
          if (!Literal.isUnsigned && ResultVal[LongSize - 1] == 0)
            Ty = Context.LongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongTy;
          // C90 6.1.3.2p5 (unsigned long for out-of-range unsigned decimal).
          else if (!getLangOpts().C99) {
            const unsigned LongLongSize =
                Context.getTargetInfo().getLongLongWidth();
            Diag(Tok.getLocation(), diag::warn_old_implicitly_unsigned_long)
                << (LongLongSize > LongSize ? /*will have type 'long long'*/ 0
                                            : /*will be ill-formed*/ 1);
            Ty = Context.UnsignedLongTy;
          }
          Width = LongSize;
        }
      }
      if (Ty.isNull() && !Literal.isSizeT) {
        unsigned LongLongSize = Context.getTargetInfo().getLongLongWidth();

        // Does it fit in a unsigned long long?
        if (ResultVal.isIntN(LongLongSize)) {
          // Does it fit in a signed long long?
          // To be compatible with MSVC, hex integer literals ending with the
          // LL or i64 suffix are always signed in Microsoft mode.
          if (!Literal.isUnsigned &&
              (ResultVal[LongLongSize - 1] == 0 ||
               (getLangOpts().MSVCCompat && Literal.isLongLong)))
            Ty = Context.LongLongTy;
          else if (AllowUnsigned)
            Ty = Context.UnsignedLongLongTy;
          Width = LongLongSize;

          // Using long long (or needing its width) is a C99 extension in
          // pre-C99 modes.
          if (!getLangOpts().C99)
            Diag(Tok.getLocation(), diag::ext_c99_longlong);
        }
      }

      // If we still couldn't decide a type, we either have 'size_t' literal
      // that is out of range, or a decimal literal that does not fit in a
      // signed long long and has no U suffix.
      if (Ty.isNull()) {
        if (Literal.isSizeT)
          Diag(Tok.getLocation(), diag::err_size_t_literal_too_large)
              << Literal.isUnsigned;
        else
          Diag(Tok.getLocation(),
               diag::ext_integer_literal_too_large_for_signed);
        Ty = Context.UnsignedLongLongTy;
        Width = Context.getTargetInfo().getLongLongWidth();
      }

      if (ResultVal.getBitWidth() != Width)
        ResultVal = ResultVal.trunc(Width);
    }
    Res = IntegerLiteral::Create(Context, ResultVal, Ty, Tok.getLocation());
  }

  // If this is an imaginary literal, create the ImaginaryLiteral wrapper.
  if (Literal.isImaginary) {
    Res = new (Context)
        ImaginaryLiteral(Res, Context.getComplexType(Res->getType()));

    Diag(Tok.getLocation(), diag::ext_imaginary_constant);
  }
  return Res;
}

