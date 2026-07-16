#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

using namespace llvm;

namespace neverc::plugin {

PluginRegistrationRecord::PluginRegistrationRecord(
    PluginRegistrationRecord &&Other) noexcept
    : Kind(Other.Kind), Interface(Other.Interface),
      Stability(Other.Stability), InterfaceTable(Other.InterfaceTable),
      Compatibility(std::move(Other.Compatibility)),
      Option(std::move(Other.Option)), Phase(Other.Phase),
      Observer(Other.Observer), Interceptor(Other.Interceptor),
      Provider(Other.Provider), VFSProvider(Other.VFSProvider),
      CanonicalName(std::move(Other.CanonicalName)),
      ProviderID(std::move(Other.ProviderID)),
      RoutePrefix(std::move(Other.RoutePrefix)),
      TargetTriple(std::move(Other.TargetTriple)), CPU(std::move(Other.CPU)),
      Features(std::move(Other.Features)),
      ObjectFormat(std::move(Other.ObjectFormat)),
      OwnedUserData(Other.OwnedUserData),
      DestroyUserData(Other.DestroyUserData) {
  Other.OwnedUserData = nullptr;
  Other.DestroyUserData = nullptr;
}

PluginRegistrationRecord &PluginRegistrationRecord::operator=(
    PluginRegistrationRecord &&Other) noexcept {
  if (this != &Other) {
    this->~PluginRegistrationRecord();
    new (this) PluginRegistrationRecord(std::move(Other));
  }
  return *this;
}

PluginRegistrationRecord::~PluginRegistrationRecord() {
  if (!DestroyUserData)
    return;
  try {
    DestroyUserData(OwnedUserData);
  } catch (...) {
  }
}

PluginPublishedRegistration::~PluginPublishedRegistration() {
  while (!Registered.empty())
    Registered.pop_back();
}

namespace {

Error registrationError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool isNumericIdentifier(StringRef Identifier) {
  return !Identifier.empty() &&
         llvm::all_of(Identifier,
                      [](char C) { return C >= '0' && C <= '9'; });
}

int comparePrerelease(StringRef Left, StringRef Right) {
  if (Left.empty() || Right.empty()) {
    if (Left.empty() && Right.empty())
      return 0;
    return Left.empty() ? 1 : -1;
  }

  for (;;) {
    auto LeftPart = Left.split('.');
    auto RightPart = Right.split('.');
    bool LeftNumeric = isNumericIdentifier(LeftPart.first);
    bool RightNumeric = isNumericIdentifier(RightPart.first);
    int Comparison = 0;
    if (LeftNumeric && RightNumeric) {
      if (LeftPart.first.size() != RightPart.first.size())
        Comparison = LeftPart.first.size() < RightPart.first.size() ? -1 : 1;
      else
        Comparison = LeftPart.first.compare(RightPart.first);
    } else if (LeftNumeric != RightNumeric) {
      Comparison = LeftNumeric ? -1 : 1;
    } else {
      Comparison = LeftPart.first.compare(RightPart.first);
    }
    if (Comparison != 0)
      return Comparison < 0 ? -1 : 1;

    bool LeftDone = LeftPart.second.empty();
    bool RightDone = RightPart.second.empty();
    if (LeftDone || RightDone) {
      if (LeftDone && RightDone)
        return 0;
      return LeftDone ? -1 : 1;
    }
    Left = LeftPart.second;
    Right = RightPart.second;
  }
}

int compareVersion(const NevercSemanticVersion &Left, StringRef LeftPrerelease,
                   const NevercSemanticVersion &Right,
                   StringRef RightPrerelease) {
  if (Left.Major != Right.Major)
    return Left.Major < Right.Major ? -1 : 1;
  if (Left.Minor != Right.Minor)
    return Left.Minor < Right.Minor ? -1 : 1;
  if (Left.Patch != Right.Patch)
    return Left.Patch < Right.Patch ? -1 : 1;
  return comparePrerelease(LeftPrerelease, RightPrerelease);
}

struct RegistrationTransaction {
  explicit RegistrationTransaction(std::string PluginIDValue)
      : PluginID(std::move(PluginIDValue)) {}

