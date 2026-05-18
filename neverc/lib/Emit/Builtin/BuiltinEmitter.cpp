#include "ABI/ABIInfo.h"
#include "ABI/TargetInfo.h"
#include "Builtin/BuiltinEmitterUtils.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "Decl/PatternInit.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Format/OSLog.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>

using namespace neverc;
using namespace Emit;
using namespace llvm;

// ===----------------------------------------------------------------------===
// Builtin emission
// ===----------------------------------------------------------------------===

namespace {
void initializeAlloca(FunctionEmitter &FE, AllocaInst *AI, Value *Size,
                      Align AlignmentInBytes) {
  ConstantInt *Byte;
  switch (FE.getLangOpts().getTrivialAutoVarInit()) {
  case LangOptions::TrivialAutoVarInitKind::Uninitialized:
    // Nothing to initialize.
    return;
  case LangOptions::TrivialAutoVarInitKind::Zero:
    Byte = FE.Builder.getInt8(0x00);
    break;
  case LangOptions::TrivialAutoVarInitKind::Pattern: {
    llvm::Type *Int8 = llvm::IntegerType::getInt8Ty(FE.ME.getLLVMContext());
    Byte = llvm::dyn_cast<llvm::ConstantInt>(
        initializationPatternFor(FE.ME, Int8));
    break;
  }
  }
  if (FE.ME.stopAutoInit())
    return;
  auto *I = FE.Builder.CreateMemSet(AI, Byte, Size, AlignmentInBytes);
  I->addAnnotationMetadata("auto-init");
}
} // namespace

llvm::Constant *ModuleEmitter::getBuiltinLibFunction(const FunctionDecl *FD,
                                                     unsigned BuiltinID) {
  assert(Context.BuiltinInfo.isLibFunction(BuiltinID));

  llvm::StringRef Name;
  GlobalDecl D(FD);

  if (FD->hasAttr<AsmLabelAttr>())
    Name = getMangledName(D);
  else
    Name = Context.BuiltinInfo.getName(BuiltinID).drop_front(10);

  llvm::FunctionType *Ty =
      cast<llvm::FunctionType>(getTypes().convertType(FD->getType()));

  return obtainLLVMFunction(Name, Ty, D);
}

namespace {
Value *genToInt(FunctionEmitter &FE, llvm::Value *V, QualType T,
                llvm::IntegerType *IntType) {
  V = FE.genToMemory(V, T);

  if (V->getType()->isPointerTy())
    return FE.Builder.CreatePtrToInt(V, IntType);

  assert(V->getType() == IntType);
  return V;
}

Value *genFromInt(FunctionEmitter &FE, llvm::Value *V, QualType T,
                  llvm::Type *ResultType) {
  V = FE.genFromMemory(V, T);

  if (ResultType->isPointerTy())
    return FE.Builder.CreateIntToPtr(V, ResultType);

  assert(V->getType() == ResultType);
  return V;
}
} // namespace

Address CheckAtomicAlignment(FunctionEmitter &FE, const CallExpr *E) {
  TreeContext &Ctx = FE.getContext();
  Address Ptr = FE.genPointerWithAlignment(E->getArg(0));
  unsigned Bytes = Ptr.getElementType()->isPointerTy()
                       ? Ctx.getTypeSizeInChars(Ctx.VoidPtrTy).getQuantity()
                       : Ptr.getElementType()->getScalarSizeInBits() / 8;
  unsigned Align = Ptr.getAlignment().getQuantity();
  if (Align % Bytes != 0) {
    DiagnosticsEngine &Diags = FE.ME.getDiags();
    Diags.Report(E->getBeginLoc(), diag::warn_sync_op_misaligned);
    // Force address to be at least naturally-aligned.
    return Ptr.withAlignment(CharUnits::fromQuantity(Bytes));
  }
  return Ptr;
}

namespace {
Value *MakeBinaryAtomicValue(
    FunctionEmitter &FE, llvm::AtomicRMWInst::BinOp Kind, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {

  QualType T = E->getType();
  assert(E->getArg(0)->getType()->isPointerType());
  assert(FE.getContext().hasSameUnqualifiedType(
      T, E->getArg(0)->getType()->getPointeeType()));
  assert(FE.getContext().hasSameUnqualifiedType(T, E->getArg(1)->getType()));

  Address DestAddr = CheckAtomicAlignment(FE, E);

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      FE.getLLVMContext(), FE.getContext().getTypeSize(T));

  llvm::Value *Val = FE.genScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Val->getType();
  Val = genToInt(FE, Val, T, IntType);

  llvm::Value *Result =
      FE.Builder.CreateAtomicRMW(Kind, DestAddr, Val, Ordering);
  return genFromInt(FE, Result, T, ValueType);
}

Value *genNontemporalStore(FunctionEmitter &FE, const CallExpr *E) {
  Value *Val = FE.genScalarExpr(E->getArg(0));
  Address Addr = FE.genPointerWithAlignment(E->getArg(1));

  Val = FE.genToMemory(Val, E->getArg(0)->getType());
  LValue LV = FE.makeAddrLValue(Addr, E->getArg(0)->getType());
  LV.setNontemporal(true);
  FE.genStoreOfScalar(Val, LV, false);
  return nullptr;
}

Value *genNontemporalLoad(FunctionEmitter &FE, const CallExpr *E) {
  Address Addr = FE.genPointerWithAlignment(E->getArg(0));

  LValue LV = FE.makeAddrLValue(Addr, E->getType());
  LV.setNontemporal(true);
  return FE.genLoadOfScalar(LV, E->getExprLoc());
}

RValue genBinaryAtomic(FunctionEmitter &FE, llvm::AtomicRMWInst::BinOp Kind,
                       const CallExpr *E) {
  return RValue::get(MakeBinaryAtomicValue(FE, Kind, E));
}

RValue genBinaryAtomicPost(FunctionEmitter &FE, llvm::AtomicRMWInst::BinOp Kind,
                           const CallExpr *E, Instruction::BinaryOps Op,
                           bool Invert = false) {
  QualType T = E->getType();
  assert(E->getArg(0)->getType()->isPointerType());
  assert(FE.getContext().hasSameUnqualifiedType(
      T, E->getArg(0)->getType()->getPointeeType()));
  assert(FE.getContext().hasSameUnqualifiedType(T, E->getArg(1)->getType()));

  Address DestAddr = CheckAtomicAlignment(FE, E);

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      FE.getLLVMContext(), FE.getContext().getTypeSize(T));

  llvm::Value *Val = FE.genScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Val->getType();
  Val = genToInt(FE, Val, T, IntType);

  llvm::Value *Result = FE.Builder.CreateAtomicRMW(
      Kind, DestAddr, Val, llvm::AtomicOrdering::SequentiallyConsistent);
  Result = FE.Builder.CreateBinOp(Op, Result, Val);
  if (Invert)
    Result =
        FE.Builder.CreateBinOp(llvm::Instruction::Xor, Result,
                               llvm::ConstantInt::getAllOnesValue(IntType));
  Result = genFromInt(FE, Result, T, ValueType);
  return RValue::get(Result);
}

Value *MakeAtomicCmpXchgValue(FunctionEmitter &FE, const CallExpr *E,
                              bool ReturnBool) {
  QualType T = ReturnBool ? E->getArg(1)->getType() : E->getType();
  Address DestAddr = CheckAtomicAlignment(FE, E);

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      FE.getLLVMContext(), FE.getContext().getTypeSize(T));

  Value *Cmp = FE.genScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Cmp->getType();
  Cmp = genToInt(FE, Cmp, T, IntType);
  Value *New = genToInt(FE, FE.genScalarExpr(E->getArg(2)), T, IntType);

  Value *Pair = FE.Builder.CreateAtomicCmpXchg(
      DestAddr, Cmp, New, llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering::SequentiallyConsistent);
  if (ReturnBool)
    // Extract boolean success flag and zext it to int.
    return FE.Builder.CreateZExt(FE.Builder.CreateExtractValue(Pair, 1),
                                 FE.convertType(E->getType()));
  else
    // Extract old value and emit it using the same type as compare value.
    return genFromInt(FE, FE.Builder.CreateExtractValue(Pair, 0), T, ValueType);
}

Value *genAtomicCmpXchgForMSIntrin(
    FunctionEmitter &FE, const CallExpr *E,
    AtomicOrdering SuccessOrdering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());
  assert(FE.getContext().hasSameUnqualifiedType(
      E->getType(), E->getArg(0)->getType()->getPointeeType()));
  assert(FE.getContext().hasSameUnqualifiedType(E->getType(),
                                                E->getArg(1)->getType()));
  assert(FE.getContext().hasSameUnqualifiedType(E->getType(),
                                                E->getArg(2)->getType()));

  Address DestAddr = CheckAtomicAlignment(FE, E);

  auto *Comparand = FE.genScalarExpr(E->getArg(2));
  auto *Exchange = FE.genScalarExpr(E->getArg(1));

  // For Release ordering, the failure ordering should be Monotonic.
  auto FailureOrdering = SuccessOrdering == AtomicOrdering::Release
                             ? AtomicOrdering::Monotonic
                             : SuccessOrdering;

  // The atomic instruction is marked volatile for consistency with MSVC. This
  // blocks the few atomics optimizations that LLVM has. If we want to optimize
  // _Interlocked* operations in the future, we will have to remove the volatile
  // marker.
  auto *Result = FE.Builder.CreateAtomicCmpXchg(
      DestAddr, Comparand, Exchange, SuccessOrdering, FailureOrdering);
  Result->setVolatile(true);
  return FE.Builder.CreateExtractValue(Result, 0);
}
} // namespace

// 64-bit Microsoft platforms support 128 bit cmpxchg operations. They are
// prototyped like this:
//
// unsigned char _InterlockedCompareExchange128...(
//     __int64 volatile * _Destination,
//     __int64 _ExchangeHigh,
//     __int64 _ExchangeLow,
//     __int64 * _ComparandResult);
//
// Note that Destination is assumed to be at least 16-byte aligned, despite
// being typed int64.

namespace {
Value *genAtomicCmpXchg128ForMSIntrin(FunctionEmitter &FE, const CallExpr *E,
                                      AtomicOrdering SuccessOrdering) {
  assert(E->getNumArgs() == 4);
  llvm::Value *DestPtr = FE.genScalarExpr(E->getArg(0));
  llvm::Value *ExchangeHigh = FE.genScalarExpr(E->getArg(1));
  llvm::Value *ExchangeLow = FE.genScalarExpr(E->getArg(2));
  Address ComparandAddr = FE.genPointerWithAlignment(E->getArg(3));

  assert(DestPtr->getType()->isPointerTy());
  assert(!ExchangeHigh->getType()->isPointerTy());
  assert(!ExchangeLow->getType()->isPointerTy());

  // For Release ordering, the failure ordering should be Monotonic.
  auto FailureOrdering = SuccessOrdering == AtomicOrdering::Release
                             ? AtomicOrdering::Monotonic
                             : SuccessOrdering;

  // Convert to i128 pointers and values. Alignment is also overridden for
  // destination pointer.
  llvm::Type *Int128Ty = llvm::IntegerType::get(FE.getLLVMContext(), 128);
  Address DestAddr(DestPtr, Int128Ty, FE.getContext().toCharUnitsFromBits(128));
  ComparandAddr = ComparandAddr.withElementType(Int128Ty);

  // (((i128)hi) << 64) | ((i128)lo)
  ExchangeHigh = FE.Builder.CreateZExt(ExchangeHigh, Int128Ty);
  ExchangeLow = FE.Builder.CreateZExt(ExchangeLow, Int128Ty);
  ExchangeHigh =
      FE.Builder.CreateShl(ExchangeHigh, llvm::ConstantInt::get(Int128Ty, 64));
  llvm::Value *Exchange = FE.Builder.CreateOr(ExchangeHigh, ExchangeLow);

  llvm::Value *Comparand = FE.Builder.CreateLoad(ComparandAddr);

  auto *CXI = FE.Builder.CreateAtomicCmpXchg(DestAddr, Comparand, Exchange,
                                             SuccessOrdering, FailureOrdering);

  // The atomic instruction is marked volatile for consistency with MSVC. This
  // blocks the few atomics optimizations that LLVM has. If we want to optimize
  // _Interlocked* operations in the future, we will have to remove the volatile
  // marker.
  CXI->setVolatile(true);

  FE.Builder.CreateStore(FE.Builder.CreateExtractValue(CXI, 0), ComparandAddr);

  Value *Success = FE.Builder.CreateExtractValue(CXI, 1);
  return FE.Builder.CreateZExt(Success, FE.Int8Ty);
}

Value *genAtomicIncrementValue(
    FunctionEmitter &FE, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());

  auto *IntTy = FE.convertType(E->getType());
  Address DestAddr = CheckAtomicAlignment(FE, E);
  auto *Result = FE.Builder.CreateAtomicRMW(
      AtomicRMWInst::Add, DestAddr, ConstantInt::get(IntTy, 1), Ordering);
  return FE.Builder.CreateAdd(Result, ConstantInt::get(IntTy, 1));
}

Value *genAtomicDecrementValue(
    FunctionEmitter &FE, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());

  auto *IntTy = FE.convertType(E->getType());
  Address DestAddr = CheckAtomicAlignment(FE, E);
  auto *Result = FE.Builder.CreateAtomicRMW(
      AtomicRMWInst::Sub, DestAddr, ConstantInt::get(IntTy, 1), Ordering);
  return FE.Builder.CreateSub(Result, ConstantInt::get(IntTy, 1));
}

Value *genISOVolatileLoad(FunctionEmitter &FE, const CallExpr *E) {
  Value *Ptr = FE.genScalarExpr(E->getArg(0));
  QualType ElTy = E->getArg(0)->getType()->getPointeeType();
  CharUnits LoadSize = FE.getContext().getTypeSizeInChars(ElTy);
  llvm::Type *ITy =
      llvm::IntegerType::get(FE.getLLVMContext(), LoadSize.getQuantity() * 8);
  llvm::LoadInst *Load = FE.Builder.CreateAlignedLoad(ITy, Ptr, LoadSize);
  Load->setVolatile(true);
  return Load;
}

Value *genISOVolatileStore(FunctionEmitter &FE, const CallExpr *E) {
  Value *Ptr = FE.genScalarExpr(E->getArg(0));
  Value *Value = FE.genScalarExpr(E->getArg(1));
  QualType ElTy = E->getArg(0)->getType()->getPointeeType();
  CharUnits StoreSize = FE.getContext().getTypeSizeInChars(ElTy);
  llvm::StoreInst *Store = FE.Builder.CreateAlignedStore(Value, Ptr, StoreSize);
  Store->setVolatile(true);
  return Store;
}

// Emit a simple mangled intrinsic that has 1 argument and a return type
// matching the argument type. Depending on mode, this may be a constrained
// floating-point intrinsic.
Value *emitUnaryMaybeConstrainedFPBuiltin(FunctionEmitter &FE,
                                          const CallExpr *E,
                                          unsigned IntrinsicID,
                                          unsigned ConstrainedIntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));

  FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, E);
  if (FE.Builder.getIsFPConstrained()) {
    Function *F = FE.ME.getIntrinsic(ConstrainedIntrinsicID, Src0->getType());
    return FE.Builder.CreateConstrainedFPCall(F, {Src0});
  } else {
    Function *F = FE.ME.getIntrinsic(IntrinsicID, Src0->getType());
    return FE.Builder.CreateCall(F, Src0);
  }
}

// Emit an intrinsic that has 2 operands of the same type as its result.
// Depending on mode, this may be a constrained floating-point intrinsic.
Value *emitBinaryMaybeConstrainedFPBuiltin(FunctionEmitter &FE,
                                           const CallExpr *E,
                                           unsigned IntrinsicID,
                                           unsigned ConstrainedIntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));
  llvm::Value *Src1 = FE.genScalarExpr(E->getArg(1));

  if (FE.Builder.getIsFPConstrained()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, E);
    Function *F = FE.ME.getIntrinsic(ConstrainedIntrinsicID, Src0->getType());
    return FE.Builder.CreateConstrainedFPCall(F, {Src0, Src1});
  } else {
    Function *F = FE.ME.getIntrinsic(IntrinsicID, Src0->getType());
    return FE.Builder.CreateCall(F, {Src0, Src1});
  }
}

// Has second type mangled argument.
Value *emitBinaryExpMaybeConstrainedFPBuiltin(
    FunctionEmitter &FE, const CallExpr *E, llvm::Intrinsic::ID IntrinsicID,
    llvm::Intrinsic::ID ConstrainedIntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));
  llvm::Value *Src1 = FE.genScalarExpr(E->getArg(1));

  if (FE.Builder.getIsFPConstrained()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, E);
    Function *F = FE.ME.getIntrinsic(ConstrainedIntrinsicID,
                                     {Src0->getType(), Src1->getType()});
    return FE.Builder.CreateConstrainedFPCall(F, {Src0, Src1});
  }

  Function *F =
      FE.ME.getIntrinsic(IntrinsicID, {Src0->getType(), Src1->getType()});
  return FE.Builder.CreateCall(F, {Src0, Src1});
}

// Emit an intrinsic that has 3 operands of the same type as its result.
// Depending on mode, this may be a constrained floating-point intrinsic.
Value *emitTernaryMaybeConstrainedFPBuiltin(FunctionEmitter &FE,
                                            const CallExpr *E,
                                            unsigned IntrinsicID,
                                            unsigned ConstrainedIntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));
  llvm::Value *Src1 = FE.genScalarExpr(E->getArg(1));
  llvm::Value *Src2 = FE.genScalarExpr(E->getArg(2));

  if (FE.Builder.getIsFPConstrained()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, E);
    Function *F = FE.ME.getIntrinsic(ConstrainedIntrinsicID, Src0->getType());
    return FE.Builder.CreateConstrainedFPCall(F, {Src0, Src1, Src2});
  } else {
    Function *F = FE.ME.getIntrinsic(IntrinsicID, Src0->getType());
    return FE.Builder.CreateCall(F, {Src0, Src1, Src2});
  }
}
} // namespace

// Emit an intrinsic where all operands are of the same type as the result.
// Depending on mode, this may be a constrained floating-point intrinsic.
Value *emitCallMaybeConstrainedFPBuiltin(FunctionEmitter &FE,
                                         unsigned IntrinsicID,
                                         unsigned ConstrainedIntrinsicID,
                                         llvm::Type *Ty,
                                         llvm::ArrayRef<Value *> Args) {
  Function *F;
  if (FE.Builder.getIsFPConstrained())
    F = FE.ME.getIntrinsic(ConstrainedIntrinsicID, Ty);
  else
    F = FE.ME.getIntrinsic(IntrinsicID, Ty);

  if (FE.Builder.getIsFPConstrained())
    return FE.Builder.CreateConstrainedFPCall(F, Args);
  else
    return FE.Builder.CreateCall(F, Args);
}

namespace {
Value *emitUnaryBuiltin(FunctionEmitter &FE, const CallExpr *E,
                        unsigned IntrinsicID, llvm::StringRef Name = "") {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));

  Function *F = FE.ME.getIntrinsic(IntrinsicID, Src0->getType());
  return FE.Builder.CreateCall(F, Src0, Name);
}

Value *emitBinaryBuiltin(FunctionEmitter &FE, const CallExpr *E,
                         unsigned IntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));
  llvm::Value *Src1 = FE.genScalarExpr(E->getArg(1));

  Function *F = FE.ME.getIntrinsic(IntrinsicID, Src0->getType());
  return FE.Builder.CreateCall(F, {Src0, Src1});
}

Value *emitTernaryBuiltin(FunctionEmitter &FE, const CallExpr *E,
                          unsigned IntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));
  llvm::Value *Src1 = FE.genScalarExpr(E->getArg(1));
  llvm::Value *Src2 = FE.genScalarExpr(E->getArg(2));

  Function *F = FE.ME.getIntrinsic(IntrinsicID, Src0->getType());
  return FE.Builder.CreateCall(F, {Src0, Src1, Src2});
}

// Emit an intrinsic that has overloaded integer result and fp operand.
Value *
emitMaybeConstrainedFPToIntRoundBuiltin(FunctionEmitter &FE, const CallExpr *E,
                                        unsigned IntrinsicID,
                                        unsigned ConstrainedIntrinsicID) {
  llvm::Type *ResultType = FE.convertType(E->getType());
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));

  if (FE.Builder.getIsFPConstrained()) {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(FE, E);
    Function *F = FE.ME.getIntrinsic(ConstrainedIntrinsicID,
                                     {ResultType, Src0->getType()});
    return FE.Builder.CreateConstrainedFPCall(F, {Src0});
  } else {
    Function *F =
        FE.ME.getIntrinsic(IntrinsicID, {ResultType, Src0->getType()});
    return FE.Builder.CreateCall(F, Src0);
  }
}

Value *emitFrexpBuiltin(FunctionEmitter &FE, const CallExpr *E,
                        llvm::Intrinsic::ID IntrinsicID) {
  llvm::Value *Src0 = FE.genScalarExpr(E->getArg(0));
  llvm::Value *Src1 = FE.genScalarExpr(E->getArg(1));

  QualType IntPtrTy = E->getArg(1)->getType()->getPointeeType();
  llvm::Type *IntTy = FE.convertType(IntPtrTy);
  llvm::Function *F = FE.ME.getIntrinsic(IntrinsicID, {Src0->getType(), IntTy});
  llvm::Value *Call = FE.Builder.CreateCall(F, Src0);

  llvm::Value *Exp = FE.Builder.CreateExtractValue(Call, 1);
  LValue LV = FE.makeNaturalAlignAddrLValue(Src1, IntPtrTy);
  FE.genStoreOfScalar(Exp, LV);

  return FE.Builder.CreateExtractValue(Call, 0);
}

Value *genFAbs(FunctionEmitter &FE, Value *V) {
  Function *F = FE.ME.getIntrinsic(Intrinsic::fabs, V->getType());
  llvm::CallInst *Call = FE.Builder.CreateCall(F, V);
  Call->setDoesNotAccessMemory();
  return Call;
}

Value *genSignBit(FunctionEmitter &FE, Value *V) {
  LLVMContext &C = FE.ME.getLLVMContext();

  llvm::Type *Ty = V->getType();
  int Width = Ty->getPrimitiveSizeInBits();
  llvm::Type *IntTy = llvm::IntegerType::get(C, Width);
  V = FE.Builder.CreateBitCast(V, IntTy);
  Value *Zero = llvm::Constant::getNullValue(IntTy);
  return FE.Builder.CreateICmpSLT(V, Zero);
}

