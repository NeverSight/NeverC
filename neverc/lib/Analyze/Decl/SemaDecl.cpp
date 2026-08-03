#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Analyze/SemaPluginHooks.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Type name resolution & classification
// ===----------------------------------------------------------------------===

Sema::DeclGroupPtrTy Sema::WrapDeclAsGroup(Decl *Ptr, Decl *OwnedType) {
  if (OwnedType) {
    Decl *Group[2] = {OwnedType, Ptr};
    return DeclGroupPtrTy::make(DeclGroupRef::Create(Context, Group, 2));
  }

  return DeclGroupPtrTy::make(DeclGroupRef(Ptr));
}

namespace {

class TypeNameValidatorCCC final : public CorrectionCandidateCallback {
public:
  explicit TypeNameValidatorCCC(bool AllowInvalid)
      : AllowInvalidDecl(AllowInvalid) {
    WantExpressionKeywords = false;
    WantRemainingKeywords = false;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (NamedDecl *ND = candidate.getCorrectionDecl()) {
      if (!AllowInvalidDecl && ND->isInvalidDecl())
        return false;

      if (!isa<TypeDecl>(ND))
        return false;

      return true;
    }

    return candidate.isKeyword();
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<TypeNameValidatorCCC>(*this);
  }

private:
  bool AllowInvalidDecl;
};

} // end anonymous namespace

/// Attach an implicit `__attribute__((cleanup(<CleanupFD>)))` to `VD`.
/// Shared by `attachNeverCStringCleanup` (auto-released `string` locals)
/// and `attachNeverCWptrCleanup` (auto-released wide pointer locals);
/// both paths build the identical `AttributeCommonInfo` + `CleanupAttr`
/// triple, so keeping the spelling in one place avoids the next maintainer
/// fixing only half of the pattern when the implicit-attribute form
/// evolves.
static void attachImplicitCleanupAttr(Sema &S, VarDecl *VD,
                                      FunctionDecl *CleanupFD) {
  AttributeCommonInfo Info(SourceRange(VD->getLocation()),
                           AttributeCommonInfo::AT_Cleanup,
                           AttributeCommonInfo::Form::Implicit());
  auto *Attr = ::new (S.Context) CleanupAttr(S.Context, Info, CleanupFD);
  Attr->setImplicit(true);
  VD->addAttr(Attr);
}

namespace neverc {

void attachNeverCStringCleanup(Sema &S, Scope *Sc, VarDecl *VD,
                               FunctionDecl *ParamOwner,
                               bool IsFunctionDefinitionParam) {
  if (!VD || VD->isInvalidDecl())
    return;
  QualType T = VD->getType();
  if (LLVM_LIKELY(T->isBuiltinType() || T->isPointerType()))
    return;
  if (VD->hasAttr<CleanupAttr>() || !S.isNeverCStringType(T))
    return;

  if (S.isInsideNeverCStringRuntime(VD))
    return;

  if (isa<ParmVarDecl>(VD)) {
    auto *FD = ParamOwner
                   ? ParamOwner
                   : dyn_cast_or_null<FunctionDecl>(VD->getDeclContext());
    if (!IsFunctionDefinitionParam || !FD || S.isNeverCStringRuntimeFD(FD))
      return;
  } else if (!VD->hasLocalStorage()) {
    return;
  }

  FunctionDecl *CleanupFD = S.lookupNeverCStringFunctionDecl(
      BuiltinStringNames::CleanupFunctionName, Sc, VD->getLocation());
  if (!CleanupFD)
    return;
  attachImplicitCleanupAttr(S, VD, CleanupFD);
}

} // namespace neverc

static bool isWptrProducingCall(Sema &S, const Expr *Init) {
  const auto *CE = dyn_cast_or_null<CallExpr>(Init->IgnoreParenImpCasts());
  if (!CE)
    return false;
  const auto *Callee = CE->getDirectCallee();
  return S.isNeverCStringWptrProducer(Callee);
}

namespace neverc {

void attachNeverCWptrCleanup(Sema &S, Scope *Sc, VarDecl *VD) {
  if (!VD || VD->isInvalidDecl() || !VD->hasLocalStorage())
    return;
  if (VD->hasAttr<CleanupAttr>())
    return;
  if (!VD->getType()->isPointerType())
    return;
  const Expr *Init = VD->getInit();
  if (!Init || !isWptrProducingCall(S, Init))
    return;

  FunctionDecl *CleanupFD = S.lookupNeverCStringFunctionDecl(
      BuiltinStringNames::WptrCleanupFunctionName, Sc, VD->getLocation());
  if (!CleanupFD)
    return;
  attachImplicitCleanupAttr(S, VD, CleanupFD);
}

} // namespace neverc

