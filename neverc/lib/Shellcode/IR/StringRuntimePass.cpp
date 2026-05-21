#include "neverc/Shellcode/IR/StringRuntimePass.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

namespace ABI = StringRuntimeABI;

using RuntimeAllocatorRole = ABI::AllocatorRole;

struct RuntimeAllocatorClassification {
  bool Matched = false;
  RuntimeAllocatorRole Role = RuntimeAllocatorRole::Alloc;
};

struct ArenaLayout {
  Type *SizeTy;
  Type *I8Ty;
  PointerType *PtrTy;
  StructType *HeaderTy;
  uint64_t HeaderSize;
};

uint64_t alignUp(uint64_t Value, uint64_t Alignment) {
  return (Value + Alignment - 1) & ~(Alignment - 1);
}

Type *getSizeType(Module &M) {
  LLVMContext &Ctx = M.getContext();
  unsigned PointerBits = M.getDataLayout().getPointerSizeInBits();
  if (PointerBits == 0)
    PointerBits = 64;
  return IntegerType::get(Ctx, PointerBits);
}

ArenaLayout getArenaLayout(Module &M) {
  LLVMContext &Ctx = M.getContext();
  ArenaLayout Layout;
  Layout.SizeTy = getSizeType(M);
  Layout.I8Ty = Type::getInt8Ty(Ctx);
  Layout.PtrTy = PointerType::getUnqual(Ctx);

  static_assert(ABI::HeaderFieldCount == 4,
                "ArenaHeaderField enum and arena header layout disagree");
  Type *Fields[ABI::HeaderFieldCount] = {};
  Fields[ABI::HeaderSizeField] = Layout.SizeTy;
  Fields[ABI::HeaderNextField] = Layout.PtrTy;
  Fields[ABI::HeaderSelfField] = Layout.PtrTy;
  Fields[ABI::HeaderTagField] = Layout.SizeTy;
  Layout.HeaderTy = StructType::get(Ctx, ArrayRef<Type *>(Fields));

  Layout.HeaderSize = alignUp(
      M.getDataLayout().getTypeAllocSize(Layout.HeaderTy), ABI::ArenaAlignment);
  return Layout;
}

bool isBuiltinStringRuntimeFunction(const Function &F) {
  return F.hasFnAttribute(ABI::kRuntimeFnAttr);
}

void setRuntimeFunctionAttrs(Function &F, bool DeferInlining = false) {
  F.setLinkage(GlobalValue::InternalLinkage);
  F.setDSOLocal(true);
  if (DeferInlining) {
    F.removeFnAttr(Attribute::AlwaysInline);
    F.removeFnAttr(Attribute::OptimizeNone);
    F.addFnAttr(Attribute::NoInline);
  } else {
    F.removeFnAttr(Attribute::NoInline);
    F.removeFnAttr(Attribute::OptimizeNone);
    F.addFnAttr(Attribute::AlwaysInline);
  }
  F.addFnAttr(Attribute::NoUnwind);
}

