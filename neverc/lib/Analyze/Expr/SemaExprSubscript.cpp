//===- SemaExprSubscript.cpp - Array/matrix subscript semantic analysis ---===//
//
// Extracted from SemaExpr.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
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
// Array subscript & matrix subscript
// ===----------------------------------------------------------------------===

ExprResult Sema::OnArraySubscriptExpr(Scope *S, Expr *base,
                                      SourceLocation lbLoc,
                                      MultiExprArg ArgExprs,
                                      SourceLocation rbLoc) {

  // Since this might be a postfix expression, get rid of ParenListExprs.
  if (isa<ParenListExpr>(base)) {
    ExprResult result = MaybeConvertParenListExprToParenExpr(S, base);
    if (result.isInvalid())
      return ExprError();
    base = result.get();
  }

  // Check if base and idx form a MatrixSubscriptExpr.
  //
  // Helper to check for comma expressions, which are not allowed as indices for
  // matrix subscript expressions.
  auto CheckAndReportCommaError = [this, base, rbLoc](Expr *E) {
    if (isa<BinaryOperator>(E) && cast<BinaryOperator>(E)->isCommaOp()) {
      Diag(E->getExprLoc(), diag::err_matrix_subscript_comma)
          << SourceRange(base->getBeginLoc(), rbLoc);
      return true;
    }
    return false;
  };
  // The matrix subscript operator ([][])is considered a single operator.
  // Separating the index expressions by parenthesis is not allowed.
  if (base && !base->getType().isNull() &&
      base->hasPlaceholderType(BuiltinType::IncompleteMatrixIdx) &&
      !isa<MatrixSubscriptExpr>(base)) {
    Diag(base->getExprLoc(), diag::err_matrix_separate_incomplete_index)
        << SourceRange(base->getBeginLoc(), rbLoc);
    return ExprError();
  }
  // If the base is a MatrixSubscriptExpr, try to create a new
  // MatrixSubscriptExpr.
  auto *matSubscriptE = dyn_cast<MatrixSubscriptExpr>(base);
  if (matSubscriptE) {
    assert(ArgExprs.size() == 1);
    if (CheckAndReportCommaError(ArgExprs.front()))
      return ExprError();

    assert(matSubscriptE->isIncomplete() &&
           "base has to be an incomplete matrix subscript");
    return CreateBuiltinMatrixSubscriptExpr(matSubscriptE->getBase(),
                                            matSubscriptE->getRowIdx(),
                                            ArgExprs.front(), rbLoc);
  }
  // Handle any non-overload placeholder types in the base and index
  // expressions.  We can't handle overloads here because the other
  // operand might be an overloadable type, in which case the overload
  // resolution for the operator overload should get the first crack
  // at the overload.
  if (base->getType()->isNonOverloadPlaceholderType()) {
    {
      ExprResult result = CheckPlaceholderExpr(base);
      if (result.isInvalid())
        return ExprError();
      base = result.get();
    }
  }

  // If the base is a matrix type, try to create a new MatrixSubscriptExpr.
  if (base->getType()->isMatrixType()) {
    assert(ArgExprs.size() == 1);
    if (CheckAndReportCommaError(ArgExprs.front()))
      return ExprError();

    return CreateBuiltinMatrixSubscriptExpr(base, ArgExprs.front(), nullptr,
                                            rbLoc);
  }

  if (ArgExprs.size() == 1 &&
      ArgExprs[0]->getType()->isNonOverloadPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(ArgExprs[0]);
    if (result.isInvalid())
      return ExprError();
    ArgExprs[0] = result.get();
  } else {
    if (resolveArgPlaceholders(*this, ArgExprs))
      return ExprError();
  }

  // NeverC builtin string `s[i]` rewrite -> `neverc_string_at(s, i)`.
  // The dotted-call `s.at(i)` already reaches the same runtime helper
  // through `BuiltinStringMethodNames.def`, but std::string users
  // expect `s[i]` to work too.  Plain `char buf[N]; buf[i]` keeps
  // reaching the regular array path because `buf`'s type is
  // `char[N]`, not the NeverC `string` record -- only base expressions
  // whose evaluated type IS the builtin string type take this branch.
  // The receiver is consumed by the runtime helper per the by-value
  // contract; an lvalue receiver gets the implicit retain copy Sema
  // inserts in `GatherArgumentsForCall` so writing `s[i]` does not
  // dangle the caller's owned buffer.  Read-only by construction: the
  // helper returns `char` (prvalue), so `s[i] = ch` is rejected at
  // ordinary assignment-expression typing time -- mutation has to go
  // through `s.replace_char(i, 1, ch)` / `s = ...` instead, matching
  // the value-typed mutation contract the rest of the surface uses.
  if (ArgExprs.size() == 1 && this->isNeverCStringType(base->getType())) {
    Expr *IdxExpr = ArgExprs[0];
    QualType IdxTy = IdxExpr->getType();
    if (!IdxTy->isIntegerType() || IdxTy->isBooleanType()) {
      Diag(IdxExpr->getBeginLoc(), diag::err_typecheck_subscript_not_integer)
          << IdxExpr->getSourceRange();
      return ExprError();
    }
    ExprResult IdxRes = DefaultLvalueConversion(IdxExpr);
    if (IdxRes.isInvalid())
      return ExprError();
    QualType SizeTy = Context.getSizeType();
    if (IdxRes.get()->getType() != SizeTy) {
      IdxRes = ImpCastExprToType(IdxRes.get(), SizeTy, CK_IntegralCast);
      if (IdxRes.isInvalid())
        return ExprError();
    }
    Expr *Args[] = {base, IdxRes.get()};
    return buildNeverCStringRuntimeCall(
        *this, S, lbLoc, BuiltinStringNames::AtFunctionName, Args, rbLoc);
  }

  ExprResult Res =
      CreateBuiltinArraySubscriptExpr(base, lbLoc, ArgExprs.front(), rbLoc);

  if (!Res.isInvalid() && isa<ArraySubscriptExpr>(Res.get()))
    CheckSubscriptAccessOfNoDeref(cast<ArraySubscriptExpr>(Res.get()));

  return Res;
}

