#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus objectStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

ObjectPluginBridge::OwnerLease bridge(void *Context, NevercTaskHandle Task,
                                      NevercStatus &Status) {
  return ObjectPluginBridge::acquire(Context, Task, true, Status);
}

NevercStatus NEVERC_CALL BeginMutation(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectMutationHandle *OutMutation) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutMutation)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutMutation = {};
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Mutation = Bridge->beginMutation();
  if (!Mutation) {
    consumeError(Mutation.takeError());
    return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMutation = *Mutation;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CommitMutation(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->commitMutation(Mutation) : Status;
}

NevercStatus NEVERC_CALL AbandonMutation(
    void *Context, NevercTaskHandle Task,
    NevercObjectMutationHandle Mutation) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->abandonMutation(Mutation) : Status;
}

} // namespace

Expected<NevercObjectMutationHandle> ObjectPluginBridge::beginMutation() {
  if (!mutationAllowed())
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph mutation is not allowed");
  if (hasActiveMutation())
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph mutation is already active");
  Working = std::make_unique<PluginObjectGraph>(Graph);
  Working->clearLayoutProof();
  auto Handle =
      Task.handles().create(PluginObjectMutationHandleKind, this);
  if (!Handle) {
    Working.reset();
    return Handle.takeError();
  }
  MutationHandle = *Handle;
  return MutationHandle;
}

NevercStatus
ObjectPluginBridge::checkMutation(NevercObjectMutationHandle Mutation) const {
  if (!mutationAllowed())
    return objectStatus(NEVERC_STATUS_POLICY_VIOLATION);
  if (!hasActiveMutation() || !Working)
    return objectStatus(NEVERC_STATUS_INVALID_STATE);
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Mutation, PluginObjectMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != this || !sameHandle(Mutation, MutationHandle))
    return objectStatus(NEVERC_STATUS_WRONG_SCOPE);
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::commitMutation(
    NevercObjectMutationHandle Mutation) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = verifyPluginObjectGraph(*Working)) {
    consumeError(std::move(E));
    finishMutation();
    return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  }
  Working->advanceGeneration();
  Graph = std::move(*Working);
  finishMutation();
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::abandonMutation(
    NevercObjectMutationHandle Mutation) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  finishMutation();
  return neverc_status_ok();
}

void ObjectPluginBridge::finishMutation() {
  finishHandles();
  if (!neverc_handle_is_null(GraphHandle)) {
    (void)Task.handles().release(GraphHandle,
                                 PluginObjectGraphHandleKind);
    GraphHandle = {};
  }
  if (!neverc_handle_is_null(MutationHandle)) {
    (void)Task.handles().release(MutationHandle,
                                 PluginObjectMutationHandleKind);
    MutationHandle = {};
  }
  Working.reset();
  if (BridgeGeneration == std::numeric_limits<uint64_t>::max())
    BridgeGeneration = 1;
  else
    ++BridgeGeneration;
}

void initializeObjectMutationAPI(NevercObjectAPI &API,
                                 ObjectPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.BeginMutation = BeginMutation;
  API.CommitMutation = CommitMutation;
  API.AbandonMutation = AbandonMutation;
}

} // namespace neverc::plugin
