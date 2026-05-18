#include "SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/AttrIterator.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include "neverc/Analyze/SemaInternal.h"

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Return value & float comparison checks
// ===----------------------------------------------------------------------===

void Sema::CheckReturnValExpr(Expr *RetValExp, QualType lhsType,
                              SourceLocation ReturnLoc, const AttrVec *Attrs) {
  if (((Attrs && hasSpecificAttr<ReturnsNonNullAttr>(*Attrs)) ||
       isNonNullType(lhsType)) &&
      CheckNonNullExpr(*this, RetValExp))
    Diag(ReturnLoc, diag::warn_null_ret) << RetValExp->getSourceRange();
}

void Sema::CheckFloatComparison(SourceLocation Loc, Expr *LHS, Expr *RHS,
                                BinaryOperatorKind Opcode) {
  if (!BinaryOperator::isEqualityOp(Opcode))
    return;

  // Match and capture subexpressions such as "(float) X == 0.1".
  FloatingLiteral *FPLiteral;
  CastExpr *FPCast;
  auto getCastAndLiteral = [&FPLiteral, &FPCast](Expr *L, Expr *R) {
    FPLiteral = dyn_cast<FloatingLiteral>(L->IgnoreParens());
    FPCast = dyn_cast<CastExpr>(R->IgnoreParens());
    return FPLiteral && FPCast;
  };

  if (getCastAndLiteral(LHS, RHS) || getCastAndLiteral(RHS, LHS)) {
    auto *SourceTy = FPCast->getSubExpr()->getType()->getAs<BuiltinType>();
    auto *TargetTy = FPLiteral->getType()->getAs<BuiltinType>();
    if (SourceTy && TargetTy && SourceTy->isFloatingPoint() &&
        TargetTy->isFloatingPoint()) {
      bool Lossy;
      llvm::APFloat TargetC = FPLiteral->getValue();
      TargetC.convert(Context.getFloatTypeSemantics(QualType(SourceTy, 0)),
                      llvm::APFloat::rmNearestTiesToEven, &Lossy);
      if (Lossy) {
        // If the literal cannot be represented in the source type, then a
        // check for == is always false and check for != is always true.
        Diag(Loc, diag::warn_float_compare_literal)
            << (Opcode == BO_EQ) << QualType(SourceTy, 0)
            << LHS->getSourceRange() << RHS->getSourceRange();
        return;
      }
    }
  }

  // Match a more general floating-point equality comparison (-Wfloat-equal).
  Expr *LeftExprSansParen = LHS->IgnoreParenImpCasts();
  Expr *RightExprSansParen = RHS->IgnoreParenImpCasts();

  // Special case: check for x == x (which is OK).
  // Do not emit warnings for such cases.
  if (auto *DRL = dyn_cast<DeclRefExpr>(LeftExprSansParen))
    if (auto *DRR = dyn_cast<DeclRefExpr>(RightExprSansParen))
      if (DRL->getDecl() == DRR->getDecl())
        return;

  // Special case: check for comparisons against literals that can be exactly
  //  represented by APFloat.  In such cases, do not emit a warning.  This
  //  is a heuristic: often comparison against such literals are used to
  //  detect if a value in a variable has not changed.  This clearly can
  //  lead to false negatives.
  if (FloatingLiteral *FLL = dyn_cast<FloatingLiteral>(LeftExprSansParen)) {
    if (FLL->isExact())
      return;
  } else if (FloatingLiteral *FLR =
                 dyn_cast<FloatingLiteral>(RightExprSansParen))
    if (FLR->isExact())
      return;

  if (CallExpr *CL = dyn_cast<CallExpr>(LeftExprSansParen))
    if (CL->getBuiltinCallee())
      return;

  if (CallExpr *CR = dyn_cast<CallExpr>(RightExprSansParen))
    if (CR->getBuiltinCallee())
      return;

  Diag(Loc, diag::warn_floatingpoint_eq)
      << LHS->getSourceRange() << RHS->getSourceRange();
}

//===--- CHECK: Integer mixed-sign comparisons (-Wsign-compare) --------===//
//===--- CHECK: Lossy implicit conversions (-Wconversion) --------------===//

namespace {

struct IntRange {
  unsigned Width;

  bool NonNegative;

  IntRange(unsigned Width, bool NonNegative)
      : Width(Width), NonNegative(NonNegative) {}

  unsigned valueBits() const { return NonNegative ? Width : Width - 1; }

  static IntRange forBoolType() { return IntRange(1, true); }

  static IntRange forValueOfType(TreeContext &C, QualType T) {
    return forValueOfCanonicalType(C,
                                   T->getCanonicalTypeInternal().getTypePtr());
  }

  static IntRange forValueOfCanonicalType(TreeContext &C, const Type *T) {
    assert(T->isCanonicalUnqualified());

    if (const VectorType *VT = dyn_cast<VectorType>(T))
      T = VT->getElementType().getTypePtr();
    if (const ComplexType *CT = dyn_cast<ComplexType>(T))
      T = CT->getElementType().getTypePtr();
    if (const AtomicType *AT = dyn_cast<AtomicType>(T))
      T = AT->getValueType().getTypePtr();

    {
      // For enum types in C code, use the underlying datatype.
      if (const EnumType *ET = dyn_cast<EnumType>(T))
        T = ET->getDecl()->getIntegerType().getDesugaredType(C).getTypePtr();
    }

    if (const auto *EIT = dyn_cast<BitIntType>(T))
      return IntRange(EIT->getNumBits(), EIT->isUnsigned());

    const BuiltinType *BT = cast<BuiltinType>(T);
    assert(BT->isInteger());

    return IntRange(C.getIntWidth(QualType(T, 0)), BT->isUnsignedInteger());
  }

  static IntRange forTargetOfCanonicalType(TreeContext &C, const Type *T) {
    assert(T->isCanonicalUnqualified());

    if (const VectorType *VT = dyn_cast<VectorType>(T))
      T = VT->getElementType().getTypePtr();
    if (const ComplexType *CT = dyn_cast<ComplexType>(T))
      T = CT->getElementType().getTypePtr();
    if (const AtomicType *AT = dyn_cast<AtomicType>(T))
      T = AT->getValueType().getTypePtr();
    if (const EnumType *ET = dyn_cast<EnumType>(T))
      T = C.getCanonicalType(ET->getDecl()->getIntegerType()).getTypePtr();

    if (const auto *EIT = dyn_cast<BitIntType>(T))
      return IntRange(EIT->getNumBits(), EIT->isUnsigned());

    const BuiltinType *BT = cast<BuiltinType>(T);
    assert(BT->isInteger());

    return IntRange(C.getIntWidth(QualType(T, 0)), BT->isUnsignedInteger());
  }

  static IntRange join(IntRange L, IntRange R) {
    bool Unsigned = L.NonNegative && R.NonNegative;
    return IntRange(std::max(L.valueBits(), R.valueBits()) + !Unsigned,
                    L.NonNegative && R.NonNegative);
  }

  static IntRange bit_and(IntRange L, IntRange R) {
    unsigned Bits = std::max(L.Width, R.Width);
    bool NonNegative = false;
    if (L.NonNegative) {
      Bits = std::min(Bits, L.Width);
      NonNegative = true;
    }
    if (R.NonNegative) {
      Bits = std::min(Bits, R.Width);
      NonNegative = true;
    }
    return IntRange(Bits, NonNegative);
  }

  static IntRange sum(IntRange L, IntRange R) {
    bool Unsigned = L.NonNegative && R.NonNegative;
    return IntRange(std::max(L.valueBits(), R.valueBits()) + 1 + !Unsigned,
                    Unsigned);
  }

  static IntRange difference(IntRange L, IntRange R) {
    // We need a 1-bit-wider range if:
    //   1) LHS can be negative: least value can be reduced.
    //   2) RHS can be negative: greatest value can be increased.
    bool CanWiden = !L.NonNegative || !R.NonNegative;
    bool Unsigned = L.NonNegative && R.Width == 0;
    return IntRange(std::max(L.valueBits(), R.valueBits()) + CanWiden +
                        !Unsigned,
                    Unsigned);
  }

  static IntRange product(IntRange L, IntRange R) {
    // If both LHS and RHS can be negative, we can form
    //   -2^L * -2^R = 2^(L + R)
    // which requires L + R + 1 value bits to represent.
    bool CanWiden = !L.NonNegative && !R.NonNegative;
    bool Unsigned = L.NonNegative && R.NonNegative;
    return IntRange(L.valueBits() + R.valueBits() + CanWiden + !Unsigned,
                    Unsigned);
  }

  static IntRange rem(IntRange L, IntRange R) {
    // The result of a remainder can't be larger than the result of
    // either side. The sign of the result is the sign of the LHS.
    bool Unsigned = L.NonNegative;
    return IntRange(std::min(L.valueBits(), R.valueBits()) + !Unsigned,
                    Unsigned);
  }
};

} // namespace

namespace {
IntRange getValueRange(TreeContext &C, llvm::APSInt &value, unsigned MaxWidth) {
  if (value.isSigned() && value.isNegative())
    return IntRange(value.getSignificantBits(), false);

  if (value.getBitWidth() > MaxWidth)
    value = value.trunc(MaxWidth);

  // isNonNegative() just checks the sign bit without considering
  // signedness.
  return IntRange(value.getActiveBits(), true);
}

IntRange getValueRange(TreeContext &C, APValue &result, QualType Ty,
                       unsigned MaxWidth) {
  if (result.isInt())
    return getValueRange(C, result.getInt(), MaxWidth);

  if (result.isVector()) {
    IntRange R = getValueRange(C, result.getVectorElt(0), Ty, MaxWidth);
    for (unsigned i = 1, e = result.getVectorLength(); i != e; ++i) {
      IntRange El = getValueRange(C, result.getVectorElt(i), Ty, MaxWidth);
      R = IntRange::join(R, El);
    }
    return R;
  }

  if (result.isComplexInt()) {
    IntRange R = getValueRange(C, result.getComplexIntReal(), MaxWidth);
    IntRange I = getValueRange(C, result.getComplexIntImag(), MaxWidth);
    return IntRange::join(R, I);
  }

  // This can happen with lossless casts to intptr_t of "based" lvalues.
  // Assume it might use arbitrary bits.
  // The only reason we need to pass the type in here is to get
  // the sign right on this one case.  It would be nice if APValue
  // preserved this.
  assert(result.isLValue() || result.isAddrLabelDiff());
  return IntRange(MaxWidth, Ty->isUnsignedIntegerOrEnumerationType());
}
} // namespace

namespace {
QualType getExprType(const Expr *E) {
  QualType Ty = E->getType();
  if (const AtomicType *AtomicRHS = Ty->getAs<AtomicType>())
    Ty = AtomicRHS->getValueType();
  return Ty;
}
} // namespace

