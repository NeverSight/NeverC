#include "neverc/Shellcode/IR/CompilerRtPass.h"
#include "ExtractorCommon.h"
#include "neverc/Shellcode/IR/ExternRewriter.h"
#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

Function *getOrCreateUDivMod(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I128 = Type::getInt128Ty(Ctx);
  Type *VoidTy = Type::getVoidTy(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy =
      FunctionType::get(VoidTy, {I128, I128, PtrTy, PtrTy}, false);
  Function *F = getOrCreateScHelper(M, ir::kUDivModTi4, FTy);
  if (!F->empty())
    return F;
  F->setOnlyAccessesArgMemory();
  F->addParamAttr(2, Attribute::NoCapture);
  F->addParamAttr(3, Attribute::NoCapture);

  Argument *Num = F->getArg(0);
  Argument *Den = F->getArg(1);
  Argument *RemOut = F->getArg(2);
  Argument *QuotOut = F->getArg(3);
  Num->setName("num");
  Den->setName("den");
  RemOut->setName("rem.out");
  QuotOut->setName("quot.out");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *DivZero = BasicBlock::Create(Ctx, "divzero", F);
  BasicBlock *Preheader = BasicBlock::Create(Ctx, "preheader", F);
  BasicBlock *Fast64 = BasicBlock::Create(Ctx, "fast64", F);
  BasicBlock *LoopHdr = BasicBlock::Create(Ctx, "loop.hdr", F);
  BasicBlock *LoopBody = BasicBlock::Create(Ctx, "loop.body", F);
  BasicBlock *DoSub = BasicBlock::Create(Ctx, "do.sub", F);
  BasicBlock *LoopLatch = BasicBlock::Create(Ctx, "loop.latch", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *Zero128 = ConstantInt::get(I128, 0);
  Value *One128 = ConstantInt::get(I128, 1);
  Value *Sixty4 = ConstantInt::get(I32, 64);
  Value *DenIsZero = B.CreateICmpEQ(Den, Zero128);
  auto *DzBr = B.CreateCondBr(DenIsZero, DivZero, Preheader);
  DzBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(DivZero);
  B.CreateStore(ConstantInt::get(I128, 0), RemOut);
  B.CreateStore(ConstantInt::getAllOnesValue(I128), QuotOut);
  B.CreateRetVoid();

  B.SetInsertPoint(Preheader);
  Value *NumHi128 = B.CreateLShr(Num, ConstantInt::get(I128, 64));
  Value *NumHi = B.CreateTrunc(NumHi128, I64, "num.hi");
  Value *NumLo = B.CreateTrunc(Num, I64, "num.lo");
  Value *DenHi128 = B.CreateLShr(Den, ConstantInt::get(I128, 64));
  Value *DenHi = B.CreateTrunc(DenHi128, I64, "den.hi");
  Value *DenLo = B.CreateTrunc(Den, I64, "den.lo");

  Value *HiIsZero = B.CreateICmpEQ(NumHi, ConstantInt::get(I64, 0));
  Value *DenHiIsZero = B.CreateICmpEQ(DenHi, ConstantInt::get(I64, 0));
  Value *BothFit64 = B.CreateAnd(HiIsZero, DenHiIsZero);
  BasicBlock *Pow2Check = BasicBlock::Create(Ctx, "pow2.check", F);
  auto *F64Br = B.CreateCondBr(BothFit64, Fast64, Pow2Check);
  F64Br->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  BasicBlock *Pow2Fast = BasicBlock::Create(Ctx, "pow2.fast", F);
  BasicBlock *SlowPrep = BasicBlock::Create(Ctx, "slow.prep", F);
  B.SetInsertPoint(Pow2Check);
  Value *DenMinus1 = B.CreateSub(Den, One128);
  Value *NotPow2 = B.CreateICmpNE(B.CreateAnd(Den, DenMinus1), Zero128);
  auto *P2Br = B.CreateCondBr(NotPow2, SlowPrep, Pow2Fast);
  P2Br->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(4, 1));

  B.SetInsertPoint(Pow2Fast);
  Value *P2Rem = B.CreateAnd(Num, DenMinus1);
  Value *DenLoNonZero = B.CreateICmpNE(DenLo, ConstantInt::get(I64, 0));
  Value *CtzLo =
      B.CreateIntrinsic(Intrinsic::cttz, {I64},
                        {DenLo, ConstantInt::getTrue(Ctx)}, nullptr, "ctz.lo");
  Value *CtzHi =
      B.CreateIntrinsic(Intrinsic::cttz, {I64},
                        {DenHi, ConstantInt::getTrue(Ctx)}, nullptr, "ctz.hi");
  Value *CtzLo32 = B.CreateTrunc(CtzLo, I32);
  Value *CtzHi32 = B.CreateTrunc(CtzHi, I32);
  Value *P2Shift = B.CreateSelect(
      DenLoNonZero, CtzLo32, B.CreateAdd(ConstantInt::get(I32, 64), CtzHi32),
      "p2.shift");

  BasicBlock *Pow2DenOne = BasicBlock::Create(Ctx, "pow2.den.one", F);
  BasicBlock *Pow2GenericShift =
      BasicBlock::Create(Ctx, "pow2.generic.shift", F);
  Value *ShiftIsZero = B.CreateICmpEQ(P2Shift, ConstantInt::get(I32, 0));
  B.CreateCondBr(ShiftIsZero, Pow2DenOne, Pow2GenericShift);

  B.SetInsertPoint(Pow2DenOne);
  B.CreateStore(Zero128, RemOut);
  B.CreateStore(Num, QuotOut);
  B.CreateRetVoid();

  B.SetInsertPoint(Pow2GenericShift);
  Value *P2SLt64 = B.CreateICmpULT(P2Shift, ConstantInt::get(I32, 64));
  Value *P2S64 = B.CreateZExt(P2Shift, I64);
  Value *P2Inv32 = B.CreateSub(ConstantInt::get(I32, 64), P2Shift);
  Value *P2Inv64 = B.CreateZExt(P2Inv32, I64);
  Value *P2SM32 = B.CreateSub(P2Shift, ConstantInt::get(I32, 64));
  Value *P2SM64 = B.CreateZExt(P2SM32, I64);
  Value *P2LoLt =
      B.CreateOr(B.CreateLShr(NumLo, P2S64), B.CreateShl(NumHi, P2Inv64));
  Value *P2HiLt = B.CreateLShr(NumHi, P2S64);
  Value *P2LoGe = B.CreateLShr(NumHi, P2SM64);
  Value *P2HiGe = ConstantInt::get(I64, 0);

  Value *P2QLo = B.CreateSelect(P2SLt64, P2LoLt, P2LoGe);
  Value *P2QHi = B.CreateSelect(P2SLt64, P2HiLt, P2HiGe);
  Value *P2QHi128 =
      B.CreateShl(B.CreateZExt(P2QHi, I128), ConstantInt::get(I128, 64));
  Value *P2QLo128 = B.CreateZExt(P2QLo, I128);
  Value *P2Quot = B.CreateOr(P2QHi128, P2QLo128);
  B.CreateStore(P2Rem, RemOut);
  B.CreateStore(P2Quot, QuotOut);
  B.CreateRetVoid();

  B.SetInsertPoint(Fast64);
  Value *QuotLo64 = B.CreateUDiv(NumLo, DenLo);
  Value *RemLo64 = B.CreateURem(NumLo, DenLo);
  B.CreateStore(B.CreateZExt(RemLo64, I128), RemOut);
  B.CreateStore(B.CreateZExt(QuotLo64, I128), QuotOut);
  B.CreateRetVoid();

  B.SetInsertPoint(SlowPrep);
  Value *ClzHi =
      B.CreateIntrinsic(Intrinsic::ctlz, {I64},
                        {NumHi, ConstantInt::getFalse(Ctx)}, nullptr, "clz.hi");
  Value *ClzLo =
      B.CreateIntrinsic(Intrinsic::ctlz, {I64},
                        {NumLo, ConstantInt::getFalse(Ctx)}, nullptr, "clz.lo");
  Value *ClzHi32 = B.CreateTrunc(ClzHi, I32);
  Value *ClzLo32 = B.CreateTrunc(ClzLo, I32);
  Value *TotalClz = B.CreateSelect(
      HiIsZero, B.CreateAdd(ConstantInt::get(I32, 64), ClzLo32), ClzHi32);
  Value *StartBit =
      B.CreateSub(ConstantInt::get(I32, 127), TotalClz, "start.bit");
  B.CreateBr(LoopHdr);

  B.SetInsertPoint(LoopHdr);
  PHINode *I = B.CreatePHI(I32, 2, "i");
  PHINode *Rem = B.CreatePHI(I128, 2, "rem");
  PHINode *Quot = B.CreatePHI(I128, 2, "quot");
  I->addIncoming(StartBit, SlowPrep);
  Rem->addIncoming(Zero128, SlowPrep);
  Quot->addIncoming(Zero128, SlowPrep);
  Value *IGEZero = B.CreateICmpSGE(I, ConstantInt::get(I32, 0));
  auto *LBr = B.CreateCondBr(IGEZero, LoopBody, Done);
  LBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(LoopBody);
  Value *IHigh = B.CreateICmpSGE(I, Sixty4);
  Value *IMinus64 = B.CreateSub(I, Sixty4);
  Value *ShHi = B.CreateZExt(IMinus64, I64);
  Value *ShLo = B.CreateZExt(I, I64);
  Value *BitHi64 =
      B.CreateAnd(B.CreateLShr(NumHi, ShHi), ConstantInt::get(I64, 1));
  Value *BitLo64 =
      B.CreateAnd(B.CreateLShr(NumLo, ShLo), ConstantInt::get(I64, 1));
  Value *Bit64 = B.CreateSelect(IHigh, BitHi64, BitLo64);
  Value *Bit128 = B.CreateZExt(Bit64, I128);
  Value *RemShifted = B.CreateShl(Rem, One128);
  Value *NewRemBase = B.CreateOr(RemShifted, Bit128);
  Value *NeedSub = B.CreateICmpUGE(NewRemBase, Den);
  auto *SBr = B.CreateCondBr(NeedSub, DoSub, LoopLatch);
  SBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 1));

  B.SetInsertPoint(DoSub);
  Value *SubRem = B.CreateSub(NewRemBase, Den);
  Value *QuotBitHi64 =
      B.CreateSelect(IHigh, B.CreateShl(ConstantInt::get(I64, 1), ShHi),
                     ConstantInt::get(I64, 0));
  Value *QuotBitLo64 =
      B.CreateSelect(IHigh, ConstantInt::get(I64, 0),
                     B.CreateShl(ConstantInt::get(I64, 1), ShLo));
  Value *QuotBitHi128 =
      B.CreateShl(B.CreateZExt(QuotBitHi64, I128), ConstantInt::get(I128, 64));
  Value *QuotBitLo128 = B.CreateZExt(QuotBitLo64, I128);
  Value *QuotBit = B.CreateOr(QuotBitHi128, QuotBitLo128);
  Value *NewQuot = B.CreateOr(Quot, QuotBit);
  B.CreateBr(LoopLatch);

  B.SetInsertPoint(LoopLatch);
  PHINode *RemNext = B.CreatePHI(I128, 2, "rem.next");
  PHINode *QuotNext = B.CreatePHI(I128, 2, "quot.next");
  RemNext->addIncoming(NewRemBase, LoopBody);
  RemNext->addIncoming(SubRem, DoSub);
  QuotNext->addIncoming(Quot, LoopBody);
  QuotNext->addIncoming(NewQuot, DoSub);
  Value *INext = B.CreateSub(I, ConstantInt::get(I32, 1));
  I->addIncoming(INext, LoopLatch);
  Rem->addIncoming(RemNext, LoopLatch);
  Quot->addIncoming(QuotNext, LoopLatch);
  B.CreateBr(LoopHdr);

  B.SetInsertPoint(Done);
  B.CreateStore(Rem, RemOut);
  B.CreateStore(Quot, QuotOut);
  B.CreateRetVoid();
  return F;
}

