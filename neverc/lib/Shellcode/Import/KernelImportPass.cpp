#include "neverc/Shellcode/Import/KernelImportPass.h"
#include "ExtractorCommon.h"
#include "neverc/Shellcode/Import/KernelImportABI.h"
#include "neverc/Shellcode/Import/PtrCacheHelpers.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
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
#include <random>

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
  CI->setDoesNotThrow();

  if (OldTy->getReturnType()->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(CI);

  return Shim;
}

Function *getOrCreateKernelWrapper(Module &M, Function &Decl,
                                   GlobalVariable *ResolverGV,
                                   GlobalVariable *CookieGV,
                                   const TargetDesc &Target, uint64_t Seed) {
  std::string WrapName =
      ("__sc_kern_" + stripLeadingUnderscore(Decl.getName())).str();
  if (Function *F = M.getFunction(WrapName))
    return F;

  LLVMContext &Ctx = M.getContext();
  FunctionType *FTy = Decl.getFunctionType();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);

  Function *Wrap =
      Function::Create(FTy, GlobalValue::InternalLinkage, WrapName, &M);
  Wrap->setCallingConv(Decl.getCallingConv());
  Wrap->addFnAttr(Attribute::AlwaysInline);
  Wrap->addFnAttr(Attribute::NoUnwind);
  Wrap->addFnAttr(Attribute::NoRecurse);
  Wrap->setDSOLocal(true);

  StringRef Bare = stripLeadingUnderscore(Decl.getName());
  GlobalVariable *CacheSlot =
      ptrcache::getOrCreateCacheSlot(M, Bare, "kern", Target.TextSectionName);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Wrap);
  IRBuilder<> B(Entry);

  PHINode *FnPhi = ptrcache::emitCacheFastSlowPath(
      B, M, Target, Wrap, CacheSlot, Seed, ptrcache::KeySource::SeedOnly,
      [&](IRBuilder<> &SB) -> Value * {
        Value *Resolver = SB.CreateLoad(
            PtrTy, ResolverGV, KernelResolverABI::IRNames::ResolverLoad);
        Value *Cookie = SB.CreateLoad(
            PtrTy, CookieGV, KernelResolverABI::IRNames::CookieLoad);
        FunctionType *ResolverTy =
            FunctionType::get(PtrTy, {I64, PtrTy}, false);
        Value *HashV =
            ConstantInt::get(I64, KernelResolverABI::hashName(Bare));
        CallInst *RawFn = SB.CreateCall(
            ResolverTy, Resolver, {HashV, Cookie},
            KernelResolverABI::IRNames::ResolvedCallee);
        RawFn->setDoesNotThrow();
        return RawFn;
      });

  SmallVector<Value *, 8> CallArgs;
  for (Argument &A : Wrap->args())
    CallArgs.push_back(&A);

  CallInst *CI = B.CreateCall(FTy, FnPhi, CallArgs);
  CI->setCallingConv(Decl.getCallingConv());
  CI->setDoesNotThrow();

  if (FTy->getReturnType()->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(CI);

  return Wrap;
}

bool hasDirectCallUse(const Function &F) {
  for (const Use &U : F.uses()) {
    if (auto *CB = dyn_cast<CallBase>(U.getUser()))
      if (CB->getCalledOperand()->stripPointerCasts() == &F)
        return true;
  }
  return false;
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

void rewriteDirectKernelCalls(Module &M, Function &Decl,
                              GlobalVariable *ResolverGV,
                              GlobalVariable *CookieGV,
                              const TargetDesc &Target, uint64_t Seed) {
  Function *Wrap = getOrCreateKernelWrapper(M, Decl, ResolverGV, CookieGV,
                                            Target, Seed);
  Decl.replaceAllUsesWith(Wrap);
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

  SmallVector<Function *, 8> DirectExterns;
  SmallPtrSet<Function *, 8> DirectExternSet;
  for (Function *Ext : Externs) {
    if (hasDirectCallUse(*Ext)) {
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
  ResolverGV->setAlignment(Align(8));
  ResolverGV->setSection(Target.TextSectionName);
  auto *CookieGV = new GlobalVariable(
      M, PtrTy, /*isConstant=*/false, GlobalValue::InternalLinkage,
      ConstantPointerNull::get(PtrTy), KernelResolverABI::CookieGlobalName);
  CookieGV->setAlignment(Align(8));
  CookieGV->setSection(Target.TextSectionName);

  injectResolverParams(M, Entry, ResolverGV, CookieGV);

  std::random_device RD;
  std::mt19937_64 Gen(RD());
  uint64_t Seed = Gen();
  if (Seed == 0)
    Seed = 0xDEAD'C0DE'CAFE'BABE;

  for (Function *Ext : DirectExterns) {
    rewriteDirectKernelCalls(M, *Ext, ResolverGV, CookieGV, Target, Seed);
    eraseExternIfUnused(Ext);
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
