//===- SemaExprCall.cpp - Call / compound-literal / init-list construction ---===//
//
// Extracted from SemaExpr.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "neverc/Analyze/Overload.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
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
// Call expression construction
// ===----------------------------------------------------------------------===

ExprResult Sema::OnCallExpr(Scope *Scope, Expr *Fn, SourceLocation LParenLoc,
                            MultiExprArg ArgExprs, SourceLocation RParenLoc) {
  // C++: resolve overloaded callee before forming the call.
  if (getLangOpts().CPlusPlus) {
    if (auto *ULE = dyn_cast<UnresolvedLookupExpr>(Fn->IgnoreParens())) {
      OverloadCandidateSet CandidateSet(ULE->getNameLoc());
      for (NamedDecl *D : ULE->decls()) {
        if (auto *FD = dyn_cast<FunctionDecl>(D))
          AddOverloadCandidate(FD, ArgExprs, CandidateSet);
      }
      FunctionDecl *Chosen = nullptr;
      if (OverloadCandidate *Best =
              CandidateSet.BestViableFunction(*this, LParenLoc)) {
        Chosen = Best->Function;
      } else if (!ULE->decls().empty()) {
        // Ambiguous or no viable candidate: recover with the first function.
        Chosen = dyn_cast<FunctionDecl>(ULE->decls().front());
      }
      if (Chosen) {
        Fn = DeclRefExpr::Create(Context, Chosen, ULE->getNameLoc(),
                                 Chosen->getType(), VK_PRValue, Chosen);
      }
    }
  }
  return FormCallExpr(Scope, Fn, LParenLoc, ArgExprs, RParenLoc,
                      /*AllowRecovery=*/true);
}

NEVERC_HOT ExprResult Sema::FormCallExpr(Scope *Scope, Expr *Fn,
                                                   SourceLocation LParenLoc,
                                                   MultiExprArg ArgExprs,
                                                   SourceLocation RParenLoc,
                                                   bool AllowRecovery) {
  ExprResult Result = MaybeConvertParenListExprToParenExpr(Scope, Fn);
  if (LLVM_UNLIKELY(Result.isInvalid()))
    return ExprError();
  Fn = Result.get();

  if (LLVM_UNLIKELY(resolveArgPlaceholders(*this, ArgExprs)))
    return ExprError();

  Expr *NakedFn = Fn->IgnoreParens();

  bool CallingNDeclIndirectly = false;
  NamedDecl *NDecl = nullptr;
  if (LLVM_UNLIKELY(isa<UnaryOperator>(NakedFn))) {
    auto *UnOp = cast<UnaryOperator>(NakedFn);
    if (UnOp->getOpcode() == UO_AddrOf) {
      CallingNDeclIndirectly = true;
      NakedFn = UnOp->getSubExpr()->IgnoreParens();
    }
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(NakedFn)) {
    NDecl = DRE->getDecl();

    FunctionDecl *FDecl = dyn_cast<FunctionDecl>(NDecl);
    if (LLVM_UNLIKELY(FDecl && FDecl->getBuiltinID())) {
      if ((FDecl =
               rewriteBuiltinFunctionDecl(this, Context, FDecl, ArgExprs))) {
        NDecl = FDecl;
        Fn = DeclRefExpr::Create(Context, FDecl, SourceLocation(),
                                 FDecl->getType(), Fn->getValueKind(), FDecl,
                                 DRE->isNonOdrUse());
      }
    }
  } else if (auto *ME = dyn_cast<MemberExpr>(NakedFn))
    NDecl = ME->getMemberDecl();

  if (FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(NDecl)) {
    if (CallingNDeclIndirectly && !checkAddressOfFunctionIsAvailable(
                                      FD, /*Complain=*/true, Fn->getBeginLoc()))
      return ExprError();

    validateDirectCallTarget(*this, Fn, FD, ArgExprs);
  }

  if (Context.isDependenceAllowed() && Fn->isTypeDependent()) {
    return CallExpr::Create(Context, Fn, ArgExprs, Context.DependentTy,
                            VK_PRValue, RParenLoc, CurFPFeatureOverrides());
  }
  return MakeResolvedCallExpr(Fn, NDecl, LParenLoc, ArgExprs, RParenLoc);
}

