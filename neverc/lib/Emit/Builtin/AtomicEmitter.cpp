#include "ABI/TargetInfo.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "Stmt/CallEmitterInfo.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// AtomicInfo
// ===----------------------------------------------------------------------===

namespace {
class AtomicInfo {
  FunctionEmitter &FE;
  QualType AtomicTy;
  QualType ValueTy;
  uint64_t AtomicSizeInBits;
  uint64_t ValueSizeInBits;
  CharUnits AtomicAlign;
  CharUnits ValueAlign;
  TypeEvaluationKind EvaluationKind;
  bool UseLibcall;
  LValue LVal;
  BitFieldInfo BFI;

public:
  AtomicInfo(FunctionEmitter &FE, LValue &lvalue)
      : FE(FE), AtomicSizeInBits(0), ValueSizeInBits(0),
        EvaluationKind(TEK_Scalar), UseLibcall(true) {
    assert(!lvalue.isGlobalReg());
    TreeContext &C = FE.getContext();
    if (lvalue.isSimple()) {
      AtomicTy = lvalue.getType();
      if (auto *ATy = AtomicTy->getAs<AtomicType>())
        ValueTy = ATy->getValueType();
      else
        ValueTy = AtomicTy;
      EvaluationKind = FE.getEvaluationKind(ValueTy);

      uint64_t ValueAlignInBits;
      uint64_t AtomicAlignInBits;
      TypeInfo ValueTI = C.getTypeInfo(ValueTy);
      ValueSizeInBits = ValueTI.Width;
      ValueAlignInBits = ValueTI.Align;

      TypeInfo AtomicTI = C.getTypeInfo(AtomicTy);
      AtomicSizeInBits = AtomicTI.Width;
      AtomicAlignInBits = AtomicTI.Align;

      assert(ValueSizeInBits <= AtomicSizeInBits);
      assert(ValueAlignInBits <= AtomicAlignInBits);

      AtomicAlign = C.toCharUnitsFromBits(AtomicAlignInBits);
      ValueAlign = C.toCharUnitsFromBits(ValueAlignInBits);
      if (lvalue.getAlignment().isZero())
        lvalue.setAlignment(AtomicAlign);

      LVal = lvalue;
    } else if (lvalue.isBitField()) {
      ValueTy = lvalue.getType();
      ValueSizeInBits = C.getTypeSize(ValueTy);
      auto &OrigBFI = lvalue.getBitFieldInfo();
      auto Offset = OrigBFI.Offset % C.toBits(lvalue.getAlignment());
      AtomicSizeInBits = C.toBits(
          C.toCharUnitsFromBits(Offset + OrigBFI.Size + C.getCharWidth() - 1)
              .alignTo(lvalue.getAlignment()));
      llvm::Value *BitFieldPtr = lvalue.getBitFieldPointer();
      auto OffsetInChars =
          (C.toCharUnitsFromBits(OrigBFI.Offset) / lvalue.getAlignment()) *
          lvalue.getAlignment();
      llvm::Value *StoragePtr = FE.Builder.CreateConstGEP1_64(
          FE.Int8Ty, BitFieldPtr, OffsetInChars.getQuantity());
      StoragePtr = FE.Builder.CreateAddrSpaceCast(StoragePtr, FE.UnqualPtrTy,
                                                  "atomic_bitfield_base");
      BFI = OrigBFI;
      BFI.Offset = Offset;
      BFI.StorageSize = AtomicSizeInBits;
      BFI.StorageOffset += OffsetInChars;
      llvm::Type *StorageTy = FE.Builder.getIntNTy(AtomicSizeInBits);
      LVal = LValue::MakeBitfield(
          Address(StoragePtr, StorageTy, lvalue.getAlignment()), BFI,
          lvalue.getType(), lvalue.getBaseInfo(), lvalue.getTBAAInfo());
      AtomicTy = C.getIntTypeForBitwidth(AtomicSizeInBits, OrigBFI.IsSigned);
      if (AtomicTy.isNull()) {
        llvm::APInt Size(
            /*numBits=*/32,
            C.toCharUnitsFromBits(AtomicSizeInBits).getQuantity());
        AtomicTy = C.getConstantArrayType(C.CharTy, Size, nullptr,
                                          ArraySizeModifier::Normal,
                                          /*IndexTypeQuals=*/0);
      }
      AtomicAlign = ValueAlign = lvalue.getAlignment();
    } else if (lvalue.isVectorElt()) {
      ValueTy = lvalue.getType()->castAs<VectorType>()->getElementType();
      ValueSizeInBits = C.getTypeSize(ValueTy);
      AtomicTy = lvalue.getType();
      AtomicSizeInBits = C.getTypeSize(AtomicTy);
      AtomicAlign = ValueAlign = lvalue.getAlignment();
      LVal = lvalue;
    } else {
      assert(lvalue.isExtVectorElt());
      ValueTy = lvalue.getType();
      ValueSizeInBits = C.getTypeSize(ValueTy);
      AtomicTy = ValueTy = FE.getContext().getExtVectorType(
          lvalue.getType(), cast<llvm::FixedVectorType>(
                                lvalue.getExtVectorAddress().getElementType())
                                ->getNumElements());
      AtomicSizeInBits = C.getTypeSize(AtomicTy);
      AtomicAlign = ValueAlign = lvalue.getAlignment();
      LVal = lvalue;
    }
    UseLibcall = !C.getTargetInfo().hasBuiltinAtomic(
        AtomicSizeInBits, C.toBits(lvalue.getAlignment()));
  }

  QualType getAtomicType() const { return AtomicTy; }
  QualType getValueType() const { return ValueTy; }
  CharUnits getAtomicAlignment() const { return AtomicAlign; }
  uint64_t getAtomicSizeInBits() const { return AtomicSizeInBits; }
  uint64_t getValueSizeInBits() const { return ValueSizeInBits; }
  TypeEvaluationKind getEvaluationKind() const { return EvaluationKind; }
  bool shouldUseLibcall() const { return UseLibcall; }
  const LValue &getAtomicLValue() const { return LVal; }
  llvm::Value *getAtomicPointer() const {
    if (LVal.isSimple())
      return LVal.getPointer(FE);
    else if (LVal.isBitField())
      return LVal.getBitFieldPointer();
    else if (LVal.isVectorElt())
      return LVal.getVectorPointer();
    assert(LVal.isExtVectorElt());
    return LVal.getExtVectorPointer();
  }
  Address getAtomicAddress() const {
    llvm::Type *ElTy;
    if (LVal.isSimple())
      ElTy = LVal.getAddress(FE).getElementType();
    else if (LVal.isBitField())
      ElTy = LVal.getBitFieldAddress().getElementType();
    else if (LVal.isVectorElt())
      ElTy = LVal.getVectorAddress().getElementType();
    else
      ElTy = LVal.getExtVectorAddress().getElementType();
    return Address(getAtomicPointer(), ElTy, getAtomicAlignment());
  }

  Address getAtomicAddressAsAtomicIntPointer() const {
    return castToAtomicIntPointer(getAtomicAddress());
  }

  bool hasPadding() const { return (ValueSizeInBits != AtomicSizeInBits); }

  bool emitMemSetZeroIfNecessary() const;

  llvm::Value *getAtomicSizeValue() const {
    CharUnits size = FE.getContext().toCharUnitsFromBits(AtomicSizeInBits);
    return FE.ME.getSize(size);
  }

  Address castToAtomicIntPointer(Address Addr) const;

  Address convertToAtomicIntPointer(Address Addr) const;

  RValue convertAtomicTempToRValue(Address addr, AggValueSlot resultSlot,
                                   SourceLocation loc, bool AsValue) const;

  llvm::Value *convertRValueToInt(RValue RVal) const;

  RValue ConvertIntToValueOrAtomic(llvm::Value *IntVal, AggValueSlot ResultSlot,
                                   SourceLocation Loc, bool AsValue) const;

  void emitCopyIntoMemory(RValue rvalue) const;

  LValue projectValue() const {
    assert(LVal.isSimple());
    Address addr = getAtomicAddress();
    if (hasPadding())
      addr = FE.Builder.CreateStructGEP(addr, 0);

    return LValue::MakeAddr(addr, getValueType(), FE.getContext(),
                            LVal.getBaseInfo(), LVal.getTBAAInfo());
  }

  RValue genAtomicLoad(AggValueSlot ResultSlot, SourceLocation Loc,
                       bool AsValue, llvm::AtomicOrdering AO, bool IsVolatile);

  std::pair<RValue, llvm::Value *>
  genAtomicCompareExchange(RValue Expected, RValue Desired,
                           llvm::AtomicOrdering Success =
                               llvm::AtomicOrdering::SequentiallyConsistent,
                           llvm::AtomicOrdering Failure =
                               llvm::AtomicOrdering::SequentiallyConsistent,
                           bool IsWeak = false);

  void genAtomicUpdate(llvm::AtomicOrdering AO,
                       const llvm::function_ref<RValue(RValue)> &UpdateOp,
                       bool IsVolatile);
  void genAtomicUpdate(llvm::AtomicOrdering AO, RValue UpdateRVal,
                       bool IsVolatile);

  Address materializeRValue(RValue rvalue) const;

  Address createTempAlloca() const;

private:
  bool requiresMemSetZero(llvm::Type *type) const;

  void genAtomicLoadLibcall(llvm::Value *AddForLoaded, llvm::AtomicOrdering AO,
                            bool IsVolatile);
  llvm::Value *genAtomicLoadOp(llvm::AtomicOrdering AO, bool IsVolatile);
  llvm::Value *genAtomicCompareExchangeLibcall(
      llvm::Value *ExpectedAddr, llvm::Value *DesiredAddr,
      llvm::AtomicOrdering Success =
          llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering Failure =
          llvm::AtomicOrdering::SequentiallyConsistent);
  std::pair<llvm::Value *, llvm::Value *>
  genAtomicCompareExchangeOp(llvm::Value *ExpectedVal, llvm::Value *DesiredVal,
                             llvm::AtomicOrdering Success =
                                 llvm::AtomicOrdering::SequentiallyConsistent,
                             llvm::AtomicOrdering Failure =
                                 llvm::AtomicOrdering::SequentiallyConsistent,
                             bool IsWeak = false);
  void
  genAtomicUpdateLibcall(llvm::AtomicOrdering AO,
                         const llvm::function_ref<RValue(RValue)> &UpdateOp,
                         bool IsVolatile);
  void genAtomicUpdateOp(llvm::AtomicOrdering AO,
                         const llvm::function_ref<RValue(RValue)> &UpdateOp,
                         bool IsVolatile);
  void genAtomicUpdateLibcall(llvm::AtomicOrdering AO, RValue UpdateRVal,
                              bool IsVolatile);
  void genAtomicUpdateOp(llvm::AtomicOrdering AO, RValue UpdateRal,
                         bool IsVolatile);
};
} // namespace

// ===----------------------------------------------------------------------===
// AtomicInfo helpers
// ===----------------------------------------------------------------------===

