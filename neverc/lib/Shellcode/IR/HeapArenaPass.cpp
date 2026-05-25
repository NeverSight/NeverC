#include "neverc/Shellcode/IR/HeapArenaPass.h"
#include "neverc/Shellcode/IR/StringRuntimeABI.h"
#include "neverc/Shellcode/IR/StringRuntimePass.h"
#include "neverc/Shellcode/Pipeline/SymbolNames.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include <cstdint>

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

namespace ABI = StringRuntimeABI;
namespace HeapABI = HeapArenaABI;

struct HeapRewriteTarget {
  enum Kind { Malloc, Free, Calloc, Realloc };
  CallBase *CB;
  Kind K;
};

StringRef canonicalize(StringRef Name) {
  return SymbolNames::canonicalRuntimeName(
      SymbolNames::stripObjectLeadingUnderscore(Name));
}

bool classifyHeapCall(StringRef Canon, HeapRewriteTarget::Kind &Out) {
#define NEVERC_HEAP_ARENA_TARGET(name, kind)                                   \
  if (Canon == #name) {                                                        \
    Out = HeapRewriteTarget::kind;                                             \
    return true;                                                               \
  }
#include "neverc/Shellcode/Tables/HeapArenaRewriteTargets.def"
#include "neverc/Shellcode/Tables/UserExtra_HeapArenaRewriteTargets.def"
#undef NEVERC_HEAP_ARENA_TARGET
  return false;
}

Type *getSizeType(Module &M) {
  unsigned Bits = M.getDataLayout().getPointerSizeInBits();
  if (Bits == 0)
    Bits = 64;
  return IntegerType::get(M.getContext(), Bits);
}

StructType *getArenaHeaderType(Module &M) {
  Type *SizeTy = getSizeType(M);
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  return StructType::get(M.getContext(), {SizeTy, PtrTy, PtrTy, SizeTy});
}

uint64_t getHeaderSize(Module &M) {
  StructType *Hdr = getArenaHeaderType(M);
  uint64_t Raw = M.getDataLayout().getTypeAllocSize(Hdr);
  return (Raw + ABI::ArenaAlignment - 1) & ~(ABI::ArenaAlignment - 1);
}

Function *getArenaAlloc(Module &M) {
  return M.getFunction(ABI::AllocFunctionName);
}

Function *getArenaFree(Module &M) {
  return M.getFunction(ABI::FreeFunctionName);
}

Function *getOrDeclareExtern(Module &M, StringRef Name, FunctionType *FTy) {
  if (Function *F = M.getFunction(Name))
    return F;
  Function *F =
      Function::Create(FTy, GlobalValue::ExternalLinkage, Name, &M);
  F->addFnAttr(Attribute::NoUnwind);
  return F;
}

Function *getOrDeclareMalloc(Module &M) {
  Type *SizeTy = getSizeType(M);
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  return getOrDeclareExtern(M, "malloc",
                            FunctionType::get(PtrTy, {SizeTy}, false));
}

Function *getOrDeclareFree(Module &M) {
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  return getOrDeclareExtern(
      M, "free",
      FunctionType::get(Type::getVoidTy(M.getContext()), {PtrTy}, false));
}

Function *getOrDeclareMmap(Module &M) {
  Type *SizeTy = getSizeType(M);
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  Type *I32 = Type::getInt32Ty(M.getContext());
  Type *I64 = Type::getInt64Ty(M.getContext());
  return getOrDeclareExtern(
      M, "mmap",
      FunctionType::get(PtrTy, {PtrTy, I64, I32, I32, I32, I64}, false));
}

Function *getOrDeclareMunmap(Module &M) {
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  Type *I64 = Type::getInt64Ty(M.getContext());
  return getOrDeclareExtern(
      M, "munmap",
      FunctionType::get(Type::getInt32Ty(M.getContext()), {PtrTy, I64}, false));
}

constexpr int kProtRW = 3; // PROT_READ(0x1) | PROT_WRITE(0x2)

