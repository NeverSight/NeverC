#include "Expr/TreeTransform.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/Overload.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

using namespace neverc;

// EmptyDeclaration / StaticAssertDeclaration / static-assert message
// evaluation moved to `SemaC.cpp` so this file is strictly about
// implicit / standard conversion sequences and condition variables.

using namespace sema;

// ===----------------------------------------------------------------------===
// Condition variables
// ===----------------------------------------------------------------------===

Sema::ConditionResult Sema::OnConditionVariable(Decl *ConditionVar,
                                                SourceLocation StmtLoc,
                                                ConditionKind CK) {
  ExprResult E =
      CheckConditionVariable(cast<VarDecl>(ConditionVar), StmtLoc, CK);
  if (E.isInvalid())
    return ConditionError();
  return ConditionResult(*this, ConditionVar, MakeFullExpr(E.get(), StmtLoc),
                         false);
}

ExprResult Sema::CheckConditionVariable(VarDecl *ConditionVar,
                                        SourceLocation StmtLoc,
                                        ConditionKind CK) {
  if (ConditionVar->isInvalidDecl())
    return ExprError();

  QualType T = ConditionVar->getType();

  // Condition variable type must not be function or array type.
  if (T->isFunctionType())
    return ExprError(Diag(ConditionVar->getLocation(),
                          diag::err_invalid_use_of_function_type)
                     << ConditionVar->getSourceRange());
  else if (T->isArrayType())
    return ExprError(
        Diag(ConditionVar->getLocation(), diag::err_invalid_use_of_array_type)
        << ConditionVar->getSourceRange());

  ExprResult Condition =
      MakeDeclRefExpr(ConditionVar, ConditionVar->getType(), VK_LValue,
                      ConditionVar->getLocation());

  switch (CK) {
  case ConditionKind::Boolean:
    return CheckBooleanCondition(StmtLoc, Condition.get());

  case ConditionKind::Switch:
    return CheckSwitchCondition(StmtLoc, Condition.get());
  }

  llvm_unreachable("unexpected condition kind");
}

bool Sema::IsStringLiteralToNonConstPointerConversion(Expr *From,
                                                      QualType ToType) {
  if (auto *Cast = dyn_cast<ImplicitCastExpr>(From))
    From = Cast->getSubExpr();

  // Narrow string literal to pointer to char; wide literal to pointer to
  // wchar_t, when the target type matches.
  if (StringLiteral *StrLit = dyn_cast<StringLiteral>(From->IgnoreParens()))
    if (const PointerType *ToPtrType = ToType->getAs<PointerType>())
      if (const BuiltinType *ToPointeeType =
              ToPtrType->getPointeeType()->getAs<BuiltinType>()) {
        // Only when the pointee type matches the string literal kind.
        if (!ToPtrType->getPointeeType().hasQualifiers()) {
          switch (StrLit->getKind()) {
          case StringLiteralKind::UTF8:
          case StringLiteralKind::UTF16:
          case StringLiteralKind::UTF32:
            // We don't allow UTF literals to be implicitly converted
            break;
          case StringLiteralKind::Ordinary:
            return (ToPointeeType->getKind() == BuiltinType::Char_U ||
                    ToPointeeType->getKind() == BuiltinType::Char_S);
          case StringLiteralKind::Wide:
            return Context.typesAreCompatible(Context.getWideCharType(),
                                              QualType(ToPointeeType, 0));
          case StringLiteralKind::Unevaluated:
            assert(false && "Unevaluated string literal in expression");
            break;
          }
        }
      }

  return false;
}

// ===----------------------------------------------------------------------===
// Implicit conversions
// ===----------------------------------------------------------------------===

