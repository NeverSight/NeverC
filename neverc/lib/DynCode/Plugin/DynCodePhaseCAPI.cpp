#include "DynCodePhaseCAPI.h"
#include "llvm/Support/Error.h"

using namespace llvm;
using neverc::plugin::PluginProcessServices;

namespace neverc {
namespace dyncode {
namespace {

NevercStatus phaseStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

std::pair<uint64_t, uint64_t> taskKey(NevercTaskHandle Task) {
  return {Task.Owner, Task.Value};
}

NevercInterfaceID dynCodePhaseInterfaceID() {
  return {NEVERC_INTERFACE_DYNCODE_PHASE_HIGH,
          NEVERC_INTERFACE_DYNCODE_PHASE_LOW};
}

} // namespace

DynCodePhaseProcessService::DynCodePhaseProcessService() {
  API.Header = {sizeof(API), NEVERC_DYNCODE_PHASE_API_MAJOR,
                NEVERC_DYNCODE_PHASE_API_MINOR, 0};
  API.Context = this;
  API.GetPhaseInfo = getPhaseInfo;
  API.GetRequest = getRequest;
  API.GetImage = getImage;
  API.GetReport = getReport;
}

Error DynCodePhaseProcessService::attach(DynCodePhaseRuntimeAccess &Runtime) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto [It, Inserted] =
      Active.emplace(taskKey(Runtime.taskHandle()), &Runtime);
  (void)It;
  if (!Inserted)
    return createStringError(
        inconvertibleErrorCode(),
        "dyncode phase runtime is already active for this task");
  return Error::success();
}

void DynCodePhaseProcessService::detach(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Active.erase(taskKey(Task));
}

void DynCodePhaseProcessService::taskScopeUnregistered(
    NevercTaskHandle Task) noexcept {
  detach(Task);
}

DynCodePhaseRuntimeAccess *
DynCodePhaseProcessService::find(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Active.find(taskKey(Task));
  return It == Active.end() ? nullptr : It->second;
}

NevercStatus NEVERC_CALL DynCodePhaseProcessService::getPhaseInfo(
    void *Context, const NevercPhaseFrame *Frame,
    NevercDynCodePhaseInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<DynCodePhaseProcessService *>(Context);
  DynCodePhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->getPhaseInfo(Frame, OutInfo)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL DynCodePhaseProcessService::getRequest(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, NevercDynCodeRequestHandle *OutRequest) {
  if (!Context || !Frame || !OutRequest)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<DynCodePhaseProcessService *>(Context);
  DynCodePhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->getRequest(Frame, Artifact, OutRequest)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL DynCodePhaseProcessService::getImage(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, NevercDynCodeImageHandle *OutImage) {
  if (!Context || !Frame || !OutImage)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<DynCodePhaseProcessService *>(Context);
  DynCodePhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->getImage(Frame, Artifact, OutImage)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL DynCodePhaseProcessService::getReport(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, NevercDynCodeReportHandle *OutReport) {
  if (!Context || !Frame || !OutReport)
    return phaseStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto *Service = static_cast<DynCodePhaseProcessService *>(Context);
  DynCodePhaseRuntimeAccess *Runtime = Service->find(Frame->Task);
  return Runtime ? Runtime->getReport(Frame, Artifact, OutReport)
                 : phaseStatus(NEVERC_STATUS_STALE_HANDLE);
}

Error registerPluginDynCodePhaseInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register dyncode phase interface after interface freeze");
  auto Service = std::make_shared<DynCodePhaseProcessService>();
  if (Error E =
          Services.registerHostService(dynCodePhaseInterfaceID(), Service))
    return E;
  return Services.interfaces().registerInterface(
      dynCodePhaseInterfaceID(), NEVERC_DYNCODE_PHASE_INTERFACE_STABILITY,
      &Service->api(), {});
}

std::shared_ptr<DynCodePhaseProcessService>
findDynCodePhaseProcessService(PluginProcessServices &Services) {
  return std::static_pointer_cast<DynCodePhaseProcessService>(
      Services.findHostService(dynCodePhaseInterfaceID()));
}

} // namespace dyncode
} // namespace neverc
