#ifndef NEVERC_LIB_PLUGIN_OBJECT_OBJECTPHASEBRIDGE_H
#define NEVERC_LIB_PLUGIN_OBJECT_OBJECTPHASEBRIDGE_H

#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/PluginObject.h"
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace neverc::plugin {

class ObjectPhaseRuntimeAccess {
public:
  virtual ~ObjectPhaseRuntimeAccess() = default;
  virtual NevercTaskHandle taskHandle() const = 0;
  virtual NevercStatus getGraph(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercObjectPhaseGraphInfo *OutInfo) = 0;
  virtual NevercStatus getImage(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercObjectImageInfo *OutInfo) = 0;
};

class ObjectPhaseProcessService final : public PluginHostService {
public:
  ObjectPhaseProcessService();

  const NevercObjectPhaseAPI &api() const { return API; }
  llvm::Error attach(ObjectPhaseRuntimeAccess &Runtime);
  void detach(NevercTaskHandle Task);
  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override;

private:
  ObjectPhaseRuntimeAccess *find(NevercTaskHandle Task);
  static ObjectPhaseProcessService *service(void *Context);
  static NevercStatus NEVERC_CALL
  getGraph(void *Context, const NevercPhaseFrame *Frame,
           NevercArtifactHandle Artifact,
           NevercObjectPhaseGraphInfo *OutInfo);
  static NevercStatus NEVERC_CALL
  getImage(void *Context, const NevercPhaseFrame *Frame,
           NevercArtifactHandle Artifact, NevercObjectImageInfo *OutInfo);

  NevercObjectPhaseAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, ObjectPhaseRuntimeAccess *>
      Active;
};

std::shared_ptr<ObjectPhaseProcessService>
findObjectPhaseProcessService(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
