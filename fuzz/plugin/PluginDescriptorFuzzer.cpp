// Fuzz the plugin descriptor validator.
//
// copyAndValidateDescriptor is exactly the normalization the loader runs on a
// caller-provided C descriptor: ABI header capacity/version negotiation,
// canonical-ID and UTF-8 checks, interface/dependency array shape and content
// validation, and SessionBegin/End + TaskBegin/End callback pairing.  This
// fuzzer builds a NevercPluginDescriptor (with interface and dependency arrays)
// from arbitrary bytes and drives the validator, checking it never crashes.  No
// native plugin code is loaded -- the callback slots are inert typed stubs the
// validator only inspects for presence.

#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/PluginCore.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

// Inert, correctly-typed callback stubs.  The validator only records whether the
// slot is non-null (and pairs them); it never invokes them.
NevercStatus NEVERC_CALL stubRegister(const NevercCoreAPI *,
                                      const NevercRegistrarAPI *, void *,
                                      void *) {
  return neverc_status_ok();
}
NevercStatus NEVERC_CALL stubSessionBegin(const NevercCoreAPI *,
                                          NevercSessionHandle, void *,
                                          void **) {
  return neverc_status_ok();
}
NevercStatus NEVERC_CALL stubSessionEnd(const NevercCoreAPI *,
                                        NevercSessionHandle, void *, void *) {
  return neverc_status_ok();
}
NevercStatus NEVERC_CALL stubTaskBegin(const NevercCoreAPI *, NevercTaskHandle,
                                       NevercTaskKind, void *, void *, void **) {
  return neverc_status_ok();
}
NevercStatus NEVERC_CALL stubTaskEnd(const NevercCoreAPI *, NevercTaskHandle,
                                     NevercTaskKind, void *, void *, void *) {
  return neverc_status_ok();
}
NevercStatus NEVERC_CALL stubDestroy(const NevercCoreAPI *, void *) {
  return neverc_status_ok();
}

