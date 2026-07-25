#ifndef NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODEOBJECTPIPELINE_H
#define NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODEOBJECTPIPELINE_H

// The format-agnostic dyncode extraction pipeline.
//
// This is the single entry the driver's DynCodeJobAction runs after codegen has
// produced a relocatable object.  It replaces the old per-format extractDynCode
// dispatch (extractELF / extractCOFF / extractMachO) with one path that:
//
//   1. reads the relocatable object bytes into an ObjectGraph through
//      the built-in LLVM object Reader (no second on-disk parse, no format
//      switch);
//   2. runs the ObjectGraphExtractor planner (section/symbol/relocation plan +
//      entry-first candidate image);
//   3. applies the intra-image relocations through the shared relocation
//      executor;
//   4. runs the bounded binary phases (bad-byte rewrite, charset encode,
//      size/alignment/padding);
//   5. runs the sealed final verifier;
//
// and returns the verified image plus its audit report.  A plugin object format
// that can be read into an ObjectGraph (and has matching relocation/target
// providers) flows through exactly the same path.

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace neverc {
namespace dyncode {

/// The verified image and its audit report produced from one relocatable
/// object.
struct DynCodeExtractedOutput {
  DynCodeImage Image;
  DynCodeReport Report;
};

/// Runs the full ObjectGraph -> verified DynCodeImage pipeline over the
/// relocatable object ``ObjectBytes`` (identified by ``LogicalPath`` for
/// diagnostics) using the frozen request ``Opts``.  On success the returned
/// image is Verified and the report is frozen.  Any error (unsupported target,
/// unreadable object, ambiguous/missing entry, unresolved external, forbidden
/// byte, over-budget size, ...) is a structured error.
llvm::Expected<DynCodeExtractedOutput>
runDynCodeExtractionPipeline(llvm::ArrayRef<uint8_t> ObjectBytes,
                             llvm::StringRef LogicalPath,
                             const DynCodeOptions &Opts);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODEOBJECTPIPELINE_H
