#include "neverc/Shellcode/IR/Data2TextPass.h"
#include "neverc/Shellcode/IR/Data2TextABI.h"
#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include <cstring>

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

bool replaceScalarLoads(GlobalVariable &GV) {
  Constant *Init = GV.getInitializer();
  if (!Init)
    return false;

  SmallVector<LoadInst *, 4> Loads;
  Loads.reserve(GV.getNumUses());
  for (User *U : GV.users())
    if (auto *LI = dyn_cast<LoadInst>(U))
      if (LI->getType() == Init->getType())
        Loads.push_back(LI);

  if (Loads.empty())
    return false;

  for (auto *LI : Loads) {
    LI->replaceAllUsesWith(Init);
    LI->eraseFromParent();
  }
  return true;
}

struct StackifyContext {
  Module &M;
  const DataLayout &DL;
  DenseMap<std::pair<Function *, GlobalValue *>, Value *> Cache;
};

Value *getOrMaterialize(StackifyContext &Ctx, Function *F, GlobalValue *GV);
bool writeInto(IRBuilder<> &B, StackifyContext &Ctx, Function *F, Value *Base,
               uint64_t Off, Constant *Init);

void emitRawBytes(IRBuilder<> &B, Value *Base, uint64_t Off,
                  ArrayRef<uint8_t> Bytes, bool IsVolatile = false) {
  Type *I8 = B.getInt8Ty();
  Type *I16 = B.getInt16Ty();
  Type *I32 = B.getInt32Ty();
  Type *I64 = B.getInt64Ty();

  uint64_t I = 0;
  while (I + 8 <= Bytes.size()) {
    uint64_t QW = 0;
    std::memcpy(&QW, Bytes.data() + I, 8);
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(I64, QW), Ptr, Align(1), IsVolatile);
    I += 8;
  }
  if (I + 4 <= Bytes.size()) {
    uint32_t DW = 0;
    std::memcpy(&DW, Bytes.data() + I, 4);
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(I32, DW), Ptr, Align(1), IsVolatile);
    I += 4;
  }
  if (I + 2 <= Bytes.size()) {
    uint16_t HW = 0;
    std::memcpy(&HW, Bytes.data() + I, 2);
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(I16, HW), Ptr, Align(1), IsVolatile);
    I += 2;
  }
  if (I < Bytes.size()) {
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(I8, Bytes[I]), Ptr, Align(1),
                         IsVolatile);
  }
}

void emitZeroBytes(IRBuilder<> &B, Value *Base, uint64_t Off, uint64_t Sz) {
  if (Sz == 0)
    return;
  Type *I8 = B.getInt8Ty();
  Type *I64 = B.getInt64Ty();
  Constant *Zero64 = ConstantInt::get(I64, 0);

  uint64_t I = 0;
  while (I + 8 <= Sz) {
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(Zero64, Ptr, Align(1));
    I += 8;
  }
  if (I + 4 <= Sz) {
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(B.getInt32Ty(), 0), Ptr, Align(1));
    I += 4;
  }
  if (I + 2 <= Sz) {
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(B.getInt16Ty(), 0), Ptr, Align(1));
    I += 2;
  }
  if (I < Sz) {
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off + I));
    B.CreateAlignedStore(ConstantInt::get(I8, 0), Ptr, Align(1));
  }
}

