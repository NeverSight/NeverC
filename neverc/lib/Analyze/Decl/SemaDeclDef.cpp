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
// Variable initialization & completion
// ===----------------------------------------------------------------------===

bool Sema::CheckForConstantInitializer(Expr *Init, QualType DclT) {
  // Need strict checking.  In C89, we need to check for
  // any assignment, increment, decrement, function-calls, or
  // commas outside of a sizeof.  In C99, it's the same list,
  // except that the aforementioned are allowed in unevaluated
  // expressions.  Everything else falls under the
  // "may accept other forms of constant expressions" exception.
  if (Init->containsErrors()) {
    return true;
  }
  const Expr *Culprit;
  if (Init->isConstantInitializer(Context, false, &Culprit))
    return false;
  Diag(Culprit->getExprLoc(), diag::err_init_element_not_constant)
      << Culprit->getSourceRange();
  return true;
}

QualType Sema::deduceVarTypeFromInitializer(VarDecl *VDecl, QualType Type,
                                            TypeSourceInfo *TSI,
                                            SourceRange Range, bool DirectInit,
                                            Expr *Init) {
  assert(VDecl && "deduceVarTypeFromInitializer requires a VarDecl");

  DeducedType *Deduced = Type->getContainedDeducedType();
  assert(Deduced && "deduceVarTypeFromInitializer for non-deduced type");

  // Diagnose auto array declarations in C23, unless it's a supported extension.
  if (getLangOpts().C23 && Type->isArrayType() &&
      !isa_and_present<StringLiteral, InitListExpr>(Init)) {
    Diag(Range.getBegin(), diag::err_auto_not_allowed)
        << (Deduced->getContainedAutoType()->isGNUAutoType() ? 1 : 0)
        << /*in array decl*/ 7 << Range;
    return QualType();
  }

  if (!Init) {
    Diag(VDecl->getLocation(), diag::err_auto_var_requires_init)
        << VDecl->getDeclName() << Type;
    return QualType();
  }

  llvm::ArrayRef<Expr *> DeduceInits = Init;

  auto *PL = dyn_cast_if_present<ParenListExpr>(Init);
  if (DirectInit && PL)
    DeduceInits = PL->exprs();

  if (DirectInit) {
    if (auto *IL = dyn_cast<InitListExpr>(Init))
      DeduceInits = IL->inits();
  }

  // Deduction only works if we have exactly one source expression.
  if (DeduceInits.empty()) {
    Diag(Init->getBeginLoc(), diag::err_auto_var_init_no_expression)
        << VDecl << Type << Range;
    return QualType();
  }

  if (DeduceInits.size() > 1) {
    Diag(DeduceInits[1]->getBeginLoc(),
         diag::err_auto_var_init_multiple_expressions)
        << VDecl << Type << Range;
    return QualType();
  }

  Expr *DeduceInit = DeduceInits[0];
  if (DirectInit && isa<InitListExpr>(DeduceInit)) {
    Diag(Init->getBeginLoc(), diag::err_auto_var_init_paren_braces)
        << isa<InitListExpr>(Init) << VDecl << Type << Range;
    return QualType();
  }

  QualType DeducedType;
  AutoDeductionResult Result =
      DeduceAutoType(TSI->getTypeLoc(), DeduceInit, DeducedType);
  if (Result != ADK_Success && Result != ADK_AlreadyDiagnosed)
    DiagnoseAutoDeductionFailure(VDecl, DeduceInit);

  return DeducedType;
}

bool Sema::DeduceVariableDeclarationType(VarDecl *VDecl, bool DirectInit,
                                         Expr *Init) {
  assert(!Init || !Init->containsErrors());
  QualType DeducedType = deduceVarTypeFromInitializer(
      VDecl, VDecl->getType(), VDecl->getTypeSourceInfo(),
      VDecl->getSourceRange(), DirectInit, Init);
  if (DeducedType.isNull()) {
    VDecl->setInvalidDecl();
    return true;
  }

  VDecl->setType(DeducedType);
  assert(VDecl->isLinkageValid());

  // If this is a redeclaration, check that the type we just deduced matches
  // the previously declared type.
  if (VarDecl *Old = VDecl->getPreviousDecl()) {
    // We never need to merge the type, because we cannot form an incomplete
    // array of auto, nor deduce such a type.
    MergeVarDeclTypes(VDecl, Old, /*MergeTypeWithPrevious*/ false);
  }

  CheckVariableDeclarationType(VDecl);
  return VDecl->isInvalidDecl();
}

namespace {

bool shouldIgnoreForRecordTriviality(const FieldDecl *FD) {
  return FD->hasAttr<UnavailableAttr>();
}

} // namespace

void Sema::AttachInitializerToDecl(Decl *RealDecl, Expr *Init,
                                   bool DirectInit) {
  // If there is no declaration, there was an error parsing it.  Just ignore
  // the initializer.
  if (!RealDecl || RealDecl->isInvalidDecl())
    return;

  VarDecl *VDecl = dyn_cast<VarDecl>(RealDecl);
  if (!VDecl) {
    assert(!isa<FieldDecl>(RealDecl) && "field init shouldn't get here");
    Diag(RealDecl->getLocation(), diag::err_illegal_initializer);
    RealDecl->setInvalidDecl();
    return;
  }

  // Deduce `auto` from the initializer.
  if (VDecl->getType()->isUndeducedType()) {
    if (Init->containsErrors()) {
      RealDecl->setInvalidDecl();
      VDecl->setInit(Init);
      return;
    }

    if (DeduceVariableDeclarationType(VDecl, DirectInit, Init))
      return;
  }

  // dllimport cannot be used on variable definitions.
  if (VDecl->hasAttr<DLLImportAttr>()) {
    Diag(VDecl->getLocation(), diag::err_attribute_dllimport_data_definition);
    VDecl->setInvalidDecl();
    return;
  }

  // C11 6.7.9p4: block scope + external linkage => no initializer here.
  if (VDecl->isLocalVarDecl() && VDecl->hasExternalStorage()) {
    Diag(VDecl->getLocation(), diag::err_block_extern_cant_init);
    VDecl->setInvalidDecl();
    return;
  }

  // A definition must end up with a complete type, which means it must be
  // complete with the restriction that an array type might be completed by
  // the initializer; note that later code assumes this restriction.
  {
    QualType BaseDeclType = VDecl->getType();
    if (const ArrayType *Array = Context.getAsIncompleteArrayType(BaseDeclType))
      BaseDeclType = Array->getElementType();
    if (RequireCompleteType(VDecl->getLocation(), BaseDeclType,
                            diag::err_typecheck_decl_incomplete_type)) {
      RealDecl->setInvalidDecl();
      return;
    }
  }

  // If adding the initializer will turn this declaration into a definition,
  // and we already have a definition for this variable, diagnose or otherwise
  // handle the situation.
  if (VarDecl *Def = VDecl->getDefinition())
    if (Def != VDecl && !VDecl->isThisDeclarationADemotedDefinition() &&
        checkVarDeclRedefinition(Def, VDecl))
      return;

  // The LoaderUninitialized attribute acts as a definition (of undef).
  if (VDecl->hasAttr<LoaderUninitializedAttr>()) {
    Diag(VDecl->getLocation(), diag::err_loader_uninitialized_cant_init);
    VDecl->setInvalidDecl();
    return;
  }

  // Get the decls type and save a reference for later, since
  // CheckInitializerTypes may change it.
  QualType DclT = VDecl->getType(), SavT = DclT;

  ParenListExpr *DirectInitList = dyn_cast<ParenListExpr>(Init);
  if (!VDecl->isInvalidDecl()) {
    InitializedEntity Entity = InitializedEntity::InitializeVariable(VDecl);
    InitializationKind Kind = InitializationKind::CreateForInit(
        VDecl->getLocation(), DirectInit, Init);

    MultiExprArg Args = Init;
    if (DirectInitList)
      Args = MultiExprArg(DirectInitList->getExprs(),
                          DirectInitList->getNumExprs());

    if (VDecl->isInvalidDecl())
      return;

    InitializationSequence InitSeq(*this, Entity, Kind, Args,
                                   /*TopLevelOfInitList=*/false,
                                   /*TreatUnavailableAsInvalid=*/false);
    ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Args, &DclT);
    if (Result.isInvalid()) {
      // If the provided initializer fails to initialize the var decl,
      // we attach a recovery expr for better recovery.
      auto RecoveryExpr =
          CreateRecoveryExpr(Init->getBeginLoc(), Init->getEndLoc(), Args);
      if (RecoveryExpr.get())
        VDecl->setInit(RecoveryExpr.get());
      return;
    }

    Init = Result.getAs<Expr>();
    QualType VDeclType = VDecl->getType();
    if (Init && !Init->getType().isNull() &&
        Context.getAsIncompleteArrayType(VDeclType) &&
        Context.getAsIncompleteArrayType(Init->getType())) {
      // Bail out if it is not possible to deduce array size from the
      // initializer.
      Diag(VDecl->getLocation(), diag::err_typecheck_decl_incomplete_type)
          << VDeclType;
      VDecl->setInvalidDecl();
      return;
    }
  }

  // If the type changed, it means we had an incomplete type that was
  // completed by the initializer. For example:
  //   int ary[] = { 1, 3, 5 };
  // "ary" transitions from an IncompleteArrayType to a ConstantArrayType.
  if (!VDecl->isInvalidDecl() && (DclT != SavT))
    VDecl->setType(DclT);

  // The initialization is usually a full-expression.
  //
  // If this is a braced initialization of an aggregate, it is not
  // an expression, and each individual field initializer is a separate
  // full-expression. For instance, in:
  //
  //   struct Temp { ~Temp(); };
  //   struct S { S(Temp); };
  //   struct T { S a, b; } t = { Temp(), Temp() }
  //
  // we should destroy the first Temp before constructing the second.
  ExprResult Result =
      OnFinishFullExpr(Init, VDecl->getLocation(),
                       /*DiscardedValue*/ false, VDecl->isConstexpr());
  if (Result.isInvalid()) {
    VDecl->setInvalidDecl();
    return;
  }
  Init = Result.get();

  // Attach the initializer to the decl.
  VDecl->setInit(Init);

  if (getLangOpts().BuiltinString && VDecl->hasLocalStorage())
    attachNeverCWptrCleanup(*this, CurScope, VDecl);

  if (VDecl->isLocalVarDecl()) {
    // Don't check the initializer if the declaration is malformed.
    if (VDecl->isInvalidDecl()) {
      // do nothing

      // C99 6.7.8p4: All the expressions in an initializer for an object that
      // has static storage duration shall be constant expressions or string
      // literals.
    } else if (VDecl->getStorageClass() == SC_Static) {
      CheckForConstantInitializer(Init, DclT);

      // C89 is stricter than C99 for aggregate initializers.
      // C89 6.5.7p3: All the expressions [...] in an initializer list
      // for an object that has aggregate or union type shall be
      // constant expressions.
    } else if (!getLangOpts().C99 && VDecl->getType()->isAggregateType() &&
               isa<InitListExpr>(Init)) {
      const Expr *Culprit;
      if (!Init->isConstantInitializer(Context, false, &Culprit)) {
        Diag(Culprit->getExprLoc(), diag::ext_aggregate_init_not_constant)
            << Culprit->getSourceRange();
      }
    }

  } else if (VDecl->isFileVarDecl()) {
    // extern with an initializer is a definition; GCC and NeverC warn.
    // Suppress for selectany on const-qualified types (MSVC compatibility).
    if (VDecl->getStorageClass() == SC_Extern &&
        (!VDecl->hasAttr<SelectAnyAttr>() ||
         !Context.getBaseElementType(VDecl->getType()).isConstQualified()))
      Diag(VDecl->getLocation(), diag::warn_extern_init);

    if (!VDecl->isInvalidDecl())
      CheckForConstantInitializer(Init, DclT);
  }

  // Direct- and copy-initialization are represented similarly for scalars
  // (e.g. int x(1) vs int x = 1). VarDecl::getInitStyle() records the source
  // form.
  if (DirectInitList) {
    assert(DirectInit && "Call-style initializer must be direct init.");
    VDecl->setInitStyle(VarDecl::CallInit);
  } else if (DirectInit) {
    // This must be list-initialization. No other way is direct-initialization.
    VDecl->setInitStyle(VarDecl::ListInit);
  }

  CheckCompleteVariableDeclaration(VDecl);
}

void Sema::OnInitializerError(Decl *D) {
  // Our main concern here is re-establishing the invariant that a variable's
  // type is complete where required.
  if (!D || D->isInvalidDecl())
    return;

  VarDecl *VD = dyn_cast<VarDecl>(D);
  if (!VD)
    return;

  // Auto types are meaningless if we can't make sense of the initializer.
  if (VD->getType()->isUndeducedType()) {
    D->setInvalidDecl();
    return;
  }

  QualType Ty = VD->getType();

  // Require a complete type.
  if (RequireCompleteType(VD->getLocation(), Context.getBaseElementType(Ty),
                          diag::err_typecheck_decl_incomplete_type)) {
    VD->setInvalidDecl();
    return;
  }
}

void Sema::OnUninitializedDecl(Decl *RealDecl) {
  // If there is no declaration, there was an error parsing it. Just ignore it.
  if (!RealDecl)
    return;

  if (VarDecl *Var = dyn_cast<VarDecl>(RealDecl)) {
    QualType Type = Var->getType();

    if (Type->isUndeducedType() &&
        DeduceVariableDeclarationType(Var, false, nullptr))
      return;

    if (Var->isConstexpr() && !Var->isThisDeclarationADefinition() &&
        !Var->isThisDeclarationADemotedDefinition()) {
      Diag(Var->getLocation(), diag::err_invalid_constexpr_var_decl);
      Var->setInvalidDecl();
      return;
    }

    if (!Var->isInvalidDecl() && RealDecl->hasAttr<LoaderUninitializedAttr>()) {
      if (Var->getStorageClass() == SC_Extern) {
        Diag(Var->getLocation(), diag::err_loader_uninitialized_extern_decl)
            << Var;
        Var->setInvalidDecl();
        return;
      }
      if (RequireCompleteType(Var->getLocation(), Var->getType(),
                              diag::err_typecheck_decl_incomplete_type)) {
        Var->setInvalidDecl();
        return;
      }
      return;
    }

    VarDecl::DefinitionKind DefKind = Var->isThisDeclarationADefinition();

    switch (DefKind) {
    case VarDecl::Definition:
      break;

    case VarDecl::DeclarationOnly:
      // It's only a declaration.

      // Block scope. C99 6.7p7: If an identifier for an object is
      // declared with no linkage (C99 6.2.2p6), the type for the
      // object shall be complete.
      if (Var->isLocalVarDecl() && !Var->hasLinkage() &&
          !Var->isInvalidDecl() &&
          RequireCompleteType(Var->getLocation(), Type,
                              diag::err_typecheck_decl_incomplete_type))
        Var->setInvalidDecl();

      if (!Var->isInvalidDecl() && Var->getStorageClass() == SC_PrivateExtern) {
        Diag(Var->getLocation(), diag::warn_private_extern);
        Diag(Var->getLocation(), diag::note_private_extern);
      }

      if (Context.getTargetInfo().allowDebugInfoForExternalRef() &&
          !Var->isInvalidDecl())
        ExternalDeclarations.push_back(Var);

      return;

    case VarDecl::TentativeDefinition:
      // File scope. C99 6.9.2p2: A declaration of an identifier for an
      // object that has file scope without an initializer, and without a
      // storage-class specifier or with the storage-class specifier "static",
      // constitutes a tentative definition. Note: A tentative definition with
      // external linkage is valid (C99 6.2.2p5).
      if (!Var->isInvalidDecl()) {
        if (const IncompleteArrayType *ArrayT =
                Context.getAsIncompleteArrayType(Type)) {
          if (RequireCompleteSizedType(
                  Var->getLocation(), ArrayT->getElementType(),
                  diag::err_array_incomplete_or_sizeless_type))
            Var->setInvalidDecl();
        } else if (Var->getStorageClass() == SC_Static) {
          // C99 6.9.2p3: If the declaration of an identifier for an object is
          // a tentative definition and has internal linkage (C99 6.2.2p3), the
          // declared type shall not be an incomplete type.
          // NOTE: code such as the following
          //     static struct s;
          //     struct s { int a; };
          // is accepted by gcc. Hence here we issue a warning instead of
          // an error and we do not invalidate the static declaration.
          // NOTE: to avoid multiple warnings, only check the first declaration.
          if (Var->isFirstDecl())
            RequireCompleteType(Var->getLocation(), Type,
                                diag::ext_typecheck_decl_incomplete_type);
        }
      }

      // Record the tentative definition; we're done.
      if (!Var->isInvalidDecl())
        TentativeDefinitions.push_back(Var);
      return;
    }

    // Provide a specific diagnostic for uninitialized variable
    // definitions with incomplete array type.
    if (Type->isIncompleteArrayType()) {
      if (Var->isConstexpr())
        Diag(Var->getLocation(), diag::err_constexpr_var_requires_const_init)
            << Var;
      else
        Diag(Var->getLocation(),
             diag::err_typecheck_incomplete_array_needs_initializer);
      Var->setInvalidDecl();
      return;
    }

    if (Var->isInvalidDecl())
      return;

    if (!Var->hasAttr<AliasAttr>()) {
      if (RequireCompleteType(Var->getLocation(),
                              Context.getBaseElementType(Type),
                              diag::err_typecheck_decl_incomplete_type)) {
        Var->setInvalidDecl();
        return;
      }
    } else {
      return;
    }

    if (Var->hasLocalStorage() && this->isNeverCStringType(Type) &&
        !Var->hasInit())
      Var->setInit(new (Context) ImplicitValueInitExpr(Type));
  }
}

