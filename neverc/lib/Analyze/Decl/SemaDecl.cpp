#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
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

/// Attach an implicit `__attribute__((cleanup(<CleanupFD>)))` to `VD`.
/// Shared by `attachNeverCStringCleanup` (auto-released `string` locals)
/// and `attachNeverCWptrCleanup` (auto-released wide pointer locals);
/// both paths build the identical `AttributeCommonInfo` + `CleanupAttr`
/// triple, so keeping the spelling in one place avoids the next maintainer
/// fixing only half of the pattern when the implicit-attribute form
/// evolves.
void attachImplicitCleanupAttr(Sema &S, VarDecl *VD, FunctionDecl *CleanupFD) {
  AttributeCommonInfo Info(SourceRange(VD->getLocation()),
                           AttributeCommonInfo::AT_Cleanup,
                           AttributeCommonInfo::Form::Implicit());
  auto *Attr = ::new (S.Context) CleanupAttr(S.Context, Info, CleanupFD);
  Attr->setImplicit(true);
  VD->addAttr(Attr);
}

void attachNeverCStringCleanup(Sema &S, Scope *Sc, VarDecl *VD,
                               FunctionDecl *ParamOwner = nullptr,
                               bool IsFunctionDefinitionParam = false) {
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

bool isWptrProducingCall(Sema &S, const Expr *Init) {
  const auto *CE = dyn_cast_or_null<CallExpr>(Init->IgnoreParenImpCasts());
  if (!CE)
    return false;
  const auto *Callee = CE->getDirectCallee();
  return S.isNeverCStringWptrProducer(Callee);
}

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

} // end anonymous namespace

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

namespace {
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
} // namespace

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

namespace {
bool isOutOfScopePreviousDeclaration(NamedDecl *, DeclContext *, TreeContext &);
} // namespace

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

namespace {
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
} // namespace

// ===----------------------------------------------------------------------===
// Builtin creation & declaration merging
// ===----------------------------------------------------------------------===

FunctionDecl *Sema::CreateBuiltin(IdentifierInfo *II, QualType Type,
                                  unsigned ID, SourceLocation Loc) {
  DeclContext *Parent = Context.getTranslationUnitDecl();

  FunctionDecl *New = FunctionDecl::Create(Context, Parent, Loc, Loc, II, Type,
                                           /*TInfo=*/nullptr, SC_Extern,
                                           getCurFPFeatures().isFPConstrained(),
                                           false, Type->isFunctionProtoType());
  New->setImplicit();
  New->addAttr(BuiltinAttr::CreateImplicit(Context, ID));

  if (const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(Type)) {
    llvm::SmallVector<ParmVarDecl *, 16> Params;
    for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
      ParmVarDecl *parm = ParmVarDecl::Create(
          Context, New, SourceLocation(), SourceLocation(), nullptr,
          FT->getParamType(i), /*TInfo=*/nullptr, SC_None, nullptr);
      parm->setScopeInfo(0, i);
      Params.push_back(parm);
    }
    New->setParams(Params);
  }

  AddKnownFunctionAttributes(New);
  return New;
}

NamedDecl *Sema::LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID, Scope *S,
                                     bool ForRedeclaration,
                                     SourceLocation Loc) {
  TreeContext::GetBuiltinTypeError Error;
  QualType R = Context.GetBuiltinType(ID, Error);
  if (Error) {
    if (!ForRedeclaration)
      return nullptr;

    // If we have a builtin without an associated type we should not emit a
    // warning when we were not able to find a type for it.
    if (Error == TreeContext::GE_Missing_type ||
        Context.BuiltinInfo.allowTypeMismatch(ID))
      return nullptr;

    // If we could not find a type for setjmp it is because the jmp_buf type was
    // not defined prior to the setjmp declaration.
    if (Error == TreeContext::GE_Missing_setjmp) {
      Diag(Loc, diag::warn_implicit_decl_no_jmp_buf)
          << Context.BuiltinInfo.getName(ID);
      return nullptr;
    }

    // Generally, we emit a warning that the declaration requires the
    // appropriate header.
    Diag(Loc, diag::warn_implicit_decl_requires_sysheader)
        << getHeaderName(Context.BuiltinInfo, ID, Error)
        << Context.BuiltinInfo.getName(ID);
    return nullptr;
  }
  // NeverC silently accepts predefined libc functions referenced without
  // the matching `#include` so first-touch programs and out-of-order
  // intra-TU calls compile cleanly.  The linker resolves the implicit
  // declaration to the real libc symbol of the same name.
  if (R.isNull())
    return nullptr;

  FunctionDecl *New = CreateBuiltin(II, R, ID, Loc);
  RegisterLocallyScopedExternCDecl(New, S);

  // TUScope is the translation-unit scope to insert this function into.
  // We need to teach PushOnScopeChains to
  // relate Scopes to DeclContexts, and probably eliminate CurContext
  // entirely, but we're not there yet.
  DeclContext *SavedContext = CurContext;
  CurContext = New->getDeclContext();
  PushOnScopeChains(New, TUScope);
  CurContext = SavedContext;
  return New;
}

bool Sema::isIncompatibleTypedef(TypeDecl *Old, TypedefNameDecl *New) {
  QualType OldType;
  if (TypedefNameDecl *OldTypedef = dyn_cast<TypedefNameDecl>(Old))
    OldType = OldTypedef->getUnderlyingType();
  else
    OldType = Context.getTypeDeclType(Old);
  QualType NewType = New->getUnderlyingType();

  if (NewType->isVariablyModifiedType()) {
    // Must not redefine a typedef with a variably-modified type.
    Diag(New->getLocation(), diag::err_redefinition_variably_modified_typedef)
        << NewType;
    if (Old->getLocation().isValid())
      notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }

  if (OldType != NewType && !Context.hasSameType(OldType, NewType)) {
    Diag(New->getLocation(), diag::err_redefinition_different_typedef)
        << NewType << OldType;
    if (Old->getLocation().isValid())
      notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }
  return false;
}

void Sema::MergeTypedefNameDecl(Scope *S, TypedefNameDecl *New,
                                LookupResult &OldDecls) {
  // If the new decl is known invalid already, don't bother doing any
  // merging checks.
  if (New->isInvalidDecl())
    return;

  TypeDecl *Old = OldDecls.getAsSingle<TypeDecl>();
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();

    NamedDecl *OldD = OldDecls.getRepresentativeDecl();
    if (OldD->getLocation().isValid())
      notePreviousDefinition(OldD, New->getLocation());

    return New->setInvalidDecl();
  }

  // If the old declaration is invalid, just give up here.
  if (Old->isInvalidDecl())
    return New->setInvalidDecl();

  if (auto *OldTD = dyn_cast<TypedefNameDecl>(Old)) {
    auto *OldTag = OldTD->getAnonDeclWithTypedefName(/*AnyRedecl*/ true);
    auto *NewTag = New->getAnonDeclWithTypedefName();
    // The BuiltinString prelude planted typedefs (e.g. `typedef struct
    // __neverc_string { ... } string;`) into the <built-in> predefines
    // buffer.  When the user source redefines the same typedef name we
    // have to redirect the new typedef onto the pre-scan tag — otherwise
    // a function call whose return type resolved against the prelude
    // tag would look `incompatible` with a variable typed against the
    // user-source tag even though both describe the same record.
    bool BuiltinOldTypedef =
        Context.getSourceManager().isWrittenInBuiltinFile(OldTD->getLocation());
    if (OldTag && NewTag &&
        OldTag->getCanonicalDecl() != NewTag->getCanonicalDecl() &&
        BuiltinOldTypedef) {
      // The old decl is a synthetic pre-decl.  Reuse the old typedef's
      // type so both decls describe the same entity.
      New->setTypeForDecl(OldTD->getTypeForDecl());
      if (OldTD->isModed())
        New->setModedTypeSourceInfo(OldTD->getTypeSourceInfo(),
                                    OldTD->getUnderlyingType());
      else
        New->setTypeSourceInfo(OldTD->getTypeSourceInfo());

      // If this was an enumeration, yank all of its enumerators
      // out of the scope.
      if (isa<EnumDecl>(NewTag)) {
        Scope *EnumScope = getNonFieldDeclScope(S);
        for (auto *D : NewTag->decls()) {
          auto *ED = cast<EnumConstantDecl>(D);
          assert(EnumScope->isDeclScope(ED));
          EnumScope->RemoveDecl(ED);
          IdResolver.RemoveDecl(ED);
          ED->getLexicalDeclContext()->removeDecl(ED);
        }
      }
    } else if (BuiltinOldTypedef) {
      // The pre-scan already supplied a compatible typedef (e.g. a pure
      // typedef without a record body).  Just chain the redeclaration
      // without any compatibility diagnostic.
      New->setPreviousDecl(OldTD);
      return;
    }
  }

  // If the typedef types are not identical, reject them in all languages and
  // with any extensions enabled.
  if (isIncompatibleTypedef(Old, New))
    return;

  // The types match.  Link up the redeclaration chain and merge attributes if
  // the old declaration was a typedef.
  if (TypedefNameDecl *Typedef = dyn_cast<TypedefNameDecl>(Old)) {
    New->setPreviousDecl(Typedef);
    mergeDeclAttributes(New, Old);
  }

  if (getLangOpts().MicrosoftExt)
    return;

  // C11 always permits redefinition of typedefs.
  if (getLangOpts().C11)
    return;

  // If we have a redefinition of a typedef in C, emit a warning.  This warning
  // is normally mapped to an error, but can be controlled with
  // -Wtypedef-redefinition.  If either the original or the redefinition is
  // in a system header, don't emit this for compatibility with GCC.
  if (getDiagnostics().getSuppressSystemWarnings() &&
      // Some standard types are defined implicitly in NeverC.
      (Old->isImplicit() ||
       Context.getSourceManager().isInSystemHeader(Old->getLocation()) ||
       Context.getSourceManager().isInSystemHeader(New->getLocation())))
    return;

  Diag(New->getLocation(), diag::ext_redefinition_of_typedef)
      << New->getDeclName();
  notePreviousDefinition(Old, New->getLocation());
}

namespace {
bool hasMatchingAttr(const Decl *D, const Attr *A) {
  const AnnotateAttr *Ann = dyn_cast<AnnotateAttr>(A);
  for (const auto *i : D->attrs())
    if (i->getKind() == A->getKind()) {
      if (Ann) {
        if (Ann->getAnnotation() == cast<AnnotateAttr>(i)->getAnnotation())
          return true;
        continue;
      }
      return true;
    }

  return false;
}
} // namespace

namespace {
bool isAttrTargetDefinition(Decl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    return VD->isThisDeclarationADefinition();
  if (TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->isCompleteDefinition() || TD->isBeingDefined();
  return true;
}
} // namespace

namespace {
bool mergeAlignedAttrs(Sema &S, NamedDecl *New, Decl *Old) {
  // Look for alignas attributes on Old, and pick out whichever attribute
  // specifies the strictest alignment requirement.
  AlignedAttr *OldAlignasAttr = nullptr;
  AlignedAttr *OldStrictestAlignAttr = nullptr;
  unsigned OldAlign = 0;
  for (auto *I : Old->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return false;

    if (I->isAlignas())
      OldAlignasAttr = I;

    unsigned Align = I->getAlignment(S.Context);
    if (Align > OldAlign) {
      OldAlign = Align;
      OldStrictestAlignAttr = I;
    }
  }

  // Look for alignas attributes on New.
  AlignedAttr *NewAlignasAttr = nullptr;
  unsigned NewAlign = 0;
  for (auto *I : New->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return false;

    if (I->isAlignas())
      NewAlignasAttr = I;

    unsigned Align = I->getAlignment(S.Context);
    if (Align > NewAlign)
      NewAlign = Align;
  }

  if (OldAlignasAttr && NewAlignasAttr && OldAlign != NewAlign) {
    // Both declarations have 'alignas' attributes. We require them to match
    // (per alignment consistency rules for multiple declarations).

    // If either declaration only contains 'alignas(0)' specifiers, then it
    // specifies the natural alignment for the type.
    if (OldAlign == 0 || NewAlign == 0) {
      QualType Ty;
      if (ValueDecl *VD = dyn_cast<ValueDecl>(New))
        Ty = VD->getType();
      else
        Ty = S.Context.getTagDeclType(cast<TagDecl>(New));

      if (OldAlign == 0)
        OldAlign = S.Context.getTypeAlign(Ty);
      if (NewAlign == 0)
        NewAlign = S.Context.getTypeAlign(Ty);
    }

    if (OldAlign != NewAlign) {
      S.Diag(NewAlignasAttr->getLocation(), diag::err_alignas_mismatch)
          << (unsigned)S.Context.toCharUnitsFromBits(OldAlign).getQuantity()
          << (unsigned)S.Context.toCharUnitsFromBits(NewAlign).getQuantity();
      S.Diag(OldAlignasAttr->getLocation(), diag::note_previous_declaration);
    }
  }

  if (OldAlignasAttr && !NewAlignasAttr && isAttrTargetDefinition(New)) {
    // Definition must carry alignas when any declaration did (and vice versa).
    S.Diag(New->getLocation(), diag::err_alignas_missing_on_definition)
        << OldAlignasAttr;
    S.Diag(OldAlignasAttr->getLocation(), diag::note_alignas_on_declaration)
        << OldAlignasAttr;
  }

  bool AnyAdded = false;

  // Ensure we have an attribute representing the strictest alignment.
  if (OldAlign > NewAlign) {
    AlignedAttr *Clone = OldStrictestAlignAttr->clone(S.Context);
    Clone->setInherited(true);
    New->addAttr(Clone);
    AnyAdded = true;
  }

  // Ensure we have an alignas attribute if the old declaration had one.
  if (OldAlignasAttr && !NewAlignasAttr &&
      !(AnyAdded && OldStrictestAlignAttr->isAlignas())) {
    AlignedAttr *Clone = OldAlignasAttr->clone(S.Context);
    Clone->setInherited(true);
    New->addAttr(Clone);
    AnyAdded = true;
  }

  return AnyAdded;
}
} // namespace

#define WANT_DECL_MERGE_LOGIC
#include "neverc/Analyze/AttrParsedAttrImpl.td.h"
#undef WANT_DECL_MERGE_LOGIC

namespace {
bool mergeDeclAttribute(Sema &S, NamedDecl *D, const InheritableAttr *Attr,
                        Sema::AvailabilityMergeKind AMK) {
  // Diagnose any mutual exclusions between the attribute that we want to add
  // and attributes that already exist on the declaration.
  if (!DiagnoseMutualExclusions(S, D, Attr))
    return false;

  // This function copies an attribute Attr from a previous declaration to the
  // new declaration D if the new declaration doesn't itself have that attribute
  // yet or if that attribute allows duplicates.
  // If you're adding a new attribute that requires logic different from
  // "use explicit attribute on decl if present, else use attribute from
  // previous decl", for example if the attribute needs to be consistent
  // between redeclarations, you need to call a custom merge function here.
  InheritableAttr *NewAttr = nullptr;
  if (const auto *AA = dyn_cast<AvailabilityAttr>(Attr))
    NewAttr = S.mergeAvailabilityAttr(
        D, *AA, AA->getPlatform(), AA->isImplicit(), AA->getIntroduced(),
        AA->getDeprecated(), AA->getObsoleted(), AA->getUnavailable(),
        AA->getMessage(), AA->getStrict(), AA->getReplacement(),
        AA->getPriority());
  else if (const auto *VA = dyn_cast<VisibilityAttr>(Attr))
    NewAttr = S.mergeVisibilityAttr(D, *VA, VA->getVisibility());
  else if (const auto *VA = dyn_cast<TypeVisibilityAttr>(Attr))
    NewAttr = S.mergeTypeVisibilityAttr(D, *VA, VA->getVisibility());
  else if (const auto *ImportA = dyn_cast<DLLImportAttr>(Attr))
    NewAttr = S.mergeDLLImportAttr(D, *ImportA);
  else if (const auto *ExportA = dyn_cast<DLLExportAttr>(Attr))
    NewAttr = S.mergeDLLExportAttr(D, *ExportA);
  else if (const auto *EA = dyn_cast<ErrorAttr>(Attr))
    NewAttr = S.mergeErrorAttr(D, *EA, EA->getUserDiagnostic());
  else if (const auto *FA = dyn_cast<FormatAttr>(Attr))
    NewAttr = S.mergeFormatAttr(D, *FA, FA->getType(), FA->getFormatIdx(),
                                FA->getFirstArg());
  else if (const auto *SA = dyn_cast<SectionAttr>(Attr))
    NewAttr = S.mergeSectionAttr(D, *SA, SA->getName());
  else if (const auto *CSA = dyn_cast<CodeSegAttr>(Attr))
    NewAttr = S.mergeCodeSegAttr(D, *CSA, CSA->getName());
  else if (const auto *AA = dyn_cast<AlwaysInlineAttr>(Attr))
    NewAttr = S.mergeAlwaysInlineAttr(D, *AA,
                                      &S.Context.Idents.get(AA->getSpelling()));
  else if (const auto *MA = dyn_cast<MinSizeAttr>(Attr))
    NewAttr = S.mergeMinSizeAttr(D, *MA);
  else if (const auto *OA = dyn_cast<OptimizeNoneAttr>(Attr))
    NewAttr = S.mergeOptimizeNoneAttr(D, *OA);
  else if (const auto *InternalLinkageA = dyn_cast<InternalLinkageAttr>(Attr))
    NewAttr = S.mergeInternalLinkageAttr(D, *InternalLinkageA);
  else if (isa<AlignedAttr>(Attr))
    // AlignedAttrs are handled separately, because we need to handle all
    // such attributes on a declaration at the same time.
    NewAttr = nullptr;
  else if (const auto *TCBA = dyn_cast<EnforceTCBAttr>(Attr))
    NewAttr = S.mergeEnforceTCBAttr(D, *TCBA);
  else if (const auto *TCBLA = dyn_cast<EnforceTCBLeafAttr>(Attr))
    NewAttr = S.mergeEnforceTCBLeafAttr(D, *TCBLA);
  else if (const auto *BTFA = dyn_cast<BTFDeclTagAttr>(Attr))
    NewAttr = S.mergeBTFDeclTagAttr(D, *BTFA);
  else if (Attr->shouldInheritEvenIfAlreadyPresent() ||
           !hasMatchingAttr(D, Attr))
    NewAttr = cast<InheritableAttr>(Attr->clone(S.Context));

  if (NewAttr) {
    NewAttr->setInherited(true);
    D->addAttr(NewAttr);
    return true;
  }

  return false;
}
} // namespace

namespace {
const NamedDecl *getDefinition(const Decl *D) {
  if (const TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    const VarDecl *Def = VD->getDefinition();
    if (Def)
      return Def;
    return VD->getActingDefinition();
  }
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionDecl *Def = nullptr;
    if (FD->isDefined(Def))
      return Def;
  }
  return nullptr;
}
} // namespace

namespace {
bool hasAttribute(const Decl *D, attr::Kind Kind) {
  for (const auto *Attribute : D->attrs())
    if (Attribute->getKind() == Kind)
      return true;
  return false;
}
} // namespace

namespace {
void checkNewAttributesAfterDef(Sema &S, Decl *New, const Decl *Old) {
  if (!New->hasAttrs())
    return;

  const NamedDecl *Def = getDefinition(Old);
  if (!Def || Def == New)
    return;

  AttrVec &NewAttributes = New->getAttrs();
  for (unsigned I = 0, E = NewAttributes.size(); I != E;) {
    const Attr *NewAttribute = NewAttributes[I];

    if (isa<AliasAttr>(NewAttribute) || isa<IFuncAttr>(NewAttribute)) {
      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(New)) {
        Sema::SkipBodyInfo SkipBody;
        S.CheckForFunctionRedefinition(FD, cast<FunctionDecl>(Def), &SkipBody);

        // If we're skipping this definition, drop the "alias" attribute.
        if (SkipBody.ShouldSkip) {
          NewAttributes.erase(NewAttributes.begin() + I);
          --E;
          continue;
        }
      } else {
        VarDecl *VD = cast<VarDecl>(New);
        unsigned Diag = cast<VarDecl>(Def)->isThisDeclarationADefinition() ==
                                VarDecl::TentativeDefinition
                            ? diag::err_alias_after_tentative
                            : diag::err_redefinition;
        S.Diag(VD->getLocation(), Diag) << VD->getDeclName();
        if (Diag == diag::err_redefinition)
          S.notePreviousDefinition(Def, VD->getLocation());
        else
          S.Diag(Def->getLocation(), diag::note_previous_definition);
        VD->setInvalidDecl();
      }
      ++I;
      continue;
    }

    if (const VarDecl *VD = dyn_cast<VarDecl>(Def)) {
      // Tentative definitions are only interesting for the alias check above.
      if (VD->isThisDeclarationADefinition() != VarDecl::Definition) {
        ++I;
        continue;
      }
    }

    if (hasAttribute(Def, NewAttribute->getKind())) {
      ++I;
      continue; // regular attr merging will take care of validating this.
    }

    if (isa<C11NoReturnAttr>(NewAttribute)) {
      // C's _Noreturn is allowed to be added to a function after it is defined.
      ++I;
      continue;
    } else if (const AlignedAttr *AA = dyn_cast<AlignedAttr>(NewAttribute)) {
      if (AA->isAlignas()) {
        // C11 6.7.5/7: if the definition has no alignment specifier, other
        // declarations of that object shall also have none (alignas mismatch).
        S.Diag(Def->getLocation(), diag::err_alignas_missing_on_definition)
            << AA;
        S.Diag(NewAttribute->getLocation(), diag::note_alignas_on_declaration)
            << AA;
        NewAttributes.erase(NewAttributes.begin() + I);
        --E;
        continue;
      }
    } else if (isa<LoaderUninitializedAttr>(NewAttribute)) {
      // Redeclaration with this attribute conflicts with the prior definition.
      {
        S.Diag(NewAttribute->getLocation(),
               diag::err_loader_uninitialized_redeclaration);
        S.Diag(Def->getLocation(), diag::note_previous_definition);
        NewAttributes.erase(NewAttributes.begin() + I);
        --E;
        continue;
      }
    } else if (isa<SelectAnyAttr>(NewAttribute) &&
               cast<VarDecl>(New)->isInline() &&
               !cast<VarDecl>(New)->isInlineSpecified()) {
      // Don't warn about applying selectany to implicitly inline variables.
      // Older compilers and language modes would require the use of selectany
      // to make such variables inline, and it would have no effect if we
      // honored it.
      ++I;
      continue;
    }

    S.Diag(NewAttribute->getLocation(),
           diag::warn_attribute_precede_definition);
    S.Diag(Def->getLocation(), diag::note_previous_definition);
    NewAttributes.erase(NewAttributes.begin() + I);
    --E;
  }
}
} // namespace

