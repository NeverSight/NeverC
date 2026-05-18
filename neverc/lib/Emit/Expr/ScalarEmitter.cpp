#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FixedPointBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TypeSize.h"
#include <cstdarg>
#include <optional>

using namespace neverc;
using namespace Emit;
using llvm::Value;

// ===----------------------------------------------------------------------===
// ScalarExprEmitter - class definition & helpers
// ===----------------------------------------------------------------------===

namespace {

bool mayHaveIntegerOverflow(llvm::ConstantInt *LHS, llvm::ConstantInt *RHS,
                            BinaryOperator::Opcode Opcode, bool Signed,
                            llvm::APInt &Result) {
  bool Overflow = true;
  const auto &LHSAP = LHS->getValue();
  const auto &RHSAP = RHS->getValue();
  if (Opcode == BO_Add) {
    Result = Signed ? LHSAP.sadd_ov(RHSAP, Overflow)
                    : LHSAP.uadd_ov(RHSAP, Overflow);
  } else if (Opcode == BO_Sub) {
    Result = Signed ? LHSAP.ssub_ov(RHSAP, Overflow)
                    : LHSAP.usub_ov(RHSAP, Overflow);
  } else if (Opcode == BO_Mul) {
    Result = Signed ? LHSAP.smul_ov(RHSAP, Overflow)
                    : LHSAP.umul_ov(RHSAP, Overflow);
  } else if (Opcode == BO_Div || Opcode == BO_Rem) {
    if (Signed && !RHS->isZero())
      Result = LHSAP.sdiv_ov(RHSAP, Overflow);
    else
      return false;
  }
  return Overflow;
}

struct BinOpInfo {
  Value *LHS;
  Value *RHS;
  QualType Ty;                   // Computation Type.
  BinaryOperator::Opcode Opcode; // Opcode of BinOp to perform
  FPOptions FPFeatures;
  const Expr *E; // Entire expr, for error unsupported.  May not be binop.

  bool mayHaveIntegerOverflow() const {
    auto *LHSCI = dyn_cast<llvm::ConstantInt>(LHS);
    auto *RHSCI = dyn_cast<llvm::ConstantInt>(RHS);
    if (!LHSCI || !RHSCI)
      return true;

    llvm::APInt Result;
    return ::mayHaveIntegerOverflow(
        LHSCI, RHSCI, Opcode, Ty->hasSignedIntegerRepresentation(), Result);
  }

  bool isDivremOp() const {
    return Opcode == BO_Div || Opcode == BO_Rem || Opcode == BO_DivAssign ||
           Opcode == BO_RemAssign;
  }

  bool mayHaveIntegerDivisionByZero() const {
    if (isDivremOp())
      if (auto *CI = dyn_cast<llvm::ConstantInt>(RHS))
        return CI->isZero();
    return true;
  }

  bool mayHaveFloatDivisionByZero() const {
    if (isDivremOp())
      if (auto *CFP = dyn_cast<llvm::ConstantFP>(RHS))
        return CFP->isZero();
    return true;
  }

  bool isFixedPointOp() const {
    // We cannot simply check the result type since comparison operations return
    // an int.
    if (const auto *BinOp = dyn_cast<BinaryOperator>(E)) {
      QualType LHSType = BinOp->getLHS()->getType();
      QualType RHSType = BinOp->getRHS()->getType();
      return LHSType->isFixedPointType() || RHSType->isFixedPointType();
    }
    if (const auto *UnOp = dyn_cast<UnaryOperator>(E))
      return UnOp->getSubExpr()->getType()->isFixedPointType();
    return false;
  }
};

bool mustVisitNullValue(const Expr *E) {
  // `nullptr_t` subexpressions may carry side effects; do not skip visitation.
  return E->getType()->isNullPtrType();
}

std::optional<QualType> getUnwidenedIntegerType(const TreeContext &Ctx,
                                                const Expr *E) {
  const Expr *Base = E->IgnoreImpCasts();
  if (E == Base)
    return std::nullopt;

  QualType BaseTy = Base->getType();
  if (!Ctx.isPromotableIntegerType(BaseTy) ||
      Ctx.getTypeSize(BaseTy) >= Ctx.getTypeSize(E->getType()))
    return std::nullopt;

  return BaseTy;
}

bool canSkipOverflowCheck(const TreeContext &Ctx, const BinOpInfo &Op) {
  assert((isa<UnaryOperator>(Op.E) || isa<BinaryOperator>(Op.E)) &&
         "Expected a unary or binary operator");

  // If the binop has constant inputs and we can prove there is no overflow,
  // we can elide the overflow check.
  if (!Op.mayHaveIntegerOverflow())
    return true;

  // If a unary op has a widened operand, the op cannot overflow.
  if (const auto *UO = dyn_cast<UnaryOperator>(Op.E))
    return !UO->canOverflow();

  // We usually don't need overflow checks for binops with widened operands.
  // Multiplication with promoted unsigned operands is a special case.
  const auto *BO = cast<BinaryOperator>(Op.E);
  auto OptionalLHSTy = getUnwidenedIntegerType(Ctx, BO->getLHS());
  if (!OptionalLHSTy)
    return false;

  auto OptionalRHSTy = getUnwidenedIntegerType(Ctx, BO->getRHS());
  if (!OptionalRHSTy)
    return false;

  QualType LHSTy = *OptionalLHSTy;
  QualType RHSTy = *OptionalRHSTy;

  // This is the simple case: binops without unsigned multiplication, and with
  // widened operands. No overflow check is needed here.
  if ((Op.Opcode != BO_Mul && Op.Opcode != BO_MulAssign) ||
      !LHSTy->isUnsignedIntegerType() || !RHSTy->isUnsignedIntegerType())
    return true;

  // For unsigned multiplication the overflow check can be elided if either one
  // of the unpromoted types are less than half the size of the promoted type.
  unsigned PromotedSize = Ctx.getTypeSize(Op.E->getType());
  return (2 * Ctx.getTypeSize(LHSTy)) < PromotedSize ||
         (2 * Ctx.getTypeSize(RHSTy)) < PromotedSize;
}

class ScalarExprEmitter : public StmtVisitor<ScalarExprEmitter, Value *> {
  FunctionEmitter &FE;
  CGBuilderTy &Builder;
  bool IgnoreResultAssign;
  llvm::LLVMContext &VMContext;

public:
  ScalarExprEmitter(FunctionEmitter &cgf, bool ira = false)
      : FE(cgf), Builder(FE.Builder), IgnoreResultAssign(ira),
        VMContext(cgf.getLLVMContext()) {}

  //===--------------------------------------------------------------------===
  //                               Utilities
  //===--------------------------------------------------------------------===

  bool TestAndClearIgnoreResultAssign() {
    bool I = IgnoreResultAssign;
    IgnoreResultAssign = false;
    return I;
  }

  llvm::Type *convertType(QualType T) { return FE.convertType(T); }
  LValue genLValue(const Expr *E) { return FE.genLValue(E); }
  LValue genCheckedLValue(const Expr *E, FunctionEmitter::TypeCheckKind TCK) {
    return FE.genCheckedLValue(E, TCK);
  }

  Value *genLoadOfLValue(LValue LV, SourceLocation Loc) {
    return FE.genLoadOfLValue(LV, Loc).getScalarVal();
  }

  void genLValueAlignmentAssumption(const Expr *E, Value *V) {
    const AlignValueAttr *AVAttr = nullptr;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      const ValueDecl *VD = DRE->getDecl();
      // Assumptions for function parameters are emitted at the start of the
      // function, so there is no need to repeat that here.
      if (isa<ParmVarDecl>(VD))
        return;

      AVAttr = VD->getAttr<AlignValueAttr>();
    }

    if (!AVAttr)
      if (const auto *TTy = E->getType()->getAs<TypedefType>())
        AVAttr = TTy->getDecl()->getAttr<AlignValueAttr>();

    if (!AVAttr)
      return;

    Value *AlignmentValue = FE.genScalarExpr(AVAttr->getAlignment());
    llvm::ConstantInt *AlignmentCI = cast<llvm::ConstantInt>(AlignmentValue);
    FE.emitAlignmentAssumption(V, E, AlignmentCI);
  }

  Value *genLoadOfLValue(const Expr *E) {
    Value *V = genLoadOfLValue(genCheckedLValue(E, FunctionEmitter::TCK_Load),
                               E->getExprLoc());

    genLValueAlignmentAssumption(E, V);
    return V;
  }

  Value *genConversionToBool(Value *Src, QualType DstTy);

  struct ScalarConversionOpts {
    bool TreatBooleanAsSigned;

    ScalarConversionOpts() : TreatBooleanAsSigned(false) {}
  };
  Value *genScalarCast(Value *Src, QualType SrcType, QualType DstType,
                       llvm::Type *SrcTy, llvm::Type *DstTy,
                       ScalarConversionOpts Opts);
  Value *
  genScalarConversion(Value *Src, QualType SrcTy, QualType DstTy,
                      SourceLocation Loc,
                      ScalarConversionOpts Opts = ScalarConversionOpts());

  Value *genFixedPointConversion(Value *Src, QualType SrcTy, QualType DstTy,
                                 SourceLocation Loc);

  Value *genComplexToScalarConversion(FunctionEmitter::ComplexPairTy Src,
                                      QualType SrcTy, QualType DstTy,
                                      SourceLocation Loc);

  Value *genNullValue(QualType Ty);

  Value *genFloatToBoolConversion(Value *V) {
    // Compare against 0.0 for fp scalars.
    llvm::Value *Zero = llvm::Constant::getNullValue(V->getType());
    return Builder.CreateFCmpUNE(V, Zero, "tobool");
  }

  Value *genPointerToBoolConversion(Value *V, QualType QT) {
    Value *Zero =
        FE.ME.getNullPointer(cast<llvm::PointerType>(V->getType()), QT);

    return Builder.CreateICmpNE(V, Zero, "tobool");
  }

  Value *genIntToBoolConversion(Value *V) {
    // Because of the type rules of C, we often end up computing a
    // logical value, then zero extending it to int, then wanting it
    // as a logical value again.  Optimize this common case.
    if (llvm::ZExtInst *ZI = dyn_cast<llvm::ZExtInst>(V)) {
      if (ZI->getOperand(0)->getType() == Builder.getInt1Ty()) {
        Value *Result = ZI->getOperand(0);
        // If there aren't any more uses, zap the instruction to save space.
        // Note that there can be more uses, for example if this
        // is the result of an assignment.
        if (ZI->use_empty())
          ZI->eraseFromParent();
        return Result;
      }
    }

    return Builder.CreateIsNotNull(V, "tobool");
  }

  //===--------------------------------------------------------------------===
  //                            Visitor Methods
  //===--------------------------------------------------------------------===

  Value *Visit(Expr *E) {
    ApplyDebugLocation DL(FE, E);
    return StmtVisitor<ScalarExprEmitter, Value *>::Visit(E);
  }

  Value *VisitStmt(Stmt *S) {
    S->dump(llvm::errs(), FE.getContext());
    llvm_unreachable("Stmt can't have complex result type!");
  }
  Value *VisitExpr(Expr *S);

  Value *VisitConstantExpr(ConstantExpr *E) {
    // A constant expression of type 'void' generates no code and produces no
    // value.
    if (E->getType()->isVoidType())
      return nullptr;

    if (Value *Result = ConstantEmitter(FE).tryEmitConstantExpr(E)) {
      if (E->isLValue())
        return FE.Builder.CreateLoad(
            Address(Result, FE.convertTypeForMem(E->getType()),
                    FE.getContext().getTypeAlignInChars(E->getType())));
      return Result;
    }
    return Visit(E->getSubExpr());
  }
  Value *VisitParenExpr(ParenExpr *PE) { return Visit(PE->getSubExpr()); }
  Value *VisitGenericSelectionExpr(GenericSelectionExpr *GE) {
    return Visit(GE->getResultExpr());
  }

  // Leaves.
  Value *VisitIntegerLiteral(const IntegerLiteral *E) {
    return Builder.getInt(E->getValue());
  }
  Value *VisitFixedPointLiteral(const FixedPointLiteral *E) {
    return Builder.getInt(E->getValue());
  }
  Value *VisitFloatingLiteral(const FloatingLiteral *E) {
    return llvm::ConstantFP::get(VMContext, E->getValue());
  }
  Value *VisitCharacterLiteral(const CharacterLiteral *E) {
    return llvm::ConstantInt::get(convertType(E->getType()), E->getValue());
  }
  Value *VisitOffsetOfExpr(OffsetOfExpr *E);
  Value *VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E);
  Value *VisitAddrLabelExpr(const AddrLabelExpr *E) {
    llvm::Value *V = FE.addrOfLabel(E->getLabel());
    return Builder.CreateBitCast(V, convertType(E->getType()));
  }

  Value *VisitPseudoObjectExpr(PseudoObjectExpr *E) {
    return FE.genPseudoObjectRValue(E).getScalarVal();
  }

  Value *VisitOpaqueValueExpr(OpaqueValueExpr *E) {
    if (E->isLValue())
      return genLoadOfLValue(FE.getOrCreateOpaqueLValueMapping(E),
                             E->getExprLoc());

    // Otherwise, assume the mapping is the scalar directly.
    return FE.getOrCreateOpaqueRValueMapping(E).getScalarVal();
  }

  // l-values.
  Value *VisitDeclRefExpr(DeclRefExpr *E) {
    if (FunctionEmitter::ConstantEmission Constant = FE.tryEmitAsConstant(E))
      return FE.emitScalarConstant(Constant);
    return genLoadOfLValue(E);
  }

  Value *VisitArraySubscriptExpr(ArraySubscriptExpr *E);
  Value *VisitMatrixSubscriptExpr(MatrixSubscriptExpr *E);
  Value *VisitShuffleVectorExpr(ShuffleVectorExpr *E);
  Value *VisitConvertVectorExpr(ConvertVectorExpr *E);
  Value *VisitMemberExpr(MemberExpr *E);
  Value *VisitExtVectorElementExpr(Expr *E) { return genLoadOfLValue(E); }
  Value *VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
    return genLoadOfLValue(E);
  }

  Value *VisitInitListExpr(InitListExpr *E);

  Value *VisitArrayInitIndexExpr(ArrayInitIndexExpr *E) {
    assert(FE.getArrayInitIndex() &&
           "ArrayInitIndexExpr not inside an ArrayInitLoopExpr?");
    return FE.getArrayInitIndex();
  }

  Value *VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E) {
    return genNullValue(E->getType());
  }
  Value *VisitExplicitCastExpr(ExplicitCastExpr *E) {
    FE.ME.genExplicitCastExprType(E, &FE);
    return VisitCastExpr(E);
  }
  Value *VisitCastExpr(CastExpr *E);

  Value *VisitCallExpr(const CallExpr *E) {
    Value *V = FE.genCallExpr(E).getScalarVal();

    genLValueAlignmentAssumption(E, V);
    return V;
  }

  Value *VisitStmtExpr(const StmtExpr *E);

  // Unary Operators.
  Value *VisitUnaryPostDec(const UnaryOperator *E) {
    LValue LV = genLValue(E->getSubExpr());
    return genScalarPrePostIncDec(E, LV, false, false);
  }
  Value *VisitUnaryPostInc(const UnaryOperator *E) {
    LValue LV = genLValue(E->getSubExpr());
    return genScalarPrePostIncDec(E, LV, true, false);
  }
  Value *VisitUnaryPreDec(const UnaryOperator *E) {
    LValue LV = genLValue(E->getSubExpr());
    return genScalarPrePostIncDec(E, LV, false, true);
  }
  Value *VisitUnaryPreInc(const UnaryOperator *E) {
    LValue LV = genLValue(E->getSubExpr());
    return genScalarPrePostIncDec(E, LV, true, true);
  }

  llvm::Value *genIncDecConsiderOverflowBehavior(const UnaryOperator *E,
                                                 llvm::Value *InVal,
                                                 bool IsInc);

  llvm::Value *genScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                                      bool isInc, bool isPre);

  Value *VisitUnaryAddrOf(const UnaryOperator *E) {
    return genLValue(E->getSubExpr()).getPointer(FE);
  }
  Value *VisitUnaryDeref(const UnaryOperator *E) {
    if (E->getType()->isVoidType())
      return Visit(E->getSubExpr()); // the actual value should be unused
    return genLoadOfLValue(E);
  }

  Value *VisitUnaryPlus(const UnaryOperator *E,
                        QualType PromotionType = QualType());
  Value *VisitPlus(const UnaryOperator *E, QualType PromotionType);
  Value *VisitUnaryMinus(const UnaryOperator *E,
                         QualType PromotionType = QualType());
  Value *VisitMinus(const UnaryOperator *E, QualType PromotionType);

  Value *VisitUnaryNot(const UnaryOperator *E);
  Value *VisitUnaryLNot(const UnaryOperator *E);
  Value *VisitUnaryReal(const UnaryOperator *E,
                        QualType PromotionType = QualType());
  Value *VisitReal(const UnaryOperator *E, QualType PromotionType);
  Value *VisitUnaryImag(const UnaryOperator *E,
                        QualType PromotionType = QualType());
  Value *VisitImag(const UnaryOperator *E, QualType PromotionType);
  Value *VisitUnaryExtension(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }

  Value *VisitSourceLocExpr(SourceLocExpr *SLE) {
    auto &Ctx = FE.getContext();
    APValue Evaluated =
        SLE->EvaluateInContext(Ctx, FE.CurSourceLocExprScope.getDefaultExpr());
    return ConstantEmitter(FE).emitAbstract(SLE->getLocation(), Evaluated,
                                            SLE->getType());
  }

  Value *VisitExprWithCleanups(ExprWithCleanups *E);

  Value *VisitNullPtrLiteralExpr(const NullPtrLiteralExpr *E) {
    return genNullValue(E->getType());
  }

  // Binary Operators.
  Value *genMul(const BinOpInfo &Ops) {
    if (Ops.Ty->isSignedIntegerOrEnumerationType()) {
      switch (FE.getLangOpts().getSignedOverflowBehavior()) {
      case LangOptions::SOB_Defined:
        return Builder.CreateMul(Ops.LHS, Ops.RHS, "mul");
      case LangOptions::SOB_Undefined:
        return Builder.CreateNSWMul(Ops.LHS, Ops.RHS, "mul");
      case LangOptions::SOB_Trapping:
        if (canSkipOverflowCheck(FE.getContext(), Ops))
          return Builder.CreateNSWMul(Ops.LHS, Ops.RHS, "mul");
        return genOverflowCheckedBinOp(Ops);
      }
    }

    if (Ops.Ty->isConstantMatrixType()) {
      llvm::MatrixBuilder MB(Builder);
      // We need to check the types of the operands of the operator to get the
      // correct matrix dimensions.
      auto *BO = cast<BinaryOperator>(Ops.E);
      auto *LHSMatTy = dyn_cast<ConstantMatrixType>(
          BO->getLHS()->getType().getCanonicalType());
      auto *RHSMatTy = dyn_cast<ConstantMatrixType>(
          BO->getRHS()->getType().getCanonicalType());
      FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Ops.FPFeatures);
      if (LHSMatTy && RHSMatTy)
        return MB.CreateMatrixMultiply(Ops.LHS, Ops.RHS, LHSMatTy->getNumRows(),
                                       LHSMatTy->getNumColumns(),
                                       RHSMatTy->getNumColumns());
      return MB.CreateScalarMultiply(Ops.LHS, Ops.RHS);
    }

    if (Ops.LHS->getType()->isFPOrFPVectorTy()) {
      //  Preserve the old values
      FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Ops.FPFeatures);
      return Builder.CreateFMul(Ops.LHS, Ops.RHS, "mul");
    }
    if (Ops.isFixedPointOp())
      return genFixedPointBinOp(Ops);
    return Builder.CreateMul(Ops.LHS, Ops.RHS, "mul");
  }
  Value *genOverflowCheckedBinOp(const BinOpInfo &Ops);

  // Common helper for getting how wide LHS of shift is.
  static Value *GetWidthMinusOneValue(Value *LHS, Value *RHS);

  // Mask for powers of 2, URem for non powers of two.
  Value *ConstrainShiftValue(Value *LHS, Value *RHS, const llvm::Twine &Name);

  Value *genDiv(const BinOpInfo &Ops);
  Value *genRem(const BinOpInfo &Ops);
  Value *genAdd(const BinOpInfo &Ops);
  Value *genSub(const BinOpInfo &Ops);
  Value *genShl(const BinOpInfo &Ops);
  Value *genShr(const BinOpInfo &Ops);
  Value *genAnd(const BinOpInfo &Ops) {
    return Builder.CreateAnd(Ops.LHS, Ops.RHS, "and");
  }
  Value *genXor(const BinOpInfo &Ops) {
    return Builder.CreateXor(Ops.LHS, Ops.RHS, "xor");
  }
  Value *genOr(const BinOpInfo &Ops) {
    return Builder.CreateOr(Ops.LHS, Ops.RHS, "or");
  }

  // Helper functions for fixed point binary operations.
  Value *genFixedPointBinOp(const BinOpInfo &Ops);

  BinOpInfo genBinOps(const BinaryOperator *E,
                      QualType PromotionTy = QualType());

  Value *genPromotedValue(Value *result, QualType PromotionType);
  Value *genUnPromotedValue(Value *result, QualType ExprType);
  Value *genPromoted(const Expr *E, QualType PromotionType);

  LValue
  genCompoundAssignLValue(const CompoundAssignOperator *E,
                          Value *(ScalarExprEmitter::*F)(const BinOpInfo &),
                          Value *&Result);

  Value *genCompoundAssign(const CompoundAssignOperator *E,
                           Value *(ScalarExprEmitter::*F)(const BinOpInfo &));

  QualType getPromotionType(QualType Ty) {
    const auto &Ctx = FE.getContext();
    if (auto *CT = Ty->getAs<ComplexType>()) {
      QualType ElementType = CT->getElementType();
      if (ElementType.UseExcessPrecision(Ctx))
        return Ctx.getComplexType(Ctx.FloatTy);
    }

    if (Ty.UseExcessPrecision(Ctx)) {
      if (auto *VT = Ty->getAs<VectorType>()) {
        unsigned NumElements = VT->getNumElements();
        return Ctx.getVectorType(Ctx.FloatTy, NumElements, VT->getVectorKind());
      }
      return Ctx.FloatTy;
    }

    return QualType();
  }

  // Binary operators and binary compound assignment operators.
