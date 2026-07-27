#include "PluginLinkRegistry.h"
#include "LinkPhaseCAPI.h"
#include "../LTO/LTOInputSet.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include <algorithm>
#include <mutex>
#include <tuple>
#include <unordered_map>

using namespace llvm;

namespace neverc::plugin {

struct PluginLinkSnapshotAccess {
  static auto &linkers(PluginLinkSnapshot &Snapshot) {
    return Snapshot.LinkerProviders;
  }
  static auto &mergers(PluginLinkSnapshot &Snapshot) {
    return Snapshot.ObjectMergeProviders;
  }
  static auto &verifiers(PluginLinkSnapshot &Snapshot) {
    return Snapshot.ImageVerifiers;
  }
  static auto &ltoProviders(PluginLinkSnapshot &Snapshot) {
    return Snapshot.LTOProviders;
  }
};

namespace {

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

std::string copyString(NevercStringView View) {
  return std::string(View.Data ? View.Data : "",
                     static_cast<size_t>(View.Length));
}

Error registryError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool validOutputKind(NevercLinkOutputKind Kind, bool AllowWildcard) {
  return (AllowWildcard && Kind == 0) ||
         (Kind >= NEVERC_LINK_OUTPUT_RELOCATABLE &&
          Kind <= NEVERC_LINK_OUTPUT_BUNDLE);
}

bool validLinkFlags(NevercLinkProviderFlags Flags) {
  constexpr NevercLinkProviderFlags Known =
      NEVERC_LINK_PROVIDER_DETERMINISTIC |
      NEVERC_LINK_PROVIDER_CACHEABLE |
      NEVERC_LINK_PROVIDER_REPLAY_REQUIRED;
  return (Flags & ~Known) == 0;
}

bool validLTOFlags(NevercLTOProviderFlags Flags) {
  constexpr NevercLTOProviderFlags Known =
      NEVERC_LTO_PROVIDER_FULL | NEVERC_LTO_PROVIDER_THIN |
      NEVERC_LTO_PROVIDER_DETERMINISTIC |
      NEVERC_LTO_PROVIDER_CACHEABLE |
      NEVERC_LTO_PROVIDER_REPLAY_REQUIRED;
  return (Flags & ~Known) == 0 &&
         (Flags & (NEVERC_LTO_PROVIDER_FULL |
                   NEVERC_LTO_PROVIDER_THIN)) != 0;
}

template <typename Record>
Error rejectDuplicateProvider(ArrayRef<Record> Records, StringRef ProviderID,
                              StringRef Kind) {
  if (llvm::any_of(Records, [&](const Record &Existing) {
        return Existing.ProviderID == ProviderID;
      }))
    return registryError("duplicate " + Kind + " provider ID '" +
                         ProviderID + "'");
  return Error::success();
}

Error appendLinker(
    PluginLinkSnapshot &Snapshot, StringRef PluginID,
    std::shared_ptr<const PluginModule> Owner,
    const NevercLinkerProviderDescriptor &Descriptor,
    StringRef OwnedProviderID = {}, StringRef OwnedCompatibility = {}) {
  std::string ProviderID =
      OwnedProviderID.empty() ? copyString(Descriptor.ProviderID)
                              : OwnedProviderID.str();
  std::string Compatibility =
      OwnedCompatibility.empty() ? copyString(Descriptor.CompatibilityKey)
                                 : OwnedCompatibility.str();
  if (!isCanonicalPluginID(ProviderID) ||
      !validOutputKind(Descriptor.OutputKind, true) ||
      Descriptor.OutputKind == NEVERC_LINK_OUTPUT_RELOCATABLE ||
      !validLinkFlags(Descriptor.Flags) ||
      !nonzero(Descriptor.ProductID) || !Descriptor.Link ||
      !Descriptor.VerifyImage)
    return registryError("invalid linker provider '" + ProviderID +
                         "' from plugin '" + PluginID + "'");
  if (Error E = rejectDuplicateProvider(
          Snapshot.linkerProviders(), ProviderID, "linker"))
    return E;

  PluginLinkSnapshot::LinkerProviderRecord Record;
  Record.PluginID = PluginID.str();
  Record.Owner = std::move(Owner);
  Record.ProviderID = std::move(ProviderID);
  Record.TargetID = Descriptor.TargetID;
  Record.InputFormat = Descriptor.InputFormat;
  Record.OutputFormat = Descriptor.OutputFormat;
  Record.OutputKind = Descriptor.OutputKind;
  Record.Flags = Descriptor.Flags;
  Record.CompatibilityKey = std::move(Compatibility);
  Record.ProductID = Descriptor.ProductID;
  Record.Link = Descriptor.Link;
  Record.VerifyImage = Descriptor.VerifyImage;
  Record.UserData = Descriptor.UserData;
  PluginLinkSnapshotAccess::linkers(Snapshot).push_back(std::move(Record));
  return Error::success();
}

Error appendMerger(
    PluginLinkSnapshot &Snapshot, StringRef PluginID,
    std::shared_ptr<const PluginModule> Owner,
    const NevercObjectMergeProviderDescriptor &Descriptor,
    StringRef OwnedProviderID = {}, StringRef OwnedCompatibility = {}) {
  std::string ProviderID =
      OwnedProviderID.empty() ? copyString(Descriptor.ProviderID)
                              : OwnedProviderID.str();
  std::string Compatibility =
      OwnedCompatibility.empty() ? copyString(Descriptor.CompatibilityKey)
                                 : OwnedCompatibility.str();
  if (!isCanonicalPluginID(ProviderID) ||
      !validLinkFlags(Descriptor.Flags) ||
      !nonzero(Descriptor.ProductID) || !Descriptor.Merge)
    return registryError("invalid object merge provider '" + ProviderID +
                         "' from plugin '" + PluginID + "'");
  if (Error E = rejectDuplicateProvider(
          Snapshot.objectMergeProviders(), ProviderID, "object merge"))
    return E;

  PluginLinkSnapshot::ObjectMergeProviderRecord Record;
  Record.PluginID = PluginID.str();
  Record.Owner = std::move(Owner);
  Record.ProviderID = std::move(ProviderID);
  Record.TargetID = Descriptor.TargetID;
  Record.FormatID = Descriptor.FormatID;
  Record.Flags = Descriptor.Flags;
  Record.CompatibilityKey = std::move(Compatibility);
  Record.ProductID = Descriptor.ProductID;
  Record.Merge = Descriptor.Merge;
  Record.UserData = Descriptor.UserData;
  PluginLinkSnapshotAccess::mergers(Snapshot).push_back(std::move(Record));
  return Error::success();
}

Error appendVerifier(
    PluginLinkSnapshot &Snapshot, StringRef PluginID,
    std::shared_ptr<const PluginModule> Owner,
    const NevercBinaryImageVerifierDescriptor &Descriptor,
    StringRef OwnedVerifierID = {}) {
  std::string VerifierID =
      OwnedVerifierID.empty() ? copyString(Descriptor.VerifierID)
                              : OwnedVerifierID.str();
  if (!isCanonicalPluginID(VerifierID) ||
      !validOutputKind(Descriptor.OutputKind, true) ||
      !Descriptor.Verify)
    return registryError("invalid binary image verifier '" + VerifierID +
                         "' from plugin '" + PluginID + "'");
  if (llvm::any_of(Snapshot.imageVerifiers(), [&](const auto &Existing) {
        return Existing.VerifierID == VerifierID;
      }))
    return registryError("duplicate binary image verifier ID '" +
                         VerifierID + "'");

  PluginLinkSnapshot::ImageVerifierRecord Record;
  Record.PluginID = PluginID.str();
  Record.Owner = std::move(Owner);
  Record.VerifierID = std::move(VerifierID);
  Record.TargetID = Descriptor.TargetID;
  Record.FormatID = Descriptor.FormatID;
  Record.OutputKind = Descriptor.OutputKind;
  Record.Verify = Descriptor.Verify;
  Record.UserData = Descriptor.UserData;
  PluginLinkSnapshotAccess::verifiers(Snapshot).push_back(std::move(Record));
  return Error::success();
}

Error appendLTO(PluginLinkSnapshot &Snapshot, StringRef PluginID,
                std::shared_ptr<const PluginModule> Owner,
                const NevercLTOProviderDescriptor &Descriptor,
                StringRef OwnedProviderID = {},
                StringRef OwnedCompatibility = {}) {
  std::string ProviderID =
      OwnedProviderID.empty() ? copyString(Descriptor.ProviderID)
                              : OwnedProviderID.str();
  std::string Compatibility =
      OwnedCompatibility.empty() ? copyString(Descriptor.CompatibilityKey)
                                 : OwnedCompatibility.str();
  if (!isCanonicalPluginID(ProviderID) ||
      !validLTOFlags(Descriptor.Flags) ||
      !nonzero(Descriptor.ProductID) || !Descriptor.Codegen ||
      ((Descriptor.Flags & NEVERC_LTO_PROVIDER_CACHEABLE) != 0 &&
       !Descriptor.BuildCacheKey))
    return registryError("invalid LTO provider '" + ProviderID +
                         "' from plugin '" + PluginID + "'");
  if (Error E =
          rejectDuplicateProvider(Snapshot.ltoProviders(), ProviderID, "LTO"))
    return E;

  PluginLinkSnapshot::LTOProviderRecord Record;
  Record.PluginID = PluginID.str();
  Record.Owner = std::move(Owner);
  Record.ProviderID = std::move(ProviderID);
  Record.TargetID = Descriptor.TargetID;
  Record.Flags = Descriptor.Flags;
  Record.CompatibilityKey = std::move(Compatibility);
  Record.ProductID = Descriptor.ProductID;
  Record.BuildCacheKey = Descriptor.BuildCacheKey;
  Record.Codegen = Descriptor.Codegen;
  Record.UserData = Descriptor.UserData;
  PluginLinkSnapshotAccess::ltoProviders(Snapshot)
      .push_back(std::move(Record));
  return Error::success();
}

template <typename Records>
void stableSortRecords(Records &Values) {
  llvm::stable_sort(Values, [](const auto &Left, const auto &Right) {
    return std::tie(Left.PluginID, Left.ProviderID) <
           std::tie(Right.PluginID, Right.ProviderID);
  });
}

class PluginLinkProcessService final : public PluginHostService {
public:
  explicit PluginLinkProcessService(PluginProcessServices &ServicesValue)
      : Services(ServicesValue) {}

