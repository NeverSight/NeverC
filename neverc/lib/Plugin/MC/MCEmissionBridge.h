#ifndef NEVERC_LIB_PLUGIN_MC_MCEMISSIONBRIDGE_H
#define NEVERC_LIB_PLUGIN_MC_MCEMISSIONBRIDGE_H

#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/PluginMC.h"
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace neverc::plugin {

class MCEmissionRuntimeAccess {
public:
  virtual ~MCEmissionRuntimeAccess() = default;
  virtual NevercTaskHandle taskHandle() const = 0;

  virtual NevercStatus getEvent(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercMCEmissionEventInfo *OutInfo) = 0;
  virtual NevercStatus beginInstructionReplacement(
      const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit,
      NevercMCInstHandle *OutInstruction) = 0;
  virtual NevercStatus publishInstructionReplacement(
      const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      NevercArtifactHandle *OutInstruction) = 0;
  virtual NevercStatus getLayoutSection(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index, NevercMCEmissionSectionLayoutInfo *OutInfo) = 0;
  virtual NevercStatus getLayoutFragment(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index, NevercMCEmissionFragmentLayoutInfo *OutInfo) = 0;
  virtual NevercStatus getLayoutSymbol(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index, NevercMCEmissionSymbolLayoutInfo *OutInfo) = 0;
  virtual NevercStatus getLayoutFixup(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index, NevercMCEmissionFixupLayoutInfo *OutInfo) = 0;
};

class MCEmissionProcessService final : public PluginHostService {
public:
  MCEmissionProcessService();

  const NevercMCEmissionAPI &api() const { return API; }
  llvm::Error attach(MCEmissionRuntimeAccess &Runtime);
  void detach(NevercTaskHandle Task);
  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override;

private:
  MCEmissionRuntimeAccess *find(NevercTaskHandle Task);
  static MCEmissionProcessService *service(void *Context);

  static NevercStatus NEVERC_CALL
  getEvent(void *Context, const NevercPhaseFrame *Frame,
           NevercArtifactHandle Artifact,
           NevercMCEmissionEventInfo *OutInfo);
  static NevercStatus NEVERC_CALL beginInstructionReplacement(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit,
      NevercMCInstHandle *OutInstruction);
  static NevercStatus NEVERC_CALL publishInstructionReplacement(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      NevercArtifactHandle *OutInstruction);
  static NevercStatus NEVERC_CALL getLayoutSection(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionSectionLayoutInfo *OutInfo);
  static NevercStatus NEVERC_CALL getLayoutFragment(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionFragmentLayoutInfo *OutInfo);
  static NevercStatus NEVERC_CALL getLayoutSymbol(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionSymbolLayoutInfo *OutInfo);
  static NevercStatus NEVERC_CALL getLayoutFixup(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, uint64_t Index,
      NevercMCEmissionFixupLayoutInfo *OutInfo);

  NevercMCEmissionAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, MCEmissionRuntimeAccess *>
      Active;
};

std::shared_ptr<MCEmissionProcessService>
findMCEmissionProcessService(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
