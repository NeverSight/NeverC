#ifndef NEVERC_LIB_DYNCODE_BINARY_DYNCODEREWRITEREGISTRY_H
#define NEVERC_LIB_DYNCODE_BINARY_DYNCODEREWRITEREGISTRY_H

// The dyncode bad-byte rewrite registry.
//
// A rewrite provider reads the candidate image through the bounded builder and
// edits it in place to remove forbidden bytes, respecting a declared growth
// budget.  Providers form a chain ordered by explicit before/after constraints;
// an ordering cycle is a hard error, never an arbitrary fallback order.  The
// chain runs before the charset encoder and the final bad-byte audit, so a
// provider that fails to remove a bad byte is still caught by the sealed
// verifier -- the rewrite is an opportunity, not a bypass.

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

/// One bad-byte rewrite provider.  ``Rewrite`` edits ``Image`` through its
/// checked builder and returns the number of bytes it changed (or an error).
struct DynCodeRewriteProvider {
  std::string ID;
  bool Deterministic = true;
  bool Repeatable = false;
  /// Maximum number of bytes the image may grow while this provider runs; 0
  /// means the provider may not grow the image at all.
  uint64_t MaxGrowth = 0;
  /// This provider must run before every listed provider ID.
  std::vector<std::string> Before;
  /// This provider must run after every listed provider ID.
  std::vector<std::string> After;
  std::function<llvm::Expected<uint64_t>(DynCodeImage &,
                                         llvm::ArrayRef<uint8_t>)>
      Rewrite;
};

class DynCodeRewriteRegistry {
public:
  /// Registers a provider.  A duplicate ID or a provider without a callback is
  /// a structured error.
  llvm::Error registerProvider(DynCodeRewriteProvider Provider);

  bool empty() const { return Providers.empty(); }
  size_t size() const { return Providers.size(); }

  /// Runs the applicable providers over ``Image`` in a stable topological order
  /// derived from their before/after constraints (ties broken by registration
  /// order).  Fails on an ordering cycle, a provider that grows the image past
  /// its declared budget, or any provider error.  ``OutChanges`` accumulates
  /// the total number of bytes changed.
  llvm::Error runChain(DynCodeImage &Image, llvm::ArrayRef<uint8_t> BadBytes,
                       uint64_t &OutChanges) const;

private:
  std::vector<DynCodeRewriteProvider> Providers;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_BINARY_DYNCODEREWRITEREGISTRY_H