//  with the specified CallArgs
Expr *Sema::FormBuiltinCallExpr(SourceLocation Loc, Builtin::ID Id,
                                MultiExprArg CallArgs) {
  llvm::StringRef Name = Context.BuiltinInfo.getName(Id);
  LookupResult R(*this, &Context.Idents.get(Name), Loc,
                 neverc::ResolveOrdinary);
  ResolveName(R, TUScope, /*AllowBuiltinCreation=*/true);

  auto *BuiltInDecl = R.getAsSingle<FunctionDecl>();
  assert(BuiltInDecl && "failed to find builtin declaration");

  ExprResult DeclRef =
      MakeDeclRefExpr(BuiltInDecl, BuiltInDecl->getType(), VK_LValue, Loc);
  assert(DeclRef.isUsable() && "Builtin reference cannot fail");

  ExprResult Call =
      FormCallExpr(/*Scope=*/nullptr, DeclRef.get(), Loc, CallArgs, Loc);

  assert(!Call.isInvalid() && "Call to builtin cannot fail!");
  return Call.get();
}

ExprResult Sema::OnConvertVectorExpr(Expr *E, ParsedType ParsedDestTy,
                                     SourceLocation BuiltinLoc,
                                     SourceLocation RParenLoc) {
  TypeSourceInfo *TInfo;
  GetTypeFromParser(ParsedDestTy, &TInfo);
  return SemaConvertVectorExpr(E, TInfo, BuiltinLoc, RParenLoc);
}