int mapPrivateAnonFlags(ShellcodeOS OS) {
  return (OS == ShellcodeOS::Darwin) ? HeapABI::MapPrivateAnonDarwin
                                     : HeapABI::MapPrivateAnonLinux;
}

Value *emitMmapAlloc(IRBuilder<> &B, Module &M, Value *Size, ShellcodeOS OS) {
  Type *SizeTy = getSizeType(M);
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  Type *I32 = Type::getInt32Ty(M.getContext());
  Type *I64 = Type::getInt64Ty(M.getContext());

  Value *HdrSz = ConstantInt::get(SizeTy, HeapABI::MmapHeaderSize);
  Value *Total = B.CreateAdd(Size, HdrSz, "mmap.total");
  Value *Total64 = B.CreateIntCast(Total, I64, false);

  Function *Mmap = getOrDeclareMmap(M);
  Value *Base = B.CreateCall(
      Mmap,
      {ConstantPointerNull::get(PtrTy), Total64,
       ConstantInt::get(I32, kProtRW),
       ConstantInt::get(I32, mapPrivateAnonFlags(OS)),
       ConstantInt::get(I32, -1), ConstantInt::get(I64, 0)},
      "mmap.base");

  Value *Failed =
      B.CreateICmpEQ(B.CreatePtrToInt(Base, SizeTy),
                     ConstantInt::getSigned(SizeTy, -1));

  Function *Fn = B.GetInsertBlock()->getParent();
  BasicBlock *OkBB = BasicBlock::Create(M.getContext(), "mmap.ok", Fn);
  BasicBlock *FailBB = BasicBlock::Create(M.getContext(), "mmap.fail", Fn);
  BasicBlock *DoneBB = BasicBlock::Create(M.getContext(), "mmap.done", Fn);
  B.CreateCondBr(Failed, FailBB, OkBB);

  B.SetInsertPoint(OkBB);
  B.CreateStore(Total, Base);
  Value *Payload = B.CreateGEP(Type::getInt8Ty(M.getContext()), Base, HdrSz,
                               "mmap.payload");
  B.CreateBr(DoneBB);

  B.SetInsertPoint(FailBB);
  B.CreateBr(DoneBB);

  B.SetInsertPoint(DoneBB);
  PHINode *Result = B.CreatePHI(PtrTy, 2, "mmap.result");
  Result->addIncoming(Payload, OkBB);
  Result->addIncoming(ConstantPointerNull::get(PtrTy), FailBB);
  return Result;
}

void emitMmapFree(IRBuilder<> &B, Module &M, Value *Ptr) {
  Type *SizeTy = getSizeType(M);
  Type *I8Ty = Type::getInt8Ty(M.getContext());
  Type *I64 = Type::getInt64Ty(M.getContext());

  Value *Base = B.CreateGEP(
      I8Ty, Ptr,
      ConstantInt::getSigned(SizeTy, -static_cast<int64_t>(HeapABI::MmapHeaderSize)),
      "munmap.base");
  Value *Total = B.CreateLoad(SizeTy, Base, "munmap.total");
  Value *Total64 = B.CreateIntCast(Total, I64, false);

  Function *Munmap = getOrDeclareMunmap(M);
  B.CreateCall(Munmap, {Base, Total64});
}

