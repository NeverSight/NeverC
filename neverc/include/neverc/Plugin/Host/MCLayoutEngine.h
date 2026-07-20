#ifndef NEVERC_PLUGIN_HOST_MCLAYOUTENGINE_H
#define NEVERC_PLUGIN_HOST_MCLAYOUTENGINE_H

#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/PluginMC.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class MCAsmBackend;
class MCSubtargetInfo;
}

namespace neverc::plugin {

class PluginMCUnit;
class PluginModule;
class PluginTaskContext;

struct MCLayoutBackendRegistrationView {
  llvm::StringRef PluginID;
  std::shared_ptr<const PluginModule> Owner;
  llvm::ArrayRef<NevercMCAsmBackendDescriptor> Backends;
};

class MCLayoutBackendRegistry {
public:
  struct BackendRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    NevercInterfaceID ProviderID{};
    NevercTargetID TargetID{};
    NevercInterfaceID SchemaID{};
    uint32_t MaximumLayoutIterations = 0;
    uint32_t MinimumInstructionAlignment = 1;
    NevercMCGetFixupKindInfoFn GetFixupKindInfo = nullptr;
    NevercMCMapRelocationFn MapRelocation = nullptr;
    NevercMCShouldRelaxFixupFn ShouldRelaxFixup = nullptr;
    NevercMCRelaxFragmentFn RelaxFragment = nullptr;
    NevercMCApplyFixupFn ApplyFixup = nullptr;
    NevercMCWriteNopsFn WriteNops = nullptr;
    void *UserData = nullptr;
  };

  static llvm::Expected<std::shared_ptr<const MCLayoutBackendRegistry>>
  freeze(llvm::ArrayRef<MCLayoutBackendRegistrationView> Registrations,
         const PluginTargetSnapshot &Targets);
  static llvm::Expected<std::shared_ptr<const MCLayoutBackendRegistry>>
  freeze(llvm::ArrayRef<std::shared_ptr<const PluginModule>> Modules,
         const PluginTargetSnapshot &Targets);

  const BackendRecord *findBackend(NevercTargetID Target,
                                   NevercInterfaceID Schema) const;
  size_t backendCount() const { return Backends.size(); }

private:
  std::vector<BackendRecord> Backends;
};

struct MCLayoutOptions {
  uint32_t MaximumIterations = 0;
};

struct MCLayoutSection {
  std::string Name;
  uint64_t Alignment = 1;
  std::vector<uint8_t> Bytes;
};

struct MCLayoutRelocation {
  std::string SectionName;
  uint64_t Offset = 0;
  uint32_t Width = 0;
  uint32_t RelocationKind = 0;
  std::string SymbolName;
  int64_t Addend = 0;
  bool IsPCRelative = false;
  bool IsSigned = false;
};

struct MCLayoutResult {
  uint32_t Iterations = 0;
  uint64_t TotalSize = 0;
  std::string Digest;
  std::vector<MCLayoutSection> Sections;
  std::vector<MCLayoutRelocation> Relocations;
};

class MCLayoutEngine {
public:
  static llvm::Expected<std::unique_ptr<MCLayoutEngine>>
  create(std::shared_ptr<const MCLayoutBackendRegistry> Registry,
         std::shared_ptr<const PluginTargetSnapshot> Targets,
         NevercTargetID Target);
  ~MCLayoutEngine();

  llvm::Expected<MCLayoutResult>
  layout(PluginTaskContext &Task, PluginMCUnit &Unit,
         const MCLayoutOptions &Options) const;

private:
  struct Impl;
  explicit MCLayoutEngine(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

class BuiltinMCAsmBackendAdapter {
public:
  static llvm::Expected<NevercMCFixupKindInfo>
  fixupKindInfo(const llvm::MCAsmBackend &Backend,
                llvm::MCFixupKind Kind);
  static llvm::Expected<std::vector<uint8_t>>
  writeNops(const llvm::MCAsmBackend &Backend, uint64_t Count,
            const llvm::MCSubtargetInfo *Subtarget);
};

} // namespace neverc::plugin

#endif