void Sema::mergeDeclAttributes(NamedDecl *New, Decl *Old,
                               AvailabilityMergeKind AMK) {
  if (UsedAttr *OldAttr = Old->getMostRecentDecl()->getAttr<UsedAttr>()) {
    UsedAttr *NewAttr = OldAttr->clone(Context);
    NewAttr->setInherited(true);
    New->addAttr(NewAttr);
  }
  if (RetainAttr *OldAttr = Old->getMostRecentDecl()->getAttr<RetainAttr>()) {
    RetainAttr *NewAttr = OldAttr->clone(Context);
    NewAttr->setInherited(true);
    New->addAttr(NewAttr);
  }

  if (!Old->hasAttrs() && !New->hasAttrs())
    return;

  // Attributes declared post-definition are currently ignored.
  checkNewAttributesAfterDef(*this, New, Old);

  if (AsmLabelAttr *NewA = New->getAttr<AsmLabelAttr>()) {
    if (AsmLabelAttr *OldA = Old->getAttr<AsmLabelAttr>()) {
      if (!OldA->isEquivalent(NewA)) {
        // This redeclaration changes __asm__ label.
        Diag(New->getLocation(), diag::err_different_asm_label);
        Diag(OldA->getLocation(), diag::note_previous_declaration);
      }
    } else if (Old->isUsed()) {
      // This redeclaration adds an __asm__ label to a declaration that has
      // already been ODR-used.
      Diag(New->getLocation(), diag::err_late_asm_label_name)
          << isa<FunctionDecl>(Old) << New->getAttr<AsmLabelAttr>()->getRange();
    }
  }

  // This redeclaration adds a section attribute.
  if (New->hasAttr<SectionAttr>() && !Old->hasAttr<SectionAttr>()) {
    if (auto *VD = dyn_cast<VarDecl>(New)) {
      if (VD->isThisDeclarationADefinition() == VarDecl::DeclarationOnly) {
        Diag(New->getLocation(), diag::warn_attribute_section_on_redeclaration);
        Diag(Old->getLocation(), diag::note_previous_declaration);
      }
    }
  }

  if (!Old->hasAttrs())
    return;

  bool foundAny = New->hasAttrs();

  // Ensure that any moving of objects within the allocated map is done before
  // we process them.
  if (!foundAny)
    New->setAttrs(AttrVec());

  for (auto *I : Old->specific_attrs<InheritableAttr>()) {
    // Ignore deprecated/unavailable/availability attributes if requested.
    AvailabilityMergeKind LocalAMK = AMK_None;
    if (isa<DeprecatedAttr>(I) || isa<UnavailableAttr>(I) ||
        isa<AvailabilityAttr>(I)) {
      switch (AMK) {
      case AMK_None:
        continue;

      case AMK_Redeclaration:
        LocalAMK = AMK;
        break;
      }
    }

    // Already handled.
    if (isa<UsedAttr>(I) || isa<RetainAttr>(I))
      continue;

    if (mergeDeclAttribute(*this, New, I, LocalAMK))
      foundAny = true;
  }

  if (mergeAlignedAttrs(*this, New, Old))
    foundAny = true;

  if (!foundAny)
    New->dropAttrs();
}

namespace {
void mergeParamDeclAttributes(ParmVarDecl *newDecl, const ParmVarDecl *oldDecl,
                              Sema &S) {
  // If any declaration carries carries_dependency, the first must too.
  const CarriesDependencyAttr *CDA = newDecl->getAttr<CarriesDependencyAttr>();
  if (CDA && !oldDecl->hasAttr<CarriesDependencyAttr>()) {
    S.Diag(CDA->getLocation(),
           diag::err_carries_dependency_missing_on_first_decl)
        << 1 /*Param*/;
    const FunctionDecl *FirstFD =
        cast<FunctionDecl>(oldDecl->getDeclContext())->getFirstDecl();
    const ParmVarDecl *FirstVD =
        FirstFD->getParamDecl(oldDecl->getFunctionScopeIndex());
    S.Diag(FirstVD->getLocation(),
           diag::note_carries_dependency_missing_first_decl)
        << 1 /*Param*/;
  }

  if (!oldDecl->hasAttrs())
    return;

  bool foundAny = newDecl->hasAttrs();

  // Ensure that any moving of objects within the allocated map is
  // done before we process them.
  if (!foundAny)
    newDecl->setAttrs(AttrVec());

  for (const auto *I : oldDecl->specific_attrs<InheritableParamAttr>()) {
    if (!hasMatchingAttr(newDecl, I)) {
      InheritableAttr *newAttr =
          cast<InheritableParamAttr>(I->clone(S.Context));
      newAttr->setInherited(true);
      newDecl->addAttr(newAttr);
      foundAny = true;
    }
  }

  if (!foundAny)
    newDecl->dropAttrs();
}
} // namespace

namespace {
bool equivalentArrayTypes(QualType Old, QualType New, const TreeContext &Ctx) {

  auto NoSizeInfo = [&Ctx](QualType Ty) {
    if (Ty->isIncompleteArrayType() || Ty->isPointerType())
      return true;
    if (const auto *VAT = Ctx.getAsVariableArrayType(Ty))
      return VAT->getSizeModifier() == ArraySizeModifier::Star;
    return false;
  };

  // `type[]` is equivalent to `type *` and `type[*]`.
  if (NoSizeInfo(Old) && NoSizeInfo(New))
    return true;

  // Don't try to compare VLA sizes, unless one of them has the star modifier.
  if (Old->isVariableArrayType() && New->isVariableArrayType()) {
    const auto *OldVAT = Ctx.getAsVariableArrayType(Old);
    const auto *NewVAT = Ctx.getAsVariableArrayType(New);
    if ((OldVAT->getSizeModifier() == ArraySizeModifier::Star) ^
        (NewVAT->getSizeModifier() == ArraySizeModifier::Star))
      return false;
    return true;
  }

  // Only compare size, ignore Size modifiers and CVR.
  if (Old->isConstantArrayType() && New->isConstantArrayType()) {
    return Ctx.getAsConstantArrayType(Old)->getSize() ==
           Ctx.getAsConstantArrayType(New)->getSize();
  }

  return Old == New;
}
} // namespace

namespace {
void mergeParamDeclTypes(ParmVarDecl *NewParam, const ParmVarDecl *OldParam,
                         Sema &S) {
  if (auto Oldnullability = OldParam->getType()->getNullability()) {
    if (auto Newnullability = NewParam->getType()->getNullability()) {
      if (*Oldnullability != *Newnullability) {
        S.Diag(NewParam->getLocation(), diag::warn_mismatched_nullability_attr)
            << DiagNullabilityKind(*Newnullability,
                                   /*IsContextSensitive=*/false)
            << DiagNullabilityKind(*Oldnullability,
                                   /*IsContextSensitive=*/false);
        S.Diag(OldParam->getLocation(), diag::note_previous_declaration);
      }
    } else {
      QualType NewT = NewParam->getType();
      NewT = S.Context.getAttributedType(
          AttributedType::getNullabilityAttrKind(*Oldnullability), NewT, NewT);
      NewParam->setType(NewT);
    }
  }
  const auto *OldParamDT = dyn_cast<DecayedType>(OldParam->getType());
  const auto *NewParamDT = dyn_cast<DecayedType>(NewParam->getType());
  if (OldParamDT && NewParamDT &&
      OldParamDT->getPointeeType() == NewParamDT->getPointeeType()) {
    QualType OldParamOT = OldParamDT->getOriginalType();
    QualType NewParamOT = NewParamDT->getOriginalType();
    if (!equivalentArrayTypes(OldParamOT, NewParamOT, S.getTreeContext())) {
      S.Diag(NewParam->getLocation(), diag::warn_inconsistent_array_form)
          << NewParam << NewParamOT;
      S.Diag(OldParam->getLocation(), diag::note_previous_declaration_as)
          << OldParamOT;
    }
  }
}
} // namespace

namespace {

struct GNUCompatibleParamWarning {
  ParmVarDecl *OldParm;
  ParmVarDecl *NewParm;
  QualType PromotedType;
};

} // end anonymous namespace

// Determine whether the previous declaration was a definition, implicit
// declaration, or a declaration.
namespace {
template <typename T>
std::pair<diag::kind, SourceLocation>
getNoteDiagForInvalidRedeclaration(const T *Old, const T *New) {
  diag::kind PrevDiag;
  SourceLocation OldLocation = Old->getLocation();
  if (Old->isThisDeclarationADefinition())
    PrevDiag = diag::note_previous_definition;
  else if (Old->isImplicit()) {
    PrevDiag = diag::note_previous_implicit_declaration;
    if (const auto *FD = dyn_cast<FunctionDecl>(Old)) {
      if (FD->getBuiltinID())
        PrevDiag = diag::note_previous_builtin_declaration;
    }
    if (OldLocation.isInvalid())
      OldLocation = New->getLocation();
  } else
    PrevDiag = diag::note_previous_declaration;
  return std::make_pair(PrevDiag, OldLocation);
}
} // namespace

namespace {
bool canRedefineFunction(const FunctionDecl *FD, const LangOptions &LangOpts) {
  return ((FD->hasAttr<GNUInlineAttr>() || LangOpts.GNUInline) &&
          FD->isInlineSpecified() && FD->getStorageClass() == SC_Extern);
}
} // namespace

const AttributedType *Sema::getCallingConvAttributedType(QualType T) const {
  const AttributedType *AT = T->getAs<AttributedType>();
  while (AT && !AT->isCallingConv())
    AT = AT->getModifiedType()->getAs<AttributedType>();
  return AT;
}

namespace {
bool hasIdenticalPassObjectSizeAttrs(const FunctionDecl *A,
                                     const FunctionDecl *B) {
  assert(A->getNumParams() == B->getNumParams());

  auto AttrEq = [](const ParmVarDecl *A, const ParmVarDecl *B) {
    const auto *AttrA = A->getAttr<PassObjectSizeAttr>();
    const auto *AttrB = B->getAttr<PassObjectSizeAttr>();
    if (AttrA == AttrB)
      return true;
    return AttrA && AttrB && AttrA->getType() == AttrB->getType() &&
           AttrA->isDynamic() == AttrB->isDynamic();
  };

  return std::equal(A->param_begin(), A->param_end(), B->param_begin(), AttrEq);
}
} // namespace

// ===----------------------------------------------------------------------===
// Function & variable declaration merging
// ===----------------------------------------------------------------------===

bool Sema::MergeFunctionDecl(FunctionDecl *New, NamedDecl *&OldD, Scope *S,
                             bool MergeTypeWithOld, bool NewDeclIsDefn) {
  FunctionDecl *Old = OldD->getAsFunction();
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();
    notePreviousDefinition(OldD, New->getLocation());
    return true;
  }

  // If the old declaration is invalid, just give up here.
  if (Old->isInvalidDecl())
    return true;

  // Disallow redeclaration of some builtins.
  if (!getTreeContext().canBuiltinBeRedeclared(Old)) {
    Diag(New->getLocation(), diag::err_builtin_redeclare) << Old->getDeclName();
    Diag(Old->getLocation(), diag::note_previous_builtin_declaration)
        << Old << Old->getType();
    return true;
  }

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);

  // Don't complain about this if we're in GNU89 mode and the old function
  // is an extern inline function.
  // Don't complain about specializations. They are not supposed to have
  // storage classes.
  if (New->getStorageClass() == SC_Static && Old->hasExternalFormalLinkage() &&
      !canRedefineFunction(Old, getLangOpts())) {
    if (getLangOpts().MicrosoftExt) {
      Diag(New->getLocation(), diag::ext_static_non_static) << New;
      Diag(OldLocation, PrevDiag) << Old << Old->getType();
    } else {
      Diag(New->getLocation(), diag::err_static_non_static) << New;
      Diag(OldLocation, PrevDiag) << Old << Old->getType();
      return true;
    }
  }

  if (const auto *ILA = New->getAttr<InternalLinkageAttr>())
    if (!Old->hasAttr<InternalLinkageAttr>()) {
      Diag(New->getLocation(), diag::err_attribute_missing_on_first_decl)
          << ILA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<InternalLinkageAttr>();
    }

  if (auto *EA = New->getAttr<ErrorAttr>()) {
    if (!Old->hasAttr<ErrorAttr>()) {
      Diag(EA->getLocation(), diag::err_attribute_missing_on_first_decl) << EA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<ErrorAttr>();
    }
  }

  {
    bool OldOvl = Old->hasAttr<OverloadableAttr>();
    if (OldOvl != New->hasAttr<OverloadableAttr>() && !Old->isImplicit()) {
      Diag(New->getLocation(), diag::err_attribute_overloadable_mismatch)
          << New << OldOvl;

      // Try our best to find a decl that actually has the overloadable
      // attribute for the note. In most cases (e.g. programs with only one
      // broken declaration/definition), this won't matter.
      //
      const Decl *DiagOld = Old;
      if (OldOvl) {
        auto OldIter = llvm::find_if(Old->redecls(), [](const Decl *D) {
          const auto *A = D->getAttr<OverloadableAttr>();
          return A && !A->isImplicit();
        });
        // If we've implicitly added *all* of the overloadable attrs to this
        // chain, emitting a "previous redecl" note is pointless.
        DiagOld = OldIter == Old->redecls_end() ? nullptr : *OldIter;
      }

      if (DiagOld)
        Diag(DiagOld->getLocation(),
             diag::note_attribute_overloadable_prev_overload)
            << OldOvl;

      if (OldOvl)
        New->addAttr(OverloadableAttr::CreateImplicit(Context));
      else
        New->dropAttr<OverloadableAttr>();
    }
  }

  // It is not permitted to redeclare an SME function with different SME
  // attributes.
  if (IsInvalidSMECallConversion(Old->getType(), New->getType(),
                                 AArch64SMECallConversionKind::MatchExactly)) {
    Diag(New->getLocation(), diag::err_sme_attr_mismatch)
        << New->getType() << Old->getType();
    Diag(OldLocation, diag::note_previous_declaration);
    return true;
  }

  // If a function is first declared with a calling convention, but is later
  // declared or defined without one, all following decls assume the calling
  // convention of the first.
  //
  // It's OK if a function is first declared without a calling convention,
  // but is later declared or defined with the default calling convention.
  //
  // To test if either decl has an explicit calling convention, we look for
  // AttributedType sugar nodes on the type as written.  If they are missing or
  // were canonicalized away, we assume the calling convention was implicit.
  //
  // Note also that we DO NOT return at this point, because we still have
  // other tests to run.
  QualType OldQType = Context.getCanonicalType(Old->getType());
  QualType NewQType = Context.getCanonicalType(New->getType());
  const FunctionType *OldType = cast<FunctionType>(OldQType);
  const FunctionType *NewType = cast<FunctionType>(NewQType);
  FunctionType::ExtInfo OldTypeInfo = OldType->getExtInfo();
  FunctionType::ExtInfo NewTypeInfo = NewType->getExtInfo();
  bool RequiresAdjustment = false;

  if (OldTypeInfo.getCC() != NewTypeInfo.getCC()) {
    FunctionDecl *First = Old->getFirstDecl();
    const FunctionType *FT =
        First->getType().getCanonicalType()->castAs<FunctionType>();
    FunctionType::ExtInfo FI = FT->getExtInfo();
    bool NewCCExplicit = getCallingConvAttributedType(New->getType());
    if (!NewCCExplicit) {
      // Inherit the CC from the previous declaration if it was specified
      // there but not here.
      NewTypeInfo = NewTypeInfo.withCallingConv(OldTypeInfo.getCC());
      RequiresAdjustment = true;
    } else if (Old->getBuiltinID()) {
      // Builtin attribute isn't propagated to the new one yet at this point,
      // so we check if the old one is a builtin.

      // Calling Conventions on a Builtin aren't really useful and setting a
      // default calling convention and cdecl'ing some builtin redeclarations is
      // common, so warn and ignore the calling convention on the redeclaration.
      Diag(New->getLocation(), diag::warn_cconv_unsupported)
          << FunctionType::getNameForCallConv(NewTypeInfo.getCC())
          << (int)CallingConventionIgnoredReason::BuiltinFunction;
      NewTypeInfo = NewTypeInfo.withCallingConv(OldTypeInfo.getCC());
      RequiresAdjustment = true;
    } else {
      // Calling conventions aren't compatible, so complain.
      bool FirstCCExplicit = getCallingConvAttributedType(First->getType());
      Diag(New->getLocation(), diag::err_cconv_change)
          << FunctionType::getNameForCallConv(NewTypeInfo.getCC())
          << !FirstCCExplicit
          << (!FirstCCExplicit ? ""
                               : FunctionType::getNameForCallConv(FI.getCC()));

      // Put the note on the first decl, since it is the one that matters.
      Diag(First->getLocation(), diag::note_previous_declaration);
      return true;
    }
  }

  if (OldTypeInfo.getNoReturn() && !NewTypeInfo.getNoReturn()) {
    NewTypeInfo = NewTypeInfo.withNoReturn(true);
    RequiresAdjustment = true;
  }

  // Merge regparm attribute.
  if (OldTypeInfo.getHasRegParm() != NewTypeInfo.getHasRegParm() ||
      OldTypeInfo.getRegParm() != NewTypeInfo.getRegParm()) {
    if (NewTypeInfo.getHasRegParm()) {
      Diag(New->getLocation(), diag::err_regparm_mismatch)
          << NewType->getRegParmType() << OldType->getRegParmType();
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withRegParm(OldTypeInfo.getRegParm());
    RequiresAdjustment = true;
  }

  if (OldTypeInfo.getNoCallerSavedRegs() !=
      NewTypeInfo.getNoCallerSavedRegs()) {
    if (NewTypeInfo.getNoCallerSavedRegs()) {
      AnyX86NoCallerSavedRegistersAttr *Attr =
          New->getAttr<AnyX86NoCallerSavedRegistersAttr>();
      Diag(New->getLocation(), diag::err_function_attribute_mismatch) << Attr;
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withNoCallerSavedRegs(true);
    RequiresAdjustment = true;
  }

  if (RequiresAdjustment) {
    const FunctionType *AdjustedType = New->getType()->getAs<FunctionType>();
    AdjustedType = Context.adjustFunctionType(AdjustedType, NewTypeInfo);
    New->setType(QualType(AdjustedType, 0));
    NewQType = Context.getCanonicalType(New->getType());
  }

  // If this redeclaration makes the function inline, we may need to add it to
  // UndefinedButUsed.
  if (!Old->isInlined() && New->isInlined() && !New->hasAttr<GNUInlineAttr>() &&
      !getLangOpts().GNUInline && Old->isUsed(false) && !Old->isDefined() &&
      !New->isThisDeclarationADefinition())
    UndefinedButUsed.insert(
        std::make_pair(Old->getCanonicalDecl(), SourceLocation()));

  // If this redeclaration makes it newly gnu_inline, we don't want to warn
  // about it.
  if (New->hasAttr<GNUInlineAttr>() && Old->isInlined() &&
      !Old->hasAttr<GNUInlineAttr>()) {
    UndefinedButUsed.erase(Old->getCanonicalDecl());
  }

  // If pass_object_size params don't match up perfectly, this isn't a valid
  // redeclaration.
  if (Old->getNumParams() > 0 && Old->getNumParams() == New->getNumParams() &&
      !hasIdenticalPassObjectSizeAttrs(Old, New)) {
    Diag(New->getLocation(), diag::err_different_pass_object_size_params)
        << New->getDeclName();
    Diag(OldLocation, PrevDiag) << Old << Old->getType();
    return true;
  }

  // C: Function types need to be compatible, not identical. This handles
  // duplicate function decls like "void f(int); void f(enum X);" properly.
  {
    // Prototype vs K&R definition: parameter counts must match and each
    // parameter type must be compatible after default argument promotions.
    // This cannot be handled by TreeContext::typesAreCompatible() because that
    // doesn't know whether the function type is for a definition or not when
    // eventually calling TreeContext::mergeFunctionTypes(). The only situation
    // we need to cover here is that the number of arguments agree as the
    // default argument promotion rules were already checked by
    // TreeContext::typesAreCompatible().
    if (Old->hasPrototype() && !New->hasWrittenPrototype() && NewDeclIsDefn &&
        Old->getNumParams() != New->getNumParams() && !Old->isImplicit()) {
      if (Old->hasInheritedPrototype())
        Old = Old->getCanonicalDecl();
      Diag(New->getLocation(), diag::err_conflicting_types) << New;
      Diag(Old->getLocation(), PrevDiag) << Old << Old->getType();
      return true;
    }

    // If we are merging two functions where only one of them has a prototype,
    // we may have enough information to decide to issue a diagnostic that the
    // function without a protoype will change behavior in C23. This handles
    // cases like:
    //   void i(); void i(int j);
    //   void i(int j); void i();
    //   void i(); void i(int j) {}
    // See OnFinishFunctionBody() for other cases of the behavior change
    // diagnostic. See GetFullTypeForDeclarator() for handling of a function
    // type without a prototype.
    if (New->hasWrittenPrototype() != Old->hasWrittenPrototype() &&
        !New->isImplicit() && !Old->isImplicit()) {
      const FunctionDecl *WithProto, *WithoutProto;
      if (New->hasWrittenPrototype()) {
        WithProto = New;
        WithoutProto = Old;
      } else {
        WithProto = Old;
        WithoutProto = New;
      }

      if (WithProto->getNumParams() != 0) {
        if (WithoutProto->getBuiltinID() == 0 && !WithoutProto->isImplicit()) {
          // The one without the prototype will be changing behavior in C23, so
          // warn about that one so long as it's a user-visible declaration.
          bool IsWithoutProtoADef = false, IsWithProtoADef = false;
          if (WithoutProto == New)
            IsWithoutProtoADef = NewDeclIsDefn;
          else
            IsWithProtoADef = NewDeclIsDefn;
          Diag(WithoutProto->getLocation(),
               diag::warn_non_prototype_changes_behavior)
              << IsWithoutProtoADef << (WithoutProto->getNumParams() ? 0 : 1)
              << (WithoutProto == Old) << IsWithProtoADef;

          // The reason the one without the prototype will be changing behavior
          // is because of the one with the prototype, so note that so long as
          // it's a user-visible declaration. There is one exception to this:
          // when the new declaration is a definition without a prototype, the
          // old declaration with a prototype is not the cause of the issue,
          // and that does not need to be noted because the one with a
          // prototype will not change behavior in C23.
          if (WithProto->getBuiltinID() == 0 && !WithProto->isImplicit() &&
              !IsWithoutProtoADef)
            Diag(WithProto->getLocation(), diag::note_conflicting_prototype);
        }
      }
    }

    if (Context.typesAreCompatible(OldQType, NewQType)) {
      const FunctionType *OldFuncType = OldQType->getAs<FunctionType>();
      const FunctionType *NewFuncType = NewQType->getAs<FunctionType>();
      const FunctionProtoType *OldProto = nullptr;
      if (MergeTypeWithOld && isa<FunctionNoProtoType>(NewFuncType) &&
          (OldProto = dyn_cast<FunctionProtoType>(OldFuncType))) {
        // The old declaration provided a function prototype, but the
        // new declaration does not. Merge in the prototype.
        assert(!OldProto->hasExceptionSpec() && "Exception spec in C");
        NewQType = Context.getFunctionType(NewFuncType->getReturnType(),
                                           OldProto->getParamTypes(),
                                           OldProto->getExtProtoInfo());
        New->setType(NewQType);
        New->setHasInheritedPrototype();

        // Synthesize parameters with the same types.
        llvm::SmallVector<ParmVarDecl *, 16> Params;
        for (const auto &ParamType : OldProto->param_types()) {
          ParmVarDecl *Param = ParmVarDecl::Create(
              Context, New, SourceLocation(), SourceLocation(), nullptr,
              ParamType, /*TInfo=*/nullptr, SC_None, nullptr);
          Param->setScopeInfo(0, Params.size());
          Param->setImplicit();
          Params.push_back(Param);
        }

        New->setParams(Params);
      }

      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
    }
  }

  // Check if the function types are compatible when pointer size address
  // spaces are ignored.
  if (Context.hasSameFunctionTypeIgnoringPtrSizes(OldQType, NewQType))
    return false;

  // GNU C permits a K&R definition to follow a prototype declaration
  // if the declared types of the parameters in the K&R definition
  // match the types in the prototype declaration, even when the
  // promoted types of the parameters from the K&R definition differ
  // from the types in the prototype. GCC then keeps the types from
  // the prototype.
  //
  // If a variadic prototype is followed by a non-variadic K&R definition,
  // Edge case: K&R definition of a previously prototyped variadic function.
  if (Old->hasPrototype() && !New->hasPrototype() &&
      New->getType()->getAs<FunctionProtoType>() &&
      Old->getNumParams() == New->getNumParams()) {
    llvm::SmallVector<QualType, 16> ArgTypes;
    llvm::SmallVector<GNUCompatibleParamWarning, 16> Warnings;
    const FunctionProtoType *OldProto =
        Old->getType()->getAs<FunctionProtoType>();
    const FunctionProtoType *NewProto =
        New->getType()->getAs<FunctionProtoType>();

    // Determine whether this is the GNU C extension.
    QualType MergedReturn = Context.mergeTypes(OldProto->getReturnType(),
                                               NewProto->getReturnType());
    bool LooseCompatible = !MergedReturn.isNull();
    for (unsigned Idx = 0, End = Old->getNumParams();
         LooseCompatible && Idx != End; ++Idx) {
      ParmVarDecl *OldParm = Old->getParamDecl(Idx);
      ParmVarDecl *NewParm = New->getParamDecl(Idx);
      if (Context.typesAreCompatible(OldParm->getType(),
                                     NewProto->getParamType(Idx))) {
        ArgTypes.push_back(NewParm->getType());
      } else if (Context.typesAreCompatible(OldParm->getType(),
                                            NewParm->getType(),
                                            /*CompareUnqualified=*/true)) {
        GNUCompatibleParamWarning Warn = {OldParm, NewParm,
                                          NewProto->getParamType(Idx)};
        Warnings.push_back(Warn);
        ArgTypes.push_back(NewParm->getType());
      } else
        LooseCompatible = false;
    }

    if (LooseCompatible) {
      for (unsigned Warn = 0; Warn < Warnings.size(); ++Warn) {
        Diag(Warnings[Warn].NewParm->getLocation(),
             diag::ext_param_promoted_not_compatible_with_prototype)
            << Warnings[Warn].PromotedType << Warnings[Warn].OldParm->getType();
        if (Warnings[Warn].OldParm->getLocation().isValid())
          Diag(Warnings[Warn].OldParm->getLocation(),
               diag::note_previous_declaration);
      }

      if (MergeTypeWithOld)
        New->setType(Context.getFunctionType(MergedReturn, ArgTypes,
                                             OldProto->getExtProtoInfo()));
      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
    }

    // Fall through to diagnose conflicting types.
  }

  // A function that has already been declared has been redeclared or
  // defined with a different type; show an appropriate diagnostic.

  // If the previous declaration was an implicitly-generated builtin
  // declaration, then at the very least we should use a specialized note.
  unsigned BuiltinID;
  if (Old->isImplicit() && (BuiltinID = Old->getBuiltinID())) {
    // If it's actually a library-defined builtin function like 'malloc'
    // or 'printf', just warn about the incompatible redeclaration.
    if (Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID)) {
      Diag(New->getLocation(), diag::warn_redecl_library_builtin) << New;
      Diag(OldLocation, diag::note_previous_builtin_declaration)
          << Old << Old->getType();
      return false;
    }

    PrevDiag = diag::note_previous_builtin_declaration;
  }

  // Define a flag to determine whether merging MSVC compatible function
  // declarations is necessary.
  bool IsMergeRequiredForMSVC = false;

  // Check if Microsoft extensions or MSVC compatibility mode are enabled.
  if (getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat)
    IsMergeRequiredForMSVC = true;

  // For Windows/COFF targets, merging is always required.
  const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
  if (Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
    IsMergeRequiredForMSVC = true;

  // Perform the merge if required.
  if (IsMergeRequiredForMSVC) {
    // Check and merge MSVC compatible function declarations.
    if (!MergeMSVCCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld)) {
      // Return false if the merge was successful.
      return false;
    }
  }

  // The old declaration is Sema's own implicit definition
  // (`int f()` from ImplicitlyDefineFunction).  The authoritative
  // version is the one in the user's source, so silently replace
  // the old prototype with the new one.
  if (Old->isImplicit() ||
      Context.getSourceManager().isWrittenInBuiltinFile(OldLocation)) {
    New->setPreviousDecl(nullptr);
    return false;
  }

  Diag(New->getLocation(), diag::err_conflicting_types) << New->getDeclName();
  Diag(OldLocation, PrevDiag) << Old << Old->getType();
  return true;
}

