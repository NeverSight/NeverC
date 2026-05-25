#include "neverc/Shellcode/IR/MemIntrinPass.h"
#include "neverc/Shellcode/IR/ExternRewriter.h"
#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "neverc/Shellcode/Pipeline/SymbolNames.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ModRef.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

struct NulDetectConstants {
  Constant *Lo01;
  Constant *Hi80;
};

NulDetectConstants getNulDetectConstants(LLVMContext &Ctx) {
  Type *I64 = Type::getInt64Ty(Ctx);
  return {ConstantInt::get(I64, 0x0101010101010101ULL),
          ConstantInt::get(I64, 0x8080808080808080ULL)};
}

Value *emitHasZeroByte(IRBuilder<> &B, Value *V, const NulDetectConstants &NDC,
                       const Twine &Name = "has.zero") {
  Value *Sub = B.CreateSub(V, NDC.Lo01);
  Value *NotV = B.CreateNot(V);
  return B.CreateAnd(B.CreateAnd(Sub, NotV), NDC.Hi80, Name);
}

void addReadOnlyBufAttrs(Function *F, unsigned ParamIdx) {
  F->addParamAttr(ParamIdx, Attribute::NoCapture);
  F->addParamAttr(ParamIdx, Attribute::ReadOnly);
  F->addParamAttr(ParamIdx, Attribute::NonNull);
  F->addParamAttr(ParamIdx, Attribute::NoFree);
}

void addWritableBufAttrs(Function *F, unsigned ParamIdx) {
  F->addParamAttr(ParamIdx, Attribute::NoCapture);
  F->addParamAttr(ParamIdx, Attribute::NonNull);
  F->addParamAttr(ParamIdx, Attribute::NoFree);
}

void setStringPureCopyAttrs(Function *F) {
  F->addParamAttr(0, Attribute::NoAlias);
  F->addParamAttr(0, Attribute::WriteOnly);
  F->addParamAttr(0, Attribute::Returned);
  addWritableBufAttrs(F, 0);
  F->addParamAttr(1, Attribute::NoAlias);
  addReadOnlyBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
}

void setStringAppendAttrs(Function *F) {
  F->addParamAttr(0, Attribute::NoAlias);
  F->addParamAttr(0, Attribute::Returned);
  addWritableBufAttrs(F, 0);
  F->addParamAttr(1, Attribute::NoAlias);
  addReadOnlyBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
}

Function *getOrCreateMemCpy(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kMemCpy, FTy);
  if (!F->empty())
    return F;
  F->addParamAttr(0, Attribute::NoAlias);
  F->addParamAttr(0, Attribute::WriteOnly);
  F->addParamAttr(0, Attribute::Returned);
  addWritableBufAttrs(F, 0);
  F->addParamAttr(1, Attribute::NoAlias);
  addReadOnlyBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
  Argument *Dst = F->getArg(0);
  Argument *Src = F->getArg(1);
  Argument *N = F->getArg(2);
  Dst->setName("dst");
  Src->setName("src");
  N->setName("n");

  Constant *Zero64 = ConstantInt::get(I64, 0);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *Wide16 = BasicBlock::Create(Ctx, "wide16", F);
  BasicBlock *Mid8Test = BasicBlock::Create(Ctx, "mid8.test", F);
  BasicBlock *Mid8 = BasicBlock::Create(Ctx, "mid8", F);
  BasicBlock *TailTest = BasicBlock::Create(Ctx, "tail.test", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Done, Prep);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Prep);
  Value *NPairs = B.CreateLShr(N, ConstantInt::get(I64, 4));
  Value *HasPairs = B.CreateICmpNE(NPairs, Zero64);
  B.CreateCondBr(HasPairs, Wide16, Mid8Test);

  B.SetInsertPoint(Wide16);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Prep);
  Value *WOff = B.CreateShl(WI, ConstantInt::get(I64, 4), "", /*HasNUW=*/true);
  Value *WSp0 = B.CreateInBoundsGEP(I8, Src, WOff);
  Value *WDp0 = B.CreateInBoundsGEP(I8, Dst, WOff);
  Value *WOff8 = B.CreateNUWAdd(WOff, ConstantInt::get(I64, 8));
  Value *WSp1 = B.CreateInBoundsGEP(I8, Src, WOff8);
  Value *WDp1 = B.CreateInBoundsGEP(I8, Dst, WOff8);
  Value *WV0 = B.CreateAlignedLoad(I64, WSp0, Align(1));
  Value *WV1 = B.CreateAlignedLoad(I64, WSp1, Align(1));
  B.CreateAlignedStore(WV0, WDp0, Align(1));
  B.CreateAlignedStore(WV1, WDp1, Align(1));
  Value *WINext = B.CreateNUWAdd(WI, ConstantInt::get(I64, 1));
  WI->addIncoming(WINext, Wide16);
  Value *WKeep = B.CreateICmpULT(WINext, NPairs);
  auto *WBr = B.CreateCondBr(WKeep, Wide16, Mid8Test);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Mid8Test);
  Value *Mid8Start = B.CreateShl(NPairs, ConstantInt::get(I64, 4), "mid8.start",
                                 /*HasNUW=*/true);
  Value *NChunks8 = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *HasMid8 = B.CreateTrunc(NChunks8, Type::getInt1Ty(Ctx));
  B.CreateCondBr(HasMid8, Mid8, TailTest);

  B.SetInsertPoint(Mid8);
  Value *MSp = B.CreateInBoundsGEP(I8, Src, Mid8Start);
  Value *MDp = B.CreateInBoundsGEP(I8, Dst, Mid8Start);
  Value *MV = B.CreateAlignedLoad(I64, MSp, Align(1));
  B.CreateAlignedStore(MV, MDp, Align(1));
  Value *Mid8End = B.CreateNUWAdd(Mid8Start, ConstantInt::get(I64, 8));
  B.CreateBr(TailTest);

  B.SetInsertPoint(TailTest);
  PHINode *TailStart = B.CreatePHI(I64, 2, "tail.start");
  TailStart->addIncoming(Mid8Start, Mid8Test);
  TailStart->addIncoming(Mid8End, Mid8);
  Value *HasTail = B.CreateICmpULT(TailStart, N);
  B.CreateCondBr(HasTail, Tail, Done);

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(TailStart, TailTest);
  Value *TSp = B.CreateInBoundsGEP(I8, Src, TI);
  Value *TDp = B.CreateInBoundsGEP(I8, Dst, TI);
  Value *TV = B.CreateAlignedLoad(I8, TSp, Align(1));
  B.CreateAlignedStore(TV, TDp, Align(1));
  Value *TINext = B.CreateNUWAdd(TI, ConstantInt::get(I64, 1));
  TI->addIncoming(TINext, Tail);
  Value *TKeep = B.CreateICmpULT(TINext, N);
  auto *TBr = B.CreateCondBr(TKeep, Tail, Done);
  TBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(4, 1));

  B.SetInsertPoint(Done);
  B.CreateRet(Dst);
  return F;
}

void buildWideFillLoop(Function *F, Value *Dst, Value *N, Value *WideVal,
                       Value *ByteVal) {
  LLVMContext &Ctx = F->getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *Wide16 = BasicBlock::Create(Ctx, "wide16", F);
  BasicBlock *Mid8Test = BasicBlock::Create(Ctx, "mid8.test", F);
  BasicBlock *Mid8 = BasicBlock::Create(Ctx, "mid8", F);
  BasicBlock *TailTest = BasicBlock::Create(Ctx, "tail.test", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(F->getContext());
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Done, Prep);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Prep);
  Value *NPairs = B.CreateLShr(N, ConstantInt::get(I64, 4));
  Value *HasPairs = B.CreateICmpNE(NPairs, Zero64);
  B.CreateCondBr(HasPairs, Wide16, Mid8Test);

  B.SetInsertPoint(Wide16);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Prep);
  Value *WOff = B.CreateShl(WI, ConstantInt::get(I64, 4), "", /*HasNUW=*/true);
  Value *WDp0 = B.CreateInBoundsGEP(I8, Dst, WOff);
  Value *WOff8 = B.CreateNUWAdd(WOff, ConstantInt::get(I64, 8));
  Value *WDp1 = B.CreateInBoundsGEP(I8, Dst, WOff8);
  B.CreateAlignedStore(WideVal, WDp0, Align(1));
  B.CreateAlignedStore(WideVal, WDp1, Align(1));
  Value *WINext = B.CreateNUWAdd(WI, ConstantInt::get(I64, 1));
  WI->addIncoming(WINext, Wide16);
  auto *WBr = B.CreateCondBr(B.CreateICmpULT(WINext, NPairs), Wide16, Mid8Test);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Mid8Test);
  Value *Mid8Start = B.CreateShl(NPairs, ConstantInt::get(I64, 4), "mid8.start",
                                 /*HasNUW=*/true);
  Value *NChunks8 = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *HasMid8 = B.CreateTrunc(NChunks8, Type::getInt1Ty(Ctx));
  B.CreateCondBr(HasMid8, Mid8, TailTest);

  B.SetInsertPoint(Mid8);
  Value *MDp = B.CreateInBoundsGEP(I8, Dst, Mid8Start);
  B.CreateAlignedStore(WideVal, MDp, Align(1));
  Value *Mid8End = B.CreateNUWAdd(Mid8Start, ConstantInt::get(I64, 8));
  B.CreateBr(TailTest);

  B.SetInsertPoint(TailTest);
  PHINode *TailStart = B.CreatePHI(I64, 2, "tail.start");
  TailStart->addIncoming(Mid8Start, Mid8Test);
  TailStart->addIncoming(Mid8End, Mid8);
  B.CreateCondBr(B.CreateICmpULT(TailStart, N), Tail, Done);

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(TailStart, TailTest);
  Value *TDp = B.CreateInBoundsGEP(I8, Dst, TI);
  B.CreateAlignedStore(ByteVal, TDp, Align(1));
  Value *TINext = B.CreateNUWAdd(TI, ConstantInt::get(I64, 1));
  TI->addIncoming(TINext, Tail);
  auto *TFBr = B.CreateCondBr(B.CreateICmpULT(TINext, N), Tail, Done);
  TFBr->setMetadata(LLVMContext::MD_prof,
                    MDBuilder(F->getContext()).createBranchWeights(4, 1));

  B.SetInsertPoint(Done);
}

