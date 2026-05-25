#include "Backend/MimallocRuntimeLinker.h"
#include "Backend/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinMimalloc.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

bool isMallocOverrideSymbol(StringRef Name) {
#define NEVERC_MALLOC_OVERRIDE_EXACT(sym) if (Name == #sym) return true;
#define NEVERC_MALLOC_OVERRIDE_PREFIX(pfx) if (Name.starts_with(#pfx)) return true;
#include "neverc/Foundation/Builtin/MallocOverrideSymbols.def"
  return false;
}

} // namespace

PreservedAnalyses
MimallocRuntimeLinkerPass::run(Module &M, ModuleAnalysisManager &) {
  Triple TT(M.getTargetTriple());
  StringRef Embedded = BuiltinMimalloc::getEmbeddedBitcode(TT.getOS());
  if (Embedded.empty())
    return PreservedAnalyses::all();

  auto MimallocMod =
      parseBitcodeAndPrepare(Embedded, M, "neverc mimalloc runtime");

  StringSet<> MimallocFnNames, MimallocGlobalNames;
  captureDefinitionNames(*MimallocMod, MimallocFnNames, MimallocGlobalNames);

  linkModuleOrFail(M, std::move(MimallocMod), "neverc mimalloc runtime");

  for (Function &F : M) {
    if (!MimallocFnNames.count(F.getName()))
      continue;
    if (!isMallocOverrideSymbol(F.getName()))
      F.setLinkage(GlobalValue::InternalLinkage);
  }

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isDeclaration() && MimallocGlobalNames.count(GV.getName()))
      GV.setLinkage(GlobalValue::InternalLinkage);
  }

  removeFromUsedLists(M, [&](Constant *C) {
    auto *GV = dyn_cast<GlobalValue>(C->stripPointerCasts());
    if (!GV)
      return isa<PoisonValue>(C) || isa<UndefValue>(C);
    if (auto *F = dyn_cast<Function>(GV))
      return MimallocFnNames.count(F->getName()) != 0 &&
             !isMallocOverrideSymbol(F->getName());
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return MimallocGlobalNames.count(GVar->getName()) != 0;
    return false;
  });

  return PreservedAnalyses::none();
}