Address AtomicInfo::createTempAlloca() const {
  Address TempAlloca = FE.createMemTemp(
      (LVal.isBitField() && ValueSizeInBits > AtomicSizeInBits) ? ValueTy
                                                                : AtomicTy,
      getAtomicAlignment(), "atomic-temp");
  if (LVal.isBitField())
    return FE.Builder.CreatePointerBitCastOrAddrSpaceCast(
        TempAlloca, getAtomicAddress().getType(),
        getAtomicAddress().getElementType());
  return TempAlloca;
}

namespace {
RValue emitAtomicLibcall(FunctionEmitter &FE, llvm::StringRef fnName,
                         QualType resultType, CallArgList &args) {
  const ABIFunctionInfo &fnInfo =
      FE.ME.getTypes().arrangeBuiltinFunctionCall(resultType, args);
  llvm::FunctionType *fnTy = FE.ME.getTypes().GetFunctionType(fnInfo);
  llvm::AttrBuilder fnAttrB(FE.getLLVMContext());
  fnAttrB.addAttribute(llvm::Attribute::NoUnwind);
  fnAttrB.addAttribute(llvm::Attribute::WillReturn);
  llvm::AttributeList fnAttrs = llvm::AttributeList::get(
      FE.getLLVMContext(), llvm::AttributeList::FunctionIndex, fnAttrB);

  llvm::FunctionCallee fn = FE.ME.createRuntimeFunction(fnTy, fnName, fnAttrs);
  auto callee = FnCallee::forDirect(fn);
  return FE.genCall(fnInfo, callee, ReturnValueSlot(), args);
}

bool isFullSizeType(ModuleEmitter &ME, llvm::Type *type,
                    uint64_t expectedSize) {
  return (ME.getDataLayout().getTypeStoreSize(type) * 8 == expectedSize);
}

bool AtomicInfo::requiresMemSetZero(llvm::Type *type) const {
  // If the atomic type has size padding, we definitely need a memset.
  if (hasPadding())
    return true;

  // Otherwise, do some simple heuristics to try to avoid it:
  switch (getEvaluationKind()) {
  // For scalars and complexes, check whether the store size of the
  // type uses the full size.
  case TEK_Scalar:
    return !isFullSizeType(FE.ME, type, AtomicSizeInBits);
  case TEK_Complex:
    return !isFullSizeType(FE.ME, type->getStructElementType(0),
                           AtomicSizeInBits / 2);

  // Padding in structs has an undefined bit pattern.  User beware.
  case TEK_Aggregate:
    return false;
  }
  llvm_unreachable("bad evaluation kind");
}

bool AtomicInfo::emitMemSetZeroIfNecessary() const {
  assert(LVal.isSimple());
  Address addr = LVal.getAddress(FE);
  if (!requiresMemSetZero(addr.getElementType()))
    return false;

  FE.Builder.CreateMemSet(
      addr.getPointer(), llvm::ConstantInt::get(FE.Int8Ty, 0),
      FE.getContext().toCharUnitsFromBits(AtomicSizeInBits).getQuantity(),
      LVal.getAlignment().getAsAlign());
  return true;
}

void emitAtomicCmpXchg(FunctionEmitter &FE, AtomicExpr *E, bool IsWeak,
                       Address Dest, Address Ptr, Address Val1, Address Val2,
                       uint64_t Size, llvm::AtomicOrdering SuccessOrder,
                       llvm::AtomicOrdering FailureOrder,
                       llvm::SyncScope::ID Scope) {
  // Note that cmpxchg doesn't support weak cmpxchg, at least at the moment.
  llvm::Value *Expected = FE.Builder.CreateLoad(Val1);
  llvm::Value *Desired = FE.Builder.CreateLoad(Val2);

  llvm::AtomicCmpXchgInst *Pair = FE.Builder.CreateAtomicCmpXchg(
      Ptr, Expected, Desired, SuccessOrder, FailureOrder, Scope);
  Pair->setVolatile(E->isVolatile());
  Pair->setWeak(IsWeak);

  // Cmp holds the result of the compare-exchange operation: true on success,
  // false on failure.
  llvm::Value *Old = FE.Builder.CreateExtractValue(Pair, 0);
  llvm::Value *Cmp = FE.Builder.CreateExtractValue(Pair, 1);

  // This basic block is used to hold the store instruction if the operation
  // failed.
  llvm::BasicBlock *StoreExpectedBB =
      FE.createBasicBlock("cmpxchg.store_expected", FE.CurFn);

  // This basic block is the exit point of the operation, we should end up
  // here regardless of whether or not the operation succeeded.
  llvm::BasicBlock *ContinueBB =
      FE.createBasicBlock("cmpxchg.continue", FE.CurFn);

  // Update Expected if Expected isn't equal to Old, otherwise branch to the
  // exit point.
  FE.Builder.CreateCondBr(Cmp, ContinueBB, StoreExpectedBB);

  FE.Builder.SetInsertPoint(StoreExpectedBB);
  FE.Builder.CreateStore(Old, Val1);
  // Finally, branch to the exit point.
  FE.Builder.CreateBr(ContinueBB);

  FE.Builder.SetInsertPoint(ContinueBB);
  FE.genStoreOfScalar(Cmp, FE.makeAddrLValue(Dest, E->getType()));
}

void emitAtomicCmpXchgFailureSet(FunctionEmitter &FE, AtomicExpr *E,
                                 bool IsWeak, Address Dest, Address Ptr,
                                 Address Val1, Address Val2,
                                 llvm::Value *FailureOrderVal, uint64_t Size,
                                 llvm::AtomicOrdering SuccessOrder,
                                 llvm::SyncScope::ID Scope) {
  llvm::AtomicOrdering FailureOrder;
  if (llvm::ConstantInt *FO = dyn_cast<llvm::ConstantInt>(FailureOrderVal)) {
    auto FOS = FO->getSExtValue();
    if (!llvm::isValidAtomicOrderingCABI(FOS))
      FailureOrder = llvm::AtomicOrdering::Monotonic;
    else
      switch ((llvm::AtomicOrderingCABI)FOS) {
      case llvm::AtomicOrderingCABI::relaxed:
      // 31.7.2.18: "The failure argument shall not be memory_order_release
      // nor memory_order_acq_rel". Fallback to monotonic.
      case llvm::AtomicOrderingCABI::release:
      case llvm::AtomicOrderingCABI::acq_rel:
        FailureOrder = llvm::AtomicOrdering::Monotonic;
        break;
      case llvm::AtomicOrderingCABI::consume:
      case llvm::AtomicOrderingCABI::acquire:
        FailureOrder = llvm::AtomicOrdering::Acquire;
        break;
      case llvm::AtomicOrderingCABI::seq_cst:
        FailureOrder = llvm::AtomicOrdering::SequentiallyConsistent;
        break;
      }
    // "The failure argument shall be no stronger than the success argument"
    // precondition is 31.7.2.18.
    emitAtomicCmpXchg(FE, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                      FailureOrder, Scope);
    return;
  }

  auto *MonotonicBB = FE.createBasicBlock("monotonic_fail", FE.CurFn);
  auto *AcquireBB = FE.createBasicBlock("acquire_fail", FE.CurFn);
  auto *SeqCstBB = FE.createBasicBlock("seqcst_fail", FE.CurFn);
  auto *ContBB = FE.createBasicBlock("atomic.continue", FE.CurFn);

  // MonotonicBB is arbitrarily chosen as the default case; in practice, this
  // doesn't matter unless someone is crazy enough to use something that
  // doesn't fold to a constant for the ordering.
  llvm::SwitchInst *SI = FE.Builder.CreateSwitch(FailureOrderVal, MonotonicBB);
  // Implemented as acquire, since it's the closest in LLVM.
  SI->addCase(FE.Builder.getInt32((int)llvm::AtomicOrderingCABI::consume),
              AcquireBB);
  SI->addCase(FE.Builder.getInt32((int)llvm::AtomicOrderingCABI::acquire),
              AcquireBB);
  SI->addCase(FE.Builder.getInt32((int)llvm::AtomicOrderingCABI::seq_cst),
              SeqCstBB);

  FE.Builder.SetInsertPoint(MonotonicBB);
  emitAtomicCmpXchg(FE, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                    llvm::AtomicOrdering::Monotonic, Scope);
  FE.Builder.CreateBr(ContBB);

  FE.Builder.SetInsertPoint(AcquireBB);
  emitAtomicCmpXchg(FE, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                    llvm::AtomicOrdering::Acquire, Scope);
  FE.Builder.CreateBr(ContBB);

  FE.Builder.SetInsertPoint(SeqCstBB);
  emitAtomicCmpXchg(FE, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                    llvm::AtomicOrdering::SequentiallyConsistent, Scope);
  FE.Builder.CreateBr(ContBB);

  FE.Builder.SetInsertPoint(ContBB);
}

llvm::Value *genPostAtomicMinMax(CGBuilderTy &Builder, AtomicExpr::AtomicOp Op,
                                 bool IsSigned, llvm::Value *OldVal,
                                 llvm::Value *RHS) {
  llvm::CmpInst::Predicate Pred;
  switch (Op) {
  default:
    llvm_unreachable("Unexpected min/max operation");
  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
    Pred = IsSigned ? llvm::CmpInst::ICMP_SGT : llvm::CmpInst::ICMP_UGT;
    break;
  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
    Pred = IsSigned ? llvm::CmpInst::ICMP_SLT : llvm::CmpInst::ICMP_ULT;
    break;
  }
  llvm::Value *Cmp = Builder.CreateICmp(Pred, OldVal, RHS, "tst");
  return Builder.CreateSelect(Cmp, OldVal, RHS, "newval");
}

// ===----------------------------------------------------------------------===
// Atomic operation codegen
// ===----------------------------------------------------------------------===

void genAtomicOp(FunctionEmitter &FE, AtomicExpr *E, Address Dest, Address Ptr,
                 Address Val1, Address Val2, llvm::Value *IsWeak,
                 llvm::Value *FailureOrder, uint64_t Size,
                 llvm::AtomicOrdering Order, llvm::SyncScope::ID Scope) {
  llvm::AtomicRMWInst::BinOp Op = llvm::AtomicRMWInst::Add;
  bool PostOpMinMax = false;
  unsigned PostOp = 0;

  switch (E->getOp()) {
  case AtomicExpr::AO__c11_atomic_init:
    llvm_unreachable("Already handled!");

  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
    emitAtomicCmpXchgFailureSet(FE, E, false, Dest, Ptr, Val1, Val2,
                                FailureOrder, Size, Order, Scope);
    return;
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    emitAtomicCmpXchgFailureSet(FE, E, true, Dest, Ptr, Val1, Val2,
                                FailureOrder, Size, Order, Scope);
    return;
  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
  case AtomicExpr::AO__scoped_atomic_compare_exchange:
  case AtomicExpr::AO__scoped_atomic_compare_exchange_n: {
    if (llvm::ConstantInt *IsWeakC = dyn_cast<llvm::ConstantInt>(IsWeak)) {
      emitAtomicCmpXchgFailureSet(FE, E, IsWeakC->getZExtValue(), Dest, Ptr,
                                  Val1, Val2, FailureOrder, Size, Order, Scope);
    } else {
      llvm::BasicBlock *StrongBB =
          FE.createBasicBlock("cmpxchg.strong", FE.CurFn);
      llvm::BasicBlock *WeakBB = FE.createBasicBlock("cmxchg.weak", FE.CurFn);
      llvm::BasicBlock *ContBB =
          FE.createBasicBlock("cmpxchg.continue", FE.CurFn);

      llvm::SwitchInst *SI = FE.Builder.CreateSwitch(IsWeak, WeakBB);
      SI->addCase(FE.Builder.getInt1(false), StrongBB);

      FE.Builder.SetInsertPoint(StrongBB);
      emitAtomicCmpXchgFailureSet(FE, E, false, Dest, Ptr, Val1, Val2,
                                  FailureOrder, Size, Order, Scope);
      FE.Builder.CreateBr(ContBB);

      FE.Builder.SetInsertPoint(WeakBB);
      emitAtomicCmpXchgFailureSet(FE, E, true, Dest, Ptr, Val1, Val2,
                                  FailureOrder, Size, Order, Scope);
      FE.Builder.CreateBr(ContBB);

      FE.Builder.SetInsertPoint(ContBB);
    }
    return;
  }
  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load: {
    llvm::LoadInst *Load = FE.Builder.CreateLoad(Ptr);
    Load->setAtomic(Order, Scope);
    Load->setVolatile(E->isVolatile());
    FE.Builder.CreateStore(Load, Dest);
    return;
  }

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_store:
  case AtomicExpr::AO__scoped_atomic_store_n: {
    llvm::Value *LoadVal1 = FE.Builder.CreateLoad(Val1);
    llvm::StoreInst *Store = FE.Builder.CreateStore(LoadVal1, Ptr);
    Store->setAtomic(Order, Scope);
    Store->setVolatile(E->isVolatile());
    return;
  }

  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__atomic_exchange_n:
  case AtomicExpr::AO__atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_exchange_n:
  case AtomicExpr::AO__scoped_atomic_exchange:
    Op = llvm::AtomicRMWInst::Xchg;
    break;

  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__scoped_atomic_add_fetch:
    PostOp = E->getValueType()->isFloatingType() ? llvm::Instruction::FAdd
                                                 : llvm::Instruction::Add;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__scoped_atomic_fetch_add:
    Op = E->getValueType()->isFloatingType() ? llvm::AtomicRMWInst::FAdd
                                             : llvm::AtomicRMWInst::Add;
    break;

  case AtomicExpr::AO__atomic_sub_fetch:
  case AtomicExpr::AO__scoped_atomic_sub_fetch:
    PostOp = E->getValueType()->isFloatingType() ? llvm::Instruction::FSub
                                                 : llvm::Instruction::Sub;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_sub:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__scoped_atomic_fetch_sub:
    Op = E->getValueType()->isFloatingType() ? llvm::AtomicRMWInst::FSub
                                             : llvm::AtomicRMWInst::Sub;
    break;

  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
    PostOpMinMax = true;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_min:
  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_fetch_min:
    Op = E->getValueType()->isFloatingType()
             ? llvm::AtomicRMWInst::FMin
             : (E->getValueType()->isSignedIntegerType()
                    ? llvm::AtomicRMWInst::Min
                    : llvm::AtomicRMWInst::UMin);
    break;

  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
    PostOpMinMax = true;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_max:
  case AtomicExpr::AO__scoped_atomic_fetch_max:
    Op = E->getValueType()->isFloatingType()
             ? llvm::AtomicRMWInst::FMax
             : (E->getValueType()->isSignedIntegerType()
                    ? llvm::AtomicRMWInst::Max
                    : llvm::AtomicRMWInst::UMax);
    break;

  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__scoped_atomic_and_fetch:
    PostOp = llvm::Instruction::And;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__scoped_atomic_fetch_and:
    Op = llvm::AtomicRMWInst::And;
    break;

  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__scoped_atomic_or_fetch:
    PostOp = llvm::Instruction::Or;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__scoped_atomic_fetch_or:
    Op = llvm::AtomicRMWInst::Or;
    break;

  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__scoped_atomic_xor_fetch:
    PostOp = llvm::Instruction::Xor;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__scoped_atomic_fetch_xor:
    Op = llvm::AtomicRMWInst::Xor;
    break;

  case AtomicExpr::AO__atomic_nand_fetch:
  case AtomicExpr::AO__scoped_atomic_nand_fetch:
    PostOp = llvm::Instruction::And; // the NOT is special cased below
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_nand:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__scoped_atomic_fetch_nand:
    Op = llvm::AtomicRMWInst::Nand;
    break;
  }

  llvm::Value *LoadVal1 = FE.Builder.CreateLoad(Val1);
  llvm::AtomicRMWInst *RMWI =
      FE.Builder.CreateAtomicRMW(Op, Ptr, LoadVal1, Order, Scope);
  RMWI->setVolatile(E->isVolatile());

  // For __atomic_*_fetch operations, perform the operation again to
  // determine the value which was written.
  llvm::Value *Result = RMWI;
  if (PostOpMinMax)
    Result = genPostAtomicMinMax(FE.Builder, E->getOp(),
                                 E->getValueType()->isSignedIntegerType(), RMWI,
                                 LoadVal1);
  else if (PostOp)
    Result = FE.Builder.CreateBinOp((llvm::Instruction::BinaryOps)PostOp, RMWI,
                                    LoadVal1);
  if (E->getOp() == AtomicExpr::AO__atomic_nand_fetch ||
      E->getOp() == AtomicExpr::AO__scoped_atomic_nand_fetch)
    Result = FE.Builder.CreateNot(Result);
  FE.Builder.CreateStore(Result, Dest);
}

// This function emits any expression (scalar, complex, or aggregate)
// into a temporary alloca.
Address genValToTemp(FunctionEmitter &FE, Expr *E) {
  Address DeclPtr = FE.createMemTemp(E->getType(), ".atomictmp");
  FE.genAnyExprToMem(E, DeclPtr, E->getType().getQualifiers(),
                     /*Init*/ true);
  return DeclPtr;
}

void genAtomicOp(FunctionEmitter &FE, AtomicExpr *Expr, Address Dest,
                 Address Ptr, Address Val1, Address Val2, llvm::Value *IsWeak,
                 llvm::Value *FailureOrder, uint64_t Size,
                 llvm::AtomicOrdering Order, llvm::Value *Scope) {
  auto ScopeModel = Expr->getScopeModel();

  // LLVM atomic instructions always have synch scope. If NeverC atomic
  // expression has no scope operand, use default LLVM synch scope.
  if (!ScopeModel) {
    genAtomicOp(FE, Expr, Dest, Ptr, Val1, Val2, IsWeak, FailureOrder, Size,
                Order, FE.ME.getLLVMContext().getOrInsertSyncScopeID(""));
    return;
  }

  if (auto SC = dyn_cast<llvm::ConstantInt>(Scope)) {
    auto SCID = FE.getTargetHooks().getLLVMSyncScopeID(
        FE.ME.getLangOpts(), ScopeModel->map(SC->getZExtValue()), Order,
        FE.ME.getLLVMContext());
    genAtomicOp(FE, Expr, Dest, Ptr, Val1, Val2, IsWeak, FailureOrder, Size,
                Order, SCID);
    return;
  }

  auto &Builder = FE.Builder;
  auto Scopes = ScopeModel->getRuntimeValues();
  llvm::DenseMap<unsigned, llvm::BasicBlock *> BB;
  for (auto S : Scopes)
    BB[S] = FE.createBasicBlock(getAsString(ScopeModel->map(S)), FE.CurFn);

  llvm::BasicBlock *ContBB =
      FE.createBasicBlock("atomic.scope.continue", FE.CurFn);

  auto *SC = Builder.CreateIntCast(Scope, Builder.getInt32Ty(), false);
  // If unsupported synch scope is encountered at run time, assume a fallback
  // synch scope value.
  auto FallBack = ScopeModel->getFallBackValue();
  llvm::SwitchInst *SI = Builder.CreateSwitch(SC, BB[FallBack]);
  for (auto S : Scopes) {
    auto *B = BB[S];
    if (S != FallBack)
      SI->addCase(Builder.getInt32(S), B);

    Builder.SetInsertPoint(B);
    genAtomicOp(FE, Expr, Dest, Ptr, Val1, Val2, IsWeak, FailureOrder, Size,
                Order,
                FE.getTargetHooks().getLLVMSyncScopeID(
                    FE.ME.getLangOpts(), ScopeModel->map(S), Order,
                    FE.getLLVMContext()));
    Builder.CreateBr(ContBB);
  }

  Builder.SetInsertPoint(ContBB);
}

void addDirectArgument(FunctionEmitter &FE, CallArgList &Args,
                       bool UseOptimizedLibcall, llvm::Value *Val,
                       QualType ValTy, SourceLocation Loc,
                       CharUnits SizeInChars) {
  if (UseOptimizedLibcall) {
    CharUnits Align = FE.getContext().getTypeAlignInChars(ValTy);
    int64_t SizeInBits = FE.getContext().toBits(SizeInChars);
    ValTy = FE.getContext().getIntTypeForBitwidth(SizeInBits, /*Signed=*/false);
    llvm::Type *ITy = llvm::IntegerType::get(FE.getLLVMContext(), SizeInBits);
    Address Ptr = Address(Val, ITy, Align);
    Val = FE.genLoadOfScalar(Ptr, false, FE.getContext().getPointerType(ValTy),
                             Loc);
    // Coerce the value into an appropriately sized integer type.
    Args.add(RValue::get(Val), ValTy);
  } else {
    // Non-optimized functions always take a reference.
    Args.add(RValue::get(Val), FE.getContext().VoidPtrTy);
  }
}
} // namespace