Function *getOrCreateUDivTi3(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I128 = Type::getInt128Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I128, {I128, I128}, false);
  Function *F = getOrCreateScHelper(M, ir::kUDivTi3, FTy);
  if (!F->empty())
    return F;

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *Rem = B.CreateAlloca(I128);
  Value *Quot = B.CreateAlloca(I128);
  Function *UDivMod = getOrCreateUDivMod(M);
  B.CreateCall(UDivMod, {F->getArg(0), F->getArg(1), Rem, Quot});
  Value *QuotVal = B.CreateLoad(I128, Quot);
  B.CreateRet(QuotVal);
  return F;
}

Function *getOrCreateUModTi3(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I128 = Type::getInt128Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I128, {I128, I128}, false);
  Function *F = getOrCreateScHelper(M, ir::kUModTi3, FTy);
  if (!F->empty())
    return F;

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *Rem = B.CreateAlloca(I128);
  Value *Quot = B.CreateAlloca(I128);
  Function *UDivMod = getOrCreateUDivMod(M);
  B.CreateCall(UDivMod, {F->getArg(0), F->getArg(1), Rem, Quot});
  Value *RemVal = B.CreateLoad(I128, Rem);
  B.CreateRet(RemVal);
  return F;
}

void emitAbsolute128(IRBuilder<> &B, Value *X, Value *&WasNeg, Value *&Abs) {
  Type *I128 = B.getInt128Ty();
  Value *Zero = ConstantInt::get(I128, 0);
  WasNeg = B.CreateICmpSLT(X, Zero);
  Value *Neg = B.CreateSub(Zero, X);
  Abs = B.CreateSelect(WasNeg, Neg, X);
}

