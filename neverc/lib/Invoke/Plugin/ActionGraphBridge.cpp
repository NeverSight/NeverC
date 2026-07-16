#include "Plugin/ActionGraph.h"
#include "Plugin/DriverAPIBridge.h"
#include "Plugin/JobGraph.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/JSON.h"
#include <cstring>
#include <limits>
#include <new>
#include <thread>

using namespace llvm;

namespace neverc::driver {
namespace {

constexpr plugin::PluginHandleKind DriverActionGraphBuilderHandleKind = 8;
constexpr plugin::PluginHandleKind DriverActionGraphMutationHandleKind = 9;
constexpr uint64_t MaximumActionNodeListCount = UINT64_C(1) << 20;

struct BoundActionGraphEdit {
  std::unique_ptr<DriverActionGraphEdit> Edit;
  const NevercPhaseFrame *Frame = nullptr;
  const NevercPhaseContinuation *Continuation = nullptr;
  std::thread::id Thread;
};

NevercStatus bridgeStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validFrame(const NevercPhaseFrame *Frame,
                const plugin::PluginTaskContext &Task,
                bool AllowBuildJobs = false) {
  return Frame && Frame->Header.StructSize >= sizeof(*Frame) &&
         Frame->Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Frame->Header.Flags == 0 && sameHandle(Frame->Task, Task.handle()) &&
         (plugin::samePluginInterfaceID(Frame->Phase,
                                        driverBuildActionsPhaseID()) ||
          (AllowBuildJobs &&
           plugin::samePluginInterfaceID(Frame->Phase,
                                         driverBuildJobsPhaseID())));
}

bool viewToStringRef(NevercStringView View, StringRef &Out) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  Out = StringRef(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  return Out.size() <= (UINT64_C(1) << 20) && !Out.contains('\0') &&
         json::isUTF8(Out);
}

bool nodeListToVector(NevercActionNodeIDList List,
                      std::vector<NevercActionNodeID> &Out) {
  Out.clear();
  if (List.Count == 0)
    return true;
  if (!List.Data || List.Count > MaximumActionNodeListCount ||
      List.ElementStride < sizeof(NevercActionNodeID) ||
      List.ElementStride > std::numeric_limits<size_t>::max() ||
      List.Count - 1 >
          std::numeric_limits<size_t>::max() / List.ElementStride)
    return false;
  Out.reserve(static_cast<size_t>(List.Count));
  const auto *Bytes = reinterpret_cast<const unsigned char *>(List.Data);
  for (uint64_t Index = 0; Index != List.Count; ++Index) {
    NevercActionNodeID Node = 0;
    std::memcpy(&Node,
                Bytes + static_cast<size_t>(Index) *
                            static_cast<size_t>(List.ElementStride),
                sizeof(Node));
    Out.push_back(Node);
  }
  return true;
}

NevercStatus editError(
    Error E, NevercStatusCode Code = NEVERC_STATUS_POLICY_VIOLATION) {
  consumeError(std::move(E));
  return bridgeStatus(Code);
}

NevercStatus resolveRequest(plugin::PluginPhaseExecutor &Executor,
                            plugin::PluginTaskContext &Task,
                            NevercArtifactHandle Request,
                            const DriverActionGraphRequestArtifact **Out) {
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Request, driverActionGraphRequestArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *Out = static_cast<const DriverActionGraphRequestArtifact *>(Payload);
  return neverc_status_ok();
}

NevercStatus resolveGraph(plugin::PluginPhaseExecutor &Executor,
                          plugin::PluginTaskContext &Task,
                          NevercArtifactHandle Graph,
                          DriverActionGraphArtifact **Out) {
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Graph, driverActionGraphArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *Out = const_cast<DriverActionGraphArtifact *>(
      static_cast<const DriverActionGraphArtifact *>(Payload));
  return neverc_status_ok();
}

NevercStatus resolveEdit(plugin::PluginPhaseExecutor &Executor,
                         plugin::PluginTaskContext &Task, NevercHandle Handle,
                         plugin::PluginHandleKind Kind,
                         BoundActionGraphEdit **Out) {
  *Out = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(Handle, Kind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Bound = static_cast<BoundActionGraphEdit *>(Payload);
  if (!Bound || !Bound->Edit || Bound->Thread != std::this_thread::get_id())
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Kind == DriverActionGraphMutationHandleKind &&
      !Executor.isActiveContinuation(Bound->Frame, Bound->Continuation))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  *Out = Bound;
  return neverc_status_ok();
}

NevercStatus resolveAnyEdit(plugin::PluginPhaseExecutor &Executor,
                            plugin::PluginTaskContext &Task,
                            NevercHandle Handle,
                            BoundActionGraphEdit **Out,
                            plugin::PluginHandleKind &OutKind) {
  NevercStatus Status = resolveEdit(
      Executor, Task, Handle, DriverActionGraphBuilderHandleKind, Out);
  if (Status.Code == NEVERC_STATUS_OK) {
    OutKind = DriverActionGraphBuilderHandleKind;
    return Status;
  }
  Status = resolveEdit(
      Executor, Task, Handle, DriverActionGraphMutationHandleKind, Out);
  if (Status.Code == NEVERC_STATUS_OK)
    OutKind = DriverActionGraphMutationHandleKind;
  return Status;
}

} // namespace

NevercStatus NEVERC_CALL DriverAPIBridge::getDriverInputCount(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, uint64_t *OutCount) {
  if (!Context || !OutCount)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const DriverActionGraphRequestArtifact *Artifact = nullptr;
  NevercStatus Status = resolveRequest(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Request, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Artifact->inputCount();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getDriverInput(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, uint64_t Index,
    NevercDriverInput *OutInput) {
  if (!Context || !OutInput ||
      OutInput->Header.StructSize < sizeof(*OutInput) ||
      OutInput->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInput = {};
  OutInput->Header = {sizeof(*OutInput), NEVERC_DRIVER_API_MAJOR,
                      NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const DriverActionGraphRequestArtifact *Artifact = nullptr;
  NevercStatus Status = resolveRequest(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Request, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Artifact->describeInput(Index, *OutInput))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getActionNodeCount(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t *OutCount) {
  if (!Context || !OutCount)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, true))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Artifact->nodeCount();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getActionNode(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t Index, NevercActionNode *OutNode) {
  if (!Context || !OutNode ||
      OutNode->Header.StructSize < sizeof(*OutNode) ||
      OutNode->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutNode = {};
  OutNode->Header = {sizeof(*OutNode), NEVERC_DRIVER_API_MAJOR,
                     NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, true))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Artifact->describeNode(Index, *OutNode))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getActionNodeInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    NevercActionNodeID Node, uint64_t Index,
    NevercActionNodeID *OutInput) {
  if (!Context || !OutInput)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInput = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, true))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Artifact->getNodeInput(Node, Index, *OutInput))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getActionRootCount(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t *OutCount) {
  if (!Context || !OutCount)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, true))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Artifact->rootCount();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getActionRoot(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t Index, NevercActionNodeID *OutRoot) {
  if (!Context || !OutRoot)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRoot = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, true))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Artifact->getRoot(Index, *OutRoot))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::createActionGraphBuilder(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, NevercActionGraphBuilderHandle *OutBuilder) {
  if (!Context || !OutBuilder)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuilder = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const DriverActionGraphRequestArtifact *Artifact = nullptr;
  NevercStatus Status = resolveRequest(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Request, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Edit = DriverActionGraphEdit::createBuilder(*Artifact);
  if (!Edit)
    return editError(Edit.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto *Bound = new (std::nothrow)
      BoundActionGraphEdit{std::move(*Edit), Frame, nullptr,
                           std::this_thread::get_id()};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverActionGraphBuilderHandleKind, Bound,
      [](void *Raw) { delete static_cast<BoundActionGraphEdit *>(Raw); });
  if (!Handle) {
    delete Bound;
    return editError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuilder = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::beginActionGraphMutation(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Graph,
    NevercActionGraphMutationHandle *OutMutation) {
  if (!Context || !OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask) ||
      !Bridge.ActiveExecutor->isActiveContinuation(Frame, Continuation))
    return bridgeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Edit = Artifact->beginMutation();
  if (!Edit)
    return editError(Edit.takeError(), NEVERC_STATUS_INVALID_STATE);
  auto *Bound = new (std::nothrow)
      BoundActionGraphEdit{std::move(*Edit), Frame, Continuation,
                           std::this_thread::get_id()};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverActionGraphMutationHandleKind, Bound,
      [](void *Raw) { delete static_cast<BoundActionGraphEdit *>(Raw); });
  if (!Handle) {
    delete Bound;
    return editError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMutation = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::addActionNode(
    void *Context, NevercActionGraphBuilderHandle Builder,
    const NevercActionNodeDescriptor *Descriptor,
    NevercActionNodeID *OutNode) {
  if (!Context || !Descriptor || !OutNode ||
      Descriptor->Header.StructSize < sizeof(*Descriptor) ||
      Descriptor->Header.Major != NEVERC_DRIVER_API_MAJOR ||
      Descriptor->Header.Flags != 0 || Descriptor->Reserved != 0)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutNode = 0;
  StringRef BindArch;
  std::vector<NevercActionNodeID> Inputs;
  if (!viewToStringRef(Descriptor->BindArch, BindArch) ||
      !nodeListToVector(Descriptor->Inputs, Inputs))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Node = Bound->Edit->addNode(
      Descriptor->Kind, Descriptor->OutputType, Descriptor->DriverInput,
      BindArch, Inputs);
  if (!Node)
    return editError(Node.takeError());
  *OutNode = *Node;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::removeActionNode(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->removeNode(Node))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::replaceActionNodeInputs(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node, NevercActionNodeIDList InputList) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::vector<NevercActionNodeID> Inputs;
  if (!nodeListToVector(InputList, Inputs))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->replaceInputs(Node, Inputs))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setActionNodeOutputType(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node, NevercDriverType OutputType) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->setOutputType(Node, OutputType))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setActionNodeBindArch(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node, NevercStringView BindArchView) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef BindArch;
  if (!viewToStringRef(BindArchView, BindArch))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->setBindArch(Node, BindArch))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setActionRoots(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeIDList RootList) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::vector<NevercActionNodeID> Roots;
  if (!nodeListToVector(RootList, Roots))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->setRoots(Roots))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::publishActionGraph(
    void *Context, const NevercPhaseFrame *Frame,
    NevercActionGraphBuilderHandle Builder,
    NevercArtifactHandle *OutGraph) {
  if (!Context || !OutGraph)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutGraph = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  NevercStatus Status = resolveEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder,
      DriverActionGraphBuilderHandleKind, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Bound->Frame != Frame)
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  auto Artifact = Bound->Edit->finishBuilder();
  if (!Artifact)
    return editError(Artifact.takeError(), NEVERC_STATUS_INVALID_STATE);
  DriverActionGraphArtifact *Payload = Artifact->release();
  auto Candidate = Bridge.ActiveExecutor->createCandidate(
      *Bridge.ActiveTask, driverActionGraphArtifactID(), Payload);
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  NevercStatus Release = Bridge.ActiveTask->handles().release(
      Builder, DriverActionGraphBuilderHandleKind);
  if (Release.Code != NEVERC_STATUS_OK)
    return Release;
  *OutGraph = *Candidate;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::commitActionGraphMutation(
    void *Context, NevercActionGraphMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  NevercStatus Status = resolveEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation,
      DriverActionGraphMutationHandleKind, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->commitMutation())
    return editError(std::move(E), NEVERC_STATUS_INVALID_STATE);
  return Bridge.ActiveTask->handles().release(
      Mutation, DriverActionGraphMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::abortActionGraphEdit(
    void *Context, NevercHandle Edit) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundActionGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Bound->Edit->abort();
  return Bridge.ActiveTask->handles().release(Edit, Kind);
}

} // namespace neverc::driver
