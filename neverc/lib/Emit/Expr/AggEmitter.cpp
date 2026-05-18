#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
using namespace neverc;
using namespace Emit;

namespace {
class AggExprEmitter : public StmtVisitor<AggExprEmitter> {
  FunctionEmitter &FE;
  CGBuilderTy &Builder;
  AggValueSlot Dest;
  bool IsResultUnused;

  AggValueSlot EnsureSlot(QualType T) {
    if (!Dest.isIgnored())
      return Dest;
    return FE.createAggTemp(T, "agg.tmp.ensured");
  }
  void EnsureDest(QualType T) {
    if (!Dest.isIgnored())
      return;
    Dest = FE.createAggTemp(T, "agg.tmp.ensured");
  }

  // Calls `Fn` with a valid return value slot, potentially creating a temporary
  // to do so. If a temporary is created, an appropriate copy into `Dest` will
  // be emitted, as will lifetime markers.
  //
  // The given function should take a ReturnValueSlot, and return an RValue that
  // points to said slot.
  void withReturnValueSlot(const Expr *E,
                           llvm::function_ref<RValue(ReturnValueSlot)> Fn);

public:
  AggExprEmitter(FunctionEmitter &cgf, AggValueSlot Dest, bool IsResultUnused)
      : FE(cgf), Builder(FE.Builder), Dest(Dest),
        IsResultUnused(IsResultUnused) {}

  //===--------------------------------------------------------------------===
  //                               Utilities
  //===--------------------------------------------------------------------===

  void genAggLoadOfLValue(const Expr *E);

  enum ExprValueKind { EVK_RValue, EVK_NonRValue };

  void genFinalDestCopy(QualType type, const LValue &src,
                        ExprValueKind SrcValueKind = EVK_NonRValue);
  void genFinalDestCopy(QualType type, RValue src);
  void genCopy(QualType type, const AggValueSlot &dest,
               const AggValueSlot &src);

  void genArrayInit(Address DestPtr, llvm::ArrayType *AType, QualType ArrayQTy,
                    Expr *ExprToVisit, llvm::ArrayRef<Expr *> Args,
                    Expr *ArrayFiller);

  //===--------------------------------------------------------------------===
  //                            Visitor Methods
  //===--------------------------------------------------------------------===

  void Visit(Expr *E) {
    ApplyDebugLocation DL(FE, E);
    StmtVisitor<AggExprEmitter>::Visit(E);
  }

  void VisitStmt(Stmt *S) { FE.errorUnsupported(S, "aggregate expression"); }
  void VisitParenExpr(ParenExpr *PE) { Visit(PE->getSubExpr()); }
  void VisitGenericSelectionExpr(GenericSelectionExpr *GE) {
    Visit(GE->getResultExpr());
  }
  void VisitUnaryExtension(UnaryOperator *E) { Visit(E->getSubExpr()); }

  void VisitConstantExpr(ConstantExpr *E) {
    EnsureDest(E->getType());

    if (llvm::Value *Result = ConstantEmitter(FE).tryEmitConstantExpr(E)) {
      Address StoreDest = Dest.getAddress();
      // The emitted value is guaranteed to have the same size as the
      // destination but can have a different type. Just do a bitcast in this
      // case to avoid incorrect GEPs.
      if (Result->getType() != StoreDest.getType())
        StoreDest = StoreDest.withElementType(Result->getType());

      FE.genAggregateStore(Result, StoreDest,
                           E->getType().isVolatileQualified());
      return;
    }
    return Visit(E->getSubExpr());
  }

  // l-values.
  void VisitDeclRefExpr(DeclRefExpr *E) { genAggLoadOfLValue(E); }
  void VisitMemberExpr(MemberExpr *ME) { genAggLoadOfLValue(ME); }
  void VisitUnaryDeref(UnaryOperator *E) { genAggLoadOfLValue(E); }
  void VisitStringLiteral(StringLiteral *E) { genAggLoadOfLValue(E); }
  void VisitCompoundLiteralExpr(CompoundLiteralExpr *E);
  void VisitArraySubscriptExpr(ArraySubscriptExpr *E) { genAggLoadOfLValue(E); }
  void VisitPredefinedExpr(const PredefinedExpr *E) { genAggLoadOfLValue(E); }

  // Operators.
  void VisitCastExpr(CastExpr *E);
  void VisitCallExpr(const CallExpr *E);
  void VisitStmtExpr(const StmtExpr *E);
  void VisitBinaryOperator(const BinaryOperator *BO);
  void VisitBinAssign(const BinaryOperator *E);
  void VisitBinComma(const BinaryOperator *E);

  void VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E);
  void VisitAbstractConditionalOperator(const AbstractConditionalOperator *CO);
  void VisitChooseExpr(const ChooseExpr *CE);
  void VisitInitListExpr(InitListExpr *E);
  void VisitParenListOrInitListExpr(Expr *ExprToVisit,
                                    llvm::ArrayRef<Expr *> Args,
                                    FieldDecl *InitializedFieldInUnion,
                                    Expr *ArrayFiller);
  void VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E,
                              llvm::Value *outerBegin = nullptr);
  void VisitImplicitValueInitExpr(ImplicitValueInitExpr *E);
  void VisitNoInitExpr(NoInitExpr *E) {} // Do nothing.
  void VisitExprWithCleanups(ExprWithCleanups *E);
  void VisitOpaqueValueExpr(OpaqueValueExpr *E);

  void VisitPseudoObjectExpr(PseudoObjectExpr *E) {
    if (E->isLValue()) {
      LValue LV = FE.genPseudoObjectLValue(E);
      return genFinalDestCopy(E->getType(), LV);
    }

    AggValueSlot Slot = EnsureSlot(E->getType());
    FE.genPseudoObjectRValue(E, Slot);
  }

  void VisitVAArgExpr(VAArgExpr *E);

  void genInitializationToLValue(Expr *E, LValue Address);
  void genNullInitializationToLValue(LValue Address);
  void VisitAtomicExpr(AtomicExpr *E) {
    RValue Res = FE.genAtomicExpr(E);
    genFinalDestCopy(E->getType(), Res);
  }
};
} // end anonymous namespace.