ExprResult Sema::PerformImplicitConversion(
    Expr *From, QualType ToType, const ImplicitConversionSequence &ICS,
    AssignmentAction Action, CheckedConversionKind CCK) {
  switch (ICS.getKind()) {
  case ImplicitConversionSequence::StandardConversion: {
    ExprResult Res = PerformImplicitConversion(From, ToType, ICS.Standard, CCK);
    if (Res.isInvalid())
      return ExprError();
    From = Res.get();
    break;
  }

  case ImplicitConversionSequence::UserDefinedConversion: {
    // NeverC Phase-4: apply unique conversion operator if present on the
    // source class type; otherwise fall back to the embedded standard seq.
    if (getLangOpts().CPlusPlus && From && !From->getType().isNull()) {
      QualType FromTy = From->getType();
      if (const Type *T = FromTy.getTypePtrOrNull()) {
        if (T->isReferenceType())
          FromTy = T->getPointeeType();
      }
      if (const CXXRecordDecl *RD = FromTy->getAsCXXRecordDecl()) {
        const CXXRecordDecl *Def =
            RD->getDefinition() ? RD->getDefinition() : RD;
        CXXConversionDecl *Found = nullptr;
        unsigned N = 0;
        for (Decl *D : Def->decls()) {
          auto *CD = dyn_cast<CXXConversionDecl>(D);
          if (!CD)
            continue;
          QualType CT = CD->getConversionType();
          if (CT.isNull())
            continue;
          // Exact match or both pointers / arithmetic left to later ranking.
          if (Context.hasSameUnqualifiedType(CT, ToType) ||
              Context.hasSameType(CT, ToType)) {
            Found = CD;
            ++N;
          }
        }
        if (N == 1 && Found) {
          Expr *Callee = DeclRefExpr::Create(
              Context, Found, From->getExprLoc(), Found->getType(), VK_LValue);
          Expr *Call = new (Context) CXXOperatorCallExpr(
              ToType, From->getExprLoc(), Callee, From, nullptr);
          return Call;
        }
      }
    }
    // Fallback: try standard sequence stored alongside (scaffold ICS).
    {
      ExprResult Res =
          PerformImplicitConversion(From, ToType, ICS.Standard, CCK);
      if (Res.isInvalid())
        return ExprError();
      return Res.get();
    }
  }
  case ImplicitConversionSequence::EllipsisConversion:
    // Ellipsis conversion is only for overload ranking; not a real convert.
    return ExprError();
  case ImplicitConversionSequence::BadConversion: {
    Sema::AssignConvertType ConvTy =
        CheckAssignmentConstraints(From->getExprLoc(), ToType, From->getType());
    bool Diagnosed = DiagnoseAssignmentResult(
        ConvTy == Compatible ? Incompatible : ConvTy, From->getExprLoc(),
        ToType, From->getType(), From, Action);
    assert(Diagnosed && "failed to diagnose bad conversion");
    (void)Diagnosed;
    return ExprError();
  }
  }

  return From;
}