void Sema::CheckCompleteVariableDeclaration(VarDecl *var) {
  if (var->isInvalidDecl())
    return;

  if (var->hasLocalStorage() &&
      var->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
    setFunctionHasBranchProtectedScope();

  if (var->getTLSKind() == VarDecl::TLS_Static) {
    if (var->getType().isDestructedType()) {
      // GNU `__thread`: type must be trivially destructible.
      Diag(var->getLocation(), diag::err_thread_nontrivial_dtor);
    }
  }

  if (!var->getType()->isStructureType() && var->hasInit() &&
      isa<InitListExpr>(var->getInit())) {
    const auto *ILE = cast<InitListExpr>(var->getInit());
    unsigned NumInits = ILE->getNumInits();
    if (NumInits > 2)
      for (unsigned I = 0; I < NumInits; ++I) {
        const auto *Init = ILE->getInit(I);
        if (!Init)
          break;
        const auto *SL = dyn_cast<StringLiteral>(Init->IgnoreImpCasts());
        if (!SL)
          break;

        unsigned NumConcat = SL->getNumConcatenated();
        // Diagnose missing comma in string array initialization.
        // Do not warn when all the elements in the initializer are concatenated
        // together. Do not warn for macros too.
        if (NumConcat == 2 && !SL->getBeginLoc().isMacroID()) {
          bool OnlyOneMissingComma = true;
          for (unsigned J = I + 1; J < NumInits; ++J) {
            const auto *Init = ILE->getInit(J);
            if (!Init)
              break;
            const auto *SLJ = dyn_cast<StringLiteral>(Init->IgnoreImpCasts());
            if (!SLJ || SLJ->getNumConcatenated() > 1) {
              OnlyOneMissingComma = false;
              break;
            }
          }

          if (OnlyOneMissingComma) {
            llvm::SmallVector<FixItHint, 1> Hints;
            for (unsigned i = 0; i < NumConcat - 1; ++i)
              Hints.push_back(FixItHint::CreateInsertion(
                  PP.getLocForEndOfToken(SL->getStrTokenLoc(i)), ","));

            Diag(SL->getStrTokenLoc(1),
                 diag::warn_concatenated_literal_array_init)
                << Hints;
            Diag(SL->getBeginLoc(),
                 diag::note_concatenated_string_literal_silence);
          }
          // In any case, stop now.
          break;
        }
      }
  }

  bool GlobalStorage = var->hasGlobalStorage();
  bool HasConstInit = true;

  // Apply section attributes and pragmas to global variables.
  if (GlobalStorage && var->isThisDeclarationADefinition()) {
    PragmaStack<StringLiteral *> *Stack = nullptr;
    int SectionFlags = TreeContext::PSF_Read;
    bool MSVCEnv =
        Context.getTargetInfo().getTriple().isWindowsMSVCEnvironment();
    std::optional<QualType::NonConstantStorageReason> Reason;
    if (HasConstInit &&
        !(Reason = var->getType().isNonConstantStorage(Context, true, false))) {
      Stack = &ConstSegStack;
    } else {
      SectionFlags |= TreeContext::PSF_Write;
      Stack = var->hasInit() && HasConstInit ? &DataSegStack : &BSSSegStack;
    }
    if (const SectionAttr *SA = var->getAttr<SectionAttr>()) {
      if (SA->getSyntax() == AttributeCommonInfo::AS_Declspec)
        SectionFlags |= TreeContext::PSF_Implicit;
      UnifySection(SA->getName(), SectionFlags, var);
    } else if (Stack->CurrentValue) {
      if (Stack != &ConstSegStack && MSVCEnv &&
          ConstSegStack.CurrentValue != ConstSegStack.DefaultValue &&
          var->getType().isConstQualified()) {
        assert((!Reason ||
                Reason != QualType::NonConstantStorageReason::NonConstType) &&
               "This case should've already been handled elsewhere");
        Diag(var->getLocation(), diag::warn_section_msvc_compat)
            << var << ConstSegStack.CurrentValue;
      }
      SectionFlags |= TreeContext::PSF_Implicit;
      auto SectionName = Stack->CurrentValue->getString();
      var->addAttr(SectionAttr::CreateImplicit(Context, SectionName,
                                               Stack->CurrentPragmaLocation,
                                               SectionAttr::Declspec_allocate));
      if (UnifySection(SectionName, SectionFlags, var))
        var->dropAttr<SectionAttr>();
      var->DeclType.addVolatile();
    }

    // Apply the init_seg attribute if this has an initializer.  If the
    // initializer turns out to not be dynamic, we'll end up ignoring this
    // attribute.
    if (CurInitSeg && var->getInit())
      var->addAttr(InitSegAttr::CreateImplicit(Context, CurInitSeg->getString(),
                                               CurInitSegLoc));
  }
}

void Sema::CheckStaticLocalForDllExport(VarDecl *VD) {
  assert(VD->isStaticLocal());

  auto *FD = dyn_cast_or_null<FunctionDecl>(VD->getParentFunctionOrMethod());
  if (!FD)
    return;

  if (Attr *A = getDLLAttr(FD)) {
    auto *NewAttr = cast<InheritableAttr>(A->clone(getTreeContext()));
    NewAttr->setInherited(true);
    VD->addAttr(NewAttr);
  }
}

void Sema::CheckThreadLocalForLargeAlignment(VarDecl *VD) {
  assert(VD->getTLSKind());

  // Perform TLS alignment check here after attributes attached to the variable
  // which may affect the alignment have been processed. Only perform the check
  // if the target has a maximum TLS alignment (zero means no constraints).
  if (unsigned MaxAlign = Context.getTargetInfo().getMaxTLSAlign()) {
    // Protect the check so that it's not performed on dependent types and
    // dependent alignments (we can't determine the alignment in that case).
    if (!VD->hasDependentAlignment()) {
      CharUnits MaxAlignChars = Context.toCharUnitsFromBits(MaxAlign);
      if (Context.getDeclAlign(VD) > MaxAlignChars) {
        Diag(VD->getLocation(), diag::err_tls_var_aligned_over_maximum)
            << (unsigned)Context.getDeclAlign(VD).getQuantity() << VD
            << (unsigned)MaxAlignChars.getQuantity();
      }
    }
  }
}

// ===----------------------------------------------------------------------===
// Declaration finalization & parameters
// ===----------------------------------------------------------------------===

void Sema::FinalizeDeclaration(Decl *ThisDecl) {
  // Note that we are no longer parsing the initializer for this declaration.
  ParsingInitForAutoVars.erase(ThisDecl);

  VarDecl *VD = dyn_cast_or_null<VarDecl>(ThisDecl);
  if (!VD)
    return;

  if (LLVM_LIKELY(VD->hasLocalStorage() && !VD->hasAttrs()))
    return;

  // Apply an implicit SectionAttr if '#pragma neverc section bss|data|rodata'
  // is active
  if (VD->hasGlobalStorage() && VD->isThisDeclarationADefinition() &&
      !VD->hasAttr<SectionAttr>()) {
    if (PragmaBSSSection.Valid)
      VD->addAttr(PragmaNeverCBSSSectionAttr::CreateImplicit(
          Context, PragmaBSSSection.SectionName,
          PragmaBSSSection.PragmaLocation));
    if (PragmaDataSection.Valid)
      VD->addAttr(PragmaNeverCDataSectionAttr::CreateImplicit(
          Context, PragmaDataSection.SectionName,
          PragmaDataSection.PragmaLocation));
    if (PragmaRodataSection.Valid)
      VD->addAttr(PragmaNeverCRodataSectionAttr::CreateImplicit(
          Context, PragmaRodataSection.SectionName,
          PragmaRodataSection.PragmaLocation));
    if (PragmaRelroSection.Valid)
      VD->addAttr(PragmaNeverCRelroSectionAttr::CreateImplicit(
          Context, PragmaRelroSection.SectionName,
          PragmaRelroSection.PragmaLocation));
  }

  checkAttributesAfterMerging(*this, *VD);

  if (VD->isStaticLocal())
    CheckStaticLocalForDllExport(VD);

  if (VD->getTLSKind())
    CheckThreadLocalForLargeAlignment(VD);

  // Grab the dllimport or dllexport attribute off of the VarDecl.
  const InheritableAttr *DLLAttr = getDLLAttr(VD);

  // dllimport/dllexport variables cannot be thread local, their TLS index
  // isn't exported with the variable.
  if (DLLAttr && VD->getTLSKind()) {
    auto *F = dyn_cast_or_null<FunctionDecl>(VD->getParentFunctionOrMethod());
    if (F && getDLLAttr(F)) {
      assert(VD->isStaticLocal());
      // But if this is a static local in a dlimport/dllexport function, the
      // function will never be inlined, which means the var would never be
      // imported, so having it marked import/export is safe.
    } else {
      Diag(VD->getLocation(), diag::err_attribute_dll_thread_local)
          << VD << DLLAttr;
      VD->setInvalidDecl();
    }
  }

  if (UsedAttr *Attr = VD->getAttr<UsedAttr>()) {
    if (!Attr->isInherited() && !VD->isThisDeclarationADefinition()) {
      Diag(Attr->getLocation(), diag::warn_attribute_ignored_on_non_definition)
          << Attr;
      VD->dropAttr<UsedAttr>();
    }
  }
  if (RetainAttr *Attr = VD->getAttr<RetainAttr>()) {
    if (!Attr->isInherited() && !VD->isThisDeclarationADefinition()) {
      Diag(Attr->getLocation(), diag::warn_attribute_ignored_on_non_definition)
          << Attr;
      VD->dropAttr<RetainAttr>();
    }
  }

  const DeclContext *DC = VD->getDeclContext();
  // If there's a #pragma GCC visibility in scope, and this isn't a
  // struct/union member, set the visibility of this variable.
  if (DC->getRedeclContext()->isFileContext() && VD->isExternallyVisible())
    AddPushedVisibilityAttribute(VD);

  if (VD->isFileVarDecl())
    MarkUnusedFileScopedDecl(VD);

  // Now we have parsed the initializer and can update the table of magic
  // tag values.
  if (!VD->hasAttr<TypeTagForDatatypeAttr>() ||
      !VD->getType()->isIntegralOrEnumerationType())
    return;

  for (const auto *I : ThisDecl->specific_attrs<TypeTagForDatatypeAttr>()) {
    const Expr *MagicValueExpr = VD->getInit();
    if (!MagicValueExpr) {
      continue;
    }
    std::optional<llvm::APSInt> MagicValueInt;
    if (!(MagicValueInt = MagicValueExpr->getIntegerConstantExpr(Context))) {
      Diag(I->getRange().getBegin(), diag::err_type_tag_for_datatype_not_ice)
          << 0 << MagicValueExpr->getSourceRange();
      continue;
    }
    if (MagicValueInt->getActiveBits() > 64) {
      Diag(I->getRange().getBegin(), diag::err_type_tag_for_datatype_too_large)
          << 0 << MagicValueExpr->getSourceRange();
      continue;
    }
    uint64_t MagicValue = MagicValueInt->getZExtValue();
    RegisterTypeTagForDatatype(I->getArgumentKind(), MagicValue,
                               I->getMatchingCType(), I->getLayoutCompatible(),
                               I->getMustBeNull());
  }
}

namespace {
bool hasDeducedAuto(DeclaratorDecl *DD) { return isa<VarDecl>(DD); }
} // namespace

Sema::DeclGroupPtrTy
Sema::FinalizeDeclaratorGroup(Scope *S, const DeclSpec &DS,
                              llvm::ArrayRef<Decl *> Group) {
  llvm::SmallVector<Decl *, 8> Decls;

  if (DS.isTypeSpecOwned())
    Decls.push_back(DS.getRepAsDecl());

  DeclaratorDecl *FirstDeclaratorInGroup = nullptr;
  DeclaratorDecl *FirstNonDeducedAutoInGroup = nullptr;
  bool DiagnosedNonDeducedAuto = false;

  for (unsigned i = 0, e = Group.size(); i != e; ++i) {
    if (Decl *D = Group[i]) {
      if (auto *DD = dyn_cast<DeclaratorDecl>(D)) {
        if (!FirstDeclaratorInGroup)
          FirstDeclaratorInGroup = DD;
        if (!FirstNonDeducedAutoInGroup && DS.hasAutoTypeSpec() &&
            !hasDeducedAuto(DD))
          FirstNonDeducedAutoInGroup = DD;

        if (FirstDeclaratorInGroup != DD) {
          if (FirstNonDeducedAutoInGroup && !DiagnosedNonDeducedAuto) {
            Diag(FirstNonDeducedAutoInGroup->getLocation(),
                 diag::err_auto_non_deduced_not_alone)
                << FirstDeclaratorInGroup->getSourceRange()
                << DD->getSourceRange();
            DiagnosedNonDeducedAuto = true;
          }
        }
      }

      Decls.push_back(D);
    }
  }

  return FormDeclaratorGroup(Decls);
}

Sema::DeclGroupPtrTy
Sema::FormDeclaratorGroup(llvm::MutableArrayRef<Decl *> Group) {
  // Multi-declarator `auto` group: all deductions must yield the same type.
  if (Group.size() > 1) {
    QualType Deduced;
    VarDecl *DeducedDecl = nullptr;
    for (unsigned i = 0, e = Group.size(); i != e; ++i) {
      VarDecl *D = dyn_cast<VarDecl>(Group[i]);
      if (!D || D->isInvalidDecl())
        break;
      DeducedType *DT = D->getType()->getContainedDeducedType();
      if (!DT || DT->getDeducedType().isNull())
        continue;
      if (Deduced.isNull()) {
        Deduced = DT->getDeducedType();
        DeducedDecl = D;
      } else if (!Context.hasSameType(DT->getDeducedType(), Deduced)) {
        auto *AT = dyn_cast<AutoType>(DT);
        auto Dia = Diag(D->getTypeSourceInfo()->getTypeLoc().getBeginLoc(),
                        diag::err_auto_different_deductions)
                   << (AT && AT->isGNUAutoType() ? 1 : 0) << Deduced
                   << DeducedDecl->getDeclName() << DT->getDeducedType()
                   << D->getDeclName();
        if (DeducedDecl->hasInit())
          Dia << DeducedDecl->getInit()->getSourceRange();
        if (D->getInit())
          Dia << D->getInit()->getSourceRange();
        D->setInvalidDecl();
        break;
      }
    }
  }

  return DeclGroupPtrTy::make(
      DeclGroupRef::Create(Context, Group.data(), Group.size()));
}

Decl *Sema::OnParamDeclarator(Scope *S, Declarator &D) {
  const DeclSpec &DS = D.getDeclSpec();

  // C99 6.7.5.3p2: the only storage-class specifier for parameters is
  // `register`.
  StorageClass SC = SC_None;
  if (DS.getStorageClassSpec() == DeclSpec::SCS_register) {
    SC = SC_Register;
  } else if (DS.getStorageClassSpec() != DeclSpec::SCS_unspecified) {
    Diag(DS.getStorageClassSpecLoc(),
         diag::err_invalid_storage_class_in_func_decl);
    D.getMutableDeclSpec().ClearStorageClassSpecs();
  }

  if (DeclSpec::TSCS TSCS = DS.getThreadStorageClassSpec())
    Diag(DS.getThreadStorageClassSpecLoc(), diag::err_invalid_thread)
        << DeclSpec::getSpecifierName(TSCS);
  if (DS.isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_non_function) << false;
  if (DS.hasConstexprSpecifier())
    Diag(DS.getConstexprSpecLoc(), diag::err_invalid_constexpr)
        << 0 << static_cast<int>(DS.getConstexprSpecifier());

  if (DS.isNoreturnSpecified())
    Diag(DS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  TypeSourceInfo *TInfo = ResolveDeclaratorType(D, S);
  QualType parmDeclType = TInfo->getType();

  // Check for redeclaration of parameters, e.g. int foo(int x, int x);
  IdentifierInfo *II = D.getIdentifier();
  if (II) {
    LookupResult R(*this, II, D.getIdentifierLoc(), ResolveOrdinary,
                   ForVisibleRedeclaration);
    ResolveName(R, S);
    if (!R.empty()) {
      NamedDecl *PrevDecl = *R.begin();
      if (PrevDecl && S->isDeclScope(PrevDecl)) {
        Diag(D.getIdentifierLoc(), diag::err_param_redefinition) << II;
        Diag(PrevDecl->getLocation(), diag::note_previous_declaration);
        // Recover by removing the name
        II = nullptr;
        D.SetIdentifier(nullptr, D.getIdentifierLoc());
        D.setInvalidType(true);
      }
    }
  }

  // Temporarily put parameter variables in the translation unit, not
  // the enclosing declaration context.
  ParmVarDecl *New =
      CheckParameter(Context.getTranslationUnitDecl(), D.getBeginLoc(),
                     D.getIdentifierLoc(), II, parmDeclType, TInfo, SC);

  if (D.isInvalidType())
    New->setInvalidDecl();

  assert(S->isFunctionPrototypeScope());
  assert(S->getFunctionPrototypeDepth() >= 1);
  New->setScopeInfo(S->getFunctionPrototypeDepth() - 1,
                    S->getNextFunctionPrototypeIndex());

  S->AddDecl(New);
  if (II)
    IdResolver.AddDecl(New);

  ApplyDeclAttributes(S, New, D);

  return New;
}

ParmVarDecl *Sema::FormParmVarDeclForTypedef(DeclContext *DC,
                                             SourceLocation Loc, QualType T) {
  /* Setting StartLoc == Loc.
     Would it be worth to modify callers so as to provide proper source
     location for the unnamed parameters, embedding the parameter's type? */
  ParmVarDecl *Param = ParmVarDecl::Create(
      Context, DC, Loc, Loc, nullptr, T,
      Context.getTrivialTypeSourceInfo(T, Loc), SC_None, nullptr);
  Param->setImplicit();
  return Param;
}

void Sema::DiagnoseUnusedParameters(llvm::ArrayRef<ParmVarDecl *> Parameters) {
  for (const ParmVarDecl *Parameter : Parameters) {
    if (!Parameter->isReferenced() && Parameter->getDeclName() &&
        !Parameter->hasAttr<UnusedAttr>() &&
        !Parameter->getIdentifier()->isPlaceholder()) {
      Diag(Parameter->getLocation(), diag::warn_unused_parameter)
          << Parameter->getDeclName();
    }
  }
}

void Sema::DiagnoseSizeOfParametersAndReturnValue(
    llvm::ArrayRef<ParmVarDecl *> Parameters, QualType ReturnTy, NamedDecl *D) {
  if (LangOpts.NumLargeByValueCopy == 0) // No check.
    return;

  // Warn if the return value is pass-by-value and larger than the specified
  // threshold.
  if (ReturnTy.isPODType(Context)) {
    unsigned Size = Context.getTypeSizeInChars(ReturnTy).getQuantity();
    if (Size > LangOpts.NumLargeByValueCopy)
      Diag(D->getLocation(), diag::warn_return_value_size) << D << Size;
  }

  // Warn if any parameter is pass-by-value and larger than the specified
  // threshold.
  for (const ParmVarDecl *Parameter : Parameters) {
    QualType T = Parameter->getType();
    if (!T.isPODType(Context))
      continue;
    unsigned Size = Context.getTypeSizeInChars(T).getQuantity();
    if (Size > LangOpts.NumLargeByValueCopy)
      Diag(Parameter->getLocation(), diag::warn_parameter_size)
          << Parameter << Size;
  }
}

ParmVarDecl *Sema::CheckParameter(DeclContext *DC, SourceLocation StartLoc,
                                  SourceLocation NameLoc, IdentifierInfo *Name,
                                  QualType T, TypeSourceInfo *TSInfo,
                                  StorageClass SC) {
  ParmVarDecl *New = ParmVarDecl::Create(Context, DC, StartLoc, NameLoc, Name,
                                         Context.getAdjustedParameterType(T),
                                         TSInfo, SC, nullptr);

  // ISO/IEC TR 18037 S6.7.3: "The type of an object with automatic storage
  // duration shall not be qualified by an address-space qualifier."
  // Since all parameters have automatic store duration, they can not have
  // an address space.
  if (T.getAddressSpace() != LangAS::Default) {
    Diag(NameLoc, diag::err_arg_with_address_space);
    New->setInvalidDecl();
  }

  return New;
}

void Sema::OnFinishKNRParamDeclarations(Scope *S, Declarator &D,
                                        SourceLocation LocAfterDecls) {
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();

  // C99 6.9.1p6 "If a declarator includes an identifier list, each declaration
  // in the declaration list shall have at least one declarator, those
  // declarators shall only declare identifiers from the identifier list, and
  // every identifier in the identifier list shall be declared.
  //
  // C89 3.7.1p5 "If a declarator includes an identifier list, only the
  // identifiers it names shall be declared in the declaration list."
  //
  // This is why we only diagnose in C99 and later. Note, the other conditions
  // listed are checked elsewhere.
  if (!FTI.hasPrototype) {
    for (int i = FTI.NumParams; i != 0; /* decrement in loop */) {
      --i;
      if (FTI.Params[i].Param == nullptr) {
        if (getLangOpts().C99) {
          llvm::SmallString<256> Code;
          llvm::raw_svector_ostream(Code)
              << "  int " << FTI.Params[i].Ident->getName() << ";\n";
          Diag(FTI.Params[i].IdentLoc, diag::ext_param_not_declared)
              << FTI.Params[i].Ident
              << FixItHint::CreateInsertion(LocAfterDecls, Code);
        }

        // Implicitly declare the argument as type 'int' for lack of a better
        // type.
        AttributeFactory attrs;
        DeclSpec DS(attrs);
        const char *PrevSpec; // unused
        unsigned DiagID;      // unused
        DS.SetTypeSpecType(DeclSpec::TST_int, FTI.Params[i].IdentLoc, PrevSpec,
                           DiagID, Context.getPrintingPolicy());
        // Use the identifier location for the type source range.
        DS.SetRangeStart(FTI.Params[i].IdentLoc);
        DS.SetRangeEnd(FTI.Params[i].IdentLoc);
        Declarator ParamD(DS, ParsedAttributesView::none(),
                          DeclaratorContext::KNRTypeList);
        ParamD.SetIdentifier(FTI.Params[i].Ident, FTI.Params[i].IdentLoc);
        FTI.Params[i].Param = OnParamDeclarator(S, ParamD);
      }
    }
  }
}

// ===----------------------------------------------------------------------===
// Function definition lifecycle
// ===----------------------------------------------------------------------===

Decl *Sema::OnStartOfFunctionDef(Scope *FnBodyScope, Declarator &D,
                                 SkipBodyInfo *SkipBody) {
  assert(getCurFunctionDecl() == nullptr && "Function parsing confused");
  assert(D.isFunctionDeclarator() && "Not a function declarator!");
  Scope *ParentScope = FnBodyScope->getParent();

  D.setFunctionDefinitionKind(FunctionDefinitionKind::Definition);
  Decl *DP = OnDeclarator(ParentScope, D);
  return OnStartOfFunctionDef(FnBodyScope, DP, SkipBody);
}

namespace {
bool findPossiblePrototype(const FunctionDecl *FD,
                           const FunctionDecl *&PossiblePrototype) {
  for (const FunctionDecl *Prev = FD->getPreviousDecl(); Prev;
       Prev = Prev->getPreviousDecl()) {
    // Ignore any declarations that occur in function scope, because they
    // aren't visible from the header.
    if (Prev->getLexicalDeclContext()->isFunctionOrMethod())
      continue;

    PossiblePrototype = Prev;
    return Prev->getType()->isFunctionProtoType();
  }
  return false;
}
} // namespace

namespace {
bool shouldWarnAboutMissingPrototype(const FunctionDecl *FD,
                                     const FunctionDecl *&PossiblePrototype) {
  // Don't warn about invalid declarations.
  if (FD->isInvalidDecl())
    return false;

  // Or declarations that aren't global.
  if (!FD->isGlobal())
    return false;

  // Don't warn about 'main'.
  if (isa<TranslationUnitDecl>(FD->getDeclContext()->getRedeclContext()))
    if (IdentifierInfo *II = FD->getIdentifier())
      if (II->isStr("main") || II->isStr("efi_main"))
        return false;

  // Don't warn about inline functions.
  if (FD->isInlined())
    return false;

  // Don't warn on implicitly local functions (such as having local-typed
  // parameters).
  if (!FD->isExternallyVisible())
    return false;

  // If we were able to find a potential prototype, don't warn.
  if (findPossiblePrototype(FD, PossiblePrototype))
    return false;

  return true;
}
} // namespace

void Sema::CheckForFunctionRedefinition(FunctionDecl *FD,
                                        const FunctionDecl *EffectiveDefinition,
                                        SkipBodyInfo *SkipBody) {
  const FunctionDecl *Definition = EffectiveDefinition;
  if (!Definition && !FD->isDefined(Definition))
    return;

  if (canRedefineFunction(Definition, getLangOpts()))
    return;

  // Don't emit an error when this is redefinition of a typo-corrected
  if (TypoCorrectedFunctionDefinitions.contains(Definition))
    return;

  if (getLangOpts().GNUMode && Definition->isInlineSpecified() &&
      Definition->getStorageClass() == SC_Extern)
    Diag(FD->getLocation(), diag::err_redefinition_extern_inline) << FD;
  else
    Diag(FD->getLocation(), diag::err_redefinition) << FD;

  Diag(Definition->getLocation(), diag::note_previous_definition);
  FD->setInvalidDecl();
}

__attribute__((hot)) Decl *Sema::OnStartOfFunctionDef(Scope *FnBodyScope,
                                                      Decl *D,
                                                      SkipBodyInfo *SkipBody) {
  if (!D) {
    // Parsing the function declaration failed in some way. Push on a fake scope
    // anyway so we can try to parse the function body.
    PushFunctionScope();
    PushExpressionEvaluationContext(ExprEvalContexts.back().Context);
    return D;
  }

  FunctionDecl *FD = cast<FunctionDecl>(D);

  PushExpressionEvaluationContext(ExprEvalContexts.back().Context);

  if (LLVM_UNLIKELY(FD->hasAttrs())) {
    if (const auto *Attr = FD->getAttr<AliasAttr>()) {
      Diag(Attr->getLocation(), diag::err_alias_is_definition) << FD << 0;
      FD->dropAttr<AliasAttr>();
      FD->setInvalidDecl();
    }
    if (const auto *Attr = FD->getAttr<IFuncAttr>()) {
      Diag(Attr->getLocation(), diag::err_alias_is_definition) << FD << 1;
      FD->dropAttr<IFuncAttr>();
      FD->setInvalidDecl();
    }
    if (const auto *Attr = FD->getAttr<TargetVersionAttr>()) {
      if (!Context.getTargetInfo().hasFeature("fmv") &&
          !Attr->isDefaultVersion()) {
        if (SkipBody)
          SkipBody->ShouldSkip = true;
        return nullptr;
      }
    }
  }

  // See if this is a redefinition. If 'will have body' is already set, these
  // checks were already performed when it was set.
  if (!FD->willHaveBody()) {
    CheckForFunctionRedefinition(FD, nullptr, SkipBody);

    // If we're skipping the body, we're done. Don't enter the scope.
    if (SkipBody && SkipBody->ShouldSkip)
      return D;
  }

  // Mark this function as "will have a body eventually".  This lets users to
  // call e.g. isInlineDefinitionExternallyVisible while we're still parsing
  // this function.
  FD->setWillHaveBody();

  PushFunctionScope();

  // Builtin functions cannot be defined.
  if (unsigned BuiltinID = FD->getBuiltinID()) {
    if (!Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID) &&
        !Context.BuiltinInfo.isPredefinedRuntimeFunction(BuiltinID)) {
      Diag(FD->getLocation(), diag::err_builtin_definition) << FD;
      FD->setInvalidDecl();
    }
  }

  // Return type must be complete at the opening `{` (C99 6.9.1p3).
  QualType ResultType = FD->getReturnType();
  if (!ResultType->isVoidType() && !FD->isInvalidDecl() &&
      RequireCompleteType(FD->getLocation(), ResultType,
                          diag::err_func_def_incomplete_result))
    FD->setInvalidDecl();

  if (FnBodyScope)
    PushDeclContext(FnBodyScope, FD);

  CheckParmsForFunctionDef(FD->parameters(),
                           /*CheckParameterNames=*/true);

  // Add non-parameter declarations already in the function to the current
  // scope.
  if (FnBodyScope) {
    for (Decl *NPD : FD->decls()) {
      auto *NonParmDecl = dyn_cast<NamedDecl>(NPD);
      if (!NonParmDecl)
        continue;
      assert(!isa<ParmVarDecl>(NonParmDecl) &&
             "parameters should not be in newly created FD yet");

      // If the decl has a name, make it accessible in the current scope.
      if (NonParmDecl->getDeclName())
        PushOnScopeChains(NonParmDecl, FnBodyScope, /*AddToContext=*/false);

      // Similarly, dive into enums and fish their constants out, making them
      // accessible in this scope.
      if (auto *ED = dyn_cast<EnumDecl>(NonParmDecl)) {
        for (auto *EI : ED->enumerators())
          PushOnScopeChains(EI, FnBodyScope, /*AddToContext=*/false);
      }
    }
  }

  // Introduce our parameters into the function scope
  for (auto *Param : FD->parameters()) {
    Param->setOwningFunction(FD);

    // If this has an identifier, add it to the scope stack.
    if (Param->getIdentifier() && FnBodyScope) {
      CheckShadow(FnBodyScope, Param);

      PushOnScopeChains(Param, FnBodyScope);
    }
  }

  // dllimport cannot be applied to non-inline function definitions.
  if (FD->hasAttr<DLLImportAttr>() && !FD->isInlined()) {
    assert(!FD->hasAttr<DLLExportAttr>());
    Diag(FD->getLocation(), diag::err_attribute_dllimport_function_definition);
    FD->setInvalidDecl();
    return D;
  }
  return D;
}

void Sema::computeNRVO(Stmt *Body, FunctionScopeInfo *Scope) {
  ReturnStmt **Returns = Scope->Returns.data();

  for (unsigned I = 0, E = Scope->Returns.size(); I != E; ++I) {
    if (const VarDecl *NRVOCandidate = Returns[I]->getNRVOCandidate()) {
      if (!NRVOCandidate->isNRVOVariable())
        Returns[I]->setNRVOCandidate(nullptr);
    }
  }
}

class ExitFunctionBodyRAII {
public:
  ExitFunctionBodyRAII(Sema &S) : S(S) {}
  ~ExitFunctionBodyRAII() { S.PopExpressionEvaluationContext(); }

private:
  Sema &S;
};

Decl *Sema::OnFinishFunctionBody(Decl *dcl, Stmt *Body) {
  FunctionScopeInfo *FSI = getCurFunction();
  FunctionDecl *FD = dcl ? dcl->getAsFunction() : nullptr;

  if (FSI->UsesFPIntrin && FD && !FD->hasAttr<StrictFPAttr>())
    FD->addAttr(StrictFPAttr::CreateImplicit(Context));

  {
    ExitFunctionBodyRAII ExitRAII(*this);
    if (FD) {
      FD->setBody(Body);
      FD->setWillHaveBody(false);
      if (!FD->isInvalidDecl()) {
        if (LLVM_LIKELY(!getDiagnostics().getIgnoreAllWarnings())) {
          if (!FD->hasAttr<NakedAttr>())
            DiagnoseUnusedParameters(FD->parameters());
          DiagnoseSizeOfParametersAndReturnValue(FD->parameters(),
                                                 FD->getReturnType(), FD);
        }

        if (FD->getReturnType()->isRecordType())
          computeNRVO(Body, FSI);
      }

      const FunctionDecl *PossiblePrototype = nullptr;
      if (LLVM_UNLIKELY(!getDiagnostics().getIgnoreAllWarnings()) &&
          shouldWarnAboutMissingPrototype(FD, PossiblePrototype)) {
        Diag(FD->getLocation(), diag::warn_missing_prototype) << FD;

        if (PossiblePrototype) {
          // We found a declaration that is not a prototype,
          // but that could be a zero-parameter prototype
          if (TypeSourceInfo *TI = PossiblePrototype->getTypeSourceInfo()) {
            TypeLoc TL = TI->getTypeLoc();
            if (FunctionNoProtoTypeLoc FTL = TL.getAs<FunctionNoProtoTypeLoc>())
              Diag(PossiblePrototype->getLocation(),
                   diag::note_declaration_not_a_prototype)
                  << (FD->getNumParams() != 0)
                  << (FD->getNumParams() == 0
                          ? FixItHint::CreateInsertion(
                                FTL.getRParenLoc(),
                                tok::getKeywordSpelling(tok::kw_void))
                          : FixItHint{});
          }
        } else {
          // Returns true if the token beginning at this Loc is `const`.
          auto isLocAtConst = [&](SourceLocation Loc, const SourceManager &SM,
                                  const LangOptions &LangOpts) {
            std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
            if (LocInfo.first.isInvalid())
              return false;

            bool Invalid = false;
            llvm::StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
            if (Invalid)
              return false;

            if (LocInfo.second > Buffer.size())
              return false;

            const char *LexStart = Buffer.data() + LocInfo.second;
            llvm::StringRef StartTok(LexStart, Buffer.size() - LocInfo.second);

            return StartTok.consume_front(
                       tok::getKeywordSpelling(tok::kw_const)) &&
                   (StartTok.empty() || isWhitespace(StartTok[0]) ||
                    StartTok.starts_with("/*") || StartTok.starts_with("//"));
          };

          auto findBeginLoc = [&]() {
            // If the return type has `const` qualifier, we want to insert
            // `static` before `const` (and not before the typename).
            if ((FD->getReturnType()->isAnyPointerType() &&
                 FD->getReturnType()->getPointeeType().isConstQualified()) ||
                FD->getReturnType().isConstQualified()) {
              // But only do this if we can determine where the `const` is.

              if (isLocAtConst(FD->getBeginLoc(), getSourceManager(),
                               getLangOpts()))

                return FD->getBeginLoc();
            }
            return FD->getTypeSpecStartLoc();
          };
          llvm::SmallString<16> StaticKw(
              tok::getKeywordSpelling(tok::kw_static));
          StaticKw += ' ';
          Diag(FD->getTypeSpecStartLoc(),
               diag::note_static_for_internal_linkage)
              << /* function */ 1
              << (FD->getStorageClass() == SC_None
                      ? FixItHint::CreateInsertion(findBeginLoc(), StaticKw)
                      : FixItHint{});
        }
      }

      if (LLVM_UNLIKELY(!getDiagnostics().getIgnoreAllWarnings())) {
        // We might not have found a prototype because we didn't wish to warn on
        // the lack of a missing prototype. Try again without the checks for
        // whether we want to warn on the missing prototype.
        if (!PossiblePrototype)
          (void)findPossiblePrototype(FD, PossiblePrototype);

        if (!FD->hasWrittenPrototype() && FD->getNumParams() != 0 &&
            (!PossiblePrototype || (!PossiblePrototype->hasWrittenPrototype() &&
                                    !PossiblePrototype->isImplicit()))) {
          Diag(FD->getLocation(), diag::warn_non_prototype_changes_behavior)
              << /*definition*/ 1 << /* not supported in C23 */ 0;

          if (PossiblePrototype)
            Diag(PossiblePrototype->getLocation(),
                 diag::warn_non_prototype_changes_behavior)
                << /*declaration*/ 0 << /* conflicting */ 1 << /*subsequent*/ 1
                << /*definition*/ 1;
        }

        if (FD->isMultiVersion() && FD->hasAttr<CPUDispatchAttr>() && Body)
          if (const auto *CmpndBody = dyn_cast<CompoundStmt>(Body))
            if (!CmpndBody->body_empty())
              Diag(CmpndBody->body_front()->getBeginLoc(),
                   diag::warn_dispatch_body_ignored);
      }

      assert(FD == getCurFunctionDecl() && "Function parsing confused");
    } else {
      // Parsing the function declaration failed in some way. Pop the fake scope
      // we pushed on.
      PopFunctionScopeInfo();
      return nullptr;
    }

    if (Body && FSI->HasPotentialAvailabilityViolations)
      DiagnoseUnguardedAvailabilityViolations(dcl);

    // Verify and clean out per-function state.
    if (Body) {
      // Verify that gotos and switch cases don't jump into scopes illegally.
      if (FSI->NeedsScopeChecking())
        DiagnoseInvalidJumps(Body);

      // If any errors have occurred, clear out any temporaries that may have
      // been leftover. This ensures that these temporaries won't be picked up
      // for deletion in some later function.
      if (hasUncompilableErrorOccurred() ||
          hasAnyUnrecoverableErrorsInThisFunction() ||
          getDiagnostics().getSuppressAllDiagnostics()) {
        DiscardCleanupsInEvaluationContext();
      }
      if (FD && FD->hasAttr<NakedAttr>()) {
        for (const Stmt *S : Body->children()) {
          // Allow local register variables without initializer as they don't
          // require prologue.
          bool RegisterVariables = false;
          if (auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const auto *Decl : DS->decls()) {
              if (const auto *Var = dyn_cast<VarDecl>(Decl)) {
                RegisterVariables =
                    Var->hasAttr<AsmLabelAttr>() && !Var->hasInit();
                if (!RegisterVariables)
                  break;
              }
            }
          }
          if (RegisterVariables)
            continue;
          if (!isa<AsmStmt>(S) && !isa<NullStmt>(S)) {
            Diag(S->getBeginLoc(), diag::err_non_asm_stmt_in_naked_function);
            Diag(FD->getAttr<NakedAttr>()->getLocation(), diag::note_attribute);
            FD->setInvalidDecl();
            break;
          }
        }
      }

      assert(ExprCleanupObjects.size() ==
                 ExprEvalContexts.back().NumCleanupObjects &&
             "Leftover temporaries in function");
      assert(!Cleanup.exprNeedsCleanups() &&
             "Unaccounted cleanups in function");
    }
  } // ExitFunctionBodyRAII must run before PopDeclContext below.

  PopDeclContext();

  PopFunctionScopeInfo();
  // If any errors have occurred, clear out any temporaries that may have
  // been leftover. This ensures that these temporaries won't be picked up for
  // deletion in some later function.
  if (hasUncompilableErrorOccurred()) {
    DiscardCleanupsInEvaluationContext();
  }

  if (FD)
    checkTypeSupport(FD->getType(), FD->getLocation(), FD);

  return dcl;
}

