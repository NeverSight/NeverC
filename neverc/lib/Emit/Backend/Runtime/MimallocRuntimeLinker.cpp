#include "Backend/Runtime/MimallocRuntimeLinker.h"
#include "Backend/Runtime/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinMimalloc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

constexpr StringLiteral MimallocRuntimeMetadata = "neverc.mimalloc.runtime";
constexpr StringLiteral MimallocRuntimeLocalPrefix =
    "__neverc_mimalloc_local.";

bool isMallocOverrideSymbol(StringRef Name) {
#define NEVERC_MALLOC_OVERRIDE_EXACT(sym) if (Name == #sym) return true;
#define NEVERC_MALLOC_OVERRIDE_PREFIX(pfx) if (Name.starts_with(#pfx)) return true;
#include "neverc/Foundation/Builtin/MallocOverrideSymbols.def"
  return false;
}

Function *getOrCreateOnceWrapper(Module &M, Function &Target,
                                 StringRef Role) {
  if (!Target.getReturnType()->isVoidTy() || Target.arg_size() != 0)
    return &Target;

  const std::string Stem =
      ("__neverc_mimalloc_" + Role + "_once." + Target.getName()).str();
  if (Function *Existing = M.getFunction(Stem))
    return Existing;

  LLVMContext &Context = M.getContext();
  auto *Guard = new GlobalVariable(
      M, Type::getInt1Ty(Context), false, GlobalValue::LinkOnceODRLinkage,
      ConstantInt::getFalse(Context), Stem + ".guard");
  Guard->setVisibility(GlobalValue::HiddenVisibility);

  Function *Wrapper =
      Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                       GlobalValue::LinkOnceODRLinkage, Stem, M);
  Wrapper->setVisibility(GlobalValue::HiddenVisibility);

  BasicBlock *Entry = BasicBlock::Create(Context, "entry", Wrapper);
  BasicBlock *Call = BasicBlock::Create(Context, "call", Wrapper);
  BasicBlock *Return = BasicBlock::Create(Context, "return", Wrapper);
  IRBuilder<> Builder(Entry);
  LoadInst *AlreadyCalled =
      Builder.CreateLoad(Type::getInt1Ty(Context), Guard);
  AlreadyCalled->setVolatile(true);
  Builder.CreateCondBr(AlreadyCalled, Return, Call);

  Builder.SetInsertPoint(Call);
  StoreInst *MarkCalled = Builder.CreateStore(ConstantInt::getTrue(Context),
                                               Guard);
  MarkCalled->setVolatile(true);
  CallInst *Invoke = Builder.CreateCall(&Target);
  Invoke->setCallingConv(Target.getCallingConv());
  Builder.CreateBr(Return);

  Builder.SetInsertPoint(Return);
  Builder.CreateRetVoid();
  return Wrapper;
}

void prepareRuntimeStructors(Module &M, StringRef GlobalName, StringRef Role,
                             bool WrapOnce) {
  auto *GV = M.getGlobalVariable(GlobalName);
  if (!GV || !GV->hasInitializer())
    return;
  auto *Init = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!Init)
    return;

  SmallVector<Constant *, 4> Entries;
  Entries.reserve(Init->getNumOperands());
  bool Changed = false;
  for (Value *Operand : Init->operands()) {
    auto *Entry = dyn_cast<ConstantStruct>(Operand);
    if (!Entry || Entry->getNumOperands() != 3) {
      Entries.push_back(cast<Constant>(Operand));
      continue;
    }

    Constant *Priority = cast<Constant>(Entry->getOperand(0));
    Constant *OriginalFunction = cast<Constant>(Entry->getOperand(1));
    Constant *FunctionValue = OriginalFunction;
    Constant *Associated = cast<Constant>(Entry->getOperand(2));
    auto *Target =
        dyn_cast<Function>(FunctionValue->stripPointerCasts());
    if (Target && WrapOnce) {
      FunctionValue = getOrCreateOnceWrapper(M, *Target, Role);
      Changed = true;
    }
    if (Target && (WrapOnce || Associated->isNullValue())) {
      if (FunctionValue->getType() != Associated->getType())
        FunctionValue = ConstantExpr::getPointerCast(
            FunctionValue, Associated->getType());
      Associated = FunctionValue;
      Changed = true;
    }

    if (FunctionValue->getType() != OriginalFunction->getType())
      FunctionValue = ConstantExpr::getPointerCast(
          FunctionValue, OriginalFunction->getType());
    Constant *Fields[] = {Priority, FunctionValue, Associated};
    Entries.push_back(ConstantStruct::get(
        cast<StructType>(Entry->getType()), Fields));
  }

  if (Changed)
    GV->setInitializer(
        ConstantArray::get(cast<ArrayType>(Init->getType()), Entries));
}

} // namespace

