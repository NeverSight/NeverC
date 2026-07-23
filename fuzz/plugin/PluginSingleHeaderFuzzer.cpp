// Fuzz the single-header ABI capacity/version negotiation.
//
// Every extensible public struct begins with a NevercABITableHeader whose
// StructSize is a capacity-in / full-size-out field, and each array view carries
// an explicit element stride.  The host must accept a well-known prefix, ignore
// an unknown tail, and reject a short/inconsistent prefix -- without ever reading
// past what StructSize/stride declare.  This fuzzer drives copyAndValidateDescriptor
// with adversarial header StructSize/Major/Minor/Flags at both the descriptor and
// element level, keeping array backing buffers exactly stride-sized so ASan flags
// any over-read.  No native plugin code is loaded.

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

NevercStatus NEVERC_CALL stubRegister(const NevercCoreAPI *,
                                      const NevercRegistrarAPI *, void *,
                                      void *) {
  return neverc_status_ok();
}

// Builds an ABI header whose StructSize is usually the true size but is
// occasionally truncated or inflated to exercise the capacity negotiation.
NevercABITableHeader takeHeader(ByteCursor &Input, uint32_t TrueSize) {
  NevercABITableHeader Header{};
  switch (Input.takeByte() % 4) {
  case 0:
    Header.StructSize = TrueSize;
    break;
  case 1:
    Header.StructSize = Input.takeU32() % (TrueSize == 0 ? 1 : TrueSize);
    break;
  case 2:
    Header.StructSize = TrueSize + (Input.takeU32() % 4096);
    break;
  default:
    Header.StructSize = Input.takeU32();
    break;
  }
  // Usually the negotiated major so validation reaches deeper fields.
  Header.Major = (Input.takeByte() & 3) ? NEVERC_PLUGIN_ABI_MAJOR
                                        : static_cast<uint16_t>(Input.takeU32());
  Header.Minor = static_cast<uint16_t>(Input.takeU32());
  Header.Flags = (Input.takeByte() & 7) ? 0 : Input.takeU64();
  return Header;
}

NevercStringView view(const std::string &Value) {
  return {Value.data(), Value.size()};
}

NevercSemanticVersion zeroVersion() {
  NevercSemanticVersion Version{};
  Version.Prerelease = {nullptr, 0};
  Version.BuildMetadata = {nullptr, 0};
  return Version;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);

  std::string ID = "com.example.singleheader";
  std::string Name = "single-header";

  // Dependency elements: backing buffer is exactly stride-sized so any read past
  // a truncated element header would be caught by ASan.
  std::vector<std::string> DepIDs;
  std::vector<NevercPluginDependency> Deps;
  const unsigned DepCount = std::min<unsigned>(Input.takeByte(), 4);
  DepIDs.reserve(DepCount);
  Deps.reserve(DepCount);
  for (unsigned I = 0; I != DepCount; ++I)
    DepIDs.push_back("com.example.dep" + std::to_string(I));
  for (unsigned I = 0; I != DepCount; ++I) {
    NevercPluginDependency Dependency{};
    Dependency.Header = takeHeader(Input, sizeof(NevercPluginDependency));
    Dependency.PluginID = view(DepIDs[I]);
    Dependency.Version.MinimumInclusive = zeroVersion();
    Dependency.Version.MaximumExclusive = zeroVersion();
    Dependency.Version.HasMaximum = NEVERC_FALSE;
    Dependency.Version.AllowPrerelease = NEVERC_TRUE;
    Dependency.Version.Reserved = 0;
    Dependency.Kind = NEVERC_DEPENDENCY_REQUIRED;
    Dependency.Reserved = 0;
    Deps.push_back(Dependency);
  }

  std::vector<NevercInterfaceRequirement> Reqs;
  const unsigned ReqCount = std::min<unsigned>(Input.takeByte(), 4);
  Reqs.reserve(ReqCount);
  for (unsigned I = 0; I != ReqCount; ++I) {
    NevercInterfaceRequirement Requirement{};
    Requirement.Header = takeHeader(Input, sizeof(NevercInterfaceRequirement));
    Requirement.Interface = {Input.takeU64(), Input.takeU64()};
    Requirement.Major = static_cast<uint16_t>(Input.takeU32());
    Requirement.MinimumMinor = static_cast<uint16_t>(Input.takeU32());
    Requirement.Required = NEVERC_TRUE;
    Requirement.Stability = NEVERC_INTERFACE_STABLE;
    Requirement.Compatibility.Header =
        takeHeader(Input, sizeof(NevercCompatibilityKey));
    Requirement.Compatibility.ProducerBuildID = {nullptr, 0};
    Requirement.Compatibility.TargetABIKey = {nullptr, 0};
    Requirement.Compatibility.LLVMMajor = LLVM_VERSION_MAJOR;
    Requirement.Compatibility.Reserved = 0;
    Reqs.push_back(Requirement);
  }

  // The array views advertise the true element stride; the buffers above are
  // exactly Count*stride bytes.
  NevercPluginDescriptor Descriptor{};
  Descriptor.Header = takeHeader(Input, sizeof(NevercPluginDescriptor));
  Descriptor.PluginID = view(ID);
  Descriptor.DisplayName = view(Name);
  Descriptor.Version = zeroVersion();
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.RequiredInterfaces = {Reqs.data(), Reqs.size(),
                                   sizeof(NevercInterfaceRequirement)};
  Descriptor.OptionalInterfaces = {nullptr, 0,
                                   sizeof(NevercInterfaceRequirement)};
  Descriptor.Dependencies = {Deps.data(), Deps.size(),
                             sizeof(NevercPluginDependency)};
  Descriptor.ProcessBegin = nullptr;
  Descriptor.Register = stubRegister;
  Descriptor.SessionBegin = nullptr;
  Descriptor.SessionEnd = nullptr;
  Descriptor.TaskBegin = nullptr;
  Descriptor.TaskEnd = nullptr;
  Descriptor.Destroy = nullptr;

  auto Result = copyAndValidateDescriptor(Descriptor, LLVM_VERSION_MAJOR);
  if (!Result)
    consume(Result.takeError());
  return 0;
}
