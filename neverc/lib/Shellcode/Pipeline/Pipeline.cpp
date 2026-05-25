#include "neverc/Shellcode/Pipeline/Pipeline.h"
#include "neverc/Shellcode/IR/AllBlrPass.h"
#include "neverc/Shellcode/IR/CompilerRtPass.h"
#include "neverc/Shellcode/IR/Data2TextPass.h"
#include "neverc/Shellcode/IR/HeapArenaPass.h"
#include "neverc/Shellcode/IR/IndirectBrPass.h"
#include "neverc/Shellcode/IR/MemIntrinPass.h"
#include "neverc/Shellcode/IR/StringRuntimePass.h"
#include "neverc/Shellcode/IR/ZeroRelocPass.h"
#include "neverc/Shellcode/Import/KernelImportPass.h"
#include "neverc/Shellcode/Import/SyscallStub.h"
#include "neverc/Shellcode/Import/WinPEBImport.h"
#include "neverc/Shellcode/MIR/MIRPrepPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {
ObfuscationHooks &hookStorage() {
  static ObfuscationHooks S;
  return S;
}

ShellcodeOptions &currentShellcodeOptionsStorage() {
  static ShellcodeOptions S;
  return S;
}

bool &machinePassCallbackInstalled() {
  static bool Installed = false;
  return Installed;
}

void runIRHook(const ObfuscationHook &Hook, ModulePassManager &MPM,
               const ShellcodeOptions &Opts) {
  if (Hook)
    Hook(MPM, Opts);
}

void runMIRHook(const MachineObfuscationHook &Hook, TargetPassConfig &TPC,
                const ShellcodeOptions &Opts) {
  if (Hook)
    Hook(TPC, Opts);
}
} // namespace

void setShellcodeObfuscationHooks(ObfuscationHooks H) {
  hookStorage() = std::move(H);
}

const ObfuscationHooks &getShellcodeObfuscationHooks() { return hookStorage(); }

const ShellcodeOptions &getCurrentShellcodeOptions() {
  return currentShellcodeOptionsStorage();
}

void applyPostExtractObfuscationHook(llvm::SmallVectorImpl<uint8_t> &Bytes) {
  const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
  if (!Opts.Enabled)
    return;
  const ObfuscationHooks &H = hookStorage();
  if (H.RunPostExtract)
    H.RunPostExtract(Bytes, Opts);
}

void applyPostFinalizeObfuscationHook(llvm::SmallVectorImpl<uint8_t> &Bytes) {
  const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
  if (!Opts.Enabled)
    return;
  const ObfuscationHooks &H = hookStorage();
  if (H.RunPostFinalize)
    H.RunPostFinalize(Bytes, Opts);
}

void registerShellcodePasses(PassBuilder &PB, const ShellcodeOptions &Opts) {
  currentShellcodeOptionsStorage() = Opts;

  if (!Opts.Enabled)
    return;

  PB.registerPipelineStartEPCallback([](ModulePassManager &MPM,
                                        OptimizationLevel) {
    const ShellcodeOptions &Opts = getCurrentShellcodeOptions();
    if (!Opts.Enabled)
      return;
    const ObfuscationHooks &H = getShellcodeObfuscationHooks();

    runIRHook(H.RunBeforePrep, MPM, Opts);
    MPM.addPass(ZeroRelocPass(Opts.EntrySymbol));
    runIRHook(H.RunAfterPrep, MPM, Opts);
    MPM.addPass(IndirectBrPass());
    MPM.addPass(MemIntrinPass());
    MPM.addPass(
        StringRuntimePass(StringRuntimePass::arenaSizeFor(Opts.Target.Level)));
    if (Opts.HeapArena) {
      HeapFallbackMode FB = HeapFallbackMode::None;
      if (Opts.WindowsPEBImport)
        FB = HeapFallbackMode::ExternalMalloc;
      else if (Opts.SyscallInlining)
        FB = HeapFallbackMode::Mmap;
      MPM.addPass(HeapArenaPass(
          StringRuntimePass::arenaSizeFor(Opts.Target.Level), FB,
          Opts.Target.OS));
      MPM.addPass(MemIntrinPass());
    }
    MPM.addPass(CompilerRtPass());

    if (Opts.SyscallInlining)
      MPM.addPass(SyscallStubPass(Opts.Target));
    if (Opts.WindowsPEBImport)
      MPM.addPass(WinPEBImportPass(Opts.Target));
    MPM.addPass(KernelImportPass(Opts));

    MPM.addPass(Data2TextPass());
    runIRHook(H.RunBeforeInlining, MPM, Opts);
  });

  PB.registerOptimizerLastEPCallback(
      [](ModulePassManager &MPM, OptimizationLevel) {
        const ShellcodeOptions &Opts = getCurrentShellcodeOptions();
        if (!Opts.Enabled)
          return;
        const ObfuscationHooks &H = getShellcodeObfuscationHooks();

        runIRHook(H.RunAfterInlining, MPM, Opts);
        MPM.addPass(StringRuntimeInlineFinalizePass());
        MPM.addPass(AlwaysInlinerPass());
        MPM.addPass(Data2TextPass());
        MPM.addPass(ZeroRelocPass(Opts.EntrySymbol));
        runIRHook(H.RunAfterStackify, MPM, Opts);

        if (Opts.AllBlr)
          MPM.addPass(AllBlrPass());

        runIRHook(H.RunAfterFinalIR, MPM, Opts);
      });
}

void registerShellcodeMachinePasses(const ShellcodeOptions &Opts) {
  currentShellcodeOptionsStorage() = Opts;

  if (!Opts.Enabled)
    return;

  if (machinePassCallbackInstalled())
    return;
  machinePassCallbackInstalled() = true;

  ListRegisterTargetPassConfigCallbacks.push_back([](TargetPassConfig &TPC) {
    const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
    if (!Opts.Enabled)
      return;
    const ObfuscationHooks &H = getShellcodeObfuscationHooks();

    runMIRHook(H.RunBeforePreEmit, TPC, Opts);
    TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
    runMIRHook(H.RunAfterPreEmit, TPC, Opts);
  });

  ListRegisterTargetPassConfigPostPreEmitCallbacks.push_back(
      [](TargetPassConfig &TPC) {
        const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
        if (!Opts.Enabled)
          return;
        const ObfuscationHooks &H = getShellcodeObfuscationHooks();

        runMIRHook(H.RunAfterFinalMIR, TPC, Opts);
      });
}

} // namespace shellcode
} // namespace neverc