bool writeInto(IRBuilder<> &B, StackifyContext &Ctx, Function *F, Value *Base,
               uint64_t Off, Constant *Init) {
  Type *Ty = Init->getType();
  uint64_t Sz = Ctx.DL.getTypeAllocSize(Ty);

  if (isa<ConstantAggregateZero>(Init) || isa<UndefValue>(Init) ||
      isa<ConstantPointerNull>(Init)) {
    emitZeroBytes(B, Base, Off, Sz);
    return true;
  }

  if (auto *CI = dyn_cast<ConstantInt>(Init)) {
    uint64_t Bytes = Ctx.DL.getTypeStoreSize(Ty);
    SmallVector<uint8_t, 16> Buf(Bytes, 0);
    APInt V = CI->getValue();
    for (uint64_t I = 0; I < Bytes; ++I)
      Buf[I] = static_cast<uint8_t>(
          V.extractBitsAsZExtValue(8, static_cast<unsigned>(I * 8)));
    emitRawBytes(B, Base, Off, Buf);
    return true;
  }

  if (auto *CFP = dyn_cast<ConstantFP>(Init)) {
    uint64_t Bytes = Ctx.DL.getTypeStoreSize(Ty);
    SmallVector<uint8_t, 16> Buf(Bytes, 0);
    APInt V = CFP->getValueAPF().bitcastToAPInt();
    for (uint64_t I = 0; I < Bytes; ++I)
      Buf[I] = static_cast<uint8_t>(
          V.extractBitsAsZExtValue(8, static_cast<unsigned>(I * 8)));
    emitRawBytes(B, Base, Off, Buf);
    return true;
  }

  if (auto *CDS = dyn_cast<ConstantDataSequential>(Init)) {
    StringRef Raw = CDS->getRawDataValues();
    SmallVector<uint8_t, 64> Buf(Raw.begin(), Raw.end());
    if (Buf.size() < Sz)
      Buf.resize(Sz, 0);
    else if (Buf.size() > Sz)
      Buf.resize(Sz);
    emitRawBytes(B, Base, Off, Buf);
    return true;
  }

  if (auto *CA = dyn_cast<ConstantArray>(Init)) {
    Type *EltTy = CA->getType()->getElementType();
    uint64_t EltSz = Ctx.DL.getTypeAllocSize(EltTy);
    for (unsigned I = 0, N = CA->getNumOperands(); I < N; ++I)
      if (!writeInto(B, Ctx, F, Base, Off + I * EltSz, CA->getOperand(I)))
        return false;
    return true;
  }

  if (auto *CS = dyn_cast<ConstantStruct>(Init)) {
    const StructLayout *SL = Ctx.DL.getStructLayout(CS->getType());
    for (unsigned I = 0, N = CS->getNumOperands(); I < N; ++I)
      if (!writeInto(B, Ctx, F, Base, Off + SL->getElementOffset(I),
                     CS->getOperand(I)))
        return false;
    return true;
  }

  if (auto *CV = dyn_cast<ConstantVector>(Init)) {
    Type *EltTy = CV->getType()->getElementType();
    uint64_t EltSz = Ctx.DL.getTypeAllocSize(EltTy);
    for (unsigned I = 0, N = CV->getType()->getNumElements(); I < N; ++I)
      if (!writeInto(B, Ctx, F, Base, Off + I * EltSz, CV->getOperand(I)))
        return false;
    return true;
  }

  if (auto *GV = dyn_cast<GlobalValue>(Init)) {
    Value *Resolved = getOrMaterialize(Ctx, F, GV);
    if (!Resolved)
      return false;
    Type *I8 = B.getInt8Ty();
    Type *I64 = B.getInt64Ty();
    Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off));
    B.CreateAlignedStore(Resolved, Ptr,
                         Ctx.DL.getABITypeAlign(Resolved->getType()));
    return true;
  }

  if (auto *CE = dyn_cast<ConstantExpr>(Init)) {
    if (CE->getOpcode() == Instruction::GetElementPtr ||
        CE->getOpcode() == Instruction::BitCast ||
        CE->getOpcode() == Instruction::AddrSpaceCast ||
        CE->getOpcode() == Instruction::IntToPtr ||
        CE->getOpcode() == Instruction::PtrToInt) {
      auto *Target = dyn_cast<GlobalValue>(CE->getOperand(0));
      if (Target) {
        Value *Resolved = getOrMaterialize(Ctx, F, Target);
        if (!Resolved)
          return false;

        Value *Val = Resolved;
        if (auto *GEP = dyn_cast<GEPOperator>(CE)) {
          APInt Acc(Ctx.DL.getIndexTypeSizeInBits(GEP->getType()), 0);
          if (GEP->accumulateConstantOffset(Ctx.DL, Acc)) {
            if (Acc.getSExtValue() != 0) {
              Type *I8 = B.getInt8Ty();
              Type *I64 = B.getInt64Ty();
              Val = B.CreateInBoundsGEP(
                  I8, Val, ConstantInt::get(I64, Acc.getSExtValue()));
            }
          } else {
            return false; // non-constant GEP offset
          }
        }

        Type *I8 = B.getInt8Ty();
        Type *I64 = B.getInt64Ty();
        Value *Ptr = B.CreateInBoundsGEP(I8, Base, ConstantInt::get(I64, Off));
        B.CreateAlignedStore(Val, Ptr, Ctx.DL.getABITypeAlign(Val->getType()));
        return true;
      }
    }
    return false;
  }

  return false;
}