namespace {
ParsedType buildNamedType(Sema &S, QualType T, SourceLocation NameLoc,
                          bool WantNontrivialTypeSourceInfo = true) {
  switch (T->getTypeClass()) {
  case Type::Enum:
  case Type::Record:
  case Type::Typedef:
    break;
  default:
    llvm_unreachable("Unexpected Type Class");
  }

  QualType ElTy =
      S.Context.getElaboratedType(ElaboratedTypeKeyword::None, T, nullptr);
  if (!WantNontrivialTypeSourceInfo)
    return ParsedType::make(ElTy);

  TypeLocBuilder Builder;
  Builder.pushTypeSpec(T).setNameLoc(NameLoc);
  ElaboratedTypeLoc ElabTL = Builder.push<ElaboratedTypeLoc>(ElTy);
  ElabTL.setElaboratedKeywordLoc(SourceLocation());
  return S.CreateParsedType(ElTy, Builder.getTypeSourceInfo(S.Context, ElTy));
}
} // namespace

ParsedType Sema::getTypeName(const IdentifierInfo &II, SourceLocation NameLoc,
                             Scope *S, bool HasTrailingDot,
                             bool WantNontrivialTypeSourceInfo,
                             IdentifierInfo **CorrectedII) {
  if (PluginHooks) {
    QualType Replacement;
    switch (PluginHooks->analyzeTypeName(*this, NameLoc, II.getName(),
                                         Replacement)) {
    case SemaPluginOutcome::NotHandled:
      break;
    case SemaPluginOutcome::Handled:
      if (Replacement.isNull())
        return nullptr;
      if (!WantNontrivialTypeSourceInfo)
        return ParsedType::make(Replacement);
      return CreateParsedType(
          Replacement,
          Context.getTrivialTypeSourceInfo(Replacement, NameLoc));
    case SemaPluginOutcome::Error:
      return nullptr;
    }
  }

  LookupResult Result(*this, &II, NameLoc, ResolveOrdinary);
  ResolveName(Result, S);

  NamedDecl *IIDecl = nullptr;
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
    if (CorrectedII) {
      TypeNameValidatorCCC CCC(/*AllowInvalid=*/true);
      TypoCorrection Correction =
          CorrectTypo(Result.getLookupNameInfo(), ResolveOrdinary, S, CCC,
                      CTK_ErrorRecovery);
      IdentifierInfo *NewII = Correction.getCorrectionAsIdentifierInfo();
      if (Correction && NewII != &II) {
        ParsedType Ty = getTypeName(*NewII, NameLoc, S, HasTrailingDot,
                                    WantNontrivialTypeSourceInfo);
        if (Ty) {
          diagnoseTypo(Correction, PDiag(diag::err_unknown_typename_suggest)
                                       << Result.getLookupName());
          *CorrectedII = NewII;
          return Ty;
        }
      }
    }
    Result.suppressDiagnostics();
    return nullptr;
  case LookupResult::FoundOverloaded:
    Result.suppressDiagnostics();
    return nullptr;

  case LookupResult::Ambiguous:
    // Look to see if we have a type anywhere in the list of results.
    for (auto *Res : Result) {
      NamedDecl *RealRes = Res->getUnderlyingDecl();
      if (isa<TypeDecl>(RealRes)) {
        if (!IIDecl ||
            // Make the selection of the recovery decl deterministic.
            RealRes->getLocation() < IIDecl->getLocation()) {
          IIDecl = RealRes;
        }
      }
    }

    if (!IIDecl) {
      // None of the entities we found is a type, so there is no way
      // to even assume that the result is a type. In this case, don't
      // complain about the ambiguity. The parser will either try to
      // perform this lookup again (e.g., as an object name), which
      // will produce the ambiguity, or will complain that it expected
      // a type name.
      Result.suppressDiagnostics();
      return nullptr;
    }

    // We found a type within the ambiguous lookup; diagnose the
    // ambiguity and then return that type. This might be the right
    // answer, or it might not be, but it suppresses any attempt to
    // perform the name lookup again.
    break;

  case LookupResult::Found:
    IIDecl = Result.getFoundDecl();
    break;
  }

  assert(IIDecl && "Didn't find decl");

  QualType T;
  if (TypeDecl *TD = dyn_cast<TypeDecl>(IIDecl)) {
    CheckDeclUsage(IIDecl, NameLoc);

    T = Context.getTypeDeclType(TD);
    MarkAnyDeclReferenced(TD->getLocation(), TD, /*OdrUse=*/false);
  }

  if (T.isNull()) {
    // If it's not plausibly a type, suppress diagnostics.
    Result.suppressDiagnostics();
    return nullptr;
  }

  return buildNamedType(*this, T, NameLoc, WantNontrivialTypeSourceInfo);
}

