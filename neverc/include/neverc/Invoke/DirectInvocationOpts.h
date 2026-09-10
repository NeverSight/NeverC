#ifndef NEVERC_INVOKE_DIRECTINVOCATIONOPTS_H
#define NEVERC_INVOKE_DIRECTINVOCATIONOPTS_H

#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include <memory>

namespace neverc {

class HeaderIndexOptions;
class OutputCoordinator;
class PrepOptions;
class FrontendOptions;
namespace plugin {
class PluginSession;
}
namespace dyncode {
class DynCodeExecutionContext;
}

namespace driver {

struct DirectInvocationOpts {
  std::shared_ptr<TargetOptions> TargetOpts;
  std::shared_ptr<LangOptions> LangOpts;
  std::shared_ptr<CodeGenOptions> CodeGenOpts;
  std::shared_ptr<HeaderIndexOptions> HeaderIdxOpts;
  std::shared_ptr<PrepOptions> PPOpts;
  std::shared_ptr<FrontendOptions> FrontendOpts;
  std::shared_ptr<plugin::PluginSession> PluginSession;
  // Frozen dyncode request for the in-process cc1 codegen,
  // replacing the former process-global dyncode options singleton.
  std::shared_ptr<const dyncode::DynCodeExecutionContext> DynCode;
  OutputCoordinator *Outputs = nullptr;

  // True when this cc1 invocation is part of a compile+link pipeline where
  // the linker runs in the same process.  LTO bitcode output stays in
  // InMemoryFileStore instead of hitting the filesystem.
  bool InMemoryLTOOutput = false;

  // When true, an invocation that does not mutate LLVM command-line options
  // may share the process option gate with other parallel frontends. Any
  // mutating invocation still takes an exclusive restoring snapshot inside
  // ExecuteFrontendDirect. An ambient LLVM pass-timing request also forces a
  // plain exclusive lease because LLVM reuses named codegen timers globally;
  // that lease preserves the host's option values and occurrence metadata.
  // Embedders with unpublished TimerGroup records must keep
  // llvm::TimePassesIsEnabled true until they publish those records. Entering
  // with it false declares there are no ambient records to preserve and lets
  // an invocation that enables pass timing publish and clear the profile.
  // Callers must not reset LLVM options around the parallel region. Fatal
  // recovery is scoped to the invoking thread in this host image; it does not
  // propagate into plugin DSOs or asynchronous LLVM workers.
  bool ParallelSafe = false;
};

inline bool hasAnyDirectOpts(const DirectInvocationOpts &D) {
  return D.TargetOpts || D.LangOpts || D.CodeGenOpts || D.HeaderIdxOpts ||
         D.PPOpts || D.FrontendOpts || D.PluginSession || D.DynCode ||
         D.Outputs || D.InMemoryLTOOutput || D.ParallelSafe;
}

} // namespace driver
} // namespace neverc

#endif // NEVERC_INVOKE_DIRECTINVOCATIONOPTS_H
