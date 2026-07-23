// The dyncode final verifier: see DynCodeFinalVerifier.h.

#include "Extractor/DynCodeFinalVerifier.h"
#include "Binary/BuiltinDynCodeBinaryVerifier.h"
#include "Extractor/DynCodeRelocationVerifier.h"
#include "Extractor/ObjectGraphExtractor.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

llvm::Error verifyDynCodeFinalImage(const DynCodeExtractionPlan &Plan,
                                    DynCodeImage &Image, DynCodeReport &Report,
                                    const DynCodeOptions &Opts) {
  auto check = [&](const char *Name, llvm::Error E) -> llvm::Error {
    if (E)
      return E;
    return Report.addRecord({33, "builtin.final_verifier", Name, "ok"});
  };

  if (llvm::Error E =
          check("plan", verifyDynCodeExtractionPlan(Plan, Image, Opts)))
    return E;
  if (llvm::Error E = check(
          "relocations", verifyDynCodeRelocations(Plan, Opts.Target, Image)))
    return E;
  if (llvm::Error E = check("binary", verifyDynCodeBinary(Image, Opts)))
    return E;

  // The final verifier is the sealed gate that blesses the image.  It is
  // idempotent with respect to an image the binary phase executor already
  // marked verified; a committed/aborted image is a programming error.
  switch (Image.state()) {
  case DynCodeImageState::Candidate:
    return Image.markVerified();
  case DynCodeImageState::Verified:
    return Error::success();
  default:
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode final verifier: image is not a verifiable candidate");
  }
}

} // namespace dyncode
} // namespace neverc