RuntimeAllocatorClassification classifyRuntimeAllocator(StringRef Name) {
  StringRef Canon = ABI::canonicalSymbolName(Name);
  struct Row {
    StringRef Name;
    RuntimeAllocatorRole Role;
  };
#define NEVERC_STRING_RUNTIME_ALLOCATOR_ROLE_alloc RuntimeAllocatorRole::Alloc
#define NEVERC_STRING_RUNTIME_ALLOCATOR_ROLE_free RuntimeAllocatorRole::Free
  static constexpr Row kRows[] = {
#define NEVERC_STRING_RUNTIME_ALLOCATOR(name, role)                            \
  {#name, NEVERC_STRING_RUNTIME_ALLOCATOR_ROLE_##role},
#include "neverc/Shellcode/Tables/StringRuntimeAllocatorNames.def"
#include "neverc/Shellcode/Tables/UserExtra_StringRuntimeAllocatorNames.def"
#undef NEVERC_STRING_RUNTIME_ALLOCATOR
  };
#undef NEVERC_STRING_RUNTIME_ALLOCATOR_ROLE_alloc
#undef NEVERC_STRING_RUNTIME_ALLOCATOR_ROLE_free
  for (const Row &R : kRows)
    if (Canon == R.Name)
      return {/*Matched=*/true, R.Role};
  return {};
}

Value *alignValue(IRBuilder<> &B, Value *V, Type *SizeTy, StringRef Name) {
  auto *AlignMinusOne = ConstantInt::get(SizeTy, ABI::ArenaAlignment - 1);
  auto *Mask = ConstantInt::get(SizeTy, ~uint64_t(ABI::ArenaAlignment - 1));
  return B.CreateAnd(B.CreateAdd(V, AlignMinusOne), Mask, Name);
}

GlobalVariable *getOrCreateArena(Module &M, uint64_t ArenaSize) {
  if (auto *GV = M.getNamedGlobal(ABI::ArenaGlobalName))
    return GV;

  LLVMContext &Ctx = M.getContext();
  Type *I8 = Type::getInt8Ty(Ctx);
  ArrayType *ArenaTy = ArrayType::get(I8, ArenaSize);
  auto *GV = new GlobalVariable(M, ArenaTy, /*isConstant=*/false,
                                GlobalValue::InternalLinkage,
                                UndefValue::get(ArenaTy), ABI::ArenaGlobalName);
  GV->setAlignment(Align(ABI::ArenaAlignment));
  return GV;
}

GlobalVariable *getOrCreateArenaOffset(Module &M, Type *SizeTy) {
  if (auto *GV = M.getNamedGlobal(ABI::ArenaOffsetGlobalName))
    return GV;

  auto *GV = new GlobalVariable(
      M, SizeTy, /*isConstant=*/false, GlobalValue::InternalLinkage,
      ConstantInt::get(SizeTy, 0), ABI::ArenaOffsetGlobalName);
  GV->setAlignment(Align(ABI::ArenaMetadataAlignment));
  return GV;
}

GlobalVariable *getOrCreateArenaFreeList(Module &M, PointerType *PtrTy) {
  if (auto *GV = M.getNamedGlobal(ABI::ArenaFreeListGlobalName))
    return GV;

  auto *GV = new GlobalVariable(
      M, PtrTy, /*isConstant=*/false, GlobalValue::InternalLinkage,
      ConstantPointerNull::get(PtrTy), ABI::ArenaFreeListGlobalName);
  GV->setAlignment(Align(ABI::ArenaMetadataAlignment));
  return GV;
}

Value *createHeaderFieldPtr(IRBuilder<> &B, const ArenaLayout &Layout,
                            Value *Header, ABI::ArenaHeaderField Field,
                            StringRef Name) {
  return B.CreateStructGEP(Layout.HeaderTy, Header, Field, Name);
}

Function *getOrCreateStringAlloc(Module &M, uint64_t ArenaSize) {
  LLVMContext &Ctx = M.getContext();
  ArenaLayout Layout = getArenaLayout(M);
  Type *SizeTy = Layout.SizeTy;
  Type *I8Ty = Layout.I8Ty;
  PointerType *PtrTy = Layout.PtrTy;

  FunctionType *FTy = FunctionType::get(PtrTy, {SizeTy}, false);
  Function *F = M.getFunction(ABI::AllocFunctionName);
  if (F && !F->isDeclaration())
    return F;
  if (!F)
    F = Function::Create(FTy, GlobalValue::InternalLinkage,
                         ABI::AllocFunctionName, &M);
  setRuntimeFunctionAttrs(*F);
  F->getArg(0)->setName(ABI::IRNames::AllocSizeArg);

  GlobalVariable *Arena = getOrCreateArena(M, ArenaSize);
  GlobalVariable *ArenaOffset = getOrCreateArenaOffset(M, SizeTy);
  GlobalVariable *ArenaFreeList = getOrCreateArenaFreeList(M, PtrTy);
  auto *ArenaTy = cast<ArrayType>(Arena->getValueType());

  BasicBlock *Entry =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocEntry, F);
  BasicBlock *ReuseCheck =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseCheck, F);
  BasicBlock *ReuseInspect =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseInspect, F);
  BasicBlock *ReuseNext =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseNext, F);
  BasicBlock *ReuseUnlink =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseUnlink, F);
  BasicBlock *ReuseUpdateHead =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseUpdateHead, F);
  BasicBlock *ReuseUpdatePrev =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseUpdatePrev, F);
  BasicBlock *ReuseReturn =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocReuseReturn, F);
  BasicBlock *Bump =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocBump, F);
  BasicBlock *BumpCommit =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocBumpCommit, F);
  BasicBlock *OOM = BasicBlock::Create(Ctx, ABI::BasicBlockNames::AllocOOM, F);

  IRBuilder<> B(Entry);
  Value *NArg = F->getArg(0);
  Value *N = B.CreateSelect(B.CreateICmpEQ(NArg, ConstantInt::get(SizeTy, 0)),
                            ConstantInt::get(SizeTy, 1), NArg,
                            ABI::IRNames::AllocSizeNonZero);
  Value *Head = B.CreateLoad(PtrTy, ArenaFreeList, ABI::IRNames::AllocFreeHead);
  B.CreateBr(ReuseCheck);

  B.SetInsertPoint(ReuseCheck);
  PHINode *Cur = B.CreatePHI(PtrTy, 2, ABI::IRNames::AllocFreeCur);
  PHINode *Prev = B.CreatePHI(PtrTy, 2, ABI::IRNames::AllocFreePrev);
  Cur->addIncoming(Head, Entry);
  Prev->addIncoming(ConstantPointerNull::get(PtrTy), Entry);
  B.CreateCondBr(B.CreateICmpEQ(Cur, ConstantPointerNull::get(PtrTy)), Bump,
                 ReuseInspect);

  B.SetInsertPoint(ReuseInspect);
  Value *CurSelfPtr = createHeaderFieldPtr(B, Layout, Cur, ABI::HeaderSelfField,
                                           ABI::IRNames::AllocFreeCurSelfPtr);
  Value *CurSelf =
      B.CreateLoad(PtrTy, CurSelfPtr, ABI::IRNames::AllocFreeCurSelf);
  Value *CurTagPtr = createHeaderFieldPtr(B, Layout, Cur, ABI::HeaderTagField,
                                          ABI::IRNames::AllocFreeCurTagPtr);
  Value *CurTag =
      B.CreateLoad(SizeTy, CurTagPtr, ABI::IRNames::AllocFreeCurTag);
  Value *CurSizePtr = createHeaderFieldPtr(B, Layout, Cur, ABI::HeaderSizeField,
                                           ABI::IRNames::AllocFreeCurSizePtr);
  Value *CurSize =
      B.CreateLoad(SizeTy, CurSizePtr, ABI::IRNames::AllocFreeCurSize);
  Value *IsFreeBlock = B.CreateAnd(
      B.CreateICmpEQ(CurSelf, Cur),
      B.CreateICmpEQ(CurTag, ConstantInt::get(SizeTy, ABI::ArenaBlockFreeTag)));
  Value *Fits = B.CreateICmpUGE(CurSize, N);
  B.CreateCondBr(B.CreateAnd(IsFreeBlock, Fits), ReuseUnlink, ReuseNext);

  B.SetInsertPoint(ReuseNext);
  Value *CurNextPtr = createHeaderFieldPtr(B, Layout, Cur, ABI::HeaderNextField,
                                           ABI::IRNames::AllocFreeCurNextPtr);
  Value *Next = B.CreateLoad(PtrTy, CurNextPtr, ABI::IRNames::AllocFreeNext);
  Cur->addIncoming(Next, ReuseNext);
  Prev->addIncoming(Cur, ReuseNext);
  B.CreateBr(ReuseCheck);

  B.SetInsertPoint(ReuseUnlink);
  Value *ReuseNextPtr = createHeaderFieldPtr(
      B, Layout, Cur, ABI::HeaderNextField, ABI::IRNames::AllocReuseNextPtr);
  Value *ReuseNextValue =
      B.CreateLoad(PtrTy, ReuseNextPtr, ABI::IRNames::AllocReuseNextValue);
  B.CreateCondBr(B.CreateICmpEQ(Prev, ConstantPointerNull::get(PtrTy)),
                 ReuseUpdateHead, ReuseUpdatePrev);

  B.SetInsertPoint(ReuseUpdateHead);
  B.CreateStore(ReuseNextValue, ArenaFreeList);
  B.CreateBr(ReuseReturn);

  B.SetInsertPoint(ReuseUpdatePrev);
  Value *PrevNextPtr =
      createHeaderFieldPtr(B, Layout, Prev, ABI::HeaderNextField,
                           ABI::IRNames::AllocFreePrevNextPtr);
  B.CreateStore(ReuseNextValue, PrevNextPtr);
  B.CreateBr(ReuseReturn);

  B.SetInsertPoint(ReuseReturn);
  Value *ReuseNextClearPtr =
      createHeaderFieldPtr(B, Layout, Cur, ABI::HeaderNextField,
                           ABI::IRNames::AllocReuseNextClearPtr);
  B.CreateStore(ConstantPointerNull::get(PtrTy), ReuseNextClearPtr);
  Value *ReuseTagPtr = createHeaderFieldPtr(B, Layout, Cur, ABI::HeaderTagField,
                                            ABI::IRNames::AllocReuseTagPtr);
  B.CreateStore(ConstantInt::get(SizeTy, ABI::ArenaBlockActiveTag),
                ReuseTagPtr);
  Value *ReusePayload =
      B.CreateGEP(I8Ty, Cur, ConstantInt::get(SizeTy, Layout.HeaderSize),
                  ABI::IRNames::AllocReusePayload);
  B.CreateRet(ReusePayload);

  B.SetInsertPoint(Bump);
  Value *Pos = B.CreateLoad(SizeTy, ArenaOffset, ABI::IRNames::AllocBumpPos);
  Value *Aligned =
      alignValue(B, Pos, SizeTy, ABI::IRNames::AlignmentRoundUpHeaderStart);
  Value *PayloadStart =
      B.CreateAdd(Aligned, ConstantInt::get(SizeTy, Layout.HeaderSize),
                  ABI::IRNames::AllocBumpPayloadStart);
  Value *EndRaw = B.CreateAdd(PayloadStart, N, ABI::IRNames::AllocBumpEndRaw);
  Value *End =
      alignValue(B, EndRaw, SizeTy, ABI::IRNames::AlignmentRoundUpPayloadEnd);
  Value *HeaderOverflow = B.CreateICmpULT(PayloadStart, Aligned);
  Value *PayloadOverflow = B.CreateICmpULT(EndRaw, PayloadStart);
  Value *AlignOverflow = B.CreateICmpULT(End, EndRaw);
  Value *Overflow =
      B.CreateOr(HeaderOverflow, B.CreateOr(PayloadOverflow, AlignOverflow));
  Value *TooLarge = B.CreateICmpUGT(End, ConstantInt::get(SizeTy, ArenaSize));
  B.CreateCondBr(B.CreateOr(Overflow, TooLarge), OOM, BumpCommit);

  B.SetInsertPoint(BumpCommit);
  B.CreateStore(End, ArenaOffset);
  Value *Block = B.CreateInBoundsGEP(
      ArenaTy, Arena, {ConstantInt::get(Type::getInt32Ty(Ctx), 0), Aligned},
      ABI::IRNames::AllocBlock);
  Value *BlockSizePtr = createHeaderFieldPtr(
      B, Layout, Block, ABI::HeaderSizeField, ABI::IRNames::AllocBlockSizePtr);
  Value *PayloadCapacity =
      B.CreateSub(End, PayloadStart, ABI::IRNames::AllocPayloadCapacity);
  B.CreateStore(PayloadCapacity, BlockSizePtr);
  Value *BlockNextPtr = createHeaderFieldPtr(
      B, Layout, Block, ABI::HeaderNextField, ABI::IRNames::AllocBlockNextPtr);
  B.CreateStore(ConstantPointerNull::get(PtrTy), BlockNextPtr);
  Value *BlockSelfPtr = createHeaderFieldPtr(
      B, Layout, Block, ABI::HeaderSelfField, ABI::IRNames::AllocBlockSelfPtr);
  B.CreateStore(Block, BlockSelfPtr);
  Value *BlockTagPtr = createHeaderFieldPtr(
      B, Layout, Block, ABI::HeaderTagField, ABI::IRNames::AllocBlockTagPtr);
  B.CreateStore(ConstantInt::get(SizeTy, ABI::ArenaBlockActiveTag),
                BlockTagPtr);
  Value *Ptr =
      B.CreateGEP(I8Ty, Block, ConstantInt::get(SizeTy, Layout.HeaderSize),
                  ABI::IRNames::AllocPayloadPtr);
  B.CreateRet(Ptr);

  B.SetInsertPoint(OOM);
  B.CreateRet(ConstantPointerNull::get(PtrTy));
  return F;
}

