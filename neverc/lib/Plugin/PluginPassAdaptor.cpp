#include "PluginPassAdaptor.h"
#include "neverc/Plugin/PluginLoader.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "neverc-plugin"

using namespace llvm;

namespace neverc {
namespace plugin {

// ===----------------------------------------------------------------------===
//  Module pass adaptor (new PM)
// ===----------------------------------------------------------------------===

PluginModulePassAdaptor::PluginModulePassAdaptor(NevercModulePassFn Fn,
                                                 const NevercHostAPI *API,
                                                 void *UserData,
                                                 StringRef PassName,
                                                 StringRef PluginPath)
    : Callback(Fn), API(API), UserData(UserData), Name(PassName.str()),
      Origin(PluginPath.str()) {
  StackMsg = "Plugin IR pass '" + Name + "'";
  if (!Origin.empty())
    StackMsg += " from " + Origin;
}

PreservedAnalyses PluginModulePassAdaptor::run(Module &M,
                                               ModuleAnalysisManager &) {
  if (!Callback || !API) {
    LLVM_DEBUG(dbgs() << "neverc: plugin pass '" << Name
                      << "' has null callback or API; skipped\n");
    return PreservedAnalyses::all();
  }
  PrettyStackTraceString CrashInfo(StackMsg.c_str());
  llvm::TimeTraceScope TimeScope("PluginPass", Name);

  LLVM_DEBUG(dbgs() << "neverc: running plugin pass '" << Name << "'\n");
  auto *MRef = reinterpret_cast<NevercModuleRef>(&M);
  int Changed = Callback(MRef, API, UserData);
  LLVM_DEBUG(dbgs() << "neverc: plugin pass '" << Name << "' "
                    << (Changed ? "modified" : "did not modify")
                    << " the module\n");

#ifndef NDEBUG
  if (Changed) {
    std::string Err;
    raw_string_ostream OS(Err);
    if (verifyModule(M, &OS)) {
      WithColor::error(errs(), "neverc-plugin")
          << "pass '" << Name << "' produced invalid IR:\n"
          << Err << "\n";
      llvm_unreachable("plugin pass broke module invariants");
    }
  }
#endif

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

void PluginModulePassAdaptor::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)>) {
  OS << Name;
}

void addPluginModulePasses(ModulePassManager &MPM, NevercHookPoint Hook,
                           PluginLoader &Loader) {
  if (!Loader.hasPlugins())
    return;

  for (const auto *P : Loader.getModulePasses(Hook)) {
    MPM.addPass(PluginModulePassAdaptor(P->Fn, &Loader.getHostAPI(),
                                        P->UserData, P->PassName,
                                        P->PluginPath));
  }
}

// ===----------------------------------------------------------------------===
//  MachineFunction pass adaptor (legacy PM, for TargetPassConfig)
// ===----------------------------------------------------------------------===

char PluginMachineFunctionPassAdaptor::ID = 0;

PluginMachineFunctionPassAdaptor::PluginMachineFunctionPassAdaptor(
    NevercMachinePassFn Fn, const NevercHostAPI *API, void *UserData,
    StringRef PassName, StringRef PluginPath)
    : MachineFunctionPass(ID), Callback(Fn), API(API), UserData(UserData),
      Name(PassName.str()), Origin(PluginPath.str()) {
  StackMsg = "Plugin MIR pass '" + Name + "'";
  if (!Origin.empty())
    StackMsg += " from " + Origin;
}

bool PluginMachineFunctionPassAdaptor::runOnMachineFunction(
    MachineFunction &MF) {
  if (!Callback || !API) {
    LLVM_DEBUG(dbgs() << "neverc: plugin MIR pass '" << Name
                      << "' has null callback or API; skipped\n");
    return false;
  }
  PrettyStackTraceString CrashInfo(StackMsg.c_str());
  llvm::TimeTraceScope TimeScope("PluginMIRPass", Name);

  LLVM_DEBUG(dbgs() << "neverc: running plugin MIR pass '" << Name
                    << "' on " << MF.getName() << "\n");
  auto *MFRef = reinterpret_cast<NevercMachineFuncRef>(&MF);
  int Changed = Callback(MFRef, API, UserData);
  return Changed != 0;
}

void addPluginMachinePasses(TargetPassConfig &TPC, NevercHookPoint Hook,
                            PluginLoader &Loader) {
  if (!Loader.hasPlugins())
    return;

  for (const auto *P : Loader.getMachinePasses(Hook)) {
    TPC.addExternalPass(new PluginMachineFunctionPassAdaptor(
        P->Fn, &Loader.getHostAPI(), P->UserData, P->PassName,
        P->PluginPath));
  }
}

} // namespace plugin
} // namespace neverc
