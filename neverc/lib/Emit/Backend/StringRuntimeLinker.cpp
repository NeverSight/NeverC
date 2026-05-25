#include "Backend/StringRuntimeLinker.h"
#include "Backend/RuntimeLinkerUtils.h"
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

template <typename GlobalT>
void poisonAndErase(GlobalT &GV) {
  GV.replaceAllUsesWith(PoisonValue::get(GV.getType()));
  GV.eraseFromParent();
}

template <typename VisitFn>
void forEachGlobalReferencedBy(Function &F, VisitFn Visit) {
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      forEachGlobalOperand(&I, Visit);
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

  // Pre-merge call-graph prune: only link functions reachable from user code.
  SmallPtrSet<Function *, 16> Needed;
  SmallPtrSet<GlobalVariable *, 8> NeededGlobals;
  SmallVector<Function *, 32> Worklist;

  auto EnqueueIfNew = [&](Function *F) {
    if (F && !F->isDeclaration() && Needed.insert(F).second)
      Worklist.push_back(F);
  };

  for (Function &Decl : M) {
    if (!Decl.isDeclaration() || Decl.use_empty())
      continue;
    EnqueueIfNew(RuntimeMod->getFunction(Decl.getName()));
  }

  while (!Worklist.empty()) {
    Function *F = Worklist.pop_back_val();
    forEachGlobalReferencedBy(*F, [&](GlobalValue *GV) {
      if (auto *Callee = dyn_cast<Function>(GV)) {
        if (Callee->getParent() == RuntimeMod.get())
          EnqueueIfNew(Callee);
      } else if (auto *GVar = dyn_cast<GlobalVariable>(GV)) {
        if (GVar->getParent() == RuntimeMod.get() && !GVar->isDeclaration())
          NeededGlobals.insert(GVar);
      }
    });
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

  // Post-merge: internalize and DCE.
  auto IsRuntimeFn = [](const Function &F) {
    return F.hasFnAttribute(kRuntimeFnAttr);
  };
  auto IsRuntimeGlobal = [&](const GlobalVariable &GV) {
    return RuntimeGlobalNames.count(GV.getName()) != 0;
  };

  for (Function &F : M)
    if (IsRuntimeFn(F))
      F.setLinkage(GlobalValue::InternalLinkage);
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && IsRuntimeGlobal(GV))
      GV.setLinkage(GlobalValue::InternalLinkage);

  // Mark-and-sweep DCE for runtime symbols.
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
        forEachGlobalOperand(GVar->getInitializer(), Enqueue);
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
