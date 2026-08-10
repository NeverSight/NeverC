#ifndef NEVERC_PLUGIN_HOST_OBJECTPLUGINBRIDGE_H
#define NEVERC_PLUGIN_HOST_OBJECTPLUGINBRIDGE_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace neverc::plugin {

class PluginPhaseExecutor;

class ObjectPluginBridge {
public:
  ObjectPluginBridge(PluginTaskContext &Task, PluginObjectGraph &Graph,
                     bool AllowMutation = true);
  ObjectPluginBridge(PluginTaskContext &Task, PluginObjectGraph &Graph,
                     const PluginPhaseExecutor &Executor, uint64_t Token);
  ObjectPluginBridge(PluginTaskContext &Task, PluginObjectGraph &Graph,
                     const void *MutationDomain, uint64_t Token);
  ~ObjectPluginBridge();

  ObjectPluginBridge(const ObjectPluginBridge &) = delete;
  ObjectPluginBridge &operator=(const ObjectPluginBridge &) = delete;

  const NevercObjectAPI &api() const { return Facade->API; }
  const NevercObjectAPI &readOnlyAPI() const { return ReadOnlyFacade->API; }
  const NevercObjectAPI &capabilityAPI(const void *MutationDomain,
                                       uint64_t Token);
  NevercTaskHandle taskHandle() const { return Task.handle(); }
  PluginObjectGraph &graphValue() const { return Graph; }
  PluginObjectGraph &activeGraph() const;
  uint64_t bridgeGeneration() const { return BridgeGeneration; }
  bool mutationAllowed() const;
  bool hasActiveMutation() const {
    return !neverc_handle_is_null(MutationHandle);
  }

  class OwnerLease;
  static OwnerLease acquire(void *Context, NevercTaskHandle Task,
                            bool RequireMutation, NevercStatus &Status);

  llvm::Expected<NevercObjectGraphHandle> graph();
  llvm::Expected<NevercObjectLayoutProofHandle> layoutProof();
  NevercStatus resolveGraph(NevercObjectGraphHandle Handle,
                            PluginObjectGraph **OutGraph) const;
  NevercStatus resolveLayoutProof(
      NevercObjectLayoutProofHandle Handle,
      const PluginObjectLayoutProof **OutProof) const;

  llvm::Expected<NevercObjectSectionHandle>
  wrapSection(PluginObjectSection &Section);
  llvm::Expected<NevercObjectSymbolHandle>
  wrapSymbol(PluginObjectSymbol &Symbol);
  llvm::Expected<NevercObjectRelocationHandle>
  wrapRelocation(PluginObjectRelocation &Relocation);
  llvm::Expected<NevercObjectComdatHandle>
  wrapComdat(PluginObjectComdat &Comdat);
  NevercStatus resolveSection(NevercObjectSectionHandle Handle,
                              PluginObjectSection **OutSection) const;
  NevercStatus resolveSymbol(NevercObjectSymbolHandle Handle,
                             PluginObjectSymbol **OutSymbol) const;
  NevercStatus resolveRelocation(
      NevercObjectRelocationHandle Handle,
      PluginObjectRelocation **OutRelocation) const;
  NevercStatus resolveComdat(NevercObjectComdatHandle Handle,
                             PluginObjectComdat **OutComdat) const;

  llvm::Expected<NevercObjectMutationHandle> beginMutation();
  NevercStatus checkMutation(NevercObjectMutationHandle Mutation) const;
  NevercStatus commitMutation(NevercObjectMutationHandle Mutation);
  NevercStatus abandonMutation(NevercObjectMutationHandle Mutation);

  llvm::Expected<NevercObjectSectionHandle>
  createSection(NevercObjectMutationHandle Mutation,
                const NevercObjectSectionDescriptor &Descriptor);
  NevercStatus replaceSection(
      NevercObjectMutationHandle Mutation,
      NevercObjectSectionHandle Section,
      const NevercObjectSectionDescriptor &Descriptor);
  NevercStatus moveSectionBefore(
      NevercObjectMutationHandle Mutation,
      NevercObjectSectionHandle Section,
      NevercObjectSectionHandle Position);
  NevercStatus eraseSection(NevercObjectMutationHandle Mutation,
                            NevercObjectSectionHandle Section);

  llvm::Expected<NevercObjectSymbolHandle>
  createSymbol(NevercObjectMutationHandle Mutation,
               const NevercObjectSymbolDescriptor &Descriptor);
  NevercStatus replaceSymbol(
      NevercObjectMutationHandle Mutation, NevercObjectSymbolHandle Symbol,
      const NevercObjectSymbolDescriptor &Descriptor);
  NevercStatus moveSymbolBefore(
      NevercObjectMutationHandle Mutation, NevercObjectSymbolHandle Symbol,
      NevercObjectSymbolHandle Position);
  NevercStatus eraseSymbol(NevercObjectMutationHandle Mutation,
                           NevercObjectSymbolHandle Symbol);