namespace {
IntRange getExprRange(TreeContext &C, const Expr *E, unsigned MaxWidth,
                      bool InConstantContext, bool Approximate) {
  E = E->IgnoreParens();

  // Try a full evaluation first.
  Expr::EvalResult result;
  if (E->EvaluateAsRValue(result, C, InConstantContext))
    return getValueRange(C, result.Val, getExprType(E), MaxWidth);

  // I think we only want to look through implicit casts here; if the
  // user has an explicit widening cast, we should treat the value as
  // being of the new, wider type.
  if (const auto *CE = dyn_cast<ImplicitCastExpr>(E)) {
    if (CE->getCastKind() == CK_NoOp || CE->getCastKind() == CK_LValueToRValue)
      return getExprRange(C, CE->getSubExpr(), MaxWidth, InConstantContext,
                          Approximate);

    IntRange OutputTypeRange = IntRange::forValueOfType(C, getExprType(CE));

    bool isIntegerCast = CE->getCastKind() == CK_IntegralCast ||
                         CE->getCastKind() == CK_BooleanToSignedIntegral;

    // Assume that non-integer casts can span the full range of the type.
    if (!isIntegerCast)
      return OutputTypeRange;

    IntRange SubRange = getExprRange(C, CE->getSubExpr(),
                                     std::min(MaxWidth, OutputTypeRange.Width),
                                     InConstantContext, Approximate);

    // Bail out if the subexpr's range is as wide as the cast type.
    if (SubRange.Width >= OutputTypeRange.Width)
      return OutputTypeRange;

    // Otherwise, we take the smaller width, and we're non-negative if
    // either the output type or the subexpr is.
    return IntRange(SubRange.Width,
                    SubRange.NonNegative || OutputTypeRange.NonNegative);
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    // If we can fold the condition, just take that operand.
    bool CondResult;
    if (CO->getCond()->EvaluateAsBooleanCondition(CondResult, C))
      return getExprRange(C,
                          CondResult ? CO->getTrueExpr() : CO->getFalseExpr(),
                          MaxWidth, InConstantContext, Approximate);

    // Otherwise, conservatively merge.
    // getExprRange requires an integer expression, but a throw expression
    // results in a void type.
    Expr *E = CO->getTrueExpr();
    IntRange L =
        E->getType()->isVoidType()
            ? IntRange{0, true}
            : getExprRange(C, E, MaxWidth, InConstantContext, Approximate);
    E = CO->getFalseExpr();
    IntRange R =
        E->getType()->isVoidType()
            ? IntRange{0, true}
            : getExprRange(C, E, MaxWidth, InConstantContext, Approximate);
    return IntRange::join(L, R);
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    IntRange (*Combine)(IntRange, IntRange) = IntRange::join;

    switch (BO->getOpcode()) {
    // Boolean-valued operations are single-bit and positive.
    case BO_LAnd:
    case BO_LOr:
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
      return IntRange::forBoolType();

    // The type of the assignments is the type of the LHS, so the RHS
    // is not necessarily the same type.
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_XorAssign:
    case BO_OrAssign:
      return IntRange::forValueOfType(C, getExprType(E));

    // Simple assignments just pass through the RHS, which will have
    // been coerced to the LHS type.
    case BO_Assign:
      return getExprRange(C, BO->getRHS(), MaxWidth, InConstantContext,
                          Approximate);

    // Bitwise-and uses the *infinum* of the two source ranges.
    case BO_And:
    case BO_AndAssign:
      Combine = IntRange::bit_and;
      break;

    // Left shift gets black-listed based on a judgement call.
    case BO_Shl:
      // ...except that we want to treat '1 << (blah)' as logically
      // positive.  It's an important idiom.
      if (IntegerLiteral *I =
              dyn_cast<IntegerLiteral>(BO->getLHS()->IgnoreParenCasts())) {
        if (I->getValue() == 1) {
          IntRange R = IntRange::forValueOfType(C, getExprType(E));
          return IntRange(R.Width, /*NonNegative*/ true);
        }
      }
      [[fallthrough]];

    case BO_ShlAssign:
      return IntRange::forValueOfType(C, getExprType(E));

    // Right shift by a constant can narrow its left argument.
    case BO_Shr:
    case BO_ShrAssign: {
      IntRange L = getExprRange(C, BO->getLHS(), MaxWidth, InConstantContext,
                                Approximate);

      // If the shift amount is a positive constant, drop the width by
      // that much.
      if (std::optional<llvm::APSInt> shift =
              BO->getRHS()->getIntegerConstantExpr(C)) {
        if (shift->isNonNegative()) {
          if (shift->uge(L.Width))
            L.Width = (L.NonNegative ? 0 : 1);
          else
            L.Width -= shift->getZExtValue();
        }
      }

      return L;
    }

    // Comma acts as its right operand.
    case BO_Comma:
      return getExprRange(C, BO->getRHS(), MaxWidth, InConstantContext,
                          Approximate);

    case BO_Add:
      if (!Approximate)
        Combine = IntRange::sum;
      break;

    case BO_Sub:
      if (BO->getLHS()->getType()->isPointerType())
        return IntRange::forValueOfType(C, getExprType(E));
      if (!Approximate)
        Combine = IntRange::difference;
      break;

    case BO_Mul:
      if (!Approximate)
        Combine = IntRange::product;
      break;

    // The width of a division result is mostly determined by the size
    // of the LHS.
    case BO_Div: {
      // Don't 'pre-truncate' the operands.
      unsigned opWidth = C.getIntWidth(getExprType(E));
      IntRange L = getExprRange(C, BO->getLHS(), opWidth, InConstantContext,
                                Approximate);

      // If the divisor is constant, use that.
      if (std::optional<llvm::APSInt> divisor =
              BO->getRHS()->getIntegerConstantExpr(C)) {
        unsigned log2 = divisor->logBase2(); // floor(log_2(divisor))
        if (log2 >= L.Width)
          L.Width = (L.NonNegative ? 0 : 1);
        else
          L.Width = std::min(L.Width - log2, MaxWidth);
        return L;
      }

      // Otherwise, just use the LHS's width.
      IntRange R = getExprRange(C, BO->getRHS(), opWidth, InConstantContext,
                                Approximate);
      return IntRange(L.Width, L.NonNegative && R.NonNegative);
    }

    case BO_Rem:
      Combine = IntRange::rem;
      break;

    // The default behavior is okay for these.
    case BO_Xor:
    case BO_Or:
      break;
    }

    // Combine the two ranges, but limit the result to the type in which we
    // performed the computation.
    QualType T = getExprType(E);
    unsigned opWidth = C.getIntWidth(T);
    IntRange L =
        getExprRange(C, BO->getLHS(), opWidth, InConstantContext, Approximate);
    IntRange R =
        getExprRange(C, BO->getRHS(), opWidth, InConstantContext, Approximate);
    IntRange C = Combine(L, R);
    C.NonNegative |= T->isUnsignedIntegerOrEnumerationType();
    C.Width = std::min(C.Width, MaxWidth);
    return C;
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    // Boolean-valued operations are white-listed.
    case UO_LNot:
      return IntRange::forBoolType();

    // Operations with opaque sources are black-listed.
    case UO_Deref:
    case UO_AddrOf: // should be impossible
      return IntRange::forValueOfType(C, getExprType(E));

    default:
      return getExprRange(C, UO->getSubExpr(), MaxWidth, InConstantContext,
                          Approximate);
    }
  }

  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    return getExprRange(C, OVE->getSourceExpr(), MaxWidth, InConstantContext,
                        Approximate);

  if (const auto *BitField = E->getSourceBitField())
    return IntRange(BitField->getBitWidthValue(C),
                    BitField->getType()->isUnsignedIntegerOrEnumerationType());

  return IntRange::forValueOfType(C, getExprType(E));
}

IntRange getExprRange(TreeContext &C, const Expr *E, bool InConstantContext,
                      bool Approximate) {
  return getExprRange(C, E, C.getIntWidth(getExprType(E)), InConstantContext,
                      Approximate);
}
} // namespace

namespace {
bool isSameFloatAfterCast(const llvm::APFloat &value,
                          const llvm::fltSemantics &Src,
                          const llvm::fltSemantics &Tgt) {
  llvm::APFloat truncated = value;

  bool ignored;
  truncated.convert(Src, llvm::APFloat::rmNearestTiesToEven, &ignored);
  truncated.convert(Tgt, llvm::APFloat::rmNearestTiesToEven, &ignored);

  return truncated.bitwiseIsEqual(value);
}

bool isSameFloatAfterCast(const APValue &value, const llvm::fltSemantics &Src,
                          const llvm::fltSemantics &Tgt) {
  if (value.isFloat())
    return isSameFloatAfterCast(value.getFloat(), Src, Tgt);

  if (value.isVector()) {
    for (unsigned i = 0, e = value.getVectorLength(); i != e; ++i)
      if (!isSameFloatAfterCast(value.getVectorElt(i), Src, Tgt))
        return false;
    return true;
  }

  assert(value.isComplexFloat());
  return (isSameFloatAfterCast(value.getComplexFloatReal(), Src, Tgt) &&
          isSameFloatAfterCast(value.getComplexFloatImag(), Src, Tgt));
}

void analyzeImplicitConversions(Sema &S, Expr *E, SourceLocation CC,
                                bool IsListInit = false);

bool isEnumConstOrFromMacro(Sema &S, Expr *E) {
  // Suppress cases where we are comparing against an enum constant.
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    if (isa<EnumConstantDecl>(DR->getDecl()))
      return true;

  // Suppress cases where the value is expanded from a macro, unless that macro
  // is how a language represents a boolean literal. This is the case in both C
  // and C.
  SourceLocation BeginLoc = E->getBeginLoc();
  if (BeginLoc.isMacroID()) {
    llvm::StringRef MacroName = SourceScanner::getImmediateMacroName(
        BeginLoc, S.getSourceManager(), S.getLangOpts());
    return MacroName != "YES" && MacroName != "NO" && MacroName != "true" &&
           MacroName != "false";
  }

  return false;
}

bool isKnownToHaveUnsignedValue(Expr *E) {
  return E->getType()->isIntegerType() &&
         (!E->getType()->isSignedIntegerType() ||
          !E->IgnoreParenImpCasts()->getType()->isSignedIntegerType());
}
} // namespace

namespace {
struct PromotedRange {
  // Min, or HoleMax if there is a hole.
  llvm::APSInt PromotedMin;
  // Max, or HoleMin if there is a hole.
  llvm::APSInt PromotedMax;

  PromotedRange(IntRange R, unsigned BitWidth, bool Unsigned) {
    if (R.Width == 0)
      PromotedMin = PromotedMax = llvm::APSInt(BitWidth, Unsigned);
    else if (R.Width >= BitWidth && !Unsigned) {
      // Promotion made the type *narrower*. This happens when promoting
      // a < 32-bit unsigned / <= 32-bit signed bit-field to 'signed int'.
      // Treat all values of 'signed int' as being in range for now.
      PromotedMin = llvm::APSInt::getMinValue(BitWidth, Unsigned);
      PromotedMax = llvm::APSInt::getMaxValue(BitWidth, Unsigned);
    } else {
      PromotedMin = llvm::APSInt::getMinValue(R.Width, R.NonNegative)
                        .extOrTrunc(BitWidth);
      PromotedMin.setIsUnsigned(Unsigned);

      PromotedMax = llvm::APSInt::getMaxValue(R.Width, R.NonNegative)
                        .extOrTrunc(BitWidth);
      PromotedMax.setIsUnsigned(Unsigned);
    }
  }

  // Determine whether this range is contiguous (has no hole).
  bool isContiguous() const { return PromotedMin <= PromotedMax; }

