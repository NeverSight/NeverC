//===--- TestSign.h - Authenticode test signing for driver images ---------===//
//
// Attaches an Authenticode signature to a linked PE image so a kernel driver
// can load on a machine with test signing enabled.  Driven by `--test-sign`,
// which the driver only forwards for `-fms-kernel` builds.
//
// The signing identity is built into the compiler (see CoffTestSignKey.cpp):
// a self-signed certificate whose private key is public by construction.  It
// buys nothing but the ability to pass the code-integrity check on a machine
// that already trusts it; it is not a substitute for a real code-signing
// certificate and must not be used for anything shipped.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LINKER_COFF_TESTSIGN_H
#define NEVERC_LINKER_COFF_TESTSIGN_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstddef>
#include <cstdint>

namespace linker {
namespace coff {

/// Sign the PE image at \p path in place.
///
/// Computes the Authenticode digest of the image, wraps it in a PKCS#7
/// SignedData signed by the built-in test identity, appends the result as an
/// attribute certificate and points the certificate table at it.  The file
/// grows; the PE checksum is recomputed to match.
llvm::Error testSignImage(llvm::StringRef path);

namespace testsign {

// Built-in signing identity.  Defined in CoffTestSignKey.cpp.
extern const uint8_t CertificateDer[];
extern const size_t CertificateDerSize;
extern const uint8_t Modulus[];
extern const size_t ModulusSize;
extern const uint8_t PrivateExponent[];
extern const size_t PrivateExponentSize;
extern const unsigned PublicExponent;
extern const uint8_t IssuerDer[];
extern const size_t IssuerDerSize;
extern const uint8_t SerialNumber[];
extern const size_t SerialNumberSize;

} // namespace testsign
} // namespace coff
} // namespace linker

#endif // NEVERC_LINKER_COFF_TESTSIGN_H
