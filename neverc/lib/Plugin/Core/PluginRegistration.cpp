#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
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
      IRPass(Other.IRPass), IRAnalysis(Other.IRAnalysis),
      MIRPass(Other.MIRPass), Target(Other.Target),
      TargetABI(Other.TargetABI),
      CallingConvention(Other.CallingConvention),
      MCSchema(Other.MCSchema),
      MCEncoder(Other.MCEncoder), MCDecoder(Other.MCDecoder),
      MCAsmBackend(Other.MCAsmBackend),
      ObjectFormatDescriptor(Other.ObjectFormatDescriptor),
      CodeGenEdge(Other.CodeGenEdge),
      LinkerProvider(Other.LinkerProvider),
      ObjectMergeProvider(Other.ObjectMergeProvider),
      BinaryImageVerifier(Other.BinaryImageVerifier),
      LTOProvider(Other.LTOProvider),
      CanonicalName(std::move(Other.CanonicalName)),
      ProviderID(std::move(Other.ProviderID)),
      PassID(std::move(Other.PassID)),
      AnalysisName(std::move(Other.AnalysisName)),
      RoutePrefix(std::move(Other.RoutePrefix)),
      TargetTriple(std::move(Other.TargetTriple)), CPU(std::move(Other.CPU)),
      Features(std::move(Other.Features)),
      ObjectFormat(std::move(Other.ObjectFormat)),
      SchemaDigest(std::move(Other.SchemaDigest)),
      CodeGenCompatibilityKey(
          std::move(Other.CodeGenCompatibilityKey)),
      LinkCompatibilityKey(std::move(Other.LinkCompatibilityKey)),
      DefaultExtension(std::move(Other.DefaultExtension)),
      Aliases(std::move(Other.Aliases)),
      TargetMatchers(std::move(Other.TargetMatchers)),
      TargetReferences(std::move(Other.TargetReferences)),
      RequiredAnalyses(std::move(Other.RequiredAnalyses)),
      IRExternalDependencyDigest(
          std::move(Other.IRExternalDependencyDigest)),
      MIRRequiredAnalyses(std::move(Other.MIRRequiredAnalyses)),
      MIRPreservedAnalyses(std::move(Other.MIRPreservedAnalyses)),
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

  // Records own plugin userdata, so rollback has to release them in reverse
  // registration order.  std::vector's own destructor does not promise an
  // order and the two standard libraries we ship against disagree.
  ~RegistrationTransaction() {
    while (!Records.empty())
      Records.pop_back();
  }

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