  // Where a constant value is within the range.
  enum ComparisonResult {
    LT = 0x1,
    LE = 0x2,
    GT = 0x4,
    GE = 0x8,
    EQ = 0x10,
    NE = 0x20,
    InRangeFlag = 0x40,

    Less = LE | LT | NE,
    Min = LE | InRangeFlag,
    InRange = InRangeFlag,
    Max = GE | InRangeFlag,
    Greater = GE | GT | NE,

    OnlyValue = LE | GE | EQ | InRangeFlag,
    InHole = NE
  };

  ComparisonResult compare(const llvm::APSInt &Value) const {
    assert(Value.getBitWidth() == PromotedMin.getBitWidth() &&
           Value.isUnsigned() == PromotedMin.isUnsigned());
    if (!isContiguous()) {
      assert(Value.isUnsigned() && "discontiguous range for signed compare");
      if (Value.isMinValue())
        return Min;
      if (Value.isMaxValue())
        return Max;
      if (Value >= PromotedMin)
        return InRange;
      if (Value <= PromotedMax)
        return InRange;
      return InHole;
    }

    switch (llvm::APSInt::compareValues(Value, PromotedMin)) {
    case -1:
      return Less;
    case 0:
      return PromotedMin == PromotedMax ? OnlyValue : Min;
    case 1:
      switch (llvm::APSInt::compareValues(Value, PromotedMax)) {
      case -1:
        return InRange;
      case 0:
        return Max;
      case 1:
        return Greater;
      }
    }

    llvm_unreachable("impossible compare result");
  }

  static std::optional<llvm::StringRef>
  constantValue(BinaryOperatorKind Op, ComparisonResult R, bool ConstantOnRHS) {
    ComparisonResult TrueFlag, FalseFlag;
    if (Op == BO_EQ) {
      TrueFlag = EQ;
      FalseFlag = NE;
    } else if (Op == BO_NE) {
      TrueFlag = NE;
      FalseFlag = EQ;
    } else {
      if ((Op == BO_LT || Op == BO_GE) ^ ConstantOnRHS) {
        TrueFlag = LT;
        FalseFlag = GE;
      } else {
        TrueFlag = GT;
        FalseFlag = LE;
      }
      if (Op == BO_GE || Op == BO_LE)
        std::swap(TrueFlag, FalseFlag);
    }
    if (R & TrueFlag)
      return llvm::StringRef("true");
    if (R & FalseFlag)
      return llvm::StringRef("false");
    return std::nullopt;
  }
};
} // namespace

namespace {
bool hasEnumType(Expr *E) {
  // Strip off implicit integral promotions.
  while (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() != CK_IntegralCast && ICE->getCastKind() != CK_NoOp)
      break;
    E = ICE->getSubExpr();
  }

  return E->getType()->isEnumeralType();
}

int classifyConstantValue(Expr *Constant) {
  // The values of this enumeration are used in the diagnostics
  // diag::warn_out_of_range_compare and diag::warn_tautological_bool_compare.
  enum ConstantValueKind { Miscellaneous = 0, LiteralTrue, LiteralFalse };
  return ConstantValueKind::Miscellaneous;
}

bool checkTautologicalComparison(Sema &S, BinaryOperator *E, Expr *Constant,
                                 Expr *Other, const llvm::APSInt &Value,
                                 bool RhsConstant) {
  Expr *OriginalOther = Other;

  Constant = Constant->IgnoreParenImpCasts();
  Other = Other->IgnoreParenImpCasts();

  // Suppress warnings on tautological comparisons between values of the same
  // enumeration type. There are only two ways we could warn on this:
  //  - If the constant is outside the range of representable values of
  //    the enumeration. In such a case, we should warn about the cast
  //    to enumeration type, not about the comparison.
  //  - If the constant is the maximum / minimum in-range value. For an
  //    enumeratin type, such comparisons can be meaningful and useful.
  if (Constant->getType()->isEnumeralType() &&
      S.Context.hasSameUnqualifiedType(Constant->getType(), Other->getType()))
    return false;

  IntRange OtherValueRange = getExprRange(
      S.Context, Other, S.isConstantEvaluatedContext(), /*Approximate=*/false);

  QualType OtherT = Other->getType();
  if (const auto *AT = OtherT->getAs<AtomicType>())
    OtherT = AT->getValueType();
  IntRange OtherTypeRange = IntRange::forValueOfType(S.Context, OtherT);

  // Whether we're treating Other as being a bool because of the form of
  // expression despite it having another type (typically 'int' in C).
  bool OtherIsBooleanDespiteType =
      !OtherT->isBooleanType() && Other->isKnownToHaveBooleanValue();
  if (OtherIsBooleanDespiteType)
    OtherTypeRange = OtherValueRange = IntRange::forBoolType();

  // Check if all values in the range of possible values of this expression
  // lead to the same comparison outcome.
  PromotedRange OtherPromotedValueRange(OtherValueRange, Value.getBitWidth(),
                                        Value.isUnsigned());
  auto Cmp = OtherPromotedValueRange.compare(Value);
  auto Result = PromotedRange::constantValue(E->getOpcode(), Cmp, RhsConstant);
  if (!Result)
    return false;

  // Also consider the range determined by the type alone. This allows us to
  // classify the warning under the proper diagnostic group.
  bool TautologicalTypeCompare = false;
  {
    PromotedRange OtherPromotedTypeRange(OtherTypeRange, Value.getBitWidth(),
                                         Value.isUnsigned());
    auto TypeCmp = OtherPromotedTypeRange.compare(Value);
    if (auto TypeResult = PromotedRange::constantValue(E->getOpcode(), TypeCmp,
                                                       RhsConstant)) {
      TautologicalTypeCompare = true;
      Cmp = TypeCmp;
      Result = TypeResult;
    }
  }

  // Don't warn if the non-constant operand actually always evaluates to the
  // same value.
  if (!TautologicalTypeCompare && OtherValueRange.Width == 0)
    return false;

  // Suppress the diagnostic for an in-range comparison if the constant comes
  // from a macro or enumerator. We don't want to diagnose
  //
  //   some_long_value <= INT_MAX
  //
  // when sizeof(int) == sizeof(long).
  bool InRange = Cmp & PromotedRange::InRangeFlag;
  if (InRange && isEnumConstOrFromMacro(S, Constant))
    return false;

  // A comparison of an unsigned bit-field against 0 is really a type problem,
  // even though at the type level the bit-field might promote to 'signed int'.
  if (Other->refersToBitField() && InRange && Value == 0 &&
      Other->getType()->isUnsignedIntegerOrEnumerationType())
    TautologicalTypeCompare = true;

  // If this is a comparison to an enum constant, include that
  // constant in the diagnostic.
  const EnumConstantDecl *ED = nullptr;
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Constant))
    ED = dyn_cast<EnumConstantDecl>(DR->getDecl());

  // Should be enough for uint128 (39 decimal digits)
  llvm::SmallString<64> PrettySourceValue;
  llvm::raw_svector_ostream OS(PrettySourceValue);
  if (ED) {
    OS << '\'' << *ED << "' (" << Value << ")";
  } else {
    OS << Value;
  }

  if (!TautologicalTypeCompare) {
    S.Diag(E->getOperatorLoc(), diag::warn_tautological_compare_value_range)
        << RhsConstant << OtherValueRange.Width << OtherValueRange.NonNegative
        << E->getOpcodeStr() << OS.str() << *Result
        << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange();
    return true;
  }

  // We use a somewhat different formatting for the in-range cases and
  // cases involving boolean values for historical reasons. We should pick a
  // consistent way of presenting these diagnostics.
  if (!InRange || Other->isKnownToHaveBooleanValue()) {

    S.DiagRuntimeBehavior(
        E->getOperatorLoc(), E,
        S.PDiag(!InRange ? diag::warn_out_of_range_compare
                         : diag::warn_tautological_bool_compare)
            << OS.str() << classifyConstantValue(Constant) << OtherT
            << OtherIsBooleanDespiteType << *Result
            << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange());
  } else {
    bool IsCharTy = OtherT.withoutLocalFastQualifiers() == S.Context.CharTy;
    unsigned Diag =
        (isKnownToHaveUnsignedValue(OriginalOther) && Value == 0)
            ? (hasEnumType(OriginalOther)
                   ? diag::warn_unsigned_enum_always_true_comparison
               : IsCharTy ? diag::warn_unsigned_char_always_true_comparison
                          : diag::warn_unsigned_always_true_comparison)
            : diag::warn_tautological_constant_compare;

    S.Diag(E->getOperatorLoc(), Diag)
        << RhsConstant << OtherT << E->getOpcodeStr() << OS.str() << *Result
        << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange();
  }

  return true;
}

void analyzeImpConvsInComparison(Sema &S, BinaryOperator *E) {
  analyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());
  analyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());
}

void analyzeComparison(Sema &S, BinaryOperator *E) {
  // The type the comparison is being performed in.
  QualType T = E->getLHS()->getType();

  // Only analyze comparison operators where both sides have been converted to
  // the same type.
  if (!S.Context.hasSameUnqualifiedType(T, E->getRHS()->getType()))
    return analyzeImpConvsInComparison(S, E);

  Expr *LHS = E->getLHS();
  Expr *RHS = E->getRHS();

  if (T->isIntegralType(S.Context)) {
    std::optional<llvm::APSInt> RHSValue =
        RHS->getIntegerConstantExpr(S.Context);
    std::optional<llvm::APSInt> LHSValue =
        LHS->getIntegerConstantExpr(S.Context);

    // We don't care about expressions whose result is a constant.
    if (RHSValue && LHSValue)
      return analyzeImpConvsInComparison(S, E);

    // We only care about expressions where just one side is literal
    if ((bool)RHSValue ^ (bool)LHSValue) {
      // Is the constant on the RHS or LHS?
      const bool RhsConstant = (bool)RHSValue;
      Expr *Const = RhsConstant ? RHS : LHS;
      Expr *Other = RhsConstant ? LHS : RHS;
      const llvm::APSInt &Value = RhsConstant ? *RHSValue : *LHSValue;

      if (checkTautologicalComparison(S, E, Const, Other, Value, RhsConstant))
        return analyzeImpConvsInComparison(S, E);
    }
  }

  if (!T->hasUnsignedIntegerRepresentation()) {
    // We don't do anything special if this isn't an unsigned integral
    // comparison:  we're only interested in integral comparisons, and
    // signed comparisons only happen in cases we don't care to warn about.
    return analyzeImpConvsInComparison(S, E);
  }

  LHS = LHS->IgnoreParenImpCasts();
  RHS = RHS->IgnoreParenImpCasts();

  // Avoid warning about comparison of integers with different signs when
  // RHS/LHS has a `typeof(E)` type whose sign is different from the sign of
  // the type of `E`.
  if (const auto *TET = dyn_cast<TypeOfExprType>(LHS->getType()))
    LHS = TET->getUnderlyingExpr()->IgnoreParenImpCasts();
  if (const auto *TET = dyn_cast<TypeOfExprType>(RHS->getType()))
    RHS = TET->getUnderlyingExpr()->IgnoreParenImpCasts();

  Expr *signedOperand, *unsignedOperand;
  if (LHS->getType()->hasSignedIntegerRepresentation()) {
    assert(!RHS->getType()->hasSignedIntegerRepresentation() &&
           "unsigned comparison between two signed integer expressions?");
    signedOperand = LHS;
    unsignedOperand = RHS;
  } else if (RHS->getType()->hasSignedIntegerRepresentation()) {
    signedOperand = RHS;
    unsignedOperand = LHS;
  } else {
    return analyzeImpConvsInComparison(S, E);
  }

  // Otherwise, calculate the effective range of the signed operand.
  IntRange signedRange =
      getExprRange(S.Context, signedOperand, S.isConstantEvaluatedContext(),
                   /*Approximate=*/true);

  // Go ahead and analyze implicit conversions in the operands.  Note
  // that we skip the implicit conversions on both sides.
  analyzeImplicitConversions(S, LHS, E->getOperatorLoc());
  analyzeImplicitConversions(S, RHS, E->getOperatorLoc());

  // If the signed range is non-negative, -Wsign-compare won't fire.
  if (signedRange.NonNegative)
    return;

  // For (in)equality comparisons, if the unsigned operand is a
  // constant which cannot collide with a overflowed signed operand,
  // then reinterpreting the signed operand as unsigned will not
  // change the result of the comparison.
  if (E->isEqualityOp()) {
    unsigned comparisonWidth = S.Context.getIntWidth(T);
    IntRange unsignedRange =
        getExprRange(S.Context, unsignedOperand, S.isConstantEvaluatedContext(),
                     /*Approximate=*/true);

    // We should never be unable to prove that the unsigned operand is
    // non-negative.
    assert(unsignedRange.NonNegative && "unsigned range includes negative?");

    if (unsignedRange.Width < comparisonWidth)
      return;
  }

  S.DiagRuntimeBehavior(E->getOperatorLoc(), E,
                        S.PDiag(diag::warn_mixed_sign_comparison)
                            << LHS->getType() << RHS->getType()
                            << LHS->getSourceRange() << RHS->getSourceRange());
}
} // namespace

