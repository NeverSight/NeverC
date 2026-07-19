#include "neverc/DynCode/Pipeline/Pipeline.h"
#include "neverc/DynCode/IR/AllBlrPass.h"
#include "neverc/DynCode/IR/CompilerRtPass.h"
#include "neverc/DynCode/IR/Data2TextPass.h"
#include "neverc/DynCode/IR/HeapArenaPass.h"
#include "neverc/DynCode/IR/IndirectBrPass.h"
#include "neverc/DynCode/IR/MemIntrinPass.h"
#include "neverc/DynCode/IR/StringRuntimePass.h"
#include "neverc/DynCode/IR/ZeroRelocPass.h"
#include "neverc/DynCode/Import/KernelImportPass.h"
#include "neverc/DynCode/Import/SyscallStub.h"
#include "neverc/DynCode/Import/WinPEBImport.h"
#include "neverc/DynCode/MIR/MIRPrepPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {
DynCodeOptions &currentDynCodeOptionsStorage() {
  static DynCodeOptions S;
  return S;
}

class DynCodeMachinePipelineHooks final : public MachinePipelineHooks {
public:
  explicit DynCodeMachinePipelineHooks(DynCodeOptions Options)
      : Options(std::move(Options)) {}

  void addPasses(TargetPassConfig &TPC,
                 MachinePipelineHookPoint Point) override {
    if (Options.Enabled && Point == MachinePipelineHookPoint::PreEmit)
      TPC.addExternalPass(createDynCodeMIRPrepPass(Options));
  }

private:
  DynCodeOptions Options;
};

void registerNoLegacyBinaryInterposes(SmallVectorImpl<uint8_t> &) {
  // The unreleased prototype binary callback ABI was removed during the
  // first-version cutover. Typed dyncode transforms are registered by the
  // task-local dyncode pipeline instead of consulting a process-global loader.
}
} // namespace

const DynCodeOptions &getCurrentDynCodeOptions() {
  return currentDynCodeOptionsStorage();
}

void applyPostExtractObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes) {
  const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
  if (!Opts.Enabled)
    return;
  registerNoLegacyBinaryInterposes(Bytes);
}

void applyPostFinalizeObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes) {
  const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
  if (!Opts.Enabled)
    return;
  registerNoLegacyBinaryInterposes(Bytes);
}

void registerDynCodePasses(PassBuilder &PB, const DynCodeOptions &Opts) {
  currentDynCodeOptionsStorage() = Opts;

  PB.registerAnalysisRegistrationCallback([](ModuleAnalysisManager &MAM) {
    MAM.registerPass([] { return CompilerRtStampAnalysis(); });
  });

  if (!Opts.Enabled)
    return;

  PB.registerPipelineStartEPCallback([Opts](ModulePassManager &MPM,
                                            OptimizationLevel) {
    if (!Opts.Enabled)
      return;

    MPM.addPass(ZeroRelocPass(Opts.EntrySymbol, Opts.InlineAll));
    MPM.addPass(IndirectBrPass());
    MPM.addPass(MemIntrinPass());
    bool DynArena = Opts.Target.Level != ExecutionLevel::Kernel;
    MPM.addPass(StringRuntimePass(
        StringRuntimePass::arenaSizeFor(Opts.Target.Level), DynArena,
        Opts.Target.OS));
    if (Opts.HeapArena) {
      HeapFallbackMode FB = HeapFallbackMode::None;
      if (Opts.WindowsPEBImport)
        FB = HeapFallbackMode::ExternalMalloc;
      else if (Opts.SyscallInlining)
        FB = HeapFallbackMode::Mmap;
      MPM.addPass(HeapArenaPass(
          StringRuntimePass::arenaSizeFor(Opts.Target.Level), FB,
          Opts.Target.OS, DynArena));
      MPM.addPass(MemIntrinPass());
    }
    MPM.addPass(CompilerRtPass(Opts.Target));

    if (Opts.SyscallInlining)
      MPM.addPass(SyscallStubPass(Opts.Target));
    if (Opts.WindowsPEBImport)
      MPM.addPass(WinPEBImportPass(Opts.Target));
    MPM.addPass(KernelImportPass(Opts));

    MPM.addPass(Data2TextPass());
  });

  PB.registerOptimizerLastEPCallback(
      [Opts](ModulePassManager &MPM, OptimizationLevel) {
        if (!Opts.Enabled)
          return;

        MPM.addPass(CompilerRtPass(Opts.Target));
        MPM.addPass(StringRuntimeInlineFinalizePass());
        MPM.addPass(AlwaysInlinerPass());
        MPM.addPass(Data2TextPass());
        MPM.addPass(ZeroRelocPass(Opts.EntrySymbol, Opts.InlineAll));

        if (Opts.AllBlr)
          MPM.addPass(AllBlrPass());

        MPM.addPass(CompilerRtPass(Opts.Target));
      });
}

void registerDynCodeMachinePasses(const DynCodeOptions &Opts) {
  currentDynCodeOptionsStorage() = Opts;
}

std::shared_ptr<MachinePipelineHooks>
createDynCodeMachinePipelineHooks(const DynCodeOptions &Opts) {
  if (!Opts.Enabled)
    return {};
  return std::make_shared<DynCodeMachinePipelineHooks>(Opts);
}

} // namespace dyncode
} // namespace neverc