RValue FunctionEmitter::genAtomicExpr(AtomicExpr *E) {
  QualType AtomicTy = E->getPtr()->getType()->getPointeeType();
  QualType MemTy = AtomicTy;
  if (const AtomicType *AT = AtomicTy->getAs<AtomicType>())
    MemTy = AT->getValueType();
  llvm::Value *IsWeak = nullptr, *OrderFail = nullptr;

  Address Val1 = Address::invalid();
  Address Val2 = Address::invalid();
  Address Dest = Address::invalid();
  Address Ptr = genPointerWithAlignment(E->getPtr());

  if (E->getOp() == AtomicExpr::AO__c11_atomic_init) {
    LValue lvalue = makeAddrLValue(Ptr, AtomicTy);
    genAtomicInit(E->getVal1(), lvalue);
    return RValue::get(nullptr);
  }

  auto TInfo = getContext().getTypeInfoInChars(AtomicTy);
  uint64_t Size = TInfo.Width.getQuantity();
  unsigned MaxInlineWidthInBits = getTarget().getMaxAtomicInlineWidth();

  bool Oversized = getContext().toBits(TInfo.Width) > MaxInlineWidthInBits;
  bool Misaligned = TInfo.Width.getQuantity() == 0
                        ? true
                        : (Ptr.getAlignment() % TInfo.Width) != 0;
  bool UseLibcall = Misaligned || Oversized;
  bool ShouldCastToIntPtrTy = true;

  CharUnits MaxInlineWidth =
      getContext().toCharUnitsFromBits(MaxInlineWidthInBits);

  DiagnosticsEngine &Diags = ME.getDiags();

  if (Misaligned) {
    Diags.Report(E->getBeginLoc(), diag::warn_atomic_op_misaligned)
        << (int)TInfo.Width.getQuantity()
        << (int)Ptr.getAlignment().getQuantity();
  }

  if (Oversized) {
    Diags.Report(E->getBeginLoc(), diag::warn_atomic_op_oversized)
        << (int)TInfo.Width.getQuantity() << (int)MaxInlineWidth.getQuantity();
  }

  llvm::Value *Order = genScalarExpr(E->getOrder());
  llvm::Value *Scope =
      E->getScopeModel() ? genScalarExpr(E->getScope()) : nullptr;

  switch (E->getOp()) {
  case AtomicExpr::AO__c11_atomic_init:
    llvm_unreachable("Already handled above with genAtomicInit!");

  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load_n:
  case AtomicExpr::AO__c11_atomic_load:
    break;

  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load:
    Dest = genPointerWithAlignment(E->getVal1());
    break;

  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__scoped_atomic_store:
    Val1 = genPointerWithAlignment(E->getVal1());
    break;

  case AtomicExpr::AO__atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_exchange:
    Val1 = genPointerWithAlignment(E->getVal1());
    Dest = genPointerWithAlignment(E->getVal2());
    break;

  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
  case AtomicExpr::AO__scoped_atomic_compare_exchange:
  case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
    Val1 = genPointerWithAlignment(E->getVal1());
    if (E->getOp() == AtomicExpr::AO__atomic_compare_exchange ||
        E->getOp() == AtomicExpr::AO__scoped_atomic_compare_exchange)
      Val2 = genPointerWithAlignment(E->getVal2());
    else
      Val2 = genValToTemp(*this, E->getVal2());
    OrderFail = genScalarExpr(E->getOrderFail());
    if (E->getOp() == AtomicExpr::AO__atomic_compare_exchange_n ||
        E->getOp() == AtomicExpr::AO__atomic_compare_exchange ||
        E->getOp() == AtomicExpr::AO__scoped_atomic_compare_exchange_n ||
        E->getOp() == AtomicExpr::AO__scoped_atomic_compare_exchange)
      IsWeak = genScalarExpr(E->getWeak());
    break;

  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__c11_atomic_fetch_sub:
    if (MemTy->isPointerType()) {
      // For pointer arithmetic, we're required to do a bit of math:
      // adding 1 to an int* is not the same as adding 1 to a uintptr_t.
      // ... but only for the C11 builtins. The GNU builtins expect the
      // user to multiply by sizeof(T).
      QualType Val1Ty = E->getVal1()->getType();
      llvm::Value *Val1Scalar = genScalarExpr(E->getVal1());
      CharUnits PointeeIncAmt =
          getContext().getTypeSizeInChars(MemTy->getPointeeType());
      Val1Scalar = Builder.CreateMul(Val1Scalar, ME.getSize(PointeeIncAmt));
      auto Temp = createMemTemp(Val1Ty, ".atomictmp");
      Val1 = Temp;
      genStoreOfScalar(Val1Scalar, makeAddrLValue(Temp, Val1Ty));
      break;
    }
    [[fallthrough]];
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__atomic_sub_fetch:
  case AtomicExpr::AO__c11_atomic_fetch_max:
  case AtomicExpr::AO__c11_atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_fetch_add:
  case AtomicExpr::AO__scoped_atomic_fetch_max:
  case AtomicExpr::AO__scoped_atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_fetch_sub:
  case AtomicExpr::AO__scoped_atomic_add_fetch:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_sub_fetch:
    ShouldCastToIntPtrTy = !MemTy->isFloatingType();
    [[fallthrough]];

  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__atomic_nand_fetch:
  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__atomic_exchange_n:
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__c11_atomic_fetch_nand:
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_fetch_and:
  case AtomicExpr::AO__scoped_atomic_fetch_nand:
  case AtomicExpr::AO__scoped_atomic_fetch_or:
  case AtomicExpr::AO__scoped_atomic_fetch_xor:
  case AtomicExpr::AO__scoped_atomic_and_fetch:
  case AtomicExpr::AO__scoped_atomic_nand_fetch:
  case AtomicExpr::AO__scoped_atomic_or_fetch:
  case AtomicExpr::AO__scoped_atomic_xor_fetch:
  case AtomicExpr::AO__scoped_atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_exchange_n:
    Val1 = genValToTemp(*this, E->getVal1());
    break;
  }

  QualType RValTy = E->getType().getUnqualifiedType();

  // The inlined atomics only function on iN types, where N is a power of 2. We
  // need to make sure (via temporaries if necessary) that all incoming values
  // are compatible.
  LValue AtomicVal = makeAddrLValue(Ptr, AtomicTy);
  AtomicInfo Atomics(*this, AtomicVal);

  if (ShouldCastToIntPtrTy) {
    Ptr = Atomics.castToAtomicIntPointer(Ptr);
    if (Val1.isValid())
      Val1 = Atomics.convertToAtomicIntPointer(Val1);
    if (Val2.isValid())
      Val2 = Atomics.convertToAtomicIntPointer(Val2);
  }
  if (Dest.isValid()) {
    if (ShouldCastToIntPtrTy)
      Dest = Atomics.castToAtomicIntPointer(Dest);
  } else if (E->isCmpXChg())
    Dest = createMemTemp(RValTy, "cmpxchg.bool");
  else if (!RValTy->isVoidType()) {
    Dest = Atomics.createTempAlloca();
    if (ShouldCastToIntPtrTy)
      Dest = Atomics.castToAtomicIntPointer(Dest);
  }

  // Use a library call.  See: http://gcc.gnu.org/wiki/Atomic/GCCMM/LIbrary .
  if (UseLibcall) {
    bool UseOptimizedLibcall = false;
    switch (E->getOp()) {
    case AtomicExpr::AO__c11_atomic_init:
      llvm_unreachable("Already handled above with genAtomicInit!");

    case AtomicExpr::AO__atomic_fetch_add:
    case AtomicExpr::AO__atomic_fetch_and:
    case AtomicExpr::AO__atomic_fetch_max:
    case AtomicExpr::AO__atomic_fetch_min:
    case AtomicExpr::AO__atomic_fetch_nand:
    case AtomicExpr::AO__atomic_fetch_or:
    case AtomicExpr::AO__atomic_fetch_sub:
    case AtomicExpr::AO__atomic_fetch_xor:
    case AtomicExpr::AO__atomic_add_fetch:
    case AtomicExpr::AO__atomic_and_fetch:
    case AtomicExpr::AO__atomic_max_fetch:
    case AtomicExpr::AO__atomic_min_fetch:
    case AtomicExpr::AO__atomic_nand_fetch:
    case AtomicExpr::AO__atomic_or_fetch:
    case AtomicExpr::AO__atomic_sub_fetch:
    case AtomicExpr::AO__atomic_xor_fetch:
    case AtomicExpr::AO__c11_atomic_fetch_add:
    case AtomicExpr::AO__c11_atomic_fetch_and:
    case AtomicExpr::AO__c11_atomic_fetch_max:
    case AtomicExpr::AO__c11_atomic_fetch_min:
    case AtomicExpr::AO__c11_atomic_fetch_nand:
    case AtomicExpr::AO__c11_atomic_fetch_or:
    case AtomicExpr::AO__c11_atomic_fetch_sub:
    case AtomicExpr::AO__c11_atomic_fetch_xor:
    case AtomicExpr::AO__scoped_atomic_fetch_add:
    case AtomicExpr::AO__scoped_atomic_fetch_and:
    case AtomicExpr::AO__scoped_atomic_fetch_max:
    case AtomicExpr::AO__scoped_atomic_fetch_min:
    case AtomicExpr::AO__scoped_atomic_fetch_nand:
    case AtomicExpr::AO__scoped_atomic_fetch_or:
    case AtomicExpr::AO__scoped_atomic_fetch_sub:
    case AtomicExpr::AO__scoped_atomic_fetch_xor:
    case AtomicExpr::AO__scoped_atomic_add_fetch:
    case AtomicExpr::AO__scoped_atomic_and_fetch:
    case AtomicExpr::AO__scoped_atomic_max_fetch:
    case AtomicExpr::AO__scoped_atomic_min_fetch:
    case AtomicExpr::AO__scoped_atomic_nand_fetch:
    case AtomicExpr::AO__scoped_atomic_or_fetch:
    case AtomicExpr::AO__scoped_atomic_sub_fetch:
    case AtomicExpr::AO__scoped_atomic_xor_fetch:
      // For these, only library calls for certain sizes exist.
      UseOptimizedLibcall = true;
      break;

    case AtomicExpr::AO__atomic_load:
    case AtomicExpr::AO__atomic_store:
    case AtomicExpr::AO__atomic_exchange:
    case AtomicExpr::AO__atomic_compare_exchange:
    case AtomicExpr::AO__scoped_atomic_load:
    case AtomicExpr::AO__scoped_atomic_store:
    case AtomicExpr::AO__scoped_atomic_exchange:
    case AtomicExpr::AO__scoped_atomic_compare_exchange:
      // Use the generic version if we don't know that the operand will be
      // suitably aligned for the optimized version.
      if (Misaligned)
        break;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_load_n:
    case AtomicExpr::AO__atomic_store_n:
    case AtomicExpr::AO__atomic_exchange_n:
    case AtomicExpr::AO__atomic_compare_exchange_n:
    case AtomicExpr::AO__c11_atomic_load:
    case AtomicExpr::AO__c11_atomic_store:
    case AtomicExpr::AO__c11_atomic_exchange:
    case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
    case AtomicExpr::AO__scoped_atomic_load_n:
    case AtomicExpr::AO__scoped_atomic_store_n:
    case AtomicExpr::AO__scoped_atomic_exchange_n:
    case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
      // Only use optimized library calls for sizes for which they exist.
      if (Size == 1 || Size == 2 || Size == 4 || Size == 8)
        UseOptimizedLibcall = true;
      break;
    }

    CallArgList Args;
    if (!UseOptimizedLibcall) {
      // For non-optimized library calls, the size is the first parameter
      Args.add(RValue::get(llvm::ConstantInt::get(SizeTy, Size)),
               getContext().getSizeType());
    }
    // Atomic address is the first or second parameter
    auto CastToGenericAddrSpace = [&](llvm::Value *V, QualType) { return V; };

    Args.add(RValue::get(CastToGenericAddrSpace(Ptr.getPointer(),
                                                E->getPtr()->getType())),
             getContext().VoidPtrTy);

    std::string LibCallName;
    QualType LoweredMemTy =
        MemTy->isPointerType() ? getContext().getIntPtrType() : MemTy;
    QualType RetTy;
    bool HaveRetTy = false;
    llvm::Instruction::BinaryOps PostOp = (llvm::Instruction::BinaryOps)0;
    bool PostOpMinMax = false;
    switch (E->getOp()) {
    case AtomicExpr::AO__c11_atomic_init:
      llvm_unreachable("Already handled!");

    // There is only one libcall for compare an exchange, because there is no
    // optimisation benefit possible from a libcall version of a weak compare
    // and exchange.
    // bool __atomic_compare_exchange(size_t size, void *mem, void *expected,
    //                                void *desired, int success, int failure)
    // bool __atomic_compare_exchange_N(T *mem, T *expected, T desired,
    //                                  int success, int failure)
    case AtomicExpr::AO__atomic_compare_exchange:
    case AtomicExpr::AO__atomic_compare_exchange_n:
    case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
    case AtomicExpr::AO__scoped_atomic_compare_exchange:
    case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
      LibCallName = "__atomic_compare_exchange";
      RetTy = getContext().BoolTy;
      HaveRetTy = true;
      Args.add(RValue::get(CastToGenericAddrSpace(Val1.getPointer(),
                                                  E->getVal1()->getType())),
               getContext().VoidPtrTy);
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val2.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      Args.add(RValue::get(Order), getContext().IntTy);
      Order = OrderFail;
      break;
    // void __atomic_exchange(size_t size, void *mem, void *val, void *return,
    //                        int order)
    // T __atomic_exchange_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_exchange:
    case AtomicExpr::AO__atomic_exchange_n:
    case AtomicExpr::AO__c11_atomic_exchange:
    case AtomicExpr::AO__scoped_atomic_exchange:
    case AtomicExpr::AO__scoped_atomic_exchange_n:
      LibCallName = "__atomic_exchange";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      break;
    // void __atomic_store(size_t size, void *mem, void *val, int order)
    // void __atomic_store_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_store:
    case AtomicExpr::AO__atomic_store_n:
    case AtomicExpr::AO__c11_atomic_store:
    case AtomicExpr::AO__scoped_atomic_store:
    case AtomicExpr::AO__scoped_atomic_store_n:
      LibCallName = "__atomic_store";
      RetTy = getContext().VoidTy;
      HaveRetTy = true;
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      break;
    // void __atomic_load(size_t size, void *mem, void *return, int order)
    // T __atomic_load_N(T *mem, int order)
    case AtomicExpr::AO__atomic_load:
    case AtomicExpr::AO__atomic_load_n:
    case AtomicExpr::AO__c11_atomic_load:
    case AtomicExpr::AO__scoped_atomic_load:
    case AtomicExpr::AO__scoped_atomic_load_n:
      LibCallName = "__atomic_load";
      break;
    // T __atomic_add_fetch_N(T *mem, T val, int order)
    // T __atomic_fetch_add_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_add_fetch:
    case AtomicExpr::AO__scoped_atomic_add_fetch:
      PostOp = llvm::Instruction::Add;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_add:
    case AtomicExpr::AO__c11_atomic_fetch_add:
    case AtomicExpr::AO__scoped_atomic_fetch_add:
      LibCallName = "__atomic_fetch_add";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        LoweredMemTy, E->getExprLoc(), TInfo.Width);
      break;
    // T __atomic_and_fetch_N(T *mem, T val, int order)
    // T __atomic_fetch_and_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_and_fetch:
    case AtomicExpr::AO__scoped_atomic_and_fetch:
      PostOp = llvm::Instruction::And;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_and:
    case AtomicExpr::AO__c11_atomic_fetch_and:
    case AtomicExpr::AO__scoped_atomic_fetch_and:
      LibCallName = "__atomic_fetch_and";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      break;
    // T __atomic_or_fetch_N(T *mem, T val, int order)
    // T __atomic_fetch_or_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_or_fetch:
    case AtomicExpr::AO__scoped_atomic_or_fetch:
      PostOp = llvm::Instruction::Or;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_or:
    case AtomicExpr::AO__c11_atomic_fetch_or:
    case AtomicExpr::AO__scoped_atomic_fetch_or:
      LibCallName = "__atomic_fetch_or";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      break;
    // T __atomic_sub_fetch_N(T *mem, T val, int order)
    // T __atomic_fetch_sub_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_sub_fetch:
    case AtomicExpr::AO__scoped_atomic_sub_fetch:
      PostOp = llvm::Instruction::Sub;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_sub:
    case AtomicExpr::AO__c11_atomic_fetch_sub:
    case AtomicExpr::AO__scoped_atomic_fetch_sub:
      LibCallName = "__atomic_fetch_sub";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        LoweredMemTy, E->getExprLoc(), TInfo.Width);
      break;
    // T __atomic_xor_fetch_N(T *mem, T val, int order)
    // T __atomic_fetch_xor_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_xor_fetch:
    case AtomicExpr::AO__scoped_atomic_xor_fetch:
      PostOp = llvm::Instruction::Xor;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_xor:
    case AtomicExpr::AO__c11_atomic_fetch_xor:
    case AtomicExpr::AO__scoped_atomic_fetch_xor:
      LibCallName = "__atomic_fetch_xor";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      break;
    case AtomicExpr::AO__atomic_min_fetch:
    case AtomicExpr::AO__scoped_atomic_min_fetch:
      PostOpMinMax = true;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_min:
    case AtomicExpr::AO__c11_atomic_fetch_min:
    case AtomicExpr::AO__scoped_atomic_fetch_min:
      LibCallName = E->getValueType()->isSignedIntegerType()
                        ? "__atomic_fetch_min"
                        : "__atomic_fetch_umin";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        LoweredMemTy, E->getExprLoc(), TInfo.Width);
      break;
    case AtomicExpr::AO__atomic_max_fetch:
    case AtomicExpr::AO__scoped_atomic_max_fetch:
      PostOpMinMax = true;
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_max:
    case AtomicExpr::AO__c11_atomic_fetch_max:
    case AtomicExpr::AO__scoped_atomic_fetch_max:
      LibCallName = E->getValueType()->isSignedIntegerType()
                        ? "__atomic_fetch_max"
                        : "__atomic_fetch_umax";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        LoweredMemTy, E->getExprLoc(), TInfo.Width);
      break;
    // T __atomic_nand_fetch_N(T *mem, T val, int order)
    // T __atomic_fetch_nand_N(T *mem, T val, int order)
    case AtomicExpr::AO__atomic_nand_fetch:
    case AtomicExpr::AO__scoped_atomic_nand_fetch:
      PostOp = llvm::Instruction::And; // the NOT is special cased below
      [[fallthrough]];
    case AtomicExpr::AO__atomic_fetch_nand:
    case AtomicExpr::AO__c11_atomic_fetch_nand:
    case AtomicExpr::AO__scoped_atomic_fetch_nand:
      LibCallName = "__atomic_fetch_nand";
      addDirectArgument(*this, Args, UseOptimizedLibcall, Val1.getPointer(),
                        MemTy, E->getExprLoc(), TInfo.Width);
      break;
    }

    // Optimized functions have the size in their name.
    if (UseOptimizedLibcall)
      LibCallName += "_" + llvm::utostr(Size);
    // By default, assume we return a value of the atomic type.
    if (!HaveRetTy) {
      if (UseOptimizedLibcall) {
        // Value is returned directly.
        // The function returns an appropriately sized integer type.
        RetTy = getContext().getIntTypeForBitwidth(
            getContext().toBits(TInfo.Width), /*Signed=*/false);
      } else {
        // Value is returned through parameter before the order.
        RetTy = getContext().VoidTy;
        Args.add(RValue::get(Dest.getPointer()), getContext().VoidPtrTy);
      }
    }
    // order is always the last parameter
    Args.add(RValue::get(Order), getContext().IntTy);

    // PostOp is only needed for the atomic_*_fetch operations, and
    // thus is only needed for and implemented in the
    // UseOptimizedLibcall codepath.
    assert(UseOptimizedLibcall || (!PostOp && !PostOpMinMax));

    RValue Res = emitAtomicLibcall(*this, LibCallName, RetTy, Args);
    // The value is returned directly from the libcall.
    if (E->isCmpXChg())
      return Res;

    // The value is returned directly for optimized libcalls but the expr
    // provided an out-param.
    if (UseOptimizedLibcall && Res.getScalarVal()) {
      llvm::Value *ResVal = Res.getScalarVal();
      if (PostOpMinMax) {
        llvm::Value *LoadVal1 = Args[1].getRValue(*this).getScalarVal();
        ResVal = genPostAtomicMinMax(Builder, E->getOp(),
                                     E->getValueType()->isSignedIntegerType(),
                                     ResVal, LoadVal1);
      } else if (PostOp) {
        llvm::Value *LoadVal1 = Args[1].getRValue(*this).getScalarVal();
        ResVal = Builder.CreateBinOp(PostOp, ResVal, LoadVal1);
      }
      if (E->getOp() == AtomicExpr::AO__atomic_nand_fetch ||
          E->getOp() == AtomicExpr::AO__scoped_atomic_nand_fetch)
        ResVal = Builder.CreateNot(ResVal);

      Builder.CreateStore(ResVal, Dest.withElementType(ResVal->getType()));
    }

    if (RValTy->isVoidType())
      return RValue::get(nullptr);

    return convertTempToRValue(Dest.withElementType(convertTypeForMem(RValTy)),
                               RValTy, E->getExprLoc());
  }

  bool IsStore = E->getOp() == AtomicExpr::AO__c11_atomic_store ||
                 E->getOp() == AtomicExpr::AO__atomic_store ||
                 E->getOp() == AtomicExpr::AO__atomic_store_n ||
                 E->getOp() == AtomicExpr::AO__scoped_atomic_store ||
                 E->getOp() == AtomicExpr::AO__scoped_atomic_store_n;
  bool IsLoad = E->getOp() == AtomicExpr::AO__c11_atomic_load ||
                E->getOp() == AtomicExpr::AO__atomic_load ||
                E->getOp() == AtomicExpr::AO__atomic_load_n ||
                E->getOp() == AtomicExpr::AO__scoped_atomic_load ||
                E->getOp() == AtomicExpr::AO__scoped_atomic_load_n;

  if (isa<llvm::ConstantInt>(Order)) {
    auto ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
    // We should not ever get to a case where the ordering isn't a valid C ABI
    // value, but it's hard to enforce that in general.
    if (llvm::isValidAtomicOrderingCABI(ord))
      switch ((llvm::AtomicOrderingCABI)ord) {
      case llvm::AtomicOrderingCABI::relaxed:
        genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                    llvm::AtomicOrdering::Monotonic, Scope);
        break;
      case llvm::AtomicOrderingCABI::consume:
      case llvm::AtomicOrderingCABI::acquire:
        if (IsStore)
          break; // Avoid crashing on code with undefined behavior
        genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                    llvm::AtomicOrdering::Acquire, Scope);
        break;
      case llvm::AtomicOrderingCABI::release:
        if (IsLoad)
          break; // Avoid crashing on code with undefined behavior
        genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                    llvm::AtomicOrdering::Release, Scope);
        break;
      case llvm::AtomicOrderingCABI::acq_rel:
        if (IsLoad || IsStore)
          break; // Avoid crashing on code with undefined behavior
        genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                    llvm::AtomicOrdering::AcquireRelease, Scope);
        break;
      case llvm::AtomicOrderingCABI::seq_cst:
        genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                    llvm::AtomicOrdering::SequentiallyConsistent, Scope);
        break;
      }
    if (RValTy->isVoidType())
      return RValue::get(nullptr);

    return convertTempToRValue(Dest.withElementType(convertTypeForMem(RValTy)),
                               RValTy, E->getExprLoc());
  }

  // Long case, when Order isn't obviously constant.

  llvm::BasicBlock *MonotonicBB = nullptr, *AcquireBB = nullptr,
                   *ReleaseBB = nullptr, *AcqRelBB = nullptr,
                   *SeqCstBB = nullptr;
  MonotonicBB = createBasicBlock("monotonic", CurFn);
  if (!IsStore)
    AcquireBB = createBasicBlock("acquire", CurFn);
  if (!IsLoad)
    ReleaseBB = createBasicBlock("release", CurFn);
  if (!IsLoad && !IsStore)
    AcqRelBB = createBasicBlock("acqrel", CurFn);
  SeqCstBB = createBasicBlock("seqcst", CurFn);
  llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

  // MonotonicBB is arbitrarily chosen as the default case; in practice, this
  // doesn't matter unless someone is crazy enough to use something that
  // doesn't fold to a constant for the ordering.
  Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
  llvm::SwitchInst *SI = Builder.CreateSwitch(Order, MonotonicBB);

  Builder.SetInsertPoint(MonotonicBB);
  genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
              llvm::AtomicOrdering::Monotonic, Scope);
  Builder.CreateBr(ContBB);
  if (!IsStore) {
    Builder.SetInsertPoint(AcquireBB);
    genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                llvm::AtomicOrdering::Acquire, Scope);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::consume),
                AcquireBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::acquire),
                AcquireBB);
  }
  if (!IsLoad) {
    Builder.SetInsertPoint(ReleaseBB);
    genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                llvm::AtomicOrdering::Release, Scope);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::release),
                ReleaseBB);
  }
  if (!IsLoad && !IsStore) {
    Builder.SetInsertPoint(AcqRelBB);
    genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                llvm::AtomicOrdering::AcquireRelease, Scope);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::acq_rel),
                AcqRelBB);
  }
  Builder.SetInsertPoint(SeqCstBB);
  genAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
              llvm::AtomicOrdering::SequentiallyConsistent, Scope);
  Builder.CreateBr(ContBB);
  SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::seq_cst),
              SeqCstBB);

  // Cleanup and return
  Builder.SetInsertPoint(ContBB);
  if (RValTy->isVoidType())
    return RValue::get(nullptr);

  assert(Atomics.getValueSizeInBits() <= Atomics.getAtomicSizeInBits());
  return convertTempToRValue(Dest.withElementType(convertTypeForMem(RValTy)),
                             RValTy, E->getExprLoc());
}