bool AnalyzeBitFieldAssignment(Sema &S, FieldDecl *Bitfield, Expr *Init,
                               SourceLocation InitLoc) {
  assert(Bitfield->isBitField());
  if (Bitfield->isInvalidDecl())
    return false;

  // White-list bool bitfields.
  QualType BitfieldType = Bitfield->getType();
  if (BitfieldType->isBooleanType())
    return false;

  Expr *OriginalInit = Init->IgnoreParenImpCasts();
  unsigned FieldWidth = Bitfield->getBitWidthValue(S.Context);

  Expr::EvalResult Result;
  if (!OriginalInit->EvaluateAsInt(Result, S.Context,
                                   Expr::SE_AllowSideEffects)) {
    // The RHS is not constant.  If the RHS has an enum type, make sure the
    // bitfield is wide enough to hold all the values of the enum without
    // truncation.
    if (const auto *EnumTy = OriginalInit->getType()->getAs<EnumType>()) {
      EnumDecl *ED = EnumTy->getDecl();
      bool SignedBitfield = BitfieldType->isSignedIntegerType();

      // Enum types are implicitly signed on Windows, so check if there are any
      // negative enumerators to see if the enum was intended to be signed or
      // not.
      bool SignedEnum = ED->getNumNegativeBits() > 0;

      // Check for surprising sign changes when assigning enum values to a
      // bitfield of different signedness.  If the bitfield is signed and we
      // have exactly the right number of bits to store this unsigned enum,
      // suggest changing the enum to an unsigned type. This typically happens
      // on Windows where unfixed enums always use an underlying type of 'int'.
      unsigned DiagID = 0;
      if (SignedEnum && !SignedBitfield) {
        DiagID = diag::warn_unsigned_bitfield_assigned_signed_enum;
      } else if (SignedBitfield && !SignedEnum &&
                 ED->getNumPositiveBits() == FieldWidth) {
        DiagID = diag::warn_signed_bitfield_enum_conversion;
      }

      if (DiagID) {
        S.Diag(InitLoc, DiagID) << Bitfield << ED;
        TypeSourceInfo *TSI = Bitfield->getTypeSourceInfo();
        SourceRange TypeRange =
            TSI ? TSI->getTypeLoc().getSourceRange() : SourceRange();
        S.Diag(Bitfield->getTypeSpecStartLoc(), diag::note_change_bitfield_sign)
            << SignedEnum << TypeRange;
      }

      // Compute the required bitwidth. If the enum has negative values, we need
      // one more bit than the normal number of positive bits to represent the
      // sign bit.
      unsigned BitsNeeded = SignedEnum ? std::max(ED->getNumPositiveBits() + 1,
                                                  ED->getNumNegativeBits())
                                       : ED->getNumPositiveBits();

      if (BitsNeeded > FieldWidth) {
        Expr *WidthExpr = Bitfield->getBitWidth();
        S.Diag(InitLoc, diag::warn_bitfield_too_small_for_enum)
            << Bitfield << ED;
        S.Diag(WidthExpr->getExprLoc(), diag::note_widen_bitfield)
            << BitsNeeded << ED << WidthExpr->getSourceRange();
      }
    }

    return false;
  }

  llvm::APSInt Value = Result.Val.getInt();

  unsigned OriginalWidth = Value.getBitWidth();

  // In C, the macro 'true' from stdbool.h will evaluate to '1'; To reduce
  // false positives where the user is demonstrating they intend to use the
  // bit-field as a Boolean, check to see if the value is 1 and we're assigning
  // to a one-bit bit-field to see if the value came from a macro named 'true'.
  bool OneAssignedToOneBitBitfield = FieldWidth == 1 && Value == 1;
  if (OneAssignedToOneBitBitfield) {
    SourceLocation MaybeMacroLoc = OriginalInit->getBeginLoc();
    if (S.SourceMgr.isInSystemMacro(MaybeMacroLoc) &&
        S.locateMacroSpelling(MaybeMacroLoc, "true"))
      return false;
  }

  if (!Value.isSigned() || Value.isNegative())
    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(OriginalInit))
      if (UO->getOpcode() == UO_Minus || UO->getOpcode() == UO_Not)
        OriginalWidth = Value.getSignificantBits();

  if (OriginalWidth <= FieldWidth)
    return false;

  llvm::APSInt TruncatedValue = Value.trunc(FieldWidth);
  TruncatedValue.setIsSigned(BitfieldType->isSignedIntegerType());

  TruncatedValue = TruncatedValue.extend(OriginalWidth);
  if (llvm::APSInt::isSameValue(Value, TruncatedValue))
    return false;

  std::string PrettyValue = toString(Value, 10);
  std::string PrettyTrunc = toString(TruncatedValue, 10);

  S.Diag(InitLoc, OneAssignedToOneBitBitfield
                      ? diag::warn_impcast_single_bit_bitield_precision_constant
                      : diag::warn_impcast_bitfield_precision_constant)
      << PrettyValue << PrettyTrunc << OriginalInit->getType()
      << Init->getSourceRange();

  return true;
}

namespace {
void analyzeAssignment(Sema &S, BinaryOperator *E) {
  // Just recurse on the LHS.
  analyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());

  // We want to recurse on the RHS as normal unless we're assigning to
  // a bitfield.
  if (FieldDecl *Bitfield = E->getLHS()->getSourceBitField()) {
    if (AnalyzeBitFieldAssignment(S, Bitfield, E->getRHS(),
                                  E->getOperatorLoc())) {
      // Recurse, ignoring any implicit conversions on the RHS.
      return analyzeImplicitConversions(S, E->getRHS()->IgnoreParenImpCasts(),
                                        E->getOperatorLoc());
    }
  }

  analyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());

  // Diagnose implicitly sequentially-consistent atomic assignment.
  if (E->getLHS()->getType()->isAtomicType())
    S.Diag(E->getRHS()->getBeginLoc(), diag::warn_atomic_implicit_seq_cst);
}

// ===----------------------------------------------------------------------===
// Implicit conversion diagnostics
// ===----------------------------------------------------------------------===

void diagnoseImpCast(Sema &S, Expr *E, QualType SourceType, QualType T,
                     SourceLocation CContext, unsigned diag,
                     bool pruneControlFlow = false) {
  if (pruneControlFlow) {
    S.DiagRuntimeBehavior(E->getExprLoc(), E,
                          S.PDiag(diag)
                              << SourceType << T << E->getSourceRange()
                              << SourceRange(CContext));
    return;
  }
  S.Diag(E->getExprLoc(), diag)
      << SourceType << T << E->getSourceRange() << SourceRange(CContext);
}

void diagnoseImpCast(Sema &S, Expr *E, QualType T, SourceLocation CContext,
                     unsigned diag, bool pruneControlFlow = false) {
  diagnoseImpCast(S, E, E->getType(), T, CContext, diag, pruneControlFlow);
}

void diagnoseFloatingImpCast(Sema &S, Expr *E, QualType T,
                             SourceLocation CContext) {
  const bool IsBool = T->isSpecificBuiltinType(BuiltinType::Bool);
  const bool PruneWarnings = false;

  Expr *InnerE = E->IgnoreParenImpCasts();
  // We also want to warn on, e.g., "int i = -1.234"
  if (UnaryOperator *UOp = dyn_cast<UnaryOperator>(InnerE))
    if (UOp->getOpcode() == UO_Minus || UOp->getOpcode() == UO_Plus)
      InnerE = UOp->getSubExpr()->IgnoreParenImpCasts();

  const bool IsLiteral =
      isa<FloatingLiteral>(E) || isa<FloatingLiteral>(InnerE);

  llvm::APFloat Value(0.0);
  bool IsConstant =
      E->EvaluateAsFloat(Value, S.Context, Expr::SE_AllowSideEffects);
  if (!IsConstant) {
    return diagnoseImpCast(S, E, T, CContext, diag::warn_impcast_float_integer,
                           PruneWarnings);
  }

  bool isExact = false;

  llvm::APSInt IntegerValue(S.Context.getIntWidth(T),
                            T->hasUnsignedIntegerRepresentation());
  llvm::APFloat::opStatus Result = Value.convertToInteger(
      IntegerValue, llvm::APFloat::rmTowardZero, &isExact);

  // Force the precision of the source value down so we don't print
  // digits which are usually useless (we don't really care here if we
  // truncate a digit by accident in edge cases).  Ideally, APFloat::toString
  // would automatically print the shortest representation, but it's a bit
  // tricky to implement.
  llvm::SmallString<16> PrettySourceValue;
  unsigned precision = llvm::APFloat::semanticsPrecision(Value.getSemantics());
  precision = (precision * 59 + 195) / 196;
  Value.toString(PrettySourceValue, precision);

  if (Result == llvm::APFloat::opOK && isExact) {
    if (IsLiteral)
      return;
    return diagnoseImpCast(S, E, T, CContext, diag::warn_impcast_float_integer,
                           PruneWarnings);
  }

  // Conversion of a floating-point value to a non-bool integer where the
  // integral part cannot be represented by the integer type is undefined.
  if (!IsBool && Result == llvm::APFloat::opInvalidOp)
    return diagnoseImpCast(
        S, E, T, CContext,
        IsLiteral ? diag::warn_impcast_literal_float_to_integer_out_of_range
                  : diag::warn_impcast_float_to_integer_out_of_range,
        PruneWarnings);

  unsigned DiagID = 0;
  if (IsLiteral) {
    // Warn on floating point literal to integer.
    DiagID = diag::warn_impcast_literal_float_to_integer;
  } else if (IntegerValue == 0) {
    if (Value.isZero()) { // Skip -0.0 to 0 conversion.
      return diagnoseImpCast(S, E, T, CContext,
                             diag::warn_impcast_float_integer, PruneWarnings);
    }
    // Warn on non-zero to zero conversion.
    DiagID = diag::warn_impcast_float_to_integer_zero;
  } else {
    if (IntegerValue.isUnsigned()) {
      if (!IntegerValue.isMaxValue()) {
        return diagnoseImpCast(S, E, T, CContext,
                               diag::warn_impcast_float_integer, PruneWarnings);
      }
    } else { // IntegerValue.isSigned()
      if (!IntegerValue.isMaxSignedValue() &&
          !IntegerValue.isMinSignedValue()) {
        return diagnoseImpCast(S, E, T, CContext,
                               diag::warn_impcast_float_integer, PruneWarnings);
      }
    }
    // Warn on evaluatable floating point expression to integer conversion.
    DiagID = diag::warn_impcast_float_to_integer;
  }

  llvm::SmallString<16> PrettyTargetValue;
  if (IsBool)
    PrettyTargetValue = Value.isZero() ? "false" : "true";
  else
    IntegerValue.toString(PrettyTargetValue);

  if (PruneWarnings) {
    S.DiagRuntimeBehavior(E->getExprLoc(), E,
                          S.PDiag(DiagID)
                              << E->getType() << T.getUnqualifiedType()
                              << PrettySourceValue << PrettyTargetValue
                              << E->getSourceRange() << SourceRange(CContext));
  } else {
    S.Diag(E->getExprLoc(), DiagID)
        << E->getType() << T.getUnqualifiedType() << PrettySourceValue
        << PrettyTargetValue << E->getSourceRange() << SourceRange(CContext);
  }
}