RValue emitLibraryCall(FunctionEmitter &FE, const FunctionDecl *FD,
                       const CallExpr *E, llvm::Constant *calleeValue) {
  FnCallee callee = FnCallee::forDirect(calleeValue, GlobalDecl(FD));
  return FE.genCall(E->getCallee()->getType(), callee, E, ReturnValueSlot());
}

llvm::Value *genOverflowIntrinsic(FunctionEmitter &FE,
                                  const llvm::Intrinsic::ID IntrinsicID,
                                  llvm::Value *X, llvm::Value *Y,
                                  llvm::Value *&Carry) {
  // Make sure we have integers of the same width.
  assert(X->getType() == Y->getType() &&
         "Arguments must be the same type. (Did you forget to make sure both "
         "arguments have the same integer width?)");

  Function *Callee = FE.ME.getIntrinsic(IntrinsicID, X->getType());
  llvm::Value *Tmp = FE.Builder.CreateCall(Callee, {X, Y});
  Carry = FE.Builder.CreateExtractValue(Tmp, 1);
  return FE.Builder.CreateExtractValue(Tmp, 0);
}
} // namespace

namespace {
struct WidthAndSignedness {
  unsigned Width;
  bool Signed;
};
} // namespace

namespace {
WidthAndSignedness
getIntegerWidthAndSignedness(const neverc::TreeContext &context,
                             const neverc::QualType Type) {
  assert(Type->isIntegerType() && "Given type is not an integer.");
  unsigned Width = Type->isBooleanType()  ? 1
                   : Type->isBitIntType() ? context.getIntWidth(Type)
                                          : context.getTypeInfo(Type).Width;
  bool Signed = Type->isSignedIntegerType();
  return {Width, Signed};
}

// Given one or more integer types, this function produces an integer type that
// encompasses them: any value in one of the given types could be expressed in
// the encompassing type.
struct WidthAndSignedness
EncompassingIntegerType(llvm::ArrayRef<struct WidthAndSignedness> Types) {
  assert(Types.size() > 0 && "Empty list of types.");

  // If any of the given types is signed, we must return a signed type.
  bool Signed = false;
  for (const auto &Type : Types) {
    Signed |= Type.Signed;
  }

  // The encompassing type must have a width greater than or equal to the width
  // of the specified types.  Additionally, if the encompassing type is signed,
  // its width must be strictly greater than the width of any unsigned types
  // given.
  unsigned Width = 0;
  for (const auto &Type : Types) {
    unsigned MinWidth = Type.Width + (Signed && !Type.Signed);
    if (Width < MinWidth) {
      Width = MinWidth;
    }
  }

  return {Width, Signed};
}
} // namespace

Value *FunctionEmitter::genVAStartEnd(Value *ArgValue, bool IsStart) {
  Intrinsic::ID inst = IsStart ? Intrinsic::vastart : Intrinsic::vaend;
  return Builder.CreateCall(ME.getIntrinsic(inst), ArgValue);
}

namespace {
bool areBOSTypesCompatible(int From, int To) {
  // Note: Our __builtin_object_size implementation currently treats Type=0 and
  // Type=2 identically. Encoding this implementation detail here may make
  // improving __builtin_object_size difficult in the future, so it's omitted.
  return From == To || (From == 0 && To == 1) || (From == 3 && To == 2);
}

llvm::Value *getDefaultBuiltinObjectSizeResult(unsigned Type,
                                               llvm::IntegerType *ResType) {
  return ConstantInt::get(ResType, (Type & 2) ? 0 : -1, /*isSigned=*/true);
}
} // namespace

llvm::Value *FunctionEmitter::evaluateOrEmitBuiltinObjectSize(
    const Expr *E, unsigned Type, llvm::IntegerType *ResType,
    llvm::Value *EmittedE, bool IsDynamic) {
  uint64_t ObjectSize;
  if (!E->tryEvaluateObjectSize(ObjectSize, getContext(), Type))
    return emitBuiltinObjectSize(E, Type, ResType, EmittedE, IsDynamic);
  return ConstantInt::get(ResType, ObjectSize, /*isSigned=*/true);
}

llvm::Value *FunctionEmitter::emitBuiltinObjectSize(const Expr *E,
                                                    unsigned Type,
                                                    llvm::IntegerType *ResType,
                                                    llvm::Value *EmittedE,
                                                    bool IsDynamic) {
  // We need to reference an argument if the pointer is a parameter with the
  // pass_object_size attribute.
  if (auto *D = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts())) {
    auto *Param = dyn_cast<ParmVarDecl>(D->getDecl());
    auto *PS = D->getDecl()->getAttr<PassObjectSizeAttr>();
    if (Param != nullptr && PS != nullptr &&
        areBOSTypesCompatible(PS->getType(), Type)) {
      auto Iter = SizeArguments.find(Param);
      assert(Iter != SizeArguments.end());

      const ImplicitParamDecl *D = Iter->second;
      auto DIter = LocalDeclMap.find(D);
      assert(DIter != LocalDeclMap.end());

      return genLoadOfScalar(DIter->second, /*Volatile=*/false,
                             getContext().getSizeType(), E->getBeginLoc());
    }
  }

  // LLVM can't handle Type=3 appropriately, and __builtin_object_size shouldn't
  // evaluate E for side-effects. In either case, we shouldn't lower to
  // @llvm.objectsize.
  if (Type == 3 || (!EmittedE && E->HasSideEffects(getContext())))
    return getDefaultBuiltinObjectSizeResult(Type, ResType);

  Value *Ptr = EmittedE ? EmittedE : genScalarExpr(E);
  assert(Ptr->getType()->isPointerTy() &&
         "Non-pointer passed to __builtin_object_size?");

  Function *F =
      ME.getIntrinsic(Intrinsic::objectsize, {ResType, Ptr->getType()});

  // LLVM only supports 0 and 2, make sure that we pass along that as a boolean.
  Value *Min = Builder.getInt1((Type & 2) != 0);
  // For GCC compatibility, __builtin_object_size treat NULL as unknown size.
  Value *NullIsUnknown = Builder.getTrue();
  Value *Dynamic = Builder.getInt1(IsDynamic);
  return Builder.CreateCall(F, {Ptr, Min, NullIsUnknown, Dynamic});
}

namespace {
struct BitTest {
  enum ActionKind : uint8_t { TestOnly, Complement, Reset, Set };
  enum InterlockingKind : uint8_t {
    Unlocked,
    Sequential,
    Acquire,
    Release,
    NoFence
  };

  ActionKind Action;
  InterlockingKind Interlocking;
  bool Is64Bit;

  static BitTest decodeBitTestBuiltin(unsigned BuiltinID);
};
} // namespace

BitTest BitTest::decodeBitTestBuiltin(unsigned BuiltinID) {
  switch (BuiltinID) {
    // Main portable variants.
  case Builtin::BI_bittest:
    return {TestOnly, Unlocked, false};
  case Builtin::BI_bittestandcomplement:
    return {Complement, Unlocked, false};
  case Builtin::BI_bittestandreset:
    return {Reset, Unlocked, false};
  case Builtin::BI_bittestandset:
    return {Set, Unlocked, false};
  case Builtin::BI_interlockedbittestandreset:
    return {Reset, Sequential, false};
  case Builtin::BI_interlockedbittestandset:
    return {Set, Sequential, false};

    // X86-specific 64-bit variants.
  case Builtin::BI_bittest64:
    return {TestOnly, Unlocked, true};
  case Builtin::BI_bittestandcomplement64:
    return {Complement, Unlocked, true};
  case Builtin::BI_bittestandreset64:
    return {Reset, Unlocked, true};
  case Builtin::BI_bittestandset64:
    return {Set, Unlocked, true};
  case Builtin::BI_interlockedbittestandreset64:
    return {Reset, Sequential, true};
  case Builtin::BI_interlockedbittestandset64:
    return {Set, Sequential, true};

    // AArch64-specific ordering variants.
  case Builtin::BI_interlockedbittestandset_acq:
    return {Set, Acquire, false};
  case Builtin::BI_interlockedbittestandset_rel:
    return {Set, Release, false};
  case Builtin::BI_interlockedbittestandset_nf:
    return {Set, NoFence, false};
  case Builtin::BI_interlockedbittestandreset_acq:
    return {Reset, Acquire, false};
  case Builtin::BI_interlockedbittestandreset_rel:
    return {Reset, Release, false};
  case Builtin::BI_interlockedbittestandreset_nf:
    return {Reset, NoFence, false};
  }
  llvm_unreachable("expected only bittest intrinsics");
}

namespace {
char bitActionToX86BTCode(BitTest::ActionKind A) {
  switch (A) {
  case BitTest::TestOnly:
    return '\0';
  case BitTest::Complement:
    return 'c';
  case BitTest::Reset:
    return 'r';
  case BitTest::Set:
    return 's';
  }
  llvm_unreachable("invalid action");
}

llvm::Value *genX86BitTestIntrinsic(FunctionEmitter &FE, BitTest BT,
                                    const CallExpr *E, Value *BitBase,
                                    Value *BitPos) {
  char Action = bitActionToX86BTCode(BT.Action);
  char SizeSuffix = BT.Is64Bit ? 'q' : 'l';

  llvm::SmallString<64> Asm;
  raw_svector_ostream AsmOS(Asm);
  if (BT.Interlocking != BitTest::Unlocked)
    AsmOS << "lock ";
  AsmOS << "bt";
  if (Action)
    AsmOS << Action;
  AsmOS << SizeSuffix << " $2, ($1)";

  std::string Constraints = "={@ccc},r,r,~{cc},~{memory}";
  std::string_view MachineClobbers = FE.getTarget().getClobbers();
  if (!MachineClobbers.empty()) {
    Constraints += ',';
    Constraints += MachineClobbers;
  }
  llvm::IntegerType *IntType = llvm::IntegerType::get(
      FE.getLLVMContext(),
      FE.getContext().getTypeSize(E->getArg(1)->getType()));
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(FE.Int8Ty, {FE.UnqualPtrTy, IntType}, false);

  llvm::InlineAsm *IA =
      llvm::InlineAsm::get(FTy, Asm, Constraints, /*hasSideEffects=*/true);
  return FE.Builder.CreateCall(IA, {BitBase, BitPos});
}

llvm::AtomicOrdering getBitTestAtomicOrdering(BitTest::InterlockingKind I) {
  switch (I) {
  case BitTest::Unlocked:
    return llvm::AtomicOrdering::NotAtomic;
  case BitTest::Sequential:
    return llvm::AtomicOrdering::SequentiallyConsistent;
  case BitTest::Acquire:
    return llvm::AtomicOrdering::Acquire;
  case BitTest::Release:
    return llvm::AtomicOrdering::Release;
  case BitTest::NoFence:
    return llvm::AtomicOrdering::Monotonic;
  }
  llvm_unreachable("invalid interlocking");
}

llvm::Value *genBitTestIntrinsic(FunctionEmitter &FE, unsigned BuiltinID,
                                 const CallExpr *E) {
  Value *BitBase = FE.genScalarExpr(E->getArg(0));
  Value *BitPos = FE.genScalarExpr(E->getArg(1));

  BitTest BT = BitTest::decodeBitTestBuiltin(BuiltinID);

  // X86 has special BT, BTC, BTR, and BTS instructions that handle the array
  // indexing operation internally. Use them if possible.
  if (FE.getTarget().getTriple().isX86())
    return genX86BitTestIntrinsic(FE, BT, E, BitBase, BitPos);

  // Otherwise, use generic code to load one byte and test the bit. Use all but
  // the bottom three bits as the array index, and the bottom three bits to form
  // a mask.
  // Bit = BitBaseI8[BitPos >> 3] & (1 << (BitPos & 0x7)) != 0;
  Value *ByteIndex = FE.Builder.CreateAShr(
      BitPos, llvm::ConstantInt::get(BitPos->getType(), 3), "bittest.byteidx");
  Value *BitBaseI8 = FE.Builder.CreatePointerCast(BitBase, FE.Int8PtrTy);
  Address ByteAddr(FE.Builder.CreateInBoundsGEP(FE.Int8Ty, BitBaseI8, ByteIndex,
                                                "bittest.byteaddr"),
                   FE.Int8Ty, CharUnits::One());
  Value *PosLow =
      FE.Builder.CreateAnd(FE.Builder.CreateTrunc(BitPos, FE.Int8Ty),
                           llvm::ConstantInt::get(FE.Int8Ty, 0x7));

  // The updating instructions will need a mask.
  Value *Mask = nullptr;
  if (BT.Action != BitTest::TestOnly) {
    Mask = FE.Builder.CreateShl(llvm::ConstantInt::get(FE.Int8Ty, 1), PosLow,
                                "bittest.mask");
  }

  llvm::AtomicOrdering Ordering = getBitTestAtomicOrdering(BT.Interlocking);

  Value *OldByte = nullptr;
  if (Ordering != llvm::AtomicOrdering::NotAtomic) {
    llvm::AtomicRMWInst::BinOp RMWOp = llvm::AtomicRMWInst::Or;
    if (BT.Action == BitTest::Reset) {
      Mask = FE.Builder.CreateNot(Mask);
      RMWOp = llvm::AtomicRMWInst::And;
    }
    OldByte = FE.Builder.CreateAtomicRMW(RMWOp, ByteAddr, Mask, Ordering);
  } else {
    OldByte = FE.Builder.CreateLoad(ByteAddr, "bittest.byte");
    Value *NewByte = nullptr;
    switch (BT.Action) {
    case BitTest::TestOnly:
      // Don't store anything.
      break;
    case BitTest::Complement:
      NewByte = FE.Builder.CreateXor(OldByte, Mask);
      break;
    case BitTest::Reset:
      NewByte = FE.Builder.CreateAnd(OldByte, FE.Builder.CreateNot(Mask));
      break;
    case BitTest::Set:
      NewByte = FE.Builder.CreateOr(OldByte, Mask);
      break;
    }
    if (NewByte)
      FE.Builder.CreateStore(NewByte, ByteAddr);
  }

  // However we loaded the old byte, either by plain load or atomicrmw, shift
  // the bit into the low position and mask it to 0 or 1.
  Value *ShiftedByte = FE.Builder.CreateLShr(OldByte, PosLow, "bittest.shr");
  return FE.Builder.CreateAnd(ShiftedByte, llvm::ConstantInt::get(FE.Int8Ty, 1),
                              "bittest.res");
}
} // namespace

namespace {
enum class MSVCSetJmpKind { _setjmpex, _setjmp };
}

namespace {
RValue genMSVCRTSetJmp(FunctionEmitter &FE, MSVCSetJmpKind SJKind,
                       const CallExpr *E) {
  llvm::Value *Arg1 = nullptr;
  llvm::Type *Arg1Ty = FE.Int8PtrTy;
  llvm::StringRef Name =
      SJKind == MSVCSetJmpKind::_setjmp ? "_setjmp" : "_setjmpex";
  bool IsVarArg = false;
  if (FE.getTarget().getTriple().getArch() == llvm::Triple::aarch64) {
    Arg1 = FE.Builder.CreateCall(
        FE.ME.getIntrinsic(Intrinsic::sponentry, FE.AllocaInt8PtrTy));
  } else
    Arg1 = FE.Builder.CreateCall(
        FE.ME.getIntrinsic(Intrinsic::frameaddress, FE.AllocaInt8PtrTy),
        llvm::ConstantInt::get(FE.Int32Ty, 0));

  // Mark the call site and declaration with ReturnsTwice.
  llvm::Type *ArgTypes[2] = {FE.Int8PtrTy, Arg1Ty};
  llvm::AttributeList ReturnsTwiceAttr = llvm::AttributeList::get(
      FE.getLLVMContext(), llvm::AttributeList::FunctionIndex,
      llvm::Attribute::ReturnsTwice);
  llvm::FunctionCallee SetJmpFn = FE.ME.createRuntimeFunction(
      llvm::FunctionType::get(FE.IntTy, ArgTypes, IsVarArg), Name,
      ReturnsTwiceAttr, /*Local=*/true);

  llvm::Value *Buf = FE.Builder.CreateBitOrPointerCast(
      FE.genScalarExpr(E->getArg(0)), FE.Int8PtrTy);
  llvm::Value *Args[] = {Buf, Arg1};
  llvm::CallBase *CB = FE.genRuntimeCallOrInvoke(SetJmpFn, Args);
  CB->setAttributes(ReturnsTwiceAttr);
  return RValue::get(CB);
}
} // namespace

// Many of MSVC builtins are on x64 and AArch64; to avoid repeating code,
// we handle them here.
enum class FunctionEmitter::MSVCIntrin {
  _BitScanForward,
  _BitScanReverse,
  _InterlockedAnd,
  _InterlockedDecrement,
  _InterlockedExchange,
  _InterlockedExchangeAdd,
  _InterlockedExchangeSub,
  _InterlockedIncrement,
  _InterlockedOr,
  _InterlockedXor,
  _InterlockedExchangeAdd_acq,
  _InterlockedExchangeAdd_rel,
  _InterlockedExchangeAdd_nf,
  _InterlockedExchange_acq,
  _InterlockedExchange_rel,
  _InterlockedExchange_nf,
  _InterlockedCompareExchange_acq,
  _InterlockedCompareExchange_rel,
  _InterlockedCompareExchange_nf,
  _InterlockedCompareExchange128,
  _InterlockedCompareExchange128_acq,
  _InterlockedCompareExchange128_rel,
  _InterlockedCompareExchange128_nf,
  _InterlockedOr_acq,
  _InterlockedOr_rel,
  _InterlockedOr_nf,
  _InterlockedXor_acq,
  _InterlockedXor_rel,
  _InterlockedXor_nf,
  _InterlockedAnd_acq,
  _InterlockedAnd_rel,
  _InterlockedAnd_nf,
  _InterlockedIncrement_acq,
  _InterlockedIncrement_rel,
  _InterlockedIncrement_nf,
  _InterlockedDecrement_acq,
  _InterlockedDecrement_rel,
  _InterlockedDecrement_nf,
  __fastfail,
};

