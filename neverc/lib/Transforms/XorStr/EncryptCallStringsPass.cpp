#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "neverc/Foundation/Builtin/XorStrCipher.h"
#include "neverc/Foundation/Builtin/XorStrNames.h"
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
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <random>

using namespace llvm;

namespace neverc {
namespace xorstr {

namespace {

uint64_t generateKey(const DataLayout &DL) {
  static std::mt19937_64 Generator = [] {
    std::random_device Device;
    std::seed_seq Seed{Device(), Device(), Device(), Device()};
    return std::mt19937_64(Seed);
  }();
  uint64_t Key =
      Generator() & maskTrailingOnes<uint64_t>(DL.getPointerSizeInBits());
  return Key | 1U;
}

std::mt19937_64 makeKeyGenerator(uint64_t KeySeed) {
  if (KeySeed != 0) {
    std::seed_seq Seed{static_cast<unsigned>(KeySeed),
                       static_cast<unsigned>(KeySeed >> 32)};
    return std::mt19937_64(Seed);
  }

  std::random_device Device;
  std::seed_seq Seed{Device(), Device(), Device(), Device(),
                     Device(), Device(), Device(), Device()};
  return std::mt19937_64(Seed);
}

uint64_t generateDifferentKey(std::mt19937_64 &Generator, unsigned WordBits,
                              uint64_t OldKey) {
  const uint64_t Mask = maskTrailingOnes<uint64_t>(WordBits);
  uint64_t Key;
  do {
    Key = (Generator() & Mask) | 1U;
  } while (Key == OldKey);
  return Key;
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

bool hasScopedNameMarker(StringRef Name, StringRef Marker) {
  const size_t Position = Name.find(Marker);
  if (Position == StringRef::npos)
    return false;
  const size_t End = Position + Marker.size();
  const bool HasNameBoundaryBefore = Position == 0 || Name[Position - 1] == '.';
  const bool HasNameBoundaryAfter = End == Name.size() || Name[End] == '.';
  return HasNameBoundaryBefore && HasNameBoundaryAfter;
}

bool isXorStrDecryptFunction(const Function &F) {
  return hasScopedNameMarker(F.getName(), XorStrNames::DecryptFunctionName);
}

bool isXorStrDecryptCall(const CallBase &CB) {
  const auto *Callee =
      dyn_cast<Function>(CB.getCalledOperand()->stripPointerCasts());
  return Callee && isXorStrDecryptFunction(*Callee) && CB.arg_size() == 4;
}

bool readI8Array(GlobalVariable *GV, uint64_t ExpectedSize,
                 SmallVectorImpl<uint8_t> &Bytes) {
  if (!GV || !GV->isConstant() || !GV->hasInitializer())
    return false;
  auto *ArrayTy = dyn_cast<ArrayType>(GV->getValueType());
  if (!ArrayTy || !ArrayTy->getElementType()->isIntegerTy(8) ||
      ArrayTy->getNumElements() < ExpectedSize ||
      ArrayTy->getNumElements() - ExpectedSize > 1)
    return false;

  Constant *Init = GV->getInitializer();
  const uint64_t StorageSize = ArrayTy->getNumElements();
  Bytes.clear();
  Bytes.reserve(StorageSize);
  for (uint64_t I = 0; I < StorageSize; ++I) {
    auto *Byte = dyn_cast_or_null<ConstantInt>(Init->getAggregateElement(I));
    if (!Byte)
      return false;
    Bytes.push_back(static_cast<uint8_t>(Byte->getZExtValue()));
  }
  return true;
}

Value *emitEquivalentXor(IRBuilder<> &B, Value *LHS, Value *RHS,
                         unsigned Variant) {
  switch (Variant & 3U) {
  case 0:
    return B.CreateXor(LHS, RHS);
  case 1: {
    Value *Either = B.CreateOr(LHS, RHS);
    Value *Both = B.CreateAnd(LHS, RHS);
    return B.CreateAnd(Either, B.CreateNot(Both));
  }
  case 2: {
    Value *Sum = B.CreateAdd(LHS, RHS);
    Value *Carry = B.CreateShl(B.CreateAnd(LHS, RHS), 1);
    return B.CreateSub(Sum, Carry);
  }
  default: {
    Value *LeftOnly = B.CreateAnd(LHS, B.CreateNot(RHS));
    Value *RightOnly = B.CreateAnd(B.CreateNot(LHS), RHS);
    return B.CreateOr(LeftOnly, RightOnly);
  }
  }
}

void emitDecryptLoop(IRBuilder<> &B, Value *DstBuf, Value *SrcEnc,
                     uint64_t TotalBytes,
                     const xorstr::CipherSchedule &Schedule, unsigned WordBits,
                     uint64_t ShapeSeed, Value *OpaqueInitialState,
                     bool UseVolatileCiphertext = false) {
  LLVMContext &Ctx = B.getContext();
  Type *I8 = B.getInt8Ty();
  Type *I64 = B.getInt64Ty();
  IntegerType *WordTy = IntegerType::get(Ctx, WordBits);

  BasicBlock *PreHeader = B.GetInsertBlock();
  Function *F = PreHeader->getParent();

  BasicBlock *LoopBB = BasicBlock::Create(Ctx, "xorstr.loop", F);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "xorstr.done", F);

  Value *TotalVal = ConstantInt::get(I64, TotalBytes);
  B.CreateBr(LoopBB);

  B.SetInsertPoint(LoopBB);
  PHINode *IV = B.CreatePHI(I64, 2, "xorstr.i");
  IV->addIncoming(ConstantInt::get(I64, 0), PreHeader);
  PHINode *State = B.CreatePHI(WordTy, 2, "xorstr.state");
  State->addIncoming(OpaqueInitialState, PreHeader);

  Value *SrcPtr = B.CreateInBoundsGEP(I8, SrcEnc, IV);
  LoadInst *EncByte = B.CreateLoad(I8, SrcPtr);
  EncByte->setVolatile(UseVolatileCiphertext);

  auto WordConstant = [WordTy](uint64_t Value) {
    return ConstantInt::get(WordTy, Value);
  };
  Value *WordIndex = B.CreateZExtOrTrunc(IV, WordTy);
  Value *Indexed;
  if ((ShapeSeed & 1U) == 0) {
    Indexed = B.CreateMul(B.CreateAdd(WordIndex, WordConstant(1)),
                          WordConstant(Schedule.IndexStep));
    Indexed = B.CreateAdd(WordConstant(Schedule.Addend), Indexed);
  } else {
    Indexed = B.CreateMul(WordIndex, WordConstant(Schedule.IndexStep));
    Indexed = B.CreateAdd(Indexed,
                          WordConstant(Schedule.Addend + Schedule.IndexStep));
  }
  Value *NextState = B.CreateAdd(State, Indexed);
  unsigned ShapeLane = 1;
  auto Mix = [&](Value *LHS, Value *RHS) {
    const unsigned Variant =
        static_cast<unsigned>((ShapeSeed >> (ShapeLane++ * 2U)) & 3U);
    return emitEquivalentXor(B, LHS, RHS, Variant);
  };
  NextState = Mix(NextState, B.CreateLShr(NextState, Schedule.ShiftA));
  NextState = B.CreateMul(NextState, WordConstant(Schedule.Multiplier));
  NextState = Mix(NextState, B.CreateShl(NextState, Schedule.ShiftB));
  NextState = Mix(NextState, B.CreateLShr(NextState, Schedule.ShiftC));

  Value *Stream =
      Mix(NextState, B.CreateLShr(NextState, Schedule.StreamShiftA));
  Stream = Mix(Stream, B.CreateShl(Stream, Schedule.StreamShiftB));
  Stream = Mix(Stream, B.CreateLShr(Stream, 8));
  Stream = Mix(Stream, B.CreateLShr(Stream, 16));
  if (WordBits > 32)
    Stream = Mix(Stream, B.CreateLShr(Stream, 32));
  Value *KB = B.CreateTrunc(Stream, I8);

  Value *DecByte = Mix(EncByte, KB);

  Value *DstPtr = B.CreateInBoundsGEP(I8, DstBuf, IV);
  B.CreateStore(DecByte, DstPtr);

  Value *NextIV = B.CreateAdd(IV, ConstantInt::get(I64, 1));
  IV->addIncoming(NextIV, LoopBB);
  State->addIncoming(NextState, LoopBB);
  Value *Done = B.CreateICmpEQ(NextIV, TotalVal);
  B.CreateCondBr(Done, ExitBB, LoopBB);

  B.SetInsertPoint(ExitBB);
}

bool lowerDecryptCall(CallBase &CB, GlobalVariable &Encrypted,
                      GlobalVariable &InitialState, uint64_t Length,
                      const xorstr::CipherSchedule &Schedule, unsigned WordBits,
                      uint64_t ShapeSeed) {
  if (!isa<CallInst>(CB) || CB.getType()->isVoidTy())
    return false;

  Value *Output = CB.getArgOperand(3);
  BasicBlock *Original = CB.getParent();
  BasicBlock *Resume = Original->splitBasicBlock(&CB, "xorstr.resume");
  Original->getTerminator()->eraseFromParent();

  IRBuilder<> B(Original);
  if (Length != 0) {
    LoadInst *OpaqueInitialState =
        B.CreateLoad(IntegerType::get(B.getContext(), WordBits), &InitialState);
    OpaqueInitialState->setVolatile(true);
    emitDecryptLoop(B, Output, &Encrypted, Length, Schedule, WordBits,
                    ShapeSeed, OpaqueInitialState,
                    /*UseVolatileCiphertext=*/true);
  }

  Value *TerminatorSlot = B.CreateInBoundsGEP(
      B.getInt8Ty(), Output, ConstantInt::get(B.getInt64Ty(), Length));
  B.CreateStore(B.getInt8(0), TerminatorSlot);
  B.CreateBr(Resume);

  IRBuilder<> ResumeBuilder(&CB);
  Value *Replacement = Output;
  if (Replacement->getType() != CB.getType())
    Replacement =
        ResumeBuilder.CreateBitOrPointerCast(Replacement, CB.getType());
  CB.replaceAllUsesWith(Replacement);
  CB.eraseFromParent();
  return true;
}

bool removeDecryptSupport(Module &M) {
  SmallVector<Function *, 4> Decoders;
  SmallVector<GlobalVariable *, 4> Anchors;
  SmallVector<GlobalVariable *, 4> RouteStates;
  SmallPtrSet<GlobalValue *, 16> UsedEntries;

  for (Function &F : M) {
    if (!isXorStrDecryptFunction(F))
      continue;
    Decoders.push_back(&F);
    UsedEntries.insert(&F);
  }
  for (GlobalVariable &GV : M.globals()) {
    if (hasScopedNameMarker(GV.getName(), XorStrNames::DecryptABIAnchorName)) {
      Anchors.push_back(&GV);
      UsedEntries.insert(&GV);
    } else if (hasScopedNameMarker(GV.getName(), XorStrNames::RouteStateName)) {
      RouteStates.push_back(&GV);
      UsedEntries.insert(&GV);
    }
  }

  if (UsedEntries.empty())
    return false;

  removeFromUsedLists(M, [&](Constant *Entry) {
    auto *GV = dyn_cast<GlobalValue>(Entry->stripPointerCasts());
    return GV && UsedEntries.contains(GV);
  });

  bool Failed = false;
  for (GlobalVariable *Anchor : Anchors) {
    Anchor->removeDeadConstantUsers();
    if (!Anchor->use_empty()) {
      M.getContext().emitError(
          "NeverC xorstr finalization could not remove decoder ABI anchor");
      Failed = true;
      continue;
    }
    Anchor->eraseFromParent();
  }

  for (Function *Decoder : Decoders) {
    Decoder->removeDeadConstantUsers();
    if (!Decoder->use_empty()) {
      M.getContext().emitError(
          "NeverC xorstr finalization left a shared decoder call");
      Failed = true;
      continue;
    }
    Decoder->setSubprogram(nullptr);
    Decoder->eraseFromParent();
  }

  for (GlobalVariable *RouteState : RouteStates) {
    RouteState->removeDeadConstantUsers();
    if (!RouteState->use_empty()) {
      M.getContext().emitError(
          "NeverC xorstr finalization left shared decoder state");
      Failed = true;
      continue;
    }
    RouteState->eraseFromParent();
  }
  return !Failed;
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
  unsigned PtrBits = DL.getPointerSizeInBits();
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
        if (isXorStrDecryptCall(*CB))
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
    xorstr::CipherSchedule Schedule =
        xorstr::makeSchedule(Key, TotalBytes, PtrBits);

    SmallVector<uint8_t, 256> EncBytes(TotalBytes);
    StringRef RawBytes = CDA->getRawDataValues();
    uint64_t State = Schedule.InitialState;
    for (uint64_t i = 0; i < TotalBytes; ++i) {
      State = xorstr::advanceState(State, i, Schedule, PtrBits);
      EncBytes[i] = static_cast<uint8_t>(RawBytes[i]) ^
                    xorstr::streamByte(State, Schedule, PtrBits);
    }

    Constant *EncInit = ConstantDataArray::getRaw(
        StringRef(reinterpret_cast<const char *>(EncBytes.data()), TotalBytes),
        NumElts, CDA->getElementType());
    auto *EncGV = new GlobalVariable(M, EncInit->getType(), true,
                                     GlobalValue::PrivateLinkage, EncInit, "");
    EncGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    EncGV->setAlignment(GV->getAlign());
    EncGV->setMetadata("neverc.xorstr", XorstrMD);

    IntegerType *WordTy = IntegerType::get(M.getContext(), PtrBits);
    auto *StateGV =
        new GlobalVariable(M, WordTy, false, GlobalValue::PrivateLinkage,
                           ConstantInt::get(WordTy, Schedule.InitialState), "");
    StateGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    StateGV->setAlignment(DL.getABITypeAlign(WordTy));
    StateGV->setMetadata("neverc.xorstr", XorstrMD);

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

    LoadInst *OpaqueInitialState = B.CreateLoad(WordTy, StateGV);
    OpaqueInitialState->setVolatile(true);
    emitDecryptLoop(B, Buf, EncGV, TotalBytes, Schedule, PtrBits, Key,
                    OpaqueInitialState,
                    /*UseVolatileCiphertext=*/true);
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

PreservedAnalyses FinalizeXorStrPass::run(Module &M, ModuleAnalysisManager &) {
  const DataLayout &DL = M.getDataLayout();
  const unsigned WordBits = DL.getPointerSizeInBits();
  if (WordBits == 0 || WordBits > 64)
    return PreservedAnalyses::all();

  SmallVector<CallBase *, 32> Worklist;
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        auto *CB = dyn_cast<CallBase>(&I);
        if (CB && isXorStrDecryptCall(*CB))
          Worklist.push_back(CB);
      }
    }
  }

