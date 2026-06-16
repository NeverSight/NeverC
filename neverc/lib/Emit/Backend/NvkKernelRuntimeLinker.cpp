#include "Backend/NvkKernelRuntimeLinker.h"
#include "Backend/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

/// On-demand module resolution: only parse and merge bitcode modules
/// whose exports are transitively needed by the user module.
///
/// Phase 1 — Lazy-parse every embedded module in a disposable context
///           to extract export name sets (reads headers only, no function
///           bodies).
/// Phase 2 — Iteratively load modules whose exports overlap with the
///           user's unresolved declarations.  Newly loaded modules may
///           introduce new unresolved references, triggering additional
///           module loads until a fixed point is reached.
std::unique_ptr<Module> mergeNeededModules(Module &UserMod) {
  unsigned Count = BuiltinNvkKernel::getEmbeddedModuleCount();
  if (Count == 0)
    return nullptr;

  StringSet<> Unresolved;
  for (Function &F : UserMod)
    if (F.isDeclaration() && !F.use_empty())
      Unresolved.insert(F.getName());
  for (GlobalVariable &GV : UserMod.globals())
    if (GV.isDeclaration() && !GV.use_empty())
      Unresolved.insert(GV.getName());

  struct ModInfo {
    StringRef Name;
    StringRef Data;
    SmallVector<std::string, 0> Exports;
    bool Loaded = false;
  };
  SmallVector<ModInfo, 32> Mods(Count);

  {
    LLVMContext LazyCtx;
    for (unsigned I = 0; I < Count; ++I) {
      auto [N, D] = BuiltinNvkKernel::getEmbeddedModule(I);
      Mods[I].Name = N;
      Mods[I].Data = D;
      if (D.empty())
        continue;

      auto Buf = MemoryBuffer::getMemBuffer(D, N, /*RequiresNullTerminator=*/false);
      auto LazyMod = getLazyBitcodeModule(Buf->getMemBufferRef(), LazyCtx,
                                          /*ShouldLazyLoadMetadata=*/true);
      if (!LazyMod) {
        consumeError(LazyMod.takeError());
        continue;
      }
      for (Function &F : **LazyMod)
        if (!F.isDeclaration())
          Mods[I].Exports.push_back(F.getName().str());
      for (GlobalVariable &GV : (*LazyMod)->globals())
        if (!GV.isDeclaration())
          Mods[I].Exports.push_back(GV.getName().str());
    }
  }

  std::unique_ptr<Module> Combined;
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (unsigned I = 0; I < Count; ++I) {
      if (Mods[I].Loaded || Mods[I].Data.empty())
        continue;

      bool Needed = llvm::any_of(Mods[I].Exports, [&](const std::string &S) {
        return Unresolved.count(S);
      });
      if (!Needed)
        continue;

      std::string Label = ("nvk kernel: " + Mods[I].Name).str();
      auto Mod = parseBitcodeAndPrepare(Mods[I].Data, UserMod, Label);

      for (Function &F : *Mod)
        if (F.isDeclaration() && !F.use_empty())
          Changed |= Unresolved.insert(F.getName()).second;
      for (GlobalVariable &GV : Mod->globals())
        if (GV.isDeclaration() && !GV.use_empty())
          Changed |= Unresolved.insert(GV.getName()).second;

      if (!Combined) {
        Combined = std::move(Mod);
      } else {
        if (Linker::linkModules(*Combined, std::move(Mod),
                                Linker::Flags::OverrideFromSrc))
          report_fatal_error(Twine("Failed to merge nvk kernel module: ") +
                             Mods[I].Name);
      }
      Mods[I].Loaded = true;
      Changed = true;
    }
  }

  return Combined;
}

} // namespace

PreservedAnalyses
NvkKernelRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::aarch64)
    return PreservedAnalyses::all();

  if (BuiltinNvkKernel::getEmbeddedModuleCount() == 0)
    return PreservedAnalyses::all();

  bool AnyNvkUsed = M.getGlobalVariable("_neverc_krt_sym_resolver") != nullptr ||
                    M.getGlobalVariable("_neverc_krt_inited") != nullptr ||
                    M.getGlobalVariable("_neverc_krt_sym_cache") != nullptr ||
                    M.getGlobalVariable("_neverc_krt_log_level") != nullptr;
  if (!AnyNvkUsed)
    return PreservedAnalyses::all();

  auto NvkMod = mergeNeededModules(M);
  if (!NvkMod)
    return PreservedAnalyses::all();

  StringSet<> NvkFnNames, NvkGlobalNames;
  captureDefinitionNames(*NvkMod, NvkFnNames, NvkGlobalNames);

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

  linkModuleOrFail(M, std::move(NvkMod), "nvk kernel runtime");

  auto IsNvkFn = [&](const Function &F) {
    return NvkFnNames.count(F.getName()) != 0;
  };
  auto IsNvkGlobal = [&](const GlobalVariable &GV) {
    return NvkGlobalNames.count(GV.getName()) != 0;
  };

  for (Function &F : M)
    if (IsNvkFn(F))
      F.setLinkage(GlobalValue::InternalLinkage);
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && IsNvkGlobal(GV))
      GV.setLinkage(GlobalValue::InternalLinkage);

  // Mark-and-sweep DCE for internalized symbols.
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

  return PreservedAnalyses::none();
}
