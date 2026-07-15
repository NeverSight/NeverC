#include "Backend/Runtime/StringRuntimeLinker.h"
#include "Backend/Runtime/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

constexpr StringLiteral kRuntimeFnAttr = BuiltinStringNames::RuntimeFnAttr;

void markAllAsRuntime(Module &Mod) {
  for (Function &F : Mod)
    if (!F.isDeclaration())
      F.addFnAttr(kRuntimeFnAttr);
  stripHostTargetAttributes(Mod);
}

} // namespace

PreservedAnalyses
StringRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  StringRef Embedded = BuiltinString::getEmbeddedStringBitcode();
  if (Embedded.empty())
    return PreservedAnalyses::all();

  bool AnyExternUsed = false;
  bool AnyDefined = false;
  for (const Function &F : M) {
    if (!F.hasFnAttribute(kRuntimeFnAttr))
      continue;
    if (F.isDeclaration()) {
      if (!F.use_empty())
        AnyExternUsed = true;
    } else {
      AnyDefined = true;
    }
  }
  for (const GlobalVariable &GV : M.globals()) {
    if (GV.isDeclaration() && !GV.use_empty() &&
        (GV.getName().starts_with(BuiltinStringNames::PublicFunctionPrefix) ||
         GV.getName().starts_with(BuiltinStringNames::InternalFunctionPrefix)))
      AnyExternUsed = true;
  }
  if (!AnyExternUsed) {
    if (AnyDefined)
      return PreservedAnalyses::none();
    return PreservedAnalyses::all();
  }

  auto RuntimeMod =
      parseBitcodeAndPrepare(Embedded, M, "neverc string runtime");

  markAllAsRuntime(*RuntimeMod);

  StringSet<> RuntimeGlobalNames;
  for (const GlobalVariable &GV : RuntimeMod->globals())
    if (!GV.isDeclaration())
      RuntimeGlobalNames.insert(GV.getName());

  // Call-graph + data-graph prune.
  SmallPtrSet<Function *, 16> Needed;
  SmallPtrSet<GlobalVariable *, 8> NeededGlobals;
  SmallVector<GlobalValue *, 32> Worklist;

  auto EnqueueFn = [&](Function *F) {
    if (F && !F->isDeclaration() && F->getParent() == RuntimeMod.get() &&
        Needed.insert(F).second)
      Worklist.push_back(F);
  };
  auto EnqueueGV = [&](GlobalVariable *GV) {
    if (GV && !GV->isDeclaration() && GV->getParent() == RuntimeMod.get() &&
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
    if (auto *F = RuntimeMod->getFunction(Decl.getName()))
      EnqueueFn(F);
  }
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isDeclaration() || GV.use_empty())
      continue;
    if (auto *Def = RuntimeMod->getGlobalVariable(GV.getName()))
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

  for (Function &F : make_early_inc_range(*RuntimeMod)) {
    if (F.isDeclaration() || Needed.count(&F))
      continue;
    poisonAndErase(F);
  }
  for (GlobalVariable &GV : make_early_inc_range(RuntimeMod->globals())) {
    if (GV.isDeclaration() || NeededGlobals.count(&GV))
      continue;
    poisonAndErase(GV);
  }

  linkModuleOrFail(M, std::move(RuntimeMod), "neverc string runtime");

  auto IsRuntimeFn = [](const Function &F) {
    return F.hasFnAttribute(kRuntimeFnAttr);
  };
  auto IsRuntimeGlobal = [&](const GlobalVariable &GV) {
    return RuntimeGlobalNames.count(GV.getName()) != 0;
  };

  // A no-LTO build embeds runtime code independently in every consumer TU.
  // Keep retained definitions hidden but coalescible so the native link emits
  // one copy instead of one internal copy per TU.
  for (Function &F : M)
    if (IsRuntimeFn(F))
      makeLinkOnceODR(F);
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && IsRuntimeGlobal(GV))
      makeLinkOnceODR(GV);

  if (IsPreLink) {
    removeFromUsedLists(M, [&](Constant *C) {
      if (auto *F = dyn_cast<Function>(C->stripPointerCasts()))
        return IsRuntimeFn(*F);
      if (auto *GVar = dyn_cast<GlobalVariable>(C->stripPointerCasts()))
        return IsRuntimeGlobal(*GVar);
      if (isa<PoisonValue>(C) || isa<UndefValue>(C))
        return true;
      return false;
    });
    return PreservedAnalyses::none();
  }

  // Non-LTO path: mark-and-sweep DCE for runtime symbols.
  SmallPtrSet<GlobalValue *, 16> Live;
  SmallVector<GlobalValue *, 32> ReachWorklist;

  auto Enqueue = [&](GlobalValue *GV) {
    if (GV && Live.insert(GV).second)
      ReachWorklist.push_back(GV);
  };

  auto IsTaggedRuntime = [&](GlobalValue *GV) {
    if (auto *F = dyn_cast<Function>(GV))
      return IsRuntimeFn(*F);
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return IsRuntimeGlobal(*GVar);
    return false;
  };

  for (Function &F : M)
    if (!IsTaggedRuntime(&F))
      Enqueue(&F);
  for (GlobalVariable &GV : M.globals())
    if (!IsTaggedRuntime(&GV))
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
    if (F.isDeclaration() || !IsRuntimeFn(F) || Live.count(&F))
      continue;
    poisonAndErase(F);
  }
  for (GlobalVariable &GV : make_early_inc_range(M.globals())) {
    if (GV.isDeclaration() || !IsRuntimeGlobal(GV) || Live.count(&GV))
      continue;
    poisonAndErase(GV);
  }

  removeFromUsedLists(M, [&](Constant *C) {
    if (auto *F = dyn_cast<Function>(C->stripPointerCasts()))
      return IsRuntimeFn(*F);
    if (auto *GVar = dyn_cast<GlobalVariable>(C->stripPointerCasts()))
      return IsRuntimeGlobal(*GVar);
    if (isa<PoisonValue>(C) || isa<UndefValue>(C))
      return true;
    return false;
  });

  return PreservedAnalyses::none();
}
