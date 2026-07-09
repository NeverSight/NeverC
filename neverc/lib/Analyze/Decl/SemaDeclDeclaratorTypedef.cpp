//===- SemaDeclDeclaratorTypedef.cpp - Declarator processing & typedef handling -===//

#include "SemaDeclUtils.h"
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
// Declarator processing & typedef handling
// ===----------------------------------------------------------------------===

namespace neverc {

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

bool isFunctionDefinitionDiscarded(Sema &S, FunctionDecl *FD) {
  // Try to avoid calling GetGVALinkageForFunction.

  // All cases of this require the 'inline' keyword.
  if (!FD->isInlined())
    return false;

  // Okay, go ahead and call the relatively-more-expensive function.
  return S.Context.GetGVALinkageForFunction(FD) == GVA_AvailableExternally;
}

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

bool shouldConsiderLinkage(const FunctionDecl *FD) {
  const DeclContext *DC = FD->getDeclContext()->getRedeclContext();
  if (DC->isFileContext() || DC->isFunctionOrMethod())
    return true;
  if (DC->isRecord())
    return false;
  llvm_unreachable("Unexpected context");
}

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

void copyAllocSizeAttrFromTypedefToDecl(Sema &S, Decl *D,
                                        const TypedefType *TT) {
  const TypedefNameDecl *TND = TT->getDecl();
  if (const auto *Attribute = TND->getAttr<AllocSizeAttr>()) {
    AllocSizeAttr *Clone = Attribute->clone(S.Context);
    Clone->setInherited(true);
    D->addAttr(Clone);
  }
}

} // namespace neverc


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

NEVERC_HOT NamedDecl *Sema::OnDeclarator(Scope *S, Declarator &D) {
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

namespace neverc {

bool isOutOfScopePreviousDeclaration(NamedDecl *PrevDecl, DeclContext *DC,
                                     TreeContext &Context) {
  if (!PrevDecl)
    return false;

  if (!PrevDecl->hasLinkage())
    return false;

  return true;
}

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

} // namespace neverc






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


// This function emits warning and a corresponding note based on the
// ReadOnlyPlacementAttr attribute. The warning checks that all global variable
// declarations of an annotated type must be const qualified.
namespace neverc {

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

} // namespace neverc

