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

template <typename VisitFn>
void forEachGlobalOperand(User *U, VisitFn Visit) {
  for (Use &Op : U->operands()) {
    if (auto *GV = dyn_cast<GlobalValue>(Op))
      Visit(GV);
    else if (auto *CE = dyn_cast<ConstantExpr>(Op))
      if (auto *Inner = dyn_cast<GlobalValue>(CE->stripPointerCasts()))
        Visit(Inner);
  }
}

template <typename VisitFn>
void forEachGlobalReferencedBy(Function &F, VisitFn Visit) {
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      forEachGlobalOperand(&I, Visit);
}

template <typename GlobalT>
void poisonAndErase(GlobalT &GV) {
  GV.replaceAllUsesWith(PoisonValue::get(GV.getType()));
  GV.eraseFromParent();
}

/// Merge all embedded std bitcode modules into a single Module.
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

  // 1. Quick scan: does user code reference any neverc_* declarations?
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

  // 2. Merge all embedded bitcode into one module.
  auto StdMod = mergeEmbeddedModules(M);
  if (!StdMod)
    return PreservedAnalyses::all();

  StringSet<> StdFnNames, StdGlobalNames;
  captureDefinitionNames(*StdMod, StdFnNames, StdGlobalNames);

  // 3. Call-graph prune: keep only functions reachable from user references.
  SmallPtrSet<Function *, 32> Needed;
  SmallPtrSet<GlobalVariable *, 16> NeededGlobals;
  SmallVector<Function *, 32> Worklist;

  auto EnqueueIfNew = [&](Function *F) {
    if (F && !F->isDeclaration() && Needed.insert(F).second)
      Worklist.push_back(F);
  };

  for (Function &Decl : M) {
    if (!Decl.isDeclaration() || Decl.use_empty())
      continue;
    EnqueueIfNew(StdMod->getFunction(Decl.getName()));
  }

  while (!Worklist.empty()) {
    Function *F = Worklist.pop_back_val();
    forEachGlobalReferencedBy(*F, [&](GlobalValue *GV) {
      if (auto *Callee = dyn_cast<Function>(GV)) {
        if (Callee->getParent() == StdMod.get())
          EnqueueIfNew(Callee);
      } else if (auto *GVar = dyn_cast<GlobalVariable>(GV)) {
        if (GVar->getParent() == StdMod.get() && !GVar->isDeclaration())
          NeededGlobals.insert(GVar);
      }
    });
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

  // 4. Merge into user module.
  linkModuleOrFail(M, std::move(StdMod), "neverc std runtime");

  // 5. Internalize std symbols for LTO inlining.
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

  // 6. Mark-and-sweep DCE for internalized std symbols.
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
        forEachGlobalOperand(GVar->getInitializer(), Enqueue);
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
