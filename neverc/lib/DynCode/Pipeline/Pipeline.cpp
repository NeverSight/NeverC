#include "neverc/DynCode/Pipeline/Pipeline.h"
#include "neverc/Plugin/PluginLoader.h"
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
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include <cstring>

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {
DynCodeOptions &currentDynCodeOptionsStorage() {
  static DynCodeOptions S;
  return S;
}

bool &machinePassCallbackInstalled() {
  static bool Installed = false;
  return Installed;
}
} // namespace

const DynCodeOptions &getCurrentDynCodeOptions() {
  return currentDynCodeOptionsStorage();
}

static void runPluginBinaryInterposes(llvm::SmallVectorImpl<uint8_t> &Bytes,
                                 NevercInterposePoint Interpose) {
  auto &PL = neverc::plugin::getGlobalPluginLoader();
  if (!PL.hasPlugins())
    return;
  auto Passes = PL.getBinaryPasses(Interpose);
  if (Passes.empty())
    return;

  const auto &API = PL.getHostAPI();
  uint64_t Len = Bytes.size();
  constexpr uint64_t kDoubleCap = 64u * 1024;
  uint64_t Cap = Len == 0       ? 256
                 : Len < kDoubleCap ? Len * 2
                                 : Len + Len / 4;
  auto *Data = static_cast<uint8_t *>(API.Alloc(Cap));
  if (!Data) {
    API.DiagWarning(
        "neverc-plugin: failed to allocate buffer for binary pass interposes");
    return;
  }
  if (Len > 0)
    std::memcpy(Data, Bytes.data(), Len);

  for (const auto *P : Passes) {
    if (!P->Fn)
      continue;
    std::string StackMsg =
        "Plugin binary pass '" + P->PassName + "'";
    if (!P->PluginPath.empty())
      StackMsg += " from " + P->PluginPath;
    PrettyStackTraceString CrashInfo(StackMsg.c_str());
    llvm::TimeTraceScope TimeScope("PluginBinaryPass", P->PassName);
    P->Fn(&Data, &Len, &Cap, &API, P->UserData);
    if (!Data) {
      API.DiagError(
          "neverc-plugin: binary pass nullified Data pointer; aborting interposes");
      Bytes.clear();
      return;
    }
    if (Len > Cap) {
      API.DiagError(
          "neverc-plugin: binary pass reported Len > Capacity; clamping");
      Len = Cap;
    }
  }

  Bytes.assign(Data, Data + Len);
  API.Free(Data);
}

void applyPostExtractObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes) {
  const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
  if (!Opts.Enabled)
    return;
  runPluginBinaryInterposes(Bytes, NEVERC_INTERPOSE_SC_POST_EXTRACT);
}

void applyPostFinalizeObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes) {
  const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
  if (!Opts.Enabled)
    return;
  runPluginBinaryInterposes(Bytes, NEVERC_INTERPOSE_SC_POST_FINALIZE);
}

void registerDynCodePasses(PassBuilder &PB, const DynCodeOptions &Opts) {
  currentDynCodeOptionsStorage() = Opts;
  neverc::plugin::setDynCodeModeState(Opts.Enabled, Opts.EntrySymbol);

  PB.registerAnalysisRegistrationCallback([](ModuleAnalysisManager &MAM) {
    MAM.registerPass([] { return CompilerRtStampAnalysis(); });
  });

  if (!Opts.Enabled)
    return;

  PB.registerPipelineStartEPCallback([](ModulePassManager &MPM,
                                        OptimizationLevel) {
    const DynCodeOptions &Opts = getCurrentDynCodeOptions();
    if (!Opts.Enabled)
      return;
    auto &PL = neverc::plugin::getGlobalPluginLoader();

    neverc::plugin::addPluginModulePasses(MPM, NEVERC_INTERPOSE_SC_BEFORE_PREP, PL);
    MPM.addPass(ZeroRelocPass(Opts.EntrySymbol, Opts.InlineAll));
    neverc::plugin::addPluginModulePasses(MPM, NEVERC_INTERPOSE_SC_AFTER_PREP, PL);
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
    neverc::plugin::addPluginModulePasses(
        MPM, NEVERC_INTERPOSE_SC_BEFORE_INLINING, PL);
  });

  PB.registerOptimizerLastEPCallback(
      [](ModulePassManager &MPM, OptimizationLevel) {
        const DynCodeOptions &Opts = getCurrentDynCodeOptions();
        if (!Opts.Enabled)
          return;
        auto &PL = neverc::plugin::getGlobalPluginLoader();

        MPM.addPass(CompilerRtPass(Opts.Target));
        neverc::plugin::addPluginModulePasses(
            MPM, NEVERC_INTERPOSE_SC_AFTER_INLINING, PL);
        MPM.addPass(StringRuntimeInlineFinalizePass());
        MPM.addPass(AlwaysInlinerPass());
        MPM.addPass(Data2TextPass());
        MPM.addPass(ZeroRelocPass(Opts.EntrySymbol, Opts.InlineAll));
        neverc::plugin::addPluginModulePasses(
            MPM, NEVERC_INTERPOSE_SC_AFTER_STACKIFY, PL);

        if (Opts.AllBlr)
          MPM.addPass(AllBlrPass());

        neverc::plugin::addPluginModulePasses(
            MPM, NEVERC_INTERPOSE_SC_AFTER_FINAL_IR, PL);

        MPM.addPass(CompilerRtPass(Opts.Target));
      });
}

void registerDynCodeMachinePasses(const DynCodeOptions &Opts) {
  currentDynCodeOptionsStorage() = Opts;
  neverc::plugin::setDynCodeModeState(Opts.Enabled, Opts.EntrySymbol);

  if (!Opts.Enabled)
    return;

  if (machinePassCallbackInstalled())
    return;
  machinePassCallbackInstalled() = true;

  ListRegisterTargetPassConfigCallbacks.push_back([](TargetPassConfig &TPC) {
    const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
    if (!Opts.Enabled)
      return;
    auto &PL = neverc::plugin::getGlobalPluginLoader();

    neverc::plugin::addPluginMachinePasses(TPC, NEVERC_INTERPOSE_SC_BEFORE_PREEMIT,
                                           PL);
    TPC.addExternalPass(createDynCodeMIRPrepPass(Opts));
    neverc::plugin::addPluginMachinePasses(TPC, NEVERC_INTERPOSE_SC_AFTER_PREEMIT,
                                           PL);
  });

  ListRegisterTargetPassConfigPostPreEmitCallbacks.push_back(
      [](TargetPassConfig &TPC) {
        const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
        if (!Opts.Enabled)
          return;
        auto &PL = neverc::plugin::getGlobalPluginLoader();

        neverc::plugin::addPluginMachinePasses(
            TPC, NEVERC_INTERPOSE_SC_AFTER_FINAL_MIR, PL);
      });
}

} // namespace dyncode
} // namespace neverc
