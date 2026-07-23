#ifndef NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODEFINALVERIFIER_H
#define NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODEFINALVERIFIER_H

// The dyncode final verifier (phase dyncode.verify, sealed host gate).
//
// This is the single sealed gate that runs after every byte-level transform and
// before output commit.  It composes the three domain verifiers into one
// checklist so no partial view can bless the image:
//
//   * the extraction plan verifier  -- entry-at-zero, symbol mapping bounds;
//   * the relocation verifier       -- no surviving external, sites/targets in
//                                      range, every fixup an intra-image form;
//   * the structural binary verifier -- size/alignment budget and no forbidden
//                                      byte anywhere in the final bytes.
//
// It records the checklist in the report and, on success, leaves the image in
// the Verified state.  Any failure is a structured error and the image is not
// verified.

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/Support/Error.h"

namespace neverc {
namespace dyncode {

/// Runs the composed final verifier over the plan/image pair.  ``Opts`` carries
/// the target and byte policy.  On success ``Image`` is left Verified.
llvm::Error verifyDynCodeFinalImage(const DynCodeExtractionPlan &Plan,
                                    DynCodeImage &Image, DynCodeReport &Report,
                                    const DynCodeOptions &Opts);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODEFINALVERIFIER_H