// ===----------------------------------------------------------------------===
// Value copy & array initialization
// ===----------------------------------------------------------------------===

void AggExprEmitter::genAggLoadOfLValue(const Expr *E) {
  LValue LV = FE.genLValue(E);

  // If the type of the l-value is atomic, then do an atomic load.
  if (LV.getType()->isAtomicType() || FE.lValueIsSuitableForInlineAtomic(LV)) {
    FE.genAtomicLoad(LV, E->getExprLoc(), Dest);
    return;
  }

  genFinalDestCopy(E->getType(), LV);
}

void AggExprEmitter::withReturnValueSlot(
    const Expr *E, llvm::function_ref<RValue(ReturnValueSlot)> genCall) {
  QualType RetTy = E->getType();

  bool UseTemp = Dest.isPotentiallyAliased();

  Address RetAddr = Address::invalid();
  Address RetAllocaAddr = Address::invalid();

  EHScopeStack::stable_iterator LifetimeEndBlock;
  llvm::Value *LifetimeSizePtr = nullptr;
  llvm::IntrinsicInst *LifetimeStartInst = nullptr;
  if (!UseTemp) {
    RetAddr = Dest.getAddress();
  } else {
    RetAddr = FE.createMemTemp(RetTy, "tmp", &RetAllocaAddr);
    llvm::TypeSize Size =
        FE.ME.getDataLayout().getTypeAllocSize(FE.convertTypeForMem(RetTy));
    LifetimeSizePtr = FE.genLifetimeStart(Size, RetAllocaAddr.getPointer());
    if (LifetimeSizePtr) {
      LifetimeStartInst =
          cast<llvm::IntrinsicInst>(std::prev(Builder.GetInsertPoint()));
      assert(LifetimeStartInst->getIntrinsicID() ==
                 llvm::Intrinsic::lifetime_start &&
             "Last insertion wasn't a lifetime.start?");

      FE.pushFullExprCleanup<FunctionEmitter::CallLifetimeEnd>(
          NormalEHLifetimeMarker, RetAllocaAddr, LifetimeSizePtr);
      LifetimeEndBlock = FE.EHStack.stable_begin();
    }
  }

  RValue Src =
      genCall(ReturnValueSlot(RetAddr, Dest.isVolatile(), IsResultUnused,
                              Dest.isExternallyDestructed()));

  if (!UseTemp)
    return;

  assert(Dest.isIgnored() || Dest.getPointer() != Src.getAggregatePointer());
  genFinalDestCopy(E->getType(), Src);

  if (LifetimeStartInst) {
    // If there's no dtor to run, the copy was the last use of our temporary.
    // Since we're not guaranteed to be in an ExprWithCleanups, clean up
    // eagerly.
    FE.deactivateCleanupBlock(LifetimeEndBlock, LifetimeStartInst);
    FE.genLifetimeEnd(LifetimeSizePtr, RetAllocaAddr.getPointer());
  }
}

void AggExprEmitter::genFinalDestCopy(QualType type, RValue src) {
  assert(src.isAggregate() && "value must be aggregate value!");
  LValue srcLV = FE.makeAddrLValue(src.getAggregateAddress(), type);
  genFinalDestCopy(type, srcLV, EVK_RValue);
}

void AggExprEmitter::genFinalDestCopy(QualType type, const LValue &src,
                                      ExprValueKind SrcValueKind) {
  // If Dest is ignored, then we're evaluating an aggregate expression
  // in a context that doesn't care about the result.  Note that loads
  // from volatile l-values force the existence of a non-ignored
  // destination.
  if (Dest.isIgnored())
    return;

  AggValueSlot srcAgg = AggValueSlot::forLValue(
      src, FE, AggValueSlot::IsDestructed, AggValueSlot::IsAliased,
      AggValueSlot::MayOverlap);
  genCopy(type, Dest, srcAgg);
}

void AggExprEmitter::genCopy(QualType type, const AggValueSlot &dest,
                             const AggValueSlot &src) {

  // If the result of the assignment is used, copy the LHS there also.
  // It's volatile if either side is.  Use the minimum alignment of
  // the two sides.
  LValue DestLV = FE.makeAddrLValue(dest.getAddress(), type);
  LValue SrcLV = FE.makeAddrLValue(src.getAddress(), type);
  FE.genAggregateCopy(DestLV, SrcLV, type, dest.mayOverlap(),
                      dest.isVolatile() || src.isVolatile());
}

namespace {
bool isTrivialFiller(Expr *E) {
  if (!E)
    return true;

  if (isa<ImplicitValueInitExpr>(E))
    return true;

  if (auto *ILE = dyn_cast<InitListExpr>(E)) {
    if (ILE->getNumInits())
      return false;
    return isTrivialFiller(ILE->getArrayFiller());
  }

  return false;
}
} // namespace