void Sema::OnFinishDelayedAttribute(Scope *S, Decl *D,
                                    ParsedAttributes &Attrs) {
  ProcessDeclAttributeList(S, D, Attrs);
}

// ===----------------------------------------------------------------------===
// Implicit declarations & known attributes
// ===----------------------------------------------------------------------===

NamedDecl *Sema::ImplicitlyDefineFunction(SourceLocation Loc,
                                          IdentifierInfo &II, Scope *S) {
  // It is not valid to implicitly define a function in C23.
  assert(LangOpts.implicitFunctionsAllowed() &&
         "Implicit function declarations aren't allowed in this language mode");

  // Find the scope in which the identifier is injected and the corresponding
  // DeclContext.
  // C89 does not say what happens if there is no enclosing block scope.
  // In that case, we inject the declaration into the translation unit scope
  // instead.
  Scope *BlockScope = S;
  while (!BlockScope->isCompoundStmtScope() && BlockScope->getParent())
    BlockScope = BlockScope->getParent();

  // Loop until we find a DeclContext that is either a function/method or the
  // translation unit, which are the only two valid places to implicitly define
  // a function. This avoids accidentally defining the function within a tag
  // declaration, for example.
  Scope *ContextScope = BlockScope;
  while (!ContextScope->getEntity() ||
         (!ContextScope->getEntity()->isFunctionOrMethod() &&
          !ContextScope->getEntity()->isTranslationUnit()))
    ContextScope = ContextScope->getParent();
  ContextRAII SavedContext(*this, ContextScope->getEntity());

  // Before we produce a declaration for an implicitly defined
  // function, see whether there was a locally-scoped declaration of
  // this name as a function or variable. If so, use that
  // (non-visible) declaration, and complain about it.
  NamedDecl *ExternCPrev = findLocallyScopedExternCDecl(&II);
  if (ExternCPrev) {
    // We still need to inject the function into the enclosing block scope so
    // that later (non-call) uses can see it.
    PushOnScopeChains(ExternCPrev, BlockScope, /*AddToContext*/ false);

    // C89 footnote 38:
    //   If in fact it is not defined as having type "function returning int",
    //   the behavior is undefined.
    if (!isa<FunctionDecl>(ExternCPrev) ||
        !Context.typesAreCompatible(
            cast<FunctionDecl>(ExternCPrev)->getType(),
            Context.getFunctionNoProtoType(Context.IntTy))) {
      Diag(Loc, diag::ext_use_out_of_scope_declaration)
          << ExternCPrev << !getLangOpts().C99;
      Diag(ExternCPrev->getLocation(), diag::note_previous_declaration);
      return ExternCPrev;
    }
  }

  // NeverC accepts implicit function declarations silently in every C
  // standard — implicitFunctionsAllowed() returns true unconditionally —
  // so we can pair `printf("hi")` (without #include <stdio.h>) and
  // out-of-order intra-TU calls with a clean compile.  The `__builtin_*`
  // family is still gated by a diagnostic because misspelling those is
  // almost always a real bug.
  unsigned diag_id =
      II.getName().starts_with("__builtin_") ? diag::warn_builtin_unknown : 0;

  TypoCorrection Corrected;
  if (diag_id) {
    // Because typo correction is expensive, only do it if the implicit
    // function declaration is going to be treated as an error.
    //
    // Perform the correction before issuing the main diagnostic, as some
    // consumers use typo-correction callbacks to enhance the main diagnostic.
    if (S && !ExternCPrev &&
        (Diags.getDiagnosticLevel(diag_id, Loc) >= DiagnosticsEngine::Error)) {
      DeclFilterCCC<FunctionDecl> CCC{};
      Corrected = CorrectTypo(DeclarationNameInfo(&II, Loc), ResolveOrdinary, S,
                              CCC, CTK_NonError);
    }

    Diag(Loc, diag_id) << &II;
    if (Corrected) {
      // If the correction is going to suggest an implicitly defined function,
      // skip the correction as not being a particularly good idea.
      bool Diagnose = true;
      if (const auto *D = Corrected.getCorrectionDecl())
        Diagnose = !D->isImplicit();
      if (Diagnose)
        diagnoseTypo(Corrected, PDiag(diag::note_function_suggestion),
                     /*ErrorRecovery*/ false);
    }
  }

  // If we found a prior declaration of this function, don't bother building
  // another one. We've already pushed that one into scope, so there's nothing
  // more to do.
  if (ExternCPrev)
    return ExternCPrev;

  // Set a Declarator for the implicit definition: int foo();
  const char *Dummy;
  AttributeFactory attrFactory;
  DeclSpec DS(attrFactory);
  unsigned DiagID;
  bool Error = DS.SetTypeSpecType(DeclSpec::TST_int, Loc, Dummy, DiagID,
                                  Context.getPrintingPolicy());
  (void)Error; // Silence warning.
  assert(!Error && "Error setting up implicit decl!");
  SourceLocation NoLoc;
  Declarator D(DS, ParsedAttributesView::none(), DeclaratorContext::Block);
  D.AddTypeInfo(DeclaratorChunk::getFunction(/*HasProto=*/false,
                                             /*IsAmbiguous=*/false,
                                             /*LParenLoc=*/NoLoc,
                                             /*Params=*/nullptr,
                                             /*NumParams=*/0,
                                             /*EllipsisLoc=*/NoLoc,
                                             /*RParenLoc=*/NoLoc,
                                             /*DeclsInPrototype=*/std::nullopt,
                                             Loc, Loc, D),
                std::move(DS.getAttributes()), SourceLocation());
  D.SetIdentifier(&II, Loc);

  // Insert this function into the enclosing block scope.
  FunctionDecl *FD = cast<FunctionDecl>(OnDeclarator(BlockScope, D));
  FD->setImplicit();

  AddKnownFunctionAttributes(FD);

  return FD;
}