Function *getOrCreateStringFree(Module &M, bool ValidateArena,
                                uint64_t ArenaSize) {
  LLVMContext &Ctx = M.getContext();
  ArenaLayout Layout = getArenaLayout(M);
  Type *SizeTy = Layout.SizeTy;
  Type *I8Ty = Layout.I8Ty;
  PointerType *PtrTy = Layout.PtrTy;
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
  Function *F = M.getFunction(ABI::FreeFunctionName);
  if (F && !F->isDeclaration())
    return F;
  if (!F)
    F = Function::Create(FTy, GlobalValue::InternalLinkage,
                         ABI::FreeFunctionName, &M);
  setRuntimeFunctionAttrs(*F);
  F->getArg(0)->setName(ABI::IRNames::FreePtrArg);

  BasicBlock *Entry =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::FreeEntry, F);
  IRBuilder<> B(Entry);
  Value *Ptr = F->getArg(0);
  if (!ValidateArena) {
    B.CreateRetVoid();
    return F;
  }

  BasicBlock *Validate =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::FreeValidate, F);
  BasicBlock *ValidateHeader =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::FreeValidateHeader, F);
  BasicBlock *Release =
      BasicBlock::Create(Ctx, ABI::BasicBlockNames::FreeRelease, F);
  BasicBlock *Done = BasicBlock::Create(Ctx, ABI::BasicBlockNames::FreeDone, F);
  B.CreateCondBr(B.CreateICmpEQ(Ptr, ConstantPointerNull::get(PtrTy)), Done,
                 Validate);

  B.SetInsertPoint(Validate);
  GlobalVariable *Arena = getOrCreateArena(M, ArenaSize);
  auto *ArenaTy = cast<ArrayType>(Arena->getValueType());
  Value *ArenaBegin = B.CreateInBoundsGEP(
      ArenaTy, Arena,
      {ConstantInt::get(Type::getInt32Ty(Ctx), 0), ConstantInt::get(SizeTy, 0)},
      ABI::IRNames::FreeArenaBegin);
  Value *ArenaEnd =
      B.CreateInBoundsGEP(ArenaTy, Arena,
                          {ConstantInt::get(Type::getInt32Ty(Ctx), 0),
                           ConstantInt::get(SizeTy, ArenaSize)},
                          ABI::IRNames::FreeArenaEnd);
  Value *PtrInt = B.CreatePtrToInt(Ptr, SizeTy, ABI::IRNames::FreePtrInt);
  Value *BeginInt =
      B.CreatePtrToInt(ArenaBegin, SizeTy, ABI::IRNames::FreeArenaBeginInt);
  Value *EndInt =
      B.CreatePtrToInt(ArenaEnd, SizeTy, ABI::IRNames::FreeArenaEndInt);
  Value *FirstPayload =
      B.CreateAdd(BeginInt, ConstantInt::get(SizeTy, Layout.HeaderSize),
                  ABI::IRNames::FreeArenaFirstPayload);
  Value *InLowerBound = B.CreateICmpUGE(PtrInt, FirstPayload);
  Value *InUpperBound = B.CreateICmpULT(PtrInt, EndInt);
  Value *Aligned = B.CreateICmpEQ(
      B.CreateAnd(PtrInt, ConstantInt::get(SizeTy, ABI::ArenaAlignment - 1)),
      ConstantInt::get(SizeTy, 0));
  Value *InArena =
      B.CreateAnd(B.CreateAnd(InLowerBound, InUpperBound), Aligned);
  B.CreateCondBr(InArena, ValidateHeader, Done);

  B.SetInsertPoint(ValidateHeader);
  Value *Header = B.CreateGEP(
      I8Ty, Ptr,
      ConstantInt::getSigned(SizeTy, -static_cast<int64_t>(Layout.HeaderSize)),
      ABI::IRNames::FreeBlock);
  Value *SelfPtr = createHeaderFieldPtr(B, Layout, Header, ABI::HeaderSelfField,
                                        ABI::IRNames::FreeBlockSelfPtr);
  Value *Self = B.CreateLoad(PtrTy, SelfPtr, ABI::IRNames::FreeBlockSelf);
  Value *TagPtr = createHeaderFieldPtr(B, Layout, Header, ABI::HeaderTagField,
                                       ABI::IRNames::FreeBlockTagPtr);
  Value *Tag = B.CreateLoad(SizeTy, TagPtr, ABI::IRNames::FreeBlockTag);
  Value *MatchesSelf = B.CreateICmpEQ(Self, Header);
  Value *IsActive =
      B.CreateICmpEQ(Tag, ConstantInt::get(SizeTy, ABI::ArenaBlockActiveTag));
  B.CreateCondBr(B.CreateAnd(MatchesSelf, IsActive), Release, Done);

  B.SetInsertPoint(Release);
  GlobalVariable *ArenaFreeList = getOrCreateArenaFreeList(M, PtrTy);
  Value *OldHead =
      B.CreateLoad(PtrTy, ArenaFreeList, ABI::IRNames::FreeOldHead);
  Value *NextPtr = createHeaderFieldPtr(B, Layout, Header, ABI::HeaderNextField,
                                        ABI::IRNames::FreeBlockNextPtr);
  B.CreateStore(OldHead, NextPtr);
  B.CreateStore(ConstantInt::get(SizeTy, ABI::ArenaBlockFreeTag), TagPtr);
  B.CreateStore(Header, ArenaFreeList);
  B.CreateBr(Done);

  B.SetInsertPoint(Done);
  B.CreateRetVoid();
  return F;
}

