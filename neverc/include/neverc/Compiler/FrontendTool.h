#ifndef NEVERC_FRONTEND_FRONTENDTOOL_H
#define NEVERC_FRONTEND_FRONTENDTOOL_H

#include "llvm/ADT/ArrayRef.h"
#include <memory>

namespace neverc {

namespace driver {
struct DirectInvocationOpts;
}

class CompilerInstance;
class FrontendAction;

std::unique_ptr<FrontendAction> CreateFrontendAction(CompilerInstance &CI);

bool ExecuteCompilerInvocation(CompilerInstance *CI);

/// In-process compilation entry used by the neverc driver only.  End users
/// invoke the neverc command-line driver; there is no separate frontend
/// binary or internal forwarding mechanism.  Argv matches the slice a
/// subprocess compiler would get; DirectOpts overlays option domains
/// ConstructJob already resolved.
///
/// \param Argv   Compiler arguments (without the executable path).
/// \param Argv0  Path to the driver executable.
/// \param MainAddr  Address inside the main executable, used for resource-dir
///                  lookup.
int ExecuteFrontendDirect(
    llvm::ArrayRef<const char *> Argv, const char *Argv0, void *MainAddr,
    const driver::DirectInvocationOpts *DirectOpts = nullptr);

} // end namespace neverc

#endif