void Sema::AddKnownFunctionAttributes(FunctionDecl *FD) {
  if (FD->isInvalidDecl())
    return;

  // If this is a built-in function, map its builtin attributes to
  // actual attributes.
  if (unsigned BuiltinID = FD->getBuiltinID()) {
    unsigned FormatIdx;
    bool HasVAListArg;
    if (Context.BuiltinInfo.isPrintfLike(BuiltinID, FormatIdx, HasVAListArg)) {
      if (!FD->hasAttr<FormatAttr>()) {
        const char *fmt = "printf";
        FD->addAttr(FormatAttr::CreateImplicit(
            Context, &Context.Idents.get(fmt), FormatIdx + 1,
            HasVAListArg ? 0 : FormatIdx + 2, FD->getLocation()));
      }
    }
    if (Context.BuiltinInfo.isScanfLike(BuiltinID, FormatIdx, HasVAListArg)) {
      if (!FD->hasAttr<FormatAttr>())
        FD->addAttr(FormatAttr::CreateImplicit(
            Context, &Context.Idents.get("scanf"), FormatIdx + 1,
            HasVAListArg ? 0 : FormatIdx + 2, FD->getLocation()));
    }

    llvm::SmallVector<int, 4> Encoding;
    if (!FD->hasAttr<CallbackAttr>() &&
        Context.BuiltinInfo.performsCallback(BuiltinID, Encoding))
      FD->addAttr(CallbackAttr::CreateImplicit(
          Context, Encoding.data(), Encoding.size(), FD->getLocation()));

    // Mark const if we don't care about errno and/or floating point exceptions
    // that are the only thing preventing the function from being const. This
    // allows IRgen to use LLVM intrinsics for such functions.
    bool NoExceptions =
        getLangOpts().getDefaultExceptionMode() == LangOptions::FPE_Ignore;
    bool ConstWithoutErrnoAndExceptions =
        Context.BuiltinInfo.isConstWithoutErrnoAndExceptions(BuiltinID);
    bool ConstWithoutExceptions =
        Context.BuiltinInfo.isConstWithoutExceptions(BuiltinID);
    if (!FD->hasAttr<ConstAttr>() &&
        (ConstWithoutErrnoAndExceptions || ConstWithoutExceptions) &&
        (!ConstWithoutErrnoAndExceptions ||
         (!getLangOpts().MathErrno && NoExceptions)) &&
        (!ConstWithoutExceptions || NoExceptions))
      FD->addAttr(ConstAttr::CreateImplicit(Context, FD->getLocation()));

    // We make "fma" on GNU or Windows const because we know it does not set
    // errno in those environments even though it could set errno based on the
    // C standard.
    const llvm::Triple &Trip = Context.getTargetInfo().getTriple();
    if ((Trip.isGNUEnvironment() || Trip.isOSMSVCRT()) &&
        !FD->hasAttr<ConstAttr>()) {
      switch (BuiltinID) {
      case Builtin::BI__builtin_fma:
      case Builtin::BI__builtin_fmaf:
      case Builtin::BI__builtin_fmal:
      case Builtin::BIfma:
      case Builtin::BIfmaf:
      case Builtin::BIfmal:
        FD->addAttr(ConstAttr::CreateImplicit(Context, FD->getLocation()));
        break;
      default:
        break;
      }
    }

    if (Context.BuiltinInfo.isReturnsTwice(BuiltinID) &&
        !FD->hasAttr<ReturnsTwiceAttr>())
      FD->addAttr(ReturnsTwiceAttr::CreateImplicit(Context, FD->getLocation()));
    if (Context.BuiltinInfo.isNoThrow(BuiltinID) && !FD->hasAttr<NoThrowAttr>())
      FD->addAttr(NoThrowAttr::CreateImplicit(Context, FD->getLocation()));
    if (Context.BuiltinInfo.isPure(BuiltinID) && !FD->hasAttr<PureAttr>())
      FD->addAttr(PureAttr::CreateImplicit(Context, FD->getLocation()));
    if (Context.BuiltinInfo.isConst(BuiltinID) && !FD->hasAttr<ConstAttr>())
      FD->addAttr(ConstAttr::CreateImplicit(Context, FD->getLocation()));
    // Add known guaranteed alignment for allocation functions.
    switch (BuiltinID) {
    case Builtin::BImemalign:
    case Builtin::BIaligned_alloc:
      if (!FD->hasAttr<AllocAlignAttr>())
        FD->addAttr(AllocAlignAttr::CreateImplicit(Context, ParamIdx(1, FD),
                                                   FD->getLocation()));
      break;
    default:
      break;
    }

    // Add allocsize attribute for allocation functions.
    switch (BuiltinID) {
    case Builtin::BIcalloc:
      FD->addAttr(AllocSizeAttr::CreateImplicit(
          Context, ParamIdx(1, FD), ParamIdx(2, FD), FD->getLocation()));
      break;
    case Builtin::BImemalign:
    case Builtin::BIaligned_alloc:
    case Builtin::BIrealloc:
      FD->addAttr(AllocSizeAttr::CreateImplicit(Context, ParamIdx(2, FD),
                                                ParamIdx(), FD->getLocation()));
      break;
    case Builtin::BImalloc:
      FD->addAttr(AllocSizeAttr::CreateImplicit(Context, ParamIdx(1, FD),
                                                ParamIdx(), FD->getLocation()));
      break;
    default:
      break;
    }
  }

  IdentifierInfo *Name = FD->getIdentifier();
  if (!Name)
    return;
  if (FD->getDeclContext()->isTranslationUnit()) {
    // Okay: this could be a libc/libm function we know about.
  } else
    return;

  if (Name->isStr("asprintf") || Name->isStr("vasprintf")) {
    if (!FD->hasAttr<FormatAttr>())
      FD->addAttr(FormatAttr::CreateImplicit(
          Context, &Context.Idents.get("printf"), 2,
          Name->isStr("vasprintf") ? 0 : 3, FD->getLocation()));
  }
}