void AggExprEmitter::genArrayInit(Address DestPtr, llvm::ArrayType *AType,
                                  QualType ArrayQTy, Expr *ExprToVisit,
                                  llvm::ArrayRef<Expr *> Args,
                                  Expr *ArrayFiller) {
  uint64_t NumInitElements = Args.size();

  uint64_t NumArrayElements = AType->getNumElements();
  assert(NumInitElements <= NumArrayElements);

  QualType elementType =
      FE.getContext().getAsArrayType(ArrayQTy)->getElementType();

  // DestPtr is an array*.  Construct an elementType* by drilling
  // down a level.
  llvm::Value *zero = llvm::ConstantInt::get(FE.SizeTy, 0);
  llvm::Value *indices[] = {zero, zero};
  llvm::Value *begin =
      Builder.CreateInBoundsGEP(DestPtr.getElementType(), DestPtr.getPointer(),
                                indices, "arrayinit.begin");

  CharUnits elementSize = FE.getContext().getTypeSizeInChars(elementType);
  CharUnits elementAlign =
      DestPtr.getAlignment().alignmentOfArrayElement(elementSize);
  llvm::Type *llvmElementType = FE.convertTypeForMem(elementType);

  // Consider initializing the array by copying from a global. For this to be
  // more efficient than per-element initialization, the size of the elements
  // with explicit initializers should be large enough.
  if (NumInitElements * elementSize.getQuantity() > 16 &&
      elementType.isTriviallyCopyableType(FE.getContext())) {
    Emit::ModuleEmitter &ME = FE.ME;
    ConstantEmitter Emitter(FE);
    LangAS AS = ArrayQTy.getAddressSpace();
    if (llvm::Constant *C =
            Emitter.tryEmitForInitializer(ExprToVisit, AS, ArrayQTy)) {
      auto GV = new llvm::GlobalVariable(
          ME.getModule(), C->getType(),
          /* isConstant= */ true, llvm::GlobalValue::PrivateLinkage, C,
          "constinit",
          /* InsertBefore= */ nullptr, llvm::GlobalVariable::NotThreadLocal,
          ME.getContext().getTargetAddressSpace(AS));
      Emitter.finalize(GV);
      CharUnits Align = ME.getContext().getTypeAlignInChars(ArrayQTy);
      GV->setAlignment(Align.getAsAlign());
      Address GVAddr(GV, GV->getValueType(), Align);
      genFinalDestCopy(ArrayQTy, FE.makeAddrLValue(GVAddr, ArrayQTy));
      return;
    }
  }

  QualType::DestructionKind dtorKind = QualType::DK_none;
  Address endOfInit = Address::invalid();
  EHScopeStack::stable_iterator cleanup;
  llvm::Instruction *cleanupDominator = nullptr;

  llvm::Value *one = llvm::ConstantInt::get(FE.SizeTy, 1);

  // The 'current element to initialize'.  The invariants on this
  // variable are complicated.  Essentially, after each iteration of
  // the loop, it points to the last initialized element, except
  // that it points to the beginning of the array before any
  // elements have been initialized.
  llvm::Value *element = begin;

  for (uint64_t i = 0; i != NumInitElements; ++i) {
    if (i > 0) {
      element = Builder.CreateInBoundsGEP(llvmElementType, element, one,
                                          "arrayinit.element");

      // Tell the cleanup that it needs to destroy up to this
      // element.
      if (endOfInit.isValid())
        Builder.CreateStore(element, endOfInit);
    }

    LValue elementLV = FE.makeAddrLValue(
        Address(element, llvmElementType, elementAlign), elementType);
    genInitializationToLValue(Args[i], elementLV);
  }

  bool hasTrivialFiller = isTrivialFiller(ArrayFiller);

  // Any remaining elements need to be zero-initialized, possibly
  // using the filler expression.  We can skip this if the we're
  // emitting to zeroed memory.
  if (NumInitElements != NumArrayElements &&
      !(Dest.isZeroed() && hasTrivialFiller &&
        FE.getTypes().isZeroInitializable(elementType))) {

    // Use an actual loop.  This is basically
    //   do { *array++ = filler; } while (array != end);

    if (NumInitElements) {
      element = Builder.CreateInBoundsGEP(llvmElementType, element, one,
                                          "arrayinit.start");
      if (endOfInit.isValid())
        Builder.CreateStore(element, endOfInit);
    }

    llvm::Value *end = Builder.CreateInBoundsGEP(
        llvmElementType, begin,
        llvm::ConstantInt::get(FE.SizeTy, NumArrayElements), "arrayinit.end");

    llvm::BasicBlock *entryBB = Builder.GetInsertBlock();
    llvm::BasicBlock *bodyBB = FE.createBasicBlock("arrayinit.body");

    FE.genBlock(bodyBB);
    llvm::PHINode *currentElement =
        Builder.CreatePHI(element->getType(), 2, "arrayinit.cur");
    currentElement->addIncoming(element, entryBB);

    {
      FunctionEmitter::RunCleanupsScope CleanupsScope(FE);
      LValue elementLV = FE.makeAddrLValue(
          Address(currentElement, llvmElementType, elementAlign), elementType);
      if (ArrayFiller)
        genInitializationToLValue(ArrayFiller, elementLV);
      else
        genNullInitializationToLValue(elementLV);
    }

    llvm::Value *nextElement = Builder.CreateInBoundsGEP(
        llvmElementType, currentElement, one, "arrayinit.next");

    if (endOfInit.isValid())
      Builder.CreateStore(nextElement, endOfInit);

    llvm::Value *done =
        Builder.CreateICmpEQ(nextElement, end, "arrayinit.done");
    llvm::BasicBlock *endBB = FE.createBasicBlock("arrayinit.end");
    Builder.CreateCondBr(done, endBB, bodyBB);
    currentElement->addIncoming(nextElement, Builder.GetInsertBlock());

    FE.genBlock(endBB);
  }

  if (dtorKind)
    FE.deactivateCleanupBlock(cleanup, cleanupDominator);
}

// ===----------------------------------------------------------------------===
// Expression visitors
// ===----------------------------------------------------------------------===

void AggExprEmitter::VisitOpaqueValueExpr(OpaqueValueExpr *e) {
  // If this is a unique OVE, just visit its source expression.
  if (e->isUnique())
    Visit(e->getSourceExpr());
  else
    genFinalDestCopy(e->getType(), FE.getOrCreateOpaqueLValueMapping(e));
}

void AggExprEmitter::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
  if (Dest.isPotentiallyAliased() && E->getType().isPODType(FE.getContext())) {
    // For a POD type, just emit a load of the lvalue + a copy, because our
    // compound literal might alias the destination.
    genAggLoadOfLValue(E);
    return;
  }

  AggValueSlot Slot = EnsureSlot(E->getType());

  bool Destruct = !Slot.isExternallyDestructed();
  if (Destruct)
    Slot.setExternallyDestructed();

  FE.genAggExpr(E->getInitializer(), Slot);
}