// ===----------------------------------------------------------------------===
// AtomicInfo load, store & update
// ===----------------------------------------------------------------------===

Address AtomicInfo::castToAtomicIntPointer(Address addr) const {
  llvm::IntegerType *ty =
      llvm::IntegerType::get(FE.getLLVMContext(), AtomicSizeInBits);
  return addr.withElementType(ty);
}

Address AtomicInfo::convertToAtomicIntPointer(Address Addr) const {
  llvm::Type *Ty = Addr.getElementType();
  uint64_t SourceSizeInBits = FE.ME.getDataLayout().getTypeSizeInBits(Ty);
  if (SourceSizeInBits != AtomicSizeInBits) {
    Address Tmp = createTempAlloca();
    FE.Builder.CreateMemCpy(Tmp, Addr,
                            std::min(AtomicSizeInBits, SourceSizeInBits) / 8);
    Addr = Tmp;
  }

  return castToAtomicIntPointer(Addr);
}

RValue AtomicInfo::convertAtomicTempToRValue(Address addr,
                                             AggValueSlot resultSlot,
                                             SourceLocation loc,
                                             bool asValue) const {
  if (LVal.isSimple()) {
    if (EvaluationKind == TEK_Aggregate)
      return resultSlot.asRValue();

    // Drill into the padding structure if we have one.
    if (hasPadding())
      addr = FE.Builder.CreateStructGEP(addr, 0);

    // Otherwise, just convert the temporary to an r-value using the
    // normal conversion routine.
    return FE.convertTempToRValue(addr, getValueType(), loc);
  }
  if (!asValue)
    return RValue::get(FE.Builder.CreateLoad(addr));
  if (LVal.isBitField())
    return FE.genLoadOfBitfieldLValue(
        LValue::MakeBitfield(addr, LVal.getBitFieldInfo(), LVal.getType(),
                             LVal.getBaseInfo(), TBAAAccessInfo()),
        loc);
  if (LVal.isVectorElt())
    return FE.genLoadOfLValue(
        LValue::MakeVectorElt(addr, LVal.getVectorIdx(), LVal.getType(),
                              LVal.getBaseInfo(), TBAAAccessInfo()),
        loc);
  assert(LVal.isExtVectorElt());
  return FE.genLoadOfExtVectorElementLValue(
      LValue::MakeExtVectorElt(addr, LVal.getExtVectorElts(), LVal.getType(),
                               LVal.getBaseInfo(), TBAAAccessInfo()));
}

