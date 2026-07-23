#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error pluginError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

Expected<StringRef> checkedStringView(NevercStringView View,
                                      const Twine &FieldName) {
  if (View.Length > std::numeric_limits<size_t>::max())
    return pluginError(FieldName + " is too large");
  if (!View.Data && View.Length != 0)
    return pluginError(FieldName + " has null data with non-zero length");
  StringRef Result(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if (Result.contains('\0'))
    return pluginError(FieldName + " contains an embedded NUL");
  return Result;
}

bool isSemVerIdentifierChar(char C) {
  return (C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') ||
         (C >= 'a' && C <= 'z') || C == '-';
}

Error validateSemVerIdentifiers(StringRef Value, bool IsPrerelease,
                                const Twine &FieldName) {
  if (Value.empty())
    return Error::success();
  while (!Value.empty()) {
    auto Split = Value.split('.');
    StringRef Identifier = Split.first;
    if (Identifier.empty())
      return pluginError(FieldName + " contains an empty identifier");
    if (!llvm::all_of(Identifier, isSemVerIdentifierChar))
      return pluginError(FieldName + " contains a non-SemVer character");
    if (IsPrerelease && Identifier.size() > 1 &&
        llvm::all_of(Identifier, [](char C) { return C >= '0' && C <= '9'; }) &&
        Identifier.front() == '0')
      return pluginError(FieldName +
                         " has a numeric identifier with a leading zero");
    Value = Split.second;
  }
  return Error::success();
}

Error copySemanticVersion(const NevercSemanticVersion &Source,
                          NevercSemanticVersion &Destination,
                          std::string &Prerelease, std::string &BuildMetadata,
                          const Twine &FieldName) {
  if (Source.Reserved != 0)
    return pluginError(FieldName + " has non-zero reserved bits");

  auto SourcePrerelease =
      checkedStringView(Source.Prerelease, FieldName + ".prerelease");
  if (!SourcePrerelease)
    return SourcePrerelease.takeError();
  auto SourceBuild =
      checkedStringView(Source.BuildMetadata, FieldName + ".build_metadata");
  if (!SourceBuild)
    return SourceBuild.takeError();
  if (Error E = validateSemVerIdentifiers(*SourcePrerelease, true,
                                          FieldName + ".prerelease"))
    return std::move(E);
  if (Error E = validateSemVerIdentifiers(*SourceBuild, false,
                                          FieldName + ".build_metadata"))
    return std::move(E);

  Destination = Source;
  Prerelease = SourcePrerelease->str();
  BuildMetadata = SourceBuild->str();
  Destination.Prerelease = {nullptr, 0};
  Destination.BuildMetadata = {nullptr, 0};
  return Error::success();
}

template <typename ElementT>
Error validateArrayShape(NevercStructArrayView View, const Twine &FieldName,
                         uint64_t RequiredPrefix) {
  if (View.Count == 0)
    return Error::success();
  if (!View.Data)
    return pluginError(FieldName + " has null data with non-zero count");
  if (View.ElementStride < sizeof(NevercABITableHeader) ||
      View.ElementStride < RequiredPrefix)
    return pluginError(FieldName + " has an undersized element stride");
  if (View.Count > std::numeric_limits<size_t>::max() / View.ElementStride)
    return pluginError(FieldName + " byte size overflows the host");
  return Error::success();
}

template <typename ElementT>
const ElementT *arrayElement(NevercStructArrayView View, uint64_t Index) {
  const auto *Bytes = static_cast<const unsigned char *>(View.Data);
  return reinterpret_cast<const ElementT *>(Bytes + Index * View.ElementStride);
}

Error copyCompatibility(const NevercCompatibilityKey &Source,
                        OwnedCompatibilityKey &Destination,
                        const Twine &FieldName) {
  constexpr uint64_t Required =
      offsetof(NevercCompatibilityKey, Reserved) +
      sizeof(NevercCompatibilityKey::Reserved);
  if (Source.Header.StructSize < Required)
    return pluginError(FieldName + " is shorter than its required prefix");
  if (Source.Reserved != 0)
    return pluginError(FieldName + " has non-zero reserved bits");
  auto Producer =
      checkedStringView(Source.ProducerBuildID, FieldName + ".producer_build_id");
  if (!Producer)
    return Producer.takeError();
  auto Target =
      checkedStringView(Source.TargetABIKey, FieldName + ".target_abi_key");
  if (!Target)
    return Target.takeError();
  if (!json::isUTF8(*Producer) || !json::isUTF8(*Target))
    return pluginError(FieldName + " is not valid UTF-8");
  Destination.ProducerBuildID = Producer->str();
  Destination.TargetABIKey = Target->str();
  Destination.LLVMMajor = Source.LLVMMajor;
  return Error::success();
}

Error copyRequirements(NevercStructArrayView Source,
                       std::vector<OwnedInterfaceRequirement> &Destination,
                       const Twine &FieldName) {
  constexpr uint64_t Required =
      offsetof(NevercInterfaceRequirement, Compatibility) +
      sizeof(NevercInterfaceRequirement::Compatibility);
  if (Error E =
          validateArrayShape<NevercInterfaceRequirement>(Source, FieldName,
                                                         Required))
    return std::move(E);
  Destination.reserve(static_cast<size_t>(Source.Count));
  for (uint64_t I = 0; I != Source.Count; ++I) {
    const auto *Item = arrayElement<NevercInterfaceRequirement>(Source, I);
    if (Item->Header.StructSize < Required)
      return pluginError(FieldName + " contains a short element");
    if (Item->Required != NEVERC_FALSE && Item->Required != NEVERC_TRUE)
      return pluginError(FieldName + " contains a non-boolean required flag");
    if (Item->Stability != NEVERC_INTERFACE_STABLE &&
        Item->Stability != NEVERC_INTERFACE_LOCKSTEP)
      return pluginError(FieldName + " contains an invalid stability value");
    OwnedInterfaceRequirement Copy;
    Copy.Interface = Item->Interface;
    Copy.Major = Item->Major;
    Copy.MinimumMinor = Item->MinimumMinor;
    Copy.Required = Item->Required == NEVERC_TRUE;
    Copy.Stability = Item->Stability;
    if (Error E = copyCompatibility(Item->Compatibility, Copy.Compatibility,
                                    FieldName + ".compatibility"))
      return std::move(E);
    Destination.push_back(std::move(Copy));
  }
  return Error::success();
}

Error copyDependencies(NevercStructArrayView Source,
                       std::vector<OwnedPluginDependency> &Destination) {
  constexpr uint64_t Required =
      offsetof(NevercPluginDependency, Reserved) +
      sizeof(NevercPluginDependency::Reserved);
  if (Error E = validateArrayShape<NevercPluginDependency>(
          Source, "dependencies", Required))
    return std::move(E);
  Destination.reserve(static_cast<size_t>(Source.Count));
  for (uint64_t I = 0; I != Source.Count; ++I) {
    const auto *Item = arrayElement<NevercPluginDependency>(Source, I);
    if (Item->Header.StructSize < Required)
      return pluginError("dependencies contains a short element");
    if (Item->Reserved != 0)
      return pluginError("dependencies contains non-zero reserved bits");
    if (Item->Kind != NEVERC_DEPENDENCY_REQUIRED &&
        Item->Kind != NEVERC_DEPENDENCY_BEFORE &&
        Item->Kind != NEVERC_DEPENDENCY_AFTER)
      return pluginError("dependencies contains an invalid kind");
    if (Item->Version.HasMaximum != NEVERC_FALSE &&
        Item->Version.HasMaximum != NEVERC_TRUE)
      return pluginError("dependency has a non-boolean maximum flag");
    if (Item->Version.AllowPrerelease != NEVERC_FALSE &&
        Item->Version.AllowPrerelease != NEVERC_TRUE)
      return pluginError("dependency has a non-boolean prerelease flag");
    if (Item->Version.Reserved != 0)
      return pluginError("dependency version range has non-zero reserved bits");

    auto ID = checkedStringView(Item->PluginID, "dependency plugin ID");
    if (!ID)
      return ID.takeError();
    if (!isCanonicalPluginID(*ID))
      return pluginError("dependency plugin ID is not canonical");

    OwnedPluginDependency Copy;
    Copy.PluginID = ID->str();
    Copy.Kind = Item->Kind;
    Copy.Version = Item->Version;
    if (Error E = copySemanticVersion(
            Item->Version.MinimumInclusive, Copy.Version.MinimumInclusive,
            Copy.MinimumPrerelease, Copy.MinimumBuildMetadata,
            "dependency minimum version"))
      return std::move(E);
    if (Error E = copySemanticVersion(
            Item->Version.MaximumExclusive, Copy.Version.MaximumExclusive,
            Copy.MaximumPrerelease, Copy.MaximumBuildMetadata,
            "dependency maximum version"))
      return std::move(E);
    if (Error E = validateDependencyRange(Copy))
      return std::move(E);
    Destination.push_back(std::move(Copy));
  }
  return Error::success();
}

NevercStatus NEVERC_CALL queryNoBootstrapInterfaces(
    void *, NevercInterfaceID, uint16_t, uint16_t, const void **OutTable,
    uint16_t *OutMinor, uint64_t *OutStructSize) {
  if (OutTable)
    *OutTable = nullptr;
  if (OutMinor)
    *OutMinor = 0;
  if (OutStructSize)
    *OutStructSize = 0;
  NevercStatus Result = neverc_status_ok();
  Result.Code = NEVERC_STATUS_MISSING_INTERFACE;
  return Result;
}

} // namespace

// Normalizes and validates a caller-provided C plugin descriptor into the
// host-owned record.  Exposed (rather than kept file-local) so the descriptor
// and single-header ABI fuzzers can drive the exact validation the loader uses
// without loading native plugin code.  The anonymous-namespace helpers above
// remain visible to this definition within the same translation unit.
Expected<PluginDescriptorRecord>
copyAndValidateDescriptor(const NevercPluginDescriptor &Source,
                          uint32_t HostLLVMMajor) {
  constexpr uint64_t Required =
      offsetof(NevercPluginDescriptor, Register) +
      sizeof(NevercPluginDescriptor::Register);
  if (Source.Header.StructSize < Required)
    return pluginError("plugin descriptor is shorter than its required prefix");
  if (Source.Header.Major != NEVERC_PLUGIN_ABI_MAJOR)
    return pluginError("plugin ABI major does not match the host");
  if (Source.Header.Minor > NEVERC_PLUGIN_ABI_MINOR)
    return pluginError("plugin ABI minor is newer than the host");
  if (Source.Header.Flags != 0)
    return pluginError("plugin descriptor has unsupported ABI flags");

  auto ID = checkedStringView(Source.PluginID, "plugin ID");
  if (!ID)
    return ID.takeError();
  if (!isCanonicalPluginID(*ID))
    return pluginError("plugin ID is not canonical");
  auto DisplayName = checkedStringView(Source.DisplayName, "display name");
  if (!DisplayName)
    return DisplayName.takeError();
  if (DisplayName->empty() || !json::isUTF8(*DisplayName))
    return pluginError("display name must be non-empty valid UTF-8");
  if (!Source.Register)
    return pluginError("plugin descriptor has no registration callback");
  if (Source.Concurrency != NEVERC_CONCURRENCY_SESSION_SERIAL &&
      Source.Concurrency != NEVERC_CONCURRENCY_THREAD_SAFE &&
      Source.Concurrency != NEVERC_CONCURRENCY_PROCESS_SERIAL)
    return pluginError("plugin descriptor has an invalid concurrency model");
  if (Source.Reentrancy != NEVERC_REENTRANCY_NONE &&
      Source.Reentrancy != NEVERC_REENTRANCY_ALLOWED)
    return pluginError("plugin descriptor has an invalid reentrancy model");

  PluginDescriptorRecord Result;
  Result.ABIMajor = Source.Header.Major;
  Result.ABIMinor = Source.Header.Minor;
  Result.ABIFlags = Source.Header.Flags;
  Result.PluginID = ID->str();
  Result.DisplayName = DisplayName->str();
  Result.Concurrency = Source.Concurrency;
  Result.Reentrancy = Source.Reentrancy;
  if (Error E = copySemanticVersion(Source.Version, Result.Version,
                                    Result.VersionPrerelease,
                                    Result.VersionBuildMetadata,
                                    "plugin version"))
    return std::move(E);
  if (Error E = copyRequirements(Source.RequiredInterfaces,
                                 Result.RequiredInterfaces,
                                 "required interfaces"))
    return std::move(E);
  if (Error E = copyRequirements(Source.OptionalInterfaces,
                                 Result.OptionalInterfaces,
                                 "optional interfaces"))
    return std::move(E);
  if (Error E = copyDependencies(Source.Dependencies, Result.Dependencies))
    return std::move(E);

  for (const OwnedInterfaceRequirement &Requirement :
       Result.RequiredInterfaces) {
    if (!Requirement.Required)
      return pluginError(
          "required interface array contains a non-required element");
    if (Requirement.Stability == NEVERC_INTERFACE_LOCKSTEP &&
        Requirement.Compatibility.LLVMMajor != HostLLVMMajor)
      return pluginError("unstable interface LLVM major does not match host");
  }
  for (const OwnedInterfaceRequirement &Requirement :
       Result.OptionalInterfaces) {
    if (Requirement.Required)
      return pluginError(
          "optional interface array contains a required element");
  }

  Result.ProcessBegin = Source.ProcessBegin;
  Result.Register = Source.Register;
  if (Source.Header.StructSize >=
      offsetof(NevercPluginDescriptor, SessionBegin) +
          sizeof(NevercPluginDescriptor::SessionBegin))
    Result.SessionBegin = Source.SessionBegin;
  if (Source.Header.StructSize >=
      offsetof(NevercPluginDescriptor, SessionEnd) +
          sizeof(NevercPluginDescriptor::SessionEnd))
    Result.SessionEnd = Source.SessionEnd;
  if (Source.Header.StructSize >= offsetof(NevercPluginDescriptor, TaskBegin) +
                                     sizeof(NevercPluginDescriptor::TaskBegin))
    Result.TaskBegin = Source.TaskBegin;
  if (Source.Header.StructSize >= offsetof(NevercPluginDescriptor, TaskEnd) +
                                     sizeof(NevercPluginDescriptor::TaskEnd))
    Result.TaskEnd = Source.TaskEnd;
  if (Source.Header.StructSize >= offsetof(NevercPluginDescriptor, Destroy) +
                                     sizeof(NevercPluginDescriptor::Destroy))
    Result.Destroy = Source.Destroy;
  if (static_cast<bool>(Result.SessionBegin) !=
      static_cast<bool>(Result.SessionEnd))
    return pluginError(
        "plugin descriptor must provide SessionBegin and SessionEnd together");
  if (static_cast<bool>(Result.TaskBegin) !=
      static_cast<bool>(Result.TaskEnd))
    return pluginError(
        "plugin descriptor must provide TaskBegin and TaskEnd together");
  return Result;
}

struct PluginModule::Storage {
  std::string CanonicalPath;
  sys::fs::UniqueID Identity;
  sys::DynamicLibrary Library;
  PluginDescriptorRecord Descriptor;
  void *ProcessState = nullptr;
  bool ProcessBegun = false;
  std::unique_ptr<PluginPublishedRegistration> Registration;
};

PluginModule::PluginModule(std::unique_ptr<Storage> StorageValue)
    : Impl(std::move(StorageValue)) {}

PluginModule::~PluginModule() {
  if (!Impl)
    return;
  Impl->Registration.reset();
  if (Impl->Library.isValid())
    sys::DynamicLibrary::closeLibrary(Impl->Library);
}

StringRef PluginModule::path() const { return Impl->CanonicalPath; }

sys::fs::UniqueID PluginModule::identity() const { return Impl->Identity; }

const PluginDescriptorRecord &PluginModule::descriptor() const {
  return Impl->Descriptor;
}

const PluginPublishedRegistration *PluginModule::registration() const {
  return Impl->Registration.get();
}

bool PluginModule::processBegun() const { return Impl->ProcessBegun; }

bool PluginModule::registered() const {
  return static_cast<bool>(Impl->Registration);
}

void *PluginModule::processState() const { return Impl->ProcessState; }

void PluginModule::setProcessState(void *State) {
  Impl->ProcessState = State;
  Impl->ProcessBegun = true;
}

void PluginModule::clearProcessState() {
  Impl->ProcessState = nullptr;
  Impl->ProcessBegun = false;
}

void PluginModule::publishRegistration(
    std::unique_ptr<PluginPublishedRegistration> Registration) {
  Impl->Registration = std::move(Registration);
}

void PluginModule::clearRegistration() { Impl->Registration.reset(); }

const PluginModule *RegistrySnapshot::findByID(StringRef PluginID) const {
  auto It = llvm::find_if(Modules, [PluginID](const auto &Module) {
    return Module->descriptor().PluginID == PluginID;
  });
  return It == Modules.end() ? nullptr : It->get();
}

RegistrySnapshotLease::RegistrySnapshotLease(
    std::shared_ptr<const RegistrySnapshot> SnapshotValue,
    std::shared_ptr<LeaseState> StateValue)
    : Snapshot(std::move(SnapshotValue)), State(std::move(StateValue)) {
  State->Count.fetch_add(1, std::memory_order_acq_rel);
}

RegistrySnapshotLease::RegistrySnapshotLease(
    RegistrySnapshotLease &&Other) noexcept
    : Snapshot(std::move(Other.Snapshot)), State(std::move(Other.State)) {}

RegistrySnapshotLease &
RegistrySnapshotLease::operator=(RegistrySnapshotLease &&Other) noexcept {
  if (this != &Other) {
    reset();
    Snapshot = std::move(Other.Snapshot);
    State = std::move(Other.State);
  }
  return *this;
}

RegistrySnapshotLease::~RegistrySnapshotLease() { reset(); }

void RegistrySnapshotLease::reset() {
  if (State)
    State->Count.fetch_sub(1, std::memory_order_acq_rel);
  Snapshot.reset();
  State.reset();
}

RegistryActivityLease::RegistryActivityLease(
    std::shared_ptr<ActivityState> StateValue, Kind LeaseKindValue)
    : State(std::move(StateValue)), LeaseKind(LeaseKindValue) {
  if (LeaseKind == Kind::Session)
    State->SessionCount.fetch_add(1, std::memory_order_acq_rel);
  else
    State->CallbackCount.fetch_add(1, std::memory_order_acq_rel);
}

RegistryActivityLease::RegistryActivityLease(
    RegistryActivityLease &&Other) noexcept
    : State(std::move(Other.State)), LeaseKind(Other.LeaseKind) {}

RegistryActivityLease &
RegistryActivityLease::operator=(RegistryActivityLease &&Other) noexcept {
  if (this != &Other) {
    reset();
    State = std::move(Other.State);
    LeaseKind = Other.LeaseKind;
  }
  return *this;
}

RegistryActivityLease::~RegistryActivityLease() { reset(); }

void RegistryActivityLease::reset() {
  if (!State)
    return;
  if (LeaseKind == Kind::Session)
    State->SessionCount.fetch_sub(1, std::memory_order_acq_rel);
  else
    State->CallbackCount.fetch_sub(1, std::memory_order_acq_rel);
  State.reset();
}

PluginRegistry::PluginRegistry(std::string HostBuildIDValue,
                               uint32_t LLVMMajorValue,
                               const PluginInterfaceRegistry *InterfacesValue,
                               const NevercCoreAPI *CoreAPIValue,
                               PluginOptionRegistry *OptionsValue)
    : HostBuildID(std::move(HostBuildIDValue)), LLVMMajor(LLVMMajorValue),
      Interfaces(InterfacesValue), CoreAPI(CoreAPIValue),
      Options(OptionsValue),
      SnapshotLeaseState(
          std::make_shared<RegistrySnapshotLease::LeaseState>()),
      ActivityState(
          std::make_shared<RegistryActivityLease::ActivityState>()) {
  publishSnapshot();
}

PluginRegistry::~PluginRegistry() {
  if (activeSnapshotLeases() == 0 && activeSessions() == 0 &&
      activeCallbacks() == 0)
    consumeError(shutdown());
}

bool isCanonicalPluginID(StringRef PluginID) {
  if (PluginID.empty() || PluginID.size() > 255)
    return false;
  while (!PluginID.empty()) {
    auto Split = PluginID.split('.');
    StringRef Segment = Split.first;
    if (Segment.empty() || Segment.size() > 63)
      return false;
    auto IsBoundary = [](char C) {
      return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9');
    };
    if (!IsBoundary(Segment.front()) || !IsBoundary(Segment.back()))
      return false;
    if (!llvm::all_of(Segment, [](char C) {
          return (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') ||
                 C == '_' || C == '-';
        }))
      return false;
    PluginID = Split.second;
  }
  return true;
}

Expected<std::shared_ptr<const PluginModule>>
PluginRegistry::load(StringRef Path) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (ShuttingDown || ShutDown)
    return pluginError("cannot load a plugin after registry shutdown");
  if (Error E = ensureQuiet("load"))
    return std::move(E);

  SmallString<256> CanonicalPath;
  if (std::error_code EC = sys::fs::real_path(Path, CanonicalPath, true))
    return pluginError("cannot resolve plugin path '" + Path + "': " +
                       EC.message());
  sys::fs::UniqueID BeforeLoad;
  if (std::error_code EC = sys::fs::getUniqueID(CanonicalPath, BeforeLoad))
    return pluginError("cannot identify plugin '" + CanonicalPath + "': " +
                       EC.message());

  for (const auto &Module : Modules)
    if (Module->identity() == BeforeLoad)
      return std::static_pointer_cast<const PluginModule>(Module);

  SmallString<256> LoadError;
  sys::DynamicLibrary Library =
      sys::DynamicLibrary::getLibrary(CanonicalPath.c_str(), &LoadError);
  if (!Library.isValid())
    return pluginError("cannot load plugin '" + CanonicalPath + "': " +
                       LoadError);

  auto CloseOnError = make_scope_exit(
      [&Library] { sys::DynamicLibrary::closeLibrary(Library); });
  sys::fs::UniqueID AfterLoad;
  if (std::error_code EC = sys::fs::getUniqueID(CanonicalPath, AfterLoad))
    return pluginError("cannot revalidate plugin identity: " + EC.message());
  if (BeforeLoad != AfterLoad)
    return pluginError("plugin file identity changed while it was loading");

  void *Address = Library.getAddressOfSymbol(NEVERC_PLUGIN_ENTRY_POINT);
  if (!Address &&
      Library.getAddressOfSymbol("nevercGetPluginInfo"))
    return pluginError(
        "plugin exports the removed 'nevercGetPluginInfo' prototype ABI; "
        "migrate it to the first public descriptor ABI and export "
        "'neverc_plugin_entry'");
  if (!Address)
    return pluginError("plugin has no '" NEVERC_PLUGIN_ENTRY_POINT "' entry");
  auto Entry = reinterpret_cast<NevercPluginEntryFn>(Address);

  NevercBootstrapAPI Bootstrap{};
  Bootstrap.Header = {sizeof(Bootstrap), NEVERC_PLUGIN_ABI_MAJOR,
                      NEVERC_PLUGIN_ABI_MINOR, 0};
  Bootstrap.Context =
      const_cast<PluginInterfaceRegistry *>(Interfaces);
  Bootstrap.QueryInterface =
      Interfaces ? queryPluginInterface : queryNoBootstrapInterfaces;
  Bootstrap.HostBuildID = {HostBuildID.data(), HostBuildID.size()};
  Bootstrap.LLVMMajor = LLVMMajor;

  NevercPluginDescriptor RawDescriptor{};
  RawDescriptor.Header.StructSize = sizeof(RawDescriptor);
  NevercStatus Status = Entry(&Bootstrap, &RawDescriptor);
  if (Status.Code != NEVERC_STATUS_OK || Status.Flags != 0 ||
      Status.Detail != 0)
    return pluginError("plugin entry returned status code " +
                       Twine(Status.Code));

  auto Descriptor = copyAndValidateDescriptor(RawDescriptor, LLVMMajor);
  if (!Descriptor)
    return Descriptor.takeError();
  if (Interfaces) {
    for (const OwnedInterfaceRequirement &Requirement :
         Descriptor->RequiredInterfaces) {
      if (Error E = Interfaces->validateRequirement(Requirement)) {
        auto Message = toString(std::move(E));
        return pluginError(Twine("plugin '") + Descriptor->PluginID +
                           "' has an unsatisfied required interface: " +
                           Message.str());
      }
    }
  } else if (!Descriptor->RequiredInterfaces.empty()) {
    return pluginError("plugin '" + Descriptor->PluginID +
                       "' requires interfaces but the host registry is "
                       "unavailable");
  }
  for (const auto &Module : Modules)
    if (Module->descriptor().PluginID == Descriptor->PluginID)
      return pluginError("duplicate plugin ID '" + Descriptor->PluginID + "'");

  auto StorageValue = std::make_unique<PluginModule::Storage>();
  StorageValue->CanonicalPath = CanonicalPath.str().str();
  StorageValue->Identity = AfterLoad;
  StorageValue->Library = Library;
  StorageValue->Descriptor = std::move(*Descriptor);
  std::unique_ptr<PluginModule> OwnedModule(
      new PluginModule(std::move(StorageValue)));
  CloseOnError.release();
  auto Module = std::shared_ptr<PluginModule>(std::move(OwnedModule));
  Modules.push_back(Module);
  ++Generation;
  publishSnapshot();
  return std::static_pointer_cast<const PluginModule>(Module);
}

Error PluginRegistry::unload(StringRef PluginID) {
  std::lock_guard<std::mutex> LifecycleLock(LifecycleMutex);
  std::shared_ptr<PluginModule> Target;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (ShuttingDown || ShutDown)
      return pluginError("cannot unload a plugin after registry shutdown");
    if (Error E = ensureQuiet("unload"))
      return std::move(E);

    auto TargetIt = llvm::find_if(Modules, [&](const auto &Module) {
      return Module->descriptor().PluginID == PluginID;
    });
    if (TargetIt == Modules.end())
      return pluginError("cannot unload unknown plugin '" + PluginID + "'");

    for (const auto &Module : Modules) {
      if (Module == *TargetIt)
        continue;
      for (const OwnedPluginDependency &Dependency :
           Module->descriptor().Dependencies) {
        if (Dependency.Kind == NEVERC_DEPENDENCY_REQUIRED &&
            Dependency.PluginID == PluginID)
          return pluginError("cannot unload plugin '" + PluginID +
                             "' while required by plugin '" +
                             Module->descriptor().PluginID + "'");
      }
    }
    Target = *TargetIt;
    ShuttingDown = true;
  }

  Error CleanupErrors = Error::success();
  const PluginDescriptorRecord &Descriptor = Target->descriptor();
  if (Options)
    CleanupErrors =
        joinErrors(std::move(CleanupErrors),
                   Options->removePlugin(Descriptor.PluginID));
  {
    RegistryActivityLease Lease(
        ActivityState, RegistryActivityLease::Kind::Callback);
    Target->clearRegistration();
  }
  if (Target->processBegun() && Descriptor.Destroy && CoreAPI) {
    NevercStatus Status = neverc_status_ok();
    bool Threw = false;
    {
      RegistryActivityLease Lease(
          ActivityState, RegistryActivityLease::Kind::Callback);
      try {
        Status = Descriptor.Destroy(CoreAPI, Target->processState());
      } catch (...) {
        Threw = true;
      }
    }
    if (Threw)
      CleanupErrors =
          pluginError("plugin '" + Descriptor.PluginID +
                      "' Destroy callback threw during unload");
    else if (Status.Code != NEVERC_STATUS_OK || Status.Flags != 0 ||
             Status.Detail != 0)
      CleanupErrors =
          pluginError("plugin '" + Descriptor.PluginID +
                      "' Destroy callback failed during unload");
  }
  Target->clearProcessState();

  InitializedModules.erase(
      std::remove(InitializedModules.begin(), InitializedModules.end(), Target),
      InitializedModules.end());
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Modules.erase(std::remove(Modules.begin(), Modules.end(), Target),
                  Modules.end());
    ++Generation;
    publishSnapshot();
    ShuttingDown = false;
  }
  return CleanupErrors;
}

