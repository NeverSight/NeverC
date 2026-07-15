#include "Backend/Runtime/NvkKernelRuntimeLinker.h"
#include "Backend/Runtime/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"
#include "neverc/Foundation/Builtin/BuiltinNvkKernelNames.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

constexpr StringLiteral NvkRuntimeMetadata = "neverc.nvk.runtime";
constexpr StringLiteral NvkRuntimeLocalPrefix = "__neverc_nvk_local.";

} // namespace

PreservedAnalyses
NvkKernelRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::aarch64)
    return PreservedAnalyses::all();

  StringRef Embedded = BuiltinNvkKernel::getEmbeddedBitcode();
  if (Embedded.empty())
    return PreservedAnalyses::all();

  if (!BuiltinNvkKernelNames::hasNvkKernelRuntimeSymbols(M))
    return PreservedAnalyses::all();

  auto NvkMod = parseBitcodeAndPrepare(Embedded, M, "nvk kernel runtime");
  namespaceRuntimeLocals(*NvkMod, NvkRuntimeLocalPrefix);

  // Call-graph + data-graph prune: keep only symbols transitively
  // reachable from user references (walks both function bodies AND
  // global-variable initializers).
  SmallPtrSet<Function *, 32> Needed;
  SmallPtrSet<GlobalVariable *, 16> NeededGlobals;
  SmallVector<GlobalValue *, 32> Worklist;

  auto EnqueueFn = [&](Function *F) {
    if (F && !F->isDeclaration() && F->getParent() == NvkMod.get() &&
        Needed.insert(F).second)
      Worklist.push_back(F);
  };
  auto EnqueueGV = [&](GlobalVariable *GV) {
    if (GV && !GV->isDeclaration() && GV->getParent() == NvkMod.get() &&
        NeededGlobals.insert(GV).second)
      Worklist.push_back(GV);
  };
  auto ProcessRef = [&](GlobalValue *GV) {
    if (auto *F = dyn_cast<Function>(GV))
      EnqueueFn(F);
    else if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      EnqueueGV(GVar);
  };

  for (Function &Decl : M) {
    if (!Decl.isDeclaration() || Decl.use_empty())
      continue;
    if (auto *F = NvkMod->getFunction(Decl.getName()))
      EnqueueFn(F);
  }
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isDeclaration() || GV.use_empty())
      continue;
    if (auto *Def = NvkMod->getGlobalVariable(GV.getName()))
      EnqueueGV(Def);
  }

  while (!Worklist.empty()) {
    GlobalValue *GV = Worklist.pop_back_val();
    if (auto *F = dyn_cast<Function>(GV)) {
      forEachGlobalReferencedBy(*F, ProcessRef);
    } else if (auto *GVar = dyn_cast<GlobalVariable>(GV)) {
      if (GVar->hasInitializer())
        forEachGlobalInConstant(GVar->getInitializer(), ProcessRef);
    }
  }

  for (Function &F : make_early_inc_range(*NvkMod)) {
    if (F.isDeclaration() || Needed.count(&F))
      continue;
    poisonAndErase(F);
  }
  for (GlobalVariable &GV : make_early_inc_range(NvkMod->globals())) {
    if (GV.isDeclaration() || NeededGlobals.count(&GV))
      continue;
    poisonAndErase(GV);
  }

  tagRuntimeDefinitions(*NvkMod, NvkRuntimeMetadata);
  linkModuleOrFail(M, std::move(NvkMod), "nvk kernel runtime");

  auto IsNvkFn = [](const Function &F) {
    return hasRuntimeDefinitionTag(F, NvkRuntimeMetadata);
  };
  auto IsNvkGlobal = [](const GlobalVariable &GV) {
    return hasRuntimeDefinitionTag(GV, NvkRuntimeMetadata);
  };

  // Every consumer TU may independently request the same runtime definition.
  // Keep those copies coalescible through both LTO and native -r links so
  // mutable runtime state remains shared in explicit -fno-lto builds too.
  auto MakeCoalescible = [](GlobalObject &GO) { makeLinkOnceODR(GO); };

  for (Function &F : M)
    if (IsNvkFn(F))
      MakeCoalescible(F);
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && IsNvkGlobal(GV) && !GV.hasAppendingLinkage())
      MakeCoalescible(GV);

  if (IsPreLink) {
    // Pre-link: skip DCE. The LTO optimizer will internalize and DCE
    // after merging all TUs.  Pruning (above) already removed functions
    // not transitively reachable from this TU's references.
    removeFromUsedLists(M, [&](Constant *C) {
      if (isa<PoisonValue>(C) || isa<UndefValue>(C))
        return true;
      return false;
    });
    clearRuntimeDefinitionTags(M, NvkRuntimeMetadata);
    return PreservedAnalyses::none();
  }

  // Non-LTO path: full mark-and-sweep DCE while retained definitions remain
  // linkonce_odr for the later native link.
  SmallPtrSet<GlobalValue *, 32> Live;
  SmallVector<GlobalValue *, 32> ReachWorklist;

  auto Enqueue = [&](GlobalValue *GV) {
    if (GV && Live.insert(GV).second)
      ReachWorklist.push_back(GV);
  };

  auto IsTaggedNvk = [&](GlobalValue *GV) -> bool {
    if (auto *F = dyn_cast<Function>(GV))
      return IsNvkFn(*F);
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return IsNvkGlobal(*GVar);
    return false;
  };

  for (Function &F : M)
    if (!IsTaggedNvk(&F))
      Enqueue(&F);
  for (GlobalVariable &GV : M.globals())
    if (!IsTaggedNvk(&GV))
      Enqueue(&GV);
  for (GlobalAlias &GA : M.aliases())
    Enqueue(&GA);

  while (!ReachWorklist.empty()) {
    GlobalValue *GV = ReachWorklist.pop_back_val();
    if (auto *Fn = dyn_cast<Function>(GV))
      forEachGlobalReferencedBy(*Fn, Enqueue);
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      if (GVar->hasInitializer())
        forEachGlobalInConstant(GVar->getInitializer(), Enqueue);
  }

  for (Function &F : make_early_inc_range(M)) {
    if (F.isDeclaration() || !IsNvkFn(F) || Live.count(&F))
      continue;
    poisonAndErase(F);
  }
  for (GlobalVariable &GV : make_early_inc_range(M.globals())) {
    if (GV.isDeclaration() || !IsNvkGlobal(GV) || Live.count(&GV))
      continue;
    poisonAndErase(GV);
  }

  removeFromUsedLists(M, [&](Constant *C) {
    if (auto *F = dyn_cast<Function>(C->stripPointerCasts()))
      return IsNvkFn(*F);
    if (auto *GVar = dyn_cast<GlobalVariable>(C->stripPointerCasts()))
      return IsNvkGlobal(*GVar);
    if (isa<PoisonValue>(C) || isa<UndefValue>(C))
      return true;
    return false;
  });

  clearRuntimeDefinitionTags(M, NvkRuntimeMetadata);
  return PreservedAnalyses::none();
}
