#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
using namespace neverc;
using namespace Emit;

typedef FunctionEmitter::ComplexPairTy ComplexPairTy;

// ===----------------------------------------------------------------------===
// ComplexExprEmitter class & helpers
// ===----------------------------------------------------------------------===

namespace {
const ComplexType *getComplexType(QualType type) {
  type = type.getCanonicalType();
  if (const ComplexType *comp = dyn_cast<ComplexType>(type)) {
    return comp;
  } else {
    return cast<ComplexType>(cast<AtomicType>(type)->getValueType());
  }
}

class ComplexExprEmitter
    : public StmtVisitor<ComplexExprEmitter, ComplexPairTy> {
  FunctionEmitter &FE;
  CGBuilderTy &Builder;
  bool IgnoreReal;
  bool IgnoreImag;

public:
  ComplexExprEmitter(FunctionEmitter &cgf, bool ir = false, bool ii = false)
      : FE(cgf), Builder(FE.Builder), IgnoreReal(ir), IgnoreImag(ii) {}

  //===--------------------------------------------------------------------===
  //                               Utilities
  //===--------------------------------------------------------------------===

  bool TestAndClearIgnoreReal() {
    bool I = IgnoreReal;
    IgnoreReal = false;
    return I;
  }
  bool TestAndClearIgnoreImag() {
    bool I = IgnoreImag;
    IgnoreImag = false;
    return I;
  }

  ComplexPairTy genLoadOfLValue(const Expr *E) {
    return genLoadOfLValue(FE.genLValue(E), E->getExprLoc());
  }

  ComplexPairTy genLoadOfLValue(LValue LV, SourceLocation Loc);

  void genStoreOfComplex(ComplexPairTy Val, LValue LV, bool isInit);

  ComplexPairTy genComplexToComplexCast(ComplexPairTy Val, QualType SrcType,
                                        QualType DestType, SourceLocation Loc);
  ComplexPairTy genScalarToComplexCast(llvm::Value *Val, QualType SrcType,
                                       QualType DestType, SourceLocation Loc);

  //===--------------------------------------------------------------------===
  //                            Visitor Methods
  //===--------------------------------------------------------------------===

  ComplexPairTy Visit(Expr *E) {
    ApplyDebugLocation DL(FE, E);
    return StmtVisitor<ComplexExprEmitter, ComplexPairTy>::Visit(E);
  }

  ComplexPairTy VisitStmt(Stmt *S) {
    S->dump(llvm::errs(), FE.getContext());
    llvm_unreachable("Stmt can't have complex result type!");
  }
  ComplexPairTy VisitExpr(Expr *S);
  ComplexPairTy VisitConstantExpr(ConstantExpr *E) {
    if (llvm::Constant *Result = ConstantEmitter(FE).tryEmitConstantExpr(E))
      return ComplexPairTy(Result->getAggregateElement(0U),
                           Result->getAggregateElement(1U));
    return Visit(E->getSubExpr());
  }
  ComplexPairTy VisitParenExpr(ParenExpr *PE) {
    return Visit(PE->getSubExpr());
  }
  ComplexPairTy VisitGenericSelectionExpr(GenericSelectionExpr *GE) {
    return Visit(GE->getResultExpr());
  }
  ComplexPairTy VisitImaginaryLiteral(const ImaginaryLiteral *IL);

  ComplexPairTy
  emitConstant(const FunctionEmitter::ConstantEmission &Constant) {
    assert(Constant && "not a constant");
    llvm::Constant *pair = Constant.getValue();
    return ComplexPairTy(pair->getAggregateElement(0U),
                         pair->getAggregateElement(1U));
  }

  // l-values.
  ComplexPairTy VisitDeclRefExpr(DeclRefExpr *E) {
    if (FunctionEmitter::ConstantEmission Constant = FE.tryEmitAsConstant(E))
      return emitConstant(Constant);
    return genLoadOfLValue(E);
  }
  ComplexPairTy VisitArraySubscriptExpr(Expr *E) { return genLoadOfLValue(E); }
  ComplexPairTy VisitMemberExpr(MemberExpr *ME) {
    if (FunctionEmitter::ConstantEmission Constant = FE.tryEmitAsConstant(ME)) {
      FE.genIgnoredExpr(ME->getBase());
      return emitConstant(Constant);
    }
    return genLoadOfLValue(ME);
  }
  ComplexPairTy VisitOpaqueValueExpr(OpaqueValueExpr *E) {
    if (E->isLValue())
      return genLoadOfLValue(FE.getOrCreateOpaqueLValueMapping(E),
                             E->getExprLoc());
    return FE.getOrCreateOpaqueRValueMapping(E).getComplexVal();
  }

  ComplexPairTy VisitPseudoObjectExpr(PseudoObjectExpr *E) {
    return FE.genPseudoObjectRValue(E).getComplexVal();
  }

  ComplexPairTy genCast(CastKind CK, Expr *Op, QualType DestTy);
  ComplexPairTy VisitImplicitCastExpr(ImplicitCastExpr *E) {
    // Unlike for scalars, we don't have to worry about function->ptr demotion
    // here.
    if (E->changesVolatileQualification())
      return genLoadOfLValue(E);
    return genCast(E->getCastKind(), E->getSubExpr(), E->getType());
  }
  ComplexPairTy VisitCastExpr(CastExpr *E) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
      FE.ME.genExplicitCastExprType(ECE, &FE);
    if (E->changesVolatileQualification())
      return genLoadOfLValue(E);
    return genCast(E->getCastKind(), E->getSubExpr(), E->getType());
  }
  ComplexPairTy VisitCallExpr(const CallExpr *E);
  ComplexPairTy VisitStmtExpr(const StmtExpr *E);