Function *getOrCreateMemSet(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, I32, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kMemSet, FTy);
  if (!F->empty())
    return F;
  F->addParamAttr(0, Attribute::WriteOnly);
  F->addParamAttr(0, Attribute::Returned);
  addWritableBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  Argument *Dst = F->getArg(0);
  Argument *Val = F->getArg(1);
  Argument *N = F->getArg(2);
  Dst->setName("dst");
  Val->setName("val");
  N->setName("n");

  IRBuilder<> B(Ctx);
  B.SetInsertPoint(BasicBlock::Create(Ctx, "repl", F));
  Value *Byte = B.CreateTrunc(Val, I8);
  Value *Wide8 = B.CreateZExt(Byte, I64);
  Value *W1 = B.CreateOr(Wide8, B.CreateShl(Wide8, ConstantInt::get(I64, 8)));
  Value *W2 = B.CreateOr(W1, B.CreateShl(W1, ConstantInt::get(I64, 16)));
  Value *WideVal = B.CreateOr(W2, B.CreateShl(W2, ConstantInt::get(I64, 32)));

  buildWideFillLoop(F, Dst, N, WideVal, Byte);

  BasicBlock *ReplBB = &F->front();
  BasicBlock *LoopEntry = ReplBB->getNextNode();
  B.SetInsertPoint(ReplBB);
  B.CreateBr(LoopEntry);

  IRBuilder<> BD(&F->back());
  BD.CreateRet(Dst);
  return F;
}

Function *getOrCreateBZero(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *VoidTy = Type::getVoidTy(Ctx);
  FunctionType *FTy = FunctionType::get(VoidTy, {PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kBZero, FTy);
  if (!F->empty())
    return F;
  F->addParamAttr(0, Attribute::WriteOnly);
  addWritableBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  Argument *Dst = F->getArg(0);
  Argument *N = F->getArg(1);
  Dst->setName("dst");
  N->setName("n");

  buildWideFillLoop(F, Dst, N, ConstantInt::get(I64, 0),
                    ConstantInt::get(I8, 0));

  IRBuilder<> BD(&F->back());
  BD.CreateRetVoid();
  return F;
}

Function *getOrCreateMemMove(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kMemMove, FTy);
  if (!F->empty())
    return F;
  F->addParamAttr(0, Attribute::WriteOnly);
  F->addParamAttr(0, Attribute::Returned);
  addWritableBufAttrs(F, 0);
  addWritableBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
  Argument *Dst = F->getArg(0);
  Argument *Src = F->getArg(1);
  Argument *N = F->getArg(2);
  Dst->setName("dst");
  Src->setName("src");
  N->setName("n");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Dispatch = BasicBlock::Create(Ctx, "dispatch", F);

  BasicBlock *FwdPrep = BasicBlock::Create(Ctx, "fwd.prep", F);
  BasicBlock *FwdWide16 = BasicBlock::Create(Ctx, "fwd.wide16", F);
  BasicBlock *FwdMid8Test = BasicBlock::Create(Ctx, "fwd.mid8.test", F);
  BasicBlock *FwdMid8 = BasicBlock::Create(Ctx, "fwd.mid8", F);
  BasicBlock *FwdTailTest = BasicBlock::Create(Ctx, "fwd.tail.test", F);
  BasicBlock *FwdTail = BasicBlock::Create(Ctx, "fwd.tail", F);

  BasicBlock *BwdPrep = BasicBlock::Create(Ctx, "bwd.prep", F);
  BasicBlock *BwdWide16 = BasicBlock::Create(Ctx, "bwd.wide16", F);
  BasicBlock *BwdMid8Test = BasicBlock::Create(Ctx, "bwd.mid8.test", F);
  BasicBlock *BwdMid8 = BasicBlock::Create(Ctx, "bwd.mid8", F);
  BasicBlock *BwdTailTest = BasicBlock::Create(Ctx, "bwd.tail.test", F);
  BasicBlock *BwdTail = BasicBlock::Create(Ctx, "bwd.tail", F);

  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Done, Dispatch);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Dispatch);
  Value *SrcI = B.CreatePtrToInt(Src, I64);
  Value *DstI = B.CreatePtrToInt(Dst, I64);
  Value *Forwards = B.CreateICmpUGE(SrcI, DstI);
  B.CreateCondBr(Forwards, FwdPrep, BwdPrep);

  B.SetInsertPoint(FwdPrep);
  Value *FNPairs = B.CreateLShr(N, ConstantInt::get(I64, 4));
  Value *FHasPairs = B.CreateICmpNE(FNPairs, Zero64);
  B.CreateCondBr(FHasPairs, FwdWide16, FwdMid8Test);

  B.SetInsertPoint(FwdWide16);
  PHINode *FWI = B.CreatePHI(I64, 2, "fwi");
  FWI->addIncoming(Zero64, FwdPrep);
  Value *FWOff = B.CreateShl(FWI, ConstantInt::get(I64, 4), "",
                             /*HasNUW=*/true);
  Value *FWSp0 = B.CreateInBoundsGEP(I8, Src, FWOff);
  Value *FWDp0 = B.CreateInBoundsGEP(I8, Dst, FWOff);
  Value *FWOff8 = B.CreateNUWAdd(FWOff, ConstantInt::get(I64, 8));
  Value *FWSp1 = B.CreateInBoundsGEP(I8, Src, FWOff8);
  Value *FWDp1 = B.CreateInBoundsGEP(I8, Dst, FWOff8);
  Value *FWV0 = B.CreateAlignedLoad(I64, FWSp0, Align(1));
  Value *FWV1 = B.CreateAlignedLoad(I64, FWSp1, Align(1));
  B.CreateAlignedStore(FWV0, FWDp0, Align(1));
  B.CreateAlignedStore(FWV1, FWDp1, Align(1));
  Value *FWINext = B.CreateNUWAdd(FWI, One64);
  FWI->addIncoming(FWINext, FwdWide16);
  auto *FWBr =
      B.CreateCondBr(B.CreateICmpULT(FWINext, FNPairs), FwdWide16, FwdMid8Test);
  FWBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(FwdMid8Test);
  Value *FMid8Start = B.CreateShl(FNPairs, ConstantInt::get(I64, 4),
                                  "fmid8.start", /*HasNUW=*/true);
  Value *FNChunks8 = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *FHasMid8 = B.CreateTrunc(FNChunks8, Type::getInt1Ty(Ctx));
  B.CreateCondBr(FHasMid8, FwdMid8, FwdTailTest);

  B.SetInsertPoint(FwdMid8);
  Value *FMSp = B.CreateInBoundsGEP(I8, Src, FMid8Start);
  Value *FMDp = B.CreateInBoundsGEP(I8, Dst, FMid8Start);
  Value *FMV = B.CreateAlignedLoad(I64, FMSp, Align(1));
  B.CreateAlignedStore(FMV, FMDp, Align(1));
  Value *FMid8End = B.CreateNUWAdd(FMid8Start, ConstantInt::get(I64, 8));
  B.CreateBr(FwdTailTest);

  B.SetInsertPoint(FwdTailTest);
  PHINode *FTailStart = B.CreatePHI(I64, 2, "ftail.start");
  FTailStart->addIncoming(FMid8Start, FwdMid8Test);
  FTailStart->addIncoming(FMid8End, FwdMid8);
  B.CreateCondBr(B.CreateICmpULT(FTailStart, N), FwdTail, Done);

  B.SetInsertPoint(FwdTail);
  PHINode *FTI = B.CreatePHI(I64, 2, "fti");
  FTI->addIncoming(FTailStart, FwdTailTest);
  Value *FTSp = B.CreateInBoundsGEP(I8, Src, FTI);
  Value *FTDp = B.CreateInBoundsGEP(I8, Dst, FTI);
  Value *FTV = B.CreateAlignedLoad(I8, FTSp, Align(1));
  B.CreateAlignedStore(FTV, FTDp, Align(1));
  Value *FTINext = B.CreateNUWAdd(FTI, One64);
  FTI->addIncoming(FTINext, FwdTail);
  auto *FTBr = B.CreateCondBr(B.CreateICmpULT(FTINext, N), FwdTail, Done);
  FTBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(4, 1));

  B.SetInsertPoint(BwdPrep);
  Value *BNPairs = B.CreateLShr(N, ConstantInt::get(I64, 4));
  Value *BHasPairs = B.CreateICmpNE(BNPairs, Zero64);
  B.CreateCondBr(BHasPairs, BwdWide16, BwdMid8Test);

  B.SetInsertPoint(BwdWide16);
  PHINode *BWI = B.CreatePHI(I64, 2, "bwi");
  BWI->addIncoming(Zero64, BwdPrep);
  Value *BWIPlus1 = B.CreateNUWAdd(BWI, One64);
  Value *BWOff = B.CreateSub(
      N, B.CreateShl(BWIPlus1, ConstantInt::get(I64, 4), "", /*HasNUW=*/true));
  Value *BWSp0 = B.CreateInBoundsGEP(I8, Src, BWOff);
  Value *BWDp0 = B.CreateInBoundsGEP(I8, Dst, BWOff);
  Value *BWOff8 = B.CreateNUWAdd(BWOff, ConstantInt::get(I64, 8));
  Value *BWSp1 = B.CreateInBoundsGEP(I8, Src, BWOff8);
  Value *BWDp1 = B.CreateInBoundsGEP(I8, Dst, BWOff8);
  Value *BWV0 = B.CreateAlignedLoad(I64, BWSp0, Align(1));
  Value *BWV1 = B.CreateAlignedLoad(I64, BWSp1, Align(1));
  B.CreateAlignedStore(BWV0, BWDp0, Align(1));
  B.CreateAlignedStore(BWV1, BWDp1, Align(1));
  BWI->addIncoming(BWIPlus1, BwdWide16);
  auto *BWBr = B.CreateCondBr(B.CreateICmpULT(BWIPlus1, BNPairs), BwdWide16,
                              BwdMid8Test);
  BWBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(BwdMid8Test);
  Value *BPairBytes = B.CreateShl(BNPairs, ConstantInt::get(I64, 4), "",
                                  /*HasNUW=*/true);
  Value *BTailEnd = B.CreateSub(N, BPairBytes, "btail.end");
  Value *BNChunks8 = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *BHasMid8 = B.CreateTrunc(BNChunks8, Type::getInt1Ty(Ctx));
  B.CreateCondBr(BHasMid8, BwdMid8, BwdTailTest);

  B.SetInsertPoint(BwdMid8);
  Value *BMid8Off = B.CreateSub(BTailEnd, ConstantInt::get(I64, 8));
  Value *BMSp = B.CreateInBoundsGEP(I8, Src, BMid8Off);
  Value *BMDp = B.CreateInBoundsGEP(I8, Dst, BMid8Off);
  Value *BMV = B.CreateAlignedLoad(I64, BMSp, Align(1));
  B.CreateAlignedStore(BMV, BMDp, Align(1));
  B.CreateBr(BwdTailTest);

  B.SetInsertPoint(BwdTailTest);
  PHINode *BTEnd = B.CreatePHI(I64, 2, "btend");
  BTEnd->addIncoming(BTailEnd, BwdMid8Test);
  BTEnd->addIncoming(BMid8Off, BwdMid8);
  B.CreateCondBr(B.CreateICmpUGT(BTEnd, Zero64), BwdTail, Done);

  B.SetInsertPoint(BwdTail);
  PHINode *BTI = B.CreatePHI(I64, 2, "bti");
  Value *BTIInit = B.CreateSub(BTEnd, One64);
  BTI->addIncoming(BTIInit, BwdTailTest);
  Value *BTSp = B.CreateInBoundsGEP(I8, Src, BTI);
  Value *BTDp = B.CreateInBoundsGEP(I8, Dst, BTI);
  Value *BTV = B.CreateAlignedLoad(I8, BTSp, Align(1));
  B.CreateAlignedStore(BTV, BTDp, Align(1));
  Value *BTKeep = B.CreateICmpUGT(BTI, Zero64);
  Value *BTINext = B.CreateSub(BTI, One64);
  BTI->addIncoming(BTINext, BwdTail);
  auto *BTBr = B.CreateCondBr(BTKeep, BwdTail, Done);
  BTBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(4, 1));

  B.SetInsertPoint(Done);
  B.CreateRet(Dst);
  return F;
}

