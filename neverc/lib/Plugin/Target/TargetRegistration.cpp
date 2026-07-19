#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Support/Error.h"
#include <mutex>
#include <unordered_map>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercInterfaceID targetInterfaceID() {
  return {NEVERC_INTERFACE_TARGET_HIGH, NEVERC_INTERFACE_TARGET_LOW};
}

class PluginTargetProcessService final : public PluginHostService {
public:
  explicit PluginTargetProcessService(PluginProcessServices &ServicesValue)
      : Services(ServicesValue) {}

  Error validatePluginRegistrations(
      ArrayRef<std::shared_ptr<const PluginModule>> Modules) override {
    auto Frozen = PluginTargetRegistry::freeze(Modules,
                                               PluginTargetRequest{});
    if (!Frozen)
      return Frozen.takeError();
    return Error::success();
  }

  Error sessionScopeRegistered(NevercSessionHandle Handle,
                               PluginSession &Session) override {
    auto Frozen = PluginTargetRegistry::freeze(Session.plugins(),
                                               PluginTargetRequest{});
    if (!Frozen)
      return Frozen.takeError();
    RegistrySnapshotLease Lease = Services.registry().acquireSnapshot();
    if (!Lease)
      return createStringError(
          inconvertibleErrorCode(),
          "plugin registry refused a Target snapshot lease");
    auto Pinned = std::make_shared<PinnedSnapshot>();
    Pinned->Lease = std::move(Lease);
    Pinned->Snapshot = std::move(*Frozen);
    std::shared_ptr<const PluginTargetSnapshot> Snapshot(
        Pinned, Pinned->Snapshot.get());
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Snapshots
             .emplace(Handle.Owner,
                      SnapshotRecord{Handle.Value, std::move(Snapshot)})
             .second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Target session snapshot");
    return Error::success();
  }

  void sessionScopeUnregistered(NevercSessionHandle Handle) noexcept override {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Snapshots.find(Handle.Owner);
    if (It != Snapshots.end() && It->second.HandleValue == Handle.Value)
      Snapshots.erase(It);
  }

  std::shared_ptr<const PluginTargetSnapshot>
  find(NevercSessionHandle Handle) const {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Snapshots.find(Handle.Owner);
    if (It == Snapshots.end() || It->second.HandleValue != Handle.Value)
      return {};
    return It->second.Snapshot;
  }

private:
  struct PinnedSnapshot {
    RegistrySnapshotLease Lease;
    std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  };

  struct SnapshotRecord {
    uint64_t HandleValue = 0;
    std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  };

  mutable std::mutex Mutex;
  std::unordered_map<uint64_t, SnapshotRecord> Snapshots;
  PluginProcessServices &Services;
};

