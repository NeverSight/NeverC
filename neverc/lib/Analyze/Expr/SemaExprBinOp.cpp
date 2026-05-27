#include "TreeTransform.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace neverc {
template <class Derived>
class ReferencedDeclWalker : public EvaluatedExprVisitor<Derived> {
protected:
  Sema &S;

public:
  typedef EvaluatedExprVisitor<Derived> Inherited;

  ReferencedDeclWalker(Sema &S) : Inherited(S.Context), S(S) {}

  Derived &asImpl() { return *static_cast<Derived *>(this); }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    auto *D = E->getDecl();
    if (isa<FunctionDecl>(D) || isa<VarDecl>(D)) {
      asImpl().visitUsedDecl(E->getLocation(), D);
    }
  }

  void VisitMemberExpr(MemberExpr *E) {
    auto *D = E->getMemberDecl();
    if (isa<FunctionDecl>(D) || isa<VarDecl>(D)) {
      asImpl().visitUsedDecl(E->getMemberLoc(), D);
    }
    asImpl().Visit(E->getBase());
  }

  void VisitInitListExpr(InitListExpr *ILE) {
    if (ILE->hasArrayFiller())
      asImpl().Visit(ILE->getArrayFiller());
    Inherited::VisitInitListExpr(ILE);
  }

  void visitUsedDecl(SourceLocation Loc, Decl *D) {}
};
} // namespace neverc

// ===----------------------------------------------------------------------===
// Helpers shared with SemaExpr.cpp
// ===----------------------------------------------------------------------===

namespace {

struct OriginalOperand {
  explicit OriginalOperand(Expr *Op) : Orig(Op) {
    if (auto *ICE = dyn_cast<ImplicitCastExpr>(Op))
      Orig = ICE->getSubExprAsWritten();
  }

  QualType getType() const { return Orig->getType(); }

  Expr *Orig;
};

void suggestParentheses(Sema &Self, SourceLocation Loc,
                        const PartialDiagnostic &Note, SourceRange ParenRange) {
  SourceLocation EndLoc = Self.getLocForEndOfToken(ParenRange.getEnd());
  if (ParenRange.getBegin().isFileID() && ParenRange.getEnd().isFileID() &&
      EndLoc.isValid()) {
    Self.Diag(Loc, Note) << FixItHint::CreateInsertion(ParenRange.getBegin(),
                                                       "(")
                         << FixItHint::CreateInsertion(EndLoc, ")");
  } else {
    Self.Diag(Loc, Note) << ParenRange;
  }
}

bool couldBeNeverCStringOperand(Expr *E) {
  QualType T = E->getType();
  if (T.isNull())
    return false;
  const Type *TP = T.getTypePtr();
  return TP->isRecordType() || TP->isArrayType() || TP->isPointerType();
}

bool isNeverCStringOperand(Sema &S, Expr *E) {
  if (!couldBeNeverCStringOperand(E))
    return false;
  return S.isNeverCStringType(E->getType()) || getNeverCStringLiteral(E);
}

QualType checkRealImagOperand(Sema &S, ExprResult &V, SourceLocation Loc,
                              bool IsReal) {
  if (V.get()->isTypeDependent())
    return S.Context.DependentTy;

  if (V.get()->getObjectKind() != OK_Ordinary) {
    V = S.DefaultLvalueConversion(V.get());
    if (V.isInvalid())
      return QualType();
  }

  if (const ComplexType *CT = V.get()->getType()->getAs<ComplexType>())
    return CT->getElementType();

  if (V.get()->getType()->isArithmeticType())
    return V.get()->getType();

  ExprResult PR = S.CheckPlaceholderExpr(V.get());
  if (PR.isInvalid())
    return QualType();
  if (PR.get() != V.get()) {
    V = PR;
    return checkRealImagOperand(S, V, Loc, IsReal);
  }

  S.Diag(Loc, diag::err_realimag_invalid_type)
      << V.get()->getType() << (IsReal ? "__real" : "__imag");
  return QualType();
}

void warnNullPtrDeref(Sema &S, Expr *E) {
  const auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParenCasts());
  if (UO && UO->getOpcode() == UO_Deref &&
      UO->getSubExpr()->getType()->isPointerType()) {
    const LangAS AS =
        UO->getSubExpr()->getType()->getPointeeType().getAddressSpace();
    if ((!isTargetAddressSpace(AS) ||
         (isTargetAddressSpace(AS) && toTargetAddressSpace(AS) == 0)) &&
        UO->getSubExpr()->IgnoreParenCasts()->isNullPointerConstant(
            S.Context, Expr::NPC_ValueDependentIsNotNull) &&
        !UO->getType().isVolatileQualified()) {
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::warn_indirection_through_null)
                                << UO->getSubExpr()->getSourceRange());
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::note_indirection_through_null));
    }
  }
}

bool isVector(QualType QT, QualType ElementType) {
  if (const VectorType *VT = QT->getAs<VectorType>())
    return VT->getElementType().getCanonicalType() == ElementType;
  return false;
}

ExprResult buildNeverCStringConcat(Sema &S, SourceLocation OpLoc, Expr *LHS,
                                   Expr *RHS) {
  Expr *Args[] = {LHS, RHS};
  return buildNeverCStringRuntimeCall(S, /*Scope=*/nullptr, OpLoc,
                                      BuiltinStringNames::ConcatFunctionName,
                                      Args, OpLoc);
}

ExprResult buildNeverCStringCompare(Sema &S, SourceLocation OpLoc,
                                    BinaryOperatorKind Opc, Expr *LHS,
                                    Expr *RHS) {
  assert(BinaryOperator::isComparisonOp(Opc) &&
         "only comparison operators compare neverc strings");

  Expr *Args[] = {LHS, RHS};

  if (BinaryOperator::isEqualityOp(Opc)) {
    const CallExpr *DecryptCall = getDecryptLiteralCall(RHS);
    Expr *OtherOperand = LHS;
    if (!DecryptCall) {
      DecryptCall = getDecryptLiteralCall(LHS);
      OtherOperand = RHS;
    }

    if (DecryptCall && DecryptCall->getNumArgs() == 3) {
      Expr *DecryptArgs[] = {
          OtherOperand,
          const_cast<Expr *>(DecryptCall->getArg(0)),
          const_cast<Expr *>(DecryptCall->getArg(1)),
          const_cast<Expr *>(DecryptCall->getArg(2))};
      ExprResult Eq = buildNeverCStringRuntimeCall(
          S, /*Scope=*/nullptr, OpLoc,
          BuiltinStringNames::DecryptEqualsFunctionName, DecryptArgs, OpLoc);
      if (Eq.isInvalid() || Opc == BO_EQ)
        return Eq;
      return S.FormUnaryOp(/*Scope=*/nullptr, OpLoc, UO_LNot, Eq.get());
    }

    ExprResult Eq = buildNeverCStringRuntimeCall(
        S, /*Scope=*/nullptr, OpLoc, BuiltinStringNames::EqualFunctionName,
        Args, OpLoc);
    if (Eq.isInvalid() || Opc == BO_EQ)
      return Eq;
    return S.FormUnaryOp(/*Scope=*/nullptr, OpLoc, UO_LNot, Eq.get());
  }

  {
    const CallExpr *DecryptCall = getDecryptLiteralCall(RHS);
    Expr *OtherOperand = LHS;
    bool DecryptOnLHS = false;
    if (!DecryptCall) {
      DecryptCall = getDecryptLiteralCall(LHS);
      OtherOperand = RHS;
      DecryptOnLHS = true;
    }
    if (DecryptCall && DecryptCall->getNumArgs() == 3) {
      Expr *DecryptArgs[] = {
          OtherOperand,
          const_cast<Expr *>(DecryptCall->getArg(0)),
          const_cast<Expr *>(DecryptCall->getArg(1)),
          const_cast<Expr *>(DecryptCall->getArg(2))};
      ExprResult Cmp = buildNeverCStringRuntimeCall(
          S, /*Scope=*/nullptr, OpLoc,
          BuiltinStringNames::DecryptCompareFunctionName, DecryptArgs, OpLoc);
      if (Cmp.isInvalid())
        return Cmp;
      ExprResult Zero = S.OnIntegerConstant(OpLoc, /*Val=*/0);
      if (Zero.isInvalid())
        return ExprError();
      BinaryOperatorKind FinalOpc =
          DecryptOnLHS ? BinaryOperator::reverseComparisonOp(Opc) : Opc;
      return S.FormBinOp(/*Scope=*/nullptr, OpLoc, FinalOpc, Cmp.get(),
                         Zero.get());
    }
  }
  ExprResult Cmp = buildNeverCStringRuntimeCall(
      S, /*Scope=*/nullptr, OpLoc, BuiltinStringNames::CompareFunctionName,
      Args, OpLoc);
  if (Cmp.isInvalid())
    return Cmp;
  ExprResult Zero = S.OnIntegerConstant(OpLoc, /*Val=*/0);
  if (Zero.isInvalid())
    return ExprError();
  return S.FormBinOp(/*Scope=*/nullptr, OpLoc, Opc, Cmp.get(), Zero.get());
}

} // namespace

// ===----------------------------------------------------------------------===
// Binary operators (arithmetic, shift, compare, assign)
// ===----------------------------------------------------------------------===

QualType Sema::InvalidOperands(SourceLocation Loc, ExprResult &LHS,
                               ExprResult &RHS) {
  OriginalOperand OrigLHS(LHS.get()), OrigRHS(RHS.get());

  Diag(Loc, diag::err_typecheck_invalid_operands)
      << OrigLHS.getType() << OrigRHS.getType() << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();

  return QualType();
}

// Diagnose invalid vector logical operands (including scalar/vector mixes).
QualType Sema::InvalidLogicalVectorOperands(SourceLocation Loc, ExprResult &LHS,
                                            ExprResult &RHS) {
  QualType LHSType = LHS.get()->IgnoreImpCasts()->getType();
  QualType RHSType = RHS.get()->IgnoreImpCasts()->getType();

  bool LHSNatVec = LHSType->isVectorType();
  bool RHSNatVec = RHSType->isVectorType();

  if (!(LHSNatVec && RHSNatVec)) {
    Expr *Vector = LHSNatVec ? LHS.get() : RHS.get();
    Expr *NonVector = !LHSNatVec ? LHS.get() : RHS.get();
    Diag(Loc, diag::err_typecheck_logical_vector_expr_restrict)
        << 0 << Vector->getType() << NonVector->IgnoreImpCasts()->getType()
        << Vector->getSourceRange();
    return QualType();
  }

  Diag(Loc, diag::err_typecheck_logical_vector_expr_restrict)
      << 1 << LHSType << RHSType << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();

  return QualType();
}

namespace {
bool attemptVectorConvertSplat(Sema &S, ExprResult *scalar, QualType scalarTy,
                               QualType vectorEltTy, QualType vectorTy,
                               unsigned &DiagID) {
  // The conversion to apply to the scalar before splatting it,
  // if necessary.
  CastKind scalarCast = CK_NoOp;

  if (vectorEltTy->isIntegralType(S.Context)) {
    if (!scalarTy->isIntegralType(S.Context))
      return true;
    scalarCast = CK_IntegralCast;
  } else if (vectorEltTy->isRealFloatingType()) {
    if (scalarTy->isRealFloatingType()) {
      scalarCast = CK_FloatingCast;
    } else if (scalarTy->isIntegralType(S.Context))
      scalarCast = CK_IntegralToFloating;
    else
      return true;
  } else {
    return true;
  }

  // Adjust scalar if desired.
  if (scalar) {
    if (scalarCast != CK_NoOp)
      *scalar = S.ImpCastExprToType(scalar->get(), vectorEltTy, scalarCast);
    *scalar = S.ImpCastExprToType(scalar->get(), vectorTy, CK_VectorSplat);
  }
  return false;
}
} // namespace

namespace neverc {
ExprResult castVectorElement(Expr *E, QualType ElementType, Sema &S) {
  const auto *VecTy = E->getType()->getAs<VectorType>();
  assert(VecTy && "Expression E must be a vector");
  QualType NewVecTy =
      VecTy->isExtVectorType()
          ? S.Context.getExtVectorType(ElementType, VecTy->getNumElements())
          : S.Context.getVectorType(ElementType, VecTy->getNumElements(),
                                    VecTy->getVectorKind());

  // Look through the implicit cast. Return the subexpression if its type is
  // NewVecTy.
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    if (ICE->getSubExpr()->getType() == NewVecTy)
      return ICE->getSubExpr();

  auto Cast = ElementType->isIntegerType() ? CK_IntegralCast : CK_FloatingCast;
  return S.ImpCastExprToType(E, NewVecTy, Cast);
}
} // namespace

namespace {
bool canPromoteIntToInt(Sema &S, ExprResult *Int, QualType OtherIntTy) {
  QualType IntTy = Int->get()->getType().getUnqualifiedType();

  // Reject cases where the value of the Int is unknown as that would
  // possibly cause truncation, but accept cases where the scalar can be
  // demoted without loss of precision.
  Expr::EvalResult EVResult;
  bool CstInt = Int->get()->EvaluateAsInt(EVResult, S.Context);
  int Order = S.Context.getIntegerTypeOrder(OtherIntTy, IntTy);
  bool IntSigned = IntTy->hasSignedIntegerRepresentation();
  bool OtherIntSigned = OtherIntTy->hasSignedIntegerRepresentation();

  if (CstInt) {
    // If the scalar is constant and is of a higher order and has more active
    // bits that the vector element type, reject it.
    llvm::APSInt Result = EVResult.Val.getInt();
    unsigned NumBits = IntSigned
                           ? (Result.isNegative() ? Result.getSignificantBits()
                                                  : Result.getActiveBits())
                           : Result.getActiveBits();
    if (Order < 0 && S.Context.getIntWidth(OtherIntTy) < NumBits)
      return true;

    // If the signedness of the scalar type and the vector element type
    // differs and the number of bits is greater than that of the vector
    // element reject it.
    return (IntSigned != OtherIntSigned &&
            NumBits > S.Context.getIntWidth(OtherIntTy));
  }

  // Reject cases where the value of the scalar is not constant and it's
  // order is greater than that of the vector element type.
  return (Order < 0);
}
} // namespace

namespace {
bool canPromoteIntToFloat(Sema &S, ExprResult *Int, QualType FloatTy) {
  QualType IntTy = Int->get()->getType().getUnqualifiedType();

  // Determine if the integer constant can be expressed as a floating point
  // number of the appropriate type.
  Expr::EvalResult EVResult;
  bool CstInt = Int->get()->EvaluateAsInt(EVResult, S.Context);

  uint64_t Bits = 0;
  if (CstInt) {
    // Reject constants that would be truncated if they were converted to
    // the floating point type. Test by simple to/from conversion.
    // Ideally the conversion to an APFloat and from an APFloat
    //        could be avoided if there was a convertFromAPInt method
    //        which could signal back if implicit truncation occurred.
    llvm::APSInt Result = EVResult.Val.getInt();
    llvm::APFloat Float(S.Context.getFloatTypeSemantics(FloatTy));
    Float.convertFromAPInt(Result, IntTy->hasSignedIntegerRepresentation(),
                           llvm::APFloat::rmTowardZero);
    llvm::APSInt ConvertBack(S.Context.getIntWidth(IntTy),
                             !IntTy->hasSignedIntegerRepresentation());
    bool Ignored = false;
    Float.convertToInteger(ConvertBack, llvm::APFloat::rmNearestTiesToEven,
                           &Ignored);
    if (Result != ConvertBack)
      return true;
  } else {
    // Reject types that cannot be fully encoded into the mantissa of
    // the float.
    Bits = S.Context.getTypeSize(IntTy);
    unsigned FloatPrec = llvm::APFloat::semanticsPrecision(
        S.Context.getFloatTypeSemantics(FloatTy));
    if (Bits > FloatPrec)
      return true;
  }

  return false;
}
} // namespace

namespace {
bool attemptGCCVectorConvertSplat(Sema &S, ExprResult *Scalar,
                                  ExprResult *Vector) {
  QualType ScalarTy = Scalar->get()->getType().getUnqualifiedType();
  QualType VectorTy = Vector->get()->getType().getUnqualifiedType();
  QualType VectorEltTy;

  if (const auto *VT = VectorTy->getAs<VectorType>()) {
    assert(!isa<ExtVectorType>(VT) &&
           "ExtVectorTypes should not be handled here!");
    VectorEltTy = VT->getElementType();
  } else if (VectorTy->isSveVLSBuiltinType()) {
    VectorEltTy =
        VectorTy->castAs<BuiltinType>()->getSveEltType(S.getTreeContext());
  } else {
    llvm_unreachable("Only Fixed-Length and SVE Vector types are handled here");
  }

  // Reject cases where the vector element type or the scalar element type are
  // not integral or floating point types.
  if (!VectorEltTy->isArithmeticType() || !ScalarTy->isArithmeticType())
    return true;

  // The conversion to apply to the scalar before splatting it,
  // if necessary.
  CastKind ScalarCast = CK_NoOp;

  // Accept cases where the vector elements are integers and the scalar is
  // an integer.
  // Notionally if the scalar was a floating point value with a precise
  //        integral representation, we could cast it to an appropriate integer
  //        type and then perform the rest of the checks here. GCC will perform
  //        this conversion in some cases as determined by the input language.
  //        We should accept it on a language independent basis.
  if (VectorEltTy->isIntegralType(S.Context) &&
      ScalarTy->isIntegralType(S.Context) &&
      S.Context.getIntegerTypeOrder(VectorEltTy, ScalarTy)) {

    if (canPromoteIntToInt(S, Scalar, VectorEltTy))
      return true;

    ScalarCast = CK_IntegralCast;
  } else if (VectorEltTy->isIntegralType(S.Context) &&
             ScalarTy->isRealFloatingType()) {
    if (S.Context.getTypeSize(VectorEltTy) == S.Context.getTypeSize(ScalarTy))
      ScalarCast = CK_FloatingToIntegral;
    else
      return true;
  } else if (VectorEltTy->isRealFloatingType()) {
    if (ScalarTy->isRealFloatingType()) {

      // Reject cases where the scalar type is not a constant and has a higher
      // Order than the vector element type.
      llvm::APFloat Result(0.0);

      // Determine whether this is a constant scalar. In the event that the
      // value is dependent (and thus cannot be evaluated by the constant
      // evaluator), skip the evaluation. This will then diagnose once the
      // expression is instantiated.
      bool CstScalar = Scalar->get()->EvaluateAsFloat(Result, S.Context);
      int Order = S.Context.getFloatingTypeOrder(VectorEltTy, ScalarTy);
      if (!CstScalar && Order < 0)
        return true;

      // If the scalar cannot be safely casted to the vector element type,
      // reject it.
      if (CstScalar) {
        bool Truncated = false;
        Result.convert(S.Context.getFloatTypeSemantics(VectorEltTy),
                       llvm::APFloat::rmNearestTiesToEven, &Truncated);
        if (Truncated)
          return true;
      }

      ScalarCast = CK_FloatingCast;
    } else if (ScalarTy->isIntegralType(S.Context)) {
      if (canPromoteIntToFloat(S, Scalar, VectorEltTy))
        return true;

      ScalarCast = CK_IntegralToFloating;
    } else
      return true;
  } else if (ScalarTy->isEnumeralType())
    return true;

  // Adjust scalar if desired.
  if (ScalarCast != CK_NoOp)
    *Scalar = S.ImpCastExprToType(Scalar->get(), VectorEltTy, ScalarCast);
  *Scalar = S.ImpCastExprToType(Scalar->get(), VectorTy, CK_VectorSplat);
  return false;
}
} // namespace