TypedefDecl *Sema::ParseTypedefDecl(Scope *S, Declarator &D, QualType T,
                                    TypeSourceInfo *TInfo) {
  assert(D.getIdentifier() && "Wrong callback for declspec without declarator");
  assert(!T.isNull() && "ResolveDeclaratorType() returned null type");

  if (!TInfo) {
    assert(D.isInvalidType() && "no declarator info for valid type");
    TInfo = Context.getTrivialTypeSourceInfo(T);
  }

  // Scope manipulation handled by caller.
  TypedefDecl *NewTD =
      TypedefDecl::Create(Context, CurContext, D.getBeginLoc(),
                          D.getIdentifierLoc(), D.getIdentifier(), TInfo);

  // Bail out immediately if we have an invalid declaration.
  if (D.isInvalidType()) {
    NewTD->setInvalidDecl();
    return NewTD;
  }

  // Unnamed class/enum in a typedef: the first typedef name supplies linkage
  // for the tag type.
  const DeclSpec &TDS = D.getDeclSpec();
  switch (TDS.getTypeSpecType()) {
  case TST_enum:
  case TST_struct:
  case TST_union: {
    TagDecl *tagFromDeclSpec = cast<TagDecl>(TDS.getRepAsDecl());
    setTagNameForLinkagePurposes(tagFromDeclSpec, NewTD);
    break;
  }
  default:
    break;
  }

  return NewTD;
}

bool Sema::CheckEnumUnderlyingType(TypeSourceInfo *TI) {
  SourceLocation UnderlyingLoc = TI->getTypeLoc().getBeginLoc();
  QualType T = TI->getType();

  // This doesn't use 'isIntegralType' despite the error message mentioning
  // integral type because isIntegralType would also allow enum types in C.
  if (const BuiltinType *BT = T->getAs<BuiltinType>())
    if (BT->isInteger())
      return false;

  return Diag(UnderlyingLoc, diag::err_enum_invalid_underlying)
         << T << T->isBitIntType();
}

bool Sema::CheckEnumRedeclaration(SourceLocation EnumLoc, bool,
                                  QualType EnumUnderlyingTy, bool IsFixed,
                                  const EnumDecl *Prev) {
  if (IsFixed && Prev->isFixed()) {
    if (!Context.hasSameUnqualifiedType(EnumUnderlyingTy,
                                        Prev->getIntegerType())) {
      Diag(EnumLoc, diag::err_enum_redeclare_type_mismatch)
          << EnumUnderlyingTy << Prev->getIntegerType();
      Diag(Prev->getLocation(), diag::note_previous_declaration)
          << Prev->getIntegerTypeRange();
      return true;
    }
  } else if (IsFixed != Prev->isFixed()) {
    Diag(EnumLoc, diag::err_enum_redeclare_fixed_mismatch) << Prev->isFixed();
    Diag(Prev->getLocation(), diag::note_previous_declaration);
    return true;
  }

  return false;
}

Sema::NonTagKind Sema::getNonTagTypeDeclKind(const Decl *PrevDecl,
                                             TagTypeKind TTK) {
  if (isa<TypedefNameDecl>(PrevDecl))
    return NTK_Typedef;
  switch (TTK) {
  case TagTypeKind::Struct:
    return NTK_NonStruct;
  case TagTypeKind::Union:
    return NTK_NonUnion;
  case TagTypeKind::Enum:
    return NTK_NonEnum;
  }
  llvm_unreachable("invalid TTK");
}

bool Sema::isAcceptableTagRedeclaration(const TagDecl *Previous,
                                        TagTypeKind NewTag) {
  return Previous->getTagKind() == NewTag;
}

// ===----------------------------------------------------------------------===
// Tag (struct/union/enum) handling
// ===----------------------------------------------------------------------===

DeclResult Sema::OnTag(Scope *S, unsigned TagSpec, TagUseKind TUK,
                       SourceLocation KWLoc, IdentifierInfo *Name,
                       SourceLocation NameLoc,
                       const ParsedAttributesView &Attrs, AccessSpecifier AS,
                       bool &OwnedDecl, TypeResult UnderlyingType,
                       OffsetOfKind OOK, SkipBodyInfo *SkipBody) {
  // If this is not a definition, it must have a name.
  assert((Name != nullptr || TUK == TUK_Definition) &&
         "Nameless record must be a definition!");

  OwnedDecl = false;
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);

  bool Invalid = false;

  // Figure out the underlying type if this a enum declaration. We need to do
  // this early, because it's needed to detect if this is an incompatible
  // redeclaration.
  llvm::PointerUnion<const Type *, TypeSourceInfo *> EnumUnderlying;
  bool IsFixed = !UnderlyingType.isUnset();

  if (Kind == TagTypeKind::Enum) {
    if (UnderlyingType.isInvalid()) {
      // No underlying type explicitly specified, or we failed to parse the
      // type, default to int.
      EnumUnderlying = Context.IntTy.getTypePtr();
    } else if (UnderlyingType.get()) {
      // Fixed underlying type: must be an integral type (cv ignored).
      TypeSourceInfo *TI = nullptr;
      GetTypeFromParser(UnderlyingType.get(), &TI);
      EnumUnderlying = TI;

      if (CheckEnumUnderlyingType(TI))
        // Recover by falling back to int.
        EnumUnderlying = Context.IntTy.getTypePtr();

    } else if (Context.getTargetInfo().getTriple().isWindowsMSVCEnvironment()) {
      // For MSVC ABI compatibility, unfixed enums must use an underlying type
      // of 'int'. However, if this is an unfixed forward declaration, don't set
      // the underlying type unless the user enables -fms-compatibility. This
      // makes unfixed forward declared enums incomplete and is more conforming.
      if (TUK == TUK_Definition || getLangOpts().MSVCCompat)
        EnumUnderlying = Context.IntTy.getTypePtr();
    }
  }

  DeclContext *SearchDC = CurContext;

  RedeclarationKind Redecl = forRedeclarationInCurContext();
  if (TUK == TUK_Reference)
    Redecl = NotForRedeclaration;

  auto createTagFromNewDecl = [&]() -> TagDecl * {
    // If there is an identifier, use the location of the identifier as the
    // location of the decl, otherwise use the location of the struct/union
    // keyword.
    SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;
    TagDecl *New = nullptr;

    if (Kind == TagTypeKind::Enum) {
      New = EnumDecl::Create(Context, SearchDC, KWLoc, Loc, Name, nullptr,
                             IsFixed);
      // If this is an undefined enum, bail.
      if (TUK != TUK_Definition && !Invalid)
        return nullptr;
      if (EnumUnderlying) {
        EnumDecl *ED = cast<EnumDecl>(New);
        if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
          ED->setIntegerTypeSourceInfo(TI);
        else
          ED->setIntegerType(QualType(EnumUnderlying.get<const Type *>(), 0));
        QualType EnumTy = ED->getIntegerType();
        ED->setPromotionType(Context.isPromotableIntegerType(EnumTy)
                                 ? Context.getPromotedIntegerType(EnumTy)
                                 : EnumTy);
      }
    } else { // struct/union
      New = RecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                               nullptr);
    }

    if (RecordDecl *RD = dyn_cast<RecordDecl>(New)) {
      // Add alignment attributes if necessary; these attributes are checked
      // when the TreeContext lays out the structure.
      //
      // It is important for implementing the correct semantics that this
      // happen here (in OnTag). The #pragma pack stack is
      // maintained as a result of parser callbacks which can occur at
      // many points during the parsing of a struct declaration (because
      // the #pragma tokens are effectively skipped over during the
      // parsing of the struct).
      if (TUK == TUK_Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
        AddAlignmentAttributesForRecord(RD);
        AddMsStructLayoutForRecord(RD);
      }
    }
    New->setLexicalDeclContext(CurContext);
    return New;
  };

  LookupResult Previous(*this, Name, NameLoc, ResolveTag, Redecl);
  if (Name) {
    // If this is a named struct, check to see if there was a previous forward
    // declaration or definition.
    // We're looking into outer scopes here, even when we
    // shouldn't be. Doing so can result in ambiguities that we
    // shouldn't be diagnosing.
    ResolveName(Previous, S);

    // When declaring or defining a tag, ignore ambiguities introduced
    // by types using'ed into this scope.
    if (Previous.isAmbiguous() &&
        (TUK == TUK_Definition || TUK == TUK_Declaration)) {
      LookupResult::Filter F = Previous.makeFilter();
      while (F.hasNext()) {
        NamedDecl *ND = F.next();
        if (!ND->getDeclContext()->getRedeclContext()->Equals(
                SearchDC->getRedeclContext()))
          F.erase();
      }
      F.done();
    }

    // Note:  there used to be some attempt at recovery here.
    if (Previous.isAmbiguous())
      return true;

    if (TUK != TUK_Reference) {
      while (isa<RecordDecl, EnumDecl>(SearchDC))
        SearchDC = SearchDC->getParent();
    }
  }

  if (Name && Previous.empty() && TUK == TUK_Reference) {
    if (Invalid)
      goto CreateNewDecl;

    // C99 6.7.2.3p8: find the scope where we'll be declaring the tag.
    SearchDC = getTagInjectionContext(SearchDC);
    S = getTagInjectionScope(S, getLangOpts());

    Previous.setRedeclarationKind(forRedeclarationInCurContext());
    ResolveName(Previous, S);
  }

  // If we have a known previous declaration to use, then use it.
  if (Previous.empty() && SkipBody && SkipBody->Previous)
    Previous.addDecl(SkipBody->Previous);

  if (!Previous.empty()) {
    NamedDecl *PrevDecl = Previous.getFoundDecl();
    NamedDecl *DirectPrevDecl = Previous.getRepresentativeDecl();

    if (TagDecl *PrevTagDecl = dyn_cast<TagDecl>(PrevDecl)) {
      // If this is a use of a previous tag, or if the tag is already declared
      // in the same scope (so that the definition/declaration completes or
      // rementions the tag), reuse the decl.
      if (TUK == TUK_Reference || isDeclInScope(DirectPrevDecl, SearchDC, S)) {
        // Make sure that this wasn't declared as an enum and now used as a
        // struct or something similar.
        if (!isAcceptableTagRedeclaration(PrevTagDecl, Kind)) {
          bool SafeToContinue =
              (PrevTagDecl->getTagKind() != TagTypeKind::Enum &&
               Kind != TagTypeKind::Enum);
          if (SafeToContinue)
            Diag(KWLoc, diag::err_use_with_wrong_tag)
                << Name
                << FixItHint::CreateReplacement(SourceRange(KWLoc),
                                                PrevTagDecl->getKindName());
          else
            Diag(KWLoc, diag::err_use_with_wrong_tag) << Name;
          Diag(PrevTagDecl->getLocation(), diag::note_previous_use);

          if (SafeToContinue)
            Kind = PrevTagDecl->getTagKind();
          else {
            // Recover by making this an anonymous redefinition.
            Name = nullptr;
            Previous.clear();
            Invalid = true;
          }
        }

        if (Kind == TagTypeKind::Enum &&
            PrevTagDecl->getTagKind() == TagTypeKind::Enum) {
          const EnumDecl *PrevEnum = cast<EnumDecl>(PrevTagDecl);
          if (TUK == TUK_Reference)
            return PrevTagDecl;

          QualType EnumUnderlyingTy;
          if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
            EnumUnderlyingTy = TI->getType().getUnqualifiedType();
          else if (const Type *T = EnumUnderlying.dyn_cast<const Type *>())
            EnumUnderlyingTy = QualType(T, 0);

          // All conflicts with previous declarations are recovered by
          // returning the previous declaration, unless this is a definition,
          // in which case we want the caller to bail out.
          if (CheckEnumRedeclaration(NameLoc.isValid() ? NameLoc : KWLoc,
                                     /*IsScoped=*/false, EnumUnderlyingTy,
                                     IsFixed, PrevEnum))
            return TUK == TUK_Declaration ? PrevTagDecl : nullptr;
        }

        if (!Invalid) {
          // If this is a use, just return the declaration we found, unless
          // we have attributes.
          if (TUK == TUK_Reference) {
            if (!Attrs.empty()) {
            } else {
              return PrevTagDecl;
            }
          }

          // Diagnose attempts to redefine a tag.
          if (TUK == TUK_Definition) {
            if (NamedDecl *Def = PrevTagDecl->getDefinition()) {
              // Allow ODR-like semantics for C, i.e., do not keep more than
              // one definition around (merge them). Ensure the decl passes the
              // structural compatibility check in C11 6.2.7/1 (or 6.1.2.6/1
              // in C89).
              // The BuiltinString prelude planted a tag (e.g.
              // `struct __neverc_string`) into <built-in>; the user's
              // real source can redefine the same tag and we silently
              // absorb the duplicate.
              bool BuiltinPriorDef =
                  Context.getSourceManager().isWrittenInBuiltinFile(
                      Def->getLocation());
              if (SkipBody && BuiltinPriorDef) {
                // The old decl is a synthetic pre-decl; merge visibility
                // after parsing.
                SkipBody->CheckSameAsPrevious = true;
                SkipBody->New = createTagFromNewDecl();
                SkipBody->Previous = Def;
                return Def;
              } else {
                // A redeclaration in function prototype scope in C isn't
                // visible elsewhere, so merely issue a warning.
                if (S->containedInPrototypeScope())
                  Diag(NameLoc, diag::warn_redefinition_in_param_list) << Name;
                else
                  Diag(NameLoc, diag::err_redefinition) << Name;
                notePreviousDefinition(Def,
                                       NameLoc.isValid() ? NameLoc : KWLoc);
                // If this is a redefinition, recover by making this
                // struct be anonymous, which will make any later
                // references get the previous definition.
                Name = nullptr;
                Previous.clear();
                Invalid = true;
              }
            } else {
              // If the type is currently being defined, complain
              // about a nested redefinition.
              auto *TD = Context.getTagDeclType(PrevTagDecl)->getAsTagDecl();
              if (TD->isBeingDefined()) {
                Diag(NameLoc, diag::err_nested_redefinition) << Name;
                Diag(PrevTagDecl->getLocation(),
                     diag::note_previous_definition);
                Name = nullptr;
                Previous.clear();
                Invalid = true;
              }
            }

            // Okay, this is definition of a previously declared or referenced
            // tag. We're going to create a new Decl for it.
          }

          // Okay, we're going to make a redeclaration.  If this is some kind
          // of reference, make sure we build the redeclaration in the same DC
          // as the original, and ignore the current access specifier.
          if (TUK == TUK_Reference) {
            SearchDC = PrevTagDecl->getDeclContext();
            AS = AS_none;
          }
        }
        // If we get here we have (another) forward declaration or we
        // have a definition.  Just create a new decl.

      } else {
        // If we get here, this is a definition of a new tag type in a nested
        // scope, e.g. "struct foo; void bar() { struct foo; }", just create a
        // new decl/type.  We set PrevDecl to NULL so that the entities
        // have distinct types.
        Previous.clear();
      }
      // If we get here, we're going to create a new Decl. If PrevDecl
      // is non-NULL, it's a definition of the tag declared by
      // PrevDecl. If it's NULL, we have a new definition.

      // PrevDecl is not a tag (e.g. typedef) but matched tag lookup.
    } else {
      // Use a better diagnostic if an elaborated-type-specifier
      // found the wrong kind of type on the first
      // (non-redeclaration) lookup.
      if (TUK == TUK_Reference && !Previous.isForRedeclaration() &&
          (!dyn_cast<TypedefNameDecl>(PrevDecl) ||
           Kind != TagTypeKind::Struct)) {
#ifndef _WIN32
        NonTagKind NTK = getNonTagTypeDeclKind(PrevDecl, Kind);
        Diag(NameLoc, diag::err_tag_reference_non_tag)
            << PrevDecl << NTK << llvm::to_underlying(Kind);
        Diag(PrevDecl->getLocation(), diag::note_declared_at);
        Invalid = true;
#endif
        // Otherwise, only diagnose if the declaration is in scope.
      } else if (!isDeclInScope(DirectPrevDecl, SearchDC, S)) {
        // do nothing

        // Diagnose implicit declarations introduced by elaborated types.
      } else if (TUK == TUK_Reference) {
#ifndef _WIN32
        NonTagKind NTK = getNonTagTypeDeclKind(PrevDecl, Kind);
        Diag(NameLoc, diag::err_tag_reference_conflict) << NTK;
        Diag(PrevDecl->getLocation(), diag::note_previous_decl) << PrevDecl;
        Invalid = true;
#endif
        // Otherwise it's a declaration.  Call out a particularly common
        // case here.
      } else if (TypedefNameDecl *TND = dyn_cast<TypedefNameDecl>(PrevDecl)) {
        Diag(NameLoc, diag::err_tag_definition_of_typedef)
            << Name << TND->getUnderlyingType();
        Diag(PrevDecl->getLocation(), diag::note_previous_decl) << PrevDecl;
        Invalid = true;

        // Otherwise, diagnose.
      } else {
        // The tag name clashes with something else in the target scope,
        // issue an error and recover by making this tag be anonymous.
        Diag(NameLoc, diag::err_redefinition_different_kind) << Name;
        notePreviousDefinition(PrevDecl, NameLoc);
        Name = nullptr;
        Invalid = true;
      }

      // The existing declaration isn't relevant to us; we're in a
      // new scope, so clear out the previous declaration.
      Previous.clear();
    }
  }