Function *getOrCreateDivTi3(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I128 = Type::getInt128Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I128, {I128, I128}, false);
  Function *F = getOrCreateScHelper(M, ir::kDivTi3, FTy);
  if (!F->empty())
    return F;

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *AbsA;
  Value *AbsB;
  Value *NegA;
  Value *NegB;
  emitAbsolute128(B, F->getArg(0), NegA, AbsA);
  emitAbsolute128(B, F->getArg(1), NegB, AbsB);
  Function *UDiv = getOrCreateUDivTi3(M);
  Value *UQ = B.CreateCall(UDiv, {AbsA, AbsB});
  Value *ResultNeg = B.CreateXor(NegA, NegB);
  Value *NegQ = B.CreateSub(ConstantInt::get(I128, 0), UQ);
  Value *Q = B.CreateSelect(ResultNeg, NegQ, UQ);
  B.CreateRet(Q);
  return F;
}

Function *getOrCreateModTi3(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I128 = Type::getInt128Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I128, {I128, I128}, false);
  Function *F = getOrCreateScHelper(M, ir::kModTi3, FTy);
  if (!F->empty())
    return F;

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *AbsA;
  Value *AbsB;
  Value *NegA;
  Value *NegB;
  emitAbsolute128(B, F->getArg(0), NegA, AbsA);
  emitAbsolute128(B, F->getArg(1), NegB, AbsB);
  (void)NegB;
  Function *UMod = getOrCreateUModTi3(M);
  Value *UR = B.CreateCall(UMod, {AbsA, AbsB});
  Value *NegR = B.CreateSub(ConstantInt::get(I128, 0), UR);
  Value *R = B.CreateSelect(NegA, NegR, UR);
  B.CreateRet(R);
  return F;
}