#define HANDLEBINOP(OP)                                                        \
  Value *VisitBin##OP(const BinaryOperator *E) {                               \
    QualType promotionTy = getPromotionType(E->getType());                     \
    auto result = gen##OP(genBinOps(E, promotionTy));                          \
    if (result && !promotionTy.isNull())                                       \
      result = genUnPromotedValue(result, E->getType());                       \
    return result;                                                             \
  }                                                                            \
  Value *VisitBin##OP##Assign(const CompoundAssignOperator *E) {               \
    return genCompoundAssign(E, &ScalarExprEmitter::gen##OP);                  \
  }
  HANDLEBINOP(Mul)
  HANDLEBINOP(Div)
  HANDLEBINOP(Rem)
  HANDLEBINOP(Add)
  HANDLEBINOP(Sub)
  HANDLEBINOP(Shl)
  HANDLEBINOP(Shr)
  HANDLEBINOP(And)
  HANDLEBINOP(Xor)
  HANDLEBINOP(Or)
#undef HANDLEBINOP

  // Comparisons.
  Value *genCompare(const BinaryOperator *E, llvm::CmpInst::Predicate UICmpOpc,
                    llvm::CmpInst::Predicate SICmpOpc,
                    llvm::CmpInst::Predicate FCmpOpc, bool IsSignaling);
#define VISITCOMP(CODE, UI, SI, FP, SIG)                                       \
  Value *VisitBin##CODE(const BinaryOperator *E) {                             \
    return genCompare(E, llvm::ICmpInst::UI, llvm::ICmpInst::SI,               \
                      llvm::FCmpInst::FP, SIG);                                \
  }
  VISITCOMP(LT, ICMP_ULT, ICMP_SLT, FCMP_OLT, true)
  VISITCOMP(GT, ICMP_UGT, ICMP_SGT, FCMP_OGT, true)
  VISITCOMP(LE, ICMP_ULE, ICMP_SLE, FCMP_OLE, true)
  VISITCOMP(GE, ICMP_UGE, ICMP_SGE, FCMP_OGE, true)
  VISITCOMP(EQ, ICMP_EQ, ICMP_EQ, FCMP_OEQ, false)
  VISITCOMP(NE, ICMP_NE, ICMP_NE, FCMP_UNE, false)
#undef VISITCOMP

  Value *VisitBinAssign(const BinaryOperator *E);

  Value *VisitBinLAnd(const BinaryOperator *E);
  Value *VisitBinLOr(const BinaryOperator *E);
  Value *VisitBinComma(const BinaryOperator *E);

  // Other Operators.
  Value *VisitAbstractConditionalOperator(const AbstractConditionalOperator *);
  Value *VisitChooseExpr(ChooseExpr *CE);
  Value *VisitVAArgExpr(VAArgExpr *VE);
  Value *VisitAtomicExpr(AtomicExpr *AE);
};
} // end anonymous namespace.

// ===----------------------------------------------------------------------===
// Type conversions
// ===----------------------------------------------------------------------===

Value *ScalarExprEmitter::genConversionToBool(Value *Src, QualType SrcType) {
  assert(SrcType.isCanonical() && "genScalarConversion strips typedefs");

  if (SrcType->isRealFloatingType())
    return genFloatToBoolConversion(Src);

  assert((SrcType->isIntegerType() || isa<llvm::PointerType>(Src->getType())) &&
         "Unknown scalar type to convert");

  if (isa<llvm::IntegerType>(Src->getType()))
    return genIntToBoolConversion(Src);

  assert(isa<llvm::PointerType>(Src->getType()));
  return genPointerToBoolConversion(Src, SrcType);
}

Value *ScalarExprEmitter::genScalarCast(Value *Src, QualType SrcType,
                                        QualType DstType, llvm::Type *SrcTy,
                                        llvm::Type *DstTy,
                                        ScalarConversionOpts Opts) {
  // The Element types determine the type of cast to perform.
  llvm::Type *SrcElementTy;
  llvm::Type *DstElementTy;
  QualType SrcElementType;
  QualType DstElementType;
  if (SrcType->isMatrixType() && DstType->isMatrixType()) {
    SrcElementTy = cast<llvm::VectorType>(SrcTy)->getElementType();
    DstElementTy = cast<llvm::VectorType>(DstTy)->getElementType();
    SrcElementType = SrcType->castAs<MatrixType>()->getElementType();
    DstElementType = DstType->castAs<MatrixType>()->getElementType();
  } else {
    assert(!SrcType->isMatrixType() && !DstType->isMatrixType() &&
           "cannot cast between matrix and non-matrix types");
    SrcElementTy = SrcTy;
    DstElementTy = DstTy;
    SrcElementType = SrcType;
    DstElementType = DstType;
  }

  if (isa<llvm::IntegerType>(SrcElementTy)) {
    bool InputSigned = SrcElementType->isSignedIntegerOrEnumerationType();
    if (SrcElementType->isBooleanType() && Opts.TreatBooleanAsSigned) {
      InputSigned = true;
    }

    if (isa<llvm::IntegerType>(DstElementTy))
      return Builder.CreateIntCast(Src, DstTy, InputSigned, "conv");
    if (InputSigned)
      return Builder.CreateSIToFP(Src, DstTy, "conv");
    return Builder.CreateUIToFP(Src, DstTy, "conv");
  }

  if (isa<llvm::IntegerType>(DstElementTy)) {
    assert(SrcElementTy->isFloatingPointTy() && "Unknown real conversion");
    bool IsSigned = DstElementType->isSignedIntegerOrEnumerationType();

    // If we can't recognize overflow as undefined behavior, assume that
    // overflow saturates. This protects against normal optimizations if we are
    // compiling with non-standard FP semantics.
    if (!FE.ME.getCodeGenOpts().StrictFloatCastOverflow) {
      llvm::Intrinsic::ID IID =
          IsSigned ? llvm::Intrinsic::fptosi_sat : llvm::Intrinsic::fptoui_sat;
      return Builder.CreateCall(FE.ME.getIntrinsic(IID, {DstTy, SrcTy}), Src);
    }

    if (IsSigned)
      return Builder.CreateFPToSI(Src, DstTy, "conv");
    return Builder.CreateFPToUI(Src, DstTy, "conv");
  }

  if (DstElementTy->getTypeID() < SrcElementTy->getTypeID())
    return Builder.CreateFPTrunc(Src, DstTy, "conv");
  return Builder.CreateFPExt(Src, DstTy, "conv");
}

Value *ScalarExprEmitter::genScalarConversion(Value *Src, QualType SrcType,
                                              QualType DstType,
                                              SourceLocation Loc,
                                              ScalarConversionOpts Opts) {
  // All conversions involving fixed point types should be handled by the
  // genFixedPoint family functions. This is done to prevent bloating up this
  // function more, and although fixed point numbers are represented by
  // integers, we do not want to follow any logic that assumes they should be
  // treated as integers.
  if (SrcType->isFixedPointType()) {
    if (DstType->isBooleanType())
      // It is important that we check this before checking if the dest type is
      // an integer because booleans are technically integer types.
      // We do not need to check the padding bit on unsigned types if unsigned
      // padding is enabled because overflow into this bit is undefined
      // behavior.
      return Builder.CreateIsNotNull(Src, "tobool");
    if (DstType->isFixedPointType() || DstType->isIntegerType() ||
        DstType->isRealFloatingType())
      return genFixedPointConversion(Src, SrcType, DstType, Loc);

    llvm_unreachable(
        "Unhandled scalar conversion from a fixed point type to another type.");
  } else if (DstType->isFixedPointType()) {
    if (SrcType->isIntegerType() || SrcType->isRealFloatingType())
      // This also includes converting booleans and enums to fixed point types.
      return genFixedPointConversion(Src, SrcType, DstType, Loc);

    llvm_unreachable(
        "Unhandled scalar conversion to a fixed point type from another type.");
  }

  SrcType = FE.getContext().getCanonicalType(SrcType);
  DstType = FE.getContext().getCanonicalType(DstType);
  if (SrcType == DstType)
    return Src;

  if (DstType->isVoidType())
    return nullptr;

  llvm::Value *OrigSrc = Src;
  QualType OrigSrcType = SrcType;
  llvm::Type *SrcTy = Src->getType();

  if (DstType->isBooleanType())
    return genConversionToBool(Src, SrcType);

  llvm::Type *DstTy = convertType(DstType);

  // Cast from half through float if half isn't a native type.
  if (SrcType->isHalfType() && !FE.getContext().getLangOpts().NativeHalfType) {
    // Cast to FP using the intrinsic if the half type itself isn't supported.
    if (DstTy->isFloatingPointTy()) {
      if (FE.getContext().getTargetInfo().useFP16ConversionIntrinsics())
        return Builder.CreateCall(
            FE.ME.getIntrinsic(llvm::Intrinsic::convert_from_fp16, DstTy), Src);
    } else {
      // Cast to other types through float, using either the intrinsic or FPExt,
      // depending on whether the half type itself is supported
      // (as opposed to operations on half, available with NativeHalfType).
      if (FE.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
        Src = Builder.CreateCall(
            FE.ME.getIntrinsic(llvm::Intrinsic::convert_from_fp16,
                               FE.ME.FloatTy),
            Src);
      } else {
        Src = Builder.CreateFPExt(Src, FE.ME.FloatTy, "conv");
      }
      SrcType = FE.getContext().FloatTy;
      SrcTy = FE.FloatTy;
    }
  }

  if (SrcTy == DstTy) {
    return Src;
  }

  if (auto DstPT = dyn_cast<llvm::PointerType>(DstTy)) {
    if (isa<llvm::PointerType>(SrcTy))
      return Builder.CreateBitCast(Src, DstTy, "conv");

    assert(SrcType->isIntegerType() && "Not ptr->ptr or int->ptr conversion?");
    llvm::Type *MiddleTy = FE.ME.getDataLayout().getIntPtrType(DstPT);
    bool InputSigned = SrcType->isSignedIntegerOrEnumerationType();
    llvm::Value *IntResult =
        Builder.CreateIntCast(Src, MiddleTy, InputSigned, "conv");
    // Then, cast to pointer.
    return Builder.CreateIntToPtr(IntResult, DstTy, "conv");
  }

  if (isa<llvm::PointerType>(SrcTy)) {
    // Must be an ptr to int cast.
    assert(isa<llvm::IntegerType>(DstTy) && "not ptr->int?");
    return Builder.CreatePtrToInt(Src, DstTy, "conv");
  }

  // A scalar can be splatted to an extended vector of the same element type
  if (DstType->isExtVectorType() && !SrcType->isVectorType()) {
    // Sema should add casts to make sure that the source expression's type is
    // the same as the vector's element type (sans qualifiers)
    assert(DstType->castAs<ExtVectorType>()->getElementType().getTypePtr() ==
               SrcType.getTypePtr() &&
           "Splatted expr doesn't match with vector element type?");

    // Splat the element across to all elements
    unsigned NumElements = cast<llvm::FixedVectorType>(DstTy)->getNumElements();
    return Builder.CreateVectorSplat(NumElements, Src, "splat");
  }

  if (SrcType->isMatrixType() && DstType->isMatrixType())
    return genScalarCast(Src, SrcType, DstType, SrcTy, DstTy, Opts);

  if (isa<llvm::VectorType>(SrcTy) || isa<llvm::VectorType>(DstTy)) {
    // Allow bitcast from vector to integer/fp of the same size.
    llvm::TypeSize SrcSize = SrcTy->getPrimitiveSizeInBits();
    llvm::TypeSize DstSize = DstTy->getPrimitiveSizeInBits();
    if (SrcSize == DstSize)
      return Builder.CreateBitCast(Src, DstTy, "conv");

    // Conversions between vectors of different sizes are not allowed except
    // when vectors of half are involved. Operations on storage-only half
    // vectors require promoting half vector operands to float vectors and
    // truncating the result, which is either an int or float vector, to a
    // short or half vector.

    // Source and destination are both expected to be vectors.
    llvm::Type *SrcElementTy = cast<llvm::VectorType>(SrcTy)->getElementType();
    llvm::Type *DstElementTy = cast<llvm::VectorType>(DstTy)->getElementType();
    (void)DstElementTy;

    assert(((SrcElementTy->isIntegerTy() && DstElementTy->isIntegerTy()) ||
            (SrcElementTy->isFloatingPointTy() &&
             DstElementTy->isFloatingPointTy())) &&
           "unexpected conversion between a floating-point vector and an "
           "integer vector");

    // Truncate an i32 vector to an i16 vector.
    if (SrcElementTy->isIntegerTy())
      return Builder.CreateIntCast(Src, DstTy, false, "conv");

    // Truncate a float vector to a half vector.
    if (SrcSize > DstSize)
      return Builder.CreateFPTrunc(Src, DstTy, "conv");

    // Promote a half vector to a float vector.
    return Builder.CreateFPExt(Src, DstTy, "conv");
  }

  // Finally, we have the arithmetic types: real int/float.
  Value *Res = nullptr;
  llvm::Type *ResTy = DstTy;

  // An overflowing conversion has undefined behavior if either the source type
  // or the destination type is a floating-point type. However, we consider the
  // Cast to half through float if half isn't a native type.
  if (DstType->isHalfType() && !FE.getContext().getLangOpts().NativeHalfType) {
    // Make sure we cast in a single step if from another FP type.
    if (SrcTy->isFloatingPointTy()) {
      // Use the intrinsic if the half type itself isn't supported
      // (as opposed to operations on half, available with NativeHalfType).
      if (FE.getContext().getTargetInfo().useFP16ConversionIntrinsics())
        return Builder.CreateCall(
            FE.ME.getIntrinsic(llvm::Intrinsic::convert_to_fp16, SrcTy), Src);
      // If the half type is supported, just use an fptrunc.
      return Builder.CreateFPTrunc(Src, DstTy);
    }
    DstTy = FE.FloatTy;
  }

  Res = genScalarCast(Src, SrcType, DstType, SrcTy, DstTy, Opts);

  if (DstTy != ResTy) {
    if (FE.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
      assert(ResTy->isIntegerTy(16) &&
             "Only half FP requires extra conversion");
      Res = Builder.CreateCall(
          FE.ME.getIntrinsic(llvm::Intrinsic::convert_to_fp16, FE.ME.FloatTy),
          Res);
    } else {
      Res = Builder.CreateFPTrunc(Res, ResTy, "conv");
    }
  }

  return Res;
}

