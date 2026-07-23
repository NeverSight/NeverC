// Fuzz the plugin dependency range/version-matching primitives.
//
// The dependency graph builder rests on three host primitives: canonical plugin
// ID validation, SemVer range validation (empty/reversed detection with the
// prerelease precedence rules), and version matching between a dependency range
// and a candidate descriptor.  This fuzzer feeds them arbitrary IDs, versions,
// prerelease identifiers and range flags, checking they never crash and that a
// range validateDependencyRange accepts is self-consistent under matching.  No
// native plugin code is loaded; makePluginActivationPlan needs real modules and
// is exercised by the loader tests instead.

#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/PluginCore.h"
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

std::string takeText(ByteCursor &Input, size_t Maximum) {
  ArrayRef<uint8_t> Bytes = Input.takeBytes(Maximum);
  return std::string(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
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

OwnedPluginDependency takeDependency(ByteCursor &Input) {
  OwnedPluginDependency Dependency;
  if (Input.takeByte() & 1)
    Dependency.PluginID = "com.example.dep" + std::to_string(Input.takeByte());
  else
    Dependency.PluginID = takeText(Input, 24);
  Dependency.Version.MinimumInclusive = takeVersion(Input);
  Dependency.Version.MaximumExclusive = takeVersion(Input);
  Dependency.Version.HasMaximum =
      (Input.takeByte() & 1) ? NEVERC_TRUE : NEVERC_FALSE;
  Dependency.Version.AllowPrerelease =
      (Input.takeByte() & 1) ? NEVERC_TRUE : NEVERC_FALSE;
  Dependency.Version.Reserved = 0;
  Dependency.MinimumPrerelease = takeText(Input, 8);
  Dependency.MaximumPrerelease = takeText(Input, 8);
  Dependency.Kind = static_cast<NevercDependencyKind>(Input.takeU32());
  return Dependency;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);

  // Canonical-ID validation must tolerate arbitrary bytes.
  (void)isCanonicalPluginID(takeText(Input, 48));

  const unsigned Count = std::min<unsigned>(Input.takeByte(), 16);
  std::vector<OwnedPluginDependency> Deps;
  Deps.reserve(Count);
  for (unsigned I = 0; I != Count && !Input.empty(); ++I)
    Deps.push_back(takeDependency(Input));

  PluginDescriptorRecord Candidate;
  Candidate.PluginID = takeText(Input, 24);
  Candidate.Version = takeVersion(Input);
  Candidate.VersionPrerelease = takeText(Input, 8);

  for (const OwnedPluginDependency &Dependency : Deps) {
    bool RangeValid = true;
    if (Error E = validateDependencyRange(Dependency)) {
      consume(std::move(E));
      RangeValid = false;
    }
    (void)dependencyVersionMatches(Dependency, Candidate);

    // A pure-numeric candidate that exactly equals the inclusive minimum, with
    // no upper bound and prerelease allowed, must match any range that
    // validateDependencyRange accepts (comparison is reflexive here).
    if (RangeValid && Dependency.MinimumPrerelease.empty() &&
        Dependency.Version.HasMaximum != NEVERC_TRUE &&
        Dependency.Version.AllowPrerelease == NEVERC_TRUE) {
      PluginDescriptorRecord AtMinimum;
      AtMinimum.Version = Dependency.Version.MinimumInclusive;
      if (!dependencyVersionMatches(Dependency, AtMinimum))
        abort();
    }
  }
  return 0;
}