bool Sema::MergeCompatibleFunctionDecls(FunctionDecl *New, FunctionDecl *Old,
                                        Scope *S, bool MergeTypeWithOld) {
  // Merge the attributes
  mergeDeclAttributes(New, Old);

  // Merge "used" flag.
  if (Old->getMostRecentDecl()->isUsed(false))
    New->setIsUsed();

  // Merge attributes from the parameters.  These can mismatch with K&R
  // declarations.
  if (New->getNumParams() == Old->getNumParams())
    for (unsigned i = 0, e = New->getNumParams(); i != e; ++i) {
      ParmVarDecl *NewParam = New->getParamDecl(i);
      ParmVarDecl *OldParam = Old->getParamDecl(i);
      mergeParamDeclAttributes(NewParam, OldParam, *this);
      mergeParamDeclTypes(NewParam, OldParam, *this);
    }

  // Merge the function types so the we get the composite types for the return
  // and argument types. Per C11 6.2.7/4, only update the type if the old decl
  // was visible.
  QualType Merged = Context.mergeTypes(Old->getType(), New->getType());
  if (!Merged.isNull() && MergeTypeWithOld)
    New->setType(Merged);

  return false;
}

// The same as MergeCompatibleFunctionDecls, but for MSVC-compatible
bool Sema::MergeMSVCCompatibleFunctionDecls(FunctionDecl *New,
                                            FunctionDecl *Old, Scope *S,
                                            bool MergeTypeWithOld) {
  QualType OldQType = Context.getCanonicalType(Old->getType());
  QualType NewQType = Context.getCanonicalType(New->getType());

  const FunctionType *OldFuncType = OldQType->getAs<FunctionType>();
  const FunctionType *NewFuncType = NewQType->getAs<FunctionType>();

  const FunctionProtoType *OldProto = nullptr;
  const FunctionProtoType *NewProto = nullptr;

  // If the types are not funtion types, do not merge the declarations.
  if (!((NewProto = dyn_cast<FunctionProtoType>(NewFuncType)) &&
        (OldProto = dyn_cast<FunctionProtoType>(OldFuncType))))
    return true;

  // If the parameter types are different, do not merge the declarations.
  if (OldProto->param_types().size() != NewProto->param_types().size())
    return true;

  bool CanMerge = false;
  if (OldProto->param_types().size() != 0) {
    for (size_t i = 0; i < OldProto->param_types().size(); ++i) {
      QualType OldParamType = OldProto->param_types()[i];
      QualType NewParamType = NewProto->param_types()[i];

      OldParamType.removeLocalConst();
      NewParamType.removeLocalConst();
      OldParamType.removeLocalVolatile();
      NewParamType.removeLocalVolatile();

      // If the parameter types are different, we need to check if we can
      // merge the declarations.
      if (OldParamType.getAsString() != NewParamType.getAsString()) {
        // If the parameter types are PointerType, we can merge the
        // declarations.
        if (isa<PointerType>(OldParamType) && isa<PointerType>(NewParamType)) {
          CanMerge = true;
          continue;
        }
        CanMerge = false;
        break;
      }
    }
  }
  if (!CanMerge)
    return true;

  if (MergeTypeWithOld) {
    // The old declaration provided a function prototype, but the
    // new declaration does not. Merge in the prototype.
    assert(!OldProto->hasExceptionSpec() && "Exception spec in C");
    NewQType = Context.getFunctionType(NewFuncType->getReturnType(),
                                       OldProto->getParamTypes(),
                                       OldProto->getExtProtoInfo());
    New->setType(NewQType);
    New->setHasInheritedPrototype();

    // Synthesize parameters with the same types.
    llvm::SmallVector<ParmVarDecl *, 16> Params;
    for (const auto &ParamType : OldProto->param_types()) {
      ParmVarDecl *Param = ParmVarDecl::Create(
          Context, New, SourceLocation(), SourceLocation(), nullptr, ParamType,
          /*TInfo=*/nullptr, SC_None, nullptr);
      Param->setScopeInfo(0, Params.size());
      Param->setImplicit();
      Params.push_back(Param);
    }

    New->setParams(Params);
  }
  return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseVarDeclTypeMismatch(Sema &S, VarDecl *New, VarDecl *Old) {
  assert(!S.Context.hasSameType(New->getType(), Old->getType()));

  S.Diag(New->getLocation(), New->isThisDeclarationADefinition()
                                 ? diag::err_redefinition_different_type
                                 : diag::err_redeclaration_different_type)
      << New->getDeclName() << New->getType() << Old->getType();

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);
  S.Diag(OldLocation, PrevDiag) << Old << Old->getType();
  New->setInvalidDecl();
}
} // namespace

void Sema::MergeVarDeclTypes(VarDecl *New, VarDecl *Old,
                             bool MergeTypeWithOld) {
  if (New->isInvalidDecl() || Old->isInvalidDecl() ||
      New->getType()->containsErrors() || Old->getType()->containsErrors())
    return;

  QualType MergedT;
  MergedT = Context.mergeTypes(New->getType(), Old->getType());
  if (MergedT.isNull())
    return diagnoseVarDeclTypeMismatch(*this, New, Old);

  // Don't actually update the type on the new declaration if the old
  // declaration was an extern declaration in a different scope.
  if (MergeTypeWithOld)
    New->setType(MergedT);
}

namespace {
bool mergeTypeWithPrevious(Sema &S, VarDecl *NewVD, VarDecl *OldVD,
                           LookupResult &Previous) {
  // C11 6.2.7p4:
  //   For an identifier with internal or external linkage declared
  //   in a scope in which a prior declaration of that identifier is
  //   visible, if the prior declaration specifies internal or
  //   external linkage, the type of the identifier at the later
  //   declaration becomes the composite type.
  //
  // If the variable isn't visible, we do not merge with its type.
  if (Previous.isShadowed())
    return false;

  // If the old declaration was function-local, don't merge with its
  // type unless we're in the same function.
  return !OldVD->getLexicalDeclContext()->isFunctionOrMethod() ||
         OldVD->getLexicalDeclContext() == NewVD->getLexicalDeclContext();
}
} // namespace