Value *ScalarExprEmitter::genFixedPointConversion(Value *Src, QualType SrcTy,
                                                  QualType DstTy,
                                                  SourceLocation Loc) {
  llvm::FixedPointBuilder<CGBuilderTy> FPBuilder(Builder);
  llvm::Value *Result;
  if (SrcTy->isRealFloatingType())
    Result = FPBuilder.CreateFloatingToFixed(
        Src, FE.getContext().getFixedPointSemantics(DstTy));
  else if (DstTy->isRealFloatingType())
    Result = FPBuilder.CreateFixedToFloating(
        Src, FE.getContext().getFixedPointSemantics(SrcTy), convertType(DstTy));
  else {
    auto SrcFPSema = FE.getContext().getFixedPointSemantics(SrcTy);
    auto DstFPSema = FE.getContext().getFixedPointSemantics(DstTy);

    if (DstTy->isIntegerType())
      Result = FPBuilder.CreateFixedToInteger(
          Src, SrcFPSema, DstFPSema.getWidth(), DstFPSema.isSigned());
    else if (SrcTy->isIntegerType())
      Result =
          FPBuilder.CreateIntegerToFixed(Src, SrcFPSema.isSigned(), DstFPSema);
    else
      Result = FPBuilder.CreateFixedToFixed(Src, SrcFPSema, DstFPSema);
  }
  return Result;
}

Value *ScalarExprEmitter::genComplexToScalarConversion(
    FunctionEmitter::ComplexPairTy Src, QualType SrcTy, QualType DstTy,
    SourceLocation Loc) {
  SrcTy = SrcTy->castAs<ComplexType>()->getElementType();

  // Handle conversions to bool first, they are special: comparisons against 0.
  if (DstTy->isBooleanType()) {
    //  Complex != 0  -> (Real != 0) | (Imag != 0)
    Src.first = genScalarConversion(Src.first, SrcTy, DstTy, Loc);
    Src.second = genScalarConversion(Src.second, SrcTy, DstTy, Loc);
    return Builder.CreateOr(Src.first, Src.second, "tobool");
  }

  // Complex-to-real: discard imaginary part, convert real part.
  return genScalarConversion(Src.first, SrcTy, DstTy, Loc);
}

Value *ScalarExprEmitter::genNullValue(QualType Ty) {
  return FE.genFromMemory(FE.ME.genNullConstant(Ty), Ty);
}

Value *ScalarExprEmitter::VisitExpr(Expr *E) {
  FE.errorUnsupported(E, "scalar expression");
  if (E->getType()->isVoidType())
    return nullptr;
  return llvm::UndefValue::get(FE.convertType(E->getType()));
}

Value *ScalarExprEmitter::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  // Vector Mask Case
  if (E->getNumSubExprs() == 2) {
    Value *LHS = FE.genScalarExpr(E->getExpr(0));
    Value *RHS = FE.genScalarExpr(E->getExpr(1));
    Value *Mask;

    auto *LTy = cast<llvm::FixedVectorType>(LHS->getType());
    unsigned LHSElts = LTy->getNumElements();

    Mask = RHS;

    auto *MTy = cast<llvm::FixedVectorType>(Mask->getType());

    // Mask off the high bits of each shuffle index.
    Value *MaskBits =
        llvm::ConstantInt::get(MTy, llvm::NextPowerOf2(LHSElts - 1) - 1);
    Mask = Builder.CreateAnd(Mask, MaskBits, "mask");

    // newv = undef
    // mask = mask & maskbits
    // for each elt
    //   n = extract mask i
    //   x = extract val n
    //   newv = insert newv, x, i
    auto *RTy = llvm::FixedVectorType::get(LTy->getElementType(),
                                           MTy->getNumElements());
    Value *NewV = llvm::PoisonValue::get(RTy);
    for (unsigned i = 0, e = MTy->getNumElements(); i != e; ++i) {
      Value *IIndx = llvm::ConstantInt::get(FE.SizeTy, i);
      Value *Indx = Builder.CreateExtractElement(Mask, IIndx, "shuf_idx");

      Value *VExt = Builder.CreateExtractElement(LHS, Indx, "shuf_elt");
      NewV = Builder.CreateInsertElement(NewV, VExt, IIndx, "shuf_ins");
    }
    return NewV;
  }

  Value *V1 = FE.genScalarExpr(E->getExpr(0));
  Value *V2 = FE.genScalarExpr(E->getExpr(1));

  llvm::SmallVector<int, 32> Indices;
  for (unsigned i = 2; i < E->getNumSubExprs(); ++i) {
    llvm::APSInt Idx = E->getShuffleMaskIdx(FE.getContext(), i - 2);
    // Check for -1 and output it as undef in the IR.
    if (Idx.isSigned() && Idx.isAllOnes())
      Indices.push_back(-1);
    else
      Indices.push_back(Idx.getZExtValue());
  }

  return Builder.CreateShuffleVector(V1, V2, Indices, "shuffle");
}

Value *ScalarExprEmitter::VisitConvertVectorExpr(ConvertVectorExpr *E) {
  QualType SrcType = E->getSrcExpr()->getType(), DstType = E->getType();

  Value *Src = FE.genScalarExpr(E->getSrcExpr());

  SrcType = FE.getContext().getCanonicalType(SrcType);
  DstType = FE.getContext().getCanonicalType(DstType);
  if (SrcType == DstType)
    return Src;

  assert(SrcType->isVectorType() &&
         "ConvertVector source type must be a vector");
  assert(DstType->isVectorType() &&
         "ConvertVector destination type must be a vector");

  llvm::Type *SrcTy = Src->getType();
  llvm::Type *DstTy = convertType(DstType);

  // Ignore conversions like int -> uint.
  if (SrcTy == DstTy)
    return Src;

  QualType SrcEltType = SrcType->castAs<VectorType>()->getElementType(),
           DstEltType = DstType->castAs<VectorType>()->getElementType();

  assert(SrcTy->isVectorTy() &&
         "ConvertVector source IR type must be a vector");
  assert(DstTy->isVectorTy() &&
         "ConvertVector destination IR type must be a vector");

  llvm::Type *SrcEltTy = cast<llvm::VectorType>(SrcTy)->getElementType(),
             *DstEltTy = cast<llvm::VectorType>(DstTy)->getElementType();

  if (DstEltType->isBooleanType()) {
    assert(
        (SrcEltTy->isFloatingPointTy() || isa<llvm::IntegerType>(SrcEltTy)) &&
        "Unknown boolean conversion");

    llvm::Value *Zero = llvm::Constant::getNullValue(SrcTy);
    if (SrcEltTy->isFloatingPointTy()) {
      return Builder.CreateFCmpUNE(Src, Zero, "tobool");
    } else {
      return Builder.CreateICmpNE(Src, Zero, "tobool");
    }
  }

  // We have the arithmetic types: real int/float.
  Value *Res = nullptr;

  if (isa<llvm::IntegerType>(SrcEltTy)) {
    bool InputSigned = SrcEltType->isSignedIntegerOrEnumerationType();
    if (isa<llvm::IntegerType>(DstEltTy))
      Res = Builder.CreateIntCast(Src, DstTy, InputSigned, "conv");
    else if (InputSigned)
      Res = Builder.CreateSIToFP(Src, DstTy, "conv");
    else
      Res = Builder.CreateUIToFP(Src, DstTy, "conv");
  } else if (isa<llvm::IntegerType>(DstEltTy)) {
    assert(SrcEltTy->isFloatingPointTy() && "Unknown real conversion");
    if (DstEltType->isSignedIntegerOrEnumerationType())
      Res = Builder.CreateFPToSI(Src, DstTy, "conv");
    else
      Res = Builder.CreateFPToUI(Src, DstTy, "conv");
  } else {
    assert(SrcEltTy->isFloatingPointTy() && DstEltTy->isFloatingPointTy() &&
           "Unknown real conversion");
    if (DstEltTy->getTypeID() < SrcEltTy->getTypeID())
      Res = Builder.CreateFPTrunc(Src, DstTy, "conv");
    else
      Res = Builder.CreateFPExt(Src, DstTy, "conv");
  }

  return Res;
}

Value *ScalarExprEmitter::VisitMemberExpr(MemberExpr *E) {
  if (FunctionEmitter::ConstantEmission Constant = FE.tryEmitAsConstant(E)) {
    FE.genIgnoredExpr(E->getBase());
    return FE.emitScalarConstant(Constant);
  } else {
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, FE.getContext(), Expr::SE_AllowSideEffects)) {
      llvm::APSInt Value = Result.Val.getInt();
      FE.genIgnoredExpr(E->getBase());
      return Builder.getInt(Value);
    }
  }

  return genLoadOfLValue(E);
}

Value *ScalarExprEmitter::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  TestAndClearIgnoreResultAssign();

  // Vector subscript bases are occasionally rvalues, so we can't always form
  // an lvalue. Handle them separately below.
  if (!E->getBase()->getType()->isVectorType() &&
      !E->getBase()->getType()->isSveVLSBuiltinType())
    return genLoadOfLValue(E);

  // Handle the vector case.  The base must be a vector, the index must be an
  // integer value.
  Value *Base = Visit(E->getBase());
  Value *Idx = Visit(E->getIdx());

  return Builder.CreateExtractElement(Base, Idx, "vecext");
}

Value *ScalarExprEmitter::VisitMatrixSubscriptExpr(MatrixSubscriptExpr *E) {
  TestAndClearIgnoreResultAssign();

  Value *RowIdx = Visit(E->getRowIdx());
  Value *ColumnIdx = Visit(E->getColumnIdx());

  const auto *MatrixTy = E->getBase()->getType()->castAs<ConstantMatrixType>();
  unsigned NumRows = MatrixTy->getNumRows();
  llvm::MatrixBuilder MB(Builder);
  Value *Idx = MB.CreateIndex(RowIdx, ColumnIdx, NumRows);
  if (FE.ME.getCodeGenOpts().OptimizationLevel > 0)
    MB.CreateIndexAssumption(Idx, MatrixTy->getNumElementsFlattened());

  Value *Matrix = Visit(E->getBase());

  return Builder.CreateExtractElement(Matrix, Idx, "matrixext");
}

namespace {
int getMaskElt(llvm::ShuffleVectorInst *SVI, unsigned Idx, unsigned Off) {
  int MV = SVI->getMaskValue(Idx);
  if (MV == -1)
    return -1;
  return Off + MV;
}

int getAsInt32(llvm::ConstantInt *C, llvm::Type *I32Ty) {
  assert(llvm::ConstantInt::isValueValidForType(I32Ty, C->getZExtValue()) &&
         "Index operand too large for shufflevector mask!");
  return C->getZExtValue();
}
} // namespace

// ===----------------------------------------------------------------------===
// Vector & matrix operations
// ===----------------------------------------------------------------------===

Value *ScalarExprEmitter::VisitInitListExpr(InitListExpr *E) {
  bool Ignore = TestAndClearIgnoreResultAssign();
  (void)Ignore;
  assert(Ignore == false && "init list ignored");
  unsigned NumInitElements = E->getNumInits();

  if (E->hadArrayRangeDesignator())
    FE.errorUnsupported(E, "GNU array range designator extension");

  llvm::VectorType *VType =
      dyn_cast<llvm::VectorType>(convertType(E->getType()));

  if (!VType) {
    if (NumInitElements == 0) {
      return genNullValue(E->getType());
    }
    // We have a scalar in braces. Just use the first element.
    return Visit(E->getInit(0));
  }

  if (isa<llvm::ScalableVectorType>(VType)) {
    if (NumInitElements == 0) {
      return genNullValue(E->getType());
    }

    if (NumInitElements == 1) {
      Expr *InitVector = E->getInit(0);

      if (InitVector->getType() == E->getType())
        return Visit(InitVector);
    }

    llvm_unreachable("Unexpected initialization of a scalable vector!");
  }

  unsigned ResElts = cast<llvm::FixedVectorType>(VType)->getNumElements();

  // Loop over initializers collecting the Value for each, and remembering
  // whether the source was swizzle (ExtVectorElementExpr).  This will allow
  // us to fold the shuffle for the swizzle into the shuffle for the vector
  // initializer, since LLVM optimizers generally do not want to touch
  // shuffles.
  unsigned CurIdx = 0;
  bool VIsPoisonShuffle = false;
  llvm::Value *V = llvm::PoisonValue::get(VType);
  for (unsigned i = 0; i != NumInitElements; ++i) {
    Expr *IE = E->getInit(i);
    Value *Init = Visit(IE);
    llvm::SmallVector<int, 16> Args;

    llvm::VectorType *VVT = dyn_cast<llvm::VectorType>(Init->getType());

    // Handle scalar elements.  If the scalar initializer is actually one
    // element of a different vector of the same width, use shuffle instead of
    // extract+insert.
    if (!VVT) {
      if (isa<ExtVectorElementExpr>(IE)) {
        llvm::ExtractElementInst *EI = cast<llvm::ExtractElementInst>(Init);

        if (cast<llvm::FixedVectorType>(EI->getVectorOperandType())
                ->getNumElements() == ResElts) {
          llvm::ConstantInt *C = cast<llvm::ConstantInt>(EI->getIndexOperand());
          Value *LHS = nullptr, *RHS = nullptr;
          if (CurIdx == 0) {
            // insert into poison -> shuffle (src, poison)
            // shufflemask must use an i32
            Args.push_back(getAsInt32(C, FE.Int32Ty));
            Args.resize(ResElts, -1);

            LHS = EI->getVectorOperand();
            RHS = V;
            VIsPoisonShuffle = true;
          } else if (VIsPoisonShuffle) {
            // insert into poison shuffle && size match -> shuffle (v, src)
            llvm::ShuffleVectorInst *SVV = cast<llvm::ShuffleVectorInst>(V);
            for (unsigned j = 0; j != CurIdx; ++j)
              Args.push_back(getMaskElt(SVV, j, 0));
            Args.push_back(ResElts + C->getZExtValue());
            Args.resize(ResElts, -1);

            LHS = cast<llvm::ShuffleVectorInst>(V)->getOperand(0);
            RHS = EI->getVectorOperand();
            VIsPoisonShuffle = false;
          }
          if (!Args.empty()) {
            V = Builder.CreateShuffleVector(LHS, RHS, Args);
            ++CurIdx;
            continue;
          }
        }
      }
      V = Builder.CreateInsertElement(V, Init, Builder.getInt32(CurIdx),
                                      "vecinit");
      VIsPoisonShuffle = false;
      ++CurIdx;
      continue;
    }

    unsigned InitElts = cast<llvm::FixedVectorType>(VVT)->getNumElements();

    // If the initializer is an ExtVecEltExpr (a swizzle), and the swizzle's
    // input is the same width as the vector being constructed, generate an
    // optimized shuffle of the swizzle input into the result.
    unsigned Offset = (CurIdx == 0) ? 0 : ResElts;
    if (isa<ExtVectorElementExpr>(IE)) {
      llvm::ShuffleVectorInst *SVI = cast<llvm::ShuffleVectorInst>(Init);
      Value *SVOp = SVI->getOperand(0);
      auto *OpTy = cast<llvm::FixedVectorType>(SVOp->getType());

      if (OpTy->getNumElements() == ResElts) {
        for (unsigned j = 0; j != CurIdx; ++j) {
          // If the current vector initializer is a shuffle with poison, merge
          // this shuffle directly into it.
          if (VIsPoisonShuffle) {
            Args.push_back(getMaskElt(cast<llvm::ShuffleVectorInst>(V), j, 0));
          } else {
            Args.push_back(j);
          }
        }
        for (unsigned j = 0, je = InitElts; j != je; ++j)
          Args.push_back(getMaskElt(SVI, j, Offset));
        Args.resize(ResElts, -1);

        if (VIsPoisonShuffle)
          V = cast<llvm::ShuffleVectorInst>(V)->getOperand(0);

        Init = SVOp;
      }
    }

    // Extend init to result vector length, and then shuffle its contribution
    // to the vector initializer into V.
    if (Args.empty()) {
      for (unsigned j = 0; j != InitElts; ++j)
        Args.push_back(j);
      Args.resize(ResElts, -1);
      Init = Builder.CreateShuffleVector(Init, Args, "vext");

      Args.clear();
      for (unsigned j = 0; j != CurIdx; ++j)
        Args.push_back(j);
      for (unsigned j = 0; j != InitElts; ++j)
        Args.push_back(j + Offset);
      Args.resize(ResElts, -1);
    }

    // If V is poison, make sure it ends up on the RHS of the shuffle to aid
    // merging subsequent shuffles into this one.
    if (CurIdx == 0)
      std::swap(V, Init);
    V = Builder.CreateShuffleVector(V, Init, Args, "vecinit");
    VIsPoisonShuffle = isa<llvm::PoisonValue>(Init);
    CurIdx += InitElts;
  }

  llvm::Type *EltTy = VType->getElementType();

  for (/* Do not initialize i*/; CurIdx < ResElts; ++CurIdx) {
    Value *Idx = Builder.getInt32(CurIdx);
    llvm::Value *Init = llvm::Constant::getNullValue(EltTy);
    V = Builder.CreateInsertElement(V, Init, Idx, "vecinit");
  }
  return V;
}

// ===----------------------------------------------------------------------===
// Cast expressions
// ===----------------------------------------------------------------------===