QualType Sema::CheckVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                   SourceLocation Loc, bool IsCompAssign,
                                   bool AllowBothBool,
                                   bool AllowBoolConversions,
                                   bool AllowBoolOperation,
                                   bool ReportInvalid) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  const VectorType *LHSVecType = LHSType->getAs<VectorType>();
  const VectorType *RHSVecType = RHSType->getAs<VectorType>();
  assert(LHSVecType || RHSVecType);

  // This operation may not be performed on boolean vectors.
  if (!AllowBoolOperation &&
      (LHSType->isExtVectorBoolType() || RHSType->isExtVectorBoolType()))
    return ReportInvalid ? InvalidOperands(Loc, LHS, RHS) : QualType();

  // If the vector types are identical, return.
  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // If we have compatible vector types, prefer the LHS ExtVector type.
  if (LHSVecType && RHSVecType &&
      Context.areCompatibleVectorTypes(LHSType, RHSType)) {
    if (isa<ExtVectorType>(LHSVecType)) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_BitCast);
      return LHSType;
    }

    if (!IsCompAssign)
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_BitCast);
    return RHSType;
  }

  // Expressions containing fixed-length and sizeless SVE/RVV vectors are
  // invalid since the ambiguity can affect the ABI.
  auto IsSveRVVConversion = [](QualType FirstType, QualType SecondType,
                               unsigned &SVEorRVV) {
    const VectorType *VecType = SecondType->getAs<VectorType>();
    SVEorRVV = 0;
    if (FirstType->isSizelessBuiltinType() && VecType) {
      if (VecType->getVectorKind() == VectorKind::SveFixedLengthData ||
          VecType->getVectorKind() == VectorKind::SveFixedLengthPredicate)
        return true;
    }

    return false;
  };

  unsigned SVEorRVV;
  if (IsSveRVVConversion(LHSType, RHSType, SVEorRVV) ||
      IsSveRVVConversion(RHSType, LHSType, SVEorRVV)) {
    Diag(Loc, diag::err_typecheck_sve_rvv_ambiguous)
        << SVEorRVV << LHSType << RHSType;
    return QualType();
  }

  // Expressions containing GNU and SVE or RVV (fixed or sizeless) vectors are
  // invalid since the ambiguity can affect the ABI.
  auto IsSveRVVGnuConversion = [](QualType FirstType, QualType SecondType,
                                  unsigned &SVEorRVV) {
    const VectorType *FirstVecType = FirstType->getAs<VectorType>();
    const VectorType *SecondVecType = SecondType->getAs<VectorType>();

    SVEorRVV = 0;
    if (FirstVecType && SecondVecType) {
      if (FirstVecType->getVectorKind() == VectorKind::Generic) {
        if (SecondVecType->getVectorKind() == VectorKind::SveFixedLengthData ||
            SecondVecType->getVectorKind() ==
                VectorKind::SveFixedLengthPredicate)
          return true;
      }
      return false;
    }

    if (SecondVecType &&
        SecondVecType->getVectorKind() == VectorKind::Generic) {
      if (FirstType->isSVESizelessBuiltinType())
        return true;
    }

    return false;
  };

  if (IsSveRVVGnuConversion(LHSType, RHSType, SVEorRVV) ||
      IsSveRVVGnuConversion(RHSType, LHSType, SVEorRVV)) {
    Diag(Loc, diag::err_typecheck_sve_rvv_gnu_ambiguous)
        << SVEorRVV << LHSType << RHSType;
    return QualType();
  }

  // If there's a vector type and a scalar, try to convert the scalar to
  // the vector element type and splat.
  unsigned DiagID = diag::err_typecheck_vector_not_convertable;
  if (!RHSVecType) {
    if (isa<ExtVectorType>(LHSVecType)) {
      if (!attemptVectorConvertSplat(*this, &RHS, RHSType,
                                     LHSVecType->getElementType(), LHSType,
                                     DiagID))
        return LHSType;
    } else {
      if (!attemptGCCVectorConvertSplat(*this, &RHS, &LHS))
        return LHSType;
    }
  }
  if (!LHSVecType) {
    if (isa<ExtVectorType>(RHSVecType)) {
      if (!attemptVectorConvertSplat(*this, (IsCompAssign ? nullptr : &LHS),
                                     LHSType, RHSVecType->getElementType(),
                                     RHSType, DiagID))
        return RHSType;
    } else {
      if (LHS.get()->isLValue() ||
          !attemptGCCVectorConvertSplat(*this, &LHS, &RHS))
        return RHSType;
    }
  }

  // The code below also handles conversion between vectors and
  // non-scalars, we should break this down into fine grained specific checks
  // and emit proper diagnostics.
  QualType VecType = LHSVecType ? LHSType : RHSType;
  const VectorType *VT = LHSVecType ? LHSVecType : RHSVecType;
  QualType OtherType = LHSVecType ? RHSType : LHSType;
  ExprResult *OtherExpr = LHSVecType ? &RHS : &LHS;
  if (isLaxVectorConversion(OtherType, VecType)) {
    // If we're allowing lax vector conversions, only the total (data) size
    // needs to be the same. For non compound assignment, if one of the types is
    // scalar, the result is always the vector type.
    if (!IsCompAssign) {
      *OtherExpr = ImpCastExprToType(OtherExpr->get(), VecType, CK_BitCast);
      return VecType;
      // In a compound assignment, lhs += rhs, 'lhs' is a lvalue src, forbidding
      // any implicit cast. Here, the 'rhs' should be implicit casted to 'lhs'
      // type. Note that this is already done by non-compound assignments in
      // CheckAssignmentConstraints. If it's a scalar type, only bitcast for
      // <1 x T> -> T. The result is also a vector type.
    } else if (OtherType->isExtVectorType() || OtherType->isVectorType() ||
               (OtherType->isScalarType() && VT->getNumElements() == 1)) {
      ExprResult *RHSExpr = &RHS;
      *RHSExpr = ImpCastExprToType(RHSExpr->get(), LHSType, CK_BitCast);
      return VecType;
    }
  }

  // Okay, the expression is invalid.

  // If there's a non-vector, non-real operand, diagnose that.
  if ((!RHSVecType && !RHSType->isRealType()) ||
      (!LHSVecType && !LHSType->isRealType())) {
    Diag(Loc, diag::err_typecheck_vector_not_convertable_non_scalar)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  // If there is a vector type that is not a ExtVector and a scalar, we reach
  // this point if scalar could not be converted to the vector's element type
  // without truncation.
  if ((RHSVecType && !isa<ExtVectorType>(RHSVecType)) ||
      (LHSVecType && !isa<ExtVectorType>(LHSVecType))) {
    QualType Scalar = LHSVecType ? RHSType : LHSType;
    QualType Vector = LHSVecType ? LHSType : RHSType;
    unsigned ScalarOrVector = LHSVecType && RHSVecType ? 1 : 0;
    Diag(Loc, diag::err_typecheck_vector_not_convertable_implict_truncation)
        << ScalarOrVector << Scalar << Vector;

    return QualType();
  }

  // Otherwise, use the generic diagnostic.
  Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                    << RHS.get()->getSourceRange();
  return QualType();
}

QualType Sema::CheckSizelessVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign,
                                           ArithConvKind OperationKind) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  const BuiltinType *LHSBuiltinTy = LHSType->getAs<BuiltinType>();
  const BuiltinType *RHSBuiltinTy = RHSType->getAs<BuiltinType>();

  unsigned DiagID = diag::err_typecheck_invalid_operands;
  if ((OperationKind == ACK_Arithmetic) &&
      ((LHSBuiltinTy && LHSBuiltinTy->isSVEBool()) ||
       (RHSBuiltinTy && RHSBuiltinTy->isSVEBool()))) {
    Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                      << RHS.get()->getSourceRange();
    return QualType();
  }

  if (Context.hasSameType(LHSType, RHSType))
    return LHSType;

  if (LHSType->isSveVLSBuiltinType() && !RHSType->isSveVLSBuiltinType()) {
    if (!attemptGCCVectorConvertSplat(*this, &RHS, &LHS))
      return LHSType;
  }
  if (RHSType->isSveVLSBuiltinType() && !LHSType->isSveVLSBuiltinType()) {
    if (LHS.get()->isLValue() ||
        !attemptGCCVectorConvertSplat(*this, &LHS, &RHS))
      return RHSType;
  }

  if ((!LHSType->isSveVLSBuiltinType() && !LHSType->isRealType()) ||
      (!RHSType->isSveVLSBuiltinType() && !RHSType->isRealType())) {
    Diag(Loc, diag::err_typecheck_vector_not_convertable_non_scalar)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType() &&
      Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC !=
          Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC) {
    Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() || RHSType->isSveVLSBuiltinType()) {
    QualType Scalar = LHSType->isSveVLSBuiltinType() ? RHSType : LHSType;
    QualType Vector = LHSType->isSveVLSBuiltinType() ? LHSType : RHSType;
    bool ScalarOrVector =
        LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType();

    Diag(Loc, diag::err_typecheck_vector_not_convertable_implict_truncation)
        << ScalarOrVector << Scalar << Vector;

    return QualType();
  }

  Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                    << RHS.get()->getSourceRange();
  return QualType();
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseDivisionSizeofPointerOrArray(Sema &S, Expr *LHS, Expr *RHS,
                                          SourceLocation Loc) {
  const auto *LUE = dyn_cast<UnaryExprOrTypeTraitExpr>(LHS);
  const auto *RUE = dyn_cast<UnaryExprOrTypeTraitExpr>(RHS);
  if (!LUE || !RUE)
    return;
  if (LUE->getKind() != UETT_SizeOf || LUE->isArgumentType() ||
      RUE->getKind() != UETT_SizeOf)
    return;

  const Expr *LHSArg = LUE->getArgumentExpr()->IgnoreParens();
  QualType LHSTy = LHSArg->getType();
  QualType RHSTy;

  if (RUE->isArgumentType())
    RHSTy = RUE->getArgumentType();
  else
    RHSTy = RUE->getArgumentExpr()->IgnoreParens()->getType();

  if (LHSTy->isPointerType() && !RHSTy->isPointerType()) {
    if (!S.Context.hasSameUnqualifiedType(LHSTy->getPointeeType(), RHSTy))
      return;

    S.Diag(Loc, diag::warn_division_sizeof_ptr) << LHS << LHS->getSourceRange();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSArg)) {
      if (const ValueDecl *LHSArgDecl = DRE->getDecl())
        S.Diag(LHSArgDecl->getLocation(), diag::note_pointer_declared_here)
            << LHSArgDecl;
    }
  } else if (const auto *ArrayTy = S.Context.getAsArrayType(LHSTy)) {
    QualType ArrayElemTy = ArrayTy->getElementType();
    if (ArrayElemTy != S.Context.getBaseElementType(ArrayTy) ||
        ArrayElemTy->isCharType() ||
        S.Context.getTypeSize(ArrayElemTy) == S.Context.getTypeSize(RHSTy))
      return;
    S.Diag(Loc, diag::warn_division_sizeof_array)
        << LHSArg->getSourceRange() << ArrayElemTy << RHSTy;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSArg)) {
      if (const ValueDecl *LHSArgDecl = DRE->getDecl())
        S.Diag(LHSArgDecl->getLocation(), diag::note_array_declared_here)
            << LHSArgDecl;
    }

    S.Diag(Loc, diag::note_precedence_silence) << RHS;
  }
}
} // namespace

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool cannotBeIntegerConstant(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const ValueDecl *D = DRE->getDecl();
    if (isa<ParmVarDecl>(D))
      return true;
    if (const auto *VD = dyn_cast<VarDecl>(D))
      return !VD->getType().isConstQualified() && !isa<EnumConstantDecl>(D);
  }
  return false;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBadDivideOrRemainderValues(Sema &S, ExprResult &LHS,
                                        ExprResult &RHS, SourceLocation Loc,
                                        bool IsDiv) {
  if (cannotBeIntegerConstant(RHS.get()))
    return;
  Expr::EvalResult RHSValue;
  if (RHS.get()->EvaluateAsInt(RHSValue, S.Context) &&
      RHSValue.Val.getInt() == 0)
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_remainder_division_by_zero)
                              << IsDiv << RHS.get()->getSourceRange());
}
} // namespace

// ===----------------------------------------------------------------------===
// Arithmetic & pointer operators
// ===----------------------------------------------------------------------===

QualType Sema::CheckMultiplyDivideOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign, bool IsDiv) {
  QualType LHSTy = LHS.get()->getType();
  QualType RHSTy = RHS.get()->getType();
  if (LLVM_UNLIKELY(LHSTy->isVectorType() || RHSTy->isVectorType()))
    return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                               /*AllowBothBool*/ false,
                               /*AllowBoolConversions*/ false,
                               /*AllowBooleanOperation*/ false,
                               /*ReportInvalid*/ true);
  if (LLVM_UNLIKELY(LHSTy->isSveVLSBuiltinType() ||
                    RHSTy->isSveVLSBuiltinType()))
    return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                       ACK_Arithmetic);
  if (LLVM_UNLIKELY(!IsDiv && (LHSTy->isConstantMatrixType() ||
                               RHSTy->isConstantMatrixType())))
    return CheckMatrixMultiplyOperands(LHS, RHS, Loc, IsCompAssign);
  if (LLVM_UNLIKELY(IsDiv && LHSTy->isConstantMatrixType() &&
                    RHSTy->isArithmeticType()))
    return CheckMatrixElementwiseOperands(LHS, RHS, Loc, IsCompAssign);

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, IsCompAssign ? ACK_CompAssign : ACK_Arithmetic);
  if (LLVM_UNLIKELY(LHS.isInvalid() || RHS.isInvalid()))
    return QualType();

  if (compType.isNull() || !compType->isArithmeticType())
    return InvalidOperands(Loc, LHS, RHS);
  if (IsDiv) {
    diagnoseBadDivideOrRemainderValues(*this, LHS, RHS, Loc, IsDiv);
    diagnoseDivisionSizeofPointerOrArray(*this, LHS.get(), RHS.get(), Loc);
  }
  return compType;
}

