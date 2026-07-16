#include "ToolChainPluginBridge.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "llvm/Support/JSON.h"
#include <new>

namespace neverc::driver {
namespace {

constexpr size_t MaximumToolChainStringBytes = 4096;
constexpr size_t MaximumToolChainFeatures = 256;

llvm::Error validateText(llvm::StringRef Text, llvm::StringRef Field,
                         bool AllowEmpty) {
  if (!AllowEmpty && Text.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "%s must not be empty", Field.str().c_str());
  if (Text.size() > MaximumToolChainStringBytes || Text.contains('\0') ||
      !llvm::json::isUTF8(Text))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "%s is not a valid bounded UTF-8 string",
                                   Field.str().c_str());
  return llvm::Error::success();
}

llvm::Error verifyRequestData(const DriverToolChainRequestData &Request) {
  if (llvm::Error E = validateText(Request.RequestedTriple,
                                   "requested target triple", false))
    return E;
  if (llvm::Error E =
          validateText(Request.ComputedTriple, "computed target triple", false))
    return E;
  if (llvm::Error E = validateText(Request.SysRoot, "sysroot", true))
    return E;
  if (llvm::Error E =
          validateText(Request.ResourceDir, "resource directory", true))
    return E;
  if (llvm::Error E = validateText(Request.CPU, "target CPU", true))
    return E;
  if (Request.Features.size() > MaximumToolChainFeatures)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "too many target features");
  for (const std::string &Feature : Request.Features) {
    if (llvm::Error E = validateText(Feature, "target feature", false))
      return E;
    if (Feature.front() != '+' && Feature.front() != '-')
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "target features must begin with '+' or '-'");
  }
  if (Request.ExecutionLevel > NEVERC_EXECUTION_LEVEL_KERNEL)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid execution level");
  return llvm::Error::success();
}

NevercStringView asView(const std::string &Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

} // namespace

DriverToolChainRequestArtifact::DriverToolChainRequestArtifact(
    DriverToolChainRequestData RequestValue)
    : Request(std::move(RequestValue)) {
  rebuildFeatureViews();
}

DriverToolChainRequestArtifact::DriverToolChainRequestArtifact(
    const DriverToolChainRequestArtifact &Other) {
  std::lock_guard<std::mutex> Lock(Other.Mutex);
  Request = Other.Request;
  rebuildFeatureViews();
}

llvm::Expected<std::unique_ptr<DriverToolChainMutation>>
DriverToolChainRequestArtifact::beginMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (MutationActive)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "toolchain mutation already active");
  auto Mutation = std::unique_ptr<DriverToolChainMutation>(
      new (std::nothrow) DriverToolChainMutation(*this, Request));
  if (!Mutation)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "unable to allocate toolchain mutation");
  MutationActive = true;
  return std::move(Mutation);
}

DriverToolChainRequestData DriverToolChainRequestArtifact::snapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return Request;
}

void DriverToolChainRequestArtifact::describe(
    NevercToolChainRequest &OutRequest) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  OutRequest.RequestedTriple = asView(Request.RequestedTriple);
  OutRequest.ComputedTriple = asView(Request.ComputedTriple);
  OutRequest.SysRoot = asView(Request.SysRoot);
  OutRequest.ResourceDir = asView(Request.ResourceDir);
  OutRequest.CPU = asView(Request.CPU);
  OutRequest.Features = {FeatureViews.data(), FeatureViews.size(),
                         sizeof(NevercStringView)};
  OutRequest.ExecutionLevel = Request.ExecutionLevel;
  OutRequest.DynamicCodeProfile =
      Request.DynamicCodeProfile ? NEVERC_TRUE : NEVERC_FALSE;
}

llvm::Error DriverToolChainRequestArtifact::verify() const {
  return verifyRequestData(snapshot());
}

llvm::Error
DriverToolChainRequestArtifact::commit(DriverToolChainRequestData NewRequest) {
  if (llvm::Error E = verifyRequestData(NewRequest))
    return E;
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!MutationActive)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "toolchain mutation is not active");
  Request = std::move(NewRequest);
  rebuildFeatureViews();
  MutationActive = false;
  return llvm::Error::success();
}

void DriverToolChainRequestArtifact::finishMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  MutationActive = false;
}

void DriverToolChainRequestArtifact::rebuildFeatureViews() {
  FeatureViews.clear();
  FeatureViews.reserve(Request.Features.size());
  for (const std::string &Feature : Request.Features)
    FeatureViews.push_back(asView(Feature));
}