bool validHeader(
    const NevercABITableHeader &Header, uint64_t RequiredSize,
    uint16_t ExpectedMajor = NEVERC_PLUGIN_ABI_MAJOR,
    uint16_t MaximumMinor = NEVERC_PLUGIN_ABI_MINOR) {
  return Header.StructSize >= RequiredSize &&
         Header.Major == ExpectedMajor &&
         Header.Minor <= MaximumMinor && Header.Flags == 0;
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

bool copyStringArray(NevercStringArrayView View,
                     std::vector<std::string> &Destination) {
  if (View.Count > 1024 ||
      (View.Count != 0 &&
       (!View.Data || View.ElementStride < sizeof(NevercStringView) ||
        View.ElementStride > std::numeric_limits<size_t>::max() ||
        View.ElementStride >
            std::numeric_limits<size_t>::max() / View.Count)))
    return false;
  const auto *Bytes = reinterpret_cast<const uint8_t *>(View.Data);
  Destination.reserve(static_cast<size_t>(View.Count));
  for (uint64_t I = 0; I != View.Count; ++I) {
    const auto *Item = reinterpret_cast<const NevercStringView *>(
        Bytes + static_cast<size_t>(I * View.ElementStride));
    std::string Value;
    if (!copyString(*Item, Value, false) ||
        llvm::is_contained(Destination, Value))
      return false;
    Destination.push_back(std::move(Value));
  }
  return true;
}

bool copyInterfaceIDs(NevercInterfaceIDArrayView View,
                      std::vector<NevercInterfaceID> &Destination) {
  if (View.Count > 1024 ||
      (View.Count != 0 &&
       (!View.Data || View.ElementStride < sizeof(NevercInterfaceID) ||
        View.ElementStride > std::numeric_limits<size_t>::max() ||
        View.ElementStride >
            std::numeric_limits<size_t>::max() / View.Count)))
    return false;
  const auto *Bytes = reinterpret_cast<const uint8_t *>(View.Data);
  Destination.reserve(static_cast<size_t>(View.Count));
  for (uint64_t I = 0; I != View.Count; ++I) {
    const auto *Item = reinterpret_cast<const NevercInterfaceID *>(
        Bytes + static_cast<size_t>(I * View.ElementStride));
    if (!nonzero(*Item) ||
        llvm::any_of(Destination, [&](NevercInterfaceID Existing) {
          return Existing.High == Item->High && Existing.Low == Item->Low;
        }))
      return false;
    Destination.push_back(*Item);
  }
  return true;
}

bool copyTargetMatchers(
    NevercStructArrayView View,
    std::vector<OwnedTargetTripleMatcher> &Destination) {
  constexpr uint64_t Required =
      offsetof(NevercTargetTripleMatcher, Reserved) +
      sizeof(NevercTargetTripleMatcher::Reserved);
  if (View.Count > 1024 ||
      (View.Count != 0 &&
       (!View.Data || View.ElementStride < Required ||
        View.ElementStride > std::numeric_limits<size_t>::max() ||
        View.ElementStride >
            std::numeric_limits<size_t>::max() / View.Count)))
    return false;
  const auto *Bytes = static_cast<const uint8_t *>(View.Data);
  Destination.reserve(static_cast<size_t>(View.Count));
  for (uint64_t I = 0; I != View.Count; ++I) {
    const auto *Item = reinterpret_cast<const NevercTargetTripleMatcher *>(
        Bytes + static_cast<size_t>(I * View.ElementStride));
    OwnedTargetTripleMatcher Matcher;
    if (!validHeader(Item->Header, Required) || Item->Reserved != 0 ||
        !copyString(Item->Architecture, Matcher.Architecture, true) ||
        !copyString(Item->Vendor, Matcher.Vendor, true) ||
        !copyString(Item->OperatingSystem, Matcher.OperatingSystem, true) ||
        !copyString(Item->Environment, Matcher.Environment, true))
      return false;
    Matcher.Priority = Item->Priority;
    Destination.push_back(std::move(Matcher));
  }
  return true;
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

NevercStatus registerPluginIRPass(
    void *Registrar, const NevercIRPassDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercIRPassDescriptor, DestroyUserData) +
        sizeof(NevercIRPassDescriptor::DestroyUserData);
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !llvm::all_of(Descriptor->Reserved,
                      [](uint8_t Value) { return Value == 0; }) ||
        !Descriptor->Run ||
        Descriptor->RequiredAnalysisCount > 1024 ||
        Descriptor->ExternalDependencyDigest.Length > 4096 ||
        (Descriptor->ExternalDependencyDigest.Length != 0 &&
         !Descriptor->ExternalDependencyDigest.Data) ||
        (Descriptor->RequiredAnalysisCount != 0 &&
         !Descriptor->RequiredAnalyses) ||
        (Descriptor->Deterministic != NEVERC_FALSE &&
         Descriptor->Deterministic != NEVERC_TRUE) ||
        (Descriptor->Cacheable != NEVERC_FALSE &&
         Descriptor->Cacheable != NEVERC_TRUE) ||
        (Descriptor->Level != NEVERC_IR_PASS_LEVEL_MODULE &&
         Descriptor->Level != NEVERC_IR_PASS_LEVEL_CGSCC &&
         Descriptor->Level != NEVERC_IR_PASS_LEVEL_FUNCTION &&
         Descriptor->Level != NEVERC_IR_PASS_LEVEL_LOOP))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    const auto IsPhase = [&](uint64_t High, uint64_t Low) {
      return Descriptor->Phase.High == High && Descriptor->Phase.Low == Low;
    };
    if (!IsPhase(NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                 NEVERC_PHASE_IR_PASS_PRE_OPT_LOW) &&
        !IsPhase(NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                 NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW) &&
        !IsPhase(NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
                 NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW) &&
        !IsPhase(NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
                 NEVERC_PHASE_IR_PASS_POST_OPT_LOW) &&
        !IsPhase(NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
                 NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::IRPass;
    Record.Interface = Descriptor->Phase;
    Record.IRPass = *Descriptor;
    if (Descriptor->ExternalDependencyDigest.Length != 0)
      Record.IRExternalDependencyDigest.assign(
          Descriptor->ExternalDependencyDigest.Data,
          Descriptor->ExternalDependencyDigest.Data +
              Descriptor->ExternalDependencyDigest.Length);
    for (uint64_t I = 0; I != Descriptor->RequiredAnalysisCount; ++I) {
      NevercInterfaceID Analysis = Descriptor->RequiredAnalyses[I];
      if (!nonzero(Analysis) ||
          llvm::any_of(Record.RequiredAnalyses,
                       [&](NevercInterfaceID Existing) {
                         return Existing.High == Analysis.High &&
                                Existing.Low == Analysis.Low;
                       }))
        return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
      Record.RequiredAnalyses.push_back(Analysis);
    }
    if (!copyString(Descriptor->PassID, Record.PassID, false) ||
        !canonicalName(Record.PassID) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind == PluginRegistrationKind::IRPass &&
                              Existing.PassID == Record.PassID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.IRPass.PassID = {};
    Record.IRPass.ExternalDependencyDigest = {};
    Record.IRPass.RequiredAnalyses = nullptr;
    Record.IRPass.RequiredAnalysisCount = 0;
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginIRAnalysis(
    void *Registrar, const NevercIRAnalysisDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercIRAnalysisDescriptor, DestroyUserData) +
        sizeof(NevercIRAnalysisDescriptor::DestroyUserData);
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !nonzero(Descriptor->AnalysisID) || Descriptor->Reserved != 0 ||
        !Descriptor->Compute || !Descriptor->Query ||
        !Descriptor->Invalidate || !Descriptor->Destroy ||
        Descriptor->DependencyCount > 1024 ||
        (Descriptor->DependencyCount != 0 && !Descriptor->Dependencies) ||
        (Descriptor->Level != NEVERC_IR_PASS_LEVEL_MODULE &&
         Descriptor->Level != NEVERC_IR_PASS_LEVEL_CGSCC &&
         Descriptor->Level != NEVERC_IR_PASS_LEVEL_FUNCTION &&
         Descriptor->Level != NEVERC_IR_PASS_LEVEL_LOOP))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::IRAnalysis;
    Record.Interface = Descriptor->AnalysisID;
    Record.IRAnalysis = *Descriptor;
    for (uint64_t I = 0; I != Descriptor->DependencyCount; ++I) {
      NevercInterfaceID Dependency = Descriptor->Dependencies[I];
      if (!nonzero(Dependency) ||
          (Dependency.High == Descriptor->AnalysisID.High &&
           Dependency.Low == Descriptor->AnalysisID.Low) ||
          llvm::any_of(Record.RequiredAnalyses,
                       [&](NevercInterfaceID Existing) {
                         return Existing.High == Dependency.High &&
                                Existing.Low == Dependency.Low;
                       }))
        return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
      Record.RequiredAnalyses.push_back(Dependency);
    }
    if (!copyString(Descriptor->Name, Record.AnalysisName, false) ||
        !canonicalName(Record.AnalysisName) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::IRAnalysis &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Record.IRAnalysis.Name = {};
    Record.IRAnalysis.Dependencies = nullptr;
    Record.IRAnalysis.DependencyCount = 0;
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginMIRPass(
    void *Registrar, const NevercMIRPassDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercMIRPassDescriptor, DestroyUserData) +
        sizeof(NevercMIRPassDescriptor::DestroyUserData);
    if (!Descriptor || !validHeader(Descriptor->Header, Required) ||
        !Descriptor->Run || Descriptor->RequiredAnalysisCount > 64 ||
        Descriptor->PreservedAnalysisCount > 64 ||
        (Descriptor->RequiredAnalysisCount != 0 &&
         !Descriptor->RequiredAnalyses) ||
        (Descriptor->PreservedAnalysisCount != 0 &&
         !Descriptor->PreservedAnalyses) ||
        (Descriptor->Deterministic != NEVERC_FALSE &&
         Descriptor->Deterministic != NEVERC_TRUE) ||
        !llvm::all_of(Descriptor->Reserved,
                      [](uint8_t Value) { return Value == 0; }) ||
        (Descriptor->Level != NEVERC_MIR_PASS_LEVEL_MODULE &&
         Descriptor->Level != NEVERC_MIR_PASS_LEVEL_FUNCTION &&
         Descriptor->Level != NEVERC_MIR_PASS_LEVEL_BASIC_BLOCK))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    const auto IsPhase = [&](uint64_t High, uint64_t Low) {
      return Descriptor->Phase.High == High && Descriptor->Phase.Low == Low;
    };
    if (!IsPhase(NEVERC_PHASE_MIR_PASS_POST_ISEL_HIGH,
                 NEVERC_PHASE_MIR_PASS_POST_ISEL_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_POST_LEGALIZE_HIGH,
                 NEVERC_PHASE_MIR_PASS_POST_LEGALIZE_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_HIGH,
                 NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_HIGH,
                 NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_HIGH,
                 NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_POST_REGALLOC_HIGH,
                 NEVERC_PHASE_MIR_PASS_POST_REGALLOC_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_POST_PROLOG_EPILOG_HIGH,
                 NEVERC_PHASE_MIR_PASS_POST_PROLOG_EPILOG_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                 NEVERC_PHASE_MIR_PASS_PREEMIT_LOW) &&
        !IsPhase(NEVERC_PHASE_MIR_PASS_FINAL_HIGH,
                 NEVERC_PHASE_MIR_PASS_FINAL_LOW))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    auto ValidAnalysis = [](NevercMIRBuiltinAnalysis Analysis) {
      return Analysis >= NEVERC_MIR_ANALYSIS_LIVE_INTERVALS &&
             Analysis <= NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE;
    };
    bool SupportsIndexedLiveness =
        Descriptor->Level != NEVERC_MIR_PASS_LEVEL_MODULE &&
        (IsPhase(NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_HIGH,
                 NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_LOW) ||
         IsPhase(NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_HIGH,
                 NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_LOW) ||
         IsPhase(NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_HIGH,
                 NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_LOW));
    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::MIRPass;
    Record.Interface = Descriptor->Phase;
    Record.MIRPass = *Descriptor;
    if (Descriptor->RequiredTargetSchemaDigest.Length > 4096 ||
        !copyString(Descriptor->RequiredTargetSchemaDigest,
                    Record.SchemaDigest, true))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    for (uint64_t I = 0; I != Descriptor->RequiredAnalysisCount; ++I) {
      NevercMIRBuiltinAnalysis Analysis = Descriptor->RequiredAnalyses[I];
      if (!ValidAnalysis(Analysis) ||
          ((Analysis == NEVERC_MIR_ANALYSIS_LIVE_INTERVALS ||
            Analysis == NEVERC_MIR_ANALYSIS_SLOT_INDEXES) &&
           !SupportsIndexedLiveness) ||
          llvm::is_contained(Record.MIRRequiredAnalyses, Analysis))
        return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
      Record.MIRRequiredAnalyses.push_back(Analysis);
    }
    for (uint64_t I = 0; I != Descriptor->PreservedAnalysisCount; ++I) {
      NevercMIRBuiltinAnalysis Analysis = Descriptor->PreservedAnalyses[I];
      if (!ValidAnalysis(Analysis) ||
          llvm::is_contained(Record.MIRPreservedAnalyses, Analysis))
        return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
      Record.MIRPreservedAnalyses.push_back(Analysis);
    }
    if (!copyString(Descriptor->PassID, Record.PassID, false) ||
        !canonicalName(Record.PassID) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::MIRPass &&
                              Existing.PassID == Record.PassID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.MIRPass.PassID = {};
    Record.MIRPass.RequiredAnalyses = nullptr;
    Record.MIRPass.RequiredAnalysisCount = 0;
    Record.MIRPass.PreservedAnalyses = nullptr;
    Record.MIRPass.PreservedAnalysisCount = 0;
    Record.MIRPass.RequiredTargetSchemaDigest = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginTarget(
    void *Registrar, const NevercTargetDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercTargetDescriptor, DestroyUserData) +
        sizeof(NevercTargetDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR) ||
        !nonzero(Descriptor->TargetID) || Descriptor->Flags != 0)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::Target;
    Record.Interface = Descriptor->TargetID;
    Record.Target = *Descriptor;
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        !copyStringArray(Descriptor->Aliases, Record.Aliases) ||
        !copyTargetMatchers(Descriptor->TripleMatchers,
                            Record.TargetMatchers) ||
        llvm::is_contained(Record.Aliases, Record.CanonicalName) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind == PluginRegistrationKind::Target &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.Target.CanonicalName = {};
    Record.Target.Aliases = {};
    Record.Target.TripleMatchers = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginTargetABI(
    void *Registrar, const NevercTargetABIDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercTargetABIDescriptor, DestroyUserData) +
        sizeof(NevercTargetABIDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_TARGET_ABI_API_MAJOR,
                     NEVERC_TARGET_ABI_API_MINOR) ||
        !nonzero(Descriptor->ABIID) || !nonzero(Descriptor->TargetID) ||
        Descriptor->Flags != 0)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::TargetABI;
    Record.Interface = Descriptor->ABIID;
    Record.TargetABI = *Descriptor;
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        !copyInterfaceIDs(Descriptor->Dependencies,
                          Record.TargetReferences) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::TargetABI &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.TargetABI.CanonicalName = {};
    Record.TargetABI.Dependencies = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginCallingConvention(
    void *Registrar, const NevercCallingConventionDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercCallingConventionDescriptor, DestroyUserData) +
        sizeof(NevercCallingConventionDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_CALLING_CONVENTION_API_MAJOR,
                     NEVERC_CALLING_CONVENTION_API_MINOR) ||
        !nonzero(Descriptor->CallingConventionID) ||
        !nonzero(Descriptor->TargetID) || Descriptor->Flags != 0)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::CallingConvention;
    Record.Interface = Descriptor->CallingConventionID;
    std::memcpy(
        &Record.CallingConvention, Descriptor,
        std::min<size_t>(Descriptor->Header.StructSize,
                         sizeof(Record.CallingConvention)));
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::CallingConvention &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.CallingConvention.CanonicalName = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginMCSchema(
    void *Registrar, const NevercMCSchemaDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercMCSchemaDescriptor, DestroyUserData) +
        sizeof(NevercMCSchemaDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR) ||
        !nonzero(Descriptor->SchemaID) || !nonzero(Descriptor->TargetID) ||
        Descriptor->Flags != 0)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::MCSchema;
    Record.Interface = Descriptor->SchemaID;
    Record.MCSchema = *Descriptor;
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        !copyString(Descriptor->Digest, Record.SchemaDigest, false) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::MCSchema &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.MCSchema.CanonicalName = {};
    Record.MCSchema.Digest = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginMCEncoder(
    void *Registrar, const NevercMCEncoderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercMCEncoderDescriptor, DestroyUserData) +
        sizeof(NevercMCEncoderDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR) ||
        !nonzero(Descriptor->ProviderID) ||
        !nonzero(Descriptor->TargetID) ||
        !nonzero(Descriptor->SchemaID) ||
        Descriptor->MaximumInstructionLength == 0 ||
        Descriptor->MaximumInstructionLength > 4096 ||
        Descriptor->Reserved != 0 || Descriptor->Flags != 0 ||
        !Descriptor->EncodeInstruction)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::MCEncoder;
    Record.Interface = Descriptor->ProviderID;
    Record.MCEncoder = *Descriptor;
    if (llvm::any_of(
            Transaction->Records,
            [&](const PluginRegistrationRecord &Existing) {
              return Existing.Kind == PluginRegistrationKind::MCEncoder &&
                     Existing.Interface.High == Record.Interface.High &&
                     Existing.Interface.Low == Record.Interface.Low;
            }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginMCDecoder(
    void *Registrar, const NevercMCDecoderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercMCDecoderDescriptor, DestroyUserData) +
        sizeof(NevercMCDecoderDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR) ||
        !nonzero(Descriptor->ProviderID) ||
        !nonzero(Descriptor->TargetID) ||
        !nonzero(Descriptor->SchemaID) ||
        Descriptor->MaximumInstructionLength == 0 ||
        Descriptor->MaximumInstructionLength > 4096 ||
        Descriptor->Reserved != 0 || Descriptor->Flags != 0 ||
        !Descriptor->DecodeInstruction)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::MCDecoder;
    Record.Interface = Descriptor->ProviderID;
    Record.MCDecoder = *Descriptor;
    if (llvm::any_of(
            Transaction->Records,
            [&](const PluginRegistrationRecord &Existing) {
              return Existing.Kind == PluginRegistrationKind::MCDecoder &&
                     Existing.Interface.High == Record.Interface.High &&
                     Existing.Interface.Low == Record.Interface.Low;
            }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginMCAsmBackend(
    void *Registrar, const NevercMCAsmBackendDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercMCAsmBackendDescriptor, DestroyUserData) +
        sizeof(NevercMCAsmBackendDescriptor::DestroyUserData);
    const bool ValidAlignment =
        Descriptor && Descriptor->MinimumInstructionAlignment != 0 &&
        Descriptor->MinimumInstructionAlignment <= 4096 &&
        (Descriptor->MinimumInstructionAlignment &
         (Descriptor->MinimumInstructionAlignment - 1)) == 0;
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR) ||
        !nonzero(Descriptor->ProviderID) ||
        !nonzero(Descriptor->TargetID) ||
        !nonzero(Descriptor->SchemaID) ||
        Descriptor->MaximumLayoutIterations == 0 ||
        Descriptor->MaximumLayoutIterations > 64 || !ValidAlignment ||
        Descriptor->Flags != 0 || !Descriptor->GetFixupKindInfo ||
        !Descriptor->MapRelocation ||
        !Descriptor->ShouldRelaxFixup ||
        !Descriptor->RelaxFragment || !Descriptor->ApplyFixup ||
        !Descriptor->WriteNops)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::MCAsmBackend;
    Record.Interface = Descriptor->ProviderID;
    Record.MCAsmBackend = *Descriptor;
    if (llvm::any_of(
            Transaction->Records,
            [&](const PluginRegistrationRecord &Existing) {
              return Existing.Kind ==
                         PluginRegistrationKind::MCAsmBackend &&
                     Existing.Interface.High == Record.Interface.High &&
                     Existing.Interface.Low == Record.Interface.Low;
            }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginObjectFormat(
    void *Registrar, const NevercObjectFormatDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercObjectFormatDescriptor, DestroyUserData) +
        sizeof(NevercObjectFormatDescriptor::DestroyUserData);
    constexpr NevercObjectFormatFlags KnownFlags =
        NEVERC_OBJECT_FORMAT_CAN_PROBE |
        NEVERC_OBJECT_FORMAT_CAN_READ |
        NEVERC_OBJECT_FORMAT_CAN_WRITE;
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_OBJECT_FORMAT_API_MAJOR,
                     NEVERC_OBJECT_FORMAT_API_MINOR) ||
        !nonzero(Descriptor->FormatID) ||
        (Descriptor->Flags & ~KnownFlags) != 0 ||
        (((Descriptor->Flags & NEVERC_OBJECT_FORMAT_CAN_PROBE) != 0) !=
         (Descriptor->Probe != nullptr)) ||
        (((Descriptor->Flags & NEVERC_OBJECT_FORMAT_CAN_READ) != 0) !=
         (Descriptor->Reader != nullptr)) ||
        (((Descriptor->Flags & NEVERC_OBJECT_FORMAT_CAN_WRITE) != 0) !=
         (Descriptor->Writer != nullptr)) ||
        (Descriptor->Reader != nullptr && Descriptor->Probe == nullptr))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::ObjectFormat;
    Record.Interface = Descriptor->FormatID;
    Record.ObjectFormatDescriptor = *Descriptor;
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        !copyStringArray(Descriptor->Aliases, Record.Aliases) ||
        !copyInterfaceIDs(Descriptor->SupportedTargets,
                          Record.TargetReferences) ||
        !copyString(Descriptor->DefaultExtension, Record.DefaultExtension,
                    true) ||
        llvm::is_contained(Record.Aliases, Record.CanonicalName) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::ObjectFormat &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.ObjectFormatDescriptor.CanonicalName = {};
    Record.ObjectFormatDescriptor.Aliases = {};
    Record.ObjectFormatDescriptor.SupportedTargets = {};
    Record.ObjectFormatDescriptor.DefaultExtension = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginCodeGenEdge(
    void *Registrar, const NevercCodeGenEdgeDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercCodeGenEdgeDescriptor, DestroyUserData) +
        sizeof(NevercCodeGenEdgeDescriptor::DestroyUserData);
    const auto ValidProduct = [](NevercCodeGenProductKind Kind) {
      return (Kind >= NEVERC_CODEGEN_PRODUCT_IR &&
              Kind <= NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE) ||
             Kind >= NEVERC_CODEGEN_PRODUCT_CUSTOM;
    };
    constexpr NevercCodeGenEdgeFlags KnownFlags =
        NEVERC_CODEGEN_EDGE_COARSE | NEVERC_CODEGEN_EDGE_BUILTIN;
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR) ||
        !nonzero(Descriptor->EdgeID) || !nonzero(Descriptor->TargetID) ||
        !ValidProduct(Descriptor->InputKind) ||
        !ValidProduct(Descriptor->OutputKind) ||
        Descriptor->InputKind == Descriptor->OutputKind ||
        (Descriptor->Flags & ~KnownFlags) != 0 ||
        ((Descriptor->Flags & NEVERC_CODEGEN_EDGE_COARSE) != 0 &&
         (Descriptor->Flags & NEVERC_CODEGEN_EDGE_BUILTIN) != 0) ||
        (Descriptor->CoarseLower &&
         (Descriptor->Flags & NEVERC_CODEGEN_EDGE_COARSE) == 0))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::CodeGenEdge;
    Record.Interface = Descriptor->EdgeID;
    Record.CodeGenEdge = *Descriptor;
    if (!copyString(Descriptor->CanonicalName, Record.CanonicalName, false) ||
        !canonicalName(Record.CanonicalName) ||
        !copyString(Descriptor->CompatibilityKey,
                    Record.CodeGenCompatibilityKey, true) ||
        !copyString(Descriptor->ProviderID, Record.ProviderID, true) ||
        (!Record.ProviderID.empty() &&
         !canonicalName(Record.ProviderID)) ||
        !copyInterfaceIDs(Descriptor->Dependencies,
                          Record.TargetReferences) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::CodeGenEdge &&
                              Existing.Interface.High ==
                                  Record.Interface.High &&
                              Existing.Interface.Low == Record.Interface.Low;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.CodeGenEdge.CanonicalName = {};
    Record.CodeGenEdge.Dependencies = {};
    Record.CodeGenEdge.CompatibilityKey = {};
    Record.CodeGenEdge.ProviderID = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginLinkerProvider(
    void *Registrar, const NevercLinkerProviderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercLinkerProviderDescriptor, DestroyUserData) +
        sizeof(NevercLinkerProviderDescriptor::DestroyUserData);
    constexpr NevercLinkProviderFlags KnownFlags =
        NEVERC_LINK_PROVIDER_DETERMINISTIC |
        NEVERC_LINK_PROVIDER_CACHEABLE |
        NEVERC_LINK_PROVIDER_REPLAY_REQUIRED;
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR) ||
        (Descriptor->OutputKind != 0 &&
         (Descriptor->OutputKind < NEVERC_LINK_OUTPUT_EXECUTABLE ||
          Descriptor->OutputKind > NEVERC_LINK_OUTPUT_BUNDLE)) ||
        (Descriptor->Flags & ~KnownFlags) != 0 ||
        !nonzero(Descriptor->ProductID) || !Descriptor->Link ||
        !Descriptor->VerifyImage)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::LinkerProvider;
    Record.Interface = Descriptor->ProductID;
    Record.LinkerProvider = *Descriptor;
    if (!copyString(Descriptor->ProviderID, Record.ProviderID, false) ||
        !canonicalName(Record.ProviderID) ||
        !copyString(Descriptor->CompatibilityKey,
                    Record.LinkCompatibilityKey, true) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::LinkerProvider &&
                              Existing.ProviderID == Record.ProviderID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.LinkerProvider.ProviderID = {};
    Record.LinkerProvider.CompatibilityKey = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginObjectMergeProvider(
    void *Registrar,
    const NevercObjectMergeProviderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercObjectMergeProviderDescriptor, DestroyUserData) +
        sizeof(NevercObjectMergeProviderDescriptor::DestroyUserData);
    constexpr NevercLinkProviderFlags KnownFlags =
        NEVERC_LINK_PROVIDER_DETERMINISTIC |
        NEVERC_LINK_PROVIDER_CACHEABLE |
        NEVERC_LINK_PROVIDER_REPLAY_REQUIRED;
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR) ||
        (Descriptor->Flags & ~KnownFlags) != 0 ||
        !nonzero(Descriptor->ProductID) || !Descriptor->Merge)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::ObjectMergeProvider;
    Record.Interface = Descriptor->ProductID;
    Record.ObjectMergeProvider = *Descriptor;
    if (!copyString(Descriptor->ProviderID, Record.ProviderID, false) ||
        !canonicalName(Record.ProviderID) ||
        !copyString(Descriptor->CompatibilityKey,
                    Record.LinkCompatibilityKey, true) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::ObjectMergeProvider &&
                              Existing.ProviderID == Record.ProviderID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.ObjectMergeProvider.ProviderID = {};
    Record.ObjectMergeProvider.CompatibilityKey = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginBinaryImageVerifier(
    void *Registrar,
    const NevercBinaryImageVerifierDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercBinaryImageVerifierDescriptor, DestroyUserData) +
        sizeof(NevercBinaryImageVerifierDescriptor::DestroyUserData);
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR) ||
        (Descriptor->OutputKind != 0 &&
         (Descriptor->OutputKind < NEVERC_LINK_OUTPUT_EXECUTABLE ||
          Descriptor->OutputKind > NEVERC_LINK_OUTPUT_BUNDLE)) ||
        !Descriptor->Verify)
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::BinaryImageVerifier;
    Record.BinaryImageVerifier = *Descriptor;
    if (!copyString(Descriptor->VerifierID, Record.ProviderID, false) ||
        !canonicalName(Record.ProviderID) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::BinaryImageVerifier &&
                              Existing.ProviderID == Record.ProviderID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.BinaryImageVerifier.VerifierID = {};
    Record.OwnedUserData = Descriptor->UserData;
    Record.DestroyUserData = Descriptor->DestroyUserData;
    Transaction->Records.push_back(std::move(Record));
    return neverc_status_ok();
  });
}