__attribute__((hot, flatten)) Value *
ScalarExprEmitter::VisitCastExpr(CastExpr *CE) {
  Expr *E = CE->getSubExpr();
  QualType DestTy = CE->getType();
  CastKind Kind = CE->getCastKind();

  if (LLVM_LIKELY(Kind == CK_NoOp || Kind == CK_LValueToRValue)) {
    FunctionEmitter::FPOptionsRAII FPOptions(FE, CE);
    TestAndClearIgnoreResultAssign();
    if (Kind == CK_LValueToRValue) {
      assert(E->isLValue() && "lvalue-to-rvalue applied to r-value!");
      return Visit(const_cast<Expr *>(E));
    }
    return CE->changesVolatileQualification() ? genLoadOfLValue(CE)
                                              : Visit(const_cast<Expr *>(E));
  }

  FunctionEmitter::FPOptionsRAII FPOptions(FE, CE);
  TestAndClearIgnoreResultAssign();

  switch (Kind) {
  case CK_Dependent:
    llvm_unreachable("dependent cast kind in IR gen!");
  case CK_BuiltinFnToFnPtr:
    llvm_unreachable("builtin functions are handled elsewhere");

  case CK_LValueBitCast: {
    Address Addr = genLValue(E).getAddress(FE);
    Addr = Addr.withElementType(FE.convertTypeForMem(DestTy));
    LValue LV = FE.makeAddrLValue(Addr, DestTy);
    return genLoadOfLValue(LV, CE->getExprLoc());
  }

  case CK_LValueToRValueBitCast: {
    LValue SourceLVal = FE.genLValue(E);
    Address Addr =
        SourceLVal.getAddress(FE).withElementType(FE.convertTypeForMem(DestTy));
    LValue DestLV = FE.makeAddrLValue(Addr, DestTy);
    DestLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
    return genLoadOfLValue(DestLV, CE->getExprLoc());
  }

  case CK_BitCast: {
    Value *Src = Visit(const_cast<Expr *>(E));
    llvm::Type *SrcTy = Src->getType();
    llvm::Type *DstTy = convertType(DestTy);
    assert(
        (!SrcTy->isPtrOrPtrVectorTy() || !DstTy->isPtrOrPtrVectorTy() ||
         SrcTy->getPointerAddressSpace() == DstTy->getPointerAddressSpace()) &&
        "Address-space cast must be used to convert address spaces");

    if (auto *CI = dyn_cast<llvm::CallBase>(Src)) {
      if (CI->getMetadata("heapallocsite") && isa<ExplicitCastExpr>(CE) &&
          !isa<CastExpr>(E)) {
        QualType PointeeType = DestTy->getPointeeType();
        if (!PointeeType.isNull())
          FE.getDebugInfo()->addHeapAllocSiteMetadata(CI, PointeeType,
                                                      CE->getExprLoc());
      }
    }

    // Fixed -> scalable vector: use llvm.vector.insert.
    if (const auto *FixedSrc = dyn_cast<llvm::FixedVectorType>(SrcTy)) {
      if (const auto *ScalableDst = dyn_cast<llvm::ScalableVectorType>(DstTy)) {
        bool NeedsBitCast = false;
        auto PredType = llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
        llvm::Type *OrigType = DstTy;
        if (ScalableDst == PredType &&
            FixedSrc->getElementType() == Builder.getInt8Ty()) {
          DstTy = llvm::ScalableVectorType::get(Builder.getInt8Ty(), 2);
          ScalableDst = cast<llvm::ScalableVectorType>(DstTy);
          NeedsBitCast = true;
        }
        if (FixedSrc->getElementType() == ScalableDst->getElementType()) {
          llvm::Value *UndefVec = llvm::UndefValue::get(DstTy);
          llvm::Value *Zero = llvm::Constant::getNullValue(FE.ME.Int64Ty);
          llvm::Value *Result = Builder.CreateInsertVector(
              DstTy, UndefVec, Src, Zero, "cast.scalable");
          if (NeedsBitCast)
            Result = Builder.CreateBitCast(Result, OrigType);
          return Result;
        }
      }
    }

    // Scalable -> fixed vector: use llvm.vector.extract.
    if (const auto *ScalableSrc = dyn_cast<llvm::ScalableVectorType>(SrcTy)) {
      if (const auto *FixedDst = dyn_cast<llvm::FixedVectorType>(DstTy)) {
        auto PredType = llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
        if (ScalableSrc == PredType &&
            FixedDst->getElementType() == Builder.getInt8Ty()) {
          SrcTy = llvm::ScalableVectorType::get(Builder.getInt8Ty(), 2);
          ScalableSrc = cast<llvm::ScalableVectorType>(SrcTy);
          Src = Builder.CreateBitCast(Src, SrcTy);
        }
        if (ScalableSrc->getElementType() == FixedDst->getElementType()) {
          llvm::Value *Zero = llvm::Constant::getNullValue(FE.ME.Int64Ty);
          return Builder.CreateExtractVector(DstTy, Src, Zero, "cast.fixed");
        }
      }
    }

    // VLAT <-> VLST: round-trip through memory.
    if ((isa<llvm::FixedVectorType>(SrcTy) &&
         isa<llvm::ScalableVectorType>(DstTy)) ||
        (isa<llvm::ScalableVectorType>(SrcTy) &&
         isa<llvm::FixedVectorType>(DstTy))) {
      Address Addr = FE.createDefaultAlignTempAlloca(SrcTy, "saved-value");
      LValue LV = FE.makeAddrLValue(Addr, E->getType());
      FE.genStoreOfScalar(Src, LV);
      Addr = Addr.withElementType(FE.convertTypeForMem(DestTy));
      LValue DestLV = FE.makeAddrLValue(Addr, DestTy);
      DestLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
      return genLoadOfLValue(DestLV, CE->getExprLoc());
    }
    return Builder.CreateBitCast(Src, DstTy);
  }
  case CK_AddressSpaceConversion: {
    Expr::EvalResult Result;
    if (E->EvaluateAsRValue(Result, FE.getContext()) &&
        Result.Val.isNullPointer()) {
      // If E has side effect, it is emitted even if its final result is a
      // null pointer. In that case, a DCE pass should be able to
      // eliminate the useless instructions emitted during translating E.
      if (Result.HasSideEffects)
        Visit(E);
      return FE.ME.getNullPointer(cast<llvm::PointerType>(convertType(DestTy)),
                                  DestTy);
    }
    // Since target may map different address spaces in AST to the same address
    // space, an address space conversion may end up as a bitcast.
    return FE.ME.getTargetCodeGenInfo().performAddrSpaceCast(
        FE, Visit(E), E->getType()->getPointeeType().getAddressSpace(),
        DestTy->getPointeeType().getAddressSpace(), convertType(DestTy));
  }
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
    return Visit(const_cast<Expr *>(E));

  case CK_NoOp: {
    return CE->changesVolatileQualification() ? genLoadOfLValue(CE)
                                              : Visit(const_cast<Expr *>(E));
  }

  case CK_ArrayToPointerDecay:
    return FE.genArrayToPointerDecay(E).getPointer();
  case CK_FunctionToPointerDecay:
    return genLValue(E).getPointer(FE);

  case CK_NullToPointer:
    if (mustVisitNullValue(E))
      FE.genIgnoredExpr(E);

    return FE.ME.getNullPointer(cast<llvm::PointerType>(convertType(DestTy)),
                                DestTy);

  case CK_FloatingRealToComplex:
  case CK_FloatingComplexCast:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_FloatingComplexToIntegralComplex:
  case CK_ToUnion:
    llvm_unreachable("scalar cast to non-scalar value");

  case CK_LValueToRValue:
    assert(FE.getContext().hasSameUnqualifiedType(E->getType(), DestTy));
    assert(E->isLValue() && "lvalue-to-rvalue applied to r-value!");
    return Visit(const_cast<Expr *>(E));

  case CK_IntegralToPointer: {
    Value *Src = Visit(const_cast<Expr *>(E));

    // First, convert to the correct width so that we control the kind of
    // extension.
    auto DestLLVMTy = convertType(DestTy);
    llvm::Type *MiddleTy = FE.ME.getDataLayout().getIntPtrType(DestLLVMTy);
    bool InputSigned = E->getType()->isSignedIntegerOrEnumerationType();
    llvm::Value *IntResult =
        Builder.CreateIntCast(Src, MiddleTy, InputSigned, "conv");

    auto *IntToPtr = Builder.CreateIntToPtr(IntResult, DestLLVMTy);
    return IntToPtr;
  }
  case CK_PointerToIntegral: {
    assert(!DestTy->isBooleanType() && "bool should use PointerToBool");
    auto *PtrExpr = Visit(E);
    return Builder.CreatePtrToInt(PtrExpr, convertType(DestTy));
  }
  case CK_ToVoid: {
    FE.genIgnoredExpr(E);
    return nullptr;
  }
  case CK_MatrixCast: {
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());
  }
  case CK_VectorSplat: {
    llvm::Type *DstTy = convertType(DestTy);
    Value *Elt = Visit(const_cast<Expr *>(E));
    // Splat the element across to all elements
    llvm::ElementCount NumElements =
        cast<llvm::VectorType>(DstTy)->getElementCount();
    return Builder.CreateVectorSplat(NumElements, Elt, "splat");
  }

  case CK_FixedPointCast:
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());

  case CK_FixedPointToBoolean:
    assert(E->getType()->isFixedPointType() &&
           "Expected src type to be fixed point type");
    assert(DestTy->isBooleanType() && "Expected dest type to be boolean type");
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());

  case CK_FixedPointToIntegral:
    assert(E->getType()->isFixedPointType() &&
           "Expected src type to be fixed point type");
    assert(DestTy->isIntegerType() && "Expected dest type to be an integer");
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());

  case CK_IntegralToFixedPoint:
    assert(E->getType()->isIntegerType() &&
           "Expected src type to be an integer");
    assert(DestTy->isFixedPointType() &&
           "Expected dest type to be fixed point type");
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());

  case CK_IntegralCast:
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingCast:
  case CK_FixedPointToFloating:
  case CK_FloatingToFixedPoint: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, CE);
    return genScalarConversion(Visit(E), E->getType(), DestTy,
                               CE->getExprLoc());
  }
  case CK_BooleanToSignedIntegral: {
    ScalarConversionOpts Opts;
    Opts.TreatBooleanAsSigned = true;
    return genScalarConversion(Visit(E), E->getType(), DestTy, CE->getExprLoc(),
                               Opts);
  }
  case CK_IntegralToBoolean:
    return genIntToBoolConversion(Visit(E));
  case CK_PointerToBoolean:
    return genPointerToBoolConversion(Visit(E), E->getType());
  case CK_FloatingToBoolean: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, CE);
    return genFloatToBoolConversion(Visit(E));
  }

  case CK_FloatingComplexToReal:
  case CK_IntegralComplexToReal:
    return FE.genComplexExpr(E, false, true).first;

  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean: {
    FunctionEmitter::ComplexPairTy V = FE.genComplexExpr(E);

    return genComplexToScalarConversion(V, E->getType(), DestTy,
                                        CE->getExprLoc());
  }

  } // end of switch

  llvm_unreachable("unknown scalar cast");
}

Value *ScalarExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  FunctionEmitter::StmtExprEvaluation eval(FE);
  Address RetAlloca =
      FE.genCompoundStmt(*E->getSubStmt(), !E->getType()->isVoidType());
  if (!RetAlloca.isValid())
    return nullptr;
  return FE.genLoadOfScalar(FE.makeAddrLValue(RetAlloca, E->getType()),
                            E->getExprLoc());
}

Value *ScalarExprEmitter::VisitExprWithCleanups(ExprWithCleanups *E) {
  FunctionEmitter::RunCleanupsScope Scope(FE);
  Value *V = Visit(E->getSubExpr());
  // Defend against dominance problems caused by jumps out of expression
  // evaluation through the shared cleanup block.
  Scope.ForceCleanup({&V});
  return V;
}

namespace {
BinOpInfo createBinOpInfoFromIncDec(const UnaryOperator *E, llvm::Value *InVal,
                                    bool IsInc, FPOptions FPFeatures) {
  BinOpInfo BinOp;
  BinOp.LHS = InVal;
  BinOp.RHS = llvm::ConstantInt::get(InVal->getType(), 1, false);
  BinOp.Ty = E->getType();
  BinOp.Opcode = IsInc ? BO_Add : BO_Sub;
  BinOp.FPFeatures = FPFeatures;
  BinOp.E = E;
  return BinOp;
}
} // namespace

llvm::Value *ScalarExprEmitter::genIncDecConsiderOverflowBehavior(
    const UnaryOperator *E, llvm::Value *InVal, bool IsInc) {
  llvm::Value *Amount =
      llvm::ConstantInt::get(InVal->getType(), IsInc ? 1 : -1, true);
  llvm::StringRef Name = IsInc ? "inc" : "dec";
  switch (FE.getLangOpts().getSignedOverflowBehavior()) {
  case LangOptions::SOB_Defined:
    return Builder.CreateAdd(InVal, Amount, Name);
  case LangOptions::SOB_Undefined:
    return Builder.CreateNSWAdd(InVal, Amount, Name);
  case LangOptions::SOB_Trapping:
    if (!E->canOverflow())
      return Builder.CreateNSWAdd(InVal, Amount, Name);
    return genOverflowCheckedBinOp(createBinOpInfoFromIncDec(
        E, InVal, IsInc, E->getFPFeaturesInEffect(FE.getLangOpts())));
  }
  llvm_unreachable("Unknown SignedOverflowBehaviorTy");
}

// ===----------------------------------------------------------------------===
// Unary operators & increment/decrement
// ===----------------------------------------------------------------------===