bool rewriteAllocatorCalls(Function &RuntimeFn, Module &M, uint64_t ArenaSize,
                           bool ModuleNeedsAlloc) {
  bool Changed = false;
  Function *Alloc = nullptr;
  Function *Free = nullptr;

  struct WorkItem {
    CallBase *CB;
    RuntimeAllocatorRole Role;
  };
  SmallVector<WorkItem, 8> Work;
  for (Instruction &I : instructions(RuntimeFn)) {
    auto *CB = dyn_cast<CallBase>(&I);
    if (!CB)
      continue;
    Function *Callee = CB->getCalledFunction();
    if (!Callee)
      continue;
    auto Classification = classifyRuntimeAllocator(Callee->getName());
    if (!Classification.Matched)
      continue;
    Work.push_back({CB, Classification.Role});
  }

  for (const WorkItem &Item : Work) {
    CallBase *CB = Item.CB;
    IRBuilder<> B(CB);
    switch (Item.Role) {
    case RuntimeAllocatorRole::Alloc: {
      if (!Alloc)
        Alloc = getOrCreateStringAlloc(M, ArenaSize);
      Value *N = CB->getArgOperand(0);
      Type *SizeTy = Alloc->getFunctionType()->getParamType(0);
      if (N->getType() != SizeTy)
        N = B.CreateIntCast(N, SizeTy, /*isSigned=*/false);
      CallInst *Replacement = B.CreateCall(Alloc, {N});
      CB->replaceAllUsesWith(Replacement);
      CB->eraseFromParent();
      Changed = true;
      break;
    }
    case RuntimeAllocatorRole::Free: {
      if (!Free)
        Free = getOrCreateStringFree(M, ModuleNeedsAlloc, ArenaSize);
      B.CreateCall(Free, {CB->getArgOperand(0)});
      CB->eraseFromParent();
      Changed = true;
      break;
    }
    }
  }

  return Changed;
}