  // Operators.
  ComplexPairTy VisitPrePostIncDec(const UnaryOperator *E, bool isInc,
                                   bool isPre) {
    LValue LV = FE.genLValue(E->getSubExpr());
    return FE.genComplexPrePostIncDec(E, LV, isInc, isPre);
  }
  ComplexPairTy VisitUnaryPostDec(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, false, false);
  }
  ComplexPairTy VisitUnaryPostInc(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, true, false);
  }
  ComplexPairTy VisitUnaryPreDec(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, false, true);
  }
  ComplexPairTy VisitUnaryPreInc(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, true, true);
  }
  ComplexPairTy VisitUnaryDeref(const Expr *E) { return genLoadOfLValue(E); }

  ComplexPairTy VisitUnaryPlus(const UnaryOperator *E,
                               QualType PromotionType = QualType());
  ComplexPairTy VisitPlus(const UnaryOperator *E, QualType PromotionType);
  ComplexPairTy VisitUnaryMinus(const UnaryOperator *E,
                                QualType PromotionType = QualType());
  ComplexPairTy VisitMinus(const UnaryOperator *E, QualType PromotionType);
  ComplexPairTy VisitUnaryNot(const UnaryOperator *E);
  // LNot,Real,Imag never return complex.
  ComplexPairTy VisitUnaryExtension(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
  ComplexPairTy VisitExprWithCleanups(ExprWithCleanups *E) {
    FunctionEmitter::RunCleanupsScope Scope(FE);
    ComplexPairTy Vals = Visit(E->getSubExpr());
    // Defend against dominance problems caused by jumps out of expression
    // evaluation through the shared cleanup block.
    Scope.ForceCleanup({&Vals.first, &Vals.second});
    return Vals;
  }
  ComplexPairTy VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
    assert(E->getType()->isAnyComplexType() && "Expected complex type!");
    QualType Elem = E->getType()->castAs<ComplexType>()->getElementType();
    llvm::Constant *Null = llvm::Constant::getNullValue(FE.convertType(Elem));
    return ComplexPairTy(Null, Null);
  }

  struct BinOpInfo {
    ComplexPairTy LHS;
    ComplexPairTy RHS;
    QualType Ty; // Computation Type.
    FPOptions FPFeatures;
  };

  BinOpInfo genBinOps(const BinaryOperator *E,
                      QualType PromotionTy = QualType());
  ComplexPairTy genPromoted(const Expr *E, QualType PromotionTy);
  ComplexPairTy genPromotedComplexOperand(const Expr *E, QualType PromotionTy);
  LValue genCompoundAssignLValue(
      const CompoundAssignOperator *E,
      ComplexPairTy (ComplexExprEmitter::*Func)(const BinOpInfo &),
      RValue &Val);
  ComplexPairTy genCompoundAssign(
      const CompoundAssignOperator *E,
      ComplexPairTy (ComplexExprEmitter::*Func)(const BinOpInfo &));

  ComplexPairTy genBinAdd(const BinOpInfo &Op);
  ComplexPairTy genBinSub(const BinOpInfo &Op);
  ComplexPairTy genBinMul(const BinOpInfo &Op);
  ComplexPairTy genBinDiv(const BinOpInfo &Op);
  ComplexPairTy genAlgebraicDiv(llvm::Value *A, llvm::Value *B, llvm::Value *C,
                                llvm::Value *D);
  ComplexPairTy genRangeReductionDiv(llvm::Value *A, llvm::Value *B,
                                     llvm::Value *C, llvm::Value *D);

  ComplexPairTy genComplexBinOpLibCall(llvm::StringRef LibCallName,
                                       const BinOpInfo &Op);

  QualType getPromotionType(QualType Ty) {
    if (auto *CT = Ty->getAs<ComplexType>()) {
      QualType ElementType = CT->getElementType();
      if (ElementType.UseExcessPrecision(FE.getContext()))
        return FE.getContext().getComplexType(FE.getContext().FloatTy);
    }
    if (Ty.UseExcessPrecision(FE.getContext()))
      return FE.getContext().FloatTy;
    return QualType();
  }

#define HANDLEBINOP(OP)                                                        \
  ComplexPairTy VisitBin##OP(const BinaryOperator *E) {                        \
    QualType promotionTy = getPromotionType(E->getType());                     \
    ComplexPairTy result = genBin##OP(genBinOps(E, promotionTy));              \
    if (!promotionTy.isNull())                                                 \
      result = FE.genUnPromotedValue(result, E->getType());                    \
    return result;                                                             \
  }

  HANDLEBINOP(Mul)
  HANDLEBINOP(Div)
  HANDLEBINOP(Add)
  HANDLEBINOP(Sub)
#undef HANDLEBINOP

  // Compound assignments.
  ComplexPairTy VisitBinAddAssign(const CompoundAssignOperator *E) {
    return genCompoundAssign(E, &ComplexExprEmitter::genBinAdd);
  }
  ComplexPairTy VisitBinSubAssign(const CompoundAssignOperator *E) {
    return genCompoundAssign(E, &ComplexExprEmitter::genBinSub);
  }
  ComplexPairTy VisitBinMulAssign(const CompoundAssignOperator *E) {
    return genCompoundAssign(E, &ComplexExprEmitter::genBinMul);
  }
  ComplexPairTy VisitBinDivAssign(const CompoundAssignOperator *E) {
    return genCompoundAssign(E, &ComplexExprEmitter::genBinDiv);
  }

  // GCC rejects rem/and/or/xor for integer complex.
  // Logical and/or always return int, never complex.

  // No comparisons produce a complex result.

  LValue genBinAssignLValue(const BinaryOperator *E, ComplexPairTy &Val);
  ComplexPairTy VisitBinAssign(const BinaryOperator *E);
  ComplexPairTy VisitBinComma(const BinaryOperator *E);

  ComplexPairTy
  VisitAbstractConditionalOperator(const AbstractConditionalOperator *CO);
  ComplexPairTy VisitChooseExpr(ChooseExpr *CE);

  ComplexPairTy VisitInitListExpr(InitListExpr *E);

  ComplexPairTy VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
    return genLoadOfLValue(E);
  }

  ComplexPairTy VisitVAArgExpr(VAArgExpr *E);

  ComplexPairTy VisitAtomicExpr(AtomicExpr *E) {
    return FE.genAtomicExpr(E).getComplexVal();
  }
};
} // end anonymous namespace.

Address FunctionEmitter::emitAddrOfRealComponent(Address addr,
                                                 QualType complexType) {
  return Builder.CreateStructGEP(addr, 0, addr.getName() + ".realp");
}

Address FunctionEmitter::emitAddrOfImagComponent(Address addr,
                                                 QualType complexType) {
  return Builder.CreateStructGEP(addr, 1, addr.getName() + ".imagp");
}

ComplexPairTy ComplexExprEmitter::genLoadOfLValue(LValue lvalue,
                                                  SourceLocation loc) {
  assert(lvalue.isSimple() && "non-simple complex l-value?");
  if (lvalue.getType()->isAtomicType())
    return FE.genAtomicLoad(lvalue, loc).getComplexVal();

  Address SrcPtr = lvalue.getAddress(FE);
  bool isVolatile = lvalue.isVolatileQualified();

  llvm::Value *Real = nullptr, *Imag = nullptr;

  if (!IgnoreReal || isVolatile) {
    Address RealP = FE.emitAddrOfRealComponent(SrcPtr, lvalue.getType());
    Real = Builder.CreateLoad(RealP, isVolatile, SrcPtr.getName() + ".real");
  }

  if (!IgnoreImag || isVolatile) {
    Address ImagP = FE.emitAddrOfImagComponent(SrcPtr, lvalue.getType());
    Imag = Builder.CreateLoad(ImagP, isVolatile, SrcPtr.getName() + ".imag");
  }

  return ComplexPairTy(Real, Imag);
}

void ComplexExprEmitter::genStoreOfComplex(ComplexPairTy Val, LValue lvalue,
                                           bool isInit) {
  if (lvalue.getType()->isAtomicType() ||
      (!isInit && FE.lValueIsSuitableForInlineAtomic(lvalue)))
    return FE.genAtomicStore(RValue::getComplex(Val), lvalue, isInit);

  Address Ptr = lvalue.getAddress(FE);
  Address RealPtr = FE.emitAddrOfRealComponent(Ptr, lvalue.getType());
  Address ImagPtr = FE.emitAddrOfImagComponent(Ptr, lvalue.getType());

  Builder.CreateStore(Val.first, RealPtr, lvalue.isVolatileQualified());
  Builder.CreateStore(Val.second, ImagPtr, lvalue.isVolatileQualified());
}