Function *getOrCreateMemCmp(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(I32, {PtrTy, PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kMemCmp, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  addReadOnlyBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *A = F->getArg(0);
  Argument *BArg = F->getArg(1);
  Argument *N = F->getArg(2);
  A->setName("a");
  BArg->setName("b");
  N->setName("n");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *Wide16 = BasicBlock::Create(Ctx, "wide16", F);
  BasicBlock *Wide16Cont = BasicBlock::Create(Ctx, "wide16.cont", F);
  BasicBlock *Wide16Diff0 = BasicBlock::Create(Ctx, "wide16.diff0", F);
  BasicBlock *Wide16Check1 = BasicBlock::Create(Ctx, "wide16.check1", F);
  BasicBlock *Wide16Diff1 = BasicBlock::Create(Ctx, "wide16.diff1", F);
  BasicBlock *Mid8Test = BasicBlock::Create(Ctx, "mid8.test", F);
  BasicBlock *Mid8 = BasicBlock::Create(Ctx, "mid8", F);
  BasicBlock *Mid8Diff = BasicBlock::Create(Ctx, "mid8.diff", F);
  BasicBlock *TailTest = BasicBlock::Create(Ctx, "tail.test", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *TailCont = BasicBlock::Create(Ctx, "tail.cont", F);
  BasicBlock *Diff = BasicBlock::Create(Ctx, "diff", F);
  BasicBlock *Eq = BasicBlock::Create(Ctx, "eq", F);

  Constant *Zero64 = ConstantInt::get(I64, 0);

  IRBuilder<> B(Entry);

  auto emitChunkDiff = [&](Value *AV, Value *BV, Value *BaseOff) {
    Value *Xor = B.CreateXor(AV, BV);
    Value *BitPos =
        B.CreateIntrinsic(Intrinsic::cttz, {I64},
                          {Xor, ConstantInt::getTrue(Ctx)}, nullptr, "bit.pos");
    Value *ByteOff = B.CreateLShr(BitPos, ConstantInt::get(I64, 3), "byte.off");
    Value *AbsOff = B.CreateNUWAdd(BaseOff, ByteOff, "abs.off");
    Value *DAp = B.CreateInBoundsGEP(I8, A, AbsOff);
    Value *DBp = B.CreateInBoundsGEP(I8, BArg, AbsOff);
    Value *DAV = B.CreateAlignedLoad(I8, DAp, Align(1));
    Value *DBV = B.CreateAlignedLoad(I8, DBp, Align(1));
    B.CreateRet(B.CreateSub(B.CreateZExt(DAV, I32), B.CreateZExt(DBV, I32)));
  };
  MDBuilder MDB(Ctx);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Eq, Prep);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Prep);
  Value *NPairs = B.CreateLShr(N, ConstantInt::get(I64, 4));
  Value *HasPairs = B.CreateICmpNE(NPairs, Zero64);
  B.CreateCondBr(HasPairs, Wide16, Mid8Test);

  B.SetInsertPoint(Wide16);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Prep);
  Value *WOff = B.CreateShl(WI, ConstantInt::get(I64, 4), "", /*HasNUW=*/true);
  Value *WAp0 = B.CreateInBoundsGEP(I8, A, WOff);
  Value *WBp0 = B.CreateInBoundsGEP(I8, BArg, WOff);
  Value *WAV0 = B.CreateAlignedLoad(I64, WAp0, Align(1));
  Value *WBV0 = B.CreateAlignedLoad(I64, WBp0, Align(1));
  Value *WOff8 = B.CreateNUWAdd(WOff, ConstantInt::get(I64, 8));
  Value *WAp1 = B.CreateInBoundsGEP(I8, A, WOff8);
  Value *WBp1 = B.CreateInBoundsGEP(I8, BArg, WOff8);
  Value *WAV1 = B.CreateAlignedLoad(I64, WAp1, Align(1));
  Value *WBV1 = B.CreateAlignedLoad(I64, WBp1, Align(1));
  Value *WEq0 = B.CreateICmpEQ(WAV0, WBV0);
  auto *W0Br = B.CreateCondBr(WEq0, Wide16Check1, Wide16Diff0);
  W0Br->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Wide16Check1);
  Value *WEq1 = B.CreateICmpEQ(WAV1, WBV1);
  Value *WINext = B.CreateNUWAdd(WI, ConstantInt::get(I64, 1));
  WI->addIncoming(WINext, Wide16Cont);
  auto *W1Br = B.CreateCondBr(WEq1, Wide16Cont, Wide16Diff1);
  W1Br->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Wide16Cont);
  Value *WKeep = B.CreateICmpULT(WINext, NPairs);
  auto *WCBr = B.CreateCondBr(WKeep, Wide16, Mid8Test);
  WCBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Wide16Diff0);
  emitChunkDiff(WAV0, WBV0, WOff);

  B.SetInsertPoint(Wide16Diff1);
  emitChunkDiff(WAV1, WBV1, WOff8);

  B.SetInsertPoint(Mid8Test);
  Value *Mid8Start = B.CreateShl(NPairs, ConstantInt::get(I64, 4), "mid8.start",
                                 /*HasNUW=*/true);
  Value *NChunks8 = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *HasMid8 = B.CreateTrunc(NChunks8, Type::getInt1Ty(Ctx));
  B.CreateCondBr(HasMid8, Mid8, TailTest);

  B.SetInsertPoint(Mid8);
  Value *MAp = B.CreateInBoundsGEP(I8, A, Mid8Start);
  Value *MBp = B.CreateInBoundsGEP(I8, BArg, Mid8Start);
  Value *MAV = B.CreateAlignedLoad(I64, MAp, Align(1));
  Value *MBV = B.CreateAlignedLoad(I64, MBp, Align(1));
  Value *MEq = B.CreateICmpEQ(MAV, MBV);
  Value *Mid8End = B.CreateNUWAdd(Mid8Start, ConstantInt::get(I64, 8));
  B.CreateCondBr(MEq, TailTest, Mid8Diff);

  B.SetInsertPoint(Mid8Diff);
  emitChunkDiff(MAV, MBV, Mid8Start);

  B.SetInsertPoint(TailTest);
  PHINode *TailStart = B.CreatePHI(I64, 2, "tail.start");
  TailStart->addIncoming(Mid8Start, Mid8Test);
  TailStart->addIncoming(Mid8End, Mid8);
  Value *HasTail = B.CreateICmpULT(TailStart, N);
  B.CreateCondBr(HasTail, Tail, Eq);

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(TailStart, TailTest);
  Value *TAp = B.CreateInBoundsGEP(I8, A, TI);
  Value *TBp = B.CreateInBoundsGEP(I8, BArg, TI);
  Value *TAV = B.CreateAlignedLoad(I8, TAp, Align(1));
  Value *TBV = B.CreateAlignedLoad(I8, TBp, Align(1));
  Value *TEq = B.CreateICmpEQ(TAV, TBV);
  Value *TINext = B.CreateNUWAdd(TI, ConstantInt::get(I64, 1));
  TI->addIncoming(TINext, TailCont);
  B.CreateCondBr(TEq, TailCont, Diff);

  B.SetInsertPoint(TailCont);
  Value *TKeep = B.CreateICmpULT(TINext, N);
  auto *TCBr = B.CreateCondBr(TKeep, Tail, Eq);
  TCBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(4, 1));

  B.SetInsertPoint(Diff);
  Value *AZ = B.CreateZExt(TAV, I32);
  Value *BZ = B.CreateZExt(TBV, I32);
  Value *Delta = B.CreateSub(AZ, BZ);
  B.CreateRet(Delta);

  B.SetInsertPoint(Eq);
  B.CreateRet(ConstantInt::get(I32, 0));
  return F;
}

Function *getOrCreateStrLen(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(I64, {PtrTy}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrLen, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *S = F->getArg(0);
  S->setName("s");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  B.CreateBr(Wide);

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Entry);
  Value *WP = B.CreateInBoundsGEP(I8, S, WI);
  Value *WV = B.CreateAlignedLoad(I64, WP, Align(1));
  Value *HasZero = emitHasZeroByte(B, WV, NDC);
  Value *FoundZero = B.CreateICmpNE(HasZero, Zero64);
  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, Wide);
  auto *WBr = B.CreateCondBr(FoundZero, Done, Wide);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Done);
  Value *BitPos = B.CreateIntrinsic(Intrinsic::cttz, {I64},
                                    {HasZero, ConstantInt::getTrue(Ctx)},
                                    nullptr, "bit.pos");
  Value *ByteOff = B.CreateLShr(BitPos, ConstantInt::get(I64, 3), "byte.off");
  Value *Len = B.CreateNUWAdd(WI, ByteOff, "len");
  B.CreateRet(Len);
  return F;
}

Function *getOrCreateStrCpy(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, PtrTy}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrCpy, FTy);
  if (!F->empty())
    return F;
  setStringPureCopyAttrs(F);
  Argument *Dst = F->getArg(0);
  Argument *Src = F->getArg(1);
  Dst->setName("dst");
  Src->setName("src");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *WideCheck = BasicBlock::Create(Ctx, "wide.check", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  B.CreateBr(Wide);

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Entry);
  Value *WSp = B.CreateInBoundsGEP(I8, Src, WI);
  Value *WDp = B.CreateInBoundsGEP(I8, Dst, WI);
  Value *WV = B.CreateAlignedLoad(I64, WSp, Align(1));
  Value *HasZero = emitHasZeroByte(B, WV, NDC);
  Value *FoundZero = B.CreateICmpNE(HasZero, Zero64);
  auto *WBr = B.CreateCondBr(FoundZero, Tail, WideCheck);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(WideCheck);
  B.CreateAlignedStore(WV, WDp, Align(1));
  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, WideCheck);
  B.CreateBr(Wide);

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(WI, Wide);
  Value *TSp = B.CreateInBoundsGEP(I8, Src, TI);
  Value *TDp = B.CreateInBoundsGEP(I8, Dst, TI);
  Value *TV = B.CreateAlignedLoad(I8, TSp, Align(1));
  B.CreateAlignedStore(TV, TDp, Align(1));
  Value *IsEnd = B.CreateICmpEQ(TV, ConstantInt::get(I8, 0));
  Value *TINext = B.CreateNUWAdd(TI, ConstantInt::get(I64, 1));
  TI->addIncoming(TINext, Tail);
  auto *SCBr = B.CreateCondBr(IsEnd, Done, Tail);
  SCBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 8));

  B.SetInsertPoint(Done);
  B.CreateRet(Dst);
  return F;
}

Function *getOrCreateStrNCpy(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrNCpy, FTy);
  if (!F->empty())
    return F;
  setStringPureCopyAttrs(F);
  Argument *Dst = F->getArg(0);
  Argument *Src = F->getArg(1);
  Argument *N = F->getArg(2);
  Dst->setName("dst");
  Src->setName("src");
  N->setName("n");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *CopyWide = BasicBlock::Create(Ctx, "copy.wide", F);
  BasicBlock *CopyWideRead = BasicBlock::Create(Ctx, "copy.wide.read", F);
  BasicBlock *CopyWideOk = BasicBlock::Create(Ctx, "copy.wide.ok", F);
  BasicBlock *CopyTail = BasicBlock::Create(Ctx, "copy.tail", F);
  BasicBlock *CopyTailBody = BasicBlock::Create(Ctx, "copy.tail.body", F);
  BasicBlock *CopyTailKeep = BasicBlock::Create(Ctx, "copy.tail.keep", F);
  BasicBlock *PadPrep = BasicBlock::Create(Ctx, "pad.prep", F);
  BasicBlock *PadWide = BasicBlock::Create(Ctx, "pad.wide", F);
  BasicBlock *PadTail = BasicBlock::Create(Ctx, "pad.tail", F);
  BasicBlock *PadTailStore = BasicBlock::Create(Ctx, "pad.tail.store", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Done, CopyWide);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(CopyWide);
  PHINode *CWI = B.CreatePHI(I64, 2, "cwi");
  CWI->addIncoming(Zero64, Entry);
  Value *CWEnd = B.CreateNUWAdd(CWI, Eight64);
  Value *CWFits = B.CreateICmpULE(CWEnd, N);
  B.CreateCondBr(CWFits, CopyWideRead, CopyTail);

  B.SetInsertPoint(CopyWideRead);
  Value *CWSp = B.CreateInBoundsGEP(I8, Src, CWI);
  Value *CWV = B.CreateAlignedLoad(I64, CWSp, Align(1));
  Value *CHas = emitHasZeroByte(B, CWV, NDC, "chas.zero");
  Value *CFound = B.CreateICmpNE(CHas, Zero64);
  auto *CWBr = B.CreateCondBr(CFound, CopyTail, CopyWideOk);
  CWBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(CopyWideOk);
  Value *CWDp = B.CreateInBoundsGEP(I8, Dst, CWI);
  B.CreateAlignedStore(CWV, CWDp, Align(1));
  Value *CWINext = B.CreateNUWAdd(CWI, Eight64);
  CWI->addIncoming(CWINext, CopyWideOk);
  B.CreateBr(CopyWide);

  B.SetInsertPoint(CopyTail);
  PHINode *CTI = B.CreatePHI(I64, 3, "cti");
  CTI->addIncoming(CWI, CopyWide);
  CTI->addIncoming(CWI, CopyWideRead);
  Value *CTDone = B.CreateICmpUGE(CTI, N);
  B.CreateCondBr(CTDone, Done, CopyTailBody);

  B.SetInsertPoint(CopyTailBody);
  Value *CTSp = B.CreateInBoundsGEP(I8, Src, CTI);
  Value *CTDp = B.CreateInBoundsGEP(I8, Dst, CTI);
  Value *CTV = B.CreateAlignedLoad(I8, CTSp, Align(1));
  B.CreateAlignedStore(CTV, CTDp, Align(1));
  Value *CTEnd = B.CreateICmpEQ(CTV, ConstantInt::get(I8, 0));
  Value *CTINext = B.CreateNUWAdd(CTI, One64);
  B.CreateCondBr(CTEnd, PadPrep, CopyTailKeep);

  B.SetInsertPoint(CopyTailKeep);
  CTI->addIncoming(CTINext, CopyTailKeep);
  Value *CTKeep = B.CreateICmpULT(CTINext, N);
  auto *CTKBr = B.CreateCondBr(CTKeep, CopyTail, Done);
  CTKBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(4, 1));

  B.SetInsertPoint(PadPrep);
  Value *PadRemain = B.CreateSub(N, CTINext);
  Value *PadChunks = B.CreateLShr(PadRemain, ConstantInt::get(I64, 3));
  Value *HasPadChunks = B.CreateICmpNE(PadChunks, Zero64);
  B.CreateCondBr(HasPadChunks, PadWide, PadTail);

  B.SetInsertPoint(PadWide);
  PHINode *PWI = B.CreatePHI(I64, 2, "pwi");
  PWI->addIncoming(Zero64, PadPrep);
  Value *PWOff = B.CreateShl(PWI, ConstantInt::get(I64, 3), "",
                             /*HasNUW=*/true);
  Value *PWIdx = B.CreateNUWAdd(CTINext, PWOff);
  Value *PWDp = B.CreateInBoundsGEP(I8, Dst, PWIdx);
  B.CreateAlignedStore(ConstantInt::get(I64, 0), PWDp, Align(1));
  Value *PWINext = B.CreateNUWAdd(PWI, One64);
  PWI->addIncoming(PWINext, PadWide);
  Value *PWKeep = B.CreateICmpULT(PWINext, PadChunks);
  auto *PWBr = B.CreateCondBr(PWKeep, PadWide, PadTail);
  PWBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(PadTail);
  Value *PadChunkBytes = B.CreateShl(PadChunks, ConstantInt::get(I64, 3), "",
                                     /*HasNUW=*/true);
  Value *PTStart = B.CreateNUWAdd(CTINext, PadChunkBytes, "pt.start");
  Value *PTDone = B.CreateICmpUGE(PTStart, N);
  B.CreateCondBr(PTDone, Done, PadTailStore);

  B.SetInsertPoint(PadTailStore);
  PHINode *PTI = B.CreatePHI(I64, 2, "pti");
  PTI->addIncoming(PTStart, PadTail);
  Value *PTDp = B.CreateInBoundsGEP(I8, Dst, PTI);
  B.CreateAlignedStore(ConstantInt::get(I8, 0), PTDp, Align(1));
  Value *PTINext = B.CreateNUWAdd(PTI, One64);
  PTI->addIncoming(PTINext, PadTailStore);
  Value *PTKeep = B.CreateICmpULT(PTINext, N);
  B.CreateCondBr(PTKeep, PadTailStore, Done);

  B.SetInsertPoint(Done);
  B.CreateRet(Dst);
  return F;
}

Function *getOrCreateStrCmp(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(I32, {PtrTy, PtrTy}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrCmp, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  addReadOnlyBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *A = F->getArg(0);
  Argument *BArg = F->getArg(1);
  A->setName("a");
  BArg->setName("b");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *WideResolve = BasicBlock::Create(Ctx, "wide.resolve", F);
  BasicBlock *WideMismatch = BasicBlock::Create(Ctx, "wide.mismatch", F);
  BasicBlock *WideDiff = BasicBlock::Create(Ctx, "wide.diff", F);
  BasicBlock *Eq = BasicBlock::Create(Ctx, "eq", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  B.CreateBr(Wide);

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Entry);
  Value *WAp = B.CreateInBoundsGEP(I8, A, WI);
  Value *WBp = B.CreateInBoundsGEP(I8, BArg, WI);
  Value *WAV = B.CreateAlignedLoad(I64, WAp, Align(1));
  Value *WBV = B.CreateAlignedLoad(I64, WBp, Align(1));
  Value *WEq = B.CreateICmpEQ(WAV, WBV);
  Value *HasZero = emitHasZeroByte(B, WAV, NDC);
  Value *FoundZero = B.CreateICmpNE(HasZero, Zero64);
  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, Wide);
  Value *NeedResolve = B.CreateOr(B.CreateNot(WEq), FoundZero);
  auto *WBr = B.CreateCondBr(NeedResolve, WideResolve, Wide);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(WideResolve);
  B.CreateCondBr(WEq, Eq, WideMismatch);

  B.SetInsertPoint(WideMismatch);
  Value *Xor = B.CreateXor(WAV, WBV);
  Value *MismatchBit =
      B.CreateIntrinsic(Intrinsic::cttz, {I64},
                        {Xor, ConstantInt::getTrue(Ctx)}, nullptr, "mm.bit");
  Value *MismatchByte =
      B.CreateLShr(MismatchBit, ConstantInt::get(I64, 3), "mm.byte");
  Value *NulBit = B.CreateIntrinsic(Intrinsic::cttz, {I64},
                                    {HasZero, ConstantInt::getFalse(Ctx)},
                                    nullptr, "nul.bit");
  Value *NulByte = B.CreateLShr(NulBit, ConstantInt::get(I64, 3), "nul.byte");
  Value *NulFirst = B.CreateICmpULT(NulByte, MismatchByte);
  B.CreateCondBr(NulFirst, Eq, WideDiff);

  B.SetInsertPoint(WideDiff);
  Value *AbsOff = B.CreateNUWAdd(WI, MismatchByte, "abs.off");
  Value *DAp = B.CreateInBoundsGEP(I8, A, AbsOff);
  Value *DBp = B.CreateInBoundsGEP(I8, BArg, AbsOff);
  Value *DAV = B.CreateAlignedLoad(I8, DAp, Align(1));
  Value *DBV = B.CreateAlignedLoad(I8, DBp, Align(1));
  Value *DAZ = B.CreateZExt(DAV, I32);
  Value *DBZ = B.CreateZExt(DBV, I32);
  B.CreateRet(B.CreateSub(DAZ, DBZ));

  B.SetInsertPoint(Eq);
  B.CreateRet(ConstantInt::get(I32, 0));
  return F;
}

Function *getOrCreateStrNCmp(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(I32, {PtrTy, PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrNCmp, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  addReadOnlyBufAttrs(F, 1);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *A = F->getArg(0);
  Argument *BArg = F->getArg(1);
  Argument *N = F->getArg(2);
  A->setName("a");
  BArg->setName("b");
  N->setName("n");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *WideRead = BasicBlock::Create(Ctx, "wide.read", F);
  BasicBlock *WideResolve = BasicBlock::Create(Ctx, "wide.resolve", F);
  BasicBlock *WideMismatch = BasicBlock::Create(Ctx, "wide.mismatch", F);
  BasicBlock *WideDiff = BasicBlock::Create(Ctx, "wide.diff", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *TailBody = BasicBlock::Create(Ctx, "tail.body", F);
  BasicBlock *Diff = BasicBlock::Create(Ctx, "diff", F);
  BasicBlock *Cont = BasicBlock::Create(Ctx, "cont", F);
  BasicBlock *Eq = BasicBlock::Create(Ctx, "eq", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Eq, Wide);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Entry);
  Value *WEnd = B.CreateNUWAdd(WI, Eight64);
  Value *WFits = B.CreateICmpULE(WEnd, N);
  B.CreateCondBr(WFits, WideRead, Tail);

  B.SetInsertPoint(WideRead);
  Value *WAp = B.CreateInBoundsGEP(I8, A, WI);
  Value *WBp = B.CreateInBoundsGEP(I8, BArg, WI);
  Value *WAV = B.CreateAlignedLoad(I64, WAp, Align(1));
  Value *WBV = B.CreateAlignedLoad(I64, WBp, Align(1));
  Value *WEq = B.CreateICmpEQ(WAV, WBV);
  Value *WHas = emitHasZeroByte(B, WAV, NDC, "whas.zero");
  Value *WFoundNul = B.CreateICmpNE(WHas, Zero64);
  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, WideRead);
  Value *WNeedResolve = B.CreateOr(B.CreateNot(WEq), WFoundNul);
  auto *WBr = B.CreateCondBr(WNeedResolve, WideResolve, Wide);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(WideResolve);
  B.CreateCondBr(WEq, Eq, WideMismatch);

  B.SetInsertPoint(WideMismatch);
  Value *WXor = B.CreateXor(WAV, WBV);
  Value *WMmBit =
      B.CreateIntrinsic(Intrinsic::cttz, {I64},
                        {WXor, ConstantInt::getTrue(Ctx)}, nullptr, "mm.bit");
  Value *WMmByte = B.CreateLShr(WMmBit, ConstantInt::get(I64, 3), "mm.byte");
  Value *WNulBit =
      B.CreateIntrinsic(Intrinsic::cttz, {I64},
                        {WHas, ConstantInt::getFalse(Ctx)}, nullptr, "nul.bit");
  Value *WNulByte = B.CreateLShr(WNulBit, ConstantInt::get(I64, 3), "nul.byte");
  Value *WNulFirst = B.CreateICmpULT(WNulByte, WMmByte);
  B.CreateCondBr(WNulFirst, Eq, WideDiff);

  B.SetInsertPoint(WideDiff);
  Value *WAbsOff = B.CreateNUWAdd(WI, WMmByte, "abs.off");
  Value *WDAp = B.CreateInBoundsGEP(I8, A, WAbsOff);
  Value *WDBp = B.CreateInBoundsGEP(I8, BArg, WAbsOff);
  Value *WDAV = B.CreateAlignedLoad(I8, WDAp, Align(1));
  Value *WDBV = B.CreateAlignedLoad(I8, WDBp, Align(1));
  Value *WDAZ = B.CreateZExt(WDAV, I32);
  Value *WDBZ = B.CreateZExt(WDBV, I32);
  B.CreateRet(B.CreateSub(WDAZ, WDBZ));

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(WI, Wide);
  Value *TDone = B.CreateICmpUGE(TI, N);
  B.CreateCondBr(TDone, Eq, TailBody);

  B.SetInsertPoint(TailBody);
  Value *TAp = B.CreateInBoundsGEP(I8, A, TI);
  Value *TBp = B.CreateInBoundsGEP(I8, BArg, TI);
  Value *TAV = B.CreateAlignedLoad(I8, TAp, Align(1));
  Value *TBV = B.CreateAlignedLoad(I8, TBp, Align(1));
  Value *TEq = B.CreateICmpEQ(TAV, TBV);
  B.CreateCondBr(TEq, Cont, Diff);

  B.SetInsertPoint(Cont);
  Value *IsEnd = B.CreateICmpEQ(TAV, ConstantInt::get(I8, 0));
  Value *TINext = B.CreateNUWAdd(TI, ConstantInt::get(I64, 1));
  TI->addIncoming(TINext, Cont);
  auto *ContBr = B.CreateCondBr(IsEnd, Eq, Tail);
  ContBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 4));

  B.SetInsertPoint(Diff);
  Value *AZ = B.CreateZExt(TAV, I32);
  Value *BZ = B.CreateZExt(TBV, I32);
  B.CreateRet(B.CreateSub(AZ, BZ));

  B.SetInsertPoint(Eq);
  B.CreateRet(ConstantInt::get(I32, 0));
  return F;
}

Function *getOrCreateStrCat(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, PtrTy}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrCat, FTy);
  if (!F->empty())
    return F;
  setStringAppendAttrs(F);
  Argument *Dst = F->getArg(0);
  Argument *Src = F->getArg(1);
  Dst->setName("dst");
  Src->setName("src");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *FindWide = BasicBlock::Create(Ctx, "find.wide", F);
  BasicBlock *FindTail = BasicBlock::Create(Ctx, "find.tail", F);
  BasicBlock *CopyWide = BasicBlock::Create(Ctx, "copy.wide", F);
  BasicBlock *CopyWideCheck = BasicBlock::Create(Ctx, "copy.wide.check", F);
  BasicBlock *CopyTail = BasicBlock::Create(Ctx, "copy.tail", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  B.CreateBr(FindWide);

  B.SetInsertPoint(FindWide);
  PHINode *FW = B.CreatePHI(I64, 2, "fw");
  FW->addIncoming(Zero64, Entry);
  Value *FWp = B.CreateInBoundsGEP(I8, Dst, FW);
  Value *FWV = B.CreateAlignedLoad(I64, FWp, Align(1));
  Value *FHas = emitHasZeroByte(B, FWV, NDC, "fhas.zero");
  Value *FFound = B.CreateICmpNE(FHas, Zero64);
  Value *FWNext = B.CreateNUWAdd(FW, Eight64);
  FW->addIncoming(FWNext, FindWide);
  auto *FBr = B.CreateCondBr(FFound, FindTail, FindWide);
  FBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(FindTail);
  PHINode *FT = B.CreatePHI(I64, 2, "ft");
  FT->addIncoming(FW, FindWide);
  Value *FTp = B.CreateInBoundsGEP(I8, Dst, FT);
  Value *FTV = B.CreateAlignedLoad(I8, FTp, Align(1));
  Value *FTEnd = B.CreateICmpEQ(FTV, ConstantInt::get(I8, 0));
  Value *FTNext = B.CreateNUWAdd(FT, One64);
  FT->addIncoming(FTNext, FindTail);
  auto *FTBr2 = B.CreateCondBr(FTEnd, CopyWide, FindTail);
  FTBr2->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 4));

  B.SetInsertPoint(CopyWide);
  PHINode *CW = B.CreatePHI(I64, 2, "cw");
  CW->addIncoming(Zero64, FindTail);
  Value *CWSp = B.CreateInBoundsGEP(I8, Src, CW);
  Value *CWV = B.CreateAlignedLoad(I64, CWSp, Align(1));
  Value *CHas = emitHasZeroByte(B, CWV, NDC, "chas.zero");
  Value *CFound = B.CreateICmpNE(CHas, Zero64);
  auto *CBr = B.CreateCondBr(CFound, CopyTail, CopyWideCheck);
  CBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(CopyWideCheck);
  Value *CWDstIdx = B.CreateNUWAdd(FT, CW);
  Value *CWDp = B.CreateInBoundsGEP(I8, Dst, CWDstIdx);
  B.CreateAlignedStore(CWV, CWDp, Align(1));
  Value *CWNext = B.CreateNUWAdd(CW, Eight64);
  CW->addIncoming(CWNext, CopyWideCheck);
  B.CreateBr(CopyWide);

  B.SetInsertPoint(CopyTail);
  PHINode *CT = B.CreatePHI(I64, 2, "ct");
  CT->addIncoming(CW, CopyWide);
  Value *CTSp = B.CreateInBoundsGEP(I8, Src, CT);
  Value *CTV = B.CreateAlignedLoad(I8, CTSp, Align(1));
  Value *CTDstIdx = B.CreateNUWAdd(FT, CT);
  Value *CTDp = B.CreateInBoundsGEP(I8, Dst, CTDstIdx);
  B.CreateAlignedStore(CTV, CTDp, Align(1));
  Value *CTEnd = B.CreateICmpEQ(CTV, ConstantInt::get(I8, 0));
  Value *CTNext = B.CreateNUWAdd(CT, One64);
  CT->addIncoming(CTNext, CopyTail);
  auto *CTBr2 = B.CreateCondBr(CTEnd, Done, CopyTail);
  CTBr2->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 8));

  B.SetInsertPoint(Done);
  B.CreateRet(Dst);
  return F;
}