  std::string PluginID;
  std::vector<PluginRegistrationRecord> Records;
  NevercStatus FirstFailure = neverc_status_ok();
  bool Failed = false;
};

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

NevercStatus fail(RegistrationTransaction *Transaction,
                  NevercStatusCode Code) {
  NevercStatus Result = status(Code);
  if (Transaction && !Transaction->Failed) {
    Transaction->Failed = true;
    Transaction->FirstFailure = Result;
  }
  return Result;
}

template <typename Callback>
NevercStatus protectRegistrar(RegistrationTransaction *Transaction,
                              Callback &&Body) {
  if (!Transaction)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  try {
    return Body();
  } catch (...) {
    return fail(Transaction, NEVERC_STATUS_PLUGIN_EXCEPTION);
  }
}

bool copyString(NevercStringView View, std::string &Destination,
                bool AllowEmpty) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0') ||
      !json::isUTF8(Text))
    return false;
  Destination = Text.str();
  return true;
}

bool validHeader(const NevercABITableHeader &Header, uint64_t RequiredSize) {
  return Header.StructSize >= RequiredSize &&
         Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Header.Minor <= NEVERC_PLUGIN_ABI_MINOR && Header.Flags == 0;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool canonicalName(StringRef Name) {
  if (Name.empty() || Name.size() > 255 || Name.front() == '.' ||
      Name.back() == '.' || Name.contains(".."))
    return false;
  return llvm::all_of(Name, [](char C) {
    return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') ||
           C == '.' || C == '_' || C == '-';
  });
}

NevercStatus NEVERC_CALL registerInterface(
    void *Registrar, NevercInterfaceID Interface,
    NevercInterfaceStability Stability, const void *Table,
    const NevercCompatibilityKey *Compatibility) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    if ((Interface.High == 0 && Interface.Low == 0) || !Table ||
        (Stability != NEVERC_INTERFACE_STABLE &&
         Stability != NEVERC_INTERFACE_LOCKSTEP))
      return fail(Transaction, NEVERC_STATUS_INVALID_ARGUMENT);
    const auto *Header = static_cast<const NevercABITableHeader *>(Table);
    if (!validHeader(*Header, sizeof(NevercABITableHeader)))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Interface;
    Record.Interface = Interface;
    Record.Stability = Stability;
    Record.InterfaceTable = Table;
    if (Compatibility) {
      constexpr uint64_t Required =
          offsetof(NevercCompatibilityKey, Reserved) +
          sizeof(NevercCompatibilityKey::Reserved);
      if (!validHeader(Compatibility->Header, Required) ||
          Compatibility->Reserved != 0 ||
          !copyString(Compatibility->ProducerBuildID,
                      Record.Compatibility.ProducerBuildID, true) ||
          !copyString(Compatibility->TargetABIKey,
                      Record.Compatibility.TargetABIKey, true))
        return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
      Record.Compatibility.LLVMMajor = Compatibility->LLVMMajor;
    }
    if (Stability == NEVERC_INTERFACE_LOCKSTEP &&
        (!Compatibility || Record.Compatibility.ProducerBuildID.empty() ||
         Record.Compatibility.TargetABIKey.empty() ||
         Record.Compatibility.LLVMMajor == 0))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus NEVERC_CALL
registerPhase(void *Registrar, const NevercPhaseDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required = offsetof(NevercPhaseDescriptor, Reserved) +
                                  sizeof(NevercPhaseDescriptor::Reserved);
    constexpr NevercPhasePolicy KnownPolicy =
        NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
        NEVERC_PHASE_REPLACEABLE | NEVERC_PHASE_SKIPPABLE_WITH_PROOF |
        NEVERC_PHASE_SEALED_HOST_GATE;
    constexpr NevercObserverPoint KnownObserverPoints =
        NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER |
        NEVERC_OBSERVER_AFTER_COMMIT;
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !nonzero(Descriptor->Phase) ||
        !nonzero(Descriptor->InputArtifact) ||
        !nonzero(Descriptor->OutputArtifact) ||
        Descriptor->Reserved != 0 ||
        Descriptor->Policy == 0 ||
        (Descriptor->Policy & ~KnownPolicy) != 0 ||
        (Descriptor->ObserverPoints & ~KnownObserverPoints) != 0 ||
        (Descriptor->ObserverPoints != 0 &&
         (Descriptor->Policy & NEVERC_PHASE_OBSERVABLE) == 0) ||
        ((Descriptor->Policy & NEVERC_PHASE_SEALED_HOST_GATE) != 0 &&
         (Descriptor->Policy &
          (NEVERC_PHASE_INTERCEPTABLE | NEVERC_PHASE_REPLACEABLE |
           NEVERC_PHASE_SKIPPABLE_WITH_PROOF)) != 0))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Phase;
    Record.Interface = Descriptor->Phase;
    Record.Phase = *Descriptor;
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::Phase &&
                              Existing.Interface.High ==
                                  Descriptor->Phase.High &&
                              Existing.Interface.Low ==
                                  Descriptor->Phase.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Record.Phase.CanonicalName = {};
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus NEVERC_CALL
registerObserver(void *Registrar, const NevercObserverDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercObserverDescriptor, DestroyUserData) +
        sizeof(NevercObserverDescriptor::DestroyUserData);
    constexpr NevercObserverPoint KnownPoints =
        NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER |
        NEVERC_OBSERVER_AFTER_COMMIT;
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !nonzero(Descriptor->Phase) || !Descriptor->Callback ||
        Descriptor->Reserved != 0 ||
        Descriptor->Points == 0 || (Descriptor->Points & ~KnownPoints) != 0)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Observer;
    Record.Interface = Descriptor->Phase;
    Record.Observer = *Descriptor;
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus NEVERC_CALL registerInterceptor(
    void *Registrar, const NevercInterceptorDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercInterceptorDescriptor, DestroyUserData) +
        sizeof(NevercInterceptorDescriptor::DestroyUserData);
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !nonzero(Descriptor->Phase) || !Descriptor->Callback)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Interceptor;
    Record.Interface = Descriptor->Phase;
    Record.Interceptor = *Descriptor;
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus NEVERC_CALL
registerProvider(void *Registrar, const NevercProviderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercProviderDescriptor, DestroyUserData) +
        sizeof(NevercProviderDescriptor::DestroyUserData);
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !nonzero(Descriptor->Phase) || !Descriptor->Callback ||
        Descriptor->Reserved != 0 ||
        !validHeader(Descriptor->Route.Header,
                     sizeof(Descriptor->Route)) ||
        Descriptor->Route.Reserved != 0 ||
        (Descriptor->Deterministic != NEVERC_FALSE &&
         Descriptor->Deterministic != NEVERC_TRUE) ||
        (Descriptor->Cacheable != NEVERC_FALSE &&
         Descriptor->Cacheable != NEVERC_TRUE) ||
        (Descriptor->FallbackSafe != NEVERC_FALSE &&
         Descriptor->FallbackSafe != NEVERC_TRUE))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Provider;
    Record.Interface = Descriptor->Phase;
    Record.Provider = *Descriptor;
    if (!copyString(Descriptor->ProviderID, Record.ProviderID, false) ||
        !canonicalName(Record.ProviderID) ||
        !copyString(Descriptor->Route.TargetTriple, Record.TargetTriple,
                    true) ||
        !copyString(Descriptor->Route.CPU, Record.CPU, true) ||
        !copyString(Descriptor->Route.Features, Record.Features, true) ||
        !copyString(Descriptor->Route.ObjectFormat, Record.ObjectFormat,
                    true))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Record.Provider.ProviderID = {};
    Record.Provider.Route.TargetTriple = {};
    Record.Provider.Route.CPU = {};
    Record.Provider.Route.Features = {};
    Record.Provider.Route.ObjectFormat = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus NEVERC_CALL