namespace {
Expr *findPeephole(Expr *op, CastKind kind, const TreeContext &ctx) {
  op = op->IgnoreParenNoopCasts(ctx);
  if (auto castE = dyn_cast<CastExpr>(op)) {
    if (castE->getCastKind() == kind)
      return castE->getSubExpr();
  }
  return nullptr;
}
} // namespace

void AggExprEmitter::VisitCastExpr(CastExpr *E) {
  if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
    FE.ME.genExplicitCastExprType(ECE, &FE);
  switch (E->getCastKind()) {
  case CK_ToUnion: {
    // Evaluate even if the destination is ignored.
    if (Dest.isIgnored()) {
      FE.genAnyExpr(E->getSubExpr(), AggValueSlot::ignored(),
                    /*ignoreResult=*/true);
      break;
    }

    // GCC union extension
    QualType Ty = E->getSubExpr()->getType();
    Address CastPtr = Dest.getAddress().withElementType(FE.convertType(Ty));
    genInitializationToLValue(E->getSubExpr(), FE.makeAddrLValue(CastPtr, Ty));
    break;
  }

  case CK_LValueToRValueBitCast: {
    if (Dest.isIgnored()) {
      FE.genAnyExpr(E->getSubExpr(), AggValueSlot::ignored(),
                    /*ignoreResult=*/true);
      break;
    }

    LValue SourceLV = FE.genLValue(E->getSubExpr());
    Address SourceAddress = SourceLV.getAddress(FE).withElementType(FE.Int8Ty);
    Address DestAddress = Dest.getAddress().withElementType(FE.Int8Ty);
    llvm::Value *SizeVal = llvm::ConstantInt::get(
        FE.SizeTy,
        FE.getContext().getTypeSizeInChars(E->getType()).getQuantity());
    Builder.CreateMemCpy(DestAddress, SourceAddress, SizeVal);
    break;
  }

  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic: {
    bool isToAtomic = (E->getCastKind() == CK_NonAtomicToAtomic);

    QualType atomicType = E->getSubExpr()->getType();
    QualType valueType = E->getType();
    if (isToAtomic)
      std::swap(atomicType, valueType);

    assert(atomicType->isAtomicType());
    assert(FE.getContext().hasSameUnqualifiedType(
        valueType, atomicType->castAs<AtomicType>()->getValueType()));

    // Just recurse normally if we're ignoring the result or the
    // atomic type doesn't change representation.
    if (Dest.isIgnored() || !FE.ME.isPaddedAtomicType(atomicType)) {
      return Visit(E->getSubExpr());
    }

    CastKind peepholeTarget =
        (isToAtomic ? CK_AtomicToNonAtomic : CK_NonAtomicToAtomic);

    // These two cases are reverses of each other; try to peephole them.
    if (Expr *op =
            findPeephole(E->getSubExpr(), peepholeTarget, FE.getContext())) {
      assert(
          FE.getContext().hasSameUnqualifiedType(op->getType(), E->getType()) &&
          "peephole significantly changed types?");
      return Visit(op);
    }

    // If we're converting an r-value of non-atomic type to an r-value
    // of atomic type, just emit directly into the relevant sub-object.
    if (isToAtomic) {
      AggValueSlot valueDest = Dest;
      if (!valueDest.isIgnored() && FE.ME.isPaddedAtomicType(atomicType)) {
        // Zero-initialize.  (Strictly speaking, we only need to initialize
        // the padding at the end, but this is simpler.)
        if (!Dest.isZeroed())
          FE.genNullInitialization(Dest.getAddress(), atomicType);

        Address valueAddr =
            FE.Builder.CreateStructGEP(valueDest.getAddress(), 0);
        valueDest = AggValueSlot::forAddr(valueAddr, valueDest.getQualifiers(),
                                          valueDest.isExternallyDestructed(),
                                          valueDest.isPotentiallyAliased(),
                                          AggValueSlot::DoesNotOverlap,
                                          AggValueSlot::IsZeroed);
      }

      FE.genAggExpr(E->getSubExpr(), valueDest);
      return;
    }

    // Otherwise, we're converting an atomic type to a non-atomic type.
    // Make an atomic temporary, emit into that, and then copy the value out.
    AggValueSlot atomicSlot =
        FE.createAggTemp(atomicType, "atomic-to-nonatomic.temp");
    FE.genAggExpr(E->getSubExpr(), atomicSlot);

    Address valueAddr = Builder.CreateStructGEP(atomicSlot.getAddress(), 0);
    RValue rvalue = RValue::getAggregate(valueAddr, atomicSlot.isVolatile());
    return genFinalDestCopy(valueType, rvalue);
  }
  case CK_AddressSpaceConversion:
    return Visit(E->getSubExpr());

  case CK_LValueToRValue:
    // If we're loading from a volatile type, force the destination
    // into existence.
    if (E->getSubExpr()->getType().isVolatileQualified()) {
      EnsureDest(E->getType());
      Visit(E->getSubExpr());
      return;
    }

    [[fallthrough]];

  case CK_NoOp:
    assert(FE.getContext().hasSameUnqualifiedType(E->getSubExpr()->getType(),
                                                  E->getType()) &&
           "Implicit cast types must be compatible");
    Visit(E->getSubExpr());
    break;

  case CK_LValueBitCast:
    llvm_unreachable("should not be emitting lvalue bitcast as rvalue");

  case CK_Dependent:
  case CK_BitCast:
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
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_BuiltinFnToFnPtr:
  case CK_MatrixCast:

  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
    llvm_unreachable("cast kind invalid for aggregate types");
  }
}

void AggExprEmitter::VisitCallExpr(const CallExpr *E) {
  withReturnValueSlot(
      E, [&](ReturnValueSlot Slot) { return FE.genCallExpr(E, Slot); });
}