RValue AtomicInfo::ConvertIntToValueOrAtomic(llvm::Value *IntVal,
                                             AggValueSlot ResultSlot,
                                             SourceLocation Loc,
                                             bool AsValue) const {
  // Try not to in some easy cases.
  assert(IntVal->getType()->isIntegerTy() && "Expected integer value");
  if (getEvaluationKind() == TEK_Scalar &&
      (((!LVal.isBitField() ||
         LVal.getBitFieldInfo().Size == ValueSizeInBits) &&
        !hasPadding()) ||
       !AsValue)) {
    auto *ValTy = AsValue ? FE.convertTypeForMem(ValueTy)
                          : getAtomicAddress().getElementType();
    if (ValTy->isIntegerTy()) {
      assert(IntVal->getType() == ValTy && "Different integer types.");
      return RValue::get(FE.genFromMemory(IntVal, ValueTy));
    } else if (ValTy->isPointerTy())
      return RValue::get(FE.Builder.CreateIntToPtr(IntVal, ValTy));
    else if (llvm::CastInst::isBitCastable(IntVal->getType(), ValTy))
      return RValue::get(FE.Builder.CreateBitCast(IntVal, ValTy));
  }

  Address Temp = Address::invalid();
  bool TempIsVolatile = false;
  if (AsValue && getEvaluationKind() == TEK_Aggregate) {
    assert(!ResultSlot.isIgnored());
    Temp = ResultSlot.getAddress();
    TempIsVolatile = ResultSlot.isVolatile();
  } else {
    Temp = createTempAlloca();
  }

  // Slam the integer into the temporary.
  Address CastTemp = castToAtomicIntPointer(Temp);
  FE.Builder.CreateStore(IntVal, CastTemp)->setVolatile(TempIsVolatile);

  return convertAtomicTempToRValue(Temp, ResultSlot, Loc, AsValue);
}

