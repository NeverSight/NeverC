#ifndef NEVERC_SHELLCODE_IRHELPERS_H
#define NEVERC_SHELLCODE_IRHELPERS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace neverc {
namespace shellcode {
namespace IRHelpers {

inline llvm::Type *getSizeType(llvm::Module &M) {
  unsigned Bits = M.getDataLayout().getPointerSizeInBits();
  if (Bits == 0)
    Bits = 64;
  return llvm::IntegerType::get(M.getContext(), Bits);
}

inline llvm::Function *getOrDeclareExtern(llvm::Module &M,
                                          llvm::StringRef Name,
                                          llvm::FunctionType *FTy) {
  if (llvm::Function *F = M.getFunction(Name))
    return F;
  llvm::Function *F =
      llvm::Function::Create(FTy, llvm::GlobalValue::ExternalLinkage, Name, &M);
  F->addFnAttr(llvm::Attribute::NoUnwind);
  return F;
}

inline llvm::Function *getOrDeclareMmap(llvm::Module &M) {
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(M.getContext());
  llvm::Type *I32 = llvm::Type::getInt32Ty(M.getContext());
  llvm::Type *I64 = llvm::Type::getInt64Ty(M.getContext());
  return getOrDeclareExtern(
      M, "mmap",
      llvm::FunctionType::get(PtrTy, {PtrTy, I64, I32, I32, I32, I64}, false));
}

inline llvm::Function *getOrDeclareMunmap(llvm::Module &M) {
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(M.getContext());
  llvm::Type *I64 = llvm::Type::getInt64Ty(M.getContext());
  return getOrDeclareExtern(
      M, "munmap",
      llvm::FunctionType::get(llvm::Type::getInt32Ty(M.getContext()),
                              {PtrTy, I64}, false));
}

} // namespace IRHelpers
} // namespace shellcode
} // namespace neverc

#endif