CreateNewDecl:

  TagDecl *PrevDecl = nullptr;
  if (Previous.isSingleResult())
    PrevDecl = cast<TagDecl>(Previous.getFoundDecl());

  // If there is an identifier, use the location of the identifier as the
  // location of the decl, otherwise use the location of the struct/union
  // keyword.
  SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;

  // Otherwise, create a new declaration. If there is a previous
  // declaration of the same entity, the two will be linked via
  // PrevDecl.
  TagDecl *New;

  if (Kind == TagTypeKind::Enum) {
    // Tag decls should be chained to any simultaneous vardecls, e.g.:
    // enum X { A, B, C } D;    D should chain to X.
    New = EnumDecl::Create(Context, SearchDC, KWLoc, Loc, Name,
                           cast_or_null<EnumDecl>(PrevDecl), IsFixed);

    // If this is an undefined enum, warn.
#ifndef _WIN32
    if (TUK != TUK_Definition && !Invalid) {
      TagDecl *Def;
      if (IsFixed && cast<EnumDecl>(New)->isFixed()) {
        // Fixed opaque enum: no extra forward-ref diagnostic.
      } else if (PrevDecl &&
                 (Def = cast<EnumDecl>(PrevDecl)->getDefinition())) {
        Diag(Loc, diag::ext_forward_ref_enum_def) << New;
        Diag(Def->getLocation(), diag::note_previous_definition);
      } else {
        unsigned DiagID = diag::ext_forward_ref_enum;
        if (getLangOpts().MSVCCompat)
          DiagID = diag::ext_ms_forward_ref_enum;
        Diag(Loc, DiagID);
      }
    }
#endif

    if (EnumUnderlying) {
      EnumDecl *ED = cast<EnumDecl>(New);
      if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
        ED->setIntegerTypeSourceInfo(TI);
      else
        ED->setIntegerType(QualType(EnumUnderlying.get<const Type *>(), 0));
      QualType EnumTy = ED->getIntegerType();
      ED->setPromotionType(Context.isPromotableIntegerType(EnumTy)
                               ? Context.getPromotedIntegerType(EnumTy)
                               : EnumTy);
      assert(ED->isComplete() && "enum with type should be complete");
    }
  } else {
    // struct/union/class

    // Tag decls should be chained to any simultaneous vardecls, e.g.:
    // struct X { int A; } D;    D should chain to X.
    New = RecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                             cast_or_null<RecordDecl>(PrevDecl));
  }

  if (OOK != OOK_Outside && TUK == TUK_Definition)
    Diag(New->getLocation(), diag::ext_type_defined_in_offsetof)
        << (OOK == OOK_Macro) << New->getSourceRange();

  if (RecordDecl *RD = dyn_cast<RecordDecl>(New)) {
    // Add alignment attributes if necessary; these attributes are checked when
    // the TreeContext lays out the structure.
    //
    // It is important for implementing the correct semantics that this
    // happen here (in OnTag). The #pragma pack stack is
    // maintained as a result of parser callbacks which can occur at
    // many points during the parsing of a struct declaration (because
    // the #pragma tokens are effectively skipped over during the
    // parsing of the struct).
    if (TUK == TUK_Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
      AddAlignmentAttributesForRecord(RD);
      AddMsStructLayoutForRecord(RD);
    }
  }

  // If we're declaring or defining a tag in function prototype scope in C,
  // note that this type can only be used within the function and add it to
  // the list of decls to inject into the function definition scope.
  if ((Name || Kind == TagTypeKind::Enum) &&
      getNonFieldDeclScope(S)->isFunctionPrototypeScope()) {
    if (!PrevDecl) {
      Diag(Loc, diag::warn_decl_in_param_list) << Context.getTagDeclType(New);
    }
  }

  if (Invalid)
    New->setInvalidDecl();

  // Lexical context follows the active scope.
  New->setLexicalDeclContext(CurContext);

  if (!Invalid && SearchDC->isRecord())
    SetMemberAccessSpecifier(New, PrevDecl, AS);

  if (TUK == TUK_Definition && (!SkipBody || !SkipBody->ShouldSkip))
    New->startDefinition();

  ProcessDeclAttributeList(S, New, Attrs);
  AddPragmaAttributes(S, New);

  if (Name) {
    S = getNonFieldDeclScope(S);
    PushOnScopeChains(New, S, true);
  } else {
    CurContext->addDecl(New);
  }

  // If this is the C FILE type, notify the AST context.
  if (IdentifierInfo *II = New->getIdentifier())
    if (!New->isInvalidDecl() &&
        New->getDeclContext()->getRedeclContext()->isTranslationUnit() &&
        II->isStr("FILE"))
      Context.setFILEDecl(New);

  if (PrevDecl)
    mergeDeclAttributes(New, PrevDecl);

  AddPushedVisibilityAttribute(New);

  OwnedDecl = true;
  if (SkipBody && SkipBody->ShouldSkip) {
    return SkipBody->Previous;
  } else {
    return New;
  }
}

void Sema::OnTagStartDefinition(Scope *S, Decl *TagD) {
  TagDecl *Tag = cast<TagDecl>(TagD);

  // Enter the tag context.
  PushDeclContext(S, Tag);

  // If there's a #pragma GCC visibility in scope, set the visibility of this
  // record.
  AddPushedVisibilityAttribute(Tag);
}

void Sema::OnTagFinishDefinition(Scope *S, Decl *TagD, SourceRange BraceRange) {
  TagDecl *Tag = cast<TagDecl>(TagD);
  Tag->setBraceRange(BraceRange);

  // Make sure we "complete" the definition even it is invalid.
  if (Tag->isBeingDefined()) {
    assert(Tag->isInvalidDecl() && "We should already have completed it");
    if (RecordDecl *RD = dyn_cast<RecordDecl>(Tag))
      RD->completeDefinition();
  }

  // Exit this scope of this tag's definition.
  PopDeclContext();

  // Notify the consumer that we've defined a tag.
  if (!Tag->isInvalidDecl())
    Consumer.ProcessTagDeclDefinition(Tag);
}

// Note that FieldName may be null for anonymous bitfields.
ExprResult Sema::VerifyBitField(SourceLocation FieldLoc,
                                IdentifierInfo *FieldName, QualType FieldTy,
                                bool IsMsStruct, Expr *BitWidth) {
  assert(BitWidth);
  if (BitWidth->containsErrors())
    return ExprError();

  // C99 6.7.2.1p4: bit-field type shall be integral or enumeration.
  if (!FieldTy->isIntegralOrEnumerationType()) {
    if (RequireCompleteSizedType(FieldLoc, FieldTy,
                                 diag::err_field_incomplete_or_sizeless))
      return ExprError();
    if (FieldName)
      return Diag(FieldLoc, diag::err_not_integral_type_bitfield)
             << FieldName << FieldTy << BitWidth->getSourceRange();
    return Diag(FieldLoc, diag::err_not_integral_type_anon_bitfield)
           << FieldTy << BitWidth->getSourceRange();
  }

  llvm::APSInt Value;
  ExprResult ICE = VerifyIntegerConstantExpression(BitWidth, &Value, AllowFold);
  if (ICE.isInvalid())
    return ICE;
  BitWidth = ICE.get();

  // Zero-width bitfield is ok for anonymous field.
  if (Value == 0 && FieldName)
    return Diag(FieldLoc, diag::err_bitfield_has_zero_width)
           << FieldName << BitWidth->getSourceRange();

  if (Value.isSigned() && Value.isNegative()) {
    if (FieldName)
      return Diag(FieldLoc, diag::err_bitfield_has_negative_width)
             << FieldName << toString(Value, 10);
    return Diag(FieldLoc, diag::err_anon_bitfield_has_negative_width)
           << toString(Value, 10);
  }

  // The size of the bit-field must not exceed our maximum permitted object
  // size.
  if (Value.getActiveBits() > ConstantArrayType::getMaxSizeBits(Context)) {
    return Diag(FieldLoc, diag::err_bitfield_too_wide)
           << !FieldName << FieldName << toString(Value, 10);
  }

  {
    uint64_t TypeStorageSize = Context.getTypeSize(FieldTy);
    uint64_t TypeWidth = Context.getIntWidth(FieldTy);
    bool BitfieldIsOverwide = Value.ugt(TypeWidth);

    // Over-wide bitfields are an error in C or when using the MSVC bitfield
    // ABI.
    bool CStdConstraintViolation = BitfieldIsOverwide;
    bool MSBitfieldViolation = Value.ugt(TypeStorageSize) && IsMsStruct;
    if (CStdConstraintViolation || MSBitfieldViolation) {
      unsigned DiagWidth =
          CStdConstraintViolation ? TypeWidth : TypeStorageSize;
      return Diag(FieldLoc, diag::err_bitfield_width_exceeds_type_width)
             << (bool)FieldName << FieldName << toString(Value, 10)
             << !CStdConstraintViolation << DiagWidth;
    }

    // Warn on types where the user might conceivably expect to get all
    // specified bits as value bits: that's all integral types other than
    // 'bool'.
    if (BitfieldIsOverwide && !FieldTy->isBooleanType() && FieldName) {
      Diag(FieldLoc, diag::warn_bitfield_width_exceeds_type_width)
          << FieldName << toString(Value, 10) << (unsigned)TypeWidth;
    }
  }

  return BitWidth;
}

// ===----------------------------------------------------------------------===
// Field declarations
// ===----------------------------------------------------------------------===

Decl *Sema::OnField(Scope *S, Decl *TagD, SourceLocation DeclStart,
                    Declarator &D, Expr *BitfieldWidth) {
  FieldDecl *Res = OnField(S, cast_if_present<RecordDecl>(TagD), DeclStart, D,
                           BitfieldWidth, AS_public);
  return Res;
}