Value *getOrMaterialize(StackifyContext &Ctx, Function *F, GlobalValue *GV) {
  auto Key = std::make_pair(F, GV);
  auto It = Ctx.Cache.find(Key);
  if (It != Ctx.Cache.end())
    return It->second;

  if (auto *Fn = dyn_cast<Function>(GV)) {
    Ctx.Cache[Key] = Fn;
    return Fn;
  }

  auto *GVar = dyn_cast<GlobalVariable>(GV);
  if (!GVar || !GVar->isConstant() || !GVar->hasInitializer())
    return nullptr;

  BasicBlock &Entry = F->getEntryBlock();
  IRBuilder<> Builder(&Entry, Entry.getFirstInsertionPt());

  Type *ValTy = GVar->getValueType();
  Align A = Ctx.DL.getABITypeAlign(ValTy);
  if (A < Align(8))
    A = Align(8);

  auto *Alloca = Builder.CreateAlloca(ValTy, nullptr, GVar->getName() + ".stk");
  Alloca->setAlignment(A);
  Ctx.Cache[Key] = Alloca; // cache before recursing

  if (!writeInto(Builder, Ctx, F, Alloca, 0, GVar->getInitializer())) {
    Ctx.Cache.erase(Key);
    return nullptr;
  }
  return Alloca;
}

struct GVUserAnalysis {
  SmallVector<Function *, 4> Functions;
  SmallVector<ConstantExpr *, 8> ConstExprs;
};

GVUserAnalysis analyzeGVUsers(GlobalVariable &GV) {
  GVUserAnalysis Result;
  SmallPtrSet<Function *, 4> SeenFns;
  SmallPtrSet<ConstantExpr *, 8> SeenCEs;
  SmallPtrSet<User *, 16> Visited;
  SmallVector<User *, 16> Stack;
  Stack.reserve(GV.getNumUses());
  for (User *U : GV.users())
    Stack.push_back(U);

  while (!Stack.empty()) {
    User *U = Stack.pop_back_val();
    if (!Visited.insert(U).second)
      continue;
    if (auto *I = dyn_cast<Instruction>(U)) {
      if (auto *F = I->getFunction())
        if (SeenFns.insert(F).second)
          Result.Functions.push_back(F);
      continue;
    }
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      if (CE->getOpcode() == Instruction::GetElementPtr ||
          CE->getOpcode() == Instruction::BitCast ||
          CE->getOpcode() == Instruction::AddrSpaceCast) {
        if (SeenCEs.insert(CE).second)
          Result.ConstExprs.push_back(CE);
      }
      for (User *UU : CE->users())
        Stack.push_back(UU);
      continue;
    }
    if (auto *C = dyn_cast<Constant>(U)) {
      for (User *UU : C->users())
        Stack.push_back(UU);
    }
  }
  return Result;
}