Function *getOrCreateUDivModTi4(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I128 = Type::getInt128Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(I128, {I128, I128, PtrTy}, false);
  Function *F = getOrCreateScHelper(M, ir::kUDivModTi4Public, FTy);
  if (!F->empty())
    return F;

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *Quot = B.CreateAlloca(I128);
  Function *UDivMod = getOrCreateUDivMod(M);
  B.CreateCall(UDivMod, {F->getArg(0), F->getArg(1), F->getArg(2), Quot});
  Value *QuotVal = B.CreateLoad(I128, Quot);
  B.CreateRet(QuotVal);
  return F;
}

Function *createAShlTi3(Module &M);
Function *createLShrTi3(Module &M);
Function *createAShrTi3(Module &M);

struct CRTHelperBundle {
  Function *UDivTi3;
  Function *DivTi3;
  Function *UModTi3;
  Function *ModTi3;
  Function *UDivModTi4;
  Function *AShlTi3;
  Function *LShrTi3;
  Function *AShrTi3;
};

using CRTHelperFactory = Function *(*)(Module &);

struct CompilerRtExternBindEntry {
  StringRef Name;
  Function *CRTHelperBundle::*Slot;
  CRTHelperFactory Create;
};

constexpr CompilerRtExternBindEntry kCompilerRtExternBinds[] = {
#define NEVERC_COMPILER_RT_EXTERN_BIND(externName, helperSlot, factory)        \
  {#externName, &CRTHelperBundle::helperSlot, factory},
#include "neverc/Shellcode/Tables/CompilerRtExternBinds.def"
#undef NEVERC_COMPILER_RT_EXTERN_BIND
};

void collectCompilerRtExternDecls(
    const Module &M,
    SmallVectorImpl<std::pair<Function *, const CompilerRtExternBindEntry *>>
        &MatchedDecls) {
  for (const auto &E : kCompilerRtExternBinds) {
    Function *F = M.getFunction(E.Name);
    if (F && F->isDeclaration())
      MatchedDecls.push_back({F, &E});
  }
}

void ensureCRTSlot(Module &M, CRTHelperBundle &H,
                   Function *(CRTHelperBundle::*Slot),
                   CRTHelperFactory Create) {
  if (H.*Slot)
    return;
  H.*Slot = Create(M);
}

void ensureCRTSlotFromBind(Module &M, CRTHelperBundle &H,
                           const CompilerRtExternBindEntry *E) {
  if (H.*(E->Slot))
    return;
  H.*(E->Slot) = E->Create(M);
}

Function *getOrCreateShiftTi3(Module &M, StringRef Name,
                              Instruction::BinaryOps Opcode) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I128 = Type::getInt128Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I128, {I128, I32}, false);
  Function *F = getOrCreateScHelper(M, Name, FTy);
  if (!F->empty())
    return F;

  Argument *X = F->getArg(0);
  Argument *S = F->getArg(1);
  X->setName("x");
  S->setName("s");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *ShiftZero = BasicBlock::Create(Ctx, "shift.zero", F);
  BasicBlock *ShiftNonZero = BasicBlock::Create(Ctx, "shift.nonzero", F);
  BasicBlock *Lt64 = BasicBlock::Create(Ctx, "lt64", F);
  BasicBlock *Ge64 = BasicBlock::Create(Ctx, "ge64", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *Mask127 = ConstantInt::get(I32, 127);
  Value *SNorm = B.CreateAnd(S, Mask127, "s.norm");
  Value *IsZero = B.CreateICmpEQ(SNorm, ConstantInt::get(I32, 0));
  auto *SBr = B.CreateCondBr(IsZero, ShiftZero, ShiftNonZero);
  SBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 100));

  B.SetInsertPoint(ShiftZero);
  B.CreateRet(X);

  B.SetInsertPoint(ShiftNonZero);
  Value *XHi128 = B.CreateLShr(X, ConstantInt::get(I128, 64));
  Value *XHi = B.CreateTrunc(XHi128, I64, "x.hi");
  Value *XLo = B.CreateTrunc(X, I64, "x.lo");
  Value *SLt64 = B.CreateICmpULT(SNorm, ConstantInt::get(I32, 64));
  auto *SLBr = B.CreateCondBr(SLt64, Lt64, Ge64);
  SLBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Lt64);
  Value *S64 = B.CreateZExt(SNorm, I64);
  Value *InvS = B.CreateSub(ConstantInt::get(I32, 64), SNorm);
  Value *InvS64 = B.CreateZExt(InvS, I64);
  Value *ResHiLt = nullptr;
  Value *ResLoLt = nullptr;
  switch (Opcode) {
  case Instruction::Shl: {
    Value *HiShift = B.CreateShl(XHi, S64);
    Value *Carry = B.CreateLShr(XLo, InvS64);
    ResHiLt = B.CreateOr(HiShift, Carry);
    ResLoLt = B.CreateShl(XLo, S64);
    break;
  }
  case Instruction::LShr: {
    Value *LoShift = B.CreateLShr(XLo, S64);
    Value *Carry = B.CreateShl(XHi, InvS64);
    ResLoLt = B.CreateOr(LoShift, Carry);
    ResHiLt = B.CreateLShr(XHi, S64);
    break;
  }
  case Instruction::AShr: {
    Value *LoShift = B.CreateLShr(XLo, S64);
    Value *Carry = B.CreateShl(XHi, InvS64);
    ResLoLt = B.CreateOr(LoShift, Carry);
    ResHiLt = B.CreateAShr(XHi, S64);
    break;
  }
  default:
    llvm_unreachable("unsupported shift opcode");
  }
  Value *ResHiLt128 =
      B.CreateShl(B.CreateZExt(ResHiLt, I128), ConstantInt::get(I128, 64));
  Value *ResLoLt128 = B.CreateZExt(ResLoLt, I128);
  Value *ResLt = B.CreateOr(ResHiLt128, ResLoLt128);
  B.CreateBr(Done);

  B.SetInsertPoint(Ge64);
  Value *SMinus64 = B.CreateSub(SNorm, ConstantInt::get(I32, 64));
  Value *SMinus6464 = B.CreateZExt(SMinus64, I64);
  Value *ResHiGe = nullptr;
  Value *ResLoGe = nullptr;
  switch (Opcode) {
  case Instruction::Shl:
    ResHiGe = B.CreateShl(XLo, SMinus6464);
    ResLoGe = ConstantInt::get(I64, 0);
    break;
  case Instruction::LShr:
    ResHiGe = ConstantInt::get(I64, 0);
    ResLoGe = B.CreateLShr(XHi, SMinus6464);
    break;
  case Instruction::AShr: {
    Value *SignFill = B.CreateAShr(XHi, ConstantInt::get(I64, 63));
    ResHiGe = SignFill;
    ResLoGe = B.CreateAShr(XHi, SMinus6464);
    break;
  }
  default:
    llvm_unreachable("unsupported shift opcode");
  }
  Value *ResHiGe128 =
      B.CreateShl(B.CreateZExt(ResHiGe, I128), ConstantInt::get(I128, 64));
  Value *ResLoGe128 = B.CreateZExt(ResLoGe, I128);
  Value *ResGe = B.CreateOr(ResHiGe128, ResLoGe128);
  B.CreateBr(Done);

  B.SetInsertPoint(Done);
  PHINode *R = B.CreatePHI(I128, 2, "r");
  R->addIncoming(ResLt, Lt64);
  R->addIncoming(ResGe, Ge64);
  B.CreateRet(R);
  return F;
}