Function *getOrCreateStrChr(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, I32}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrChr, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *S = F->getArg(0);
  Argument *C = F->getArg(1);
  S->setName("s");
  C->setName("c");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *Resolve = BasicBlock::Create(Ctx, "resolve", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *CByte = B.CreateTrunc(C, I8);
  B.CreateBr(Prep);

  B.SetInsertPoint(Prep);
  Value *CZ = B.CreateZExt(CByte, I64);
  Value *CWide = B.CreateMul(CZ, NDC.Lo01, "c.wide");
  B.CreateBr(Wide);

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Prep);
  Value *WP = B.CreateInBoundsGEP(I8, S, WI);
  Value *WV = B.CreateAlignedLoad(I64, WP, Align(1));
  Value *Xored = B.CreateXor(WV, CWide);
  Value *HasMatch = emitHasZeroByte(B, Xored, NDC, "has.match");
  Value *FoundMatch = B.CreateICmpNE(HasMatch, Zero64);
  Value *HasNul = emitHasZeroByte(B, WV, NDC, "has.nul");
  Value *FoundNul = B.CreateICmpNE(HasNul, Zero64);
  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, Wide);
  Value *NeedResolve = B.CreateOr(FoundMatch, FoundNul);
  auto *WBr = B.CreateCondBr(NeedResolve, Resolve, Wide);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Resolve);
  Value *MatchBit = B.CreateIntrinsic(Intrinsic::cttz, {I64},
                                      {HasMatch, ConstantInt::getFalse(Ctx)},
                                      nullptr, "match.bit");
  Value *MatchByte =
      B.CreateLShr(MatchBit, ConstantInt::get(I64, 3), "match.byte");
  Value *NulBit = B.CreateIntrinsic(Intrinsic::cttz, {I64},
                                    {HasNul, ConstantInt::getFalse(Ctx)},
                                    nullptr, "nul.bit");
  Value *NulByte = B.CreateLShr(NulBit, ConstantInt::get(I64, 3), "nul.byte");
  Value *MatchFirst = B.CreateICmpULE(MatchByte, NulByte);
  Value *AbsOff = B.CreateAdd(WI, MatchByte);
  Value *HitPtr = B.CreateGEP(I8, S, AbsOff, "hit.ptr");
  Value *Result =
      B.CreateSelect(MatchFirst, HitPtr, ConstantPointerNull::get(PtrTy));
  B.CreateRet(Result);
  return F;
}

Function *getOrCreateStrRChr(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, I32}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrRChr, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *S = F->getArg(0);
  Argument *C = F->getArg(1);
  S->setName("s");
  C->setName("c");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);
  Constant *Eight64 = ConstantInt::get(I64, 8);
  Constant *Seven64 = ConstantInt::get(I64, 7);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *CByte = B.CreateTrunc(C, I8);
  B.CreateBr(Prep);

  B.SetInsertPoint(Prep);
  Value *CZ = B.CreateZExt(CByte, I64);
  Value *CWide = B.CreateMul(CZ, NDC.Lo01, "c.wide");
  B.CreateBr(Wide);

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Prep);
  PHINode *LastHit = B.CreatePHI(PtrTy, 2, "last.hit");
  LastHit->addIncoming(ConstantPointerNull::get(PtrTy), Prep);

  Value *WP = B.CreateInBoundsGEP(I8, S, WI);
  Value *WV = B.CreateAlignedLoad(I64, WP, Align(1));
  Value *Xored = B.CreateXor(WV, CWide);
  Value *HasMatch = emitHasZeroByte(B, Xored, NDC, "has.match");
  Value *FoundMatch = B.CreateICmpNE(HasMatch, Zero64);
  Value *HasNul = emitHasZeroByte(B, WV, NDC, "has.nul");
  Value *FoundNul = B.CreateICmpNE(HasNul, Zero64);

  Value *CtlzMatch = B.CreateIntrinsic(Intrinsic::ctlz, {I64},
                                       {HasMatch, ConstantInt::getTrue(Ctx)},
                                       nullptr, "ctlz.match");
  Value *MatchByteFromTop = B.CreateLShr(CtlzMatch, ConstantInt::get(I64, 3));
  Value *MatchByte = B.CreateSub(Seven64, MatchByteFromTop, "match.byte");
  Value *MatchOff = B.CreateNUWAdd(WI, MatchByte);
  Value *CandidatePtr = B.CreateInBoundsGEP(I8, S, MatchOff, "candidate");
  Value *NewLastHit = B.CreateSelect(FoundMatch, CandidatePtr, LastHit);

  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, Wide);
  LastHit->addIncoming(NewLastHit, Wide);

  auto *WBr = B.CreateCondBr(FoundNul, Tail, Wide);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(WI, Wide);
  PHINode *TLast = B.CreatePHI(PtrTy, 2, "tlast");
  TLast->addIncoming(LastHit, Wide);
  Value *TP = B.CreateInBoundsGEP(I8, S, TI);
  Value *TV = B.CreateAlignedLoad(I8, TP, Align(1));
  Value *TMatch = B.CreateICmpEQ(TV, CByte);
  Value *TNewLast = B.CreateSelect(TMatch, TP, TLast);
  Value *TIsEnd = B.CreateICmpEQ(TV, ConstantInt::get(I8, 0));
  Value *TINext = B.CreateNUWAdd(TI, One64);
  TI->addIncoming(TINext, Tail);
  TLast->addIncoming(TNewLast, Tail);
  auto *TBr = B.CreateCondBr(TIsEnd, Done, Tail);
  TBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 8));

  B.SetInsertPoint(Done);
  B.CreateRet(TNewLast);
  return F;
}

Function *getOrCreateStrNLen(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(I64, {PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrNLen, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *S = F->getArg(0);
  Argument *MaxLen = F->getArg(1);
  S->setName("s");
  MaxLen->setName("maxlen");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *WideRead = BasicBlock::Create(Ctx, "wide.read", F);
  BasicBlock *WideFound = BasicBlock::Create(Ctx, "wide.found", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *TailBody = BasicBlock::Create(Ctx, "tail.body", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *MaxIsZero = B.CreateICmpEQ(MaxLen, Zero64);
  auto *ZBr = B.CreateCondBr(MaxIsZero, Done, Wide);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Entry);
  Value *WEnd = B.CreateNUWAdd(WI, Eight64);
  Value *WFits = B.CreateICmpULE(WEnd, MaxLen);
  B.CreateCondBr(WFits, WideRead, Tail);

  B.SetInsertPoint(WideRead);
  Value *WP = B.CreateInBoundsGEP(I8, S, WI);
  Value *WV = B.CreateAlignedLoad(I64, WP, Align(1));
  Value *HasZero = emitHasZeroByte(B, WV, NDC);
  Value *FoundZero = B.CreateICmpNE(HasZero, Zero64);
  Value *WINext = B.CreateNUWAdd(WI, Eight64);
  WI->addIncoming(WINext, WideRead);
  auto *WBr = B.CreateCondBr(FoundZero, WideFound, Wide);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(WideFound);
  Value *BitPos = B.CreateIntrinsic(Intrinsic::cttz, {I64},
                                    {HasZero, ConstantInt::getTrue(Ctx)},
                                    nullptr, "bit.pos");
  Value *ByteOff = B.CreateLShr(BitPos, ConstantInt::get(I64, 3), "byte.off");
  Value *WideLen = B.CreateNUWAdd(WI, ByteOff);
  Value *WClamp =
      B.CreateSelect(B.CreateICmpULT(WideLen, MaxLen), WideLen, MaxLen);
  B.CreateBr(Done);

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(WI, Wide);
  Value *TDone = B.CreateICmpUGE(TI, MaxLen);
  B.CreateCondBr(TDone, Done, TailBody);

  B.SetInsertPoint(TailBody);
  Value *TP = B.CreateInBoundsGEP(I8, S, TI);
  Value *TV = B.CreateAlignedLoad(I8, TP, Align(1));
  Value *IsEnd = B.CreateICmpEQ(TV, ConstantInt::get(I8, 0));
  Value *TINext = B.CreateNUWAdd(TI, ConstantInt::get(I64, 1));
  TI->addIncoming(TINext, TailBody);
  auto *SNBr = B.CreateCondBr(IsEnd, Done, Tail);
  SNBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 4));

  B.SetInsertPoint(Done);
  PHINode *Len = B.CreatePHI(I64, 4, "len");
  Len->addIncoming(Zero64, Entry);
  Len->addIncoming(WClamp, WideFound);
  Len->addIncoming(MaxLen, Tail);
  Len->addIncoming(TI, TailBody);
  B.CreateRet(Len);
  return F;
}

Function *getOrCreateStrNCat(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, PtrTy, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kStrNCat, FTy);
  if (!F->empty())
    return F;
  setStringAppendAttrs(F);
  Argument *Dst = F->getArg(0);
  Argument *Src = F->getArg(1);
  Argument *N = F->getArg(2);
  Dst->setName("dst");
  Src->setName("src");
  N->setName("n");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);
  Constant *Eight64 = ConstantInt::get(I64, 8);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *FindWide = BasicBlock::Create(Ctx, "find.wide", F);
  BasicBlock *FindTail = BasicBlock::Create(Ctx, "find.tail", F);
  BasicBlock *CopyWide = BasicBlock::Create(Ctx, "copy.wide", F);
  BasicBlock *CopyWideRead = BasicBlock::Create(Ctx, "copy.wide.read", F);
  BasicBlock *CopyWideStore = BasicBlock::Create(Ctx, "copy.wide.store", F);
  BasicBlock *CopyTail = BasicBlock::Create(Ctx, "copy.tail", F);
  BasicBlock *CopyTailBody = BasicBlock::Create(Ctx, "copy.tail.body", F);
  BasicBlock *AppendNul = BasicBlock::Create(Ctx, "append.nul", F);
  BasicBlock *RetDst = BasicBlock::Create(Ctx, "ret.dst", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, RetDst, FindWide);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(FindWide);
  PHINode *FW = B.CreatePHI(I64, 2, "fw");
  FW->addIncoming(Zero64, Entry);
  Value *FWp = B.CreateInBoundsGEP(I8, Dst, FW);
  Value *FWV = B.CreateAlignedLoad(I64, FWp, Align(1));
  Value *FHas = emitHasZeroByte(B, FWV, NDC, "fhas.zero");
  Value *FFound = B.CreateICmpNE(FHas, Zero64);
  Value *FWNext = B.CreateNUWAdd(FW, Eight64);
  FW->addIncoming(FWNext, FindWide);
  auto *FBr = B.CreateCondBr(FFound, FindTail, FindWide);
  FBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(FindTail);
  PHINode *FT = B.CreatePHI(I64, 2, "ft");
  FT->addIncoming(FW, FindWide);
  Value *FTp = B.CreateInBoundsGEP(I8, Dst, FT);
  Value *FTV = B.CreateAlignedLoad(I8, FTp, Align(1));
  Value *FTEnd = B.CreateICmpEQ(FTV, ConstantInt::get(I8, 0));
  Value *FTNext = B.CreateNUWAdd(FT, One64);
  FT->addIncoming(FTNext, FindTail);
  auto *FTBr3 = B.CreateCondBr(FTEnd, CopyWide, FindTail);
  FTBr3->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 4));

  B.SetInsertPoint(CopyWide);
  PHINode *CWI = B.CreatePHI(I64, 2, "cwi");
  CWI->addIncoming(Zero64, FindTail);
  Value *CWEnd = B.CreateNUWAdd(CWI, Eight64);
  Value *CWFits = B.CreateICmpULE(CWEnd, N);
  B.CreateCondBr(CWFits, CopyWideRead, CopyTail);

  B.SetInsertPoint(CopyWideRead);
  Value *CWSp = B.CreateInBoundsGEP(I8, Src, CWI);
  Value *CWV = B.CreateAlignedLoad(I64, CWSp, Align(1));
  Value *CHas = emitHasZeroByte(B, CWV, NDC, "chas.zero");
  Value *CFound = B.CreateICmpNE(CHas, Zero64);
  auto *CWBr = B.CreateCondBr(CFound, CopyTail, CopyWideStore);
  CWBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(CopyWideStore);
  Value *CWDstIdx = B.CreateNUWAdd(FT, CWI);
  Value *CWDp = B.CreateInBoundsGEP(I8, Dst, CWDstIdx);
  B.CreateAlignedStore(CWV, CWDp, Align(1));
  Value *CWINext = B.CreateNUWAdd(CWI, Eight64);
  CWI->addIncoming(CWINext, CopyWideStore);
  B.CreateBr(CopyWide);

  B.SetInsertPoint(CopyTail);
  PHINode *CTI = B.CreatePHI(I64, 3, "cti");
  CTI->addIncoming(CWI, CopyWide);
  CTI->addIncoming(CWI, CopyWideRead);
  Value *CTDone = B.CreateICmpUGE(CTI, N);
  B.CreateCondBr(CTDone, AppendNul, CopyTailBody);

  B.SetInsertPoint(CopyTailBody);
  Value *CTSp = B.CreateInBoundsGEP(I8, Src, CTI);
  Value *CTV = B.CreateAlignedLoad(I8, CTSp, Align(1));
  Value *CTDstIdx = B.CreateNUWAdd(FT, CTI);
  Value *CTDp = B.CreateInBoundsGEP(I8, Dst, CTDstIdx);
  B.CreateAlignedStore(CTV, CTDp, Align(1));
  Value *CSrcEnd = B.CreateICmpEQ(CTV, ConstantInt::get(I8, 0));
  Value *CTINext = B.CreateNUWAdd(CTI, One64);
  CTI->addIncoming(CTINext, CopyTailBody);
  auto *CSBr = B.CreateCondBr(CSrcEnd, RetDst, CopyTail);
  CSBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 8));

  B.SetInsertPoint(AppendNul);
  Value *NulIdx = B.CreateNUWAdd(FT, N);
  Value *NulP = B.CreateInBoundsGEP(I8, Dst, NulIdx);
  B.CreateAlignedStore(ConstantInt::get(I8, 0), NulP, Align(1));
  B.CreateBr(RetDst);

  B.SetInsertPoint(RetDst);
  B.CreateRet(Dst);
  return F;
}