QualType Sema::CheckRemainderOperands(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc, bool IsCompAssign) {
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                 /*AllowBothBool*/ false,
                                 /*AllowBoolConversions*/ false,
                                 /*AllowBooleanOperation*/ false,
                                 /*ReportInvalid*/ true);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_Arithmetic);

    return InvalidOperands(Loc, LHS, RHS);
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, IsCompAssign ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  if (compType.isNull() || !compType->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);
  diagnoseBadDivideOrRemainderValues(*this, LHS, RHS, Loc, false /* IsDiv */);
  return compType;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnTwoVoidPtrs(Sema &S, SourceLocation Loc, Expr *LHSExpr,
                                 Expr *RHSExpr) {
  S.Diag(Loc, diag::ext_gnu_void_ptr)
      << 1 /* two pointers */ << LHSExpr->getSourceRange()
      << RHSExpr->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnVoidPtr(Sema &S, SourceLocation Loc, Expr *Pointer) {
  S.Diag(Loc, diag::ext_gnu_void_ptr)
      << 0 /* one pointer */ << Pointer->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnNullPtr(Sema &S, SourceLocation Loc, Expr *Pointer,
                             bool IsGNUIdiom) {
  if (IsGNUIdiom)
    S.Diag(Loc, diag::warn_gnu_null_ptr_arith) << Pointer->getSourceRange();
  else
    S.Diag(Loc, diag::warn_pointer_arith_null_ptr)
        << false << Pointer->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnSubtractionOnNullPtr(Sema &S, SourceLocation Loc, Expr *Pointer,
                              bool BothNull) {
  // Is this a macro from a system header?
  if (S.Diags.getSuppressSystemWarnings() && S.SourceMgr.isInSystemMacro(Loc))
    return;

  S.DiagRuntimeBehavior(Loc, Pointer,
                        S.PDiag(diag::warn_pointer_sub_null_ptr)
                            << false << Pointer->getSourceRange());
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnTwoFnPtrs(Sema &S, SourceLocation Loc, Expr *LHS,
                               Expr *RHS) {
  assert(LHS->getType()->isAnyPointerType());
  assert(RHS->getType()->isAnyPointerType());
  S.Diag(Loc, diag::ext_gnu_ptr_func_arith)
      << 1 /* two pointers */
      << LHS->getType()->getPointeeType()
      // We only show the second type if it differs from the first.
      << (unsigned)!S.Context.hasSameUnqualifiedType(LHS->getType(),
                                                     RHS->getType())
      << RHS->getType()->getPointeeType() << LHS->getSourceRange()
      << RHS->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnArithmeticOnFnPtr(Sema &S, SourceLocation Loc, Expr *Pointer) {
  assert(Pointer->getType()->isAnyPointerType());
  S.Diag(Loc, diag::ext_gnu_ptr_func_arith)
      << 0 /* one pointer */ << Pointer->getType()->getPointeeType()
      << 0 /* one pointer, so only one type */
      << Pointer->getSourceRange();
}
} // namespace

namespace {
bool verifyArithmeticPointerComplete(Sema &S, SourceLocation Loc,
                                     Expr *Operand) {
  QualType ResType = Operand->getType();
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  assert(ResType->isAnyPointerType());
  QualType PointeeTy = ResType->getPointeeType();
  return S.RequireCompleteSizedType(
      Loc, PointeeTy,
      diag::err_typecheck_arithmetic_incomplete_or_sizeless_type,
      Operand->getSourceRange());
}
} // namespace

namespace neverc {
bool verifyArithmeticPointerOp(Sema &S, SourceLocation Loc, Expr *Operand) {
  QualType ResType = Operand->getType();
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  if (!ResType->isAnyPointerType())
    return true;

  QualType PointeeTy = ResType->getPointeeType();
  if (PointeeTy->isVoidType()) {
    warnArithmeticOnVoidPtr(S, Loc, Operand);
    return true;
  }
  if (PointeeTy->isFunctionType()) {
    warnArithmeticOnFnPtr(S, Loc, Operand);
    return true;
  }

  if (verifyArithmeticPointerComplete(S, Loc, Operand))
    return false;

  return true;
}
} // namespace

namespace {
bool verifyArithmeticBinPtrOps(Sema &S, SourceLocation Loc, Expr *LHSExpr,
                               Expr *RHSExpr) {
  bool isLHSPointer = LHSExpr->getType()->isAnyPointerType();
  bool isRHSPointer = RHSExpr->getType()->isAnyPointerType();
  if (!isLHSPointer && !isRHSPointer)
    return true;

  QualType LHSPointeeTy, RHSPointeeTy;
  if (isLHSPointer)
    LHSPointeeTy = LHSExpr->getType()->getPointeeType();
  if (isRHSPointer)
    RHSPointeeTy = RHSExpr->getType()->getPointeeType();

  // if both are pointers check if operation is valid wrt address spaces
  if (isLHSPointer && isRHSPointer) {
    if (!LHSPointeeTy.isAddressSpaceOverlapping(RHSPointeeTy)) {
      S.Diag(Loc,
             diag::err_typecheck_op_on_nonoverlapping_address_space_pointers)
          << LHSExpr->getType() << RHSExpr->getType() << 1 /*arithmetic op*/
          << LHSExpr->getSourceRange() << RHSExpr->getSourceRange();
      return false;
    }
  }

  // Check for arithmetic on pointers to incomplete types.
  bool isLHSVoidPtr = isLHSPointer && LHSPointeeTy->isVoidType();
  bool isRHSVoidPtr = isRHSPointer && RHSPointeeTy->isVoidType();
  if (isLHSVoidPtr || isRHSVoidPtr) {
    if (!isRHSVoidPtr)
      warnArithmeticOnVoidPtr(S, Loc, LHSExpr);
    else if (!isLHSVoidPtr)
      warnArithmeticOnVoidPtr(S, Loc, RHSExpr);
    else
      warnArithmeticOnTwoVoidPtrs(S, Loc, LHSExpr, RHSExpr);

    return true;
  }

  bool isLHSFuncPtr = isLHSPointer && LHSPointeeTy->isFunctionType();
  bool isRHSFuncPtr = isRHSPointer && RHSPointeeTy->isFunctionType();
  if (isLHSFuncPtr || isRHSFuncPtr) {
    if (!isRHSFuncPtr)
      warnArithmeticOnFnPtr(S, Loc, LHSExpr);
    else if (!isLHSFuncPtr)
      warnArithmeticOnFnPtr(S, Loc, RHSExpr);
    else
      warnArithmeticOnTwoFnPtrs(S, Loc, LHSExpr, RHSExpr);

    return true;
  }

  if (isLHSPointer && verifyArithmeticPointerComplete(S, Loc, LHSExpr))
    return false;
  if (isRHSPointer && verifyArithmeticPointerComplete(S, Loc, RHSExpr))
    return false;

  return true;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnStringPlusInt(Sema &Self, SourceLocation OpLoc, Expr *LHSExpr,
                       Expr *RHSExpr) {
  StringLiteral *StrExpr = dyn_cast<StringLiteral>(LHSExpr->IgnoreImpCasts());
  Expr *IndexExpr = RHSExpr;
  if (!StrExpr) {
    StrExpr = dyn_cast<StringLiteral>(RHSExpr->IgnoreImpCasts());
    IndexExpr = LHSExpr;
  }

  bool IsStringPlusInt =
      StrExpr && IndexExpr->getType()->isIntegralOrUnscopedEnumerationType();
  if (!IsStringPlusInt)
    return;

  SourceRange DiagRange(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());
  Self.Diag(OpLoc, diag::warn_string_plus_int)
      << DiagRange << IndexExpr->IgnoreImpCasts()->getType();

  // Only print a fixit for "str" + int, not for int + "str".
  if (IndexExpr == RHSExpr) {
    SourceLocation EndLoc = Self.getLocForEndOfToken(RHSExpr->getEndLoc());
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence)
        << FixItHint::CreateInsertion(LHSExpr->getBeginLoc(), "&")
        << FixItHint::CreateReplacement(SourceRange(OpLoc), "[")
        << FixItHint::CreateInsertion(EndLoc, "]");
  } else
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence);
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnStringPlusChar(Sema &Self, SourceLocation OpLoc, Expr *LHSExpr,
                        Expr *RHSExpr) {
  const Expr *StringRefExpr = LHSExpr;
  const CharacterLiteral *CharExpr =
      dyn_cast<CharacterLiteral>(RHSExpr->IgnoreImpCasts());

  if (!CharExpr) {
    CharExpr = dyn_cast<CharacterLiteral>(LHSExpr->IgnoreImpCasts());
    StringRefExpr = RHSExpr;
  }

  if (!CharExpr || !StringRefExpr)
    return;

  const QualType StringType = StringRefExpr->getType();
  if (!StringType->isAnyPointerType())
    return;

  if (!StringType->getPointeeType()->isAnyCharacterType())
    return;

  TreeContext &Ctx = Self.getTreeContext();
  SourceRange DiagRange(LHSExpr->getBeginLoc(), RHSExpr->getEndLoc());

  const QualType CharType = CharExpr->getType();
  if (!CharType->isAnyCharacterType() && CharType->isIntegerType() &&
      llvm::isUIntN(Ctx.getCharWidth(), CharExpr->getValue())) {
    Self.Diag(OpLoc, diag::warn_string_plus_char) << DiagRange << Ctx.CharTy;
  } else {
    Self.Diag(OpLoc, diag::warn_string_plus_char)
        << DiagRange << CharExpr->getType();
  }

  // Only print a fixit for str + char, not for char + str.
  if (isa<CharacterLiteral>(RHSExpr->IgnoreImpCasts())) {
    SourceLocation EndLoc = Self.getLocForEndOfToken(RHSExpr->getEndLoc());
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence)
        << FixItHint::CreateInsertion(LHSExpr->getBeginLoc(), "&")
        << FixItHint::CreateReplacement(SourceRange(OpLoc), "[")
        << FixItHint::CreateInsertion(EndLoc, "]");
  } else {
    Self.Diag(OpLoc, diag::note_string_plus_scalar_silence);
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnPointerIncompatibility(Sema &S, SourceLocation Loc, Expr *LHSExpr,
                                Expr *RHSExpr) {
  assert(LHSExpr->getType()->isAnyPointerType());
  assert(RHSExpr->getType()->isAnyPointerType());
  S.Diag(Loc, diag::err_typecheck_sub_ptr_compatible)
      << LHSExpr->getType() << RHSExpr->getType() << LHSExpr->getSourceRange()
      << RHSExpr->getSourceRange();
}
} // namespace

// C99 6.5.6
QualType Sema::CheckAdditionOperands(ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc, BinaryOperatorKind Opc,
                                     QualType *CompLHSTy) {
  QualType LTy = LHS.get()->getType(), RTy = RHS.get()->getType();

  if (LLVM_UNLIKELY(LTy->isVectorType() || RTy->isVectorType())) {
    QualType compType = CheckVectorOperands(LHS, RHS, Loc, CompLHSTy,
                                            /*AllowBothBool*/ false,
                                            /*AllowBoolConversions*/ false,
                                            /*AllowBooleanOperation*/ false,
                                            /*ReportInvalid*/ true);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isSveVLSBuiltinType() || RTy->isSveVLSBuiltinType())) {
    QualType compType =
        CheckSizelessVectorOperands(LHS, RHS, Loc, CompLHSTy, ACK_Arithmetic);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isConstantMatrixType() ||
                    RTy->isConstantMatrixType())) {
    QualType compType =
        CheckMatrixElementwiseOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, CompLHSTy ? ACK_CompAssign : ACK_Arithmetic);
  if (LLVM_UNLIKELY(LHS.isInvalid() || RHS.isInvalid()))
    return QualType();

  if (LLVM_LIKELY(!compType.isNull() && compType->isArithmeticType())) {
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (Opc == BO_Add) {
    warnStringPlusInt(*this, Loc, LHS.get(), RHS.get());
    warnStringPlusChar(*this, Loc, LHS.get(), RHS.get());
  }

  // Type-checking.  Ultimately the pointer's going to be in PExp;
  // note that we bias towards the LHS being the pointer.
  Expr *PExp = LHS.get(), *IExp = RHS.get();
  if (PExp->getType()->isPointerType()) {
  } else {
    std::swap(PExp, IExp);
    if (!PExp->getType()->isPointerType()) {
      return InvalidOperands(Loc, LHS, RHS);
    }
  }
  assert(PExp->getType()->isPointerType());

  if (!IExp->getType()->isIntegerType())
    return InvalidOperands(Loc, LHS, RHS);

  // Adding to a null pointer results in undefined behavior.
  if (PExp->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull)) {
    bool IsGNUIdiom = BinaryOperator::isNullPointerArithmeticExtension(
        Context, BO_Add, PExp, IExp);
    warnArithmeticOnNullPtr(*this, Loc, PExp, IsGNUIdiom);
  }

  if (!verifyArithmeticPointerOp(*this, Loc, PExp))
    return QualType();
  CheckArrayAccess(PExp, IExp);

  if (CompLHSTy) {
    QualType LHSTy = Context.isPromotableBitField(LHS.get());
    if (LHSTy.isNull()) {
      LHSTy = LHS.get()->getType();
      if (Context.isPromotableIntegerType(LHSTy))
        LHSTy = Context.getPromotedIntegerType(LHSTy);
    }
    *CompLHSTy = LHSTy;
  }

  return PExp->getType();
}

// C99 6.5.6
QualType Sema::CheckSubtractionOperands(ExprResult &LHS, ExprResult &RHS,
                                        SourceLocation Loc,
                                        QualType *CompLHSTy) {
  QualType LTy = LHS.get()->getType(), RTy = RHS.get()->getType();

  if (LLVM_UNLIKELY(LTy->isVectorType() || RTy->isVectorType())) {
    QualType compType = CheckVectorOperands(LHS, RHS, Loc, CompLHSTy,
                                            /*AllowBothBool*/ false,
                                            /*AllowBoolConversions*/ false,
                                            /*AllowBooleanOperation*/ false,
                                            /*ReportInvalid*/ true);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isSveVLSBuiltinType() || RTy->isSveVLSBuiltinType())) {
    QualType compType =
        CheckSizelessVectorOperands(LHS, RHS, Loc, CompLHSTy, ACK_Arithmetic);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  if (LLVM_UNLIKELY(LTy->isConstantMatrixType() ||
                    RTy->isConstantMatrixType())) {
    QualType compType =
        CheckMatrixElementwiseOperands(LHS, RHS, Loc, CompLHSTy);
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  QualType compType = UsualArithmeticConversions(
      LHS, RHS, Loc, CompLHSTy ? ACK_CompAssign : ACK_Arithmetic);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Enforce type constraints: C99 6.5.6p3.

  // Handle the common case first (both operands are arithmetic).
  if (!compType.isNull() && compType->isArithmeticType()) {
    if (CompLHSTy)
      *CompLHSTy = compType;
    return compType;
  }

  // Either ptr - int   or   ptr - ptr.
  if (LHS.get()->getType()->isAnyPointerType()) {
    QualType lpointee = LHS.get()->getType()->getPointeeType();

    // The result type of a pointer-int computation is the pointer type.
    if (RHS.get()->getType()->isIntegerType()) {
      // Subtracting from a null pointer should produce a warning.
      // The last argument to the diagnose call says this doesn't match the
      // GNU int-to-pointer idiom.
      if (LHS.get()->IgnoreParenCasts()->isNullPointerConstant(
              Context, Expr::NPC_ValueDependentIsNotNull)) {
        warnArithmeticOnNullPtr(*this, Loc, LHS.get(), false);
      }

      if (!verifyArithmeticPointerOp(*this, Loc, LHS.get()))
        return QualType();
      CheckArrayAccess(LHS.get(), RHS.get(), /*ArraySubscriptExpr*/ nullptr,
                       /*AllowOnePastEnd*/ true, /*IndexNegated*/ true);

      if (CompLHSTy)
        *CompLHSTy = LHS.get()->getType();
      return LHS.get()->getType();
    }

    // Handle pointer-pointer subtractions.
    if (const PointerType *RHSPTy =
            RHS.get()->getType()->getAs<PointerType>()) {
      QualType rpointee = RHSPTy->getPointeeType();

      if (!Context.typesAreCompatible(
              Context.getCanonicalType(lpointee).getUnqualifiedType(),
              Context.getCanonicalType(rpointee).getUnqualifiedType())) {
        warnPointerIncompatibility(*this, Loc, LHS.get(), RHS.get());
        return QualType();
      }

      if (!verifyArithmeticBinPtrOps(*this, Loc, LHS.get(), RHS.get()))
        return QualType();

      bool LHSIsNullPtr = LHS.get()->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull);
      bool RHSIsNullPtr = RHS.get()->IgnoreParenCasts()->isNullPointerConstant(
          Context, Expr::NPC_ValueDependentIsNotNull);

      // Subtracting nullptr or from nullptr is suspect
      if (LHSIsNullPtr)
        warnSubtractionOnNullPtr(*this, Loc, LHS.get(), RHSIsNullPtr);
      if (RHSIsNullPtr)
        warnSubtractionOnNullPtr(*this, Loc, RHS.get(), LHSIsNullPtr);

      // The pointee type may have zero size.  As an extension, a structure or
      // union may have zero size or an array may have zero length.  In this
      // case subtraction does not make sense.
      if (!rpointee->isVoidType() && !rpointee->isFunctionType()) {
        CharUnits ElementSize = Context.getTypeSizeInChars(rpointee);
        if (ElementSize.isZero()) {
          Diag(Loc, diag::warn_sub_ptr_zero_size_types)
              << rpointee.getUnqualifiedType() << LHS.get()->getSourceRange()
              << RHS.get()->getSourceRange();
        }
      }

      if (CompLHSTy)
        *CompLHSTy = LHS.get()->getType();
      return Context.getPointerDiffType();
    }
  }

  return InvalidOperands(Loc, LHS, RHS);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBadShiftValues(Sema &S, ExprResult &LHS, ExprResult &RHS,
                            SourceLocation Loc, BinaryOperatorKind Opc,
                            QualType LHSType) {
  if (cannotBeIntegerConstant(RHS.get()))
    return;
  Expr::EvalResult RHSResult;
  if (!RHS.get()->EvaluateAsInt(RHSResult, S.Context))
    return;
  llvm::APSInt Right = RHSResult.Val.getInt();

  if (Right.isNegative()) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_negative)
                              << RHS.get()->getSourceRange());
    return;
  }

  QualType LHSExprType = LHS.get()->getType();
  uint64_t LeftSize = S.Context.getTypeSize(LHSExprType);
  if (LHSExprType->isBitIntType())
    LeftSize = S.Context.getIntWidth(LHSExprType);
  else if (LHSExprType->isFixedPointType()) {
    auto FXSema = S.Context.getFixedPointSemantics(LHSExprType);
    LeftSize = FXSema.getWidth() - (unsigned)FXSema.hasUnsignedPadding();
  }
  if (Right.uge(LeftSize)) {
    S.DiagRuntimeBehavior(Loc, RHS.get(),
                          S.PDiag(diag::warn_shift_gt_typewidth)
                              << RHS.get()->getSourceRange());
    return;
  }

  if (Opc != BO_Shl || LHSExprType->isFixedPointType())
    return;

  // Signed left-shift overflow: undefined in older C; unsigned shifts wrap.
  Expr::EvalResult LHSResult;
  if (LHSType->hasUnsignedIntegerRepresentation() ||
      !LHS.get()->EvaluateAsInt(LHSResult, S.Context))
    return;
  llvm::APSInt Left = LHSResult.Val.getInt();

  // Skip when signed overflow is defined (GCC/NeverC) or when signed integers
  // use two's-complement (C23).
  if (S.getLangOpts().isSignedOverflowDefined())
    return;

  // Negative LHS is undefined for signed left shift in C.
  if (Left.isNegative()) {
    S.DiagRuntimeBehavior(Loc, LHS.get(),
                          S.PDiag(diag::warn_shift_lhs_negative)
                              << LHS.get()->getSourceRange());
    return;
  }

  llvm::APInt ResultBits =
      static_cast<llvm::APInt &>(Right) + Left.getSignificantBits();
  if (ResultBits.ule(LeftSize))
    return;
  llvm::APSInt Result = Left.extend(ResultBits.getLimitedValue());
  Result = Result.shl(Right);

  // Print the bit representation of the signed integer as an unsigned
  // hexadecimal number.
  llvm::SmallString<40> HexResult;
  Result.toString(HexResult, 16, /*Signed =*/false, /*Literal =*/true);

  // If we are only missing a sign bit, this is less likely to result in actual
  // bugs -- if the result is cast back to an unsigned type, it will have the
  // expected value. Thus we place this behind a different warning that can be
  // turned off separately if needed.
  if (ResultBits - 1 == LeftSize) {
    S.Diag(Loc, diag::warn_shift_result_sets_sign_bit)
        << HexResult << LHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return;
  }

  S.Diag(Loc, diag::warn_shift_result_gt_typewidth)
      << HexResult.str() << Result.getSignificantBits() << LHSType
      << Left.getBitWidth() << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();
}
} // namespace

namespace {
QualType validateVectorShift(Sema &S, ExprResult &LHS, ExprResult &RHS,
                             SourceLocation Loc, bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = S.UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = S.UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType();
  // Note that LHS might be a scalar because the routine calls not only in
  const VectorType *LHSVecTy = LHSType->getAs<VectorType>();
  QualType LHSEleType = LHSVecTy ? LHSVecTy->getElementType() : LHSType;

  // Note that RHS might not be a vector.
  QualType RHSType = RHS.get()->getType();
  const VectorType *RHSVecTy = RHSType->getAs<VectorType>();
  QualType RHSEleType = RHSVecTy ? RHSVecTy->getElementType() : RHSType;

  // Do not allow shifts for boolean vectors.
  if ((LHSVecTy && LHSVecTy->isExtVectorBoolType()) ||
      (RHSVecTy && RHSVecTy->isExtVectorBoolType())) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHS.get()->getType() << RHS.get()->getType()
        << LHS.get()->getSourceRange();
    return QualType();
  }

  // The operands need to be integers.
  if (!LHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << LHS.get()->getType() << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << RHS.get()->getType() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSVecTy) {
    assert(RHSVecTy);
    if (IsCompAssign)
      return RHSType;
    if (LHSEleType != RHSEleType) {
      LHS = S.ImpCastExprToType(LHS.get(), RHSEleType, CK_IntegralCast);
      LHSEleType = RHSEleType;
    }
    QualType VecTy =
        S.Context.getExtVectorType(LHSEleType, RHSVecTy->getNumElements());
    LHS = S.ImpCastExprToType(LHS.get(), VecTy, CK_VectorSplat);
    LHSType = VecTy;
  } else if (RHSVecTy) {
    // For vector types, operators are applied component-wise. Ensure
    // the number of elements is the same as LHS.
    if (RHSVecTy->getNumElements() != LHSVecTy->getNumElements()) {
      S.Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
          << LHS.get()->getType() << RHS.get()->getType()
          << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      return QualType();
    }
    const BuiltinType *LHSBT = LHSEleType->getAs<neverc::BuiltinType>();
    const BuiltinType *RHSBT = RHSEleType->getAs<neverc::BuiltinType>();
    if (LHSBT != RHSBT &&
        S.Context.getTypeSize(LHSBT) != S.Context.getTypeSize(RHSBT)) {
      S.Diag(Loc, diag::warn_typecheck_vector_element_sizes_not_equal)
          << LHS.get()->getType() << RHS.get()->getType()
          << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    }
  } else {
    // ...else expand RHS to match the number of elements in LHS.
    QualType VecTy =
        S.Context.getExtVectorType(RHSEleType, LHSVecTy->getNumElements());
    RHS = S.ImpCastExprToType(RHS.get(), VecTy, CK_VectorSplat);
  }