FieldDecl *Sema::OnField(Scope *S, RecordDecl *Record, SourceLocation DeclStart,
                         Declarator &D, Expr *BitWidth, AccessSpecifier AS) {
  IdentifierInfo *II = D.getIdentifier();
  SourceLocation Loc = DeclStart;
  if (II)
    Loc = D.getIdentifierLoc();

  TypeSourceInfo *TInfo = ResolveDeclaratorType(D, S);
  QualType T = TInfo->getType();
  const DeclSpec &FDS = D.getDeclSpec();

  if (FDS.isNoreturnSpecified())
    Diag(FDS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  if (FDS.isInlineSpecified())
    Diag(FDS.getInlineSpecLoc(), diag::err_inline_non_function) << false;
  if (DeclSpec::TSCS TSCS = FDS.getThreadStorageClassSpec())
    Diag(FDS.getThreadStorageClassSpecLoc(), diag::err_invalid_thread)
        << DeclSpec::getSpecifierName(TSCS);

  NamedDecl *PrevDecl = nullptr;
  LookupResult Previous(*this, II, Loc, ResolveMember, ForVisibleRedeclaration);
  ResolveName(Previous, S);
  switch (Previous.getResultKind()) {
  case LookupResult::Found:
    PrevDecl = Previous.getAsSingle<NamedDecl>();
    break;

  case LookupResult::FoundOverloaded:
    PrevDecl = Previous.getRepresentativeDecl();
    break;

  case LookupResult::NotFound:
  case LookupResult::Ambiguous:
    break;
  }
  Previous.suppressDiagnostics();

  if (PrevDecl && !isDeclInScope(PrevDecl, Record, S))
    PrevDecl = nullptr;

  SourceLocation TSSL = D.getBeginLoc();
  FieldDecl *NewFD = CheckFieldDecl(II, T, TInfo, Record, Loc, BitWidth, TSSL,
                                    AS, PrevDecl, &D);

  if (NewFD->isInvalidDecl())
    Record->setInvalidDecl();

  if (NewFD->isInvalidDecl() && PrevDecl) {
    // Don't introduce NewFD into scope; there's already something
    // with the same name in the same scope.
  } else if (II) {
    PushOnScopeChains(NewFD, S);
  } else
    Record->addDecl(NewFD);

  return NewFD;
}

FieldDecl *Sema::CheckFieldDecl(DeclarationName Name, QualType T,
                                TypeSourceInfo *TInfo, RecordDecl *Record,
                                SourceLocation Loc, Expr *BitWidth,
                                SourceLocation TSSL, AccessSpecifier AS,
                                NamedDecl *PrevDecl, Declarator *D) {
  IdentifierInfo *II = Name.getAsIdentifierInfo();
  bool InvalidDecl = false;
  if (D)
    InvalidDecl = D->isInvalidType();

  // If we receive a broken type, recover by assuming 'int' and
  // marking this declaration as invalid.
  if (T.isNull() || T->containsErrors()) {
    InvalidDecl = true;
    T = Context.IntTy;
  }

  QualType EltTy = Context.getBaseElementType(T);
  if (!EltTy->containsErrors()) {
    if (RequireCompleteSizedType(Loc, EltTy,
                                 diag::err_field_incomplete_or_sizeless)) {
      // Fields of incomplete type force their record to be invalid.
      Record->setInvalidDecl();
      InvalidDecl = true;
    } else {
      NamedDecl *Def;
      EltTy->isIncompleteType(&Def);
      if (Def && Def->isInvalidDecl()) {
        Record->setInvalidDecl();
        InvalidDecl = true;
      }
    }
  }

  // TR 18037 does not allow fields to be declared with address space
  if (T.hasAddressSpace()) {
    Diag(Loc, diag::err_field_with_address_space);
    Record->setInvalidDecl();
    InvalidDecl = true;
  }

  // C99 6.7.2.1p8: A member of a structure or union may have any type other
  // than a variably modified type.
  if (!InvalidDecl && T->isVariablyModifiedType()) {
    if (!tryToFixVariablyModifiedVarType(
            TInfo, T, Loc, diag::err_typecheck_field_variable_size))
      InvalidDecl = true;
  }

  if (InvalidDecl)
    BitWidth = nullptr;
  // If this is declared as a bit-field, check the bit-field.
  if (BitWidth) {
    BitWidth =
        VerifyBitField(Loc, II, T, Record->isMsStruct(Context), BitWidth).get();
    if (!BitWidth) {
      InvalidDecl = true;
      BitWidth = nullptr;
    }
  }

  // At most one union variant may have a default initializer.

  FieldDecl *NewFD =
      FieldDecl::Create(Context, Record, TSSL, Loc, II, T, TInfo, BitWidth);
  if (InvalidDecl)
    NewFD->setInvalidDecl();

  if (PrevDecl && !isa<TagDecl>(PrevDecl)) {
    Diag(Loc, diag::err_duplicate_member) << II;
    Diag(PrevDecl->getLocation(), diag::note_previous_declaration);
    NewFD->setInvalidDecl();
  }

  // We need to pass in the attributes given an AST
  // representation, not a parser representation.
  if (D) {
    ApplyDeclAttributes(getCurScope(), NewFD, *D);

    if (NewFD->hasAttrs())
      CheckAlignasUnderalignment(NewFD);
  }

  NewFD->setAccess(AS);
  return NewFD;
}

void Sema::OnFields(Scope *S, SourceLocation RecLoc, Decl *EnclosingDecl,
                    llvm::ArrayRef<Decl *> Fields, SourceLocation LBrac,
                    SourceLocation RBrac, const ParsedAttributesView &Attrs) {
  assert(EnclosingDecl && "missing record or interface decl");

  RecordDecl *Record = dyn_cast<RecordDecl>(EnclosingDecl);
  assert(Record && "EnclosingDecl must be a RecordDecl");
  if (!Record)
    return;
  // Start counting up the number of named members; make sure to include
  // members of anonymous structs and unions in the total.
  unsigned NumNamedMembers = 0;
  for (const auto *I : Record->decls()) {
    if (const auto *IFD = dyn_cast<IndirectFieldDecl>(I))
      if (IFD->getDeclName())
        ++NumNamedMembers;
  }

  for (llvm::ArrayRef<Decl *>::iterator i = Fields.begin(), end = Fields.end();
       i != end; ++i) {
    FieldDecl *FD = cast<FieldDecl>(*i);

    const Type *FDTy = FD->getType().getTypePtr();

    // If the field is already invalid for some reason, don't emit more
    // diagnostics about it.
    if (FD->isInvalidDecl()) {
      EnclosingDecl->setInvalidDecl();
      continue;
    }

    // C99 6.7.2.1p2:
    //   A structure or union shall not contain a member with
    //   incomplete or function type (hence, a structure shall not
    //   contain an instance of itself, but may contain a pointer to
    //   an instance of itself), except that the last member of a
    //   structure with more than one named member may have incomplete
    //   array type; such a structure (and any union containing,
    //   possibly recursively, a member that is such a structure)
    //   shall not be a member of a structure or an element of an
    //   array.
    bool IsLastField = (i + 1 == Fields.end());
    if (FDTy->isFunctionType()) {
      // Field declared as a function.
      Diag(FD->getLocation(), diag::err_field_declared_as_function)
          << FD->getDeclName();
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    } else if (FDTy->isIncompleteArrayType()) {
      // Flexible array member.
      // Microsoft and g++ is more permissive regarding flexible array.
      // It will accept flexible array in union and also
      // as the sole element of a struct.
      unsigned DiagID = 0;
      if (!Record->isUnion() && !IsLastField) {
        Diag(FD->getLocation(), diag::err_flexible_array_not_at_end)
            << FD->getDeclName() << FD->getType()
            << llvm::to_underlying(Record->getTagKind());
        Diag((*(i + 1))->getLocation(), diag::note_next_field_declaration);
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      } else if (Record->isUnion())
        DiagID = getLangOpts().MicrosoftExt ? diag::ext_flexible_array_union_ms
                                            : diag::err_flexible_array_union;
      else if (NumNamedMembers < 1)
        DiagID = getLangOpts().MicrosoftExt
                     ? diag::ext_flexible_array_empty_aggregate_ms
                     : diag::err_flexible_array_empty_aggregate;

      if (DiagID)
        Diag(FD->getLocation(), DiagID)
            << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());
      if (!getLangOpts().C99)
        Diag(FD->getLocation(), diag::ext_c99_flexible_array_member)
            << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());

      // If the element type has non-trivial cleanup, we would not
      // implicitly destroy the elements, so disallow it for now.
      QualType BaseElem = Context.getBaseElementType(FD->getType());
      if (BaseElem.isDestructedType()) {
        Diag(FD->getLocation(), diag::err_flexible_array_has_nontrivial_dtor)
            << FD->getDeclName() << FD->getType();
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      }
      // Okay, we have a legal flexible array member at the end of the struct.
      Record->setHasFlexibleArrayMember(true);
    } else if (RequireCompleteSizedType(
                   FD->getLocation(), FD->getType(),
                   diag::err_field_incomplete_or_sizeless)) {
      // Incomplete type
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    } else if (const RecordType *FDTTy = FDTy->getAs<RecordType>()) {
      if (Record && FDTTy->getDecl()->hasFlexibleArrayMember()) {
        // A type which contains a flexible array member is considered to be a
        // flexible array member.
        Record->setHasFlexibleArrayMember(true);
        if (!Record->isUnion()) {
          // If this is a struct and this is not the last element, reject
          // it.  Note that GCC supports variable sized arrays in the middle of
          // structures.
          if (!IsLastField)
            Diag(FD->getLocation(), diag::ext_variable_sized_type_in_struct)
                << FD->getDeclName() << FD->getType();
          else {
            // We support flexible arrays at the end of structs in
            // other structs as an extension.
            Diag(FD->getLocation(), diag::ext_flexible_array_in_struct)
                << FD->getDeclName();
          }
        }
      }
      if (Record && FDTTy->getDecl()->hasVolatileMember())
        Record->setHasVolatileMember(true);
    }

    if (Record && !shouldIgnoreForRecordTriviality(FD)) {
      QualType FT = FD->getType();
      if (FT.isNonTrivialToPrimitiveDefaultInitialize()) {
        Record->setNonTrivialToPrimitiveDefaultInitialize(true);
        if (FT.hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
            Record->isUnion())
          Record->setHasNonTrivialToPrimitiveDefaultInitializeCUnion(true);
      }
      QualType::PrimitiveCopyKind PCK = FT.isNonTrivialToPrimitiveCopy();
      if (PCK != QualType::PCK_Trivial &&
          PCK != QualType::PCK_VolatileTrivial) {
        Record->setNonTrivialToPrimitiveCopy(true);
        if (FT.hasNonTrivialToPrimitiveCopyCUnion() || Record->isUnion())
          Record->setHasNonTrivialToPrimitiveCopyCUnion(true);
      }
      if (FT.isDestructedType()) {
        Record->setNonTrivialToPrimitiveDestroy(true);
        if (FT.hasNonTrivialToPrimitiveDestructCUnion() || Record->isUnion())
          Record->setHasNonTrivialToPrimitiveDestructCUnion(true);
      }
    }

    if (Record && FD->getType().isVolatileQualified())
      Record->setHasVolatileMember(true);
    // Keep track of the number of named members.
    if (FD->getIdentifier())
      ++NumNamedMembers;
  }

  // Okay, we successfully defined 'Record'.
  if (Record) {
    bool Completed = false;

    if (!Completed)
      Record->completeDefinition();

    ProcessDeclAttributeList(S, Record, Attrs);

    if (Record->hasAttrs()) {
      CheckAlignasUnderalignment(Record);
    }

    {
      bool IsEmpty = true;
      unsigned NonBitFields = 0;
      for (RecordDecl::field_iterator I = Record->field_begin(),
                                      E = Record->field_end();
           NonBitFields == 0 && I != E; ++I) {
        IsEmpty = false;
        if (!I->isUnnamedBitfield())
          ++NonBitFields;
      }

      if (NonBitFields == 0) {
        Diag(RecLoc, IsEmpty ? diag::ext_empty_struct_union
                             : diag::ext_no_named_members_in_struct_union)
            << Record->isUnion();
      }
    }
  }
}

namespace {
bool isRepresentableIntegerValue(TreeContext &Context, llvm::APSInt &Value,
                                 QualType T) {
  assert((T->isIntegralType(Context) || T->isEnumeralType()) &&
         "Integral type required!");
  unsigned BitWidth = Context.getIntWidth(T);

  if (Value.isUnsigned() || Value.isNonNegative()) {
    if (T->isSignedIntegerOrEnumerationType())
      --BitWidth;
    return Value.getActiveBits() <= BitWidth;
  }
  return Value.getSignificantBits() <= BitWidth;
}
} // namespace

// Given an integral type, return the next larger integral type
// (or a NULL type of no such type exists).
namespace {
QualType getNextLargerIntegralType(TreeContext &Context, QualType T) {
  assert((T->isIntegralType(Context) || T->isEnumeralType()) &&
         "Integral type required!");
  const unsigned NumTypes = 4;
  QualType SignedIntegralTypes[NumTypes] = {Context.ShortTy, Context.IntTy,
                                            Context.LongTy, Context.LongLongTy};
  QualType UnsignedIntegralTypes[NumTypes] = {
      Context.UnsignedShortTy, Context.UnsignedIntTy, Context.UnsignedLongTy,
      Context.UnsignedLongLongTy};

  unsigned BitWidth = Context.getTypeSize(T);
  QualType *Types = T->isSignedIntegerOrEnumerationType()
                        ? SignedIntegralTypes
                        : UnsignedIntegralTypes;
  for (unsigned I = 0; I != NumTypes; ++I)
    if (Context.getTypeSize(Types[I]) > BitWidth)
      return Types[I];

  return QualType();
}
} // namespace

// ===----------------------------------------------------------------------===
// Enum constants & enum body
// ===----------------------------------------------------------------------===

EnumConstantDecl *Sema::CheckEnumConstant(EnumDecl *Enum,
                                          EnumConstantDecl *LastEnumConst,
                                          SourceLocation IdLoc,
                                          IdentifierInfo *Id, Expr *Val) {
  unsigned IntWidth = Context.getTargetInfo().getIntWidth();
  llvm::APSInt EnumVal(IntWidth);
  QualType EltTy;

  if (Val)
    Val = DefaultLvalueConversion(Val).get();

  if (Val) {
    if (Val->containsErrors())
      EltTy = Context.DependentTy;
    else {
      if (!(Val = VerifyIntegerConstantExpression(Val, &EnumVal, AllowFold)
                      .get())) {
        // C99 6.7.2.2p2: Make sure we have an integer constant expression.
      } else {
        if (Enum->isComplete()) {
          EltTy = Enum->getIntegerType();

          // MSVC mode: value must fit the fixed underlying type.
          if (!isRepresentableIntegerValue(Context, EnumVal, EltTy)) {
            if (Context.getTargetInfo()
                    .getTriple()
                    .isWindowsMSVCEnvironment()) {
              Diag(IdLoc, diag::ext_enumerator_too_large) << EltTy;
            } else {
              Diag(IdLoc, diag::err_enumerator_too_large) << EltTy;
            }
          }

          // Cast to the underlying type.
          Val = ImpCastExprToType(Val, EltTy,
                                  EltTy->isBooleanType() ? CK_IntegralToBoolean
                                                         : CK_IntegralCast)
                    .get();
        } else {
          // C99 6.7.2.2p2:
          //   The expression that defines the value of an enumeration constant
          //   shall be an integer constant expression that has a value
          //   representable as an int.

          // Complain if the value is not representable in an int.
          if (!isRepresentableIntegerValue(Context, EnumVal, Context.IntTy))
            Diag(IdLoc, diag::ext_enum_value_not_int)
                << toString(EnumVal, 10) << Val->getSourceRange()
                << (EnumVal.isUnsigned() || EnumVal.isNonNegative());
          else if (!Context.hasSameType(Val->getType(), Context.IntTy)) {
            // Force the type of the expression to 'int'.
            Val = ImpCastExprToType(Val, Context.IntTy, CK_IntegralCast).get();
          }
          EltTy = Val->getType();
        }
      }
    }
  }

  if (!Val) {
    if (!LastEnumConst) {
      // First enumerator with no `=`: type is `int` when unfixed (GCC/NeverC;
      // C99 6.7.2.2p3).
      if (Enum->isFixed()) {
        EltTy = Enum->getIntegerType();
      } else {
        EltTy = Context.IntTy;
      }
    } else {
      // Assign the last value + 1.
      EnumVal = LastEnumConst->getInitVal();
      ++EnumVal;
      EltTy = LastEnumConst->getType();

      // Check for overflow on increment.
      if (EnumVal < LastEnumConst->getInitVal()) {
        // Implicit successor: widen integral type if `last+1` does not fit.
        QualType T = getNextLargerIntegralType(Context, EltTy);
        if (T.isNull() || Enum->isFixed()) {
          // There is no integral type larger enough to represent this
          // value. Complain, then allow the value to wrap around.
          EnumVal = LastEnumConst->getInitVal();
          EnumVal = EnumVal.zext(EnumVal.getBitWidth() * 2);
          ++EnumVal;
          if (Enum->isFixed())
            // When the underlying type is fixed, this is ill-formed.
            Diag(IdLoc, diag::err_enumerator_wrapped)
                << toString(EnumVal, 10) << EltTy;
          else
            Diag(IdLoc, diag::ext_enumerator_increment_too_large)
                << toString(EnumVal, 10);
        } else {
          EltTy = T;
        }

        // Retrieve the last enumerator's value, extent that type to the
        // type that is supposed to be large enough to represent the incremented
        // value, then increment.
        EnumVal = LastEnumConst->getInitVal();
        EnumVal.setIsSigned(EltTy->isSignedIntegerOrEnumerationType());
        EnumVal = EnumVal.zextOrTrunc(Context.getIntWidth(EltTy));
        ++EnumVal;

        // C99 6.7.2.2p2 (int representability); GCC allows wider fixed types.
        if (!T.isNull())
          Diag(IdLoc, diag::warn_enum_value_overflow);
      } else if (!isRepresentableIntegerValue(Context, EnumVal, EltTy)) {
        // Enforce C99 6.7.2.2p2 even when we compute the next value.
        Diag(IdLoc, diag::ext_enum_value_not_int) << toString(EnumVal, 10) << 1;
      }
    }
  }

  if (!EltTy.isNull()) {
    // Make the enumerator value match the signedness and size of the
    // enumerator's type.
    EnumVal = EnumVal.extOrTrunc(Context.getIntWidth(EltTy));
    EnumVal.setIsSigned(EltTy->isSignedIntegerOrEnumerationType());
  }

  return EnumConstantDecl::Create(Context, Enum, IdLoc, Id, EltTy, Val,
                                  EnumVal);
}

Decl *Sema::OnEnumConstant(Scope *S, Decl *theEnumDecl, Decl *lastEnumConst,
                           SourceLocation IdLoc, IdentifierInfo *Id,
                           const ParsedAttributesView &Attrs,
                           SourceLocation EqualLoc, Expr *Val) {
  EnumDecl *TheEnumDecl = cast<EnumDecl>(theEnumDecl);
  EnumConstantDecl *LastEnumConst =
      cast_or_null<EnumConstantDecl>(lastEnumConst);

  // The scope passed in may not be a decl scope.  Zip up the scope tree until
  // we find one that is.
  S = getNonFieldDeclScope(S);

  // Verify that there isn't already something declared with this name in this
  // scope.
  LookupResult R(*this, Id, IdLoc, ResolveOrdinary, ForVisibleRedeclaration);
  ResolveName(R, S);
  NamedDecl *PrevDecl = R.getAsSingle<NamedDecl>();

  // Enumerator must not collide with a member name (enum in a struct).
  EnumConstantDecl *New =
      CheckEnumConstant(TheEnumDecl, LastEnumConst, IdLoc, Id, Val);
  if (!New)
    return nullptr;

  if (PrevDecl) {
    if (isa<ValueDecl>(PrevDecl))
      CheckShadow(New, PrevDecl, R);

    assert(!isa<TagDecl>(PrevDecl) &&
           "unexpected TagDecl as previous enumerator decl");
    if (isDeclInScope(PrevDecl, CurContext, S)) {
      // The BuiltinString prelude may have planted an enum constant in
      // <built-in>.  Let the user's real definition silently supersede
      // the synthetic one; drop the old enumerator from scope and from
      // its enclosing enum so later lookups bind to the source-provided
      // constant.
      bool BuiltinPriorEnum = isa<EnumConstantDecl>(PrevDecl) &&
                              Context.getSourceManager().isWrittenInBuiltinFile(
                                  PrevDecl->getLocation());
      if (BuiltinPriorEnum) {
        Scope *EnumScope = getNonFieldDeclScope(S);
        if (EnumScope->isDeclScope(PrevDecl))
          EnumScope->RemoveDecl(PrevDecl);
        IdResolver.RemoveDecl(PrevDecl);
        if (auto *OldEnum =
                dyn_cast<EnumDecl>(PrevDecl->getLexicalDeclContext()))
          OldEnum->removeDecl(PrevDecl);
      } else {
        if (isa<EnumConstantDecl>(PrevDecl))
          Diag(IdLoc, diag::err_redefinition_of_enumerator) << Id;
        else
          Diag(IdLoc, diag::err_redefinition) << Id;
        notePreviousDefinition(PrevDecl, IdLoc);
        return nullptr;
      }
    }
  }

  ProcessDeclAttributeList(S, New, Attrs);
  AddPragmaAttributes(S, New);
  New->setAccess(TheEnumDecl->getAccess());
  PushOnScopeChains(New, S);

  return New;
}

