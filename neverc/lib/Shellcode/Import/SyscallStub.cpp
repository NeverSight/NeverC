#include "neverc/Shellcode/Import/SyscallStub.h"
#include "ExtractorCommon.h"
#include "neverc/Shellcode/Import/SyscallTables.h"
#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

Value *toABIWidth(IRBuilder<> &B, Value *V) {
  Type *Ty = V->getType();
  Type *I64 = B.getInt64Ty();
  if (Ty == I64)
    return V;
  if (Ty->isPointerTy())
    return B.CreatePtrToInt(V, I64);
  if (Ty->isIntegerTy()) {
    unsigned BW = Ty->getIntegerBitWidth();
    if (BW < 64)
      return B.CreateZExt(V, I64);
    if (BW > 64)
      return B.CreateTrunc(V, I64);
    return V;
  }
  return B.CreateBitOrPointerCast(V, I64);
}

Value *fromABIWidth(IRBuilder<> &B, Value *V, Type *DstTy) {
  if (DstTy->isVoidTy())
    return nullptr;
  if (V->getType() == DstTy)
    return V;
  if (DstTy->isPointerTy())
    return B.CreateIntToPtr(V, DstTy);
  if (DstTy->isIntegerTy()) {
    unsigned BW = DstTy->getIntegerBitWidth();
    if (BW < 64)
      return B.CreateTrunc(V, DstTy);
    if (BW > 64)
      return B.CreateZExt(V, DstTy);
    return V;
  }
  return B.CreateBitOrPointerCast(V, DstTy);
}

bool isSpecialPosixName(StringRef Name) {
  StringRef Bare = Name.starts_with("_") ? Name.drop_front(1) : Name;
  return Bare == "sleep";
}

std::string buildConstraint(const TargetDesc &T, size_t NumArgs) {
  SmallString<128> C;
  C += "={";
  C += T.SyscallRetReg;
  C += "},{";
  C += T.SyscallNumberReg;
  C += "}";
  for (size_t I = 0; I < NumArgs && I < T.NumArgRegs; ++I) {
    C += ",{";
    C += T.ArgRegs[I];
    C += "}";
  }
  C += ",~{memory},~{cc}";
  if (T.Syscall == SyscallABI::LinuxSyscall)
    C += ",~{rcx},~{r11}";
  return std::string(C);
}

Value *emitKernelCall(IRBuilder<> &B, const TargetDesc &T, uint64_t SyscallNr,
                      ArrayRef<Value *> ArgsI64) {
  LLVMContext &Ctx = B.getContext();
  Type *I64 = Type::getInt64Ty(Ctx);

  SmallVector<Type *, 9> ArgTys;
  ArgTys.push_back(I64); // syscall number
  for (Value *V : ArgsI64) {
    (void)V;
    ArgTys.push_back(I64);
  }
  FunctionType *FTy = FunctionType::get(I64, ArgTys, false);

  std::string Constraint = buildConstraint(T, ArgsI64.size());

  InlineAsm *Asm = InlineAsm::get(FTy, T.AsmTemplate.str(), Constraint,
                                  /*hasSideEffects=*/true,
                                  /*isAlignStack=*/false);

  uint64_t FinalNr = SyscallNr | T.SyscallNumberMask;

  SmallVector<Value *, 9> Args;
  Args.push_back(ConstantInt::get(I64, FinalNr));
  for (Value *A : ArgsI64)
    Args.push_back(A);

  return B.CreateCall(FTy, Asm, Args);
}

