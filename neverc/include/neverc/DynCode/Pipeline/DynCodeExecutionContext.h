#ifndef NEVERC_DYNCODE_PIPELINE_DYNCODEEXECUTIONCONTEXT_H
#define NEVERC_DYNCODE_PIPELINE_DYNCODEEXECUTIONCONTEXT_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"

namespace neverc {
namespace dyncode {

/// Task-local dyncode execution state.
///
/// Replaces the former process-global mutable dyncode-options
/// singleton.  The frozen \ref DynCodeOptions
/// are owned by whichever task drives the dyncode compile -- the in-process cc1
/// ``CompilerInstance`` for codegen and the ``DynCodeJobAction`` callback for
/// extraction -- and are threaded explicitly through ``DirectInvocationOpts``
/// rather than read back from a mutable process global.  This keeps concurrent
/// invocations isolated: two parallel dyncode compiles with different targets,
/// entries or options never observe each other's request.
///
/// Later dyncode tasks extend this context with the typed phase state, proof
/// journal and cancel token; only the immutable request is needed here.
class DynCodeExecutionContext {
public:
  explicit DynCodeExecutionContext(DynCodeOptions Opts)
      : Options(std::move(Opts)) {}

  const DynCodeOptions &options() const { return Options; }
  bool enabled() const { return Options.Enabled; }

private:
  const DynCodeOptions Options;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_PIPELINE_DYNCODEEXECUTIONCONTEXT_H
