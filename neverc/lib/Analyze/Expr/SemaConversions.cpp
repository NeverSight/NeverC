#include "TreeTransform.h"
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

// ===----------------------------------------------------------------------===
// Standard conversion sequences
// ===----------------------------------------------------------------------===

namespace {
bool isStandardConversion(Sema &S, Expr *From, QualType ToType,
                          StandardConversionSequence &SCS, bool CStyle);
} // namespace

namespace {
bool isTransparentUnionStandardConversion(Sema &S, Expr *From, QualType &ToType,
                                          StandardConversionSequence &SCS,
                                          bool CStyle);
} // namespace

void StandardConversionSequence::setAsIdentityConversion() {
  First = ICK_Identity;
  Second = ICK_Identity;
  Third = ICK_Identity;
  DeprecatedStringLiteralToCharPtr = false;
}

namespace {
bool checkPlaceholderForOverload(Sema &S, Expr *&E) {
  if (const BuiltinType *placeholder = E->getType()->getAsPlaceholderType()) {
    // We can't handle overloaded expressions here because overload
    // resolution might reasonably tweak them.
    if (placeholder->getKind() == BuiltinType::Overload)
      return false;

    // Go ahead and check everything else.
    ExprResult result = S.CheckPlaceholderExpr(E);
    if (result.isInvalid())
      return true;

    E = result.get();
    return false;
  }

  // Nothing to do.
  return false;
}
} // namespace

//
namespace {
ImplicitConversionSequence TryImplicitConversion(Sema &S, Expr *From,
                                                 QualType ToType, bool CStyle) {
  ImplicitConversionSequence ICS;
  if (isStandardConversion(S, From, ToType, ICS.Standard, CStyle)) {
    ICS.setStandard();
    return ICS;
  }

  ICS.setBad(From, ToType);
  return ICS;
}
} // namespace

ExprResult Sema::PerformImplicitConversion(Expr *From, QualType ToType,
                                           AssignmentAction Action) {
  if (checkPlaceholderForOverload(*this, From))
    return ExprError();

  ImplicitConversionSequence ICS =
      ::TryImplicitConversion(*this, From, ToType, /*CStyle=*/false);
  return PerformImplicitConversion(From, ToType, ICS, Action);
}

bool Sema::IsFunctionConversion(QualType FromType, QualType ToType,
                                QualType &ResultTy) {
  if (Context.hasSameUnqualifiedType(FromType, ToType))
    return false;

  // Permit the conversion F(t __attribute__((noreturn))) -> F(t)
  //                    or F(t noexcept) -> F(t)
  // where F adds a pointer at most once.
  CanQualType CanTo = Context.getCanonicalType(ToType);
  CanQualType CanFrom = Context.getCanonicalType(FromType);
  Type::TypeClass TyClass = CanTo->getTypeClass();
  if (TyClass != CanFrom->getTypeClass())
    return false;
  if (TyClass != Type::FunctionProto && TyClass != Type::FunctionNoProto) {
    if (TyClass == Type::Pointer) {
      CanTo = CanTo.castAs<PointerType>()->getPointeeType();
      CanFrom = CanFrom.castAs<PointerType>()->getPointeeType();
    } else {
      return false;
    }

    TyClass = CanTo->getTypeClass();
    if (TyClass != CanFrom->getTypeClass())
      return false;
    if (TyClass != Type::FunctionProto && TyClass != Type::FunctionNoProto)
      return false;
  }

  const auto *FromFn = cast<FunctionType>(CanFrom);
  FunctionType::ExtInfo FromEInfo = FromFn->getExtInfo();

  const auto *ToFn = cast<FunctionType>(CanTo);
  FunctionType::ExtInfo ToEInfo = ToFn->getExtInfo();

  bool Changed = false;

  // Drop 'noreturn' if not present in target type.
  if (FromEInfo.getNoReturn() && !ToEInfo.getNoReturn()) {
    FromFn = Context.adjustFunctionType(FromFn, FromEInfo.withNoReturn(false));
    Changed = true;
  }

  // [MSVC compatibility]: treat noreturn as not affecting function pointer
  // type.
  if (getLangOpts().MSVCCompat && !FromEInfo.getNoReturn() &&
      ToEInfo.getNoReturn()) {
    FromFn = Context.adjustFunctionType(FromFn, FromEInfo.withNoReturn(true));
    Changed = true;
  }

  // Drop the 'arm_preserves_za' if not present in the target type (we can do
  // that because it is merely a hint).
  if (const auto *FromFPT = dyn_cast<FunctionProtoType>(FromFn)) {
    FunctionProtoType::ExtProtoInfo ExtInfo = FromFPT->getExtProtoInfo();
    if (ExtInfo.AArch64SMEAttributes &
        FunctionType::SME_PStateZAPreservedMask) {
      unsigned ToFlags = 0;
      if (const auto *ToFPT = dyn_cast<FunctionProtoType>(ToFn))
        ToFlags = ToFPT->getExtProtoInfo().AArch64SMEAttributes;
      if (!(ToFlags & FunctionType::SME_PStateZAPreservedMask)) {
        ExtInfo.setArmSMEAttribute(FunctionType::SME_PStateZAPreservedMask,
                                   false);
        QualType QT = Context.getFunctionType(
            FromFPT->getReturnType(), FromFPT->getParamTypes(), ExtInfo);
        FromFn = QT->getAs<FunctionType>();
        Changed = true;
      }
    }
  }

  if (const auto *FromFPT = dyn_cast<FunctionProtoType>(FromFn)) {
    const auto *ToFPT = cast<FunctionProtoType>(ToFn);

    llvm::SmallVector<FunctionProtoType::ExtParameterInfo, 4> NewParamInfos;
    bool CanUseToFPT, CanUseFromFPT;
    if (Context.mergeExtParameterInfo(ToFPT, FromFPT, CanUseToFPT,
                                      CanUseFromFPT, NewParamInfos) &&
        CanUseToFPT && !CanUseFromFPT) {
      FunctionProtoType::ExtProtoInfo ExtInfo = FromFPT->getExtProtoInfo();
      ExtInfo.ExtParameterInfos =
          NewParamInfos.empty() ? nullptr : NewParamInfos.data();
      QualType QT = Context.getFunctionType(FromFPT->getReturnType(),
                                            FromFPT->getParamTypes(), ExtInfo);
      FromFn = QT->getAs<FunctionType>();
      Changed = true;
    }
  }

  if (!Changed)
    return false;

  assert(QualType(FromFn, 0).isCanonical());
  if (QualType(FromFn, 0) != CanTo)
    return false;

  ResultTy = ToType;
  return true;
}

