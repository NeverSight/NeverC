//===- SemaDeclDeclaratorVar.cpp - Variable declarations ------------------------===//

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
// Variable declarations
// ===----------------------------------------------------------------------===

namespace neverc {

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

} // namespace neverc

namespace neverc {

bool checkForConflictWithNonVisibleExternC(Sema &S, const VarDecl *ND,
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

bool checkForConflictWithNonVisibleExternC(Sema &S, const FunctionDecl *ND,
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

} // namespace neverc

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
    else if (!Context.getTargetInfo().isTLSSupported() &&
             !getLangOpts().DynCodeMode)
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



namespace neverc {

DeclContext *getTagInjectionContext(DeclContext *DC) {
  while (!DC->isFileContext() && !DC->isFunctionOrMethod())
    DC = DC->getParent();
  return DC;
}

Scope *getTagInjectionScope(Scope *S, const LangOptions &LangOpts) {
  while (S->isRecordScope() || ((S->getFlags() & Scope::DeclScope) == 0) ||
         (S->getEntity() && S->getEntity()->isTransparentContext()))
    S = S->getParent();
  return S;
}

} // namespace neverc