PreservedAnalyses
MimallocRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  Triple TT(M.getTargetTriple());
  StringRef Embedded = BuiltinMimalloc::getEmbeddedBitcode(TT);
  if (Embedded.empty())
    return PreservedAnalyses::all();

  auto MimallocMod =
      parseBitcodeAndPrepare(Embedded, M, "neverc mimalloc runtime");
  namespaceRuntimeLocals(*MimallocMod, MimallocRuntimeLocalPrefix);

  // Associate each constructor/destructor record with its runtime function.
  // Native linkers can then discard duplicate records together with duplicate
  // linkonce_odr functions from other consumer TUs.
  // Mach-O has no COMDAT association for separate non-LTO constructor
  // sections, so make those duplicate calls idempotent as well.
  prepareRuntimeStructors(*MimallocMod, "llvm.global_ctors", "ctor",
                          !IsPreLink);
  prepareRuntimeStructors(*MimallocMod, "llvm.global_dtors", "dtor",
                          !IsPreLink);

  tagRuntimeDefinitions(*MimallocMod, MimallocRuntimeMetadata);
  linkModuleOrFail(M, std::move(MimallocMod), "neverc mimalloc runtime");

  // mimalloc's Windows path calls OpenProcessToken / AdjustTokenPrivileges /
  // LookupPrivilegeValueA.  Re-add the advapi32 defaultlib after
  // parseBitcodeAndPrepare strips host linker.options, and independently of
  // whether PCG later preserves .drectve from partition 0.
  if (TT.isOSBinFormatCOFF()) {
    auto *NMD = M.getOrInsertNamedMetadata("llvm.linker.options");
    bool HasAdvapi = false;
    for (const MDNode *Op : NMD->operands()) {
      for (const MDOperand &Piece : Op->operands()) {
        if (auto *S = dyn_cast<MDString>(Piece)) {
          if (S->getString().contains_insensitive("advapi32")) {
            HasAdvapi = true;
            break;
          }
        }
      }
      if (HasAdvapi)
        break;
    }
    if (!HasAdvapi) {
      LLVMContext &Ctx = M.getContext();
      Metadata *Ops[] = {MDString::get(Ctx, "/DEFAULTLIB:advapi32.lib")};
      NMD->addOperand(MDNode::get(Ctx, Ops));
    }
  }

  auto IsMimallocFn = [](const Function &F) {
    return hasRuntimeDefinitionTag(F, MimallocRuntimeMetadata);
  };
  auto IsMimallocGlobal = [](const GlobalVariable &GV) {
    return hasRuntimeDefinitionTag(GV, MimallocRuntimeMetadata);
  };

  for (Function &F : M) {
    if (!IsMimallocFn(F))
      continue;
    if (!F.hasLocalLinkage() && isMallocOverrideSymbol(F.getName())) {
      // Keep allocator entry points externally visible, but make independently
      // embedded copies coalescible across auto/full/no-LTO translation units.
      makeWeakODR(F, GlobalValue::DefaultVisibility);
    } else {
      makeLinkOnceODR(F);
    }
  }

  // mimalloc exposes many allocator overrides as aliases (malloc/free,
  // __libc_*, strdup, C++ operator new/delete, ...).  Updating only Function
  // linkage — or only the small malloc-family name list — leaves the rest as
  // strong aliases on ELF, so every non-LTO consumer TU contributes another
  // definition and the native link reports duplicates.  Aliases cannot own
  // COMDAT groups; weak ODR linkage is the coalescing mechanism for them.
  for (GlobalAlias &GA : M.aliases()) {
    if (GA.hasLocalLinkage())
      continue;
    GlobalObject *Aliasee = GA.getAliaseeObject();
    if (!Aliasee ||
        !hasRuntimeDefinitionTag(*Aliasee, MimallocRuntimeMetadata))
      continue;
    GA.setLinkage(GlobalValue::WeakODRLinkage);
    GA.setVisibility(GlobalValue::DefaultVisibility);
  }

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isDeclaration() && IsMimallocGlobal(GV) &&
        !GV.hasAppendingLinkage()) {
      makeLinkOnceODR(GV);
    }
  }

  removeFromUsedLists(M, [&](Constant *C) {
    auto *GV = dyn_cast<GlobalValue>(C->stripPointerCasts());
    if (!GV)
      return isa<PoisonValue>(C) || isa<UndefValue>(C);
    if (auto *F = dyn_cast<Function>(GV))
      return IsMimallocFn(*F) &&
             !isMallocOverrideSymbol(F->getName());
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return IsMimallocGlobal(*GVar);
    return false;
  });

  clearRuntimeDefinitionTags(M, MimallocRuntimeMetadata);
  return PreservedAnalyses::none();
}