NevercStatus NEVERC_CALL
registerTarget(void *, void *RegistrarContext,
               const NevercTargetDescriptor *Descriptor) {
  return registerPluginTarget(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerCodeGenEdge(void *, void *RegistrarContext,
                    const NevercCodeGenEdgeDescriptor *Descriptor) {
  return registerPluginCodeGenEdge(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerABI(void *, void *RegistrarContext,
            const NevercTargetABIDescriptor *Descriptor) {
  return registerPluginTargetABI(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerCallingConvention(
    void *, void *RegistrarContext,
    const NevercCallingConventionDescriptor *Descriptor) {
  return registerPluginCallingConvention(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerMCSchema(void *, void *RegistrarContext,
                 const NevercMCSchemaDescriptor *Descriptor) {
  return registerPluginMCSchema(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerObjectFormat(void *, void *RegistrarContext,
                     const NevercObjectFormatDescriptor *Descriptor) {
  return registerPluginObjectFormat(RegistrarContext, Descriptor);
}

const NevercTargetAPI TargetAPI = {
    {sizeof(NevercTargetAPI), NEVERC_TARGET_API_MAJOR,
     NEVERC_TARGET_API_MINOR, 0},
    nullptr,
    registerTarget,
    registerCodeGenEdge,
};

const NevercTargetABIAPI TargetABIAPI = {
    {sizeof(NevercTargetABIAPI), NEVERC_TARGET_ABI_API_MAJOR,
     NEVERC_TARGET_ABI_API_MINOR, 0},
    nullptr,
    registerABI,
};

const NevercCallingConventionAPI CallingConventionAPI = {
    {sizeof(NevercCallingConventionAPI),
     NEVERC_CALLING_CONVENTION_API_MAJOR,
     NEVERC_CALLING_CONVENTION_API_MINOR, 0},
    nullptr,
    registerCallingConvention,
};

const NevercMCAPI MCAPI = {
    {sizeof(NevercMCAPI), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0},
    nullptr,
    registerMCSchema,
};

const NevercObjectAPI ObjectAPI = {
    {sizeof(NevercObjectAPI), NEVERC_OBJECT_API_MAJOR,
     NEVERC_OBJECT_API_MINOR, 0},
    nullptr,
};

const NevercObjectFormatAPI ObjectFormatAPI = {
    {sizeof(NevercObjectFormatAPI), NEVERC_OBJECT_FORMAT_API_MAJOR,
     NEVERC_OBJECT_FORMAT_API_MINOR, 0},
    nullptr,
    registerObjectFormat,
};

Error addInterface(PluginProcessServices &Services, NevercInterfaceID ID,
                   NevercInterfaceStability Stability, const void *Table) {
  return Services.interfaces().registerInterface(ID, Stability, Table, {});
}

} // namespace

Error registerPluginTargetInterfaces(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin Target interfaces after interface freeze");
  auto ProcessService =
      std::make_shared<PluginTargetProcessService>(Services);
  if (Error E =
          Services.registerHostService(targetInterfaceID(), ProcessService))
    return E;
  if (Error E = addInterface(
          Services, targetInterfaceID(),
          NEVERC_TARGET_INTERFACE_STABILITY, &TargetAPI))
    return E;
  if (Error E = addInterface(
          Services,
          {NEVERC_INTERFACE_TARGET_ABI_HIGH,
           NEVERC_INTERFACE_TARGET_ABI_LOW},
          NEVERC_TARGET_ABI_INTERFACE_STABILITY, &TargetABIAPI))
    return E;
  if (Error E = addInterface(
          Services,
          {NEVERC_INTERFACE_CALLING_CONVENTION_HIGH,
           NEVERC_INTERFACE_CALLING_CONVENTION_LOW},
          NEVERC_CALLING_CONVENTION_INTERFACE_STABILITY,
          &CallingConventionAPI))
    return E;
  if (Error E = addInterface(
          Services, {NEVERC_INTERFACE_MC_HIGH, NEVERC_INTERFACE_MC_LOW},
          NEVERC_MC_INTERFACE_STABILITY, &MCAPI))
    return E;
  if (Error E = addInterface(
          Services,
          {NEVERC_INTERFACE_OBJECT_HIGH, NEVERC_INTERFACE_OBJECT_LOW},
          NEVERC_OBJECT_INTERFACE_STABILITY, &ObjectAPI))
    return E;
  return addInterface(
      Services,
      {NEVERC_INTERFACE_OBJECT_FORMAT_HIGH,
       NEVERC_INTERFACE_OBJECT_FORMAT_LOW},
      NEVERC_OBJECT_FORMAT_INTERFACE_STABILITY, &ObjectFormatAPI);
}

std::shared_ptr<const PluginTargetSnapshot>
findPluginTargetSnapshot(PluginProcessServices &Services,
                         NevercSessionHandle Session) {
  auto Service = std::static_pointer_cast<PluginTargetProcessService>(
      Services.findHostService(targetInterfaceID()));
  return Service ? Service->find(Session)
                 : std::shared_ptr<const PluginTargetSnapshot>();
}

} // namespace neverc::plugin
