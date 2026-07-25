#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPHASECAPI_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEPHASECAPI_H

// C ABI bridge for the dyncode phase-frame accessor table.
//
// Mirrors the Link phase process service: a per-process host service publishes
// the read-only NevercDynCodePhaseAPI, and the active DynCodePhasePipeline
// attaches itself for the current task so callbacks resolve back to the running
// pipeline.  The table is read-only -- dyncode phase replacement produces its
// output through the shared IR/MIR/Object/Image builders, not through a
// dyncode-specific publish entry.

#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/PluginDynCode.h"
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace neverc {
namespace dyncode {

class DynCodePhaseRuntimeAccess {
public:
  virtual ~DynCodePhaseRuntimeAccess() = default;
  virtual NevercTaskHandle taskHandle() const = 0;
  virtual NevercStatus getPhaseInfo(const NevercPhaseFrame *Frame,
                                    NevercDynCodePhaseInfo *OutInfo) = 0;
  virtual NevercStatus getRequest(const NevercPhaseFrame *Frame,
                                  NevercArtifactHandle Artifact,
                                  NevercDynCodeRequestHandle *OutRequest) = 0;
  virtual NevercStatus getImage(const NevercPhaseFrame *Frame,
                                NevercArtifactHandle Artifact,
                                NevercDynCodeImageHandle *OutImage) = 0;
  virtual NevercStatus getReport(const NevercPhaseFrame *Frame,
                                 NevercArtifactHandle Artifact,
                                 NevercDynCodeReportHandle *OutReport) = 0;
};

class DynCodePhaseProcessService final : public plugin::PluginHostService {
public:
  DynCodePhaseProcessService();

  const NevercDynCodePhaseAPI &api() const { return API; }
  llvm::Error attach(DynCodePhaseRuntimeAccess &Runtime);
  void detach(NevercTaskHandle Task);
  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override;

private:
  DynCodePhaseRuntimeAccess *find(NevercTaskHandle Task);
  static NevercStatus NEVERC_CALL getPhaseInfo(void *Context,
                                               const NevercPhaseFrame *Frame,
                                               NevercDynCodePhaseInfo *OutInfo);
  static NevercStatus NEVERC_CALL getRequest(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercDynCodeRequestHandle *OutRequest);
  static NevercStatus NEVERC_CALL getImage(void *Context,
                                           const NevercPhaseFrame *Frame,
                                           NevercArtifactHandle Artifact,
                                           NevercDynCodeImageHandle *OutImage);
  static NevercStatus NEVERC_CALL getReport(void *Context,
                                            const NevercPhaseFrame *Frame,
                                            NevercArtifactHandle Artifact,
                                            NevercDynCodeReportHandle *OutReport);

  NevercDynCodePhaseAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, DynCodePhaseRuntimeAccess *> Active;
};

llvm::Error
registerPluginDynCodePhaseInterface(plugin::PluginProcessServices &Services);
std::shared_ptr<DynCodePhaseProcessService>
findDynCodePhaseProcessService(plugin::PluginProcessServices &Services);

} // namespace dyncode
} // namespace neverc

#endif