ExprResult Sema::MakeResolvedCallExpr(Expr *Fn, NamedDecl *NDecl,
                                      SourceLocation LParenLoc,
                                      llvm::ArrayRef<Expr *> Args,
                                      SourceLocation RParenLoc) {
  FunctionDecl *FDecl = dyn_cast_or_null<FunctionDecl>(NDecl);
  unsigned BuiltinID = (FDecl ? FDecl->getBuiltinID() : 0);

  // Functions with 'interrupt' attribute cannot be called directly.
  if (FDecl && FDecl->hasAttr<AnyX86InterruptAttr>()) {
    Diag(Fn->getExprLoc(), diag::err_anyx86_interrupt_called);
    return ExprError();
  }

  // Interrupt handlers don't save off the VFP regs automatically on AArch64,
  // so there's some risk when calling out to non-interrupt handler functions
  // that the callee might not preserve them. This is easy to diagnose here,
  // but can be very challenging to debug.
  // Likewise, X86 interrupt handlers may only call routines with attribute
  // no_caller_saved_registers since there is no efficient way to
  // save and restore the non-GPR state.
  if (auto *Caller = getCurFunctionDecl()) {
    if (Caller->hasAttr<AnyX86InterruptAttr>() ||
        Caller->hasAttr<AnyX86NoCallerSavedRegistersAttr>()) {
      const TargetInfo &TI = Context.getTargetInfo();
      bool HasNonGPRRegisters =
          TI.hasFeature("sse") || TI.hasFeature("x87") || TI.hasFeature("mmx");
      if (HasNonGPRRegisters &&
          (!FDecl || !FDecl->hasAttr<AnyX86NoCallerSavedRegistersAttr>())) {
        Diag(Fn->getExprLoc(), diag::warn_anyx86_excessive_regsave)
            << (Caller->hasAttr<AnyX86InterruptAttr>() ? 0 : 1);
        if (FDecl)
          Diag(FDecl->getLocation(), diag::note_callee_decl) << FDecl;
      }
    }
  }

  // Promote the function operand.
  // We special-case function promotion here because we only allow promoting
  // builtin functions to function pointers in the callee of a call.
  ExprResult Result;
  QualType ResultTy;
  if (BuiltinID &&
      Fn->getType()->isSpecificBuiltinType(BuiltinType::BuiltinFn)) {
    // Extract the return type from the (builtin) function pointer type.
    // Several builtins still have setType in
    // Sema::CheckBuiltinFunctionCall. One should review their definitions in
    // Builtins.def to ensure they are correct before removing setType calls.
    QualType FnPtrTy = Context.getPointerType(FDecl->getType());
    Result = ImpCastExprToType(Fn, FnPtrTy, CK_BuiltinFnToFnPtr).get();
    ResultTy = FDecl->getCallResultType();
  } else {
    Result = CallExprUnaryConversions(Fn);
    ResultTy = Context.BoolTy;
  }
  if (Result.isInvalid())
    return ExprError();
  Fn = Result.get();

  // Check for a valid function type, but only if it is not a builtin which
  // requires custom type checking. These will be handled by
  // CheckBuiltinFunctionCall below just after creation of the call expression.
  const FunctionType *FuncT = nullptr;
  if (!BuiltinID || !Context.BuiltinInfo.hasCustomTypechecking(BuiltinID)) {
    if (const PointerType *PT = Fn->getType()->getAs<PointerType>()) {
      // C99 6.5.2.2p1 - "The expression that denotes the called function shall
      // have type pointer to function".
      FuncT = PT->getPointeeType()->getAs<FunctionType>();
      if (!FuncT)
        return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                         << Fn->getType() << Fn->getSourceRange());
    } else {
      return ExprError(Diag(LParenLoc, diag::err_typecheck_call_not_function)
                       << Fn->getType() << Fn->getSourceRange());
    }
  }

  const auto *Proto = dyn_cast_or_null<FunctionProtoType>(FuncT);
  unsigned NumParams = Proto ? Proto->getNumParams() : 0;

  CallExpr *TheCall;
  TheCall = CallExpr::Create(Context, Fn, Args, ResultTy, VK_PRValue, RParenLoc,
                             CurFPFeatureOverrides(), NumParams);

  if (!Context.isDependenceAllowed()) {
    // Forget about the nulled arguments since typo correction
    // do not handle them well.
    TheCall->shrinkNumArgs(Args.size());
    Args = llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs());
    TheCall->setNumArgsUnsafe(std::max<unsigned>(Args.size(), NumParams));
  }

  // Bail out early if calling a builtin with custom type checking.
  if (BuiltinID && Context.BuiltinInfo.hasCustomTypechecking(BuiltinID))
    return CheckBuiltinFunctionCall(FDecl, BuiltinID, TheCall);
  if (CheckCallReturnType(FuncT->getReturnType(), Fn->getBeginLoc(), TheCall,
                          FDecl))
    return ExprError();

  // We know the result type of the call, set it.
  TheCall->setType(FuncT->getCallResultType(Context));
  TheCall->setValueKind(VK_PRValue);

  if (Proto) {
    if (ConvertArgumentsForCall(TheCall, Fn, FDecl, Proto, Args, RParenLoc))
      return ExprError();
  } else {
    assert(isa<FunctionNoProtoType>(FuncT) && "Unknown FunctionType!");

    if (FDecl) {
      const FunctionDecl *Def = nullptr;
      if (FDecl->hasBody(Def) && Args.size() != Def->param_size()) {
        Proto = Def->getType()->getAs<FunctionProtoType>();
        if (!Proto ||
            !(Proto->isVariadic() && Args.size() >= Def->param_size()))
          Diag(RParenLoc, diag::warn_call_wrong_number_of_arguments)
              << (Args.size() > Def->param_size()) << FDecl
              << Fn->getSourceRange();
      }

      // If the function we're calling isn't a function prototype, but we have
      // a function prototype from a prior declaratiom, use that prototype.
      if (!FDecl->hasPrototype())
        Proto = FDecl->getType()->getAs<FunctionProtoType>();
    }

    // If we still haven't found a prototype to use but there are arguments to
    // the call, diagnose this as calling a function without a prototype.
    // However, if we found a function declaration, check to see if
    // -Wdeprecated-non-prototype was disabled where the function was declared.
    // If so, we will silence the diagnostic here on the assumption that this
    // interface is intentional and the user knows what they're doing. We will
    // also silence the diagnostic if there is a function declaration but it
    // was implicitly defined (the user already gets diagnostics about the
    // creation of the implicit function declaration, so the additional warning
    // is not helpful).
    if (!Proto && !Args.empty() &&
        (!FDecl || (!FDecl->isImplicit() &&
                    !Diags.isIgnored(diag::warn_strict_uses_without_prototype,
                                     FDecl->getLocation()))))
      Diag(LParenLoc, diag::warn_strict_uses_without_prototype)
          << (FDecl != nullptr) << FDecl;

    // Promote the arguments (C99 6.5.2.2p6).
    for (unsigned i = 0, e = Args.size(); i != e; i++) {
      Expr *Arg = Args[i];

      if (Proto && i < Proto->getNumParams()) {
        InitializedEntity Entity = InitializedEntity::InitializeParameter(
            Context, Proto->getParamType(i), false);
        ExprResult ArgE =
            PerformCopyInitialization(Entity, SourceLocation(), Arg);
        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.getAs<Expr>();

      } else {
        if (FDecl) {
          const FunctionDecl *Def = nullptr;
          if (FDecl->hasBody(Def) || FDecl->isDefined(Def)) {
            if (const auto *DefProto =
                    Def->getType()->getAs<FunctionProtoType>()) {
              if (i < DefProto->getNumParams() &&
                  this->isNeverCStringType(DefProto->getParamType(i))) {
                if (StringLiteral *SL = getNeverCStringLiteral(Arg)) {
                  ExprResult LV = buildNeverCStringLiteral(
                      *this, DefProto->getParamType(i), Arg, SL);
                  if (!LV.isInvalid())
                    Arg = LV.get();
                }
              }
            }
          }
        }
        ExprResult ArgE = DefaultArgumentPromotion(Arg);

        if (ArgE.isInvalid())
          return true;

        Arg = ArgE.getAs<Expr>();
      }

      if (RequireCompleteType(Arg->getBeginLoc(), Arg->getType(),
                              diag::err_call_incomplete_argument, Arg))
        return ExprError();

      TheCall->setArg(i, Arg);
    }
    TheCall->computeDependence();
  }
  if (NDecl)
    CheckSentinelArgs(NDecl, LParenLoc, Args);

  // Do special checking on direct calls to functions.
  if (FDecl) {
    if (CheckFunctionCall(FDecl, TheCall, Proto))
      return ExprError();

    checkFortifiedBuiltinMemoryFunction(FDecl, TheCall);

    if (BuiltinID)
      return CheckBuiltinFunctionCall(FDecl, BuiltinID, TheCall);
  } else if (NDecl) {
    if (CheckPointerCall(NDecl, TheCall, Proto))
      return ExprError();
  } else {
    if (CheckOtherCall(TheCall, Proto))
      return ExprError();
  }

  return MaybeBindToTemporary(TheCall);
}

