#ifndef NEVERC_PLUGIN_PLUGINLOADER_H
#define NEVERC_PLUGIN_PLUGINLOADER_H

#include "neverc/Plugin/NevercPluginAPI.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <string>
#include <vector>

namespace llvm {
class TargetPassConfig;
}

namespace neverc {
namespace plugin {

struct RegisteredModulePass {
  NevercHookPoint Hook;
  NevercModulePassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct RegisteredMachinePass {
  NevercHookPoint Hook;
  NevercMachinePassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct RegisteredBinaryPass {
  NevercHookPoint Hook;
  NevercBinaryPassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct RegisteredLinkerPass {
  NevercHookPoint Hook;
  NevercLinkerPassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct LoadedPlugin {
  void *Handle;
  NevercPluginInfo Info;
  std::string Path;
};

/// Manages loading of neverc C-ABI plugins.
/// Collects pass registrations across all loaded plugins.
class PluginLoader {
public:
  ~PluginLoader();

  /// Load a plugin from the given path.
  /// Returns true on success, false on error (message stored in ErrMsg).
  bool loadPlugin(llvm::StringRef Path, std::string &ErrMsg);

  /// Get all module passes registered for a specific hook point.
  llvm::SmallVector<const RegisteredModulePass *, 4>
  getModulePasses(NevercHookPoint Hook) const;

  /// Get all machine passes registered for a specific hook point.
  llvm::SmallVector<const RegisteredMachinePass *, 4>
  getMachinePasses(NevercHookPoint Hook) const;

  /// Get all binary passes registered for a specific hook point.
  llvm::SmallVector<const RegisteredBinaryPass *, 4>
  getBinaryPasses(NevercHookPoint Hook) const;

  /// Get all linker passes registered for a specific hook point.
  llvm::SmallVector<const RegisteredLinkerPass *, 4>
  getLinkerPasses(NevercHookPoint Hook) const;

  bool hasPlugins() const { return !Plugins.empty(); }

  bool hasPassesForHook(NevercHookPoint Hook) const {
    unsigned H = static_cast<unsigned>(Hook);
    auto MI = ModulePasses.find(H);
    if (MI != ModulePasses.end() && !MI->second.empty())
      return true;
    auto MA = MachinePasses.find(H);
    if (MA != MachinePasses.end() && !MA->second.empty())
      return true;
    auto BI = BinaryPasses.find(H);
    if (BI != BinaryPasses.end() && !BI->second.empty())
      return true;
    auto LI = LinkerPasses.find(H);
    return LI != LinkerPasses.end() && !LI->second.empty();
  }

  const NevercHostAPI &getHostAPI() const { return HostAPI; }

private:
  void ensureHostAPIBuilt();

  NevercHostAPI HostAPI{};
  bool HostAPIReady = false;
  std::vector<LoadedPlugin> Plugins;
  llvm::DenseMap<unsigned, llvm::SmallVector<RegisteredModulePass, 4>>
      ModulePasses;
  llvm::DenseMap<unsigned, llvm::SmallVector<RegisteredMachinePass, 4>>
      MachinePasses;
  llvm::DenseMap<unsigned, llvm::SmallVector<RegisteredBinaryPass, 4>>
      BinaryPasses;
  llvm::DenseMap<unsigned, llvm::SmallVector<RegisteredLinkerPass, 4>>
      LinkerPasses;
};

/// Global plugin loader singleton.
PluginLoader &getGlobalPluginLoader();

/// Publish the current shellcode compilation state so that plugin passes can
/// query HostIsShellcodeMode / HostGetShellcodeEntrySymbol via the vtable.
/// Called by the shellcode pipeline when it observes Opts.Enabled change.
/// Plugin → Shellcode is one-way; this proxy avoids a circular dependency.
void setShellcodeModeState(bool Enabled, llvm::StringRef EntrySymbol);

/// Parse and store plugin arguments from -fplugin-pass-arg=key=value.
/// Must be called before plugins are loaded so args are available during
/// RegisterPasses and pass callbacks.
void setPluginArgs(const std::vector<std::string> &RawArgs);

/// Insert all plugin module passes for a given hook point into an MPM.
void addPluginModulePasses(llvm::ModulePassManager &MPM, NevercHookPoint Hook,
                           PluginLoader &Loader);

/// Insert all plugin machine passes for a given hook point via
/// TargetPassConfig.
void addPluginMachinePasses(llvm::TargetPassConfig &TPC, NevercHookPoint Hook,
                            PluginLoader &Loader);

} // namespace plugin
} // namespace neverc

#endif