NevercStatus registerPluginLTOProvider(
    void *Registrar, const NevercLTOProviderDescriptor *Descriptor) {
  auto *Transaction = static_cast<RegistrationTransaction *>(Registrar);
  return protectRegistrar(Transaction, [&] {
    constexpr uint64_t Required =
        offsetof(NevercLTOProviderDescriptor, DestroyUserData) +
        sizeof(NevercLTOProviderDescriptor::DestroyUserData);
    constexpr NevercLTOProviderFlags KnownFlags =
        NEVERC_LTO_PROVIDER_FULL | NEVERC_LTO_PROVIDER_THIN |
        NEVERC_LTO_PROVIDER_DETERMINISTIC |
        NEVERC_LTO_PROVIDER_CACHEABLE |
        NEVERC_LTO_PROVIDER_REPLAY_REQUIRED;
    if (!Descriptor ||
        !validHeader(Descriptor->Header, Required,
                     NEVERC_LTO_API_MAJOR, NEVERC_LTO_API_MINOR) ||
        (Descriptor->Flags & ~KnownFlags) != 0 ||
        (Descriptor->Flags &
         (NEVERC_LTO_PROVIDER_FULL | NEVERC_LTO_PROVIDER_THIN)) == 0 ||
        !nonzero(Descriptor->ProductID) || !Descriptor->Codegen ||
        ((Descriptor->Flags & NEVERC_LTO_PROVIDER_CACHEABLE) != 0 &&
         !Descriptor->BuildCacheKey))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    PluginRegistrationRecord Record;
    Record.Kind = PluginRegistrationKind::LTOProvider;
    Record.Interface = Descriptor->ProductID;
    Record.LTOProvider = *Descriptor;
    if (!copyString(Descriptor->ProviderID, Record.ProviderID, false) ||
        !canonicalName(Record.ProviderID) ||
        !copyString(Descriptor->CompatibilityKey,
                    Record.LinkCompatibilityKey, true) ||
        llvm::any_of(Transaction->Records,
                     [&](const PluginRegistrationRecord &Existing) {
                       return Existing.Kind ==
                                  PluginRegistrationKind::LTOProvider &&
                              Existing.ProviderID == Record.ProviderID;
                     }))
      return fail(Transaction, NEVERC_STATUS_INVALID_DESCRIPTOR);

    Record.LTOProvider.ProviderID = {};
    Record.LTOProvider.CompatibilityKey = {};
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
        StatusValue.Code > NEVERC_STATUS_NOT_FOUND)
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

  auto discardPending = [&] {
    while (!PendingRegistrations.empty())
      PendingRegistrations.pop_back();
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
      discardPending();
      Transaction.reset();
      destroyBegun();
      return Result.takeError();
    }
    if (Error E = callbackError(*Module, "Register", *Result)) {
      discardPending();
      Transaction.reset();
      destroyBegun();
      return std::move(E);
    }
    if (Transaction->Failed) {
      Error E = callbackError(*Module, "Registrar",
                              Transaction->FirstFailure);
      discardPending();
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

  for (auto &Pending : PendingRegistrations)
    Pending.first->publishRegistration(
        std::make_unique<PluginPublishedRegistration>(
            std::move(Pending.second->Records)));
  const auto ClearPendingRegistrations = [&] {
    for (auto It = PendingRegistrations.rbegin();
         It != PendingRegistrations.rend(); ++It)
      It->first->clearRegistration();
  };

  if (Error E =
          ProcessServices.validatePluginRegistrations(
              Plan.Snapshot->modules())) {
    ClearPendingRegistrations();
    discardPending();
    destroyBegun();
    return E;
  }

  if (!PendingOptions.empty()) {
    if (!Registry.Options) {
      ClearPendingRegistrations();
      discardPending();
      destroyBegun();
      return registrationError(
          "plugin options were registered without an option registry");
    }
    if (Error E =
            Registry.Options->registerBatch(std::move(PendingOptions))) {
      ClearPendingRegistrations();
      discardPending();
      destroyBegun();
      return std::move(E);
    }
  }

  for (const auto &Module : NewlyBegun)
    Registry.InitializedModules.push_back(Module);
  return Error::success();
}

} // namespace neverc::plugin