ExprResult
Sema::PerformImplicitConversion(Expr *From, QualType ToType,
                                const StandardConversionSequence &SCS,
                                CheckedConversionKind CCK) {
  bool CStyle = (CCK == CCK_CStyleCast);

  // We are recomputing too many types here and doing far too
  // much extra work. What this means is that we need to keep track of more
  // information that is computed when we try the implicit conversion initially,
  // so that we don't need to recompute anything here.
  QualType FromType = From->getType();

  // If we're converting to an atomic type, first convert to the corresponding
  // non-atomic type.
  QualType ToAtomicType;
  if (const AtomicType *ToAtomic = ToType->getAs<AtomicType>()) {
    ToAtomicType = ToType;
    ToType = ToAtomic->getValueType();
  }

  QualType InitialFromType = FromType;
  // Perform the first implicit conversion.
  switch (SCS.First) {
  case ICK_Identity:
    if (const AtomicType *FromAtomic = FromType->getAs<AtomicType>()) {
      FromType = FromAtomic->getValueType().getUnqualifiedType();
      From = ImplicitCastExpr::Create(Context, FromType, CK_AtomicToNonAtomic,
                                      From, VK_PRValue, FPOptionsOverride());
    }
    break;

  case ICK_Lvalue_To_Rvalue: {
    ExprResult FromRes = DefaultLvalueConversion(From);
    if (FromRes.isInvalid())
      return ExprError();

    From = FromRes.get();
    FromType = From->getType();
    break;
  }

  case ICK_Array_To_Pointer:
    FromType = Context.getArrayDecayedType(FromType);
    From = ImpCastExprToType(From, FromType, CK_ArrayToPointerDecay, VK_PRValue,
                             CCK)
               .get();
    break;

  case ICK_Function_To_Pointer:
    FromType = Context.getPointerType(FromType);
    From = ImpCastExprToType(From, FromType, CK_FunctionToPointerDecay,
                             VK_PRValue, CCK)
               .get();
    break;

  default:
    llvm_unreachable("Improper first standard conversion");
  }

  // Perform the second implicit conversion
  switch (SCS.Second) {
  case ICK_Identity:
    break;

  case ICK_Integral_Promotion:
  case ICK_Integral_Conversion:
    if (ToType->isBooleanType()) {
      assert(FromType->castAs<EnumType>()->getDecl()->isFixed() &&
             SCS.Second == ICK_Integral_Promotion &&
             "only enums with fixed underlying type can promote to bool");
      From =
          ImpCastExprToType(From, ToType, CK_IntegralToBoolean, VK_PRValue, CCK)
              .get();
    } else {
      From = ImpCastExprToType(From, ToType, CK_IntegralCast, VK_PRValue, CCK)
                 .get();
    }
    break;

  case ICK_Floating_Promotion:
  case ICK_Floating_Conversion:
    From =
        ImpCastExprToType(From, ToType, CK_FloatingCast, VK_PRValue, CCK).get();
    break;

  case ICK_Complex_Promotion:
  case ICK_Complex_Conversion: {
    QualType FromEl = From->getType()->castAs<ComplexType>()->getElementType();
    QualType ToEl = ToType->castAs<ComplexType>()->getElementType();
    CastKind CK;
    if (FromEl->isRealFloatingType()) {
      if (ToEl->isRealFloatingType())
        CK = CK_FloatingComplexCast;
      else
        CK = CK_FloatingComplexToIntegralComplex;
    } else if (ToEl->isRealFloatingType()) {
      CK = CK_IntegralComplexToFloatingComplex;
    } else {
      CK = CK_IntegralComplexCast;
    }
    From = ImpCastExprToType(From, ToType, CK, VK_PRValue, CCK).get();
    break;
  }

  case ICK_Floating_Integral:
    if (ToType->isRealFloatingType())
      From = ImpCastExprToType(From, ToType, CK_IntegralToFloating, VK_PRValue,
                               CCK)
                 .get();
    else
      From = ImpCastExprToType(From, ToType, CK_FloatingToIntegral, VK_PRValue,
                               CCK)
                 .get();
    break;

  case ICK_Fixed_Point_Conversion:
    assert((FromType->isFixedPointType() || ToType->isFixedPointType()) &&
           "Attempting implicit fixed point conversion without a fixed "
           "point operand");
    if (FromType->isFloatingType())
      From = ImpCastExprToType(From, ToType, CK_FloatingToFixedPoint,
                               VK_PRValue, CCK)
                 .get();
    else if (ToType->isFloatingType())
      From = ImpCastExprToType(From, ToType, CK_FixedPointToFloating,
                               VK_PRValue, CCK)
                 .get();
    else if (FromType->isIntegralType(Context))
      From = ImpCastExprToType(From, ToType, CK_IntegralToFixedPoint,
                               VK_PRValue, CCK)
                 .get();
    else if (ToType->isIntegralType(Context))
      From = ImpCastExprToType(From, ToType, CK_FixedPointToIntegral,
                               VK_PRValue, CCK)
                 .get();
    else if (ToType->isBooleanType())
      From = ImpCastExprToType(From, ToType, CK_FixedPointToBoolean, VK_PRValue,
                               CCK)
                 .get();
    else
      From = ImpCastExprToType(From, ToType, CK_FixedPointCast, VK_PRValue, CCK)
                 .get();
    break;

  case ICK_Compatible_Conversion:
    From = ImpCastExprToType(From, ToType, CK_NoOp, From->getValueKind(), CCK)
               .get();
    break;

  case ICK_Pointer_Conversion: {
    // Defer address space conversion to the third conversion.
    QualType FromPteeType = From->getType()->getPointeeType();
    QualType ToPteeType = ToType->getPointeeType();
    QualType NewToType = ToType;
    if (!FromPteeType.isNull() && !ToPteeType.isNull() &&
        FromPteeType.getAddressSpace() != ToPteeType.getAddressSpace()) {
      NewToType = Context.removeAddrSpaceQualType(ToPteeType);
      NewToType = Context.getAddrSpaceQualType(NewToType,
                                               FromPteeType.getAddressSpace());
      NewToType = Context.getPointerType(NewToType);
    }

    CastKind Kind;
    if (CheckPointerConversion(From, NewToType, Kind, CStyle))
      return ExprError();

    From = ImpCastExprToType(From, NewToType, Kind, VK_PRValue, CCK).get();
    break;
  }

  case ICK_Boolean_Conversion:
    // Perform half-to-boolean conversion via float.
    if (From->getType()->isHalfType()) {
      From = ImpCastExprToType(From, Context.FloatTy, CK_FloatingCast).get();
      FromType = Context.FloatTy;
    }

    From = ImpCastExprToType(From, Context.BoolTy,
                             ScalarTypeToBooleanCastKind(FromType), VK_PRValue,
                             CCK)
               .get();
    break;

  case ICK_Vector_Conversion:
    From = ImpCastExprToType(From, ToType, CK_BitCast, VK_PRValue, CCK).get();
    break;

  case ICK_Vector_Splat: {
    // Vector splat from any arithmetic type to a vector.
    Expr *Elem = prepareVectorSplat(ToType, From).get();
    From =
        ImpCastExprToType(Elem, ToType, CK_VectorSplat, VK_PRValue, CCK).get();
    break;
  }

  case ICK_Complex_Real:
    // Case 1.  x -> _Complex y
    if (const ComplexType *ToComplex = ToType->getAs<ComplexType>()) {
      QualType ElType = ToComplex->getElementType();
      bool isFloatingComplex = ElType->isRealFloatingType();

      // x -> y
      if (Context.hasSameUnqualifiedType(ElType, From->getType())) {
        // do nothing
      } else if (From->getType()->isRealFloatingType()) {
        From = ImpCastExprToType(From, ElType,
                                 isFloatingComplex ? CK_FloatingCast
                                                   : CK_FloatingToIntegral)
                   .get();
      } else {
        assert(From->getType()->isIntegerType());
        From = ImpCastExprToType(From, ElType,
                                 isFloatingComplex ? CK_IntegralToFloating
                                                   : CK_IntegralCast)
                   .get();
      }
      // y -> _Complex y
      From = ImpCastExprToType(From, ToType,
                               isFloatingComplex ? CK_FloatingRealToComplex
                                                 : CK_IntegralRealToComplex)
                 .get();

      // Case 2.  _Complex x -> y
    } else {
      auto *FromComplex = From->getType()->castAs<ComplexType>();
      QualType ElType = FromComplex->getElementType();
      bool isFloatingComplex = ElType->isRealFloatingType();

      // _Complex x -> x
      From = ImpCastExprToType(From, ElType,
                               isFloatingComplex ? CK_FloatingComplexToReal
                                                 : CK_IntegralComplexToReal,
                               VK_PRValue, CCK)
                 .get();

      // x -> y
      if (Context.hasSameUnqualifiedType(ElType, ToType)) {
        // do nothing
      } else if (ToType->isRealFloatingType()) {
        From = ImpCastExprToType(From, ToType,
                                 isFloatingComplex ? CK_FloatingCast
                                                   : CK_IntegralToFloating,
                                 VK_PRValue, CCK)
                   .get();
      } else {
        assert(ToType->isIntegerType());
        From = ImpCastExprToType(From, ToType,
                                 isFloatingComplex ? CK_FloatingToIntegral
                                                   : CK_IntegralCast,
                                 VK_PRValue, CCK)
                   .get();
      }
    }
    break;

  case ICK_TransparentUnionConversion: {
    ExprResult FromRes = From;
    Sema::AssignConvertType ConvTy =
        CheckTransparentUnionArgumentConstraints(ToType, FromRes);
    if (FromRes.isInvalid())
      return ExprError();
    From = FromRes.get();
    assert((ConvTy == Sema::Compatible) &&
           "Improper transparent union conversion");
    (void)ConvTy;
    break;
  }

  default:
    llvm_unreachable("Improper second standard conversion");
  }

  switch (SCS.Third) {
  case ICK_Identity:
    break;

  case ICK_Function_Conversion:
    From = ImpCastExprToType(From, ToType, CK_NoOp, VK_PRValue, CCK).get();
    break;

  case ICK_Qualification: {
    ExprValueKind VK = From->getValueKind();
    CastKind CK = CK_NoOp;

    if (ToType->isPointerType() &&
        ToType->getPointeeType().getAddressSpace() !=
            From->getType()->getPointeeType().getAddressSpace())
      CK = CK_AddressSpaceConversion;

    if (!isCast(CCK) &&
        !ToType->getPointeeType().getQualifiers().hasUnaligned() &&
        From->getType()->getPointeeType().getQualifiers().hasUnaligned()) {
      Diag(From->getBeginLoc(), diag::warn_imp_cast_drops_unaligned)
          << InitialFromType << ToType;
    }

    From = ImpCastExprToType(From, ToType.getNonLValueExprType(Context), CK, VK,
                             CCK)
               .get();
#ifndef _WIN32
    if (SCS.DeprecatedStringLiteralToCharPtr &&
        !getLangOpts().WritableStrings) {
      Diag(From->getBeginLoc(), diag::warn_deprecated_string_literal_conversion)
          << ToType;
    }
#endif
    break;
  }

  default:
    llvm_unreachable("Improper third standard conversion");
  }

  // If this conversion sequence involved a scalar -> atomic conversion, perform
  // that conversion now.
  if (!ToAtomicType.isNull()) {
    assert(Context.hasSameType(
        ToAtomicType->castAs<AtomicType>()->getValueType(), From->getType()));
    From = ImpCastExprToType(From, ToAtomicType, CK_NonAtomicToAtomic,
                             VK_PRValue, CCK)
               .get();
  }

  // If this conversion sequence succeeded and involved implicitly converting a
  // _Nullable type to a _Nonnull one, complain.
  if (!isCast(CCK))
    diagnoseNullableToNonnullConversion(ToType, InitialFromType,
                                        From->getBeginLoc());

  return From;
}