llvm::Value *ScalarExprEmitter::genScalarPrePostIncDec(const UnaryOperator *E,
                                                       LValue LV, bool isInc,
                                                       bool isPre) {
  QualType type = E->getSubExpr()->getType();
  llvm::PHINode *atomicPHI = nullptr;
  llvm::Value *value;
  llvm::Value *input;

  int amount = (isInc ? 1 : -1);
  bool isSubtraction = !isInc;

  if (const AtomicType *atomicTy = type->getAs<AtomicType>()) {
    type = atomicTy->getValueType();
    if (isInc && type->isBooleanType()) {
      llvm::Value *True = FE.genToMemory(Builder.getTrue(), type);
      if (isPre) {
        Builder.CreateStore(True, LV.getAddress(FE), LV.isVolatileQualified())
            ->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        return Builder.getTrue();
      }
      // For atomic bool increment, we just store true and return it for
      // preincrement, do an atomic swap with true for postincrement
      return Builder.CreateAtomicRMW(
          llvm::AtomicRMWInst::Xchg, LV.getAddress(FE), True,
          llvm::AtomicOrdering::SequentiallyConsistent);
    }
    // Special case for atomic increment / decrement on integers, emit
    // atomicrmw instructions.  We skip this if we want trapping overflow
    // checks, and fall into the slow path with the atomic cmpxchg loop.
    if (!type->isBooleanType() && type->isIntegerType() &&
        FE.getLangOpts().getSignedOverflowBehavior() !=
            LangOptions::SOB_Trapping) {
      llvm::AtomicRMWInst::BinOp aop =
          isInc ? llvm::AtomicRMWInst::Add : llvm::AtomicRMWInst::Sub;
      llvm::Instruction::BinaryOps op =
          isInc ? llvm::Instruction::Add : llvm::Instruction::Sub;
      llvm::Value *amt = FE.genToMemory(
          llvm::ConstantInt::get(convertType(type), 1, true), type);
      llvm::Value *old =
          Builder.CreateAtomicRMW(aop, LV.getAddress(FE), amt,
                                  llvm::AtomicOrdering::SequentiallyConsistent);
      return isPre ? Builder.CreateBinOp(op, old, amt) : old;
    }
    value = genLoadOfLValue(LV, E->getExprLoc());
    input = value;
    // For every other atomic operation, we need to emit a load-op-cmpxchg loop
    llvm::BasicBlock *startBB = Builder.GetInsertBlock();
    llvm::BasicBlock *opBB = FE.createBasicBlock("atomic_op", FE.CurFn);
    value = FE.genToMemory(value, type);
    Builder.CreateBr(opBB);
    Builder.SetInsertPoint(opBB);
    atomicPHI = Builder.CreatePHI(value->getType(), 2);
    atomicPHI->addIncoming(value, startBB);
    value = atomicPHI;
  } else {
    value = genLoadOfLValue(LV, E->getExprLoc());
    input = value;
  }

  // Special case of integer increment that we have to check first: bool++.
  // Due to promotion rules, we get:
  //   bool++ -> bool = bool + 1
  //          -> bool = (int)bool + 1
  //          -> bool = ((int)bool + 1 != 0)
  // An interesting aspect of this is that increment is always true.
  // Decrement does not have this property.
  if (isInc && type->isBooleanType()) {
    value = Builder.getTrue();

    // Most common case by far: integer increment.
  } else if (type->isIntegerType()) {
    if (E->canOverflow() && type->isSignedIntegerOrEnumerationType()) {
      value = genIncDecConsiderOverflowBehavior(E, value, isInc);
    } else {
      llvm::Value *amt = llvm::ConstantInt::get(value->getType(), amount, true);
      value = Builder.CreateAdd(value, amt, isInc ? "inc" : "dec");
    }

    // Next most common: pointer increment.
  } else if (const PointerType *ptr = type->getAs<PointerType>()) {
    QualType type = ptr->getPointeeType();

    // VLA types don't have constant size.
    if (const VariableArrayType *vla =
            FE.getContext().getAsVariableArrayType(type)) {
      llvm::Value *numElts = FE.getVLASize(vla).NumElts;
      if (!isInc)
        numElts = Builder.CreateNSWNeg(numElts, "vla.negsize");
      llvm::Type *elemTy = FE.convertTypeForMem(vla->getElementType());
      if (FE.getLangOpts().isSignedOverflowDefined())
        value = Builder.CreateGEP(elemTy, value, numElts, "vla.inc");
      else
        value = FE.genCheckedInBoundsGEP(elemTy, value, numElts,
                                         /*SignedIndices=*/false, isSubtraction,
                                         E->getExprLoc(), "vla.inc");

      // Arithmetic on function pointers (!) is just +-1.
    } else if (type->isFunctionType()) {
      llvm::Value *amt = Builder.getInt32(amount);

      if (FE.getLangOpts().isSignedOverflowDefined())
        value = Builder.CreateGEP(FE.Int8Ty, value, amt, "incdec.funcptr");
      else
        value = FE.genCheckedInBoundsGEP(FE.Int8Ty, value, amt,
                                         /*SignedIndices=*/false, isSubtraction,
                                         E->getExprLoc(), "incdec.funcptr");

      // For everything else, we can just do a simple increment.
    } else {
      llvm::Value *amt = Builder.getInt32(amount);
      llvm::Type *elemTy = FE.convertTypeForMem(type);
      if (FE.getLangOpts().isSignedOverflowDefined())
        value = Builder.CreateGEP(elemTy, value, amt, "incdec.ptr");
      else
        value = FE.genCheckedInBoundsGEP(elemTy, value, amt,
                                         /*SignedIndices=*/false, isSubtraction,
                                         E->getExprLoc(), "incdec.ptr");
    }

    // Vector increment/decrement.
  } else if (type->isVectorType()) {
    if (type->hasIntegerRepresentation()) {
      llvm::Value *amt = llvm::ConstantInt::get(value->getType(), amount);

      value = Builder.CreateAdd(value, amt, isInc ? "inc" : "dec");
    } else {
      value = Builder.CreateFAdd(
          value, llvm::ConstantFP::get(value->getType(), amount),
          isInc ? "inc" : "dec");
    }

    // Floating point.
  } else if (type->isRealFloatingType()) {
    llvm::Value *amt;
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, E);

    if (type->isHalfType() && !FE.getContext().getLangOpts().NativeHalfType) {
      // Another special case: half FP increment should be done via float
      if (FE.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
        value = Builder.CreateCall(
            FE.ME.getIntrinsic(llvm::Intrinsic::convert_from_fp16,
                               FE.ME.FloatTy),
            input, "incdec.conv");
      } else {
        value = Builder.CreateFPExt(input, FE.ME.FloatTy, "incdec.conv");
      }
    }

    if (value->getType()->isFloatTy())
      amt = llvm::ConstantFP::get(VMContext,
                                  llvm::APFloat(static_cast<float>(amount)));
    else if (value->getType()->isDoubleTy())
      amt = llvm::ConstantFP::get(VMContext,
                                  llvm::APFloat(static_cast<double>(amount)));
    else {
      // Remaining types are Half, Bfloat16, LongDouble or __float128.
      llvm::APFloat F(static_cast<float>(amount));
      bool ignored;
      const llvm::fltSemantics *FS;
      // Don't use getFloatTypeSemantics because Half isn't
      // necessarily represented using the "half" LLVM type.
      if (value->getType()->isFP128Ty())
        FS = &FE.getTarget().getFloat128Format();
      else if (value->getType()->isHalfTy())
        FS = &FE.getTarget().getHalfFormat();
      else if (value->getType()->isBFloatTy())
        FS = &FE.getTarget().getBFloat16Format();
      else
        FS = &FE.getTarget().getLongDoubleFormat();
      F.convert(*FS, llvm::APFloat::rmTowardZero, &ignored);
      amt = llvm::ConstantFP::get(VMContext, F);
    }
    value = Builder.CreateFAdd(value, amt, isInc ? "inc" : "dec");

    if (type->isHalfType() && !FE.getContext().getLangOpts().NativeHalfType) {
      if (FE.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
        value = Builder.CreateCall(
            FE.ME.getIntrinsic(llvm::Intrinsic::convert_to_fp16, FE.ME.FloatTy),
            value, "incdec.conv");
      } else {
        value = Builder.CreateFPTrunc(value, input->getType(), "incdec.conv");
      }
    }

    // Fixed-point types.
  } else if (type->isFixedPointType()) {
    // Fixed-point types are tricky. In some cases, it isn't possible to
    // represent a 1 or a -1 in the type at all. Piggyback off of
    // genFixedPointBinOp to avoid having to reimplement saturation.
    BinOpInfo Info;
    Info.E = E;
    Info.Ty = E->getType();
    Info.Opcode = isInc ? BO_Add : BO_Sub;
    Info.LHS = value;
    Info.RHS = llvm::ConstantInt::get(value->getType(), 1, false);
    // If the type is signed, it's better to represent this as +(-1) or -(-1),
    // since -1 is guaranteed to be representable.
    if (type->isSignedFixedPointType()) {
      Info.Opcode = isInc ? BO_Sub : BO_Add;
      Info.RHS = Builder.CreateNeg(Info.RHS);
    }
    // Now, convert from our invented integer literal to the type of the unary
    // op. This will upscale and saturate if necessary. This value can become
    // undef in some cases.
    llvm::FixedPointBuilder<CGBuilderTy> FPBuilder(Builder);
    auto DstSema = FE.getContext().getFixedPointSemantics(Info.Ty);
    Info.RHS = FPBuilder.CreateIntegerToFixed(Info.RHS, true, DstSema);
    value = genFixedPointBinOp(Info);

  } else {
    llvm_unreachable("unexpected type for increment/decrement");
  }

  if (atomicPHI) {
    llvm::BasicBlock *curBlock = Builder.GetInsertBlock();
    llvm::BasicBlock *contBB = FE.createBasicBlock("atomic_cont", FE.CurFn);
    auto Pair = FE.genAtomicCompareExchange(
        LV, RValue::get(atomicPHI), RValue::get(value), E->getExprLoc());
    llvm::Value *old = FE.genToMemory(Pair.first.getScalarVal(), type);
    llvm::Value *success = Pair.second;
    atomicPHI->addIncoming(old, curBlock);
    Builder.CreateCondBr(success, contBB, atomicPHI->getParent());
    Builder.SetInsertPoint(contBB);
    return isPre ? value : input;
  }

  if (LV.isBitField())
    FE.genStoreThroughBitfieldLValue(RValue::get(value), LV, &value);
  else
    FE.genStoreThroughLValue(RValue::get(value), LV);

  // If this is a postinc, return the value read from memory, otherwise use the
  // updated value.
  return isPre ? value : input;
}