  llvm::Expected<NevercObjectRelocationHandle>
  createRelocation(
      NevercObjectMutationHandle Mutation,
      const NevercObjectRelocationDescriptor &Descriptor);
  NevercStatus replaceRelocation(
      NevercObjectMutationHandle Mutation,
      NevercObjectRelocationHandle Relocation,
      const NevercObjectRelocationDescriptor &Descriptor);
  NevercStatus moveRelocationBefore(
      NevercObjectMutationHandle Mutation,
      NevercObjectRelocationHandle Relocation,
      NevercObjectRelocationHandle Position);
  NevercStatus eraseRelocation(
      NevercObjectMutationHandle Mutation,
      NevercObjectRelocationHandle Relocation);

  llvm::Expected<NevercObjectComdatHandle>
  createComdat(NevercObjectMutationHandle Mutation,
               const NevercObjectComdatDescriptor &Descriptor);
  NevercStatus replaceComdat(
      NevercObjectMutationHandle Mutation, NevercObjectComdatHandle Comdat,
      const NevercObjectComdatDescriptor &Descriptor);
  NevercStatus moveComdatBefore(
      NevercObjectMutationHandle Mutation, NevercObjectComdatHandle Comdat,
      NevercObjectComdatHandle Position);
  NevercStatus eraseComdat(NevercObjectMutationHandle Mutation,
                           NevercObjectComdatHandle Comdat);

public:
  enum class EntityKind : uint8_t {
    Section,
    Symbol,
    Relocation,
    Comdat,
  };

private:
  struct OwnerControl {
    std::recursive_mutex Mutex;
    ObjectPluginBridge *Owner = nullptr;
  };

  struct APIFacade {
    NevercObjectAPI API{};
    PluginTaskContext *Task = nullptr;
    NevercTaskHandle TaskHandle{};
    std::shared_ptr<OwnerControl> Control;
    const void *MutationDomain = nullptr;
    uint64_t Token = 0;
    bool MutationAllowed = false;
  };

public:
  class OwnerLease {
  public:
    OwnerLease() = default;
    OwnerLease(std::shared_ptr<OwnerControl> ControlValue,
               std::unique_lock<std::recursive_mutex> LockValue,
               ObjectPluginBridge *OwnerValue)
        : Control(std::move(ControlValue)), Lock(std::move(LockValue)),
          Owner(OwnerValue) {}
    explicit operator bool() const { return Owner != nullptr; }
    ObjectPluginBridge &operator*() const { return *Owner; }
    ObjectPluginBridge *operator->() const { return Owner; }

  private:
    std::shared_ptr<OwnerControl> Control;
    std::unique_lock<std::recursive_mutex> Lock;
    ObjectPluginBridge *Owner = nullptr;
  };

private:
  struct EntityReference {
    ObjectPluginBridge *Bridge = nullptr;
    EntityKind Kind = EntityKind::Section;
    uint64_t ID = 0;
    uint64_t Generation = 0;
  };

  llvm::Expected<NevercHandle> wrapEntity(EntityKind Kind, uint64_t ID);
  NevercStatus resolveEntity(NevercHandle Handle, EntityKind Kind,
                             uint64_t *OutID) const;
  void finishHandles();
  void finishMutation();
  std::shared_ptr<APIFacade> createFacade(bool AllowMutation,
                                          const void *MutationDomain = nullptr,
                                          uint64_t Token = 0);

  PluginTaskContext &Task;
  PluginObjectGraph &Graph;
  bool MutationAllowed = true;
  const void *MutationDomain = nullptr;
  uint64_t MutationCapabilityToken = 0;
  uint64_t BridgeGeneration = 1;
  std::shared_ptr<OwnerControl> Control;
  std::shared_ptr<APIFacade> Facade;
  std::shared_ptr<APIFacade> ReadOnlyFacade;
  std::vector<std::shared_ptr<APIFacade>> CapabilityFacades;
  NevercObjectGraphHandle GraphHandle{};
  NevercObjectMutationHandle MutationHandle{};
  NevercObjectLayoutProofHandle LayoutProofHandle{};
  std::vector<std::pair<NevercHandle, PluginHandleKind>> EntityHandles;
  std::unique_ptr<PluginObjectGraph> Working;
};

void initializeObjectQueryAPI(NevercObjectAPI &API,
                              ObjectPluginBridge &Bridge);
void initializeObjectMutationAPI(NevercObjectAPI &API,
                                 ObjectPluginBridge &Bridge);
void initializeObjectBuilderAPI(NevercObjectAPI &API,
                                ObjectPluginBridge &Bridge);

} // namespace neverc::plugin

#endif