ExprResult Sema::tryConvertExprToType(Expr *E, QualType Ty) {
  InitializedEntity Entity = InitializedEntity::InitializeTemporary(Ty);
  InitializationKind Kind =
      InitializationKind::CreateCopy(E->getBeginLoc(), SourceLocation());
  InitializationSequence InitSeq(*this, Entity, Kind, E);
  return InitSeq.Perform(*this, Entity, Kind, E);
}

ExprResult Sema::CreateBuiltinMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                                  Expr *ColumnIdx,
                                                  SourceLocation RBLoc) {
  ExprResult BaseR = CheckPlaceholderExpr(Base);
  if (BaseR.isInvalid())
    return BaseR;
  Base = BaseR.get();

  ExprResult RowR = CheckPlaceholderExpr(RowIdx);
  if (RowR.isInvalid())
    return RowR;
  RowIdx = RowR.get();

  if (!ColumnIdx)
    return new (Context) MatrixSubscriptExpr(
        Base, RowIdx, ColumnIdx, Context.IncompleteMatrixIdxTy, RBLoc);

  // Build an unanalyzed expression if any of the operands is type-dependent.

  ExprResult ColumnR = CheckPlaceholderExpr(ColumnIdx);
  if (ColumnR.isInvalid())
    return ColumnR;
  ColumnIdx = ColumnR.get();

  // Check that IndexExpr is an integer expression. If it is a constant
  // expression, check that it is less than Dim (= the number of elements in the
  // corresponding dimension).
  auto IsIndexValid = [&](Expr *IndexExpr, unsigned Dim,
                          bool IsColumnIdx) -> Expr * {
    if (!IndexExpr->getType()->isIntegerType()) {
      Diag(IndexExpr->getBeginLoc(), diag::err_matrix_index_not_integer)
          << IsColumnIdx;
      return nullptr;
    }

    if (std::optional<llvm::APSInt> Idx =
            IndexExpr->getIntegerConstantExpr(Context)) {
      if ((*Idx < 0 || *Idx >= Dim)) {
        Diag(IndexExpr->getBeginLoc(), diag::err_matrix_index_outside_range)
            << IsColumnIdx << Dim;
        return nullptr;
      }
    }

    ExprResult ConvExpr =
        tryConvertExprToType(IndexExpr, Context.getSizeType());
    assert(!ConvExpr.isInvalid() &&
           "should be able to convert any integer type to size type");
    return ConvExpr.get();
  };

  auto *MTy = Base->getType()->getAs<ConstantMatrixType>();
  RowIdx = IsIndexValid(RowIdx, MTy->getNumRows(), false);
  ColumnIdx = IsIndexValid(ColumnIdx, MTy->getNumColumns(), true);
  if (!RowIdx || !ColumnIdx)
    return ExprError();

  return new (Context) MatrixSubscriptExpr(Base, RowIdx, ColumnIdx,
                                           MTy->getElementType(), RBLoc);
}

