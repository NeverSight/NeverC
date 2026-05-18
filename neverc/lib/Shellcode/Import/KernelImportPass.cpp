#include "neverc/Shellcode/Import/KernelImportPass.h"
#include "ExtractorCommon.h"
#include "neverc/Shellcode/Import/KernelImportABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

bool isReservedHelper(StringRef Name) {
#define NEVERC_KERN_IMPORT_RESERVED_PREFIX(prefix)                             \
  if (Name.starts_with(prefix))                                                \
    return true;
#include "neverc/Shellcode/Tables/KernelImportReservedPrefixes.def"
#include "neverc/Shellcode/Tables/UserExtra_KernelImportReservedPrefixes.def"
#undef NEVERC_KERN_IMPORT_RESERVED_PREFIX
  StringRef Bare = stripLeadingUnderscore(Name);
  if (isReservedMemStdlibName(Bare) || isBuiltinStringRuntimeName(Bare) ||
      isHeapAllocatorName(Bare))
    return true;
  if (isCompilerRtRuntimeHelperName(Bare))
    return true;
  return false;
}

Function *findEntry(Module &M, StringRef UserEntry) {
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (isShellcodeEntryCandidate(F.getName(), UserEntry))
      return &F;
  }
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (!F.hasInternalLinkage() && !F.hasPrivateLinkage())
      return &F;
  }
  return nullptr;
}

Function *injectResolverParams(Module &M, Function *Entry,
                               GlobalVariable *ResolverGV,
                               GlobalVariable *CookieGV) {
  LLVMContext &Ctx = M.getContext();
  FunctionType *OldTy = Entry->getFunctionType();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  SmallVector<Type *, 8> ShimParams;
  ShimParams.push_back(PtrTy); // resolver
  ShimParams.push_back(PtrTy); // cookie
  for (Type *T : OldTy->params())
    ShimParams.push_back(T);

  FunctionType *ShimTy =
      FunctionType::get(OldTy->getReturnType(), ShimParams, OldTy->isVarArg());

  std::string OrigName = Entry->getName().str();
  Entry->setName(Twine(OrigName) + KernelResolverABI::OrigEntryRenameSuffix);
  Entry->setLinkage(GlobalValue::InternalLinkage);
  Entry->addFnAttr(Attribute::AlwaysInline);

  Function *Shim =
      Function::Create(ShimTy, GlobalValue::ExternalLinkage, OrigName, &M);
  Shim->addFnAttr(Attribute::NoUnwind);
  Shim->setDSOLocal(true);

  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Shim);
  IRBuilder<> B(BB);

  B.CreateStore(Shim->getArg(0), ResolverGV);
  B.CreateStore(Shim->getArg(1), CookieGV);

  SmallVector<Value *, 8> FwdArgs;
  for (unsigned I = 2, E = Shim->arg_size(); I < E; ++I)
    FwdArgs.push_back(Shim->getArg(I));

  CallInst *CI = B.CreateCall(Entry, FwdArgs);
  CI->setCallingConv(Entry->getCallingConv());

  if (OldTy->getReturnType()->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(CI);

  return Shim;
}

CallInst *emitResolvedKernelCall(IRBuilder<> &B, Function &Decl,
                                 FunctionType *CallTy, ArrayRef<Value *> Args,
                                 ArrayRef<OperandBundleDef> Bundles,
                                 GlobalVariable *ResolverGV,
                                 GlobalVariable *CookieGV) {
  LLVMContext &Ctx = Decl.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);

  Value *Resolver =
      B.CreateLoad(PtrTy, ResolverGV, KernelResolverABI::IRNames::ResolverLoad);
  Value *Cookie =
      B.CreateLoad(PtrTy, CookieGV, KernelResolverABI::IRNames::CookieLoad);

  FunctionType *ResolverTy = FunctionType::get(PtrTy, {I64, PtrTy}, false);
  StringRef Bare = stripLeadingUnderscore(Decl.getName());
  Value *HashV = ConstantInt::get(I64, KernelResolverABI::hashName(Bare));
  Value *FnPtr = B.CreateCall(ResolverTy, Resolver, {HashV, Cookie},
                              KernelResolverABI::IRNames::ResolvedCallee);

  CallInst *CI = B.CreateCall(CallTy, FnPtr, Args, Bundles);
  CI->setDoesNotThrow();
  return CI;
}

void collectAllKernelCalls(
    Module &M, const SmallDenseSet<Function *, 8> &Externs,
    DenseMap<Function *, SmallVector<CallInst *, 4>> &CallMap) {
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        auto *CI = dyn_cast<CallInst>(&I);
        if (!CI)
          continue;
        Value *Callee = CI->getCalledOperand();
        if (!Callee)
          continue;
        auto *CalledFn = dyn_cast<Function>(Callee->stripPointerCasts());
        if (!CalledFn || !Externs.contains(CalledFn))
          continue;
        CallMap[CalledFn].push_back(CI);
      }
    }
  }
}