  if (Worklist.empty()) {
    const bool Changed = removeDecryptSupport(M);
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  std::mt19937_64 Generator = makeKeyGenerator(KeySeed);
  MDNode *XorstrMD = MDNode::get(M.getContext(), {});
  SmallPtrSet<GlobalVariable *, 16> OldGlobals;
  bool Changed = false;

  for (CallBase *CB : Worklist) {
    auto *LengthToken = dyn_cast<ConstantInt>(CB->getArgOperand(1));
    auto *OldKeyValue = dyn_cast<ConstantInt>(CB->getArgOperand(2));
    GlobalVariable *OldGV = findStringGlobal(CB->getArgOperand(0));
    if (!LengthToken || !OldKeyValue || !OldGV)
      continue;

    const uint64_t OldKey =
        xorstr::truncateWord(OldKeyValue->getZExtValue(), WordBits);
    const uint64_t Token =
        xorstr::truncateWord(LengthToken->getZExtValue(), WordBits);
    const uint64_t Length = xorstr::truncateWord(
        Token ^ xorstr::lengthMask(OldKey, WordBits), WordBits);

    SmallVector<uint8_t, 256> OldBytes;
    if (!readI8Array(OldGV, Length, OldBytes))
      continue;

    const xorstr::CipherSchedule OldSchedule =
        xorstr::makeSchedule(OldKey, Length, WordBits);
    SmallVector<uint8_t, 256> Plaintext(Length);
    uint64_t State = OldSchedule.InitialState;
    for (uint64_t I = 0; I < Length; ++I) {
      State = xorstr::advanceState(State, I, OldSchedule, WordBits);
      Plaintext[I] =
          OldBytes[I] ^ xorstr::streamByte(State, OldSchedule, WordBits);
    }

    const uint64_t NewKey = generateDifferentKey(Generator, WordBits, OldKey);
    const uint64_t ShapeSeed = Generator();
    const xorstr::CipherSchedule NewSchedule =
        xorstr::makeSchedule(NewKey, Length, WordBits);
    SmallVector<uint8_t, 256> NewBytes(OldBytes.begin(), OldBytes.end());
    State = NewSchedule.InitialState;
    for (uint64_t I = 0; I < Length; ++I) {
      State = xorstr::advanceState(State, I, NewSchedule, WordBits);
      NewBytes[I] =
          Plaintext[I] ^ xorstr::streamByte(State, NewSchedule, WordBits);
    }

    Constant *NewInit = ConstantDataArray::getRaw(
        StringRef(reinterpret_cast<const char *>(NewBytes.data()),
                  NewBytes.size()),
        NewBytes.size(), Type::getInt8Ty(M.getContext()));
    auto *NewGV = new GlobalVariable(M, NewInit->getType(), true,
                                     GlobalValue::PrivateLinkage, NewInit, "");
    NewGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    NewGV->setAlignment(OldGV->getAlign());
    NewGV->setMetadata("neverc.xorstr", XorstrMD);

    IntegerType *WordTy = IntegerType::get(M.getContext(), WordBits);
    auto *StateGV = new GlobalVariable(
        M, WordTy, false, GlobalValue::PrivateLinkage,
        ConstantInt::get(WordTy, NewSchedule.InitialState), "");
    StateGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    StateGV->setAlignment(DL.getABITypeAlign(WordTy));
    StateGV->setMetadata("neverc.xorstr", XorstrMD);

    if (!lowerDecryptCall(*CB, *NewGV, *StateGV, Length, NewSchedule, WordBits,
                          ShapeSeed)) {
      M.getContext().emitError(
          "NeverC xorstr finalization could not expand a decoder call");
      continue;
    }
    if (NewGV->use_empty())
      NewGV->eraseFromParent();
    if (StateGV->use_empty())
      StateGV->eraseFromParent();
    OldGlobals.insert(OldGV);
    Changed = true;
  }

  for (GlobalVariable *OldGV : OldGlobals)
    if (OldGV->use_empty())
      OldGV->eraseFromParent();

  Changed |= removeDecryptSupport(M);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace xorstr
} // namespace neverc