Value *emitFallbackAlloc(IRBuilder<> &B, Module &M, Value *Size,
                         HeapFallbackMode Mode, ShellcodeOS OS) {
  if (Mode == HeapFallbackMode::ExternalMalloc) {
    Type *SizeTy = getSizeType(M);
    PointerType *PtrTy = PointerType::getUnqual(M.getContext());
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    Value *N = B.CreateIntCast(Size, SizeTy, false);
    Value *HdrSz = ConstantInt::get(SizeTy, HeapABI::MmapHeaderSize);
    Value *Total = B.CreateAdd(N, HdrSz, "ext.total");

    Function *MallocFn = getOrDeclareMalloc(M);
    Type *MallocArgTy = MallocFn->getFunctionType()->getParamType(0);
    Value *MallocArg = B.CreateIntCast(Total, MallocArgTy, false);
    Value *Base = B.CreateCall(MallocFn, {MallocArg}, "ext.base");

    Value *IsNull = B.CreateICmpEQ(Base, ConstantPointerNull::get(PtrTy));
    Function *Fn = B.GetInsertBlock()->getParent();
    BasicBlock *OkBB = BasicBlock::Create(M.getContext(), "ext.ok", Fn);
    BasicBlock *FailBB = BasicBlock::Create(M.getContext(), "ext.fail", Fn);
    BasicBlock *DoneBB = BasicBlock::Create(M.getContext(), "ext.done", Fn);
    B.CreateCondBr(IsNull, FailBB, OkBB);

    B.SetInsertPoint(OkBB);
    B.CreateStore(Total, Base);
    Value *Payload =
        B.CreateGEP(I8Ty, Base, HdrSz, "ext.payload");
    B.CreateBr(DoneBB);

    B.SetInsertPoint(FailBB);
    B.CreateBr(DoneBB);

    B.SetInsertPoint(DoneBB);
    PHINode *Result = B.CreatePHI(PtrTy, 2, "ext.result");
    Result->addIncoming(Payload, OkBB);
    Result->addIncoming(ConstantPointerNull::get(PtrTy), FailBB);
    return Result;
  }
  return emitMmapAlloc(B, M, Size, OS);
}

void emitFallbackFree(IRBuilder<> &B, Module &M, Value *Ptr,
                      HeapFallbackMode Mode) {
  if (Mode == HeapFallbackMode::ExternalMalloc) {
    Type *SizeTy = getSizeType(M);
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    Value *Base = B.CreateGEP(
        I8Ty, Ptr,
        ConstantInt::getSigned(SizeTy,
                               -static_cast<int64_t>(HeapABI::MmapHeaderSize)),
        "ext.free.base");
    Function *FreeFn = getOrDeclareFree(M);
    B.CreateCall(FreeFn, {Base});
    return;
  }
  emitMmapFree(B, M, Ptr);
}

Value *emitHeapAlloc(IRBuilder<> &B, Module &M, Value *Size,
                     uint64_t ArenaSize, HeapFallbackMode Fallback,
                     ShellcodeOS OS) {
  Type *SizeTy = getSizeType(M);
  Function *ArenaAlloc = getArenaAlloc(M);
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());

  Value *N = B.CreateIntCast(Size, SizeTy, false);

  if (Fallback == HeapFallbackMode::None)
    return B.CreateCall(ArenaAlloc, {N});

  Value *Threshold =
      ConstantInt::get(SizeTy, HeapABI::DefaultArenaThreshold);
  Value *SmallEnough = B.CreateICmpULE(N, Threshold);

  Function *Fn = B.GetInsertBlock()->getParent();
  BasicBlock *ArenaBB = BasicBlock::Create(M.getContext(), "heap.arena", Fn);
  BasicBlock *ArenaOOMBB =
      BasicBlock::Create(M.getContext(), "heap.arena.oom", Fn);
  BasicBlock *FallbackBB =
      BasicBlock::Create(M.getContext(), "heap.fallback", Fn);
  BasicBlock *MergeBB = BasicBlock::Create(M.getContext(), "heap.merge", Fn);

  B.CreateCondBr(SmallEnough, ArenaBB, FallbackBB);

  B.SetInsertPoint(ArenaBB);
  Value *ArenaResult = B.CreateCall(ArenaAlloc, {N});
  Value *ArenaOK =
      B.CreateICmpNE(ArenaResult, ConstantPointerNull::get(PtrTy));
  B.CreateCondBr(ArenaOK, MergeBB, ArenaOOMBB);
  BasicBlock *ArenaSuccessBB = ArenaBB;

  B.SetInsertPoint(ArenaOOMBB);
  Value *OOMFallback = emitFallbackAlloc(B, M, N, Fallback, OS);
  BasicBlock *OOMEndBB = B.GetInsertBlock();
  B.CreateBr(MergeBB);

  B.SetInsertPoint(FallbackBB);
  Value *FallbackResult = emitFallbackAlloc(B, M, N, Fallback, OS);
  BasicBlock *FallbackEndBB = B.GetInsertBlock();
  B.CreateBr(MergeBB);

  B.SetInsertPoint(MergeBB);
  PHINode *Result = B.CreatePHI(PtrTy, 3, "heap.ptr");
  Result->addIncoming(ArenaResult, ArenaSuccessBB);
  Result->addIncoming(OOMFallback, OOMEndBB);
  Result->addIncoming(FallbackResult, FallbackEndBB);
  return Result;
}