void Sema::MergeVarDecl(VarDecl *New, LookupResult &Previous) {
  // If the new decl is already invalid, don't do any other checking.
  if (New->isInvalidDecl())
    return;

  if (!shouldLinkPossiblyHiddenDecl(Previous, New))
    return;

  VarDecl *Old = nullptr;
  if (Previous.isSingleResult())
    Old = dyn_cast<VarDecl>(Previous.getFoundDecl());
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();
    notePreviousDefinition(Previous.getRepresentativeDecl(),
                           New->getLocation());
    return New->setInvalidDecl();
  }

  mergeDeclAttributes(New, Old);
  // Warn if an already-declared variable is made a weak_import in a subsequent
  // declaration
  if (New->hasAttr<WeakImportAttr>() && Old->getStorageClass() == SC_None &&
      !Old->hasAttr<WeakImportAttr>()) {
    Diag(New->getLocation(), diag::warn_weak_import) << New->getDeclName();
    Diag(Old->getLocation(), diag::note_previous_declaration);
    // Remove weak_import attribute on new declaration.
    New->dropAttr<WeakImportAttr>();
  }

  if (const auto *ILA = New->getAttr<InternalLinkageAttr>())
    if (!Old->hasAttr<InternalLinkageAttr>()) {
      Diag(New->getLocation(), diag::err_attribute_missing_on_first_decl)
          << ILA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<InternalLinkageAttr>();
    }

  // Merge the types.
  VarDecl *MostRecent = Old->getMostRecentDecl();
  if (MostRecent != Old) {
    MergeVarDeclTypes(New, MostRecent,
                      mergeTypeWithPrevious(*this, New, MostRecent, Previous));
    if (New->isInvalidDecl())
      return;
  }

  MergeVarDeclTypes(New, Old, mergeTypeWithPrevious(*this, New, Old, Previous));
  if (New->isInvalidDecl())
    return;

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);

  // Static redeclaration after a declaration with external linkage (C11 6.2.2).
  if (New->getStorageClass() == SC_Static && Old->hasExternalFormalLinkage()) {
    if (getLangOpts().MicrosoftExt) {
      Diag(New->getLocation(), diag::ext_static_non_static)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else {
      Diag(New->getLocation(), diag::err_static_non_static)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
      return New->setInvalidDecl();
    }
  }
  // C99 6.2.2p4:
  //   For an identifier declared with the storage-class specifier
  //   extern in a scope in which a prior declaration of that
  //   identifier is visible,23) if the prior declaration specifies
  //   internal or external linkage, the linkage of the identifier at
  //   the later declaration is the same as the linkage specified at
  //   the prior declaration. If no prior declaration is visible, or
  //   if the prior declaration specifies no linkage, then the
  //   identifier has external linkage.
  if (New->hasExternalStorage() && Old->hasLinkage())
    /* Okay */;
  else if (New->getCanonicalDecl()->getStorageClass() != SC_Static &&
           Old->getCanonicalDecl()->getStorageClass() == SC_Static) {
    Diag(New->getLocation(), diag::err_non_static_static) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  // Check if extern is followed by non-extern and vice-versa.
  if (New->hasExternalStorage() && !Old->hasLinkage() &&
      Old->isLocalVarDeclOrParm()) {
    Diag(New->getLocation(), diag::err_extern_non_extern) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }
  if (Old->hasLinkage() && New->isLocalVarDeclOrParm() &&
      !New->hasExternalStorage()) {
    Diag(New->getLocation(), diag::err_non_extern_extern) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  // Variables with external linkage are analyzed in FinalizeDeclaratorGroup.

  if (!New->hasExternalStorage() && !New->isFileVarDecl()) {
    Diag(New->getLocation(), diag::err_redefinition) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  if (New->isInline() && !Old->getMostRecentDecl()->isInline()) {
    if (VarDecl *Def = Old->getDefinition()) {
      // Definition cannot precede the first `inline` declaration (C23 inline
      // variables).
      Diag(New->getLocation(), diag::err_inline_decl_follows_def) << New;
      Diag(Def->getLocation(), diag::note_previous_definition);
    }
  }

  // If this redeclaration makes the variable inline, we may need to add it to
  // UndefinedButUsed.
  if (!Old->isInline() && New->isInline() && Old->isUsed(false) &&
      !Old->getDefinition() && !New->isThisDeclarationADefinition())
    UndefinedButUsed.insert(
        std::make_pair(Old->getCanonicalDecl(), SourceLocation()));

  if (New->getTLSKind() != Old->getTLSKind()) {
    if (!Old->getTLSKind()) {
      Diag(New->getLocation(), diag::err_thread_non_thread)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else if (!New->getTLSKind()) {
      Diag(New->getLocation(), diag::err_non_thread_thread)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else {
      // Do not allow redeclaration to change the variable between requiring
      // static and dynamic initialization.
      // GCC allows this, but uses the TLS keyword on the first
      // declaration to determine the kind.
      Diag(New->getLocation(), diag::err_thread_thread_different_kind)
          << New->getDeclName() << (New->getTLSKind() == VarDecl::TLS_Dynamic);
      Diag(OldLocation, PrevDiag);
    }
  }

  // Merge "used" flag.
  if (Old->getMostRecentDecl()->isUsed(false))
    New->setIsUsed();

  // Keep a chain of previous declarations.
  New->setPreviousDecl(Old);

  // Inherit access appropriately.
  New->setAccess(Old->getAccess());

  if (Old->isInline())
    New->setImplicitlyInline();
}

void Sema::notePreviousDefinition(const NamedDecl *Old, SourceLocation New) {
  SourceManager &SrcMgr = getSourceManager();
  auto FNewDecLoc = SrcMgr.getDecomposedLoc(New);
  auto FOldDecLoc = SrcMgr.getDecomposedLoc(Old->getLocation());
  auto *FNew = SrcMgr.getFileEntryForID(FNewDecLoc.first);
  auto FOld = SrcMgr.getFileEntryRefForID(FOldDecLoc.first);
  auto &HSI = PP.getIncludeResolver();
  llvm::StringRef HdrFilename =
      SrcMgr.getFilename(SrcMgr.getSpellingLoc(Old->getLocation()));

  auto noteFromInclude = [&](SourceLocation IncLoc) -> bool {
    if (IncLoc.isValid()) {
      Diag(IncLoc, diag::note_redefinition_include_same_file) << HdrFilename;
      return true;
    }
    return false;
  };

  // Is it the same file and same offset? Provide more information on why
  // this leads to a redefinition error.
  if (FNew == FOld && FNewDecLoc.second == FOldDecLoc.second) {
    SourceLocation OldIncLoc = SrcMgr.getIncludeLoc(FOldDecLoc.first);
    SourceLocation NewIncLoc = SrcMgr.getIncludeLoc(FNewDecLoc.first);
    bool EmittedDiag = noteFromInclude(OldIncLoc);
    EmittedDiag |= noteFromInclude(NewIncLoc);

    // If the header has no guards, emit a note suggesting one.
    if (FOld && !HSI.isFileMultipleIncludeGuarded(*FOld))
      Diag(Old->getLocation(), diag::note_use_ifdef_guards);

    if (EmittedDiag)
      return;
  }

  // Redefinition coming from different files or couldn't do better above.
  if (Old->getLocation().isValid())
    Diag(Old->getLocation(), diag::note_previous_definition);
}

bool Sema::checkVarDeclRedefinition(VarDecl *Old, VarDecl *New) {
  Diag(New->getLocation(), diag::err_redefinition) << New;
  notePreviousDefinition(Old, New->getLocation());
  New->setInvalidDecl();
  return true;
}

// ===----------------------------------------------------------------------===
// Standalone declarations & anonymous structs
// ===----------------------------------------------------------------------===

void Sema::setTagNameForLinkagePurposes(TagDecl *TagFromDeclSpec,
                                        TypedefNameDecl *NewTD) {
  if (TagFromDeclSpec->isInvalidDecl())
    return;

  // Do nothing if the tag already has a name for linkage purposes.
  if (TagFromDeclSpec->hasNameForLinkage())
    return;

  // A well-formed anonymous tag must always be a TUK_Definition.
  assert(TagFromDeclSpec->isThisDeclarationADefinition());

  // The type must match the tag exactly;  no qualifiers allowed.
  if (!Context.hasSameType(NewTD->getUnderlyingType(),
                           Context.getTagDeclType(TagFromDeclSpec)))
    return;

  TagFromDeclSpec->setTypedefNameForAnonDecl(NewTD);
}

namespace {
unsigned mapTypeSpecifierToDiagID(const DeclSpec &DS) {
  DeclSpec::TST T = DS.getTypeSpecType();
  switch (T) {
  case DeclSpec::TST_struct:
    return 0;
  case DeclSpec::TST_union:
    return 1;
  case DeclSpec::TST_enum:
    return 2;
  default:
    llvm_unreachable("unexpected type specifier");
  }
}
} // namespace
Decl *Sema::ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS,
                                       DeclSpec &DS,
                                       const ParsedAttributesView &DeclAttrs,
                                       RecordDecl *&AnonRecord) {
  Decl *TagD = nullptr;
  TagDecl *Tag = nullptr;
  if (DS.getTypeSpecType() == DeclSpec::TST_struct ||
      DS.getTypeSpecType() == DeclSpec::TST_union ||
      DS.getTypeSpecType() == DeclSpec::TST_enum) {
    TagD = DS.getRepAsDecl();

    if (!TagD) // We probably had an error
      return nullptr;

    // Note that the above type specs guarantee that the
    // type rep is a Decl, whereas in many of the others
    // it's a Type.
    if (isa<TagDecl>(TagD))
      Tag = cast<TagDecl>(TagD);
  }

  if (Tag) {
    Tag->setFreeStanding();
    if (Tag->isInvalidDecl())
      return Tag;
  }

  if (unsigned TypeQuals = DS.getTypeQualifiers()) {
    // Enforce C99 6.7.3p2: "Types other than pointer types derived from object
    // or incomplete types shall not be restrict-qualified."
    if (TypeQuals & DeclSpec::TQ_restrict)
      Diag(DS.getRestrictSpecLoc(),
           diag::err_typecheck_invalid_restrict_not_pointer_noarg)
          << DS.getSourceRange();
  }

  if (DS.isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_non_function) << false;

  if (DS.hasConstexprSpecifier()) {
    // constexpr applies only to functions or variables, not tags.
    if (Tag)
      Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_tag)
          << mapTypeSpecifierToDiagID(DS)
          << static_cast<int>(DS.getConstexprSpecifier());
    else
      Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_wrong_decl_kind)
          << static_cast<int>(DS.getConstexprSpecifier());
    // Don't emit warnings after this error.
    return TagD;
  }

  if (DS.isNoreturnSpecified())
    Diag(DS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  // Track whether this decl-specifier declares anything.
  bool DeclaresAnything = true;

  // Handle anonymous struct definitions.
  if (RecordDecl *Record = dyn_cast_or_null<RecordDecl>(Tag)) {
    if (!Record->getDeclName() && Record->isCompleteDefinition() &&
        DS.getStorageClassSpec() != DeclSpec::SCS_typedef) {
      if (Record->getDeclContext()->isRecord()) {
        // If CurContext is a DeclContext that can contain statements,
        // RecursiveTreeVisitor won't visit the decls that
        // FormAnonymousStructOrUnion() will put into CurContext.
        // Also store them here so that they can be part of the
        // DeclStmt that gets created in this case.
        if (CurContext->isFunctionOrMethod())
          AnonRecord = Record;
        return FormAnonymousStructOrUnion(S, DS, AS, Record,
                                          Context.getPrintingPolicy());
      }

      DeclaresAnything = false;
    }
  }

  // C11 6.7.2.1p2:
  //   A struct-declaration that does not declare an anonymous structure or
  //   anonymous union shall contain a struct-declarator-list.
  //
  // This rule also existed in C89 and C99; the grammar for struct-declaration
  // did not permit a struct-declaration without a struct-declarator-list.
  if (CurContext->isRecord() &&
      DS.getStorageClassSpec() == DeclSpec::SCS_unspecified) {
    // Check for Microsoft C extension: anonymous struct/union member.
    // Handle 2 kinds of anonymous struct/union:
    //   struct STRUCT;
    //   union UNION;
    // and
    //   STRUCT_TYPE;  <- where STRUCT_TYPE is a typedef struct.
    //   UNION_TYPE;   <- where UNION_TYPE is a typedef union.
    if ((Tag && Tag->getDeclName()) ||
        DS.getTypeSpecType() == DeclSpec::TST_typename) {
      RecordDecl *Record = nullptr;
      if (Tag)
        Record = dyn_cast<RecordDecl>(Tag);
      else if (const RecordType *RT =
                   DS.getRepAsType().get()->getAsStructureType())
        Record = RT->getDecl();
      else if (const RecordType *UT = DS.getRepAsType().get()->getAsUnionType())
        Record = UT->getDecl();

      if (Record && getLangOpts().MicrosoftExt) {
        Diag(DS.getBeginLoc(), diag::ext_ms_anonymous_record)
            << Record->isUnion() << DS.getSourceRange();
        return FormMicrosoftCAnonymousStruct(S, DS, Record);
      }

      DeclaresAnything = false;
    }
  }

  // Skip all the checks below if we have a type error.
  if (DS.getTypeSpecType() == DeclSpec::TST_error ||
      (TagD && TagD->isInvalidDecl()))
    return TagD;

#ifndef _WIN32
  if (!DS.isMissingDeclaratorOk()) {
    // Customize diagnostic for a typedef missing a name.
    if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      Diag(DS.getBeginLoc(), diag::ext_typedef_without_a_name)
          << DS.getSourceRange();
    else
      DeclaresAnything = false;
  }
#endif

  // A declaration must introduce something (declarator, tag, or enum);
  // otherwise emit the extension diagnostic below.
  if (!DeclaresAnything) {
    // In C, we allow this as a (popular) extension / bug. Don't bother
    // producing further diagnostics for redundant qualifiers after this.
    Diag(DS.getBeginLoc(), diag::ext_no_declarators) << DS.getSourceRange();
    return TagD;
  }

  // Standalone storage-class or type qualifiers without a declarator list are
  // usually invalid; some cases remain as extensions.
  unsigned DiagID = diag::warn_standalone_specifier;

  // Note that a linkage-specification sets a storage class, but
  // 'extern "C" struct foo;' is actually valid and not theoretically
  // useless.
  if (DeclSpec::SCS SCS = DS.getStorageClassSpec()) {
    if (!DS.isExternInLinkageSpec() && SCS != DeclSpec::SCS_typedef)
      Diag(DS.getStorageClassSpecLoc(), DiagID)
          << DeclSpec::getSpecifierName(SCS);
  }

  if (DeclSpec::TSCS TSCS = DS.getThreadStorageClassSpec())
    Diag(DS.getThreadStorageClassSpecLoc(), DiagID)
        << DeclSpec::getSpecifierName(TSCS);
  if (DS.getTypeQualifiers()) {
    if (DS.getTypeQualifiers() & DeclSpec::TQ_const)
      Diag(DS.getConstSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw_const);
    if (DS.getTypeQualifiers() & DeclSpec::TQ_volatile)
      Diag(DS.getConstSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw_volatile);
    // Restrict is covered above.
    if (DS.getTypeQualifiers() & DeclSpec::TQ_atomic)
      Diag(DS.getAtomicSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw__Atomic);
    if (DS.getTypeQualifiers() & DeclSpec::TQ_unaligned)
      Diag(DS.getUnalignedSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw___unaligned);
  }

  // Warn about ignored type attributes, for example:
  // __attribute__((aligned)) struct A;
  // Attributes should be placed after tag to apply to type declaration.
  if (!DS.getAttributes().empty() || !DeclAttrs.empty()) {
    DeclSpec::TST TypeSpecType = DS.getTypeSpecType();
    if (TypeSpecType == DeclSpec::TST_struct ||
        TypeSpecType == DeclSpec::TST_union ||
        TypeSpecType == DeclSpec::TST_enum) {

      auto GenAttributeDiagnostic = [this, &DS](const ParsedAttr &AL) {
        unsigned DiagnosticId = diag::warn_declspec_attribute_ignored;
        if (AL.isAlignas())
          DiagnosticId = diag::warn_attribute_ignored;
        else if (AL.isRegularKeywordAttribute())
          DiagnosticId = diag::err_declspec_keyword_has_no_effect;
        else
          DiagnosticId = diag::warn_declspec_attribute_ignored;
        Diag(AL.getLoc(), DiagnosticId) << AL << mapTypeSpecifierToDiagID(DS);
      };

      llvm::for_each(DS.getAttributes(), GenAttributeDiagnostic);
      llvm::for_each(DeclAttrs, GenAttributeDiagnostic);
    }
  }

  return TagD;
}

namespace {
bool checkAnonMemberRedeclaration(Sema &SemaRef, Scope *S, DeclContext *Owner,
                                  DeclarationName Name, SourceLocation NameLoc,
                                  bool IsUnion, StorageClass SC) {
  LookupResult R(SemaRef, Name, NameLoc,
                 Owner->isRecord() ? neverc::ResolveMember
                                   : neverc::ResolveOrdinary,
                 neverc::ForVisibleRedeclaration);
  if (!SemaRef.ResolveName(R, S))
    return false;

  // Pick a representative declaration.
  NamedDecl *PrevDecl = R.getRepresentativeDecl()->getUnderlyingDecl();
  assert(PrevDecl && "Expected a non-null Decl");

  if (!SemaRef.isDeclInScope(PrevDecl, Owner, S))
    return false;

  const llvm::Triple &Triple = SemaRef.Context.getTargetInfo().getTriple();
  if (SemaRef.getLangOpts().MicrosoftExt || SemaRef.getLangOpts().MSVCCompat ||
      Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
    return false;

  SemaRef.Diag(NameLoc, diag::err_anonymous_record_member_redecl)
      << IsUnion << Name;
  SemaRef.Diag(PrevDecl->getLocation(), diag::note_previous_declaration);

  return true;
}
} // namespace

namespace {
bool injectAnonymousStructOrUnionMembers(
    Sema &SemaRef, Scope *S, DeclContext *Owner, RecordDecl *AnonRecord,
    AccessSpecifier AS, StorageClass SC,
    llvm::SmallVectorImpl<NamedDecl *> &Chaining) {
  bool Invalid = false;

  // Look every FieldDecl and IndirectFieldDecl with a name.
  for (auto *D : AnonRecord->decls()) {
    if ((isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D)) &&
        cast<NamedDecl>(D)->getDeclName()) {
      ValueDecl *VD = cast<ValueDecl>(D);
      if (checkAnonMemberRedeclaration(SemaRef, S, Owner, VD->getDeclName(),
                                       VD->getLocation(), AnonRecord->isUnion(),
                                       SC)) {
        Invalid = true;
      } else {
        // Expose members in the enclosing scope (via IndirectFieldDecl when
        // nested).
        unsigned OldChainingSize = Chaining.size();
        if (IndirectFieldDecl *IF = dyn_cast<IndirectFieldDecl>(VD))
          Chaining.append(IF->chain_begin(), IF->chain_end());
        else
          Chaining.push_back(VD);

        assert(Chaining.size() >= 2);
        NamedDecl **NamedChain =
            new (SemaRef.Context) NamedDecl *[Chaining.size()];
        for (unsigned i = 0; i < Chaining.size(); i++)
          NamedChain[i] = Chaining[i];

        IndirectFieldDecl *IndirectField = IndirectFieldDecl::Create(
            SemaRef.Context, Owner, VD->getLocation(), VD->getIdentifier(),
            VD->getType(), {NamedChain, Chaining.size()});

        for (const auto *Attr : VD->attrs())
          IndirectField->addAttr(Attr->clone(SemaRef.Context));

        IndirectField->setAccess(AS);
        IndirectField->setImplicit();
        SemaRef.PushOnScopeChains(IndirectField, S);

        // That includes picking up the appropriate access specifier.
        if (AS != AS_none)
          IndirectField->setAccess(AS);

        Chaining.resize(OldChainingSize);
      }
    }
  }

  return Invalid;
}
} // namespace

namespace {
StorageClass storageClassSpecToVarDeclStorageClass(const DeclSpec &DS) {
  DeclSpec::SCS StorageClassSpec = DS.getStorageClassSpec();
  assert(StorageClassSpec != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class VarDecl.");
  switch (StorageClassSpec) {
  case DeclSpec::SCS_unspecified:
    return SC_None;
  case DeclSpec::SCS_extern:
    if (DS.isExternInLinkageSpec())
      return SC_None;
    return SC_Extern;
  case DeclSpec::SCS_static:
    return SC_Static;
  case DeclSpec::SCS_auto:
    return SC_Auto;
  case DeclSpec::SCS_register:
    return SC_Register;
  case DeclSpec::SCS_private_extern:
    return SC_PrivateExtern;
    // Illegal SCSs map to None: error reporting is up to the caller.
  case DeclSpec::SCS_typedef:
    return SC_None;
  }
  llvm_unreachable("unknown storage class specifier");
}
} // namespace

Decl *Sema::FormAnonymousStructOrUnion(Scope *S, DeclSpec &DS,
                                       AccessSpecifier AS, RecordDecl *Record,
                                       const PrintingPolicy &Policy) {
  DeclContext *Owner = Record->getDeclContext();

  // Diagnose whether this anonymous struct/union is an extension.
  if (Record->isUnion() && !getLangOpts().C11)
    Diag(Record->getLocation(), diag::ext_anonymous_union);
  else if (!Record->isUnion() && !getLangOpts().C11)
    Diag(Record->getLocation(), diag::ext_c11_anonymous_struct);

  // Anonymous struct/union: placement and extension checks.
  bool Invalid = false;

  if (!Record->isUnion() && !Owner->isRecord()) {
    Diag(Record->getLocation(), diag::err_anonymous_struct_not_member);
    Invalid = true;
  }

  // No declarator-id: build a dummy declarator so the anonymous member has a
  // type and attributes.
  Declarator Dc(DS, ParsedAttributesView::none(), DeclaratorContext::Member);
  StorageClass SC = storageClassSpecToVarDeclStorageClass(DS);
  TypeSourceInfo *TInfo = ResolveDeclaratorType(Dc, S);
  assert(TInfo && "couldn't build declarator info for anonymous struct/union");

  NamedDecl *Anon = nullptr;
  if (RecordDecl *OwningClass = dyn_cast<RecordDecl>(Owner)) {
    Anon = FieldDecl::Create(
        Context, OwningClass, DS.getBeginLoc(), Record->getLocation(),
        /*IdentifierInfo=*/nullptr, Context.getTypeDeclType(Record), TInfo,
        /*BitWidth=*/nullptr);
    Anon->setAccess(AS);
    ApplyDeclAttributes(S, Anon, Dc);

  } else {
    Anon = VarDecl::Create(Context, Owner, DS.getBeginLoc(),
                           Record->getLocation(), /*IdentifierInfo=*/nullptr,
                           Context.getTypeDeclType(Record), TInfo, SC);
    ApplyDeclAttributes(S, Anon, Dc);

    // Default-initialize the implicit variable.
    OnUninitializedDecl(Anon);
  }
  Anon->setImplicit();

  // Mark this as an anonymous struct/union type.
  Record->setAnonymousStructOrUnion(true);

  Owner->addDecl(Anon);

  // Inject members into the owning context and identifier resolver
  // so they're directly accessible by name.
  llvm::SmallVector<NamedDecl *, 2> Chain;
  Chain.push_back(Anon);

  if (injectAnonymousStructOrUnionMembers(*this, S, Owner, Record, AS, SC,
                                          Chain))
    Invalid = true;

  if (Invalid)
    Anon->setInvalidDecl();

  return Anon;
}

Decl *Sema::FormMicrosoftCAnonymousStruct(Scope *S, DeclSpec &DS,
                                          RecordDecl *Record) {
  assert(Record && "expected a record!");

  // Mock up a declarator.
  Declarator Dc(DS, ParsedAttributesView::none(), DeclaratorContext::TypeName);
  TypeSourceInfo *TInfo = ResolveDeclaratorType(Dc, S);
  assert(TInfo && "couldn't build declarator info for anonymous struct");

  auto *ParentDecl = cast<RecordDecl>(CurContext);
  QualType RecTy = Context.getTypeDeclType(Record);

  NamedDecl *Anon =
      FieldDecl::Create(Context, ParentDecl, DS.getBeginLoc(), DS.getBeginLoc(),
                        /*IdentifierInfo=*/nullptr, RecTy, TInfo,
                        /*BitWidth=*/nullptr);
  Anon->setImplicit();

  CurContext->addDecl(Anon);

  // Inject members into the current context and identifier resolver
  // so they're directly accessible by name.
  llvm::SmallVector<NamedDecl *, 2> Chain;
  Chain.push_back(Anon);

  RecordDecl *RecordDef = Record->getDefinition();
  if (RequireCompleteSizedType(Anon->getLocation(), RecTy,
                               diag::err_field_incomplete_or_sizeless) ||
      injectAnonymousStructOrUnionMembers(
          *this, S, CurContext, RecordDef, AS_none,
          storageClassSpecToVarDeclStorageClass(DS), Chain)) {
    Anon->setInvalidDecl();
    ParentDecl->setInvalidDecl();
  }

  return Anon;
}

// ===----------------------------------------------------------------------===
// Declarator processing & typedef handling
// ===----------------------------------------------------------------------===

DeclarationNameInfo Sema::GetNameForDeclarator(Declarator &D) {
  return GetNameFromUnqualifiedId(D.getName());
}

DeclarationNameInfo Sema::GetNameFromUnqualifiedId(const UnqualifiedId &Name) {
  DeclarationNameInfo NameInfo;
  NameInfo.setLoc(Name.getBeginLoc());
  NameInfo.setName(Name.getIdentifierInfo());
  return NameInfo;
}

namespace {
bool isFromSystemHeader(SourceManager &SM, const Decl *D) {
  return SM.isInSystemHeader(D->getLocation()) ||
         SM.isInSystemMacro(D->getLocation());
}
} // namespace

void Sema::warnOnReservedIdentifier(const NamedDecl *D) {
  if (const auto *II = D->getIdentifier()) {
    llvm::StringRef Name = II->getName();
    if (LLVM_LIKELY(!Name.empty() && Name.front() != '_'))
      return;
  }
  if (D->getPreviousDecl() || D->isImplicit())
    return;
  ReservedIdentifierStatus Status = D->isReserved(getLangOpts());
  if (Status != ReservedIdentifierStatus::NotReserved &&
      !isFromSystemHeader(Context.getSourceManager(), D)) {
    Diag(D->getLocation(), diag::warn_reserved_extern_symbol)
        << D << static_cast<int>(Status);
  }
}

__attribute__((hot)) NamedDecl *Sema::OnDeclarator(Scope *S, Declarator &D) {
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  const DeclSpec &HDS = D.getDeclSpec();

  // All of these full declarators require an identifier.  If it doesn't have
  // one, the ParsedFreeStandingDeclSpec action should be used.
  if (!Name) {
    if (!D.isInvalidType()) // Reject this if we think it is valid.
      Diag(HDS.getBeginLoc(), diag::err_declarator_need_ident)
          << HDS.getSourceRange() << D.getSourceRange();
    return nullptr;
  }

  // The scope passed in may not be a decl scope.  Zip up the scope tree until
  // we find one that is.
  while ((S->getFlags() & Scope::DeclScope) == 0)
    S = S->getParent();

  DeclContext *DC = CurContext;

  TypeSourceInfo *TInfo = ResolveDeclaratorType(D, S);
  QualType R = TInfo->getType();

  LookupResult Previous(*this, NameInfo, ResolveOrdinary,
                        forRedeclarationInCurContext());

  // See if this is a redefinition of a variable in the same scope.
  {
    bool IsLinkageLookup = false;
    bool CreateBuiltins = false;

    // If the declaration we're planning to build will be a function
    // or object with linkage, then look for another declaration with
    // linkage (C99 6.2.2p4–p5).
    //
    // If the declaration we're planning to build will be declared with
    // external linkage in the translation unit, create any builtin with
    // the same name.
    if (HDS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      /* Do nothing*/;
    else if (CurContext->isFunctionOrMethod() &&
             (HDS.getStorageClassSpec() == DeclSpec::SCS_extern ||
              R->isFunctionType())) {
      IsLinkageLookup = true;
      CreateBuiltins = true;
    } else if (CurContext->getRedeclContext()->isTranslationUnit() &&
               HDS.getStorageClassSpec() != DeclSpec::SCS_static)
      CreateBuiltins = true;

    if (IsLinkageLookup) {
      Previous.clear(ResolveRedeclWithLinkage);
      Previous.setRedeclarationKind(ForExternalRedeclaration);
    }

    ResolveName(Previous, S, CreateBuiltins);
  }

  NamedDecl *New;

  bool AddToScope = true;
  if (HDS.getStorageClassSpec() == DeclSpec::SCS_typedef) {
    New = OnTypedefDeclarator(S, D, DC, TInfo, Previous);
  } else if (R->isFunctionType()) {
    New = OnFunctionDeclarator(S, D, DC, TInfo, Previous, AddToScope);
  } else {
    New = OnVariableDeclarator(S, D, DC, TInfo, Previous, AddToScope);
  }

  if (!New)
    return nullptr;

  // Install the name in the current scope when requested (e.g. FMV decls may
  // opt out via AddToScope).
  if (New->getDeclName() && AddToScope)
    PushOnScopeChains(New, S);

  return New;
}

namespace {
QualType tryToFixInvalidVariablyModifiedType(QualType T, TreeContext &Context,
                                             bool &SizeIsNegative,
                                             llvm::APSInt &Oversized) {
  // This method tries to turn a variable array into a constant
  // array even when the size isn't an ICE.  This is necessary
  // for compatibility with code that depends on gcc's buggy
  // constant expression folding, like struct {char x[(int)(char*)2];}
  SizeIsNegative = false;
  Oversized = 0;

  QualifierCollector Qs;
  const Type *Ty = Qs.strip(T);

  if (const PointerType *PTy = dyn_cast<PointerType>(Ty)) {
    QualType Pointee = PTy->getPointeeType();
    QualType FixedType = tryToFixInvalidVariablyModifiedType(
        Pointee, Context, SizeIsNegative, Oversized);
    if (FixedType.isNull())
      return FixedType;
    FixedType = Context.getPointerType(FixedType);
    return Qs.apply(Context, FixedType);
  }
  if (const ParenType *PTy = dyn_cast<ParenType>(Ty)) {
    QualType Inner = PTy->getInnerType();
    QualType FixedType = tryToFixInvalidVariablyModifiedType(
        Inner, Context, SizeIsNegative, Oversized);
    if (FixedType.isNull())
      return FixedType;
    FixedType = Context.getParenType(FixedType);
    return Qs.apply(Context, FixedType);
  }

  const VariableArrayType *VLATy = dyn_cast<VariableArrayType>(T);
  if (!VLATy)
    return QualType();

  QualType ElemTy = VLATy->getElementType();
  if (ElemTy->isVariablyModifiedType()) {
    ElemTy = tryToFixInvalidVariablyModifiedType(ElemTy, Context,
                                                 SizeIsNegative, Oversized);
    if (ElemTy.isNull())
      return QualType();
  }

  Expr::EvalResult Result;
  if (!VLATy->getSizeExpr() ||
      !VLATy->getSizeExpr()->EvaluateAsInt(Result, Context))
    return QualType();

  llvm::APSInt Res = Result.Val.getInt();

  if (Res.isSigned() && Res.isNegative()) {
    SizeIsNegative = true;
    return QualType();
  }

  unsigned ActiveSizeBits =
      (!ElemTy->isVariablyModifiedType() && !ElemTy->isIncompleteType() &&
       !ElemTy->isUndeducedType())
          ? ConstantArrayType::getNumAddressingBits(Context, ElemTy, Res)
          : Res.getActiveBits();
  if (ActiveSizeBits > ConstantArrayType::getMaxSizeBits(Context)) {
    Oversized = Res;
    return QualType();
  }

  QualType FoldedArrayType = Context.getConstantArrayType(
      ElemTy, Res, VLATy->getSizeExpr(), ArraySizeModifier::Normal, 0);
  return Qs.apply(Context, FoldedArrayType);
}
} // namespace

namespace {
void fixInvalidVariablyModifiedTypeLoc(TypeLoc SrcTL, TypeLoc DstTL) {
  SrcTL = SrcTL.getUnqualifiedLoc();
  DstTL = DstTL.getUnqualifiedLoc();
  if (PointerTypeLoc SrcPTL = SrcTL.getAs<PointerTypeLoc>()) {
    PointerTypeLoc DstPTL = DstTL.castAs<PointerTypeLoc>();
    fixInvalidVariablyModifiedTypeLoc(SrcPTL.getPointeeLoc(),
                                      DstPTL.getPointeeLoc());
    DstPTL.setStarLoc(SrcPTL.getStarLoc());
    return;
  }
  if (ParenTypeLoc SrcPTL = SrcTL.getAs<ParenTypeLoc>()) {
    ParenTypeLoc DstPTL = DstTL.castAs<ParenTypeLoc>();
    fixInvalidVariablyModifiedTypeLoc(SrcPTL.getInnerLoc(),
                                      DstPTL.getInnerLoc());
    DstPTL.setLParenLoc(SrcPTL.getLParenLoc());
    DstPTL.setRParenLoc(SrcPTL.getRParenLoc());
    return;
  }
  ArrayTypeLoc SrcATL = SrcTL.castAs<ArrayTypeLoc>();
  ArrayTypeLoc DstATL = DstTL.castAs<ArrayTypeLoc>();
  TypeLoc SrcElemTL = SrcATL.getElementLoc();
  TypeLoc DstElemTL = DstATL.getElementLoc();
  if (VariableArrayTypeLoc SrcElemATL =
          SrcElemTL.getAs<VariableArrayTypeLoc>()) {
    ConstantArrayTypeLoc DstElemATL = DstElemTL.castAs<ConstantArrayTypeLoc>();
    fixInvalidVariablyModifiedTypeLoc(SrcElemATL, DstElemATL);
  } else {
    DstElemTL.initializeFullCopy(SrcElemTL);
  }
  DstATL.setLBracketLoc(SrcATL.getLBracketLoc());
  DstATL.setSizeExpr(SrcATL.getSizeExpr());
  DstATL.setRBracketLoc(SrcATL.getRBracketLoc());
}
} // namespace

namespace {
TypeSourceInfo *tryToFixInvalidVariablyModifiedTypeSourceInfo(
    TypeSourceInfo *TInfo, TreeContext &Context, bool &SizeIsNegative,
    llvm::APSInt &Oversized) {
  QualType FixedTy = tryToFixInvalidVariablyModifiedType(
      TInfo->getType(), Context, SizeIsNegative, Oversized);
  if (FixedTy.isNull())
    return nullptr;
  TypeSourceInfo *FixedTInfo = Context.getTrivialTypeSourceInfo(FixedTy);
  fixInvalidVariablyModifiedTypeLoc(TInfo->getTypeLoc(),
                                    FixedTInfo->getTypeLoc());
  return FixedTInfo;
}
} // namespace

bool Sema::tryToFixVariablyModifiedVarType(TypeSourceInfo *&TInfo, QualType &T,
                                           SourceLocation Loc,
                                           unsigned FailedFoldDiagID) {
  bool SizeIsNegative;
  llvm::APSInt Oversized;
  TypeSourceInfo *FixedTInfo = tryToFixInvalidVariablyModifiedTypeSourceInfo(
      TInfo, Context, SizeIsNegative, Oversized);
  if (FixedTInfo) {
    Diag(Loc, diag::ext_vla_folded_to_constant);
    TInfo = FixedTInfo;
    T = FixedTInfo->getType();
    return true;
  }

  if (SizeIsNegative)
    Diag(Loc, diag::err_typecheck_negative_array_size);
  else if (Oversized.getBoolValue())
    Diag(Loc, diag::err_array_too_large) << toString(Oversized, 10);
  else if (FailedFoldDiagID)
    Diag(Loc, FailedFoldDiagID);
  return false;
}

void Sema::RegisterLocallyScopedExternCDecl(NamedDecl *ND, Scope *S) {
  if (ND->getLexicalDeclContext()->getRedeclContext()->isTranslationUnit())
    return;

  // Note that we have a locally-scoped external with this name.
  Context.getExternCContextDecl()->makeDeclVisibleInContext(ND);
}

NamedDecl *Sema::findLocallyScopedExternCDecl(DeclarationName Name) {
  auto Result = Context.getExternCContextDecl()->lookup(Name);
  return Result.empty() ? nullptr : *Result.begin();
}

NamedDecl *Sema::OnTypedefDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                     TypeSourceInfo *TInfo,
                                     LookupResult &Previous) {
  const DeclSpec &TDS = D.getDeclSpec();
  if (TDS.isNoreturnSpecified())
    Diag(TDS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  if (TDS.isInlineSpecified())
    Diag(TDS.getInlineSpecLoc(), diag::err_inline_non_function) << false;
  if (TDS.hasConstexprSpecifier())
    Diag(TDS.getConstexprSpecLoc(), diag::err_invalid_constexpr)
        << 1 << static_cast<int>(TDS.getConstexprSpecifier());

  if (!D.getIdentifier()) {
    Diag(D.getName().getBeginLoc(), diag::err_typedef_not_identifier)
        << D.getName().getSourceRange();
    return nullptr;
  }

  TypedefDecl *NewTD = ParseTypedefDecl(S, D, TInfo->getType(), TInfo);
  if (!NewTD)
    return nullptr;

  ApplyDeclAttributes(S, NewTD, D);

  CheckTypedefForVariablyModifiedType(S, NewTD);

  bool Redeclaration = D.isRedeclaration();
  NamedDecl *ND = OnTypedefNameDecl(S, DC, NewTD, Previous, Redeclaration);
  D.setRedeclaration(Redeclaration);
  return ND;
}

void Sema::CheckTypedefForVariablyModifiedType(Scope *S,
                                               TypedefNameDecl *NewTD) {
  // C99 6.7.7p2: If a typedef name specifies a variably modified type
  // then it shall have block scope.
  // Note that variably modified types must be fixed before merging the decl so
  // that redeclarations will match.
  TypeSourceInfo *TInfo = NewTD->getTypeSourceInfo();
  QualType T = TInfo->getType();
  if (T->isVariablyModifiedType()) {
    setFunctionHasBranchProtectedScope();

    if (S->getFnParent() == nullptr) {
      bool SizeIsNegative;
      llvm::APSInt Oversized;
      TypeSourceInfo *FixedTInfo =
          tryToFixInvalidVariablyModifiedTypeSourceInfo(
              TInfo, Context, SizeIsNegative, Oversized);
      if (FixedTInfo) {
        Diag(NewTD->getLocation(), diag::ext_vla_folded_to_constant);
        NewTD->setTypeSourceInfo(FixedTInfo);
      } else {
        if (SizeIsNegative)
          Diag(NewTD->getLocation(), diag::err_typecheck_negative_array_size);
        else if (T->isVariableArrayType())
          Diag(NewTD->getLocation(), diag::err_vla_decl_in_file_scope);
        else if (Oversized.getBoolValue())
          Diag(NewTD->getLocation(), diag::err_array_too_large)
              << toString(Oversized, 10);
        else
          Diag(NewTD->getLocation(), diag::err_vm_decl_in_file_scope);
        NewTD->setInvalidDecl();
      }
    }
  }
}

NamedDecl *Sema::OnTypedefNameDecl(Scope *S, DeclContext *DC,
                                   TypedefNameDecl *NewTD,
                                   LookupResult &Previous,
                                   bool &Redeclaration) {

  // Find the shadowed declaration before filtering for scope.
  NamedDecl *ShadowedDecl = getShadowedDeclaration(NewTD, Previous);

  // Merge the decl with the existing one if appropriate. If the decl is
  // in an outer scope, it isn't the same thing.
  FilterLookupForScope(Previous, DC, S, /*ConsiderLinkage*/ false);
  if (!Previous.empty()) {
    Redeclaration = true;
    MergeTypedefNameDecl(S, NewTD, Previous);
  }

  if (ShadowedDecl && !Redeclaration)
    CheckShadow(NewTD, ShadowedDecl, Previous);

  // If this is the C FILE type, notify the AST context.
  if (IdentifierInfo *II = NewTD->getIdentifier())
    if (!NewTD->isInvalidDecl() &&
        NewTD->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      switch (II->getInterestingIdentifierID()) {
      case tok::InterestingIdentifierKind::FILE:
        Context.setFILEDecl(NewTD);
        break;
      case tok::InterestingIdentifierKind::jmp_buf:
        Context.setjmp_bufDecl(NewTD);
        break;
      case tok::InterestingIdentifierKind::sigjmp_buf:
        Context.setsigjmp_bufDecl(NewTD);
        break;
      case tok::InterestingIdentifierKind::ucontext_t:
        Context.setucontext_tDecl(NewTD);
        break;
      case tok::InterestingIdentifierKind::float_t:
      case tok::InterestingIdentifierKind::double_t:
        NewTD->addAttr(AvailableOnlyInDefaultEvalMethodAttr::Create(Context));
        break;
      default:
        break;
      }
    }

  return NewTD;
}

namespace {
bool isOutOfScopePreviousDeclaration(NamedDecl *PrevDecl, DeclContext *DC,
                                     TreeContext &Context) {
  if (!PrevDecl)
    return false;

  if (!PrevDecl->hasLinkage())
    return false;

  return true;
}
} // namespace

namespace {
void checkAttributesAfterMerging(Sema &S, NamedDecl &ND) {
  assert(!S.ParsingInitForAutoVars.contains(&ND));

  if (LLVM_LIKELY(!ND.hasAttrs()))
    return;

  // 'weak' only applies to declarations with external linkage.
  if (WeakAttr *Attr = ND.getAttr<WeakAttr>()) {
    if (!ND.isExternallyVisible()) {
      S.Diag(Attr->getLocation(), diag::err_attribute_weak_static);
      ND.dropAttr<WeakAttr>();
    }
  }
  if (WeakRefAttr *Attr = ND.getAttr<WeakRefAttr>()) {
    if (ND.isExternallyVisible()) {
      S.Diag(Attr->getLocation(), diag::err_attribute_weakref_not_static);
      ND.dropAttr<WeakRefAttr>();
      ND.dropAttr<AliasAttr>();
    }
  }

  if (auto *VD = dyn_cast<VarDecl>(&ND)) {
    if (VD->hasInit()) {
      if (const auto *Attr = VD->getAttr<AliasAttr>()) {
        assert(VD->isThisDeclarationADefinition() &&
               !VD->isExternallyVisible() && "Broken AliasAttr handled late!");
        S.Diag(Attr->getLocation(), diag::err_alias_is_definition) << VD << 0;
        VD->dropAttr<AliasAttr>();
      }
    }
  }

  // 'selectany' only applies to externally visible variable declarations.
  // It does not apply to functions.
  if (SelectAnyAttr *Attr = ND.getAttr<SelectAnyAttr>()) {
    if (isa<FunctionDecl>(ND) || !ND.isExternallyVisible()) {
      S.Diag(Attr->getLocation(),
             diag::err_attribute_selectany_non_extern_data);
      ND.dropAttr<SelectAnyAttr>();
    }
  }

  if (const InheritableAttr *Attr = getDLLAttr(&ND)) {
    auto *VD = dyn_cast<VarDecl>(&ND);
    if (!ND.isExternallyVisible() || (VD && VD->isStaticLocal())) {
      S.Diag(ND.getLocation(), diag::err_attribute_dll_not_extern)
          << &ND << Attr;
      ND.setInvalidDecl();
    }
  }
}
} // namespace

namespace {
void checkDLLAttributeRedeclaration(Sema &S, NamedDecl *OldDecl,
                                    NamedDecl *NewDecl, bool IsSpecialization,
                                    bool IsDefinition) {
  if (OldDecl->isInvalidDecl() || NewDecl->isInvalidDecl())
    return;

  if (!OldDecl || !NewDecl)
    return;

  const DLLImportAttr *OldImportAttr = OldDecl->getAttr<DLLImportAttr>();
  const DLLExportAttr *OldExportAttr = OldDecl->getAttr<DLLExportAttr>();
  const DLLImportAttr *NewImportAttr = NewDecl->getAttr<DLLImportAttr>();
  const DLLExportAttr *NewExportAttr = NewDecl->getAttr<DLLExportAttr>();

  bool HasNewAttr = (NewImportAttr && !NewImportAttr->isInherited()) ||
                    (NewExportAttr && !NewExportAttr->isInherited());

  bool AddsAttr = !(OldImportAttr || OldExportAttr) && HasNewAttr;

  if (AddsAttr && !OldDecl->isImplicit()) {
    bool JustWarn = isa<VarDecl>(OldDecl) || isa<FunctionDecl>(OldDecl);

    if (OldDecl->isUsed())
      if (!isa<FunctionDecl>(OldDecl) || !NewImportAttr)
        JustWarn = false;

    unsigned DiagID = JustWarn ? diag::warn_attribute_dll_redeclaration
                               : diag::err_attribute_dll_redeclaration;
    S.Diag(NewDecl->getLocation(), DiagID)
        << NewDecl
        << (NewImportAttr ? (const Attr *)NewImportAttr : NewExportAttr);
    S.Diag(OldDecl->getLocation(), diag::note_previous_declaration);
    if (!JustWarn) {
      NewDecl->setInvalidDecl();
      return;
    }
  }

  bool IsInline = false;
  bool IsMicrosoftABI =
      S.Context.getTargetInfo().shouldDLLImportComdatSymbols();
  if (const auto *VD = dyn_cast<VarDecl>(NewDecl)) {
    IsDefinition =
        VD->isThisDeclarationADefinition(S.Context) != VarDecl::DeclarationOnly;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(NewDecl)) {
    IsInline = FD->isInlined();
  }

  if (OldImportAttr && !HasNewAttr && !IsInline &&
      !NewDecl->isLocalExternDecl()) {
    if (IsMicrosoftABI && IsDefinition) {
      S.Diag(NewDecl->getLocation(),
             diag::warn_redeclaration_without_import_attribute)
          << NewDecl;
      S.Diag(OldDecl->getLocation(), diag::note_previous_declaration);
      NewDecl->dropAttr<DLLImportAttr>();
      NewDecl->addAttr(
          DLLExportAttr::CreateImplicit(S.Context, NewImportAttr->getRange()));
    } else {
      S.Diag(NewDecl->getLocation(),
             diag::warn_redeclaration_without_attribute_prev_attribute_ignored)
          << NewDecl << OldImportAttr;
      S.Diag(OldDecl->getLocation(), diag::note_previous_declaration);
      S.Diag(OldImportAttr->getLocation(), diag::note_previous_attribute);
      OldDecl->dropAttr<DLLImportAttr>();
      NewDecl->dropAttr<DLLImportAttr>();
    }
  } else if (IsInline && OldImportAttr && !IsMicrosoftABI) {
    OldDecl->dropAttr<DLLImportAttr>();
    NewDecl->dropAttr<DLLImportAttr>();
    S.Diag(NewDecl->getLocation(),
           diag::warn_dllimport_dropped_from_inline_function)
        << NewDecl << OldImportAttr;
  }
}
} // namespace

namespace {
bool isFunctionDefinitionDiscarded(Sema &S, FunctionDecl *FD) {
  // Try to avoid calling GetGVALinkageForFunction.

  // All cases of this require the 'inline' keyword.
  if (!FD->isInlined())
    return false;

  // Okay, go ahead and call the relatively-more-expensive function.
  return S.Context.GetGVALinkageForFunction(FD) == GVA_AvailableExternally;
}
} // namespace

namespace {
bool shouldConsiderLinkage(const VarDecl *VD) {
  const DeclContext *DC = VD->getDeclContext()->getRedeclContext();
  if (DC->isFunctionOrMethod())
    return VD->hasExternalStorage();
  if (DC->isFileContext())
    return true;
  if (DC->isRecord())
    return false;
  llvm_unreachable("Unexpected context");
}
} // namespace

namespace {
bool shouldConsiderLinkage(const FunctionDecl *FD) {
  const DeclContext *DC = FD->getDeclContext()->getRedeclContext();
  if (DC->isFileContext() || DC->isFunctionOrMethod())
    return true;
  if (DC->isRecord())
    return false;
  llvm_unreachable("Unexpected context");
}
} // namespace

namespace {
bool hasParsedAttr(Scope *S, const Declarator &PD, ParsedAttr::Kind Kind) {
  if (PD.getDeclSpec().getAttributes().hasAttribute(Kind))
    return true;

  // Walk the declarator structure, checking decl attributes that were in a type
  // position to the decl itself.
  for (unsigned I = 0, E = PD.getNumTypeObjects(); I != E; ++I) {
    if (PD.getTypeObject(I).getAttrs().hasAttribute(Kind))
      return true;
  }

  // Finally, check attributes on the decl itself.
  return PD.getAttributes().hasAttribute(Kind) ||
         PD.getDeclarationAttributes().hasAttribute(Kind);
}
} // namespace

bool Sema::adjustContextForLocalExternDecl(DeclContext *&DC) {
  if (!DC->isFunctionOrMethod())
    return false;

  // Block-scope declaration with linkage: walk to file / linkage-spec context
  // for matching prior declarations.
  while (!DC->isFileContext())
    DC = DC->getParent();
  return true;
}

namespace {
bool isDeclExternC(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->isExternC();
  if (const auto *VD = dyn_cast<VarDecl>(D))
    return VD->isExternC();

  llvm_unreachable("Unknown type of decl!");
}
} // namespace

namespace {
void copyAllocSizeAttrFromTypedefToDecl(Sema &S, Decl *D,
                                        const TypedefType *TT) {
  const TypedefNameDecl *TND = TT->getDecl();
  if (const auto *Attribute = TND->getAttr<AllocSizeAttr>()) {
    AllocSizeAttr *Clone = Attribute->clone(S.Context);
    Clone->setInherited(true);
    D->addAttr(Clone);
  }
}
} // namespace

// This function emits warning and a corresponding note based on the
// ReadOnlyPlacementAttr attribute. The warning checks that all global variable
// declarations of an annotated type must be const qualified.
void emitReadOnlyPlacementAttrWarning(Sema &S, const VarDecl *VD) {
  QualType VarType = VD->getType().getCanonicalType();

  // Ignore local declarations (for now) and those with const qualification.
  if (!VD || VD->hasLocalStorage() || VD->getType().isConstQualified())
    return;

  if (VarType->isArrayType()) {
    // Retrieve element type for array declarations.
    VarType = S.getTreeContext().getBaseElementType(VarType);
  }

  const RecordDecl *RD = VarType->getAsRecordDecl();

  if (RD == nullptr)
    return;

  if (const auto *ConstDecl = RD->getAttr<ReadOnlyPlacementAttr>()) {
    S.Diag(VD->getLocation(), diag::warn_var_decl_not_read_only) << RD;
    S.Diag(ConstDecl->getLocation(), diag::note_enforce_read_only_placement);
    return;
  }
}

// ===----------------------------------------------------------------------===
// Variable declarations
// ===----------------------------------------------------------------------===

NamedDecl *Sema::OnVariableDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                      TypeSourceInfo *TInfo,
                                      LookupResult &Previous,
                                      bool &AddToScope) {
  QualType R = TInfo->getType();
  DeclarationName Name = GetNameForDeclarator(D).getName();

  IdentifierInfo *II = Name.getAsIdentifierInfo();

  if (!II) {
    Diag(D.getIdentifierLoc(), diag::err_bad_variable_name) << Name;
    return nullptr;
  }

  const DeclSpec &VDS = D.getDeclSpec();
  DeclSpec::SCS SCSpec = VDS.getStorageClassSpec();
  StorageClass SC = storageClassSpecToVarDeclStorageClass(VDS);

  // dllimport globals without explicit storage class are treated as extern. We
  // have to change the storage class this early to get the right DeclContext.
  if (SC == SC_None && !DC->isRecord() &&
      hasParsedAttr(S, D, ParsedAttr::AT_DLLImport) &&
      !hasParsedAttr(S, D, ParsedAttr::AT_DLLExport))
    SC = SC_Extern;

  DeclContext *OriginalDC = DC;
  bool IsLocalExternDecl =
      SC == SC_Extern && adjustContextForLocalExternDecl(DC);

  if (VDS.isNoreturnSpecified())
    Diag(VDS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  if (!DC->isRecord() && S->getFnParent() == nullptr) {
    // auto/register not allowed at file scope (register+asm is a GNU
    // extension).
    if (SC == SC_Auto || (SC == SC_Register && !D.getAsmLabel())) {
      Diag(D.getIdentifierLoc(), diag::err_typecheck_sclass_fscope);
      D.setInvalidType();
    }
  }

  // If this variable has a VLA type and an initializer, try to
  // fold to a constant-sized type. This is otherwise invalid.
  if (D.hasInitializer() && R->isVariableArrayType())
    tryToFixVariablyModifiedVarType(TInfo, R, D.getIdentifierLoc(),
                                    /*DiagID=*/0);

  VarDecl *NewVD = VarDecl::Create(Context, DC, D.getBeginLoc(),
                                   D.getIdentifierLoc(), II, R, TInfo, SC);

  if (R->getContainedDeducedType())
    ParsingInitForAutoVars.insert(NewVD);

  if (D.isInvalidType())
    NewVD->setInvalidDecl();

  if (VDS.isInlineSpecified()) {
    Diag(VDS.getInlineSpecLoc(), diag::err_inline_non_function) << 0;
  }

  // Lexical vs semantic context can differ when a nested-name-specifier is
  // present.
  NewVD->setLexicalDeclContext(CurContext);

  if (IsLocalExternDecl)
    NewVD->setLocalExternDecl();

  if (DeclSpec::TSCS TSCS = VDS.getThreadStorageClassSpec()) {
    // thread_local at block scope implies static unless `extern` is explicit.
    if (NewVD->hasLocalStorage() &&
        (SCSpec != DeclSpec::SCS_unspecified ||
         TSCS != DeclSpec::TSCS_thread_local || !DC->isFunctionOrMethod()))
      Diag(VDS.getThreadStorageClassSpecLoc(), diag::err_thread_non_global)
          << DeclSpec::getSpecifierName(TSCS);
    else if (!Context.getTargetInfo().isTLSSupported())
      Diag(VDS.getThreadStorageClassSpecLoc(), diag::err_thread_unsupported);
    else
      NewVD->setTSCSpec(TSCS);
  }

  switch (VDS.getConstexprSpecifier()) {
  case ConstexprSpecKind::Unspecified:
    break;

  case ConstexprSpecKind::Constexpr:
    NewVD->setConstexpr(true);
    break;
  }

  // C99 6.7.4p3
  //   An inline definition of a function with external linkage shall
  //   not contain a definition of a modifiable object with static or
  //   thread storage duration...
  // We only apply this when the function is required to be defined
  // elsewhere, i.e. when the function is not 'extern inline'.  Note
  // that a local variable with thread storage duration still has to
  // be marked 'static'. GNU `gnu_inline` can produce similar situations.
  if (SC == SC_Static && S->getFnParent() != nullptr &&
      !NewVD->getType().isConstQualified()) {
    FunctionDecl *CurFD = getCurFunctionDecl();
    if (CurFD && isFunctionDefinitionDiscarded(*this, CurFD)) {
#ifndef _WIN32
      Diag(VDS.getStorageClassSpecLoc(),
           diag::warn_static_local_in_extern_inline);
      MaybeSuggestAddingStaticToDecl(CurFD);
#endif
    }
  }

  ApplyDeclAttributes(S, NewVD, D);
  attachNeverCStringCleanup(*this, S, NewVD);

  // This is probably the wrong location to be doing this and we should
  // probably be doing this for more attributes (especially for function
  // pointer attributes such as format, warn_unused_result, etc.). Ideally
  // the code to copy attributes would be generated by TableGen.
  if (R->isFunctionPointerType())
    if (const auto *TT = R->getAs<TypedefType>())
      copyAllocSizeAttrFromTypedefToDecl(*this, NewVD, TT);

  // Ensure that dllimport globals without explicit storage class are treated as
  // extern. The storage class is set above using parsed attributes. Now we can
  // check the VarDecl itself.
  assert(!NewVD->hasAttr<DLLImportAttr>() ||
         NewVD->getAttr<DLLImportAttr>()->isInherited() ||
         NewVD->getStorageClass() != SC_None);

  // Handle GNU asm-label extension (encoded as an attribute).
  if (Expr *E = (Expr *)D.getAsmLabel()) {
    // The parser guarantees this is a string.
    StringLiteral *SE = cast<StringLiteral>(E);
    llvm::StringRef Label = SE->getString();
    if (S->getFnParent() != nullptr) {
      switch (SC) {
      case SC_None:
      case SC_Auto:
        Diag(E->getExprLoc(), diag::warn_asm_label_on_auto_decl) << Label;
        break;
      case SC_Register:
        // Local Named register
        if (!Context.getTargetInfo().isValidGCCRegisterName(Label))
          Diag(E->getExprLoc(), diag::err_asm_unknown_register_name) << Label;
        break;
      case SC_Static:
      case SC_Extern:
      case SC_PrivateExtern:
        break;
      }
    } else if (SC == SC_Register) {
      // Global Named register
      {
        const auto &TI = Context.getTargetInfo();
        bool HasSizeMismatch;

        if (!TI.isValidGCCRegisterName(Label))
          Diag(E->getExprLoc(), diag::err_asm_unknown_register_name) << Label;
        else if (!TI.validateGlobalRegisterVariable(
                     Label, Context.getTypeSize(R), HasSizeMismatch))
          Diag(E->getExprLoc(), diag::err_asm_invalid_global_var_reg) << Label;
        else if (HasSizeMismatch)
          Diag(E->getExprLoc(), diag::err_asm_register_size_mismatch) << Label;
      }

      if (!R->isIntegralType(Context) && !R->isPointerType()) {
        Diag(D.getBeginLoc(), diag::err_asm_bad_register_type);
        NewVD->setInvalidDecl(true);
      }
    }

    NewVD->addAttr(AsmLabelAttr::Create(Context, Label,
                                        /*IsLiteralLabel=*/true,
                                        SE->getStrTokenLoc(0)));
  }

  NamedDecl *ShadowedDecl = getShadowedDeclaration(NewVD, Previous);

  FilterLookupForScope(Previous, OriginalDC, S, shouldConsiderLinkage(NewVD));

  // Same block scope affects whether types are merged with a prior decl.
  D.setRedeclaration(CheckVariableDeclaration(NewVD, Previous));

  // Diagnose shadowed variables iff this isn't a redeclaration.
  if (ShadowedDecl && !D.isRedeclaration())
    CheckShadow(NewVD, ShadowedDecl, Previous);

  ProcessPragmaWeak(S, NewVD);

  if (LLVM_UNLIKELY(NewVD->getStorageClass() == SC_Extern) &&
      NewVD->isFirstDecl() && !NewVD->isInvalidDecl() && NewVD->isExternC())
    RegisterLocallyScopedExternCDecl(NewVD, S);

  if (Name.getAsIdentifierInfo() && Name.getAsIdentifierInfo()->isStr("main") &&
      NewVD->getDeclContext()->getRedeclContext()->isTranslationUnit() &&
      !getLangOpts().Freestanding) {
    if (NewVD->hasExternalFormalLinkage())
      Diag(D.getBeginLoc(), diag::warn_main_redefined);
  }

  if (D.isRedeclaration() && !Previous.empty()) {
    NamedDecl *Prev = Previous.getRepresentativeDecl();
    checkDLLAttributeRedeclaration(*this, Prev, NewVD, false,
                                   D.isFunctionDefinition());
  }

  emitReadOnlyPlacementAttrWarning(*this, NewVD);

  return NewVD;
}

enum ShadowedDeclKind {
  SDK_Local,
  SDK_Global,
  SDK_Field,
  SDK_Typedef,
};

namespace {
ShadowedDeclKind computeShadowedDeclKind(const NamedDecl *ShadowedDecl,
                                         const DeclContext *OldDC) {
  if (isa<TypedefNameDecl>(ShadowedDecl))
    return SDK_Typedef;
  if (isa<RecordDecl>(OldDC))
    return SDK_Field;

  return OldDC->isFileContext() ? SDK_Global : SDK_Local;
}
} // namespace

namespace {
bool shouldWarnIfShadowedDecl(const DiagnosticsEngine &Diags,
                              const LookupResult &R) {
  // Only diagnose if we're shadowing an unambiguous field or variable.
  if (R.getResultKind() != LookupResult::Found)
    return false;

  // Return false if warning is ignored.
  return !Diags.isIgnored(diag::warn_decl_shadow, R.getNameLoc());
}
} // namespace

NamedDecl *Sema::getShadowedDeclaration(const VarDecl *D,
                                        const LookupResult &R) {
  if (!shouldWarnIfShadowedDecl(Diags, R))
    return nullptr;

  // Don't diagnose declarations at file scope.
  if (D->hasGlobalStorage() && !D->isStaticLocal())
    return nullptr;

  NamedDecl *ShadowedDecl = R.getFoundDecl();
  return isa<VarDecl, FieldDecl>(ShadowedDecl) ? ShadowedDecl : nullptr;
}

NamedDecl *Sema::getShadowedDeclaration(const TypedefNameDecl *D,
                                        const LookupResult &R) {
  // Don't warn if typedef declaration is part of a struct/union
  if (D->getDeclContext()->isRecord())
    return nullptr;

  if (!shouldWarnIfShadowedDecl(Diags, R))
    return nullptr;

  NamedDecl *ShadowedDecl = R.getFoundDecl();
  return isa<TypedefNameDecl>(ShadowedDecl) ? ShadowedDecl : nullptr;
}

void Sema::CheckShadow(NamedDecl *D, NamedDecl *ShadowedDecl,
                       const LookupResult &R) {
  DeclContext *NewDC = D->getDeclContext();

  if (VarDecl *shadowedVar = dyn_cast<VarDecl>(ShadowedDecl))
    if (shadowedVar->isExternC()) {
      // For shadowing external vars, make sure that we point to the global
      // declaration, not a locally scoped extern declaration.
      for (auto *I : shadowedVar->redecls())
        if (I->isFileVarDecl()) {
          ShadowedDecl = I;
          break;
        }
    }

  DeclContext *OldDC = ShadowedDecl->getDeclContext()->getRedeclContext();

  if (NewDC && NewDC->isRecord() && !OldDC->isRecord())
    return;

  DeclarationName Name = R.getLookupName();

  ShadowedDeclKind Kind = computeShadowedDeclKind(ShadowedDecl, OldDC);
  Diag(R.getNameLoc(), diag::warn_decl_shadow) << Name << Kind << OldDC;
  Diag(ShadowedDecl->getLocation(), diag::note_previous_declaration);
}

void Sema::CheckShadow(Scope *S, VarDecl *D) {
  if (Diags.isIgnored(diag::warn_decl_shadow, D->getLocation()))
    return;

  LookupResult R(*this, D->getDeclName(), D->getLocation(),
                 neverc::ResolveOrdinary, neverc::ForVisibleRedeclaration);
  ResolveName(R, S);
  if (NamedDecl *ShadowedDecl = getShadowedDeclaration(D, R))
    CheckShadow(D, ShadowedDecl, R);
}

namespace {
template <typename T>
bool checkForConflictWithNonVisibleExternC(Sema &S, const T *ND,
                                           LookupResult &Previous) {
  if (ND->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
    if (NamedDecl *Prev = S.findLocallyScopedExternCDecl(ND->getDeclName())) {
      Previous.clear();
      Previous.addDecl(Prev);
      return true;
    }
  }
  return false;
}
} // namespace

void Sema::CheckVariableDeclarationType(VarDecl *NewVD) {
  // If the decl is already known invalid, don't check it.
  if (NewVD->isInvalidDecl())
    return;

  QualType T = NewVD->getType();

  // Defer checking an 'auto' type until its initializer is attached.
  if (T->isUndeducedType())
    return;

  if (NewVD->hasAttrs())
    CheckAlignasUnderalignment(NewVD);

  // ISO/IEC TR 18037 S5.1.2
  if (NewVD->hasLocalStorage() && T.getAddressSpace() != LangAS::Default) {
    Diag(NewVD->getLocation(), diag::err_as_qualified_auto_decl) << 0;
    NewVD->setInvalidDecl();
    return;
  }

  bool isVM = T->isVariablyModifiedType();
  if (isVM || NewVD->hasAttr<CleanupAttr>())
    setFunctionHasBranchProtectedScope();

  if ((isVM && NewVD->hasLinkage()) ||
      (T->isVariableArrayType() && NewVD->hasGlobalStorage())) {
    bool SizeIsNegative;
    llvm::APSInt Oversized;
    TypeSourceInfo *FixedTInfo = tryToFixInvalidVariablyModifiedTypeSourceInfo(
        NewVD->getTypeSourceInfo(), Context, SizeIsNegative, Oversized);
    QualType FixedT;
    if (FixedTInfo && T == NewVD->getTypeSourceInfo()->getType())
      FixedT = FixedTInfo->getType();
    else if (FixedTInfo) {
      // Type and type-as-written are canonically different. We need to fix up
      // both types separately.
      FixedT = tryToFixInvalidVariablyModifiedType(T, Context, SizeIsNegative,
                                                   Oversized);
    }
    if ((!FixedTInfo || FixedT.isNull()) && T->isVariableArrayType()) {
      const VariableArrayType *VAT = Context.getAsVariableArrayType(T);
      // This won't give the correct result for
      // int a[10][n];
      SourceRange SizeRange = VAT->getSizeExpr()->getSourceRange();

      if (NewVD->isFileVarDecl())
        Diag(NewVD->getLocation(), diag::err_vla_decl_in_file_scope)
            << SizeRange;
      else if (NewVD->isStaticLocal())
        Diag(NewVD->getLocation(), diag::err_vla_decl_has_static_storage)
            << SizeRange;
      else
        Diag(NewVD->getLocation(), diag::err_vla_decl_has_extern_linkage)
            << SizeRange;
      NewVD->setInvalidDecl();
      return;
    }

    if (!FixedTInfo) {
      if (NewVD->isFileVarDecl())
        Diag(NewVD->getLocation(), diag::err_vm_decl_in_file_scope);
      else
        Diag(NewVD->getLocation(), diag::err_vm_decl_has_extern_linkage);
      NewVD->setInvalidDecl();
      return;
    }

    Diag(NewVD->getLocation(), diag::ext_vla_folded_to_constant);
    NewVD->setType(FixedT);
    NewVD->setTypeSourceInfo(FixedTInfo);
  }

  if (T->isVoidType()) {
    if (NewVD->isThisDeclarationADefinition()) {
      Diag(NewVD->getLocation(), diag::err_typecheck_decl_incomplete_type) << T;
      NewVD->setInvalidDecl();
      return;
    }
  }

  if (!NewVD->hasLocalStorage() && T->isSizelessType()) {
    Diag(NewVD->getLocation(), diag::err_sizeless_nonlocal) << T;
    NewVD->setInvalidDecl();
    return;
  }

  if (NewVD->isConstexpr() &&
      RequireLiteralType(NewVD->getLocation(), T,
                         diag::err_constexpr_var_non_literal)) {
    NewVD->setInvalidDecl();
    return;
  }

  // Check that SVE types are only used in functions with SVE available.
  if (T->isSVESizelessBuiltinType() && isa<FunctionDecl>(CurContext)) {
    const FunctionDecl *FD = cast<FunctionDecl>(CurContext);
    llvm::StringMap<bool> CallerFeatureMap;
    Context.getFunctionFeatureMap(CallerFeatureMap, FD);
    if (!Builtin::evaluateRequiredTargetFeatures("sve", CallerFeatureMap)) {
      Diag(NewVD->getLocation(), diag::err_sve_vector_in_non_sve_target) << T;
      NewVD->setInvalidDecl();
      return;
    }
  }
}

bool Sema::CheckVariableDeclaration(VarDecl *NewVD, LookupResult &Previous) {
  CheckVariableDeclarationType(NewVD);

  // If the decl is already known invalid, don't check it.
  if (NewVD->isInvalidDecl())
    return false;

  // If we did not find anything by this name, look for a non-visible
  // extern "C" declaration with the same name.
  if (Previous.empty() &&
      checkForConflictWithNonVisibleExternC(*this, NewVD, Previous))
    Previous.setShadowed();

  if (!Previous.empty()) {
    MergeVarDecl(NewVD, Previous);
    return true;
  }
  return false;
}

namespace {
StorageClass getFunctionStorageClass(Sema &SemaRef, Declarator &D) {
  const DeclSpec &DS = D.getDeclSpec();
  switch (DS.getStorageClassSpec()) {
  default:
    llvm_unreachable("Unknown storage class!");
  case DeclSpec::SCS_auto:
  case DeclSpec::SCS_register:
    SemaRef.Diag(DS.getStorageClassSpecLoc(), diag::err_typecheck_sclass_func);
    D.getMutableDeclSpec().ClearStorageClassSpecs();
    D.setInvalidType();
    break;
  case DeclSpec::SCS_unspecified:
    break;
  case DeclSpec::SCS_extern:
    if (DS.isExternInLinkageSpec())
      return SC_None;
    return SC_Extern;
  case DeclSpec::SCS_static: {
    if (SemaRef.CurContext->getRedeclContext()->isFunctionOrMethod()) {
      // C99 6.7.1p5: block-scope functions shall not use `static` (only
      // `extern` or unspecified).
      SemaRef.Diag(DS.getStorageClassSpecLoc(), diag::err_static_block_func);
      break;
    } else
      return SC_Static;
  }
  case DeclSpec::SCS_private_extern:
    return SC_PrivateExtern;
  }

  // No explicit storage class has already been returned
  return SC_None;
}
} // namespace

namespace {
FunctionDecl *createNewFunctionDecl(Sema &SemaRef, Declarator &D,
                                    DeclContext *DC, QualType &R,
                                    TypeSourceInfo *TInfo, StorageClass SC) {
  DeclarationNameInfo NameInfo = SemaRef.GetNameForDeclarator(D);
  const DeclSpec &DS = D.getDeclSpec();

  FunctionDecl *NewFD = nullptr;
  bool isInline = DS.isInlineSpecified();

  // Determine whether the function was written with a prototype. This is
  // true when:
  //   - there is a prototype in the declarator, or
  //   - the type R of the function is some kind of typedef or other non-
  //     attributed reference to a type name (which eventually refers to a
  //     function type). Note, we can't always look at the adjusted type to
  //     check this case because attributes may cause a non-function
  //     declarator to still have a function type. e.g.,
  //       typedef void func(int a);
  //       __attribute__((noreturn)) func other_func; // This has a prototype
  bool HasPrototype =
      (D.isFunctionDeclarator() && D.getFunctionTypeInfo().hasPrototype) ||
      (DS.isTypeRep() && SemaRef.GetTypeFromParser(DS.getRepAsType(), nullptr)
                             ->isFunctionProtoType()) ||
      (!R->getAsAdjusted<FunctionType>() && R->isFunctionProtoType());
  assert((HasPrototype || !SemaRef.getLangOpts().requiresStrictPrototypes()) &&
         "Strict prototypes are required");

  NewFD = FunctionDecl::Create(
      SemaRef.Context, DC, D.getBeginLoc(), NameInfo, R, TInfo, SC,
      SemaRef.getCurFPFeatures().isFPConstrained(), isInline, HasPrototype,
      ConstexprSpecKind::Unspecified);
  if (D.isInvalidType())
    NewFD->setInvalidDecl();

  return NewFD;
}
} // namespace

namespace {
DeclContext *getTagInjectionContext(DeclContext *DC) {
  while (!DC->isFileContext() && !DC->isFunctionOrMethod())
    DC = DC->getParent();
  return DC;
}
} // namespace

namespace {
Scope *getTagInjectionScope(Scope *S, const LangOptions &LangOpts) {
  while (S->isRecordScope() || ((S->getFlags() & Scope::DeclScope) == 0) ||
         (S->getEntity() && S->getEntity()->isTransparentContext()))
    S = S->getParent();
  return S;
}
} // namespace

// ===----------------------------------------------------------------------===
// Function declarations & multiversion dispatch
// ===----------------------------------------------------------------------===

NamedDecl *Sema::OnFunctionDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                      TypeSourceInfo *TInfo,
                                      LookupResult &Previous,
                                      bool &AddToScope) {
  QualType R = TInfo->getType();

  assert(R->isFunctionType());

  const DeclSpec &FDS = D.getDeclSpec();
  StorageClass SC = getFunctionStorageClass(*this, D);

  if (DeclSpec::TSCS TSCS = FDS.getThreadStorageClassSpec())
    Diag(FDS.getThreadStorageClassSpecLoc(), diag::err_invalid_thread)
        << DeclSpec::getSpecifierName(TSCS);

  DeclContext *OriginalDC = DC;
  bool IsLocalExternDecl = adjustContextForLocalExternDecl(DC);

  FunctionDecl *NewFD = createNewFunctionDecl(*this, D, DC, R, TInfo, SC);
  if (!NewFD)
    return nullptr;

  // Lexical context may differ from semantic context for locals.
  NewFD->setLexicalDeclContext(CurContext);

  if (IsLocalExternDecl)
    NewFD->setLocalExternDecl();

  FilterLookupForScope(Previous, OriginalDC, S, shouldConsiderLinkage(NewFD));

  // Handle GNU asm-label extension (encoded as an attribute).
  if (Expr *E = (Expr *)D.getAsmLabel()) {
    // The parser guarantees this is a string.
    StringLiteral *SE = cast<StringLiteral>(E);
    NewFD->addAttr(AsmLabelAttr::Create(Context, SE->getString(),
                                        /*IsLiteralLabel=*/true,
                                        SE->getStrTokenLoc(0)));
  }

  // Copy the parameter declarations from the declarator D to the function
  // declaration NewFD, if they are available.  First scavenge them into Params.
  llvm::SmallVector<ParmVarDecl *, 16> Params;
  unsigned FTIIdx;
  if (D.isFunctionDeclarator(FTIIdx)) {
    DeclaratorChunk::FunctionTypeInfo &FTI = D.getTypeObject(FTIIdx).Fun;

    // Check for C99 6.7.5.3p10 - foo(void) is a non-varargs
    // function that takes no arguments, not a function that takes a
    // single void argument.
    // We let through "const void" here because Sema::ResolveDeclaratorType
    // already checks for that case.
    if (FTIHasNonVoidParameters(FTI) && FTI.Params[0].Param) {
      for (unsigned i = 0, e = FTI.NumParams; i != e; ++i) {
        ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
        assert(Param->getDeclContext() != NewFD && "Was set before ?");
        Param->setDeclContext(NewFD);
        Params.push_back(Param);

        if (Param->isInvalidDecl())
          NewFD->setInvalidDecl();
      }
    }

    {
      // Find all the tag declarations from the prototype and move them
      // into the function DeclContext.
      DeclContext *PrototypeTagContext =
          getTagInjectionContext(NewFD->getLexicalDeclContext());
      for (NamedDecl *NonParmDecl : FTI.getDeclsInPrototype()) {
        auto *TD = dyn_cast<TagDecl>(NonParmDecl);

        // We don't want to reparent enumerators. Look at their parent enum
        // instead.
        if (!TD) {
          if (auto *ECD = dyn_cast<EnumConstantDecl>(NonParmDecl))
            TD = cast<EnumDecl>(ECD->getDeclContext());
        }
        if (!TD)
          continue;
        DeclContext *TagDC = TD->getLexicalDeclContext();
        if (!TagDC->containsDecl(TD))
          continue;
        TagDC->removeDecl(TD);
        TD->setDeclContext(NewFD);
        NewFD->addDecl(TD);

        // Preserve the lexical DeclContext if it is not the surrounding tag
        // injection context of the FD. In this example, the semantic context of
        // E will be f and the lexical context will be S, while both the
        // semantic and lexical contexts of S will be f:
        //   void f(struct S { enum E { a } f; } s);
        if (TagDC != PrototypeTagContext)
          TD->setLexicalDeclContext(TagDC);
      }
    }
  } else if (const FunctionProtoType *FT = R->getAs<FunctionProtoType>()) {
    // When we're declaring a function with a typedef, typeof, etc as in the
    // following example, we'll need to synthesize (unnamed)
    // parameters for use in the declaration.
    //
    // @code
    // typedef void fn(int);
    // fn f;
    // @endcode

    // Synthesize a parameter for each argument type.
    for (const auto &AI : FT->param_types()) {
      ParmVarDecl *Param =
          FormParmVarDeclForTypedef(NewFD, D.getIdentifierLoc(), AI);
      Param->setScopeInfo(0, Params.size());
      Params.push_back(Param);
    }
  } else {
    assert(R->isFunctionNoProtoType() && NewFD->getNumParams() == 0 &&
           "Should not need args for typedef of non-prototype fn");
  }

  // Finally, we know we have the right number of parameters, install them.
  NewFD->setParams(Params);
  if (D.isFunctionDefinition())
    for (ParmVarDecl *Param : Params)
      attachNeverCStringCleanup(*this, S, Param, NewFD,
                                /*IsFunctionDefinitionParam=*/true);

  if (FDS.isNoreturnSpecified())
    NewFD->addAttr(C11NoReturnAttr::Create(Context, FDS.getNoreturnSpecLoc()));

  // Functions returning a variably modified type violate C99 6.7.5.2p2
  // because all functions have linkage.
  if (!NewFD->isInvalidDecl() &&
      NewFD->getReturnType()->isVariablyModifiedType()) {
    Diag(NewFD->getLocation(), diag::err_vm_func_decl);
    NewFD->setInvalidDecl();
  }

  // Apply an implicit SectionAttr if '#pragma neverc section text' is active
  if (PragmaTextSection.Valid && D.isFunctionDefinition() &&
      !NewFD->hasAttr<SectionAttr>())
    NewFD->addAttr(PragmaNeverCTextSectionAttr::CreateImplicit(
        Context, PragmaTextSection.SectionName,
        PragmaTextSection.PragmaLocation));

  // Apply an implicit SectionAttr if #pragma code_seg is active.
  if (CodeSegStack.CurrentValue && D.isFunctionDefinition() &&
      !NewFD->hasAttr<SectionAttr>()) {
    NewFD->addAttr(SectionAttr::CreateImplicit(
        Context, CodeSegStack.CurrentValue->getString(),
        CodeSegStack.CurrentPragmaLocation, SectionAttr::Declspec_allocate));
    if (UnifySection(CodeSegStack.CurrentValue->getString(),
                     TreeContext::PSF_Implicit | TreeContext::PSF_Execute |
                         TreeContext::PSF_Read,
                     NewFD))
      NewFD->dropAttr<SectionAttr>();
  }

  // Apply an implicit StrictGuardStackCheckAttr if #pragma strict_gs_check is
  // active.
  if (StrictGuardStackCheckStack.CurrentValue && D.isFunctionDefinition() &&
      !NewFD->hasAttr<StrictGuardStackCheckAttr>())
    NewFD->addAttr(StrictGuardStackCheckAttr::CreateImplicit(
        Context, PragmaTextSection.PragmaLocation));

  // Apply an implicit SectionAttr from #pragma code_seg if active.
  if (!NewFD->hasAttr<CodeSegAttr>()) {
    if (Attr *SAttr = getImplicitCodeSegOrSectionAttrForFunction(
            NewFD, D.isFunctionDefinition())) {
      NewFD->addAttr(SAttr);
    }
  }

  ApplyDeclAttributes(S, NewFD, D);
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  if (NewTVA && !NewTVA->isDefaultVersion() &&
      !Context.getTargetInfo().hasFeature("fmv")) {
    // Don't add to scope fmv functions declarations if fmv disabled
    AddToScope = false;
    return NewFD;
  }

  // Perform semantic checking on the function declaration.
  if (!NewFD->isInvalidDecl() && NewFD->isMain())
    CheckMain(NewFD, FDS);

  if (!NewFD->isInvalidDecl() && NewFD->isMSVCRTEntryPoint())
    CheckMSVCRTEntryPoint(NewFD);

  if (!NewFD->isInvalidDecl())
    D.setRedeclaration(
        CheckFunctionDeclaration(S, NewFD, Previous, D.isFunctionDefinition()));
  else if (!Previous.empty())
    // Recover gracefully from an invalid redeclaration.
    D.setRedeclaration(true);
  assert((NewFD->isInvalidDecl() || !D.isRedeclaration() ||
          Previous.getResultKind() != LookupResult::FoundOverloaded) &&
         "previous declaration set still overloaded");

  // Diagnose no-prototype function declarations with calling conventions that
  // don't support variadic calls. Only do this in C and do it after merging
  // possibly prototyped redeclarations.
  const FunctionType *FT = NewFD->getType()->castAs<FunctionType>();
  if (isa<FunctionNoProtoType>(FT) && !D.isFunctionDefinition()) {
    CallingConv CC = FT->getExtInfo().getCC();
    if (!supportsVariadicCall(CC)) {
      // Windows system headers sometimes accidentally use stdcall without
      // (void) parameters, so we relax this to a warning.
      int DiagID =
          CC == CC_X86StdCall ? diag::warn_cconv_knr : diag::err_cconv_knr;
      Diag(NewFD->getLocation(), DiagID)
          << FunctionType::getNameForCallConv(CC);
    }
  }

  // If this is the first declaration of a library builtin function, add
  // attributes as appropriate.
  if (!D.isRedeclaration()) {
    if (IdentifierInfo *II = Previous.getLookupName().getAsIdentifierInfo()) {
      if (unsigned BuiltinID = II->getBuiltinID()) {
        if (NewFD->getDeclContext()->getRedeclContext()->isFileContext()) {
          // Validate the type matches unless this builtin is specified as
          // matching regardless of its declared type.
          if (Context.BuiltinInfo.allowTypeMismatch(BuiltinID)) {
            NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
          } else {
            TreeContext::GetBuiltinTypeError Error;
            QualType BuiltinType = Context.GetBuiltinType(BuiltinID, Error);

            if (!Error && !BuiltinType.isNull() &&
                Context.hasSameType(NewFD->getType(), BuiltinType))
              NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
          }
        }
      }
    }
  }

  ProcessPragmaWeak(S, NewFD);
  checkAttributesAfterMerging(*this, *NewFD);

  AddKnownFunctionAttributes(NewFD);

  if (NewFD->hasAttr<OverloadableAttr>() &&
      !NewFD->getType()->getAs<FunctionProtoType>()) {
    Diag(NewFD->getLocation(), diag::err_attribute_overloadable_no_prototype)
        << NewFD;
    NewFD->dropAttr<OverloadableAttr>();
  }

  // If there's a #pragma GCC visibility in scope, and this isn't a struct or
  // union member, set the visibility of this function.
  if (!DC->isRecord() && NewFD->isExternallyVisible())
    AddPushedVisibilityAttribute(NewFD);

  // If this is a function definition, check if we have to apply any
  // attributes (i.e. optnone and no_builtin) due to a pragma.
  if (D.isFunctionDefinition()) {
    AddRangeBasedOptnone(NewFD);
    AddImplicitMSFunctionNoBuiltinAttr(NewFD);
    AddSectionMSAllocText(NewFD);
    ModifyFnAttributesMSPragmaOptimize(NewFD);
  }

  // If this is the first declaration of an extern "C" function, update the map.
  if (NewFD->isFirstDecl() && !NewFD->isInvalidDecl() && NewFD->isExternC())
    RegisterLocallyScopedExternCDecl(NewFD, S);

  NewFD->setRangeEnd(D.getSourceRange().getEnd());

  if (D.isRedeclaration() && !Previous.empty()) {
    NamedDecl *Prev = Previous.getRepresentativeDecl();
    checkDLLAttributeRedeclaration(*this, Prev, NewFD, false,
                                   D.isFunctionDefinition());
  }

  MarkUnusedFileScopedDecl(NewFD);

  // Diagnose availability attributes. Availability cannot be used on functions
  // that are run during load/unload.
  if (const auto *attr = NewFD->getAttr<AvailabilityAttr>()) {
    if (NewFD->hasAttr<ConstructorAttr>()) {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 0;
      NewFD->dropAttr<AvailabilityAttr>();
    }
    if (NewFD->hasAttr<DestructorAttr>()) {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 1;
      NewFD->dropAttr<AvailabilityAttr>();
    }
  }

#ifndef _WIN32
  // Diagnose no_builtin attribute on function declaration that are not a
  // definition.
  // We should really be doing this in
  // SemaDeclAttr.cpp::handleNoBuiltinAttr, unfortunately we only have access to
  // the FunctionDecl and at this point of the code
  // FunctionDecl::isThisDeclarationADefinition() which always returns `false`
  // because Sema::OnStartOfFunctionDef has not been called yet.
  if (const auto *NBA = NewFD->getAttr<NoBuiltinAttr>())
    switch (D.getFunctionDefinitionKind()) {
    case FunctionDefinitionKind::Declaration:
      Diag(NBA->getLocation(), diag::err_attribute_no_builtin_on_non_definition)
          << NBA->getSpelling();
      break;
    case FunctionDefinitionKind::Definition:
      break;
    }
#endif
  return NewFD;
}

Attr *Sema::getImplicitCodeSegOrSectionAttrForFunction(const FunctionDecl *FD,
                                                       bool IsDefinition) {
  if (!FD->hasAttr<SectionAttr>() && IsDefinition && CodeSegStack.CurrentValue)
    return SectionAttr::CreateImplicit(
        getTreeContext(), CodeSegStack.CurrentValue->getString(),
        CodeSegStack.CurrentPragmaLocation, SectionAttr::Declspec_allocate);
  return nullptr;
}

namespace {
bool checkMultiVersionValue(Sema &S, const FunctionDecl *FD) {
  const auto *TA = FD->getAttr<TargetAttr>();
  const auto *TVA = FD->getAttr<TargetVersionAttr>();
  assert(
      (TA || TVA) &&
      "MultiVersion candidate requires a target or target_version attribute");
  const TargetInfo &TargetInfo = S.Context.getTargetInfo();
  enum ErrType { Feature = 0, Architecture = 1 };

  if (TA) {
    ParsedTargetAttr ParseInfo =
        S.getTreeContext().getTargetInfo().parseTargetAttr(
            TA->getFeaturesStr());
    if (!ParseInfo.CPU.empty() && !TargetInfo.validateCpuIs(ParseInfo.CPU)) {
      S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
          << Architecture << ParseInfo.CPU;
      return true;
    }
    for (const auto &Feat : ParseInfo.Features) {
      auto BareFeat = llvm::StringRef{Feat}.substr(1);
      if (Feat[0] == '-') {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << ("no-" + BareFeat).str();
        return true;
      }

      if (!TargetInfo.validateCpuSupports(BareFeat) ||
          !TargetInfo.isValidFeatureName(BareFeat)) {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << BareFeat;
        return true;
      }
    }
  }

  if (TVA) {
    llvm::SmallVector<llvm::StringRef, 8> Feats;
    TVA->getFeatures(Feats);
    for (const auto &Feat : Feats) {
      if (!TargetInfo.validateCpuSupports(Feat)) {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << Feat;
        return true;
      }
    }
  }
  return false;
}
} // namespace

// Provide a white-list of attributes that are allowed to be combined with
// multiversion functions.
namespace {
bool attrCompatibleWithMultiVersion(attr::Kind Kind, MultiVersionKind MVKind) {
  // Note: this list/diagnosis must match the list in
  // checkMultiversionAttributesAllSame.
  switch (Kind) {
  default:
    return false;
  case attr::Used:
    return MVKind == MultiVersionKind::Target;
  case attr::NonNull:
  case attr::NoThrow:
    return true;
  }
}
} // namespace

namespace {
bool checkNonMultiVersionCompatAttributes(Sema &S, const FunctionDecl *FD,
                                          const FunctionDecl *CausedFD,
                                          MultiVersionKind MVKind) {
  const auto Diagnose = [FD, CausedFD, MVKind](Sema &S, const Attr *A) {
    S.Diag(FD->getLocation(), diag::err_multiversion_disallowed_other_attr)
        << static_cast<unsigned>(MVKind) << A;
    if (CausedFD)
      S.Diag(CausedFD->getLocation(), diag::note_multiversioning_caused_here);
    return true;
  };

  for (const Attr *A : FD->attrs()) {
    switch (A->getKind()) {
    case attr::CPUDispatch:
    case attr::CPUSpecific:
      if (MVKind != MultiVersionKind::CPUDispatch &&
          MVKind != MultiVersionKind::CPUSpecific)
        return Diagnose(S, A);
      break;
    case attr::Target:
      if (MVKind != MultiVersionKind::Target)
        return Diagnose(S, A);
      break;
    case attr::TargetVersion:
      if (MVKind != MultiVersionKind::TargetVersion)
        return Diagnose(S, A);
      break;
    case attr::TargetClones:
      if (MVKind != MultiVersionKind::TargetClones)
        return Diagnose(S, A);
      break;
    default:
      if (!attrCompatibleWithMultiVersion(A->getKind(), MVKind))
        return Diagnose(S, A);
      break;
    }
  }
  return false;
}
} // namespace

bool Sema::areMultiversionVariantFunctionsCompatible(
    const FunctionDecl *OldFD, const FunctionDecl *NewFD,
    const PartialDiagnostic &NoProtoDiagID,
    const PartialDiagnosticAt &NoteCausedDiagIDAt,
    const PartialDiagnosticAt &DiffDiagIDAt) {
  // Indices match err_multiversion_diff %select (see DiagnosticSemaKinds.td).
  enum Different {
    CallingConv = 0,
    ReturnType = 1,
    InlineSpec = 3,
    Linkage = 4,
    LanguageLinkage = 5,
  };

  if (NoProtoDiagID.getDiagID() != 0 && OldFD &&
      !OldFD->getType()->getAs<FunctionProtoType>()) {
    Diag(OldFD->getLocation(), NoProtoDiagID);
    Diag(NoteCausedDiagIDAt.first, NoteCausedDiagIDAt.second);
    return true;
  }

  if (NoProtoDiagID.getDiagID() != 0 &&
      !NewFD->getType()->getAs<FunctionProtoType>())
    return Diag(NewFD->getLocation(), NoProtoDiagID);

  QualType NewQType = Context.getCanonicalType(NewFD->getType());
  const auto *NewType = cast<FunctionType>(NewQType);
  QualType NewReturnType = NewType->getReturnType();

  // Ensure the return type is identical.
  if (OldFD) {
    QualType OldQType = Context.getCanonicalType(OldFD->getType());
    const auto *OldType = cast<FunctionType>(OldQType);
    FunctionType::ExtInfo OldTypeInfo = OldType->getExtInfo();
    FunctionType::ExtInfo NewTypeInfo = NewType->getExtInfo();

    if (OldTypeInfo.getCC() != NewTypeInfo.getCC())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << CallingConv;

    QualType OldReturnType = OldType->getReturnType();

    if (OldReturnType != NewReturnType)
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << ReturnType;

    if (OldFD->isInlineSpecified() != NewFD->isInlineSpecified())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << InlineSpec;

    if (OldFD->getFormalLinkage() != NewFD->getFormalLinkage())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << Linkage;

    if (OldFD->isExternC() != NewFD->isExternC())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << LanguageLinkage;
  }
  return false;
}

namespace {
bool checkMultiVersionAdditionalRules(Sema &S, const FunctionDecl *OldFD,
                                      const FunctionDecl *NewFD, bool CausesMV,
                                      MultiVersionKind MVKind) {
  if (!S.getTreeContext().getTargetInfo().supportsMultiVersioning()) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_not_supported);
    if (OldFD)
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
    return true;
  }

  if (CausesMV && OldFD &&
      checkNonMultiVersionCompatAttributes(S, OldFD, NewFD, MVKind))
    return true;

  if (checkNonMultiVersionCompatAttributes(S, NewFD, nullptr, MVKind))
    return true;

  // Only allow transition to MultiVersion if it hasn't been used.
  if (OldFD && CausesMV && OldFD->isUsed(false))
    return S.Diag(NewFD->getLocation(), diag::err_multiversion_after_used);

  return S.areMultiversionVariantFunctionsCompatible(
      OldFD, NewFD, S.PDiag(diag::err_multiversion_noproto),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::note_multiversioning_caused_here)),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::err_multiversion_diff)));
}
} // namespace