DeclSpec::TST Sema::isTagName(IdentifierInfo &II, Scope *S) {
  // Do a tag name lookup in this scope.
  LookupResult R(*this, &II, SourceLocation(), ResolveTag);
  ResolveName(R, S, false);
  R.suppressDiagnostics();
  if (R.getResultKind() == LookupResult::Found)
    if (const TagDecl *TD = R.getAsSingle<TagDecl>()) {
      switch (TD->getTagKind()) {
      case TagTypeKind::Struct:
        return DeclSpec::TST_struct;
      case TagTypeKind::Union:
        return DeclSpec::TST_union;
      case TagTypeKind::Class:
        return DeclSpec::TST_class;
      case TagTypeKind::Enum:
        return DeclSpec::TST_enum;
      }
    }

  return DeclSpec::TST_unspecified;
}

void Sema::DiagnoseUnknownTypeName(IdentifierInfo *&II, SourceLocation IILoc,
                                   Scope *S, ParsedType &SuggestedType) {
  // We don't have anything to suggest (yet).
  SuggestedType = nullptr;

  // There may have been a typo in the name of the type. Look up typo
  // results, in case we have something that we can suggest.
  TypeNameValidatorCCC CCC(/*AllowInvalid=*/false);
  if (TypoCorrection Corrected =
          CorrectTypo(DeclarationNameInfo(II, IILoc), ResolveOrdinary, S, CCC,
                      CTK_ErrorRecovery)) {
    if (Corrected.isKeyword()) {
      // We corrected to a keyword.
      diagnoseTypo(Corrected, PDiag(diag::err_unknown_typename_suggest) << II);
      II = Corrected.getCorrectionAsIdentifierInfo();
    } else {
      diagnoseTypo(Corrected, PDiag(diag::err_unknown_typename_suggest) << II,
                   true);
      SuggestedType = getTypeName(*Corrected.getCorrectionAsIdentifierInfo(),
                                  IILoc, S, false, true);
    }
    return;
  }

  Diag(IILoc, diag::err_unknown_typename) << II;
}

namespace {
bool isTagTypeWithMissingTag(Sema &SemaRef, LookupResult &Result, Scope *S,
                             IdentifierInfo *&Name, SourceLocation NameLoc) {
  LookupResult R(SemaRef, Name, NameLoc, neverc::ResolveTag);
  SemaRef.LookupParsedName(R, S);
  if (TagDecl *Tag = R.getAsSingle<TagDecl>()) {
    tok::TokenKind TagTokKind = tok::unknown;
    switch (Tag->getTagKind()) {
    case TagTypeKind::Enum:
      TagTokKind = tok::kw_enum;
      break;
    case TagTypeKind::Struct:
      TagTokKind = tok::kw_struct;
      break;
    case TagTypeKind::Union:
      TagTokKind = tok::kw_union;
      break;
    case TagTypeKind::Class:
      TagTokKind = tok::kw_class;
      break;
    }
    if (TagTokKind == tok::unknown)
      return false;
    llvm::StringRef TagName = tok::getKeywordSpelling(TagTokKind);
    llvm::SmallString<16> FixItTagName(TagName);
    FixItTagName += ' ';
    SemaRef.Diag(NameLoc, diag::err_use_of_tag_name_without_tag)
        << Name << TagName << false
        << FixItHint::CreateInsertion(NameLoc, FixItTagName);

    for (auto *I : Result)
      SemaRef.Diag(I->getLocation(), diag::note_decl_hiding_tag_type)
          << Name << TagName;

    // Replace lookup results with just the tag decl.
    Result.clear(neverc::ResolveTag);
    SemaRef.LookupParsedName(Result, S);
    return true;
  }

  return false;
}
} // namespace