Value *ScalarExprEmitter::VisitUnaryPlus(const UnaryOperator *E,
                                         QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitPlus(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = genUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitPlus(const UnaryOperator *E,
                                    QualType PromotionType) {
  // This differs from gcc, though, most likely due to a bug in gcc.
  TestAndClearIgnoreResultAssign();
  if (!PromotionType.isNull())
    return FE.genPromotedScalarExpr(E->getSubExpr(), PromotionType);
  return Visit(E->getSubExpr());
}

Value *ScalarExprEmitter::VisitUnaryMinus(const UnaryOperator *E,
                                          QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitMinus(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = genUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitMinus(const UnaryOperator *E,
                                     QualType PromotionType) {
  TestAndClearIgnoreResultAssign();
  Value *Op;
  if (!PromotionType.isNull())
    Op = FE.genPromotedScalarExpr(E->getSubExpr(), PromotionType);
  else
    Op = Visit(E->getSubExpr());

  if (Op->getType()->isFPOrFPVectorTy())
    return Builder.CreateFNeg(Op, "fneg");

  BinOpInfo BinOp;
  BinOp.RHS = Op;
  BinOp.LHS = llvm::Constant::getNullValue(BinOp.RHS->getType());
  BinOp.Ty = E->getType();
  BinOp.Opcode = BO_Sub;
  BinOp.FPFeatures = E->getFPFeaturesInEffect(FE.getLangOpts());
  BinOp.E = E;
  return genSub(BinOp);
}

Value *ScalarExprEmitter::VisitUnaryNot(const UnaryOperator *E) {
  TestAndClearIgnoreResultAssign();
  Value *Op = Visit(E->getSubExpr());
  return Builder.CreateNot(Op, "not");
}

Value *ScalarExprEmitter::VisitUnaryLNot(const UnaryOperator *E) {
  if (E->getType()->isVectorType() &&
      E->getType()->castAs<VectorType>()->getVectorKind() ==
          VectorKind::Generic) {
    Value *Oper = Visit(E->getSubExpr());
    Value *Zero = llvm::Constant::getNullValue(Oper->getType());
    Value *Result;
    if (Oper->getType()->isFPOrFPVectorTy()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(
          FE, E->getFPFeaturesInEffect(FE.getLangOpts()));
      Result = Builder.CreateFCmp(llvm::CmpInst::FCMP_OEQ, Oper, Zero, "cmp");
    } else
      Result = Builder.CreateICmp(llvm::CmpInst::ICMP_EQ, Oper, Zero, "cmp");
    return Builder.CreateSExt(Result, convertType(E->getType()), "sext");
  }

  Value *BoolVal = FE.evaluateExprAsBool(E->getSubExpr());
  BoolVal = Builder.CreateNot(BoolVal, "lnot");
  return Builder.CreateZExt(BoolVal, convertType(E->getType()), "lnot.ext");
}

Value *ScalarExprEmitter::VisitOffsetOfExpr(OffsetOfExpr *E) {
  Expr::EvalResult EVResult;
  if (E->EvaluateAsInt(EVResult, FE.getContext())) {
    llvm::APSInt Value = EVResult.Val.getInt();
    return Builder.getInt(Value);
  }

  unsigned n = E->getNumComponents();
  llvm::Type *ResultType = convertType(E->getType());
  llvm::Value *Result = llvm::Constant::getNullValue(ResultType);
  QualType CurrentType = E->getTypeSourceInfo()->getType();
  for (unsigned i = 0; i != n; ++i) {
    OffsetOfNode ON = E->getComponent(i);
    llvm::Value *Offset = nullptr;
    switch (ON.getKind()) {
    case OffsetOfNode::Array: {
      Expr *IdxExpr = E->getIndexExpr(ON.getArrayExprIndex());
      llvm::Value *Idx = FE.genScalarExpr(IdxExpr);
      bool IdxSigned = IdxExpr->getType()->isSignedIntegerOrEnumerationType();
      Idx = Builder.CreateIntCast(Idx, ResultType, IdxSigned, "conv");

      CurrentType =
          FE.getContext().getAsArrayType(CurrentType)->getElementType();

      llvm::Value *ElemSize = llvm::ConstantInt::get(
          ResultType,
          FE.getContext().getTypeSizeInChars(CurrentType).getQuantity());

      Offset = Builder.CreateMul(Idx, ElemSize);
      break;
    }

    case OffsetOfNode::Field: {
      FieldDecl *MemberDecl = ON.getField();
      RecordDecl *RD = CurrentType->castAs<RecordType>()->getDecl();
      const StructRecordLayout &RL = FE.getContext().getStructRecordLayout(RD);

      unsigned i = 0;
      for (RecordDecl::field_iterator Field = RD->field_begin(),
                                      FieldEnd = RD->field_end();
           Field != FieldEnd; ++Field, ++i) {
        if (*Field == MemberDecl)
          break;
      }
      assert(i < RL.getFieldCount() && "offsetof field in wrong type");

      int64_t OffsetInt = RL.getFieldOffset(i) / FE.getContext().getCharWidth();
      Offset = llvm::ConstantInt::get(ResultType, OffsetInt);

      CurrentType = MemberDecl->getType();
      break;
    }

    case OffsetOfNode::Identifier:
      llvm_unreachable("dependent __builtin_offsetof");
    }
    Result = Builder.CreateAdd(Result, Offset);
  }
  return Result;
}

Value *ScalarExprEmitter::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *E) {
  QualType TypeToSize = E->getTypeOfArgument();
  if (auto Kind = E->getKind(); Kind == UETT_SizeOf) {
    if (const VariableArrayType *VAT =
            FE.getContext().getAsVariableArrayType(TypeToSize)) {
      if (E->isArgumentType()) {
        // sizeof(type) - make sure to emit the VLA size.
        FE.genVariablyModifiedType(TypeToSize);
      } else {
        // sizeof(VLA) requires evaluating the size expression.
        FE.genIgnoredExpr(E->getArgumentExpr());
      }

      auto VlaSize = FE.getVLASize(VAT);
      llvm::Value *size = VlaSize.NumElts;

      // Scale the number of non-VLA elements by the non-VLA element size.
      CharUnits eltSize = FE.getContext().getTypeSizeInChars(VlaSize.Type);
      if (!eltSize.isOne())
        size = FE.Builder.CreateNUWMul(FE.ME.getSize(eltSize), size);

      return size;
    }
  } else if (E->getKind() == UETT_VectorElements) {
    auto *VecTy = cast<llvm::VectorType>(convertType(E->getTypeOfArgument()));
    return Builder.CreateElementCount(FE.SizeTy, VecTy->getElementCount());
  }

  // If this isn't sizeof(vla), the result must be constant; use the constant
  // folding logic so we don't have to duplicate it here.
  return Builder.getInt(E->EvaluateKnownConstInt(FE.getContext()));
}

Value *ScalarExprEmitter::VisitUnaryReal(const UnaryOperator *E,
                                         QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitReal(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = genUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitReal(const UnaryOperator *E,
                                    QualType PromotionType) {
  Expr *Op = E->getSubExpr();
  if (Op->getType()->isAnyComplexType()) {
    // If it's an l-value, load through the appropriate subobject l-value.
    // Note that we have to ask E because Op might be an l-value that
    // requires special load handling.
    if (E->isLValue()) {
      if (!PromotionType.isNull()) {
        FunctionEmitter::ComplexPairTy result = FE.genComplexExpr(
            Op, /*IgnoreReal*/ IgnoreResultAssign, /*IgnoreImag*/ true);
        if (result.first)
          result.first = FE.genPromotedValue(result, PromotionType).first;
        return result.first;
      } else {
        return FE.genLoadOfLValue(FE.genLValue(E), E->getExprLoc())
            .getScalarVal();
      }
    }
    // Otherwise, calculate and project.
    return FE.genComplexExpr(Op, false, true).first;
  }

  if (!PromotionType.isNull())
    return FE.genPromotedScalarExpr(Op, PromotionType);
  return Visit(Op);
}

Value *ScalarExprEmitter::VisitUnaryImag(const UnaryOperator *E,
                                         QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitImag(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = genUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitImag(const UnaryOperator *E,
                                    QualType PromotionType) {
  Expr *Op = E->getSubExpr();
  if (Op->getType()->isAnyComplexType()) {
    // If it's an l-value, load through the appropriate subobject l-value.
    // Note that we have to ask E because Op might be an l-value that
    // requires special load handling.
    if (Op->isLValue()) {
      if (!PromotionType.isNull()) {
        FunctionEmitter::ComplexPairTy result = FE.genComplexExpr(
            Op, /*IgnoreReal*/ true, /*IgnoreImag*/ IgnoreResultAssign);
        if (result.second)
          result.second = FE.genPromotedValue(result, PromotionType).second;
        return result.second;
      } else {
        return FE.genLoadOfLValue(FE.genLValue(E), E->getExprLoc())
            .getScalarVal();
      }
    }
    // Otherwise, calculate and project.
    return FE.genComplexExpr(Op, true, false).second;
  }

  // __imag on a scalar returns zero.  Emit the subexpr to ensure side
  // effects are evaluated, but not the actual value.
  if (Op->isLValue())
    FE.genLValue(Op);
  else if (!PromotionType.isNull())
    FE.genPromotedScalarExpr(Op, PromotionType);
  else
    FE.genScalarExpr(Op, true);
  if (!PromotionType.isNull())
    return llvm::Constant::getNullValue(convertType(PromotionType));
  return llvm::Constant::getNullValue(convertType(E->getType()));
}

Value *ScalarExprEmitter::genPromotedValue(Value *result,
                                           QualType PromotionType) {
  return FE.Builder.CreateFPExt(result, convertType(PromotionType), "ext");
}

Value *ScalarExprEmitter::genUnPromotedValue(Value *result, QualType ExprType) {
  return FE.Builder.CreateFPTrunc(result, convertType(ExprType), "unpromotion");
}

Value *ScalarExprEmitter::genPromoted(const Expr *E, QualType PromotionType) {
  E = E->IgnoreParens();
  if (auto BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
#define HANDLE_BINOP(OP)                                                       \
  case BO_##OP:                                                                \
    return gen##OP(genBinOps(BO, PromotionType));
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
    case UO_Imag:
      return VisitImag(UO, PromotionType);
    case UO_Real:
      return VisitReal(UO, PromotionType);
    case UO_Minus:
      return VisitMinus(UO, PromotionType);
    case UO_Plus:
      return VisitPlus(UO, PromotionType);
    default:
      break;
    }
  }
  auto result = Visit(const_cast<Expr *>(E));
  if (result) {
    if (!PromotionType.isNull())
      return genPromotedValue(result, PromotionType);
    else
      return genUnPromotedValue(result, E->getType());
  }
  return result;
}

BinOpInfo ScalarExprEmitter::genBinOps(const BinaryOperator *E,
                                       QualType PromotionType) {
  TestAndClearIgnoreResultAssign();
  BinOpInfo Result;
  Result.LHS = FE.genPromotedScalarExpr(E->getLHS(), PromotionType);
  Result.RHS = FE.genPromotedScalarExpr(E->getRHS(), PromotionType);
  if (!PromotionType.isNull())
    Result.Ty = PromotionType;
  else
    Result.Ty = E->getType();
  Result.Opcode = E->getOpcode();
  Result.FPFeatures = E->getFPFeaturesInEffect(FE.getLangOpts());
  Result.E = E;
  return Result;
}

LValue ScalarExprEmitter::genCompoundAssignLValue(
    const CompoundAssignOperator *E,
    Value *(ScalarExprEmitter::*Func)(const BinOpInfo &), Value *&Result) {
  QualType LHSTy = E->getLHS()->getType();
  BinOpInfo OpInfo;

  if (E->getComputationResultType()->isAnyComplexType())
    return FE.genScalarCompoundAssignWithComplex(E, Result);

  QualType PromotionTypeCR;
  PromotionTypeCR = getPromotionType(E->getComputationResultType());
  if (PromotionTypeCR.isNull())
    PromotionTypeCR = E->getComputationResultType();
  QualType PromotionTypeLHS = getPromotionType(E->getComputationLHSType());
  QualType PromotionTypeRHS = getPromotionType(E->getRHS()->getType());
  if (!PromotionTypeRHS.isNull())
    OpInfo.RHS = FE.genPromotedScalarExpr(E->getRHS(), PromotionTypeRHS);
  else
    OpInfo.RHS = Visit(E->getRHS());
  OpInfo.Ty = PromotionTypeCR;
  OpInfo.Opcode = E->getOpcode();
  OpInfo.FPFeatures = E->getFPFeaturesInEffect(FE.getLangOpts());
  OpInfo.E = E;
  LValue LHSLV = genCheckedLValue(E->getLHS(), FunctionEmitter::TCK_Store);

  llvm::PHINode *atomicPHI = nullptr;
  if (const AtomicType *atomicTy = LHSTy->getAs<AtomicType>()) {
    QualType type = atomicTy->getValueType();
    if (!type->isBooleanType() && type->isIntegerType() &&
        FE.getLangOpts().getSignedOverflowBehavior() !=
            LangOptions::SOB_Trapping) {
      llvm::AtomicRMWInst::BinOp AtomicOp = llvm::AtomicRMWInst::BAD_BINOP;
      llvm::Instruction::BinaryOps Op;
      switch (OpInfo.Opcode) {
      // We don't have atomicrmw operands for *, %, /, <<, >>
      case BO_MulAssign:
      case BO_DivAssign:
      case BO_RemAssign:
      case BO_ShlAssign:
      case BO_ShrAssign:
        break;
      case BO_AddAssign:
        AtomicOp = llvm::AtomicRMWInst::Add;
        Op = llvm::Instruction::Add;
        break;
      case BO_SubAssign:
        AtomicOp = llvm::AtomicRMWInst::Sub;
        Op = llvm::Instruction::Sub;
        break;
      case BO_AndAssign:
        AtomicOp = llvm::AtomicRMWInst::And;
        Op = llvm::Instruction::And;
        break;
      case BO_XorAssign:
        AtomicOp = llvm::AtomicRMWInst::Xor;
        Op = llvm::Instruction::Xor;
        break;
      case BO_OrAssign:
        AtomicOp = llvm::AtomicRMWInst::Or;
        Op = llvm::Instruction::Or;
        break;
      default:
        llvm_unreachable("Invalid compound assignment type");
      }
      if (AtomicOp != llvm::AtomicRMWInst::BAD_BINOP) {
        llvm::Value *Amt = FE.genToMemory(
            genScalarConversion(OpInfo.RHS, E->getRHS()->getType(), LHSTy,
                                E->getExprLoc()),
            LHSTy);
        Value *OldVal = Builder.CreateAtomicRMW(
            AtomicOp, LHSLV.getAddress(FE), Amt,
            llvm::AtomicOrdering::SequentiallyConsistent);

        // Since operation is atomic, the result type is guaranteed to be the
        // same as the input in LLVM terms.
        Result = Builder.CreateBinOp(Op, OldVal, Amt);
        return LHSLV;
      }
    }
    llvm::BasicBlock *startBB = Builder.GetInsertBlock();
    llvm::BasicBlock *opBB = FE.createBasicBlock("atomic_op", FE.CurFn);
    OpInfo.LHS = genLoadOfLValue(LHSLV, E->getExprLoc());
    OpInfo.LHS = FE.genToMemory(OpInfo.LHS, type);
    Builder.CreateBr(opBB);
    Builder.SetInsertPoint(opBB);
    atomicPHI = Builder.CreatePHI(OpInfo.LHS->getType(), 2);
    atomicPHI->addIncoming(OpInfo.LHS, startBB);
    OpInfo.LHS = atomicPHI;
  } else
    OpInfo.LHS = genLoadOfLValue(LHSLV, E->getExprLoc());

  FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, OpInfo.FPFeatures);
  SourceLocation Loc = E->getExprLoc();
  if (!PromotionTypeLHS.isNull())
    OpInfo.LHS = genScalarConversion(OpInfo.LHS, LHSTy, PromotionTypeLHS,
                                     E->getExprLoc());
  else
    OpInfo.LHS =
        genScalarConversion(OpInfo.LHS, LHSTy, E->getComputationLHSType(), Loc);

  // Expand the binary operator.
  Result = (this->*Func)(OpInfo);

  Result = genScalarConversion(Result, PromotionTypeCR, LHSTy, Loc);

  if (atomicPHI) {
    llvm::BasicBlock *curBlock = Builder.GetInsertBlock();
    llvm::BasicBlock *contBB = FE.createBasicBlock("atomic_cont", FE.CurFn);
    auto Pair = FE.genAtomicCompareExchange(
        LHSLV, RValue::get(atomicPHI), RValue::get(Result), E->getExprLoc());
    llvm::Value *old = FE.genToMemory(Pair.first.getScalarVal(), LHSTy);
    llvm::Value *success = Pair.second;
    atomicPHI->addIncoming(old, curBlock);
    Builder.CreateCondBr(success, contBB, atomicPHI->getParent());
    Builder.SetInsertPoint(contBB);
    return LHSLV;
  }

  // Compound assignment result is the value after store (matters for
  // bitfields).
  if (LHSLV.isBitField())
    FE.genStoreThroughBitfieldLValue(RValue::get(Result), LHSLV, &Result);
  else
    FE.genStoreThroughLValue(RValue::get(Result), LHSLV);

  return LHSLV;
}

Value *ScalarExprEmitter::genCompoundAssign(
    const CompoundAssignOperator *E,
    Value *(ScalarExprEmitter::*Func)(const BinOpInfo &)) {
  bool Ignore = TestAndClearIgnoreResultAssign();
  Value *RHS = nullptr;
  genCompoundAssignLValue(E, Func, RHS);

  if (Ignore)
    return nullptr;

  return RHS;
}

Value *ScalarExprEmitter::genDiv(const BinOpInfo &Ops) {
  if (Ops.Ty->isConstantMatrixType()) {
    llvm::MatrixBuilder MB(Builder);
    // We need to check the types of the operands of the operator to get the
    // correct matrix dimensions.
    auto *BO = cast<BinaryOperator>(Ops.E);
    (void)BO;
    assert(
        isa<ConstantMatrixType>(BO->getLHS()->getType().getCanonicalType()) &&
        "first operand must be a matrix");
    assert(BO->getRHS()->getType().getCanonicalType()->isArithmeticType() &&
           "second operand must be an arithmetic type");
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Ops.FPFeatures);
    return MB.CreateScalarDiv(Ops.LHS, Ops.RHS,
                              Ops.Ty->hasUnsignedIntegerRepresentation());
  }

  if (Ops.LHS->getType()->isFPOrFPVectorTy()) {
    llvm::Value *Val;
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, Ops.FPFeatures);
    Val = Builder.CreateFDiv(Ops.LHS, Ops.RHS, "div");
    return Val;
  } else if (Ops.isFixedPointOp())
    return genFixedPointBinOp(Ops);
  else if (Ops.Ty->hasUnsignedIntegerRepresentation())
    return Builder.CreateUDiv(Ops.LHS, Ops.RHS, "div");
  else
    return Builder.CreateSDiv(Ops.LHS, Ops.RHS, "div");
}

Value *ScalarExprEmitter::genRem(const BinOpInfo &Ops) {
  // Remainder is integer-only in C.
  if (Ops.Ty->hasUnsignedIntegerRepresentation())
    return Builder.CreateURem(Ops.LHS, Ops.RHS, "rem");
  else
    return Builder.CreateSRem(Ops.LHS, Ops.RHS, "rem");
}

Value *ScalarExprEmitter::genOverflowCheckedBinOp(const BinOpInfo &Ops) {
  unsigned IID;
  unsigned OpID = 0;
  SanitizerHandler OverflowKind;

  bool isSigned = Ops.Ty->isSignedIntegerOrEnumerationType();
  switch (Ops.Opcode) {
  case BO_Add:
  case BO_AddAssign:
    OpID = 1;
    IID = isSigned ? llvm::Intrinsic::sadd_with_overflow
                   : llvm::Intrinsic::uadd_with_overflow;
    OverflowKind = SanitizerHandler::AddOverflow;
    break;
  case BO_Sub:
  case BO_SubAssign:
    OpID = 2;
    IID = isSigned ? llvm::Intrinsic::ssub_with_overflow
                   : llvm::Intrinsic::usub_with_overflow;
    OverflowKind = SanitizerHandler::SubOverflow;
    break;
  case BO_Mul:
  case BO_MulAssign:
    OpID = 3;
    IID = isSigned ? llvm::Intrinsic::smul_with_overflow
                   : llvm::Intrinsic::umul_with_overflow;
    OverflowKind = SanitizerHandler::MulOverflow;
    break;
  default:
    llvm_unreachable("Unsupported operation for overflow detection");
  }
  OpID <<= 1;
  if (isSigned)
    OpID |= 1;

  FunctionEmitter::SanitizerScope SanScope(&FE);
  llvm::Type *opTy = FE.ME.getTypes().convertType(Ops.Ty);

  llvm::Function *intrinsic = FE.ME.getIntrinsic(IID, opTy);

  Value *resultAndOverflow = Builder.CreateCall(intrinsic, {Ops.LHS, Ops.RHS});
  Value *result = Builder.CreateExtractValue(resultAndOverflow, 0);
  Value *overflow = Builder.CreateExtractValue(resultAndOverflow, 1);

  const std::string *handlerName = &FE.getLangOpts().OverflowHandler;
  if (handlerName->empty()) {
    FE.genTrapCheck(Builder.CreateNot(overflow), OverflowKind);
    return result;
  }

  llvm::BasicBlock *initialBB = Builder.GetInsertBlock();
  llvm::BasicBlock *continueBB =
      FE.createBasicBlock("nooverflow", FE.CurFn, initialBB->getNextNode());
  llvm::BasicBlock *overflowBB = FE.createBasicBlock("overflow", FE.CurFn);

  Builder.CreateCondBr(overflow, overflowBB, continueBB);

  Builder.SetInsertPoint(overflowBB);

  llvm::Type *Int8Ty = FE.Int8Ty;
  llvm::Type *argTypes[] = {FE.Int64Ty, FE.Int64Ty, Int8Ty, Int8Ty};
  llvm::FunctionType *handlerTy =
      llvm::FunctionType::get(FE.Int64Ty, argTypes, true);
  llvm::FunctionCallee handler =
      FE.ME.createRuntimeFunction(handlerTy, *handlerName);

  llvm::Value *lhs = Builder.CreateSExt(Ops.LHS, FE.Int64Ty);
  llvm::Value *rhs = Builder.CreateSExt(Ops.RHS, FE.Int64Ty);

  llvm::Value *handlerArgs[] = {
      lhs, rhs, Builder.getInt8(OpID),
      Builder.getInt8(cast<llvm::IntegerType>(opTy)->getBitWidth())};
  llvm::Value *handlerResult = FE.genNounwindRuntimeCall(handler, handlerArgs);

  handlerResult = Builder.CreateTrunc(handlerResult, opTy);
  Builder.CreateBr(continueBB);

  Builder.SetInsertPoint(continueBB);
  llvm::PHINode *phi = Builder.CreatePHI(opTy, 2);
  phi->addIncoming(result, initialBB);
  phi->addIncoming(handlerResult, overflowBB);

  return phi;
}

namespace {
Value *emitPointerArithmetic(FunctionEmitter &FE, const BinOpInfo &op,
                             bool isSubtraction) {
  // Must have binary (not unary) expr here.  Unary pointer
  // increment/decrement doesn't use this path.
  const BinaryOperator *expr = cast<BinaryOperator>(op.E);

  Value *pointer = op.LHS;
  Expr *pointerOperand = expr->getLHS();
  Value *index = op.RHS;
  Expr *indexOperand = expr->getRHS();

  // In a subtraction, the LHS is always the pointer.
  if (!isSubtraction && !pointer->getType()->isPointerTy()) {
    std::swap(pointer, index);
    std::swap(pointerOperand, indexOperand);
  }

  bool isSigned = indexOperand->getType()->isSignedIntegerOrEnumerationType();

  unsigned width = cast<llvm::IntegerType>(index->getType())->getBitWidth();
  auto &DL = FE.ME.getDataLayout();
  auto PtrTy = cast<llvm::PointerType>(pointer->getType());

  // Some versions of glibc and gcc use idioms (particularly in their malloc
  // routines) that add a pointer-sized integer (known to be a pointer value)
  // to a null pointer in order to cast the value back to an integer or as
  // part of a pointer alignment algorithm.  This is undefined behavior, but
  // we'd like to be able to compile programs that use it.
  //
  // Normally, we'd generate a GEP with a null-pointer base here in response
  // to that code, but it's also UB to dereference a pointer created that
  // way.  Instead (as an acknowledged hack to tolerate the idiom) we will
  // generate a direct cast of the integer value to a pointer.
  //
  // The idiom (p = nullptr + N) is not met if any of the following are true:
  //
  //   The operation is subtraction.
  //   The index is not pointer-sized.
  //   The pointer type is not byte-sized.
  //
  if (BinaryOperator::isNullPointerArithmeticExtension(
          FE.getContext(), op.Opcode, expr->getLHS(), expr->getRHS()))
    return FE.Builder.CreateIntToPtr(index, pointer->getType());

  if (width != DL.getIndexTypeSizeInBits(PtrTy)) {
    // Zero-extend or sign-extend the pointer value according to
    // whether the index is signed or not.
    index = FE.Builder.CreateIntCast(index, DL.getIndexType(PtrTy), isSigned,
                                     "idx.ext");
  }

  // If this is subtraction, negate the index.
  if (isSubtraction)
    index = FE.Builder.CreateNeg(index, "idx.neg");

  const PointerType *pointerType =
      pointerOperand->getType()->getAs<PointerType>();
  assert(pointerType && "expected pointer type for arithmetic");

  QualType elementType = pointerType->getPointeeType();
  if (const VariableArrayType *vla =
          FE.getContext().getAsVariableArrayType(elementType)) {
    // The element count here is the total number of non-VLA elements.
    llvm::Value *numElements = FE.getVLASize(vla).NumElts;

    // Effectively, the multiply by the VLA size is part of the GEP.
    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    llvm::Type *elemTy = FE.convertTypeForMem(vla->getElementType());
    if (FE.getLangOpts().isSignedOverflowDefined()) {
      index = FE.Builder.CreateMul(index, numElements, "vla.index");
      pointer = FE.Builder.CreateGEP(elemTy, pointer, index, "add.ptr");
    } else {
      index = FE.Builder.CreateNSWMul(index, numElements, "vla.index");
      pointer = FE.genCheckedInBoundsGEP(elemTy, pointer, index, isSigned,
                                         isSubtraction, op.E->getExprLoc(),
                                         "add.ptr");
    }
    return pointer;
  }

  // Explicitly handle GNU void* and function pointer arithmetic extensions. The
  // GNU void* casts amount to no-ops since our void* type is i8*, but this is
  // future proof.
  llvm::Type *elemTy;
  if (elementType->isVoidType() || elementType->isFunctionType())
    elemTy = FE.Int8Ty;
  else
    elemTy = FE.convertTypeForMem(elementType);

  if (FE.getLangOpts().isSignedOverflowDefined())
    return FE.Builder.CreateGEP(elemTy, pointer, index, "add.ptr");

  return FE.genCheckedInBoundsGEP(elemTy, pointer, index, isSigned,
                                  isSubtraction, op.E->getExprLoc(), "add.ptr");
}
} // namespace

namespace {

/// Build an fmuladd intrinsic. negMul/negAdd negate the mul's first operand
/// or the addend, enabling a*b-c and c-a*b forms.
Value *buildFMulAdd(llvm::Instruction *MulOp, Value *Addend,
                    const FunctionEmitter &FE, CGBuilderTy &Builder,
                    bool negMul, bool negAdd) {
  Value *MulOp0 = MulOp->getOperand(0);
  Value *MulOp1 = MulOp->getOperand(1);
  if (negMul)
    MulOp0 = Builder.CreateFNeg(MulOp0, "neg");
  if (negAdd)
    Addend = Builder.CreateFNeg(Addend, "neg");

  Value *FMulAdd = nullptr;
  if (Builder.getIsFPConstrained()) {
    assert(isa<llvm::ConstrainedFPIntrinsic>(MulOp) &&
           "Only constrained operation should be created when Builder is in FP "
           "constrained mode");
    FMulAdd = Builder.CreateConstrainedFPCall(
        FE.ME.getIntrinsic(llvm::Intrinsic::experimental_constrained_fmuladd,
                           Addend->getType()),
        {MulOp0, MulOp1, Addend});
  } else {
    FMulAdd = Builder.CreateCall(
        FE.ME.getIntrinsic(llvm::Intrinsic::fmuladd, Addend->getType()),
        {MulOp0, MulOp1, Addend});
  }
  MulOp->eraseFromParent();

  return FMulAdd;
}

/// Try to fuse an fadd/fsub with a preceding fmul into fmuladd.
/// Requires -ffp-contract=on; does NOT verify type contractability.
Value *tryEmitFMulAdd(const BinOpInfo &op, const FunctionEmitter &FE,
                      CGBuilderTy &Builder, bool isSub = false) {

  assert((op.Opcode == BO_Add || op.Opcode == BO_AddAssign ||
          op.Opcode == BO_Sub || op.Opcode == BO_SubAssign) &&
         "Only fadd/fsub can be the root of an fmuladd.");

  if (!op.FPFeatures.allowFPContractWithinStatement())
    return nullptr;

  Value *LHS = op.LHS;
  Value *RHS = op.RHS;

  // Peek through fneg to look for fmul. Make sure fneg has no users, and that
  // it is the only use of its operand.
  bool NegLHS = false;
  if (auto *LHSUnOp = dyn_cast<llvm::UnaryOperator>(LHS)) {
    if (LHSUnOp->getOpcode() == llvm::Instruction::FNeg &&
        LHSUnOp->use_empty() && LHSUnOp->getOperand(0)->hasOneUse()) {
      LHS = LHSUnOp->getOperand(0);
      NegLHS = true;
    }
  }

  bool NegRHS = false;
  if (auto *RHSUnOp = dyn_cast<llvm::UnaryOperator>(RHS)) {
    if (RHSUnOp->getOpcode() == llvm::Instruction::FNeg &&
        RHSUnOp->use_empty() && RHSUnOp->getOperand(0)->hasOneUse()) {
      RHS = RHSUnOp->getOperand(0);
      NegRHS = true;
    }
  }

  // We have a potentially fusable op. Look for a mul on one of the operands.
  // Also, make sure that the mul result isn't used directly. In that case,
  // there's no point creating a muladd operation.
  if (auto *LHSBinOp = dyn_cast<llvm::BinaryOperator>(LHS)) {
    if (LHSBinOp->getOpcode() == llvm::Instruction::FMul &&
        (LHSBinOp->use_empty() || NegLHS)) {
      // If we looked through fneg, erase it.
      if (NegLHS)
        cast<llvm::Instruction>(op.LHS)->eraseFromParent();
      return buildFMulAdd(LHSBinOp, op.RHS, FE, Builder, NegLHS, isSub);
    }
  }
  if (auto *RHSBinOp = dyn_cast<llvm::BinaryOperator>(RHS)) {
    if (RHSBinOp->getOpcode() == llvm::Instruction::FMul &&
        (RHSBinOp->use_empty() || NegRHS)) {
      // If we looked through fneg, erase it.
      if (NegRHS)
        cast<llvm::Instruction>(op.RHS)->eraseFromParent();
      return buildFMulAdd(RHSBinOp, op.LHS, FE, Builder, isSub ^ NegRHS, false);
    }
  }

  if (auto *LHSBinOp = dyn_cast<llvm::CallBase>(LHS)) {
    if (LHSBinOp->getIntrinsicID() ==
            llvm::Intrinsic::experimental_constrained_fmul &&
        (LHSBinOp->use_empty() || NegLHS)) {
      // If we looked through fneg, erase it.
      if (NegLHS)
        cast<llvm::Instruction>(op.LHS)->eraseFromParent();
      return buildFMulAdd(LHSBinOp, op.RHS, FE, Builder, NegLHS, isSub);
    }
  }
  if (auto *RHSBinOp = dyn_cast<llvm::CallBase>(RHS)) {
    if (RHSBinOp->getIntrinsicID() ==
            llvm::Intrinsic::experimental_constrained_fmul &&
        (RHSBinOp->use_empty() || NegRHS)) {
      // If we looked through fneg, erase it.
      if (NegRHS)
        cast<llvm::Instruction>(op.RHS)->eraseFromParent();
      return buildFMulAdd(RHSBinOp, op.LHS, FE, Builder, isSub ^ NegRHS, false);
    }
  }

  return nullptr;
}
} // namespace

// ===----------------------------------------------------------------------===
// Binary arithmetic operators
// ===----------------------------------------------------------------------===

Value *ScalarExprEmitter::genAdd(const BinOpInfo &op) {
  if (op.LHS->getType()->isPointerTy() || op.RHS->getType()->isPointerTy())
    return emitPointerArithmetic(FE, op, FunctionEmitter::NotSubtraction);

  if (op.Ty->isSignedIntegerOrEnumerationType()) {
    switch (FE.getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      return Builder.CreateAdd(op.LHS, op.RHS, "add");
    case LangOptions::SOB_Undefined:
      return Builder.CreateNSWAdd(op.LHS, op.RHS, "add");
    case LangOptions::SOB_Trapping:
      if (canSkipOverflowCheck(FE.getContext(), op))
        return Builder.CreateNSWAdd(op.LHS, op.RHS, "add");
      return genOverflowCheckedBinOp(op);
    }
  }

  // For vector and matrix adds, try to fold into a fmuladd.
  if (op.LHS->getType()->isFPOrFPVectorTy()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, op.FPFeatures);
    // Try to form an fmuladd.
    if (Value *FMulAdd = tryEmitFMulAdd(op, FE, Builder))
      return FMulAdd;
  }

  if (op.Ty->isConstantMatrixType()) {
    llvm::MatrixBuilder MB(Builder);
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, op.FPFeatures);
    return MB.CreateAdd(op.LHS, op.RHS);
  }

  if (op.LHS->getType()->isFPOrFPVectorTy()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, op.FPFeatures);
    return Builder.CreateFAdd(op.LHS, op.RHS, "add");
  }

  if (op.isFixedPointOp())
    return genFixedPointBinOp(op);

  return Builder.CreateAdd(op.LHS, op.RHS, "add");
}