namespace {
bool checkMultiVersionFirstFunction(Sema &S, FunctionDecl *FD) {
  MultiVersionKind MVKind = FD->getMultiVersionKind();
  assert(MVKind != MultiVersionKind::None &&
         "Function lacks multiversion attribute");
  const auto *TA = FD->getAttr<TargetAttr>();
  const auto *TVA = FD->getAttr<TargetVersionAttr>();
  // Target and target_version only causes MV if it is default, otherwise this
  // is a normal function.
  if ((TA && !TA->isDefaultVersion()) || (TVA && !TVA->isDefaultVersion()))
    return false;

  if ((TA || TVA) && checkMultiVersionValue(S, FD)) {
    FD->setInvalidDecl();
    return true;
  }

  if (checkMultiVersionAdditionalRules(S, nullptr, FD, true, MVKind)) {
    FD->setInvalidDecl();
    return true;
  }

  FD->setIsMultiVersion();
  return false;
}
} // namespace

namespace {
bool previousDeclsHaveMultiVersionAttribute(const FunctionDecl *FD) {
  for (const Decl *D = FD->getPreviousDecl(); D; D = D->getPreviousDecl()) {
    if (D->getAsFunction()->getMultiVersionKind() != MultiVersionKind::None)
      return true;
  }

  return false;
}
} // namespace

