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
#include "llvm/IR/Module.h"

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

inline llvm::Value *emitReadPEB(llvm::IRBuilder<> &B, const TargetDesc &T) {
  llvm::LLVMContext &Ctx = B.getContext();
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(Ctx);
  llvm::FunctionType *FTy = llvm::FunctionType::get(PtrTy, {}, false);
  llvm::InlineAsm *Asm = llvm::InlineAsm::get(
      FTy, T.TCBReadAsm.str(), T.TCBReadConstraint.str(),
      /*hasSideEffects=*/true, /*isAlignStack=*/false,
      llvm::InlineAsm::AD_ATT);
  return B.CreateCall(FTy, Asm);
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

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  llvm::Value *Key;
  if (Source == KeySource::PEB && !T.TCBReadAsm.empty()) {
    llvm::Value *PEB = emitReadPEB(B, T);
    llvm::Value *PEBInt = B.CreatePtrToInt(PEB, I64, "peb.int");
    Key = B.CreateXor(PEBInt, llvm::ConstantInt::get(I64, Seed), "key");
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

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  llvm::Function *DK = getOrCreateDeriveKey(M, T, Seed, Source);
  llvm::Value *Key = B.CreateCall(DK, {}, "key");
  llvm::Value *Plain = B.CreatePtrToInt(F->getArg(0), I64, "plain");
  llvm::Value *Enc = B.CreateXor(Plain, Key, "enc");
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

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  llvm::Function *DK = getOrCreateDeriveKey(M, T, Seed, Source);
  llvm::Value *Key = B.CreateCall(DK, {}, "key");
  llvm::Value *Dec = B.CreateXor(F->getArg(0), Key, "dec");
  llvm::Value *Ptr = B.CreateIntToPtr(Dec, PtrTy, "ptr");
  B.CreateRet(Ptr);
  return F;
}

inline llvm::GlobalVariable *getOrCreateCacheSlot(llvm::Module &M,
                                                  llvm::StringRef ApiName,
                                                  llvm::StringRef Prefix) {
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
  GV->setSection(".text");
  return GV;
}

} // namespace ptrcache
} // namespace shellcode
} // namespace neverc

#endif