Function *getOrCreateMemRChr(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, I32, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kMemRChr, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *S = F->getArg(0);
  Argument *C = F->getArg(1);
  Argument *N = F->getArg(2);
  S->setName("s");
  C->setName("c");
  N->setName("n");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);
  Constant *Seven64 = ConstantInt::get(I64, 7);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *TailTest = BasicBlock::Create(Ctx, "tail.test", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *TailHit = BasicBlock::Create(Ctx, "tail.hit", F);
  BasicBlock *WideTest = BasicBlock::Create(Ctx, "wide.test", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *WideCont = BasicBlock::Create(Ctx, "wide.cont", F);
  BasicBlock *WideHit = BasicBlock::Create(Ctx, "wide.hit", F);
  BasicBlock *Miss = BasicBlock::Create(Ctx, "miss", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *CByte = B.CreateTrunc(C, I8);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Miss, Prep);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Prep);
  Value *CZ = B.CreateZExt(CByte, I64);
  Value *CWide = B.CreateMul(CZ, NDC.Lo01, "c.wide");
  Value *NChunks = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *TailStart = B.CreateShl(NChunks, ConstantInt::get(I64, 3), "",
                                 /*HasNUW=*/true);
  Value *HasTail = B.CreateICmpULT(TailStart, N);
  B.CreateCondBr(HasTail, TailTest, WideTest);

  B.SetInsertPoint(TailTest);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(N, Prep);
  Value *TIMinusOne = B.CreateSub(TI, One64);
  Value *TP = B.CreateInBoundsGEP(I8, S, TIMinusOne);
  Value *TV = B.CreateAlignedLoad(I8, TP, Align(1));
  Value *TMatch = B.CreateICmpEQ(TV, CByte);
  auto *TBr = B.CreateCondBr(TMatch, TailHit, Tail);
  TBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Tail);
  Value *TailDone = B.CreateICmpEQ(TIMinusOne, TailStart);
  TI->addIncoming(TIMinusOne, Tail);
  auto *TailBr = B.CreateCondBr(TailDone, WideTest, TailTest);
  TailBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 4));

  B.SetInsertPoint(TailHit);
  B.CreateRet(TP);

  B.SetInsertPoint(WideTest);
  Value *HasChunks = B.CreateICmpNE(NChunks, Zero64);
  auto *WCBr = B.CreateCondBr(HasChunks, Wide, Miss);
  WCBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Wide);
  PHINode *BWI = B.CreatePHI(I64, 2, "bwi");
  BWI->addIncoming(NChunks, WideTest);
  Value *BWIMinusOne = B.CreateSub(BWI, One64);
  Value *WOff = B.CreateShl(BWIMinusOne, ConstantInt::get(I64, 3), "",
                            /*HasNUW=*/true);
  Value *WP = B.CreateInBoundsGEP(I8, S, WOff);
  Value *WV = B.CreateAlignedLoad(I64, WP, Align(1));
  Value *Xored = B.CreateXor(WV, CWide);
  Value *HasMatch = emitHasZeroByte(B, Xored, NDC, "has.match");
  Value *FoundMatch = B.CreateICmpNE(HasMatch, Zero64);
  auto *WBr = B.CreateCondBr(FoundMatch, WideHit, WideCont);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(WideCont);
  BWI->addIncoming(BWIMinusOne, WideCont);
  Value *KeepGoing = B.CreateICmpNE(BWIMinusOne, Zero64);
  auto *WCBr2 = B.CreateCondBr(KeepGoing, Wide, Miss);
  WCBr2->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(WideHit);
  Value *CtlzVal =
      B.CreateIntrinsic(Intrinsic::ctlz, {I64},
                        {HasMatch, ConstantInt::getTrue(Ctx)}, nullptr, "ctlz");
  Value *CtlzShift = B.CreateLShr(CtlzVal, ConstantInt::get(I64, 3));
  Value *MatchByte = B.CreateSub(Seven64, CtlzShift, "match.byte");
  Value *HitOff = B.CreateNUWAdd(WOff, MatchByte, "hit.off");
  Value *HitPtr = B.CreateInBoundsGEP(I8, S, HitOff, "hit.ptr");
  B.CreateRet(HitPtr);

  B.SetInsertPoint(Miss);
  B.CreateRet(ConstantPointerNull::get(PtrTy));
  return F;
}

Function *emitAbsHelper(Module &M, StringRef Name, unsigned BitWidth) {
  LLVMContext &Ctx = M.getContext();
  Type *Ty = Type::getIntNTy(Ctx, BitWidth);
  FunctionType *FTy = FunctionType::get(Ty, {Ty}, false);
  Function *F = getOrCreateScHelper(M, Name, FTy);
  if (!F->empty())
    return F;
  F->setDoesNotAccessMemory();
  F->addFnAttr(Attribute::Speculatable);
  Argument *X = F->getArg(0);
  X->setName("x");

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *Zero = ConstantInt::get(Ty, 0);
  Value *Neg = B.CreateSub(Zero, X);
  Value *IsNeg = B.CreateICmpSLT(X, Zero);
  Value *R = B.CreateSelect(IsNeg, Neg, X);
  B.CreateRet(R);
  return F;
}

Function *getOrCreateAbs(Module &M) { return emitAbsHelper(M, ir::kAbs, 32); }
Function *getOrCreateLabs(Module &M) { return emitAbsHelper(M, ir::kLabs, 64); }
Function *getOrCreateLLAbs(Module &M) {
  return emitAbsHelper(M, ir::kLLAbs, 64);
}