namespace {
bool isVectorConversion(Sema &S, QualType FromType, QualType ToType,
                        ImplicitConversionKind &ICK) {
  // We need at least one of these types to be a vector type to have a vector
  // conversion.
  if (!ToType->isVectorType() && !FromType->isVectorType())
    return false;

  // Identical types require no conversions.
  if (S.Context.hasSameUnqualifiedType(FromType, ToType))
    return false;

  // There are no conversions between extended vector types, only identity.
  if (ToType->isExtVectorType()) {
    // There are no conversions between extended vector types other than the
    // identity conversion.
    if (FromType->isExtVectorType())
      return false;

    // Vector splat from any arithmetic type to a vector.
    if (FromType->isArithmeticType()) {
      ICK = ICK_Vector_Splat;
      return true;
    }
  }

  if (ToType->isSVESizelessBuiltinType() ||
      FromType->isSVESizelessBuiltinType())
    if (S.Context.areCompatibleSveTypes(FromType, ToType) ||
        S.Context.areLaxCompatibleSveTypes(FromType, ToType)) {
      ICK = ICK_Vector_Conversion;
      return true;
    }

  // GCC-compatible vector types, or lax same-sized vector conversion.
  if (ToType->isVectorType() && FromType->isVectorType()) {
    if (S.Context.areCompatibleVectorTypes(FromType, ToType) ||
        S.isLaxVectorConversion(FromType, ToType)) {
      ICK = ICK_Vector_Conversion;
      return true;
    }
  }

  return false;
}
} // namespace

namespace {
bool tryAtomicConversion(Sema &S, Expr *From, QualType ToType,
                         StandardConversionSequence &SCS, bool CStyle);
} // namespace

namespace {
bool isStandardConversion(Sema &S, Expr *From, QualType ToType,
                          StandardConversionSequence &SCS, bool CStyle) {
  QualType FromType = From->getType();

  SCS.setAsIdentityConversion();
  SCS.setFromType(FromType);

  // The first conversion can be an lvalue-to-rvalue conversion,
  // array-to-pointer conversion, or function-to-pointer conversion
  // (Standard conversion: lvalue-to-rvalue, array-to-pointer, etc.)

  if (FromType == S.Context.OverloadTy)
    return false;
  // Lvalue-to-rvalue: lvalue of non-function, non-array type becomes a
  // prvalue.
  bool argIsLValue = From->isLValue();
  if (argIsLValue && !FromType->isFunctionType() && !FromType->isArrayType() &&
      S.Context.getCanonicalType(FromType) != S.Context.OverloadTy) {
    SCS.First = ICK_Lvalue_To_Rvalue;

    // C11 6.3.2.1p2:
    //   ... if the lvalue has atomic type, the value has the non-atomic version
    //   of the type of the lvalue ...
    if (const AtomicType *Atomic = FromType->getAs<AtomicType>())
      FromType = Atomic->getValueType();

    // Non-class: rvalue uses the unqualified type; in C, qualifiers are
    // stripped here.
    FromType = FromType.getUnqualifiedType();
  } else if (FromType->isArrayType()) {
    // Array-to-pointer conversion (C 6.3.2.1).
    SCS.First = ICK_Array_To_Pointer;

    // "array of T" decays to "pointer to T".
    FromType = S.Context.getArrayDecayedType(FromType);

    if (S.IsStringLiteralToNonConstPointerConversion(From, ToType)) {
      // Deprecated string literal → non-const char *.
      SCS.DeprecatedStringLiteralToCharPtr = true;

      // Model as array-to-pointer plus qualification adjustment for ranking.
      SCS.Second = ICK_Identity;
      SCS.Third = ICK_Qualification;
      SCS.setAllToTypes(FromType);
      return true;
    }
  } else if (FromType->isFunctionType() && argIsLValue) {
    // Function lvalue decays to pointer to function.
    SCS.First = ICK_Function_To_Pointer;

    if (auto *DRE = dyn_cast<DeclRefExpr>(From->IgnoreParenCasts()))
      if (auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl()))
        if (!S.checkAddressOfFunctionIsAvailable(FD))
          return false;

    // Result type is pointer to T.
    FromType = S.Context.getPointerType(FromType);
  } else {
    // We don't require any conversions for the first step.
    SCS.First = ICK_Identity;
  }
  SCS.setToType(0, FromType);

