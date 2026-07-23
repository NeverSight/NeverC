#ifndef NEVERC_DYNCODE_MIRPREPPASS_H
#define NEVERC_DYNCODE_MIRPREPPASS_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"

namespace llvm {
class FunctionPass;
}

namespace neverc {
namespace dyncode {

// The dyncode MIR stage is split into a replaceable transform
// and a sealed final verifier.  Both passes only hold immutable target/request
// data captured from the frozen DynCodeOptions; neither reads process-global
// current options.
//
//   * neverc.dyncode.mir.prepare      -- transform, run at the PreEmit hook,
//                                        OBSERVABLE | INTERCEPTABLE | REPLACEABLE
//   * neverc.dyncode.mir.final_verify -- sealed gate, run at the Final hook
//                                        (immediately before AsmPrinter),
//                                        OBSERVABLE | SEALED_HOST_GATE

/// Builtin provider for the mir.prepare transition: strips dyncode/SEH pseudos
/// and applies the target-specific constant-pool rewrites.
llvm::FunctionPass *createDynCodeMIRTransformPass(const DynCodeOptions &Opts);

/// Sealed final-MIR verifier gate: rejects any forbidden pseudo that survived
/// the transform and audits external references / constant-pool residue.  Never
/// mutates the MIR.
llvm::FunctionPass *createDynCodeMIRVerifierPass(const DynCodeOptions &Opts);

} // namespace dyncode
} // namespace neverc

#endif
