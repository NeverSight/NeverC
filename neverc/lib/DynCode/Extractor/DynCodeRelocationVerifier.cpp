// The dyncode relocation verifier: see DynCodeRelocationVerifier.h.

#include "Extractor/DynCodeRelocationVerifier.h"
#include "Extractor/DynCodeRelocationProvider.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

llvm::Error verifyDynCodeRelocations(const DynCodeExtractionPlan &Plan,
                                     const TargetDesc &Target,
                                     const DynCodeImage &Image) {
  const uint64_t Size = Image.size();

  for (const DynCodeExternalContract &C : Plan.externalContracts()) {
    if (C.Disposition == DynCodeExternalDisposition::Unresolved)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation verifier: external reference '%s' survived to "
          "the final image (must be eliminated, resolved in-image, or turned "
          "into a declared runtime contract)",
          C.Symbol.c_str());
  }

  for (const DynCodeRelocationEntry &E : Plan.relocations()) {
    if (E.Width == 0 || E.SiteOffset > Size || E.Width > Size - E.SiteOffset)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation verifier: site 0x%llx (width %u) is outside the "
          "image (size %llu)",
          (unsigned long long)E.SiteOffset, E.Width, (unsigned long long)Size);

    if (!E.Resolved)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation verifier: relocation at site 0x%llx was never "
          "resolved",
          (unsigned long long)E.SiteOffset);

    DynCodeRelocationMapping M = mapDynCodeRelocation(Target, E.NativeType);
    if (M.Class != DynCodeRelocationClass::IntraImage)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation verifier: relocation at site 0x%llx (native type "
          "%llu) is not an intra-image form",
          (unsigned long long)E.SiteOffset, (unsigned long long)E.NativeType);

    int64_t FinalAddr = static_cast<int64_t>(E.TargetOffset) + E.Addend +
                        M.AddendAdjust;
    if (FinalAddr < 0 || static_cast<uint64_t>(FinalAddr) > Size)
      return createStringError(
          errc::invalid_argument,
          "dyncode relocation verifier: relocation at site 0x%llx resolves to "
          "target address %lld outside the image (size %llu)",
          (unsigned long long)E.SiteOffset, (long long)FinalAddr,
          (unsigned long long)Size);
  }

  return Error::success();
}

} // namespace dyncode
} // namespace neverc
