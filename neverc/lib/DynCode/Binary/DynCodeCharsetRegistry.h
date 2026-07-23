#ifndef NEVERC_LIB_DYNCODE_BINARY_DYNCODECHARSETREGISTRY_H
#define NEVERC_LIB_DYNCODE_BINARY_DYNCODECHARSETREGISTRY_H

// The dyncode charset encoder registry.
//
// A charset encoder rewrites the whole image into an alternate byte alphabet
// plus a self-decoding stub, so the emitted payload avoids the forbidden byte
// set entirely.  Unlike the rewrite chain, exactly one charset provider is
// selected, chosen by exact stable ID; an unknown or duplicate ID is a hard
// error rather than a silent pick.  The encoded payload (stub included) is
// still subject to the final bad-byte audit.

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

/// One charset encoder provider.  ``Encode`` rewrites ``Image`` through its
/// checked builder (emitting the decoder stub + encoded payload) and updates
/// the entry offset if the stub moves it.
struct DynCodeCharsetProvider {
  std::string ID;
  bool Deterministic = true;
  std::function<llvm::Error(DynCodeImage &, llvm::ArrayRef<uint8_t>)> Encode;
};

class DynCodeCharsetRegistry {
public:
  /// Registers an encoder.  A duplicate ID or a provider without a callback is
  /// a structured error.
  llvm::Error registerProvider(DynCodeCharsetProvider Provider);

  const DynCodeCharsetProvider *find(llvm::StringRef ID) const;

  /// Selects the encoder with exactly ``ID`` and runs it.  An unknown ID is a
  /// structured error.
  llvm::Error run(llvm::StringRef ID, DynCodeImage &Image,
                  llvm::ArrayRef<uint8_t> BadBytes) const;

private:
  std::vector<DynCodeCharsetProvider> Providers;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_BINARY_DYNCODECHARSETREGISTRY_H