// Returns true when the enum initial expression does not trigger the
// duplicate enum warning.  A few common cases are exempted as follows:
// Element2 = Element1
// Element2 = Element1 + 1
// Element2 = Element1 - 1
// Where Element2 and Element1 are from the same enum.
namespace {
bool validDuplicateEnum(EnumConstantDecl *ECD, EnumDecl *Enum) {
  Expr *InitExpr = ECD->getInitExpr();
  if (!InitExpr)
    return true;
  InitExpr = InitExpr->IgnoreImpCasts();

  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(InitExpr)) {
    if (!BO->isAdditiveOp())
      return true;
    IntegerLiteral *IL = dyn_cast<IntegerLiteral>(BO->getRHS());
    if (!IL)
      return true;
    if (IL->getValue() != 1)
      return true;

    InitExpr = BO->getLHS();
  }

  // This checks if the elements are from the same enum.
  DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(InitExpr);
  if (!DRE)
    return true;

  EnumConstantDecl *EnumConstant = dyn_cast<EnumConstantDecl>(DRE->getDecl());
  if (!EnumConstant)
    return true;

  if (cast<EnumDecl>(TagDecl::castFromDeclContext(ECD->getDeclContext())) !=
      Enum)
    return true;

  return false;
}
} // namespace

// Emits a warning when an element is implicitly set a value that
// a previous element has already been set to.
namespace {
void checkForDuplicateEnumValues(Sema &S, llvm::ArrayRef<Decl *> Elements,
                                 EnumDecl *Enum, QualType EnumType) {
  // Avoid anonymous enums
  if (!Enum->getIdentifier())
    return;

  // Only check for small enums.
  if (Enum->getNumPositiveBits() > 63 || Enum->getNumNegativeBits() > 64)
    return;

  if (S.Diags.isIgnored(diag::warn_duplicate_enum_values, Enum->getLocation()))
    return;

  using ECDVector = llvm::SmallVector<EnumConstantDecl *, 3>;
  using DuplicatesVector = llvm::SmallVector<std::unique_ptr<ECDVector>, 3>;

  using DeclOrVector = llvm::PointerUnion<EnumConstantDecl *, ECDVector *>;

  // DenseMaps cannot contain the all ones int64_t value, so use unordered_map.
  using ValueToVectorMap = std::unordered_map<int64_t, DeclOrVector>;

  // Use int64_t as a key to avoid needing special handling for map keys.
  auto EnumConstantToKey = [](const EnumConstantDecl *D) {
    llvm::APSInt Val = D->getInitVal();
    return Val.isSigned() ? Val.getSExtValue() : Val.getZExtValue();
  };

  DuplicatesVector DupVector;
  ValueToVectorMap EnumMap;

  // Populate the EnumMap with all values represented by enum constants without
  // an initializer.
  for (auto *Element : Elements) {
    EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(Element);

    // Null EnumConstantDecl means a previous diagnostic has been emitted for
    // this constant.  Skip this enum since it may be ill-formed.
    if (!ECD) {
      return;
    }

    // Constants with initializers are handled in the next loop.
    if (ECD->getInitExpr())
      continue;

    // Duplicate values are handled in the next loop.
    EnumMap.insert({EnumConstantToKey(ECD), ECD});
  }

  if (EnumMap.size() == 0)
    return;

  for (auto *Element : Elements) {
    // The last loop returned if any constant was null.
    EnumConstantDecl *ECD = cast<EnumConstantDecl>(Element);
    if (!validDuplicateEnum(ECD, Enum))
      continue;

    auto Iter = EnumMap.find(EnumConstantToKey(ECD));
    if (Iter == EnumMap.end())
      continue;

    DeclOrVector &Entry = Iter->second;
    if (EnumConstantDecl *D = Entry.dyn_cast<EnumConstantDecl *>()) {
      // Ensure constants are different.
      if (D == ECD)
        continue;

      auto Vec = std::make_unique<ECDVector>();
      Vec->push_back(D);
      Vec->push_back(ECD);

      // Update entry to point to the duplicates vector.
      Entry = Vec.get();

      // Store the vector somewhere we can consult later for quick emission of
      // diagnostics.
      DupVector.emplace_back(std::move(Vec));
      continue;
    }

    ECDVector *Vec = Entry.get<ECDVector *>();
    // Make sure constants are not added more than once.
    if (*Vec->begin() == ECD)
      continue;

    Vec->push_back(ECD);
  }

  for (const auto &Vec : DupVector) {
    assert(Vec->size() > 1 && "ECDVector should have at least 2 elements.");

    auto *FirstECD = Vec->front();
    S.Diag(FirstECD->getLocation(), diag::warn_duplicate_enum_values)
        << FirstECD << toString(FirstECD->getInitVal(), 10)
        << FirstECD->getSourceRange();

    for (auto *ECD : llvm::drop_begin(*Vec))
      S.Diag(ECD->getLocation(), diag::note_duplicate_element)
          << ECD << toString(ECD->getInitVal(), 10) << ECD->getSourceRange();
  }
}
} // namespace

bool Sema::IsValueInFlagEnum(const EnumDecl *ED, const llvm::APInt &Val,
                             bool AllowMask) const {
  assert(ED->isClosedFlag() && "looking for value in non-flag or open enum");
  assert(ED->isCompleteDefinition() && "expected enum definition");

  auto R = FlagBitsCache.insert(std::make_pair(ED, llvm::APInt()));
  llvm::APInt &FlagBits = R.first->second;

  if (R.second) {
    for (auto *E : ED->enumerators()) {
      const auto &EVal = E->getInitVal();
      // Only single-bit enumerators introduce new flag values.
      if (EVal.isPowerOf2())
        FlagBits = FlagBits.zext(EVal.getBitWidth()) | EVal;
    }
  }

  // A value is in a flag enum if either its bits are a subset of the enum's
  // flag bits (the first condition) or we are allowing masks and the same is
  // true of its complement (the second condition). When masks are allowed, we
  // allow the common idiom of ~(enum1 | enum2) to be a valid enum value.
  //
  // While it's true that any value could be used as a mask, the assumption is
  // that a mask will have all of the insignificant bits set. Anything else is
  // likely a logic error.
  llvm::APInt FlagMask = ~FlagBits.zextOrTrunc(Val.getBitWidth());
  return !(FlagMask & Val) || (AllowMask && !(FlagMask & ~Val));
}

void Sema::OnEnumBody(SourceLocation EnumLoc, SourceRange BraceRange,
                      Decl *EnumDeclX, llvm::ArrayRef<Decl *> Elements,
                      Scope *S, const ParsedAttributesView &Attrs) {
  EnumDecl *Enum = cast<EnumDecl>(EnumDeclX);
  QualType EnumType = Context.getTypeDeclType(Enum);

  ProcessDeclAttributeList(S, Enum, Attrs);

  // If the result value doesn't fit in an int, it must be a long or long
  // long value.  ISO C does not support this, but GCC does as an extension,
  // emit a warning.
  unsigned IntWidth = Context.getTargetInfo().getIntWidth();
  unsigned CharWidth = Context.getTargetInfo().getCharWidth();
  unsigned ShortWidth = Context.getTargetInfo().getShortWidth();

  // Verify that all the values are okay, compute the size of the values, and
  // reverse the list.
  unsigned NumNegativeBits = 0;
  unsigned NumPositiveBits = 0;

  for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
    EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(Elements[i]);
    if (!ECD)
      continue; // Already issued a diagnostic.

    const llvm::APSInt &InitVal = ECD->getInitVal();

    // Keep track of the size of positive and negative values.
    if (InitVal.isUnsigned() || InitVal.isNonNegative()) {
      // If the enumerator is zero that should still be counted as a positive
      // bit since we need a bit to store the value zero.
      unsigned ActiveBits = InitVal.getActiveBits();
      NumPositiveBits = std::max({NumPositiveBits, ActiveBits, 1u});
    } else {
      NumNegativeBits =
          std::max(NumNegativeBits, (unsigned)InitVal.getSignificantBits());
    }
  }

  // Empty enumerator-list is treated as if it had a single 0-valued constant.
  if (!NumPositiveBits && !NumNegativeBits)
    NumPositiveBits = 1;

  // Figure out the type that should be used for this enum.
  QualType BestType;
  unsigned BestWidth;

  // Pick the smallest promoted integral type that fits all values (GCC
  // -fshort-enums etc.); C99 constants otherwise behave like int.
  QualType BestPromotionType;

  bool Packed = Enum->hasAttr<PackedAttr>();
  // -fshort-enums is the equivalent to specifying the packed attribute on all
  // enum definitions.
  if (LangOpts.ShortEnums)
    Packed = true;

  // If the enum already has a type because it is fixed or dictated by the
  // target, promote that type instead of analyzing the enumerators.
  if (Enum->isComplete()) {
    BestType = Enum->getIntegerType();
    if (Context.isPromotableIntegerType(BestType))
      BestPromotionType = Context.getPromotedIntegerType(BestType);
    else
      BestPromotionType = BestType;

    BestWidth = Context.getIntWidth(BestType);
  } else if (NumNegativeBits) {
    // If there is a negative value, figure out the smallest integer type (of
    // int/long/longlong) that fits.
    // If it's packed, check also if it fits a char or a short.
    if (Packed && NumNegativeBits <= CharWidth && NumPositiveBits < CharWidth) {
      BestType = Context.SignedCharTy;
      BestWidth = CharWidth;
    } else if (Packed && NumNegativeBits <= ShortWidth &&
               NumPositiveBits < ShortWidth) {
      BestType = Context.ShortTy;
      BestWidth = ShortWidth;
    } else if (NumNegativeBits <= IntWidth && NumPositiveBits < IntWidth) {
      BestType = Context.IntTy;
      BestWidth = IntWidth;
    } else {
      BestWidth = Context.getTargetInfo().getLongWidth();

      if (NumNegativeBits <= BestWidth && NumPositiveBits < BestWidth) {
        BestType = Context.LongTy;
      } else {
        BestWidth = Context.getTargetInfo().getLongLongWidth();

        if (NumNegativeBits > BestWidth || NumPositiveBits >= BestWidth)
          Diag(Enum->getLocation(), diag::ext_enum_too_large);
        BestType = Context.LongLongTy;
      }
    }
    BestPromotionType = (BestWidth <= IntWidth ? Context.IntTy : BestType);
  } else {
    // If there is no negative value, figure out the smallest type that fits
    // all of the enumerator values.
    // If it's packed, check also if it fits a char or a short.
    if (Packed && NumPositiveBits <= CharWidth) {
      BestType = Context.UnsignedCharTy;
      BestPromotionType = Context.IntTy;
      BestWidth = CharWidth;
    } else if (Packed && NumPositiveBits <= ShortWidth) {
      BestType = Context.UnsignedShortTy;
      BestPromotionType = Context.IntTy;
      BestWidth = ShortWidth;
    } else if (NumPositiveBits <= IntWidth) {
      BestType = Context.UnsignedIntTy;
      BestWidth = IntWidth;
      BestPromotionType = Context.UnsignedIntTy;
    } else if (NumPositiveBits <=
               (BestWidth = Context.getTargetInfo().getLongWidth())) {
      BestType = Context.UnsignedLongTy;
      BestPromotionType = Context.UnsignedLongTy;
    } else {
      BestWidth = Context.getTargetInfo().getLongLongWidth();
      assert(NumPositiveBits <= BestWidth &&
             "How could an initializer get larger than ULL?");
      BestType = Context.UnsignedLongLongTy;
      BestPromotionType = Context.UnsignedLongLongTy;
    }
  }

  for (auto *D : Elements) {
    auto *ECD = cast_or_null<EnumConstantDecl>(D);
    if (!ECD)
      continue; // Already issued a diagnostic.

    // Standard C says the enumerators have int type, but we allow, as an
    // extension, the enumerators to be larger than int size.  If each
    // enumerator value fits in an int, type it as an int, otherwise type it the
    // same as the enumerator decl itself.  This means that in "enum { X = 1U }"
    // that X has type 'int', not 'unsigned'.

    llvm::APSInt InitVal = ECD->getInitVal();

    // If it fits into an integer type, force it.  Otherwise force it to match
    // the enum decl type.
    QualType NewTy;
    unsigned NewWidth;
    bool NewSign;
    if (!Enum->isFixed() &&
        isRepresentableIntegerValue(Context, InitVal, Context.IntTy)) {
      NewTy = Context.IntTy;
      NewWidth = IntWidth;
      NewSign = true;
    } else if (ECD->getType() == BestType) {
      continue;
    } else {
      NewTy = BestType;
      NewWidth = BestWidth;
      NewSign = BestType->isSignedIntegerOrEnumerationType();
    }

    // Adjust the APSInt value.
    InitVal = InitVal.extOrTrunc(NewWidth);
    InitVal.setIsSigned(NewSign);
    ECD->setInitVal(InitVal);

    // Adjust the Expr initializer and type.
    if (ECD->getInitExpr() &&
        !Context.hasSameType(NewTy, ECD->getInitExpr()->getType()))
      ECD->setInitExpr(ImplicitCastExpr::Create(Context, NewTy, CK_IntegralCast,
                                                ECD->getInitExpr(), VK_PRValue,
                                                FPOptionsOverride()));
    ECD->setType(NewTy);
  }

  Enum->completeDefinition(BestType, BestPromotionType, NumPositiveBits,
                           NumNegativeBits);

  checkForDuplicateEnumValues(*this, Elements, Enum, EnumType);

  if (Enum->isClosedFlag()) {
    for (Decl *D : Elements) {
      EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(D);
      if (!ECD)
        continue; // Already issued a diagnostic.

      llvm::APSInt InitVal = ECD->getInitVal();
      if (InitVal != 0 && !InitVal.isPowerOf2() &&
          !IsValueInFlagEnum(Enum, InitVal, true))
        Diag(ECD->getLocation(), diag::warn_flag_enum_constant_out_of_range)
            << ECD << Enum;
    }
  }

  // Now that the enum type is defined, ensure it's not been underaligned.
  if (Enum->hasAttrs())
    CheckAlignasUnderalignment(Enum);
}

// ===----------------------------------------------------------------------===
// File-scope asm & pragma directives
// ===----------------------------------------------------------------------===

Decl *Sema::OnFileScopeAsmDecl(Expr *expr, SourceLocation StartLoc,
                               SourceLocation EndLoc) {
  StringLiteral *AsmString = cast<StringLiteral>(expr);

  FileScopeAsmDecl *New = FileScopeAsmDecl::Create(Context, CurContext,
                                                   AsmString, StartLoc, EndLoc);
  CurContext->addDecl(New);
  return New;
}

void Sema::OnPragmaWeakID(IdentifierInfo *Name, SourceLocation PragmaLoc,
                          SourceLocation NameLoc) {
  Decl *PrevDecl = LookupSingleName(TUScope, Name, NameLoc, ResolveOrdinary);

  if (PrevDecl) {
    PrevDecl->addAttr(WeakAttr::CreateImplicit(Context, PragmaLoc));
  } else {
    (void)WeakUndeclaredIdentifiers[Name].insert(WeakInfo(nullptr, NameLoc));
  }
}

void Sema::OnPragmaWeakAlias(IdentifierInfo *Name, IdentifierInfo *AliasName,
                             SourceLocation PragmaLoc, SourceLocation NameLoc,
                             SourceLocation AliasNameLoc) {
  Decl *PrevDecl =
      LookupSingleName(TUScope, AliasName, AliasNameLoc, ResolveOrdinary);
  WeakInfo W = WeakInfo(Name, NameLoc);

  if (PrevDecl && (isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl))) {
    if (!PrevDecl->hasAttr<AliasAttr>())
      if (NamedDecl *ND = dyn_cast<NamedDecl>(PrevDecl))
        DeclApplyPragmaWeak(TUScope, ND, W);
  } else {
    (void)WeakUndeclaredIdentifiers[AliasName].insert(W);
  }
}