void AggExprEmitter::VisitBinComma(const BinaryOperator *E) {
  FE.genIgnoredExpr(E->getLHS());
  Visit(E->getRHS());
}

void AggExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  FunctionEmitter::StmtExprEvaluation eval(FE);
  FE.genCompoundStmt(*E->getSubStmt(), true, Dest);
}

void AggExprEmitter::VisitBinaryOperator(const BinaryOperator *E) {
  FE.errorUnsupported(E, "aggregate binary expression");
}

void AggExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  assert(FE.getContext().hasSameUnqualifiedType(E->getLHS()->getType(),
                                                E->getRHS()->getType()) &&
         "Invalid assignment");

  LValue LHS = FE.genLValue(E->getLHS());

  if (LHS.getType()->isAtomicType() ||
      FE.lValueIsSuitableForInlineAtomic(LHS)) {
    EnsureDest(E->getRHS()->getType());
    Visit(E->getRHS());
    FE.genAtomicStore(Dest.asRValue(), LHS, /*isInit*/ false);
    return;
  }

  AggValueSlot LHSSlot = AggValueSlot::forLValue(
      LHS, FE, AggValueSlot::IsDestructed, AggValueSlot::IsAliased,
      AggValueSlot::MayOverlap);
  // A non-volatile aggregate destination might have volatile member.
  if (!LHSSlot.isVolatile() && FE.hasVolatileMember(E->getLHS()->getType()))
    LHSSlot.setVolatile(true);

  FE.genAggExpr(E->getRHS(), LHSSlot);

  genFinalDestCopy(E->getType(), LHS);
}

void AggExprEmitter::VisitAbstractConditionalOperator(
    const AbstractConditionalOperator *E) {
  llvm::BasicBlock *LHSBlock = FE.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = FE.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = FE.createBasicBlock("cond.end");

  FunctionEmitter::OpaqueValueMapping binding(FE, E);

  FunctionEmitter::ConditionalEvaluation eval(FE);
  FE.genBranchOnBoolExpr(E->getCond(), LHSBlock, RHSBlock);

  eval.begin(FE);
  FE.genBlock(LHSBlock);
  Visit(E->getTrueExpr());
  eval.end(FE);

  assert(FE.haveInsertPoint() && "expression evaluation ended with no IP!");
  FE.Builder.CreateBr(ContBlock);

  // If the result of an agg expression is unused, then the emission
  // of the LHS might need to create a destination slot.  That's fine
  // with us, and we can safely emit the RHS into the same slot, but
  // we shouldn't claim that it's already being destructed.
  Dest.setExternallyDestructed(Dest.isExternallyDestructed());

  eval.begin(FE);
  FE.genBlock(RHSBlock);
  Visit(E->getFalseExpr());
  eval.end(FE);

  FE.genBlock(ContBlock);
}

void AggExprEmitter::VisitChooseExpr(const ChooseExpr *CE) {
  Visit(CE->getChosenSubExpr());
}

void AggExprEmitter::VisitVAArgExpr(VAArgExpr *VE) {
  Address ArgValue = Address::invalid();
  Address ArgPtr = FE.genVAArg(VE, ArgValue);

  if (!ArgPtr.isValid()) {
    FE.errorUnsupported(VE, "aggregate va_arg expression");
    return;
  }

  genFinalDestCopy(VE->getType(), FE.makeAddrLValue(ArgPtr, VE->getType()));
}

void AggExprEmitter::VisitExprWithCleanups(ExprWithCleanups *E) {
  FunctionEmitter::RunCleanupsScope cleanups(FE);
  Visit(E->getSubExpr());
}

void AggExprEmitter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  QualType T = E->getType();
  AggValueSlot Slot = EnsureSlot(T);
  genNullInitializationToLValue(FE.makeAddrLValue(Slot.getAddress(), T));
}

namespace {
bool castPreservesZero(const CastExpr *CE) {
  switch (CE->getCastKind()) {
    // No-ops.
  case CK_NoOp:
  case CK_BitCast:
  case CK_ToUnion:
  case CK_ToVoid:
    // Conversions between (possibly-complex) integral, (possibly-complex)
    // floating-point, and bool.
  case CK_BooleanToSignedIntegral:
  case CK_FloatingCast:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexToIntegralComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingRealToComplex:
  case CK_FloatingToBoolean:
  case CK_FloatingToIntegral:
  case CK_IntegralCast:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexToFloatingComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralRealToComplex:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
    // Reinterpreting integers as pointers and vice versa.
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
    // Language extensions.
  case CK_VectorSplat:
  case CK_MatrixCast:
  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic:
    return true;

  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToFloating:
  case CK_FixedPointToIntegral:
  case CK_FloatingToFixedPoint:
  case CK_IntegralToFixedPoint:
    return false;

  case CK_AddressSpaceConversion:
  case CK_NullToPointer:
  case CK_PointerToBoolean:
    return false;

  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_BuiltinFnToFnPtr:
  case CK_Dependent:
  case CK_LValueBitCast:
  case CK_LValueToRValue:
  case CK_LValueToRValueBitCast:
    return false;
  }
  llvm_unreachable("Unhandled neverc::CastKind enum");
}

bool isSimpleZero(const Expr *E, FunctionEmitter &FE) {
  E = E->IgnoreParens();
  while (auto *CE = dyn_cast<CastExpr>(E)) {
    if (!castPreservesZero(CE))
      break;
    E = CE->getSubExpr()->IgnoreParens();
  }

  // 0
  if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E))
    return IL->getValue() == 0;
  // +0.0
  if (const FloatingLiteral *FL = dyn_cast<FloatingLiteral>(E))
    return FL->getValue().isPosZero();
  // int()
  if (isa<ImplicitValueInitExpr>(E) &&
      FE.getTypes().isZeroInitializable(E->getType()))
    return true;
  // (int*)0 - Null pointer expressions.
  if (const CastExpr *ICE = dyn_cast<CastExpr>(E))
    return ICE->getCastKind() == CK_NullToPointer &&
           FE.getTypes().isPointerZeroInitializable(E->getType()) &&
           !E->HasSideEffects(FE.getContext());
  // '\0'
  if (const CharacterLiteral *CL = dyn_cast<CharacterLiteral>(E))
    return CL->getValue() == 0;

  // Otherwise, hard case: conservatively return false.
  return false;
}