void analyzeCompoundAssignment(Sema &S, BinaryOperator *E) {
  assert(isa<CompoundAssignOperator>(E) &&
         "Must be compound assignment operation");
  // Recurse on the LHS and RHS in here
  analyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());
  analyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());

  if (E->getLHS()->getType()->isAtomicType())
    S.Diag(E->getOperatorLoc(), diag::warn_atomic_implicit_seq_cst);

  // Now check the outermost expression
  const auto *ResultBT = E->getLHS()->getType()->getAs<BuiltinType>();
  const auto *RBT = cast<CompoundAssignOperator>(E)
                        ->getComputationResultType()
                        ->getAs<BuiltinType>();

  // The below checks assume source is floating point.
  if (!ResultBT || !RBT || !RBT->isFloatingPoint())
    return;

  // If source is floating point but target is an integer.
  if (ResultBT->isInteger())
    return diagnoseImpCast(S, E, E->getRHS()->getType(), E->getLHS()->getType(),
                           E->getExprLoc(), diag::warn_impcast_float_integer);

  if (!ResultBT->isFloatingPoint())
    return;

  // If both source and target are floating points, warn about losing precision.
  int Order = S.getTreeContext().getFloatingTypeSemanticOrder(
      QualType(ResultBT, 0), QualType(RBT, 0));
  if (Order < 0 && !S.SourceMgr.isInSystemMacro(E->getOperatorLoc()))
    // warn about dropping FP rank.
    diagnoseImpCast(S, E->getRHS(), E->getLHS()->getType(), E->getOperatorLoc(),
                    diag::warn_impcast_float_result_precision);
}

std::string prettyPrintInRange(const llvm::APSInt &Value, IntRange Range) {
  if (!Range.Width)
    return "0";

  llvm::APSInt ValueInRange = Value;
  ValueInRange.setIsSigned(!Range.NonNegative);
  ValueInRange = ValueInRange.trunc(Range.Width);
  return toString(ValueInRange, 10);
}

bool isImplicitBoolFloatConversion(Sema &S, Expr *Ex, bool ToBool) {
  if (!isa<ImplicitCastExpr>(Ex))
    return false;

  Expr *InnerE = Ex->IgnoreParenImpCasts();
  const Type *Target = S.Context.getCanonicalType(Ex->getType()).getTypePtr();
  const Type *Source =
      S.Context.getCanonicalType(InnerE->getType()).getTypePtr();

  const BuiltinType *FloatCandidateBT =
      dyn_cast<BuiltinType>(ToBool ? Source : Target);
  const Type *BoolCandidateType = ToBool ? Target : Source;

  return (BoolCandidateType->isSpecificBuiltinType(BuiltinType::Bool) &&
          FloatCandidateBT && (FloatCandidateBT->isFloatingPoint()));
}

void checkImplicitArgumentConversions(Sema &S, CallExpr *TheCall,
                                      SourceLocation CC) {
  unsigned NumArgs = TheCall->getNumArgs();
  for (unsigned i = 0; i < NumArgs; ++i) {
    Expr *CurrA = TheCall->getArg(i);
    if (!isImplicitBoolFloatConversion(S, CurrA, true))
      continue;

    bool IsSwapped = ((i > 0) && isImplicitBoolFloatConversion(
                                     S, TheCall->getArg(i - 1), false));
    IsSwapped |= ((i < (NumArgs - 1)) && isImplicitBoolFloatConversion(
                                             S, TheCall->getArg(i + 1), false));
    if (IsSwapped) {
      // Warn on this floating-point to bool conversion.
      diagnoseImpCast(S, CurrA->IgnoreParenImpCasts(), CurrA->getType(), CC,
                      diag::warn_impcast_floating_point_to_bool);
    }
  }
}

void diagnoseNullConversion(Sema &S, Expr *E, QualType T, SourceLocation CC) {
  if (S.Diags.isIgnored(diag::warn_impcast_null_pointer_to_integer,
                        E->getExprLoc()))
    return;

  // Don't warn on functions which have return type nullptr_t.
  if (isa<CallExpr>(E))
    return;

  const Expr *NewE = E->IgnoreParenImpCasts();
  bool HasNullPtrType = NewE->getType()->isNullPtrType();
  if (!HasNullPtrType)
    return;

  if (T->isAnyPointerType() || !T->isScalarType() || T->isNullPtrType())
    return;

  SourceLocation Loc = E->getSourceRange().getBegin();

  // Venture through the macro stacks to get to the source of macro arguments.
  // The new location is a better location than the complete location that was
  // passed in.
  Loc = S.SourceMgr.getTopMacroCallerLoc(Loc);
  CC = S.SourceMgr.getTopMacroCallerLoc(CC);

  // Only warn if the null and context location are in the same macro expansion.
  if (S.SourceMgr.getFileID(Loc) != S.SourceMgr.getFileID(CC))
    return;

  S.Diag(Loc, diag::warn_impcast_null_pointer_to_integer)
      << HasNullPtrType << T << SourceRange(CC)
      << FixItHint::CreateReplacement(Loc,
                                      S.getFixItZeroLiteralForType(T, Loc));
}
} // namespace

// Helper function to filter out cases for constant width constant conversion.
// Don't warn on char array initialization or for non-decimal values.
namespace {
bool isSameWidthConstantConversion(Sema &S, Expr *E, QualType T,
                                   SourceLocation CC) {
  // If initializing from a constant, and the constant starts with '0',
  // then it is a binary, octal, or hexadecimal.  Allow these constants
  // to fill all the bits, even if there is a sign change.
  if (auto *IntLit = dyn_cast<IntegerLiteral>(E->IgnoreParenImpCasts())) {
    const char FirstLiteralCharacter =
        S.getSourceManager().getCharacterData(IntLit->getBeginLoc())[0];
    if (FirstLiteralCharacter == '0')
      return false;
  }

  // If the CC location points to a '{', and the type is char, then assume
  // assume it is an array initialization.
  if (CC.isValid() && T->isCharType()) {
    const char FirstContextCharacter =
        S.getSourceManager().getCharacterData(CC)[0];
    if (FirstContextCharacter == '{')
      return false;
  }

  return true;
}

const IntegerLiteral *getIntegerLiteral(Expr *E) {
  const auto *IL = dyn_cast<IntegerLiteral>(E);
  if (!IL) {
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() == UO_Minus)
        return dyn_cast<IntegerLiteral>(UO->getSubExpr());
    }
  }

  return IL;
}

void diagnoseIntInBoolContext(Sema &S, Expr *E) {
  E = E->IgnoreParenImpCasts();
  SourceLocation ExprLoc = E->getExprLoc();

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    BinaryOperator::Opcode Opc = BO->getOpcode();
    Expr::EvalResult Result;
    // Do not diagnose unsigned shifts.
    if (Opc == BO_Shl) {
      const auto *LHS = getIntegerLiteral(BO->getLHS());
      const auto *RHS = getIntegerLiteral(BO->getRHS());
      if (LHS && LHS->getValue() == 0)
        S.Diag(ExprLoc, diag::warn_left_shift_always) << 0;
      else if (LHS && RHS && RHS->getValue().isNonNegative() &&
               E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects))
        S.Diag(ExprLoc, diag::warn_left_shift_always)
            << (Result.Val.getInt() != 0);
      else if (E->getType()->isSignedIntegerType())
        S.Diag(ExprLoc, diag::warn_left_shift_in_bool_context) << E;
    }
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    const auto *LHS = getIntegerLiteral(CO->getTrueExpr());
    const auto *RHS = getIntegerLiteral(CO->getFalseExpr());
    if (!LHS || !RHS)
      return;
    if ((LHS->getValue() == 0 || LHS->getValue() == 1) &&
        (RHS->getValue() == 0 || RHS->getValue() == 1))
      // Do not diagnose common idioms.
      return;
    if (LHS->getValue() != 0 && RHS->getValue() != 0)
      S.Diag(ExprLoc, diag::warn_integer_constants_in_conditional_always_true);
  }
}