ExprResult Sema::MaybeBindToTemporary(Expr *E) {
  if (!E)
    return ExprError();

  // If the result is an lvalue, we shouldn't bind it.
  if (E->isLValue())
    return E;

  if (E->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
    Cleanup.setExprNeedsCleanups(true);

  return E;
}

ExprResult Sema::MaybeCreateExprWithCleanups(ExprResult SubExpr) {
  if (SubExpr.isInvalid())
    return ExprError();

  return MaybeCreateExprWithCleanups(SubExpr.get());
}

Expr *Sema::MaybeCreateExprWithCleanups(Expr *SubExpr) {
  assert(SubExpr && "subexpression can't be null!");

  unsigned FirstCleanup = ExprEvalContexts.back().NumCleanupObjects;
  assert(ExprCleanupObjects.size() >= FirstCleanup);
  assert(Cleanup.exprNeedsCleanups() ||
         ExprCleanupObjects.size() == FirstCleanup);
  if (!Cleanup.exprNeedsCleanups())
    return SubExpr;

  auto Cleanups = llvm::ArrayRef(ExprCleanupObjects.begin() + FirstCleanup,
                                 ExprCleanupObjects.size() - FirstCleanup);

  auto *E = ExprWithCleanups::Create(
      Context, SubExpr, Cleanup.cleanupsHaveSideEffects(), Cleanups);
  DiscardCleanupsInEvaluationContext();

  return E;
}

namespace {
void maybeDecrementCount(
    Expr *E, llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  DeclRefExpr *LHS = nullptr;
  bool IsCompoundAssign = false;
  bool isIncrementDecrementUnaryOp = false;
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (!BO->isAssignmentOp())
      return;
    else
      IsCompoundAssign = BO->isCompoundAssignmentOp();
    LHS = dyn_cast<DeclRefExpr>(BO->getLHS());
  } else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (!UO->isIncrementDecrementOp())
      return;
    isIncrementDecrementUnaryOp = true;
    LHS = dyn_cast<DeclRefExpr>(UO->getSubExpr());
  }
  if (!LHS)
    return;
  VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl());
  if (!VD)
    return;
  // Don't decrement RefsMinusAssignments if volatile variable with compound
  // assignment (+=, ...) or increment/decrement unary operator to avoid
  // potential unused-but-set-variable warning.
  if ((IsCompoundAssign || isIncrementDecrementUnaryOp) &&
      VD->getType().isVolatileQualified())
    return;
  auto iter = RefsMinusAssignments.find(VD);
  if (iter == RefsMinusAssignments.end())
    return;
  iter->getSecond()--;
}
} // namespace

