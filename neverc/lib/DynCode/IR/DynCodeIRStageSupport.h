#ifndef NEVERC_LIB_DYNCODE_IR_DYNCODEIRSTAGESUPPORT_H
#define NEVERC_LIB_DYNCODE_IR_DYNCODEIRSTAGESUPPORT_H

// Volume 6 task 6: shared helpers for the split dyncode IR-stage passes
// (DynCodePreparePass / StackifyPass / DynCodeIRVerifier).  These were factored
// out of the monolithic ZeroRelocPass so the passes no longer key their
// behaviour off a "which run is this" named-metadata sentinel.  The only
// bookkeeping metadata that survives is the hard-error flag, which merely stops
// later IR-stage passes from cascading secondary diagnostics.

#include "llvm/ADT/StringRef.h"

namespace llvm {
class Function;
class Module;
class Twine;
} // namespace llvm

namespace neverc {
namespace dyncode {
namespace ir_stage {

/// True once a hard dyncode error has been reported for this module.
bool hadHardError(llvm::Module &M);

/// Emits a structured dyncode diagnostic and records the hard-error flag so
/// later IR-stage passes skip further work.  Compilation still fails through the
/// normal LLVM diagnostic handler; the flag only suppresses duplicate errors.
void reportError(llvm::Module &M, const llvm::Twine &Msg);

/// Clears the internal hard-error bookkeeping metadata so it never leaks into
/// later dyncode phases or the emitted object.
void clearHardError(llvm::Module &M);

/// Selects the single dyncode entry: the first defined function matching the
/// user entry (or a default entry name), else the first defined function.
llvm::Function *findEntry(llvm::Module &M, llvm::StringRef UserEntry);

/// Normalises the module for dyncode: rejects global ctors/dtors and
/// external-weak globals, demotes thread-local storage, strips llvm.used, makes
/// the entry external and every other function internal, and (when requested)
/// marks non-entry functions always-inline.  Returns true if it changed the IR.
bool prep(llvm::Module &M, llvm::Function *Entry, bool InlineAll);

} // namespace ir_stage
} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_IR_DYNCODEIRSTAGESUPPORT_H