ComplexPairTy ComplexExprEmitter::VisitExpr(Expr *E) {
  FE.errorUnsupported(E, "complex expression");
  llvm::Type *EltTy =
      FE.convertType(getComplexType(E->getType())->getElementType());
  llvm::Value *U = llvm::UndefValue::get(EltTy);
  return ComplexPairTy(U, U);
}

ComplexPairTy
ComplexExprEmitter::VisitImaginaryLiteral(const ImaginaryLiteral *IL) {
  llvm::Value *Imag = FE.genScalarExpr(IL->getSubExpr());
  return ComplexPairTy(llvm::Constant::getNullValue(Imag->getType()), Imag);
}

ComplexPairTy ComplexExprEmitter::VisitCallExpr(const CallExpr *E) {
  return FE.genCallExpr(E).getComplexVal();
}

ComplexPairTy ComplexExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  FunctionEmitter::StmtExprEvaluation eval(FE);
  Address RetAlloca = FE.genCompoundStmt(*E->getSubStmt(), true);
  assert(RetAlloca.isValid() && "Expected complex return value");
  return genLoadOfLValue(FE.makeAddrLValue(RetAlloca, E->getType()),
                         E->getExprLoc());
}

ComplexPairTy ComplexExprEmitter::genComplexToComplexCast(ComplexPairTy Val,
                                                          QualType SrcType,
                                                          QualType DestType,
                                                          SourceLocation Loc) {
  SrcType = SrcType->castAs<ComplexType>()->getElementType();
  DestType = DestType->castAs<ComplexType>()->getElementType();

  if (Val.first)
    Val.first = FE.genScalarConversion(Val.first, SrcType, DestType, Loc);
  if (Val.second)
    Val.second = FE.genScalarConversion(Val.second, SrcType, DestType, Loc);
  return Val;
}

ComplexPairTy ComplexExprEmitter::genScalarToComplexCast(llvm::Value *Val,
                                                         QualType SrcType,
                                                         QualType DestType,
                                                         SourceLocation Loc) {
  DestType = DestType->castAs<ComplexType>()->getElementType();
  Val = FE.genScalarConversion(Val, SrcType, DestType, Loc);

  return ComplexPairTy(Val, llvm::Constant::getNullValue(Val->getType()));
}

ComplexPairTy ComplexExprEmitter::genCast(CastKind CK, Expr *Op,
                                          QualType DestTy) {
  switch (CK) {
  case CK_Dependent:
    llvm_unreachable("dependent cast kind in IR gen!");

  // Atomic to non-atomic casts may be more than a no-op for some platforms and
  // for some types.
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_NoOp:
  case CK_LValueToRValue:
    return Visit(Op);

  case CK_LValueBitCast: {
    LValue origLV = FE.genLValue(Op);
    Address V = origLV.getAddress(FE).withElementType(FE.convertType(DestTy));
    return genLoadOfLValue(FE.makeAddrLValue(V, DestTy), Op->getExprLoc());
  }

  case CK_LValueToRValueBitCast: {
    LValue SourceLVal = FE.genLValue(Op);
    Address Addr =
        SourceLVal.getAddress(FE).withElementType(FE.convertTypeForMem(DestTy));
    LValue DestLV = FE.makeAddrLValue(Addr, DestTy);
    DestLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
    return genLoadOfLValue(DestLV, Op->getExprLoc());
  }

  case CK_BitCast:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_BuiltinFnToFnPtr:
  case CK_AddressSpaceConversion:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
    llvm_unreachable("invalid cast kind for complex value");

  case CK_FloatingRealToComplex:
  case CK_IntegralRealToComplex: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Op);
    return genScalarToComplexCast(FE.genScalarExpr(Op), Op->getType(), DestTy,
                                  Op->getExprLoc());
  }

  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Op);
    return genComplexToComplexCast(Visit(Op), Op->getType(), DestTy,
                                   Op->getExprLoc());
  }
  }

  llvm_unreachable("unknown cast resulting in complex value");
}

ComplexPairTy ComplexExprEmitter::VisitUnaryPlus(const UnaryOperator *E,
                                                 QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  ComplexPairTy result = VisitPlus(E, promotionTy);
  if (!promotionTy.isNull())
    return FE.genUnPromotedValue(result, E->getSubExpr()->getType());
  return result;
}

ComplexPairTy ComplexExprEmitter::VisitPlus(const UnaryOperator *E,
                                            QualType PromotionType) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  if (!PromotionType.isNull())
    return FE.genPromotedComplexExpr(E->getSubExpr(), PromotionType);
  return Visit(E->getSubExpr());
}

