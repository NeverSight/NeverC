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
  NevercInterposePoint Interpose;
  NevercModulePassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct RegisteredMachinePass {
  NevercInterposePoint Interpose;
  NevercMachinePassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct RegisteredBinaryPass {
  NevercInterposePoint Interpose;
  NevercBinaryPassFn Fn;
  void *UserData;
  std::string PassName;
  std::string PluginPath;
};

struct RegisteredLinkerPass {
  NevercInterposePoint Interpose;
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

  /// Get all module passes registered for a specific interpose point.
  llvm::SmallVector<const RegisteredModulePass *, 4>
  getModulePasses(NevercInterposePoint Interpose) const;

  /// Get all machine passes registered for a specific interpose point.
  llvm::SmallVector<const RegisteredMachinePass *, 4>
  getMachinePasses(NevercInterposePoint Interpose) const;

  /// Get all binary passes registered for a specific interpose point.
  llvm::SmallVector<const RegisteredBinaryPass *, 4>
  getBinaryPasses(NevercInterposePoint Interpose) const;

  /// Get all linker passes registered for a specific interpose point.
  llvm::SmallVector<const RegisteredLinkerPass *, 4>
  getLinkerPasses(NevercInterposePoint Interpose) const;

  bool hasPlugins() const { return !Plugins.empty(); }

  bool hasPassesForInterpose(NevercInterposePoint Interpose) const {
    unsigned H = static_cast<unsigned>(Interpose);
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

  /// True if any plugin registered a linker pass (any LINK_* interpose).  Linker
  /// backends gate their accessor-table setup and LINK_* interpose firing on this
  /// so a plugin that only uses IR/MIR interposes adds zero cost to the link.
  bool hasLinkerPasses() const {
    for (const auto &KV : LinkerPasses)
      if (!KV.second.empty())
        return true;
    return false;
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

/// Publish the current dyncode compilation state so that plugin passes can
/// query HostIsDynCodeMode / HostGetDynCodeEntrySymbol via the vtable.
/// Called by the dyncode pipeline when it observes Opts.Enabled change.
/// Plugin -> DynCode is one-way; this proxy avoids a circular dependency.
void setDynCodeModeState(bool Enabled, llvm::StringRef EntrySymbol);

/// Parse and store plugin arguments from -fplugin-pass-arg=key=value.
/// Must be called before plugins are loaded so args are available during
/// RegisterPasses and pass callbacks.
void setPluginArgs(const std::vector<std::string> &RawArgs);

/// Insert all plugin module passes for a given interpose point into an MPM.
void addPluginModulePasses(llvm::ModulePassManager &MPM, NevercInterposePoint Interpose,
                           PluginLoader &Loader);

/// Insert all plugin machine passes for a given interpose point via
/// TargetPassConfig.
void addPluginMachinePasses(llvm::TargetPassConfig &TPC, NevercInterposePoint Interpose,
                            PluginLoader &Loader);

/// Backend-provided accessor table for linker symbol/section state.
///
/// A linker backend (ELF/COFF/MachO) installs one of these via
/// setLinkerBackend() while it runs linker passes, so the host vtable's
/// Link* queries resolve against backend-specific state WITHOUT the plugin
/// library depending on any linker backend.  Opaque refs are backend-defined;
/// the ELF backend encodes a 1-based array index (0 == end-of-list /
/// not-found) for O(1) iteration.  Every member may be null; the bridge falls
/// back to safe defaults (empty string / 0 / nullptr).
struct NevercLinkerBackend {
  NevercLinkerSymbolRef (*GetFirstSymbol)();
  NevercLinkerSymbolRef (*GetNextSymbol)(NevercLinkerSymbolRef S);
  NevercLinkerSymbolRef (*FindSymbol)(const char *Name);
  const char *(*SymbolGetName)(NevercLinkerSymbolRef S);
  uint64_t (*SymbolGetValue)(NevercLinkerSymbolRef S);
  uint64_t (*SymbolGetSize)(NevercLinkerSymbolRef S);
  int (*SymbolIsDefined)(NevercLinkerSymbolRef S);
  int (*SymbolIsLocal)(NevercLinkerSymbolRef S);
  int (*SymbolIsHidden)(NevercLinkerSymbolRef S);
  void (*SymbolSetVisibilityHidden)(NevercLinkerSymbolRef S, int IsHidden);

  NevercLinkerSectionRef (*GetFirstSection)();
  NevercLinkerSectionRef (*GetNextSection)(NevercLinkerSectionRef S);
  NevercLinkerSectionRef (*FindSection)(const char *Name);
  const char *(*SectionGetName)(NevercLinkerSectionRef S);
  uint64_t (*SectionGetSize)(NevercLinkerSectionRef S);
  uint64_t (*SectionGetAlignment)(NevercLinkerSectionRef S);
  unsigned (*SectionGetFlags)(NevercLinkerSectionRef S);

  const char *(*GetOutputPath)();
  unsigned (*GetOutputFormat)();
};

/// Install / clear the active linker backend accessor table.  A backend sets
/// it around its link-pass invocation and clears it afterwards (RAII is
/// recommended so it is cleared on early returns).  Single-threaded: links run
/// sequentially and the writer phase that fires linker interposes is not reentrant.
void setLinkerBackend(const NevercLinkerBackend *Backend);
const NevercLinkerBackend *getLinkerBackend();
void clearLinkerBackend();

/// Run every linker pass registered for a interpose point, in registration order.
/// No-op when no plugin registered a linker pass for Interpose, so the common
/// (no-plugin) link path pays nothing.  The backend must install its accessor
/// table (setLinkerBackend) before calling this so Link* queries resolve.
void runLinkerPasses(NevercInterposePoint Interpose, PluginLoader &Loader);

} // namespace plugin
} // namespace neverc

#endif