void checkImplicitConversion(Sema &S, Expr *E, QualType T, SourceLocation CC,
                             bool *ICContext = nullptr,
                             bool IsListInit = false) {
  const Type *Source = S.Context.getCanonicalType(E->getType()).getTypePtr();
  const Type *Target = S.Context.getCanonicalType(T).getTypePtr();
  if (Source == Target)
    return;

  // If the conversion context location is invalid don't complain. We also
  // don't want to emit a warning if the issue occurs from the expansion of
  // a system macro. The problem is that 'getSpellingLoc()' is slow, so we
  // delay this check as long as possible. Once we detect we are in that
  // scenario, we just return.
  if (CC.isInvalid())
    return;

  if (Source->isAtomicType())
    S.Diag(E->getExprLoc(), diag::warn_atomic_implicit_seq_cst);

  // Diagnose implicit casts to bool.
  if (Target->isSpecificBuiltinType(BuiltinType::Bool)) {
    if (isa<StringLiteral>(E))
      // Warn on string literal to bool.  Checks for string literals in logical
      // and expressions, for instance, assert(0 && "error here"), are
      // prevented by a check in analyzeImplicitConversions().
      return diagnoseImpCast(S, E, T, CC,
                             diag::warn_impcast_string_literal_to_bool);
    if (Source->isPointerType() || Source->canDecayToPointerType()) {
      // Warn on pointer to bool conversion that is always true.
      S.DiagnoseAlwaysNonNullPointer(E, Expr::NPCK_NotNull, /*IsEqual*/ false,
                                     SourceRange(CC));
    }
  }

  // Strip vector types.
  if (isa<VectorType>(Source)) {
    if (Target->isSveVLSBuiltinType() &&
        (S.Context.areCompatibleSveTypes(QualType(Target, 0),
                                         QualType(Source, 0)) ||
         S.Context.areLaxCompatibleSveTypes(QualType(Target, 0),
                                            QualType(Source, 0))))
      return;

    if (!isa<VectorType>(Target)) {
      if (S.SourceMgr.isInSystemMacro(CC))
        return;
      return diagnoseImpCast(S, E, T, CC, diag::warn_impcast_vector_scalar);
    }

    // If the vector cast is cast between two vectors of the same size, it is
    // a bitcast, not a conversion.
    if (S.Context.getTypeSize(Source) == S.Context.getTypeSize(Target))
      return;

    Source = cast<VectorType>(Source)->getElementType().getTypePtr();
    Target = cast<VectorType>(Target)->getElementType().getTypePtr();
  }
  if (auto VecTy = dyn_cast<VectorType>(Target))
    Target = VecTy->getElementType().getTypePtr();

  // Strip complex types.
  if (isa<ComplexType>(Source)) {
    if (!isa<ComplexType>(Target)) {
      if (S.SourceMgr.isInSystemMacro(CC) || Target->isBooleanType())
        return;

      return diagnoseImpCast(S, E, T, CC, diag::warn_impcast_complex_scalar);
    }

    Source = cast<ComplexType>(Source)->getElementType().getTypePtr();
    Target = cast<ComplexType>(Target)->getElementType().getTypePtr();
  }

  const BuiltinType *SourceBT = dyn_cast<BuiltinType>(Source);
  const BuiltinType *TargetBT = dyn_cast<BuiltinType>(Target);

  // Strip SVE vector types
  if (SourceBT && SourceBT->isSveVLSBuiltinType()) {
    // Need the original target type for vector type checks
    const Type *OriginalTarget = S.Context.getCanonicalType(T).getTypePtr();
    // Handle conversion from scalable to fixed when msve-vector-bits is
    // specified
    if (S.Context.areCompatibleSveTypes(QualType(OriginalTarget, 0),
                                        QualType(Source, 0)) ||
        S.Context.areLaxCompatibleSveTypes(QualType(OriginalTarget, 0),
                                           QualType(Source, 0)))
      return;

    // If the vector cast is cast between two vectors of the same size, it is
    // a bitcast, not a conversion.
    if (S.Context.getTypeSize(Source) == S.Context.getTypeSize(Target))
      return;

    Source = SourceBT->getSveEltType(S.Context).getTypePtr();
  }

  if (TargetBT && TargetBT->isSveVLSBuiltinType())
    Target = TargetBT->getSveEltType(S.Context).getTypePtr();

  // If the source is floating point...
  if (SourceBT && SourceBT->isFloatingPoint()) {
    // ...and the target is floating point...
    if (TargetBT && TargetBT->isFloatingPoint()) {
      // ...then warn if we're dropping FP rank.

      int Order = S.getTreeContext().getFloatingTypeSemanticOrder(
          QualType(SourceBT, 0), QualType(TargetBT, 0));
      if (Order > 0) {
        // Don't warn about float constants that are precisely
        // representable in the target type.
        Expr::EvalResult result;
        if (E->EvaluateAsRValue(result, S.Context)) {
          // Value might be a float, a float vector, or a float complex.
          if (isSameFloatAfterCast(
                  result.Val,
                  S.Context.getFloatTypeSemantics(QualType(TargetBT, 0)),
                  S.Context.getFloatTypeSemantics(QualType(SourceBT, 0))))
            return;
        }

        if (S.SourceMgr.isInSystemMacro(CC))
          return;

        diagnoseImpCast(S, E, T, CC, diag::warn_impcast_float_precision);
      }
      // ... or possibly if we're increasing rank, too
      else if (Order < 0) {
        if (S.SourceMgr.isInSystemMacro(CC))
          return;

        diagnoseImpCast(S, E, T, CC, diag::warn_impcast_double_promotion);
      }
      return;
    }

    // If the target is integral, always warn.
    if (TargetBT && TargetBT->isInteger()) {
      if (S.SourceMgr.isInSystemMacro(CC))
        return;

      diagnoseFloatingImpCast(S, E, T, CC);
    }

    // Detect the case where a call result is converted from floating-point to
    // to bool, and the final argument to the call is converted from bool, to
    // discover this typo:
    //
    //    bool b = fabs(x < 1.0);  // should be "bool b = fabs(x) < 1.0;"
    //
    if (Target->isBooleanType() && isa<CallExpr>(E)) {
      CallExpr *CEx = cast<CallExpr>(E);
      if (unsigned NumArgs = CEx->getNumArgs()) {
        Expr *LastA = CEx->getArg(NumArgs - 1);
        Expr *InnerE = LastA->IgnoreParenImpCasts();
        if (isa<ImplicitCastExpr>(LastA) &&
            InnerE->getType()->isBooleanType()) {
          // Warn on this floating-point to bool conversion
          diagnoseImpCast(S, E, T, CC,
                          diag::warn_impcast_floating_point_to_bool);
        }
      }
    }
    return;
  }

  // Valid casts involving fixed point types should be accounted for here.
  if (Source->isFixedPointType()) {
    if (Target->isUnsaturatedFixedPointType()) {
      Expr::EvalResult Result;
      if (E->EvaluateAsFixedPoint(Result, S.Context, Expr::SE_AllowSideEffects,
                                  S.isConstantEvaluatedContext())) {
        llvm::APFixedPoint Value = Result.Val.getFixedPoint();
        llvm::APFixedPoint MaxVal = S.Context.getFixedPointMax(T);
        llvm::APFixedPoint MinVal = S.Context.getFixedPointMin(T);
        if (Value > MaxVal || Value < MinVal) {
          S.DiagRuntimeBehavior(E->getExprLoc(), E,
                                S.PDiag(diag::warn_impcast_fixed_point_range)
                                    << Value.toString() << T
                                    << E->getSourceRange()
                                    << neverc::SourceRange(CC));
          return;
        }
      }
    } else if (Target->isIntegerType()) {
      Expr::EvalResult Result;
      if (!S.isConstantEvaluatedContext() &&
          E->EvaluateAsFixedPoint(Result, S.Context,
                                  Expr::SE_AllowSideEffects)) {
        llvm::APFixedPoint FXResult = Result.Val.getFixedPoint();

        bool Overflowed;
        llvm::APSInt IntResult = FXResult.convertToInt(
            S.Context.getIntWidth(T),
            Target->isSignedIntegerOrEnumerationType(), &Overflowed);

        if (Overflowed) {
          S.DiagRuntimeBehavior(E->getExprLoc(), E,
                                S.PDiag(diag::warn_impcast_fixed_point_range)
                                    << FXResult.toString() << T
                                    << E->getSourceRange()
                                    << neverc::SourceRange(CC));
          return;
        }
      }
    }
  } else if (Target->isUnsaturatedFixedPointType()) {
    if (Source->isIntegerType()) {
      Expr::EvalResult Result;
      if (!S.isConstantEvaluatedContext() &&
          E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects)) {
        llvm::APSInt Value = Result.Val.getInt();

        bool Overflowed;
        llvm::APFixedPoint IntResult = llvm::APFixedPoint::getFromIntValue(
            Value, S.Context.getFixedPointSemantics(T), &Overflowed);

        if (Overflowed) {
          S.DiagRuntimeBehavior(E->getExprLoc(), E,
                                S.PDiag(diag::warn_impcast_fixed_point_range)
                                    << toString(Value, /*Radix=*/10) << T
                                    << E->getSourceRange()
                                    << neverc::SourceRange(CC));
          return;
        }
      }
    }
  }

  // If we are casting an integer type to a floating point type without
  // initialization-list syntax, we might lose accuracy if the floating
  // point type has a narrower significand than the integer type.
  if (SourceBT && TargetBT && SourceBT->isIntegerType() &&
      TargetBT->isFloatingType() && !IsListInit) {
    IntRange SourceRange =
        getExprRange(S.Context, E, S.isConstantEvaluatedContext(),
                     /*Approximate=*/true);
    unsigned int SourcePrecision = SourceRange.Width;
    unsigned int TargetPrecision = llvm::APFloatBase::semanticsPrecision(
        S.Context.getFloatTypeSemantics(QualType(TargetBT, 0)));

    if (SourcePrecision > 0 && TargetPrecision > 0 &&
        SourcePrecision > TargetPrecision) {

      if (std::optional<llvm::APSInt> SourceInt =
              E->getIntegerConstantExpr(S.Context)) {
        // If the source integer is a constant, convert it to the target
        // floating point type. Issue a warning if the value changes
        // during the whole conversion.
        llvm::APFloat TargetFloatValue(
            S.Context.getFloatTypeSemantics(QualType(TargetBT, 0)));
        llvm::APFloat::opStatus ConversionStatus =
            TargetFloatValue.convertFromAPInt(
                *SourceInt, SourceBT->isSignedInteger(),
                llvm::APFloat::rmNearestTiesToEven);

        if (ConversionStatus != llvm::APFloat::opOK) {
          llvm::SmallString<32> PrettySourceValue;
          SourceInt->toString(PrettySourceValue, 10);
          llvm::SmallString<32> PrettyTargetValue;
          TargetFloatValue.toString(PrettyTargetValue, TargetPrecision);

          S.DiagRuntimeBehavior(
              E->getExprLoc(), E,
              S.PDiag(diag::warn_impcast_integer_float_precision_constant)
                  << PrettySourceValue << PrettyTargetValue << E->getType() << T
                  << E->getSourceRange() << neverc::SourceRange(CC));
        }
      } else {
        // Otherwise, the implicit conversion may lose precision.
        diagnoseImpCast(S, E, T, CC,
                        diag::warn_impcast_integer_float_precision);
      }
    }
  }

  diagnoseNullConversion(S, E, T, CC);

  S.DiscardMisalignedMemberAddress(Target, E);

  if (Target->isBooleanType())
    diagnoseIntInBoolContext(S, E);

  if (!Source->isIntegerType() || !Target->isIntegerType())
    return;

  if (Target->isSpecificBuiltinType(BuiltinType::Bool))
    return;

  IntRange SourceTypeRange =
      IntRange::forTargetOfCanonicalType(S.Context, Source);
  IntRange LikelySourceRange = getExprRange(
      S.Context, E, S.isConstantEvaluatedContext(), /*Approximate=*/true);
  IntRange TargetRange = IntRange::forTargetOfCanonicalType(S.Context, Target);

  if (LikelySourceRange.Width > TargetRange.Width) {
    // If the source is a constant, use a default-on diagnostic.
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects,
                         S.isConstantEvaluatedContext())) {
      llvm::APSInt Value(32);
      Value = Result.Val.getInt();

      if (S.SourceMgr.isInSystemMacro(CC))
        return;

      std::string PrettySourceValue = toString(Value, 10);
      std::string PrettyTargetValue = prettyPrintInRange(Value, TargetRange);

      S.DiagRuntimeBehavior(
          E->getExprLoc(), E,
          S.PDiag(diag::warn_impcast_integer_precision_constant)
              << PrettySourceValue << PrettyTargetValue << E->getType() << T
              << E->getSourceRange() << SourceRange(CC));
      return;
    }

    // People want to build with -Wshorten-64-to-32 and not -Wconversion.
    if (S.SourceMgr.isInSystemMacro(CC))
      return;

    if (TargetRange.Width == 32 && S.Context.getIntWidth(E->getType()) == 64)
      return diagnoseImpCast(S, E, T, CC, diag::warn_impcast_integer_64_32,
                             /* pruneControlFlow */ true);
    return diagnoseImpCast(S, E, T, CC, diag::warn_impcast_integer_precision);
  }

  if (TargetRange.Width > SourceTypeRange.Width) {
    if (auto *UO = dyn_cast<UnaryOperator>(E))
      if (UO->getOpcode() == UO_Minus)
        if (Source->isUnsignedIntegerType()) {
          if (Target->isUnsignedIntegerType())
            return diagnoseImpCast(S, E, T, CC,
                                   diag::warn_impcast_high_order_zero_bits);
          if (Target->isSignedIntegerType())
            return diagnoseImpCast(S, E, T, CC,
                                   diag::warn_impcast_nonnegative_result);
        }
  }

  if (TargetRange.Width == LikelySourceRange.Width &&
      !TargetRange.NonNegative && LikelySourceRange.NonNegative &&
      Source->isSignedIntegerType()) {
    // Warn when doing a signed to signed conversion, warn if the positive
    // source value is exactly the width of the target type, which will
    // cause a negative value to be stored.

    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects) &&
        !S.SourceMgr.isInSystemMacro(CC)) {
      llvm::APSInt Value = Result.Val.getInt();
      if (isSameWidthConstantConversion(S, E, T, CC)) {
        std::string PrettySourceValue = toString(Value, 10);
        std::string PrettyTargetValue = prettyPrintInRange(Value, TargetRange);

        S.DiagRuntimeBehavior(
            E->getExprLoc(), E,
            S.PDiag(diag::warn_impcast_integer_precision_constant)
                << PrettySourceValue << PrettyTargetValue << E->getType() << T
                << E->getSourceRange() << SourceRange(CC));
        return;
      }
    }

    // Fall through for non-constants to give a sign conversion warning.
  }

  if ((!isa<EnumType>(Target) || !isa<EnumType>(Source)) &&
      ((TargetRange.NonNegative && !LikelySourceRange.NonNegative) ||
       (!TargetRange.NonNegative && LikelySourceRange.NonNegative &&
        LikelySourceRange.Width == TargetRange.Width))) {
    if (S.SourceMgr.isInSystemMacro(CC))
      return;

    if (SourceBT && SourceBT->isInteger() && TargetBT &&
        TargetBT->isInteger() &&
        Source->isSignedIntegerType() == Target->isSignedIntegerType()) {
      return;
    }

    unsigned DiagID = diag::warn_impcast_integer_sign;

    // Traditionally, gcc has warned about this under -Wsign-compare.
    // We also want to warn about it in -Wconversion.
    // So if -Wconversion is off, use a completely identical diagnostic
    // in the sign-compare group.
    // The conditional-checking code will
    if (ICContext) {
      DiagID = diag::warn_impcast_integer_sign_conditional;
      *ICContext = true;
    }

    return diagnoseImpCast(S, E, T, CC, DiagID);
  }

  // Diagnose conversions between different enumeration types.
  // In C, we pretend that the type of an EnumConstantDecl is its enumeration
  // type, to give us better diagnostics.
  QualType SourceType = E->getType();
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    if (EnumConstantDecl *ECD = dyn_cast<EnumConstantDecl>(DRE->getDecl())) {
      EnumDecl *Enum = cast<EnumDecl>(ECD->getDeclContext());
      SourceType = S.Context.getTypeDeclType(Enum);
      Source = S.Context.getCanonicalType(SourceType).getTypePtr();
    }

  if (const EnumType *SourceEnum = Source->getAs<EnumType>())
    if (const EnumType *TargetEnum = Target->getAs<EnumType>())
      if (SourceEnum->getDecl()->hasNameForLinkage() &&
          TargetEnum->getDecl()->hasNameForLinkage() &&
          SourceEnum != TargetEnum) {
        if (S.SourceMgr.isInSystemMacro(CC))
          return;

        return diagnoseImpCast(S, E, SourceType, T, CC,
                               diag::warn_impcast_different_enum_types);
      }
}