  return LHSType;
}
} // namespace

namespace {
QualType validateSizelessVectorShift(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc, bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = S.UsualUnaryConversions(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }

  RHS = S.UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  QualType LHSType = LHS.get()->getType();
  const BuiltinType *LHSBuiltinTy = LHSType->castAs<BuiltinType>();
  QualType LHSEleType = LHSType->isSveVLSBuiltinType()
                            ? LHSBuiltinTy->getSveEltType(S.getTreeContext())
                            : LHSType;

  // Note that RHS might not be a vector
  QualType RHSType = RHS.get()->getType();
  const BuiltinType *RHSBuiltinTy = RHSType->castAs<BuiltinType>();
  QualType RHSEleType = RHSType->isSveVLSBuiltinType()
                            ? RHSBuiltinTy->getSveEltType(S.getTreeContext())
                            : RHSType;

  if ((LHSBuiltinTy && LHSBuiltinTy->isSVEBool()) ||
      (RHSBuiltinTy && RHSBuiltinTy->isSVEBool())) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHSType << RHSType << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << LHS.get()->getType() << LHS.get()->getSourceRange();
    return QualType();
  }

  if (!RHSEleType->isIntegerType()) {
    S.Diag(Loc, diag::err_typecheck_expect_int)
        << RHS.get()->getType() << RHS.get()->getSourceRange();
    return QualType();
  }

  if (LHSType->isSveVLSBuiltinType() && RHSType->isSveVLSBuiltinType() &&
      (S.Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC !=
       S.Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC)) {
    S.Diag(Loc, diag::err_typecheck_invalid_operands)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
    return QualType();
  }

  if (!LHSType->isSveVLSBuiltinType()) {
    assert(RHSType->isSveVLSBuiltinType());
    if (IsCompAssign)
      return RHSType;
    if (LHSEleType != RHSEleType) {
      LHS = S.ImpCastExprToType(LHS.get(), RHSEleType, neverc::CK_IntegralCast);
      LHSEleType = RHSEleType;
    }
    const llvm::ElementCount VecSize =
        S.Context.getBuiltinVectorTypeInfo(RHSBuiltinTy).EC;
    QualType VecTy =
        S.Context.getScalableVectorType(LHSEleType, VecSize.getKnownMinValue());
    LHS = S.ImpCastExprToType(LHS.get(), VecTy, neverc::CK_VectorSplat);
    LHSType = VecTy;
  } else if (RHSBuiltinTy && RHSBuiltinTy->isSveVLSBuiltinType()) {
    if (S.Context.getTypeSize(RHSBuiltinTy) !=
        S.Context.getTypeSize(LHSBuiltinTy)) {
      S.Diag(Loc, diag::err_typecheck_vector_lengths_not_equal)
          << LHSType << RHSType << LHS.get()->getSourceRange()
          << RHS.get()->getSourceRange();
      return QualType();
    }
  } else {
    const llvm::ElementCount VecSize =
        S.Context.getBuiltinVectorTypeInfo(LHSBuiltinTy).EC;
    if (LHSEleType != RHSEleType) {
      RHS = S.ImpCastExprToType(RHS.get(), LHSEleType, neverc::CK_IntegralCast);
      RHSEleType = LHSEleType;
    }
    QualType VecTy =
        S.Context.getScalableVectorType(RHSEleType, VecSize.getKnownMinValue());
    RHS = S.ImpCastExprToType(RHS.get(), VecTy, CK_VectorSplat);
  }

  return LHSType;
}
} // namespace

// C99 6.5.7
QualType Sema::CheckShiftOperands(ExprResult &LHS, ExprResult &RHS,
                                  SourceLocation Loc, BinaryOperatorKind Opc,
                                  bool IsCompAssign) {
  // Vector shifts promote their scalar inputs to vector type.
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    return validateVectorShift(*this, LHS, RHS, Loc, IsCompAssign);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType())
    return validateSizelessVectorShift(*this, LHS, RHS, Loc, IsCompAssign);

  // Shifts don't perform usual arithmetic conversions, they just do integer
  // promotions on each operand. C99 6.5.7p3

  // For the LHS, do usual unary conversions, but then reset them away
  // if this is a compound assignment.
  ExprResult OldLHS = LHS;
  LHS = UsualUnaryConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();
  QualType LHSType = LHS.get()->getType();
  if (IsCompAssign)
    LHS = OldLHS;

  // The RHS is simpler.
  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();
  QualType RHSType = RHS.get()->getType();

  // C99 6.5.7p2: Each of the operands shall have integer type.
  // Embedded-C 4.1.6.2.2: The LHS may also be fixed-point.
  if ((!LHSType->isFixedPointOrIntegerType() &&
       !LHSType->hasIntegerRepresentation()) ||
      !RHSType->hasIntegerRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  diagnoseBadShiftValues(*this, LHS, RHS, Loc, Opc, LHSType);

  // "The type of the result is that of the promoted left operand."
  return LHSType;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnDistinctPointerCmp(Sema &S, SourceLocation Loc, ExprResult &LHS,
                            ExprResult &RHS, bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_distinct_pointers
                      : diag::ext_typecheck_comparison_of_distinct_pointers)
      << LHS.get()->getType() << RHS.get()->getType()
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnFnPtrToVoidCmp(Sema &S, SourceLocation Loc, ExprResult &LHS,
                        ExprResult &RHS, bool IsError) {
  S.Diag(Loc, IsError ? diag::err_typecheck_comparison_of_fptr_to_void
                      : diag::ext_typecheck_comparison_of_fptr_to_void)
      << LHS.get()->getType() << RHS.get()->getType()
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnLogicalNotOnLHS(Sema &S, ExprResult &LHS, ExprResult &RHS,
                         SourceLocation Loc, BinaryOperatorKind Opc) {
  UnaryOperator *UO = dyn_cast<UnaryOperator>(LHS.get()->IgnoreImpCasts());
  if (!UO || UO->getOpcode() != UO_LNot)
    return;

  // Only check if the right hand side is non-bool arithmetic type.
  if (RHS.get()->isKnownToHaveBooleanValue())
    return;

  // Make sure that the something in !something is not bool.
  Expr *SubExpr = UO->getSubExpr()->IgnoreImpCasts();
  if (SubExpr->isKnownToHaveBooleanValue())
    return;
  bool IsBitwiseOp = Opc == BO_And || Opc == BO_Or || Opc == BO_Xor;
  S.Diag(UO->getOperatorLoc(), diag::warn_logical_not_on_lhs_of_check)
      << Loc << IsBitwiseOp;

  // First note suggest !(x < y)
  SourceLocation FirstOpen = SubExpr->getBeginLoc();
  SourceLocation FirstClose = RHS.get()->getEndLoc();
  FirstClose = S.getLocForEndOfToken(FirstClose);
  if (FirstClose.isInvalid())
    FirstOpen = SourceLocation();
  S.Diag(UO->getOperatorLoc(), diag::note_logical_not_fix)
      << IsBitwiseOp << FixItHint::CreateInsertion(FirstOpen, "(")
      << FixItHint::CreateInsertion(FirstClose, ")");

  // Second note suggests (!x) < y
  SourceLocation SecondOpen = LHS.get()->getBeginLoc();
  SourceLocation SecondClose = LHS.get()->getEndLoc();
  SecondClose = S.getLocForEndOfToken(SecondClose);
  if (SecondClose.isInvalid())
    SecondOpen = SourceLocation();
  S.Diag(UO->getOperatorLoc(), diag::note_logical_not_silence_with_parens)
      << FixItHint::CreateInsertion(SecondOpen, "(")
      << FixItHint::CreateInsertion(SecondClose, ")");
}
} // namespace

// Returns true if E refers to a non-weak array.
namespace {
bool checkForArray(const Expr *E) {
  const ValueDecl *D = nullptr;
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E)) {
    D = DR->getDecl();
  }
  if (!D)
    return false;
  return D->getType()->isArrayType() && !D->isWeak();
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnTautologicalCmp(Sema &S, SourceLocation Loc, Expr *LHS, Expr *RHS,
                         BinaryOperatorKind Opc) {
  Expr *LHSStripped = LHS->IgnoreParenImpCasts();
  Expr *RHSStripped = RHS->IgnoreParenImpCasts();

  QualType LHSType = LHS->getType();
  if (LHSType->hasFloatingRepresentation())
    return;

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  //
  // NOTE: Don't warn about comparison expressions resulting from macro
  // expansion. The idea is to warn when the comparison operator will always
  // evaluate to the same result.

  // Used for indexing into %select in warn_comparison_always
  enum {
    AlwaysConstant,
    AlwaysTrue,
    AlwaysFalse,
  };

  if (!LHS->getBeginLoc().isMacroID() && !RHS->getBeginLoc().isMacroID()) {
    if (Expr::isSameComparisonOperand(LHS, RHS)) {
      unsigned Result;
      switch (Opc) {
      case BO_EQ:
      case BO_LE:
      case BO_GE:
        Result = AlwaysTrue;
        break;
      case BO_NE:
      case BO_LT:
      case BO_GT:
        Result = AlwaysFalse;
        break;
      default:
        Result = AlwaysConstant;
        break;
      }
      S.DiagRuntimeBehavior(Loc, nullptr,
                            S.PDiag(diag::warn_comparison_always)
                                << 0 /*self-comparison*/
                                << Result);
    } else if (checkForArray(LHSStripped) && checkForArray(RHSStripped)) {
      // What is it always going to evaluate to?
      unsigned Result;
      switch (Opc) {
      case BO_EQ: // e.g. array1 == array2
        Result = AlwaysFalse;
        break;
      case BO_NE: // e.g. array1 != array2
        Result = AlwaysTrue;
        break;
      default: // e.g. array1 <= array2
        // The best we can say is 'a constant'
        Result = AlwaysConstant;
        break;
      }
      S.DiagRuntimeBehavior(Loc, nullptr,
                            S.PDiag(diag::warn_comparison_always)
                                << 1 /*array comparison*/
                                << Result);
    }
  }

  if (isa<CastExpr>(LHSStripped))
    LHSStripped = LHSStripped->IgnoreParenCasts();
  if (isa<CastExpr>(RHSStripped))
    RHSStripped = RHSStripped->IgnoreParenCasts();

  // Warn about comparisons against a string constant (unless the other
  // operand is null); the user probably wants string comparison function.
  Expr *LiteralString = nullptr;
  if (isa<StringLiteral>(LHSStripped) &&
      !RHSStripped->isNullPointerConstant(S.Context,
                                          Expr::NPC_ValueDependentIsNull)) {
    LiteralString = LHS;
  } else if (isa<StringLiteral>(RHSStripped) &&
             !LHSStripped->isNullPointerConstant(
                 S.Context, Expr::NPC_ValueDependentIsNull)) {
    LiteralString = RHS;
  }

  if (LiteralString) {
    S.DiagRuntimeBehavior(Loc, nullptr,
                          S.PDiag(diag::warn_stringcompare)
                              << LiteralString->getSourceRange());
  }
}
} // namespace

namespace {
QualType validateArithmeticOrEnumCmp(Sema &S, ExprResult &LHS, ExprResult &RHS,
                                     SourceLocation Loc,
                                     BinaryOperatorKind Opc) {
  // C99 6.5.8p3 / C99 6.5.9p4
  QualType Type =
      S.UsualArithmeticConversions(LHS, RHS, Loc, Sema::ACK_Comparison);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (Type.isNull())
    return S.InvalidOperands(Loc, LHS, RHS);
  assert(Type->isArithmeticType() || Type->isEnumeralType());

  if (Type->isAnyComplexType() && BinaryOperator::isRelationalOp(Opc))
    return S.InvalidOperands(Loc, LHS, RHS);
  if (Type->hasFloatingRepresentation())
    S.CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);

  // Result type is the language's "logical" type (typically `int` in C).
  return S.Context.getLogicalOperationType();
}
} // namespace

void Sema::CheckPtrComparisonWithNullChar(ExprResult &E, ExprResult &NullE) {
  if (!NullE.get()->getType()->isAnyPointerType())
    return;
  int NullValue = PP.isMacroDefined("NULL") ? 0 : 1;
  if (!E.get()->getType()->isAnyPointerType() &&
      E.get()->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull) ==
          Expr::NPCK_ZeroExpression) {
    if (const auto *CL = dyn_cast<CharacterLiteral>(E.get())) {
      if (CL->getValue() == 0)
        Diag(E.get()->getExprLoc(), diag::warn_pointer_compare)
            << NullValue
            << FixItHint::CreateReplacement(E.get()->getExprLoc(),
                                            NullValue ? "NULL" : "(void *)0");
    } else if (const auto *CE = dyn_cast<CStyleCastExpr>(E.get())) {
      TypeSourceInfo *TI = CE->getTypeInfoAsWritten();
      QualType T = Context.getCanonicalType(TI->getType()).getUnqualifiedType();
      if (T == Context.CharTy)
        Diag(E.get()->getExprLoc(), diag::warn_pointer_compare)
            << NullValue
            << FixItHint::CreateReplacement(E.get()->getExprLoc(),
                                            NullValue ? "NULL" : "(void *)0");
    }
  }
}

// Relational / equality operands (C99 6.5.8, 6.5.9).
// ===----------------------------------------------------------------------===
// Comparison operators
// ===----------------------------------------------------------------------===

QualType Sema::CheckCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                    SourceLocation Loc,
                                    BinaryOperatorKind Opc) {
  bool IsRelational = BinaryOperator::isRelationalOp(Opc);
  bool IsOrdered = IsRelational;

  LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
  if (LLVM_UNLIKELY(LHS.isInvalid()))
    return QualType();
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (LLVM_UNLIKELY(RHS.isInvalid()))
    return QualType();

  QualType LHSType = LHS.get()->getType();
  QualType RHSType = RHS.get()->getType();

  if (LLVM_LIKELY((LHSType->isArithmeticType() || LHSType->isEnumeralType()) &&
                  (RHSType->isArithmeticType() || RHSType->isEnumeralType()))) {
    warnLogicalNotOnLHS(*this, LHS, RHS, Loc, Opc);
    warnTautologicalCmp(*this, Loc, LHS.get(), RHS.get(), Opc);
    return validateArithmeticOrEnumCmp(*this, LHS, RHS, Loc, Opc);
  }

  if (LLVM_UNLIKELY(BinaryOperator::isEqualityOp(Opc))) {
    CheckPtrComparisonWithNullChar(LHS, RHS);
    CheckPtrComparisonWithNullChar(RHS, LHS);
  }

  if (LLVM_UNLIKELY(LHSType->isVectorType() || RHSType->isVectorType()))
    return CheckVectorCompareOperands(LHS, RHS, Loc, Opc);

  if (LLVM_UNLIKELY(LHSType->isSveVLSBuiltinType() ||
                    RHSType->isSveVLSBuiltinType()))
    return CheckSizelessVectorCompareOperands(LHS, RHS, Loc, Opc);

  warnLogicalNotOnLHS(*this, LHS, RHS, Loc, Opc);
  warnTautologicalCmp(*this, Loc, LHS.get(), RHS.get(), Opc);

  const Expr::NullPointerConstantKind LHSNullKind =
      LHS.get()->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
  const Expr::NullPointerConstantKind RHSNullKind =
      RHS.get()->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull);
  bool LHSIsNull = LHSNullKind != Expr::NPCK_NotNull;
  bool RHSIsNull = RHSNullKind != Expr::NPCK_NotNull;

  auto computeResultTy = [&]() { return Context.getLogicalOperationType(); };

  if (!IsOrdered && LHSIsNull != RHSIsNull) {
    bool IsEquality = Opc == BO_EQ;
    if (RHSIsNull)
      DiagnoseAlwaysNonNullPointer(LHS.get(), RHSNullKind, IsEquality,
                                   RHS.get()->getSourceRange());
    else
      DiagnoseAlwaysNonNullPointer(RHS.get(), LHSNullKind, IsEquality,
                                   LHS.get()->getSourceRange());
  }

  if (IsOrdered && LHSType->isFunctionPointerType() &&
      RHSType->isFunctionPointerType()) {
    Diag(Loc, diag::ext_typecheck_ordered_comparison_of_function_pointers)
        << LHSType << RHSType << LHS.get()->getSourceRange()
        << RHS.get()->getSourceRange();
  }

  if ((LHSType->isIntegerType() && !LHSIsNull) ||
      (RHSType->isIntegerType() && !RHSIsNull)) {
    // Skip normal pointer conversion checks in this case; we have better
    // diagnostics for this below.
  } else if (LHSType->isPointerType() &&
             RHSType->isPointerType()) { // C99 6.5.8p2
    // All of the following pointer-related warnings are GCC extensions, except
    // when handling null pointer constants.
    QualType LCanPointeeTy =
        LHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();
    QualType RCanPointeeTy =
        RHSType->castAs<PointerType>()->getPointeeType().getCanonicalType();

    // C99 6.5.9p2 and C99 6.5.8p2
    if (Context.typesAreCompatible(LCanPointeeTy.getUnqualifiedType(),
                                   RCanPointeeTy.getUnqualifiedType())) {
      if (IsRelational) {
        // Pointers both need to point to complete or incomplete types
        if ((LCanPointeeTy->isIncompleteType() !=
             RCanPointeeTy->isIncompleteType()) &&
            !getLangOpts().C11) {
          Diag(Loc, diag::ext_typecheck_compare_complete_incomplete_pointers)
              << LHS.get()->getSourceRange() << RHS.get()->getSourceRange()
              << LHSType << RHSType << LCanPointeeTy->isIncompleteType()
              << RCanPointeeTy->isIncompleteType();
        }
      }
    } else if (!IsRelational &&
               (LCanPointeeTy->isVoidType() || RCanPointeeTy->isVoidType())) {
      // Valid unless comparison between non-null pointer and function pointer
      if ((LCanPointeeTy->isFunctionType() ||
           RCanPointeeTy->isFunctionType()) &&
          !LHSIsNull && !RHSIsNull)
        warnFnPtrToVoidCmp(*this, Loc, LHS, RHS,
                           /*isError*/ false);
    } else {
      // Invalid
      warnDistinctPointerCmp(*this, Loc, LHS, RHS,
                             /*isError*/ false);
    }
    if (LCanPointeeTy != RCanPointeeTy) {
      LangAS AddrSpaceL = LCanPointeeTy.getAddressSpace();
      LangAS AddrSpaceR = RCanPointeeTy.getAddressSpace();
      CastKind Kind =
          AddrSpaceL != AddrSpaceR ? CK_AddressSpaceConversion : CK_BitCast;
      if (LHSIsNull && !RHSIsNull)
        LHS = ImpCastExprToType(LHS.get(), RHSType, Kind);
      else
        RHS = ImpCastExprToType(RHS.get(), LHSType, Kind);
    }
    return computeResultTy();
  }

  // C23 6.5.9p5: nullptr_t vs null pointer constant compares equal when
  // allowed.
  if (!IsOrdered && LHSIsNull && RHSIsNull) {
    if (LHSType->isNullPtrType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
    if (RHSType->isNullPtrType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  if (!IsOrdered && (LHSIsNull || RHSIsNull)) {
    // C23 6.5.9p6:
    //   Otherwise, at least one operand is a pointer. If one is a pointer and
    //   the other is a null pointer constant or has type nullptr_t, they
    //   compare equal
    if (LHSIsNull && RHSType->isPointerType()) {
      LHS = ImpCastExprToType(LHS.get(), RHSType, CK_NullToPointer);
      return computeResultTy();
    }
    if (RHSIsNull && LHSType->isPointerType()) {
      RHS = ImpCastExprToType(RHS.get(), LHSType, CK_NullToPointer);
      return computeResultTy();
    }
  }

  if ((LHSType->isAnyPointerType() && RHSType->isIntegerType()) ||
      (LHSType->isIntegerType() && RHSType->isAnyPointerType())) {
    unsigned DiagID = 0;
    if ((LHSIsNull && LHSType->isIntegerType()) ||
        (RHSIsNull && RHSType->isIntegerType())) {
    } else if (IsOrdered)
      DiagID = diag::ext_typecheck_ordered_comparison_of_pointer_integer;
    else
      DiagID = diag::ext_typecheck_comparison_of_pointer_integer;

    if (DiagID) {
      Diag(Loc, DiagID) << LHSType << RHSType << LHS.get()->getSourceRange()
                        << RHS.get()->getSourceRange();
    }

    if (LHSType->isIntegerType())
      LHS = ImpCastExprToType(LHS.get(), RHSType,
                              LHSIsNull ? CK_NullToPointer
                                        : CK_IntegralToPointer);
    else
      RHS = ImpCastExprToType(RHS.get(), LHSType,
                              RHSIsNull ? CK_NullToPointer
                                        : CK_IntegralToPointer);
    return computeResultTy();
  }

  return InvalidOperands(Loc, LHS, RHS);
}

// Return a signed ext_vector_type that is of identical size and number of
// elements. For floating point vectors, return an integer type of identical
// size and number of elements. In the non ext_vector_type case, search from
// the largest type to the smallest type to avoid cases where long long == long,
// where long gets picked over long long.
QualType Sema::GetSignedVectorType(QualType V) {
  const VectorType *VTy = V->castAs<VectorType>();
  unsigned TypeSize = Context.getTypeSize(VTy->getElementType());

  if (isa<ExtVectorType>(VTy)) {
    if (VTy->isExtVectorBoolType())
      return Context.getExtVectorType(Context.BoolTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.CharTy))
      return Context.getExtVectorType(Context.CharTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.ShortTy))
      return Context.getExtVectorType(Context.ShortTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.IntTy))
      return Context.getExtVectorType(Context.IntTy, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.Int128Ty))
      return Context.getExtVectorType(Context.Int128Ty, VTy->getNumElements());
    if (TypeSize == Context.getTypeSize(Context.LongTy))
      return Context.getExtVectorType(Context.LongTy, VTy->getNumElements());
    assert(TypeSize == Context.getTypeSize(Context.LongLongTy) &&
           "Unhandled vector element size in vector compare");
    return Context.getExtVectorType(Context.LongLongTy, VTy->getNumElements());
  }

  if (TypeSize == Context.getTypeSize(Context.Int128Ty))
    return Context.getVectorType(Context.Int128Ty, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.LongLongTy))
    return Context.getVectorType(Context.LongLongTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.LongTy))
    return Context.getVectorType(Context.LongTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.IntTy))
    return Context.getVectorType(Context.IntTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  if (TypeSize == Context.getTypeSize(Context.ShortTy))
    return Context.getVectorType(Context.ShortTy, VTy->getNumElements(),
                                 VectorKind::Generic);
  assert(TypeSize == Context.getTypeSize(Context.CharTy) &&
         "Unhandled vector element size in vector compare");
  return Context.getVectorType(Context.CharTy, VTy->getNumElements(),
                               VectorKind::Generic);
}

