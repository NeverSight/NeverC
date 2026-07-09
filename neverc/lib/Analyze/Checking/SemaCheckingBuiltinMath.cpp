#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Elementwise / reduce / matrix builtins (moved from SemaCheckingDiag.cpp)
// ===----------------------------------------------------------------------===

bool Sema::PrepareBuiltinElementwiseMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  QualType TyA = A.get()->getType();

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA))
    return true;

  TheCall->setType(TyA);
  return false;
}

bool Sema::SemaBuiltinElementwiseMath(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  ExprResult A = TheCall->getArg(0);
  ExprResult B = TheCall->getArg(1);
  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res =
      UsualArithmeticConversions(A, B, TheCall->getExprLoc(), ACK_Comparison);
  if (A.isInvalid() || B.isInvalid())
    return true;

  QualType TyA = A.get()->getType();
  QualType TyB = B.get()->getType();

  if (Res.isNull() || TyA.getCanonicalType() != TyB.getCanonicalType())
    return Diag(A.get()->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << TyA << TyB;

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA))
    return true;

  TheCall->setArg(0, A.get());
  TheCall->setArg(1, B.get());
  TheCall->setType(Res);
  return false;
}

bool Sema::SemaBuiltinElementwiseTernaryMath(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 3))
    return true;

  Expr *Args[3];
  for (int I = 0; I < 3; ++I) {
    ExprResult Converted = UsualUnaryConversions(TheCall->getArg(I));
    if (Converted.isInvalid())
      return true;
    Args[I] = Converted.get();
  }

  int ArgOrdinal = 1;
  for (Expr *Arg : Args) {
    if (checkFPMathBuiltinElementType(*this, Arg->getBeginLoc(), Arg->getType(),
                                      ArgOrdinal++))
      return true;
  }

  for (int I = 1; I < 3; ++I) {
    if (Args[0]->getType().getCanonicalType() !=
        Args[I]->getType().getCanonicalType()) {
      return Diag(Args[0]->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << Args[0]->getType() << Args[I]->getType();
    }

    TheCall->setArg(I, Args[I]);
  }

  TheCall->setType(Args[0]->getType());
  return false;
}

bool Sema::PrepareBuiltinReduceMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  return false;
}

bool Sema::SemaBuiltinNonDeterministicValue(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult Arg = TheCall->getArg(0);
  QualType TyArg = Arg.get()->getType();

  if (!TyArg->isBuiltinType() && !TyArg->isVectorType())
    return Diag(TheCall->getArg(0)->getBeginLoc(),
                diag::err_builtin_invalid_arg_type)
           << 1 << /*vector, integer or floating point ty*/ 0 << TyArg;

  TheCall->setType(TyArg);
  return false;
}

ExprResult Sema::SemaBuiltinMatrixTranspose(CallExpr *TheCall,
                                            ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 1))
    return ExprError();

  ExprResult MatrixArg = DefaultLvalueConversion(TheCall->getArg(0));
  if (MatrixArg.isInvalid())
    return MatrixArg;
  Expr *Matrix = MatrixArg.get();

  auto *MType = Matrix->getType()->getAs<ConstantMatrixType>();
  if (!MType) {
    Diag(Matrix->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /* matrix ty*/ 1 << Matrix->getType();
    return ExprError();
  }
  QualType ResultType = Context.getConstantMatrixType(
      MType->getElementType(), MType->getNumColumns(), MType->getNumRows());

  // Change the return type to the type of the returned matrix.
  TheCall->setType(ResultType);

  // Update call argument to use the possibly converted matrix argument.
  TheCall->setArg(0, Matrix);
  return CallResult;
}

// Get and verify the matrix dimensions.
std::optional<unsigned>
getAndVerifyMatrixDimension(Expr *Expr, llvm::StringRef Name, Sema &S) {
  SourceLocation ErrorPos;
  std::optional<llvm::APSInt> Value =
      Expr->getIntegerConstantExpr(S.Context, &ErrorPos);
  if (!Value) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_scalar_unsigned_arg)
        << Name;
    return {};
  }
  uint64_t Dim = Value->getZExtValue();
  if (!ConstantMatrixType::isDimensionValid(Dim)) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_invalid_dimension)
        << Name << ConstantMatrixType::getMaxElementsPerDimension();
    return {};
  }
  return Dim;
}