namespace {
ExprResult buildNeverCStringDiscard(Sema &S, Expr *E) {
  Expr *Args[] = {E};
  return buildNeverCStringRuntimeCall(S, /*Scope=*/nullptr, E->getExprLoc(),
                                      BuiltinStringNames::FreeFunctionName,
                                      Args, E->getEndLoc());
}
} // namespace

ExprResult Sema::IgnoredValueConversions(Expr *E) {
  if (LLVM_UNLIKELY(TrackUnusedButSet))
    maybeDecrementCount(E, RefsMinusAssignments);

  if (E->hasPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid())
      return E;
    E = result.get();
  }

  // C99 6.3.2.1:
  //   [Except in specific positions,] an lvalue that does not have
  //   array type is converted to the value stored in the
  //   designated object (and is no longer an lvalue).
  if (E->isPRValue()) {
    if (isNeverCStringType(E->getType()) &&
        !isInsideNeverCStringRuntime())
      return buildNeverCStringDiscard(*this, E);

    // In C, function designators (i.e. expressions of function type)
    // are r-values, but we still want to do function-to-pointer decay
    // on them.  This is both technically correct and convenient for
    // some clients.
    if (E->getType()->isFunctionType())
      return DefaultFunctionArrayConversion(E);

    return E;
  }

  // GCC seems to also exclude expressions of incomplete enum type.
  if (const EnumType *T = E->getType()->getAs<EnumType>()) {
    if (!T->getDecl()->isComplete()) {
      E = ImpCastExprToType(E, Context.VoidTy, CK_ToVoid).get();
      return E;
    }
  }

  ExprResult Res = DefaultFunctionArrayLvalueConversion(E);
  if (Res.isInvalid())
    return E;
  E = Res.get();

  if (!E->getType()->isVoidType())
    RequireCompleteType(E->getExprLoc(), E->getType(),
                        diag::err_incomplete_type);
  return E;
}

