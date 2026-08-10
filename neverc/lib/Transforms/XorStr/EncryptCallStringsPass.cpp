#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "neverc/Foundation/Builtin/XorStrCipher.h"
#include "neverc/Foundation/Builtin/XorStrNames.h"
#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/xxhash.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <array>
#include <limits>
#include <optional>
#include <random>
#include <system_error>

using namespace llvm;

namespace neverc {
namespace xorstr {

namespace {

constexpr StringLiteral AutoMustTailDiagnosticMetadataName =
    "neverc.xorstr.auto-musttail-diagnosed";

bool isSupportedWordBits(unsigned WordBits) {
  return WordBits >= 8 && WordBits <= 64 && isPowerOf2_32(WordBits);
}

std::optional<std::mt19937_64>
makeKeyGenerator(uint64_t KeySeed, uint64_t DomainSalt, LLVMContext &Ctx) {
  if (KeySeed != 0) {
    std::seed_seq Seed{static_cast<unsigned>(KeySeed),
                       static_cast<unsigned>(KeySeed >> 32),
                       static_cast<unsigned>(DomainSalt),
                       static_cast<unsigned>(DomainSalt >> 32)};
    return std::mt19937_64(Seed);
  }

  std::array<uint32_t, 8> Entropy;
  if (int Error = llvm::getRandomBytes(Entropy.data(), sizeof(Entropy))) {
    Ctx.emitError("NeverC xorstr could not obtain operating-system entropy: " +
                  std::error_code(Error, std::system_category()).message());
    return std::nullopt;
  }
  std::seed_seq Seed(Entropy.begin(), Entropy.end());
  return std::mt19937_64(Seed);
}

void appendDomainWord(SmallVectorImpl<uint8_t> &Material, uint64_t Value) {
  for (unsigned Shift = 0; Shift != 64; Shift += 8)
    Material.push_back(static_cast<uint8_t>(Value >> Shift));
}

void appendDomainBytes(SmallVectorImpl<uint8_t> &Material, StringRef Bytes) {
  appendDomainWord(Material, Bytes.size());
  Material.append(Bytes.bytes_begin(), Bytes.bytes_end());
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

GlobalVariable *findStringGlobalWithOffset(Value *V, const DataLayout &DL,
                                           uint64_t &ByteOffset) {
  if (!V->getType()->isPointerTy())
    return nullptr;
  int64_t SignedOffset = 0;
  Value *Base = GetPointerBaseWithConstantOffset(V, SignedOffset, DL);
  auto *GV = dyn_cast<GlobalVariable>(Base->stripPointerCasts());
  if (!GV || SignedOffset < 0)
    return nullptr;
  ByteOffset = static_cast<uint64_t>(SignedOffset);
  return GV;
}

bool isStringConstant(GlobalVariable *GV) {
  // A ConstantDataArray is not necessarily a language string literal.  In
  // particular, an externally visible `const char[]` has the same initializer
  // shape but its symbol and storage are part of the program ABI.  NeverC's IR
  // emitter represents actual string-literal storage as private unnamed_addr;
  // limit the automatic transform to that compiler-owned storage.
  if (!GV->isConstant() || !GV->hasInitializer() || !GV->hasPrivateLinkage() ||
      !GV->hasGlobalUnnamedAddr())
    return false;
  auto *Init = GV->getInitializer();
  if (auto *CDA = dyn_cast<ConstantDataArray>(Init)) {
    Type *EltTy = CDA->getElementType();
    return EltTy->isIntegerTy(8) || EltTy->isIntegerTy(16) ||
           EltTy->isIntegerTy(32);
  }
  return false;
}

bool isXorStrDecryptFunction(const Function &F) {
  return XorStrNames::isDecryptFunctionName(F.getName());
}

bool isXorStrSupportFunction(const Function &F) {
  return XorStrNames::isSupportFunctionName(F.getName());
}

bool isXorStrDecryptCall(const CallBase &CB) {
  const auto *Callee =
      dyn_cast<Function>(CB.getCalledOperand()->stripPointerCasts());
  return Callee && isXorStrDecryptFunction(*Callee) && CB.arg_size() == 4;
}

bool readI8Array(GlobalVariable *GV, uint64_t ByteOffset, uint64_t ExpectedSize,
                 SmallVectorImpl<uint8_t> &Bytes) {
  if (!GV || !GV->isConstant() || !GV->hasInitializer())
    return false;
  auto *ArrayTy = dyn_cast<ArrayType>(GV->getValueType());
  if (!ArrayTy || !ArrayTy->getElementType()->isIntegerTy(8))
    return false;
  const uint64_t StorageSize = ArrayTy->getNumElements();
  if (ByteOffset > StorageSize || ExpectedSize > StorageSize - ByteOffset)
    return false;

  Constant *Init = GV->getInitializer();
  Bytes.clear();
  Bytes.reserve(ExpectedSize);
  for (uint64_t I = 0; I < ExpectedSize; ++I) {
    auto *Byte = dyn_cast_or_null<ConstantInt>(
        Init->getAggregateElement(ByteOffset + I));
    if (!Byte)
      return false;
    Bytes.push_back(static_cast<uint8_t>(Byte->getZExtValue()));
  }
  return true;
}

struct StackSlice {
  AllocaInst *Storage;
  uint64_t ByteOffset;
};

bool collectOutputStackSlices(Value *Root, const DataLayout &DL,
                              SmallVectorImpl<StackSlice> &Slices,
                              SmallPtrSetImpl<Value *> &Seen) {
  if (!Root || !Root->getType()->isPointerTy())
    return false;

  int64_t SignedOffset = 0;
  Value *Base = GetPointerBaseWithConstantOffset(Root, SignedOffset, DL);
  if (auto *AI = dyn_cast<AllocaInst>(Base->stripPointerCasts())) {
    if (SignedOffset < 0)
      return false;
    Slices.push_back({AI, static_cast<uint64_t>(SignedOffset)});
    return true;
  }

  Root = Root->stripPointerCasts();
  if (!Seen.insert(Root).second)
    return true;

  bool Complete = true;
  if (auto *PN = dyn_cast<PHINode>(Root)) {
    for (Value *Incoming : PN->incoming_values())
      Complete &= collectOutputStackSlices(Incoming, DL, Slices, Seen);
    return Complete;
  }
  if (auto *SI = dyn_cast<SelectInst>(Root)) {
    Complete &= collectOutputStackSlices(SI->getTrueValue(), DL, Slices, Seen);
    Complete &= collectOutputStackSlices(SI->getFalseValue(), DL, Slices, Seen);
    return Complete;
  }
  if (auto *Freeze = dyn_cast<FreezeInst>(Root))
    return collectOutputStackSlices(Freeze->getOperand(0), DL, Slices, Seen);
  if (auto *CB = dyn_cast<CallBase>(Root)) {
    if (isXorStrDecryptCall(*CB))
      return collectOutputStackSlices(CB->getArgOperand(3), DL, Slices, Seen);
    return false;
  }
  if (auto *Load = dyn_cast<LoadInst>(Root)) {
    auto *Slot =
        dyn_cast<AllocaInst>(getUnderlyingObject(Load->getPointerOperand()));
    if (!Slot || !Slot->getAllocatedType()->isPointerTy() ||
        !isAllocaPromotable(Slot))
      return false;

    bool SawStore = false;
    for (BasicBlock &BB : *Load->getFunction()) {
      for (Instruction &I : BB) {
        auto *Store = dyn_cast<StoreInst>(&I);
        if (!Store || !Store->getValueOperand()->getType()->isPointerTy() ||
            getUnderlyingObject(Store->getPointerOperand()) != Slot)
          continue;
        SawStore = true;
        Complete &= collectOutputStackSlices(Store->getValueOperand(), DL,
                                             Slices, Seen);
      }
    }
    return SawStore && Complete;
  }

  // A non-constant GEP, an argument/global, or any other opaque pointer flow
  // cannot prove that the expanded loop and its trailing NUL stay in-bounds.
  return false;
}

bool outputHasSufficientStackStorage(Value *Output, uint64_t RequiredBytes,
                                     const DataLayout &DL) {
  SmallVector<StackSlice, 4> Slices;
  SmallPtrSet<Value *, 16> Seen;
  if (!collectOutputStackSlices(Output, DL, Slices, Seen) || Slices.empty())
    return false;

  for (const StackSlice &Slice : Slices) {
    std::optional<TypeSize> Size = Slice.Storage->getAllocationSize(DL);
    if (!Size || Size->isScalable())
      return false;
    const uint64_t Bytes = Size->getFixedValue();
    if (Slice.ByteOffset > Bytes || RequiredBytes > Bytes - Slice.ByteOffset)
      return false;
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
  if (WordBits > 8)
    Stream = Mix(Stream, B.CreateLShr(Stream, 8));
  if (WordBits > 16)
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
  if (!isa<CallInst>(CB) || !CB.getType()->isPointerTy() ||
      !CB.getArgOperand(3)->getType()->isPointerTy())
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
  SmallVector<Function *, 8> Helpers;
  SmallVector<GlobalVariable *, 4> Anchors;
  SmallVector<GlobalVariable *, 4> RouteStates;
  SmallPtrSet<GlobalValue *, 16> UsedEntries;

  for (Function &F : M) {
    if (!isXorStrSupportFunction(F))
      continue;
    if (isXorStrDecryptFunction(F))
      Decoders.push_back(&F);
    else
      Helpers.push_back(&F);
    UsedEntries.insert(&F);
  }
  for (GlobalVariable &GV : M.globals()) {
    if (XorStrNames::hasScopedNameMarker(GV.getName(),
                                         XorStrNames::DecryptABIAnchorName)) {
      Anchors.push_back(&GV);
      UsedEntries.insert(&GV);
    } else if (XorStrNames::hasScopedNameMarker(GV.getName(),
                                                XorStrNames::RouteStateName)) {
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

  for (GlobalVariable *Anchor : Anchors) {
    Anchor->removeDeadConstantUsers();
    if (!Anchor->use_empty()) {
      M.getContext().emitError(
          "NeverC xorstr finalization could not remove decoder ABI anchor");
      continue;
    }
    Anchor->eraseFromParent();
  }

  for (Function *Decoder : Decoders) {
    Decoder->removeDeadConstantUsers();
    if (!Decoder->use_empty()) {
      M.getContext().emitError(
          "NeverC xorstr finalization left a shared decoder call");
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
      continue;
    }
    RouteState->eraseFromParent();
  }

  // Removing the decoder makes its private helper graph unreachable.  Peel
  // that graph from callers to callees so finalization is self-contained and
  // does not depend on a later generic optimizer or linker dead-strip pass.
  bool RemovedHelper;
  do {
    RemovedHelper = false;
    for (Function *&Helper : Helpers) {
      if (!Helper)
        continue;
      Helper->removeDeadConstantUsers();
      if (!Helper->isDiscardableIfUnused() || !Helper->use_empty())
        continue;
      Helper->setSubprogram(nullptr);
      Helper->eraseFromParent();
      Helper = nullptr;
      RemovedHelper = true;
    }
  } while (RemovedHelper);

  for (Function *Helper : Helpers) {
    if (!Helper)
      continue;
    M.getContext().emitError(
        "NeverC xorstr finalization left a shared decoder helper");
  }
  // Return whether the pass encountered support objects, not whether their
  // removal succeeded.  emitError() makes an incomplete finalization fail the
  // compilation, while the pass manager must still invalidate analyses for
  // any llvm.used edits or partial removals performed before that diagnostic.
  return true;
}

struct StringCandidate {
  Value *Source;
  GlobalVariable *GV;
  uint64_t ByteOffset;
};

struct CallStringArg {
  CallBase *CB;
  unsigned ArgIdx;
  SmallVector<StringCandidate, 2> Candidates;
};

void collectStringCandidates(Value *V, const DataLayout &DL, unsigned MaxLen,
                             SmallVectorImpl<StringCandidate> &Candidates,
                             SmallPtrSetImpl<Value *> &SeenValues) {
  uint64_t ByteOffset = 0;
  if (GlobalVariable *GV = findStringGlobalWithOffset(V, DL, ByteOffset)) {
    if (!isStringConstant(GV))
      return;
    auto *CDA = cast<ConstantDataArray>(GV->getInitializer());
    const uint64_t TotalBytes =
        CDA->getNumElements() *
        (CDA->getElementType()->getPrimitiveSizeInBits() / 8);
    if (TotalBytes == 0 || ByteOffset > TotalBytes ||
        (MaxLen > 0 && TotalBytes > MaxLen))
      return;
    Candidates.push_back({V, GV, ByteOffset});
    return;
  }

  if (!SeenValues.insert(V).second)
    return;

  if (auto *Select = dyn_cast<SelectInst>(V)) {
    collectStringCandidates(Select->getTrueValue(), DL, MaxLen, Candidates,
                            SeenValues);
    collectStringCandidates(Select->getFalseValue(), DL, MaxLen, Candidates,
                            SeenValues);
    return;
  }
  if (auto *Phi = dyn_cast<PHINode>(V)) {
    for (Value *Incoming : Phi->incoming_values())
      collectStringCandidates(Incoming, DL, MaxLen, Candidates, SeenValues);
    return;
  }
  if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
    collectStringCandidates(GEP->getPointerOperand(), DL, MaxLen, Candidates,
                            SeenValues);
    return;
  }
  if (auto *Cast = dyn_cast<CastInst>(V)) {
    collectStringCandidates(Cast->getOperand(0), DL, MaxLen, Candidates,
                            SeenValues);
    return;
  }
  if (auto *Freeze = dyn_cast<FreezeInst>(V))
    collectStringCandidates(Freeze->getOperand(0), DL, MaxLen, Candidates,
                            SeenValues);
}

bool isSupportedPointerFlowInstruction(const Instruction &I) {
  return isa<SelectInst, PHINode, GetElementPtrInst, CastInst, FreezeInst>(I);
}

void collectPromotableArgumentAllocas(Value *V,
                                      SmallPtrSetImpl<AllocaInst *> &Allocas,
                                      SmallPtrSetImpl<Value *> &SeenValues) {
  if (!SeenValues.insert(V).second)
    return;

  if (auto *Load = dyn_cast<LoadInst>(V)) {
    if (!Load->getType()->isPointerTy() || Load->isVolatile())
      return;
    auto *AI =
        dyn_cast<AllocaInst>(Load->getPointerOperand()->stripPointerCasts());
    if (AI && isAllocaPromotable(AI))
      Allocas.insert(AI);
    return;
  }

  auto *I = dyn_cast<Instruction>(V);
  if (!I || !isSupportedPointerFlowInstruction(*I))
    return;
  for (Value *Operand : I->operand_values())
    collectPromotableArgumentAllocas(Operand, Allocas, SeenValues);
}

bool promoteCallArgumentAllocas(Module &M) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    // Promotion can expose a load from another pointer slot (for example,
    // `p2 = p1; call(p2)`).  Iterate until no promotable call-argument slot
    // remains.  This is deliberately local and does not turn -O0 into a
    // general optimization pipeline.
    while (true) {
      SmallPtrSet<AllocaInst *, 8> UniqueAllocas;
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          auto *CB = dyn_cast<CallBase>(&I);
          if (!CB || isa<IntrinsicInst>(CB) || CB->isInlineAsm() ||
              isXorStrDecryptCall(*CB))
            continue;
          for (Value *Argument : CB->args()) {
            SmallPtrSet<Value *, 8> SeenValues;
            collectPromotableArgumentAllocas(Argument, UniqueAllocas,
                                             SeenValues);
          }
        }
      }
      if (UniqueAllocas.empty())
        break;

      SmallVector<AllocaInst *, 8> Allocas(UniqueAllocas.begin(),
                                           UniqueAllocas.end());
      DominatorTree DT(F);
      PromoteMemToReg(Allocas, DT);
      Changed = true;
    }
  }
  return Changed;
}

Value *rebuildStringValue(Value *V,
                          const DenseMap<Value *, Value *> &Replacements,
                          DenseMap<Value *, Value *> &Cache) {
  if (auto It = Replacements.find(V); It != Replacements.end())
    return It->second;
  if (auto It = Cache.find(V); It != Cache.end())
    return It->second;

  auto *I = dyn_cast<Instruction>(V);
  if (!I || !isSupportedPointerFlowInstruction(*I))
    return V;

  if (auto *Phi = dyn_cast<PHINode>(I)) {
    auto *Replacement =
        PHINode::Create(Phi->getType(), Phi->getNumIncomingValues(),
                        "xorstr.phi", &*Phi->getParent()->getFirstNonPHIIt());
    Cache[V] = Replacement;
    bool AnyChanged = false;
    for (unsigned Index = 0; Index != Phi->getNumIncomingValues(); ++Index) {
      Value *OldIncoming = Phi->getIncomingValue(Index);
      Value *NewIncoming = rebuildStringValue(OldIncoming, Replacements, Cache);
      Replacement->addIncoming(NewIncoming, Phi->getIncomingBlock(Index));
      AnyChanged |= NewIncoming != OldIncoming;
    }
    if (!AnyChanged) {
      Cache.erase(V);
      Replacement->eraseFromParent();
      return V;
    }
    return Replacement;
  }

  Instruction *Replacement = I->clone();
  bool AnyChanged = false;
  for (unsigned Index = 0; Index != I->getNumOperands(); ++Index) {
    Value *OldOperand = I->getOperand(Index);
    Value *NewOperand = rebuildStringValue(OldOperand, Replacements, Cache);
    Replacement->setOperand(Index, NewOperand);
    AnyChanged |= NewOperand != OldOperand;
  }
  if (!AnyChanged) {
    Replacement->deleteValue();
    return V;
  }
  Replacement->setName("xorstr.value");
  Replacement->insertAfter(I);
  Cache[V] = Replacement;
  return Replacement;
}

uint64_t automaticEncryptionDomain(ArrayRef<CallStringArg> Worklist,
                                   unsigned WordBits) {
  SmallVector<uint8_t, 1024> Material;
  appendDomainWord(Material, WordBits);
  appendDomainWord(Material, Worklist.size());
  for (const CallStringArg &Entry : Worklist) {
    appendDomainBytes(Material, Entry.CB->getFunction()->getName());
    appendDomainWord(Material, Entry.ArgIdx);
    appendDomainWord(Material, Entry.Candidates.size());
    for (const StringCandidate &Candidate : Entry.Candidates) {
      appendDomainWord(Material, Candidate.ByteOffset);
      auto *CDA = cast<ConstantDataArray>(Candidate.GV->getInitializer());
      appendDomainWord(Material,
                       CDA->getElementType()->getPrimitiveSizeInBits());
      appendDomainBytes(Material, CDA->getRawDataValues());
    }
  }
  return xxh3_64bits(Material);
}

uint64_t finalizationDomain(ArrayRef<CallBase *> Worklist, const DataLayout &DL,
                            unsigned WordBits) {
  SmallVector<uint8_t, 1024> Material;
  appendDomainWord(Material, WordBits);
  appendDomainWord(Material, Worklist.size());
  for (CallBase *CB : Worklist) {
    appendDomainBytes(Material, CB->getFunction()->getName());

    auto *LengthToken = dyn_cast<ConstantInt>(CB->getArgOperand(1));
    auto *OldKeyValue = dyn_cast<ConstantInt>(CB->getArgOperand(2));
    uint64_t ByteOffset = 0;
    GlobalVariable *OldGV =
        findStringGlobalWithOffset(CB->getArgOperand(0), DL, ByteOffset);
    if (!LengthToken || !OldKeyValue || !OldGV) {
      appendDomainWord(Material, 0);
      continue;
    }

    const uint64_t OldKey =
        xorstr::truncateWord(OldKeyValue->getZExtValue(), WordBits);
    const uint64_t Token =
        xorstr::truncateWord(LengthToken->getZExtValue(), WordBits);
    const uint64_t Length = xorstr::truncateWord(
        Token ^ xorstr::lengthMask(OldKey, WordBits), WordBits);
    appendDomainWord(Material, Length);
    appendDomainWord(Material, Token);
    appendDomainWord(Material, OldKey);
    appendDomainWord(Material, ByteOffset);

    SmallVector<uint8_t, 256> Bytes;
    if (!readI8Array(OldGV, ByteOffset, Length, Bytes)) {
      appendDomainWord(Material, 0);
      continue;
    }
    appendDomainWord(Material, Bytes.size());
    Material.append(Bytes.begin(), Bytes.end());
  }
  return xxh3_64bits(Material);
}

} // anonymous namespace

PreservedAnalyses EncryptCallStringsPass::run(Module &M,
                                              ModuleAnalysisManager &MAM) {
  const DataLayout &DL = M.getDataLayout();
  unsigned PtrBits = DL.getPointerSizeInBits();
  if (!isSupportedWordBits(PtrBits)) {
    M.getContext().emitError(
        "NeverC xorstr requires a power-of-two pointer width from 8 to 64 "
        "bits");
    return PreservedAnalyses::all();
  }
  MDNode *XorstrMD = MDNode::get(M.getContext(), {});
  const bool CanonicalizedArguments = promoteCallArgumentAllocas(M);
  bool DiagnosedUnsupported = false;

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
        if (CB->isInlineAsm())
          continue;
        if (isXorStrDecryptCall(*CB))
          continue;
        for (unsigned i = 0, e = CB->arg_size(); i < e; ++i) {
          SmallVector<StringCandidate, 2> Candidates;
          SmallPtrSet<Value *, 8> SeenValues;
          collectStringCandidates(CB->getArgOperand(i), DL, MaxLen, Candidates,
                                  SeenValues);
          if (Candidates.empty())
            continue;
          if (CB->isMustTailCall()) {
            if (!CB->hasMetadata(AutoMustTailDiagnosticMetadataName)) {
              M.getContext().emitError(
                  "NeverC automatic string encryption cannot protect a "
                  "musttail argument; remove musttail or pass an already "
                  "protected value from the callee");
              CB->setMetadata(AutoMustTailDiagnosticMetadataName, XorstrMD);
              DiagnosedUnsupported = true;
            }
            break;
          }
          Worklist.push_back({CB, i, std::move(Candidates)});
        }
      }
    }
  }

  if (Worklist.empty())
    return CanonicalizedArguments || DiagnosedUnsupported
               ? PreservedAnalyses::none()
               : PreservedAnalyses::all();

  std::optional<std::mt19937_64> GeneratorValue = makeKeyGenerator(
      KeySeed, automaticEncryptionDomain(Worklist, PtrBits), M.getContext());
  if (!GeneratorValue)
    return PreservedAnalyses::none();
  std::mt19937_64 &Generator = *GeneratorValue;

  struct GeneratedString {
    GlobalVariable *GV;
    AllocaInst *Buffer;
  };
  DenseMap<Function *, SmallVector<GeneratedString, 4>> GeneratedByFunction;
  SmallVector<WeakTrackingVH, 16> ReplacedArgumentRoots;

  for (auto &Entry : Worklist) {
    CallBase *CB = Entry.CB;
    Function *F = CB->getFunction();
    DenseMap<Value *, Value *> Replacements;
    auto &Generated = GeneratedByFunction[F];

    for (const StringCandidate &Candidate : Entry.Candidates) {
      if (Replacements.contains(Candidate.Source))
        continue;

      AllocaInst *Buffer = nullptr;
      for (const GeneratedString &Existing : Generated) {
        if (Existing.GV == Candidate.GV) {
          Buffer = Existing.Buffer;
          break;
        }
      }

      if (!Buffer) {
        GlobalVariable *GV = Candidate.GV;
        auto *CDA = cast<ConstantDataArray>(GV->getInitializer());
        const uint64_t NumElts = CDA->getNumElements();
        const uint64_t EltBytes =
            CDA->getElementType()->getPrimitiveSizeInBits() / 8;
        const uint64_t TotalBytes = NumElts * EltBytes;

        const uint64_t Key = generateDifferentKey(Generator, PtrBits, 0);
        const xorstr::CipherSchedule Schedule =
            xorstr::makeSchedule(Key, TotalBytes, PtrBits);

        SmallVector<uint8_t, 256> EncBytes(TotalBytes);
        StringRef RawBytes = CDA->getRawDataValues();
        uint64_t State = Schedule.InitialState;
        for (uint64_t I = 0; I < TotalBytes; ++I) {
          State = xorstr::advanceState(State, I, Schedule, PtrBits);
          EncBytes[I] = static_cast<uint8_t>(RawBytes[I]) ^
                        xorstr::streamByte(State, Schedule, PtrBits);
        }

        Constant *EncInit = ConstantDataArray::getRaw(
            StringRef(reinterpret_cast<const char *>(EncBytes.data()),
                      TotalBytes),
            NumElts, CDA->getElementType());
        const Align BufferAlign =
            DL.getValueOrABITypeAlignment(GV->getAlign(), GV->getValueType());
        auto *EncGV =
            new GlobalVariable(M, EncInit->getType(), true,
                               GlobalValue::PrivateLinkage, EncInit, "");
        EncGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
        EncGV->setAlignment(BufferAlign);
        EncGV->setMetadata("neverc.xorstr", XorstrMD);

        IntegerType *WordTy = IntegerType::get(M.getContext(), PtrBits);
        auto *StateGV = new GlobalVariable(
            M, WordTy, false, GlobalValue::PrivateLinkage,
            ConstantInt::get(WordTy, Schedule.InitialState), "");
        StateGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
        StateGV->setAlignment(DL.getABITypeAlign(WordTy));
        StateGV->setMetadata("neverc.xorstr", XorstrMD);

        IRBuilder<> AllocaBuilder(&*F->getEntryBlock().getFirstInsertionPt());
        Buffer = AllocaBuilder.CreateAlloca(
            ArrayType::get(AllocaBuilder.getInt8Ty(), TotalBytes), nullptr,
            "xorstr.buf");
        Buffer->setAlignment(BufferAlign);
        Buffer->setMetadata("neverc.xorstr", XorstrMD);

        // Decrypt once per source global and function invocation.  Besides
        // avoiding duplicate work, this preserves observable pointer identity
        // across multiple calls and between base/interior pointers derived
        // from the same source object.
        BasicBlock &EntryBB = F->getEntryBlock();
        auto Anchor = EntryBB.begin();
        while (Anchor != EntryBB.end() && isa<AllocaInst>(*Anchor))
          ++Anchor;
        assert(Anchor != EntryBB.end() &&
               "function entry must contain a terminator");
        BasicBlock *PostDecryptBB =
            EntryBB.splitBasicBlock(&*Anchor, "xorstr.entry");
        EntryBB.getTerminator()->eraseFromParent();
        IRBuilder<> B(&EntryBB);

        LoadInst *OpaqueInitialState = B.CreateLoad(WordTy, StateGV);
        OpaqueInitialState->setVolatile(true);
        emitDecryptLoop(B, Buffer, EncGV, TotalBytes, Schedule, PtrBits, Key,
                        OpaqueInitialState,
                        /*UseVolatileCiphertext=*/true);
        B.CreateBr(PostDecryptBB);
        Generated.push_back({Candidate.GV, Buffer});
      }

      Value *ArgumentBuffer = Buffer;
      if (Candidate.ByteOffset != 0) {
        IRBuilder<> OffsetBuilder(Buffer->getParent(),
                                  std::next(Buffer->getIterator()));
        ArgumentBuffer = OffsetBuilder.CreateInBoundsGEP(
            OffsetBuilder.getInt8Ty(), Buffer,
            OffsetBuilder.getInt64(Candidate.ByteOffset));
      }
      Value *Replacement = ArgumentBuffer;
      if (Replacement->getType() != Candidate.Source->getType()) {
        auto *Definition = cast<Instruction>(ArgumentBuffer);
        IRBuilder<> CastBuilder(Definition->getParent(),
                                std::next(Definition->getIterator()));
        Replacement = CastBuilder.CreateBitOrPointerCast(
            ArgumentBuffer, Candidate.Source->getType());
      }
      Replacements[Candidate.Source] = Replacement;
    }

    Value *OldArgument = CB->getArgOperand(Entry.ArgIdx);
    IRBuilder<> ArgumentBuilder(CB);
    DenseMap<Value *, Value *> RebuildCache;
    Value *NewArgument =
        rebuildStringValue(OldArgument, Replacements, RebuildCache);
    if (NewArgument->getType() != OldArgument->getType())
      NewArgument = ArgumentBuilder.CreateBitOrPointerCast(
          NewArgument, OldArgument->getType());
    CB->setArgOperand(Entry.ArgIdx, NewArgument);
    if (isa<Instruction>(OldArgument))
      ReplacedArgumentRoots.push_back(OldArgument);
  }

  for (WeakTrackingVH &Root : ReplacedArgumentRoots) {
    auto *I = dyn_cast_or_null<Instruction>(static_cast<Value *>(Root));
    if (!I)
      continue;
    if (auto *Phi = dyn_cast<PHINode>(I))
      if (RecursivelyDeleteDeadPHINode(Phi))
        continue;
    RecursivelyDeleteTriviallyDeadInstructions(I);
  }

  SmallPtrSet<GlobalVariable *, 16> Processed;
  for (auto &Entry : Worklist) {
    for (const StringCandidate &Candidate : Entry.Candidates) {
      GlobalVariable *GV = Candidate.GV;
      if (!Processed.insert(GV).second)
        continue;
      GV->removeDeadConstantUsers();
      if (GV->use_empty())
        GV->eraseFromParent();
    }
  }

  return PreservedAnalyses::none();
}

