#ifndef NEVERC_SHELLCODE_PTRCACHEHELPERS_H
#define NEVERC_SHELLCODE_PTRCACHEHELPERS_H

#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include <cassert>

namespace neverc {
namespace shellcode {
namespace ptrcache {

inline void setHelperAttrs(llvm::Function *F) {
  llvm::AttrBuilder AB(F->getContext());
  AB.addAttribute(llvm::Attribute::NoUnwind);
  AB.addAttribute(llvm::Attribute::WillReturn);
  AB.addAttribute(llvm::Attribute::NoSync);
  AB.addAttribute(llvm::Attribute::NoRecurse);
  AB.addAttribute(llvm::Attribute::NoFree);
  AB.addAttribute(llvm::Attribute::MustProgress);
  F->addFnAttrs(AB);
  F->setDSOLocal(true);
}

/// Emit a ^ b as (a + b) - 2*(a & b) using volatile intermediates
/// to prevent InstCombine from recognizing and re-folding to xor.
inline llvm::Value *emitXorFree(llvm::IRBuilder<> &B, llvm::Value *LHS,
                                llvm::Value *RHS) {
  llvm::Type *Ty = LHS->getType();
  llvm::AllocaInst *SlotA = B.CreateAlloca(Ty, nullptr, "xf.sa");
  llvm::AllocaInst *SlotB = B.CreateAlloca(Ty, nullptr, "xf.sb");
  B.CreateStore(LHS, SlotA)->setVolatile(true);
  B.CreateStore(RHS, SlotB)->setVolatile(true);
  auto *VA1 = B.CreateLoad(Ty, SlotA, /*isVolatile=*/true, "xf.va1");
  auto *VB1 = B.CreateLoad(Ty, SlotB, /*isVolatile=*/true, "xf.vb1");
  auto *Sum = B.CreateAdd(VA1, VB1, "xf.sum");
  auto *VA2 = B.CreateLoad(Ty, SlotA, /*isVolatile=*/true, "xf.va2");
  auto *VB2 = B.CreateLoad(Ty, SlotB, /*isVolatile=*/true, "xf.vb2");
  auto *And1 = B.CreateAnd(VA2, VB2, "xf.ab");
  auto *VA3 = B.CreateLoad(Ty, SlotA, /*isVolatile=*/true, "xf.va3");
  auto *VB3 = B.CreateLoad(Ty, SlotB, /*isVolatile=*/true, "xf.vb3");
  auto *And2 = B.CreateAnd(VB3, VA3, "xf.ba");
  auto *Dbl = B.CreateAdd(And1, And2, "xf.dbl");
  return B.CreateSub(Sum, Dbl, "xf.res");
}

inline llvm::Value *emitReadPEB(llvm::IRBuilder<> &B, const TargetDesc &T) {
  llvm::LLVMContext &Ctx = B.getContext();
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(Ctx);
  llvm::FunctionType *FTy = llvm::FunctionType::get(PtrTy, {}, false);
  llvm::InlineAsm *Asm = llvm::InlineAsm::get(
      FTy, T.TCBReadAsm, T.TCBReadConstraint,
      /*hasSideEffects=*/false, /*isAlignStack=*/false,
      llvm::InlineAsm::AD_ATT);
  llvm::CallInst *Call = B.CreateCall(FTy, Asm);
  Call->setDoesNotThrow();
  Call->addRetAttr(llvm::Attribute::NonNull);
  return Call;
}

enum class KeySource {
  PEB,
  SeedOnly,
};

inline llvm::Function *getOrCreateDeriveKey(llvm::Module &M,
                                            const TargetDesc &T, uint64_t Seed,
                                            KeySource Source) {
  llvm::StringRef Name = ir::kScDeriveKey;
  if (llvm::Function *F = M.getFunction(Name))
    return F;

  llvm::LLVMContext &Ctx = M.getContext();
  llvm::Type *I64 = llvm::Type::getInt64Ty(Ctx);

  llvm::FunctionType *FTy = llvm::FunctionType::get(I64, {}, false);
  llvm::Function *F = llvm::Function::Create(
      FTy, llvm::GlobalValue::InternalLinkage, Name, &M);
  F->addFnAttr(llvm::Attribute::AlwaysInline);
  setHelperAttrs(F);
  // The PEB read via inline asm (gs:0x60 / x18) is not modeled in LLVM's
  // memory system (hasSideEffects=false, no ~{memory} clobber), so none()
  // is correct and lets LLVM CSE redundant derive_key() calls.
  F->setMemoryEffects(llvm::MemoryEffects::none());

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  llvm::Value *Key;
  if (Source == KeySource::PEB && !T.TCBReadAsm.empty()) {
    llvm::Value *PEB = emitReadPEB(B, T);
    llvm::Value *PEBInt = B.CreatePtrToInt(PEB, I64, "peb.int");
    Key = emitXorFree(B, PEBInt, llvm::ConstantInt::get(I64, Seed));
  } else {
    Key = llvm::ConstantInt::get(I64, Seed);
  }
  B.CreateRet(Key);
  return F;
}

inline llvm::Function *getOrCreatePtrEncrypt(llvm::Module &M,
                                             const TargetDesc &T,
                                             uint64_t Seed, KeySource Source) {
  llvm::StringRef Name = ir::kScPtrEncrypt;
  if (llvm::Function *F = M.getFunction(Name))
    return F;

  llvm::LLVMContext &Ctx = M.getContext();
  llvm::Type *I64 = llvm::Type::getInt64Ty(Ctx);
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(Ctx);

  llvm::FunctionType *FTy = llvm::FunctionType::get(I64, {PtrTy}, false);
  llvm::Function *F = llvm::Function::Create(
      FTy, llvm::GlobalValue::InternalLinkage, Name, &M);
  F->addFnAttr(llvm::Attribute::AlwaysInline);
  setHelperAttrs(F);
  F->setMemoryEffects(llvm::MemoryEffects::none());

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  llvm::Function *DK = getOrCreateDeriveKey(M, T, Seed, Source);
  llvm::CallInst *Key = B.CreateCall(DK, {}, "key");
  Key->setDoesNotThrow();
  llvm::Value *Plain = B.CreatePtrToInt(F->getArg(0), I64, "plain");
  llvm::Value *Enc = emitXorFree(B, Plain, Key);
  B.CreateRet(Enc);
  return F;
}

inline llvm::Function *getOrCreatePtrDecrypt(llvm::Module &M,
                                             const TargetDesc &T,
                                             uint64_t Seed, KeySource Source) {
  llvm::StringRef Name = ir::kScPtrDecrypt;
  if (llvm::Function *F = M.getFunction(Name))
    return F;

  llvm::LLVMContext &Ctx = M.getContext();
  llvm::Type *I64 = llvm::Type::getInt64Ty(Ctx);
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(Ctx);

  llvm::FunctionType *FTy = llvm::FunctionType::get(PtrTy, {I64}, false);
  llvm::Function *F = llvm::Function::Create(
      FTy, llvm::GlobalValue::InternalLinkage, Name, &M);
  F->addFnAttr(llvm::Attribute::AlwaysInline);
  setHelperAttrs(F);
  F->setMemoryEffects(llvm::MemoryEffects::none());

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  llvm::Function *DK = getOrCreateDeriveKey(M, T, Seed, Source);
  llvm::CallInst *Key = B.CreateCall(DK, {}, "key");
  Key->setDoesNotThrow();
  llvm::Value *Dec = emitXorFree(B, F->getArg(0), Key);
  llvm::Value *Ptr = B.CreateIntToPtr(Dec, PtrTy, "ptr");
  B.CreateRet(Ptr);
  return F;
}

inline llvm::GlobalVariable *getOrCreateCacheSlot(llvm::Module &M,
                                                  llvm::StringRef ApiName,
                                                  llvm::StringRef Prefix,
                                                  llvm::StringRef TextSection) {
  llvm::Type *I64 = llvm::Type::getInt64Ty(M.getContext());
  std::string SlotName =
      (llvm::Twine(ir::kScCachePrefix) + Prefix + "_" + ApiName).str();
  if (llvm::GlobalVariable *GV = M.getGlobalVariable(SlotName))
    return GV;

  auto *GV = new llvm::GlobalVariable(M, I64, /*isConstant=*/false,
                                      llvm::GlobalValue::InternalLinkage,
                                      llvm::ConstantInt::get(I64, 0), SlotName);
  GV->setAlignment(llvm::Align(8));
  GV->setDSOLocal(true);
  GV->setSection(TextSection);
  return GV;
}

/// Emit the cache fast/slow path pattern shared by PEB-import and
/// kernel-import wrappers.  B must be positioned in the function's
/// entry block.  \p resolveEmitter is called with B positioned in
/// the slow-path block and must return a ptr-typed Value* (the raw
/// resolved function pointer, or null on failure).  On return, B is
/// positioned in the merge block after the PHI.
template <typename ResolveEmitter>
llvm::PHINode *emitCacheFastSlowPath(llvm::IRBuilder<> &B, llvm::Module &M,
                                     const TargetDesc &T, llvm::Function *F,
                                     llvm::GlobalVariable *CacheSlot,
                                     uint64_t Seed, KeySource KS,
                                     ResolveEmitter emit) {
  assert(B.GetInsertBlock()->getParent() == F &&
         "IRBuilder must be positioned inside the target function");
  assert(B.GetInsertBlock()->empty() &&
         "entry block must be empty before emitCacheFastSlowPath");

  llvm::LLVMContext &Ctx = M.getContext();
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(Ctx);
  llvm::Type *I64 = llvm::Type::getInt64Ty(Ctx);

  llvm::Function *Encrypt = getOrCreatePtrEncrypt(M, T, Seed, KS);
  llvm::Function *Decrypt = getOrCreatePtrDecrypt(M, T, Seed, KS);

  llvm::BasicBlock *FastBB = llvm::BasicBlock::Create(Ctx, "fast", F);
  llvm::BasicBlock *SlowBB = llvm::BasicBlock::Create(Ctx, "slow", F);
  llvm::BasicBlock *StoreBB = llvm::BasicBlock::Create(Ctx, "store", F);
  llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(Ctx, "merge", F);

  auto *Cached = B.CreateLoad(I64, CacheSlot, "cached");
  Cached->setAtomic(llvm::AtomicOrdering::Monotonic);
  Cached->setAlignment(llvm::Align(8));
  llvm::Value *IsZero =
      B.CreateICmpEQ(Cached, llvm::ConstantInt::get(I64, 0), "empty");
  llvm::MDBuilder MDB(Ctx);
  auto *Br = B.CreateCondBr(IsZero, SlowBB, FastBB);
  Br->setMetadata(llvm::LLVMContext::MD_prof,
                  MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(FastBB);
  llvm::CallInst *DecFn = B.CreateCall(Decrypt, {Cached}, "dec.fn");
  DecFn->setDoesNotThrow();
  B.CreateBr(MergeBB);

  B.SetInsertPoint(SlowBB);
  llvm::Value *RawFn = emit(B);
  llvm::Value *IsNull =
      B.CreateICmpEQ(RawFn, llvm::ConstantPointerNull::get(PtrTy));
  auto *NullBr = B.CreateCondBr(IsNull, MergeBB, StoreBB);
  NullBr->setMetadata(llvm::LLVMContext::MD_prof,
                      MDB.createBranchWeights(1, 2000));
  llvm::BasicBlock *SlowExit = B.GetInsertBlock();

  B.SetInsertPoint(StoreBB);
  llvm::CallInst *EncFn = B.CreateCall(Encrypt, {RawFn}, "enc.fn");
  EncFn->setDoesNotThrow();
  auto *CX = B.CreateAtomicCmpXchg(
      CacheSlot, llvm::ConstantInt::get(I64, 0), EncFn, llvm::Align(8),
      llvm::AtomicOrdering::Release, llvm::AtomicOrdering::Monotonic);
  CX->setWeak(true);
  B.CreateBr(MergeBB);

  B.SetInsertPoint(MergeBB);
  llvm::PHINode *FnPhi = B.CreatePHI(PtrTy, 3, "fn.ptr");
  FnPhi->addIncoming(DecFn, FastBB);
  FnPhi->addIncoming(RawFn, SlowExit);
  FnPhi->addIncoming(RawFn, StoreBB);

  return FnPhi;
}

} // namespace ptrcache
} // namespace shellcode
} // namespace neverc

#endif
