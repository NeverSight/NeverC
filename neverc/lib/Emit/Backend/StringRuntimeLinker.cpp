#include "Backend/StringRuntimeLinker.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

constexpr StringLiteral kRuntimeFnAttr = BuiltinStringNames::RuntimeFnAttr;

/// Stamp `kRuntimeFnAttr` on every definition in `Mod` and strip
/// host-specific `target-cpu` / `target-features` attributes.
///
/// The precompiled bitcode is built on the host (e.g. arm64-apple-macos)
/// and bakes the host's CPU name and feature set into per-function
/// attributes.  When the user cross-compiles to a different arch
/// (e.g. x86_64-apple-macos), the `Linker::linkModules` merge
/// preserves those per-function attributes even though we reset the
/// module-level triple via `setTargetTriple`.  The x86_64 backend
/// then sees arm64 features it cannot handle, leading to
/// `64-bit code requested on a subtarget that doesn't support it!`.
///
/// Stripping these attributes before the merge lets the merged
/// functions inherit the user module's target defaults, which is
/// correct: the runtime source is target-agnostic C with no
/// architecture-specific intrinsics.
void markAllAsRuntime(Module &Mod) {
  for (Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    F.addFnAttr(kRuntimeFnAttr);
    F.removeFnAttr("target-cpu");
    F.removeFnAttr("target-features");
    F.removeFnAttr("tune-cpu");
  }
}

/// Visit every Function / GlobalVariable referenced by `U`'s operands
/// (recursing through ConstantExpr).  The caller's `Visit` receives a
/// `GlobalValue *` and decides whether to enqueue it.
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

/// Replace every use of `GV` with poison and erase it from its parent
/// module.  Shared by the pre-merge prune (on `RuntimeMod`) and the
/// post-merge mark-and-sweep (on `M`) -- both phases rip dead runtime
/// globals out the same way, so funnel the two-step idiom through one
/// place to keep them in lockstep if the LLVM API ever changes.
template <typename GlobalT>
void poisonAndErase(GlobalT &GV) {
  GV.replaceAllUsesWith(PoisonValue::get(GV.getType()));
  GV.eraseFromParent();
}

/// Walk every instruction in every basic block of `F`, calling
/// `Visit(GV)` for each global operand referenced by the body.  Shared
/// by both the pre-merge call-graph BFS and the post-merge mark phase.
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

  // Codegen stamps kRuntimeFnAttr on every runtime function.  We use
  // two signals to decide what to do:
  //   AnyExternUsed → thin-header path, bitcode merge needed.
  //   AnyDefined only → full source-prelude, all bodies present.
  //   neither → TU doesn't use `string`.
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

  auto Buf = MemoryBuffer::getMemBuffer(
      Embedded, "neverc_string_runtime", /*RequiresNullTerminator=*/false);

  auto ExpectedMod = parseBitcodeFile(Buf->getMemBufferRef(), M.getContext());
  if (!ExpectedMod)
    report_fatal_error(Twine("Failed to parse neverc string runtime: ") +
                       toString(ExpectedMod.takeError()));
  auto RuntimeMod = std::move(*ExpectedMod);

  // Stamp `kRuntimeFnAttr` on every function definition in RuntimeMod
  // BEFORE the merge.  The bitcode was compiled from prelude-only
  // source, so every definition IS a runtime function by construction
  // -- no name matching needed.  The attribute survives the merge,
  // and all downstream phases use `hasFnAttribute()` to identify
  // runtime functions, eliminating string-prefix matching entirely.
  markAllAsRuntime(*RuntimeMod);

  // Side-set of runtime global names captured from RuntimeMod before
  // the merge.  GlobalVariable has no function-attribute equivalent;
  // capturing names while RuntimeMod is still isolated is the simplest
  // way to identify "this global came from the runtime" post-merge.
  DenseSet<StringRef> RuntimeGlobalNames;
  for (const GlobalVariable &GV : RuntimeMod->globals())
    if (!GV.isDeclaration())
      RuntimeGlobalNames.insert(GV.getName());

  // Compute the transitive set of RuntimeMod functions needed by user
  // code.  Seed from user-referenced declarations whose names map to
  // a real RuntimeMod definition (no prefix matching: RuntimeMod's
  // function table is the authoritative roster).
  DenseSet<Function *> Needed;
  DenseSet<GlobalVariable *> NeededGlobals;
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

  // Walk the call graph in RuntimeMod.  Every callee that is defined
  // in RuntimeMod IS a runtime helper -- no name gating needed.
  // Global operands are tracked separately so unused runtime globals
  // can be pruned alongside unused functions.
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

  // Drop unreferenced runtime functions and globals from RuntimeMod
  // BEFORE the merge -- avoids merging ~170 functions only to DCE
  // them afterward.
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

  RuntimeMod->setDataLayout(M.getDataLayout());
  RuntimeMod->setTargetTriple(M.getTargetTriple());

  if (auto *Flags = RuntimeMod->getModuleFlagsMetadata())
    Flags->clearOperands();

  if (Linker::linkModules(M, std::move(RuntimeMod),
                          Linker::Flags::OverrideFromSrc)) {
    report_fatal_error("Failed to link neverc string runtime");
  }

  // ── Post-merge: internalize ──
  //
  // The attribute we stamped pre-merge survived the link, so every
  // identification check below is a single bit test on the function.
  // Globals fall back to the side-set captured before the merge.
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

  // Mark-and-sweep DCE.  Runtime functions form an interconnected
  // call graph; simple use_empty() can't reclaim cycles.  Walk from
  // every non-runtime root to find reachable runtime symbols, then
  // erase everything else.  Defensive: pre-merge prune already
  // removes unreachable runtime functions, but cycles introduced by
  // optimisations between the merge and downstream passes (none
  // today, but plenty of hooks coming) can leave dead runtime code
  // attached.
  DenseSet<GlobalValue *> Live;
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

  // Sweep unreachable runtime functions / globals.
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

  // Clean llvm.used / llvm.compiler.used AFTER the sweep so no erased
  // runtime symbol leaves behind a poison entry in the intrinsic
  // array.  Runtime symbols (now internalized) belong to the pipeline
  // -- the linker pass owns their lifetime, not the user's
  // __attribute__((used)) decision.
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