QualType Sema::GetSignedSizelessVectorType(QualType V) {
  const BuiltinType *VTy = V->castAs<BuiltinType>();
  assert(VTy->isSizelessBuiltinType() && "expected sizeless type");

  const QualType ETy = V->getSveEltType(Context);
  const auto TypeSize = Context.getTypeSize(ETy);

  const QualType IntTy = Context.getIntTypeForBitwidth(TypeSize, true);
  const llvm::ElementCount VecSize = Context.getBuiltinVectorTypeInfo(VTy).EC;
  return Context.getScalableVectorType(IntTy, VecSize.getKnownMinValue());
}

QualType Sema::CheckVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          BinaryOperatorKind Opc) {
  // Check to make sure we're operating on vectors of the same type and width,
  // Allowing one side to be a scalar of element type.
  QualType vType = CheckVectorOperands(LHS, RHS, Loc, /*isCompAssign*/ false,
                                       /*AllowBothBool*/ true,
                                       /*AllowBoolConversions*/ false,
                                       /*AllowBooleanOperation*/ true,
                                       /*ReportInvalid*/ true);
  if (vType.isNull())
    return vType;

  QualType LHSType = LHS.get()->getType();

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  warnTautologicalCmp(*this, Loc, LHS.get(), RHS.get(), Opc);

  if (LHSType->hasFloatingRepresentation()) {
    assert(RHS.get()->getType()->hasFloatingRepresentation());
    CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);
  }

  return GetSignedVectorType(vType);
}

QualType Sema::CheckSizelessVectorCompareOperands(ExprResult &LHS,
                                                  ExprResult &RHS,
                                                  SourceLocation Loc,
                                                  BinaryOperatorKind Opc) {
  // Check to make sure we're operating on vectors of the same type and width,
  // Allowing one side to be a scalar of element type.
  QualType vType = CheckSizelessVectorOperands(
      LHS, RHS, Loc, /*isCompAssign*/ false, ACK_Comparison);

  if (vType.isNull())
    return vType;

  QualType LHSType = LHS.get()->getType();

  // For non-floating point types, check for self-comparisons of the form
  // x == x, x != x, x < x, etc.  These always evaluate to a constant, and
  // often indicate logic errors in the program.
  warnTautologicalCmp(*this, Loc, LHS.get(), RHS.get(), Opc);

  if (LHSType->hasFloatingRepresentation()) {
    assert(RHS.get()->getType()->hasFloatingRepresentation());
    CheckFloatComparison(Loc, LHS.get(), RHS.get(), Opc);
  }

  const BuiltinType *LHSBuiltinTy = LHSType->getAs<BuiltinType>();
  const BuiltinType *RHSBuiltinTy = RHS.get()->getType()->getAs<BuiltinType>();

  if (LHSBuiltinTy && RHSBuiltinTy && LHSBuiltinTy->isSVEBool() &&
      RHSBuiltinTy->isSVEBool())
    return LHSType;

  return GetSignedSizelessVectorType(vType);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnXorMisusedAsPow(Sema &S, const ExprResult &XorLHS,
                         const ExprResult &XorRHS, const SourceLocation Loc) {
  // Do not diagnose macros.
  if (Loc.isMacroID())
    return;

  // Do not diagnose if both LHS and RHS are macros.
  if (XorLHS.get()->getExprLoc().isMacroID() &&
      XorRHS.get()->getExprLoc().isMacroID())
    return;

  bool Negative = false;
  bool ExplicitPlus = false;
  const auto *LHSInt = dyn_cast<IntegerLiteral>(XorLHS.get());
  const auto *RHSInt = dyn_cast<IntegerLiteral>(XorRHS.get());

  if (!LHSInt)
    return;
  if (!RHSInt) {
    if (const auto *UO = dyn_cast<UnaryOperator>(XorRHS.get())) {
      UnaryOperatorKind Opc = UO->getOpcode();
      if (Opc != UO_Minus && Opc != UO_Plus)
        return;
      RHSInt = dyn_cast<IntegerLiteral>(UO->getSubExpr());
      if (!RHSInt)
        return;
      Negative = (Opc == UO_Minus);
      ExplicitPlus = !Negative;
    } else {
      return;
    }
  }

  const llvm::APInt &LeftSideValue = LHSInt->getValue();
  llvm::APInt RightSideValue = RHSInt->getValue();
  if (LeftSideValue != 2 && LeftSideValue != 10)
    return;

  if (LeftSideValue.getBitWidth() != RightSideValue.getBitWidth())
    return;

  CharSourceRange ExprRange = CharSourceRange::getCharRange(
      LHSInt->getBeginLoc(), S.getLocForEndOfToken(RHSInt->getLocation()));
  llvm::StringRef ExprStr = SourceScanner::getSourceText(
      ExprRange, S.getSourceManager(), S.getLangOpts());

  CharSourceRange XorRange =
      CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));
  llvm::StringRef XorStr = SourceScanner::getSourceText(
      XorRange, S.getSourceManager(), S.getLangOpts());
  // Do not diagnose if xor keyword/macro is used.
  if (XorStr == "xor")
    return;

  std::string LHSStr = std::string(SourceScanner::getSourceText(
      CharSourceRange::getTokenRange(LHSInt->getSourceRange()),
      S.getSourceManager(), S.getLangOpts()));
  std::string RHSStr = std::string(SourceScanner::getSourceText(
      CharSourceRange::getTokenRange(RHSInt->getSourceRange()),
      S.getSourceManager(), S.getLangOpts()));

  if (Negative) {
    RightSideValue = -RightSideValue;
    RHSStr = "-" + RHSStr;
  } else if (ExplicitPlus) {
    RHSStr = "+" + RHSStr;
  }

  llvm::StringRef LHSStrRef = LHSStr;
  llvm::StringRef RHSStrRef = RHSStr;
  // Do not diagnose literals with digit separators, binary, hexadecimal, octal
  // literals.
  if (LHSStrRef.starts_with("0b") || LHSStrRef.starts_with("0B") ||
      RHSStrRef.starts_with("0b") || RHSStrRef.starts_with("0B") ||
      LHSStrRef.starts_with("0x") || LHSStrRef.starts_with("0X") ||
      RHSStrRef.starts_with("0x") || RHSStrRef.starts_with("0X") ||
      (LHSStrRef.size() > 1 && LHSStrRef.starts_with("0")) ||
      (RHSStrRef.size() > 1 && RHSStrRef.starts_with("0")) ||
      LHSStrRef.contains('\'') || RHSStrRef.contains('\''))
    return;

  bool SuggestXor = S.getPrepEngine().isMacroDefined("xor");
  const llvm::APInt XorValue = LeftSideValue ^ RightSideValue;
  int64_t RightSideIntValue = RightSideValue.getSExtValue();
  if (LeftSideValue == 2 && RightSideIntValue >= 0) {
    std::string SuggestedExpr = "1 << " + RHSStr;
    bool Overflow = false;
    llvm::APInt One = (LeftSideValue - 1);
    llvm::APInt PowValue = One.sshl_ov(RightSideValue, Overflow);
    if (Overflow) {
      if (RightSideIntValue < 64)
        S.Diag(Loc, diag::warn_xor_used_as_pow_base)
            << ExprStr << toString(XorValue, 10, true) << ("1LL << " + RHSStr)
            << FixItHint::CreateReplacement(ExprRange, "1LL << " + RHSStr);
      else if (RightSideIntValue == 64)
        S.Diag(Loc, diag::warn_xor_used_as_pow)
            << ExprStr << toString(XorValue, 10, true);
      else
        return;
    } else {
      S.Diag(Loc, diag::warn_xor_used_as_pow_base_extra)
          << ExprStr << toString(XorValue, 10, true) << SuggestedExpr
          << toString(PowValue, 10, true)
          << FixItHint::CreateReplacement(
                 ExprRange, (RightSideIntValue == 0) ? "1" : SuggestedExpr);
    }

    S.Diag(Loc, diag::note_xor_used_as_pow_silence)
        << ("0x2 ^ " + RHSStr) << SuggestXor;
  } else if (LeftSideValue == 10) {
    std::string SuggestedValue = "1e" + std::to_string(RightSideIntValue);
    S.Diag(Loc, diag::warn_xor_used_as_pow_base)
        << ExprStr << toString(XorValue, 10, true) << SuggestedValue
        << FixItHint::CreateReplacement(ExprRange, SuggestedValue);
    S.Diag(Loc, diag::note_xor_used_as_pow_silence)
        << ("0xA ^ " + RHSStr) << SuggestXor;
  }
}
} // namespace

QualType Sema::CheckVectorLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc) {
  // Ensure that either both operands are of the same vector type, or
  // one operand is of a vector type and the other is of its element type.
  QualType vType = CheckVectorOperands(LHS, RHS, Loc, false,
                                       /*AllowBothBool*/ true,
                                       /*AllowBoolConversions*/ false,
                                       /*AllowBooleanOperation*/ false,
                                       /*ReportInvalid*/ false);
  if (vType.isNull())
    return InvalidOperands(Loc, LHS, RHS);
  // Logical &&/|| on vectors: only extended (GNU) vector types are
  // accepted here; plain C vectors use InvalidLogicalVectorOperands.
  if (!(isa<ExtVectorType>(vType->getAs<VectorType>())))
    return InvalidLogicalVectorOperands(Loc, LHS, RHS);

  return GetSignedVectorType(LHS.get()->getType());
}

QualType Sema::CheckMatrixElementwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                              SourceLocation Loc,
                                              bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  // For conversion purposes, we ignore any qualifiers.
  // For example, "const float" and "float" are equivalent.
  QualType LHSType = LHS.get()->getType().getUnqualifiedType();
  QualType RHSType = RHS.get()->getType().getUnqualifiedType();

  const MatrixType *LHSMatType = LHSType->getAs<MatrixType>();
  const MatrixType *RHSMatType = RHSType->getAs<MatrixType>();
  assert((LHSMatType || RHSMatType) && "At least one operand must be a matrix");

  if (Context.hasSameType(LHSType, RHSType))
    return Context.getCommonSugaredType(LHSType, RHSType);

  // Type conversion may change LHS/RHS. Keep copies to the original results, in
  // case we have to return InvalidOperands.
  ExprResult OriginalLHS = LHS;
  ExprResult OriginalRHS = RHS;
  if (LHSMatType && !RHSMatType) {
    RHS = tryConvertExprToType(RHS.get(), LHSMatType->getElementType());
    if (!RHS.isInvalid())
      return LHSType;

    return InvalidOperands(Loc, OriginalLHS, OriginalRHS);
  }

  if (!LHSMatType && RHSMatType) {
    LHS = tryConvertExprToType(LHS.get(), RHSMatType->getElementType());
    if (!LHS.isInvalid())
      return RHSType;
    return InvalidOperands(Loc, OriginalLHS, OriginalRHS);
  }

  return InvalidOperands(Loc, LHS, RHS);
}

QualType Sema::CheckMatrixMultiplyOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           bool IsCompAssign) {
  if (!IsCompAssign) {
    LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
    if (LHS.isInvalid())
      return QualType();
  }
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  auto *LHSMatType = LHS.get()->getType()->getAs<ConstantMatrixType>();
  auto *RHSMatType = RHS.get()->getType()->getAs<ConstantMatrixType>();
  assert((LHSMatType || RHSMatType) && "At least one operand must be a matrix");

  if (LHSMatType && RHSMatType) {
    if (LHSMatType->getNumColumns() != RHSMatType->getNumRows())
      return InvalidOperands(Loc, LHS, RHS);

    if (Context.hasSameType(LHSMatType, RHSMatType))
      return Context.getCommonSugaredType(
          LHS.get()->getType().getUnqualifiedType(),
          RHS.get()->getType().getUnqualifiedType());

    QualType LHSELTy = LHSMatType->getElementType(),
             RHSELTy = RHSMatType->getElementType();
    if (!Context.hasSameType(LHSELTy, RHSELTy))
      return InvalidOperands(Loc, LHS, RHS);

    return Context.getConstantMatrixType(
        Context.getCommonSugaredType(LHSELTy, RHSELTy),
        LHSMatType->getNumRows(), RHSMatType->getNumColumns());
  }
  return CheckMatrixElementwiseOperands(LHS, RHS, Loc, IsCompAssign);
}

namespace {
bool isValidBoolVectorBinOp(BinaryOperatorKind Opc) {
  switch (Opc) {
  default:
    return false;
  case BO_And:
  case BO_AndAssign:
  case BO_Or:
  case BO_OrAssign:
  case BO_Xor:
  case BO_XorAssign:
    return true;
  }
}
} // namespace

inline QualType Sema::CheckBitwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  bool IsCompAssign =
      Opc == BO_AndAssign || Opc == BO_OrAssign || Opc == BO_XorAssign;

  bool LegalBoolVecOperator = isValidBoolVectorBinOp(Opc);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                 /*AllowBothBool*/ true,
                                 /*AllowBoolConversions*/ false,
                                 /*AllowBooleanOperation*/ LegalBoolVecOperator,
                                 /*ReportInvalid*/ true);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_BitwiseOp);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_BitwiseOp);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (Opc == BO_And)
    warnLogicalNotOnLHS(*this, LHS, RHS, Loc, Opc);

  if (LHS.get()->getType()->hasFloatingRepresentation() ||
      RHS.get()->getType()->hasFloatingRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  ExprResult LHSResult = LHS, RHSResult = RHS;
  QualType compType = UsualArithmeticConversions(
      LHSResult, RHSResult, Loc, IsCompAssign ? ACK_CompAssign : ACK_BitwiseOp);
  if (LHSResult.isInvalid() || RHSResult.isInvalid())
    return QualType();
  LHS = LHSResult.get();
  RHS = RHSResult.get();

  if (Opc == BO_Xor)
    warnXorMisusedAsPow(*this, LHS, RHS, Loc);

  if (!compType.isNull() && compType->isIntegralOrUnscopedEnumerationType())
    return compType;
  return InvalidOperands(Loc, LHS, RHS);
}