std::optional<FunctionEmitter::MSVCIntrin>
translateAarch64ToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = FunctionEmitter::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case neverc::AArch64::BI_BitScanForward:
  case neverc::AArch64::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case neverc::AArch64::BI_BitScanReverse:
  case neverc::AArch64::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case neverc::AArch64::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case neverc::AArch64::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case neverc::AArch64::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case neverc::AArch64::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case neverc::AArch64::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case neverc::AArch64::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case neverc::AArch64::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case neverc::AArch64::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  case neverc::AArch64::BI_InterlockedExchangeAdd8_acq:
  case neverc::AArch64::BI_InterlockedExchangeAdd16_acq:
  case neverc::AArch64::BI_InterlockedExchangeAdd_acq:
  case neverc::AArch64::BI_InterlockedExchangeAdd64_acq:
    return MSVCIntrin::_InterlockedExchangeAdd_acq;
  case neverc::AArch64::BI_InterlockedExchangeAdd8_rel:
  case neverc::AArch64::BI_InterlockedExchangeAdd16_rel:
  case neverc::AArch64::BI_InterlockedExchangeAdd_rel:
  case neverc::AArch64::BI_InterlockedExchangeAdd64_rel:
    return MSVCIntrin::_InterlockedExchangeAdd_rel;
  case neverc::AArch64::BI_InterlockedExchangeAdd8_nf:
  case neverc::AArch64::BI_InterlockedExchangeAdd16_nf:
  case neverc::AArch64::BI_InterlockedExchangeAdd_nf:
  case neverc::AArch64::BI_InterlockedExchangeAdd64_nf:
    return MSVCIntrin::_InterlockedExchangeAdd_nf;
  case neverc::AArch64::BI_InterlockedExchange8_acq:
  case neverc::AArch64::BI_InterlockedExchange16_acq:
  case neverc::AArch64::BI_InterlockedExchange_acq:
  case neverc::AArch64::BI_InterlockedExchange64_acq:
    return MSVCIntrin::_InterlockedExchange_acq;
  case neverc::AArch64::BI_InterlockedExchange8_rel:
  case neverc::AArch64::BI_InterlockedExchange16_rel:
  case neverc::AArch64::BI_InterlockedExchange_rel:
  case neverc::AArch64::BI_InterlockedExchange64_rel:
    return MSVCIntrin::_InterlockedExchange_rel;
  case neverc::AArch64::BI_InterlockedExchange8_nf:
  case neverc::AArch64::BI_InterlockedExchange16_nf:
  case neverc::AArch64::BI_InterlockedExchange_nf:
  case neverc::AArch64::BI_InterlockedExchange64_nf:
    return MSVCIntrin::_InterlockedExchange_nf;
  case neverc::AArch64::BI_InterlockedCompareExchange8_acq:
  case neverc::AArch64::BI_InterlockedCompareExchange16_acq:
  case neverc::AArch64::BI_InterlockedCompareExchange_acq:
  case neverc::AArch64::BI_InterlockedCompareExchange64_acq:
    return MSVCIntrin::_InterlockedCompareExchange_acq;
  case neverc::AArch64::BI_InterlockedCompareExchange8_rel:
  case neverc::AArch64::BI_InterlockedCompareExchange16_rel:
  case neverc::AArch64::BI_InterlockedCompareExchange_rel:
  case neverc::AArch64::BI_InterlockedCompareExchange64_rel:
    return MSVCIntrin::_InterlockedCompareExchange_rel;
  case neverc::AArch64::BI_InterlockedCompareExchange8_nf:
  case neverc::AArch64::BI_InterlockedCompareExchange16_nf:
  case neverc::AArch64::BI_InterlockedCompareExchange_nf:
  case neverc::AArch64::BI_InterlockedCompareExchange64_nf:
    return MSVCIntrin::_InterlockedCompareExchange_nf;
  case neverc::AArch64::BI_InterlockedCompareExchange128:
    return MSVCIntrin::_InterlockedCompareExchange128;
  case neverc::AArch64::BI_InterlockedCompareExchange128_acq:
    return MSVCIntrin::_InterlockedCompareExchange128_acq;
  case neverc::AArch64::BI_InterlockedCompareExchange128_nf:
    return MSVCIntrin::_InterlockedCompareExchange128_nf;
  case neverc::AArch64::BI_InterlockedCompareExchange128_rel:
    return MSVCIntrin::_InterlockedCompareExchange128_rel;
  case neverc::AArch64::BI_InterlockedOr8_acq:
  case neverc::AArch64::BI_InterlockedOr16_acq:
  case neverc::AArch64::BI_InterlockedOr_acq:
  case neverc::AArch64::BI_InterlockedOr64_acq:
    return MSVCIntrin::_InterlockedOr_acq;
  case neverc::AArch64::BI_InterlockedOr8_rel:
  case neverc::AArch64::BI_InterlockedOr16_rel:
  case neverc::AArch64::BI_InterlockedOr_rel:
  case neverc::AArch64::BI_InterlockedOr64_rel:
    return MSVCIntrin::_InterlockedOr_rel;
  case neverc::AArch64::BI_InterlockedOr8_nf:
  case neverc::AArch64::BI_InterlockedOr16_nf:
  case neverc::AArch64::BI_InterlockedOr_nf:
  case neverc::AArch64::BI_InterlockedOr64_nf:
    return MSVCIntrin::_InterlockedOr_nf;
  case neverc::AArch64::BI_InterlockedXor8_acq:
  case neverc::AArch64::BI_InterlockedXor16_acq:
  case neverc::AArch64::BI_InterlockedXor_acq:
  case neverc::AArch64::BI_InterlockedXor64_acq:
    return MSVCIntrin::_InterlockedXor_acq;
  case neverc::AArch64::BI_InterlockedXor8_rel:
  case neverc::AArch64::BI_InterlockedXor16_rel:
  case neverc::AArch64::BI_InterlockedXor_rel:
  case neverc::AArch64::BI_InterlockedXor64_rel:
    return MSVCIntrin::_InterlockedXor_rel;
  case neverc::AArch64::BI_InterlockedXor8_nf:
  case neverc::AArch64::BI_InterlockedXor16_nf:
  case neverc::AArch64::BI_InterlockedXor_nf:
  case neverc::AArch64::BI_InterlockedXor64_nf:
    return MSVCIntrin::_InterlockedXor_nf;
  case neverc::AArch64::BI_InterlockedAnd8_acq:
  case neverc::AArch64::BI_InterlockedAnd16_acq:
  case neverc::AArch64::BI_InterlockedAnd_acq:
  case neverc::AArch64::BI_InterlockedAnd64_acq:
    return MSVCIntrin::_InterlockedAnd_acq;
  case neverc::AArch64::BI_InterlockedAnd8_rel:
  case neverc::AArch64::BI_InterlockedAnd16_rel:
  case neverc::AArch64::BI_InterlockedAnd_rel:
  case neverc::AArch64::BI_InterlockedAnd64_rel:
    return MSVCIntrin::_InterlockedAnd_rel;
  case neverc::AArch64::BI_InterlockedAnd8_nf:
  case neverc::AArch64::BI_InterlockedAnd16_nf:
  case neverc::AArch64::BI_InterlockedAnd_nf:
  case neverc::AArch64::BI_InterlockedAnd64_nf:
    return MSVCIntrin::_InterlockedAnd_nf;
  case neverc::AArch64::BI_InterlockedIncrement16_acq:
  case neverc::AArch64::BI_InterlockedIncrement_acq:
  case neverc::AArch64::BI_InterlockedIncrement64_acq:
    return MSVCIntrin::_InterlockedIncrement_acq;
  case neverc::AArch64::BI_InterlockedIncrement16_rel:
  case neverc::AArch64::BI_InterlockedIncrement_rel:
  case neverc::AArch64::BI_InterlockedIncrement64_rel:
    return MSVCIntrin::_InterlockedIncrement_rel;
  case neverc::AArch64::BI_InterlockedIncrement16_nf:
  case neverc::AArch64::BI_InterlockedIncrement_nf:
  case neverc::AArch64::BI_InterlockedIncrement64_nf:
    return MSVCIntrin::_InterlockedIncrement_nf;
  case neverc::AArch64::BI_InterlockedDecrement16_acq:
  case neverc::AArch64::BI_InterlockedDecrement_acq:
  case neverc::AArch64::BI_InterlockedDecrement64_acq:
    return MSVCIntrin::_InterlockedDecrement_acq;
  case neverc::AArch64::BI_InterlockedDecrement16_rel:
  case neverc::AArch64::BI_InterlockedDecrement_rel:
  case neverc::AArch64::BI_InterlockedDecrement64_rel:
    return MSVCIntrin::_InterlockedDecrement_rel;
  case neverc::AArch64::BI_InterlockedDecrement16_nf:
  case neverc::AArch64::BI_InterlockedDecrement_nf:
  case neverc::AArch64::BI_InterlockedDecrement64_nf:
    return MSVCIntrin::_InterlockedDecrement_nf;
  }
  llvm_unreachable("must return from switch");
}

std::optional<FunctionEmitter::MSVCIntrin>
translateX86ToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = FunctionEmitter::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case neverc::X86::BI_BitScanForward:
  case neverc::X86::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case neverc::X86::BI_BitScanReverse:
  case neverc::X86::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case neverc::X86::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case neverc::X86::BI_InterlockedCompareExchange128:
    return MSVCIntrin::_InterlockedCompareExchange128;
  case neverc::X86::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case neverc::X86::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case neverc::X86::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case neverc::X86::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case neverc::X86::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case neverc::X86::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case neverc::X86::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  }
  llvm_unreachable("must return from switch");
}