ExprResult Sema::OnFinishFullExpr(Expr *FE, SourceLocation CC,
                                  bool DiscardedValue, bool IsConstexpr) {
  ExprResult FullExpr = FE;

  if (!FullExpr.get())
    return ExprError();

  if (DiscardedValue) {
    if (LLVM_UNLIKELY(FullExpr.get()->getType()->getAsPlaceholderType())) {
      FullExpr = CheckPlaceholderExpr(FullExpr.get());
      if (FullExpr.isInvalid())
        return ExprError();
    }

    FullExpr = IgnoredValueConversions(FullExpr.get());
    if (FullExpr.isInvalid())
      return ExprError();

    DiagnoseUnusedExprResult(FullExpr.get(), diag::warn_unused_expr);
  }

  CheckCompletedExpr(FullExpr.get(), CC, IsConstexpr);

  return MaybeCreateExprWithCleanups(FullExpr);
}

Sema::IfExistsResult
Sema::CheckMicrosoftIfExistsSymbol(Scope *S,
                                   const DeclarationNameInfo &TargetNameInfo) {
  DeclarationName TargetName = TargetNameInfo.getName();
  if (!TargetName)
    return IER_DoesNotExist;

  // Do the redeclaration lookup in the current scope.
  LookupResult R(*this, TargetNameInfo, neverc::ResolveAny,
                 neverc::NotForRedeclaration);
  LookupParsedName(R, S);
  R.suppressDiagnostics();

  switch (R.getResultKind()) {
  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
  case LookupResult::Ambiguous:
    return IER_Exists;

  case LookupResult::NotFound:
    return IER_DoesNotExist;
  }

  llvm_unreachable("Invalid LookupResult Kind!");
}

Sema::IfExistsResult
Sema::CheckMicrosoftIfExistsSymbol(Scope *S, SourceLocation KeywordLoc,
                                   bool IsIfExists, UnqualifiedId &Name) {
  DeclarationNameInfo TargetNameInfo = GetNameFromUnqualifiedId(Name);

  return CheckMicrosoftIfExistsSymbol(S, TargetNameInfo);
}

