#include "ObjectPhaseBridge.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus phaseStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

std::pair<uint64_t, uint64_t> key(NevercTaskHandle Task) {
  return {Task.Owner, Task.Value};
}

NevercInterfaceID objectPhaseInterfaceID() {
  return {NEVERC_INTERFACE_OBJECT_PHASE_HIGH,
          NEVERC_INTERFACE_OBJECT_PHASE_LOW};
}

} // namespace

ObjectPhaseProcessService::ObjectPhaseProcessService() {
  API.Header = {sizeof(API), NEVERC_OBJECT_PHASE_API_MAJOR,
                NEVERC_OBJECT_PHASE_API_MINOR, 0};
  API.Context = this;
  API.GetGraph = getGraph;
  API.GetImage = getImage;
}

Error ObjectPhaseProcessService::attach(
    ObjectPhaseRuntimeAccess &Runtime) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto [It, Inserted] =
      Active.emplace(key(Runtime.taskHandle()), &Runtime);
  if (!Inserted)
    return createStringError(
        inconvertibleErrorCode(),
        "object phase runtime is already active for this task");
  return Error::success();
}

void ObjectPhaseProcessService::detach(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Active.erase(key(Task));
}

void ObjectPhaseProcessService::taskScopeUnregistered(
    NevercTaskHandle Task) noexcept {
  detach(Task);
}

ObjectPhaseRuntimeAccess *
ObjectPhaseProcessService::find(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Active.find(key(Task));
  return It == Active.end() ? nullptr : It->second;
}

ObjectPhaseProcessService *
ObjectPhaseProcessService::service(void *Context) {
  return static_cast<ObjectPhaseProcessService *>(Context);
}

NevercStatus NEVERC_CALL ObjectPhaseProcessService::getGraph(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact,
    NevercObjectPhaseGraphInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  ObjectPhaseRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime ? Runtime->getGraph(Frame, Artifact, OutInfo)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL ObjectPhaseProcessService::getImage(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, NevercObjectImageInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  ObjectPhaseRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime ? Runtime->getImage(Frame, Artifact, OutInfo)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

std::shared_ptr<ObjectPhaseProcessService>
findObjectPhaseProcessService(PluginProcessServices &Services) {
  std::shared_ptr<PluginHostService> Service =
      Services.findHostService(objectPhaseInterfaceID());
  return std::static_pointer_cast<ObjectPhaseProcessService>(
      std::move(Service));
}

Error registerPluginObjectPhaseInterface(
    PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register object phase interface after interface freeze");
  auto Service = std::make_shared<ObjectPhaseProcessService>();
  if (Error E =
          Services.registerHostService(objectPhaseInterfaceID(), Service))
    return E;
  return Services.interfaces().registerInterface(
      objectPhaseInterfaceID(),
      NEVERC_OBJECT_PHASE_INTERFACE_STABILITY, &Service->api(), {});
}

} // namespace neverc::plugin