void emitHeapFree(IRBuilder<> &B, Module &M, Value *Ptr, uint64_t ArenaSize,
                  HeapFallbackMode Fallback) {
  Function *ArenaFree = getArenaFree(M);

  if (Fallback == HeapFallbackMode::None) {
    B.CreateCall(ArenaFree, {Ptr});
    return;
  }

  PointerType *PtrTy = PointerType::getUnqual(M.getContext());
  Type *SizeTy = getSizeType(M);
  GlobalVariable *Arena = M.getNamedGlobal(ABI::ArenaGlobalName);

  if (!Arena) {
    B.CreateCall(ArenaFree, {Ptr});
    return;
  }

  auto *ArenaTy = cast<ArrayType>(Arena->getValueType());
  Value *ArenaBegin = B.CreateInBoundsGEP(
      ArenaTy, Arena,
      {ConstantInt::get(Type::getInt32Ty(M.getContext()), 0),
       ConstantInt::get(SizeTy, 0)},
      "heap.free.begin");
  Value *ArenaEnd = B.CreateInBoundsGEP(
      ArenaTy, Arena,
      {ConstantInt::get(Type::getInt32Ty(M.getContext()), 0),
       ConstantInt::get(SizeTy, ArenaSize)},
      "heap.free.end");

  Value *PtrInt = B.CreatePtrToInt(Ptr, SizeTy);
  Value *BeginInt = B.CreatePtrToInt(ArenaBegin, SizeTy);
  Value *EndInt = B.CreatePtrToInt(ArenaEnd, SizeTy);
  Value *InArena = B.CreateAnd(B.CreateICmpUGE(PtrInt, BeginInt),
                               B.CreateICmpULT(PtrInt, EndInt));

  Function *Fn = B.GetInsertBlock()->getParent();
  BasicBlock *ArenaFreeBB =
      BasicBlock::Create(M.getContext(), "heap.free.arena", Fn);
  BasicBlock *ExtFreeBB =
      BasicBlock::Create(M.getContext(), "heap.free.ext", Fn);
  BasicBlock *DoneBB =
      BasicBlock::Create(M.getContext(), "heap.free.done", Fn);

  B.CreateCondBr(InArena, ArenaFreeBB, ExtFreeBB);

  B.SetInsertPoint(ArenaFreeBB);
  B.CreateCall(ArenaFree, {Ptr});
  B.CreateBr(DoneBB);

  B.SetInsertPoint(ExtFreeBB);
  emitFallbackFree(B, M, Ptr, Fallback);
  B.CreateBr(DoneBB);

  B.SetInsertPoint(DoneBB);
}

Value *readOldBlockSize(IRBuilder<> &B, Module &M, Value *Ptr) {
  Type *SizeTy = getSizeType(M);
  Type *I8Ty = Type::getInt8Ty(M.getContext());
  uint64_t HdrSize = getHeaderSize(M);

  StructType *HeaderTy = getArenaHeaderType(M);
  Value *Header = B.CreateGEP(
      I8Ty, Ptr,
      ConstantInt::getSigned(SizeTy, -static_cast<int64_t>(HdrSize)),
      "realloc.header");
  Value *SizePtr = B.CreateStructGEP(HeaderTy, Header, ABI::HeaderSizeField,
                                     "realloc.old.size.ptr");
  return B.CreateLoad(SizeTy, SizePtr, "realloc.old.size");
}