ComplexPairTy ComplexExprEmitter::VisitUnaryMinus(const UnaryOperator *E,
                                                  QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  ComplexPairTy result = VisitMinus(E, promotionTy);
  if (!promotionTy.isNull())
    return FE.genUnPromotedValue(result, E->getSubExpr()->getType());
  return result;
}
ComplexPairTy ComplexExprEmitter::VisitMinus(const UnaryOperator *E,
                                             QualType PromotionType) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  ComplexPairTy Op;
  if (!PromotionType.isNull())
    Op = FE.genPromotedComplexExpr(E->getSubExpr(), PromotionType);
  else
    Op = Visit(E->getSubExpr());

  llvm::Value *ResR, *ResI;
  if (Op.first->getType()->isFloatingPointTy()) {
    ResR = Builder.CreateFNeg(Op.first, "neg.r");
    ResI = Builder.CreateFNeg(Op.second, "neg.i");
  } else {
    ResR = Builder.CreateNeg(Op.first, "neg.r");
    ResI = Builder.CreateNeg(Op.second, "neg.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy ComplexExprEmitter::VisitUnaryNot(const UnaryOperator *E) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  // ~(a+ib) = a + i*-b
  ComplexPairTy Op = Visit(E->getSubExpr());
  llvm::Value *ResI;
  if (Op.second->getType()->isFloatingPointTy())
    ResI = Builder.CreateFNeg(Op.second, "conj.i");
  else
    ResI = Builder.CreateNeg(Op.second, "conj.i");

  return ComplexPairTy(Op.first, ResI);
}

// ===----------------------------------------------------------------------===
// Binary arithmetic (add / sub / mul / div)
// ===----------------------------------------------------------------------===

ComplexPairTy ComplexExprEmitter::genBinAdd(const BinOpInfo &Op) {
  llvm::Value *ResR, *ResI;

  if (Op.LHS.first->getType()->isFloatingPointTy()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Op.FPFeatures);
    ResR = Builder.CreateFAdd(Op.LHS.first, Op.RHS.first, "add.r");
    if (Op.LHS.second && Op.RHS.second)
      ResI = Builder.CreateFAdd(Op.LHS.second, Op.RHS.second, "add.i");
    else
      ResI = Op.LHS.second ? Op.LHS.second : Op.RHS.second;
    assert(ResI && "Only one operand may be real!");
  } else {
    ResR = Builder.CreateAdd(Op.LHS.first, Op.RHS.first, "add.r");
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    ResI = Builder.CreateAdd(Op.LHS.second, Op.RHS.second, "add.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy ComplexExprEmitter::genBinSub(const BinOpInfo &Op) {
  llvm::Value *ResR, *ResI;
  if (Op.LHS.first->getType()->isFloatingPointTy()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Op.FPFeatures);
    ResR = Builder.CreateFSub(Op.LHS.first, Op.RHS.first, "sub.r");
    if (Op.LHS.second && Op.RHS.second)
      ResI = Builder.CreateFSub(Op.LHS.second, Op.RHS.second, "sub.i");
    else
      ResI = Op.LHS.second ? Op.LHS.second
                           : Builder.CreateFNeg(Op.RHS.second, "sub.i");
    assert(ResI && "Only one operand may be real!");
  } else {
    ResR = Builder.CreateSub(Op.LHS.first, Op.RHS.first, "sub.r");
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    ResI = Builder.CreateSub(Op.LHS.second, Op.RHS.second, "sub.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy
ComplexExprEmitter::genComplexBinOpLibCall(llvm::StringRef LibCallName,
                                           const BinOpInfo &Op) {
  CallArgList Args;
  Args.add(RValue::get(Op.LHS.first),
           Op.Ty->castAs<ComplexType>()->getElementType());
  Args.add(RValue::get(Op.LHS.second),
           Op.Ty->castAs<ComplexType>()->getElementType());
  Args.add(RValue::get(Op.RHS.first),
           Op.Ty->castAs<ComplexType>()->getElementType());
  Args.add(RValue::get(Op.RHS.second),
           Op.Ty->castAs<ComplexType>()->getElementType());

  // We *must* use the full CG function call building logic here because the
  // complex type has special ABI handling. We also should not forget about
  // special calling convention which may be used for compiler builtins.

  // We create a function qualified type to state that this call does not have
  // any exceptions.
  FunctionProtoType::ExtProtoInfo EPI;
  EPI =
      EPI.withExceptionSpec(FunctionProtoType::ExceptionSpecInfo(EST_NoThrow));
  llvm::SmallVector<QualType, 4> ArgsQTys(
      4, Op.Ty->castAs<ComplexType>()->getElementType());
  QualType FQTy = FE.getContext().getFunctionType(Op.Ty, ArgsQTys, EPI);
  const ABIFunctionInfo &FuncInfo = FE.ME.getTypes().arrangeFreeFunctionCall(
      Args, cast<FunctionType>(FQTy.getTypePtr()), false);

  llvm::FunctionType *FTy = FE.ME.getTypes().GetFunctionType(FuncInfo);
  llvm::FunctionCallee Func = FE.ME.createRuntimeFunction(
      FTy, LibCallName, llvm::AttributeList(), true);
  FnCallee Callee = FnCallee::forDirect(Func, FQTy->getAs<FunctionProtoType>());

  llvm::CallBase *Call;
  RValue Res = FE.genCall(FuncInfo, Callee, ReturnValueSlot(), Args, &Call);
  Call->setCallingConv(FE.ME.getRuntimeCC());
  return Res.getComplexVal();
}

namespace {
llvm::StringRef getComplexMultiplyLibCallName(llvm::Type *Ty) {
  switch (Ty->getTypeID()) {
  default:
    llvm_unreachable("Unsupported floating point type!");
  case llvm::Type::HalfTyID:
    return "__mulhc3";
  case llvm::Type::FloatTyID:
    return "__mulsc3";
  case llvm::Type::DoubleTyID:
    return "__muldc3";
  case llvm::Type::X86_FP80TyID:
    return "__mulxc3";
  case llvm::Type::FP128TyID:
    return "__multc3";
  }
}
} // namespace

// See C11 Annex G.5.1 for the semantics of multiplicative operators on complex
// typed values.
ComplexPairTy ComplexExprEmitter::genBinMul(const BinOpInfo &Op) {
  using llvm::Value;
  Value *ResR, *ResI;
  llvm::MDBuilder MDHelper(FE.getLLVMContext());

  if (Op.LHS.first->getType()->isFloatingPointTy()) {
    // The general formulation is:
    // (a + ib) * (c + id) = (a * c - b * d) + i(a * d + b * c)
    //
    // But we can fold away components which would be zero due to a real
    // operand according to C11 Annex G.5.1p2.

    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Op.FPFeatures);
    if (Op.LHS.second && Op.RHS.second) {
      // If both operands are complex, emit the core math directly, and then
      // test for NaNs. If we find NaNs in the result, we delegate to a libcall
      // to carefully re-compute the correct infinity representation if
      // possible. The expectation is that the presence of NaNs here is
      // *extremely* rare, and so the cost of the libcall is almost irrelevant.
      // This is good, because the libcall re-computes the core multiplication
      // exactly the same as we do here and re-tests for NaNs in order to be
      // a generic complex*complex libcall.

      // First compute the four products.
      Value *AC = Builder.CreateFMul(Op.LHS.first, Op.RHS.first, "mul_ac");
      Value *BD = Builder.CreateFMul(Op.LHS.second, Op.RHS.second, "mul_bd");
      Value *AD = Builder.CreateFMul(Op.LHS.first, Op.RHS.second, "mul_ad");
      Value *BC = Builder.CreateFMul(Op.LHS.second, Op.RHS.first, "mul_bc");

      // The real part is the difference of the first two, the imaginary part is
      // the sum of the second.
      ResR = Builder.CreateFSub(AC, BD, "mul_r");
      ResI = Builder.CreateFAdd(AD, BC, "mul_i");

      if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Limited ||
          Op.FPFeatures.getComplexRange() == LangOptions::CX_Improved)
        return ComplexPairTy(ResR, ResI);

      // Emit the test for the real part becoming NaN and create a branch to
      // handle it. We test for NaN by comparing the number to itself.
      Value *IsRNaN = Builder.CreateFCmpUNO(ResR, ResR, "isnan_cmp");
      llvm::BasicBlock *ContBB = FE.createBasicBlock("complex_mul_cont");
      llvm::BasicBlock *INaNBB = FE.createBasicBlock("complex_mul_imag_nan");
      llvm::Instruction *Branch = Builder.CreateCondBr(IsRNaN, INaNBB, ContBB);
      llvm::BasicBlock *OrigBB = Branch->getParent();

      // Give hint that we very much don't expect to see NaNs.
      // Value chosen to match UR_NONTAKEN_WEIGHT, see BranchProbabilityInfo.cpp
      llvm::MDNode *BrWeight = MDHelper.createBranchWeights(1, (1U << 20) - 1);
      Branch->setMetadata(llvm::LLVMContext::MD_prof, BrWeight);

      // Now test the imaginary part and create its branch.
      FE.genBlock(INaNBB);
      Value *IsINaN = Builder.CreateFCmpUNO(ResI, ResI, "isnan_cmp");
      llvm::BasicBlock *LibCallBB = FE.createBasicBlock("complex_mul_libcall");
      Branch = Builder.CreateCondBr(IsINaN, LibCallBB, ContBB);
      Branch->setMetadata(llvm::LLVMContext::MD_prof, BrWeight);

      // Now emit the libcall on this slowest of the slow paths.
      FE.genBlock(LibCallBB);
      Value *LibCallR, *LibCallI;
      std::tie(LibCallR, LibCallI) = genComplexBinOpLibCall(
          getComplexMultiplyLibCallName(Op.LHS.first->getType()), Op);
      Builder.CreateBr(ContBB);

      // Finally continue execution by phi-ing together the different
      // computation paths.
      FE.genBlock(ContBB);
      llvm::PHINode *RealPHI =
          Builder.CreatePHI(ResR->getType(), 3, "real_mul_phi");
      RealPHI->addIncoming(ResR, OrigBB);
      RealPHI->addIncoming(ResR, INaNBB);
      RealPHI->addIncoming(LibCallR, LibCallBB);
      llvm::PHINode *ImagPHI =
          Builder.CreatePHI(ResI->getType(), 3, "imag_mul_phi");
      ImagPHI->addIncoming(ResI, OrigBB);
      ImagPHI->addIncoming(ResI, INaNBB);
      ImagPHI->addIncoming(LibCallI, LibCallBB);
      return ComplexPairTy(RealPHI, ImagPHI);
    }
    assert((Op.LHS.second || Op.RHS.second) &&
           "At least one operand must be complex!");

    // If either of the operands is a real rather than a complex, the
    // imaginary component is ignored when computing the real component of the
    // result.
    ResR = Builder.CreateFMul(Op.LHS.first, Op.RHS.first, "mul.rl");

    ResI = Op.LHS.second
               ? Builder.CreateFMul(Op.LHS.second, Op.RHS.first, "mul.il")
               : Builder.CreateFMul(Op.LHS.first, Op.RHS.second, "mul.ir");
  } else {
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    Value *ResRl = Builder.CreateMul(Op.LHS.first, Op.RHS.first, "mul.rl");
    Value *ResRr = Builder.CreateMul(Op.LHS.second, Op.RHS.second, "mul.rr");
    ResR = Builder.CreateSub(ResRl, ResRr, "mul.r");

    Value *ResIl = Builder.CreateMul(Op.LHS.second, Op.RHS.first, "mul.il");
    Value *ResIr = Builder.CreateMul(Op.LHS.first, Op.RHS.second, "mul.ir");
    ResI = Builder.CreateAdd(ResIl, ResIr, "mul.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy ComplexExprEmitter::genAlgebraicDiv(llvm::Value *LHSr,
                                                  llvm::Value *LHSi,
                                                  llvm::Value *RHSr,
                                                  llvm::Value *RHSi) {
  // (a+ib) / (c+id) = ((ac+bd)/(cc+dd)) + i((bc-ad)/(cc+dd))
  llvm::Value *DSTr, *DSTi;

  llvm::Value *AC = Builder.CreateFMul(LHSr, RHSr); // a*c
  llvm::Value *BD = Builder.CreateFMul(LHSi, RHSi); // b*d
  llvm::Value *ACpBD = Builder.CreateFAdd(AC, BD);  // ac+bd

  llvm::Value *CC = Builder.CreateFMul(RHSr, RHSr); // c*c
  llvm::Value *DD = Builder.CreateFMul(RHSi, RHSi); // d*d
  llvm::Value *CCpDD = Builder.CreateFAdd(CC, DD);  // cc+dd

  llvm::Value *BC = Builder.CreateFMul(LHSi, RHSr); // b*c
  llvm::Value *AD = Builder.CreateFMul(LHSr, RHSi); // a*d
  llvm::Value *BCmAD = Builder.CreateFSub(BC, AD);  // bc-ad

  DSTr = Builder.CreateFDiv(ACpBD, CCpDD);
  DSTi = Builder.CreateFDiv(BCmAD, CCpDD);
  return ComplexPairTy(DSTr, DSTi);
}

namespace {
llvm::Value *emitLlvmFAbs(FunctionEmitter &FE, llvm::Value *Value) {
  llvm::Function *Func =
      FE.ME.getIntrinsic(llvm::Intrinsic::fabs, Value->getType());
  return FE.Builder.CreateCall(Func, Value);
}
} // namespace

// genRangeReductionDiv - Implements Smith's algorithm for complex division.
// SMITH, R. L. Algorithm 116: Complex division. Commun. ACM 5, 8 (1962).
ComplexPairTy ComplexExprEmitter::genRangeReductionDiv(llvm::Value *LHSr,
                                                       llvm::Value *LHSi,
                                                       llvm::Value *RHSr,
                                                       llvm::Value *RHSi) {
  // (a + ib) / (c + id) = (e + if)
  llvm::Value *FAbsRHSr = emitLlvmFAbs(FE, RHSr); // |c|
  llvm::Value *FAbsRHSi = emitLlvmFAbs(FE, RHSi); // |d|
  // |c| >= |d|
  llvm::Value *IsR = Builder.CreateFCmpUGT(FAbsRHSr, FAbsRHSi, "abs_cmp");

  llvm::BasicBlock *TrueBB =
      FE.createBasicBlock("abs_rhsr_greater_or_equal_abs_rhsi");
  llvm::BasicBlock *FalseBB =
      FE.createBasicBlock("abs_rhsr_less_than_abs_rhsi");
  llvm::BasicBlock *ContBB = FE.createBasicBlock("complex_div");
  Builder.CreateCondBr(IsR, TrueBB, FalseBB);

  FE.genBlock(TrueBB);
  // abs(c) >= abs(d)
  // r = d/c
  // tmp = c + rd
  // e = (a + br)/tmp
  // f = (b - ar)/tmp
  llvm::Value *DdC = Builder.CreateFDiv(RHSi, RHSr); // r=d/c

  llvm::Value *RD = Builder.CreateFMul(DdC, RHSi);  // rd
  llvm::Value *CpRD = Builder.CreateFAdd(RHSr, RD); // tmp=c+rd

  llvm::Value *T3 = Builder.CreateFMul(LHSi, DdC);   // br
  llvm::Value *T4 = Builder.CreateFAdd(LHSr, T3);    // a+br
  llvm::Value *DSTTr = Builder.CreateFDiv(T4, CpRD); // (a+br)/tmp

  llvm::Value *T5 = Builder.CreateFMul(LHSr, DdC);   // ar
  llvm::Value *T6 = Builder.CreateFSub(LHSi, T5);    // b-ar
  llvm::Value *DSTTi = Builder.CreateFDiv(T6, CpRD); // (b-ar)/tmp
  Builder.CreateBr(ContBB);

  FE.genBlock(FalseBB);
  // abs(c) < abs(d)
  // r = c/d
  // tmp = d + rc
  // e = (ar + b)/tmp
  // f = (br - a)/tmp
  llvm::Value *CdD = Builder.CreateFDiv(RHSr, RHSi); // r=c/d

  llvm::Value *RC = Builder.CreateFMul(CdD, RHSr);  // rc
  llvm::Value *DpRC = Builder.CreateFAdd(RHSi, RC); // tmp=d+rc

  llvm::Value *T7 = Builder.CreateFMul(LHSr, RC);    // ar
  llvm::Value *T8 = Builder.CreateFAdd(T7, LHSi);    // ar+b
  llvm::Value *DSTFr = Builder.CreateFDiv(T8, DpRC); // (ar+b)/tmp

  llvm::Value *T9 = Builder.CreateFMul(LHSi, CdD);    // br
  llvm::Value *T10 = Builder.CreateFSub(T9, LHSr);    // br-a
  llvm::Value *DSTFi = Builder.CreateFDiv(T10, DpRC); // (br-a)/tmp
  Builder.CreateBr(ContBB);

  // Phi together the computation paths.
  FE.genBlock(ContBB);
  llvm::PHINode *VALr = Builder.CreatePHI(DSTTr->getType(), 2);
  VALr->addIncoming(DSTTr, TrueBB);
  VALr->addIncoming(DSTFr, FalseBB);
  llvm::PHINode *VALi = Builder.CreatePHI(DSTTi->getType(), 2);
  VALi->addIncoming(DSTTi, TrueBB);
  VALi->addIncoming(DSTFi, FalseBB);
  return ComplexPairTy(VALr, VALi);
}

// See C11 Annex G.5.1 for the semantics of multiplicative operators on complex
// typed values.
ComplexPairTy ComplexExprEmitter::genBinDiv(const BinOpInfo &Op) {
  llvm::Value *LHSr = Op.LHS.first, *LHSi = Op.LHS.second;
  llvm::Value *RHSr = Op.RHS.first, *RHSi = Op.RHS.second;
  llvm::Value *DSTr, *DSTi;
  if (LHSr->getType()->isFloatingPointTy()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Op.FPFeatures);
    if (!RHSi) {
      assert(LHSi && "Can have at most one non-complex operand!");

      DSTr = Builder.CreateFDiv(LHSr, RHSr);
      DSTi = Builder.CreateFDiv(LHSi, RHSr);
      return ComplexPairTy(DSTr, DSTi);
    }
    llvm::Value *OrigLHSi = LHSi;
    if (!LHSi)
      LHSi = llvm::Constant::getNullValue(RHSi->getType());
    if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Improved)
      return genRangeReductionDiv(LHSr, LHSi, RHSr, RHSi);
    else if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Limited)
      return genAlgebraicDiv(LHSr, LHSi, RHSr, RHSi);
    else if (!FE.getLangOpts().FastMath) {
      LHSi = OrigLHSi;
      // If we have a complex operand on the RHS and FastMath is not allowed, we
      // delegate to a libcall to handle all of the complexities and minimize
      // underflow/overflow cases. When FastMath is allowed we construct the
      // divide inline using the same algorithm as for integer operands.
      //
      BinOpInfo LibCallOp = Op;
      // If LHS was a real, supply a null imaginary part.
      if (!LHSi)
        LibCallOp.LHS.second = llvm::Constant::getNullValue(LHSr->getType());

      switch (LHSr->getType()->getTypeID()) {
      default:
        llvm_unreachable("Unsupported floating point type!");
      case llvm::Type::HalfTyID:
        return genComplexBinOpLibCall("__divhc3", LibCallOp);
      case llvm::Type::FloatTyID:
        return genComplexBinOpLibCall("__divsc3", LibCallOp);
      case llvm::Type::DoubleTyID:
        return genComplexBinOpLibCall("__divdc3", LibCallOp);
      case llvm::Type::X86_FP80TyID:
        return genComplexBinOpLibCall("__divxc3", LibCallOp);
      case llvm::Type::FP128TyID:
        return genComplexBinOpLibCall("__divtc3", LibCallOp);
      }
    } else {
      return genAlgebraicDiv(LHSr, LHSi, RHSr, RHSi);
    }
  } else {
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    // (a+ib) / (c+id) = ((ac+bd)/(cc+dd)) + i((bc-ad)/(cc+dd))
    llvm::Value *Tmp1 = Builder.CreateMul(LHSr, RHSr); // a*c
    llvm::Value *Tmp2 = Builder.CreateMul(LHSi, RHSi); // b*d
    llvm::Value *Tmp3 = Builder.CreateAdd(Tmp1, Tmp2); // ac+bd

    llvm::Value *Tmp4 = Builder.CreateMul(RHSr, RHSr); // c*c
    llvm::Value *Tmp5 = Builder.CreateMul(RHSi, RHSi); // d*d
    llvm::Value *Tmp6 = Builder.CreateAdd(Tmp4, Tmp5); // cc+dd

    llvm::Value *Tmp7 = Builder.CreateMul(LHSi, RHSr); // b*c
    llvm::Value *Tmp8 = Builder.CreateMul(LHSr, RHSi); // a*d
    llvm::Value *Tmp9 = Builder.CreateSub(Tmp7, Tmp8); // bc-ad

    if (Op.Ty->castAs<ComplexType>()
            ->getElementType()
            ->isUnsignedIntegerType()) {
      DSTr = Builder.CreateUDiv(Tmp3, Tmp6);
      DSTi = Builder.CreateUDiv(Tmp9, Tmp6);
    } else {
      DSTr = Builder.CreateSDiv(Tmp3, Tmp6);
      DSTi = Builder.CreateSDiv(Tmp9, Tmp6);
    }
  }

  return ComplexPairTy(DSTr, DSTi);
}

// ===----------------------------------------------------------------------===
// FunctionEmitter entry points
// ===----------------------------------------------------------------------===

ComplexPairTy FunctionEmitter::genUnPromotedValue(ComplexPairTy result,
                                                  QualType UnPromotionType) {
  llvm::Type *ComplexElementTy =
      convertType(UnPromotionType->castAs<ComplexType>()->getElementType());
  if (result.first)
    result.first =
        Builder.CreateFPTrunc(result.first, ComplexElementTy, "unpromotion");
  if (result.second)
    result.second =
        Builder.CreateFPTrunc(result.second, ComplexElementTy, "unpromotion");
  return result;
}

ComplexPairTy FunctionEmitter::genPromotedValue(ComplexPairTy result,
                                                QualType PromotionType) {
  llvm::Type *ComplexElementTy =
      convertType(PromotionType->castAs<ComplexType>()->getElementType());
  if (result.first)
    result.first = Builder.CreateFPExt(result.first, ComplexElementTy, "ext");
  if (result.second)
    result.second = Builder.CreateFPExt(result.second, ComplexElementTy, "ext");

  return result;
}

ComplexPairTy ComplexExprEmitter::genPromoted(const Expr *E,
                                              QualType PromotionType) {
  E = E->IgnoreParens();
  if (auto BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
#define HANDLE_BINOP(OP)                                                       \
  case BO_##OP:                                                                \
    return genBin##OP(genBinOps(BO, PromotionType));
      HANDLE_BINOP(Add)
      HANDLE_BINOP(Sub)
      HANDLE_BINOP(Mul)
      HANDLE_BINOP(Div)
#undef HANDLE_BINOP
    default:
      break;
    }
  } else if (auto UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    case UO_Minus:
      return VisitMinus(UO, PromotionType);
    case UO_Plus:
      return VisitPlus(UO, PromotionType);
    default:
      break;
    }
  }
  auto result = Visit(const_cast<Expr *>(E));
  if (!PromotionType.isNull())
    return FE.genPromotedValue(result, PromotionType);
  else
    return result;
}

