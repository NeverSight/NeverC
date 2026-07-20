#include "MCEmissionBridge.h"
#include "neverc/Plugin/Host/MCEmissionPlan.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus emissionStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

std::pair<uint64_t, uint64_t> key(NevercTaskHandle Task) {
  return {Task.Owner, Task.Value};
}

NevercInterfaceID emissionInterfaceID() {
  return {NEVERC_INTERFACE_MC_EMISSION_HIGH,
          NEVERC_INTERFACE_MC_EMISSION_LOW};
}

} // namespace

MCEmissionProcessService::MCEmissionProcessService() {
  API.Header = {sizeof(API), NEVERC_MC_EMISSION_API_MAJOR,
                NEVERC_MC_EMISSION_API_MINOR, 0};
  API.Context = this;
  API.GetEvent = getEvent;
  API.BeginInstructionReplacement = beginInstructionReplacement;
  API.PublishInstructionReplacement = publishInstructionReplacement;
  API.GetLayoutSection = getLayoutSection;
  API.GetLayoutFragment = getLayoutFragment;
  API.GetLayoutSymbol = getLayoutSymbol;
  API.GetLayoutFixup = getLayoutFixup;
}

Error MCEmissionProcessService::attach(
    MCEmissionRuntimeAccess &Runtime) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto [It, Inserted] =
      Active.emplace(key(Runtime.taskHandle()), &Runtime);
  if (!Inserted)
    return createStringError(
        inconvertibleErrorCode(),
        "MC emission runtime is already active for this task");
  return Error::success();
}

void MCEmissionProcessService::detach(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Active.erase(key(Task));
}

void MCEmissionProcessService::taskScopeUnregistered(
    NevercTaskHandle Task) noexcept {
  detach(Task);
}

MCEmissionRuntimeAccess *
MCEmissionProcessService::find(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Active.find(key(Task));
  return It == Active.end() ? nullptr : It->second;
}

MCEmissionProcessService *
MCEmissionProcessService::service(void *Context) {
  return static_cast<MCEmissionProcessService *>(Context);
}

NevercStatus NEVERC_CALL MCEmissionProcessService::getEvent(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact,
    NevercMCEmissionEventInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime ? Runtime->getEvent(Frame, Artifact, OutInfo)
                 : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL
MCEmissionProcessService::beginInstructionReplacement(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit,
    NevercMCInstHandle *OutInstruction) {
  if (!Context || !Frame || !Continuation || !OutMC || !OutUnit ||
      !OutInstruction)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime
             ? Runtime->beginInstructionReplacement(
                   Frame, Continuation, OutMC, OutUnit, OutInstruction)
             : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL
MCEmissionProcessService::publishInstructionReplacement(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercArtifactHandle *OutInstruction) {
  if (!Context || !Frame || !Continuation || !OutInstruction)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime
             ? Runtime->publishInstructionReplacement(
                   Frame, Continuation, OutInstruction)
             : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL MCEmissionProcessService::getLayoutSection(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, uint64_t Index,
    NevercMCEmissionSectionLayoutInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime
             ? Runtime->getLayoutSection(Frame, Artifact, Index, OutInfo)
             : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL MCEmissionProcessService::getLayoutFragment(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, uint64_t Index,
    NevercMCEmissionFragmentLayoutInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime
             ? Runtime->getLayoutFragment(Frame, Artifact, Index, OutInfo)
             : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL MCEmissionProcessService::getLayoutSymbol(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, uint64_t Index,
    NevercMCEmissionSymbolLayoutInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime
             ? Runtime->getLayoutSymbol(Frame, Artifact, Index, OutInfo)
             : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL MCEmissionProcessService::getLayoutFixup(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Artifact, uint64_t Index,
    NevercMCEmissionFixupLayoutInfo *OutInfo) {
  if (!Context || !Frame || !OutInfo)
    return emissionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCEmissionRuntimeAccess *Runtime =
      service(Context)->find(Frame->Task);
  return Runtime
             ? Runtime->getLayoutFixup(Frame, Artifact, Index, OutInfo)
             : emissionStatus(NEVERC_STATUS_STALE_HANDLE);
}

std::shared_ptr<MCEmissionProcessService>
findMCEmissionProcessService(PluginProcessServices &Services) {
  std::shared_ptr<PluginHostService> Service =
      Services.findHostService(emissionInterfaceID());
  return std::static_pointer_cast<MCEmissionProcessService>(
      std::move(Service));
}

Error registerPluginMCEmissionInterface(
    PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register MC emission interface after interface freeze");
  auto Service = std::make_shared<MCEmissionProcessService>();
  if (Error E =
          Services.registerHostService(emissionInterfaceID(), Service))
    return E;
  return Services.interfaces().registerInterface(
      emissionInterfaceID(), NEVERC_MC_EMISSION_INTERFACE_STABILITY,
      &Service->api(), {});
}

} // namespace neverc::plugin