void AggExprEmitter::genInitializationToLValue(Expr *E, LValue LV) {
  QualType type = LV.getType();
  if (Dest.isZeroed() && isSimpleZero(E, FE)) {
    // Storing "i32 0" to a zero'd memory location is a noop.
    return;
  } else if (isa<ImplicitValueInitExpr>(E)) {
    return genNullInitializationToLValue(LV);
  } else if (isa<NoInitExpr>(E)) {
    // Do nothing.
    return;
  }

  switch (FE.getEvaluationKind(type)) {
  case TEK_Complex:
    FE.genComplexExprIntoLValue(E, LV, /*isInit*/ true);
    return;
  case TEK_Aggregate:
    FE.genAggExpr(E, AggValueSlot::forLValue(LV, FE, AggValueSlot::IsDestructed,
                                             AggValueSlot::IsNotAliased,
                                             AggValueSlot::MayOverlap,
                                             Dest.isZeroed()));
    return;
  case TEK_Scalar:
    if (LV.isSimple()) {
      FE.genScalarInit(E, /*D=*/nullptr, LV, /*Captured=*/false);
    } else {
      FE.genStoreThroughLValue(RValue::get(FE.genScalarExpr(E)), LV);
    }
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

void AggExprEmitter::genNullInitializationToLValue(LValue lv) {
  QualType type = lv.getType();

  // If the destination slot is already zeroed out before the aggregate is
  // copied into it, we don't have to emit any zeros here.
  if (Dest.isZeroed() && FE.getTypes().isZeroInitializable(type))
    return;

  if (FE.hasScalarEvaluationKind(type)) {
    llvm::Value *null = FE.ME.genNullConstant(type);
    // Note that the following is not equivalent to
    // genStoreThroughBitfieldLValue for non-trivial types.
    if (lv.isBitField()) {
      FE.genStoreThroughBitfieldLValue(RValue::get(null), lv);
    } else {
      assert(lv.isSimple());
      FE.genStoreOfScalar(null, lv, /* isInitialization */ true);
    }
  } else {
    // There's a potential optimization opportunity in combining
    // memsets; that would be easy for arrays, but relatively
    // difficult for structures with the current code.
    FE.genNullInitialization(lv.getAddress(FE), lv.getType());
  }
}

void AggExprEmitter::VisitInitListExpr(InitListExpr *E) {
  if (E->hadArrayRangeDesignator())
    FE.errorUnsupported(E, "GNU array range designator extension");

  if (E->isTransparent())
    return Visit(E->getInit(0));

  VisitParenListOrInitListExpr(E, E->inits(), E->getInitializedFieldInUnion(),
                               E->getArrayFiller());
}

void AggExprEmitter::VisitParenListOrInitListExpr(
    Expr *ExprToVisit, llvm::ArrayRef<Expr *> InitExprs,
    FieldDecl *InitializedFieldInUnion, Expr *ArrayFiller) {

  AggValueSlot Dest = EnsureSlot(ExprToVisit->getType());

  LValue DestLV = FE.makeAddrLValue(Dest.getAddress(), ExprToVisit->getType());

  if (ExprToVisit->getType()->isConstantArrayType()) {
    auto AType = cast<llvm::ArrayType>(Dest.getAddress().getElementType());
    genArrayInit(Dest.getAddress(), AType, ExprToVisit->getType(), ExprToVisit,
                 InitExprs, ArrayFiller);
    return;
  } else if (ExprToVisit->getType()->isVariableArrayType()) {
    // A variable array type that has an initializer can only do empty
    // initialization; memset the array memory to zero.
    assert(InitExprs.size() == 0 &&
           "you can only use an empty initializer with VLAs");
    FE.genNullInitialization(Dest.getAddress(), ExprToVisit->getType());
    return;
  }

  assert(ExprToVisit->getType()->isRecordType() &&
         "Only support structs/unions here!");

  unsigned NumInitElements = InitExprs.size();
  RecordDecl *record = ExprToVisit->getType()->castAs<RecordType>()->getDecl();

  unsigned curInitIndex = 0;

  if (record->isUnion()) {
    if (!InitializedFieldInUnion) {
#ifndef NDEBUG
      // Make sure that it's really an empty and not a failure of
      // semantic analysis.
      for (const auto *Field : record->fields())
        assert(
            (Field->isUnnamedBitfield() || Field->isAnonymousStructOrUnion()) &&
            "Only unnamed bitfields or anonymous struct/union allowed");
#endif
      return;
    }

    FieldDecl *Field = InitializedFieldInUnion;

    LValue FieldLoc = FE.genLValueForFieldInitialization(DestLV, Field);
    if (NumInitElements) {
      genInitializationToLValue(InitExprs[0], FieldLoc);
    } else {
      genNullInitializationToLValue(FieldLoc);
    }

    return;
  }

  for (const auto *field : record->fields()) {
    if (field->getType()->isIncompleteArrayType())
      break;

    if (field->isUnnamedBitfield())
      continue;

    if (curInitIndex == NumInitElements && Dest.isZeroed() &&
        FE.getTypes().isZeroInitializable(ExprToVisit->getType()))
      break;

    LValue LV = FE.genLValueForFieldInitialization(DestLV, field);

    if (curInitIndex < NumInitElements) {
      genInitializationToLValue(InitExprs[curInitIndex++], LV);
    } else {
      genNullInitializationToLValue(LV);
    }

    // Clean up unused GEP instructions that may remain after initialization.
    if (LV.isSimple())
      if (llvm::GetElementPtrInst *GEP =
              dyn_cast<llvm::GetElementPtrInst>(LV.getPointer(FE)))
        if (GEP->use_empty())
          GEP->eraseFromParent();
  }
}

void AggExprEmitter::VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E,
                                            llvm::Value *outerBegin) {
  FunctionEmitter::OpaqueValueMapping binding(FE, E->getCommonExpr());

  Address destPtr = EnsureSlot(E->getType()).getAddress();
  uint64_t numElements = E->getArraySize().getZExtValue();

  if (!numElements)
    return;

  // destPtr is an array*. Construct an elementType* by drilling down a level.
  llvm::Value *zero = llvm::ConstantInt::get(FE.SizeTy, 0);
  llvm::Value *indices[] = {zero, zero};
  llvm::Value *begin =
      Builder.CreateInBoundsGEP(destPtr.getElementType(), destPtr.getPointer(),
                                indices, "arrayinit.begin");

  // Prepare to special-case multidimensional array initialization: we avoid
  // emitting multiple cleanup loops in that case.
  if (!outerBegin)
    outerBegin = begin;
  ArrayInitLoopExpr *InnerLoop = dyn_cast<ArrayInitLoopExpr>(E->getSubExpr());

  QualType elementType =
      FE.getContext().getAsArrayType(E->getType())->getElementType();
  CharUnits elementSize = FE.getContext().getTypeSizeInChars(elementType);
  CharUnits elementAlign =
      destPtr.getAlignment().alignmentOfArrayElement(elementSize);
  llvm::Type *llvmElementType = FE.convertTypeForMem(elementType);

  llvm::BasicBlock *entryBB = Builder.GetInsertBlock();
  llvm::BasicBlock *bodyBB = FE.createBasicBlock("arrayinit.body");

  FE.genBlock(bodyBB);
  llvm::PHINode *index =
      Builder.CreatePHI(zero->getType(), 2, "arrayinit.index");
  index->addIncoming(zero, entryBB);
  llvm::Value *element =
      Builder.CreateInBoundsGEP(llvmElementType, begin, index);

  QualType::DestructionKind dtorKind = QualType::DK_none;
  EHScopeStack::stable_iterator cleanup;

  {
    // Temporaries created in an array initialization loop are destroyed
    // at the end of each iteration.
    FunctionEmitter::RunCleanupsScope CleanupsScope(FE);
    FunctionEmitter::ArrayInitLoopExprScope Scope(FE, index);
    LValue elementLV = FE.makeAddrLValue(
        Address(element, llvmElementType, elementAlign), elementType);

    if (InnerLoop) {
      // If the subexpression is an ArrayInitLoopExpr, share its cleanup.
      auto elementSlot = AggValueSlot::forLValue(
          elementLV, FE, AggValueSlot::IsDestructed, AggValueSlot::IsNotAliased,
          AggValueSlot::DoesNotOverlap);
      AggExprEmitter(FE, elementSlot, false)
          .VisitArrayInitLoopExpr(InnerLoop, outerBegin);
    } else
      genInitializationToLValue(E->getSubExpr(), elementLV);
  }

  llvm::Value *nextIndex = Builder.CreateNUWAdd(
      index, llvm::ConstantInt::get(FE.SizeTy, 1), "arrayinit.next");
  index->addIncoming(nextIndex, Builder.GetInsertBlock());

  llvm::Value *done = Builder.CreateICmpEQ(
      nextIndex, llvm::ConstantInt::get(FE.SizeTy, numElements),
      "arrayinit.done");
  llvm::BasicBlock *endBB = FE.createBasicBlock("arrayinit.end");
  Builder.CreateCondBr(done, endBB, bodyBB);

  FE.genBlock(endBB);

  if (dtorKind)
    FE.deactivateCleanupBlock(cleanup, index);
}

void AggExprEmitter::VisitDesignatedInitUpdateExpr(
    DesignatedInitUpdateExpr *E) {
  AggValueSlot Dest = EnsureSlot(E->getType());

  LValue DestLV = FE.makeAddrLValue(Dest.getAddress(), E->getType());
  genInitializationToLValue(E->getBase(), DestLV);
  VisitInitListExpr(E->getUpdater());
}

// ===----------------------------------------------------------------------===
// Initialization analysis & memset optimization
// ===----------------------------------------------------------------------===

CharUnits getNumNonZeroBytesInInit(const Expr *E, FunctionEmitter &FE) {
  E = E->IgnoreParenNoopCasts(FE.getContext());

  // 0 and 0.0 won't require any non-zero stores!
  if (isSimpleZero(E, FE))
    return CharUnits::Zero();

  // If this is an initlist expr, sum up the size of sizes of the (present)
  // elements.  If this is something weird, assume the whole thing is non-zero.
  const InitListExpr *ILE = dyn_cast<InitListExpr>(E);
  while (ILE && ILE->isTransparent())
    ILE = dyn_cast<InitListExpr>(ILE->getInit(0));
  if (!ILE || !FE.getTypes().isZeroInitializable(ILE->getType()))
    return FE.getContext().getTypeSizeInChars(E->getType());

  // InitListExprs for structs have to be handled carefully.  If there are
  // reference members, we need to consider the size of the reference, not the
  // referencee.  InitListExprs for unions and arrays can't have references.
  if (const RecordType *RT = E->getType()->getAs<RecordType>()) {
    if (!RT->isUnionType()) {
      RecordDecl *SD = RT->getDecl();
      CharUnits NumNonZeroBytes = CharUnits::Zero();

      unsigned ILEElement = 0;
      for (const auto *Field : SD->fields()) {
        if (Field->getType()->isIncompleteArrayType() ||
            ILEElement == ILE->getNumInits())
          break;
        if (Field->isUnnamedBitfield())
          continue;

        const Expr *E = ILE->getInit(ILEElement++);

        NumNonZeroBytes += getNumNonZeroBytesInInit(E, FE);
      }

      return NumNonZeroBytes;
    }
  }

  CharUnits NumNonZeroBytes = CharUnits::Zero();
  for (unsigned i = 0, e = ILE->getNumInits(); i != e; ++i)
    NumNonZeroBytes += getNumNonZeroBytesInInit(ILE->getInit(i), FE);
  return NumNonZeroBytes;
}

void checkAggExprForMemSetUse(AggValueSlot &Slot, const Expr *E,
                              FunctionEmitter &FE) {
  // If the slot is already known to be zeroed, nothing to do.  Don't mess with
  // volatile stores.
  if (Slot.isZeroed() || Slot.isVolatile() || !Slot.getAddress().isValid())
    return;

  // If the type is 16-bytes or smaller, prefer individual stores over memset.
  CharUnits Size = Slot.getPreferredSize(FE.getContext(), E->getType());
  if (Size <= CharUnits::fromQuantity(16))
    return;

  // If >3/4 of the initializer is zero, memset + individual stores is cheaper.
  CharUnits NumNonZeroBytes = getNumNonZeroBytesInInit(E, FE);
  if (NumNonZeroBytes * 4 > Size)
    return;

  llvm::Constant *SizeVal = FE.Builder.getInt64(Size.getQuantity());

  Address Loc = Slot.getAddress().withElementType(FE.Int8Ty);
  FE.Builder.CreateMemSet(Loc, FE.Builder.getInt8(0), SizeVal, false);

  Slot.setZeroed();
}
} // namespace