DriverToolChainMutation::DriverToolChainMutation(
    DriverToolChainRequestArtifact &OwnerValue,
    DriverToolChainRequestData RequestValue)
    : Owner(&OwnerValue), Request(std::move(RequestValue)) {}

DriverToolChainMutation::~DriverToolChainMutation() { abort(); }

llvm::Error DriverToolChainMutation::setTriple(llvm::StringRef Triple) {
  if (Finished)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "toolchain mutation is finished");
  if (llvm::Error E = validateText(Triple, "computed target triple", false))
    return E;
  Request.ComputedTriple = Triple.str();
  return llvm::Error::success();
}

llvm::Error DriverToolChainMutation::setCPU(llvm::StringRef CPU) {
  if (Finished)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "toolchain mutation is finished");
  if (llvm::Error E = validateText(CPU, "target CPU", true))
    return E;
  Request.CPU = CPU.str();
  return llvm::Error::success();
}

llvm::Error DriverToolChainMutation::setFeatures(
    llvm::ArrayRef<llvm::StringRef> FeatureValues) {
  if (Finished)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "toolchain mutation is finished");
  if (FeatureValues.size() > MaximumToolChainFeatures)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "too many target features");
  std::vector<std::string> NewFeatures;
  NewFeatures.reserve(FeatureValues.size());
  for (llvm::StringRef Feature : FeatureValues) {
    if (llvm::Error E = validateText(Feature, "target feature", false))
      return E;
    if (Feature.front() != '+' && Feature.front() != '-')
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "target features must begin with '+' or '-'");
    NewFeatures.push_back(Feature.str());
  }
  Request.Features = std::move(NewFeatures);
  return llvm::Error::success();
}

llvm::Error DriverToolChainMutation::commit() {
  if (Finished || !Owner)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "toolchain mutation is finished");
  if (llvm::Error E = Owner->commit(std::move(Request)))
    return E;
  Finished = true;
  Owner = nullptr;
  return llvm::Error::success();
}

void DriverToolChainMutation::abort() {
  if (Finished || !Owner)
    return;
  Owner->finishMutation();
  Owner = nullptr;
  Finished = true;
}

DriverToolChainSelectionArtifact::DriverToolChainSelectionArtifact(
    const DriverToolChainSelectionArtifact &Other)
    : ToolChainID(Other.ToolChainID), TargetKey(Other.TargetKey),
      TargetTriple(Other.TargetTriple), CPU(Other.CPU),
      Features(Other.Features), Provider(Other.Provider),
      BuiltinProviderUsed(Other.BuiltinProviderUsed) {
  rebuildFeatureViews();
}

DriverToolChainSelectionArtifact &DriverToolChainSelectionArtifact::operator=(
    const DriverToolChainSelectionArtifact &Other) {
  if (this == &Other)
    return *this;
  ToolChainID = Other.ToolChainID;
  TargetKey = Other.TargetKey;
  TargetTriple = Other.TargetTriple;
  CPU = Other.CPU;
  Features = Other.Features;
  Provider = Other.Provider;
  BuiltinProviderUsed = Other.BuiltinProviderUsed;
  rebuildFeatureViews();
  return *this;
}

DriverToolChainSelectionArtifact::DriverToolChainSelectionArtifact(
    DriverToolChainSelectionArtifact &&Other) noexcept
    : ToolChainID(std::move(Other.ToolChainID)),
      TargetKey(std::move(Other.TargetKey)),
      TargetTriple(std::move(Other.TargetTriple)), CPU(std::move(Other.CPU)),
      Features(std::move(Other.Features)), Provider(Other.Provider),
      BuiltinProviderUsed(Other.BuiltinProviderUsed) {
  rebuildFeatureViews();
}

DriverToolChainSelectionArtifact &DriverToolChainSelectionArtifact::operator=(
    DriverToolChainSelectionArtifact &&Other) noexcept {
  if (this == &Other)
    return *this;
  ToolChainID = std::move(Other.ToolChainID);
  TargetKey = std::move(Other.TargetKey);
  TargetTriple = std::move(Other.TargetTriple);
  CPU = std::move(Other.CPU);
  Features = std::move(Other.Features);
  Provider = Other.Provider;
  BuiltinProviderUsed = Other.BuiltinProviderUsed;
  rebuildFeatureViews();
  return *this;
}

