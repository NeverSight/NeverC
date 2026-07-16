#include "Plugin/ActionGraph.h"
#include "Plugin/DriverAPIBridge.h"
#include "Plugin/JobGraph.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/JSON.h"
#include <cstring>
#include <limits>
#include <new>
#include <thread>

using namespace llvm;

namespace neverc::driver {
namespace {

constexpr plugin::PluginHandleKind DriverJobGraphBuilderHandleKind = 10;
constexpr plugin::PluginHandleKind DriverJobGraphMutationHandleKind = 11;
constexpr uint64_t MaximumJobListCount = UINT64_C(1) << 20;

struct BoundJobGraphEdit {
  std::unique_ptr<DriverJobGraphEdit> Edit;
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
                const plugin::PluginTaskContext &Task) {
  return Frame && Frame->Header.StructSize >= sizeof(*Frame) &&
         Frame->Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Frame->Header.Flags == 0 && sameHandle(Frame->Task, Task.handle()) &&
         plugin::samePluginInterfaceID(Frame->Phase,
                                       driverBuildJobsPhaseID());
}

bool viewToStringRef(NevercStringView View, StringRef &Out) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  Out = StringRef(View.Data ? View.Data : "",
                  static_cast<size_t>(View.Length));
  return Out.size() <= (UINT64_C(1) << 20) && !Out.contains('\0') &&
         json::isUTF8(Out);
}

bool stringListToVector(NevercStringList List,
                        std::vector<std::string> &Out) {
  Out.clear();
  if (List.Count == 0)
    return true;
  if (!List.Data || List.Count > MaximumJobListCount ||
      List.ElementStride < sizeof(NevercStringView) ||
      List.ElementStride > std::numeric_limits<size_t>::max() ||
      List.Count - 1 >
          std::numeric_limits<size_t>::max() / List.ElementStride)
    return false;
  const auto *Bytes =
      reinterpret_cast<const unsigned char *>(List.Data);
  Out.reserve(static_cast<size_t>(List.Count));
  for (uint64_t Index = 0; Index != List.Count; ++Index) {
    NevercStringView View{};
    std::memcpy(&View,
                Bytes + static_cast<size_t>(Index) *
                            static_cast<size_t>(List.ElementStride),
                sizeof(View));
    StringRef Text;
    if (!viewToStringRef(View, Text))
      return false;
    Out.push_back(Text.str());
  }
  return true;
}

bool jobIDListToVector(NevercJobIDList List,
                       std::vector<NevercJobID> &Out) {
  Out.clear();
  if (List.Count == 0)
    return true;
  if (!List.Data || List.Count > MaximumJobListCount ||
      List.ElementStride < sizeof(NevercJobID) ||
      List.ElementStride > std::numeric_limits<size_t>::max() ||
      List.Count - 1 >
          std::numeric_limits<size_t>::max() / List.ElementStride)
    return false;
  const auto *Bytes =
      reinterpret_cast<const unsigned char *>(List.Data);
  Out.reserve(static_cast<size_t>(List.Count));
  for (uint64_t Index = 0; Index != List.Count; ++Index) {
    NevercJobID Job = 0;
    std::memcpy(&Job,
                Bytes + static_cast<size_t>(Index) *
                            static_cast<size_t>(List.ElementStride),
                sizeof(Job));
    Out.push_back(Job);
  }
  return true;
}

bool jobFileToRecord(const NevercJobFile &File,
                     DriverJobFileRecord &Out) {
  if (File.Header.StructSize < sizeof(File) ||
      File.Header.Major != NEVERC_DRIVER_API_MAJOR ||
      File.Header.Flags != 0 || File.Reserved != 0)
    return false;
  StringRef Path;
  if (!viewToStringRef(File.Path, Path) || Path.empty())
    return false;
  auto Internal = toInternalDriverType(File.Type);
  if (!Internal) {
    consumeError(Internal.takeError());
    return false;
  }
  Out.Path = Path.str();
  Out.PublicType = File.Type;
  Out.InternalType = *Internal;
  return true;
}

bool jobFileListToVector(NevercJobFileList List,
                         std::vector<DriverJobFileRecord> &Out) {
  Out.clear();
  if (List.Count == 0)
    return true;
  if (!List.Data || List.Count > MaximumJobListCount ||
      List.ElementStride < sizeof(NevercJobFile) ||
      List.ElementStride > std::numeric_limits<size_t>::max() ||
      List.Count - 1 >
          std::numeric_limits<size_t>::max() / List.ElementStride)
    return false;
  const auto *Bytes =
      reinterpret_cast<const unsigned char *>(List.Data);
  Out.reserve(static_cast<size_t>(List.Count));
  for (uint64_t Index = 0; Index != List.Count; ++Index) {
    NevercJobFile File{};
    std::memcpy(&File,
                Bytes + static_cast<size_t>(Index) *
                            static_cast<size_t>(List.ElementStride),
                sizeof(File));
    DriverJobFileRecord Record;
    if (!jobFileToRecord(File, Record))
      return false;
    Out.push_back(std::move(Record));
  }
  return true;
}

NevercStatus parseJobDescriptor(
    const NevercJobDescriptor *Descriptor,
    plugin::PluginTaskContext &Task, DriverJobGraphNode &Out) {
  if (!Descriptor ||
      Descriptor->Header.StructSize < sizeof(*Descriptor) ||
      Descriptor->Header.Major != NEVERC_DRIVER_API_MAJOR ||
      Descriptor->Header.Flags != 0 || Descriptor->Reserved != 0 ||
      (Descriptor->InProcess != NEVERC_FALSE &&
       Descriptor->InProcess != NEVERC_TRUE))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef Executable;
  StringRef CallbackID;
  if (!viewToStringRef(Descriptor->Executable, Executable) ||
      !viewToStringRef(Descriptor->CallbackID, CallbackID) ||
      !stringListToVector(Descriptor->Arguments, Out.Arguments) ||
      !stringListToVector(Descriptor->Environment, Out.Environment) ||
      !jobFileListToVector(Descriptor->Inputs, Out.Inputs) ||
      !jobFileListToVector(Descriptor->Outputs, Out.Outputs) ||
      !jobIDListToVector(Descriptor->Dependencies, Out.Dependencies))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Out.Kind = Descriptor->Kind;
  Out.SourceAction = Descriptor->SourceAction;
  Out.Executable = Executable.str();
  Out.ResponseFileKind = Descriptor->ResponseFileKind;
  Out.ResponseFileEncoding = Descriptor->ResponseFileEncoding;
  Out.InProcess = Descriptor->InProcess == NEVERC_TRUE;
  Out.LinkerFlavor = Descriptor->LinkerFlavor;
  Out.CallbackID = CallbackID.str();
  Out.Callback = Descriptor->Callback;
  Out.CallbackUserData = Descriptor->UserData;
  if (Out.Kind == NEVERC_JOB_PLUGIN) {
    StringRef PluginID = Task.session().currentCallbackPluginID();
    if (PluginID.empty())
      return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
    Out.PluginID = PluginID.str();
  }
  return neverc_status_ok();
}

NevercStatus editError(
    Error E, NevercStatusCode Code = NEVERC_STATUS_POLICY_VIOLATION) {
  consumeError(std::move(E));
  return bridgeStatus(Code);
}

NevercStatus resolveActionGraph(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, NevercArtifactHandle Graph,
    const DriverActionGraphArtifact **Out) {
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Graph, driverActionGraphArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *Out = static_cast<const DriverActionGraphArtifact *>(Payload);
  return neverc_status_ok();
}

NevercStatus resolveJobGraph(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, NevercArtifactHandle Graph,
    DriverJobGraphArtifact **Out) {
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Graph, driverJobGraphArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *Out = const_cast<DriverJobGraphArtifact *>(
      static_cast<const DriverJobGraphArtifact *>(Payload));
  return neverc_status_ok();
}

NevercStatus resolveEdit(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, NevercHandle Handle,
    plugin::PluginHandleKind Kind, BoundJobGraphEdit **Out) {
  *Out = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(Handle, Kind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Bound = static_cast<BoundJobGraphEdit *>(Payload);
  if (!Bound || !Bound->Edit ||
      Bound->Thread != std::this_thread::get_id())
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  if (Kind == DriverJobGraphMutationHandleKind &&
      !Executor.isActiveContinuation(Bound->Frame, Bound->Continuation))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  *Out = Bound;
  return neverc_status_ok();
}

NevercStatus resolveAnyEdit(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, NevercHandle Handle,
    BoundJobGraphEdit **Out, plugin::PluginHandleKind &OutKind) {
  NevercStatus Status = resolveEdit(
      Executor, Task, Handle, DriverJobGraphBuilderHandleKind, Out);
  if (Status.Code == NEVERC_STATUS_OK) {
    OutKind = DriverJobGraphBuilderHandleKind;
    return Status;
  }
  Status = resolveEdit(
      Executor, Task, Handle, DriverJobGraphMutationHandleKind, Out);
  if (Status.Code == NEVERC_STATUS_OK)
    OutKind = DriverJobGraphMutationHandleKind;
  return Status;
}

} // namespace

NevercStatus NEVERC_CALL DriverAPIBridge::getJobCount(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, uint64_t *OutCount) {
  if (!Context || !OutCount)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveJobGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Artifact->jobCount();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJob(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, uint64_t Index, NevercJob *OutJob) {
  if (!Context || !OutJob ||
      OutJob->Header.StructSize < sizeof(*OutJob) ||
      OutJob->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutJob = {};
  OutJob->Header = {sizeof(*OutJob), NEVERC_DRIVER_API_MAJOR,
                    NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveJobGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Artifact->describeJob(Index, *OutJob))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobDependency(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercJobID *OutDependency) {
  if (!Context || !OutDependency)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDependency = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveJobGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Artifact->getDependency(Job, Index, *OutDependency))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

static NevercStatus getJobString(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercStringView *OutValue, bool Environment) {
  if (!OutValue)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = {};
  if (!validFrame(Frame, Task))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveJobGraph(
      Executor, Task, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  bool Found = Environment
                   ? Artifact->getEnvironment(Job, Index, *OutValue)
                   : Artifact->getArgument(Job, Index, *OutValue);
  return Found ? neverc_status_ok()
               : bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobArgument(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercStringView *OutValue) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return getJobString(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Frame,
                      Graph, Job, Index, OutValue, false);
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobEnvironment(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercStringView *OutValue) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return getJobString(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Frame,
                      Graph, Job, Index, OutValue, true);
}

static NevercStatus getJobFile(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercJobFile *OutFile, bool Output) {
  if (!OutFile || OutFile->Header.StructSize < sizeof(*OutFile) ||
      OutFile->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutFile = {};
  OutFile->Header = {sizeof(*OutFile), NEVERC_DRIVER_API_MAJOR,
                     NEVERC_DRIVER_API_MINOR, 0};
  if (!validFrame(Frame, Task))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveJobGraph(
      Executor, Task, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  bool Found = Output ? Artifact->getOutput(Job, Index, *OutFile)
                      : Artifact->getInput(Job, Index, *OutFile);
  return Found ? neverc_status_ok()
               : bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobInput(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercJobFile *OutFile) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return getJobFile(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Frame,
                    Graph, Job, Index, OutFile, false);
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobOutput(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Graph, NevercJobID Job, uint64_t Index,
    NevercJobFile *OutFile) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return getJobFile(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Frame,
                    Graph, Job, Index, OutFile, true);
}

NevercStatus NEVERC_CALL DriverAPIBridge::createJobGraphBuilder(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle ActionGraph,
    NevercJobGraphBuilderHandle *OutBuilder) {
  if (!Context || !OutBuilder)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuilder = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const DriverActionGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveActionGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, ActionGraph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Edit = DriverJobGraphEdit::createBuilder(*Artifact);
  if (!Edit)
    return editError(Edit.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto *Bound = new (std::nothrow)
      BoundJobGraphEdit{std::move(*Edit), Frame, nullptr,
                        std::this_thread::get_id()};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverJobGraphBuilderHandleKind, Bound,
      [](void *Raw) { delete static_cast<BoundJobGraphEdit *>(Raw); });
  if (!Handle) {
    delete Bound;
    return editError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuilder = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::beginJobGraphMutation(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Graph,
    NevercJobGraphMutationHandle *OutMutation) {
  if (!Context || !OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask) ||
      !Bridge.ActiveExecutor->isActiveContinuation(Frame, Continuation))
    return bridgeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  DriverJobGraphArtifact *Artifact = nullptr;
  NevercStatus Status = resolveJobGraph(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Graph, &Artifact);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Edit = Artifact->beginMutation();
  if (!Edit)
    return editError(Edit.takeError(), NEVERC_STATUS_INVALID_STATE);
  auto *Bound = new (std::nothrow)
      BoundJobGraphEdit{std::move(*Edit), Frame, Continuation,
                        std::this_thread::get_id()};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverJobGraphMutationHandleKind, Bound,
      [](void *Raw) { delete static_cast<BoundJobGraphEdit *>(Raw); });
  if (!Handle) {
    delete Bound;
    return editError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMutation = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::addJob(
    void *Context, NevercHandle Edit,
    const NevercJobDescriptor *Descriptor, NevercJobID *OutJob) {
  if (!Context || !OutJob)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutJob = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphNode Node;
  NevercStatus Status =
      parseJobDescriptor(Descriptor, *Bridge.ActiveTask, Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Job = Bound->Edit->addJob(std::move(Node));
  if (!Job)
    return editError(Job.takeError());
  *OutJob = *Job;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::removeJob(
    void *Context, NevercHandle Edit, NevercJobID Job) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->removeJob(Job))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::moveJobBefore(
    void *Context, NevercHandle Edit, NevercJobID Job,
    NevercJobID Before) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->moveJobBefore(Job, Before))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::replaceJob(
    void *Context, NevercHandle Edit, NevercJobID Job,
    const NevercJobDescriptor *Descriptor) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphNode Node;
  NevercStatus Status =
      parseJobDescriptor(Descriptor, *Bridge.ActiveTask, Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->replaceJob(Job, std::move(Node)))
    return editError(std::move(E));
  return neverc_status_ok();
}

static NevercStatus setJobString(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, NevercHandle Edit, NevercJobID Job,
    uint64_t Index, NevercStringView Value, bool Environment) {
  StringRef Text;
  if (!viewToStringRef(Value, Text))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      Executor, Task, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Error E = Environment ? Bound->Edit->setEnvironment(Job, Index, Text)
                        : Bound->Edit->setArgument(Job, Index, Text);
  if (E)
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setJobArgument(
    void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
    NevercStringView Value) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return setJobString(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit,
                      Job, Index, Value, false);
}

NevercStatus NEVERC_CALL DriverAPIBridge::setJobEnvironment(
    void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
    NevercStringView Value) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return setJobString(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit,
                      Job, Index, Value, true);
}

static NevercStatus setJobFile(
    plugin::PluginPhaseExecutor &Executor,
    plugin::PluginTaskContext &Task, NevercHandle Edit, NevercJobID Job,
    uint64_t Index, const NevercJobFile *File, bool Output) {
  if (!File)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  DriverJobFileRecord Record;
  if (!jobFileToRecord(*File, Record))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      Executor, Task, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Error E = Output ? Bound->Edit->setOutput(Job, Index, std::move(Record))
                   : Bound->Edit->setInput(Job, Index, std::move(Record));
  if (E)
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setJobInput(
    void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
    const NevercJobFile *File) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return setJobFile(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, Job,
                    Index, File, false);
}

NevercStatus NEVERC_CALL DriverAPIBridge::setJobOutput(
    void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
    const NevercJobFile *File) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  return setJobFile(*Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, Job,
                    Index, File, true);
}

NevercStatus NEVERC_CALL DriverAPIBridge::replaceJobDependencies(
    void *Context, NevercHandle Edit, NevercJobID Job,
    NevercJobIDList DependencyList) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::vector<NevercJobID> Dependencies;
  if (!jobIDListToVector(DependencyList, Dependencies))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->replaceDependencies(Job, Dependencies))
    return editError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::publishJobGraph(
    void *Context, const NevercPhaseFrame *Frame,
    NevercJobGraphBuilderHandle Builder,
    NevercArtifactHandle *OutGraph) {
  if (!Context || !OutGraph)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutGraph = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundJobGraphEdit *Bound = nullptr;
  NevercStatus Status = resolveEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Builder,
      DriverJobGraphBuilderHandleKind, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Bound->Frame != Frame)
    return bridgeStatus(NEVERC_STATUS_WRONG_SCOPE);
  auto Artifact = Bound->Edit->finishBuilder();
  if (!Artifact)
    return editError(Artifact.takeError(), NEVERC_STATUS_INVALID_STATE);
  DriverJobGraphArtifact *Payload = Artifact->release();
  auto Candidate = Bridge.ActiveExecutor->createCandidate(
      *Bridge.ActiveTask, driverJobGraphArtifactID(), Payload);
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  NevercStatus Release = Bridge.ActiveTask->handles().release(
      Builder, DriverJobGraphBuilderHandleKind);
  if (Release.Code != NEVERC_STATUS_OK)
    return Release;
  *OutGraph = *Candidate;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::commitJobGraphMutation(
    void *Context, NevercJobGraphMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundJobGraphEdit *Bound = nullptr;
  NevercStatus Status = resolveEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation,
      DriverJobGraphMutationHandleKind, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Edit->commitMutation())
    return editError(std::move(E), NEVERC_STATUS_VERIFICATION_FAILED);
  return Bridge.ActiveTask->handles().release(
      Mutation, DriverJobGraphMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::abortJobGraphEdit(
    void *Context, NevercHandle Edit) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundJobGraphEdit *Bound = nullptr;
  plugin::PluginHandleKind Kind = 0;
  NevercStatus Status = resolveAnyEdit(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Edit, &Bound, Kind);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Bound->Edit->abort();
  return Bridge.ActiveTask->handles().release(Edit, Kind);
}

} // namespace neverc::driver
