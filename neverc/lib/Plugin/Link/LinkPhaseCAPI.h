#ifndef NEVERC_PLUGIN_LINK_LINKPHASECAPI_H
#define NEVERC_PLUGIN_LINK_LINKPHASECAPI_H

#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/PluginLink.h"
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace neverc::plugin {

class LinkPhaseRuntimeAccess {
public:
  virtual ~LinkPhaseRuntimeAccess() = default;
  virtual NevercTaskHandle taskHandle() const = 0;
  virtual NevercStatus getGraph(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercLinkPhaseGraphInfo *OutInfo) = 0;
  virtual NevercStatus publishGraph(
      const NevercPhaseFrame *Frame, NevercLinkGraphHandle Graph,
      NevercArtifactHandle *OutArtifact) = 0;
  virtual NevercStatus getImage(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercLinkPhaseImageInfo *OutInfo) = 0;
};

class LinkPhaseProcessService final : public PluginHostService {
public:
  LinkPhaseProcessService();

  const NevercLinkPhaseAPI &api() const { return API; }
  llvm::Error attach(LinkPhaseRuntimeAccess &Runtime);
  void detach(NevercTaskHandle Task);
  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override;

private:
  LinkPhaseRuntimeAccess *find(NevercTaskHandle Task);
  static NevercStatus NEVERC_CALL
  getGraph(void *Context, const NevercPhaseFrame *Frame,
           NevercArtifactHandle Artifact, NevercLinkPhaseGraphInfo *OutInfo);
  static NevercStatus NEVERC_CALL
  publishGraph(void *Context, const NevercPhaseFrame *Frame,
               NevercLinkGraphHandle Graph,
               NevercArtifactHandle *OutArtifact);
  static NevercStatus NEVERC_CALL
  getImage(void *Context, const NevercPhaseFrame *Frame,
           NevercArtifactHandle Artifact,
           NevercLinkPhaseImageInfo *OutInfo);

  NevercLinkPhaseAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, LinkPhaseRuntimeAccess *> Active;
};

llvm::Error registerPluginLinkPhaseInterface(
    PluginProcessServices &Services);
std::shared_ptr<LinkPhaseProcessService>
findLinkPhaseProcessService(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