void lowerCollectedConstExprs(ArrayRef<ConstantExpr *> CEs) {
  for (auto *CE : CEs) {
    SmallVector<User *, 8> CEUsers(CE->users());
    for (User *CEU : CEUsers) {
      auto *InstU = dyn_cast<Instruction>(CEU);
      if (!InstU)
        continue;
      Instruction *NewInst = CE->getAsInstruction();
      NewInst->insertBefore(InstU->getIterator());
      InstU->replaceUsesOfWith(CE, NewInst);
    }
  }
}

bool inlineConstantOperands(Module &M) {
  bool Changed = false;
  LLVMContext &Ctx = M.getContext();

  SmallVector<std::pair<Use *, ConstantFP *>, 16> FloatWork;
  SmallVector<std::pair<Use *, Constant *>, 16> VectorWork;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (F.getName().starts_with(ir::kScPrefix))
      continue;
    if (F.hasInternalLinkage() && F.use_empty())
      continue;

    FloatWork.clear();
    VectorWork.clear();

    for (BasicBlock &BB : F)
      for (Instruction &I : BB) {
        bool SkipVec = isa<InsertElementInst>(&I) ||
                       isa<ExtractElementInst>(&I) ||
                       isa<ShuffleVectorInst>(&I) || isa<StoreInst>(&I);
        for (Use &U : I.operands()) {
          Value *V = U.get();
          if (auto *FP = dyn_cast<ConstantFP>(V)) {
            FloatWork.push_back({&U, FP});
            continue;
          }
          if (SkipVec)
            continue;
          auto *C = dyn_cast<Constant>(V);
          if (!C)
            continue;
          auto *VTy = dyn_cast<FixedVectorType>(C->getType());
          if (!VTy)
            continue;
          if (isa<ConstantAggregateZero>(C) || isa<UndefValue>(C) ||
              isa<PoisonValue>(C))
            continue;
          Type *EltTy = VTy->getElementType();
          if (!EltTy->isIntegerTy() && !EltTy->isFloatingPointTy())
            continue;
          uint64_t EltBits = EltTy->getPrimitiveSizeInBits();
          if (EltBits == 0 || EltBits > 64)
            continue;
          VectorWork.push_back({&U, C});
        }
      }

    if (FloatWork.empty() && VectorWork.empty())
      continue;

    BasicBlock &Entry = F.getEntryBlock();
    IRBuilder<> EntryBuilder(&Entry, Entry.getFirstInsertionPt());

    AllocaInst *OpaqueSlot = nullptr;
    DenseMap<unsigned, AllocaInst *> WideFPSlots; // > 64 bits (rare)
    auto getOpaqueSlot = [&]() -> AllocaInst * {
      if (!OpaqueSlot) {
        Type *I64 = Type::getInt64Ty(Ctx);
        OpaqueSlot =
            EntryBuilder.CreateAlloca(I64, nullptr, "__sc_opaque_slot");
      }
      return OpaqueSlot;
    };

    if (!FloatWork.empty()) {
      for (auto &Pair : FloatWork) {
        Use *U = Pair.first;
        ConstantFP *FP = Pair.second;
        Type *FPTy = FP->getType();

        APInt IntBits = FP->getValueAPF().bitcastToAPInt();
        unsigned BW = IntBits.getBitWidth();
        Type *IntTy = IntegerType::get(Ctx, BW);

        auto *User = cast<Instruction>(U->getUser());
        IRBuilder<> B(User);

        if (BW <= 64) {
          AllocaInst *Slot = getOpaqueSlot();
          Type *I64 = Type::getInt64Ty(Ctx);
          Constant *Wide = ConstantInt::get(I64, IntBits.zext(64));
          auto *Store = B.CreateStore(Wide, Slot);
          Store->setVolatile(true);
          auto *Load =
              B.CreateLoad(I64, Slot, /*isVolatile=*/true, "__sc_fp_bits");
          Value *Narrow = BW < 64 ? B.CreateTrunc(Load, IntTy) : Load;
          Value *Cast = B.CreateBitCast(Narrow, FPTy, "__sc_fp");
          U->set(Cast);
        } else {
          auto &WSlot = WideFPSlots[BW];
          if (!WSlot)
            WSlot = EntryBuilder.CreateAlloca(IntTy, nullptr,
                                              "__sc_fp_slot." + Twine(BW));
          Constant *CI = ConstantInt::get(IntTy, IntBits);
          auto *Store = B.CreateStore(CI, WSlot);
          Store->setVolatile(true);
          auto *Load =
              B.CreateLoad(IntTy, WSlot, /*isVolatile=*/true, "__sc_fp_bits");
          Value *Cast = B.CreateBitCast(Load, FPTy, "__sc_fp");
          U->set(Cast);
        }
      }
      Changed = true;
    }

    if (!VectorWork.empty()) {
      Type *I64 = Type::getInt64Ty(Ctx);
      AllocaInst *VecSlot = getOpaqueSlot();

      auto buildLaneChain = [&](IRBuilder<> &B, Constant *C,
                                FixedVectorType *VTy) -> Value * {
        unsigned NumE = VTy->getNumElements();
        Type *EltTy = VTy->getElementType();
        Value *Acc = PoisonValue::get(VTy);
        for (unsigned I = 0; I < NumE; ++I) {
          Constant *Elt = C->getAggregateElement(I);
          if (!Elt)
            return nullptr;

          APInt Bits;
          if (auto *CI = dyn_cast<ConstantInt>(Elt))
            Bits = CI->getValue().zextOrTrunc(64);
          else if (auto *CFP = dyn_cast<ConstantFP>(Elt))
            Bits = CFP->getValueAPF().bitcastToAPInt().zextOrTrunc(64);
          else
            return nullptr;

          Constant *BitsK = ConstantInt::get(I64, Bits);
          auto *Store = B.CreateStore(BitsK, VecSlot);
          Store->setVolatile(true);
          auto *Load =
              B.CreateLoad(I64, VecSlot, /*isVolatile=*/true, "__sc_vec_lane");
          Value *Lane;
          if (EltTy->isIntegerTy()) {
            unsigned EBits = EltTy->getIntegerBitWidth();
            Lane = EBits < 64 ? B.CreateTrunc(Load, EltTy) : Load;
          } else {
            unsigned EBits = EltTy->getPrimitiveSizeInBits();
            Type *IntTy = IntegerType::get(Ctx, EBits);
            Value *Trunc = EBits < 64 ? B.CreateTrunc(Load, IntTy) : Load;
            Lane = B.CreateBitCast(Trunc, EltTy);
          }
          Acc = B.CreateInsertElement(Acc, Lane, ConstantInt::get(I64, I));
        }
        return Acc;
      };

      for (auto &Pair : VectorWork) {
        Use *U = Pair.first;
        Constant *C = Pair.second;
        auto *VTy = cast<FixedVectorType>(C->getType());
        auto *User = cast<Instruction>(U->getUser());

        if (auto *Phi = dyn_cast<PHINode>(User)) {
          unsigned IncomingIdx = U->getOperandNo();
          BasicBlock *Pred = Phi->getIncomingBlock(IncomingIdx);
          Instruction *Term = Pred->getTerminator();
          IRBuilder<> B(Term);
          Value *Acc = buildLaneChain(B, C, VTy);
          if (!Acc)
            continue;
          U->set(Acc);
          Changed = true;
          continue;
        }

        IRBuilder<> B(User);
        Value *Acc = buildLaneChain(B, C, VTy);
        if (!Acc)
          continue;
        U->set(Acc);
        Changed = true;
      }
    }
  }
  return Changed;
}