Sema::NameClassification Sema::ClassifyName(Scope *S, IdentifierInfo *&Name,
                                            SourceLocation NameLoc,
                                            const Token &NextToken,
                                            CorrectionCandidateCallback *CCC) {
  DeclarationNameInfo NameInfo(Name, NameLoc);

  LookupResult Result(*this, Name, NameLoc, ResolveOrdinary);
  LookupParsedName(Result, S, /*AllowBuiltinCreation=*/true);

  bool SecondTry = false;

Corrected:
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
    // If an unqualified-id is followed by a '(', then we have a function
    // call.
    if (NextToken.is(tok::l_paren)) {
      // C90 6.3.2.2:
      //   If the expression that precedes the parenthesized argument list in a
      //   function call consists solely of an identifier, and if no
      //   declaration is visible for this identifier, the identifier is
      //   implicitly declared exactly as if, in the innermost block containing
      //   the function call, the declaration
      //
      //     extern int identifier ();
      //
      //   appeared.
      //
      // We also allow this in C99 as an extension. However, this is not
      // allowed in all language modes as functions without prototypes may not
      // be supported.
      if (getLangOpts().implicitFunctionsAllowed()) {
        if (NamedDecl *D = ImplicitlyDefineFunction(NameLoc, *Name, S))
          return NameClassification::NonType(D);
      }
    }

    if (!SecondTry &&
        isTagTypeWithMissingTag(*this, Result, S, Name, NameLoc)) {
      break;
    }

    // Perform typo correction to determine if there is another name that is
    // close to this name.
    if (!SecondTry && CCC) {
      SecondTry = true;
      if (TypoCorrection Corrected =
              CorrectTypo(Result.getLookupNameInfo(), Result.getLookupKind(), S,
                          *CCC, CTK_ErrorRecovery)) {
        unsigned UnqualifiedDiag = diag::err_undeclared_var_use_suggest;

        NamedDecl *FirstDecl = Corrected.getFoundDecl();
        NamedDecl *UnderlyingFirstDecl = Corrected.getCorrectionDecl();
        if (UnderlyingFirstDecl && isa<TypeDecl>(UnderlyingFirstDecl))
          UnqualifiedDiag = diag::err_unknown_typename_suggest;

        diagnoseTypo(Corrected, PDiag(UnqualifiedDiag) << Name);

        // Update the name, so that the caller has the new name.
        Name = Corrected.getCorrectionAsIdentifierInfo();

        // Typo correction corrected to a keyword.
        if (Corrected.isKeyword())
          return Name;

        // Also update the LookupResult...
        Result.clear();
        Result.setLookupName(Corrected.getCorrection());
        if (FirstDecl)
          Result.addDecl(FirstDecl);

        goto Corrected;
      }
    }

    // We failed to correct; just fall through and let the parser deal with it.
    Result.suppressDiagnostics();
    return NameClassification::Unknown();

  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
    break;

  case LookupResult::Ambiguous:
    return NameClassification::Error();
  }

  auto FormTypeFor = [&](TypeDecl *Type, NamedDecl *) {
    QualType T = Context.getTypeDeclType(Type);
    return buildNamedType(*this, T, NameLoc);
  };

  NamedDecl *FirstDecl = (*Result.begin())->getUnderlyingDecl();
  if (TypeDecl *Type = dyn_cast<TypeDecl>(FirstDecl)) {
    CheckDeclUsage(Type, NameLoc);
    MarkAnyDeclReferenced(Type->getLocation(), Type, /*OdrUse=*/false);
    return FormTypeFor(Type, *Result.begin());
  }

  // Check for a tag type hidden by a non-type decl in a few cases where it
  // seems likely a type is wanted instead of the non-type that was found.
  bool NextIsOp = NextToken.isOneOf(tok::amp, tok::star);
  if ((NextToken.is(tok::identifier) ||
       (NextIsOp && FirstDecl->getUnderlyingDecl()->getAsFunction())) &&
      isTagTypeWithMissingTag(*this, Result, S, Name, NameLoc)) {
    TypeDecl *Type = Result.getAsSingle<TypeDecl>();
    CheckDeclUsage(Type, NameLoc);
    return FormTypeFor(Type, *Result.begin());
  }

  NamedDecl *Rep = Result.getRepresentativeDecl();
  CachedClassifyNameDecl = Rep;
  CachedClassifyNameLoc = NameLoc;
  return NameClassification::NonType(Rep);
}

// ===----------------------------------------------------------------------===
// Declaration context & scope management
// ===----------------------------------------------------------------------===

void Sema::PushDeclContext(Scope *S, DeclContext *DC) {
  assert(
      DC->getLexicalParent() == CurContext &&
      "The next DeclContext should be lexically contained in the current one.");
  CurContext = DC;
  S->setEntity(DC);
}

