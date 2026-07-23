#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEIMPORTREGISTRY_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEIMPORTREGISTRY_H

// Session-scoped registry of dyncode ImportProviders.  Each
// external reference in the ledger is offered to the providers whose declared
// symbol matcher, target and execution level accept it.  Resolution is by host
// matcher specificity (an exact symbol name beats a "*" wildcard); when two
// providers tie at the highest specificity for the same symbol the host reports
// a conflict at route freeze rather than letting load order or a plugin-reported
// numeric priority silently win -- the same rule the target route registry uses.

#include "DynCodeRegistry.h" // idEqual / idNonzero / targetIDEqual helpers
#include "neverc/Plugin/PluginDynCode.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

/// Owned host mirror of NevercDynCodeImportProviderDescriptor.  Owns its backing
/// storage so borrowed views stay valid for the provider's lifetime.
struct OwnedDynCodeImportProvider {
  NevercInterfaceID ProviderID{};
  std::string CanonicalName;
  NevercDynCodeImportKind Kind = 0;
  /// Zero (0,0) matches any target; otherwise the underlying built-in TargetID.
  NevercTargetID TargetID{};
  NevercDynCodeExecutionLevel Level = NEVERC_DYNCODE_LEVEL_USER;
  /// When true the provider accepts both user and kernel level.
  bool AnyLevel = false;
  /// Exact symbol names, or the single-element "*" wildcard catch-all.
  std::vector<std::string> SymbolMatchers;

  /// Returns the match specificity for Symbol: 2 for an exact name match, 1 for
  /// a "*" wildcard, 0 for no match.
  unsigned matchSpecificity(llvm::StringRef Symbol) const;
  bool acceptsTarget(NevercTargetID Target) const;
  bool acceptsLevel(NevercDynCodeExecutionLevel L) const;
};

enum class ImportResolveStatus : uint8_t {
  Resolved = 0, // exactly one highest-specificity provider matched.
  NoProvider,   // no provider accepts this symbol/target/level.
  Conflict,     // two or more providers tie at the highest specificity.
};

struct ImportResolution {
  ImportResolveStatus Status = ImportResolveStatus::NoProvider;
  const OwnedDynCodeImportProvider *Provider = nullptr; // set when Resolved.
  NevercInterfaceID ConflictA{}; // the two tying providers when Conflict.
  NevercInterfaceID ConflictB{};
};

class DynCodeImportRegistry {
public:
  /// Registers one import provider.  Rejects a zero ProviderID, an out-of-range
  /// Kind, empty matchers, a "*" wildcard mixed with named matchers, and a
  /// duplicate ProviderID.
  llvm::Error registerImportProvider(OwnedDynCodeImportProvider Provider);

  /// Resolves the provider for Symbol under Target/Level, applying the
  /// specificity + conflict rules described above.
  ImportResolution resolve(llvm::StringRef Symbol, NevercTargetID Target,
                           NevercDynCodeExecutionLevel Level) const;

  llvm::ArrayRef<OwnedDynCodeImportProvider> providers() const {
    return Providers;
  }
  bool empty() const { return Providers.empty(); }

private:
  std::vector<OwnedDynCodeImportProvider> Providers;
};

} // namespace dyncode
} // namespace neverc

#endif