// C99 6.5.[13,14]
inline QualType Sema::CheckLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorLogicalOperands(LHS, RHS, Loc);

  bool EnumConstantInBoolContext = false;
  for (const ExprResult &HS : {LHS, RHS}) {
    if (const auto *DREHS = dyn_cast<DeclRefExpr>(HS.get())) {
      const auto *ECDHS = dyn_cast<EnumConstantDecl>(DREHS->getDecl());
      if (ECDHS && ECDHS->getInitVal() != 0 && ECDHS->getInitVal() != 1)
        EnumConstantInBoolContext = true;
    }
  }

  if (EnumConstantInBoolContext)
    Diag(Loc, diag::warn_enum_constant_in_bool_context);

  // Diagnose cases where the user write a logical and/or but probably meant a
  // bitwise one.  We do this when the LHS is a non-bool integer and the RHS
  // is a constant.
  if (!EnumConstantInBoolContext && LHS.get()->getType()->isIntegerType() &&
      !LHS.get()->getType()->isBooleanType() &&
      RHS.get()->getType()->isIntegerType() &&
      // Don't warn in macros.
      !Loc.isMacroID()) {
    // If the RHS can be constant folded, and if it constant folds to something
    // that isn't 0 or 1 (which indicate a potential logical operation that
    // happened to fold to true/false) then warn.
    // Parens on the RHS are ignored.
    Expr::EvalResult EVResult;
    if (RHS.get()->EvaluateAsInt(EVResult, Context)) {
      llvm::APSInt Result = EVResult.Val.getInt();
      if ((getLangOpts().Bool && !RHS.get()->getType()->isBooleanType() &&
           !RHS.get()->getExprLoc().isMacroID()) ||
          (Result != 0 && Result != 1)) {
        Diag(Loc, diag::warn_logical_instead_of_bitwise)
            << RHS.get()->getSourceRange() << (Opc == BO_LAnd ? "&&" : "||");
        // Suggest replacing the logical operator with the bitwise version
        Diag(Loc, diag::note_logical_instead_of_bitwise_change_operator)
            << (Opc == BO_LAnd ? "&" : "|")
            << FixItHint::CreateReplacement(
                   SourceRange(Loc, getLocForEndOfToken(Loc)),
                   Opc == BO_LAnd ? "&" : "|");
        if (Opc == BO_LAnd)
          // Suggest replacing "Foo() && kNonZero" with "Foo()"
          Diag(Loc, diag::note_logical_instead_of_bitwise_remove_constant)
              << FixItHint::CreateRemoval(
                     SourceRange(getLocForEndOfToken(LHS.get()->getEndLoc()),
                                 RHS.get()->getEndLoc()));
      }
    }
  }

  LHS = UsualUnaryConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();

  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  if (!LHS.get()->getType()->isScalarType() ||
      !RHS.get()->getType()->isScalarType())
    return InvalidOperands(Loc, LHS, RHS);

  return Context.IntTy;
}

namespace {
bool isTypeWritable(QualType Ty, bool IsDereference) {
  if (IsDereference && Ty->isPointerType())
    Ty = Ty->getPointeeType();
  return !Ty.isConstQualified();
}
} // namespace

// Update err_typecheck_assign_const and note_typecheck_assign_const
// when this enum is changed.
enum {
  ConstFunction,
  ConstVariable,
  ConstMember,
  NestedConstMember,
  ConstUnknown, // Keep as last element
};

namespace {
LLVM_ATTRIBUTE_NOINLINE
void reportConstAssignment(Sema &S, const Expr *E, SourceLocation Loc) {
  SourceRange ExprRange = E->getSourceRange();

  // Only emit one error on the first const found.  All other consts will emit
  // a note to the error.
  bool DiagnosticEmitted = false;

  // Track if the current expression is the result of a dereference, and if the
  // next checked expression is the result of a dereference.
  bool IsDereference = false;
  bool NextIsDereference = false;
  while (true) {
    IsDereference = NextIsDereference;

    E = E->IgnoreImplicit()->IgnoreParenImpCasts();
    if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      NextIsDereference = ME->isArrow();
      const ValueDecl *VD = ME->getMemberDecl();
      if (const FieldDecl *Field = dyn_cast<FieldDecl>(VD)) {
        if (!isTypeWritable(Field->getType(), IsDereference)) {
          if (!DiagnosticEmitted) {
            S.Diag(Loc, diag::err_typecheck_assign_const)
                << ExprRange << ConstMember << Field << Field->getType();
            DiagnosticEmitted = true;
          }
          S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
              << ConstMember << Field << Field->getType()
              << Field->getSourceRange();
        }
        break;
      }
      break; // End MemberExpr
    } else if (const ArraySubscriptExpr *ASE =
                   dyn_cast<ArraySubscriptExpr>(E)) {
      E = ASE->getBase()->IgnoreParenImpCasts();
      continue;
    } else if (const ExtVectorElementExpr *EVE =
                   dyn_cast<ExtVectorElementExpr>(E)) {
      E = EVE->getBase()->IgnoreParenImpCasts();
      continue;
    }
    break;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    // Function calls
    const FunctionDecl *FD = CE->getDirectCallee();
    if (FD && !isTypeWritable(FD->getReturnType(), IsDereference)) {
      if (!DiagnosticEmitted) {
        S.Diag(Loc, diag::err_typecheck_assign_const)
            << ExprRange << ConstFunction << FD;
        DiagnosticEmitted = true;
      }
      S.Diag(FD->getReturnTypeSourceRange().getBegin(),
             diag::note_typecheck_assign_const)
          << ConstFunction << FD << FD->getReturnType()
          << FD->getReturnTypeSourceRange();
    }
  } else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    // Point to variable declaration.
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (!isTypeWritable(VD->getType(), IsDereference)) {
        if (!DiagnosticEmitted) {
          S.Diag(Loc, diag::err_typecheck_assign_const)
              << ExprRange << ConstVariable << VD << VD->getType();
          DiagnosticEmitted = true;
        }
        S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
            << ConstVariable << VD << VD->getType() << VD->getSourceRange();
      }
    }
  }

  if (DiagnosticEmitted)
    return;

  // Can't determine a more specific message, so display the generic error.
  S.Diag(Loc, diag::err_typecheck_assign_const) << ExprRange << ConstUnknown;
}
} // namespace

enum OriginalExprKind { OEK_Variable, OEK_Member, OEK_LValue };

namespace {
LLVM_ATTRIBUTE_NOINLINE
void reportRecursiveConstFields(Sema &S, const ValueDecl *VD,
                                const RecordType *Ty, SourceLocation Loc,
                                SourceRange Range, OriginalExprKind OEK,
                                bool &DiagnosticEmitted) {
  std::vector<const RecordType *> RecordTypeList;
  RecordTypeList.push_back(Ty);
  unsigned NextToCheckIndex = 0;
  // We walk the record hierarchy breadth-first to ensure that we print
  // diagnostics in field nesting order.
  while (RecordTypeList.size() > NextToCheckIndex) {
    bool IsNested = NextToCheckIndex > 0;
    for (const FieldDecl *Field :
         RecordTypeList[NextToCheckIndex]->getDecl()->fields()) {
      // First, check every field for constness.
      QualType FieldTy = Field->getType();
      if (FieldTy.isConstQualified()) {
        if (!DiagnosticEmitted) {
          S.Diag(Loc, diag::err_typecheck_assign_const)
              << Range << NestedConstMember << OEK << VD << IsNested << Field;
          DiagnosticEmitted = true;
        }
        S.Diag(Field->getLocation(), diag::note_typecheck_assign_const)
            << NestedConstMember << IsNested << Field << FieldTy
            << Field->getSourceRange();
      }

      // Then we append it to the list to check next in order.
      FieldTy = FieldTy.getCanonicalType();
      if (const auto *FieldRecTy = FieldTy->getAs<RecordType>()) {
        if (!llvm::is_contained(RecordTypeList, FieldRecTy))
          RecordTypeList.push_back(FieldRecTy);
      }
    }
    ++NextToCheckIndex;
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void reportRecursiveConstFields(Sema &S, const Expr *E, SourceLocation Loc) {
  QualType Ty = E->getType();
  assert(Ty->isRecordType() && "lvalue was not record?");
  SourceRange Range = E->getSourceRange();
  const RecordType *RTy = Ty.getCanonicalType()->getAs<RecordType>();
  bool DiagEmitted = false;

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E))
    reportRecursiveConstFields(S, ME->getMemberDecl(), RTy, Loc, Range,
                               OEK_Member, DiagEmitted);
  else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    reportRecursiveConstFields(S, DRE->getDecl(), RTy, Loc, Range, OEK_Variable,
                               DiagEmitted);
  else
    reportRecursiveConstFields(S, nullptr, RTy, Loc, Range, OEK_LValue,
                               DiagEmitted);
  if (!DiagEmitted)
    reportConstAssignment(S, E, Loc);
}
} // namespace

namespace neverc {
bool checkForModifiableLvalue(Expr *E, SourceLocation Loc, Sema &S) {
  assert(!E->hasPlaceholderType(BuiltinType::PseudoObject));

  SourceLocation OrigLoc = Loc;
  Expr::isModifiableLvalueResult IsLV = E->isModifiableLvalue(S.Context, &Loc);
  if (IsLV == Expr::MLV_Valid)
    return false;

  unsigned DiagID = 0;
  bool NeedType = false;
  switch (IsLV) { // C99 6.5.16p2
  case Expr::MLV_ConstQualified:
    reportConstAssignment(S, E, Loc);
    return true;
  case Expr::MLV_ConstQualifiedField:
    reportRecursiveConstFields(S, E, Loc);
    return true;
  case Expr::MLV_ArrayType:
  case Expr::MLV_ArrayTemporary:
    DiagID = diag::err_typecheck_array_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_NotObjectType:
    DiagID = diag::err_typecheck_non_object_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_LValueCast:
    DiagID = diag::err_typecheck_lvalue_casts_not_supported;
    break;
  case Expr::MLV_Valid:
    llvm_unreachable("did not take early return for MLV_Valid");
  case Expr::MLV_InvalidExpression:
  case Expr::MLV_RecordTemporary:
    DiagID = diag::err_typecheck_expression_not_modifiable_lvalue;
    break;
  case Expr::MLV_IncompleteType:
  case Expr::MLV_IncompleteVoidType:
    return S.RequireCompleteType(
        Loc, E->getType(),
        diag::err_typecheck_incomplete_type_not_modifiable_lvalue, E);
  case Expr::MLV_DuplicateVectorComponents:
    DiagID = diag::err_typecheck_duplicate_vector_components_not_mlvalue;
    break;
  }

  SourceRange Assign;
  if (Loc != OrigLoc)
    Assign = SourceRange(OrigLoc, OrigLoc);
  if (NeedType)
    S.Diag(Loc, DiagID) << E->getType() << E->getSourceRange() << Assign;
  else
    S.Diag(Loc, DiagID) << E->getSourceRange() << Assign;
  return true;
}
} // namespace

namespace {
void checkIdentityFieldAssignment(Expr *LHSExpr, Expr *RHSExpr,
                                  SourceLocation Loc, Sema &Sema) {
  if (Sema.isUnevaluatedContext())
    return;
  if (Loc.isInvalid() || Loc.isMacroID())
    return;
  if (LHSExpr->getExprLoc().isMacroID() || RHSExpr->getExprLoc().isMacroID())
    return;

  // Member-to-member assignment
  MemberExpr *ML = dyn_cast<MemberExpr>(LHSExpr);
  MemberExpr *MR = dyn_cast<MemberExpr>(RHSExpr);
  if (ML && MR) {
    const ValueDecl *LHSDecl =
        cast<ValueDecl>(ML->getMemberDecl()->getCanonicalDecl());
    const ValueDecl *RHSDecl =
        cast<ValueDecl>(MR->getMemberDecl()->getCanonicalDecl());
    if (LHSDecl != RHSDecl)
      return;
    if (LHSDecl->getType().isVolatileQualified())
      return;

    Sema.Diag(Loc, diag::warn_identity_field_assign);
  }
}
} // namespace

// C99 6.5.16.1
// ===----------------------------------------------------------------------===
// Assignment & comma operators
// ===----------------------------------------------------------------------===

QualType Sema::CheckAssignmentOperands(Expr *LHSExpr, ExprResult &RHS,
                                       SourceLocation Loc,
                                       QualType CompoundType,
                                       BinaryOperatorKind Opc) {
  assert(!LHSExpr->hasPlaceholderType(BuiltinType::PseudoObject));

  // Verify that LHS is a modifiable lvalue, and emit error if not.
  if (checkForModifiableLvalue(LHSExpr, Loc, *this))
    return QualType();

  QualType LHSType = LHSExpr->getType();

  if (LLVM_LIKELY(CompoundType.isNull())) {
    QualType RHSType = RHS.get()->getType();
    if (LLVM_LIKELY(LHSType == RHSType)) {
      if (const auto *BT = LHSType->getAs<BuiltinType>()) {
        unsigned K = BT->getKind();
        if (LLVM_LIKELY(K == BuiltinType::Int || K == BuiltinType::UInt ||
                        K == BuiltinType::Long || K == BuiltinType::ULong ||
                        K == BuiltinType::Short || K == BuiltinType::UShort ||
                        K == BuiltinType::Char_U || K == BuiltinType::Char_S ||
                        K == BuiltinType::SChar || K == BuiltinType::UChar ||
                        K == BuiltinType::LongLong ||
                        K == BuiltinType::ULongLong)) {
          RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
          if (RHS.isInvalid())
            return QualType();
          return LHSType.getAtomicUnqualifiedType();
        }
      }
    }

    Expr *RHSCheck = RHS.get();

    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings()))
      checkIdentityFieldAssignment(LHSExpr, RHSCheck, Loc, *this);

    QualType LHSTy(LHSType);
    AssignConvertType ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return QualType();
    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings())) {
      if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(RHSCheck))
        RHSCheck = ICE->getSubExpr();
      if (UnaryOperator *UO = dyn_cast<UnaryOperator>(RHSCheck)) {
        if ((UO->getOpcode() == UO_Plus || UO->getOpcode() == UO_Minus) &&
            Loc.isFileID() && UO->getOperatorLoc().isFileID() &&
            Loc.getLocWithOffset(1) == UO->getOperatorLoc() &&
            Loc.getLocWithOffset(2) != UO->getSubExpr()->getBeginLoc() &&
            UO->getSubExpr()->getBeginLoc().isFileID()) {
          Diag(Loc, diag::warn_not_compound_assign)
              << (UO->getOpcode() == UO_Plus ? "+" : "-")
              << SourceRange(UO->getOperatorLoc(), UO->getOperatorLoc());
        }
      }
    }

    if (LLVM_UNLIKELY(DiagnoseAssignmentResult(ConvTy, Loc, LHSType, RHSType,
                                               RHS.get(), AA_Assigning)))
      return QualType();
  } else {
    QualType RHSType = CompoundType;
    AssignConvertType ConvTy =
        CheckAssignmentConstraints(Loc, LHSType, RHSType);
    if (LLVM_UNLIKELY(DiagnoseAssignmentResult(ConvTy, Loc, LHSType, RHSType,
                                               RHS.get(), AA_Assigning)))
      return QualType();
  }

  if (LLVM_UNLIKELY(
          (!isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(LHSExpr))))
    warnNullPtrDeref(*this, LHSExpr);

  return LHSType.getAtomicUnqualifiedType();
}

// Scenarios to ignore if expression E is:
// 1. an explicit cast expression into void
// 2. a function call expression that returns void
namespace {
bool shouldIgnoreCommaOperand(const Expr *E, const TreeContext &Context) {
  E = E->IgnoreParens();

  if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
    if (CE->getCastKind() == CK_ToVoid) {
      return true;
    }
  }

  if (const auto *CE = dyn_cast<CallExpr>(E))
    return CE->getCallReturnType(Context)->isVoidType();
  return false;
}
} // namespace

// Look for instances where it is likely the comma operator is confused with
// another operator.  There is an explicit list of acceptable expressions for
// the left hand side of the comma operator, otherwise emit a warning.
void Sema::DiagnoseCommaOperator(const Expr *LHS, SourceLocation Loc) {
  // No warnings in macros
  if (Loc.isMacroID())
    return;

  // Scope isn't fine-grained enough to explicitly list the specific cases, so
  // instead, skip more than needed, then call back into here with the
  // CommaVisitor in SemaStmt.cpp.
  // The listed locations are the initialization and increment portions
  // of a for loop.  The additional checks are on the condition of
  // if statements, do/while loops, and for loops.
  // Differences in scope flags for C89 mode requires the extra logic.
  const unsigned ForIncrementFlags =
      getLangOpts().C99
          ? Scope::ControlScope | Scope::ContinueScope | Scope::BreakScope
          : Scope::ContinueScope | Scope::BreakScope;
  const unsigned ForInitFlags = Scope::ControlScope | Scope::DeclScope;
  const unsigned ScopeFlags = getCurScope()->getFlags();
  if ((ScopeFlags & ForIncrementFlags) == ForIncrementFlags ||
      (ScopeFlags & ForInitFlags) == ForInitFlags)
    return;

  // If there are multiple comma operators used together, get the RHS of the
  // of the comma operator as the LHS.
  while (const BinaryOperator *BO = dyn_cast<BinaryOperator>(LHS)) {
    if (BO->getOpcode() != BO_Comma)
      break;
    LHS = BO->getRHS();
  }

  // Only allow some expressions on LHS to not warn.
  if (shouldIgnoreCommaOperand(LHS, Context))
    return;

  Diag(Loc, diag::warn_comma_operator);
  Diag(LHS->getBeginLoc(), diag::note_cast_to_void)
      << LHS->getSourceRange()
      << FixItHint::CreateInsertion(LHS->getBeginLoc(), "(void)(")
      << FixItHint::CreateInsertion(PP.getLocForEndOfToken(LHS->getEndLoc()),
                                    ")");
}

// C99 6.5.17
namespace {
QualType checkCommaOperands(Sema &S, ExprResult &LHS, ExprResult &RHS,
                            SourceLocation Loc) {
  LHS = S.CheckPlaceholderExpr(LHS.get());
  RHS = S.CheckPlaceholderExpr(RHS.get());
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Comma: discard the LHS value, then complete the RHS type for checks.
  LHS = S.IgnoredValueConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();
#ifndef _WIN32
  S.DiagnoseUnusedExprResult(LHS.get(), diag::warn_unused_comma_left_operand);
#endif
  RHS = S.DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();
  if (!RHS.get()->getType()->isVoidType())
    S.RequireCompleteType(Loc, RHS.get()->getType(), diag::err_incomplete_type);

  if (!S.getDiagnostics().isIgnored(diag::warn_comma_operator, Loc))
    S.DiagnoseCommaOperator(LHS.get(), Loc);

  return RHS.get()->getType();
}
} // namespace