FunctionType *getCanonicalSyscallType(LLVMContext &Ctx, StringRef Name) {
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *Void = Type::getVoidTy(Ctx);
  PointerType *Ptr = PointerType::getUnqual(Ctx);

  StringRef Bare = Name.starts_with("_") ? Name.drop_front(1) : Name;

#define NEVERC_POSIX_CANON(name, ret, ...)                                     \
  if (Bare == #name)                                                           \
    return FunctionType::get(ret, {__VA_ARGS__}, false);
#define NEVERC_POSIX_CANON_NOARG(name, ret)                                    \
  if (Bare == #name)                                                           \
    return FunctionType::get(ret, {}, false);
#include "neverc/Shellcode/Tables/PosixCanonicalTypes.def"
#include "neverc/Shellcode/Tables/UserExtra_PosixCanonicalTypes.def"
#undef NEVERC_POSIX_CANON_NOARG
#undef NEVERC_POSIX_CANON

  (void)I32;
  (void)I64;
  (void)Void;
  (void)Ptr;
  return nullptr;
}

Function *createWrapper(Module &M, const TargetDesc &T, Function &Decl,
                        uint64_t SyscallNr, ArrayRef<int64_t> PrependArgs,
                        ArrayRef<int64_t> AppendArgs,
                        ArrayRef<CompatSlot> Template = {}) {
  LLVMContext &Ctx = M.getContext();
  FunctionType *FTy = Decl.getFunctionType();

  if (FTy->getNumParams() == 0) {
    if (FunctionType *Canon = getCanonicalSyscallType(Ctx, Decl.getName()))
      FTy = Canon;
  }
  Function *Wrap =
      Function::Create(FTy, GlobalValue::InternalLinkage,
                       Twine(ir::kScSysPrefix) + Decl.getName(), &M);
  Wrap->addFnAttr(Attribute::AlwaysInline);
  Wrap->addFnAttr(Attribute::NoUnwind);
  Wrap->setDSOLocal(true);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Wrap);
  IRBuilder<> B(Entry);

  SmallVector<Value *, 8> ArgsI64;

  if (!Template.empty()) {
    for (const CompatSlot &S : Template) {
      if (ArgsI64.size() >= T.NumArgRegs)
        break;
      if (S.IsUserArg) {
        unsigned Idx = static_cast<unsigned>(S.Value);
        if (Idx < Wrap->arg_size())
          ArgsI64.push_back(toABIWidth(B, Wrap->getArg(Idx)));
      } else {
        ArgsI64.push_back(
            ConstantInt::get(B.getInt64Ty(), static_cast<uint64_t>(S.Value)));
      }
    }
  } else {
    for (int64_t C : PrependArgs)
      ArgsI64.push_back(ConstantInt::get(B.getInt64Ty(), (uint64_t)C));
    for (Argument &A : Wrap->args()) {
      if (ArgsI64.size() >= T.NumArgRegs)
        break;
      ArgsI64.push_back(toABIWidth(B, &A));
    }
    for (int64_t C : AppendArgs) {
      if (ArgsI64.size() >= T.NumArgRegs)
        break;
      ArgsI64.push_back(ConstantInt::get(B.getInt64Ty(), (uint64_t)C));
    }
  }

  Value *RetI64 = emitKernelCall(B, T, SyscallNr, ArgsI64);

  Type *RetTy = FTy->getReturnType();
  if (RetTy->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(fromABIWidth(B, RetI64, RetTy));

  return Wrap;
}

Function *tryCreateSleepWrapper(Module &M, const TargetDesc &T,
                                Function &Decl) {
  StringRef Name = Decl.getName();
  StringRef Bare = Name.starts_with("_") ? Name.drop_front(1) : Name;
  if (Bare != "sleep")
    return nullptr;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy = Decl.getFunctionType();
  if (FTy->getNumParams() < 1)
    FTy = FunctionType::get(I32, {I32}, false);

  Function *Wrap = Function::Create(FTy, GlobalValue::InternalLinkage,
                                    ir::kScPosixSleep, &M);
  Wrap->addFnAttr(Attribute::AlwaysInline);
  Wrap->addFnAttr(Attribute::NoUnwind);
  Wrap->setDSOLocal(true);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Wrap);
  IRBuilder<> B(Entry);

  Value *Secs = Wrap->getArg(0);
  if (Secs->getType() != I64)
    Secs = B.CreateZExt(Secs, I64);

  if (T.Syscall == SyscallABI::DarwinSvc80 ||
      T.Syscall == SyscallABI::DarwinSyscall) {
    StructType *TVTy = StructType::get(Ctx, {I64, I32});
    Value *TV = B.CreateAlloca(TVTy, nullptr, "tv");
    B.CreateStore(Secs, B.CreateStructGEP(TVTy, TV, 0));
    B.CreateStore(ConstantInt::get(I32, 0), B.CreateStructGEP(TVTy, TV, 1));

    FunctionType *SelTy =
        FunctionType::get(I32, {I32, PtrTy, PtrTy, PtrTy, PtrTy}, false);
    FunctionCallee SelFn = M.getOrInsertFunction("select", SelTy);
    B.CreateCall(SelTy, SelFn.getCallee(),
                 {ConstantInt::get(I32, 0), ConstantPointerNull::get(PtrTy),
                  ConstantPointerNull::get(PtrTy),
                  ConstantPointerNull::get(PtrTy), TV});
  } else {
    StructType *TSTy = StructType::get(Ctx, {I64, I64});
    Value *TS = B.CreateAlloca(TSTy, nullptr, "ts");
    B.CreateStore(Secs, B.CreateStructGEP(TSTy, TS, 0));
    B.CreateStore(ConstantInt::get(I64, 0), B.CreateStructGEP(TSTy, TS, 1));

    FunctionType *NsTy = FunctionType::get(I32, {PtrTy, PtrTy}, false);
    FunctionCallee NsFn = M.getOrInsertFunction("nanosleep", NsTy);
    B.CreateCall(NsTy, NsFn.getCallee(), {TS, ConstantPointerNull::get(PtrTy)});
  }

  Type *RetTy = FTy->getReturnType();
  if (RetTy->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(ConstantInt::get(RetTy, 0));
  return Wrap;
}

} // namespace

PreservedAnalyses SyscallStubPass::run(Module &M, ModuleAnalysisManager &) {
  if (Target.Level == ExecutionLevel::Kernel)
    return PreservedAnalyses::all();
  if (Target.Syscall == SyscallABI::None ||
      Target.Syscall == SyscallABI::WindowsPEB)
    return PreservedAnalyses::all();
  if (Target.AsmTemplate.empty() || Target.SyscallNumberReg.empty())
    return PreservedAnalyses::all();

  {
    SmallVector<Function *, 4> Specials;
    for (Function &F : M) {
      if (!F.isDeclaration())
        continue;
      StringRef Name = F.getName();
      if (isShellcodeInternalRuntimeName(Name))
        continue;
      if (isSpecialPosixName(Name))
        Specials.push_back(&F);
    }
    for (Function *Decl : Specials) {
      if (Function *W = tryCreateSleepWrapper(M, Target, *Decl)) {
        Decl->replaceAllUsesWith(W);
        Decl->eraseFromParent();
      }
    }
  }

  SmallVector<Function *, 8> ToReplace;
  for (Function &F : M) {
    if (!F.isDeclaration())
      continue;
    StringRef Name = F.getName();
    if (isShellcodeInternalRuntimeName(Name))
      continue;
    if (lookupSyscall(Target, Name).Found ||
        lookupSyscallCompat(Target, Name).Found)
      ToReplace.push_back(&F);
  }

  if (ToReplace.empty())
    return PreservedAnalyses::all();

  DenseMap<StringRef, Function *> ByName;
  for (Function *Decl : ToReplace) {
    StringRef Name = Decl->getName();
    SyscallLookup L = lookupSyscall(Target, Name);
    ArrayRef<int64_t> Prepend;
    ArrayRef<int64_t> Append;
    ArrayRef<CompatSlot> Tmpl;
    if (!L.Found) {
      SyscallCompat C = lookupSyscallCompat(Target, Name);
      if (!C.Found)
        continue;
      L = lookupSyscall(Target, C.TargetName);
      if (!L.Found)
        continue;
      Prepend = C.PrependArgs;
      Append = C.AppendArgs;
      Tmpl = C.Template;
    }
    Function *&Slot = ByName[Name];
    if (!Slot)
      Slot = createWrapper(M, Target, *Decl, L.Number, Prepend, Append, Tmpl);
    Decl->replaceAllUsesWith(Slot);
    Decl->eraseFromParent();
  }

  return PreservedAnalyses::none();
}

} // namespace shellcode
} // namespace neverc
