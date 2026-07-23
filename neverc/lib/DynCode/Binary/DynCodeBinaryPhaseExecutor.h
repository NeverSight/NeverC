#ifndef NEVERC_LIB_DYNCODE_BINARY_DYNCODEBINARYPHASEEXECUTOR_H
#define NEVERC_LIB_DYNCODE_BINARY_DYNCODEBINARYPHASEEXECUTOR_H

// The dyncode binary phase executor.
//
// It runs the fixed byte-level phase order that follows code extraction:
//
//   image built
//   -> bad-byte rewrite providers (chain, when enabled)
//   -> selected charset encoder (when a provider is requested)
//   -> size / alignment / padding
//   -> final structural verifier (sealed)
//   -> mark verified
//
// Every mutation goes through the bounded DynCodeImage builder, so there is no
// raw pointer/length path.  Disabling the rewrite (-fno-dyncode-bad-byte-
// rewrite) runs an explicit no-op step -- the topology never changes -- and the
// final audit still runs, so a disabled rewrite cannot smuggle a bad byte past
// the verifier.

#include "Binary/DynCodeCharsetRegistry.h"
#include "Binary/DynCodeRewriteRegistry.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/Support/Error.h"

namespace neverc {
namespace dyncode {

/// Runs the fixed binary phase order over ``Image`` and records per-phase
/// outcomes in ``Report``.  On success the image is left in the Verified state.
/// Any provider error, over-budget growth, unknown charset, size overflow, or
/// forbidden final byte is a structured error and the image is not verified.
llvm::Error runDynCodeBinaryPhases(DynCodeImage &Image, DynCodeReport &Report,
                                   const DynCodeOptions &Opts,
                                   const DynCodeRewriteRegistry &Rewrites,
                                   const DynCodeCharsetRegistry &Charsets);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_BINARY_DYNCODEBINARYPHASEEXECUTOR_H
