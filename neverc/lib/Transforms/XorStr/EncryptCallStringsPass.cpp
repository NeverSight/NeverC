#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MathExtras.h"
#include <ctime>

using namespace llvm;

namespace neverc {
namespace xorstr {

namespace {

uint64_t generateKey(const DataLayout &DL) {
  static uint64_t Counter = 0;
  uint64_t TimeVal =
      static_cast<uint64_t>(std::time(nullptr)) * 0x9E3779B97F4A7C15ULL;
  TimeVal |= 1;
  uint64_t Key = TimeVal ^ (++Counter * 0x517CC1B727220A95ULL);
  unsigned PtrBits = DL.getPointerSizeInBits();
  Key &= maskTrailingOnes<uint64_t>(PtrBits);
  return Key;
}

unsigned char encryptByte(unsigned char Byte, uint64_t Key, unsigned Idx,
                          unsigned KeyBytes) {
  auto KB = static_cast<unsigned char>(Key >> (8 * (Idx % KeyBytes)));
  return Byte ^ KB;
}

GlobalVariable *findStringGlobal(Value *V) {
  V = V->stripPointerCasts();
  if (auto *GV = dyn_cast<GlobalVariable>(V))
    return GV;
  if (auto *CE = dyn_cast<ConstantExpr>(V)) {
    if (CE->getOpcode() == Instruction::GetElementPtr)
      return dyn_cast<GlobalVariable>(CE->getOperand(0)->stripPointerCasts());
  }
  return nullptr;
}

bool isStringConstant(GlobalVariable *GV) {
  if (!GV->isConstant() || !GV->hasInitializer())
    return false;
  auto *Init = GV->getInitializer();
  if (auto *CDA = dyn_cast<ConstantDataArray>(Init)) {
    Type *EltTy = CDA->getElementType();
    return EltTy->isIntegerTy(8) || EltTy->isIntegerTy(16) ||
           EltTy->isIntegerTy(32);
  }
  return false;
}

void emitDecryptLoop(IRBuilder<> &B, Value *DstBuf, Value *SrcEnc,
                     uint64_t TotalBytes, uint64_t Key, unsigned KeyBytes) {
  LLVMContext &Ctx = B.getContext();
  Type *I8 = B.getInt8Ty();
  Type *I64 = B.getInt64Ty();

  BasicBlock *PreHeader = B.GetInsertBlock();
  Function *F = PreHeader->getParent();

  BasicBlock *LoopBB = BasicBlock::Create(Ctx, "xorstr.loop", F);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "xorstr.done", F);

  Value *TotalVal = ConstantInt::get(I64, TotalBytes);
  B.CreateBr(LoopBB);

  B.SetInsertPoint(LoopBB);
  PHINode *IV = B.CreatePHI(I64, 2, "xorstr.i");
  IV->addIncoming(ConstantInt::get(I64, 0), PreHeader);

  Value *SrcPtr = B.CreateInBoundsGEP(I8, SrcEnc, IV);
  Value *EncByte = B.CreateLoad(I8, SrcPtr);

  Value *IdxMod = B.CreateURem(IV, ConstantInt::get(I64, KeyBytes));
  Value *ShiftAmt = B.CreateMul(IdxMod, ConstantInt::get(I64, 8));
  Value *KeyVal = ConstantInt::get(I64, Key);
  Value *Shifted = B.CreateLShr(KeyVal, ShiftAmt);
  Value *KB = B.CreateTrunc(Shifted, I8);

  // dec(a, b) = a + b - 2*(a & b)  ==  a ^ b  (avoids XOR instruction)
  Value *Va = B.CreateZExt(EncByte, I64);
  Value *Vb = B.CreateZExt(KB, I64);
  Value *Sum = B.CreateAdd(Va, Vb);
  Value *Band = B.CreateAnd(Va, Vb);
  Value *DoubleBand = B.CreateAdd(Band, Band);
  Value *DecVal = B.CreateSub(Sum, DoubleBand);
  Value *DecByte = B.CreateTrunc(DecVal, I8);

  Value *DstPtr = B.CreateInBoundsGEP(I8, DstBuf, IV);
  B.CreateStore(DecByte, DstPtr);

  Value *NextIV = B.CreateAdd(IV, ConstantInt::get(I64, 1));
  IV->addIncoming(NextIV, LoopBB);
  Value *Done = B.CreateICmpEQ(NextIV, TotalVal);
  B.CreateCondBr(Done, ExitBB, LoopBB);

