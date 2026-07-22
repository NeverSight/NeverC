#ifndef NEVERC_DYNCODE_DRIVERINTEGRATION_H
#define NEVERC_DYNCODE_DRIVERINTEGRATION_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/SmallVector.h"
#include <set>
#include <string>

namespace neverc {
namespace dyncode {

// Result of parsing -fdyncode driver options once, up front.  When Enabled is
// set, the caller wires Driver::DynCodeEnabled / Driver::DynCodeMain so the
// dyncode image extraction runs as an ordinary in-process Action/Job instead
// of a post-`main()` step.
struct DynCodeDriverSetup {
  DynCodeOptions Opts;
  bool Enabled = false;
  // Backing storage for injected argv strings appended to Args.
  std::set<std::string> StringPool;
};

// Parse -fdyncode options from Args, validate the target, apply implicit
// lowering, and (when enabled) append the dyncode codegen inject flags to Args
// in place.  The user's -o and source inputs are left untouched: the driver
// builds source -> object -> dyncode(image).  Returns non-zero on a fatal
// option error (message already emitted to stderr).
int prepareDriverDynCode(llvm::SmallVectorImpl<const char *> &Args,
                         DynCodeDriverSetup &Setup);

} // namespace dyncode
} // namespace neverc

#endif