namespace {
bool checkTargetCausesMultiVersioning(Sema &S, FunctionDecl *OldFD,
                                      FunctionDecl *NewFD, bool &Redeclaration,
                                      NamedDecl *&OldDecl,
                                      LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  const auto *OldTA = OldFD->getAttr<TargetAttr>();
  const auto *OldTVA = OldFD->getAttr<TargetVersionAttr>();
  // If the old decl is NOT MultiVersioned yet, and we don't cause that
  // to change, this is a simple redeclaration.
  if ((NewTA && !NewTA->isDefaultVersion() &&
       (!OldTA || OldTA->getFeaturesStr() == NewTA->getFeaturesStr())) ||
      (NewTVA && !NewTVA->isDefaultVersion() &&
       (!OldTVA || OldTVA->getName() == NewTVA->getName())))
    return false;

  // Otherwise, this decl causes MultiVersioning.
  if (checkMultiVersionAdditionalRules(S, OldFD, NewFD, true,
                                       NewTVA ? MultiVersionKind::TargetVersion
                                              : MultiVersionKind::Target)) {
    NewFD->setInvalidDecl();
    return true;
  }

  if (checkMultiVersionValue(S, NewFD)) {
    NewFD->setInvalidDecl();
    return true;
  }

  // If this is 'default', permit the forward declaration.
  if (!OldFD->isMultiVersion() &&
      ((NewTA && NewTA->isDefaultVersion() && !OldTA) ||
       (NewTVA && NewTVA->isDefaultVersion() && !OldTVA))) {
    Redeclaration = true;
    OldDecl = OldFD;
    OldFD->setIsMultiVersion();
    NewFD->setIsMultiVersion();
    return false;
  }

  if (checkMultiVersionValue(S, OldFD)) {
    S.Diag(NewFD->getLocation(), diag::note_multiversioning_caused_here);
    NewFD->setInvalidDecl();
    return true;
  }

  if (NewTA) {
    ParsedTargetAttr OldParsed =
        S.getTreeContext().getTargetInfo().parseTargetAttr(
            OldTA->getFeaturesStr());
    llvm::sort(OldParsed.Features);
    ParsedTargetAttr NewParsed =
        S.getTreeContext().getTargetInfo().parseTargetAttr(
            NewTA->getFeaturesStr());
    // Sort order doesn't matter, it just needs to be consistent.
    llvm::sort(NewParsed.Features);
    if (OldParsed == NewParsed) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  if (NewTVA) {
    llvm::SmallVector<llvm::StringRef, 8> Feats;
    OldTVA->getFeatures(Feats);
    llvm::sort(Feats);
    llvm::SmallVector<llvm::StringRef, 8> NewFeats;
    NewTVA->getFeatures(NewFeats);
    llvm::sort(NewFeats);

    if (Feats == NewFeats) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  for (const auto *FD : OldFD->redecls()) {
    const auto *CurTA = FD->getAttr<TargetAttr>();
    const auto *CurTVA = FD->getAttr<TargetVersionAttr>();
    // We allow forward declarations before ANY multiversioning attributes, but
    // nothing after the fact.
    if (previousDeclsHaveMultiVersionAttribute(FD) &&
        ((NewTA && (!CurTA || CurTA->isInherited())) ||
         (NewTVA && (!CurTVA || CurTVA->isInherited())))) {
      S.Diag(FD->getLocation(), diag::err_multiversion_required_in_redecl)
          << (NewTA ? 0 : 2);
      S.Diag(NewFD->getLocation(), diag::note_multiversioning_caused_here);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  OldFD->setIsMultiVersion();
  NewFD->setIsMultiVersion();
  Redeclaration = false;
  OldDecl = nullptr;
  Previous.clear();
  return false;
}
} // namespace

namespace {
bool multiVersionTypesCompatible(MultiVersionKind Old, MultiVersionKind New) {
  if (Old == New || Old == MultiVersionKind::None ||
      New == MultiVersionKind::None)
    return true;

  return (Old == MultiVersionKind::CPUDispatch &&
          New == MultiVersionKind::CPUSpecific) ||
         (Old == MultiVersionKind::CPUSpecific &&
          New == MultiVersionKind::CPUDispatch);
}
} // namespace

namespace {
bool checkMultiVersionAdditionalDecl(
    Sema &S, FunctionDecl *OldFD, FunctionDecl *NewFD,
    MultiVersionKind NewMVKind, const CPUDispatchAttr *NewCPUDisp,
    const CPUSpecificAttr *NewCPUSpec, const TargetClonesAttr *NewClones,
    bool &Redeclaration, NamedDecl *&OldDecl, LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  MultiVersionKind OldMVKind = OldFD->getMultiVersionKind();
  // Disallow mixing of multiversioning types.
  if (!multiVersionTypesCompatible(OldMVKind, NewMVKind)) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_types_mixed);
    S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
    NewFD->setInvalidDecl();
    return true;
  }

  ParsedTargetAttr NewParsed;
  if (NewTA) {
    NewParsed = S.getTreeContext().getTargetInfo().parseTargetAttr(
        NewTA->getFeaturesStr());
    llvm::sort(NewParsed.Features);
  }
  llvm::SmallVector<llvm::StringRef, 8> NewFeats;
  if (NewTVA) {
    NewTVA->getFeatures(NewFeats);
    llvm::sort(NewFeats);
  }

  bool MayNeedOverloadableChecks =
      canOverloadFunction(Previous, S.Context, NewFD);

  // Next, check ALL non-invalid non-overloads to see if this is a redeclaration
  // of a previous member of the MultiVersion set.
  for (NamedDecl *ND : Previous) {
    FunctionDecl *CurFD = ND->getAsFunction();
    if (!CurFD || CurFD->isInvalidDecl())
      continue;
    if (MayNeedOverloadableChecks && S.IsOverload(NewFD, CurFD))
      continue;

    if (NewMVKind == MultiVersionKind::None &&
        OldMVKind == MultiVersionKind::TargetVersion) {
      NewFD->addAttr(TargetVersionAttr::CreateImplicit(
          S.Context, "default", NewFD->getSourceRange()));
      NewFD->setIsMultiVersion();
      NewMVKind = MultiVersionKind::TargetVersion;
      if (!NewTVA) {
        NewTVA = NewFD->getAttr<TargetVersionAttr>();
        NewTVA->getFeatures(NewFeats);
        llvm::sort(NewFeats);
      }
    }

    switch (NewMVKind) {
    case MultiVersionKind::None:
      assert(OldMVKind == MultiVersionKind::TargetClones &&
             "Only target_clones can be omitted in subsequent declarations");
      break;
    case MultiVersionKind::Target: {
      const auto *CurTA = CurFD->getAttr<TargetAttr>();
      if (CurTA->getFeaturesStr() == NewTA->getFeaturesStr()) {
        NewFD->setIsMultiVersion();
        Redeclaration = true;
        OldDecl = ND;
        return false;
      }

      ParsedTargetAttr CurParsed =
          S.getTreeContext().getTargetInfo().parseTargetAttr(
              CurTA->getFeaturesStr());
      llvm::sort(CurParsed.Features);
      if (CurParsed == NewParsed) {
        S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      break;
    }
    case MultiVersionKind::TargetVersion: {
      const auto *CurTVA = CurFD->getAttr<TargetVersionAttr>();
      if (CurTVA->getName() == NewTVA->getName()) {
        NewFD->setIsMultiVersion();
        Redeclaration = true;
        OldDecl = ND;
        return false;
      }
      llvm::SmallVector<llvm::StringRef, 8> CurFeats;
      if (CurTVA) {
        CurTVA->getFeatures(CurFeats);
        llvm::sort(CurFeats);
      }
      if (CurFeats == NewFeats) {
        S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      break;
    }
    case MultiVersionKind::TargetClones: {
      const auto *CurClones = CurFD->getAttr<TargetClonesAttr>();
      Redeclaration = true;
      OldDecl = CurFD;
      NewFD->setIsMultiVersion();

      if (CurClones && NewClones &&
          (CurClones->featuresStrs_size() != NewClones->featuresStrs_size() ||
           !std::equal(CurClones->featuresStrs_begin(),
                       CurClones->featuresStrs_end(),
                       NewClones->featuresStrs_begin()))) {
        S.Diag(NewFD->getLocation(), diag::err_target_clone_doesnt_match);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }

      return false;
    }
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::CPUDispatch: {
      const auto *CurCPUSpec = CurFD->getAttr<CPUSpecificAttr>();
      const auto *CurCPUDisp = CurFD->getAttr<CPUDispatchAttr>();
      // Handle CPUDispatch/CPUSpecific versions.
      // Only 1 CPUDispatch function is allowed, this will make it go through
      // the redeclaration errors.
      if (NewMVKind == MultiVersionKind::CPUDispatch &&
          CurFD->hasAttr<CPUDispatchAttr>()) {
        if (CurCPUDisp->cpus_size() == NewCPUDisp->cpus_size() &&
            std::equal(
                CurCPUDisp->cpus_begin(), CurCPUDisp->cpus_end(),
                NewCPUDisp->cpus_begin(),
                [](const IdentifierInfo *Cur, const IdentifierInfo *New) {
                  return Cur->getName() == New->getName();
                })) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }

        // If the declarations don't match, this is an error condition.
        S.Diag(NewFD->getLocation(), diag::err_cpu_dispatch_mismatch);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      if (NewMVKind == MultiVersionKind::CPUSpecific && CurCPUSpec) {
        if (CurCPUSpec->cpus_size() == NewCPUSpec->cpus_size() &&
            std::equal(
                CurCPUSpec->cpus_begin(), CurCPUSpec->cpus_end(),
                NewCPUSpec->cpus_begin(),
                [](const IdentifierInfo *Cur, const IdentifierInfo *New) {
                  return Cur->getName() == New->getName();
                })) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }

        // Only 1 version of CPUSpecific is allowed for each CPU.
        for (const IdentifierInfo *CurII : CurCPUSpec->cpus()) {
          for (const IdentifierInfo *NewII : NewCPUSpec->cpus()) {
            if (CurII == NewII) {
              S.Diag(NewFD->getLocation(), diag::err_cpu_specific_multiple_defs)
                  << NewII;
              S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
              NewFD->setInvalidDecl();
              return true;
            }
          }
        }
      }
      break;
    }
    }
  }

  // Else, this is simply a non-redecl case.  Checking the 'value' is only
  // necessary in the Target case, since The CPUSpecific/Dispatch cases are
  // handled in the attribute adding step.
  if ((NewMVKind == MultiVersionKind::TargetVersion ||
       NewMVKind == MultiVersionKind::Target) &&
      checkMultiVersionValue(S, NewFD)) {
    NewFD->setInvalidDecl();
    return true;
  }

  if (checkMultiVersionAdditionalRules(S, OldFD, NewFD,
                                       !OldFD->isMultiVersion(), NewMVKind)) {
    NewFD->setInvalidDecl();
    return true;
  }

  // Permit forward declarations in the case where these two are compatible.
  if (!OldFD->isMultiVersion()) {
    OldFD->setIsMultiVersion();
    NewFD->setIsMultiVersion();
    Redeclaration = true;
    OldDecl = OldFD;
    return false;
  }

  NewFD->setIsMultiVersion();
  Redeclaration = false;
  OldDecl = nullptr;
  Previous.clear();
  return false;
}
} // namespace

namespace {
bool checkMultiVersionFunction(Sema &S, FunctionDecl *NewFD,
                               bool &Redeclaration, NamedDecl *&OldDecl,
                               LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  const auto *NewCPUDisp = NewFD->getAttr<CPUDispatchAttr>();
  const auto *NewCPUSpec = NewFD->getAttr<CPUSpecificAttr>();
  const auto *NewClones = NewFD->getAttr<TargetClonesAttr>();
  MultiVersionKind MVKind = NewFD->getMultiVersionKind();

  // Main isn't allowed to become a multiversion function, however it IS
  // permitted to have 'main' be marked with the 'target' optimization hint,
  // for 'target_version' only default is allowed.
  if (NewFD->isMain()) {
    if (MVKind != MultiVersionKind::None &&
        !(MVKind == MultiVersionKind::Target && !NewTA->isDefaultVersion()) &&
        !(MVKind == MultiVersionKind::TargetVersion &&
          NewTVA->isDefaultVersion())) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_not_allowed_on_main);
      NewFD->setInvalidDecl();
      return true;
    }
    return false;
  }

  // Target attribute on AArch64 is not used for multiversioning
  if (NewTA && S.getTreeContext().getTargetInfo().getTriple().isAArch64())
    return false;

  if (!OldDecl || !OldDecl->getAsFunction() ||
      OldDecl->getDeclContext()->getRedeclContext() !=
          NewFD->getDeclContext()->getRedeclContext()) {
    // If there's no previous declaration, AND this isn't attempting to cause
    // multiversioning, this isn't an error condition.
    if (MVKind == MultiVersionKind::None)
      return false;
    return checkMultiVersionFirstFunction(S, NewFD);
  }

  FunctionDecl *OldFD = OldDecl->getAsFunction();

  if (!OldFD->isMultiVersion() && MVKind == MultiVersionKind::None) {
    if (NewTVA || !OldFD->getAttr<TargetVersionAttr>())
      return false;
    if (!NewFD->getType()->getAs<FunctionProtoType>()) {
      // Multiversion declaration doesn't have prototype.
      S.Diag(NewFD->getLocation(), diag::err_multiversion_noproto);
      NewFD->setInvalidDecl();
    } else {
      // No "target_version" attribute is equivalent to "default" attribute.
      NewFD->addAttr(TargetVersionAttr::CreateImplicit(
          S.Context, "default", NewFD->getSourceRange()));
      NewFD->setIsMultiVersion();
      OldFD->setIsMultiVersion();
      OldDecl = OldFD;
      Redeclaration = true;
    }
    return true;
  }

  // Multiversioned redeclarations aren't allowed to omit the attribute, except
  // for target_clones and target_version.
  if (OldFD->isMultiVersion() && MVKind == MultiVersionKind::None &&
      OldFD->getMultiVersionKind() != MultiVersionKind::TargetClones &&
      OldFD->getMultiVersionKind() != MultiVersionKind::TargetVersion) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_required_in_redecl)
        << (OldFD->getMultiVersionKind() != MultiVersionKind::Target);
    NewFD->setInvalidDecl();
    return true;
  }