// ===----------------------------------------------------------------------===
// FunctionEmitter entry points
// ===----------------------------------------------------------------------===

void FunctionEmitter::genAggExpr(const Expr *E, AggValueSlot Slot) {
  assert(E && hasAggregateEvaluationKind(E->getType()) &&
         "Invalid aggregate expression to emit");
  assert((Slot.getAddress().isValid() || Slot.isIgnored()) &&
         "slot has bits but no address");

  checkAggExprForMemSetUse(Slot, E, *this);

  AggExprEmitter(*this, Slot, Slot.isIgnored()).Visit(const_cast<Expr *>(E));
}

LValue FunctionEmitter::genAggExprToLValue(const Expr *E) {
  assert(hasAggregateEvaluationKind(E->getType()) && "Invalid argument!");
  Address Temp = createMemTemp(E->getType());
  LValue LV = makeAddrLValue(Temp, E->getType());
  genAggExpr(E, AggValueSlot::forLValue(
                    LV, *this, AggValueSlot::IsNotDestructed,
                    AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap));
  return LV;
}

AggValueSlot::Overlap_t
FunctionEmitter::getOverlapForFieldInit(const FieldDecl *FD) {
  if (!FD->getType()->isRecordType())
    return AggValueSlot::DoesNotOverlap;

  const RecordDecl *RD = FD->getParent();
  const StructRecordLayout &Layout = getContext().getStructRecordLayout(RD);
  if (Layout.getFieldOffset(FD->getFieldIndex()) +
          getContext().getTypeSize(FD->getType()) <=
      (uint64_t)getContext().toBits(Layout.getSize()))
    return AggValueSlot::DoesNotOverlap;

  // The tail padding may contain values we need to preserve.
  return AggValueSlot::MayOverlap;
}