void Sema::PopDeclContext() {
  assert(CurContext && "DeclContext imbalance!");

  CurContext = CurContext->getLexicalParent();
  assert(CurContext && "Popped translation unit!");
}

void Sema::EnterDeclaratorContext(Scope *S, DeclContext *DC) {
  // Names in the declarator are resolved in the declaration's semantic
  // context (e.g. after a nested-name-specifier). That context may not be
  // lexically nested in CurContext, so we cannot use PushDeclContext here.

  assert(!S->getEntity() && "scope already has entity");

#ifndef NDEBUG
  Scope *Ancestor = S->getParent();
  while (!Ancestor->getEntity())
    Ancestor = Ancestor->getParent();
  assert(Ancestor->getEntity() == CurContext && "ancestor context mismatch");
#endif

  CurContext = DC;
  S->setEntity(DC);
}

void Sema::OnReenterFunctionContext(Scope *S, Decl *D) {
  FunctionDecl *FD = D->getAsFunction();
  if (!FD)
    return;

  // Same implementation as PushDeclContext, but enters the context
  // from the lexical parent, rather than the top-level record.
  assert(
      CurContext == FD->getLexicalParent() &&
      "The next DeclContext should be lexically contained in the current one.");
  CurContext = FD;
  S->setEntity(CurContext);

  for (unsigned P = 0, NumParams = FD->getNumParams(); P < NumParams; ++P) {
    ParmVarDecl *Param = FD->getParamDecl(P);
    // If the parameter has an identifier, then add it to the scope
    if (Param->getIdentifier()) {
      S->AddDecl(Param);
      IdResolver.AddDecl(Param);
    }
  }
}

void Sema::OnExitFunctionContext() {
  // Same implementation as PopDeclContext, but returns to the lexical parent,
  // rather than the top-level record.
  assert(CurContext && "DeclContext imbalance!");
  CurContext = CurContext->getLexicalParent();
  assert(CurContext && "Popped translation unit!");
}

namespace neverc {

bool canOverloadFunction(const LookupResult &Previous, TreeContext &Context,
                         const FunctionDecl *New) {
  if (New->hasAttr<OverloadableAttr>())
    return true;

  // Multiversion function declarations are not overloads in the
  // usual sense of that term, but lookup will report that an
  // overload set was found if more than one multiversion function
  // declaration is present for the same name. It is therefore
  // inadequate to assume that some prior declaration(s) had
  // the overloadable attribute; checking is required. Since one
  // declaration is permitted to omit the attribute, it is necessary
  // to check at least two; hence the 'any_of' check below. Note that
  // the overloadable attribute is implicitly added to declarations
  // that were required to have it but did not.
  if (Previous.getResultKind() == LookupResult::FoundOverloaded) {
    return llvm::any_of(Previous, [](const NamedDecl *ND) {
      return ND->hasAttr<OverloadableAttr>();
    });
  } else if (Previous.getResultKind() == LookupResult::Found)
    return Previous.getFoundDecl()->hasAttr<OverloadableAttr>();

  return false;
}

} // namespace neverc

void Sema::PushOnScopeChains(NamedDecl *D, Scope *S, bool AddToContext) {
  while (
      LLVM_UNLIKELY(S->getEntity() && S->getEntity()->isTransparentContext()))
    S = S->getParent();

  if (LLVM_LIKELY(AddToContext))
    CurContext->addDecl(D);

  IdentifierResolver::iterator I = IdResolver.begin(D->getDeclName()),
                               IEnd = IdResolver.end();
  for (; I != IEnd; ++I) {
    if (S->isDeclScope(*I) && D->declarationReplaces(*I)) {
      S->RemoveDecl(*I);
      IdResolver.RemoveDecl(*I);
      break;
    }
  }

  S->AddDecl(D);

  if (LLVM_UNLIKELY(isa<LabelDecl>(D) && !cast<LabelDecl>(D)->isGnuLocal())) {
    // Labels may be non-lexically ordered; insert at correct chain position.
    for (I = IdResolver.begin(D->getDeclName()); I != IEnd; ++I) {
      DeclContext *IDC = (*I)->getLexicalDeclContext()->getRedeclContext();
      if (IDC == CurContext) {
        if (!S->isDeclScope(*I))
          continue;
      } else if (IDC->Encloses(CurContext))
        break;
    }

    IdResolver.InsertDeclAfter(I, D);
  } else {
    IdResolver.AddDecl(D);
  }
  warnOnReservedIdentifier(D);
}

bool Sema::isDeclInScope(NamedDecl *D, DeclContext *Ctx, Scope *S) const {
  return IdResolver.isDeclInScope(D, Ctx, S);
}