void AtomicInfo::genAtomicLoadLibcall(llvm::Value *AddForLoaded,
                                      llvm::AtomicOrdering AO, bool) {
  // void __atomic_load(size_t size, void *mem, void *return, int order);
  CallArgList Args;
  Args.add(RValue::get(getAtomicSizeValue()), FE.getContext().getSizeType());
  Args.add(RValue::get(getAtomicPointer()), FE.getContext().VoidPtrTy);
  Args.add(RValue::get(AddForLoaded), FE.getContext().VoidPtrTy);
  Args.add(RValue::get(llvm::ConstantInt::get(FE.IntTy, (int)llvm::toCABI(AO))),
           FE.getContext().IntTy);
  emitAtomicLibcall(FE, "__atomic_load", FE.getContext().VoidTy, Args);
}

llvm::Value *AtomicInfo::genAtomicLoadOp(llvm::AtomicOrdering AO,
                                         bool IsVolatile) {
  // Okay, we're doing this natively.
  Address Addr = getAtomicAddressAsAtomicIntPointer();
  llvm::LoadInst *Load = FE.Builder.CreateLoad(Addr, "atomic-load");
  Load->setAtomic(AO);

  // Other decoration.
  if (IsVolatile)
    Load->setVolatile(true);
  FE.ME.decorateInstructionWithTBAA(Load, LVal.getTBAAInfo());
  return Load;
}

bool FunctionEmitter::lValueIsSuitableForInlineAtomic(LValue LV) {
  if (!ME.getLangOpts().MSVolatile)
    return false;
  AtomicInfo AI(*this, LV);
  bool IsVolatile = LV.isVolatile() || hasVolatileMember(LV.getType());
  // An atomic is inline if we don't need to use a libcall.
  bool AtomicIsInline = !AI.shouldUseLibcall();
  // MSVC doesn't seem to do this for types wider than a pointer.
  if (getContext().getTypeSize(LV.getType()) >
      getContext().getTypeSize(getContext().getIntPtrType()))
    return false;
  return IsVolatile && AtomicIsInline;
}

RValue FunctionEmitter::genAtomicLoad(LValue LV, SourceLocation SL,
                                      AggValueSlot Slot) {
  llvm::AtomicOrdering AO;
  bool IsVolatile = LV.isVolatileQualified();
  if (LV.getType()->isAtomicType()) {
    AO = llvm::AtomicOrdering::SequentiallyConsistent;
  } else {
    AO = llvm::AtomicOrdering::Acquire;
    IsVolatile = true;
  }
  return genAtomicLoad(LV, SL, AO, IsVolatile, Slot);
}

RValue AtomicInfo::genAtomicLoad(AggValueSlot ResultSlot, SourceLocation Loc,
                                 bool AsValue, llvm::AtomicOrdering AO,
                                 bool IsVolatile) {
  if (shouldUseLibcall()) {
    Address TempAddr = Address::invalid();
    if (LVal.isSimple() && !ResultSlot.isIgnored()) {
      assert(getEvaluationKind() == TEK_Aggregate);
      TempAddr = ResultSlot.getAddress();
    } else
      TempAddr = createTempAlloca();

    genAtomicLoadLibcall(TempAddr.getPointer(), AO, IsVolatile);

    // Okay, turn that back into the original value or whole atomic (for
    // non-simple lvalues) type.
    return convertAtomicTempToRValue(TempAddr, ResultSlot, Loc, AsValue);
  }

  // Okay, we're doing this natively.
  auto *Load = genAtomicLoadOp(AO, IsVolatile);

  // If we're ignoring an aggregate return, don't do anything.
  if (getEvaluationKind() == TEK_Aggregate && ResultSlot.isIgnored())
    return RValue::getAggregate(Address::invalid(), false);

  // Okay, turn that back into the original value or atomic (for non-simple
  // lvalues) type.
  return ConvertIntToValueOrAtomic(Load, ResultSlot, Loc, AsValue);
}