  B.SetInsertPoint(ExitBB);
}

struct CallStringArg {
  CallBase *CB;
  unsigned ArgIdx;
  GlobalVariable *GV;
};

} // anonymous namespace

PreservedAnalyses EncryptCallStringsPass::run(Module &M,
                                              ModuleAnalysisManager &MAM) {
  const DataLayout &DL = M.getDataLayout();
  unsigned PtrBytes = DL.getPointerSize();
  MDNode *XorstrMD = MDNode::get(M.getContext(), {});

  SmallVector<CallStringArg, 16> Worklist;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        if (isa<IntrinsicInst>(CB))
          continue;
        if (!CB->getCalledFunction())
          continue;
        if (BB.getFirstNonPHI()->isEHPad())
          continue;

        for (unsigned i = 0, e = CB->arg_size(); i < e; ++i) {
          GlobalVariable *GV = findStringGlobal(CB->getArgOperand(i));
          if (!GV || !isStringConstant(GV))
            continue;
          if (GV->hasMetadata("neverc.xorstr"))
            continue;

          auto *CDA = cast<ConstantDataArray>(GV->getInitializer());
          uint64_t TotalBytes =
              CDA->getNumElements() *
              (CDA->getElementType()->getPrimitiveSizeInBits() / 8);
          if (MaxLen > 0 && TotalBytes > MaxLen)
            continue;

          Worklist.push_back({CB, i, GV});
        }
      }
    }
  }

  if (Worklist.empty())
    return PreservedAnalyses::all();

  for (auto &Entry : Worklist) {
    CallBase *CB = Entry.CB;
    GlobalVariable *GV = Entry.GV;
    auto *CDA = cast<ConstantDataArray>(GV->getInitializer());

    uint64_t NumElts = CDA->getNumElements();
    uint64_t EltBytes =
        CDA->getElementType()->getPrimitiveSizeInBits() / 8;
    uint64_t TotalBytes = NumElts * EltBytes;

    uint64_t Key = generateKey(DL);

    SmallVector<uint8_t, 256> EncBytes(TotalBytes);
    StringRef RawBytes = CDA->getRawDataValues();
    for (uint64_t i = 0; i < TotalBytes; ++i)
      EncBytes[i] = encryptByte(static_cast<uint8_t>(RawBytes[i]), Key,
                                static_cast<unsigned>(i), PtrBytes);

    Constant *EncInit = ConstantDataArray::getRaw(
        StringRef(reinterpret_cast<const char *>(EncBytes.data()), TotalBytes),
        NumElts, CDA->getElementType());
    auto *EncGV = new GlobalVariable(
        M, EncInit->getType(), true, GlobalValue::PrivateLinkage, EncInit,
        GV->getName() + ".xorstr.enc");
    EncGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    EncGV->setAlignment(GV->getAlign());
    EncGV->setMetadata("neverc.xorstr", XorstrMD);

    Function *F = CB->getFunction();
    IRBuilder<> AllocaBuilder(&*F->getEntryBlock().getFirstInsertionPt());
    AllocaInst *Buf = AllocaBuilder.CreateAlloca(
        ArrayType::get(AllocaBuilder.getInt8Ty(), TotalBytes), nullptr,
        "xorstr.buf");
    Buf->setAlignment(GV->getAlign().valueOrOne());
    Buf->setMetadata("neverc.xorstr", XorstrMD);

    IRBuilder<> B(CB);

    BasicBlock *OrigBB = CB->getParent();
    BasicBlock *PostDecryptBB =
        OrigBB->splitBasicBlock(CB, "xorstr.post");

    OrigBB->getTerminator()->eraseFromParent();
    B.SetInsertPoint(OrigBB);

    emitDecryptLoop(B, Buf, EncGV, TotalBytes, Key, PtrBytes);
    B.CreateBr(PostDecryptBB);

    IRBuilder<> PostB(CB);
    Value *CastBuf = PostB.CreateBitOrPointerCast(
        Buf, CB->getArgOperand(Entry.ArgIdx)->getType());
    CB->setArgOperand(Entry.ArgIdx, CastBuf);
  }

  SmallPtrSet<GlobalVariable *, 16> Removed;
  for (auto &Entry : Worklist) {
    GlobalVariable *GV = Entry.GV;
    if (GV->use_empty() && !Removed.count(GV)) {
      Removed.insert(GV);
      GV->eraseFromParent();
    }
  }

  return PreservedAnalyses::none();
}

} // namespace xorstr
} // namespace neverc