Function *createAShlTi3(Module &M) {
  return getOrCreateShiftTi3(M, ir::kAShlTi3, Instruction::Shl);
}
Function *createLShrTi3(Module &M) {
  return getOrCreateShiftTi3(M, ir::kLShrTi3, Instruction::LShr);
}
Function *createAShrTi3(Module &M) {
  return getOrCreateShiftTi3(M, ir::kAShrTi3, Instruction::AShr);
}

constexpr Function *CRTHelperBundle::*const kCRTCleanupSlots[] = {
    &CRTHelperBundle::UDivTi3,    &CRTHelperBundle::DivTi3,
    &CRTHelperBundle::UModTi3,    &CRTHelperBundle::ModTi3,
    &CRTHelperBundle::UDivModTi4, &CRTHelperBundle::AShlTi3,
    &CRTHelperBundle::LShrTi3,    &CRTHelperBundle::AShrTi3,
};

void syncDependentBundlePointers(Module &M, CRTHelperBundle &H) {
  if (H.DivTi3 && !H.UDivTi3)
    H.UDivTi3 = M.getFunction(ir::kUDivTi3);
  if (H.ModTi3 && !H.UModTi3)
    H.UModTi3 = M.getFunction(ir::kUModTi3);
}

bool rewriteExternDecls(
    Module &M, const CRTHelperBundle &H,
    ArrayRef<std::pair<Function *, const CompilerRtExternBindEntry *>>
        CachedDecls) {
  bool Changed = false;
  for (const auto &Entry : CachedDecls) {
    Function *Decl = Entry.first;
    Function *Helper = H.*(Entry.second->Slot);
    if (!Helper)
      continue;
    Changed |= neverc::shellcode::rewriteExternCalls(M, *Decl, *Helper);
  }
  return Changed;
}

