#include "neverc/Shellcode/IR/IndirectBrPass.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

Value *stripExt(Value *V) {
  while (true) {
    if (auto *Z = dyn_cast<ZExtInst>(V)) {
      V = Z->getOperand(0);
      continue;
    }
    if (auto *S = dyn_cast<SExtInst>(V)) {
      V = S->getOperand(0);
      continue;
    }
    break;
  }
  return V;
}

struct LoadSource {
  LoadInst *L;
  BasicBlock *FromBB;
};

bool collectLoads(Value *Root, SmallVectorImpl<LoadSource> &Out) {
  SmallPtrSet<Value *, 8> Visited;
  SmallVector<std::pair<Value *, BasicBlock *>, 4> Stack;
  Stack.push_back({Root, nullptr});
  while (!Stack.empty()) {
    auto [Cur, FromBB] = Stack.pop_back_val();
    if (!Visited.insert(Cur).second)
      continue;
    if (auto *LI = dyn_cast<LoadInst>(Cur)) {
      Out.push_back({LI, FromBB ? FromBB : LI->getParent()});
      continue;
    }
    if (auto *PN = dyn_cast<PHINode>(Cur)) {
      for (unsigned I = 0, N = PN->getNumIncomingValues(); I < N; ++I)
        Stack.push_back({PN->getIncomingValue(I), PN->getIncomingBlock(I)});
      continue;
    }
    return false;
  }
  return !Out.empty();
}

std::pair<Value *, Value *> getAsGEP(Value *V) {
  auto *GEP = dyn_cast<GEPOperator>(V);
  if (!GEP)
    return {nullptr, nullptr};
  if (GEP->getNumIndices() == 2) {
    auto *First = dyn_cast<ConstantInt>(GEP->getOperand(1));
    if (!First || !First->isZero())
      return {nullptr, nullptr};
    return {GEP->getPointerOperand(), GEP->getOperand(2)};
  }
  if (GEP->getNumIndices() == 1)
    return {GEP->getPointerOperand(), GEP->getOperand(1)};
  return {nullptr, nullptr};
}

bool analyzeLoad(LoadInst *L, Function *Parent, GlobalVariable *&GV,
                 Value *&Index) {
  if (!L->getType()->isPointerTy())
    return false;
  if (L->isAtomic() || L->isVolatile())
    return false;
  auto [BaseV, IndexOp] = getAsGEP(L->getPointerOperand());
  if (!BaseV || !IndexOp)
    return false;
  auto *Cand = dyn_cast<GlobalVariable>(BaseV);
  if (!Cand || !Cand->hasInitializer() || !Cand->hasLocalLinkage())
    return false;
  if (!Cand->isConstant()) {
    for (User *U : Cand->users())
      if (auto *SI = dyn_cast<StoreInst>(U))
        if (SI->getPointerOperand() == Cand)
          return false;
  }
  Value *Stripped = stripExt(IndexOp);
  if (!Stripped->getType()->isIntegerTy())
    return false;
  GV = Cand;
  Index = Stripped;
  (void)Parent; // caller checks label set
  return true;
}

GlobalVariable *matchPattern(IndirectBrInst &IBI, Value *&Idx, Type *&IdxTy,
                             SmallVectorImpl<BasicBlock *> &Labels) {
  Function *Parent = IBI.getFunction();

  SmallVector<LoadSource, 4> Loads;
  if (!collectLoads(IBI.getAddress(), Loads))
    return nullptr;
  if (Loads.empty())
    return nullptr;

  GlobalVariable *CommonGV = nullptr;
  SmallVector<std::pair<Value *, BasicBlock *>, 4> PerBBIndex;
  Type *CommonIdxTy = nullptr;
  for (const LoadSource &LS : Loads) {
    GlobalVariable *GV = nullptr;
    Value *Index = nullptr;
    if (!analyzeLoad(LS.L, Parent, GV, Index))
      return nullptr;
    if (!CommonGV)
      CommonGV = GV;
    else if (CommonGV != GV)
      return nullptr;
    if (!CommonIdxTy)
      CommonIdxTy = Index->getType();
    else if (CommonIdxTy != Index->getType())
      return nullptr;
    PerBBIndex.push_back({Index, LS.FromBB});
  }

  auto *Arr = dyn_cast<ConstantArray>(CommonGV->getInitializer());
  if (!Arr)
    return nullptr;
  auto *ArrTy = dyn_cast<ArrayType>(Arr->getType());
  if (!ArrTy || !ArrTy->getElementType()->isPointerTy())
    return nullptr;

  SmallVector<BasicBlock *, 8> TableBBs;
  TableBBs.reserve(Arr->getNumOperands());
  for (Use &U : Arr->operands()) {
    auto *BA = dyn_cast<BlockAddress>(U.get());
    if (!BA || BA->getFunction() != Parent)
      return nullptr;
    TableBBs.push_back(BA->getBasicBlock());
  }

  SmallPtrSet<BasicBlock *, 4> IBISuccs;
  for (unsigned I = 0, N = IBI.getNumDestinations(); I < N; ++I)
    IBISuccs.insert(IBI.getDestination(I));
  for (BasicBlock *BB : TableBBs)
    if (!IBISuccs.count(BB))
      return nullptr;

  if (PerBBIndex.size() == 1) {
    Idx = PerBBIndex[0].first;
    IdxTy = CommonIdxTy;
    Labels.assign(TableBBs.begin(), TableBBs.end());
    return CommonGV;
  }

  BasicBlock *IBB = IBI.getParent();
  IRBuilder<> B(IBB, IBB->getFirstInsertionPt());
  auto *IdxPhi = B.CreatePHI(
      CommonIdxTy, static_cast<unsigned>(PerBBIndex.size()), "__sc_cgoto_idx");
  for (auto &Pair : PerBBIndex)
    IdxPhi->addIncoming(Pair.first, Pair.second);

  Idx = IdxPhi;
  IdxTy = CommonIdxTy;
  Labels.assign(TableBBs.begin(), TableBBs.end());
  return CommonGV;
}

bool rewriteOne(IndirectBrInst &IBI) {
  Value *Idx = nullptr;
  Type *IdxTy = nullptr;
  SmallVector<BasicBlock *, 8> Labels;
  GlobalVariable *Table = matchPattern(IBI, Idx, IdxTy, Labels);
  if (!Table)
    return false;

  IRBuilder<> B(&IBI);
  BasicBlock *Default = IBI.getDestination(0);

  auto *SI = B.CreateSwitch(Idx, Default, static_cast<unsigned>(Labels.size()));
  for (unsigned I = 0, N = Labels.size(); I < N; ++I) {
    SI->addCase(ConstantInt::get(cast<IntegerType>(IdxTy), I), Labels[I]);
  }
  IBI.eraseFromParent();
  return true;
}

} // namespace

PreservedAnalyses IndirectBrPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    SmallVector<IndirectBrInst *, 4> Worklist;
    for (BasicBlock &BB : F)
      if (auto *IBI = dyn_cast<IndirectBrInst>(BB.getTerminator()))
        Worklist.push_back(IBI);
    for (IndirectBrInst *IBI : Worklist)
      if (rewriteOne(*IBI))
        Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