Value *readFallbackOldSize(IRBuilder<> &B, Module &M, Value *Ptr) {
  Type *SizeTy = getSizeType(M);
  Type *I8Ty = Type::getInt8Ty(M.getContext());
  Value *Base = B.CreateGEP(
      I8Ty, Ptr,
      ConstantInt::getSigned(SizeTy,
                             -static_cast<int64_t>(HeapABI::MmapHeaderSize)),
      "realloc.fb.base");
  Value *Total = B.CreateLoad(SizeTy, Base, "realloc.fb.total");
  return B.CreateSub(Total,
                     ConstantInt::get(SizeTy, HeapABI::MmapHeaderSize),
                     "realloc.fb.size");
}

Value *emitOldSizeRead(IRBuilder<> &B, Module &M, Value *Ptr,
                       uint64_t ArenaSize, HeapFallbackMode Fallback) {
  if (Fallback == HeapFallbackMode::None)
    return readOldBlockSize(B, M, Ptr);

  Type *SizeTy = getSizeType(M);
  GlobalVariable *Arena = M.getNamedGlobal(ABI::ArenaGlobalName);
  if (!Arena)
    return readOldBlockSize(B, M, Ptr);

  auto *ArenaTy = cast<ArrayType>(Arena->getValueType());
  Value *ArenaBegin = B.CreateInBoundsGEP(
      ArenaTy, Arena,
      {ConstantInt::get(Type::getInt32Ty(M.getContext()), 0),
       ConstantInt::get(SizeTy, 0)},
      "realloc.arena.begin");
  Value *ArenaEnd = B.CreateInBoundsGEP(
      ArenaTy, Arena,
      {ConstantInt::get(Type::getInt32Ty(M.getContext()), 0),
       ConstantInt::get(SizeTy, ArenaSize)},
      "realloc.arena.end");

  Value *PtrInt = B.CreatePtrToInt(Ptr, SizeTy);
  Value *BeginInt = B.CreatePtrToInt(ArenaBegin, SizeTy);
  Value *EndInt = B.CreatePtrToInt(ArenaEnd, SizeTy);
  Value *InArena = B.CreateAnd(B.CreateICmpUGE(PtrInt, BeginInt),
                               B.CreateICmpULT(PtrInt, EndInt));

  Function *Fn = B.GetInsertBlock()->getParent();
  BasicBlock *ArenaReadBB =
      BasicBlock::Create(M.getContext(), "realloc.read.arena", Fn);
  BasicBlock *FbReadBB =
      BasicBlock::Create(M.getContext(), "realloc.read.fb", Fn);
  BasicBlock *ReadMergeBB =
      BasicBlock::Create(M.getContext(), "realloc.read.merge", Fn);

  B.CreateCondBr(InArena, ArenaReadBB, FbReadBB);

  B.SetInsertPoint(ArenaReadBB);
  Value *ArenaSz = readOldBlockSize(B, M, Ptr);
  B.CreateBr(ReadMergeBB);

  B.SetInsertPoint(FbReadBB);
  Value *FbSz = readFallbackOldSize(B, M, Ptr);
  B.CreateBr(ReadMergeBB);

  B.SetInsertPoint(ReadMergeBB);
  PHINode *OldSz = B.CreatePHI(SizeTy, 2, "realloc.old.sz");
  OldSz->addIncoming(ArenaSz, ArenaReadBB);
  OldSz->addIncoming(FbSz, FbReadBB);
  return OldSz;
}

BasicBlock *splitCallSite(CallBase *CB) {
  BasicBlock *BB = CB->getParent();
  Instruction *After = CB->getNextNode();
  assert(After && "CallBase must not be the last instruction (BB needs terminator)");
  BasicBlock *TailBB = BB->splitBasicBlock(After, "heap.cont");
  BB->getTerminator()->eraseFromParent();
  return TailBB;
}

} // namespace