PreservedAnalyses FinalizeXorStrPass::run(Module &M, ModuleAnalysisManager &) {
  const DataLayout &DL = M.getDataLayout();
  const unsigned WordBits = DL.getPointerSizeInBits();
  bool Changed = false;
  FunctionAnalysisManager FAM;
  for (Function &F : M)
    if (!F.isDeclaration())
      Changed |= !XorStrCleanupPass().run(F, FAM).areAllPreserved();

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
    Changed |= removeDecryptSupport(M);
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  // Finalization is installed as a mandatory code-generation boundary even
  // for modules that do not use xorstr.  Only validate the word model once a
  // decoder call actually needs lowering; otherwise an unrelated target with
  // an unusual pointer width would fail merely because this no-op pass was in
  // its pipeline.
  if (!isSupportedWordBits(WordBits)) {
    M.getContext().emitError(
        "NeverC xorstr requires a power-of-two pointer width from 8 to 64 "
        "bits");
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  std::optional<std::mt19937_64> GeneratorValue = makeKeyGenerator(
      KeySeed, finalizationDomain(Worklist, DL, WordBits), M.getContext());
  if (!GeneratorValue)
    return PreservedAnalyses::none();
  std::mt19937_64 &Generator = *GeneratorValue;
  MDNode *XorstrMD = MDNode::get(M.getContext(), {});
  SmallPtrSet<GlobalVariable *, 16> OldGlobals;
  for (CallBase *CB : Worklist) {
    if (!CB->getType()->isPointerTy() ||
        !CB->getArgOperand(3)->getType()->isPointerTy()) {
      M.getContext().emitError(
          "NeverC xorstr finalization found an invalid decoder ABI");
      continue;
    }
    auto *LengthToken = dyn_cast<ConstantInt>(CB->getArgOperand(1));
    auto *OldKeyValue = dyn_cast<ConstantInt>(CB->getArgOperand(2));
    uint64_t ByteOffset = 0;
    GlobalVariable *OldGV =
        findStringGlobalWithOffset(CB->getArgOperand(0), DL, ByteOffset);
    if (!LengthToken || !OldKeyValue || !OldGV)
      continue;

    const uint64_t OldKey =
        xorstr::truncateWord(OldKeyValue->getZExtValue(), WordBits);
    const uint64_t Token =
        xorstr::truncateWord(LengthToken->getZExtValue(), WordBits);
    const uint64_t Length = xorstr::truncateWord(
        Token ^ xorstr::lengthMask(OldKey, WordBits), WordBits);

    SmallVector<uint8_t, 256> OldBytes;
    if (!readI8Array(OldGV, ByteOffset, Length, OldBytes))
      continue;

    if (Length == std::numeric_limits<uint64_t>::max() ||
        !outputHasSufficientStackStorage(CB->getArgOperand(3), Length + 1,
                                         DL)) {
      M.getContext().emitError(
          "NeverC xorstr decoder output must resolve to fixed stack storage "
          "large enough for the decoded bytes and trailing NUL");
      continue;
    }

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
    if (OldGV->removeDeadConstantUsers(); OldGV->use_empty())
      OldGV->eraseFromParent();

  Changed |= removeDecryptSupport(M);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace xorstr
} // namespace neverc