Function *getOrCreateMemChr(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, I32, I64}, false);
  Function *F = getOrCreateScHelper(M, ir::kMemChr, FTy);
  if (!F->empty())
    return F;
  addReadOnlyBufAttrs(F, 0);
  F->setOnlyAccessesArgMemory();
  F->setOnlyReadsMemory();
  Argument *S = F->getArg(0);
  Argument *C = F->getArg(1);
  Argument *N = F->getArg(2);
  S->setName("s");
  C->setName("c");
  N->setName("n");

  NulDetectConstants NDC = getNulDetectConstants(Ctx);
  Constant *Zero64 = ConstantInt::get(I64, 0);
  Constant *One64 = ConstantInt::get(I64, 1);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Prep = BasicBlock::Create(Ctx, "prep", F);
  BasicBlock *Wide = BasicBlock::Create(Ctx, "wide", F);
  BasicBlock *WideCont = BasicBlock::Create(Ctx, "wide.cont", F);
  BasicBlock *WideHit = BasicBlock::Create(Ctx, "wide.hit", F);
  BasicBlock *Tail = BasicBlock::Create(Ctx, "tail", F);
  BasicBlock *TailCont = BasicBlock::Create(Ctx, "tail.cont", F);
  BasicBlock *Miss = BasicBlock::Create(Ctx, "miss", F);

  IRBuilder<> B(Entry);
  MDBuilder MDB(Ctx);
  Value *CByte = B.CreateTrunc(C, I8);
  Value *NIsZero = B.CreateICmpEQ(N, Zero64);
  auto *ZBr = B.CreateCondBr(NIsZero, Miss, Prep);
  ZBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Prep);
  Value *CZ = B.CreateZExt(CByte, I64);
  Value *C1 = B.CreateMul(CZ, NDC.Lo01, "c.wide");
  Value *NChunks = B.CreateLShr(N, ConstantInt::get(I64, 3));
  Value *ChunkEnd = B.CreateShl(NChunks, ConstantInt::get(I64, 3), "",
                                /*HasNUW=*/true);
  Value *HasChunks = B.CreateICmpNE(NChunks, Zero64);
  BasicBlock *TailEntry = BasicBlock::Create(Ctx, "tail.entry", F);
  B.CreateCondBr(HasChunks, Wide, TailEntry);

  B.SetInsertPoint(Wide);
  PHINode *WI = B.CreatePHI(I64, 2, "wi");
  WI->addIncoming(Zero64, Prep);
  Value *WOff = B.CreateShl(WI, ConstantInt::get(I64, 3), "", /*HasNUW=*/true);
  Value *WP = B.CreateInBoundsGEP(I8, S, WOff);
  Value *WV = B.CreateAlignedLoad(I64, WP, Align(1));
  Value *Xored = B.CreateXor(WV, C1);
  Value *HasMatch = emitHasZeroByte(B, Xored, NDC, "has.match");
  Value *FoundMatch = B.CreateICmpNE(HasMatch, Zero64);
  Value *WINext = B.CreateNUWAdd(WI, One64);
  WI->addIncoming(WINext, WideCont);
  auto *WBr = B.CreateCondBr(FoundMatch, WideHit, WideCont);
  WBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(WideCont);
  Value *WKeep = B.CreateICmpULT(WINext, NChunks);
  auto *WCBr = B.CreateCondBr(WKeep, Wide, TailEntry);
  WCBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(WideHit);
  Value *MatchBit = B.CreateIntrinsic(Intrinsic::cttz, {I64},
                                      {HasMatch, ConstantInt::getTrue(Ctx)},
                                      nullptr, "match.bit");
  Value *MatchByte =
      B.CreateLShr(MatchBit, ConstantInt::get(I64, 3), "match.byte");
  Value *HitOff = B.CreateNUWAdd(WOff, MatchByte, "hit.off");
  Value *HitPtr = B.CreateInBoundsGEP(I8, S, HitOff, "hit.ptr");
  B.CreateRet(HitPtr);

  B.SetInsertPoint(TailEntry);
  PHINode *TailStart = B.CreatePHI(I64, 2, "tail.start");
  TailStart->addIncoming(Zero64, Prep);
  TailStart->addIncoming(ChunkEnd, WideCont);
  B.CreateBr(Tail);

  B.SetInsertPoint(Tail);
  PHINode *TI = B.CreatePHI(I64, 2, "ti");
  TI->addIncoming(TailStart, TailEntry);
  Value *TDone = B.CreateICmpUGE(TI, N);
  B.CreateCondBr(TDone, Miss, TailCont);

  B.SetInsertPoint(TailCont);
  Value *TP = B.CreateInBoundsGEP(I8, S, TI);
  Value *TV = B.CreateAlignedLoad(I8, TP, Align(1));
  Value *TMatch = B.CreateICmpEQ(TV, CByte);
  Value *TINext = B.CreateNUWAdd(TI, One64);
  TI->addIncoming(TINext, TailCont);
  BasicBlock *TailHit = BasicBlock::Create(Ctx, "tail.hit", F);
  auto *MCBr = B.CreateCondBr(TMatch, TailHit, Tail);
  MCBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 4));

  B.SetInsertPoint(TailHit);
  B.CreateRet(TP);

  B.SetInsertPoint(Miss);
  B.CreateRet(ConstantPointerNull::get(PtrTy));
  return F;
}

bool rewriteCollectedMemIntrinsics(SmallVectorImpl<IntrinsicInst *> &Work,
                                   Function *MemCpy, Function *MemSet,
                                   Function *MemMove) {
  for (IntrinsicInst *II : Work) {
    IRBuilder<> B(II);
    Intrinsic::ID ID = II->getIntrinsicID();
    Function *Helper = nullptr;
    SmallVector<Value *, 3> Args;
    Args.push_back(II->getArgOperand(0));
    Args.push_back(II->getArgOperand(1));
    Value *N = II->getArgOperand(2);
    Type *I64 = B.getInt64Ty();
    if (N->getType() != I64)
      N = B.CreateZExtOrTrunc(N, I64);
    if (ID == Intrinsic::memcpy || ID == Intrinsic::memcpy_inline) {
      Helper = MemCpy;
      Args.push_back(N);
    } else if (ID == Intrinsic::memset) {
      Helper = MemSet;
      Value *Val = II->getArgOperand(1);
      if (Val->getType() != B.getInt32Ty())
        Val = B.CreateZExt(Val, B.getInt32Ty());
      Args.clear();
      Args.push_back(II->getArgOperand(0));
      Args.push_back(Val);
      Args.push_back(N);
    } else {
      Helper = MemMove;
      Args.push_back(N);
    }
    B.CreateCall(Helper, Args);
    II->eraseFromParent();
  }
  return !Work.empty();
}

struct LibcHelperBundle {
  Function *MemCpy;
  Function *MemSet;
  Function *MemMove;
  Function *MemCmp;
  Function *BZero;
  Function *MemChr;
  Function *MemRChr;
  Function *StrLen;
  Function *StrNLen;
  Function *StrCpy;
  Function *StrNCpy;
  Function *StrCmp;
  Function *StrNCmp;
  Function *StrCat;
  Function *StrNCat;
  Function *StrChr;
  Function *StrRChr;
  Function *Abs;
  Function *Labs;
  Function *LLAbs;
};

using LibcHelperFactory = Function *(*)(Module &);

struct DeclBindEntry {
  StringRef Name;
  Function *LibcHelperBundle::*Slot;
  LibcHelperFactory Create;
};

constexpr DeclBindEntry kDeclBinds[] = {
#define NEVERC_LIBC_INLINE_HELPER(externName, helperSlot, factory)             \
  {#externName, &LibcHelperBundle::helperSlot, factory},
#include "neverc/Shellcode/Tables/LibcInlineHelpers.def"
#include "neverc/Shellcode/Tables/UserExtra_LibcInlineHelpers.def"
#undef NEVERC_LIBC_INLINE_HELPER
};

const DeclBindEntry *findDeclBindEntry(StringRef Name) {
  for (const auto &E : kDeclBinds)
    if (E.Name == Name)
      return &E;
  return nullptr;
}

constexpr Function *LibcHelperBundle::*const kLibcHelperCleanupSlots[] = {
    &LibcHelperBundle::MemCpy,  &LibcHelperBundle::MemSet,
    &LibcHelperBundle::MemMove, &LibcHelperBundle::MemCmp,
    &LibcHelperBundle::BZero,   &LibcHelperBundle::MemChr,
    &LibcHelperBundle::MemRChr, &LibcHelperBundle::StrLen,
    &LibcHelperBundle::StrNLen, &LibcHelperBundle::StrCpy,
    &LibcHelperBundle::StrNCpy, &LibcHelperBundle::StrCmp,
    &LibcHelperBundle::StrNCmp, &LibcHelperBundle::StrCat,
    &LibcHelperBundle::StrNCat, &LibcHelperBundle::StrChr,
    &LibcHelperBundle::StrRChr, &LibcHelperBundle::Abs,
    &LibcHelperBundle::Labs,    &LibcHelperBundle::LLAbs,
};

void ensureMemIntrinSlot(Module &M, LibcHelperBundle &H,
                         const DeclBindEntry *E) {
  if (H.*(E->Slot))
    return;
  H.*(E->Slot) = E->Create(M);
}

bool rewriteCachedExternDecls(
    Module &M, const LibcHelperBundle &H,
    ArrayRef<std::pair<Function *, const DeclBindEntry *>> CachedDecls) {
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

} // namespace

PreservedAnalyses MemIntrinPass::run(Module &M, ModuleAnalysisManager &) {
  bool NeedMemCpy = false, NeedMemSet = false, NeedMemMove = false;
  SmallVector<std::pair<Function *, const DeclBindEntry *>, 16> MatchedDecls;
  MatchedDecls.reserve(std::size(kDeclBinds));
  SmallVector<IntrinsicInst *, 16> MemIntrinsics;

  for (Function &F : M) {
    if (F.isDeclaration()) {
      StringRef Canon = SymbolNames::stripObjectLeadingUnderscore(F.getName());
      if (const DeclBindEntry *Bind = findDeclBindEntry(Canon))
        MatchedDecls.push_back({&F, Bind});
      continue;
    }
    if (F.getName().starts_with(ir::kScPrefix))
      continue;
    for (Instruction &I : instructions(F)) {
      auto *II = dyn_cast<IntrinsicInst>(&I);
      if (!II)
        continue;
      switch (II->getIntrinsicID()) {
      case Intrinsic::memcpy:
      case Intrinsic::memcpy_inline:
        NeedMemCpy = true;
        MemIntrinsics.push_back(II);
        break;
      case Intrinsic::memset:
        NeedMemSet = true;
        MemIntrinsics.push_back(II);
        break;
      case Intrinsic::memmove:
        NeedMemMove = true;
        MemIntrinsics.push_back(II);
        break;
      default:
        break;
      }
    }
  }

  bool HasAnyWork =
      NeedMemCpy || NeedMemSet || NeedMemMove || !MatchedDecls.empty();
  if (!HasAnyWork)
    return PreservedAnalyses::all();

  LibcHelperBundle H = {};
  if (NeedMemCpy)
    H.MemCpy = getOrCreateMemCpy(M);
  if (NeedMemSet)
    H.MemSet = getOrCreateMemSet(M);
  if (NeedMemMove)
    H.MemMove = getOrCreateMemMove(M);

  for (const auto &Entry : MatchedDecls)
    ensureMemIntrinSlot(M, H, Entry.second);

  bool Changed = rewriteCollectedMemIntrinsics(MemIntrinsics, H.MemCpy,
                                               H.MemSet, H.MemMove);

  if (!MatchedDecls.empty())
    Changed |= rewriteCachedExternDecls(M, H, MatchedDecls);

  for (Function *LibcHelperBundle::*Slot : kLibcHelperCleanupSlots) {
    Function *F = H.*Slot;
    if (F && F->use_empty())
      F->eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