ExprResult Sema::SemaBuiltinMatrixColumnMajorLoad(CallExpr *TheCall,
                                                  ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 4))
    return ExprError();

  unsigned PtrArgIdx = 0;
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *RowsExpr = TheCall->getArg(1);
  Expr *ColumnsExpr = TheCall->getArg(2);
  Expr *StrideExpr = TheCall->getArg(3);

  bool ArgError = false;
  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(0, PtrExpr);
  }

  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  QualType ElementTy;
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    ElementTy = PtrTy->getPointeeType().getUnqualifiedType();

    if (!ConstantMatrixType::isValidElementType(ElementTy)) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << PtrArgIdx + 1 << /* pointer to element ty*/ 2
          << PtrExpr->getType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the expression to size_t.
  auto ApplyArgumentConversions = [this](Expr *E) {
    ExprResult Conv = DefaultLvalueConversion(E);
    if (Conv.isInvalid())
      return Conv;

    return tryConvertExprToType(Conv.get(), Context.getSizeType());
  };

  // Apply conversion to row and column expressions.
  ExprResult RowsConv = ApplyArgumentConversions(RowsExpr);
  if (!RowsConv.isInvalid()) {
    RowsExpr = RowsConv.get();
    TheCall->setArg(1, RowsExpr);
  } else
    RowsExpr = nullptr;

  ExprResult ColumnsConv = ApplyArgumentConversions(ColumnsExpr);
  if (!ColumnsConv.isInvalid()) {
    ColumnsExpr = ColumnsConv.get();
    TheCall->setArg(2, ColumnsExpr);
  } else
    ColumnsExpr = nullptr;
  std::optional<unsigned> MaybeRows;
  if (RowsExpr)
    MaybeRows = getAndVerifyMatrixDimension(RowsExpr, "row", *this);

  std::optional<unsigned> MaybeColumns;
  if (ColumnsExpr)
    MaybeColumns = getAndVerifyMatrixDimension(ColumnsExpr, "column", *this);
  ExprResult StrideConv = ApplyArgumentConversions(StrideExpr);
  if (StrideConv.isInvalid())
    return ExprError();
  StrideExpr = StrideConv.get();
  TheCall->setArg(3, StrideExpr);

  if (MaybeRows) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < *MaybeRows) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError || !MaybeRows || !MaybeColumns)
    return ExprError();

  TheCall->setType(
      Context.getConstantMatrixType(ElementTy, *MaybeRows, *MaybeColumns));
  return CallResult;
}

ExprResult Sema::SemaBuiltinMatrixColumnMajorStore(CallExpr *TheCall,
                                                   ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 3))
    return ExprError();

  unsigned PtrArgIdx = 1;
  Expr *MatrixExpr = TheCall->getArg(0);
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *StrideExpr = TheCall->getArg(2);

  bool ArgError = false;

  {
    ExprResult MatrixConv = DefaultLvalueConversion(MatrixExpr);
    if (MatrixConv.isInvalid())
      return MatrixConv;
    MatrixExpr = MatrixConv.get();
    TheCall->setArg(0, MatrixExpr);
  }
  auto *MatrixTy = MatrixExpr->getType()->getAs<ConstantMatrixType>();
  if (!MatrixTy) {
    Diag(MatrixExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /*matrix ty */ 1 << MatrixExpr->getType();
    ArgError = true;
  }

  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(1, PtrExpr);
  }
  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    QualType ElementTy = PtrTy->getPointeeType();
    if (ElementTy.isConstQualified()) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_matrix_store_to_const);
      ArgError = true;
    }
    ElementTy = ElementTy.getUnqualifiedType().getCanonicalType();
    if (MatrixTy &&
        !Context.hasSameType(ElementTy, MatrixTy->getElementType())) {
      Diag(PtrExpr->getBeginLoc(),
           diag::err_builtin_matrix_pointer_arg_mismatch)
          << ElementTy << MatrixTy->getElementType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the stride expression to
  // size_t.
  {
    ExprResult StrideConv = DefaultLvalueConversion(StrideExpr);
    if (StrideConv.isInvalid())
      return StrideConv;

    StrideConv = tryConvertExprToType(StrideConv.get(), Context.getSizeType());
    if (StrideConv.isInvalid())
      return StrideConv;
    StrideExpr = StrideConv.get();
    TheCall->setArg(2, StrideExpr);
  }
  if (MatrixTy) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < MatrixTy->getNumRows()) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError)
    return ExprError();

  return CallResult;
}