Scope *Sema::getScopeForDeclContext(Scope *S, DeclContext *DC) {
  DeclContext *TargetDC = DC->getPrimaryContext();
  do {
    if (DeclContext *ScopeDC = S->getEntity())
      if (ScopeDC->getPrimaryContext() == TargetDC)
        return S;
  } while ((S = S->getParent()));

  return nullptr;
}


void Sema::FilterLookupForScope(LookupResult &R, DeclContext *Ctx, Scope *S,
                                bool ConsiderLinkage) {
  LookupResult::Filter F = R.makeFilter();
  while (F.hasNext()) {
    NamedDecl *D = F.next();

    if (isDeclInScope(D, Ctx, S))
      continue;

    if (ConsiderLinkage && isOutOfScopePreviousDeclaration(D, Ctx, Context))
      continue;

    F.erase();
  }

  F.done();
}

// We need this to handle
//
// typedef struct {
//   void *foo() { return 0; }
// } A;
//
// When we see foo we don't know if after the typedef we will get 'A' or '*A'
// for example. If 'A', foo will have external linkage. If we have '*A',
// foo will have no linkage. Since we can't know until we get to the end
// of the typedef, this function finds out if D might have non-external linkage.
// Callers should verify at the end of the TU if it D has external linkage or
// not.
// ===----------------------------------------------------------------------===
// Unused declaration diagnostics
// ===----------------------------------------------------------------------===

bool Sema::mightHaveNonExternalLinkage(const DeclaratorDecl *D) {
  const DeclContext *DC = D->getDeclContext();
  while (!DC->isTranslationUnit()) {
    if (const RecordDecl *RD = dyn_cast<RecordDecl>(DC)) {
      if (!RD->hasNameForLinkage())
        return true;
    }
    DC = DC->getParent();
  }

  return !D->isExternallyVisible();
}

namespace {
bool inMainSourceFile(const Sema &S, SourceLocation Loc) {
  if (S.getLangOpts().IsHeaderFile)
    return false;
  return S.SourceMgr.isInMainFile(Loc);
}
} // namespace

bool Sema::ShouldWarnIfUnusedFileScopedDecl(const DeclaratorDecl *D) const {
  assert(D);

  if (D->isInvalidDecl() || D->isUsed() || D->hasAttr<UnusedAttr>())
    return false;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isInlined() && !inMainSourceFile(*this, FD->getLocation()))
      return false;

    if (FD->doesThisDeclarationHaveABody() && Context.DeclMustBeEmitted(FD))
      return false;
  } else if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    if (!inMainSourceFile(*this, VD->getLocation()))
      return false;

    if (Context.DeclMustBeEmitted(VD))
      return false;

    if (VD->isInline() && !inMainSourceFile(*this, VD->getLocation()))
      return false;
  } else {
    return false;
  }

  // Only warn for unused decls internal to the translation unit.
  return mightHaveNonExternalLinkage(D);
}

void Sema::MarkUnusedFileScopedDecl(const DeclaratorDecl *D) {
  if (!D)
    return;

  if (LLVM_LIKELY(getDiagnostics().getIgnoreAllWarnings()))
    return;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionDecl *First = FD->getFirstDecl();
    if (FD != First && ShouldWarnIfUnusedFileScopedDecl(First))
      return; // First should already be in the vector.
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    const VarDecl *First = VD->getFirstDecl();
    if (VD != First && ShouldWarnIfUnusedFileScopedDecl(First))
      return; // First should already be in the vector.
  }

  if (ShouldWarnIfUnusedFileScopedDecl(D))
    UnusedFileScopedDecls.push_back(D);
}

namespace {
bool needsUnusedDiagnostic(const LangOptions &LangOpts, const NamedDecl *D) {
  if (D->isInvalidDecl())
    return false;

  if (!D->getDeclName())
    return false;

  if (D->isReferenced() || D->isUsed())
    return false;

  if (D->hasAttr<UnusedAttr>() || D->hasAttr<CleanupAttr>())
    return false;

  if (isa<LabelDecl>(D))
    return true;

  // Except for labels, we only care about unused decls that are local to
  // functions.
  bool WithinFunction = D->getDeclContext()->isFunctionOrMethod();
  if (!WithinFunction)
    return false;

  if (isa<TypedefNameDecl>(D))
    return true;

  // White-list anything that isn't a local variable.
  if (!isa<VarDecl>(D) || isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D))
    return false;