// Assumes that arguments have *not* been evaluated.
Value *FunctionEmitter::genMSVCBuiltinExpr(MSVCIntrin BuiltinID,
                                           const CallExpr *E) {
  switch (BuiltinID) {
  case MSVCIntrin::_BitScanForward:
  case MSVCIntrin::_BitScanReverse: {
    Address IndexAddress(genPointerWithAlignment(E->getArg(0)));
    Value *ArgValue = genScalarExpr(E->getArg(1));

    llvm::Type *ArgType = ArgValue->getType();
    llvm::Type *IndexType = IndexAddress.getElementType();
    llvm::Type *ResultType = convertType(E->getType());

    Value *ArgZero = llvm::Constant::getNullValue(ArgType);
    Value *ResZero = llvm::Constant::getNullValue(ResultType);
    Value *ResOne = llvm::ConstantInt::get(ResultType, 1);

    BasicBlock *Begin = Builder.GetInsertBlock();
    BasicBlock *End = createBasicBlock("bitscan_end", this->CurFn);
    Builder.SetInsertPoint(End);
    PHINode *Result = Builder.CreatePHI(ResultType, 2, "bitscan_result");

    Builder.SetInsertPoint(Begin);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, ArgZero);
    BasicBlock *NotZero = createBasicBlock("bitscan_not_zero", this->CurFn);
    Builder.CreateCondBr(IsZero, End, NotZero);
    Result->addIncoming(ResZero, Begin);

    Builder.SetInsertPoint(NotZero);

    if (BuiltinID == MSVCIntrin::_BitScanForward) {
      Function *F = ME.getIntrinsic(Intrinsic::cttz, ArgType);
      Value *ZeroCount = Builder.CreateCall(F, {ArgValue, Builder.getTrue()});
      ZeroCount = Builder.CreateIntCast(ZeroCount, IndexType, false);
      Builder.CreateStore(ZeroCount, IndexAddress, false);
    } else {
      unsigned ArgWidth = cast<llvm::IntegerType>(ArgType)->getBitWidth();
      Value *ArgTypeLastIndex = llvm::ConstantInt::get(IndexType, ArgWidth - 1);

      Function *F = ME.getIntrinsic(Intrinsic::ctlz, ArgType);
      Value *ZeroCount = Builder.CreateCall(F, {ArgValue, Builder.getTrue()});
      ZeroCount = Builder.CreateIntCast(ZeroCount, IndexType, false);
      Value *Index = Builder.CreateNSWSub(ArgTypeLastIndex, ZeroCount);
      Builder.CreateStore(Index, IndexAddress, false);
    }
    Builder.CreateBr(End);
    Result->addIncoming(ResOne, NotZero);

    Builder.SetInsertPoint(End);
    return Result;
  }
  case MSVCIntrin::_InterlockedAnd:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E);
  case MSVCIntrin::_InterlockedExchange:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E);
  case MSVCIntrin::_InterlockedExchangeAdd:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E);
  case MSVCIntrin::_InterlockedExchangeSub:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Sub, E);
  case MSVCIntrin::_InterlockedOr:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E);
  case MSVCIntrin::_InterlockedXor:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E);
  case MSVCIntrin::_InterlockedExchangeAdd_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedExchangeAdd_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedExchangeAdd_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedExchange_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedExchange_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedExchange_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedCompareExchange_acq:
    return genAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange_rel:
    return genAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedCompareExchange_nf:
    return genAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedCompareExchange128:
    return genAtomicCmpXchg128ForMSIntrin(
        *this, E, AtomicOrdering::SequentiallyConsistent);
  case MSVCIntrin::_InterlockedCompareExchange128_acq:
    return genAtomicCmpXchg128ForMSIntrin(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange128_rel:
    return genAtomicCmpXchg128ForMSIntrin(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedCompareExchange128_nf:
    return genAtomicCmpXchg128ForMSIntrin(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedOr_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedOr_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedOr_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedXor_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedXor_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedXor_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedAnd_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedAnd_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedAnd_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedIncrement_acq:
    return genAtomicIncrementValue(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedIncrement_rel:
    return genAtomicIncrementValue(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedIncrement_nf:
    return genAtomicIncrementValue(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedDecrement_acq:
    return genAtomicDecrementValue(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedDecrement_rel:
    return genAtomicDecrementValue(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedDecrement_nf:
    return genAtomicDecrementValue(*this, E, AtomicOrdering::Monotonic);

  case MSVCIntrin::_InterlockedDecrement:
    return genAtomicDecrementValue(*this, E);
  case MSVCIntrin::_InterlockedIncrement:
    return genAtomicIncrementValue(*this, E);

  case MSVCIntrin::__fastfail: {
    // Request immediate process termination from the kernel. The instruction
    // sequences to do this are documented on MSDN:
    // https://msdn.microsoft.com/en-us/library/dn774154.aspx
    llvm::Triple::ArchType ISA = getTarget().getTriple().getArch();
    llvm::StringRef Asm, Constraints;
    switch (ISA) {
    default:
      errorUnsupported(E, "__fastfail call for this architecture");
      break;
    case llvm::Triple::x86_64:
      Asm = "int $$0x29";
      Constraints = "{cx}";
      break;
    case llvm::Triple::aarch64:
      Asm = "brk #0xF003";
      Constraints = "{w0}";
    }
    llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, {Int32Ty}, false);
    llvm::InlineAsm *IA =
        llvm::InlineAsm::get(FTy, Asm, Constraints, /*hasSideEffects=*/true);
    llvm::AttributeList NoReturnAttr = llvm::AttributeList::get(
        getLLVMContext(), llvm::AttributeList::FunctionIndex,
        llvm::Attribute::NoReturn);
    llvm::CallInst *CI = Builder.CreateCall(IA, genScalarExpr(E->getArg(0)));
    CI->setAttributes(NoReturnAttr);
    return CI;
  }
  }
  llvm_unreachable("Incorrect MSVC intrinsic!");
}

Value *FunctionEmitter::genCheckedArgForBuiltin(const Expr *E,
                                                BuiltinCheckKind Kind) {
  assert((Kind == BCK_CLZPassedZero || Kind == BCK_CTZPassedZero) &&
         "Unsupported builtin check kind");
  return genScalarExpr(E);
}

namespace {
Value *genAbs(FunctionEmitter &FE, Value *ArgValue, bool HasNSW) {
  return FE.Builder.CreateBinaryIntrinsic(
      Intrinsic::abs, ArgValue,
      ConstantInt::get(FE.Builder.getInt1Ty(), HasNSW));
}

Value *genOverflowCheckedAbs(FunctionEmitter &FE, const CallExpr *E,
                             bool /*SanitizeOverflow*/) {
  Value *ArgValue = FE.genScalarExpr(E->getArg(0));

  // Try to eliminate overflow check.
  if (const auto *VCI = dyn_cast<llvm::ConstantInt>(ArgValue)) {
    if (!VCI->isMinSignedValue())
      return genAbs(FE, ArgValue, true);
  }

  FunctionEmitter::SanitizerScope SanScope(&FE);

  Constant *Zero = Constant::getNullValue(ArgValue->getType());
  Value *ResultAndOverflow = FE.Builder.CreateBinaryIntrinsic(
      Intrinsic::ssub_with_overflow, Zero, ArgValue);
  Value *Result = FE.Builder.CreateExtractValue(ResultAndOverflow, 0);
  Value *NotOverflow =
      FE.Builder.CreateNot(FE.Builder.CreateExtractValue(ResultAndOverflow, 1));

  FE.genTrapCheck(NotOverflow, SanitizerHandler::SubOverflow);

  Value *CmpResult = FE.Builder.CreateICmpSLT(ArgValue, Zero, "abscond");
  return FE.Builder.CreateSelect(CmpResult, Result, ArgValue, "abs");
}

CanQualType getOSLogArgType(TreeContext &C, int Size) {
  QualType UnsignedTy = C.getIntTypeForBitwidth(Size * 8, /*Signed=*/false);
  return C.getCanonicalType(UnsignedTy);
}
} // namespace

llvm::Function *FunctionEmitter::generateBuiltinOSLogHelperFunction(
    const analyze_os_log::OSLogBufferLayout &Layout,
    CharUnits BufferAlignment) {
  TreeContext &Ctx = getContext();

  llvm::SmallString<64> Name;
  {
    raw_svector_ostream OS(Name);
    OS << "__os_log_helper";
    OS << "_" << BufferAlignment.getQuantity();
    OS << "_" << int(Layout.getSummaryByte());
    OS << "_" << int(Layout.getNumArgsByte());
    for (const auto &Item : Layout.Items)
      OS << "_" << int(Item.getSizeByte()) << "_"
         << int(Item.getDescriptorByte());
  }

  if (llvm::Function *F = ME.getModule().getFunction(Name))
    return F;

  llvm::SmallVector<QualType, 4> ArgTys;
  FunctionArgList Args;
  Args.push_back(ImplicitParamDecl::Create(Ctx, nullptr, SourceLocation(),
                                           &Ctx.Idents.get("buffer"),
                                           Ctx.VoidPtrTy));
  ArgTys.emplace_back(Ctx.VoidPtrTy);

  for (unsigned int I = 0, E = Layout.Items.size(); I < E; ++I) {
    char Size = Layout.Items[I].getSizeByte();
    if (!Size)
      continue;

    QualType ArgTy = getOSLogArgType(Ctx, Size);
    Args.push_back(ImplicitParamDecl::Create(
        Ctx, nullptr, SourceLocation(),
        &Ctx.Idents.get(std::string("arg") + llvm::to_string(I)), ArgTy));
    ArgTys.emplace_back(ArgTy);
  }

  QualType ReturnTy = Ctx.VoidTy;

  // The helper function has linkonce_odr linkage to enable the linker to merge
  // identical functions. To ensure the merging always happens, 'noinline' is
  // attached to the function when compiling with -Oz.
  const ABIFunctionInfo &FI =
      ME.getTypes().arrangeBuiltinFunctionDeclaration(ReturnTy, Args);
  llvm::FunctionType *FuncTy = ME.getTypes().GetFunctionType(FI);
  llvm::Function *Fn = llvm::Function::Create(
      FuncTy, llvm::GlobalValue::LinkOnceODRLinkage, Name, &ME.getModule());
  Fn->setVisibility(llvm::GlobalValue::HiddenVisibility);
  ME.setLLVMFunctionAttributes(GlobalDecl(), FI, Fn);
  ME.setLLVMFunctionAttributesForDefinition(nullptr, Fn);
  Fn->setDoesNotThrow();

  // Attach 'noinline' at -Oz.
  if (ME.getCodeGenOpts().OptimizeSize == 2)
    Fn->addFnAttr(llvm::Attribute::NoInline);

  auto NL = ApplyDebugLocation::CreateEmpty(*this);
  startFunction(GlobalDecl(), ReturnTy, Fn, FI, Args);

  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  CharUnits Offset;
  Address BufAddr = Address(Builder.CreateLoad(addrOfLocalVar(Args[0]), "buf"),
                            Int8Ty, BufferAlignment);
  Builder.CreateStore(Builder.getInt8(Layout.getSummaryByte()),
                      Builder.CreateConstByteGEP(BufAddr, Offset++, "summary"));
  Builder.CreateStore(Builder.getInt8(Layout.getNumArgsByte()),
                      Builder.CreateConstByteGEP(BufAddr, Offset++, "numArgs"));

  unsigned I = 1;
  for (const auto &Item : Layout.Items) {
    Builder.CreateStore(
        Builder.getInt8(Item.getDescriptorByte()),
        Builder.CreateConstByteGEP(BufAddr, Offset++, "argDescriptor"));
    Builder.CreateStore(
        Builder.getInt8(Item.getSizeByte()),
        Builder.CreateConstByteGEP(BufAddr, Offset++, "argSize"));

    CharUnits Size = Item.size();
    if (!Size.getQuantity())
      continue;

    Address Arg = addrOfLocalVar(Args[I]);
    Address Addr = Builder.CreateConstByteGEP(BufAddr, Offset, "argData");
    Addr = Addr.withElementType(Arg.getElementType());
    Builder.CreateStore(Builder.CreateLoad(Arg), Addr);
    Offset += Size;
    ++I;
  }

  finishFunction();

  return Fn;
}

RValue FunctionEmitter::emitBuiltinOSLogFormat(const CallExpr &E) {
  assert(E.getNumArgs() >= 2 &&
         "__builtin_os_log_format takes at least 2 arguments");
  TreeContext &Ctx = getContext();
  analyze_os_log::OSLogBufferLayout Layout;
  analyze_os_log::computeOSLogBufferLayout(Ctx, &E, Layout);
  Address BufAddr = genPointerWithAlignment(E.getArg(0));
  llvm::SmallVector<llvm::Value *, 4> RetainableOperands;

  // Ignore argument 1, the format string. It is not currently used.
  CallArgList Args;
  Args.add(RValue::get(BufAddr.getPointer()), Ctx.VoidPtrTy);

  for (const auto &Item : Layout.Items) {
    int Size = Item.getSizeByte();
    if (!Size)
      continue;

    llvm::Value *ArgVal;

    if (Item.getKind() == analyze_os_log::OSLogBufferItem::MaskKind) {
      uint64_t Val = 0;
      for (unsigned I = 0, E = Item.getMaskType().size(); I < E; ++I)
        Val |= ((uint64_t)Item.getMaskType()[I]) << I * 8;
      ArgVal = llvm::Constant::getIntegerValue(Int64Ty, llvm::APInt(64, Val));
    } else if (const Expr *TheExpr = Item.getExpr()) {
      ArgVal = genScalarExpr(TheExpr, /*Ignore*/ false);

    } else {
      ArgVal = Builder.getInt32(Item.getConstValue().getQuantity());
    }

    unsigned ArgValSize =
        ME.getDataLayout().getTypeSizeInBits(ArgVal->getType());
    llvm::IntegerType *IntTy =
        llvm::Type::getIntNTy(getLLVMContext(), ArgValSize);
    ArgVal = Builder.CreateBitOrPointerCast(ArgVal, IntTy);
    CanQualType ArgTy = getOSLogArgType(Ctx, Size);
    // If ArgVal has type x86_fp80, zero-extend ArgVal.
    ArgVal = Builder.CreateZExtOrBitCast(ArgVal, convertType(ArgTy));
    Args.add(RValue::get(ArgVal), ArgTy);
  }

  const ABIFunctionInfo &FI =
      ME.getTypes().arrangeBuiltinFunctionCall(Ctx.VoidTy, Args);
  llvm::Function *F = FunctionEmitter(ME).generateBuiltinOSLogHelperFunction(
      Layout, BufAddr.getAlignment());
  genCall(FI, FnCallee::forDirect(F), ReturnValueSlot(), Args);
  return RValue::get(BufAddr.getPointer());
}

namespace {
bool isSpecialUnsignedMultiplySignedResult(unsigned BuiltinID,
                                           WidthAndSignedness Op1Info,
                                           WidthAndSignedness Op2Info,
                                           WidthAndSignedness ResultInfo) {
  return BuiltinID == Builtin::BI__builtin_mul_overflow &&
         Op1Info.Width == Op2Info.Width && Op2Info.Width == ResultInfo.Width &&
         !Op1Info.Signed && !Op2Info.Signed && ResultInfo.Signed;
}

RValue genCheckedUnsignedMultiplySignedResult(
    FunctionEmitter &FE, const neverc::Expr *Op1, WidthAndSignedness Op1Info,
    const neverc::Expr *Op2, WidthAndSignedness Op2Info,
    const neverc::Expr *ResultArg, QualType ResultQTy,
    WidthAndSignedness ResultInfo) {
  assert(isSpecialUnsignedMultiplySignedResult(
             Builtin::BI__builtin_mul_overflow, Op1Info, Op2Info, ResultInfo) &&
         "Cannot specialize this multiply");

  llvm::Value *V1 = FE.genScalarExpr(Op1);
  llvm::Value *V2 = FE.genScalarExpr(Op2);

  llvm::Value *HasOverflow;
  llvm::Value *Result = genOverflowIntrinsic(
      FE, llvm::Intrinsic::umul_with_overflow, V1, V2, HasOverflow);

  // The intrinsic call will detect overflow when the value is > UINT_MAX,
  // however, since the original builtin had a signed result, we need to report
  // an overflow when the result is greater than INT_MAX.
  auto IntMax = llvm::APInt::getSignedMaxValue(ResultInfo.Width);
  llvm::Value *IntMaxValue = llvm::ConstantInt::get(Result->getType(), IntMax);

  llvm::Value *IntMaxOverflow = FE.Builder.CreateICmpUGT(Result, IntMaxValue);
  HasOverflow = FE.Builder.CreateOr(HasOverflow, IntMaxOverflow);

  bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
  Address ResultPtr = FE.genPointerWithAlignment(ResultArg);
  FE.Builder.CreateStore(FE.genToMemory(Result, ResultQTy), ResultPtr,
                         isVolatile);
  return RValue::get(HasOverflow);
}

bool isSpecialMixedSignMultiply(unsigned BuiltinID, WidthAndSignedness Op1Info,
                                WidthAndSignedness Op2Info,
                                WidthAndSignedness ResultInfo) {
  return BuiltinID == Builtin::BI__builtin_mul_overflow &&
         std::max(Op1Info.Width, Op2Info.Width) >= ResultInfo.Width &&
         Op1Info.Signed != Op2Info.Signed;
}

RValue genCheckedMixedSignMultiply(FunctionEmitter &FE, const neverc::Expr *Op1,
                                   WidthAndSignedness Op1Info,
                                   const neverc::Expr *Op2,
                                   WidthAndSignedness Op2Info,
                                   const neverc::Expr *ResultArg,
                                   QualType ResultQTy,
                                   WidthAndSignedness ResultInfo) {
  assert(isSpecialMixedSignMultiply(Builtin::BI__builtin_mul_overflow, Op1Info,
                                    Op2Info, ResultInfo) &&
         "Not a mixed-sign multipliction we can specialize");

  const neverc::Expr *SignedOp = Op1Info.Signed ? Op1 : Op2;
  const neverc::Expr *UnsignedOp = Op1Info.Signed ? Op2 : Op1;
  llvm::Value *Signed = FE.genScalarExpr(SignedOp);
  llvm::Value *Unsigned = FE.genScalarExpr(UnsignedOp);
  unsigned SignedOpWidth = Op1Info.Signed ? Op1Info.Width : Op2Info.Width;
  unsigned UnsignedOpWidth = Op1Info.Signed ? Op2Info.Width : Op1Info.Width;

  // One of the operands may be smaller than the other. If so, [s|z]ext it.
  if (SignedOpWidth < UnsignedOpWidth)
    Signed = FE.Builder.CreateSExt(Signed, Unsigned->getType(), "op.sext");
  if (UnsignedOpWidth < SignedOpWidth)
    Unsigned = FE.Builder.CreateZExt(Unsigned, Signed->getType(), "op.zext");

  llvm::Type *OpTy = Signed->getType();
  llvm::Value *Zero = llvm::Constant::getNullValue(OpTy);
  Address ResultPtr = FE.genPointerWithAlignment(ResultArg);
  llvm::Type *ResTy = ResultPtr.getElementType();
  unsigned OpWidth = std::max(Op1Info.Width, Op2Info.Width);

  // Take the absolute value of the signed operand.
  llvm::Value *IsNegative = FE.Builder.CreateICmpSLT(Signed, Zero);
  llvm::Value *AbsOfNegative = FE.Builder.CreateSub(Zero, Signed);
  llvm::Value *AbsSigned =
      FE.Builder.CreateSelect(IsNegative, AbsOfNegative, Signed);

  // Perform a checked unsigned multiplication.
  llvm::Value *UnsignedOverflow;
  llvm::Value *UnsignedResult =
      genOverflowIntrinsic(FE, llvm::Intrinsic::umul_with_overflow, AbsSigned,
                           Unsigned, UnsignedOverflow);

  llvm::Value *Overflow, *Result;
  if (ResultInfo.Signed) {
    // Signed overflow occurs if the result is greater than INT_MAX or lesser
    // than INT_MIN, i.e when |Result| > (INT_MAX + IsNegative).
    auto IntMax =
        llvm::APInt::getSignedMaxValue(ResultInfo.Width).zext(OpWidth);
    llvm::Value *MaxResult =
        FE.Builder.CreateAdd(llvm::ConstantInt::get(OpTy, IntMax),
                             FE.Builder.CreateZExt(IsNegative, OpTy));
    llvm::Value *SignedOverflow =
        FE.Builder.CreateICmpUGT(UnsignedResult, MaxResult);
    Overflow = FE.Builder.CreateOr(UnsignedOverflow, SignedOverflow);

    // Prepare the signed result (possibly by negating it).
    llvm::Value *NegativeResult = FE.Builder.CreateNeg(UnsignedResult);
    llvm::Value *SignedResult =
        FE.Builder.CreateSelect(IsNegative, NegativeResult, UnsignedResult);
    Result = FE.Builder.CreateTrunc(SignedResult, ResTy);
  } else {
    // Unsigned overflow occurs if the result is < 0 or greater than UINT_MAX.
    llvm::Value *Underflow = FE.Builder.CreateAnd(
        IsNegative, FE.Builder.CreateIsNotNull(UnsignedResult));
    Overflow = FE.Builder.CreateOr(UnsignedOverflow, Underflow);
    if (ResultInfo.Width < OpWidth) {
      auto IntMax = llvm::APInt::getMaxValue(ResultInfo.Width).zext(OpWidth);
      llvm::Value *TruncOverflow = FE.Builder.CreateICmpUGT(
          UnsignedResult, llvm::ConstantInt::get(OpTy, IntMax));
      Overflow = FE.Builder.CreateOr(Overflow, TruncOverflow);
    }

    // Negate the product if it would be negative in infinite precision.
    Result = FE.Builder.CreateSelect(
        IsNegative, FE.Builder.CreateNeg(UnsignedResult), UnsignedResult);

    Result = FE.Builder.CreateTrunc(Result, ResTy);
  }
  assert(Overflow && Result && "Missing overflow or result");

  bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
  FE.Builder.CreateStore(FE.genToMemory(Result, ResultQTy), ResultPtr,
                         isVolatile);
  return RValue::get(Overflow);
}
} // namespace

RValue FunctionEmitter::emitRotate(const CallExpr *E, bool IsRotateRight) {
  llvm::Value *Src = genScalarExpr(E->getArg(0));
  llvm::Value *ShiftAmt = genScalarExpr(E->getArg(1));

  // The builtin's shift arg may have a different type than the source arg and
  // result, but the LLVM intrinsic uses the same type for all values.
  llvm::Type *Ty = Src->getType();
  ShiftAmt = Builder.CreateIntCast(ShiftAmt, Ty, false);

  // Rotate is a special case of LLVM funnel shift - 1st 2 args are the same.
  unsigned IID = IsRotateRight ? Intrinsic::fshr : Intrinsic::fshl;
  Function *F = ME.getIntrinsic(IID, Ty);
  return RValue::get(Builder.CreateCall(F, {Src, Src, ShiftAmt}));
}

namespace {
Value *tryUseTestFPKind(FunctionEmitter &FE, unsigned BuiltinID, Value *V) {
  if (FE.Builder.getIsFPConstrained() &&
      FE.Builder.getDefaultConstrainedExcept() != fp::ebIgnore) {
    if (Value *Result =
            FE.getTargetHooks().testFPKind(V, BuiltinID, FE.Builder, FE.ME))
      return Result;
  }
  return nullptr;
}
} // namespace

RValue FunctionEmitter::genBuiltinExpr(const GlobalDecl GD, unsigned BuiltinID,
                                       const CallExpr *E,
                                       ReturnValueSlot ReturnValue) {
  const FunctionDecl *FD = GD.getDecl()->getAsFunction();
  // See if we can constant fold this builtin.  If so, don't emit it at all.
  Expr::EvalResult Result;
  if (E->isPRValue() && E->EvaluateAsRValue(Result, ME.getContext()) &&
      !Result.hasSideEffects()) {
    if (Result.Val.isInt())
      return RValue::get(
          llvm::ConstantInt::get(getLLVMContext(), Result.Val.getInt()));
    if (Result.Val.isFloat())
      return RValue::get(
          llvm::ConstantFP::get(getLLVMContext(), Result.Val.getFloat()));
  }

  // If the builtin has been declared explicitly with an assembler label,
  // disable the specialized emitting below. Ideally we should communicate the
  // rename in IR, or at least avoid generating the intrinsic calls that are
  // likely to get lowered to the renamed library functions.
  const unsigned BuiltinIDIfNoAsmLabel =
      FD->hasAttr<AsmLabelAttr>() ? 0 : BuiltinID;

  std::optional<bool> ErrnoOverriden;
  // ErrnoOverriden is true if math-errno is overriden via the
  // '#pragma float_control(precise, on)'. This pragma disables fast-math,
  // which implies math-errno.
  if (E->hasStoredFPFeatures()) {
    FPOptionsOverride OP = E->getFPFeatures();
    if (OP.hasMathErrnoOverride())
      ErrnoOverriden = OP.getMathErrnoOverride();
  }
  // True if 'atttibute__((optnone)) is used. This attibute overrides
  // fast-math which implies math-errno.
  bool OptNone = CurFuncDecl && CurFuncDecl->hasAttr<OptimizeNoneAttr>();

  // True if we are compiling at -O2 and errno has been disabled
  // using the '#pragma float_control(precise, off)', and
  // attribute opt-none hasn't been seen.
  bool ErrnoOverridenToFalseWithOpt =
      ErrnoOverriden.has_value() && !ErrnoOverriden.value() && !OptNone &&
      ME.getCodeGenOpts().OptimizationLevel != 0;

  // There are LLVM math intrinsics/instructions corresponding to math library
  // functions except the LLVM op will never set errno while the math library
  // might. Also, math builtins have the same semantics as their math library
  // twins. Thus, we can transform math library and builtin calls to their
  // LLVM counterparts if the call is marked 'const' (known to never set errno).
  // In case FP exceptions are enabled, the experimental versions of the
  // intrinsics model those.
  bool ConstAlways = getContext().BuiltinInfo.isConst(BuiltinID);

  // There's a special case with the fma builtins where they are always const
  // if the target environment is GNU or the target is OS is Windows and we're
  // targeting the MSVCRT.dll environment.
  switch (BuiltinID) {
  case Builtin::BI__builtin_fma:
  case Builtin::BI__builtin_fmaf:
  case Builtin::BI__builtin_fmal:
  case Builtin::BIfma:
  case Builtin::BIfmaf:
  case Builtin::BIfmal: {
    auto &Trip = ME.getTriple();
    if (Trip.isGNUEnvironment() || Trip.isOSMSVCRT())
      ConstAlways = true;
    break;
  }
  default:
    break;
  }

  bool ConstWithoutErrnoAndExceptions =
      getContext().BuiltinInfo.isConstWithoutErrnoAndExceptions(BuiltinID);
  bool ConstWithoutExceptions =
      getContext().BuiltinInfo.isConstWithoutExceptions(BuiltinID);

  // ConstAttr is enabled in fast-math mode. In fast-math mode, math-errno is
  // disabled.
  // Math intrinsics are generated only when math-errno is disabled. Any pragmas
  // or attributes that affect math-errno should prevent or allow math
  // intrincs to be generated. Intrinsics are generated:
  //   1- In fast math mode, unless math-errno is overriden
  //      via '#pragma float_control(precise, on)', or via an
  //      'attribute__((optnone))'.
  //   2- If math-errno was enabled on command line but overriden
  //      to false via '#pragma float_control(precise, off))' and
  //      'attribute__((optnone))' hasn't been used.
  //   3- If we are compiling with optimization and errno has been disabled
  //      via '#pragma float_control(precise, off)', and
  //      'attribute__((optnone))' hasn't been used.

  bool ConstWithoutErrnoOrExceptions =
      ConstWithoutErrnoAndExceptions || ConstWithoutExceptions;
  bool GenerateIntrinsics =
      (ConstAlways && !OptNone) ||
      (!getLangOpts().MathErrno &&
       !(ErrnoOverriden.has_value() && ErrnoOverriden.value()) && !OptNone);
  if (!GenerateIntrinsics) {
    GenerateIntrinsics =
        ConstWithoutErrnoOrExceptions && !ConstWithoutErrnoAndExceptions;
    if (!GenerateIntrinsics)
      GenerateIntrinsics =
          ConstWithoutErrnoOrExceptions &&
          (!getLangOpts().MathErrno &&
           !(ErrnoOverriden.has_value() && ErrnoOverriden.value()) && !OptNone);
    if (!GenerateIntrinsics)
      GenerateIntrinsics =
          ConstWithoutErrnoOrExceptions && ErrnoOverridenToFalseWithOpt;
  }
  if (GenerateIntrinsics) {
    switch (BuiltinIDIfNoAsmLabel) {
    case Builtin::BIceil:
    case Builtin::BIceilf:
    case Builtin::BIceill:
    case Builtin::BI__builtin_ceil:
    case Builtin::BI__builtin_ceilf:
    case Builtin::BI__builtin_ceilf16:
    case Builtin::BI__builtin_ceill:
    case Builtin::BI__builtin_ceilf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::ceil, Intrinsic::experimental_constrained_ceil));

    case Builtin::BIcopysign:
    case Builtin::BIcopysignf:
    case Builtin::BIcopysignl:
    case Builtin::BI__builtin_copysign:
    case Builtin::BI__builtin_copysignf:
    case Builtin::BI__builtin_copysignf16:
    case Builtin::BI__builtin_copysignl:
    case Builtin::BI__builtin_copysignf128:
      return RValue::get(emitBinaryBuiltin(*this, E, Intrinsic::copysign));

    case Builtin::BIcos:
    case Builtin::BIcosf:
    case Builtin::BIcosl:
    case Builtin::BI__builtin_cos:
    case Builtin::BI__builtin_cosf:
    case Builtin::BI__builtin_cosf16:
    case Builtin::BI__builtin_cosl:
    case Builtin::BI__builtin_cosf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::cos, Intrinsic::experimental_constrained_cos));

    case Builtin::BIexp:
    case Builtin::BIexpf:
    case Builtin::BIexpl:
    case Builtin::BI__builtin_exp:
    case Builtin::BI__builtin_expf:
    case Builtin::BI__builtin_expf16:
    case Builtin::BI__builtin_expl:
    case Builtin::BI__builtin_expf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::exp, Intrinsic::experimental_constrained_exp));

    case Builtin::BIexp2:
    case Builtin::BIexp2f:
    case Builtin::BIexp2l:
    case Builtin::BI__builtin_exp2:
    case Builtin::BI__builtin_exp2f:
    case Builtin::BI__builtin_exp2f16:
    case Builtin::BI__builtin_exp2l:
    case Builtin::BI__builtin_exp2f128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::exp2, Intrinsic::experimental_constrained_exp2));
    case Builtin::BI__builtin_exp10:
    case Builtin::BI__builtin_exp10f:
    case Builtin::BI__builtin_exp10f16:
    case Builtin::BI__builtin_exp10l:
    case Builtin::BI__builtin_exp10f128: {
      if (Builder.getIsFPConstrained())
        break;
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::exp10));
    }
    case Builtin::BIfabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabsl:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabsf16:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_fabsf128:
      return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::fabs));

    case Builtin::BIfloor:
    case Builtin::BIfloorf:
    case Builtin::BIfloorl:
    case Builtin::BI__builtin_floor:
    case Builtin::BI__builtin_floorf:
    case Builtin::BI__builtin_floorf16:
    case Builtin::BI__builtin_floorl:
    case Builtin::BI__builtin_floorf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::floor,
          Intrinsic::experimental_constrained_floor));

    case Builtin::BIfma:
    case Builtin::BIfmaf:
    case Builtin::BIfmal:
    case Builtin::BI__builtin_fma:
    case Builtin::BI__builtin_fmaf:
    case Builtin::BI__builtin_fmaf16:
    case Builtin::BI__builtin_fmal:
    case Builtin::BI__builtin_fmaf128:
      return RValue::get(emitTernaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::fma, Intrinsic::experimental_constrained_fma));

    case Builtin::BIfmax:
    case Builtin::BIfmaxf:
    case Builtin::BIfmaxl:
    case Builtin::BI__builtin_fmax:
    case Builtin::BI__builtin_fmaxf:
    case Builtin::BI__builtin_fmaxf16:
    case Builtin::BI__builtin_fmaxl:
    case Builtin::BI__builtin_fmaxf128:
      return RValue::get(emitBinaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::maxnum,
          Intrinsic::experimental_constrained_maxnum));

    case Builtin::BIfmin:
    case Builtin::BIfminf:
    case Builtin::BIfminl:
    case Builtin::BI__builtin_fmin:
    case Builtin::BI__builtin_fminf:
    case Builtin::BI__builtin_fminf16:
    case Builtin::BI__builtin_fminl:
    case Builtin::BI__builtin_fminf128:
      return RValue::get(emitBinaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::minnum,
          Intrinsic::experimental_constrained_minnum));

    // fmod() is a special-case. It maps to the frem instruction rather than an
    // LLVM intrinsic.
    case Builtin::BIfmod:
    case Builtin::BIfmodf:
    case Builtin::BIfmodl:
    case Builtin::BI__builtin_fmod:
    case Builtin::BI__builtin_fmodf:
    case Builtin::BI__builtin_fmodf16:
    case Builtin::BI__builtin_fmodl:
    case Builtin::BI__builtin_fmodf128: {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
      Value *Arg1 = genScalarExpr(E->getArg(0));
      Value *Arg2 = genScalarExpr(E->getArg(1));
      return RValue::get(Builder.CreateFRem(Arg1, Arg2, "fmod"));
    }

    case Builtin::BIlog:
    case Builtin::BIlogf:
    case Builtin::BIlogl:
    case Builtin::BI__builtin_log:
    case Builtin::BI__builtin_logf:
    case Builtin::BI__builtin_logf16:
    case Builtin::BI__builtin_logl:
    case Builtin::BI__builtin_logf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::log, Intrinsic::experimental_constrained_log));

    case Builtin::BIlog10:
    case Builtin::BIlog10f:
    case Builtin::BIlog10l:
    case Builtin::BI__builtin_log10:
    case Builtin::BI__builtin_log10f:
    case Builtin::BI__builtin_log10f16:
    case Builtin::BI__builtin_log10l:
    case Builtin::BI__builtin_log10f128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::log10,
          Intrinsic::experimental_constrained_log10));

    case Builtin::BIlog2:
    case Builtin::BIlog2f:
    case Builtin::BIlog2l:
    case Builtin::BI__builtin_log2:
    case Builtin::BI__builtin_log2f:
    case Builtin::BI__builtin_log2f16:
    case Builtin::BI__builtin_log2l:
    case Builtin::BI__builtin_log2f128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::log2, Intrinsic::experimental_constrained_log2));

    case Builtin::BInearbyint:
    case Builtin::BInearbyintf:
    case Builtin::BInearbyintl:
    case Builtin::BI__builtin_nearbyint:
    case Builtin::BI__builtin_nearbyintf:
    case Builtin::BI__builtin_nearbyintl:
    case Builtin::BI__builtin_nearbyintf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::nearbyint,
          Intrinsic::experimental_constrained_nearbyint));

    case Builtin::BIpow:
    case Builtin::BIpowf:
    case Builtin::BIpowl:
    case Builtin::BI__builtin_pow:
    case Builtin::BI__builtin_powf:
    case Builtin::BI__builtin_powf16:
    case Builtin::BI__builtin_powl:
    case Builtin::BI__builtin_powf128:
      return RValue::get(emitBinaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::pow, Intrinsic::experimental_constrained_pow));

    case Builtin::BIrint:
    case Builtin::BIrintf:
    case Builtin::BIrintl:
    case Builtin::BI__builtin_rint:
    case Builtin::BI__builtin_rintf:
    case Builtin::BI__builtin_rintf16:
    case Builtin::BI__builtin_rintl:
    case Builtin::BI__builtin_rintf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::rint, Intrinsic::experimental_constrained_rint));

    case Builtin::BIround:
    case Builtin::BIroundf:
    case Builtin::BIroundl:
    case Builtin::BI__builtin_round:
    case Builtin::BI__builtin_roundf:
    case Builtin::BI__builtin_roundf16:
    case Builtin::BI__builtin_roundl:
    case Builtin::BI__builtin_roundf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::round,
          Intrinsic::experimental_constrained_round));

    case Builtin::BIroundeven:
    case Builtin::BIroundevenf:
    case Builtin::BIroundevenl:
    case Builtin::BI__builtin_roundeven:
    case Builtin::BI__builtin_roundevenf:
    case Builtin::BI__builtin_roundevenf16:
    case Builtin::BI__builtin_roundevenl:
    case Builtin::BI__builtin_roundevenf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::roundeven,
          Intrinsic::experimental_constrained_roundeven));

    case Builtin::BIsin:
    case Builtin::BIsinf:
    case Builtin::BIsinl:
    case Builtin::BI__builtin_sin:
    case Builtin::BI__builtin_sinf:
    case Builtin::BI__builtin_sinf16:
    case Builtin::BI__builtin_sinl:
    case Builtin::BI__builtin_sinf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::sin, Intrinsic::experimental_constrained_sin));

    case Builtin::BIsqrt:
    case Builtin::BIsqrtf:
    case Builtin::BIsqrtl:
    case Builtin::BI__builtin_sqrt:
    case Builtin::BI__builtin_sqrtf:
    case Builtin::BI__builtin_sqrtf16:
    case Builtin::BI__builtin_sqrtl:
    case Builtin::BI__builtin_sqrtf128:
    case Builtin::BI__builtin_elementwise_sqrt: {
      llvm::Value *Call = emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::sqrt, Intrinsic::experimental_constrained_sqrt);
      return RValue::get(Call);
    }
    case Builtin::BItrunc:
    case Builtin::BItruncf:
    case Builtin::BItruncl:
    case Builtin::BI__builtin_trunc:
    case Builtin::BI__builtin_truncf:
    case Builtin::BI__builtin_truncf16:
    case Builtin::BI__builtin_truncl:
    case Builtin::BI__builtin_truncf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::trunc,
          Intrinsic::experimental_constrained_trunc));

    case Builtin::BIlround:
    case Builtin::BIlroundf:
    case Builtin::BIlroundl:
    case Builtin::BI__builtin_lround:
    case Builtin::BI__builtin_lroundf:
    case Builtin::BI__builtin_lroundl:
    case Builtin::BI__builtin_lroundf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::lround,
          Intrinsic::experimental_constrained_lround));

    case Builtin::BIllround:
    case Builtin::BIllroundf:
    case Builtin::BIllroundl:
    case Builtin::BI__builtin_llround:
    case Builtin::BI__builtin_llroundf:
    case Builtin::BI__builtin_llroundl:
    case Builtin::BI__builtin_llroundf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::llround,
          Intrinsic::experimental_constrained_llround));

    case Builtin::BIlrint:
    case Builtin::BIlrintf:
    case Builtin::BIlrintl:
    case Builtin::BI__builtin_lrint:
    case Builtin::BI__builtin_lrintf:
    case Builtin::BI__builtin_lrintl:
    case Builtin::BI__builtin_lrintf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::lrint,
          Intrinsic::experimental_constrained_lrint));

    case Builtin::BIllrint:
    case Builtin::BIllrintf:
    case Builtin::BIllrintl:
    case Builtin::BI__builtin_llrint:
    case Builtin::BI__builtin_llrintf:
    case Builtin::BI__builtin_llrintl:
    case Builtin::BI__builtin_llrintf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::llrint,
          Intrinsic::experimental_constrained_llrint));
    case Builtin::BI__builtin_ldexp:
    case Builtin::BI__builtin_ldexpf:
    case Builtin::BI__builtin_ldexpl:
    case Builtin::BI__builtin_ldexpf16:
    case Builtin::BI__builtin_ldexpf128: {
      return RValue::get(emitBinaryExpMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::ldexp,
          Intrinsic::experimental_constrained_ldexp));
    }
    default:
      break;
    }
  }

  switch (BuiltinIDIfNoAsmLabel) {
  default:
    break;
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
  case Builtin::BI__va_start:
  case Builtin::BI__builtin_va_end:
    genVAStartEnd(BuiltinID == Builtin::BI__va_start
                      ? genScalarExpr(E->getArg(0))
                      : genVAListRef(E->getArg(0)).getPointer(),
                  BuiltinID != Builtin::BI__builtin_va_end);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_va_copy: {
    Value *DstPtr = genVAListRef(E->getArg(0)).getPointer();
    Value *SrcPtr = genVAListRef(E->getArg(1)).getPointer();
    Builder.CreateCall(ME.getIntrinsic(Intrinsic::vacopy), {DstPtr, SrcPtr});
    return RValue::get(nullptr);
  }
  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs: {
    Value *Result;
    switch (getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      Result = genAbs(*this, genScalarExpr(E->getArg(0)), false);
      break;
    case LangOptions::SOB_Undefined:
      Result = genAbs(*this, genScalarExpr(E->getArg(0)), true);
      break;
    case LangOptions::SOB_Trapping:
      Result = genOverflowCheckedAbs(*this, E, /*SanitizeOverflow=*/false);
      break;
    }
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_complex: {
    Value *Real = genScalarExpr(E->getArg(0));
    Value *Imag = genScalarExpr(E->getArg(1));
    return RValue::getComplex({Real, Imag});
  }
  case Builtin::BI__builtin_conj:
  case Builtin::BI__builtin_conjf:
  case Builtin::BI__builtin_conjl:
  case Builtin::BIconj:
  case Builtin::BIconjf:
  case Builtin::BIconjl: {
    ComplexPairTy ComplexVal = genComplexExpr(E->getArg(0));
    Value *Real = ComplexVal.first;
    Value *Imag = ComplexVal.second;
    Imag = Builder.CreateFNeg(Imag, "neg");
    return RValue::getComplex(std::make_pair(Real, Imag));
  }
  case Builtin::BI__builtin_creal:
  case Builtin::BI__builtin_crealf:
  case Builtin::BI__builtin_creall:
  case Builtin::BIcreal:
  case Builtin::BIcrealf:
  case Builtin::BIcreall: {
    ComplexPairTy ComplexVal = genComplexExpr(E->getArg(0));
    return RValue::get(ComplexVal.first);
  }

  case Builtin::BI__builtin_preserve_access_index: {
    // Only enabled preserved access index region when debuginfo
    // is available as debuginfo is needed to preserve user-level
    // access pattern.
    if (!getDebugInfo()) {
      ME.error(E->getExprLoc(),
               "using builtin_preserve_access_index() without -g");
      return RValue::get(genScalarExpr(E->getArg(0)));
    }

    // Nested builtin_preserve_access_index() not supported
    if (IsInPreservedAIRegion) {
      ME.error(E->getExprLoc(),
               "nested builtin_preserve_access_index() not supported");
      return RValue::get(genScalarExpr(E->getArg(0)));
    }

    IsInPreservedAIRegion = true;
    Value *Res = genScalarExpr(E->getArg(0));
    IsInPreservedAIRegion = false;
    return RValue::get(Res);
  }

  case Builtin::BI__builtin_cimag:
  case Builtin::BI__builtin_cimagf:
  case Builtin::BI__builtin_cimagl:
  case Builtin::BIcimag:
  case Builtin::BIcimagf:
  case Builtin::BIcimagl: {
    ComplexPairTy ComplexVal = genComplexExpr(E->getArg(0));
    return RValue::get(ComplexVal.second);
  }

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll: {
    // clrsb(x) -> clz(x < 0 ? ~x : x) - 1 or
    Value *ArgValue = genScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsNeg = Builder.CreateICmpSLT(ArgValue, Zero, "isneg");
    Value *Inverse = Builder.CreateNot(ArgValue, "not");
    Value *Tmp = Builder.CreateSelect(IsNeg, Inverse, ArgValue);
    Value *Ctlz = Builder.CreateCall(F, {Tmp, Builder.getFalse()});
    Value *Result = Builder.CreateSub(Ctlz, llvm::ConstantInt::get(ArgType, 1));
    Result =
        Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ctzs:
  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll: {
    Value *ArgValue = genCheckedArgForBuiltin(E->getArg(0), BCK_CTZPassedZero);

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::cttz, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *ZeroUndef = Builder.getInt1(getTarget().isCLZForZeroUndef());
    Value *Result = Builder.CreateCall(F, {ArgValue, ZeroUndef});
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_clzs:
  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll: {
    Value *ArgValue = genCheckedArgForBuiltin(E->getArg(0), BCK_CLZPassedZero);

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *ZeroUndef = Builder.getInt1(getTarget().isCLZForZeroUndef());
    Value *Result = Builder.CreateCall(F, {ArgValue, ZeroUndef});
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll: {
    // ffs(x) -> x ? cttz(x) + 1 : 0
    Value *ArgValue = genScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::cttz, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *Tmp =
        Builder.CreateAdd(Builder.CreateCall(F, {ArgValue, Builder.getTrue()}),
                          llvm::ConstantInt::get(ArgType, 1));
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, Zero, "iszero");
    Value *Result = Builder.CreateSelect(IsZero, Zero, Tmp, "ffs");
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll: {
    // parity(x) -> ctpop(x) & 1
    Value *ArgValue = genScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::ctpop, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *Tmp = Builder.CreateCall(F, ArgValue);
    Value *Result = Builder.CreateAnd(Tmp, llvm::ConstantInt::get(ArgType, 1));
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__lzcnt16:
  case Builtin::BI__lzcnt:
  case Builtin::BI__lzcnt64: {
    Value *ArgValue = genScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *Result = Builder.CreateCall(F, {ArgValue, Builder.getFalse()});
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__popcnt16:
  case Builtin::BI__popcnt:
  case Builtin::BI__popcnt64:
  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll: {
    Value *ArgValue = genScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = ME.getIntrinsic(Intrinsic::ctpop, ArgType);

    llvm::Type *ResultType = convertType(E->getType());
    Value *Result = Builder.CreateCall(F, ArgValue);
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_unpredictable: {
    // Always return the argument of __builtin_unpredictable. LLVM does not
    // handle this builtin. Metadata for this builtin should be added directly
    // to instructions such as branches or switches that use it.
    return RValue::get(genScalarExpr(E->getArg(0)));
  }
  case Builtin::BI__builtin_expect: {
    Value *ArgValue = genScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();

    Value *ExpectedValue = genScalarExpr(E->getArg(1));
    // Don't generate llvm.expect on -O0 as the backend won't use it for
    // anything.
    // Note, we still IRGen ExpectedValue because it could have side-effects.
    if (ME.getCodeGenOpts().OptimizationLevel == 0)
      return RValue::get(ArgValue);

    Function *FnExpect = ME.getIntrinsic(Intrinsic::expect, ArgType);
    Value *Result =
        Builder.CreateCall(FnExpect, {ArgValue, ExpectedValue}, "expval");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_expect_with_probability: {
    Value *ArgValue = genScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();

    Value *ExpectedValue = genScalarExpr(E->getArg(1));
    llvm::APFloat Probability(0.0);
    const Expr *ProbArg = E->getArg(2);
    bool EvalSucceed = ProbArg->EvaluateAsFloat(Probability, ME.getContext());
    assert(EvalSucceed && "probability should be able to evaluate as float");
    (void)EvalSucceed;
    bool LoseInfo = false;
    Probability.convert(llvm::APFloat::IEEEdouble(),
                        llvm::RoundingMode::Dynamic, &LoseInfo);
    llvm::Type *Ty = convertType(ProbArg->getType());
    Constant *Confidence = ConstantFP::get(Ty, Probability);
    // Don't generate llvm.expect.with.probability on -O0 as the backend
    // won't use it for anything.
    // Note, we still IRGen ExpectedValue because it could have side-effects.
    if (ME.getCodeGenOpts().OptimizationLevel == 0)
      return RValue::get(ArgValue);

    Function *FnExpect =
        ME.getIntrinsic(Intrinsic::expect_with_probability, ArgType);
    Value *Result = Builder.CreateCall(
        FnExpect, {ArgValue, ExpectedValue, Confidence}, "expval");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_assume_aligned: {
    const Expr *Ptr = E->getArg(0);
    Value *PtrValue = genScalarExpr(Ptr);
    Value *OffsetValue =
        (E->getNumArgs() > 2) ? genScalarExpr(E->getArg(2)) : nullptr;

    Value *AlignmentValue = genScalarExpr(E->getArg(1));
    ConstantInt *AlignmentCI = cast<ConstantInt>(AlignmentValue);
    if (AlignmentCI->getValue().ugt(llvm::Value::MaximumAlignment))
      AlignmentCI = ConstantInt::get(AlignmentCI->getIntegerType(),
                                     llvm::Value::MaximumAlignment);

    emitAlignmentAssumption(PtrValue, Ptr, AlignmentCI, OffsetValue);
    return RValue::get(PtrValue);
  }
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume: {
    if (E->getArg(0)->HasSideEffects(getContext()))
      return RValue::get(nullptr);

    Value *ArgValue = genScalarExpr(E->getArg(0));
    Function *FnAssume = ME.getIntrinsic(Intrinsic::assume);
    Builder.CreateCall(FnAssume, ArgValue);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_assume_separate_storage: {
    const Expr *Arg0 = E->getArg(0);
    const Expr *Arg1 = E->getArg(1);

    Value *Value0 = genScalarExpr(Arg0);
    Value *Value1 = genScalarExpr(Arg1);

    Value *Values[] = {Value0, Value1};
    OperandBundleDefT<Value *> OBD("separate_storage", Values);
    Builder.CreateAssumption(ConstantInt::getTrue(getLLVMContext()), {OBD});
    return RValue::get(nullptr);
  }
  case Builtin::BI__arithmetic_fence: {
    // Create the builtin call if FastMath is selected, and the target
    // supports the builtin, otherwise just return the argument.
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    llvm::FastMathFlags FMF = Builder.getFastMathFlags();
    bool isArithmeticFenceEnabled =
        FMF.allowReassoc() &&
        getContext().getTargetInfo().checkArithmeticFenceSupported();
    QualType ArgType = E->getArg(0)->getType();
    if (ArgType->isComplexType()) {
      if (isArithmeticFenceEnabled) {
        QualType ElementType = ArgType->castAs<ComplexType>()->getElementType();
        ComplexPairTy ComplexVal = genComplexExpr(E->getArg(0));
        Value *Real = Builder.CreateArithmeticFence(ComplexVal.first,
                                                    convertType(ElementType));
        Value *Imag = Builder.CreateArithmeticFence(ComplexVal.second,
                                                    convertType(ElementType));
        return RValue::getComplex(std::make_pair(Real, Imag));
      }
      ComplexPairTy ComplexVal = genComplexExpr(E->getArg(0));
      Value *Real = ComplexVal.first;
      Value *Imag = ComplexVal.second;
      return RValue::getComplex(std::make_pair(Real, Imag));
    }
    Value *ArgValue = genScalarExpr(E->getArg(0));
    if (isArithmeticFenceEnabled)
      return RValue::get(
          Builder.CreateArithmeticFence(ArgValue, convertType(ArgType)));
    return RValue::get(ArgValue);
  }
  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64:
  case Builtin::BI_byteswap_ushort:
  case Builtin::BI_byteswap_ulong:
  case Builtin::BI_byteswap_uint64: {
    return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::bswap));
  }
  case Builtin::BI__builtin_bitreverse8:
  case Builtin::BI__builtin_bitreverse16:
  case Builtin::BI__builtin_bitreverse32:
  case Builtin::BI__builtin_bitreverse64: {
    return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::bitreverse));
  }
  case Builtin::BI__builtin_rotateleft8:
  case Builtin::BI__builtin_rotateleft16:
  case Builtin::BI__builtin_rotateleft32:
  case Builtin::BI__builtin_rotateleft64:
  case Builtin::BI_rotl8: // Microsoft variants of rotate left
  case Builtin::BI_rotl16:
  case Builtin::BI_rotl:
  case Builtin::BI_lrotl:
  case Builtin::BI_rotl64:
    return emitRotate(E, false);

  case Builtin::BI__builtin_rotateright8:
  case Builtin::BI__builtin_rotateright16:
  case Builtin::BI__builtin_rotateright32:
  case Builtin::BI__builtin_rotateright64:
  case Builtin::BI_rotr8: // Microsoft variants of rotate right
  case Builtin::BI_rotr16:
  case Builtin::BI_rotr:
  case Builtin::BI_lrotr:
  case Builtin::BI_rotr64:
    return emitRotate(E, true);

  case Builtin::BI__builtin_constant_p: {
    llvm::Type *ResultType = convertType(E->getType());

    const Expr *Arg = E->getArg(0);
    QualType ArgType = Arg->getType();
    if (!ArgType->isIntegralOrEnumerationType() && !ArgType->isFloatingType())
      // Per the GCC documentation, only numeric constants are recognized after
      // inlining.
      return RValue::get(ConstantInt::get(ResultType, 0));

    if (Arg->HasSideEffects(getContext()))
      // The argument is unevaluated, so be conservative if it might have
      // side-effects.
      return RValue::get(ConstantInt::get(ResultType, 0));

    Value *ArgValue = genScalarExpr(Arg);
    Function *F = ME.getIntrinsic(Intrinsic::is_constant, convertType(ArgType));
    Value *Result = Builder.CreateCall(F, ArgValue);
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/ false);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size: {
    unsigned Type =
        E->getArg(1)->EvaluateKnownConstInt(getContext()).getZExtValue();
    auto *ResType = cast<llvm::IntegerType>(convertType(E->getType()));

    // We pass this builtin onto the optimizer so that it can figure out the
    // object size in more complex cases.
    bool IsDynamic = BuiltinID == Builtin::BI__builtin_dynamic_object_size;
    return RValue::get(emitBuiltinObjectSize(E->getArg(0), Type, ResType,
                                             /*EmittedE=*/nullptr, IsDynamic));
  }
  case Builtin::BI__builtin_prefetch: {
    Value *Locality, *RW, *Address = genScalarExpr(E->getArg(0));
    RW = (E->getNumArgs() > 1) ? genScalarExpr(E->getArg(1))
                               : llvm::ConstantInt::get(Int32Ty, 0);
    Locality = (E->getNumArgs() > 2) ? genScalarExpr(E->getArg(2))
                                     : llvm::ConstantInt::get(Int32Ty, 3);
    Value *Data = llvm::ConstantInt::get(Int32Ty, 1);
    Function *F = ME.getIntrinsic(Intrinsic::prefetch, Address->getType());
    Builder.CreateCall(F, {Address, RW, Locality, Data});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_readcyclecounter: {
    Function *F = ME.getIntrinsic(Intrinsic::readcyclecounter);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin___clear_cache: {
    Value *Begin = genScalarExpr(E->getArg(0));
    Value *End = genScalarExpr(E->getArg(1));
    Function *F = ME.getIntrinsic(Intrinsic::clear_cache);
    return RValue::get(Builder.CreateCall(F, {Begin, End}));
  }
  case Builtin::BI__builtin_trap:
    genTrapCall(Intrinsic::trap);
    return RValue::get(nullptr);
  case Builtin::BI__debugbreak:
    genTrapCall(Intrinsic::debugtrap);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_unreachable: {
    genUnreachable(E->getExprLoc());

    // We do need to preserve an insertion point.
    genBlock(createBasicBlock("unreachable.cont"));

    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_powi:
  case Builtin::BI__builtin_powif:
  case Builtin::BI__builtin_powil: {
    llvm::Value *Src0 = genScalarExpr(E->getArg(0));
    llvm::Value *Src1 = genScalarExpr(E->getArg(1));

    if (Builder.getIsFPConstrained()) {
      FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
      Function *F = ME.getIntrinsic(Intrinsic::experimental_constrained_powi,
                                    Src0->getType());
      return RValue::get(Builder.CreateConstrainedFPCall(F, {Src0, Src1}));
    }

    Function *F =
        ME.getIntrinsic(Intrinsic::powi, {Src0->getType(), Src1->getType()});
    return RValue::get(Builder.CreateCall(F, {Src0, Src1}));
  }
  case Builtin::BI__builtin_frexpl:
  case Builtin::BI__builtin_frexp:
  case Builtin::BI__builtin_frexpf:
  case Builtin::BI__builtin_frexpf128:
  case Builtin::BI__builtin_frexpf16:
    return RValue::get(emitFrexpBuiltin(*this, E, Intrinsic::frexp));
  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered: {
    // Ordered comparisons: we know the arguments to these are matching scalar
    // floating point values.
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *LHS = genScalarExpr(E->getArg(0));
    Value *RHS = genScalarExpr(E->getArg(1));

    switch (BuiltinID) {
    default:
      llvm_unreachable("Unknown ordered comparison");
    case Builtin::BI__builtin_isgreater:
      LHS = Builder.CreateFCmpOGT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isgreaterequal:
      LHS = Builder.CreateFCmpOGE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isless:
      LHS = Builder.CreateFCmpOLT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessequal:
      LHS = Builder.CreateFCmpOLE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessgreater:
      LHS = Builder.CreateFCmpONE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isunordered:
      LHS = Builder.CreateFCmpUNO(LHS, RHS, "cmp");
      break;
    }
    // ZExt bool to int type.
    return RValue::get(Builder.CreateZExt(LHS, convertType(E->getType())));
  }

  case Builtin::BI__builtin_isnan: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    if (Value *Result = tryUseTestFPKind(*this, BuiltinID, V))
      return RValue::get(Result);
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcNan),
                           convertType(E->getType())));
  }

  case Builtin::BI__builtin_issignaling: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcSNan),
                           convertType(E->getType())));
  }

  case Builtin::BI__builtin_isinf: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    if (Value *Result = tryUseTestFPKind(*this, BuiltinID, V))
      return RValue::get(Result);
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcInf),
                           convertType(E->getType())));
  }

  case Builtin::BIfinite:
  case Builtin::BI__finite:
  case Builtin::BIfinitef:
  case Builtin::BI__finitef:
  case Builtin::BIfinitel:
  case Builtin::BI__finitel:
  case Builtin::BI__builtin_isfinite: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    if (Value *Result = tryUseTestFPKind(*this, BuiltinID, V))
      return RValue::get(Result);
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcFinite),
                           convertType(E->getType())));
  }

  case Builtin::BI__builtin_isnormal: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcNormal),
                           convertType(E->getType())));
  }

  case Builtin::BI__builtin_issubnormal: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcSubnormal),
                           convertType(E->getType())));
  }

  case Builtin::BI__builtin_iszero: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcZero),
                           convertType(E->getType())));
  }

  case Builtin::BI__builtin_isfpclass: {
    Expr::EvalResult Result;
    if (!E->getArg(1)->EvaluateAsInt(Result, ME.getContext()))
      break;
    uint64_t Test = Result.Val.getInt().getLimitedValue();
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(0));
    return RValue::get(Builder.CreateZExt(Builder.createIsFPClass(V, Test),
                                          convertType(E->getType())));
  }

  case Builtin::BI__builtin_nondeterministic_value: {
    llvm::Type *Ty = convertType(E->getArg(0)->getType());

    Value *Result = PoisonValue::get(Ty);
    Result = Builder.CreateFreeze(Result);

    return RValue::get(Result);
  }

  case Builtin::BI__builtin_elementwise_abs: {
    Value *Result;
    QualType QT = E->getArg(0)->getType();

    if (auto *VecTy = QT->getAs<VectorType>())
      QT = VecTy->getElementType();
    if (QT->isIntegerType())
      Result = Builder.CreateBinaryIntrinsic(
          llvm::Intrinsic::abs, genScalarExpr(E->getArg(0)), Builder.getFalse(),
          nullptr, "elt.abs");
    else
      Result = emitUnaryBuiltin(*this, E, llvm::Intrinsic::fabs, "elt.abs");

    return RValue::get(Result);
  }

  case Builtin::BI__builtin_elementwise_ceil:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::ceil, "elt.ceil"));
  case Builtin::BI__builtin_elementwise_exp:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::exp, "elt.exp"));
  case Builtin::BI__builtin_elementwise_exp2:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::exp2, "elt.exp2"));
  case Builtin::BI__builtin_elementwise_log:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::log, "elt.log"));
  case Builtin::BI__builtin_elementwise_log2:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::log2, "elt.log2"));
  case Builtin::BI__builtin_elementwise_log10:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::log10, "elt.log10"));
  case Builtin::BI__builtin_elementwise_pow: {
    return RValue::get(emitBinaryBuiltin(*this, E, llvm::Intrinsic::pow));
  }
  case Builtin::BI__builtin_elementwise_bitreverse:
    return RValue::get(emitUnaryBuiltin(*this, E, llvm::Intrinsic::bitreverse,
                                        "elt.bitreverse"));
  case Builtin::BI__builtin_elementwise_cos:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::cos, "elt.cos"));
  case Builtin::BI__builtin_elementwise_floor:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::floor, "elt.floor"));
  case Builtin::BI__builtin_elementwise_roundeven:
    return RValue::get(emitUnaryBuiltin(*this, E, llvm::Intrinsic::roundeven,
                                        "elt.roundeven"));
  case Builtin::BI__builtin_elementwise_round:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::round, "elt.round"));
  case Builtin::BI__builtin_elementwise_rint:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::rint, "elt.rint"));
  case Builtin::BI__builtin_elementwise_nearbyint:
    return RValue::get(emitUnaryBuiltin(*this, E, llvm::Intrinsic::nearbyint,
                                        "elt.nearbyint"));
  case Builtin::BI__builtin_elementwise_sin:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::sin, "elt.sin"));

  case Builtin::BI__builtin_elementwise_trunc:
    return RValue::get(
        emitUnaryBuiltin(*this, E, llvm::Intrinsic::trunc, "elt.trunc"));
  case Builtin::BI__builtin_elementwise_canonicalize:
    return RValue::get(emitUnaryBuiltin(*this, E, llvm::Intrinsic::canonicalize,
                                        "elt.canonicalize"));
  case Builtin::BI__builtin_elementwise_copysign:
    return RValue::get(emitBinaryBuiltin(*this, E, llvm::Intrinsic::copysign));
  case Builtin::BI__builtin_elementwise_fma:
    return RValue::get(emitTernaryBuiltin(*this, E, llvm::Intrinsic::fma));
  case Builtin::BI__builtin_elementwise_add_sat:
  case Builtin::BI__builtin_elementwise_sub_sat: {
    Value *Op0 = genScalarExpr(E->getArg(0));
    Value *Op1 = genScalarExpr(E->getArg(1));
    Value *Result;
    assert(Op0->getType()->isIntOrIntVectorTy() && "integer type expected");
    QualType Ty = E->getArg(0)->getType();
    if (auto *VecTy = Ty->getAs<VectorType>())
      Ty = VecTy->getElementType();
    bool IsSigned = Ty->isSignedIntegerType();
    unsigned Opc;
    if (BuiltinIDIfNoAsmLabel == Builtin::BI__builtin_elementwise_add_sat)
      Opc = IsSigned ? llvm::Intrinsic::sadd_sat : llvm::Intrinsic::uadd_sat;
    else
      Opc = IsSigned ? llvm::Intrinsic::ssub_sat : llvm::Intrinsic::usub_sat;
    Result = Builder.CreateBinaryIntrinsic(Opc, Op0, Op1, nullptr, "elt.sat");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_elementwise_max: {
    Value *Op0 = genScalarExpr(E->getArg(0));
    Value *Op1 = genScalarExpr(E->getArg(1));
    Value *Result;
    if (Op0->getType()->isIntOrIntVectorTy()) {
      QualType Ty = E->getArg(0)->getType();
      if (auto *VecTy = Ty->getAs<VectorType>())
        Ty = VecTy->getElementType();
      Result = Builder.CreateBinaryIntrinsic(Ty->isSignedIntegerType()
                                                 ? llvm::Intrinsic::smax
                                                 : llvm::Intrinsic::umax,
                                             Op0, Op1, nullptr, "elt.max");
    } else
      Result = Builder.CreateMaxNum(Op0, Op1, "elt.max");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_elementwise_min: {
    Value *Op0 = genScalarExpr(E->getArg(0));
    Value *Op1 = genScalarExpr(E->getArg(1));
    Value *Result;
    if (Op0->getType()->isIntOrIntVectorTy()) {
      QualType Ty = E->getArg(0)->getType();
      if (auto *VecTy = Ty->getAs<VectorType>())
        Ty = VecTy->getElementType();
      Result = Builder.CreateBinaryIntrinsic(Ty->isSignedIntegerType()
                                                 ? llvm::Intrinsic::smin
                                                 : llvm::Intrinsic::umin,
                                             Op0, Op1, nullptr, "elt.min");
    } else
      Result = Builder.CreateMinNum(Op0, Op1, "elt.min");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_reduce_max: {
    auto GetIntrinsicID = [](QualType QT) {
      if (auto *VecTy = QT->getAs<VectorType>())
        QT = VecTy->getElementType();
      if (QT->isSignedIntegerType())
        return llvm::Intrinsic::vector_reduce_smax;
      if (QT->isUnsignedIntegerType())
        return llvm::Intrinsic::vector_reduce_umax;
      assert(QT->isFloatingType() && "must have a float here");
      return llvm::Intrinsic::vector_reduce_fmax;
    };
    return RValue::get(emitUnaryBuiltin(
        *this, E, GetIntrinsicID(E->getArg(0)->getType()), "rdx.min"));
  }

  case Builtin::BI__builtin_reduce_min: {
    auto GetIntrinsicID = [](QualType QT) {
      if (auto *VecTy = QT->getAs<VectorType>())
        QT = VecTy->getElementType();
      if (QT->isSignedIntegerType())
        return llvm::Intrinsic::vector_reduce_smin;
      if (QT->isUnsignedIntegerType())
        return llvm::Intrinsic::vector_reduce_umin;
      assert(QT->isFloatingType() && "must have a float here");
      return llvm::Intrinsic::vector_reduce_fmin;
    };

    return RValue::get(emitUnaryBuiltin(
        *this, E, GetIntrinsicID(E->getArg(0)->getType()), "rdx.min"));
  }

  case Builtin::BI__builtin_reduce_add:
    return RValue::get(emitUnaryBuiltin(
        *this, E, llvm::Intrinsic::vector_reduce_add, "rdx.add"));
  case Builtin::BI__builtin_reduce_mul:
    return RValue::get(emitUnaryBuiltin(
        *this, E, llvm::Intrinsic::vector_reduce_mul, "rdx.mul"));
  case Builtin::BI__builtin_reduce_xor:
    return RValue::get(emitUnaryBuiltin(
        *this, E, llvm::Intrinsic::vector_reduce_xor, "rdx.xor"));
  case Builtin::BI__builtin_reduce_or:
    return RValue::get(emitUnaryBuiltin(
        *this, E, llvm::Intrinsic::vector_reduce_or, "rdx.or"));
  case Builtin::BI__builtin_reduce_and:
    return RValue::get(emitUnaryBuiltin(
        *this, E, llvm::Intrinsic::vector_reduce_and, "rdx.and"));

  case Builtin::BI__builtin_matrix_transpose: {
    auto *MatrixTy = E->getArg(0)->getType()->castAs<ConstantMatrixType>();
    Value *MatValue = genScalarExpr(E->getArg(0));
    MatrixBuilder MB(Builder);
    Value *Result = MB.CreateMatrixTranspose(MatValue, MatrixTy->getNumRows(),
                                             MatrixTy->getNumColumns());
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_matrix_column_major_load: {
    MatrixBuilder MB(Builder);
    Value *Stride = genScalarExpr(E->getArg(3));
    const auto *ResultTy = E->getType()->getAs<ConstantMatrixType>();
    auto *PtrTy = E->getArg(0)->getType()->getAs<PointerType>();
    assert(PtrTy && "arg0 must be of pointer type");
    bool IsVolatile = PtrTy->getPointeeType().isVolatileQualified();

    Address Src = genPointerWithAlignment(E->getArg(0));
    Value *Result = MB.CreateColumnMajorLoad(
        Src.getElementType(), Src.getPointer(),
        Align(Src.getAlignment().getQuantity()), Stride, IsVolatile,
        ResultTy->getNumRows(), ResultTy->getNumColumns(), "matrix");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_matrix_column_major_store: {
    MatrixBuilder MB(Builder);
    Value *Matrix = genScalarExpr(E->getArg(0));
    Address Dst = genPointerWithAlignment(E->getArg(1));
    Value *Stride = genScalarExpr(E->getArg(2));

    const auto *MatrixTy = E->getArg(0)->getType()->getAs<ConstantMatrixType>();
    auto *PtrTy = E->getArg(1)->getType()->getAs<PointerType>();
    assert(PtrTy && "arg1 must be of pointer type");
    bool IsVolatile = PtrTy->getPointeeType().isVolatileQualified();

    Value *Result = MB.CreateColumnMajorStore(
        Matrix, Dst.getPointer(), Align(Dst.getAlignment().getQuantity()),
        Stride, IsVolatile, MatrixTy->getNumRows(), MatrixTy->getNumColumns());
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_isinf_sign: {
    // isinf_sign(x) -> fabs(x) == infinity ? (signbit(x) ? -1 : 1) : 0
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *Arg = genScalarExpr(E->getArg(0));
    Value *AbsArg = genFAbs(*this, Arg);
    Value *IsInf = Builder.CreateFCmpOEQ(
        AbsArg, ConstantFP::getInfinity(Arg->getType()), "isinf");
    Value *IsNeg = genSignBit(*this, Arg);

    llvm::Type *IntTy = convertType(E->getType());
    Value *Zero = Constant::getNullValue(IntTy);
    Value *One = ConstantInt::get(IntTy, 1);
    Value *NegativeOne = ConstantInt::get(IntTy, -1);
    Value *SignResult = Builder.CreateSelect(IsNeg, NegativeOne, One);
    Value *Result = Builder.CreateSelect(IsInf, SignResult, Zero);
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_flt_rounds: {
    Function *F = ME.getIntrinsic(Intrinsic::get_rounding);

    llvm::Type *ResultType = convertType(E->getType());
    Value *Result = Builder.CreateCall(F);
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ true, "cast");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_set_flt_rounds: {
    Function *F = ME.getIntrinsic(Intrinsic::set_rounding);

    Value *V = genScalarExpr(E->getArg(0));
    Builder.CreateCall(F, V);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_fpclassify: {
    FunctionEmitter::FPOptionsRAII FPOptsRAII(*this, E);
    Value *V = genScalarExpr(E->getArg(5));
    llvm::Type *Ty = convertType(E->getArg(5)->getType());

    BasicBlock *Begin = Builder.GetInsertBlock();
    BasicBlock *End = createBasicBlock("fpclassify_end", this->CurFn);
    Builder.SetInsertPoint(End);
    PHINode *Result = Builder.CreatePHI(convertType(E->getArg(0)->getType()), 4,
                                        "fpclassify_result");

    // if (V==0) return FP_ZERO
    Builder.SetInsertPoint(Begin);
    Value *IsZero =
        Builder.CreateFCmpOEQ(V, Constant::getNullValue(Ty), "iszero");
    Value *ZeroLiteral = genScalarExpr(E->getArg(4));
    BasicBlock *NotZero = createBasicBlock("fpclassify_not_zero", this->CurFn);
    Builder.CreateCondBr(IsZero, End, NotZero);
    Result->addIncoming(ZeroLiteral, Begin);

    // if (V != V) return FP_NAN
    Builder.SetInsertPoint(NotZero);
    Value *IsNan = Builder.CreateFCmpUNO(V, V, "cmp");
    Value *NanLiteral = genScalarExpr(E->getArg(0));
    BasicBlock *NotNan = createBasicBlock("fpclassify_not_nan", this->CurFn);
    Builder.CreateCondBr(IsNan, End, NotNan);
    Result->addIncoming(NanLiteral, NotZero);

    // if (fabs(V) == infinity) return FP_INFINITY
    Builder.SetInsertPoint(NotNan);
    Value *VAbs = genFAbs(*this, V);
    Value *IsInf = Builder.CreateFCmpOEQ(
        VAbs, ConstantFP::getInfinity(V->getType()), "isinf");
    Value *InfLiteral = genScalarExpr(E->getArg(1));
    BasicBlock *NotInf = createBasicBlock("fpclassify_not_inf", this->CurFn);
    Builder.CreateCondBr(IsInf, End, NotInf);
    Result->addIncoming(InfLiteral, NotNan);

    // if (fabs(V) >= MIN_NORMAL) return FP_NORMAL else FP_SUBNORMAL
    Builder.SetInsertPoint(NotInf);
    APFloat Smallest = APFloat::getSmallestNormalized(
        getContext().getFloatTypeSemantics(E->getArg(5)->getType()));
    Value *IsNormal = Builder.CreateFCmpUGE(
        VAbs, ConstantFP::get(V->getContext(), Smallest), "isnormal");
    Value *NormalResult = Builder.CreateSelect(
        IsNormal, genScalarExpr(E->getArg(2)), genScalarExpr(E->getArg(3)));
    Builder.CreateBr(End);
    Result->addIncoming(NormalResult, NotInf);

    // return Result
    Builder.SetInsertPoint(End);
    return RValue::get(Result);
  }

  // An alloca will always return a pointer to the alloca (stack) address
  // space. This address space need not be the same as the AST / Language
  // default (ordinary locals use the generic address space). At
  // the AST level this is handled within createTempAlloca et al., but for the
  // builtin / dynamic alloca we have to handle it here. We use an explicit cast
  // instead of passing an AS to CreateAlloca so as to not inhibit optimisation.
  case Builtin::BIalloca:
  case Builtin::BI_alloca:
  case Builtin::BI__builtin_alloca_uninitialized:
  case Builtin::BI__builtin_alloca: {
    Value *Size = genScalarExpr(E->getArg(0));
    const TargetInfo &TI = getContext().getTargetInfo();
    // The alignment of the alloca should correspond to __BIGGEST_ALIGNMENT__.
    const Align SuitableAlignmentInBytes =
        ME.getContext().toCharUnitsFromBits(TI.getSuitableAlign()).getAsAlign();
    AllocaInst *AI = Builder.CreateAlloca(Builder.getInt8Ty(), Size);
    AI->setAlignment(SuitableAlignmentInBytes);
    if (BuiltinID != Builtin::BI__builtin_alloca_uninitialized)
      initializeAlloca(*this, AI, Size, SuitableAlignmentInBytes);
    LangAS AAS = getASTAllocaAddressSpace();
    LangAS EAS = E->getType()->getPointeeType().getAddressSpace();
    if (AAS != EAS) {
      llvm::Type *Ty = ME.getTypes().convertType(E->getType());
      return RValue::get(
          getTargetHooks().performAddrSpaceCast(*this, AI, AAS, EAS, Ty));
    }
    return RValue::get(AI);
  }

  case Builtin::BI__builtin_alloca_with_align_uninitialized:
  case Builtin::BI__builtin_alloca_with_align: {
    Value *Size = genScalarExpr(E->getArg(0));
    Value *AlignmentInBitsValue = genScalarExpr(E->getArg(1));
    auto *AlignmentInBitsCI = cast<ConstantInt>(AlignmentInBitsValue);
    unsigned AlignmentInBits = AlignmentInBitsCI->getZExtValue();
    const Align AlignmentInBytes =
        ME.getContext().toCharUnitsFromBits(AlignmentInBits).getAsAlign();
    AllocaInst *AI = Builder.CreateAlloca(Builder.getInt8Ty(), Size);
    AI->setAlignment(AlignmentInBytes);
    if (BuiltinID != Builtin::BI__builtin_alloca_with_align_uninitialized)
      initializeAlloca(*this, AI, Size, AlignmentInBytes);
    LangAS AAS = getASTAllocaAddressSpace();
    LangAS EAS = E->getType()->getPointeeType().getAddressSpace();
    if (AAS != EAS) {
      llvm::Type *Ty = ME.getTypes().convertType(E->getType());
      return RValue::get(
          getTargetHooks().performAddrSpaceCast(*this, AI, AAS, EAS, Ty));
    }
    return RValue::get(AI);
  }

  case Builtin::BIbzero:
  case Builtin::BI__builtin_bzero: {
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Value *SizeVal = genScalarExpr(E->getArg(1));
    Builder.CreateMemSet(Dest, Builder.getInt8(0), SizeVal, false);
    return RValue::get(nullptr);
  }

  case Builtin::BIbcopy:
  case Builtin::BI__builtin_bcopy: {
    Address Src = genPointerWithAlignment(E->getArg(0));
    Address Dest = genPointerWithAlignment(E->getArg(1));
    Value *SizeVal = genScalarExpr(E->getArg(2));
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }

  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy:
  case Builtin::BImempcpy:
  case Builtin::BI__builtin_mempcpy: {
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Address Src = genPointerWithAlignment(E->getArg(1));
    Value *SizeVal = genScalarExpr(E->getArg(2));
    Builder.CreateMemCpy(Dest, Src, SizeVal, false);
    if (BuiltinID == Builtin::BImempcpy ||
        BuiltinID == Builtin::BI__builtin_mempcpy)
      return RValue::get(Builder.CreateInBoundsGEP(Dest.getElementType(),
                                                   Dest.getPointer(), SizeVal));
    else
      return RValue::get(Dest.getPointer());
  }

  case Builtin::BI__builtin_memcpy_inline: {
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Address Src = genPointerWithAlignment(E->getArg(1));
    uint64_t Size =
        E->getArg(2)->EvaluateKnownConstInt(getContext()).getZExtValue();
    Builder.CreateMemCpyInline(Dest, Src, Size);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_char_memchr:
    BuiltinID = Builtin::BI__builtin_memchr;
    break;

  case Builtin::BI__builtin___memcpy_chk: {
    // fold __builtin_memcpy_chk(x, y, cst1, cst2) to memcpy iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, ME.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, ME.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Address Src = genPointerWithAlignment(E->getArg(1));
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemCpy(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }

  case Builtin::BI__builtin___memmove_chk: {
    // fold __builtin_memmove_chk(x, y, cst1, cst2) to memmove iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, ME.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, ME.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Address Src = genPointerWithAlignment(E->getArg(1));
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }

  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove: {
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Address Src = genPointerWithAlignment(E->getArg(1));
    Value *SizeVal = genScalarExpr(E->getArg(2));
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset: {
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Value *ByteVal =
        Builder.CreateTrunc(genScalarExpr(E->getArg(1)), Builder.getInt8Ty());
    Value *SizeVal = genScalarExpr(E->getArg(2));
    Builder.CreateMemSet(Dest, ByteVal, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }
  case Builtin::BI__builtin_memset_inline: {
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Value *ByteVal =
        Builder.CreateTrunc(genScalarExpr(E->getArg(1)), Builder.getInt8Ty());
    uint64_t Size =
        E->getArg(2)->EvaluateKnownConstInt(getContext()).getZExtValue();
    Builder.CreateMemSetInline(Dest, ByteVal, Size);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin___memset_chk: {
    // fold __builtin_memset_chk(x, y, cst1, cst2) to memset iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, ME.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, ME.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = genPointerWithAlignment(E->getArg(0));
    Value *ByteVal =
        Builder.CreateTrunc(genScalarExpr(E->getArg(1)), Builder.getInt8Ty());
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemSet(Dest, ByteVal, SizeVal, false);
    return RValue::get(Dest.getPointer());
  }
  case Builtin::BI__builtin_wmemchr: {
    // The MSVC runtime library does not provide a definition of wmemchr, so we
    // need an inline implementation.
    if (!getTarget().getTriple().isOSMSVCRT())
      break;

    llvm::Type *WCharTy = convertType(getContext().WCharTy);
    Value *Str = genScalarExpr(E->getArg(0));
    Value *Chr = genScalarExpr(E->getArg(1));
    Value *Size = genScalarExpr(E->getArg(2));

    BasicBlock *Entry = Builder.GetInsertBlock();
    BasicBlock *CmpEq = createBasicBlock("wmemchr.eq");
    BasicBlock *Next = createBasicBlock("wmemchr.next");
    BasicBlock *Exit = createBasicBlock("wmemchr.exit");
    Value *SizeEq0 = Builder.CreateICmpEQ(Size, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(SizeEq0, Exit, CmpEq);

    genBlock(CmpEq);
    PHINode *StrPhi = Builder.CreatePHI(Str->getType(), 2);
    StrPhi->addIncoming(Str, Entry);
    PHINode *SizePhi = Builder.CreatePHI(SizeTy, 2);
    SizePhi->addIncoming(Size, Entry);
    CharUnits WCharAlign =
        getContext().getTypeAlignInChars(getContext().WCharTy);
    Value *StrCh = Builder.CreateAlignedLoad(WCharTy, StrPhi, WCharAlign);
    Value *FoundChr = Builder.CreateConstInBoundsGEP1_32(WCharTy, StrPhi, 0);
    Value *StrEqChr = Builder.CreateICmpEQ(StrCh, Chr);
    Builder.CreateCondBr(StrEqChr, Exit, Next);

    genBlock(Next);
    Value *NextStr = Builder.CreateConstInBoundsGEP1_32(WCharTy, StrPhi, 1);
    Value *NextSize = Builder.CreateSub(SizePhi, ConstantInt::get(SizeTy, 1));
    Value *NextSizeEq0 =
        Builder.CreateICmpEQ(NextSize, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(NextSizeEq0, Exit, CmpEq);
    StrPhi->addIncoming(NextStr, Next);
    SizePhi->addIncoming(NextSize, Next);

    genBlock(Exit);
    PHINode *Ret = Builder.CreatePHI(Str->getType(), 3);
    Ret->addIncoming(llvm::Constant::getNullValue(Str->getType()), Entry);
    Ret->addIncoming(llvm::Constant::getNullValue(Str->getType()), Next);
    Ret->addIncoming(FoundChr, CmpEq);
    return RValue::get(Ret);
  }
  case Builtin::BI__builtin_wmemcmp: {
    // The MSVC runtime library does not provide a definition of wmemcmp, so we
    // need an inline implementation.
    if (!getTarget().getTriple().isOSMSVCRT())
      break;

    llvm::Type *WCharTy = convertType(getContext().WCharTy);

    Value *Dst = genScalarExpr(E->getArg(0));
    Value *Src = genScalarExpr(E->getArg(1));
    Value *Size = genScalarExpr(E->getArg(2));

    BasicBlock *Entry = Builder.GetInsertBlock();
    BasicBlock *CmpGT = createBasicBlock("wmemcmp.gt");
    BasicBlock *CmpLT = createBasicBlock("wmemcmp.lt");
    BasicBlock *Next = createBasicBlock("wmemcmp.next");
    BasicBlock *Exit = createBasicBlock("wmemcmp.exit");
    Value *SizeEq0 = Builder.CreateICmpEQ(Size, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(SizeEq0, Exit, CmpGT);

    genBlock(CmpGT);
    PHINode *DstPhi = Builder.CreatePHI(Dst->getType(), 2);
    DstPhi->addIncoming(Dst, Entry);
    PHINode *SrcPhi = Builder.CreatePHI(Src->getType(), 2);
    SrcPhi->addIncoming(Src, Entry);
    PHINode *SizePhi = Builder.CreatePHI(SizeTy, 2);
    SizePhi->addIncoming(Size, Entry);
    CharUnits WCharAlign =
        getContext().getTypeAlignInChars(getContext().WCharTy);
    Value *DstCh = Builder.CreateAlignedLoad(WCharTy, DstPhi, WCharAlign);
    Value *SrcCh = Builder.CreateAlignedLoad(WCharTy, SrcPhi, WCharAlign);
    Value *DstGtSrc = Builder.CreateICmpUGT(DstCh, SrcCh);
    Builder.CreateCondBr(DstGtSrc, Exit, CmpLT);

    genBlock(CmpLT);
    Value *DstLtSrc = Builder.CreateICmpULT(DstCh, SrcCh);
    Builder.CreateCondBr(DstLtSrc, Exit, Next);

    genBlock(Next);
    Value *NextDst = Builder.CreateConstInBoundsGEP1_32(WCharTy, DstPhi, 1);
    Value *NextSrc = Builder.CreateConstInBoundsGEP1_32(WCharTy, SrcPhi, 1);
    Value *NextSize = Builder.CreateSub(SizePhi, ConstantInt::get(SizeTy, 1));
    Value *NextSizeEq0 =
        Builder.CreateICmpEQ(NextSize, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(NextSizeEq0, Exit, CmpGT);
    DstPhi->addIncoming(NextDst, Next);
    SrcPhi->addIncoming(NextSrc, Next);
    SizePhi->addIncoming(NextSize, Next);

    genBlock(Exit);
    PHINode *Ret = Builder.CreatePHI(IntTy, 4);
    Ret->addIncoming(ConstantInt::get(IntTy, 0), Entry);
    Ret->addIncoming(ConstantInt::get(IntTy, 1), CmpGT);
    Ret->addIncoming(ConstantInt::get(IntTy, -1), CmpLT);
    Ret->addIncoming(ConstantInt::get(IntTy, 0), Next);
    return RValue::get(Ret);
  }
  case Builtin::BI__builtin_dwarf_cfa: {
    // The offset in bytes from the first argument to the CFA.
    //
    // Why on earth is this in the frontend?  Is there any reason at
    // all that the backend can't reasonably determine this while
    // lowering llvm.eh.dwarf.cfa()?
    //
    int32_t Offset = 0;

    Function *F = ME.getIntrinsic(Intrinsic::eh_dwarf_cfa);
    return RValue::get(
        Builder.CreateCall(F, llvm::ConstantInt::get(Int32Ty, Offset)));
  }
  case Builtin::BI__builtin_return_address: {
    Value *Depth = ConstantEmitter(*this).emitAbstract(
        E->getArg(0), getContext().UnsignedIntTy);
    Function *F = ME.getIntrinsic(Intrinsic::returnaddress);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI_ReturnAddress: {
    Function *F = ME.getIntrinsic(Intrinsic::returnaddress);
    return RValue::get(Builder.CreateCall(F, Builder.getInt32(0)));
  }
  case Builtin::BI__builtin_frame_address: {
    Value *Depth = ConstantEmitter(*this).emitAbstract(
        E->getArg(0), getContext().UnsignedIntTy);
    Function *F = ME.getIntrinsic(Intrinsic::frameaddress, AllocaInt8PtrTy);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI__builtin_extract_return_addr: {
    Value *Address = genScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().decodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_frob_return_addr: {
    Value *Address = genScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().encodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_dwarf_sp_column: {
    llvm::IntegerType *Ty = cast<llvm::IntegerType>(convertType(E->getType()));
    int Column = getTargetHooks().getDwarfEHStackPointer(ME);
    if (Column == -1) {
      ME.errorUnsupported(E, "__builtin_dwarf_sp_column");
      return RValue::get(llvm::UndefValue::get(Ty));
    }
    return RValue::get(llvm::ConstantInt::get(Ty, Column, true));
  }
  case Builtin::BI__builtin_init_dwarf_reg_size_table: {
    Value *Address = genScalarExpr(E->getArg(0));
    if (getTargetHooks().initDwarfEHRegSizeTable(*this, Address))
      ME.errorUnsupported(E, "__builtin_init_dwarf_reg_size_table");
    return RValue::get(llvm::UndefValue::get(convertType(E->getType())));
  }
  case Builtin::BI__builtin_eh_return: {
    Value *Int = genScalarExpr(E->getArg(0));
    Value *Ptr = genScalarExpr(E->getArg(1));

    llvm::IntegerType *IntTy = cast<llvm::IntegerType>(Int->getType());
    assert((IntTy->getBitWidth() == 32 || IntTy->getBitWidth() == 64) &&
           "LLVM's __builtin_eh_return only supports 32- and 64-bit variants");
    Function *F =
        ME.getIntrinsic(IntTy->getBitWidth() == 32 ? Intrinsic::eh_return_i32
                                                   : Intrinsic::eh_return_i64);
    Builder.CreateCall(F, {Int, Ptr});
    Builder.CreateUnreachable();

    // We do need to preserve an insertion point.
    genBlock(createBasicBlock("builtin_eh_return.cont"));

    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_unwind_init: {
    Function *F = ME.getIntrinsic(Intrinsic::eh_unwind_init);
    Builder.CreateCall(F);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_extend_pointer: {
    // Extends a pointer to the size of an _Unwind_Word, which is
    // uint64_t on all platforms.  Generally this gets poked into a
    // register and eventually used as an address, so if the
    // addressing registers are wider than pointers and the platform
    // doesn't implicitly ignore high-order bits when doing
    // addressing, we need to make sure we zext / sext based on
    // the platform's expectations.
    //
    // See: http://gcc.gnu.org/ml/gcc-bugs/2002-02/msg00237.html

    Value *Ptr = genScalarExpr(E->getArg(0));
    Value *Result = Builder.CreatePtrToInt(Ptr, IntPtrTy, "extend.cast");

    // If that's 64 bits, we're done.
    if (IntPtrTy->getBitWidth() == 64)
      return RValue::get(Result);

    // Otherwise, ask the codegen data what to do.
    if (getTargetHooks().extendPointerWithSExt())
      return RValue::get(Builder.CreateSExt(Result, Int64Ty, "extend.sext"));
    else
      return RValue::get(Builder.CreateZExt(Result, Int64Ty, "extend.zext"));
  }
  case Builtin::BI__builtin_setjmp: {
    // Buffer is a void**.
    Address Buf = genPointerWithAlignment(E->getArg(0));

    Value *FrameAddr = Builder.CreateCall(
        ME.getIntrinsic(Intrinsic::frameaddress, AllocaInt8PtrTy),
        ConstantInt::get(Int32Ty, 0));
    Builder.CreateStore(FrameAddr, Buf);

    Value *StackAddr = Builder.CreateStackSave();
    assert(Buf.getPointer()->getType() == StackAddr->getType());

    Address StackSaveSlot = Builder.CreateConstInBoundsGEP(Buf, 2);
    Builder.CreateStore(StackAddr, StackSaveSlot);

    // Call LLVM's EH setjmp, which is lightweight.
    Function *F = ME.getIntrinsic(Intrinsic::eh_sjlj_setjmp);
    return RValue::get(Builder.CreateCall(F, Buf.getPointer()));
  }
  case Builtin::BI__builtin_longjmp: {
    Value *Buf = genScalarExpr(E->getArg(0));

    // Call LLVM's EH longjmp, which is lightweight.
    Builder.CreateCall(ME.getIntrinsic(Intrinsic::eh_sjlj_longjmp), Buf);

    // longjmp doesn't return; mark this as unreachable.
    Builder.CreateUnreachable();

    // We do need to preserve an insertion point.
    genBlock(createBasicBlock("longjmp.cont"));

    return RValue::get(nullptr);
  }
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_swap:
    llvm_unreachable("Shouldn't make it through sema");
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Add, E);
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Sub, E);
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Or, E);
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::And, E);
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Xor, E);
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Nand, E);

  // NeverC extensions: not overloaded yet.
  case Builtin::BI__sync_fetch_and_min:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Min, E);
  case Builtin::BI__sync_fetch_and_max:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Max, E);
  case Builtin::BI__sync_fetch_and_umin:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::UMin, E);
  case Builtin::BI__sync_fetch_and_umax:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::UMax, E);

  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    return genBinaryAtomicPost(*this, llvm::AtomicRMWInst::Add, E,
                               llvm::Instruction::Add);
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    return genBinaryAtomicPost(*this, llvm::AtomicRMWInst::Sub, E,
                               llvm::Instruction::Sub);
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    return genBinaryAtomicPost(*this, llvm::AtomicRMWInst::And, E,
                               llvm::Instruction::And);
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    return genBinaryAtomicPost(*this, llvm::AtomicRMWInst::Or, E,
                               llvm::Instruction::Or);
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    return genBinaryAtomicPost(*this, llvm::AtomicRMWInst::Xor, E,
                               llvm::Instruction::Xor);
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    return genBinaryAtomicPost(*this, llvm::AtomicRMWInst::Nand, E,
                               llvm::Instruction::And, true);

  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    return RValue::get(MakeAtomicCmpXchgValue(*this, E, false));

  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    return RValue::get(MakeAtomicCmpXchgValue(*this, E, true));

  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Xchg, E);

  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    return genBinaryAtomic(*this, llvm::AtomicRMWInst::Xchg, E);

  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16: {
    Address Ptr = CheckAtomicAlignment(*this, E);
    QualType ElTy = E->getArg(0)->getType()->getPointeeType();

    llvm::Type *ITy = llvm::IntegerType::get(getLLVMContext(),
                                             getContext().getTypeSize(ElTy));
    llvm::StoreInst *Store =
        Builder.CreateStore(llvm::Constant::getNullValue(ITy), Ptr);
    Store->setAtomic(llvm::AtomicOrdering::Release);
    return RValue::get(nullptr);
  }

  case Builtin::BI__sync_synchronize: {
    // Full sequentially-consistent fence (GNU __sync_synchronize); intended for
    // synchronization, not device I/O. The intrinsic is poorly specified: in
    // theory, there isn't any way to safely use it... but in practice, it
    // mostly works to use it with non-atomic loads and stores to get
    // acquire/release semantics.
    Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_nontemporal_load:
    return RValue::get(genNontemporalLoad(*this, E));
  case Builtin::BI__builtin_nontemporal_store:
    return RValue::get(genNontemporalStore(*this, E));
  case Builtin::BI__c11_atomic_is_lock_free:
  case Builtin::BI__atomic_is_lock_free: {
    // Call "bool __atomic_is_lock_free(size_t size, void *ptr)". For the
    // __c11 builtin, ptr is 0 (indicating a properly-aligned object), since
    // _Atomic(T) is always properly-aligned.
    const char *LibCallName = "__atomic_is_lock_free";
    CallArgList Args;
    Args.add(RValue::get(genScalarExpr(E->getArg(0))),
             getContext().getSizeType());
    if (BuiltinID == Builtin::BI__atomic_is_lock_free)
      Args.add(RValue::get(genScalarExpr(E->getArg(1))),
               getContext().VoidPtrTy);
    else
      Args.add(RValue::get(llvm::Constant::getNullValue(VoidPtrTy)),
               getContext().VoidPtrTy);
    const ABIFunctionInfo &FuncInfo =
        ME.getTypes().arrangeBuiltinFunctionCall(E->getType(), Args);
    llvm::FunctionType *FTy = ME.getTypes().GetFunctionType(FuncInfo);
    llvm::FunctionCallee Func = ME.createRuntimeFunction(FTy, LibCallName);
    return genCall(FuncInfo, FnCallee::forDirect(Func), ReturnValueSlot(),
                   Args);
  }

  case Builtin::BI__atomic_test_and_set: {
    // Look at the argument type to determine whether this is a volatile
    // operation. The parameter type is always volatile.
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();
    bool Volatile =
        PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();

    Address Ptr = genPointerWithAlignment(E->getArg(0)).withElementType(Int8Ty);

    Value *NewVal = Builder.getInt8(1);
    Value *Order = genScalarExpr(E->getArg(1));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      AtomicRMWInst *Result = nullptr;
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Monotonic);
        break;
      case 1: // memory_order_consume
      case 2: // memory_order_acquire
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Acquire);
        break;
      case 3: // memory_order_release
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Release);
        break;
      case 4: // memory_order_acq_rel

        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::AcquireRelease);
        break;
      case 5: // memory_order_seq_cst
        Result = Builder.CreateAtomicRMW(
            llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
            llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      }
      Result->setVolatile(Volatile);
      return RValue::get(Builder.CreateIsNotNull(Result, "tobool"));
    }

    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    llvm::BasicBlock *BBs[5] = {
        createBasicBlock("monotonic", CurFn),
        createBasicBlock("acquire", CurFn), createBasicBlock("release", CurFn),
        createBasicBlock("acqrel", CurFn), createBasicBlock("seqcst", CurFn)};
    llvm::AtomicOrdering Orders[5] = {
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Acquire,
        llvm::AtomicOrdering::Release, llvm::AtomicOrdering::AcquireRelease,
        llvm::AtomicOrdering::SequentiallyConsistent};

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, BBs[0]);

    Builder.SetInsertPoint(ContBB);
    PHINode *Result = Builder.CreatePHI(Int8Ty, 5, "was_set");

    for (unsigned i = 0; i < 5; ++i) {
      Builder.SetInsertPoint(BBs[i]);
      AtomicRMWInst *RMW = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg,
                                                   Ptr, NewVal, Orders[i]);
      RMW->setVolatile(Volatile);
      Result->addIncoming(RMW, BBs[i]);
      Builder.CreateBr(ContBB);
    }

    SI->addCase(Builder.getInt32(0), BBs[0]);
    SI->addCase(Builder.getInt32(1), BBs[1]);
    SI->addCase(Builder.getInt32(2), BBs[1]);
    SI->addCase(Builder.getInt32(3), BBs[2]);
    SI->addCase(Builder.getInt32(4), BBs[3]);
    SI->addCase(Builder.getInt32(5), BBs[4]);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(Builder.CreateIsNotNull(Result, "tobool"));
  }

  case Builtin::BI__atomic_clear: {
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();
    bool Volatile =
        PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();

    Address Ptr = genPointerWithAlignment(E->getArg(0));
    Ptr = Ptr.withElementType(Int8Ty);
    Value *NewVal = Builder.getInt8(0);
    Value *Order = genScalarExpr(E->getArg(1));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      StoreInst *Store = Builder.CreateStore(NewVal, Ptr, Volatile);
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        Store->setOrdering(llvm::AtomicOrdering::Monotonic);
        break;
      case 3: // memory_order_release
        Store->setOrdering(llvm::AtomicOrdering::Release);
        break;
      case 5: // memory_order_seq_cst
        Store->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      }
      return RValue::get(nullptr);
    }

    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    llvm::BasicBlock *BBs[3] = {createBasicBlock("monotonic", CurFn),
                                createBasicBlock("release", CurFn),
                                createBasicBlock("seqcst", CurFn)};
    llvm::AtomicOrdering Orders[3] = {
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Release,
        llvm::AtomicOrdering::SequentiallyConsistent};

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, BBs[0]);

    for (unsigned i = 0; i < 3; ++i) {
      Builder.SetInsertPoint(BBs[i]);
      StoreInst *Store = Builder.CreateStore(NewVal, Ptr, Volatile);
      Store->setOrdering(Orders[i]);
      Builder.CreateBr(ContBB);
    }

    SI->addCase(Builder.getInt32(0), BBs[0]);
    SI->addCase(Builder.getInt32(3), BBs[1]);
    SI->addCase(Builder.getInt32(5), BBs[2]);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(nullptr);
  }

  case Builtin::BI__atomic_thread_fence:
  case Builtin::BI__atomic_signal_fence:
  case Builtin::BI__c11_atomic_thread_fence:
  case Builtin::BI__c11_atomic_signal_fence: {
    llvm::SyncScope::ID SSID;
    if (BuiltinID == Builtin::BI__atomic_signal_fence ||
        BuiltinID == Builtin::BI__c11_atomic_signal_fence)
      SSID = llvm::SyncScope::SingleThread;
    else
      SSID = llvm::SyncScope::System;
    Value *Order = genScalarExpr(E->getArg(0));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        break;
      case 1: // memory_order_consume
      case 2: // memory_order_acquire
        Builder.CreateFence(llvm::AtomicOrdering::Acquire, SSID);
        break;
      case 3: // memory_order_release
        Builder.CreateFence(llvm::AtomicOrdering::Release, SSID);
        break;
      case 4: // memory_order_acq_rel
        Builder.CreateFence(llvm::AtomicOrdering::AcquireRelease, SSID);
        break;
      case 5: // memory_order_seq_cst
        Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, SSID);
        break;
      }
      return RValue::get(nullptr);
    }

    llvm::BasicBlock *AcquireBB, *ReleaseBB, *AcqRelBB, *SeqCstBB;
    AcquireBB = createBasicBlock("acquire", CurFn);
    ReleaseBB = createBasicBlock("release", CurFn);
    AcqRelBB = createBasicBlock("acqrel", CurFn);
    SeqCstBB = createBasicBlock("seqcst", CurFn);
    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, ContBB);

    Builder.SetInsertPoint(AcquireBB);
    Builder.CreateFence(llvm::AtomicOrdering::Acquire, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(1), AcquireBB);
    SI->addCase(Builder.getInt32(2), AcquireBB);

    Builder.SetInsertPoint(ReleaseBB);
    Builder.CreateFence(llvm::AtomicOrdering::Release, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(3), ReleaseBB);

    Builder.SetInsertPoint(AcqRelBB);
    Builder.CreateFence(llvm::AtomicOrdering::AcquireRelease, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(4), AcqRelBB);

    Builder.SetInsertPoint(SeqCstBB);
    Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(5), SeqCstBB);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl: {
    return RValue::get(
        Builder.CreateZExt(genSignBit(*this, genScalarExpr(E->getArg(0))),
                           convertType(E->getType())));
  }
  case Builtin::BI__warn_memset_zero_len:
    return RValue::getIgnored();
  case Builtin::BI__annotation:
    return RValue::getIgnored();
  case Builtin::BI__builtin_annotation: {
    llvm::Value *AnnVal = genScalarExpr(E->getArg(0));
    llvm::Function *F = ME.getIntrinsic(
        llvm::Intrinsic::annotation, {AnnVal->getType(), ME.ConstGlobalsPtrTy});

    // Sema guarantees a non-wide string literal here, so cast<> is safe.
    const Expr *AnnotationStrExpr = E->getArg(1)->IgnoreParenCasts();
    llvm::StringRef Str = cast<StringLiteral>(AnnotationStrExpr)->getString();
    return RValue::get(
        genAnnotationCall(F, AnnVal, Str, E->getExprLoc(), nullptr));
  }
  case Builtin::BI__builtin_addcb:
  case Builtin::BI__builtin_addcs:
  case Builtin::BI__builtin_addc:
  case Builtin::BI__builtin_addcl:
  case Builtin::BI__builtin_addcll:
  case Builtin::BI__builtin_subcb:
  case Builtin::BI__builtin_subcs:
  case Builtin::BI__builtin_subc:
  case Builtin::BI__builtin_subcl:
  case Builtin::BI__builtin_subcll: {

    // We translate all of these builtins from expressions of the form:
    //   int x = ..., y = ..., carryin = ..., carryout, result;
    //   result = __builtin_addc(x, y, carryin, &carryout);
    //
    // to LLVM IR of the form:
    //
    //   %tmp1 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %x, i32 %y)
    //   %tmpsum1 = extractvalue {i32, i1} %tmp1, 0
    //   %carry1 = extractvalue {i32, i1} %tmp1, 1
    //   %tmp2 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %tmpsum1,
    //                                                       i32 %carryin)
    //   %result = extractvalue {i32, i1} %tmp2, 0
    //   %carry2 = extractvalue {i32, i1} %tmp2, 1
    //   %tmp3 = or i1 %carry1, %carry2
    //   %tmp4 = zext i1 %tmp3 to i32
    //   store i32 %tmp4, i32* %carryout

    // Scalarize our inputs.
    llvm::Value *X = genScalarExpr(E->getArg(0));
    llvm::Value *Y = genScalarExpr(E->getArg(1));
    llvm::Value *Carryin = genScalarExpr(E->getArg(2));
    Address CarryOutPtr = genPointerWithAlignment(E->getArg(3));

    // Decide if we are lowering to a uadd.with.overflow or usub.with.overflow.
    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unknown multiprecision builtin id.");
    case Builtin::BI__builtin_addcb:
    case Builtin::BI__builtin_addcs:
    case Builtin::BI__builtin_addc:
    case Builtin::BI__builtin_addcl:
    case Builtin::BI__builtin_addcll:
      IntrinsicId = llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_subcb:
    case Builtin::BI__builtin_subcs:
    case Builtin::BI__builtin_subc:
    case Builtin::BI__builtin_subcl:
    case Builtin::BI__builtin_subcll:
      IntrinsicId = llvm::Intrinsic::usub_with_overflow;
      break;
    }

    // Construct our resulting LLVM IR expression.
    llvm::Value *Carry1;
    llvm::Value *Sum1 = genOverflowIntrinsic(*this, IntrinsicId, X, Y, Carry1);
    llvm::Value *Carry2;
    llvm::Value *Sum2 =
        genOverflowIntrinsic(*this, IntrinsicId, Sum1, Carryin, Carry2);
    llvm::Value *CarryOut =
        Builder.CreateZExt(Builder.CreateOr(Carry1, Carry2), X->getType());
    Builder.CreateStore(CarryOut, CarryOutPtr);
    return RValue::get(Sum2);
  }

  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow: {
    const neverc::Expr *LeftArg = E->getArg(0);
    const neverc::Expr *RightArg = E->getArg(1);
    const neverc::Expr *ResultArg = E->getArg(2);

    neverc::QualType ResultQTy =
        ResultArg->getType()->castAs<PointerType>()->getPointeeType();

    WidthAndSignedness LeftInfo =
        getIntegerWidthAndSignedness(ME.getContext(), LeftArg->getType());
    WidthAndSignedness RightInfo =
        getIntegerWidthAndSignedness(ME.getContext(), RightArg->getType());
    WidthAndSignedness ResultInfo =
        getIntegerWidthAndSignedness(ME.getContext(), ResultQTy);

    // Handle mixed-sign multiplication as a special case, because adding
    // runtime or backend support for our generic irgen would be too expensive.
    if (isSpecialMixedSignMultiply(BuiltinID, LeftInfo, RightInfo, ResultInfo))
      return genCheckedMixedSignMultiply(*this, LeftArg, LeftInfo, RightArg,
                                         RightInfo, ResultArg, ResultQTy,
                                         ResultInfo);

    if (isSpecialUnsignedMultiplySignedResult(BuiltinID, LeftInfo, RightInfo,
                                              ResultInfo))
      return genCheckedUnsignedMultiplySignedResult(
          *this, LeftArg, LeftInfo, RightArg, RightInfo, ResultArg, ResultQTy,
          ResultInfo);

    WidthAndSignedness EncompassingInfo =
        EncompassingIntegerType({LeftInfo, RightInfo, ResultInfo});

    llvm::Type *EncompassingLLVMTy =
        llvm::IntegerType::get(ME.getLLVMContext(), EncompassingInfo.Width);

    llvm::Type *ResultLLVMTy = ME.getTypes().convertType(ResultQTy);

    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_add_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::sadd_with_overflow
                        : llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_sub_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::ssub_with_overflow
                        : llvm::Intrinsic::usub_with_overflow;
      break;
    case Builtin::BI__builtin_mul_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::smul_with_overflow
                        : llvm::Intrinsic::umul_with_overflow;
      break;
    }

    llvm::Value *Left = genScalarExpr(LeftArg);
    llvm::Value *Right = genScalarExpr(RightArg);
    Address ResultPtr = genPointerWithAlignment(ResultArg);

    // Extend each operand to the encompassing type.
    Left = Builder.CreateIntCast(Left, EncompassingLLVMTy, LeftInfo.Signed);
    Right = Builder.CreateIntCast(Right, EncompassingLLVMTy, RightInfo.Signed);

    // Perform the operation on the extended values.
    llvm::Value *Overflow, *Result;
    Result = genOverflowIntrinsic(*this, IntrinsicId, Left, Right, Overflow);

    if (EncompassingInfo.Width > ResultInfo.Width) {
      // The encompassing type is wider than the result type, so we need to
      // truncate it.
      llvm::Value *ResultTrunc = Builder.CreateTrunc(Result, ResultLLVMTy);

      // To see if the truncation caused an overflow, we will extend
      // the result and then compare it to the original result.
      llvm::Value *ResultTruncExt = Builder.CreateIntCast(
          ResultTrunc, EncompassingLLVMTy, ResultInfo.Signed);
      llvm::Value *TruncationOverflow =
          Builder.CreateICmpNE(Result, ResultTruncExt);

      Overflow = Builder.CreateOr(Overflow, TruncationOverflow);
      Result = ResultTrunc;
    }

    // Finally, store the result using the pointer.
    bool isVolatile =
        ResultArg->getType()->getPointeeType().isVolatileQualified();
    Builder.CreateStore(genToMemory(Result, ResultQTy), ResultPtr, isVolatile);

    return RValue::get(Overflow);
  }

  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow: {

    // We translate all of these builtins directly to the relevant llvm IR node.

    // Scalarize our inputs.
    llvm::Value *X = genScalarExpr(E->getArg(0));
    llvm::Value *Y = genScalarExpr(E->getArg(1));
    Address SumOutPtr = genPointerWithAlignment(E->getArg(2));

    // Decide which of the overflow intrinsics we are lowering to:
    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_uadd_overflow:
    case Builtin::BI__builtin_uaddl_overflow:
    case Builtin::BI__builtin_uaddll_overflow:
      IntrinsicId = llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_usub_overflow:
    case Builtin::BI__builtin_usubl_overflow:
    case Builtin::BI__builtin_usubll_overflow:
      IntrinsicId = llvm::Intrinsic::usub_with_overflow;
      break;
    case Builtin::BI__builtin_umul_overflow:
    case Builtin::BI__builtin_umull_overflow:
    case Builtin::BI__builtin_umulll_overflow:
      IntrinsicId = llvm::Intrinsic::umul_with_overflow;
      break;
    case Builtin::BI__builtin_sadd_overflow:
    case Builtin::BI__builtin_saddl_overflow:
    case Builtin::BI__builtin_saddll_overflow:
      IntrinsicId = llvm::Intrinsic::sadd_with_overflow;
      break;
    case Builtin::BI__builtin_ssub_overflow:
    case Builtin::BI__builtin_ssubl_overflow:
    case Builtin::BI__builtin_ssubll_overflow:
      IntrinsicId = llvm::Intrinsic::ssub_with_overflow;
      break;
    case Builtin::BI__builtin_smul_overflow:
    case Builtin::BI__builtin_smull_overflow:
    case Builtin::BI__builtin_smulll_overflow:
      IntrinsicId = llvm::Intrinsic::smul_with_overflow;
      break;
    }

    llvm::Value *Carry;
    llvm::Value *Sum = genOverflowIntrinsic(*this, IntrinsicId, X, Y, Carry);
    Builder.CreateStore(Sum, SumOutPtr);

    return RValue::get(Carry);
  }
  case Builtin::BI__builtin_function_start:
    return RValue::get(ME.getFunctionStart(
        E->getArg(0)->getAsBuiltinConstantDeclRef(ME.getContext())));
  case Builtin::BI__builtin_is_aligned:
    return genBuiltinIsAligned(E);
  case Builtin::BI__builtin_align_up:
    return genBuiltinAlignTo(E, true);
  case Builtin::BI__builtin_align_down:
    return genBuiltinAlignTo(E, false);

  case Builtin::BI__noop:
    // __noop always evaluates to an integer literal zero.
    return RValue::get(ConstantInt::get(IntTy, 0));
  case Builtin::BI__builtin_call_with_static_chain: {
    const CallExpr *Call = cast<CallExpr>(E->getArg(0));
    const Expr *Chain = E->getArg(1);
    return genCall(Call->getCallee()->getType(), genCallee(Call->getCallee()),
                   Call, ReturnValue, genScalarExpr(Chain));
  }
  case Builtin::BI_InterlockedExchange8:
  case Builtin::BI_InterlockedExchange16:
  case Builtin::BI_InterlockedExchange:
  case Builtin::BI_InterlockedExchangePointer:
    return RValue::get(genMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, E));
  case Builtin::BI_InterlockedCompareExchangePointer:
  case Builtin::BI_InterlockedCompareExchangePointer_nf: {
    llvm::Type *RTy;
    llvm::IntegerType *IntType = IntegerType::get(
        getLLVMContext(), getContext().getTypeSize(E->getType()));

    Address DestAddr = CheckAtomicAlignment(*this, E);

    llvm::Value *Exchange = genScalarExpr(E->getArg(1));
    RTy = Exchange->getType();
    Exchange = Builder.CreatePtrToInt(Exchange, IntType);

    llvm::Value *Comparand =
        Builder.CreatePtrToInt(genScalarExpr(E->getArg(2)), IntType);

    auto Ordering =
        BuiltinID == Builtin::BI_InterlockedCompareExchangePointer_nf
            ? AtomicOrdering::Monotonic
            : AtomicOrdering::SequentiallyConsistent;

    auto Result = Builder.CreateAtomicCmpXchg(DestAddr, Comparand, Exchange,
                                              Ordering, Ordering);
    Result->setVolatile(true);

    return RValue::get(
        Builder.CreateIntToPtr(Builder.CreateExtractValue(Result, 0), RTy));
  }
  case Builtin::BI_InterlockedCompareExchange8:
  case Builtin::BI_InterlockedCompareExchange16:
  case Builtin::BI_InterlockedCompareExchange:
  case Builtin::BI_InterlockedCompareExchange64:
    return RValue::get(genAtomicCmpXchgForMSIntrin(*this, E));
  case Builtin::BI_InterlockedIncrement16:
  case Builtin::BI_InterlockedIncrement:
    return RValue::get(
        genMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, E));
  case Builtin::BI_InterlockedDecrement16:
  case Builtin::BI_InterlockedDecrement:
    return RValue::get(
        genMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, E));
  case Builtin::BI_InterlockedAnd8:
  case Builtin::BI_InterlockedAnd16:
  case Builtin::BI_InterlockedAnd:
    return RValue::get(genMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, E));
  case Builtin::BI_InterlockedExchangeAdd8:
  case Builtin::BI_InterlockedExchangeAdd16:
  case Builtin::BI_InterlockedExchangeAdd:
    return RValue::get(
        genMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, E));
  case Builtin::BI_InterlockedExchangeSub8:
  case Builtin::BI_InterlockedExchangeSub16:
  case Builtin::BI_InterlockedExchangeSub:
    return RValue::get(
        genMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, E));
  case Builtin::BI_InterlockedOr8:
  case Builtin::BI_InterlockedOr16:
  case Builtin::BI_InterlockedOr:
    return RValue::get(genMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, E));
  case Builtin::BI_InterlockedXor8:
  case Builtin::BI_InterlockedXor16:
  case Builtin::BI_InterlockedXor:
    return RValue::get(genMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, E));

  case Builtin::BI_bittest64:
  case Builtin::BI_bittest:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandcomplement:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandreset:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_bittestandset:
  case Builtin::BI_interlockedbittestandreset:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
  case Builtin::BI_interlockedbittestandset:
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    return RValue::get(genBitTestIntrinsic(*this, BuiltinID, E));

    // These builtins exist to emit regular volatile loads and stores not
    // affected by the -fms-volatile setting.
  case Builtin::BI__iso_volatile_load8:
  case Builtin::BI__iso_volatile_load16:
  case Builtin::BI__iso_volatile_load32:
  case Builtin::BI__iso_volatile_load64:
    return RValue::get(genISOVolatileLoad(*this, E));
  case Builtin::BI__iso_volatile_store8:
  case Builtin::BI__iso_volatile_store16:
  case Builtin::BI__iso_volatile_store32:
  case Builtin::BI__iso_volatile_store64:
    return RValue::get(genISOVolatileStore(*this, E));

  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    return RValue::get(genSEHExceptionCode());
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    return RValue::get(genSEHExceptionInfo());
  case Builtin::BI__abnormal_termination:
  case Builtin::BI_abnormal_termination:
    return RValue::get(genSEHAbnormalTermination());
  case Builtin::BI_setjmpex:
    if (getTarget().getTriple().isOSMSVCRT() && E->getNumArgs() == 1 &&
        E->getArg(0)->getType()->isPointerType())
      return genMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmpex, E);
    break;
  case Builtin::BI_setjmp:
    if (getTarget().getTriple().isOSMSVCRT() && E->getNumArgs() == 1 &&
        E->getArg(0)->getType()->isPointerType()) {
      if (getTarget().getTriple().getArch() == llvm::Triple::aarch64)
        return genMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmpex, E);
      return genMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmp, E);
    }
    break;

  case Builtin::BI__fastfail:
    return RValue::get(genMSVCBuiltinExpr(MSVCIntrin::__fastfail, E));

  case Builtin::BIprintf:
    break;
  case Builtin::BI__builtin_canonicalize:
  case Builtin::BI__builtin_canonicalizef:
  case Builtin::BI__builtin_canonicalizef16:
  case Builtin::BI__builtin_canonicalizel:
    return RValue::get(emitUnaryBuiltin(*this, E, Intrinsic::canonicalize));

  case Builtin::BI__builtin_thread_pointer: {
    if (!getContext().getTargetInfo().isTLSSupported())
      ME.errorUnsupported(E, "__builtin_thread_pointer");
    // Fall through - it's already mapped to the intrinsic by the builtin def.
    break;
  }
  case Builtin::BI__builtin_os_log_format:
    return emitBuiltinOSLogFormat(*E);

  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_ms_va_end:
    return RValue::get(
        genVAStartEnd(genMSVAListRef(E->getArg(0)).getPointer(),
                      BuiltinID == Builtin::BI__builtin_ms_va_start));

  case Builtin::BI__builtin_ms_va_copy: {
    // Lower this manually. We can't reliably determine whether or not any
    // given va_copy() is for a Win64 va_list from the calling convention
    // alone, because it's legal to do this from a System V ABI function.
    // With opaque pointer types, we won't have enough information in LLVM
    // IR to determine this from the argument types, either. Best to do it
    // now, while we have enough information.
    Address DestAddr = genMSVAListRef(E->getArg(0));
    Address SrcAddr = genMSVAListRef(E->getArg(1));

    DestAddr = DestAddr.withElementType(Int8PtrTy);
    SrcAddr = SrcAddr.withElementType(Int8PtrTy);

    Value *ArgPtr = Builder.CreateLoad(SrcAddr, "ap.val");
    return RValue::get(Builder.CreateStore(ArgPtr, DestAddr));
  }
  }

  // If this is an alias for a lib function (e.g. __builtin_sin), emit
  // the call using the normal call path, but using the unmangled
  // version of the function name.
  if (getContext().BuiltinInfo.isLibFunction(BuiltinID))
    return emitLibraryCall(*this, FD, E,
                           ME.getBuiltinLibFunction(FD, BuiltinID));

  // If this is a predefined lib function (e.g. malloc), emit the call
  // using exactly the normal call path.
  if (getContext().BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return emitLibraryCall(*this, FD, E,
                           cast<llvm::Constant>(genScalarExpr(E->getCallee())));

  // This is down here to avoid non-target specific builtins, however, if
  // generic builtins start to require generic target features then we
  // can move this up to the beginning of the function.
  checkTargetFeatures(E, FD);

  if (unsigned VectorWidth =
          getContext().BuiltinInfo.getRequiredVectorWidth(BuiltinID))
    LargestVectorWidth = std::max(LargestVectorWidth, VectorWidth);

  // See if we have a target specific intrinsic.
  llvm::StringRef Name = getContext().BuiltinInfo.getName(BuiltinID);
  Intrinsic::ID IntrinsicID = Intrinsic::not_intrinsic;
  llvm::StringRef Prefix =
      llvm::Triple::getArchTypePrefix(getTarget().getTriple().getArch());
  if (!Prefix.empty()) {
    IntrinsicID = Intrinsic::getIntrinsicForClangBuiltin(Prefix.data(), Name);
    // NOTE we don't need to perform a compatibility flag check here since the
    // intrinsics are declared in Builtins*.def via LANGBUILTIN which filter the
    // MS builtins via ALL_MS_LANGUAGES and are filtered earlier.
    if (IntrinsicID == Intrinsic::not_intrinsic)
      IntrinsicID = Intrinsic::getIntrinsicForMSBuiltin(Prefix.data(), Name);
  }

  if (IntrinsicID != Intrinsic::not_intrinsic) {
    llvm::SmallVector<Value *, 16> Args;

    // Find out if any arguments are required to be integer constant
    // expressions.
    unsigned ICEArguments = 0;
    TreeContext::GetBuiltinTypeError Error;
    getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
    assert(Error == TreeContext::GE_None && "Should not codegen an error");

    Function *F = ME.getIntrinsic(IntrinsicID);
    llvm::FunctionType *FTy = F->getFunctionType();

    for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
      Value *ArgValue = genScalarOrConstFoldImmArg(ICEArguments, i, E);
      // If the intrinsic arg type is different from the builtin arg type
      // we need to do a bit cast.
      llvm::Type *PTy = FTy->getParamType(i);
      if (PTy != ArgValue->getType()) {
        if (auto *PtrTy = dyn_cast<llvm::PointerType>(PTy)) {
          if (PtrTy->getAddressSpace() !=
              ArgValue->getType()->getPointerAddressSpace()) {
            ArgValue = Builder.CreateAddrSpaceCast(
                ArgValue, llvm::PointerType::get(getLLVMContext(),
                                                 PtrTy->getAddressSpace()));
          }
        }

        assert(PTy->canLosslesslyBitCastTo(FTy->getParamType(i)) &&
               "Must be able to losslessly bit cast to param");
        // Cast vector type (e.g., v256i32) to x86_amx, this only happen
        // in amx intrinsics.
        if (PTy->isX86_AMXTy())
          ArgValue = Builder.CreateIntrinsic(Intrinsic::x86_cast_vector_to_tile,
                                             {ArgValue->getType()}, {ArgValue});
        else
          ArgValue = Builder.CreateBitCast(ArgValue, PTy);
      }

      Args.push_back(ArgValue);
    }

    Value *V = Builder.CreateCall(F, Args);
    QualType BuiltinRetType = E->getType();

    llvm::Type *RetTy = VoidTy;
    if (!BuiltinRetType->isVoidType())
      RetTy = convertType(BuiltinRetType);

    if (RetTy != V->getType()) {
      if (auto *PtrTy = dyn_cast<llvm::PointerType>(RetTy)) {
        if (PtrTy->getAddressSpace() !=
            V->getType()->getPointerAddressSpace()) {
          V = Builder.CreateAddrSpaceCast(
              V, llvm::PointerType::get(getLLVMContext(),
                                        PtrTy->getAddressSpace()));
        }
      }

      assert(V->getType()->canLosslesslyBitCastTo(RetTy) &&
             "Must be able to losslessly bit cast result type");
      // Cast x86_amx to vector type (e.g., v256i32), this only happen
      // in amx intrinsics.
      if (V->getType()->isX86_AMXTy())
        V = Builder.CreateIntrinsic(Intrinsic::x86_cast_tile_to_vector, {RetTy},
                                    {V});
      else
        V = Builder.CreateBitCast(V, RetTy);
    }

    if (RetTy->isVoidTy())
      return RValue::get(nullptr);

    return RValue::get(V);
  }

  // Some target-specific builtins can have aggregate return values.
  // If the result is an aggregate, force ReturnValue to be non-null,
  // so that the target-specific emission code can always just emit into it.
  TypeEvaluationKind EvalKind = getEvaluationKind(E->getType());
  if (EvalKind == TEK_Aggregate && ReturnValue.isNull()) {
    Address DestPtr = createMemTemp(E->getType(), "agg.tmp");
    ReturnValue = ReturnValueSlot(DestPtr, false);
  }

  // Now see if we can emit a target-specific builtin.
  if (Value *V = genTargetBuiltinExpr(BuiltinID, E, ReturnValue)) {
    switch (EvalKind) {
    case TEK_Scalar:
      if (V->getType()->isVoidTy())
        return RValue::get(nullptr);
      return RValue::get(V);
    case TEK_Aggregate:
      return RValue::getAggregate(ReturnValue.getValue(),
                                  ReturnValue.isVolatile());
    case TEK_Complex:
      llvm_unreachable("No current target builtin returns complex");
    }
    llvm_unreachable("Bad evaluation kind in genBuiltinExpr");
  }

  errorUnsupported(E, "builtin function");

  // Unknown builtin, for now just dump it out and return undef.
  return getUndefRValue(E->getType());
}