registerOption(void *Registrar, const NevercOptionDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    if (!Descriptor)
      return fail(Transaction, NEVERC_STATUS_INVALID_ARGUMENT);
    auto Option =
        copyPluginOptionDescriptor(Transaction->PluginID, *Descriptor);
    if (!Option) {
      consumeError(Option.takeError());
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    }

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Option;
    Record.Option =
        std::make_unique<OwnedPluginOption>(std::move(*Option));
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

} // namespace

NevercStatus registerPluginVFSProvider(
    void *Registrar, const NevercVFSProviderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercVFSProviderDescriptor, DestroyUserData) +
        sizeof(NevercVFSProviderDescriptor::DestroyUserData);
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        Descriptor->Reserved != 0 ||
        (Descriptor->Deterministic != NEVERC_FALSE &&
         Descriptor->Deterministic != NEVERC_TRUE) ||
        (Descriptor->Cacheable != NEVERC_FALSE &&
         Descriptor->Cacheable != NEVERC_TRUE) ||
        (Descriptor->Cacheable == NEVERC_TRUE &&
         Descriptor->Deterministic != NEVERC_TRUE) ||
        (!Descriptor->Status && !Descriptor->OpenRead &&
         !Descriptor->ReadDirectory && !Descriptor->Canonicalize))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::VFSProvider;
    Record.VFSProvider = *Descriptor;
    if (!copyString(Descriptor->ProviderID, Record.ProviderID, false) ||
        !canonicalName(Record.ProviderID) ||
        !copyString(Descriptor->RoutePrefix, Record.RoutePrefix, true) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::VFSProvider &&
                              Existing.ProviderID == Record.ProviderID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.VFSProvider.ProviderID = {};
    Record.VFSProvider.RoutePrefix = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

Error validateDependencyRange(const OwnedPluginDependency &Dependency) {
  if (Dependency.Version.HasMaximum == NEVERC_TRUE &&
      compareVersion(Dependency.Version.MinimumInclusive,
                     Dependency.MinimumPrerelease,
                     Dependency.Version.MaximumExclusive,
                     Dependency.MaximumPrerelease) >= 0)
    return registrationError("plugin dependency has an empty or reversed "
                             "semantic-version range");
  return Error::success();
}

bool dependencyVersionMatches(const OwnedPluginDependency &Dependency,
                              const PluginDescriptorRecord &Candidate) {
  if (Dependency.Version.AllowPrerelease != NEVERC_TRUE &&
      !Candidate.VersionPrerelease.empty())
    return false;
  if (compareVersion(Candidate.Version, Candidate.VersionPrerelease,
                     Dependency.Version.MinimumInclusive,
                     Dependency.MinimumPrerelease) < 0)
    return false;
  if (Dependency.Version.HasMaximum == NEVERC_TRUE &&
      compareVersion(Candidate.Version, Candidate.VersionPrerelease,
                     Dependency.Version.MaximumExclusive,
                     Dependency.MaximumPrerelease) >= 0)
    return false;
  return true;
}

Expected<PluginActivationPlan>
makePluginActivationPlan(PluginRegistry &Registry,
                         ArrayRef<StringRef> SelectedPluginIDs) {
  RegistrySnapshotLease Snapshot = Registry.acquireSnapshot();
  if (!Snapshot)
    return registrationError(
        "cannot create a plugin activation plan after registry shutdown");

  std::unordered_map<std::string, std::shared_ptr<const PluginModule>>
      Available;
  for (const auto &Module : Snapshot->modules())
    Available.emplace(Module->descriptor().PluginID, Module);

  std::vector<std::shared_ptr<const PluginModule>> Selected;
  Selected.reserve(SelectedPluginIDs.size());
  std::unordered_map<std::string, size_t> SelectedIndex;
  for (StringRef ID : SelectedPluginIDs) {
    auto AvailableIt = Available.find(ID.str());
    if (AvailableIt == Available.end())
      return registrationError("selected plugin '" + ID +
                               "' is not loaded in the registry");
    size_t Index = Selected.size();
    if (!SelectedIndex.emplace(ID.str(), Index).second)
      return registrationError("plugin '" + ID +
                               "' appears more than once in the activation "
                               "set");
    Selected.push_back(AvailableIt->second);
  }

  std::vector<std::vector<size_t>> Edges(Selected.size());
  std::vector<size_t> InDegree(Selected.size(), 0);
  auto addEdge = [&](size_t From, size_t To) {
    if (From == To)
      return;
    if (llvm::is_contained(Edges[From], To))
      return;
    Edges[From].push_back(To);
    ++InDegree[To];
  };

  for (size_t PluginIndex = 0; PluginIndex != Selected.size();
       ++PluginIndex) {
    const PluginDescriptorRecord &Plugin =
        Selected[PluginIndex]->descriptor();
    for (const OwnedPluginDependency &Dependency : Plugin.Dependencies) {
      if (Error E = validateDependencyRange(Dependency))
        return std::move(E);
      auto DependencyIt = SelectedIndex.find(Dependency.PluginID);
      if (DependencyIt == SelectedIndex.end()) {
        if (Dependency.Kind == NEVERC_DEPENDENCY_REQUIRED)
          return registrationError(
              "plugin '" + Plugin.PluginID + "' requires plugin '" +
              Dependency.PluginID +
              "', but it is absent from the current activation set");
        continue;
      }

      size_t DependencyIndex = DependencyIt->second;
      if (!dependencyVersionMatches(
              Dependency, Selected[DependencyIndex]->descriptor())) {
        if (Dependency.Kind == NEVERC_DEPENDENCY_REQUIRED)
          return registrationError(
              "plugin '" + Plugin.PluginID + "' requires a different version "
              "of plugin '" +
              Dependency.PluginID + "'");
        continue;
      }

      switch (Dependency.Kind) {
      case NEVERC_DEPENDENCY_REQUIRED:
      case NEVERC_DEPENDENCY_AFTER:
        addEdge(DependencyIndex, PluginIndex);
        break;
      case NEVERC_DEPENDENCY_BEFORE:
        addEdge(PluginIndex, DependencyIndex);
        break;
      default:
        return registrationError("plugin dependency has an invalid kind");
      }
    }
  }

  std::set<size_t> Ready;
  for (size_t I = 0; I != InDegree.size(); ++I)
    if (InDegree[I] == 0)
      Ready.insert(I);

  std::vector<std::shared_ptr<const PluginModule>> Ordered;
  Ordered.reserve(Selected.size());
  while (!Ready.empty()) {
    size_t Current = *Ready.begin();
    Ready.erase(Ready.begin());
    Ordered.push_back(Selected[Current]);
    for (size_t Successor : Edges[Current]) {
      if (--InDegree[Successor] == 0)
        Ready.insert(Successor);
    }
  }

  if (Ordered.size() != Selected.size()) {
    std::string Message = "plugin dependency cycle includes:";
    for (size_t I = 0; I != InDegree.size(); ++I)
      if (InDegree[I] != 0)
        Message += " " + Selected[I]->descriptor().PluginID;
    return registrationError(Message);
  }

  return PluginActivationPlan(&Registry, std::move(Snapshot),
                              std::move(Ordered));
}

Error activatePluginPlan(PluginProcessServices &ProcessServices,
                         PluginActivationPlan &Plan) {
  PluginRegistry &Registry = ProcessServices.registry();
  if (Plan.Owner != &Registry || !Plan.Snapshot)
    return registrationError(
        "plugin activation plan belongs to a different registry");

  std::lock_guard<std::mutex> LifecycleLock(Registry.LifecycleMutex);
  {
    std::lock_guard<std::mutex> RegistryLock(Registry.Mutex);
    if (Registry.ShuttingDown || Registry.ShutDown)
      return registrationError(
          "cannot activate plugins while the registry is shutting down");
  }

  const NevercCoreAPI *Core = &ProcessServices.coreAPI();
  NevercRegistrarAPI Registrar{};
  Registrar.Header = {sizeof(Registrar), NEVERC_PLUGIN_ABI_MAJOR,
                      NEVERC_PLUGIN_ABI_MINOR, 0};
  Registrar.RegisterInterface = registerInterface;
  Registrar.RegisterPhase = registerPhase;
  Registrar.RegisterObserver = registerObserver;
  Registrar.RegisterInterceptor = registerInterceptor;
  Registrar.RegisterProvider = registerProvider;
  Registrar.RegisterOption = registerOption;

  std::vector<std::shared_ptr<PluginModule>> NewlyBegun;
  std::vector<std::pair<std::shared_ptr<PluginModule>,
                        std::unique_ptr<RegistrationTransaction>>>
      PendingRegistrations;

  auto callbackError = [&](const PluginModule &Module, StringRef Callback,
                           NevercStatus StatusValue) -> Error {
    if (StatusValue.Code == NEVERC_STATUS_OK) {
      if (StatusValue.Flags == 0 && StatusValue.Detail == 0)
        return Error::success();
      return registrationError(
          "plugin '" + Module.descriptor().PluginID + "' callback '" +
          Callback + "' returned an invalid success status");
    }
    if (StatusValue.Code < NEVERC_STATUS_INVALID_ARGUMENT ||
        StatusValue.Code > NEVERC_STATUS_REENTRANCY_DENIED)
      return registrationError(
          "plugin '" + Module.descriptor().PluginID + "' callback '" +
          Callback + "' returned an unknown status code");
    return registrationError("plugin '" + Module.descriptor().PluginID +
                             "' callback '" + Callback +
                             "' failed with status code " +
                             Twine(StatusValue.Code));
  };

  auto invoke = [&](auto &&Callback) -> Expected<NevercStatus> {
    RegistryActivityLease Lease = Registry.acquireCallbackLease();
    if (!Lease)
      return registrationError(
          "registry refused a callback activity lease during activation");
    try {
      return Callback();
    } catch (...) {
      return registrationError("plugin callback threw a C++ exception");
    }
  };

  auto destroyBegun = [&] {
    for (auto It = NewlyBegun.rbegin(); It != NewlyBegun.rend(); ++It) {
      PluginModule &Module = **It;
      const PluginDescriptorRecord &Descriptor = Module.descriptor();
      if (Descriptor.Destroy) {
        auto Ignored = invoke([&] {
          return Descriptor.Destroy(Core, Module.processState());
        });
        if (!Ignored)
          consumeError(Ignored.takeError());
        else if (Error E = callbackError(Module, "Destroy", *Ignored))
          consumeError(std::move(E));
      }
      Module.clearProcessState();
    }
    NewlyBegun.clear();
  };

  for (const auto &ConstModule : Plan.OrderedPlugins) {
    auto Module = std::const_pointer_cast<PluginModule>(ConstModule);
    if (Module->processBegun())
      continue;

    void *ProcessState = nullptr;
    NevercStatus StatusValue = neverc_status_ok();
    if (Module->descriptor().ProcessBegin) {
      auto Result = invoke([&] {
        return Module->descriptor().ProcessBegin(Core, &ProcessState);
      });
      if (!Result) {
        destroyBegun();
        return Result.takeError();
      }
      StatusValue = *Result;
    }
    if (Error E = callbackError(*Module, "ProcessBegin", StatusValue)) {
      if (StatusValue.Code != NEVERC_STATUS_OK && ProcessState != nullptr) {
        destroyBegun();
        return joinErrors(
            std::move(E),
            registrationError(
                "failed ProcessBegin returned a non-null process state"));
      }
      destroyBegun();
      return std::move(E);
    }
    Module->setProcessState(ProcessState);
    NewlyBegun.push_back(std::move(Module));
  }

  for (const auto &ConstModule : Plan.OrderedPlugins) {
    auto Module = std::const_pointer_cast<PluginModule>(ConstModule);
    if (Module->registered())
      continue;

    auto Transaction = std::make_unique<RegistrationTransaction>(
        Module->descriptor().PluginID);
    auto Result = invoke([&] {
      return Module->descriptor().Register(
          Core, &Registrar, Transaction.get(), Module->processState());
    });
    if (!Result) {
      PendingRegistrations.clear();
      Transaction.reset();
      destroyBegun();
      return Result.takeError();
    }
    if (Error E = callbackError(*Module, "Register", *Result)) {
      PendingRegistrations.clear();
      Transaction.reset();
      destroyBegun();
      return std::move(E);
    }
    if (Transaction->Failed) {
      Error E = callbackError(*Module, "Registrar",
                              Transaction->FirstFailure);
      PendingRegistrations.clear();
      Transaction.reset();
      destroyBegun();
      return std::move(E);
    }
    PendingRegistrations.emplace_back(std::move(Module),
                                      std::move(Transaction));
  }

  std::vector<OwnedPluginOption> PendingOptions;
  for (const auto &Pending : PendingRegistrations)
    for (const PluginRegistrationRecord &Record :
         Pending.second->Records)
      if (Record.Option)
        PendingOptions.push_back(*Record.Option);

  if (!PendingOptions.empty()) {
    if (!Registry.Options) {
      PendingRegistrations.clear();
      destroyBegun();
      return registrationError(
          "plugin options were registered without an option registry");
    }
    if (Error E =
            Registry.Options->registerBatch(std::move(PendingOptions))) {
      PendingRegistrations.clear();
      destroyBegun();
      return std::move(E);
    }
  }

  for (auto &Pending : PendingRegistrations) {
    Pending.first->publishRegistration(
        std::make_unique<PluginPublishedRegistration>(
            std::move(Pending.second->Records)));
  }
  for (const auto &Module : NewlyBegun)
    Registry.InitializedModules.push_back(Module);
  return Error::success();
}

} // namespace neverc::plugin