void Sema::CheckAddressOfNoDeref(const Expr *E) {
  ExpressionEvaluationContextRecord &LastRecord = ExprEvalContexts.back();
  const Expr *StrippedExpr = E->IgnoreParenImpCasts();

  // For expressions like `&(*s).b`, the base is recorded and what should be
  // checked.
  const MemberExpr *Member = nullptr;
  while ((Member = dyn_cast<MemberExpr>(StrippedExpr)) && !Member->isArrow())
    StrippedExpr = Member->getBase()->IgnoreParenImpCasts();

  LastRecord.PossibleDerefs.erase(StrippedExpr);
}

void Sema::CheckSubscriptAccessOfNoDeref(const ArraySubscriptExpr *E) {
  if (isUnevaluatedContext())
    return;

  QualType ResultTy = E->getType();
  ExpressionEvaluationContextRecord &LastRecord = ExprEvalContexts.back();

  // Bail if the element is an array since it is not memory access.
  if (isa<ArrayType>(ResultTy))
    return;

  if (ResultTy->hasAttr(attr::NoDeref)) {
    LastRecord.PossibleDerefs.insert(E);
    return;
  }

  // Check if the base type is a pointer to a member access of a struct
  // marked with noderef.
  const Expr *Base = E->getBase();
  QualType BaseTy = Base->getType();
  if (!(isa<ArrayType>(BaseTy) || isa<PointerType>(BaseTy)))
    // Not a pointer access
    return;

  const MemberExpr *Member = nullptr;
  while ((Member = dyn_cast<MemberExpr>(Base->IgnoreParenCasts())) &&
         Member->isArrow())
    Base = Member->getBase();

  if (const auto *Ptr = dyn_cast<PointerType>(Base->getType())) {
    if (Ptr->getPointeeType()->hasAttr(attr::NoDeref))
      LastRecord.PossibleDerefs.insert(E);
  }
}