namespace {
Value *genTargetArchBuiltinExpr(FunctionEmitter *FE, unsigned BuiltinID,
                                const CallExpr *E, ReturnValueSlot ReturnValue,
                                llvm::Triple::ArchType Arch) {
  switch (Arch) {
  case llvm::Triple::aarch64:
    return FE->genAArch64BuiltinExpr(BuiltinID, E, Arch);
  case llvm::Triple::x86_64:
    return FE->genX86BuiltinExpr(BuiltinID, E);
  default:
    return nullptr;
  }
}
} // namespace

Value *FunctionEmitter::genTargetBuiltinExpr(unsigned BuiltinID,
                                             const CallExpr *E,
                                             ReturnValueSlot ReturnValue) {
  return genTargetArchBuiltinExpr(this, BuiltinID, E, ReturnValue,
                                  getTarget().getTriple().getArch());
}

llvm::Value *FunctionEmitter::genScalarOrConstFoldImmArg(unsigned ICEArguments,
                                                         unsigned Idx,
                                                         const CallExpr *E) {
  llvm::Value *Arg = nullptr;
  if ((ICEArguments & (1 << Idx)) == 0) {
    Arg = genScalarExpr(E->getArg(Idx));
  } else {
    std::optional<llvm::APSInt> Result =
        E->getArg(Idx)->getIntegerConstantExpr(getContext());
    assert(Result && "Expected argument to be a constant");
    Arg = llvm::ConstantInt::get(getLLVMContext(), *Result);
  }
  return Arg;
}

