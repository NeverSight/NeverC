#ifndef NEVERC_PLUGIN_HOST_PLUGINPHASEEXECUTOR_H
#define NEVERC_PLUGIN_HOST_PLUGINPHASEEXECUTOR_H

#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginSession;
class PluginTaskContext;

class PluginPhaseExecutor {
public:
  using BuiltinProvider = std::function<NevercStatus(const NevercPhaseFrame *,
                                                     NevercPhaseResult *)>;
  using ProofVerifier = std::function<llvm::Error(
      PluginSession &, PluginTaskContext &, const NevercPhaseFrame &,
      NevercArtifactHandle, NevercProofHandle)>;

  PluginPhaseExecutor(const PluginPhaseGraph &Graph,
                      const PluginArtifactRegistry &Artifacts);
  ~PluginPhaseExecutor();

  PluginPhaseExecutor(const PluginPhaseExecutor &) = delete;
  PluginPhaseExecutor &operator=(const PluginPhaseExecutor &) = delete;

  llvm::Error importSessionRegistrations(const PluginSession &Session);
  llvm::Error addObserver(llvm::StringRef PluginID,
                          const NevercObserverDescriptor &Descriptor);
  llvm::Error addInterceptor(llvm::StringRef PluginID,
                             const NevercInterceptorDescriptor &Descriptor);
  llvm::Error addProvider(llvm::StringRef PluginID,
                          const NevercProviderDescriptor &Descriptor);
  llvm::Error setBuiltinProvider(NevercInterfaceID Phase,
                                 BuiltinProvider Provider);
  llvm::Error selectProvider(NevercInterfaceID Phase, llvm::StringRef PluginID);
  llvm::Error enableRecoverableBuiltinFallback(NevercInterfaceID Phase);
  llvm::Error freeze();
  bool isFrozen() const;
  bool hasBindings(NevercInterfaceID Phase) const;
  bool hasInterceptors(NevercInterfaceID Phase) const;
  bool hasProvider(NevercInterfaceID Phase) const;
  std::optional<uint64_t>
  currentArtifactMutationCapability(const PluginTaskContext &Task) const;
  bool validatesArtifactMutationCapability(const PluginTaskContext &Task,
                                           uint64_t Token) const;
  std::vector<std::string> fallbackProvenance() const;
  llvm::Error setProofVerifier(ProofVerifier Verifier);

  llvm::Expected<NevercArtifactHandle> createCandidate(PluginTaskContext &Task,
                                                       NevercInterfaceID Type,
                                                       void *Payload);
  llvm::Expected<NevercArtifactHandle>
  createArtifactView(PluginTaskContext &Task, NevercInterfaceID Type,
                     const void *Payload, uint64_t Generation);
  NevercStatus resolveArtifactPayload(PluginTaskContext &Task,
                                      NevercArtifactHandle Artifact,
                                      NevercInterfaceID ExpectedType,
                                      const void **OutPayload);
  bool isActiveContinuation(const NevercPhaseFrame *Frame,
                            const NevercPhaseContinuation *Continuation);
  llvm::Expected<NevercProofHandle>
  createEquivalenceProof(PluginTaskContext &Task, NevercInterfaceID Phase,
                         NevercArtifactHandle Input,
                         const PluginArtifactSlot &OutputSlot,
                         const NevercPhaseRoute &Route);

  llvm::Error execute(PluginSession &Session, PluginTaskContext &Task,
                      NevercInterfaceID Phase, const NevercPhaseRoute &Route,
                      NevercArtifactHandle Input,
                      PluginArtifactSlot &OutputSlot);
  llvm::Error notify(PluginSession &Session, PluginTaskContext &Task,
                     NevercInterfaceID Phase, const NevercPhaseRoute &Route,
                     NevercArtifactHandle Artifact);

  // Internal storage types are public only so the out-of-line runtime helpers
  // can operate without exposing their fields in this header.
  struct ObserverBinding;
  struct InterceptorBinding;
  struct ProviderBinding;
  struct BuiltinBinding;
  struct Selection;
  struct FallbackSelection;
  struct CandidateState;
  struct ProofState;
  struct ChainContext;
  struct ContinuationContext;

private:
  llvm::Error validatePhaseRegistration(NevercInterfaceID Phase,
                                        NevercPhasePolicy RequiredPolicy,
                                        llvm::StringRef Kind) const;
  NevercStatus invokeChain(ChainContext &Context, size_t Index,
                           NevercPhaseResult &OutResult);
  NevercStatus invokeProvider(ChainContext &Context,
                              NevercPhaseResult &OutResult);
  static NevercStatus NEVERC_CALL
  invokeNext(NevercPhaseContinuation *Continuation,
             const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult);

  const PluginPhaseGraph &Graph;
  const PluginArtifactRegistry &Artifacts;
  std::vector<ObserverBinding> Observers;
  std::vector<InterceptorBinding> Interceptors;
  std::vector<ProviderBinding> Providers;
  std::vector<BuiltinBinding> Builtins;
  std::vector<Selection> Selections;
  std::vector<FallbackSelection> FallbackSelections;
  ProofVerifier VerifyProof;
  mutable std::mutex ConfigurationMutex;
  bool Frozen = false;
  mutable std::mutex ProvenanceMutex;
  std::vector<std::string> FallbackProvenance;
  std::atomic<uint64_t> NextContinuationGeneration{1};
  std::mutex ContinuationMutex;
  std::vector<std::unique_ptr<ContinuationContext>> Continuations;
};

} // namespace neverc::plugin

#endif