void checkConditionalOperator(Sema &S, AbstractConditionalOperator *E,
                              SourceLocation CC, QualType T);

void checkConditionalOperand(Sema &S, Expr *E, QualType T, SourceLocation CC,
                             bool &ICContext) {
  E = E->IgnoreParenImpCasts();
  // Diagnose incomplete type for second or third operand in C.
  if (E->getType()->isRecordType())
    S.RequireCompleteExprType(E, diag::err_incomplete_type);

  if (auto *CO = dyn_cast<AbstractConditionalOperator>(E))
    return checkConditionalOperator(S, CO, CC, T);

  analyzeImplicitConversions(S, E, CC);
  if (E->getType() != T)
    return checkImplicitConversion(S, E, T, CC, &ICContext);
}

void checkConditionalOperator(Sema &S, AbstractConditionalOperator *E,
                              SourceLocation CC, QualType T) {
  analyzeImplicitConversions(S, E->getCond(), E->getQuestionLoc());

  Expr *TrueExpr = E->getTrueExpr();
  if (auto *BCO = dyn_cast<BinaryConditionalOperator>(E))
    TrueExpr = BCO->getCommon();

  bool Suspicious = false;
  checkConditionalOperand(S, TrueExpr, T, CC, Suspicious);
  checkConditionalOperand(S, E->getFalseExpr(), T, CC, Suspicious);

  if (T->isBooleanType())
    diagnoseIntInBoolContext(S, E);

  // If -Wconversion would have warned about either of the candidates
  // for a signedness conversion to the context type...
  if (!Suspicious)
    return;

  // ...but it's currently ignored...
  if (!S.Diags.isIgnored(diag::warn_impcast_integer_sign_conditional, CC))
    return;

  // ...then check whether it would have warned about either of the
  // candidates for a signedness conversion to the condition type.
  if (E->getType() == T)
    return;

  Suspicious = false;
  checkImplicitConversion(S, TrueExpr->IgnoreParenImpCasts(), E->getType(), CC,
                          &Suspicious);
  if (!Suspicious)
    checkImplicitConversion(S, E->getFalseExpr()->IgnoreParenImpCasts(),
                            E->getType(), CC, &Suspicious);
}

void checkBoolLikeConversion(Sema &S, Expr *E, SourceLocation CC) {
  if (S.getLangOpts().Bool)
    return;
  if (E->IgnoreParenImpCasts()->getType()->isAtomicType())
    return;
  checkImplicitConversion(S, E->IgnoreParenImpCasts(), S.Context.BoolTy, CC);
}
} // namespace

namespace {
struct analyzeImplicitConversionsWorkItem {
  Expr *E;
  SourceLocation CC;
  bool IsListInit;
};
} // namespace

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isAnalysisTrivialLeaf(const Expr *E) {
  switch (E->getStmtClass()) {
  case Stmt::IntegerLiteralClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::StringLiteralClass:
  case Stmt::ImaginaryLiteralClass:
  case Stmt::FixedPointLiteralClass:
  case Stmt::DeclRefExprClass:
  case Stmt::MemberExprClass:
  case Stmt::UnaryExprOrTypeTraitExprClass:
    return true;
  default:
    return false;
  }
}