  if (!OldFD->isMultiVersion()) {
    switch (MVKind) {
    case MultiVersionKind::Target:
    case MultiVersionKind::TargetVersion:
      return checkTargetCausesMultiVersioning(S, OldFD, NewFD, Redeclaration,
                                              OldDecl, Previous);
    case MultiVersionKind::TargetClones:
      if (OldFD->isUsed(false)) {
        NewFD->setInvalidDecl();
        return S.Diag(NewFD->getLocation(), diag::err_multiversion_after_used);
      }
      OldFD->setIsMultiVersion();
      break;

    case MultiVersionKind::CPUDispatch:
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::None:
      break;
    }
  }

  // At this point, we have a multiversion function decl (in OldFD) AND an
  // appropriate attribute in the current function decl.  Resolve that these are
  // still compatible with previous declarations.
  return checkMultiVersionAdditionalDecl(S, OldFD, NewFD, MVKind, NewCPUDisp,
                                         NewCPUSpec, NewClones, Redeclaration,
                                         OldDecl, Previous);
}
} // namespace

bool Sema::CheckFunctionDeclaration(Scope *S, FunctionDecl *NewFD,
                                    LookupResult &Previous, bool DeclIsDefn) {
  assert(!NewFD->getReturnType()->isVariablyModifiedType() &&
         "Variably modified return types are not handled here");

  // When lookup is not shadowed, merge the function type with the previous
  // visible declaration.
  bool MergeTypeWithPrevious = !Previous.isShadowed();

  bool Redeclaration = false;
  NamedDecl *OldDecl = nullptr;
  bool MayNeedOverloadableChecks = false;

  // Merge or overload the declaration with an existing declaration of
  // the same name, if appropriate.
  if (!Previous.empty()) {
    // Determine whether NewFD is an overload of PrevDecl or
    // a declaration that requires merging. If it's an overload,
    // there's no more work to do here; we'll just add the new
    // function to the scope.
    if (!canOverloadFunction(Previous, Context, NewFD)) {
      NamedDecl *Candidate = Previous.getRepresentativeDecl();
      if (shouldLinkPossiblyHiddenDecl(Candidate, NewFD)) {
        Redeclaration = true;
        OldDecl = Candidate;
      }
    } else {
      MayNeedOverloadableChecks = true;
      switch (CheckOverload(NewFD, Previous, OldDecl)) {
      case Ovl_Match:
        Redeclaration = true;
        break;

      case Ovl_NonFunction:
        Redeclaration = true;
        break;

      case Ovl_Overload:
        Redeclaration = false;
        break;
      }
    }
  }

  // Check for a previous extern "C" declaration with this name.
  if (!Redeclaration &&
      checkForConflictWithNonVisibleExternC(*this, NewFD, Previous)) {
    if (!Previous.empty()) {
      // This is an extern "C" declaration with the same name as a previous
      // declaration, and thus redeclares that entity...
      Redeclaration = true;
      OldDecl = Previous.getFoundDecl();
      MergeTypeWithPrevious = false;

      // ... except in the presence of __attribute__((overloadable)).
      if (OldDecl->hasAttr<OverloadableAttr>() ||
          NewFD->hasAttr<OverloadableAttr>()) {
        if (IsOverload(NewFD, cast<FunctionDecl>(OldDecl))) {
          MayNeedOverloadableChecks = true;
          Redeclaration = false;
          OldDecl = nullptr;
        }
      }
    }
  }

  if (checkMultiVersionFunction(*this, NewFD, Redeclaration, OldDecl, Previous))
    return Redeclaration;

  if (Redeclaration) {
    // NewFD and OldDecl represent declarations that need to be
    // merged.
    if (MergeFunctionDecl(NewFD, OldDecl, S, MergeTypeWithPrevious,
                          DeclIsDefn)) {
      NewFD->setInvalidDecl();
      return Redeclaration;
    }

    Previous.clear();
    Previous.addDecl(OldDecl);

    NewFD->setPreviousDeclaration(cast<FunctionDecl>(OldDecl));
  } else if (MayNeedOverloadableChecks && !NewFD->getAttr<OverloadableAttr>()) {
    assert((Previous.empty() ||
            llvm::any_of(Previous,
                         [](const NamedDecl *ND) {
                           return ND->hasAttr<OverloadableAttr>();
                         })) &&
           "Non-redecls shouldn't happen without overloadable present");

    auto OtherUnmarkedIter = llvm::find_if(Previous, [](const NamedDecl *ND) {
      const auto *FD = dyn_cast<FunctionDecl>(ND);
      return FD && !FD->hasAttr<OverloadableAttr>();
    });

    if (OtherUnmarkedIter != Previous.end()) {
      Diag(NewFD->getLocation(),
           diag::err_attribute_overloadable_multiple_unmarked_overloads);
      Diag((*OtherUnmarkedIter)->getLocation(),
           diag::note_attribute_overloadable_prev_overload)
          << false;

      NewFD->addAttr(OverloadableAttr::CreateImplicit(Context));
    }
  }

  // Semantic checking for this function declaration (in isolation).

  // Check if the function definition uses any AArch64 SME features without
  // having the '+sme' feature enabled.
  if (DeclIsDefn) {
    bool UsesSM = NewFD->hasAttr<ArmLocallyStreamingAttr>();
    bool UsesZA = NewFD->hasAttr<ArmNewZAAttr>();
    if (const auto *FPT = NewFD->getType()->getAs<FunctionProtoType>()) {
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      UsesSM |=
          EPI.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask;
      UsesZA |= EPI.AArch64SMEAttributes & FunctionType::SME_PStateZASharedMask;
    }

    if (UsesSM || UsesZA) {
      llvm::StringMap<bool> FeatureMap;
      Context.getFunctionFeatureMap(FeatureMap, NewFD);
      if (!FeatureMap.contains("sme")) {
        if (UsesSM)
          Diag(NewFD->getLocation(),
               diag::err_sme_definition_using_sm_in_non_sme_target);
        else
          Diag(NewFD->getLocation(),
               diag::err_sme_definition_using_za_in_non_sme_target);
      }
    }
  }

  return Redeclaration;
}