ExprResult Sema::OnCompoundLiteral(SourceLocation LParenLoc, ParsedType Ty,
                                   SourceLocation RParenLoc, Expr *InitExpr) {
  assert(Ty && "OnCompoundLiteral(): missing type");
  assert(InitExpr && "OnCompoundLiteral(): missing expression");

  TypeSourceInfo *TInfo;
  QualType literalType = GetTypeFromParser(Ty, &TInfo);
  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(literalType);

  return FormCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, InitExpr);
}

ExprResult Sema::FormCompoundLiteralExpr(SourceLocation LParenLoc,
                                         TypeSourceInfo *TInfo,
                                         SourceLocation RParenLoc,
                                         Expr *LiteralExpr) {
  QualType literalType = TInfo->getType();

  if (literalType->isArrayType()) {
    if (RequireCompleteSizedType(
            LParenLoc, Context.getBaseElementType(literalType),
            diag::err_array_incomplete_or_sizeless_type,
            SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd())))
      return ExprError();
    if (literalType->isVariableArrayType()) {
      // C23 6.7.10p4: An entity of variable length array type shall not be
      // initialized except by an empty initializer.
      //
      // Brace-init diagnostics for VLAs are handled in ParseBraceInitializer();
      // here we reject a non-empty initializer on a VLA compound literal.
      std::optional<unsigned> NumInits;
      if (const auto *ILE = dyn_cast<InitListExpr>(LiteralExpr))
        NumInits = ILE->getNumInits();
      if (NumInits.value_or(0) &&
          !tryToFixVariablyModifiedVarType(TInfo, literalType, LParenLoc,
                                           diag::err_variable_object_no_init))
        return ExprError();
    }
  } else if (RequireCompleteType(
                 LParenLoc, literalType,
                 diag::err_typecheck_decl_incomplete_type,
                 SourceRange(LParenLoc,
                             LiteralExpr->getSourceRange().getEnd())))
    return ExprError();

  InitializedEntity Entity =
      InitializedEntity::InitializeCompoundLiteralInit(TInfo);
  InitializationKind Kind = InitializationKind::CreateCStyleCast(
      LParenLoc, SourceRange(LParenLoc, RParenLoc),
      /*InitList=*/true);
  InitializationSequence InitSeq(*this, Entity, Kind, LiteralExpr);
  ExprResult Result =
      InitSeq.Perform(*this, Entity, Kind, LiteralExpr, &literalType);
  if (Result.isInvalid())
    return ExprError();
  LiteralExpr = Result.get();

  bool isFileScope = !CurContext->isFunctionOrMethod();

  // In C, compound literals are l-values.
  ExprValueKind VK = VK_LValue;

  if (isFileScope)
    if (auto ILE = dyn_cast<InitListExpr>(LiteralExpr))
      for (unsigned i = 0, j = ILE->getNumInits(); i != j; i++) {
        Expr *Init = ILE->getInit(i);
        ILE->setInit(i, ConstantExpr::Create(Context, Init));
      }

  auto *E = new (Context) CompoundLiteralExpr(LParenLoc, TInfo, literalType, VK,
                                              LiteralExpr, isFileScope);
  if (isFileScope) {
    if (CheckForConstantInitializer(LiteralExpr, literalType)) // C99 6.5.2.5p3
      return ExprError();
  } else if (literalType.getAddressSpace() != LangAS::Default) {
    // Embedded-C extensions to C99 6.5.2.5:
    //   "If the compound literal occurs inside the body of a function, the
    //   type name shall not be qualified by an address-space qualifier."
    Diag(LParenLoc, diag::err_compound_literal_with_address_space)
        << SourceRange(LParenLoc, LiteralExpr->getSourceRange().getEnd());
    return ExprError();
  }

  if (!isFileScope) {
    // Block-scope compound literals have automatic storage duration (C99
    // 6.5.2.5).

    // Diagnose jumps that enter or exit the lifetime of the compound literal.
    if (literalType.isDestructedType()) {
      Cleanup.setExprNeedsCleanups(true);
      ExprCleanupObjects.push_back(E);
      getCurFunction()->setHasBranchProtectedScope();
    }
  }

  return MaybeBindToTemporary(E);
}