  // Types of valid local variables should be complete, so this should succeed.
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {

    const Expr *Init = VD->getInit();
    if (const auto *Cleanups = dyn_cast_if_present<ExprWithCleanups>(Init))
      Init = Cleanups->getSubExpr();

    const auto *Ty = VD->getType().getTypePtr();

    // Only look at the outermost level of typedef.
    if (const TypedefType *TT = Ty->getAs<TypedefType>()) {
      // Allow anything marked with __attribute__((unused)).
      if (TT->getDecl()->hasAttr<UnusedAttr>())
        return false;
    }

    if (Ty->isIncompleteType())
      return false;

    // Look at the element type to ensure that the warning behaviour is
    // consistent for both scalars and arrays.
    Ty = Ty->getBaseElementTypeUnsafe();

    if (const TagType *TT = Ty->getAs<TagType>()) {
      const TagDecl *Tag = TT->getDecl();
      if (Tag->hasAttr<UnusedAttr>())
        return false;
    }
  }

  return true;
}
} // namespace

namespace {
void buildUnusedDeclFixIt(const NamedDecl *D, TreeContext &Ctx,
                          FixItHint &Hint) {
  if (isa<LabelDecl>(D)) {
    SourceLocation AfterColon = SourceScanner::locateAfterToken(
        D->getEndLoc(), tok::colon, Ctx.getSourceManager(), Ctx.getLangOpts(),
        /*SkipTrailingWhitespaceAndNewline=*/false);
    if (AfterColon.isInvalid())
      return;
    Hint = FixItHint::CreateRemoval(
        CharSourceRange::getCharRange(D->getBeginLoc(), AfterColon));
  }
}
} // namespace

void Sema::DiagnoseUnusedNestedTypedefs(const RecordDecl *D) {
  DiagnoseUnusedNestedTypedefs(
      D, [this](SourceLocation Loc, PartialDiagnostic PD) { Diag(Loc, PD); });
}

void Sema::DiagnoseUnusedNestedTypedefs(const RecordDecl *D,
                                        DiagReceiverTy DiagReceiver) {
  for (auto *TmpD : D->decls()) {
    if (const auto *T = dyn_cast<TypedefNameDecl>(TmpD))
      DiagnoseUnusedDecl(T, DiagReceiver);
    else if (const auto *R = dyn_cast<RecordDecl>(TmpD))
      DiagnoseUnusedNestedTypedefs(R, DiagReceiver);
  }
}

void Sema::DiagnoseUnusedDecl(const NamedDecl *D) {
  DiagnoseUnusedDecl(
      D, [this](SourceLocation Loc, PartialDiagnostic PD) { Diag(Loc, PD); });
}

void Sema::DiagnoseUnusedDecl(const NamedDecl *D, DiagReceiverTy DiagReceiver) {
  if (!needsUnusedDiagnostic(getLangOpts(), D))
    return;

  if (auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    // typedefs can be referenced later on, so the diagnostics are emitted
    // at end-of-translation-unit.
    UnusedLocalTypedefNameCandidates.insert(TD);
    return;
  }

  FixItHint Hint;
  buildUnusedDeclFixIt(D, Context, Hint);

  unsigned DiagID =
      isa<LabelDecl>(D) ? diag::warn_unused_label : diag::warn_unused_variable;

  SourceLocation DiagLoc = D->getLocation();
  DiagReceiver(DiagLoc, PDiag(DiagID) << D << Hint << SourceRange(DiagLoc));
}

void Sema::DiagnoseUnusedButSetDecl(const VarDecl *VD,
                                    DiagReceiverTy DiagReceiver) {
  // If it's not referenced, it can't be set. If it has the Cleanup attribute,
  // it's not really unused.
  if (!VD->isReferenced() || !VD->getDeclName() || VD->hasAttr<CleanupAttr>())
    return;

  if (VD->hasAttr<UnusedAttr>())
    return;

  const auto *Ty = VD->getType().getTypePtr()->getBaseElementTypeUnsafe();

  if (const TagType *TT = Ty->getAs<TagType>()) {
    const TagDecl *Tag = TT->getDecl();
    if (Tag->hasAttr<UnusedAttr>())
      return;
  }

  auto iter = RefsMinusAssignments.find(VD);
  if (iter == RefsMinusAssignments.end())
    return;

  assert(iter->getSecond() >= 0 &&
         "Found a negative number of references to a VarDecl");
  if (iter->getSecond() != 0)
    return;

  unsigned DiagID = isa<ParmVarDecl>(VD) ? diag::warn_unused_but_set_parameter
                                         : diag::warn_unused_but_set_variable;
  DiagReceiver(VD->getLocation(), PDiag(DiagID) << VD);
}