  Error validatePluginRegistrations(
      ArrayRef<std::shared_ptr<const PluginModule>> Modules) override {
    auto Frozen = PluginLinkRegistry::freeze(Modules);
    return Frozen ? Error::success() : Frozen.takeError();
  }

  Error sessionScopeRegistered(NevercSessionHandle Handle,
                               PluginSession &Session) override {
    auto Frozen = PluginLinkRegistry::freeze(Session.plugins());
    if (!Frozen)
      return Frozen.takeError();
    RegistrySnapshotLease Lease = Services.registry().acquireSnapshot();
    if (!Lease)
      return registryError(
          "plugin registry refused a Link snapshot lease");
    auto Pinned = std::make_shared<PinnedSnapshot>();
    Pinned->Lease = std::move(Lease);
    Pinned->Snapshot = std::move(*Frozen);
    std::shared_ptr<const PluginLinkSnapshot> Snapshot(
        Pinned, Pinned->Snapshot.get());
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Snapshots
             .emplace(Handle.Owner,
                      SnapshotRecord{Handle.Value, std::move(Snapshot)})
             .second)
      return registryError("duplicate Link session snapshot");
    return Error::success();
  }

  void sessionScopeUnregistered(NevercSessionHandle Handle) noexcept override {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Snapshots.find(Handle.Owner);
    if (It != Snapshots.end() && It->second.HandleValue == Handle.Value)
      Snapshots.erase(It);
  }

