#include "neverc/Shellcode/IR/ExternRewriter.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

void applyScHelperFnAttrs(Function *F) {
  LLVMContext &Ctx = F->getContext();
  AttrBuilder AB(Ctx);
  AB.addAttribute(Attribute::AlwaysInline);
  AB.addAttribute(Attribute::NoUnwind);
  AB.addAttribute(Attribute::WillReturn);
  AB.addAttribute(Attribute::NoSync);
  AB.addAttribute(Attribute::NoRecurse);
  AB.addAttribute(Attribute::NoFree);
  AB.addAttribute(Attribute::NoCallback);
  AB.addAttribute(Attribute::MustProgress);
  AB.addAttribute(Attribute::NoProfile);
  F->addFnAttrs(AB);
  F->addFnAttr("no-stack-arg-probe");
  F->setDSOLocal(true);
}

} // namespace

Function *getOrCreateScHelper(Module &M, StringRef Name, FunctionType *FTy) {
  if (Function *F = M.getFunction(Name)) {
    if (F->isDeclaration()) {
      F->setLinkage(GlobalValue::InternalLinkage);
      F->setDSOLocal(true);
    }
    return F;
  }
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  applyScHelperFnAttrs(F);
  return F;
}

bool rewriteExternCalls(Module &M, Function &Decl, Function &Helper) {
  SmallVector<Use *, 8> UsesToRewrite;
  UsesToRewrite.reserve(Decl.getNumUses());
  for (Use &U : Decl.uses())
    UsesToRewrite.push_back(&U);
  if (UsesToRewrite.empty())
    return false;

  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *HelperFTy = Helper.getFunctionType();
  bool Changed = false;

  unsigned NumFormal = HelperFTy->getNumParams();

  FunctionType *DeclFTy = Decl.getFunctionType();
  bool ExactMatch = DeclFTy->getNumParams() == NumFormal &&
                    DeclFTy->getReturnType() == HelperFTy->getReturnType();
  if (ExactMatch) {
    for (unsigned Idx = 0; Idx < NumFormal; ++Idx) {
      if (DeclFTy->getParamType(Idx) != HelperFTy->getParamType(Idx)) {
        ExactMatch = false;
        break;
      }
    }
  }

  for (Use *U : UsesToRewrite) {
    auto *Call = dyn_cast<CallBase>(U->getUser());
    if (!Call || &Call->getCalledOperandUse() != U)
      continue;
    IRBuilder<> B(Call);
    SmallVector<Value *, 8> Args;
    Args.reserve(NumFormal);

    if (ExactMatch && Call->arg_size() >= NumFormal) {
      for (unsigned Idx = 0; Idx < NumFormal; ++Idx)
        Args.push_back(Call->getArgOperand(Idx));
    } else {
      for (unsigned Idx = 0; Idx < NumFormal; ++Idx) {
        Value *A = Idx < Call->arg_size() ? Call->getArgOperand(Idx) : nullptr;
        Type *WantTy = HelperFTy->getParamType(Idx);
        if (!A) {
          A = WantTy->isPointerTy()
                  ? static_cast<Value *>(ConstantPointerNull::get(PtrTy))
                  : static_cast<Value *>(ConstantInt::get(WantTy, 0));
        } else if (A->getType() != WantTy) {
          if (WantTy->isPointerTy() && A->getType()->isPointerTy())
            A = B.CreateBitCast(A, WantTy);
          else if (WantTy->isIntegerTy() && A->getType()->isIntegerTy())
            A = B.CreateZExtOrTrunc(A, WantTy);
          else if (WantTy->isIntegerTy() && A->getType()->isPointerTy())
            A = B.CreatePtrToInt(A, WantTy);
          else if (WantTy->isPointerTy() && A->getType()->isIntegerTy())
            A = B.CreateIntToPtr(A, WantTy);
          else
            A = B.CreateBitOrPointerCast(A, WantTy);
        }
        Args.push_back(A);
      }
    }

    CallInst *NewCall = B.CreateCall(&Helper, Args);
    NewCall->setCallingConv(Helper.getCallingConv());
    Value *Replacement = NewCall;
    if (!ExactMatch) {
      Type *OrigRetTy = Call->getType();
      if (!OrigRetTy->isVoidTy() && NewCall->getType() != OrigRetTy) {
        if (OrigRetTy->isPointerTy() && NewCall->getType()->isPointerTy())
          Replacement = B.CreateBitCast(NewCall, OrigRetTy);
        else if (OrigRetTy->isIntegerTy() && NewCall->getType()->isIntegerTy())
          Replacement = B.CreateZExtOrTrunc(NewCall, OrigRetTy);
        else
          Replacement = B.CreateBitOrPointerCast(NewCall, OrigRetTy);
      }
    }
    if (!Call->use_empty())
      Call->replaceAllUsesWith(Replacement);
    Call->eraseFromParent();
    Changed = true;
  }
  if (Decl.use_empty())
    Decl.eraseFromParent();
  return Changed;
}

} // namespace shellcode
} // namespace neverc