std::string takeText(ByteCursor &Input, size_t Maximum) {
  ArrayRef<uint8_t> Bytes = Input.takeBytes(Maximum);
  return std::string(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
}

NevercStringView view(const std::string &Value) {
  return {Value.data(), Value.size()};
}

NevercSemanticVersion takeVersion(ByteCursor &Input) {
  NevercSemanticVersion Version{};
  Version.Major = Input.takeU32();
  Version.Minor = Input.takeU32();
  Version.Patch = Input.takeU32();
  Version.Reserved = 0;
  Version.Prerelease = {nullptr, 0};
  Version.BuildMetadata = {nullptr, 0};
  return Version;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);

  // Backing storage for every borrowed view; must outlive the validator call.
  std::string ID = takeText(Input, 40);
  if (Input.takeByte() & 1)
    ID = "com.example.p" + std::to_string(Input.takeByte());
  std::string Name = takeText(Input, 40);
  if (Name.empty())
    Name = "n";

  std::vector<std::string> DepIDs;
  const unsigned DepCount = std::min<unsigned>(Input.takeByte(), 8);
  DepIDs.reserve(DepCount);
  for (unsigned I = 0; I != DepCount; ++I) {
    if (Input.takeByte() & 1)
      DepIDs.push_back("com.example.dep" + std::to_string(Input.takeByte()));
    else
      DepIDs.push_back(takeText(Input, 20));
  }
  std::vector<NevercPluginDependency> Deps;
  Deps.reserve(DepCount);
  for (unsigned I = 0; I != DepCount; ++I) {
    NevercPluginDependency Dependency{};
    Dependency.Header = {sizeof(NevercPluginDependency), NEVERC_PLUGIN_ABI_MAJOR,
                         NEVERC_PLUGIN_ABI_MINOR, 0};
    Dependency.PluginID = view(DepIDs[I]);
    Dependency.Version.MinimumInclusive = takeVersion(Input);
    Dependency.Version.MaximumExclusive = takeVersion(Input);
    Dependency.Version.HasMaximum =
        (Input.takeByte() & 1) ? NEVERC_TRUE : NEVERC_FALSE;
    Dependency.Version.AllowPrerelease =
        (Input.takeByte() & 1) ? NEVERC_TRUE : NEVERC_FALSE;
    Dependency.Version.Reserved = 0;
    Dependency.Kind =
        static_cast<NevercDependencyKind>(1 + (Input.takeByte() % 3));
    Dependency.Reserved = 0;
    Deps.push_back(Dependency);
  }

  auto takeRequirements = [&](std::vector<NevercInterfaceRequirement> &Out,
                              bool Required) {
    const unsigned N = std::min<unsigned>(Input.takeByte(), 6);
    Out.reserve(N);
    for (unsigned I = 0; I != N; ++I) {
      NevercInterfaceRequirement Requirement{};
      Requirement.Header = {sizeof(NevercInterfaceRequirement),
                            NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
      Requirement.Interface = {Input.takeU64(), Input.takeU64()};
      Requirement.Major = static_cast<uint16_t>(Input.takeU32());
      Requirement.MinimumMinor = static_cast<uint16_t>(Input.takeU32());
      Requirement.Required = Required ? NEVERC_TRUE : NEVERC_FALSE;
      Requirement.Stability = (Input.takeByte() & 1) ? NEVERC_INTERFACE_LOCKSTEP
                                                     : NEVERC_INTERFACE_STABLE;
      Requirement.Compatibility.Header = {sizeof(NevercCompatibilityKey),
                                          NEVERC_PLUGIN_ABI_MAJOR,
                                          NEVERC_PLUGIN_ABI_MINOR, 0};
      Requirement.Compatibility.ProducerBuildID = {nullptr, 0};
      Requirement.Compatibility.TargetABIKey = {nullptr, 0};
      Requirement.Compatibility.LLVMMajor = Input.takeU32();
      Requirement.Compatibility.Reserved = 0;
      Out.push_back(Requirement);
    }
  };
  std::vector<NevercInterfaceRequirement> Required, Optional;
  takeRequirements(Required, true);
  takeRequirements(Optional, false);

  const uint8_t CallbackBits = Input.takeByte();

  NevercPluginDescriptor Descriptor{};
  Descriptor.Header = {sizeof(NevercPluginDescriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = view(ID);
  Descriptor.DisplayName = view(Name);
  Descriptor.Version = takeVersion(Input);
  Descriptor.Concurrency = static_cast<NevercConcurrencyModel>(Input.takeU32());
  Descriptor.Reentrancy = static_cast<NevercReentrancyModel>(Input.takeU32());
  Descriptor.RequiredInterfaces = {Required.data(), Required.size(),
                                   sizeof(NevercInterfaceRequirement)};
  Descriptor.OptionalInterfaces = {Optional.data(), Optional.size(),
                                   sizeof(NevercInterfaceRequirement)};
  Descriptor.Dependencies = {Deps.data(), Deps.size(),
                             sizeof(NevercPluginDependency)};
  Descriptor.ProcessBegin = nullptr;
  Descriptor.Register = (CallbackBits & 1) ? stubRegister : nullptr;
  Descriptor.SessionBegin = (CallbackBits & 2) ? stubSessionBegin : nullptr;
  Descriptor.SessionEnd = (CallbackBits & 4) ? stubSessionEnd : nullptr;
  Descriptor.TaskBegin = (CallbackBits & 8) ? stubTaskBegin : nullptr;
  Descriptor.TaskEnd = (CallbackBits & 16) ? stubTaskEnd : nullptr;
  Descriptor.Destroy = (CallbackBits & 32) ? stubDestroy : nullptr;

  // Occasionally truncate the advertised StructSize to exercise the required
  // prefix / capacity negotiation.
  if (CallbackBits & 64)
    Descriptor.Header.StructSize = Input.takeU32();

  auto Result = copyAndValidateDescriptor(Descriptor, LLVM_VERSION_MAJOR);
  if (!Result)
    consume(Result.takeError());
  return 0;
}