ExprResult Sema::OnInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                            SourceLocation RBraceLoc) {
  // Only produce each kind of designated initialization diagnostic once.
  SourceLocation FirstDesignator;
  for (unsigned I = 0, E = InitArgList.size(); I != E; ++I) {
    if (auto *DIE = dyn_cast<DesignatedInitExpr>(InitArgList[I])) {
      if (FirstDesignator.isInvalid())
        FirstDesignator = DIE->getBeginLoc();
      break;
    }
  }

  if (FirstDesignator.isValid()) {
    if (!getLangOpts().C99)
      Diag(FirstDesignator, diag::ext_designated_init);
  }

  return FormInitList(LBraceLoc, InitArgList, RBraceLoc);
}

ExprResult Sema::FormInitList(SourceLocation LBraceLoc,
                              MultiExprArg InitArgList,
                              SourceLocation RBraceLoc) {
  // Semantic analysis for initializers is done by OnDeclarator() and
  // CheckInitializer() - it requires knowledge of the object being initialized.

  // Immediately handle non-overload placeholders.  Overloads can be
  // resolved contextually, but everything else here can't.
  for (unsigned I = 0, E = InitArgList.size(); I != E; ++I) {
    if (InitArgList[I]->getType()->isNonOverloadPlaceholderType()) {
      ExprResult result = CheckPlaceholderExpr(InitArgList[I]);

      // Ignore failures; dropping the entire initializer list because
      // of one failure would be terrible for indexing/etc.
      if (result.isInvalid())
        continue;

      InitArgList[I] = result.get();
    }
  }

  InitListExpr *E =
      new (Context) InitListExpr(Context, LBraceLoc, InitArgList, RBraceLoc);
  E->setType(Context.VoidTy);
  return E;
}