ComplexPairTy FunctionEmitter::genPromotedComplexExpr(const Expr *E,
                                                      QualType DstTy) {
  return ComplexExprEmitter(*this).genPromoted(E, DstTy);
}

ComplexPairTy
ComplexExprEmitter::genPromotedComplexOperand(const Expr *E,
                                              QualType OverallPromotionType) {
  if (const auto *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    auto CK = ICE->getCastKind();
    if (CK == CK_FloatingRealToComplex || CK == CK_IntegralRealToComplex) {
      llvm::Value *Scalar;
      if (!OverallPromotionType.isNull()) {
        QualType EltTy =
            OverallPromotionType->castAs<ComplexType>()->getElementType();
        Scalar = FE.genPromotedScalarExpr(ICE->getSubExpr(), EltTy);
      } else {
        Scalar = FE.genScalarExpr(ICE->getSubExpr());
        QualType DestEltTy =
            ICE->getType()->castAs<ComplexType>()->getElementType();
        Scalar = FE.genScalarConversion(Scalar, ICE->getSubExpr()->getType(),
                                        DestEltTy, ICE->getExprLoc());
      }
      return ComplexPairTy(Scalar, nullptr);
    }
  }
  if (E->getType()->isAnyComplexType()) {
    if (!OverallPromotionType.isNull())
      return FE.genPromotedComplexExpr(E, OverallPromotionType);
    else
      return Visit(const_cast<Expr *>(E));
  } else {
    if (!OverallPromotionType.isNull()) {
      QualType ComplexElementTy =
          OverallPromotionType->castAs<ComplexType>()->getElementType();
      return ComplexPairTy(FE.genPromotedScalarExpr(E, ComplexElementTy),
                           nullptr);
    } else {
      return ComplexPairTy(FE.genScalarExpr(E), nullptr);
    }
  }
}