void DriverToolChainSelectionArtifact::set(
    std::string ToolChainIDValue, std::string TargetKeyValue,
    std::string TargetTripleValue, std::string CPUValue,
    std::vector<std::string> FeatureValues,
    NevercToolChainProviderHandle ProviderValue,
    bool BuiltinProviderUsedValue) {
  ToolChainID = std::move(ToolChainIDValue);
  TargetKey = std::move(TargetKeyValue);
  TargetTriple = std::move(TargetTripleValue);
  CPU = std::move(CPUValue);
  Features = std::move(FeatureValues);
  Provider = ProviderValue;
  BuiltinProviderUsed = BuiltinProviderUsedValue;
  rebuildFeatureViews();
}

llvm::Error DriverToolChainSelectionArtifact::verify() const {
  if (llvm::Error E = validateText(ToolChainID, "toolchain ID", false))
    return E;
  if (llvm::Error E = validateText(TargetKey, "toolchain target key", false))
    return E;
  DriverToolChainRequestData Request;
  Request.RequestedTriple = TargetTriple;
  Request.ComputedTriple = TargetTriple;
  Request.CPU = CPU;
  Request.Features = Features;
  return verifyRequestData(Request);
}

void DriverToolChainSelectionArtifact::rebuildFeatureViews() {
  FeatureViews.clear();
  FeatureViews.reserve(Features.size());
  for (const std::string &Feature : Features)
    FeatureViews.push_back(asView(Feature));
}

NevercInterfaceID driverToolChainRequestArtifactID() {
  return {NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_INPUT_HIGH,
          NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_INPUT_LOW};
}

NevercInterfaceID driverToolChainSelectionArtifactID() {
  return {NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_OUTPUT_HIGH,
          NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_OUTPUT_LOW};
}

NevercInterfaceID driverSelectToolChainPhaseID() {
  return {NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_HIGH,
          NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_LOW};
}

llvm::Expected<DriverToolChainArtifactTypes> registerDriverToolChainArtifacts(
    plugin::PluginArtifactRegistry &Registry,
    DriverToolChainSelectionVerifier SelectionVerifier) {
  plugin::PluginArtifactTypeDescriptor RequestDescriptor;
  RequestDescriptor.ID = driverToolChainRequestArtifactID();
  RequestDescriptor.Name = "neverc.driver.toolchain_request";
  RequestDescriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  RequestDescriptor.Clone = [](const void *Payload) -> llvm::Expected<void *> {
    if (!Payload)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "cannot clone null toolchain request");
    auto *Copy = new (std::nothrow) DriverToolChainRequestArtifact(
        *static_cast<const DriverToolChainRequestArtifact *>(Payload));
    if (!Copy)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "unable to allocate toolchain request clone");
    return static_cast<void *>(Copy);
  };
  RequestDescriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverToolChainRequestArtifact *>(Payload);
  };
  RequestDescriptor.Verify = [](const void *Payload) {
    if (!Payload)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "toolchain request payload is null");
    return static_cast<const DriverToolChainRequestArtifact *>(Payload)
        ->verify();
  };
  auto RequestType = Registry.registerType(std::move(RequestDescriptor));
  if (!RequestType)
    return RequestType.takeError();

  plugin::PluginArtifactTypeDescriptor SelectionDescriptor;
  SelectionDescriptor.ID = driverToolChainSelectionArtifactID();
  SelectionDescriptor.Name = "neverc.driver.toolchain_selection";
  SelectionDescriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  SelectionDescriptor.Clone =
      [](const void *Payload) -> llvm::Expected<void *> {
    if (!Payload)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "cannot clone null toolchain selection");
    auto *Copy = new (std::nothrow) DriverToolChainSelectionArtifact(
        *static_cast<const DriverToolChainSelectionArtifact *>(Payload));
    if (!Copy)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "unable to allocate toolchain selection clone");
    return static_cast<void *>(Copy);
  };
  SelectionDescriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverToolChainSelectionArtifact *>(Payload);
  };
  SelectionDescriptor.Verify = [Verifier = std::move(SelectionVerifier)](
                                   const void *Payload) -> llvm::Error {
    if (!Payload)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "toolchain selection payload is null");
    const auto &Selection =
        *static_cast<const DriverToolChainSelectionArtifact *>(Payload);
    if (llvm::Error E = Selection.verify())
      return E;
    if (Verifier)
      return Verifier(Selection);
    return llvm::Error::success();
  };
  auto SelectionType = Registry.registerType(std::move(SelectionDescriptor));
  if (!SelectionType)
    return SelectionType.takeError();

  return DriverToolChainArtifactTypes{*RequestType, *SelectionType};
}

} // namespace neverc::driver