namespace {
void validatePoppedLabel(LabelDecl *L, Sema &S,
                         Sema::DiagReceiverTy DiagReceiver) {
  // Verify that we have no forward references left.  If so, there was a goto
  // or address of a label taken, but no definition of it.  Label fwd
  // definitions are indicated with a null substmt which is also not a resolved
  // MS inline assembly label name.
  bool Diagnose = false;
  if (L->isMSAsmLabel())
    Diagnose = !L->isResolvedMSAsmLabel();
  else
    Diagnose = L->getStmt() == nullptr;
  if (Diagnose)
    DiagReceiver(L->getLocation(), S.PDiag(diag::err_undeclared_label_use)
                                       << L);
}
} // namespace

void Sema::OnPopScope(SourceLocation Loc, Scope *S) {
  S->applyNRVO();

  if (LLVM_LIKELY(S->decl_empty()))
    return;
  assert((S->getFlags() & Scope::DeclScope) &&
         "Scope shouldn't contain decls!");

  const bool IgnoreWarnings = Diags.getIgnoreAllWarnings();

  struct LocAndDiag {
    SourceLocation Loc;
    std::optional<SourceLocation> PreviousDeclLoc;
    PartialDiagnostic PD;
  };
  llvm::SmallVector<LocAndDiag, 16> DeclDiags;
  auto addDiag = [&DeclDiags](SourceLocation Loc, PartialDiagnostic PD) {
    DeclDiags.push_back(LocAndDiag{Loc, std::nullopt, std::move(PD)});
  };
  const bool NeedDiag = !S->hasUnrecoverableErrorOccurred() && !IgnoreWarnings;
  for (auto *TmpD : S->decls()) {
    assert(TmpD && "This decl didn't get pushed??");

    assert(isa<NamedDecl>(TmpD) && "Decl isn't NamedDecl?");
    NamedDecl *D = cast<NamedDecl>(TmpD);

    if (NeedDiag) {
      const bool Referenced = D->isReferenced() || D->isUsed();
      if (LLVM_LIKELY(Referenced)) {
        if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
          DiagnoseUnusedButSetDecl(VD, addDiag);
          RefsMinusAssignments.erase(VD);
        }
      } else {
        DiagnoseUnusedDecl(D, addDiag);
        if (const auto *RD = dyn_cast<RecordDecl>(D))
          DiagnoseUnusedNestedTypedefs(RD, addDiag);
        if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
          DiagnoseUnusedButSetDecl(VD, addDiag);
          RefsMinusAssignments.erase(VD);
        }
      }
    } else if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
      RefsMinusAssignments.erase(VD);
    }

    if (!D->getDeclName())
      continue;

    if (LLVM_UNLIKELY(isa<LabelDecl>(D)))
      validatePoppedLabel(cast<LabelDecl>(D), *this, addDiag);

    IdResolver.RemoveDecl(D);
  }

  if (!DeclDiags.empty()) {
    llvm::sort(DeclDiags,
               [](const LocAndDiag &LHS, const LocAndDiag &RHS) -> bool {
                 return LHS.Loc.getRawEncoding() < RHS.Loc.getRawEncoding();
               });
    for (const LocAndDiag &D : DeclDiags) {
      Diag(D.Loc, D.PD);
      if (D.PreviousDeclLoc)
        Diag(*D.PreviousDeclLoc, diag::note_previous_declaration);
    }
  }
}

Scope *Sema::getNonFieldDeclScope(Scope *S) {
  while (((S->getFlags() & Scope::DeclScope) == 0) ||
         (S->getEntity() && S->getEntity()->isTransparentContext()) ||
         (S->isRecordScope()))
    S = S->getParent();
  return S;
}

namespace neverc {

llvm::StringRef getHeaderName(Builtin::Context &BuiltinInfo, unsigned ID,
                              TreeContext::GetBuiltinTypeError Error) {
  switch (Error) {
  case TreeContext::GE_None:
    return "";
  case TreeContext::GE_Missing_type:
    return BuiltinInfo.getHeaderName(ID);
  case TreeContext::GE_Missing_stdio:
    return "stdio.h";
  case TreeContext::GE_Missing_setjmp:
    return "setjmp.h";
  case TreeContext::GE_Missing_ucontext:
    return "ucontext.h";
  }
  llvm_unreachable("unhandled error kind");
}

} // namespace neverc