namespace {
struct BuiltinAlignArgs {
  llvm::Value *Src = nullptr;
  llvm::Type *SrcType = nullptr;
  llvm::Value *Alignment = nullptr;
  llvm::Value *Mask = nullptr;
  llvm::IntegerType *IntType = nullptr;

  BuiltinAlignArgs(const CallExpr *E, FunctionEmitter &FE) {
    QualType AstType = E->getArg(0)->getType();
    if (AstType->isArrayType())
      Src = FE.genArrayToPointerDecay(E->getArg(0)).getPointer();
    else
      Src = FE.genScalarExpr(E->getArg(0));
    SrcType = Src->getType();
    if (SrcType->isPointerTy()) {
      IntType = IntegerType::get(
          FE.getLLVMContext(),
          FE.ME.getDataLayout().getIndexTypeSizeInBits(SrcType));
    } else {
      assert(SrcType->isIntegerTy());
      IntType = cast<llvm::IntegerType>(SrcType);
    }
    Alignment = FE.genScalarExpr(E->getArg(1));
    Alignment = FE.Builder.CreateZExtOrTrunc(Alignment, IntType, "alignment");
    auto *One = llvm::ConstantInt::get(IntType, 1);
    Mask = FE.Builder.CreateSub(Alignment, One, "mask");
  }
};
} // namespace

