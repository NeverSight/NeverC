#include "Backend/StdRuntimeLinker.h"
#include "Backend/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Std/BuiltinStd.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

std::unique_ptr<Module> mergeEmbeddedModules(Module &UserMod) {
  unsigned Count = BuiltinStd::getEmbeddedModuleCount();
  if (Count == 0)
    return nullptr;

  std::unique_ptr<Module> Combined;

  for (unsigned I = 0; I < Count; ++I) {
    auto [Name, Data] = BuiltinStd::getEmbeddedModule(I);
    if (Data.empty())
      continue;

    std::string Label = ("neverc std: " + Name).str();
    auto Mod = parseBitcodeAndPrepare(Data, UserMod, Label);

    if (!Combined) {
      Combined = std::move(Mod);
    } else {
      if (Linker::linkModules(*Combined, std::move(Mod),
                              Linker::Flags::OverrideFromSrc))
        report_fatal_error(Twine("Failed to merge std module: ") + Name);
    }
  }

  return Combined;
}

} // namespace

PreservedAnalyses
StdRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  if (BuiltinStd::getEmbeddedModuleCount() == 0)
    return PreservedAnalyses::all();

  bool AnyStdUsed = false;
  for (const Function &F : M) {
    if (F.isDeclaration() && !F.use_empty() &&
        BuiltinStd::isStdRuntimeFunction(F.getName())) {
      AnyStdUsed = true;
      break;
    }
  }
  if (!AnyStdUsed)
    return PreservedAnalyses::all();

  auto StdMod = mergeEmbeddedModules(M);
  if (!StdMod)
    return PreservedAnalyses::all();

  StringSet<> StdFnNames, StdGlobalNames;
  captureDefinitionNames(*StdMod, StdFnNames, StdGlobalNames);

  // Call-graph + data-graph prune: walk both function bodies and
  // global-variable initializers for transitive references.
  SmallPtrSet<Function *, 32> Needed;
  SmallPtrSet<GlobalVariable *, 16> NeededGlobals;
  SmallVector<GlobalValue *, 32> Worklist;

  auto EnqueueFn = [&](Function *F) {
    if (F && !F->isDeclaration() && F->getParent() == StdMod.get() &&
        Needed.insert(F).second)
      Worklist.push_back(F);
  };
  auto EnqueueGV = [&](GlobalVariable *GV) {
    if (GV && !GV->isDeclaration() && GV->getParent() == StdMod.get() &&
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
    if (auto *F = StdMod->getFunction(Decl.getName()))
      EnqueueFn(F);
  }
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isDeclaration() || GV.use_empty())
      continue;
    if (auto *Def = StdMod->getGlobalVariable(GV.getName()))
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

  for (Function &F : make_early_inc_range(*StdMod)) {
    if (F.isDeclaration() || Needed.count(&F))
      continue;
    poisonAndErase(F);
  }
  for (GlobalVariable &GV : make_early_inc_range(StdMod->globals())) {
    if (GV.isDeclaration() || NeededGlobals.count(&GV))
      continue;
    poisonAndErase(GV);
  }

  linkModuleOrFail(M, std::move(StdMod), "neverc std runtime");

  auto IsStdFn = [&](const Function &F) {
    return StdFnNames.count(F.getName()) != 0;
  };
  auto IsStdGlobal = [&](const GlobalVariable &GV) {
    return StdGlobalNames.count(GV.getName()) != 0;
  };

  for (Function &F : M)
    if (IsStdFn(F))
      F.setLinkage(GlobalValue::InternalLinkage);
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && IsStdGlobal(GV))
      GV.setLinkage(GlobalValue::InternalLinkage);

  // Mark-and-sweep DCE for internalized symbols.
  SmallPtrSet<GlobalValue *, 32> Live;
  SmallVector<GlobalValue *, 32> ReachWorklist;

  auto Enqueue = [&](GlobalValue *GV) {
    if (GV && Live.insert(GV).second)
      ReachWorklist.push_back(GV);
  };

  auto IsTaggedStd = [&](GlobalValue *GV) -> bool {
    if (auto *F = dyn_cast<Function>(GV))
      return IsStdFn(*F);
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return IsStdGlobal(*GVar);
    return false;
  };

  for (Function &F : M)
    if (!IsTaggedStd(&F))
      Enqueue(&F);
  for (GlobalVariable &GV : M.globals())
    if (!IsTaggedStd(&GV))
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
    if (F.isDeclaration() || !IsStdFn(F) || Live.count(&F))
      continue;
    poisonAndErase(F);
  }
  for (GlobalVariable &GV : make_early_inc_range(M.globals())) {
    if (GV.isDeclaration() || !IsStdGlobal(GV) || Live.count(&GV))
      continue;
    poisonAndErase(GV);
  }

  removeFromUsedLists(M, [&](Constant *C) {
    if (auto *F = dyn_cast<Function>(C->stripPointerCasts()))
      return IsStdFn(*F);
    if (auto *GVar = dyn_cast<GlobalVariable>(C->stripPointerCasts()))
      return IsStdGlobal(*GVar);
    if (isa<PoisonValue>(C) || isa<UndefValue>(C))
      return true;
    return false;
  });

  return PreservedAnalyses::none();
}