ExprResult Sema::CreateBuiltinArraySubscriptExpr(Expr *Base,
                                                 SourceLocation LLoc, Expr *Idx,
                                                 SourceLocation RLoc) {
  Expr *LHSExp = Base;
  Expr *RHSExp = Idx;

  ExprValueKind VK = VK_LValue;
  ExprObjectKind OK = OK_Ordinary;

  if (LLVM_LIKELY(!LHSExp->getType()->getAs<VectorType>())) {
    ExprResult Result = DefaultFunctionArrayLvalueConversion(LHSExp);
    if (LLVM_UNLIKELY(Result.isInvalid()))
      return ExprError();
    LHSExp = Result.get();
  }
  ExprResult Result = DefaultFunctionArrayLvalueConversion(RHSExp);
  if (LLVM_UNLIKELY(Result.isInvalid()))
    return ExprError();
  RHSExp = Result.get();

  QualType LHSTy = LHSExp->getType(), RHSTy = RHSExp->getType();

  // C99 6.5.2.1p2: the expression e1[e2] is by definition precisely equivalent
  // to the expression *((e1)+(e2)). This means the array "Base" may actually be
  // in the subscript position. As a result, we need to derive the array base
  // and index from the expression types.
  Expr *BaseExpr, *IndexExpr;
  QualType ResultType;
  if (const PointerType *PTy = LHSTy->getAs<PointerType>()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const PointerType *PTy = RHSTy->getAs<PointerType>()) {
    // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = PTy->getPointeeType();
  } else if (const VectorType *VTy = LHSTy->getAs<VectorType>()) {
    BaseExpr = LHSExp; // vectors: V[123]
    IndexExpr = RHSExp;
    VK = LHSExp->getValueKind();
    if (VK != VK_PRValue)
      OK = OK_VectorComponent;

    ResultType = VTy->getElementType();
    QualType BaseType = BaseExpr->getType();
    Qualifiers BaseQuals = BaseType.getQualifiers();
    Qualifiers MemberQuals = ResultType.getQualifiers();
    Qualifiers Combined = BaseQuals + MemberQuals;
    if (Combined != MemberQuals)
      ResultType = Context.getQualifiedType(ResultType, Combined);
  } else if (LHSTy->isBuiltinType() &&
             LHSTy->getAs<BuiltinType>()->isSveVLSBuiltinType()) {
    const BuiltinType *BTy = LHSTy->getAs<BuiltinType>();
    if (BTy->isSVEBool())
      return ExprError(Diag(LLoc, diag::err_subscript_svbool_t)
                       << LHSExp->getSourceRange() << RHSExp->getSourceRange());

    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    VK = LHSExp->getValueKind();
    if (VK != VK_PRValue)
      OK = OK_VectorComponent;

    ResultType = BTy->getSveEltType(Context);

    QualType BaseType = BaseExpr->getType();
    Qualifiers BaseQuals = BaseType.getQualifiers();
    Qualifiers MemberQuals = ResultType.getQualifiers();
    Qualifiers Combined = BaseQuals + MemberQuals;
    if (Combined != MemberQuals)
      ResultType = Context.getQualifiedType(ResultType, Combined);
  } else if (LHSTy->isArrayType()) {
    // If we see an array that wasn't promoted by
    // DefaultFunctionArrayLvalueConversion, it must be an array that
    // wasn't promoted because of the C90 rule that doesn't
    // allow promoting non-lvalue arrays.  Warn, then
    // force the promotion here.
    Diag(LHSExp->getBeginLoc(), diag::ext_subscript_non_lvalue)
        << LHSExp->getSourceRange();
    LHSExp = ImpCastExprToType(LHSExp, Context.getArrayDecayedType(LHSTy),
                               CK_ArrayToPointerDecay)
                 .get();
    LHSTy = LHSExp->getType();

    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    ResultType = LHSTy->castAs<PointerType>()->getPointeeType();
  } else if (RHSTy->isArrayType()) {
    // Same as previous, except for 123[f().a] case
    Diag(RHSExp->getBeginLoc(), diag::ext_subscript_non_lvalue)
        << RHSExp->getSourceRange();
    RHSExp = ImpCastExprToType(RHSExp, Context.getArrayDecayedType(RHSTy),
                               CK_ArrayToPointerDecay)
                 .get();
    RHSTy = RHSExp->getType();

    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    ResultType = RHSTy->castAs<PointerType>()->getPointeeType();
  } else {
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_value)
                     << LHSExp->getSourceRange() << RHSExp->getSourceRange());
  }
  // C99 6.5.2.1p1
  if (!IndexExpr->getType()->isIntegerType())
    return ExprError(Diag(LLoc, diag::err_typecheck_subscript_not_integer)
                     << IndexExpr->getSourceRange());

  if (IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
      IndexExpr->getType()->isSpecificBuiltinType(BuiltinType::Char_U)) {
    std::optional<llvm::APSInt> IntegerContantExpr =
        IndexExpr->getIntegerConstantExpr(getTreeContext());
    if (!IntegerContantExpr.has_value() ||
        IntegerContantExpr.value().isNegative())
      Diag(LLoc, diag::warn_subscript_is_char) << IndexExpr->getSourceRange();
  }

  // Subscript base must be pointer to object type (not function); complete
  // object type required except where dependent.
  if (ResultType->isFunctionType()) {
    Diag(BaseExpr->getBeginLoc(), diag::err_subscript_function_type)
        << ResultType << BaseExpr->getSourceRange();
    return ExprError();
  }

  if (ResultType->isVoidType()) {
    // GNU extension: subscripting on pointer to void
    Diag(LLoc, diag::ext_gnu_subscript_void_type) << BaseExpr->getSourceRange();

    // C forbids expressions of unqualified void type from being l-values.
    // See IsCForbiddenLValueType.
    if (!ResultType.hasQualifiers())
      VK = VK_PRValue;
  } else if (RequireCompleteSizedType(
                 LLoc, ResultType,
                 diag::err_subscript_incomplete_or_sizeless_type, BaseExpr))
    return ExprError();

  assert(VK == VK_PRValue || !ResultType.isCForbiddenLValueType());

  return new (Context)
      ArraySubscriptExpr(LHSExp, RHSExp, ResultType, VK, OK, RLoc);
}