Value *ScalarExprEmitter::genFixedPointBinOp(const BinOpInfo &op) {
  using llvm::APSInt;
  using llvm::ConstantInt;

  // This is either a binary operation where at least one of the operands is
  // a fixed-point type, or a unary operation where the operand is a fixed-point
  // type. The result type of a binary operation is determined by
  // Sema::handleFixedPointConversions().
  QualType ResultTy = op.Ty;
  QualType LHSTy, RHSTy;
  if (const auto *BinOp = dyn_cast<BinaryOperator>(op.E)) {
    RHSTy = BinOp->getRHS()->getType();
    if (const auto *CAO = dyn_cast<CompoundAssignOperator>(BinOp)) {
      // For compound assignment, the effective type of the LHS at this point
      // is the computation LHS type, not the actual LHS type, and the final
      // result type is not the type of the expression but rather the
      // computation result type.
      LHSTy = CAO->getComputationLHSType();
      ResultTy = CAO->getComputationResultType();
    } else
      LHSTy = BinOp->getLHS()->getType();
  } else if (const auto *UnOp = dyn_cast<UnaryOperator>(op.E)) {
    LHSTy = UnOp->getSubExpr()->getType();
    RHSTy = UnOp->getSubExpr()->getType();
  }
  TreeContext &Ctx = FE.getContext();
  Value *LHS = op.LHS;
  Value *RHS = op.RHS;

  auto LHSFixedSema = Ctx.getFixedPointSemantics(LHSTy);
  auto RHSFixedSema = Ctx.getFixedPointSemantics(RHSTy);
  auto ResultFixedSema = Ctx.getFixedPointSemantics(ResultTy);
  auto CommonFixedSema = LHSFixedSema.getCommonSemantics(RHSFixedSema);

  // Perform the actual operation.
  Value *Result;
  llvm::FixedPointBuilder<CGBuilderTy> FPBuilder(Builder);
  switch (op.Opcode) {
  case BO_AddAssign:
  case BO_Add:
    Result = FPBuilder.CreateAdd(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_SubAssign:
  case BO_Sub:
    Result = FPBuilder.CreateSub(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_MulAssign:
  case BO_Mul:
    Result = FPBuilder.CreateMul(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_DivAssign:
  case BO_Div:
    Result = FPBuilder.CreateDiv(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_ShlAssign:
  case BO_Shl:
    Result = FPBuilder.CreateShl(LHS, LHSFixedSema, RHS);
    break;
  case BO_ShrAssign:
  case BO_Shr:
    Result = FPBuilder.CreateShr(LHS, LHSFixedSema, RHS);
    break;
  case BO_LT:
    return FPBuilder.CreateLT(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_GT:
    return FPBuilder.CreateGT(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_LE:
    return FPBuilder.CreateLE(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_GE:
    return FPBuilder.CreateGE(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_EQ:
    // For equality operations, we assume any padding bits on unsigned types are
    // zero'd out. They could be overwritten through non-saturating operations
    // that cause overflow, but this leads to undefined behavior.
    return FPBuilder.CreateEQ(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_NE:
    return FPBuilder.CreateNE(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_LAnd:
  case BO_LOr:
    llvm_unreachable("Found unimplemented fixed point binary operation");
  case BO_Rem:
  case BO_Xor:
  case BO_And:
  case BO_Or:
  case BO_Assign:
  case BO_RemAssign:
  case BO_AndAssign:
  case BO_XorAssign:
  case BO_OrAssign:
  case BO_Comma:
    llvm_unreachable(
        "Found unsupported binary operation for fixed point types.");
  }

  bool IsShift = BinaryOperator::isShiftOp(op.Opcode) ||
                 BinaryOperator::isShiftAssignOp(op.Opcode);
  return FPBuilder.CreateFixedToFixed(
      Result, IsShift ? LHSFixedSema : CommonFixedSema, ResultFixedSema);
}

Value *ScalarExprEmitter::genSub(const BinOpInfo &op) {
  // The LHS is always a pointer if either side is.
  if (!op.LHS->getType()->isPointerTy()) {
    if (op.Ty->isSignedIntegerOrEnumerationType()) {
      switch (FE.getLangOpts().getSignedOverflowBehavior()) {
      case LangOptions::SOB_Defined:
        return Builder.CreateSub(op.LHS, op.RHS, "sub");
      case LangOptions::SOB_Undefined:
        return Builder.CreateNSWSub(op.LHS, op.RHS, "sub");
      case LangOptions::SOB_Trapping:
        if (canSkipOverflowCheck(FE.getContext(), op))
          return Builder.CreateNSWSub(op.LHS, op.RHS, "sub");
        return genOverflowCheckedBinOp(op);
      }
    }

    // For vector and matrix subs, try to fold into a fmuladd.
    if (op.LHS->getType()->isFPOrFPVectorTy()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, op.FPFeatures);
      // Try to form an fmuladd.
      if (Value *FMulAdd = tryEmitFMulAdd(op, FE, Builder, true))
        return FMulAdd;
    }

    if (op.Ty->isConstantMatrixType()) {
      llvm::MatrixBuilder MB(Builder);
      FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, op.FPFeatures);
      return MB.CreateSub(op.LHS, op.RHS);
    }

    if (op.LHS->getType()->isFPOrFPVectorTy()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, op.FPFeatures);
      return Builder.CreateFSub(op.LHS, op.RHS, "sub");
    }

    if (op.isFixedPointOp())
      return genFixedPointBinOp(op);

    return Builder.CreateSub(op.LHS, op.RHS, "sub");
  }

  // If the RHS is not a pointer, then we have normal pointer
  // arithmetic.
  if (!op.RHS->getType()->isPointerTy())
    return emitPointerArithmetic(FE, op, FunctionEmitter::IsSubtraction);

  // Otherwise, this is a pointer subtraction.

  // Do the raw subtraction part.
  llvm::Value *LHS =
      Builder.CreatePtrToInt(op.LHS, FE.PtrDiffTy, "sub.ptr.lhs.cast");
  llvm::Value *RHS =
      Builder.CreatePtrToInt(op.RHS, FE.PtrDiffTy, "sub.ptr.rhs.cast");
  Value *diffInChars = Builder.CreateSub(LHS, RHS, "sub.ptr.sub");

  // Okay, figure out the element size.
  const BinaryOperator *expr = cast<BinaryOperator>(op.E);
  QualType elementType = expr->getLHS()->getType()->getPointeeType();

  llvm::Value *divisor = nullptr;

  // For a variable-length array, this is going to be non-constant.
  if (const VariableArrayType *vla =
          FE.getContext().getAsVariableArrayType(elementType)) {
    auto VlaSize = FE.getVLASize(vla);
    elementType = VlaSize.Type;
    divisor = VlaSize.NumElts;

    // Scale the number of non-VLA elements by the non-VLA element size.
    CharUnits eltSize = FE.getContext().getTypeSizeInChars(elementType);
    if (!eltSize.isOne())
      divisor = FE.Builder.CreateNUWMul(FE.ME.getSize(eltSize), divisor);

    // For everything elese, we can just compute it, safe in the
    // assumption that Sema won't let anything through that we can't
    // safely compute the size of.
  } else {
    CharUnits elementSize;
    // Handle GCC extension for pointer arithmetic on void* and
    // function pointer types.
    if (elementType->isVoidType() || elementType->isFunctionType())
      elementSize = CharUnits::One();
    else
      elementSize = FE.getContext().getTypeSizeInChars(elementType);

    // Don't even emit the divide for element size of 1.
    if (elementSize.isOne())
      return diffInChars;

    divisor = FE.ME.getSize(elementSize);
  }

  // Otherwise, do a full sdiv. This uses the "exact" form of sdiv, since
  // pointer difference in C is only defined in the case where both operands
  // are pointing to elements of an array.
  return Builder.CreateExactSDiv(diffInChars, divisor, "sub.ptr.div");
}

Value *ScalarExprEmitter::GetWidthMinusOneValue(Value *LHS, Value *RHS) {
  llvm::IntegerType *Ty;
  if (llvm::VectorType *VT = dyn_cast<llvm::VectorType>(LHS->getType()))
    Ty = cast<llvm::IntegerType>(VT->getElementType());
  else
    Ty = cast<llvm::IntegerType>(LHS->getType());
  return llvm::ConstantInt::get(RHS->getType(), Ty->getBitWidth() - 1);
}

Value *ScalarExprEmitter::ConstrainShiftValue(Value *LHS, Value *RHS,
                                              const llvm::Twine &Name) {
  llvm::IntegerType *Ty;
  if (auto *VT = dyn_cast<llvm::VectorType>(LHS->getType()))
    Ty = cast<llvm::IntegerType>(VT->getElementType());
  else
    Ty = cast<llvm::IntegerType>(LHS->getType());

  if (llvm::isPowerOf2_64(Ty->getBitWidth()))
    return Builder.CreateAnd(RHS, GetWidthMinusOneValue(LHS, RHS), Name);

  return Builder.CreateURem(
      RHS, llvm::ConstantInt::get(RHS->getType(), Ty->getBitWidth()), Name);
}

Value *ScalarExprEmitter::genShl(const BinOpInfo &Ops) {
  if (Ops.isFixedPointOp())
    return genFixedPointBinOp(Ops);

  // LLVM requires the LHS and RHS to be the same type: promote or truncate the
  // RHS to the same size as the LHS.
  Value *RHS = Ops.RHS;
  if (Ops.LHS->getType() != RHS->getType())
    RHS = Builder.CreateIntCast(RHS, Ops.LHS->getType(), false, "sh_prom");

  return Builder.CreateShl(Ops.LHS, RHS, "shl");
}

Value *ScalarExprEmitter::genShr(const BinOpInfo &Ops) {
  if (Ops.isFixedPointOp())
    return genFixedPointBinOp(Ops);

  // LLVM requires the LHS and RHS to be the same type: promote or truncate the
  // RHS to the same size as the LHS.
  Value *RHS = Ops.RHS;
  if (Ops.LHS->getType() != RHS->getType())
    RHS = Builder.CreateIntCast(RHS, Ops.LHS->getType(), false, "sh_prom");

  if (Ops.Ty->hasUnsignedIntegerRepresentation())
    return Builder.CreateLShr(Ops.LHS, RHS, "shr");
  return Builder.CreateAShr(Ops.LHS, RHS, "shr");
}

Value *ScalarExprEmitter::genCompare(const BinaryOperator *E,
                                     llvm::CmpInst::Predicate UICmpOpc,
                                     llvm::CmpInst::Predicate SICmpOpc,
                                     llvm::CmpInst::Predicate FCmpOpc,
                                     bool IsSignaling) {
  TestAndClearIgnoreResultAssign();
  Value *Result;
  QualType LHSTy = E->getLHS()->getType();
  QualType RHSTy = E->getRHS()->getType();
  if (!LHSTy->isAnyComplexType() && !RHSTy->isAnyComplexType()) {
    BinOpInfo BOInfo = genBinOps(E);
    Value *LHS = BOInfo.LHS;
    Value *RHS = BOInfo.RHS;

    if (LHSTy->isVectorType() && !E->getType()->isVectorType())
      llvm_unreachable("Vector comparison producing scalar is not supported");

    if (BOInfo.isFixedPointOp()) {
      Result = genFixedPointBinOp(BOInfo);
    } else if (LHS->getType()->isFPOrFPVectorTy()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, BOInfo.FPFeatures);
      if (!IsSignaling)
        Result = Builder.CreateFCmp(FCmpOpc, LHS, RHS, "cmp");
      else
        Result = Builder.CreateFCmpS(FCmpOpc, LHS, RHS, "cmp");
    } else if (LHSTy->hasSignedIntegerRepresentation()) {
      Result = Builder.CreateICmp(SICmpOpc, LHS, RHS, "cmp");
    } else {
      Result = Builder.CreateICmp(UICmpOpc, LHS, RHS, "cmp");
    }

    // If this is a vector comparison, sign extend the result to the appropriate
    // vector integer type and return it (don't convert to bool).
    if (LHSTy->isVectorType())
      return Builder.CreateSExt(Result, convertType(E->getType()), "sext");

  } else {
    // Complex Comparison: can only be an equality comparison.
    FunctionEmitter::ComplexPairTy LHS, RHS;
    QualType CETy;
    if (auto *CTy = LHSTy->getAs<ComplexType>()) {
      LHS = FE.genComplexExpr(E->getLHS());
      CETy = CTy->getElementType();
    } else {
      LHS.first = Visit(E->getLHS());
      LHS.second = llvm::Constant::getNullValue(LHS.first->getType());
      CETy = LHSTy;
    }
    if (auto *CTy = RHSTy->getAs<ComplexType>()) {
      RHS = FE.genComplexExpr(E->getRHS());
      assert(
          FE.getContext().hasSameUnqualifiedType(CETy, CTy->getElementType()) &&
          "The element types must always match.");
      (void)CTy;
    } else {
      RHS.first = Visit(E->getRHS());
      RHS.second = llvm::Constant::getNullValue(RHS.first->getType());
      assert(FE.getContext().hasSameUnqualifiedType(CETy, RHSTy) &&
             "The element types must always match.");
    }

    Value *ResultR, *ResultI;
    if (CETy->isRealFloatingType()) {
      // As complex comparisons can only be equality comparisons, they
      // are never signaling comparisons.
      ResultR = Builder.CreateFCmp(FCmpOpc, LHS.first, RHS.first, "cmp.r");
      ResultI = Builder.CreateFCmp(FCmpOpc, LHS.second, RHS.second, "cmp.i");
    } else {
      // Complex comparisons can only be equality comparisons.  As such, signed
      // and unsigned opcodes are the same.
      ResultR = Builder.CreateICmp(UICmpOpc, LHS.first, RHS.first, "cmp.r");
      ResultI = Builder.CreateICmp(UICmpOpc, LHS.second, RHS.second, "cmp.i");
    }

    if (E->getOpcode() == BO_EQ) {
      Result = Builder.CreateAnd(ResultR, ResultI, "and.ri");
    } else {
      assert(E->getOpcode() == BO_NE &&
             "Complex comparison other than == or != ?");
      Result = Builder.CreateOr(ResultR, ResultI, "or.ri");
    }
  }

  return genScalarConversion(Result, FE.getContext().BoolTy, E->getType(),
                             E->getExprLoc());
}

Value *ScalarExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  bool Ignore = TestAndClearIgnoreResultAssign();

  Value *RHS;
  LValue LHS;

  RHS = Visit(E->getRHS());
  LHS = genCheckedLValue(E->getLHS(), FunctionEmitter::TCK_Store);
  if (LHS.isBitField()) {
    FE.genStoreThroughBitfieldLValue(RValue::get(RHS), LHS, &RHS);
  } else {
    FE.genStoreThroughLValue(RValue::get(RHS), LHS);
  }

  if (Ignore)
    return nullptr;

  return RHS;
}

// ===----------------------------------------------------------------------===
// Logical & conditional operators
// ===----------------------------------------------------------------------===

Value *ScalarExprEmitter::VisitBinLAnd(const BinaryOperator *E) {
  // Perform vector logical and on comparisons with zero vectors.
  if (E->getType()->isVectorType()) {

    Value *LHS = Visit(E->getLHS());
    Value *RHS = Visit(E->getRHS());
    Value *Zero = llvm::ConstantAggregateZero::get(LHS->getType());
    if (LHS->getType()->isFPOrFPVectorTy()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(
          FE, E->getFPFeaturesInEffect(FE.getLangOpts()));
      LHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, LHS, Zero, "cmp");
      RHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, RHS, Zero, "cmp");
    } else {
      LHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, LHS, Zero, "cmp");
      RHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, RHS, Zero, "cmp");
    }
    Value *And = Builder.CreateAnd(LHS, RHS);
    return Builder.CreateSExt(And, convertType(E->getType()), "sext");
  }

  llvm::Type *ResTy = convertType(E->getType());

  // If we have 0 && RHS, see if we can elide RHS, if so, just return 0.
  // If we have 1 && X, just emit X without inserting the control flow.
  bool LHSCondVal;
  if (FE.constantFoldsToSimpleInteger(E->getLHS(), LHSCondVal)) {
    if (LHSCondVal) { // If we have 1 && X, just emit X.

      Value *RHSCond = FE.evaluateExprAsBool(E->getRHS());

      // ZExt result to int or bool.
      return Builder.CreateZExtOrBitCast(RHSCond, ResTy, "land.ext");
    }

    // 0 && RHS: If it is safe, just elide the RHS, and return 0/false.
    if (!FE.containsLabel(E->getRHS()))
      return llvm::Constant::getNullValue(ResTy);
  }

  llvm::BasicBlock *ContBlock = FE.createBasicBlock("land.end");
  llvm::BasicBlock *RHSBlock = FE.createBasicBlock("land.rhs");

  FunctionEmitter::ConditionalEvaluation eval(FE);

  // Branch on the LHS first.  If it is false, go to the failure (cont) block.
  FE.genBranchOnBoolExpr(E->getLHS(), RHSBlock, ContBlock);

  // Any edges into the ContBlock are now from an (indeterminate number of)
  // edges from this first condition.  All of these values will be false.  Start
  // setting up the PHI node in the Cont Block for this.
  llvm::PHINode *PN =
      llvm::PHINode::Create(llvm::Type::getInt1Ty(VMContext), 2, "", ContBlock);
  for (llvm::pred_iterator PI = pred_begin(ContBlock), PE = pred_end(ContBlock);
       PI != PE; ++PI)
    PN->addIncoming(llvm::ConstantInt::getFalse(VMContext), *PI);

  eval.begin(FE);
  FE.genBlock(RHSBlock);
  Value *RHSCond = FE.evaluateExprAsBool(E->getRHS());
  eval.end(FE);

  // Reaquire the RHS block, as there may be subblocks inserted.
  RHSBlock = Builder.GetInsertBlock();

  {
    auto NL = ApplyDebugLocation::CreateEmpty(FE);
    FE.genBlock(ContBlock);
  }
  PN->addIncoming(RHSCond, RHSBlock);

  // Artificial location to preserve the scope information
  {
    auto NL = ApplyDebugLocation::CreateArtificial(FE);
    PN->setDebugLoc(Builder.getCurrentDebugLocation());
  }

  // ZExt result to int.
  return Builder.CreateZExtOrBitCast(PN, ResTy, "land.ext");
}

Value *ScalarExprEmitter::VisitBinLOr(const BinaryOperator *E) {
  // Perform vector logical or on comparisons with zero vectors.
  if (E->getType()->isVectorType()) {

    Value *LHS = Visit(E->getLHS());
    Value *RHS = Visit(E->getRHS());
    Value *Zero = llvm::ConstantAggregateZero::get(LHS->getType());
    if (LHS->getType()->isFPOrFPVectorTy()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(
          FE, E->getFPFeaturesInEffect(FE.getLangOpts()));
      LHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, LHS, Zero, "cmp");
      RHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, RHS, Zero, "cmp");
    } else {
      LHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, LHS, Zero, "cmp");
      RHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, RHS, Zero, "cmp");
    }
    Value *Or = Builder.CreateOr(LHS, RHS);
    return Builder.CreateSExt(Or, convertType(E->getType()), "sext");
  }

  llvm::Type *ResTy = convertType(E->getType());

  // If we have 1 || RHS, see if we can elide RHS, if so, just return 1.
  // If we have 0 || X, just emit X without inserting the control flow.
  bool LHSCondVal;
  if (FE.constantFoldsToSimpleInteger(E->getLHS(), LHSCondVal)) {
    if (!LHSCondVal) { // If we have 0 || X, just emit X.

      Value *RHSCond = FE.evaluateExprAsBool(E->getRHS());

      // ZExt result to int or bool.
      return Builder.CreateZExtOrBitCast(RHSCond, ResTy, "lor.ext");
    }

    // 1 || RHS: If it is safe, just elide the RHS, and return 1/true.
    if (!FE.containsLabel(E->getRHS()))
      return llvm::ConstantInt::get(ResTy, 1);
  }

  llvm::BasicBlock *ContBlock = FE.createBasicBlock("lor.end");
  llvm::BasicBlock *RHSBlock = FE.createBasicBlock("lor.rhs");

  FunctionEmitter::ConditionalEvaluation eval(FE);

  // Branch on the LHS first.  If it is true, go to the success (cont) block.
  FE.genBranchOnBoolExpr(E->getLHS(), ContBlock, RHSBlock);

  // Any edges into the ContBlock are now from an (indeterminate number of)
  // edges from this first condition.  All of these values will be true.  Start
  // setting up the PHI node in the Cont Block for this.
  llvm::PHINode *PN =
      llvm::PHINode::Create(llvm::Type::getInt1Ty(VMContext), 2, "", ContBlock);
  for (llvm::pred_iterator PI = pred_begin(ContBlock), PE = pred_end(ContBlock);
       PI != PE; ++PI)
    PN->addIncoming(llvm::ConstantInt::getTrue(VMContext), *PI);

  eval.begin(FE);

  FE.genBlock(RHSBlock);
  Value *RHSCond = FE.evaluateExprAsBool(E->getRHS());

  eval.end(FE);

  // Reaquire the RHS block, as there may be subblocks inserted.
  RHSBlock = Builder.GetInsertBlock();

  FE.genBlock(ContBlock);
  PN->addIncoming(RHSCond, RHSBlock);

  // ZExt result to int.
  return Builder.CreateZExtOrBitCast(PN, ResTy, "lor.ext");
}