bool anyRuntimeAllocatorCalls(ArrayRef<Function *> RuntimeFunctions,
                              RuntimeAllocatorRole Role) {
  for (Function *F : RuntimeFunctions) {
    for (Instruction &I : instructions(*F)) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      Function *Callee = CB->getCalledFunction();
      if (!Callee)
        continue;
      auto Classification = classifyRuntimeAllocator(Callee->getName());
      if (Classification.Matched && Classification.Role == Role)
        return true;
    }
  }
  return false;
}

} // namespace

void StringRuntimePass::ensureArenaInfrastructure(Module &M,
                                                   uint64_t ArenaSize) {
  getOrCreateStringAlloc(M, ArenaSize);
  getOrCreateStringFree(M, /*ValidateArena=*/true, ArenaSize);
}

PreservedAnalyses StringRuntimePass::run(Module &M, ModuleAnalysisManager &) {
  SmallVector<Function *, 8> RuntimeFunctions;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (!isBuiltinStringRuntimeFunction(F))
      continue;
    RuntimeFunctions.push_back(&F);
  }

  bool Changed = false;
  bool ModuleNeedsAlloc =
      anyRuntimeAllocatorCalls(RuntimeFunctions, RuntimeAllocatorRole::Alloc);
  for (Function *F : RuntimeFunctions) {
    setRuntimeFunctionAttrs(*F, /*DeferInlining=*/true);
    Changed = true;
    Changed |= rewriteAllocatorCalls(*F, M, ArenaSize, ModuleNeedsAlloc);
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

PreservedAnalyses
StringRuntimeInlineFinalizePass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (!F.hasFnAttribute(ABI::kRuntimeFnAttr))
      continue;
    if (!F.hasFnAttribute(Attribute::NoInline))
      continue;
    F.removeFnAttr(Attribute::NoInline);
    F.addFnAttr(Attribute::AlwaysInline);
    Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