namespace {
ValueDecl *getPrimaryDecl(Expr *E) {
  switch (E->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    return cast<DeclRefExpr>(E)->getDecl();
  case Stmt::MemberExprClass:
    // If this is an arrow operator, the address is an offset from
    // the base's value, so the object the base refers to is
    // irrelevant.
    if (cast<MemberExpr>(E)->isArrow())
      return nullptr;
    // Otherwise, the expression refers to a part of the base
    return getPrimaryDecl(cast<MemberExpr>(E)->getBase());
  case Stmt::ArraySubscriptExprClass: {
    Expr *Base = cast<ArraySubscriptExpr>(E)->getBase();
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Base)) {
      if (ICE->getSubExpr()->getType()->isArrayType())
        return getPrimaryDecl(ICE->getSubExpr());
    }
    return nullptr;
  }
  case Stmt::UnaryOperatorClass: {
    UnaryOperator *UO = cast<UnaryOperator>(E);

    switch (UO->getOpcode()) {
    case UO_Real:
    case UO_Imag:
    case UO_Extension:
      return getPrimaryDecl(UO->getSubExpr());
    default:
      return nullptr;
    }
  }
  case Stmt::ParenExprClass:
    return getPrimaryDecl(cast<ParenExpr>(E)->getSubExpr());
  case Stmt::ImplicitCastExprClass:
    // If the result of an implicit cast is an l-value, we care about
    // the sub-expression; otherwise, the result here doesn't matter.
    return getPrimaryDecl(cast<ImplicitCastExpr>(E)->getSubExpr());
  default:
    return nullptr;
  }
}
} // namespace

namespace {
enum {
  AO_Bit_Field = 0,
  AO_Vector_Element = 1,
  AO_Property_Expansion = 2,
  AO_Register_Variable = 3,
  AO_Matrix_Element = 4,
  AO_No_Error = 5
};
}
namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnAddressOfInvalidType(Sema &S, SourceLocation Loc, Expr *E,
                              unsigned Type) {
  S.Diag(Loc, diag::err_typecheck_address_of) << Type << E->getSourceRange();
}
} // namespace

QualType Sema::CheckAddressOfOperand(ExprResult &OrigOp, SourceLocation OpLoc) {
  if (OrigOp.get()->getType()->getAsPlaceholderType()) {
    OrigOp = CheckPlaceholderExpr(OrigOp.get());
    if (OrigOp.isInvalid())
      return QualType();
  }

  if (OrigOp.get()->isTypeDependent())
    return Context.DependentTy;

  assert(!OrigOp.get()->hasPlaceholderType());

  // Make sure to ignore parentheses in subsequent checks
  Expr *op = OrigOp.get()->IgnoreParens();

  if (getLangOpts().C99) {
    // Implement C99-only parts of addressof rules.
    if (UnaryOperator *uOp = dyn_cast<UnaryOperator>(op)) {
      if (uOp->getOpcode() == UO_Deref)
        // Per C99 6.5.3.2, the address of a deref always returns a valid result
        // (assuming the deref expression is valid).
        return uOp->getSubExpr()->getType();
    }
    // Technically, there should be a check for array subscript
    // expressions here, but the result of one is always an lvalue anyway.
  }
  ValueDecl *dcl = getPrimaryDecl(op);

  if (auto *FD = dyn_cast_or_null<FunctionDecl>(dcl))
    if (!checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                           op->getBeginLoc()))
      return QualType();

  Expr::LValueClassification lval = op->ClassifyLValue(Context);
  unsigned AddressOfError = AO_No_Error;

  if (lval != Expr::LV_Valid && lval != Expr::LV_IncompleteVoidType) {
    // C99 6.5.3.2p1
    // The operand must be either an l-value or a function designator
    if (!op->getType()->isFunctionType()) {
      Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof)
          << op->getType() << op->getSourceRange();
      return QualType();
    }

  } else if (op->getObjectKind() == OK_BitField) { // C99 6.5.3.2p1
    // The operand cannot be a bit-field
    AddressOfError = AO_Bit_Field;
  } else if (op->getObjectKind() == OK_VectorComponent) {
    // The operand cannot be an element of a vector
    AddressOfError = AO_Vector_Element;
  } else if (op->getObjectKind() == OK_MatrixComponent) {
    // The operand cannot be an element of a matrix.
    AddressOfError = AO_Matrix_Element;
  } else if (dcl) { // C99 6.5.3.2p1
    // We have an lvalue with a decl. Make sure the decl is not declared
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      if (vd->getStorageClass() == SC_Register)
        AddressOfError = AO_Register_Variable;
    } else if (isa<FieldDecl>(dcl) || isa<IndirectFieldDecl>(dcl)) {
      // Okay: we can take the address of a field.
    } else if (!isa<FunctionDecl>(dcl))
      llvm_unreachable("Unknown/unexpected decl type");
  }

  if (AddressOfError != AO_No_Error) {
    warnAddressOfInvalidType(*this, OpLoc, op, AddressOfError);
    return QualType();
  }

  if (lval == Expr::LV_IncompleteVoidType) {
    // Taking the address of a void variable is technically illegal, but we
    // allow it in cases which are otherwise valid.
    // Example: "extern void x; void* y = &x;".
    Diag(OpLoc, diag::ext_typecheck_addrof_void) << op->getSourceRange();
  }

  CheckAddressOfPackedMember(op);

  return Context.getPointerType(op->getType());
}

namespace neverc {
void noteModifiableNonNullParam(Sema &S, const Expr *Exp) {
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Exp);
  if (!DRE)
    return;
  const Decl *D = DRE->getDecl();
  if (!D)
    return;
  const ParmVarDecl *Param = dyn_cast<ParmVarDecl>(D);
  if (!Param)
    return;
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(Param->getDeclContext()))
    if (!FD->hasAttr<NonNullAttr>() && !Param->hasAttr<NonNullAttr>())
      return;
  if (FunctionScopeInfo *FD = S.getCurFunction())
    FD->ModifiedNonNullParams.insert(Param);
}
} // namespace

namespace neverc {
QualType checkIndirectionOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                 SourceLocation OpLoc,
                                 bool IsAfterAmp = false) {
  if (Op->isTypeDependent())
    return S.Context.DependentTy;

  ExprResult ConvResult = S.UsualUnaryConversions(Op);
  if (ConvResult.isInvalid())
    return QualType();
  Op = ConvResult.get();
  QualType OpTy = Op->getType();
  QualType Result;

  if (const PointerType *PT = OpTy->getAs<PointerType>()) {
    Result = PT->getPointeeType();
  } else {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid())
      return QualType();
    if (PR.get() != Op)
      return checkIndirectionOperand(S, PR.get(), VK, OpLoc);
  }

  if (Result.isNull()) {
    S.Diag(OpLoc, diag::err_typecheck_indirection_requires_pointer)
        << OpTy << Op->getSourceRange();
    return QualType();
  }

  if (Result->isVoidType()) {
    if (!(S.getLangOpts().C99 && IsAfterAmp) && !S.isUnevaluatedContext())
      S.Diag(OpLoc, diag::ext_typecheck_indirection_through_void_pointer)
          << OpTy << Op->getSourceRange();
  }

  // Dereferences are usually l-values...
  VK = VK_LValue;

  if (Result.isCForbiddenLValueType())
    VK = VK_PRValue;

  return Result;
}
} // namespace

BinaryOperatorKind Sema::ConvertTokenKindToBinaryOpcode(tok::TokenKind Kind) {
  BinaryOperatorKind Opc;
  switch (Kind) {
  default:
    llvm_unreachable("Unknown binop!");
  case tok::star:
    Opc = BO_Mul;
    break;
  case tok::slash:
    Opc = BO_Div;
    break;
  case tok::percent:
    Opc = BO_Rem;
    break;
  case tok::plus:
    Opc = BO_Add;
    break;
  case tok::minus:
    Opc = BO_Sub;
    break;
  case tok::lessless:
    Opc = BO_Shl;
    break;
  case tok::greatergreater:
    Opc = BO_Shr;
    break;
  case tok::lessequal:
    Opc = BO_LE;
    break;
  case tok::less:
    Opc = BO_LT;
    break;
  case tok::greaterequal:
    Opc = BO_GE;
    break;
  case tok::greater:
    Opc = BO_GT;
    break;
  case tok::exclaimequal:
    Opc = BO_NE;
    break;
  case tok::equalequal:
    Opc = BO_EQ;
    break;
  case tok::amp:
    Opc = BO_And;
    break;
  case tok::caret:
    Opc = BO_Xor;
    break;
  case tok::pipe:
    Opc = BO_Or;
    break;
  case tok::ampamp:
    Opc = BO_LAnd;
    break;
  case tok::pipepipe:
    Opc = BO_LOr;
    break;
  case tok::equal:
    Opc = BO_Assign;
    break;
  case tok::starequal:
    Opc = BO_MulAssign;
    break;
  case tok::slashequal:
    Opc = BO_DivAssign;
    break;
  case tok::percentequal:
    Opc = BO_RemAssign;
    break;
  case tok::plusequal:
    Opc = BO_AddAssign;
    break;
  case tok::minusequal:
    Opc = BO_SubAssign;
    break;
  case tok::lesslessequal:
    Opc = BO_ShlAssign;
    break;
  case tok::greatergreaterequal:
    Opc = BO_ShrAssign;
    break;
  case tok::ampequal:
    Opc = BO_AndAssign;
    break;
  case tok::caretequal:
    Opc = BO_XorAssign;
    break;
  case tok::pipeequal:
    Opc = BO_OrAssign;
    break;
  case tok::comma:
    Opc = BO_Comma;
    break;
  }
  return Opc;
}

namespace {
inline UnaryOperatorKind ConvertTokenKindToUnaryOpcode(tok::TokenKind Kind) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default:
    llvm_unreachable("Unknown unary op!");
  case tok::plusplus:
    Opc = UO_PreInc;
    break;
  case tok::minusminus:
    Opc = UO_PreDec;
    break;
  case tok::amp:
    Opc = UO_AddrOf;
    break;
  case tok::star:
    Opc = UO_Deref;
    break;
  case tok::plus:
    Opc = UO_Plus;
    break;
  case tok::minus:
    Opc = UO_Minus;
    break;
  case tok::tilde:
    Opc = UO_Not;
    break;
  case tok::exclaim:
    Opc = UO_LNot;
    break;
  case tok::kw___real:
    Opc = UO_Real;
    break;
  case tok::kw___imag:
    Opc = UO_Imag;
    break;
  case tok::kw___extension__:
    Opc = UO_Extension;
    break;
  }
  return Opc;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseSelfAssignment(Sema &S, Expr *LHSExpr, Expr *RHSExpr,
                            SourceLocation OpLoc) {
  if (S.isUnevaluatedContext())
    return;
  if (OpLoc.isInvalid() || OpLoc.isMacroID())
    return;
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();
  const DeclRefExpr *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr);
  const DeclRefExpr *RHSDeclRef = dyn_cast<DeclRefExpr>(RHSExpr);
  if (!LHSDeclRef || !RHSDeclRef || LHSDeclRef->getLocation().isMacroID() ||
      RHSDeclRef->getLocation().isMacroID())
    return;
  const ValueDecl *LHSDecl =
      cast<ValueDecl>(LHSDeclRef->getDecl()->getCanonicalDecl());
  const ValueDecl *RHSDecl =
      cast<ValueDecl>(RHSDeclRef->getDecl()->getCanonicalDecl());
  if (LHSDecl != RHSDecl)
    return;
  if (LHSDecl->getType().isVolatileQualified())
    return;
  return;

  S.Diag(OpLoc, diag::warn_self_assignment_builtin)
      << LHSDeclRef->getType() << LHSExpr->getSourceRange()
      << RHSExpr->getSourceRange() << 0;
}
} // namespace

// This helper function promotes a binary operator's operands (which are of a
// half vector type) to a vector of floats and then truncates the result to
// a vector of either half or short.
namespace {
ExprResult emitHalfVecBinOp(Sema &S, ExprResult LHS, ExprResult RHS,
                            BinaryOperatorKind Opc, QualType ResultTy,
                            ExprValueKind VK, ExprObjectKind OK,
                            bool IsCompAssign, SourceLocation OpLoc,
                            FPOptionsOverride FPFeatures) {
  auto &Context = S.getTreeContext();
  assert((isVector(ResultTy, Context.HalfTy) ||
          isVector(ResultTy, Context.ShortTy)) &&
         "Result must be a vector of half or short");
  assert(isVector(LHS.get()->getType(), Context.HalfTy) &&
         isVector(RHS.get()->getType(), Context.HalfTy) &&
         "both operands expected to be a half vector");

  RHS = castVectorElement(RHS.get(), Context.FloatTy, S);
  QualType BinOpResTy = RHS.get()->getType();

  // If Opc is a comparison, ResultType is a vector of shorts. In that case,
  // change BinOpResTy to a vector of ints.
  if (isVector(ResultTy, Context.ShortTy))
    BinOpResTy = S.GetSignedVectorType(BinOpResTy);

  if (IsCompAssign)
    return CompoundAssignOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                          ResultTy, VK, OK, OpLoc, FPFeatures,
                                          BinOpResTy, BinOpResTy);

  LHS = castVectorElement(LHS.get(), Context.FloatTy, S);
  auto *BO = BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                    BinOpResTy, VK, OK, OpLoc, FPFeatures);
  return castVectorElement(BO, ResultTy->castAs<VectorType>()->getElementType(),
                           S);
}
} // namespace

namespace neverc {
bool requiresHalfVecConversion(bool OpRequiresConversion, TreeContext &Ctx,
                               Expr *E0, Expr *E1 = nullptr) {
  if (!OpRequiresConversion || Ctx.getLangOpts().NativeHalfType ||
      Ctx.getTargetInfo().useFP16ConversionIntrinsics())
    return false;

  auto HasVectorOfHalfType = [&Ctx](Expr *E) {
    QualType Ty = E->IgnoreImplicit()->getType();

    if (const VectorType *VT = Ty->getAs<VectorType>()) {
      if (VT->getVectorKind() == VectorKind::Neon)
        return false;
      return VT->getElementType().getCanonicalType() == Ctx.HalfTy;
    }
    return false;
  };

  return HasVectorOfHalfType(E0) && (!E1 || HasVectorOfHalfType(E1));
}
} // namespace