void Sema::CheckMain(FunctionDecl *FD, const DeclSpec &DS) {
  // Hosted: function specifiers on main are restricted. static main warns,
  // inline main errors, _Noreturn main is accepted as extension.
  if (FD->getStorageClass() == SC_Static)
    Diag(DS.getStorageClassSpecLoc(), diag::warn_static_main)
        << FixItHint::CreateRemoval(DS.getStorageClassSpecLoc());
  if (FD->isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_main)
        << FixItHint::CreateRemoval(DS.getInlineSpecLoc());
  if (DS.isNoreturnSpecified()) {
    SourceLocation NoreturnLoc = DS.getNoreturnSpecLoc();
    SourceRange NoreturnRange(NoreturnLoc, getLocForEndOfToken(NoreturnLoc));
    Diag(NoreturnLoc, diag::ext_noreturn_main);
    Diag(NoreturnLoc, diag::note_main_remove_noreturn)
        << FixItHint::CreateRemoval(NoreturnRange);
  }
  if (FD->isConstexpr()) {
    Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_main)
        << FixItHint::CreateRemoval(DS.getConstexprSpecLoc());
    FD->setConstexprKind(ConstexprSpecKind::Unspecified);
  }

  QualType T = FD->getType();
  assert(T->isFunctionType() && "function decl is not of function type");
  const FunctionType *FT = T->castAs<FunctionType>();

  if (FT->getCallConv() != CC_C) {
    FT = Context.adjustFunctionType(FT, FT->getExtInfo().withCallingConv(CC_C));
    FD->setType(QualType(FT, 0));
    T = Context.getCanonicalType(FD->getType());
  }

  if (getLangOpts().GNUMode || getLangOpts().MSVCCompat) {
    // In C with GNU extensions/MSVC we allow main() to have non-integer return
    // type, but we should warn about the extension, and we disable the
    // implicit-return-zero rule.

    // GCC in C mode accepts qualified 'int'.
    if (Context.hasSameUnqualifiedType(FT->getReturnType(), Context.IntTy))
      FD->setHasImplicitReturnZero(true);
    else {
      Diag(FD->getTypeSpecStartLoc(), diag::ext_main_returns_nonint);
      SourceRange RTRange = FD->getReturnTypeSourceRange();
      if (RTRange.isValid())
        Diag(RTRange.getBegin(), diag::note_main_change_return_type)
            << FixItHint::CreateReplacement(
                   RTRange, tok::getKeywordSpelling(tok::kw_int));
    }
  } else {
    // C99 5.1.2.2.3: falling off the end of `main` returns 0.
    // All the standards say that main() should return 'int'.
    if (Context.hasSameType(FT->getReturnType(), Context.IntTy))
      FD->setHasImplicitReturnZero(true);
    else {
      // Otherwise, this is just a flat-out error.
      SourceRange RTRange = FD->getReturnTypeSourceRange();
      Diag(FD->getTypeSpecStartLoc(), diag::err_main_returns_nonint)
          << (RTRange.isValid()
                  ? FixItHint::CreateReplacement(
                        RTRange, tok::getKeywordSpelling(tok::kw_int))
                  : FixItHint());
      FD->setInvalidDecl(true);
    }
  }

  // Treat protoless main() as nullary.
  if (isa<FunctionNoProtoType>(FT))
    return;

  const FunctionProtoType *FTP = cast<const FunctionProtoType>(FT);
  unsigned nparams = FTP->getNumParams();
  assert(FD->getNumParams() == nparams);

  bool HasExtraParameters = (nparams > 3);

  if (FTP->isVariadic()) {
    Diag(FD->getLocation(), diag::ext_variadic_main);
  }

  // Darwin passes an undocumented fourth argument of type char**.  If
  // other platforms start sprouting these, the logic below will start
  // getting shifty.
  if (nparams == 4 && Context.getTargetInfo().getTriple().isOSDarwin())
    HasExtraParameters = false;

  if (HasExtraParameters) {
    Diag(FD->getLocation(), diag::err_main_surplus_args) << nparams;
    FD->setInvalidDecl(true);
    nparams = 3;
  }

  QualType CharPP =
      Context.getPointerType(Context.getPointerType(Context.CharTy));
  QualType Expected[] = {Context.IntTy, CharPP, CharPP, CharPP};

  for (unsigned i = 0; i < nparams; ++i) {
    QualType AT = FTP->getParamType(i);

    bool mismatch = true;

    if (Context.hasSameUnqualifiedType(AT, Expected[i]))
      mismatch = false;
    else if (Expected[i] == CharPP) {
      // As an extension, the following forms are okay:
      //   char const **
      //   char const * const *
      //   char * const *

      QualifierCollector qs;
      const PointerType *PT;
      if ((PT = qs.strip(AT)->getAs<PointerType>()) &&
          (PT = qs.strip(PT->getPointeeType())->getAs<PointerType>()) &&
          Context.hasSameType(QualType(qs.strip(PT->getPointeeType()), 0),
                              Context.CharTy)) {
        qs.removeConst();
        mismatch = !qs.empty();
      }
    }

    if (mismatch) {
      Diag(FD->getLocation(), diag::err_main_arg_wrong) << i << Expected[i];
      FD->setInvalidDecl(true);
    }
  }

  if (nparams == 1 && !FD->isInvalidDecl()) {
    Diag(FD->getLocation(), diag::warn_main_one_arg);
  }
}

void Sema::CheckMSVCRTEntryPoint(FunctionDecl *FD) {
  QualType T = FD->getType();
  assert(T->isFunctionType() && "function decl is not of function type");
  const FunctionType *FT = T->castAs<FunctionType>();

  // Set an implicit return of 'zero' if the function can return some integral,
  // enumeration, pointer or nullptr type.
  if (FT->getReturnType()->isIntegralOrEnumerationType() ||
      FT->getReturnType()->isAnyPointerType() ||
      FT->getReturnType()->isNullPtrType())
    // DllMain is exempt because a return value of zero means it failed.
    if (FD->getName() != "DllMain")
      FD->setHasImplicitReturnZero(true);

  // Default calling convention for MSVC entry points is __cdecl.
  if (!hasExplicitCallingConv(T) && FT->getCallConv() != CC_C) {
    FT = Context.adjustFunctionType(FT, FT->getExtInfo().withCallingConv(CC_C));
    FD->setType(QualType(FT, 0));
  }
}

