#include "LinkPhaseCAPI.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus phaseStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

std::pair<uint64_t, uint64_t> taskKey(NevercTaskHandle Task) {
  return {Task.Owner, Task.Value};
}

NevercInterfaceID linkPhaseInterfaceID() {
  return {NEVERC_INTERFACE_LINK_PHASE_HIGH,
          NEVERC_INTERFACE_LINK_PHASE_LOW};
}

} // namespace

LinkPhaseProcessService::LinkPhaseProcessService() {
  API.Header = {sizeof(API), NEVERC_LINK_PHASE_API_MAJOR,
                NEVERC_LINK_PHASE_API_MINOR, 0};
  API.Context = this;
  API.GetGraph = getGraph;
  API.PublishGraph = publishGraph;
  API.GetImage = getImage;
}

Error LinkPhaseProcessService::attach(LinkPhaseRuntimeAccess &Runtime) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto [It, Inserted] =
      Active.emplace(taskKey(Runtime.taskHandle()), &Runtime);
  if (!Inserted)
    return createStringError(
        inconvertibleErrorCode(),
        "Link phase runtime is already active for this task");
  return Error::success();
}

void LinkPhaseProcessService::detach(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Active.erase(taskKey(Task));
}

void LinkPhaseProcessService::taskScopeUnregistered(
    NevercTaskHandle Task) noexcept {
  detach(Task);
}

LinkPhaseRuntimeAccess *
LinkPhaseProcessService::find(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Active.find(taskKey(Task));
  return It == Active.end() ? nullptr : It->second;
}

NevercStatus NEVERC_CALL LinkPhaseProcessService::getGraph(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, NevercLinkPhaseGraphInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<LinkPhaseProcessService *>(Context);
  LinkPhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->getGraph(Frame, Artifact, OutInfo)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL LinkPhaseProcessService::publishGraph(
    void *Context, const NevercPhaseFrame *Frame,
    NevercLinkGraphHandle Graph, NevercArtifactHandle *OutArtifact) {
  if (!Context || !Frame || !OutArtifact)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<LinkPhaseProcessService *>(Context);
  LinkPhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->publishGraph(Frame, Graph, OutArtifact)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL LinkPhaseProcessService::getImage(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, NevercLinkPhaseImageInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<LinkPhaseProcessService *>(Context);
  LinkPhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->getImage(Frame, Artifact, OutInfo)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

Error registerPluginLinkPhaseInterface(
    PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register Link phase interface after interface freeze");
  auto Service = std::make_shared<LinkPhaseProcessService>();
  if (Error E =
          Services.registerHostService(linkPhaseInterfaceID(), Service))
    return E;
  return Services.interfaces().registerInterface(
      linkPhaseInterfaceID(), NEVERC_LINK_PHASE_INTERFACE_STABILITY,
      &Service->api(), {});
}

std::shared_ptr<LinkPhaseProcessService>
findLinkPhaseProcessService(PluginProcessServices &Services) {
  return std::static_pointer_cast<LinkPhaseProcessService>(
      Services.findHostService(linkPhaseInterfaceID()));
}

} // namespace neverc::plugin