__attribute__((hot)) ExprResult Sema::CreateBuiltinBinOp(SourceLocation OpLoc,
                                                         BinaryOperatorKind Opc,
                                                         Expr *LHSExpr,
                                                         Expr *RHSExpr) {
  {
    bool FastPathOpc;
    if (LLVM_LIKELY(Diags.getIgnoreAllWarnings()))
      FastPathOpc = (Opc >= BO_Mul && Opc <= BO_Or) || Opc == BO_Assign;
    else
      FastPathOpc =
          (Opc == BO_Mul || Opc == BO_Add || Opc == BO_Sub || Opc == BO_And ||
           Opc == BO_Xor || Opc == BO_Or || Opc == BO_Assign);
    if (LLVM_LIKELY(FastPathOpc)) {
      QualType LTy = LHSExpr->getType(), RTy = RHSExpr->getType();
      if (LLVM_LIKELY(LTy == RTy)) {
        if (const auto *BT = LTy->getAs<BuiltinType>()) {
          unsigned K = BT->getKind();
          if (LLVM_LIKELY(K == BuiltinType::Int || K == BuiltinType::UInt ||
                          K == BuiltinType::Long || K == BuiltinType::ULong ||
                          K == BuiltinType::LongLong ||
                          K == BuiltinType::ULongLong)) {
            QualType ResultTy = LTy.getUnqualifiedType();
            FPOptionsOverride FPO = CurFPFeatureOverrides();
            if (LLVM_UNLIKELY(Opc == BO_Assign)) {
              if (LLVM_LIKELY(LHSExpr->isLValue() && !LTy.isConstQualified() &&
                              !LTy.isVolatileQualified())) {
                ExprResult RHS = RHSExpr;
                if (RHSExpr->isLValue())
                  RHS = ImplicitCastExpr::Create(Context, ResultTy,
                                                 CK_LValueToRValue, RHSExpr,
                                                 VK_PRValue, FPO);
                return BinaryOperator::Create(Context, LHSExpr, RHS.get(), Opc,
                                              ResultTy, VK_PRValue, OK_Ordinary,
                                              OpLoc, FPO);
              }
            } else {
              ExprResult LHS = LHSExpr, RHS = RHSExpr;
              if (LHSExpr->isLValue())
                LHS = ImplicitCastExpr::Create(Context, ResultTy,
                                               CK_LValueToRValue, LHSExpr,
                                               VK_PRValue, FPO);
              if (RHSExpr->isLValue())
                RHS = ImplicitCastExpr::Create(Context, ResultTy,
                                               CK_LValueToRValue, RHSExpr,
                                               VK_PRValue, FPO);
              QualType BinOpTy = BinaryOperator::isComparisonOp(Opc)
                                     ? Context.getLogicalOperationType()
                                     : ResultTy;
              return BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                            BinOpTy, VK_PRValue, OK_Ordinary,
                                            OpLoc, FPO);
            }
          }
        }
      }
    }
  }

  ExprResult LHS = LHSExpr, RHS = RHSExpr;
  QualType ResultTy; // Result type of the binary operator.
  // The following two variables are used for compound assignment operators
  QualType CompLHSTy;    // Type of LHS after promotions for computation
  QualType CompResultTy; // Type of computation result
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  bool ConvertHalfVec = false;

  {
    QualType LTy = LHSExpr->getType(), RTy = RHSExpr->getType();
    if (LLVM_UNLIKELY(!LTy->isIntegerType() && !LTy->isPointerType() &&
                      !LTy->isRealFloatingType()))
      checkTypeSupport(LTy, OpLoc, /*ValueDecl*/ nullptr);
    if (LLVM_UNLIKELY(!RTy->isIntegerType() && !RTy->isPointerType() &&
                      !RTy->isRealFloatingType()))
      checkTypeSupport(RTy, OpLoc, /*ValueDecl*/ nullptr);
  }

  switch (Opc) {
  case BO_Assign:
    ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, QualType(), Opc);
    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings() && !ResultTy.isNull())) {
      diagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc);
      noteModifiableNonNullParam(*this, LHS.get());
    }
    break;
  case BO_Mul:
  case BO_Div:
    ConvertHalfVec = true;
    ResultTy =
        CheckMultiplyDivideOperands(LHS, RHS, OpLoc, false, Opc == BO_Div);
    break;
  case BO_Rem:
    ResultTy = CheckRemainderOperands(LHS, RHS, OpLoc);
    break;
  case BO_Add:
    ConvertHalfVec = true;
    ResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_Sub:
    ConvertHalfVec = true;
    ResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc);
    break;
  case BO_Shl:
  case BO_Shr:
    ResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LE:
  case BO_LT:
  case BO_GE:
  case BO_GT:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_EQ:
  case BO_NE:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_And:
  case BO_Xor:
  case BO_Or:
    ResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LAnd:
  case BO_LOr:
    ConvertHalfVec = true;
    ResultTy = CheckLogicalOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_MulAssign:
  case BO_DivAssign:
    ConvertHalfVec = true;
    CompResultTy =
        CheckMultiplyDivideOperands(LHS, RHS, OpLoc, true, Opc == BO_DivAssign);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_RemAssign:
    CompResultTy = CheckRemainderOperands(LHS, RHS, OpLoc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_AddAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, Opc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_SubAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_ShlAssign:
  case BO_ShrAssign:
    CompResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_AndAssign:
  case BO_OrAssign: // fallthrough
    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings()))
      diagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc);
    [[fallthrough]];
  case BO_XorAssign:
    CompResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, Opc);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_Comma:
    ResultTy = checkCommaOperands(*this, LHS, RHS, OpLoc);
    break;
  }
  if (ResultTy.isNull() || LHS.isInvalid() || RHS.isInvalid())
    return ExprError();

  // Some of the binary operations require promoting operands of half vector to
  // float vectors and truncating the result back to half vector. For now, we do
  // this only when HalfArgsAndReturn is set (that is, when the target is arm or
  // arm64).
  assert(
      (Opc == BO_Comma || isVector(RHS.get()->getType(), Context.HalfTy) ==
                              isVector(LHS.get()->getType(), Context.HalfTy)) &&
      "both sides are half vectors or neither sides are");
  ConvertHalfVec =
      requiresHalfVecConversion(ConvertHalfVec, Context, LHS.get(), RHS.get());

  {
    auto SC = LHS.get()->getStmtClass();
    if (LLVM_UNLIKELY(SC != Stmt::BinaryOperatorClass &&
                      SC != Stmt::CompoundAssignOperatorClass &&
                      SC != Stmt::IntegerLiteralClass &&
                      SC != Stmt::FloatingLiteralClass &&
                      SC != Stmt::CharacterLiteralClass &&
                      SC != Stmt::DeclRefExprClass &&
                      SC != Stmt::CallExprClass))
      CheckArrayAccess(LHS.get());
    SC = RHS.get()->getStmtClass();
    if (LLVM_UNLIKELY(SC != Stmt::BinaryOperatorClass &&
                      SC != Stmt::CompoundAssignOperatorClass &&
                      SC != Stmt::IntegerLiteralClass &&
                      SC != Stmt::FloatingLiteralClass &&
                      SC != Stmt::CharacterLiteralClass &&
                      SC != Stmt::DeclRefExprClass &&
                      SC != Stmt::CallExprClass))
      CheckArrayAccess(RHS.get());
  }

  // Opc is not a compound assignment if CompResultTy is null.
  if (CompResultTy.isNull()) {
    if (ConvertHalfVec)
      return emitHalfVecBinOp(*this, LHS, RHS, Opc, ResultTy, VK, OK, false,
                              OpLoc, CurFPFeatureOverrides());
    return BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc, ResultTy,
                                  VK, OK, OpLoc, CurFPFeatureOverrides());
  }

  // The LHS is not converted to the result type for fixed-point compound
  // assignment as the common type is computed on demand. Reset the CompLHSTy
  // to the LHS type we would have gotten after unary conversions.
  if (CompResultTy->isFixedPointType())
    CompLHSTy = UsualUnaryConversions(LHS.get()).get()->getType();

  if (ConvertHalfVec)
    return emitHalfVecBinOp(*this, LHS, RHS, Opc, ResultTy, VK, OK, true, OpLoc,
                            CurFPFeatureOverrides());

  return CompoundAssignOperator::Create(
      Context, LHS.get(), RHS.get(), Opc, ResultTy, VK, OK, OpLoc,
      CurFPFeatureOverrides(), CompLHSTy, CompResultTy);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBitwisePrecedence(Sema &Self, BinaryOperatorKind Opc,
                               SourceLocation OpLoc, Expr *LHSExpr,
                               Expr *RHSExpr) {
  BinaryOperator *LHSBO = dyn_cast<BinaryOperator>(LHSExpr);
  BinaryOperator *RHSBO = dyn_cast<BinaryOperator>(RHSExpr);

  // Check that one of the sides is a comparison operator and the other isn't.
  bool isLeftComp = LHSBO && LHSBO->isComparisonOp();
  bool isRightComp = RHSBO && RHSBO->isComparisonOp();
  if (isLeftComp == isRightComp)
    return;

  // Bitwise operations are sometimes used as eager logical ops.
  // Don't diagnose this.
  bool isLeftBitwise = LHSBO && LHSBO->isBitwiseOp();
  bool isRightBitwise = RHSBO && RHSBO->isBitwiseOp();
  if (isLeftBitwise || isRightBitwise)
    return;

  SourceRange DiagRange = isLeftComp
                              ? SourceRange(LHSExpr->getBeginLoc(), OpLoc)
                              : SourceRange(OpLoc, RHSExpr->getEndLoc());
  llvm::StringRef OpStr =
      isLeftComp ? LHSBO->getOpcodeStr() : RHSBO->getOpcodeStr();
  SourceRange ParensRange =
      isLeftComp
          ? SourceRange(LHSBO->getRHS()->getBeginLoc(), RHSExpr->getEndLoc())
          : SourceRange(LHSExpr->getBeginLoc(), RHSBO->getLHS()->getEndLoc());

  Self.Diag(OpLoc, diag::warn_precedence_bitwise_rel)
      << DiagRange << BinaryOperator::getOpcodeStr(Opc) << OpStr;
  suggestParentheses(Self, OpLoc,
                     Self.PDiag(diag::note_precedence_silence) << OpStr,
                     (isLeftComp ? LHSExpr : RHSExpr)->getSourceRange());
  suggestParentheses(Self, OpLoc,
                     Self.PDiag(diag::note_precedence_bitwise_first)
                         << BinaryOperator::getOpcodeStr(Opc),
                     ParensRange);
}
} // namespace

namespace {
void genDiagnosticForLogicalAndInLogicalOr(Sema &Self, SourceLocation OpLoc,
                                           BinaryOperator *Bop) {
  assert(Bop->getOpcode() == BO_LAnd);
  Self.Diag(Bop->getOperatorLoc(), diag::warn_logical_and_in_logical_or)
      << Bop->getSourceRange() << OpLoc;
  suggestParentheses(Self, Bop->getOperatorLoc(),
                     Self.PDiag(diag::note_precedence_silence)
                         << Bop->getOpcodeStr(),
                     Bop->getSourceRange());
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseLogicalAndInLogicalOrLHS(Sema &S, SourceLocation OpLoc,
                                      Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(LHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "string_literal && a || b" don't warn since the precedence
      // doesn't matter.
      if (!isa<StringLiteral>(Bop->getLHS()->IgnoreParenImpCasts()))
        return genDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    } else if (Bop->getOpcode() == BO_LOr) {
      if (BinaryOperator *RBop = dyn_cast<BinaryOperator>(Bop->getRHS())) {
        // If it's "a || b && string_literal || c" we didn't warn earlier for
        // "a || b && string_literal", but warn now.
        if (RBop->getOpcode() == BO_LAnd &&
            isa<StringLiteral>(RBop->getRHS()->IgnoreParenImpCasts()))
          return genDiagnosticForLogicalAndInLogicalOr(S, OpLoc, RBop);
      }
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseLogicalAndInLogicalOrRHS(Sema &S, SourceLocation OpLoc,
                                      Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(RHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "a || b && string_literal" don't warn since the precedence
      // doesn't matter.
      if (!isa<StringLiteral>(Bop->getRHS()->IgnoreParenImpCasts()))
        return genDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBitwiseOpInBitwiseOp(Sema &S, BinaryOperatorKind Opc,
                                  SourceLocation OpLoc, Expr *SubExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(SubExpr)) {
    if (Bop->isBitwiseOp() && Bop->getOpcode() < Opc) {
      S.Diag(Bop->getOperatorLoc(), diag::warn_bitwise_op_in_bitwise_op)
          << Bop->getOpcodeStr() << BinaryOperator::getOpcodeStr(Opc)
          << Bop->getSourceRange() << OpLoc;
      suggestParentheses(S, Bop->getOperatorLoc(),
                         S.PDiag(diag::note_precedence_silence)
                             << Bop->getOpcodeStr(),
                         Bop->getSourceRange());
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseAdditionInShift(Sema &S, SourceLocation OpLoc, Expr *SubExpr,
                             llvm::StringRef Shift) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(SubExpr)) {
    if (Bop->getOpcode() == BO_Add || Bop->getOpcode() == BO_Sub) {
      llvm::StringRef Op = Bop->getOpcodeStr();
      S.Diag(Bop->getOperatorLoc(), diag::warn_addition_in_bitshift)
          << Bop->getSourceRange() << OpLoc << Shift << Op;
      suggestParentheses(S, Bop->getOperatorLoc(),
                         S.PDiag(diag::note_precedence_silence) << Op,
                         Bop->getSourceRange());
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBinOpPrecedence(Sema &Self, BinaryOperatorKind Opc,
                             SourceLocation OpLoc, Expr *LHSExpr,
                             Expr *RHSExpr) {
  // Diagnose "arg1 'bitwise' arg2 'eq' arg3".
  if (BinaryOperator::isBitwiseOp(Opc))
    diagnoseBitwisePrecedence(Self, Opc, OpLoc, LHSExpr, RHSExpr);

  // Diagnose "arg1 & arg2 | arg3"
  if ((Opc == BO_Or || Opc == BO_Xor) &&
      !OpLoc.isMacroID() /* Don't warn in macros. */) {
    diagnoseBitwiseOpInBitwiseOp(Self, Opc, OpLoc, LHSExpr);
    diagnoseBitwiseOpInBitwiseOp(Self, Opc, OpLoc, RHSExpr);
  }

  // Warn about arg1 || arg2 && arg3, as GCC 4.3+ does.
  // We don't warn for 'assert(a || b && "bad")' since this is safe.
  if (Opc == BO_LOr && !OpLoc.isMacroID() /* Don't warn in macros. */) {
    diagnoseLogicalAndInLogicalOrLHS(Self, OpLoc, LHSExpr, RHSExpr);
    diagnoseLogicalAndInLogicalOrRHS(Self, OpLoc, LHSExpr, RHSExpr);
  }

  if ((Opc == BO_Shl &&
       LHSExpr->getType()->isIntegralType(Self.getTreeContext())) ||
      Opc == BO_Shr) {
    llvm::StringRef Shift = BinaryOperator::getOpcodeStr(Opc);
    diagnoseAdditionInShift(Self, OpLoc, LHSExpr, Shift);
    diagnoseAdditionInShift(Self, OpLoc, RHSExpr, Shift);
  }
}
} // namespace

namespace {
ExprResult buildNeverCStringAssign(Sema &S, Scope *Sc, SourceLocation OpLoc,
                                   Expr *LHS, Expr *RHS) {
  if (checkForModifiableLvalue(LHS, OpLoc, S))
    return ExprError();

  ExprResult LHSAddr = S.FormUnaryOp(Sc, OpLoc, UO_AddrOf, LHS);
  if (LHSAddr.isInvalid())
    return ExprError();

  Expr *Args[] = {LHSAddr.get(), RHS};
  return buildNeverCStringRuntimeCall(
      S, Sc, OpLoc, BuiltinStringNames::AssignFunctionName, Args, OpLoc);
}
} // namespace

namespace {
Expr *promoteCharToNeverCString(Sema &S, SourceLocation OpLoc, Expr *E) {
  if (!E)
    return nullptr;
  QualType T = E->getType();
  if (!T->isIntegerType() || S.isNeverCStringType(T))
    return nullptr;
  // Reject `_Bool` so `s + true` is still a hard error -- the
  // canonical std::string-parity surface only accepts char-like
  // integers (the from_char helper truncates the value to a byte).
  if (T->isBooleanType())
    return nullptr;
  ExprResult Lvalue = S.DefaultLvalueConversion(E);
  if (Lvalue.isInvalid())
    return nullptr;
  // Truncate to `char` so the wrapped value matches the runtime
  // helper signature; mirrors the implicit conversion C performs
  // for `char x = some_int;` itself.
  ExprResult Truncated = Lvalue;
  if (Truncated.get()->getType() != S.Context.CharTy)
    Truncated =
        S.ImpCastExprToType(Truncated.get(), S.Context.CharTy, CK_IntegralCast);
  if (Truncated.isInvalid())
    return nullptr;
  Expr *Args[] = {Truncated.get()};
  ExprResult Wrapped = buildNeverCStringRuntimeCall(
      S, /*Scope=*/nullptr, OpLoc, BuiltinStringNames::FromCharFunctionName,
      Args, OpLoc);
  if (Wrapped.isInvalid())
    return nullptr;
  return Wrapped.get();
}
} // namespace

namespace {
std::optional<ExprResult> tryNeverCStringBinaryOpRewrite(Sema &S, Scope *Sc,
                                                         SourceLocation OpLoc,
                                                         BinaryOperatorKind Opc,
                                                         Expr *LHS, Expr *RHS) {
  // Fast reject: only assign / add-assign / add / comparison ops can be
  // NeverC string rewrites, and at least one operand must look like it
  // could be a record or string-literal type.  This keeps every other
  // binary op (shifts, bitwise, logical, ...) from paying the
  // isNeverCStringType type-class check.
  if (Opc != BO_Assign && Opc != BO_AddAssign && Opc != BO_Add &&
      !BinaryOperator::isComparisonOp(Opc))
    return std::nullopt;
  if (!couldBeNeverCStringOperand(LHS) && !couldBeNeverCStringOperand(RHS))
    return std::nullopt;

  // Plain assignment `s = t`.
  if (Opc == BO_Assign && S.isNeverCStringType(LHS->getType()) &&
      !S.isInsideNeverCStringRuntime())
    return buildNeverCStringAssign(S, Sc, OpLoc, LHS, RHS);

  // Compound `s += t`.
  if (Opc == BO_AddAssign && S.isNeverCStringType(LHS->getType())) {
    Expr *RHSExpr = RHS;
    if (!isNeverCStringOperand(S, RHS)) {
      Expr *Promoted = promoteCharToNeverCString(S, OpLoc, RHS);
      if (!Promoted)
        return std::nullopt;
      RHSExpr = Promoted;
    }
    ExprResult Joined = buildNeverCStringConcat(S, OpLoc, LHS, RHSExpr);
    if (Joined.isInvalid())
      return Joined;
    return buildNeverCStringAssign(S, Sc, OpLoc, LHS, Joined.get());
  }

  // Pure concat `s + t` and the char shapes `s + ch` / `ch + s`.
  if (Opc == BO_Add) {
    bool LHSIsString = isNeverCStringOperand(S, LHS);
    bool RHSIsString = isNeverCStringOperand(S, RHS);
    if (LHSIsString && RHSIsString)
      return buildNeverCStringConcat(S, OpLoc, LHS, RHS);
    if (LHSIsString && !RHSIsString) {
      if (Expr *Wrapped = promoteCharToNeverCString(S, OpLoc, RHS))
        return buildNeverCStringConcat(S, OpLoc, LHS, Wrapped);
    }
    if (RHSIsString && !LHSIsString) {
      if (Expr *Wrapped = promoteCharToNeverCString(S, OpLoc, LHS))
        return buildNeverCStringConcat(S, OpLoc, Wrapped, RHS);
    }
  }

  // Comparison operators.
  if (BinaryOperator::isComparisonOp(Opc) && isNeverCStringOperand(S, LHS) &&
      isNeverCStringOperand(S, RHS))
    return buildNeverCStringCompare(S, OpLoc, Opc, LHS, RHS);

  return std::nullopt;
}
} // namespace

// Binary Operators.  'Tok' is the token for the operator.
__attribute__((hot)) ExprResult Sema::OnBinOp(Scope *S, SourceLocation TokLoc,
                                              tok::TokenKind Kind,
                                              Expr *LHSExpr, Expr *RHSExpr) {
  BinaryOperatorKind Opc = ConvertTokenKindToBinaryOpcode(Kind);
  assert(LHSExpr && "OnBinOp(): missing left expression");
  assert(RHSExpr && "OnBinOp(): missing right expression");

  // Only bitwise, logical-or and shift operators can trigger precedence
  // warnings.  Skip the noinline diagnoseBinOpPrecedence call for the
  // overwhelmingly common arithmetic / assignment / comparison operators.
  if (LLVM_UNLIKELY(BinaryOperator::isBitwiseOp(Opc) || Opc == BO_LOr ||
                    Opc == BO_Shl || Opc == BO_Shr))
    diagnoseBinOpPrecedence(*this, Opc, TokLoc, LHSExpr, RHSExpr);

  return FormBinOp(S, TokLoc, Opc, LHSExpr, RHSExpr);
}

__attribute__((hot)) ExprResult Sema::FormBinOp(Scope *S, SourceLocation OpLoc,
                                                BinaryOperatorKind Opc,
                                                Expr *LHSExpr, Expr *RHSExpr) {
  const Type *LT = LHSExpr->getType().getTypePtrOrNull();
  const Type *RT = RHSExpr->getType().getTypePtrOrNull();

  if (LLVM_LIKELY(LT && RT && LT->isBuiltinType() && RT->isBuiltinType() &&
                  !LT->isPlaceholderType() && !RT->isPlaceholderType()))
    return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);

  if (LLVM_UNLIKELY(LT && LT->getAsPlaceholderType())) {
    ExprResult LHS = CheckPlaceholderExpr(LHSExpr);
    if (LHS.isInvalid())
      return ExprError();
    LHSExpr = LHS.get();
  }

  if (LLVM_UNLIKELY(RT && RT->getAsPlaceholderType())) {
    const BuiltinType *pty = RT->getAsPlaceholderType();
    if (Opc == BO_Assign && pty->getKind() == BuiltinType::Overload) {
      return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
    }

    ExprResult resolvedRHS = CheckPlaceholderExpr(RHSExpr);
    if (!resolvedRHS.isUsable())
      return ExprError();
    RHSExpr = resolvedRHS.get();
  }

  if (auto NeverCRewrite = tryNeverCStringBinaryOpRewrite(*this, S, OpLoc, Opc,
                                                          LHSExpr, RHSExpr))
    return *NeverCRewrite;

  if (getLangOpts().RecoveryAST &&
      (LHSExpr->isTypeDependent() || RHSExpr->isTypeDependent())) {
    if (BinaryOperator::isCompoundAssignmentOp(Opc))
      return CompoundAssignOperator::Create(
          Context, LHSExpr, RHSExpr, Opc,
          LHSExpr->getType().getUnqualifiedType(), VK_PRValue, OK_Ordinary,
          OpLoc, CurFPFeatureOverrides());
    QualType ResultType;
    switch (Opc) {
    case BO_Assign:
      ResultType = LHSExpr->getType().getUnqualifiedType();
      break;
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
    case BO_LAnd:
    case BO_LOr:
      ResultType = Context.IntTy;
      break;
    case BO_Comma:
      ResultType = RHSExpr->getType();
      break;
    default:
      ResultType = Context.DependentTy;
      break;
    }
    return BinaryOperator::Create(Context, LHSExpr, RHSExpr, Opc, ResultType,
                                  VK_PRValue, OK_Ordinary, OpLoc,
                                  CurFPFeatureOverrides());
  }

  return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
}