PreservedAnalyses HeapArenaPass::run(Module &M, ModuleAnalysisManager &) {
  SmallVector<HeapRewriteTarget, 16> Work;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F) {
      for (auto &I : BB) {
        auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        Function *Callee = CB->getCalledFunction();
        if (!Callee)
          continue;
        StringRef Canon = canonicalize(Callee->getName());
        HeapRewriteTarget::Kind K;
        if (!classifyHeapCall(Canon, K))
          continue;
        Work.push_back({CB, K});
      }
    }
  }

  if (Work.empty())
    return PreservedAnalyses::all();

  StringRuntimePass::ensureArenaInfrastructure(M, ArenaSize);

  Type *SizeTy = getSizeType(M);
  PointerType *PtrTy = PointerType::getUnqual(M.getContext());

  for (const HeapRewriteTarget &T : Work) {
    CallBase *CB = T.CB;
    BasicBlock *TailBB = splitCallSite(CB);
    IRBuilder<> B(CB);

    switch (T.K) {
    case HeapRewriteTarget::Malloc: {
      Value *N = B.CreateIntCast(CB->getArgOperand(0), SizeTy, false);
      Value *Result = emitHeapAlloc(B, M, N, ArenaSize, Fallback, TargetOS);
      CB->replaceAllUsesWith(Result);
      B.CreateBr(TailBB);
      CB->eraseFromParent();
      break;
    }
    case HeapRewriteTarget::Free: {
      Value *Ptr = CB->getArgOperand(0);
      Value *IsNull = B.CreateICmpEQ(Ptr, ConstantPointerNull::get(PtrTy));

      Function *Fn = B.GetInsertBlock()->getParent();
      BasicBlock *FreeBB =
          BasicBlock::Create(M.getContext(), "heap.free.nonnull", Fn);
      BasicBlock *SkipBB =
          BasicBlock::Create(M.getContext(), "heap.free.skip", Fn);

      B.CreateCondBr(IsNull, SkipBB, FreeBB);

      B.SetInsertPoint(FreeBB);
      emitHeapFree(B, M, Ptr, ArenaSize, Fallback);
      B.CreateBr(SkipBB);

      B.SetInsertPoint(SkipBB);
      B.CreateBr(TailBB);
      CB->eraseFromParent();
      break;
    }
    case HeapRewriteTarget::Calloc: {
      Value *Count = CB->getArgOperand(0);
      Value *EltSize = CB->getArgOperand(1);
      Value *CountSz = B.CreateIntCast(Count, SizeTy, false);
      Value *EltSz = B.CreateIntCast(EltSize, SizeTy, false);

      Function *UMulOv = Intrinsic::getDeclaration(
          &M, Intrinsic::umul_with_overflow, {SizeTy});
      Value *MulRes = B.CreateCall(UMulOv, {CountSz, EltSz}, "calloc.mul");
      Value *Product = B.CreateExtractValue(MulRes, 0, "calloc.total");
      Value *Overflow = B.CreateExtractValue(MulRes, 1, "calloc.ovf");

      Function *Fn = B.GetInsertBlock()->getParent();
      BasicBlock *OverflowBB =
          BasicBlock::Create(M.getContext(), "calloc.overflow", Fn);
      BasicBlock *AllocBB =
          BasicBlock::Create(M.getContext(), "calloc.alloc", Fn);
      B.CreateCondBr(Overflow, OverflowBB, AllocBB);

      B.SetInsertPoint(AllocBB);
      Value *Ptr = emitHeapAlloc(B, M, Product, ArenaSize, Fallback, TargetOS);
      BasicBlock *AllocEndBB = B.GetInsertBlock();
      Value *IsNull = B.CreateICmpEQ(Ptr, ConstantPointerNull::get(PtrTy));
      BasicBlock *ZeroBB =
          BasicBlock::Create(M.getContext(), "calloc.zero", Fn);
      BasicBlock *DoneBB =
          BasicBlock::Create(M.getContext(), "calloc.done", Fn);
      B.CreateCondBr(IsNull, DoneBB, ZeroBB);

      B.SetInsertPoint(ZeroBB);
      B.CreateMemSet(Ptr, B.getInt8(0), Product, Align(1));
      B.CreateBr(DoneBB);

      B.SetInsertPoint(OverflowBB);
      B.CreateBr(DoneBB);

      B.SetInsertPoint(DoneBB);
      PHINode *Result = B.CreatePHI(PtrTy, 3, "calloc.result");
      Result->addIncoming(ConstantPointerNull::get(PtrTy), OverflowBB);
      Result->addIncoming(ConstantPointerNull::get(PtrTy), AllocEndBB);
      Result->addIncoming(Ptr, ZeroBB);

      CB->replaceAllUsesWith(Result);
      B.CreateBr(TailBB);
      CB->eraseFromParent();
      break;
    }
    case HeapRewriteTarget::Realloc: {
      Value *OldPtr = CB->getArgOperand(0);
      Value *NewSize = CB->getArgOperand(1);
      Value *NewSz = B.CreateIntCast(NewSize, SizeTy, false);

      Value *OldIsNull =
          B.CreateICmpEQ(OldPtr, ConstantPointerNull::get(PtrTy));
      Function *Fn = B.GetInsertBlock()->getParent();
      BasicBlock *JustAllocBB =
          BasicBlock::Create(M.getContext(), "realloc.new", Fn);
      BasicBlock *CopyBB =
          BasicBlock::Create(M.getContext(), "realloc.copy", Fn);
      BasicBlock *MergeBB =
          BasicBlock::Create(M.getContext(), "realloc.merge", Fn);

      B.CreateCondBr(OldIsNull, JustAllocBB, CopyBB);

      B.SetInsertPoint(JustAllocBB);
      Value *FreshPtr =
          emitHeapAlloc(B, M, NewSz, ArenaSize, Fallback, TargetOS);
      BasicBlock *JustAllocEnd = B.GetInsertBlock();
      B.CreateBr(MergeBB);

      B.SetInsertPoint(CopyBB);
      Value *NewPtr =
          emitHeapAlloc(B, M, NewSz, ArenaSize, Fallback, TargetOS);
      BasicBlock *CopyAllocEnd = B.GetInsertBlock();
      Value *NewIsNull =
          B.CreateICmpEQ(NewPtr, ConstantPointerNull::get(PtrTy));

      BasicBlock *DoCopyBB =
          BasicBlock::Create(M.getContext(), "realloc.docopy", Fn);
      B.CreateCondBr(NewIsNull, MergeBB, DoCopyBB);

      B.SetInsertPoint(DoCopyBB);
      Value *OldSize = emitOldSizeRead(B, M, OldPtr, ArenaSize, Fallback);
      Value *CopyLen = B.CreateSelect(B.CreateICmpULT(OldSize, NewSz),
                                      OldSize, NewSz, "realloc.copylen");
      B.CreateMemCpy(NewPtr, Align(1), OldPtr, Align(1), CopyLen);
      emitHeapFree(B, M, OldPtr, ArenaSize, Fallback);
      BasicBlock *CopyDoneBB = B.GetInsertBlock();
      B.CreateBr(MergeBB);

      B.SetInsertPoint(MergeBB);
      PHINode *Result = B.CreatePHI(PtrTy, 3, "realloc.result");
      Result->addIncoming(FreshPtr, JustAllocEnd);
      Result->addIncoming(ConstantPointerNull::get(PtrTy), CopyAllocEnd);
      Result->addIncoming(NewPtr, CopyDoneBB);

      CB->replaceAllUsesWith(Result);
      B.CreateBr(TailBB);
      CB->eraseFromParent();
      break;
    }
    }
  }

  return PreservedAnalyses::none();
}

} // namespace shellcode
} // namespace neverc