  // Second step: promotions, arithmetic conversions, pointer conversions,
  // boolean conversion, etc. C overload resolution also uses compatible-type
  // conversion.
  ImplicitConversionKind SecondICK = ICK_Identity;
  if (S.Context.hasSameUnqualifiedType(FromType, ToType)) {
    // The unqualified versions of the types are the same: there's no
    // conversion to do.
    SCS.Second = ICK_Identity;
  } else if (S.IsIntegralPromotion(From, FromType, ToType)) {
    // Integral promotion.
    SCS.Second = ICK_Integral_Promotion;
    FromType = ToType.getUnqualifiedType();
  } else if (S.IsFloatingPointPromotion(FromType, ToType)) {
    // Floating-point promotion.
    SCS.Second = ICK_Floating_Promotion;
    FromType = ToType.getUnqualifiedType();
  } else if (S.IsComplexPromotion(FromType, ToType)) {
    // Complex promotion (NeverC extension)
    SCS.Second = ICK_Complex_Promotion;
    FromType = ToType.getUnqualifiedType();
  } else if (ToType->isBooleanType() &&
             (FromType->isArithmeticType() || FromType->isAnyPointerType())) {
    SCS.Second = ICK_Boolean_Conversion;
    FromType = S.Context.BoolTy;
  } else if (FromType->isIntegralOrUnscopedEnumerationType() &&
             ToType->isIntegralType(S.Context)) {
    // Integral conversion.
    SCS.Second = ICK_Integral_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if (FromType->isAnyComplexType() && ToType->isAnyComplexType()) {
    // Complex conversions (C99 6.3.1.6)
    SCS.Second = ICK_Complex_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if ((FromType->isAnyComplexType() && ToType->isArithmeticType()) ||
             (ToType->isAnyComplexType() && FromType->isArithmeticType())) {
    // Complex-real conversions (C99 6.3.1.7)
    SCS.Second = ICK_Complex_Real;
    FromType = ToType.getUnqualifiedType();
  } else if (FromType->isRealFloatingType() && ToType->isRealFloatingType()) {
    // Disable conversions between long double and __float128
    // if their representation is different until there is back end support
    // We of course allow this conversion if long double is really double.

    // Conversions between bfloat16 and float16 are currently not supported.
    if ((FromType->isBFloat16Type() &&
         (ToType->isFloat16Type() || ToType->isHalfType())) ||
        (ToType->isBFloat16Type() &&
         (FromType->isFloat16Type() || FromType->isHalfType())))
      return false;

    // conversion check.

    // Floating-point conversion.
    SCS.Second = ICK_Floating_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if ((FromType->isRealFloatingType() &&
              ToType->isIntegralType(S.Context)) ||
             (FromType->isIntegralOrUnscopedEnumerationType() &&
              ToType->isRealFloatingType())) {

    // Floating–integral conversion.
    SCS.Second = ICK_Floating_Integral;
    FromType = ToType.getUnqualifiedType();
  } else if (S.IsPointerConversion(From, FromType, ToType, FromType)) {
    // Pointer conversion.
    SCS.Second = ICK_Pointer_Conversion;
    FromType = FromType.getUnqualifiedType();
  } else if (isVectorConversion(S, FromType, ToType, SecondICK)) {
    SCS.Second = SecondICK;
    FromType = ToType.getUnqualifiedType();
  } else if (S.Context.typesAreCompatible(ToType, FromType)) {
    // Compatible conversions (NeverC extension for C function overloading)
    SCS.Second = ICK_Compatible_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if (isTransparentUnionStandardConversion(S, From, ToType, SCS,
                                                  CStyle)) {
    SCS.Second = ICK_TransparentUnionConversion;
    FromType = ToType;
  } else if (tryAtomicConversion(S, From, ToType, SCS, CStyle)) {
    return true;
  } else if (ToType->isFixedPointType() || FromType->isFixedPointType()) {
    SCS.Second = ICK_Fixed_Point_Conversion;
    FromType = ToType;
  } else {
    // No second conversion required.
    SCS.Second = ICK_Identity;
  }
  SCS.setToType(1, FromType);

  // Third step: function pointer conversion or qualification conversion.
  if (S.IsFunctionConversion(FromType, ToType, FromType)) {
    SCS.Third = ICK_Function_Conversion;
  } else if (S.IsQualificationConversion(FromType, ToType, CStyle)) {
    SCS.Third = ICK_Qualification;
    FromType = ToType;
  } else {
    // No conversion required
    SCS.Third = ICK_Identity;
  }

  // Top-level cv-qualifier differences are handled by the initialization, not
  // as a separate conversion step.
  QualType CanonFrom = S.Context.getCanonicalType(FromType);
  QualType CanonTo = S.Context.getCanonicalType(ToType);
  if (CanonFrom.getLocalUnqualifiedType() ==
          CanonTo.getLocalUnqualifiedType() &&
      CanonFrom.getLocalQualifiers() != CanonTo.getLocalQualifiers()) {
    FromType = ToType;
    CanonFrom = CanonTo;
  }

  SCS.setToType(2, FromType);

  if (CanonFrom == CanonTo)
    return true;

  return false;
}
} // namespace

namespace {
bool isTransparentUnionStandardConversion(Sema &S, Expr *From, QualType &ToType,
                                          StandardConversionSequence &SCS,
                                          bool CStyle) {

  const RecordType *UT = ToType->getAsUnionType();
  if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
    return false;
  // The field to initialize within the transparent union.
  RecordDecl *UD = UT->getDecl();
  // It's compatible if the expression matches any of the fields.
  for (const auto *it : UD->fields()) {
    if (isStandardConversion(S, From, it->getType(), SCS, CStyle)) {
      ToType = it->getType();
      return true;
    }
  }
  return false;
}
} // namespace

// ===----------------------------------------------------------------------===
// Type promotions & pointer conversions
// ===----------------------------------------------------------------------===

bool Sema::IsIntegralPromotion(Expr *From, QualType FromType, QualType ToType) {
  const BuiltinType *To = ToType->getAs<BuiltinType>();
  // All integers are built-in.
  if (!To) {
    return false;
  }

  // Small integer types promote to int or unsigned int (usual arithmetic
  // conversions / integer promotions).
  if (Context.isPromotableIntegerType(FromType) && !FromType->isBooleanType() &&
      !FromType->isEnumeralType()) {
    if ( // We can promote any signed, promotable integer type to an int
        (FromType->isSignedIntegerType() ||
         // We can promote any unsigned integer type whose size is
         // less than int to an int.
         Context.getTypeSize(FromType) < Context.getTypeSize(ToType))) {
      return To->getKind() == BuiltinType::Int;
    }

    return To->getKind() == BuiltinType::UInt;
  }

  // Enumeration types: integral promotion to a suitable integer type.
  if (const EnumType *FromEnumType = FromType->getAs<EnumType>()) {
    // We can perform an integral promotion to the underlying type of the enum,
    // even if that's not the promoted type. Note that the check for promoting
    // the underlying type is based on the type alone, and does not consider
    // the bitfield-ness of the actual source expression.
    if (FromEnumType->getDecl()->isFixed()) {
      QualType Underlying = FromEnumType->getDecl()->getIntegerType();
      return Context.hasSameUnqualifiedType(Underlying, ToType) ||
             IsIntegralPromotion(nullptr, Underlying, ToType);
    }

    // We have already pre-calculated the promotion type, so this is trivial.
    if (ToType->isIntegerType() &&
        isCompleteType(From->getBeginLoc(), FromType))
      return Context.hasSameUnqualifiedType(
          ToType, FromEnumType->getDecl()->getPromotionType());
  }

  // char16_t / char32_t / wchar_t promote like other wide character types.
  if (FromType->isAnyCharacterType() && !FromType->isCharType() &&
      ToType->isIntegerType()) {
    // Determine whether the type we're converting from is signed or
    // unsigned.
    bool FromIsSigned = FromType->isSignedIntegerType();
    uint64_t FromSize = Context.getTypeSize(FromType);

    // The types we'll try to promote to, in the appropriate
    // order. Try each of these types.
    QualType PromoteTypes[6] = {Context.IntTy,      Context.UnsignedIntTy,
                                Context.LongTy,     Context.UnsignedLongTy,
                                Context.LongLongTy, Context.UnsignedLongLongTy};
    for (int Idx = 0; Idx < 6; ++Idx) {
      uint64_t ToSize = Context.getTypeSize(PromoteTypes[Idx]);
      if (FromSize < ToSize ||
          (FromSize == ToSize &&
           FromIsSigned == PromoteTypes[Idx]->isSignedIntegerType())) {
        // We found the type that we can promote to. If this is the
        // type we wanted, we have a promotion. Otherwise, no
        // promotion.
        return Context.hasSameUnqualifiedType(ToType, PromoteTypes[Idx]);
      }
    }
  }

  // Bit-field integral promotion.
  // In C, only bit-fields of types _Bool, int, or unsigned int may be
  // promoted, per C11 6.3.1.1/2. We promote all bit-fields (including enum
  // bit-fields and those whose underlying type is larger than int) for GCC
  // compatibility.
  if (From) {
    if (FieldDecl *MemberDecl = From->getSourceBitField()) {
      std::optional<llvm::APSInt> BitWidth;
      if (FromType->isIntegralType(Context) &&
          (BitWidth =
               MemberDecl->getBitWidth()->getIntegerConstantExpr(Context))) {
        llvm::APSInt ToSize(BitWidth->getBitWidth(), BitWidth->isUnsigned());
        ToSize = Context.getTypeSize(ToType);

        // Are we promoting to an int from a bitfield that fits in an int?
        if (*BitWidth < ToSize ||
            (FromType->isSignedIntegerType() && *BitWidth <= ToSize)) {
          return To->getKind() == BuiltinType::Int;
        }

        // Are we promoting to an unsigned int from an unsigned bitfield
        // that fits into an unsigned int?
        if (FromType->isUnsignedIntegerType() && *BitWidth <= ToSize) {
          return To->getKind() == BuiltinType::UInt;
        }

        return false;
      }
    }
  }

  // bool to int: false → 0, true → 1.
  if (FromType->isBooleanType() && To->getKind() == BuiltinType::Int) {
    return true;
  }

  return false;
}

bool Sema::IsFloatingPointPromotion(QualType FromType, QualType ToType) {
  if (const BuiltinType *FromBuiltin = FromType->getAs<BuiltinType>())
    if (const BuiltinType *ToBuiltin = ToType->getAs<BuiltinType>()) {
      // float promotes to double.
      if (FromBuiltin->getKind() == BuiltinType::Float &&
          ToBuiltin->getKind() == BuiltinType::Double)
        return true;

      // C99 6.3.1.5p1:
      //   When a float is promoted to double or long double, or a
      //   double is promoted to long double [...].
      if ((FromBuiltin->getKind() == BuiltinType::Float ||
           FromBuiltin->getKind() == BuiltinType::Double) &&
          (ToBuiltin->getKind() == BuiltinType::LongDouble ||
           ToBuiltin->getKind() == BuiltinType::Float128))
        return true;

      // Half can be promoted to float.
      if (!getLangOpts().NativeHalfType &&
          FromBuiltin->getKind() == BuiltinType::Half &&
          ToBuiltin->getKind() == BuiltinType::Float)
        return true;
    }

  return false;
}

bool Sema::IsComplexPromotion(QualType FromType, QualType ToType) {
  const ComplexType *FromComplex = FromType->getAs<ComplexType>();
  if (!FromComplex)
    return false;

  const ComplexType *ToComplex = ToType->getAs<ComplexType>();
  if (!ToComplex)
    return false;

  return IsFloatingPointPromotion(FromComplex->getElementType(),
                                  ToComplex->getElementType()) ||
         IsIntegralPromotion(nullptr, FromComplex->getElementType(),
                             ToComplex->getElementType());
}

namespace {
QualType formSimilarlyQualifiedPointerType(const Type *FromPtr,
                                           QualType ToPointee, QualType ToType,
                                           TreeContext &Context) {
  assert(FromPtr->getTypeClass() == Type::Pointer &&
         "Invalid similarly-qualified pointer type");

  QualType CanonFromPointee =
      Context.getCanonicalType(FromPtr->getPointeeType());
  QualType CanonToPointee = Context.getCanonicalType(ToPointee);
  Qualifiers Quals = CanonFromPointee.getQualifiers();

  // Exact qualifier match -> return the pointer type we're converting to.
  if (CanonToPointee.getLocalQualifiers() == Quals) {
    // ToType is exactly what we need. Return it.
    if (!ToType.isNull())
      return ToType.getUnqualifiedType();

    // Build a pointer to ToPointee. It has the right qualifiers
    // already.
    return Context.getPointerType(ToPointee);
  }

  // Just build a canonical type that has the right qualifiers.
  QualType QualifiedCanonToPointee =
      Context.getQualifiedType(CanonToPointee.getLocalUnqualifiedType(), Quals);

  return Context.getPointerType(QualifiedCanonToPointee);
}
} // namespace

namespace {
bool isNullPointerConstantForConversion(Expr *Expr, TreeContext &Context) {

  return Expr->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
}
} // namespace

bool Sema::IsPointerConversion(Expr *From, QualType FromType, QualType ToType,
                               QualType &ConvertedType) {

  // If the left-hand-side is nullptr_t, the right side can be a null
  // pointer constant.
  if (ToType->isNullPtrType() &&
      isNullPointerConstantForConversion(From, Context)) {
    ConvertedType = ToType;
    return true;
  }

  const PointerType *ToTypePtr = ToType->getAs<PointerType>();
  if (!ToTypePtr)
    return false;

  // Null pointer constant to any pointer type.
  if (isNullPointerConstantForConversion(From, Context)) {
    ConvertedType = ToType;
    return true;
  }

  // Beyond this point, both sides are ordinary pointers.
  QualType ToPointeeType = ToTypePtr->getPointeeType();
  const PointerType *FromTypePtr = FromType->getAs<PointerType>();
  if (!FromTypePtr)
    return false;

  QualType FromPointeeType = FromTypePtr->getPointeeType();

  // If the unqualified pointee types are the same, this can't be a
  // pointer conversion, so don't do all of the work below.
  if (Context.hasSameUnqualifiedType(FromPointeeType, ToPointeeType))
    return false;

  // Object pointer to void pointer (same qualifiers as far as allowed).
  if (FromPointeeType->isIncompleteOrObjectType() &&
      ToPointeeType->isVoidType()) {
    ConvertedType = formSimilarlyQualifiedPointerType(
        FromTypePtr, ToPointeeType, ToType, Context);
    return true;
  }

  // MSVC allows implicit function to void* type conversion.
  if (getLangOpts().MSVCCompat && FromPointeeType->isFunctionType() &&
      ToPointeeType->isVoidType()) {
    ConvertedType = formSimilarlyQualifiedPointerType(
        FromTypePtr, ToPointeeType, ToType, Context);
    return true;
  }

  if (Context.typesAreCompatible(FromPointeeType, ToPointeeType)) {
    ConvertedType = formSimilarlyQualifiedPointerType(
        FromTypePtr, ToPointeeType, ToType, Context);
    return true;
  }

  if (FromPointeeType->isVectorType() && ToPointeeType->isVectorType() &&
      Context.areCompatibleVectorTypes(FromPointeeType, ToPointeeType)) {
    ConvertedType = formSimilarlyQualifiedPointerType(
        FromTypePtr, ToPointeeType, ToType, Context);
    return true;
  }

  return false;
}

enum {
  ft_default,
  ft_parameter_arity,
  ft_parameter_mismatch,
  ft_return_type,
  ft_qualifer_mismatch,
};

namespace {
const FunctionProtoType *tryGetFunctionProtoType(QualType FromType) {
  if (auto *FPT = FromType->getAs<FunctionProtoType>())
    return FPT;

  return nullptr;
}
} // namespace

void Sema::DiagnoseFunctionTypeMismatch(PartialDiagnostic &PDiag,
                                        QualType FromType, QualType ToType) {
  // If either type is not valid, include no extra info.
  if (FromType.isNull() || ToType.isNull()) {
    PDiag << ft_default;
    return;
  }

  if (FromType->isPointerType())
    FromType = FromType->getPointeeType();
  if (ToType->isPointerType())
    ToType = ToType->getPointeeType();

  // No extra info for same types.
  if (Context.hasSameType(FromType, ToType)) {
    PDiag << ft_default;
    return;
  }

  const FunctionProtoType *FromFunction = tryGetFunctionProtoType(FromType),
                          *ToFunction = tryGetFunctionProtoType(ToType);

  // Both types need to be function types.
  if (!FromFunction || !ToFunction) {
    PDiag << ft_default;
    return;
  }

  if (FromFunction->getNumParams() != ToFunction->getNumParams()) {
    PDiag << ft_parameter_arity << ToFunction->getNumParams()
          << FromFunction->getNumParams();
    return;
  }

  // Handle different parameter types.
  unsigned ArgPos;
  if (!FunctionParamTypesAreEqual(FromFunction, ToFunction, &ArgPos)) {
    PDiag << ft_parameter_mismatch << ArgPos + 1
          << ToFunction->getParamType(ArgPos)
          << FromFunction->getParamType(ArgPos);
    return;
  }

  // Handle different return type.
  if (!Context.hasSameType(FromFunction->getReturnType(),
                           ToFunction->getReturnType())) {
    PDiag << ft_return_type << ToFunction->getReturnType()
          << FromFunction->getReturnType();
    return;
  }

  PDiag << ft_default;
}

bool Sema::FunctionParamTypesAreEqual(llvm::ArrayRef<QualType> Old,
                                      llvm::ArrayRef<QualType> New,
                                      unsigned *ArgPos, bool Reversed) {
  assert(llvm::size(Old) == llvm::size(New) &&
         "Can't compare parameters of functions with different number of "
         "parameters!");

  for (auto &&[Idx, Type] : llvm::enumerate(Old)) {
    // Reverse iterate over the parameters of `OldType` if `Reversed` is true.
    size_t J = Reversed ? (llvm::size(New) - Idx - 1) : Idx;

    // Ignore address spaces in pointee type. This is to disallow overloading
    // on __ptr32/__ptr64 address spaces.
    QualType OldType =
        Context.removePtrSizeAddrSpace(Type.getUnqualifiedType());
    QualType NewType =
        Context.removePtrSizeAddrSpace((New.begin() + J)->getUnqualifiedType());

    if (!Context.hasSameType(OldType, NewType)) {
      if (ArgPos)
        *ArgPos = Idx;
      return false;
    }
  }
  return true;
}

bool Sema::FunctionParamTypesAreEqual(const FunctionProtoType *OldType,
                                      const FunctionProtoType *NewType,
                                      unsigned *ArgPos, bool Reversed) {
  return FunctionParamTypesAreEqual(OldType->param_types(),
                                    NewType->param_types(), ArgPos, Reversed);
}

bool Sema::CheckPointerConversion(Expr *From, QualType ToType, CastKind &Kind,
                                  bool IgnoreBaseAccess, bool Diagnose) {
  QualType FromType = From->getType();
  bool IsCStyleOrFunctionalCast = IgnoreBaseAccess;

  Kind = CK_BitCast;

  if (Diagnose && !IsCStyleOrFunctionalCast && !FromType->isAnyPointerType() &&
      From->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNotNull) ==
          Expr::NPCK_ZeroExpression) {
    if (Context.hasSameUnqualifiedType(From->getType(), Context.BoolTy))
      DiagRuntimeBehavior(From->getExprLoc(), From,
                          PDiag(diag::warn_impcast_bool_to_null_pointer)
                              << ToType << From->getSourceRange());
    else if (!isUnevaluatedContext())
      Diag(From->getExprLoc(), diag::warn_non_literal_null_pointer)
          << ToType << From->getSourceRange();
  }
  if (const PointerType *ToPtrType = ToType->getAs<PointerType>()) {
    if (const PointerType *FromPtrType = FromType->getAs<PointerType>()) {
#ifndef _WIN32
      QualType FromPointeeType = FromPtrType->getPointeeType(),
               ToPointeeType = ToPtrType->getPointeeType();
      if (Diagnose && !IsCStyleOrFunctionalCast &&
          FromPointeeType->isFunctionType() && ToPointeeType->isVoidType()) {
        assert(getLangOpts().MSVCCompat &&
               "this should only be possible with MSVCCompat!");
        Diag(From->getExprLoc(), diag::ext_ms_impcast_fn_obj)
            << From->getSourceRange();
      }
#else
      (void)FromPtrType;
#endif
    }
  }

  // We shouldn't fall into this case unless it's valid for other
  // reasons.
  if (From->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull))
    Kind = CK_NullToPointer;

  return false;
}

namespace {
bool isQualificationConversionStep(QualType FromType, QualType ToType,
                                   bool CStyle, bool IsTopLevel,
                                   bool &PreviousToQualsIncludeConst) {
  Qualifiers FromQuals = FromType.getQualifiers();
  Qualifiers ToQuals = ToType.getQualifiers();

  // Ignore __unaligned qualifier.
  FromQuals.removeUnaligned();

  //   -- for every j > 0, if const is in cv 1,j then const is in cv
  //      2,j, and similarly for volatile.
  if (!CStyle && !ToQuals.compatiblyIncludes(FromQuals))
    return false;

  // If address spaces mismatch:
  //  - in top level it is only valid to convert to addr space that is a
  //    superset in all cases apart from C-style casts where we allow
  //    conversions between overlapping address spaces.
  //  - in non-top levels it is not a valid conversion.
  if (ToQuals.getAddressSpace() != FromQuals.getAddressSpace() &&
      (!IsTopLevel ||
       !(ToQuals.isAddressSpaceSupersetOf(FromQuals) ||
         (CStyle && FromQuals.isAddressSpaceSupersetOf(ToQuals)))))
    return false;

  //   -- if the cv 1,j and cv 2,j are different, then const is in
  //      every cv for 0 < k < j.
  if (!CStyle && FromQuals.getCVRQualifiers() != ToQuals.getCVRQualifiers() &&
      !PreviousToQualsIncludeConst)
    return false;

  // Multi-level pointer qualification: track P3 vs P1 for array/incomplete
  // cases.
  //   -- if [...] P1,i [...] is "array of unknown bound of", P3,i is
  //      "array of unknown bound of"
  if (FromType->isIncompleteArrayType() && !ToType->isIncompleteArrayType())
    return false;

  //   -- if the resulting P3,i is different from P1,i [...], then const is
  //      added to every cv 3_k for 0 < k < i.
  if (!CStyle && FromType->isConstantArrayType() &&
      ToType->isIncompleteArrayType() && !PreviousToQualsIncludeConst)
    return false;

  // Keep track of whether all prior cv-qualifiers in the "to" type
  // include const.
  PreviousToQualsIncludeConst =
      PreviousToQualsIncludeConst && ToQuals.hasConst();
  return true;
}
} // namespace

bool Sema::IsQualificationConversion(QualType FromType, QualType ToType,
                                     bool CStyle) {
  FromType = Context.getCanonicalType(FromType);
  ToType = Context.getCanonicalType(ToType);

  if (FromType.getUnqualifiedType() == ToType.getUnqualifiedType())
    return false;

  // Qualification conversion: add cv-qualifiers at inner pointer levels per
  // the usual rules.
  bool PreviousToQualsIncludeConst = true;
  bool UnwrappedAnyPointer = false;
  while (Context.UnwrapSimilarTypes(FromType, ToType)) {
    if (!isQualificationConversionStep(FromType, ToType, CStyle,
                                       !UnwrappedAnyPointer,
                                       PreviousToQualsIncludeConst))
      return false;
    UnwrappedAnyPointer = true;
  }

  // We are left with FromType and ToType being the pointee types
  // after unwrapping the original FromType and ToType the same number
  // of times. If we unwrapped any pointers, and if FromType and
  // ToType have the same unqualified type (since we checked
  // qualifiers above), then this is a qualification conversion.
  return UnwrappedAnyPointer &&
         Context.hasSameUnqualifiedType(FromType, ToType);
}

namespace {
bool tryAtomicConversion(Sema &S, Expr *From, QualType ToType,
                         StandardConversionSequence &SCS, bool CStyle) {
  const AtomicType *ToAtomic = ToType->getAs<AtomicType>();
  if (!ToAtomic)
    return false;

  StandardConversionSequence InnerSCS;
  if (!isStandardConversion(S, From, ToAtomic->getValueType(), InnerSCS,
                            CStyle))
    return false;

  SCS.Second = InnerSCS.Second;
  SCS.setToType(1, InnerSCS.getToType(1));
  SCS.Third = InnerSCS.Third;
  SCS.setToType(2, InnerSCS.getToType(2));
  return true;
}
} // namespace

namespace {
ImplicitConversionSequence tryContextuallyConvertToBool(Sema &S, Expr *From) {
  // nullptr_t to bool direct-initialization yields false.
  if (From->getType()->isNullPtrType())
    return ImplicitConversionSequence::getNullptrToBool(
        From->getType(), S.Context.BoolTy, From->isLValue());

  // All other direct-initialization of bool is equivalent to an implicit
  // conversion to bool in which explicit conversions are permitted.
  return TryImplicitConversion(S, From, S.Context.BoolTy, /*CStyle=*/false);
}
} // namespace

ExprResult Sema::PerformContextuallyConvertToBool(Expr *From) {
  if (checkPlaceholderForOverload(*this, From))
    return ExprError();

  ImplicitConversionSequence ICS = tryContextuallyConvertToBool(*this, From);
  if (!ICS.isBad())
    return PerformImplicitConversion(From, Context.BoolTy, ICS, AA_Converting);

  return Diag(From->getBeginLoc(), diag::err_typecheck_bool_condition)
         << From->getType() << From->getSourceRange();
}

namespace {
template <typename CheckFn>
bool diagnoseDiagnoseIfAttrsWith(Sema &S, const NamedDecl *ND,
                                 bool ArgDependent, SourceLocation Loc,
                                 CheckFn &&IsSuccessful) {
  llvm::SmallVector<const DiagnoseIfAttr *, 8> Attrs;
  for (const auto *DIA : ND->specific_attrs<DiagnoseIfAttr>()) {
    if (ArgDependent == DIA->getArgDependent())
      Attrs.push_back(DIA);
  }

  // Common case: No diagnose_if attributes, so we can quit early.
  if (Attrs.empty())
    return false;

  auto WarningBegin = std::stable_partition(
      Attrs.begin(), Attrs.end(),
      [](const DiagnoseIfAttr *DIA) { return DIA->isError(); });

  // Note that diagnose_if attributes are late-parsed, so they appear in the
  // correct order (unlike enable_if attributes).
  auto ErrAttr = llvm::find_if(llvm::make_range(Attrs.begin(), WarningBegin),
                               IsSuccessful);
  if (ErrAttr != WarningBegin) {
    const DiagnoseIfAttr *DIA = *ErrAttr;
    S.Diag(Loc, diag::err_diagnose_if_succeeded) << DIA->getMessage();
    S.Diag(DIA->getLocation(), diag::note_from_diagnose_if)
        << DIA->getParent() << DIA->getCond()->getSourceRange();
    return true;
  }

  for (const auto *DIA : llvm::make_range(WarningBegin, Attrs.end()))
    if (IsSuccessful(DIA)) {
      S.Diag(Loc, diag::warn_diagnose_if_succeeded) << DIA->getMessage();
      S.Diag(DIA->getLocation(), diag::note_from_diagnose_if)
          << DIA->getParent() << DIA->getCond()->getSourceRange();
    }

  return false;
}
} // namespace

bool Sema::diagnoseArgDependentDiagnoseIfAttrs(
    const FunctionDecl *Function, llvm::ArrayRef<const Expr *> Args,
    SourceLocation Loc) {
  return diagnoseDiagnoseIfAttrsWith(
      *this, Function, /*ArgDependent=*/true, Loc,
      [&](const DiagnoseIfAttr *DIA) {
        APValue Result;
        // It's sane to use the same Args for any redecl of this function, since
        // EvaluateWithSubstitution only cares about the position of each
        // argument in the arg list, not the ParmVarDecl* it maps to.
        if (!DIA->getCond()->EvaluateWithSubstitution(
                Result, Context, cast<FunctionDecl>(DIA->getParent()), Args))
          return false;
        return Result.isInt() && Result.getInt().getBoolValue();
      });
}

bool Sema::diagnoseArgIndependentDiagnoseIfAttrs(const NamedDecl *ND,
                                                 SourceLocation Loc) {
  return diagnoseDiagnoseIfAttrsWith(
      *this, ND, /*ArgDependent=*/false, Loc, [&](const DiagnoseIfAttr *DIA) {
        bool Result;
        return DIA->getCond()->EvaluateAsBooleanCondition(Result, Context) &&
               Result;
      });
}

namespace {
bool isFunctionAlwaysEnabled(const TreeContext &Ctx, const FunctionDecl *FD) {
  for (auto *EnableIf : FD->specific_attrs<EnableIfAttr>()) {
    bool AlwaysTrue;
    if (!EnableIf->getCond()->EvaluateAsBooleanCondition(AlwaysTrue, Ctx))
      return false;
    if (!AlwaysTrue)
      return false;
  }
  return true;
}
} // namespace

namespace {
bool checkAddressOfFunctionIsAvailable(Sema &S, const FunctionDecl *FD,
                                       bool Complain, SourceLocation Loc) {
  if (!isFunctionAlwaysEnabled(S.Context, FD)) {
    if (Complain)
      S.Diag(Loc, diag::err_addrof_function_disabled_by_enable_if_attr) << FD;
    return false;
  }

  auto I = llvm::find_if(FD->parameters(), [](const ParmVarDecl *P) {
    return P->hasAttr<PassObjectSizeAttr>();
  });
  if (I == FD->param_end())
    return true;

  if (Complain) {
    // Add one to ParamNo because it's user-facing
    unsigned ParamNo = std::distance(FD->param_begin(), I) + 1;
    S.Diag(Loc, diag::err_address_of_function_with_pass_object_size_params)
        << FD << ParamNo;
  }
  return false;
}
} // namespace

bool Sema::checkAddressOfFunctionIsAvailable(const FunctionDecl *Function,
                                             bool Complain,
                                             SourceLocation Loc) {
  return ::checkAddressOfFunctionIsAvailable(*this, Function, Complain, Loc);
}

// C23 Auto Type Deduction (uses TreeTransform.h; kept here to limit include
// surface of TreeTransform.h)

namespace {

class SubstituteDeducedTypeTransform
    : public TreeTransform<SubstituteDeducedTypeTransform> {
  QualType Replacement;
  bool UseTypeSugar;

public:
  SubstituteDeducedTypeTransform(Sema &SemaRef, QualType Replacement,
                                 bool UseTypeSugar = true)
      : TreeTransform<SubstituteDeducedTypeTransform>(SemaRef),
        Replacement(Replacement), UseTypeSugar(UseTypeSugar) {}

  QualType TransformDesugared(TypeLocBuilder &TLB, DeducedTypeLoc TL) {
    QualType Result = Replacement;
    TLB.pushTrivial(SemaRef.Context, Result, TL.getBeginLoc());
    return Result;
  }

  QualType TransformAutoType(TypeLocBuilder &TLB, AutoTypeLoc TL) {
    if (!UseTypeSugar)
      return TransformDesugared(TLB, TL);

    QualType Result = SemaRef.Context.getAutoType(
        Replacement, TL.getTypePtr()->getKeyword(), Replacement.isNull());
    auto NewTL = TLB.push<AutoTypeLoc>(Result);
    NewTL.copy(TL);
    return Result;
  }

  QualType Apply(TypeLoc TL) {
    TypeLocBuilder TLB;
    TLB.reserve(TL.getFullDataSize());
    return TransformType(TLB, TL);
  }
};

} // namespace

QualType Sema::ReplaceAutoType(QualType TypeWithAuto,
                               QualType TypeToReplaceAuto) {
  return SubstituteDeducedTypeTransform(*this, TypeToReplaceAuto,
                                        /*UseTypeSugar*/ false)
      .TransformType(TypeWithAuto);
}

namespace {
QualType deduceAutoForC(QualType Pattern, QualType Init, bool StripTopLevelCV) {
  if (StripTopLevelCV) {
    Pattern = Pattern.getUnqualifiedType();
    Init = Init.getUnqualifiedType();
  }

  if (Pattern->getAs<AutoType>())
    return Init;

  if (const auto *PP = Pattern->getAs<PointerType>()) {
    const auto *IP = Init->getAs<PointerType>();
    if (!IP)
      return QualType();
    return deduceAutoForC(PP->getPointeeType(), IP->getPointeeType(), false);
  }

  return QualType();
}
} // namespace

Sema::AutoDeductionResult Sema::DeduceAutoType(TypeLoc Type, Expr *Init,
                                               QualType &Result) {
  if (Init->containsErrors())
    return ADK_AlreadyDiagnosed;

  const AutoType *AT = Type.getType()->getContainedAutoType();
  assert(AT);

  if (Init->getType()->isNonOverloadPlaceholderType()) {
    ExprResult NonPlaceholder = CheckPlaceholderExpr(Init);
    if (NonPlaceholder.isInvalid())
      return ADK_AlreadyDiagnosed;
    Init = NonPlaceholder.get();
  }

  auto *String = dyn_cast<StringLiteral>(Init);
  if (getLangOpts().C23 && String && Type.getType()->isArrayType()) {
    Diag(Type.getBeginLoc(), diag::ext_c23_auto_non_plain_identifier);
    TypeLoc TL = TypeLoc(Init->getType(), Type.getOpaqueData());
    Result = SubstituteDeducedTypeTransform(*this, QualType()).Apply(TL);
    assert(!Result.isNull() && "substituting DependentTy can't fail");
    return ADK_Success;
  }

  if (getLangOpts().C23 && Type.getType()->isPointerType())
    Diag(Type.getBeginLoc(), diag::ext_c23_auto_non_plain_identifier);

  if (auto *InitList = dyn_cast<InitListExpr>(Init)) {
    (void)InitList;
    Diag(Init->getBeginLoc(), diag::err_auto_init_list_from_c)
        << (AT->isGNUAutoType() ? 1 : 0) << getLangOpts().C23;
    return ADK_AlreadyDiagnosed;
  }

  if (Init->refersToBitField()) {
    Diag(Init->getExprLoc(), diag::err_auto_bitfield);
    return ADK_AlreadyDiagnosed;
  }

  QualType InitType = Init->getType();

  if (InitType->isArrayType())
    InitType = Context.getArrayDecayedType(InitType);
  else if (InitType->isFunctionType())
    InitType = Context.getPointerType(InitType);

  QualType DeducedType =
      deduceAutoForC(Type.getType(), InitType, /*StripTopLevelCV=*/true);
  if (DeducedType.isNull())
    return ADK_Invalid;

  if (!Result.isNull()) {
    if (!Context.hasSameType(DeducedType, Result)) {
      return ADK_Inconsistent;
    }
    DeducedType = Context.getCommonSugaredType(Result, DeducedType);
  }

  Result = SubstituteDeducedTypeTransform(*this, DeducedType).Apply(Type);
  if (Result.isNull())
    return ADK_AlreadyDiagnosed;

  return ADK_Success;
}

ValueDecl *
Sema::tryLookupUnambiguousFieldDecl(RecordDecl *RD,
                                    const IdentifierInfo *MemberOrBase) {
  for (auto *D : RD->lookup(MemberOrBase)) {
    if (isa<FieldDecl, IndirectFieldDecl>(D))
      return cast<ValueDecl>(D);
  }
  return nullptr;
}

ExprResult Sema::OnBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind) {
  llvm::APInt V(Context.getIntWidth(Context.BoolTy),
                Kind == tok::kw_true ? 1u : 0u);
  return IntegerLiteral::Create(Context, V, Context.BoolTy, OpLoc);
}

