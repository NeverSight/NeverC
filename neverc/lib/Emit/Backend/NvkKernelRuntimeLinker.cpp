#include "Backend/NvkKernelRuntimeLinker.h"
#include "Backend/RuntimeLinkerUtils.h"
#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace neverc;

namespace {

std::unique_ptr<Module> mergeEmbeddedModules(Module &UserMod) {
  unsigned Count = BuiltinNvkKernel::getEmbeddedModuleCount();
  if (Count == 0)
    return nullptr;

  std::unique_ptr<Module> Combined;

  for (unsigned I = 0; I < Count; ++I) {
    auto [Name, Data] = BuiltinNvkKernel::getEmbeddedModule(I);
    if (Data.empty())
      continue;

    std::string Label = ("nvk kernel: " + Name).str();
    auto Mod = parseBitcodeAndPrepare(Data, UserMod, Label);

    if (!Combined) {
      Combined = std::move(Mod);
    } else {
      if (Linker::linkModules(*Combined, std::move(Mod),
                              Linker::Flags::OverrideFromSrc))
        report_fatal_error(Twine("Failed to merge nvk kernel module: ") +
                           Name);
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

  // Quick scan: does user code include any NVK runtime headers?
  // Check for well-known runtime globals that are always declared when
  // headers are included (NVK_RT_VAR → extern declarations).
  // _nvk_sym_resolver / _nvk_sym_cache come from <linux/kallsyms.h>,
  // _nvk_inited from <nvk_hook.h>, _nvk_log_level from <nvk_log.h>.
  // Together they cover all realistic include combinations.
  bool AnyNvkUsed = M.getGlobalVariable("_nvk_sym_resolver") != nullptr ||
                    M.getGlobalVariable("_nvk_inited") != nullptr ||
                    M.getGlobalVariable("_nvk_sym_cache") != nullptr ||
                    M.getGlobalVariable("_nvk_log_level") != nullptr;
  if (!AnyNvkUsed)
    return PreservedAnalyses::all();

  auto NvkMod = mergeEmbeddedModules(M);
  if (!NvkMod)
    return PreservedAnalyses::all();

  StringSet<> NvkFnNames, NvkGlobalNames;
  captureDefinitionNames(*NvkMod, NvkFnNames, NvkGlobalNames);

  linkModuleOrFail(M, std::move(NvkMod), "nvk kernel runtime");

  // Internalize merged symbols for LTO inlining.
  for (Function &F : M)
    if (!F.isDeclaration() && NvkFnNames.count(F.getName()))
      F.setLinkage(GlobalValue::InternalLinkage);
  for (GlobalVariable &GV : M.globals())
    if (!GV.isDeclaration() && NvkGlobalNames.count(GV.getName()))
      GV.setLinkage(GlobalValue::InternalLinkage);

  removeFromUsedLists(M, [&](Constant *C) {
    auto *GV = dyn_cast<GlobalValue>(C->stripPointerCasts());
    if (!GV)
      return isa<PoisonValue>(C) || isa<UndefValue>(C);
    if (auto *F = dyn_cast<Function>(GV))
      return NvkFnNames.count(F->getName()) != 0;
    if (auto *GVar = dyn_cast<GlobalVariable>(GV))
      return NvkGlobalNames.count(GVar->getName()) != 0;
    return false;
  });

  return PreservedAnalyses::none();
}