RValue FunctionEmitter::genBuiltinIsAligned(const CallExpr *E) {
  BuiltinAlignArgs Args(E, *this);
  llvm::Value *SrcAddress = Args.Src;
  if (Args.SrcType->isPointerTy())
    SrcAddress =
        Builder.CreateBitOrPointerCast(Args.Src, Args.IntType, "src_addr");
  return RValue::get(Builder.CreateICmpEQ(
      Builder.CreateAnd(SrcAddress, Args.Mask, "set_bits"),
      llvm::Constant::getNullValue(Args.IntType), "is_aligned"));
}

RValue FunctionEmitter::genBuiltinAlignTo(const CallExpr *E, bool AlignUp) {
  BuiltinAlignArgs Args(E, *this);
  llvm::Value *SrcForMask = Args.Src;
  if (AlignUp) {
    // When aligning up we have to first add the mask to ensure we go over the
    // next alignment value and then align down to the next valid multiple.
    // By adding the mask, we ensure that align_up on an already aligned
    // value will not change the value.
    if (Args.Src->getType()->isPointerTy()) {
      if (getLangOpts().isSignedOverflowDefined())
        SrcForMask =
            Builder.CreateGEP(Int8Ty, SrcForMask, Args.Mask, "over_boundary");
      else
        SrcForMask = genCheckedInBoundsGEP(Int8Ty, SrcForMask, Args.Mask,
                                           /*SignedIndices=*/true,
                                           /*isSubtraction=*/false,
                                           E->getExprLoc(), "over_boundary");
    } else {
      SrcForMask = Builder.CreateAdd(SrcForMask, Args.Mask, "over_boundary");
    }
  }
  // Invert the mask to only clear the lower bits.
  llvm::Value *InvertedMask = Builder.CreateNot(Args.Mask, "inverted_mask");
  llvm::Value *Result = nullptr;
  if (Args.Src->getType()->isPointerTy()) {
    Result = Builder.CreateIntrinsic(
        Intrinsic::ptrmask, {Args.SrcType, Args.IntType},
        {SrcForMask, InvertedMask}, nullptr, "aligned_result");
  } else {
    Result = Builder.CreateAnd(SrcForMask, InvertedMask, "aligned_result");
  }
  assert(Result->getType() == Args.SrcType);
  return RValue::get(Result);
}