ExprResult Sema::OnNullPtrLiteral(SourceLocation Loc) {
  return new (Context) NullPtrLiteralExpr(Context.NullPtrTy, Loc);
}

Sema::OverloadKind Sema::CheckOverload(FunctionDecl *New,
                                       const LookupResult &Old,
                                       NamedDecl *&OldDecl) {
  OldDecl = nullptr;
  for (auto *OldD : Old) {
    if (FunctionDecl *OldFD = OldD->getAsFunction()) {
      if (!IsOverload(New, OldFD)) {
        OldDecl = OldD;
        return Ovl_Match;
      }
    } else {
      // Non-function declaration: end overload resolution here.
      OldDecl = OldD;
      return Ovl_NonFunction;
    }
  }
  return Ovl_Overload;
}

bool Sema::IsOverload(FunctionDecl *New, FunctionDecl *Old) {
  if (New->getNumParams() != Old->getNumParams())
    return true;
  for (unsigned I = 0, N = New->getNumParams(); I != N; ++I) {
    QualType NT = Context.getCanonicalType(New->getParamDecl(I)->getType());
    QualType OT = Context.getCanonicalType(Old->getParamDecl(I)->getType());
    if (NT != OT)
      return true;
  }
  return false;
}

void Sema::DiagnoseAutoDeductionFailure(VarDecl *VDecl, Expr *Init) {
  if (isa<InitListExpr>(Init))
    Diag(VDecl->getLocation(),
         diag::err_auto_var_deduction_failure_from_init_list)
        << VDecl->getDeclName() << VDecl->getType() << Init->getSourceRange();
  else
    Diag(VDecl->getLocation(), diag::err_auto_var_deduction_failure)
        << VDecl->getDeclName() << VDecl->getType() << Init->getType()
        << Init->getSourceRange();
}

bool Sema::SetMemberAccessSpecifier(NamedDecl *MemberDecl,
                                    NamedDecl *PrevMemberDecl,
                                    AccessSpecifier LexicalAS) {
  if (!PrevMemberDecl)
    MemberDecl->setAccess(LexicalAS);
  else
    MemberDecl->setAccess(PrevMemberDecl->getAccess());
  return false;
}