  std::shared_ptr<const PluginLinkSnapshot>
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
    std::shared_ptr<const PluginLinkSnapshot> Snapshot;
  };
  struct SnapshotRecord {
    uint64_t HandleValue = 0;
    std::shared_ptr<const PluginLinkSnapshot> Snapshot;
  };

  PluginProcessServices &Services;
  mutable std::mutex Mutex;
  std::unordered_map<uint64_t, SnapshotRecord> Snapshots;
};

NevercStatus NEVERC_CALL
registerLinker(void *, void *RegistrarContext,
               const NevercLinkerProviderDescriptor *Descriptor) {
  return registerPluginLinkerProvider(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerMerger(void *, void *RegistrarContext,
               const NevercObjectMergeProviderDescriptor *Descriptor) {
  return registerPluginObjectMergeProvider(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerVerifier(void *, void *RegistrarContext,
                 const NevercBinaryImageVerifierDescriptor *Descriptor) {
  return registerPluginBinaryImageVerifier(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerLTO(void *, void *RegistrarContext,
            const NevercLTOProviderDescriptor *Descriptor) {
  return registerPluginLTOProvider(RegistrarContext, Descriptor);
}

// The tables these builders produce must be constant-initialized: a runtime
// initializer would place them in writable storage, which the plugin
// no-global-mutable-state gate forbids.
constexpr NevercLinkAPI makeLinkAPI() {
  NevercLinkAPI API{};
  API.Header = {sizeof(API), NEVERC_LINK_API_MAJOR,
                NEVERC_LINK_API_MINOR, 0};
  return API;
}

constexpr NevercLinkRegistrarAPI makeLinkRegistrarAPI() {
  NevercLinkRegistrarAPI API{};
  API.Header = {sizeof(API), NEVERC_LINK_REGISTRAR_API_MAJOR,
                NEVERC_LINK_REGISTRAR_API_MINOR, 0};
  API.RegisterLinkerProvider = registerLinker;
  API.RegisterObjectMergeProvider = registerMerger;
  API.RegisterBinaryImageVerifier = registerVerifier;
  return API;
}

constexpr NevercLTORegistrarAPI makeLTORegistrarAPI() {
  NevercLTORegistrarAPI API{};
  API.Header = {sizeof(API), NEVERC_LTO_REGISTRAR_API_MAJOR,
                NEVERC_LTO_REGISTRAR_API_MINOR, 0};
  API.RegisterProvider = registerLTO;
  return API;
}

constexpr NevercLinkAPI LinkAPI = makeLinkAPI();
constexpr NevercLinkRegistrarAPI LinkRegistrarAPI = makeLinkRegistrarAPI();
constexpr NevercLTORegistrarAPI LTORegistrarAPI = makeLTORegistrarAPI();

Error addInterface(PluginProcessServices &Services, NevercInterfaceID ID,
                   NevercInterfaceStability Stability, const void *Table) {
  return Services.interfaces().registerInterface(ID, Stability, Table, {});
}

} // namespace

Expected<std::shared_ptr<const PluginLinkSnapshot>>
PluginLinkRegistry::freeze(ArrayRef<PluginLinkRegistrationView> Registrations) {
  auto Snapshot = std::make_shared<PluginLinkSnapshot>();
  for (const PluginLinkRegistrationView &Registration : Registrations) {
    if (!isCanonicalPluginID(Registration.PluginID))
      return registryError("invalid plugin ID in Link registry");
    for (const NevercLinkerProviderDescriptor &Descriptor :
         Registration.LinkerProviders)
      if (Error E = appendLinker(*Snapshot, Registration.PluginID,
                                 Registration.Owner, Descriptor))
        return std::move(E);
    for (const NevercObjectMergeProviderDescriptor &Descriptor :
         Registration.ObjectMergeProviders)
      if (Error E = appendMerger(*Snapshot, Registration.PluginID,
                                 Registration.Owner, Descriptor))
        return std::move(E);
    for (const NevercBinaryImageVerifierDescriptor &Descriptor :
         Registration.ImageVerifiers)
      if (Error E = appendVerifier(*Snapshot, Registration.PluginID,
                                   Registration.Owner, Descriptor))
        return std::move(E);
    for (const NevercLTOProviderDescriptor &Descriptor :
         Registration.LTOProviders)
      if (Error E = appendLTO(*Snapshot, Registration.PluginID,
                              Registration.Owner, Descriptor))
        return std::move(E);
  }

  stableSortRecords(PluginLinkSnapshotAccess::linkers(*Snapshot));
  stableSortRecords(PluginLinkSnapshotAccess::mergers(*Snapshot));
  stableSortRecords(PluginLinkSnapshotAccess::ltoProviders(*Snapshot));
  llvm::stable_sort(
      PluginLinkSnapshotAccess::verifiers(*Snapshot),
      [](const auto &Left, const auto &Right) {
        return std::tie(Left.PluginID, Left.VerifierID) <
               std::tie(Right.PluginID, Right.VerifierID);
      });
  return std::shared_ptr<const PluginLinkSnapshot>(std::move(Snapshot));
}

Expected<std::shared_ptr<const PluginLinkSnapshot>>
PluginLinkRegistry::freeze(
    ArrayRef<std::shared_ptr<const PluginModule>> Modules) {
  auto Snapshot = std::make_shared<PluginLinkSnapshot>();
  for (const auto &Module : Modules) {
    if (!Module || !Module->registration())
      continue;
    StringRef PluginID = Module->descriptor().PluginID;
    for (const PluginRegistrationRecord &Record :
         Module->registration()->records()) {
      switch (Record.Kind) {
      case PluginRegistrationKind::LinkerProvider:
        if (Error E = appendLinker(
                *Snapshot, PluginID, Module, Record.LinkerProvider,
                Record.ProviderID, Record.LinkCompatibilityKey))
          return std::move(E);
        break;
      case PluginRegistrationKind::ObjectMergeProvider:
        if (Error E = appendMerger(
                *Snapshot, PluginID, Module, Record.ObjectMergeProvider,
                Record.ProviderID, Record.LinkCompatibilityKey))
          return std::move(E);
        break;
      case PluginRegistrationKind::BinaryImageVerifier:
        if (Error E = appendVerifier(
                *Snapshot, PluginID, Module, Record.BinaryImageVerifier,
                Record.ProviderID))
          return std::move(E);
        break;
      case PluginRegistrationKind::LTOProvider:
        if (Error E = appendLTO(
                *Snapshot, PluginID, Module, Record.LTOProvider,
                Record.ProviderID, Record.LinkCompatibilityKey))
          return std::move(E);
        break;
      default:
        break;
      }
    }
  }
  stableSortRecords(PluginLinkSnapshotAccess::linkers(*Snapshot));
  stableSortRecords(PluginLinkSnapshotAccess::mergers(*Snapshot));
  stableSortRecords(PluginLinkSnapshotAccess::ltoProviders(*Snapshot));
  llvm::stable_sort(
      PluginLinkSnapshotAccess::verifiers(*Snapshot),
      [](const auto &Left, const auto &Right) {
        return std::tie(Left.PluginID, Left.VerifierID) <
               std::tie(Right.PluginID, Right.VerifierID);
      });
  return std::shared_ptr<const PluginLinkSnapshot>(std::move(Snapshot));
}

Error registerPluginLinkInterfaces(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return registryError(
        "cannot register plugin Link interfaces after interface freeze");
  auto ProcessService =
      std::make_shared<PluginLinkProcessService>(Services);
  if (Error E =
          Services.registerHostService(linkInterfaceID(), ProcessService))
    return E;
  if (Error E = addInterface(
          Services, linkInterfaceID(),
          NEVERC_LINK_INTERFACE_STABILITY, &LinkAPI))
    return E;
  if (Error E = addInterface(
          Services, linkRegistrarInterfaceID(),
          NEVERC_LINK_REGISTRAR_INTERFACE_STABILITY, &LinkRegistrarAPI))
    return E;
  if (Error E = registerPluginLinkPhaseInterface(Services))
    return E;
  if (Error E = registerPluginLTOInterface(Services))
    return E;
  return addInterface(
      Services, ltoRegistrarInterfaceID(),
      NEVERC_LTO_REGISTRAR_INTERFACE_STABILITY, &LTORegistrarAPI);
}

std::shared_ptr<const PluginLinkSnapshot>
findPluginLinkSnapshot(PluginProcessServices &Services,
                       NevercSessionHandle Session) {
  auto Service = std::static_pointer_cast<PluginLinkProcessService>(
      Services.findHostService(linkInterfaceID()));
  return Service ? Service->find(Session)
                 : std::shared_ptr<const PluginLinkSnapshot>();
}

} // namespace neverc::plugin
