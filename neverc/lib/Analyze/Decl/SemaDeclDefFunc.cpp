//===- SemaDeclDefFunc.cpp - Function definition lifecycle ---------------===//

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

NEVERC_HOT Decl *Sema::OnStartOfFunctionDef(Scope *FnBodyScope,
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
  // C23 removed implicit function declarations, so implicitFunctionsAllowed()
  // is false there and callers must not reach this.
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

  // NeverC lets an implicit function declaration through quietly in the
  // standards that still permit one, so `printf("hi")` without <stdio.h> and
  // out-of-order intra-TU calls compile cleanly.  That is a *default*, not a
  // hard-coded silence: both spellings below belong to
  // -Wimplicit-function-declaration and default to Ignored, so raising them
  // with -W/-Werror is what someone who wants the ISO rule enforced does.
  // Suppressing the diagnostic outright instead would take that choice away.
  // C23 never gets here at all -- implicitFunctionsAllowed() is false there,
  // so the call fails in ordinary lookup as an undeclared identifier.
  // `__builtin_*` stays a hard error: misspelling one is essentially always a
  // real bug, never an intentional implicit declaration.
  unsigned diag_id;
  if (II.getName().starts_with("__builtin_"))
    diag_id = diag::warn_builtin_unknown;
  else if (getLangOpts().C99)
    diag_id = diag::ext_implicit_function_decl_c99;
  else
    diag_id = diag::warn_implicit_function_decl;

  TypoCorrection Corrected;
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
  case TST_union:
  case TST_class: {
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
  case TagTypeKind::Class:
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