bool eliminateConstantGlobals(Module &M) {
  bool Changed = false;
  const DataLayout &DL = M.getDataLayout();

  SmallVector<GlobalVariable *, 16> Scalars;
  SmallVector<GlobalVariable *, 16> Candidates;
  Candidates.reserve(M.global_size());
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getName().starts_with(ir::kLlvmDotPrefix))
      continue;
    if (!GV.isConstant() || !GV.hasInitializer())
      continue;
    if (DL.getTypeAllocSize(GV.getValueType()) <= 8)
      Scalars.push_back(&GV);
    Candidates.push_back(&GV);
  }

  for (GlobalVariable *GV : Scalars)
    if (replaceScalarLoads(*GV))
      Changed = true;

  StackifyContext Ctx{M, DL, decltype(StackifyContext::Cache)()};

  SmallPtrSet<GlobalVariable *, 8> Handled;
  bool Progress = true;
  while (Progress) {
    Progress = false;

    for (GlobalVariable *&Slot : Candidates) {
      if (!Slot || Slot->use_empty() || Handled.contains(Slot))
        continue;

      GVUserAnalysis Info = analyzeGVUsers(*Slot);
      if (Info.Functions.empty())
        continue;

      lowerCollectedConstExprs(Info.ConstExprs);

      bool AllGood = true;
      for (Function *F : Info.Functions) {
        if (!getOrMaterialize(Ctx, F, Slot)) {
          AllGood = false;
          break;
        }
      }
      if (!AllGood)
        continue;

      SmallVector<Instruction *, 8> InstUsers;
      InstUsers.reserve(Slot->getNumUses());
      for (User *U : Slot->users())
        if (auto *I = dyn_cast<Instruction>(U))
          InstUsers.push_back(I);

      for (Instruction *I : InstUsers) {
        Function *F = I->getFunction();
        if (!F)
          continue;
        auto It = Ctx.Cache.find(std::make_pair(F, cast<GlobalValue>(Slot)));
        if (It == Ctx.Cache.end())
          continue;
        I->replaceUsesOfWith(Slot, It->second);
      }

      Handled.insert(Slot);
      Progress = true;
      Changed = true;
    }

    for (GlobalVariable *&Slot : Candidates) {
      if (!Slot)
        continue;
      if (Slot->use_empty()) {
        Handled.erase(Slot);
        Slot->eraseFromParent();
        Slot = nullptr;
        Progress = true;
        Changed = true;
      }
    }

    if (Progress) {
      llvm::erase_if(Candidates, [&](GlobalVariable *GV) {
        return !GV || Handled.contains(GV);
      });
    }
  }

  return Changed;
}