void FunctionEmitter::genAggregateCopy(LValue Dest, LValue Src, QualType Ty,
                                       AggValueSlot::Overlap_t MayOverlap,
                                       bool isVolatile) {
  assert(!Ty->isAnyComplexType() && "Shouldn't happen for complex");

  Address DestPtr = Dest.getAddress(*this);
  Address SrcPtr = Src.getAddress(*this);

  // Aggregate assignment uses llvm.memcpy. Overlapping source/dest is
  // only valid when they are exactly the same object with compatible type.
  //
  // memcpy is not defined if the source and destination pointers are exactly
  // equal, but other compilers do this optimization, and almost every memcpy
  // implementation handles this case safely.  If there is a libc that does not
  // safely handle this, we can add a target hook.

  // Skip tail padding for potentially-overlapping subobjects — it may be
  // occupied by a different object.
  TypeInfoChars TypeInfo;
  if (MayOverlap)
    TypeInfo = getContext().getTypeInfoDataSizeInChars(Ty);
  else
    TypeInfo = getContext().getTypeInfoInChars(Ty);

  llvm::Value *SizeVal = nullptr;
  if (TypeInfo.Width.isZero()) {
    // But note that getTypeInfo returns 0 for a VLA.
    if (auto *VAT = dyn_cast_or_null<VariableArrayType>(
            getContext().getAsArrayType(Ty))) {
      QualType BaseEltTy;
      SizeVal = emitArrayLength(VAT, BaseEltTy, DestPtr);
      TypeInfo = getContext().getTypeInfoInChars(BaseEltTy);
      assert(!TypeInfo.Width.isZero());
      SizeVal = Builder.CreateNUWMul(
          SizeVal,
          llvm::ConstantInt::get(SizeTy, TypeInfo.Width.getQuantity()));
    }
  }
  if (!SizeVal) {
    SizeVal = llvm::ConstantInt::get(SizeTy, TypeInfo.Width.getQuantity());
  }

  DestPtr = DestPtr.withElementType(Int8Ty);
  SrcPtr = SrcPtr.withElementType(Int8Ty);

  auto Inst = Builder.CreateMemCpy(DestPtr, SrcPtr, SizeVal, isVolatile);

  // Determine the metadata to describe the position of any padding in this
  // memcpy, as well as the TBAA tags for the members of the struct, in case
  // the optimizer wishes to expand it in to scalar memory operations.
  if (llvm::MDNode *TBAAStructTag = ME.getTBAAStructInfo(Ty))
    Inst->setMetadata(llvm::LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ME.getCodeGenOpts().NewStructPathTBAA) {
    TBAAAccessInfo TBAAInfo = ME.mergeTBAAInfoForMemoryTransfer(
        Dest.getTBAAInfo(), Src.getTBAAInfo());
    ME.decorateInstructionWithTBAA(Inst, TBAAInfo);
  }
}
