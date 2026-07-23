#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEREGISTRY_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEREGISTRY_H

// Session-scoped dyncode registry, built-in target descriptor provider and the
// owned host mirror of the public NevercDynCodeTargetDescriptor. There is no
// process-global registry; each PluginSession owns one DynCodeRegistry seeded
// with the built-in target adapters.

#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginDynCode.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

inline bool idEqual(NevercInterfaceID A, NevercInterfaceID B) {
  return A.High == B.High && A.Low == B.Low;
}
inline bool targetIDEqual(NevercTargetID A, NevercTargetID B) {
  return A.High == B.High && A.Low == B.Low;
}
inline bool idNonzero(NevercInterfaceID A) { return A.High != 0 || A.Low != 0; }

/// Owned host mirror of NevercDynCodeTargetDescriptor. Owns all backing storage
/// so the view() borrows stay valid for the descriptor's lifetime.
struct OwnedDynCodeTargetDescriptor {
  plugin::OwnedTargetKey Target;
  NevercTargetID UnderlyingTargetID{};
  NevercDynCodeTargetID DynCodeTargetID{};
  NevercObjectFormatID ObjectFormat{};
  std::string CodeSectionRole;
  std::string CodeSectionName;
  uint64_t DefaultFragmentAlignment = 16;
  NevercDynCodeTargetFlags Flags = 0;
  NevercInterfaceID RelocationApplicatorID{};
  NevercInterfaceID UserImportStrategyID{};
  NevercInterfaceID KernelImportStrategyID{};
  NevercInterfaceID EntryABIID{};
  NevercDynCodePICFlags PICConstraints = 0;

  NevercDynCodeTargetDescriptor view() const;
};

/// Builds the read-only built-in dyncode target descriptor for a triple by
/// reusing the Volume 4 built-in TargetKey route. Fails for unsupported triples.
llvm::Expected<OwnedDynCodeTargetDescriptor>
buildBuiltinDynCodeTarget(llvm::StringRef Triple,
                          NevercDynCodeExecutionLevel Level);

/// Session-scoped dyncode target/provider registry. Applies Volume 4 style
/// conflict rules: no two descriptors may claim the same (TargetKey, object
/// format) pair, and no plugin-reported numeric priority disambiguation exists.
class DynCodeRegistry {
public:
  /// Seeds the registry with the eight built-in user + kernel dyncode targets.
  llvm::Error registerBuiltinTargets();

  /// Registers one dyncode target descriptor. Rejects zero IDs, invalid schema
  /// digests, duplicate dyncode target IDs and (TargetKey, format) conflicts.
  llvm::Error registerTarget(OwnedDynCodeTargetDescriptor Desc);

  const OwnedDynCodeTargetDescriptor *
  findTargetByDynCodeID(NevercDynCodeTargetID ID) const;
  const OwnedDynCodeTargetDescriptor *
  findTargetByKey(NevercTargetID TargetID, NevercObjectFormatID Format) const;

  llvm::ArrayRef<OwnedDynCodeTargetDescriptor> targets() const {
    return Targets;
  }

private:
  std::vector<OwnedDynCodeTargetDescriptor> Targets;
};

} // namespace dyncode
} // namespace neverc

#endif