RValue FunctionEmitter::genAtomicLoad(LValue src, SourceLocation loc,
                                      llvm::AtomicOrdering AO, bool IsVolatile,
                                      AggValueSlot resultSlot) {
  AtomicInfo Atomics(*this, src);
  return Atomics.genAtomicLoad(resultSlot, loc, /*AsValue=*/true, AO,
                               IsVolatile);
}

void AtomicInfo::emitCopyIntoMemory(RValue rvalue) const {
  assert(LVal.isSimple());
  // If we have an r-value, the rvalue should be of the atomic type,
  // which means that the caller is responsible for having zeroed
  // any padding.  Just do an aggregate copy of that type.
  if (rvalue.isAggregate()) {
    LValue Dest = FE.makeAddrLValue(getAtomicAddress(), getAtomicType());
    LValue Src =
        FE.makeAddrLValue(rvalue.getAggregateAddress(), getAtomicType());
    bool IsVolatile =
        rvalue.isVolatileQualified() || LVal.isVolatileQualified();
    FE.genAggregateCopy(Dest, Src, getAtomicType(),
                        AggValueSlot::DoesNotOverlap, IsVolatile);
    return;
  }

  // Okay, otherwise we're copying stuff.

  // Zero out the buffer if necessary.
  emitMemSetZeroIfNecessary();

  // Drill past the padding if present.
  LValue TempLVal = projectValue();

  // Okay, store the rvalue in.
  if (rvalue.isScalar()) {
    FE.genStoreOfScalar(rvalue.getScalarVal(), TempLVal, /*init*/ true);
  } else {
    FE.genStoreOfComplex(rvalue.getComplexVal(), TempLVal, /*init*/ true);
  }
}

Address AtomicInfo::materializeRValue(RValue rvalue) const {
  // Aggregate r-values are already in memory, and genAtomicStore
  // requires them to be values of the atomic type.
  if (rvalue.isAggregate())
    return rvalue.getAggregateAddress();

  // Otherwise, make a temporary and materialize into it.
  LValue TempLV = FE.makeAddrLValue(createTempAlloca(), getAtomicType());
  AtomicInfo Atomics(FE, TempLV);
  Atomics.emitCopyIntoMemory(rvalue);
  return TempLV.getAddress(FE);
}

llvm::Value *AtomicInfo::convertRValueToInt(RValue RVal) const {
  // If we've got a scalar value of the right size, try to avoid going
  // through memory.
  if (RVal.isScalar() && (!hasPadding() || !LVal.isSimple())) {
    llvm::Value *Value = RVal.getScalarVal();
    if (isa<llvm::IntegerType>(Value->getType()))
      return FE.genToMemory(Value, ValueTy);
    else {
      llvm::IntegerType *InputIntTy = llvm::IntegerType::get(
          FE.getLLVMContext(),
          LVal.isSimple() ? getValueSizeInBits() : getAtomicSizeInBits());
      if (isa<llvm::PointerType>(Value->getType()))
        return FE.Builder.CreatePtrToInt(Value, InputIntTy);
      else if (llvm::BitCastInst::isBitCastable(Value->getType(), InputIntTy))
        return FE.Builder.CreateBitCast(Value, InputIntTy);
    }
  }
  // Otherwise, we need to go through memory.
  // Put the r-value in memory.
  Address Addr = materializeRValue(RVal);

  Addr = castToAtomicIntPointer(Addr);
  return FE.Builder.CreateLoad(Addr);
}

std::pair<llvm::Value *, llvm::Value *> AtomicInfo::genAtomicCompareExchangeOp(
    llvm::Value *ExpectedVal, llvm::Value *DesiredVal,
    llvm::AtomicOrdering Success, llvm::AtomicOrdering Failure, bool IsWeak) {
  // Do the atomic store.
  Address Addr = getAtomicAddressAsAtomicIntPointer();
  auto *Inst = FE.Builder.CreateAtomicCmpXchg(Addr, ExpectedVal, DesiredVal,
                                              Success, Failure);
  // Other decoration.
  Inst->setVolatile(LVal.isVolatileQualified());
  Inst->setWeak(IsWeak);

  // Okay, turn that back into the original value type.
  auto *PreviousVal = FE.Builder.CreateExtractValue(Inst, /*Idxs=*/0);
  auto *SuccessFailureVal = FE.Builder.CreateExtractValue(Inst, /*Idxs=*/1);
  return std::make_pair(PreviousVal, SuccessFailureVal);
}

llvm::Value *AtomicInfo::genAtomicCompareExchangeLibcall(
    llvm::Value *ExpectedAddr, llvm::Value *DesiredAddr,
    llvm::AtomicOrdering Success, llvm::AtomicOrdering Failure) {
  // bool __atomic_compare_exchange(size_t size, void *obj, void *expected,
  // void *desired, int success, int failure);
  CallArgList Args;
  Args.add(RValue::get(getAtomicSizeValue()), FE.getContext().getSizeType());
  Args.add(RValue::get(getAtomicPointer()), FE.getContext().VoidPtrTy);
  Args.add(RValue::get(ExpectedAddr), FE.getContext().VoidPtrTy);
  Args.add(RValue::get(DesiredAddr), FE.getContext().VoidPtrTy);
  Args.add(
      RValue::get(llvm::ConstantInt::get(FE.IntTy, (int)llvm::toCABI(Success))),
      FE.getContext().IntTy);
  Args.add(
      RValue::get(llvm::ConstantInt::get(FE.IntTy, (int)llvm::toCABI(Failure))),
      FE.getContext().IntTy);
  auto SuccessFailureRVal = emitAtomicLibcall(FE, "__atomic_compare_exchange",
                                              FE.getContext().BoolTy, Args);

  return SuccessFailureRVal.getScalarVal();
}

std::pair<RValue, llvm::Value *> AtomicInfo::genAtomicCompareExchange(
    RValue Expected, RValue Desired, llvm::AtomicOrdering Success,
    llvm::AtomicOrdering Failure, bool IsWeak) {
  if (shouldUseLibcall()) {
    // Produce a source address.
    Address ExpectedAddr = materializeRValue(Expected);
    Address DesiredAddr = materializeRValue(Desired);
    auto *Res = genAtomicCompareExchangeLibcall(
        ExpectedAddr.getPointer(), DesiredAddr.getPointer(), Success, Failure);
    return std::make_pair(
        convertAtomicTempToRValue(ExpectedAddr, AggValueSlot::ignored(),
                                  SourceLocation(), /*AsValue=*/false),
        Res);
  }

  // If we've got a scalar value of the right size, try to avoid going
  // through memory.
  auto *ExpectedVal = convertRValueToInt(Expected);
  auto *DesiredVal = convertRValueToInt(Desired);
  auto Res = genAtomicCompareExchangeOp(ExpectedVal, DesiredVal, Success,
                                        Failure, IsWeak);
  return std::make_pair(
      ConvertIntToValueOrAtomic(Res.first, AggValueSlot::ignored(),
                                SourceLocation(), /*AsValue=*/false),
      Res.second);
}