bool devectorizeConstantStores(Function &F) {
  bool Changed = false;
  SmallVector<Instruction *, 16> ToErase;

  SmallVector<StoreInst *, 16> StoresToExpand;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB) {
      auto *SI = dyn_cast<StoreInst>(&I);
      if (!SI)
        continue;
      Value *V = SI->getValueOperand();
      if (!isa<Constant>(V) || isa<GlobalValue>(V))
        continue;
      Type *VTy = V->getType();
      if (auto *CI = dyn_cast<ConstantInt>(V)) {
        (void)CI;
        StoresToExpand.push_back(SI);
      } else if (VTy->isVectorTy() || VTy->isArrayTy()) {
        StoresToExpand.push_back(SI);
      }
    }

  for (StoreInst *SI : StoresToExpand) {
    Constant *C = cast<Constant>(SI->getValueOperand());
    Type *VTy = C->getType();
    Module *M = SI->getModule();
    const DataLayout &DL = M->getDataLayout();

    auto expandBytes = [&](ArrayRef<uint8_t> Bytes) {
      IRBuilder<> B(SI);
      emitRawBytes(B, SI->getPointerOperand(), 0, Bytes, /*IsVolatile=*/true);
    };

    auto trySplitScalar = [&](ConstantInt *CI) -> bool {
      uint64_t Sz = DL.getTypeStoreSize(CI->getType());
      if (Sz == 1) {
        if (SI->isVolatile())
          return false;
        SI->setVolatile(true);
        return true;
      }
      SmallVector<uint8_t, 16> Bytes(Sz, 0);
      APInt V = CI->getValue();
      for (size_t I = 0; I < Sz; ++I)
        Bytes[I] = static_cast<uint8_t>(
            V.extractBitsAsZExtValue(8, static_cast<unsigned>(I * 8)));
      expandBytes(Bytes);
      ToErase.push_back(SI);
      return true;
    };

    auto tryDataSequential = [&](Constant *C) -> bool {
      if (auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
        StringRef Raw = CDS->getRawDataValues();
        SmallVector<uint8_t, 64> Bytes(Raw.begin(), Raw.end());
        expandBytes(Bytes);
        ToErase.push_back(SI);
        return true;
      }
      if (isa<ConstantAggregateZero>(C)) {
        if (VTy->isVectorTy())
          return false;
        uint64_t Sz = DL.getTypeStoreSize(VTy);
        SmallVector<uint8_t, 64> Bytes(Sz, 0);
        expandBytes(Bytes);
        ToErase.push_back(SI);
        return true;
      }
      if (auto *CV = dyn_cast<ConstantVector>(C)) {
        unsigned NumE = CV->getType()->getNumElements();
        Type *EltTy = CV->getType()->getElementType();
        if (!EltTy->isIntegerTy())
          return false;
        uint64_t EltBytes = DL.getTypeStoreSize(EltTy);
        SmallVector<uint8_t, 64> Bytes(EltBytes * NumE, 0);
        for (unsigned I = 0; I < NumE; ++I) {
          auto *EltC = dyn_cast<ConstantInt>(CV->getOperand(I));
          if (!EltC)
            return false;
          APInt V = EltC->getValue();
          for (uint64_t J = 0; J < EltBytes; ++J)
            Bytes[I * EltBytes + J] = static_cast<uint8_t>(
                V.extractBitsAsZExtValue(8, static_cast<unsigned>(J * 8)));
        }
        expandBytes(Bytes);
        ToErase.push_back(SI);
        return true;
      }
      return false;
    };

    if (auto *CI = dyn_cast<ConstantInt>(C)) {
      if (trySplitScalar(CI))
        Changed = true;
      continue;
    }
    if (VTy->isVectorTy() || VTy->isArrayTy()) {
      if (tryDataSequential(C))
        Changed = true;
      continue;
    }
  }

  for (Instruction *I : ToErase)
    I->eraseFromParent();

  return Changed;
}

bool hasGlobalDataRefs(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getName().starts_with(ir::kLlvmDotPrefix))
      continue;
    if (!GV.use_empty())
      return true;
  }
  return false;
}

} // namespace

PreservedAnalyses Data2TextPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;

  bool IsLatePhase =
      M.getNamedMetadata(Data2TextABI::PipelinePhaseSentinel.data()) != nullptr;

  if (!IsLatePhase) {
    if (inlineConstantOperands(M))
      Changed = true;
    if (eliminateConstantGlobals(M))
      Changed = true;
    M.getOrInsertNamedMetadata(Data2TextABI::PipelinePhaseSentinel.data());
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  if (inlineConstantOperands(M))
    Changed = true;

  if (eliminateConstantGlobals(M))
    Changed = true;

  for (Function &F : M)
    if (!F.isDeclaration() && !F.getName().starts_with(ir::kScPrefix) &&
        !(F.hasInternalLinkage() && F.use_empty()))
      if (devectorizeConstantStores(F))
        Changed = true;

  if (auto *N = M.getNamedMetadata(Data2TextABI::PipelinePhaseSentinel.data()))
    N->eraseFromParent();

  (void)hasGlobalDataRefs(M);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
