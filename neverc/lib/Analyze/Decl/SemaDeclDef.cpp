#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
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

namespace neverc {

bool shouldIgnoreForRecordTriviality(const FieldDecl *FD) {
  return FD->hasAttr<UnavailableAttr>();
}

} // namespace neverc


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