namespace {
void genAtomicUpdateValue(FunctionEmitter &FE, AtomicInfo &Atomics,
                          RValue OldRVal,
                          const llvm::function_ref<RValue(RValue)> &UpdateOp,
                          Address DesiredAddr) {
  RValue UpRVal;
  LValue AtomicLVal = Atomics.getAtomicLValue();
  LValue DesiredLVal;
  if (AtomicLVal.isSimple()) {
    UpRVal = OldRVal;
    DesiredLVal = FE.makeAddrLValue(DesiredAddr, AtomicLVal.getType());
  } else {
    Address Ptr = Atomics.materializeRValue(OldRVal);
    LValue UpdateLVal;
    if (AtomicLVal.isBitField()) {
      UpdateLVal = LValue::MakeBitfield(
          Ptr, AtomicLVal.getBitFieldInfo(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
      DesiredLVal = LValue::MakeBitfield(
          DesiredAddr, AtomicLVal.getBitFieldInfo(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
    } else if (AtomicLVal.isVectorElt()) {
      UpdateLVal = LValue::MakeVectorElt(
          Ptr, AtomicLVal.getVectorIdx(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
      DesiredLVal = LValue::MakeVectorElt(
          DesiredAddr, AtomicLVal.getVectorIdx(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
    } else {
      assert(AtomicLVal.isExtVectorElt());
      UpdateLVal = LValue::MakeExtVectorElt(
          Ptr, AtomicLVal.getExtVectorElts(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
      DesiredLVal = LValue::MakeExtVectorElt(
          DesiredAddr, AtomicLVal.getExtVectorElts(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
    }
    UpRVal = FE.genLoadOfLValue(UpdateLVal, SourceLocation());
  }
  RValue NewRVal = UpdateOp(UpRVal);
  if (NewRVal.isScalar()) {
    FE.genStoreThroughLValue(NewRVal, DesiredLVal);
  } else {
    assert(NewRVal.isComplex());
    FE.genStoreOfComplex(NewRVal.getComplexVal(), DesiredLVal,
                         /*isInit=*/false);
  }
}

void AtomicInfo::genAtomicUpdateLibcall(
    llvm::AtomicOrdering AO, const llvm::function_ref<RValue(RValue)> &UpdateOp,
    bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  Address ExpectedAddr = createTempAlloca();

  genAtomicLoadLibcall(ExpectedAddr.getPointer(), AO, IsVolatile);
  auto *ContBB = FE.createBasicBlock("atomic_cont");
  auto *ExitBB = FE.createBasicBlock("atomic_exit");
  FE.genBlock(ContBB);
  Address DesiredAddr = createTempAlloca();
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    auto *OldVal = FE.Builder.CreateLoad(ExpectedAddr);
    FE.Builder.CreateStore(OldVal, DesiredAddr);
  }
  auto OldRVal =
      convertAtomicTempToRValue(ExpectedAddr, AggValueSlot::ignored(),
                                SourceLocation(), /*AsValue=*/false);
  genAtomicUpdateValue(FE, *this, OldRVal, UpdateOp, DesiredAddr);
  auto *Res = genAtomicCompareExchangeLibcall(
      ExpectedAddr.getPointer(), DesiredAddr.getPointer(), AO, Failure);
  FE.Builder.CreateCondBr(Res, ExitBB, ContBB);
  FE.genBlock(ExitBB, /*IsFinished=*/true);
}

void AtomicInfo::genAtomicUpdateOp(
    llvm::AtomicOrdering AO, const llvm::function_ref<RValue(RValue)> &UpdateOp,
    bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  // Do the atomic load.
  auto *OldVal = genAtomicLoadOp(Failure, IsVolatile);
  // For non-simple lvalues perform compare-and-swap procedure.
  auto *ContBB = FE.createBasicBlock("atomic_cont");
  auto *ExitBB = FE.createBasicBlock("atomic_exit");
  auto *CurBB = FE.Builder.GetInsertBlock();
  FE.genBlock(ContBB);
  llvm::PHINode *PHI = FE.Builder.CreatePHI(OldVal->getType(),
                                            /*NumReservedValues=*/2);
  PHI->addIncoming(OldVal, CurBB);
  Address NewAtomicAddr = createTempAlloca();
  Address NewAtomicIntAddr = castToAtomicIntPointer(NewAtomicAddr);
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    FE.Builder.CreateStore(PHI, NewAtomicIntAddr);
  }
  auto OldRVal = ConvertIntToValueOrAtomic(PHI, AggValueSlot::ignored(),
                                           SourceLocation(), /*AsValue=*/false);
  genAtomicUpdateValue(FE, *this, OldRVal, UpdateOp, NewAtomicAddr);
  auto *DesiredVal = FE.Builder.CreateLoad(NewAtomicIntAddr);
  // Try to write new value using cmpxchg operation.
  auto Res = genAtomicCompareExchangeOp(PHI, DesiredVal, AO, Failure);
  PHI->addIncoming(Res.first, FE.Builder.GetInsertBlock());
  FE.Builder.CreateCondBr(Res.second, ExitBB, ContBB);
  FE.genBlock(ExitBB, /*IsFinished=*/true);
}

void genAtomicUpdateValue(FunctionEmitter &FE, AtomicInfo &Atomics,
                          RValue UpdateRVal, Address DesiredAddr) {
  LValue AtomicLVal = Atomics.getAtomicLValue();
  LValue DesiredLVal;
  if (AtomicLVal.isBitField()) {
    DesiredLVal = LValue::MakeBitfield(
        DesiredAddr, AtomicLVal.getBitFieldInfo(), AtomicLVal.getType(),
        AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
  } else if (AtomicLVal.isVectorElt()) {
    DesiredLVal = LValue::MakeVectorElt(
        DesiredAddr, AtomicLVal.getVectorIdx(), AtomicLVal.getType(),
        AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
  } else {
    assert(AtomicLVal.isExtVectorElt());
    DesiredLVal = LValue::MakeExtVectorElt(
        DesiredAddr, AtomicLVal.getExtVectorElts(), AtomicLVal.getType(),
        AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
  }
  assert(UpdateRVal.isScalar());
  FE.genStoreThroughLValue(UpdateRVal, DesiredLVal);
}
} // namespace

void AtomicInfo::genAtomicUpdateLibcall(llvm::AtomicOrdering AO,
                                        RValue UpdateRVal, bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  Address ExpectedAddr = createTempAlloca();

  genAtomicLoadLibcall(ExpectedAddr.getPointer(), AO, IsVolatile);
  auto *ContBB = FE.createBasicBlock("atomic_cont");
  auto *ExitBB = FE.createBasicBlock("atomic_exit");
  FE.genBlock(ContBB);
  Address DesiredAddr = createTempAlloca();
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    auto *OldVal = FE.Builder.CreateLoad(ExpectedAddr);
    FE.Builder.CreateStore(OldVal, DesiredAddr);
  }
  genAtomicUpdateValue(FE, *this, UpdateRVal, DesiredAddr);
  auto *Res = genAtomicCompareExchangeLibcall(
      ExpectedAddr.getPointer(), DesiredAddr.getPointer(), AO, Failure);
  FE.Builder.CreateCondBr(Res, ExitBB, ContBB);
  FE.genBlock(ExitBB, /*IsFinished=*/true);
}

void AtomicInfo::genAtomicUpdateOp(llvm::AtomicOrdering AO, RValue UpdateRVal,
                                   bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  // Do the atomic load.
  auto *OldVal = genAtomicLoadOp(Failure, IsVolatile);
  // For non-simple lvalues perform compare-and-swap procedure.
  auto *ContBB = FE.createBasicBlock("atomic_cont");
  auto *ExitBB = FE.createBasicBlock("atomic_exit");
  auto *CurBB = FE.Builder.GetInsertBlock();
  FE.genBlock(ContBB);
  llvm::PHINode *PHI = FE.Builder.CreatePHI(OldVal->getType(),
                                            /*NumReservedValues=*/2);
  PHI->addIncoming(OldVal, CurBB);
  Address NewAtomicAddr = createTempAlloca();
  Address NewAtomicIntAddr = castToAtomicIntPointer(NewAtomicAddr);
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    FE.Builder.CreateStore(PHI, NewAtomicIntAddr);
  }
  genAtomicUpdateValue(FE, *this, UpdateRVal, NewAtomicAddr);
  auto *DesiredVal = FE.Builder.CreateLoad(NewAtomicIntAddr);
  // Try to write new value using cmpxchg operation.
  auto Res = genAtomicCompareExchangeOp(PHI, DesiredVal, AO, Failure);
  PHI->addIncoming(Res.first, FE.Builder.GetInsertBlock());
  FE.Builder.CreateCondBr(Res.second, ExitBB, ContBB);
  FE.genBlock(ExitBB, /*IsFinished=*/true);
}

void AtomicInfo::genAtomicUpdate(
    llvm::AtomicOrdering AO, const llvm::function_ref<RValue(RValue)> &UpdateOp,
    bool IsVolatile) {
  if (shouldUseLibcall()) {
    genAtomicUpdateLibcall(AO, UpdateOp, IsVolatile);
  } else {
    genAtomicUpdateOp(AO, UpdateOp, IsVolatile);
  }
}

void AtomicInfo::genAtomicUpdate(llvm::AtomicOrdering AO, RValue UpdateRVal,
                                 bool IsVolatile) {
  if (shouldUseLibcall()) {
    genAtomicUpdateLibcall(AO, UpdateRVal, IsVolatile);
  } else {
    genAtomicUpdateOp(AO, UpdateRVal, IsVolatile);
  }
}

void FunctionEmitter::genAtomicStore(RValue rvalue, LValue lvalue,
                                     bool isInit) {
  bool IsVolatile = lvalue.isVolatileQualified();
  llvm::AtomicOrdering AO;
  if (lvalue.getType()->isAtomicType()) {
    AO = llvm::AtomicOrdering::SequentiallyConsistent;
  } else {
    AO = llvm::AtomicOrdering::Release;
    IsVolatile = true;
  }
  return genAtomicStore(rvalue, lvalue, AO, IsVolatile, isInit);
}

void FunctionEmitter::genAtomicStore(RValue rvalue, LValue dest,
                                     llvm::AtomicOrdering AO, bool IsVolatile,
                                     bool isInit) {
  // If this is an aggregate r-value, it should agree in type except
  // maybe for address-space qualification.
  assert(!rvalue.isAggregate() ||
         rvalue.getAggregateAddress().getElementType() ==
             dest.getAddress(*this).getElementType());

  AtomicInfo atomics(*this, dest);
  LValue LVal = atomics.getAtomicLValue();

  // If this is an initialization, just put the value there normally.
  if (LVal.isSimple()) {
    if (isInit) {
      atomics.emitCopyIntoMemory(rvalue);
      return;
    }

    if (atomics.shouldUseLibcall()) {
      // Produce a source address.
      Address srcAddr = atomics.materializeRValue(rvalue);

      // void __atomic_store(size_t size, void *mem, void *val, int order)
      CallArgList args;
      args.add(RValue::get(atomics.getAtomicSizeValue()),
               getContext().getSizeType());
      args.add(RValue::get(atomics.getAtomicPointer()), getContext().VoidPtrTy);
      args.add(RValue::get(srcAddr.getPointer()), getContext().VoidPtrTy);
      args.add(
          RValue::get(llvm::ConstantInt::get(IntTy, (int)llvm::toCABI(AO))),
          getContext().IntTy);
      emitAtomicLibcall(*this, "__atomic_store", getContext().VoidTy, args);
      return;
    }

    // Okay, we're doing this natively.
    llvm::Value *intValue = atomics.convertRValueToInt(rvalue);

    // Do the atomic store.
    Address addr = atomics.castToAtomicIntPointer(atomics.getAtomicAddress());
    intValue = Builder.CreateIntCast(intValue, addr.getElementType(),
                                     /*isSigned=*/false);
    llvm::StoreInst *store = Builder.CreateStore(intValue, addr);

    if (AO == llvm::AtomicOrdering::Acquire)
      AO = llvm::AtomicOrdering::Monotonic;
    else if (AO == llvm::AtomicOrdering::AcquireRelease)
      AO = llvm::AtomicOrdering::Release;
    // Initializations don't need to be atomic.
    if (!isInit)
      store->setAtomic(AO);

    // Other decoration.
    if (IsVolatile)
      store->setVolatile(true);
    ME.decorateInstructionWithTBAA(store, dest.getTBAAInfo());
    return;
  }

  atomics.genAtomicUpdate(AO, rvalue, IsVolatile);
}

std::pair<RValue, llvm::Value *> FunctionEmitter::genAtomicCompareExchange(
    LValue Obj, RValue Expected, RValue Desired, SourceLocation Loc,
    llvm::AtomicOrdering Success, llvm::AtomicOrdering Failure, bool IsWeak,
    AggValueSlot Slot) {
  // If this is an aggregate r-value, it should agree in type except
  // maybe for address-space qualification.
  assert(!Expected.isAggregate() ||
         Expected.getAggregateAddress().getElementType() ==
             Obj.getAddress(*this).getElementType());
  assert(!Desired.isAggregate() ||
         Desired.getAggregateAddress().getElementType() ==
             Obj.getAddress(*this).getElementType());
  AtomicInfo Atomics(*this, Obj);

  return Atomics.genAtomicCompareExchange(Expected, Desired, Success, Failure,
                                          IsWeak);
}

void FunctionEmitter::genAtomicUpdate(
    LValue LVal, llvm::AtomicOrdering AO,
    const llvm::function_ref<RValue(RValue)> &UpdateOp, bool IsVolatile) {
  AtomicInfo Atomics(*this, LVal);
  Atomics.genAtomicUpdate(AO, UpdateOp, IsVolatile);
}

void FunctionEmitter::genAtomicInit(Expr *init, LValue dest) {
  AtomicInfo atomics(*this, dest);

  switch (atomics.getEvaluationKind()) {
  case TEK_Scalar: {
    llvm::Value *value = genScalarExpr(init);
    atomics.emitCopyIntoMemory(RValue::get(value));
    return;
  }

  case TEK_Complex: {
    ComplexPairTy value = genComplexExpr(init);
    atomics.emitCopyIntoMemory(RValue::getComplex(value));
    return;
  }

  case TEK_Aggregate: {
    // Fix up the destination if the initializer isn't an expression
    // of atomic type.
    bool Zeroed = false;
    if (!init->getType()->isAtomicType()) {
      Zeroed = atomics.emitMemSetZeroIfNecessary();
      dest = atomics.projectValue();
    }

    // Evaluate the expression directly into the destination.
    AggValueSlot slot = AggValueSlot::forLValue(
        dest, *this, AggValueSlot::IsNotDestructed, AggValueSlot::IsNotAliased,
        AggValueSlot::DoesNotOverlap,
        Zeroed ? AggValueSlot::IsZeroed : AggValueSlot::IsNotZeroed);

    genAggExpr(init, slot);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}