Value *ScalarExprEmitter::VisitBinComma(const BinaryOperator *E) {
  FE.genIgnoredExpr(E->getLHS());
  FE.ensureInsertPoint();
  return Visit(E->getRHS());
}

namespace {
bool isLowCostUnconditionalEval(const Expr *E, FunctionEmitter &FE) {
  return E->IgnoreParens()->isEvaluatable(FE.getContext());
}
} // namespace

Value *ScalarExprEmitter::VisitAbstractConditionalOperator(
    const AbstractConditionalOperator *E) {
  TestAndClearIgnoreResultAssign();

  // Bind the common expression if necessary.
  FunctionEmitter::OpaqueValueMapping binding(FE, E);

  Expr *condExpr = E->getCond();
  Expr *lhsExpr = E->getTrueExpr();
  Expr *rhsExpr = E->getFalseExpr();

  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm.
  bool CondExprBool;
  if (FE.constantFoldsToSimpleInteger(condExpr, CondExprBool)) {
    Expr *live = lhsExpr, *dead = rhsExpr;
    if (!CondExprBool)
      std::swap(live, dead);

    // If the dead side doesn't have labels we need, just emit the Live part.
    if (!FE.containsLabel(dead)) {
      Value *Result = Visit(live);

      // If the live part is a throw expression, it acts like it has a void
      // type, so evaluating it returns a null Value*.  However, a conditional
      // with non-void type must return a non-null Value*.
      if (!Result && !E->getType()->isVoidType())
        Result = llvm::UndefValue::get(FE.convertType(E->getType()));

      return Result;
    }
  }

  if (condExpr->getType()->isExtVectorType()) {

    llvm::Value *CondV = FE.genScalarExpr(condExpr);
    llvm::Value *LHS = Visit(lhsExpr);
    llvm::Value *RHS = Visit(rhsExpr);

    llvm::Type *condType = convertType(condExpr->getType());
    auto *vecTy = cast<llvm::FixedVectorType>(condType);

    unsigned numElem = vecTy->getNumElements();
    llvm::Type *elemType = vecTy->getElementType();

    llvm::Value *zeroVec = llvm::Constant::getNullValue(vecTy);
    llvm::Value *TestMSB = Builder.CreateICmpSLT(CondV, zeroVec);
    llvm::Value *tmp = Builder.CreateSExt(
        TestMSB, llvm::FixedVectorType::get(elemType, numElem), "sext");
    llvm::Value *tmp2 = Builder.CreateNot(tmp);

    // Cast float to int to perform ANDs if necessary.
    llvm::Value *RHSTmp = RHS;
    llvm::Value *LHSTmp = LHS;
    bool wasCast = false;
    llvm::VectorType *rhsVTy = cast<llvm::VectorType>(RHS->getType());
    if (rhsVTy->getElementType()->isFloatingPointTy()) {
      RHSTmp = Builder.CreateBitCast(RHS, tmp2->getType());
      LHSTmp = Builder.CreateBitCast(LHS, tmp->getType());
      wasCast = true;
    }

    llvm::Value *tmp3 = Builder.CreateAnd(RHSTmp, tmp2);
    llvm::Value *tmp4 = Builder.CreateAnd(LHSTmp, tmp);
    llvm::Value *tmp5 = Builder.CreateOr(tmp3, tmp4, "cond");
    if (wasCast)
      tmp5 = Builder.CreateBitCast(tmp5, RHS->getType());

    return tmp5;
  }

  if (condExpr->getType()->isVectorType() ||
      condExpr->getType()->isSveVLSBuiltinType()) {

    llvm::Value *CondV = FE.genScalarExpr(condExpr);
    llvm::Value *LHS = Visit(lhsExpr);
    llvm::Value *RHS = Visit(rhsExpr);

    llvm::Type *CondType = convertType(condExpr->getType());
    auto *VecTy = cast<llvm::VectorType>(CondType);
    llvm::Value *ZeroVec = llvm::Constant::getNullValue(VecTy);

    CondV = Builder.CreateICmpNE(CondV, ZeroVec, "vector_cond");
    return Builder.CreateSelect(CondV, LHS, RHS, "vector_select");
  }

  // If this is a really simple expression (like x ? 4 : 5), emit this as a
  // select instead of as control flow.  We can only do this if it is cheap and
  // safe to evaluate the LHS and RHS unconditionally.
  if (isLowCostUnconditionalEval(lhsExpr, FE) &&
      isLowCostUnconditionalEval(rhsExpr, FE)) {
    llvm::Value *CondV = FE.evaluateExprAsBool(condExpr);
    llvm::Value *StepV = Builder.CreateZExtOrBitCast(CondV, FE.Int64Ty);

    llvm::Value *LHS = Visit(lhsExpr);
    llvm::Value *RHS = Visit(rhsExpr);
    if (!LHS) {
      // If the conditional has void type, make sure we return a null Value*.
      assert(!RHS && "LHS and RHS types must match");
      return nullptr;
    }
    return Builder.CreateSelect(CondV, LHS, RHS, "cond");
  }

  llvm::BasicBlock *LHSBlock = FE.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = FE.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = FE.createBasicBlock("cond.end");

  FunctionEmitter::ConditionalEvaluation eval(FE);
  FE.genBranchOnBoolExpr(condExpr, LHSBlock, RHSBlock);

  FE.genBlock(LHSBlock);
  eval.begin(FE);
  Value *LHS = Visit(lhsExpr);
  eval.end(FE);

  LHSBlock = Builder.GetInsertBlock();
  Builder.CreateBr(ContBlock);

  FE.genBlock(RHSBlock);
  eval.begin(FE);
  Value *RHS = Visit(rhsExpr);
  eval.end(FE);

  RHSBlock = Builder.GetInsertBlock();
  FE.genBlock(ContBlock);

  // If the LHS or RHS is a throw expression, it will be legitimately null.
  if (!LHS)
    return RHS;
  if (!RHS)
    return LHS;

  llvm::PHINode *PN = Builder.CreatePHI(LHS->getType(), 2, "cond");
  PN->addIncoming(LHS, LHSBlock);
  PN->addIncoming(RHS, RHSBlock);
  return PN;
}

Value *ScalarExprEmitter::VisitChooseExpr(ChooseExpr *E) {
  return Visit(E->getChosenSubExpr());
}

Value *ScalarExprEmitter::VisitVAArgExpr(VAArgExpr *VE) {
  QualType Ty = VE->getType();

  if (Ty->isVariablyModifiedType())
    FE.genVariablyModifiedType(Ty);

  Address ArgValue = Address::invalid();
  Address ArgPtr = FE.genVAArg(VE, ArgValue);

  llvm::Type *ArgTy = convertType(VE->getType());

  // If genVAArg fails, emit an error.
  if (!ArgPtr.isValid()) {
    FE.errorUnsupported(VE, "va_arg expression");
    return llvm::UndefValue::get(ArgTy);
  }

  llvm::Value *Val = Builder.CreateLoad(ArgPtr);

  // If genVAArg promoted the type, we must truncate it.
  if (ArgTy != Val->getType()) {
    if (ArgTy->isPointerTy() && !Val->getType()->isPointerTy())
      Val = Builder.CreateIntToPtr(Val, ArgTy);
    else
      Val = Builder.CreateTrunc(Val, ArgTy);
  }

  return Val;
}

Value *ScalarExprEmitter::VisitAtomicExpr(AtomicExpr *E) {
  return FE.genAtomicExpr(E).getScalarVal();
}

// ===----------------------------------------------------------------------===
// FunctionEmitter entry points
// ===----------------------------------------------------------------------===

__attribute__((hot)) Value *
FunctionEmitter::genScalarExpr(const Expr *E, bool IgnoreResultAssign) {
  assert(E && hasScalarEvaluationKind(E->getType()) &&
         "Invalid scalar expression to emit");

  return ScalarExprEmitter(*this, IgnoreResultAssign)
      .Visit(const_cast<Expr *>(E));
}

Value *FunctionEmitter::genScalarConversion(Value *Src, QualType SrcTy,
                                            QualType DstTy,
                                            SourceLocation Loc) {
  assert(hasScalarEvaluationKind(SrcTy) && hasScalarEvaluationKind(DstTy) &&
         "Invalid scalar expression to emit");
  return ScalarExprEmitter(*this).genScalarConversion(Src, SrcTy, DstTy, Loc);
}

Value *FunctionEmitter::genComplexToScalarConversion(ComplexPairTy Src,
                                                     QualType SrcTy,
                                                     QualType DstTy,
                                                     SourceLocation Loc) {
  assert(SrcTy->isAnyComplexType() && hasScalarEvaluationKind(DstTy) &&
         "Invalid complex -> scalar conversion");
  return ScalarExprEmitter(*this).genComplexToScalarConversion(Src, SrcTy,
                                                               DstTy, Loc);
}

__attribute__((hot)) Value *
FunctionEmitter::genPromotedScalarExpr(const Expr *E, QualType PromotionType) {
  if (!PromotionType.isNull())
    return ScalarExprEmitter(*this).genPromoted(E, PromotionType);
  else
    return ScalarExprEmitter(*this).Visit(const_cast<Expr *>(E));
}

llvm::Value *FunctionEmitter::genScalarPrePostIncDec(const UnaryOperator *E,
                                                     LValue LV, bool isInc,
                                                     bool isPre) {
  return ScalarExprEmitter(*this).genScalarPrePostIncDec(E, LV, isInc, isPre);
}

LValue
FunctionEmitter::genCompoundAssignmentLValue(const CompoundAssignOperator *E) {
  ScalarExprEmitter Scalar(*this);
  Value *Result = nullptr;
  switch (E->getOpcode()) {
#define COMPOUND_OP(Op)                                                        \
  case BO_##Op##Assign:                                                        \
    return Scalar.genCompoundAssignLValue(E, &ScalarExprEmitter::gen##Op,      \
                                          Result)
    COMPOUND_OP(Mul);
    COMPOUND_OP(Div);
    COMPOUND_OP(Rem);
    COMPOUND_OP(Add);
    COMPOUND_OP(Sub);
    COMPOUND_OP(Shl);
    COMPOUND_OP(Shr);
    COMPOUND_OP(And);
    COMPOUND_OP(Xor);
    COMPOUND_OP(Or);
#undef COMPOUND_OP

  case BO_Mul:
  case BO_Div:
  case BO_Rem:
  case BO_Add:
  case BO_Sub:
  case BO_Shl:
  case BO_Shr:
  case BO_LT:
  case BO_GT:
  case BO_LE:
  case BO_GE:
  case BO_EQ:
  case BO_NE:
  case BO_And:
  case BO_Xor:
  case BO_Or:
  case BO_LAnd:
  case BO_LOr:
  case BO_Assign:
  case BO_Comma:
    llvm_unreachable("Not valid compound assignment operators");
  }

  llvm_unreachable("Unhandled compound assignment operator");
}

Value *FunctionEmitter::genCheckedInBoundsGEP(llvm::Type *ElemTy, Value *Ptr,
                                              llvm::ArrayRef<Value *> IdxList,
                                              bool, bool, SourceLocation,
                                              const llvm::Twine &Name) {
  return Builder.CreateInBoundsGEP(ElemTy, Ptr, IdxList, Name);
}
