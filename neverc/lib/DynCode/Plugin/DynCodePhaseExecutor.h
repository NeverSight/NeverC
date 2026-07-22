#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPHASEEXECUTOR_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPHASEEXECUTOR_H

// Task-local dyncode phase pipeline (Volume 6 task 5).
//
// Drives the 34-phase dyncode chain over the generic PluginPhaseExecutor: each
// ordinary transition runs pre-observer -> interceptor -> selected/default
// provider -> host verifier/proof -> post-observer -> atomic publish; the four
// sealed gates (ir.final_verify, mir.final_verify, verify, commit) run a
// host-only verifier and fire their read-only observers, and the core executor
// rejects any Provider/Interceptor/SKIP against them by policy.
//
// Task 5 carries an opaque pipeline value through the chain; later tasks attach
// the real IR module / MIR / ObjectGraph / DynCodeImage behind each artifact.

#include "DynCodePhaseRegistry.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace neverc {
namespace dyncode {

/// Opaque value threaded through the dyncode phase chain.  Its ``Type`` tracks
/// the current artifact in the linear chain and ``Generation`` bumps on every
/// republish so stale artifact/proof handles are detectable.
class DynCodePipelineValue {
public:
  DynCodePipelineValue(NevercInterfaceID Type, uint64_t Generation)
      : Type(Type), Generation(Generation) {}

  NevercInterfaceID type() const { return Type; }
  uint64_t generation() const { return Generation; }
  void setType(NevercInterfaceID Value) { Type = Value; }
  void bumpGeneration() { ++Generation; }

private:
  NevercInterfaceID Type{};
  uint64_t Generation = 1;
};

class DynCodePhasePipeline {
public:
  static llvm::Expected<std::unique_ptr<DynCodePhasePipeline>>
  create(plugin::PluginTaskContext &Task);
  ~DynCodePhasePipeline();

  DynCodePhasePipeline(const DynCodePhasePipeline &) = delete;
  DynCodePhasePipeline &operator=(const DynCodePhasePipeline &) = delete;

  llvm::Error addObserver(llvm::StringRef PluginID,
                          const NevercObserverDescriptor &Descriptor);
  llvm::Error addInterceptor(llvm::StringRef PluginID,
                             const NevercInterceptorDescriptor &Descriptor);
  llvm::Error addProvider(llvm::StringRef PluginID,
                          const NevercProviderDescriptor &Descriptor);
  llvm::Error selectProvider(NevercInterfaceID Phase, llvm::StringRef PluginID);
  llvm::Error
  setBuiltinProvider(NevercInterfaceID Phase,
                     plugin::PluginPhaseExecutor::BuiltinProvider Provider);
  llvm::Error freeze();

  /// Runs the chain from ``Input`` through (and including) ``ThroughPhase``.
  /// The default runs the full pipeline to ``dyncode.commit``.
  llvm::Expected<std::shared_ptr<DynCodePipelineValue>>
  execute(std::shared_ptr<DynCodePipelineValue> Input,
          NevercInterfaceID ThroughPhase = {});

  const DynCodePhaseRegistry &registry() const;
  uint32_t rerunCount(NevercInterfaceID Phase) const;

private:
  struct Impl;
  explicit DynCodePhasePipeline(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

} // namespace dyncode
} // namespace neverc

#endif