ComplexExprEmitter::BinOpInfo
ComplexExprEmitter::genBinOps(const BinaryOperator *E, QualType PromotionType) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  BinOpInfo Ops;

  Ops.LHS = genPromotedComplexOperand(E->getLHS(), PromotionType);
  Ops.RHS = genPromotedComplexOperand(E->getRHS(), PromotionType);
  if (!PromotionType.isNull())
    Ops.Ty = PromotionType;
  else
    Ops.Ty = E->getType();
  Ops.FPFeatures = E->getFPFeaturesInEffect(FE.getLangOpts());
  return Ops;
}

LValue ComplexExprEmitter::genCompoundAssignLValue(
    const CompoundAssignOperator *E,
    ComplexPairTy (ComplexExprEmitter::*Func)(const BinOpInfo &), RValue &Val) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  QualType LHSTy = E->getLHS()->getType();
  if (const AtomicType *AT = LHSTy->getAs<AtomicType>())
    LHSTy = AT->getValueType();

  BinOpInfo OpInfo;
  OpInfo.FPFeatures = E->getFPFeaturesInEffect(FE.getLangOpts());
  FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, OpInfo.FPFeatures);

  QualType PromotionTypeCR;
  PromotionTypeCR = getPromotionType(E->getComputationResultType());
  if (PromotionTypeCR.isNull())
    PromotionTypeCR = E->getComputationResultType();
  OpInfo.Ty = PromotionTypeCR;
  QualType ComplexElementTy =
      OpInfo.Ty->castAs<ComplexType>()->getElementType();
  QualType PromotionTypeRHS = getPromotionType(E->getRHS()->getType());

  if (E->getRHS()->getType()->isRealFloatingType()) {
    if (!PromotionTypeRHS.isNull())
      OpInfo.RHS = ComplexPairTy(
          FE.genPromotedScalarExpr(E->getRHS(), PromotionTypeRHS), nullptr);
    else {
      assert(FE.getContext().hasSameUnqualifiedType(ComplexElementTy,
                                                    E->getRHS()->getType()));

      OpInfo.RHS = ComplexPairTy(FE.genScalarExpr(E->getRHS()), nullptr);
    }
  } else {
    if (!PromotionTypeRHS.isNull()) {
      OpInfo.RHS = ComplexPairTy(
          FE.genPromotedComplexExpr(E->getRHS(), PromotionTypeRHS));
    } else {
      assert(FE.getContext().hasSameUnqualifiedType(OpInfo.Ty,
                                                    E->getRHS()->getType()));
      OpInfo.RHS = Visit(E->getRHS());
    }
  }

  LValue LHS = FE.genLValue(E->getLHS());

  // Load from the l-value and convert it.
  SourceLocation Loc = E->getExprLoc();
  QualType PromotionTypeLHS = getPromotionType(E->getComputationLHSType());
  if (LHSTy->isAnyComplexType()) {
    ComplexPairTy LHSVal = genLoadOfLValue(LHS, Loc);
    if (!PromotionTypeLHS.isNull())
      OpInfo.LHS =
          genComplexToComplexCast(LHSVal, LHSTy, PromotionTypeLHS, Loc);
    else
      OpInfo.LHS = genComplexToComplexCast(LHSVal, LHSTy, OpInfo.Ty, Loc);
  } else {
    llvm::Value *LHSVal = FE.genLoadOfScalar(LHS, Loc);
    // For floating point real operands we can directly pass the scalar form
    // to the binary operator emission and potentially get more efficient code.
    if (LHSTy->isRealFloatingType()) {
      QualType PromotedComplexElementTy;
      if (!PromotionTypeLHS.isNull()) {
        PromotedComplexElementTy =
            cast<ComplexType>(PromotionTypeLHS)->getElementType();
        if (!FE.getContext().hasSameUnqualifiedType(PromotedComplexElementTy,
                                                    PromotionTypeLHS))
          LHSVal = FE.genScalarConversion(LHSVal, LHSTy,
                                          PromotedComplexElementTy, Loc);
      } else {
        if (!FE.getContext().hasSameUnqualifiedType(ComplexElementTy, LHSTy))
          LHSVal = FE.genScalarConversion(LHSVal, LHSTy, ComplexElementTy, Loc);
      }
      OpInfo.LHS = ComplexPairTy(LHSVal, nullptr);
    } else {
      OpInfo.LHS = genScalarToComplexCast(LHSVal, LHSTy, OpInfo.Ty, Loc);
    }
  }

  // Expand the binary operator.
  ComplexPairTy Result = (this->*Func)(OpInfo);

  // Truncate the result and store it into the LHS lvalue.
  if (LHSTy->isAnyComplexType()) {
    ComplexPairTy ResVal =
        genComplexToComplexCast(Result, OpInfo.Ty, LHSTy, Loc);
    genStoreOfComplex(ResVal, LHS, /*isInit*/ false);
    Val = RValue::getComplex(ResVal);
  } else {
    llvm::Value *ResVal =
        FE.genComplexToScalarConversion(Result, OpInfo.Ty, LHSTy, Loc);
    FE.genStoreOfScalar(ResVal, LHS, /*isInit*/ false);
    Val = RValue::get(ResVal);
  }

  return LHS;
}