RegistrySnapshotLease PluginRegistry::acquireSnapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (ShuttingDown || ShutDown)
    return {};
  return RegistrySnapshotLease(CurrentSnapshot, SnapshotLeaseState);
}

RegistryActivityLease PluginRegistry::acquireSessionLease() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (ShuttingDown || ShutDown)
    return {};
  return RegistryActivityLease(ActivityState,
                               RegistryActivityLease::Kind::Session);
}

RegistryActivityLease PluginRegistry::acquireCallbackLease() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (ShuttingDown || ShutDown)
    return {};
  return RegistryActivityLease(ActivityState,
                               RegistryActivityLease::Kind::Callback);
}

Error PluginRegistry::ensureQuiet(StringRef Operation) const {
  uint64_t Leases =
      SnapshotLeaseState->Count.load(std::memory_order_acquire);
  if (Leases != 0)
    return pluginError("cannot " + Operation + " while " + Twine(Leases) +
                       " registry snapshot lease(s) are active");
  uint64_t Sessions =
      ActivityState->SessionCount.load(std::memory_order_acquire);
  if (Sessions != 0)
    return pluginError("cannot " + Operation + " while " + Twine(Sessions) +
                       " plugin session(s) are active");
  uint64_t Callbacks =
      ActivityState->CallbackCount.load(std::memory_order_acquire);
  if (Callbacks != 0)
    return pluginError("cannot " + Operation + " while " + Twine(Callbacks) +
                       " plugin callback(s) are active");
  return Error::success();
}