bool rewriteCollectedOps(SmallVectorImpl<BinaryOperator *> &DivRemOps,
                         SmallVectorImpl<BinaryOperator *> &ShiftOps,
                         const CRTHelperBundle &H) {
  if (DivRemOps.empty() && ShiftOps.empty())
    return false;

  for (BinaryOperator *BO : DivRemOps) {
    IRBuilder<> B(BO);
    Function *Helper = nullptr;
    switch (BO->getOpcode()) {
    case Instruction::UDiv:
      Helper = H.UDivTi3;
      break;
    case Instruction::SDiv:
      Helper = H.DivTi3;
      break;
    case Instruction::URem:
      Helper = H.UModTi3;
      break;
    case Instruction::SRem:
      Helper = H.ModTi3;
      break;
    default:
      llvm_unreachable("pre-screened during collection");
    }
    Value *Call = B.CreateCall(Helper, {BO->getOperand(0), BO->getOperand(1)});
    BO->replaceAllUsesWith(Call);
    BO->eraseFromParent();
  }

  for (BinaryOperator *BO : ShiftOps) {
    IRBuilder<> B(BO);
    Function *Helper = nullptr;
    switch (BO->getOpcode()) {
    case Instruction::Shl:
      Helper = H.AShlTi3;
      break;
    case Instruction::LShr:
      Helper = H.LShrTi3;
      break;
    case Instruction::AShr:
      Helper = H.AShrTi3;
      break;
    default:
      llvm_unreachable("pre-screened during collection");
    }
    Value *ShiftAmt = BO->getOperand(1);
    if (!ShiftAmt->getType()->isIntegerTy(32))
      ShiftAmt = B.CreateZExtOrTrunc(ShiftAmt, B.getInt32Ty());
    Value *Call = B.CreateCall(Helper, {BO->getOperand(0), ShiftAmt});
    BO->replaceAllUsesWith(Call);
    BO->eraseFromParent();
  }

  return true;
}

} // namespace