void analyzeImplicitConversions(
    Sema &S, analyzeImplicitConversionsWorkItem Item,
    llvm::SmallVectorImpl<analyzeImplicitConversionsWorkItem> &WorkList) {
  Expr *OrigE = Item.E;
  SourceLocation CC = Item.CC;

  QualType T = OrigE->getType();
  Expr *E = OrigE->IgnoreParenImpCasts();

  if (T == E->getType()) {
    if (isAnalysisTrivialLeaf(E))
      return;
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (!BO->isComparisonOp() && !BO->isAssignmentOp() &&
          !BO->isLogicalOp() && BO->getOpcode() != BO_And &&
          BO->getOpcode() != BO_Or) {
        SourceLocation SubCC = E->getExprLoc();
        bool IsListInit =
            Item.IsListInit || (isa<InitListExpr>(OrigE) && false);
        WorkList.push_back({BO->getLHS(), SubCC, IsListInit});
        WorkList.push_back({BO->getRHS(), SubCC, IsListInit});
        return;
      }
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() != UO_LNot && UO->getOpcode() != UO_Not) {
        SourceLocation SubCC = E->getExprLoc();
        bool IsListInit =
            Item.IsListInit || (isa<InitListExpr>(OrigE) && false);
        WorkList.push_back({UO->getSubExpr(), SubCC, IsListInit});
        return;
      }
    }
    if (auto *CE = dyn_cast<CallExpr>(E)) {
      SourceLocation SubCC = E->getExprLoc();
      bool IsListInit = Item.IsListInit || (isa<InitListExpr>(OrigE) && false);
      checkImplicitArgumentConversions(S, CE, SubCC);
      for (unsigned i = 0, n = CE->getNumArgs(); i < n; i++)
        WorkList.push_back({CE->getArg(i), SubCC, IsListInit});
      return;
    }
    if (isa<MemberExpr>(E))
      return;
  }

  // List-init suppresses duplicate float narrowing warnings (handled
  // elsewhere).
  bool IsListInit = Item.IsListInit || (isa<InitListExpr>(OrigE) && false);

  Expr *SourceExpr = E;
  // Examine, but don't traverse into the source expression of an
  // OpaqueValueExpr, since it may have multiple parents and we don't want to
  // emit duplicate diagnostics. Its fine to examine the form or attempt to
  // evaluate it in the context of checking the specific conversion to T though.
  if (auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    if (auto *Src = OVE->getSourceExpr())
      SourceExpr = Src;

  if (const auto *UO = dyn_cast<UnaryOperator>(SourceExpr))
    if (UO->getOpcode() == UO_Not &&
        UO->getSubExpr()->isKnownToHaveBooleanValue())
      S.Diag(UO->getBeginLoc(), diag::warn_bitwise_negation_bool)
          << OrigE->getSourceRange() << T->isBooleanType()
          << FixItHint::CreateReplacement(UO->getBeginLoc(), "!");

  if (const auto *BO = dyn_cast<BinaryOperator>(SourceExpr))
    if ((BO->getOpcode() == BO_And || BO->getOpcode() == BO_Or) &&
        BO->getLHS()->isKnownToHaveBooleanValue() &&
        BO->getRHS()->isKnownToHaveBooleanValue() &&
        BO->getLHS()->HasSideEffects(S.Context) &&
        BO->getRHS()->HasSideEffects(S.Context)) {
      S.Diag(BO->getBeginLoc(), diag::warn_bitwise_instead_of_logical)
          << (BO->getOpcode() == BO_And ? "&" : "|") << OrigE->getSourceRange()
          << FixItHint::CreateReplacement(
                 BO->getOperatorLoc(),
                 (BO->getOpcode() == BO_And ? "&&" : "||"));
      S.Diag(BO->getBeginLoc(), diag::note_cast_operand_to_int);
    }

  // For conditional operators, we analyze the arguments as if they
  // were being fed directly into the output.
  if (auto *CO = dyn_cast<AbstractConditionalOperator>(SourceExpr)) {
    checkConditionalOperator(S, CO, CC, T);
    return;
  }

  if (CallExpr *Call = dyn_cast<CallExpr>(SourceExpr))
    checkImplicitArgumentConversions(S, Call, CC);

  // Go ahead and check any implicit conversions we might have skipped.
  // The non-canonical typecheck is just an optimization;
  // checkImplicitConversion will filter out dead implicit conversions.
  if (SourceExpr->getType() != T)
    checkImplicitConversion(S, SourceExpr, T, CC, nullptr, IsListInit);

  // Now continue drilling into this expression.

  if (PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E)) {
    // The bound subexpressions in a PseudoObjectExpr are not reachable
    // as transitive children.
    for (auto *SE : POE->semantics())
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(SE))
        WorkList.push_back({OVE->getSourceExpr(), CC, IsListInit});
  }

  // Skip past explicit casts.
  if (auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
    E = CE->getSubExpr()->IgnoreParenImpCasts();
    if (!CE->getType()->isVoidType() && E->getType()->isAtomicType())
      S.Diag(E->getBeginLoc(), diag::warn_atomic_implicit_seq_cst);
    WorkList.push_back({E, CC, IsListInit});
    return;
  }

  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    // Do a somewhat different check with comparison operators.
    if (BO->isComparisonOp())
      return analyzeComparison(S, BO);

    // And with simple assignments.
    if (BO->getOpcode() == BO_Assign)
      return analyzeAssignment(S, BO);
    // And with compound assignments.
    if (BO->isAssignmentOp())
      return analyzeCompoundAssignment(S, BO);
  }

  // These break the otherwise-useful invariant below.  Fortunately,
  // we don't really need to recurse into them, because any internal
  // expressions should have been analyzed already when they were
  // built into statements.
  if (isa<StmtExpr>(E))
    return;

  // Don't descend into unevaluated contexts.
  if (isa<UnaryExprOrTypeTraitExpr>(E))
    return;

  // Now just recurse over the expression's children.
  CC = E->getExprLoc();
  BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
  bool IsLogicalAndOperator = BO && BO->getOpcode() == BO_LAnd;
  for (Stmt *SubStmt : E->children()) {
    Expr *ChildExpr = dyn_cast_or_null<Expr>(SubStmt);
    if (!ChildExpr)
      continue;

    if (IsLogicalAndOperator &&
        isa<StringLiteral>(ChildExpr->IgnoreParenImpCasts()))
      // Ignore checking string literals that are in logical and operators.
      // This is a common pattern for asserts.
      continue;
    WorkList.push_back({ChildExpr, CC, IsListInit});
  }

  if (BO && BO->isLogicalOp()) {
    Expr *SubExpr = BO->getLHS()->IgnoreParenImpCasts();
    if (!IsLogicalAndOperator || !isa<StringLiteral>(SubExpr))
      ::checkBoolLikeConversion(S, SubExpr, BO->getExprLoc());

    SubExpr = BO->getRHS()->IgnoreParenImpCasts();
    if (!IsLogicalAndOperator || !isa<StringLiteral>(SubExpr))
      ::checkBoolLikeConversion(S, SubExpr, BO->getExprLoc());
  }

  if (const UnaryOperator *U = dyn_cast<UnaryOperator>(E)) {
    if (U->getOpcode() == UO_LNot) {
      ::checkBoolLikeConversion(S, U->getSubExpr(), CC);
    } else if (U->getOpcode() != UO_AddrOf) {
      if (U->getSubExpr()->getType()->isAtomicType())
        S.Diag(U->getSubExpr()->getBeginLoc(),
               diag::warn_atomic_implicit_seq_cst);
    }
  }
}

void analyzeImplicitConversions(Sema &S, Expr *OrigE, SourceLocation CC,
                                bool IsListInit /*= false*/) {
  llvm::SmallVector<analyzeImplicitConversionsWorkItem, 16> WorkList;
  WorkList.push_back({OrigE, CC, IsListInit});
  while (!WorkList.empty())
    analyzeImplicitConversions(S, WorkList.pop_back_val(), WorkList);
}
} // namespace

// ===----------------------------------------------------------------------===
// Nonnull pointer & bool-like conversion checks
// ===----------------------------------------------------------------------===

namespace {
bool isInAnyMacroBody(const SourceManager &SM, SourceLocation Loc) {
  if (Loc.isInvalid())
    return false;

  while (Loc.isMacroID()) {
    if (SM.isMacroBodyExpansion(Loc))
      return true;
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  }

  return false;
}
} // namespace

void Sema::DiagnoseAlwaysNonNullPointer(Expr *E,
                                        Expr::NullPointerConstantKind NullKind,
                                        bool IsEqual, SourceRange Range) {
  if (!E)
    return;

  // Don't warn inside macros.
  if (E->getExprLoc().isMacroID()) {
    const SourceManager &SM = getSourceManager();
    if (isInAnyMacroBody(SM, E->getExprLoc()) ||
        isInAnyMacroBody(SM, Range.getBegin()))
      return;
  }
  E = E->IgnoreImpCasts();

  const bool IsCompare = NullKind != Expr::NPCK_NotNull;

  bool IsAddressOf = false;

  if (auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParens())) {
    if (UO->getOpcode() != UO_AddrOf)
      return;
    IsAddressOf = true;
    E = UO->getSubExpr();
  }

  auto ComplainAboutNonnullParamOrCall = [&](const Attr *NonnullAttr) {
    bool IsParam = isa<NonNullAttr>(NonnullAttr);
    std::string Str;
    llvm::raw_string_ostream S(Str);
    E->printPretty(S, nullptr, getPrintingPolicy());
    unsigned DiagID = IsCompare ? diag::warn_nonnull_expr_compare
                                : diag::warn_cast_nonnull_to_bool;
    Diag(E->getExprLoc(), DiagID)
        << IsParam << S.str() << E->getSourceRange() << Range << IsEqual;
    Diag(NonnullAttr->getLocation(), diag::note_declared_nonnull) << IsParam;
  };

  // If we have a CallExpr that is tagged with returns_nonnull, we can complain.
  if (auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts())) {
    if (auto *Callee = Call->getDirectCallee()) {
      if (const Attr *A = Callee->getAttr<ReturnsNonNullAttr>()) {
        ComplainAboutNonnullParamOrCall(A);
        return;
      }
    }
  }

  // Expect to find a single Decl.  Skip anything more complicated.
  ValueDecl *D = nullptr;
  if (DeclRefExpr *R = dyn_cast<DeclRefExpr>(E)) {
    D = R->getDecl();
  } else if (MemberExpr *M = dyn_cast<MemberExpr>(E)) {
    D = M->getMemberDecl();
  }

  // Weak Decls can be null.
  if (!D || D->isWeak())
    return;

  if (const auto *PV = dyn_cast<ParmVarDecl>(D)) {
    if (getCurFunction() &&
        !getCurFunction()->ModifiedNonNullParams.contains(PV)) {
      if (const Attr *A = PV->getAttr<NonNullAttr>()) {
        ComplainAboutNonnullParamOrCall(A);
        return;
      }

      if (const auto *FD = dyn_cast<FunctionDecl>(PV->getDeclContext())) {
        auto ParamIter = llvm::find(FD->parameters(), PV);
        assert(ParamIter != FD->param_end());
        unsigned ParamNo = std::distance(FD->param_begin(), ParamIter);

        for (const auto *NonNull : FD->specific_attrs<NonNullAttr>()) {
          if (!NonNull->args_size()) {
            ComplainAboutNonnullParamOrCall(NonNull);
            return;
          }

          for (const ParamIdx &ArgNo : NonNull->args()) {
            if (ArgNo.getASTIndex() == ParamNo) {
              ComplainAboutNonnullParamOrCall(NonNull);
              return;
            }
          }
        }
      }
    }
  }

  QualType T = D->getType();
  const bool IsArray = T->isArrayType();
  const bool IsFunction = T->isFunctionType();

  // Address of function is used to silence the function warning.
  if (IsAddressOf && IsFunction) {
    return;
  }

  // Found nothing.
  if (!IsAddressOf && !IsFunction && !IsArray)
    return;

  // Pretty print the expression for the diagnostic.
  std::string Str;
  llvm::raw_string_ostream S(Str);
  E->printPretty(S, nullptr, getPrintingPolicy());

  unsigned DiagID = IsCompare ? diag::warn_null_pointer_compare
                              : diag::warn_impcast_pointer_to_bool;
  enum { AddressOf, FunctionPointer, ArrayPointer } DiagType;
  if (IsAddressOf)
    DiagType = AddressOf;
  else if (IsFunction)
    DiagType = FunctionPointer;
  else if (IsArray)
    DiagType = ArrayPointer;
  else
    llvm_unreachable("Could not determine diagnostic.");
  Diag(E->getExprLoc(), DiagID)
      << DiagType << S.str() << E->getSourceRange() << Range << IsEqual;

  if (!IsFunction)
    return;

  // Suggest '&' to silence the function warning.
  Diag(E->getExprLoc(), diag::note_function_warning_silence)
      << FixItHint::CreateInsertion(E->getBeginLoc(), "&");

  QualType ReturnType;
  tryExprAsCall(*E, ReturnType);
  if (ReturnType.isNull())
    return;

  if (IsCompare) {
    // There are two cases here.  If there is null constant, the only suggest
    // for a pointer return type.  If the null is 0, then suggest if the return
    // type is a pointer or an integer type.
    if (!ReturnType->isPointerType()) {
      if (NullKind == Expr::NPCK_ZeroExpression ||
          NullKind == Expr::NPCK_ZeroLiteral) {
        if (!ReturnType->isIntegerType())
          return;
      } else {
        return;
      }
    }
  } else { // !IsCompare
    // For function to bool, only suggest if the function pointer has bool
    // return type.
    if (!ReturnType->isSpecificBuiltinType(BuiltinType::Bool))
      return;
  }
  Diag(E->getExprLoc(), diag::note_function_to_function_call)
      << FixItHint::CreateInsertion(getLocForEndOfToken(E->getEndLoc()), "()");
}

__attribute__((noinline)) void
Sema::CheckImplicitConversions(Expr *E, SourceLocation CC) {
  // Don't diagnose in unevaluated contexts.
  if (isUnevaluatedContext())
    return;

  // Check for array bounds violations in cases where the check isn't triggered
  // elsewhere for other Expr types (like BinaryOperators), e.g. when an
  // ArraySubscriptExpr is on the RHS of a variable initialization.
  CheckArrayAccess(E);

  // This is not the right CC for (e.g.) a variable initialization.
  analyzeImplicitConversions(*this, E, CC);
}

void Sema::CheckBoolLikeConversion(Expr *E, SourceLocation CC) {
  ::checkBoolLikeConversion(*this, E, CC);
}