Error PluginRegistry::shutdown() {
  std::lock_guard<std::mutex> LifecycleLock(LifecycleMutex);
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (ShutDown)
      return Error::success();
    if (ShuttingDown)
      return pluginError("plugin registry shutdown is already in progress");
    if (Error E = ensureQuiet("shut down"))
      return std::move(E);
    ShuttingDown = true;
  }

  Error CleanupErrors = Error::success();
  for (auto It = InitializedModules.rbegin();
       It != InitializedModules.rend(); ++It) {
    PluginModule &Module = **It;
    const PluginDescriptorRecord &Descriptor = Module.descriptor();
    if (Options)
      CleanupErrors =
          joinErrors(std::move(CleanupErrors),
                     Options->removePlugin(Descriptor.PluginID));
    {
      RegistryActivityLease Lease(
          ActivityState, RegistryActivityLease::Kind::Callback);
      Module.clearRegistration();
    }

    if (Module.processBegun() && Descriptor.Destroy && CoreAPI) {
      NevercStatus Status = neverc_status_ok();
      bool Threw = false;
      {
        RegistryActivityLease Lease(
            ActivityState, RegistryActivityLease::Kind::Callback);
        try {
          Status = Descriptor.Destroy(CoreAPI, Module.processState());
        } catch (...) {
          Threw = true;
        }
      }
      if (Threw)
        CleanupErrors =
            joinErrors(std::move(CleanupErrors),
                       pluginError("plugin '" + Descriptor.PluginID +
                                   "' Destroy callback threw an exception"));
      else if (Status.Code != NEVERC_STATUS_OK || Status.Flags != 0 ||
               Status.Detail != 0)
        CleanupErrors = joinErrors(
            std::move(CleanupErrors),
            pluginError("plugin '" + Descriptor.PluginID +
                        "' Destroy callback failed during shutdown"));
    }
    Module.clearProcessState();
  }
  InitializedModules.clear();

  {
    std::lock_guard<std::mutex> Lock(Mutex);
    CurrentSnapshot.reset();
    Modules.clear();
    ++Generation;
    ShutDown = true;
    ShuttingDown = false;
  }
  return CleanupErrors;
}

void PluginRegistry::publishSnapshot() {
  auto Snapshot = std::make_shared<RegistrySnapshot>();
  Snapshot->Generation = Generation;
  Snapshot->Modules.reserve(Modules.size());
  for (const auto &Module : Modules)
    Snapshot->Modules.push_back(Module);
  CurrentSnapshot = std::move(Snapshot);
}

uint64_t PluginRegistry::generation() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Generation;
}

size_t PluginRegistry::moduleCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Modules.size();
}

uint64_t PluginRegistry::activeSnapshotLeases() const {
  return SnapshotLeaseState->Count.load(std::memory_order_acquire);
}

uint64_t PluginRegistry::activeSessions() const {
  return ActivityState->SessionCount.load(std::memory_order_acquire);
}

uint64_t PluginRegistry::activeCallbacks() const {
  return ActivityState->CallbackCount.load(std::memory_order_acquire);
}

} // namespace neverc::plugin