// Compound assignments.
ComplexPairTy ComplexExprEmitter::genCompoundAssign(
    const CompoundAssignOperator *E,
    ComplexPairTy (ComplexExprEmitter::*Func)(const BinOpInfo &)) {
  RValue Val;
  (void)genCompoundAssignLValue(E, Func, Val);

  return Val.getComplexVal();
}

LValue ComplexExprEmitter::genBinAssignLValue(const BinaryOperator *E,
                                              ComplexPairTy &Val) {
  assert(FE.getContext().hasSameUnqualifiedType(E->getLHS()->getType(),
                                                E->getRHS()->getType()) &&
         "Invalid assignment");
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();

  Val = Visit(E->getRHS());
  LValue LHS = FE.genLValue(E->getLHS());
  genStoreOfComplex(Val, LHS, /*isInit*/ false);

  return LHS;
}

ComplexPairTy ComplexExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  ComplexPairTy Val;
  genBinAssignLValue(E, Val);
  return Val;
}

ComplexPairTy ComplexExprEmitter::VisitBinComma(const BinaryOperator *E) {
  FE.genIgnoredExpr(E->getLHS());
  return Visit(E->getRHS());
}

ComplexPairTy ComplexExprEmitter::VisitAbstractConditionalOperator(
    const AbstractConditionalOperator *E) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  llvm::BasicBlock *LHSBlock = FE.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = FE.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = FE.createBasicBlock("cond.end");

  // Bind the common expression if necessary.
  FunctionEmitter::OpaqueValueMapping binding(FE, E);

  FunctionEmitter::ConditionalEvaluation eval(FE);
  FE.genBranchOnBoolExpr(E->getCond(), LHSBlock, RHSBlock);

  eval.begin(FE);
  FE.genBlock(LHSBlock);
  ComplexPairTy LHS = Visit(E->getTrueExpr());
  LHSBlock = Builder.GetInsertBlock();
  FE.genBranch(ContBlock);
  eval.end(FE);

  eval.begin(FE);
  FE.genBlock(RHSBlock);
  ComplexPairTy RHS = Visit(E->getFalseExpr());
  RHSBlock = Builder.GetInsertBlock();
  FE.genBlock(ContBlock);
  eval.end(FE);

  llvm::PHINode *RealPN = Builder.CreatePHI(LHS.first->getType(), 2, "cond.r");
  RealPN->addIncoming(LHS.first, LHSBlock);
  RealPN->addIncoming(RHS.first, RHSBlock);

  llvm::PHINode *ImagPN = Builder.CreatePHI(LHS.first->getType(), 2, "cond.i");
  ImagPN->addIncoming(LHS.second, LHSBlock);
  ImagPN->addIncoming(RHS.second, RHSBlock);

  return ComplexPairTy(RealPN, ImagPN);
}

