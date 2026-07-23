#ifndef NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONVERIFIER_H
#define NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONVERIFIER_H

// The dyncode relocation verifier.
//
// After the relocation executor has applied the worklist, this verifier
// confirms the invariants the flat PIC image relies on: no external reference
// survived, every relocation was resolved to an intra-image target of a form
// this target can encode, and every site/target/width stays inside the image.
// It is the shared gate that a plugin replacement of the extract/relocate
// transitions must also satisfy before the image is accepted.

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "llvm/Support/Error.h"

namespace neverc {
namespace dyncode {

/// Verifies the post-relocation plan/image pair.  Fails (structured error) on a
/// surviving unresolved external, an unresolved / external / unsupported
/// relocation, or a site/target that runs outside the image.
llvm::Error verifyDynCodeRelocations(const DynCodeExtractionPlan &Plan,
                                     const TargetDesc &Target,
                                     const DynCodeImage &Image);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONVERIFIER_H