void reportAddressTakenKernelExtern(const Function &Ext) {
  errs() << KernelResolverABI::DiagnosticPrefix
         << "cannot rewrite address-taken kernel extern '" << Ext.getName()
         << "'. " << KernelResolverABI::AddressTakenExternHint << "\n";
}

bool eraseExternIfUnused(Function *Ext) {
  Ext->removeDeadConstantUsers();
  if (!Ext->use_empty())
    return false;
  Ext->eraseFromParent();
  return true;
}

unsigned rewriteDirectKernelCalls(Module &M, Function &Decl,
                                  ArrayRef<CallInst *> Calls,
                                  GlobalVariable *ResolverGV,
                                  GlobalVariable *CookieGV) {
  unsigned Rewritten = 0;
  for (CallInst *CI : Calls) {
    SmallVector<Value *, 8> Args;
    for (Value *Arg : CI->args())
      Args.push_back(Arg);
    SmallVector<OperandBundleDef, 2> Bundles;
    CI->getOperandBundlesAsDefs(Bundles);

    IRBuilder<> B(CI);
    CallInst *NewCall = emitResolvedKernelCall(
        B, Decl, CI->getFunctionType(), Args, Bundles, ResolverGV, CookieGV);
    NewCall->setCallingConv(CI->getCallingConv());
    NewCall->setAttributes(CI->getAttributes());
    if (CI->getTailCallKind() != CallInst::TCK_MustTail)
      NewCall->setTailCallKind(CI->getTailCallKind());
    NewCall->setDebugLoc(CI->getDebugLoc());

    if (!CI->getType()->isVoidTy()) {
      NewCall->takeName(CI);
      CI->replaceAllUsesWith(NewCall);
    }
    CI->eraseFromParent();
    ++Rewritten;
  }
  return Rewritten;
}

} // namespace

PreservedAnalyses KernelImportPass::run(Module &M, ModuleAnalysisManager &) {
  if (Target.Level != ExecutionLevel::Kernel ||
      Target.KernelImport == KernelImportABI::None)
    return PreservedAnalyses::all();

  SmallVector<Function *, 8> Externs;
  for (Function &F : M) {
    if (!F.isDeclaration())
      continue;
    if (isReservedHelper(F.getName()))
      continue;
    F.removeDeadConstantUsers();
    if (F.use_empty())
      continue;
    Externs.push_back(&F);
  }
  if (Externs.empty())
    return PreservedAnalyses::all();

  SmallDenseSet<Function *, 8> ExternSet(Externs.begin(), Externs.end());
  DenseMap<Function *, SmallVector<CallInst *, 4>> CallMap;
  collectAllKernelCalls(M, ExternSet, CallMap);

  SmallVector<Function *, 8> DirectExterns;
  SmallDenseSet<Function *, 8> DirectExternSet;
  for (Function *Ext : Externs) {
    auto It = CallMap.find(Ext);
    if (It != CallMap.end() && !It->second.empty()) {
      DirectExterns.push_back(Ext);
      DirectExternSet.insert(Ext);
    }
  }

  if (DirectExterns.empty()) {
    for (Function *Ext : Externs)
      reportAddressTakenKernelExtern(*Ext);
    return PreservedAnalyses::all();
  }

  Function *Entry = findEntry(M, EntrySymbol);
  if (!Entry) {
    errs() << KernelResolverABI::DiagnosticPrefix
           << "cannot find entry function for resolver parameter injection; "
           << KernelResolverABI::MissingEntryHint << " ("
           << defaultEntryNameList() << " or -fshellcode-entry=<name>).\n";
    return PreservedAnalyses::all();
  }

  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  auto *ResolverGV = new GlobalVariable(
      M, PtrTy, /*isConstant=*/false, GlobalValue::InternalLinkage,
      ConstantPointerNull::get(PtrTy), KernelResolverABI::ResolverGlobalName);
  auto *CookieGV = new GlobalVariable(
      M, PtrTy, /*isConstant=*/false, GlobalValue::InternalLinkage,
      ConstantPointerNull::get(PtrTy), KernelResolverABI::CookieGlobalName);

  injectResolverParams(M, Entry, ResolverGV, CookieGV);

  for (Function *Ext : DirectExterns) {
    auto It = CallMap.find(Ext);
    rewriteDirectKernelCalls(M, *Ext, It->second, ResolverGV, CookieGV);
    if (eraseExternIfUnused(Ext))
      continue;
    reportAddressTakenKernelExtern(*Ext);
  }

  for (Function *Ext : Externs) {
    if (DirectExternSet.contains(Ext))
      continue;
    if (eraseExternIfUnused(Ext))
      continue;
    reportAddressTakenKernelExtern(*Ext);
  }

  return PreservedAnalyses::none();
}

} // namespace shellcode
} // namespace neverc