ComplexPairTy ComplexExprEmitter::VisitChooseExpr(ChooseExpr *E) {
  return Visit(E->getChosenSubExpr());
}

ComplexPairTy ComplexExprEmitter::VisitInitListExpr(InitListExpr *E) {
  bool Ignore = TestAndClearIgnoreReal();
  (void)Ignore;
  assert(Ignore == false && "init list ignored");
  Ignore = TestAndClearIgnoreImag();
  (void)Ignore;
  assert(Ignore == false && "init list ignored");

  if (E->getNumInits() == 2) {
    llvm::Value *Real = FE.genScalarExpr(E->getInit(0));
    llvm::Value *Imag = FE.genScalarExpr(E->getInit(1));
    return ComplexPairTy(Real, Imag);
  } else if (E->getNumInits() == 1) {
    return Visit(E->getInit(0));
  }

  // Empty init list initializes to null
  assert(E->getNumInits() == 0 && "Unexpected number of inits");
  QualType Ty = E->getType()->castAs<ComplexType>()->getElementType();
  llvm::Type *LTy = FE.convertType(Ty);
  llvm::Value *zeroConstant = llvm::Constant::getNullValue(LTy);
  return ComplexPairTy(zeroConstant, zeroConstant);
}

ComplexPairTy ComplexExprEmitter::VisitVAArgExpr(VAArgExpr *E) {
  Address ArgValue = Address::invalid();
  Address ArgPtr = FE.genVAArg(E, ArgValue);

  if (!ArgPtr.isValid()) {
    FE.errorUnsupported(E, "complex va_arg expression");
    llvm::Type *EltTy =
        FE.convertType(E->getType()->castAs<ComplexType>()->getElementType());
    llvm::Value *U = llvm::UndefValue::get(EltTy);
    return ComplexPairTy(U, U);
  }

  return genLoadOfLValue(FE.makeAddrLValue(ArgPtr, E->getType()),
                         E->getExprLoc());
}

ComplexPairTy FunctionEmitter::genComplexExpr(const Expr *E, bool IgnoreReal,
                                              bool IgnoreImag) {
  assert(E && getComplexType(E->getType()) &&
         "Invalid complex expression to emit");

  return ComplexExprEmitter(*this, IgnoreReal, IgnoreImag)
      .Visit(const_cast<Expr *>(E));
}

void FunctionEmitter::genComplexExprIntoLValue(const Expr *E, LValue dest,
                                               bool isInit) {
  assert(E && getComplexType(E->getType()) &&
         "Invalid complex expression to emit");
  ComplexExprEmitter Emitter(*this);
  ComplexPairTy Val = Emitter.Visit(const_cast<Expr *>(E));
  Emitter.genStoreOfComplex(Val, dest, isInit);
}

void FunctionEmitter::genStoreOfComplex(ComplexPairTy V, LValue dest,
                                        bool isInit) {
  ComplexExprEmitter(*this).genStoreOfComplex(V, dest, isInit);
}

ComplexPairTy FunctionEmitter::genLoadOfComplex(LValue src,
                                                SourceLocation loc) {
  return ComplexExprEmitter(*this).genLoadOfLValue(src, loc);
}

LValue FunctionEmitter::genComplexAssignmentLValue(const BinaryOperator *E) {
  assert(E->getOpcode() == BO_Assign);
  ComplexPairTy Val; // ignored
  LValue LVal = ComplexExprEmitter(*this).genBinAssignLValue(E, Val);
  return LVal;
}

typedef ComplexPairTy (ComplexExprEmitter::*CompoundFunc)(
    const ComplexExprEmitter::BinOpInfo &);

namespace {
CompoundFunc getComplexOp(BinaryOperatorKind Op) {
  switch (Op) {
  case BO_MulAssign:
    return &ComplexExprEmitter::genBinMul;
  case BO_DivAssign:
    return &ComplexExprEmitter::genBinDiv;
  case BO_SubAssign:
    return &ComplexExprEmitter::genBinSub;
  case BO_AddAssign:
    return &ComplexExprEmitter::genBinAdd;
  default:
    llvm_unreachable("unexpected complex compound assignment");
  }
}
} // namespace

LValue FunctionEmitter::genComplexCompoundAssignmentLValue(
    const CompoundAssignOperator *E) {
  CompoundFunc Op = getComplexOp(E->getOpcode());
  RValue Val;
  return ComplexExprEmitter(*this).genCompoundAssignLValue(E, Op, Val);
}

LValue FunctionEmitter::genScalarCompoundAssignWithComplex(
    const CompoundAssignOperator *E, llvm::Value *&Result) {
  CompoundFunc Op = getComplexOp(E->getOpcode());
  RValue Val;
  LValue Ret = ComplexExprEmitter(*this).genCompoundAssignLValue(E, Op, Val);
  Result = Val.getScalarVal();
  return Ret;
}
