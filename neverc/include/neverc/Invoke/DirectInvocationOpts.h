#ifndef NEVERC_DRIVER_DIRECTINVOCATIONOPTS_H
#define NEVERC_DRIVER_DIRECTINVOCATIONOPTS_H

#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include <memory>

namespace neverc {

class HeaderIndexOptions;
class PrepOptions;
class FrontendOptions;

namespace driver {

struct DirectInvocationOpts {
  std::shared_ptr<TargetOptions> TargetOpts;
  std::shared_ptr<LangOptions> LangOpts;
  std::shared_ptr<CodeGenOptions> CodeGenOpts;
  std::shared_ptr<HeaderIndexOptions> HeaderIdxOpts;
  std::shared_ptr<PrepOptions> PPOpts;
  std::shared_ptr<FrontendOptions> FrontendOpts;

  // True when this cc1 invocation is part of a compile+link pipeline where
  // the linker runs in the same process.  LTO bitcode output stays in
  // InMemoryFileStore instead of hitting the filesystem.
  bool InMemoryLTOOutput = false;

  // When true, ExecuteFrontendDirect skips LLVM global-state operations
  // (cl::Reset, fatal-error-handler, TimerGroup) that are not thread-safe.
  // The caller is responsible for handling those before/after the
  // parallel region.
  bool ParallelSafe = false;
};

inline bool hasAnyDirectOpts(const DirectInvocationOpts &D) {
  return D.TargetOpts || D.LangOpts || D.CodeGenOpts || D.HeaderIdxOpts ||
         D.PPOpts || D.FrontendOpts;
}

} // namespace driver
} // namespace neverc

#endif // NEVERC_DRIVER_DIRECTINVOCATIONOPTS_H