PreservedAnalyses CompilerRtPass::run(Module &M, ModuleAnalysisManager &) {
  bool StampedProbe = false;
  for (Function &F : M) {
    if (!F.hasFnAttribute("no-stack-arg-probe")) {
      F.addFnAttr("no-stack-arg-probe");
      StampedProbe = true;
    }
  }
  static constexpr StringLiteral kStackProbeNames[] = {
#define NEVERC_NAME(name) #name,
#include "neverc/Shellcode/Tables/StackProbeNames.def"
#include "neverc/Shellcode/Tables/UserExtra_StackProbeNames.def"
#undef NEVERC_NAME
  };
  for (StringRef Name : kStackProbeNames) {
    if (Function *ChkF = M.getFunction(Name)) {
      if (ChkF->use_empty()) {
        ChkF->eraseFromParent();
        StampedProbe = true;
      }
    }
  }

  bool W_UDiv = false, W_SDiv = false, W_URem = false, W_SRem = false;
  bool W_Shl = false, W_LShr = false, W_AShr = false;
  SmallVector<BinaryOperator *, 8> AllDivRemOps;
  SmallVector<BinaryOperator *, 8> AllShiftOps;
  SmallVector<std::pair<Function *, const CompilerRtExternBindEntry *>, 8>
      MatchedExternDecls;
  collectCompilerRtExternDecls(M, MatchedExternDecls);
  const bool HasExternDecls = !MatchedExternDecls.empty();

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (isShellcodeInternalRuntimeName(F.getName()))
      continue;
    for (Instruction &I : instructions(F)) {
      auto *BO = dyn_cast<BinaryOperator>(&I);
      if (!BO || !BO->getType()->isIntegerTy(128))
        continue;
      switch (BO->getOpcode()) {
      case Instruction::UDiv:
        W_UDiv = true;
        AllDivRemOps.push_back(BO);
        break;
      case Instruction::SDiv:
        W_SDiv = true;
        AllDivRemOps.push_back(BO);
        break;
      case Instruction::URem:
        W_URem = true;
        AllDivRemOps.push_back(BO);
        break;
      case Instruction::SRem:
        W_SRem = true;
        AllDivRemOps.push_back(BO);
        break;
      case Instruction::Shl:
        if (!isa<ConstantInt>(BO->getOperand(1))) {
          W_Shl = true;
          AllShiftOps.push_back(BO);
        }
        break;
      case Instruction::LShr:
        if (!isa<ConstantInt>(BO->getOperand(1))) {
          W_LShr = true;
          AllShiftOps.push_back(BO);
        }
        break;
      case Instruction::AShr:
        if (!isa<ConstantInt>(BO->getOperand(1))) {
          W_AShr = true;
          AllShiftOps.push_back(BO);
        }
        break;
      default:
        break;
      }
    }
  }

  const bool AnyWideNeed =
      W_UDiv || W_SDiv || W_URem || W_SRem || W_Shl || W_LShr || W_AShr;
  if (!AnyWideNeed && !HasExternDecls)
    return StampedProbe ? PreservedAnalyses::none() : PreservedAnalyses::all();

  CRTHelperBundle H = {};
  if (AnyWideNeed) {
    if (W_UDiv)
      ensureCRTSlot(M, H, &CRTHelperBundle::UDivTi3, getOrCreateUDivTi3);
    if (W_SDiv)
      ensureCRTSlot(M, H, &CRTHelperBundle::DivTi3, getOrCreateDivTi3);
    if (W_URem)
      ensureCRTSlot(M, H, &CRTHelperBundle::UModTi3, getOrCreateUModTi3);
    if (W_SRem)
      ensureCRTSlot(M, H, &CRTHelperBundle::ModTi3, getOrCreateModTi3);
    if (W_Shl)
      ensureCRTSlot(M, H, &CRTHelperBundle::AShlTi3, createAShlTi3);
    if (W_LShr)
      ensureCRTSlot(M, H, &CRTHelperBundle::LShrTi3, createLShrTi3);
    if (W_AShr)
      ensureCRTSlot(M, H, &CRTHelperBundle::AShrTi3, createAShrTi3);
  }
  if (HasExternDecls) {
    for (const auto &Entry : MatchedExternDecls)
      ensureCRTSlotFromBind(M, H, Entry.second);
  }
  syncDependentBundlePointers(M, H);

  bool Changed =
      HasExternDecls ? rewriteExternDecls(M, H, MatchedExternDecls) : false;

  Changed |= rewriteCollectedOps(AllDivRemOps, AllShiftOps, H);

  bool AnyHelper = false;
  for (Function *CRTHelperBundle::*Slot : kCRTCleanupSlots)
    if (H.*Slot) {
      AnyHelper = true;
      break;
    }
  if (AnyHelper) {
    for (int Pass = 0; Pass < 2; ++Pass) {
      for (Function *CRTHelperBundle::*Slot : kCRTCleanupSlots) {
        Function *F = H.*Slot;
        if (F && F->use_empty()) {
          F->eraseFromParent();
          H.*Slot = nullptr;
        }
      }
    }
  }
  if (Function *Workhorse = M.getFunction(ir::kUDivModTi4)) {
    if (Workhorse->use_empty())
      Workhorse->eraseFromParent();
  }

  return (Changed || StampedProbe) ? PreservedAnalyses::none()
                                   : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
