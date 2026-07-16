#include "Plugin/DriverAPIBridge.h"
#include "Plugin/DriverArtifacts.h"
#include "Plugin/ToolChainPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/JSON.h"
#include <cstring>
#include <limits>
#include <memory>
#include <new>

using namespace llvm;

namespace neverc::driver {
namespace {

constexpr plugin::PluginHandleKind DriverArgumentMutationHandleKind = 5;
constexpr plugin::PluginHandleKind DriverParsedArgumentMutationHandleKind = 6;
constexpr plugin::PluginHandleKind DriverToolChainMutationHandleKind = 7;

struct BoundArgumentMutation {
  std::unique_ptr<DriverArgumentMutation> Mutation;
  const NevercPhaseFrame *Frame = nullptr;
  const NevercPhaseContinuation *Continuation = nullptr;
};

struct BoundParsedArgumentMutation {
  std::unique_ptr<DriverParsedArgumentMutation> Mutation;
  const NevercPhaseFrame *Frame = nullptr;
  const NevercPhaseContinuation *Continuation = nullptr;
};

struct BoundToolChainMutation {
  std::unique_ptr<DriverToolChainMutation> Mutation;
  const NevercPhaseFrame *Frame = nullptr;
  const NevercPhaseContinuation *Continuation = nullptr;
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
                NevercInterfaceID ExpectedPhase) {
  return Frame && Frame->Header.StructSize >= sizeof(*Frame) &&
         Frame->Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Frame->Header.Flags == 0 && sameHandle(Frame->Task, Task.handle()) &&
         plugin::samePluginInterfaceID(Frame->Phase, ExpectedPhase);
}

bool viewToStringRef(NevercStringView View, StringRef &Out) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  Out = StringRef(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  return Out.size() <= (UINT64_C(1) << 20) && !Out.contains('\0') &&
         json::isUTF8(Out);
}

bool listToStringRefs(NevercStringList Values, std::vector<StringRef> &Out) {
  Out.clear();
  if (Values.Count == 0)
    return true;
  if (Values.Count > UINT64_C(1024) || !Values.Data ||
      Values.ElementStride < sizeof(NevercStringView) ||
      Values.ElementStride > std::numeric_limits<size_t>::max() ||
      Values.Count - 1 >
          std::numeric_limits<size_t>::max() / Values.ElementStride)
    return false;
  Out.reserve(static_cast<size_t>(Values.Count));
  const auto *Bytes = reinterpret_cast<const unsigned char *>(Values.Data);
  for (uint64_t Index = 0; Index != Values.Count; ++Index) {
    NevercStringView View{};
    std::memcpy(&View,
                Bytes + static_cast<size_t>(Index) *
                            static_cast<size_t>(Values.ElementStride),
                sizeof(View));
    StringRef Text;
    if (!viewToStringRef(View, Text))
      return false;
    Out.push_back(Text);
  }
  return true;
}

NevercStatus
mutationError(Error E, NevercStatusCode Code = NEVERC_STATUS_POLICY_VIOLATION) {
  consumeError(std::move(E));
  return bridgeStatus(Code);
}

NevercStatus resolveMutation(plugin::PluginPhaseExecutor &Executor,
                             plugin::PluginTaskContext &Task,
                             NevercArgumentMutationHandle Handle,
                             BoundArgumentMutation **OutMutation) {
  if (!OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, DriverArgumentMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Mutation = static_cast<BoundArgumentMutation *>(Payload);
  if (!Mutation || !Mutation->Mutation ||
      !Executor.isActiveContinuation(Mutation->Frame, Mutation->Continuation))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  *OutMutation = Mutation;
  return neverc_status_ok();
}

NevercStatus resolveParsedMutation(plugin::PluginPhaseExecutor &Executor,
                                   plugin::PluginTaskContext &Task,
                                   NevercParsedArgumentMutationHandle Handle,
                                   BoundParsedArgumentMutation **OutMutation) {
  if (!OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, DriverParsedArgumentMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Mutation = static_cast<BoundParsedArgumentMutation *>(Payload);
  if (!Mutation || !Mutation->Mutation ||
      !Executor.isActiveContinuation(Mutation->Frame, Mutation->Continuation))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  *OutMutation = Mutation;
  return neverc_status_ok();
}

NevercStatus resolveToolChainMutation(plugin::PluginPhaseExecutor &Executor,
                                      plugin::PluginTaskContext &Task,
                                      NevercToolChainMutationHandle Handle,
                                      BoundToolChainMutation **OutMutation) {
  if (!OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, DriverToolChainMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Mutation = static_cast<BoundToolChainMutation *>(Payload);
  if (!Mutation || !Mutation->Mutation ||
      !Executor.isActiveContinuation(Mutation->Frame, Mutation->Continuation))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  *OutMutation = Mutation;
  return neverc_status_ok();
}

} // namespace

DriverAPIBridge::DriverAPIBridge() {
  API.Header = {sizeof(API), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
                0};
  API.Context = this;
  API.GetArgumentCount = getArgumentCount;
  API.GetArgument = getArgument;
  API.BeginArgumentMutation = beginArgumentMutation;
  API.InsertArgument = insertArgument;
  API.ReplaceArgument = replaceArgument;
  API.EraseArgument = eraseArgument;
  API.CommitArgumentMutation = commitArgumentMutation;
  API.AbortArgumentMutation = abortArgumentMutation;
  API.GetOptionOccurrenceCount = getOptionOccurrenceCount;
  API.GetOptionOccurrence = getOptionOccurrence;
  API.BeginParsedArgumentMutation = beginParsedArgumentMutation;
  API.AddOptionOccurrence = addOptionOccurrence;
  API.RemoveOptionOccurrence = removeOptionOccurrence;
  API.ReplaceOptionOccurrence = replaceOptionOccurrence;
  API.CommitParsedArgumentMutation = commitParsedArgumentMutation;
  API.AbortParsedArgumentMutation = abortParsedArgumentMutation;
  API.GetToolChainRequest = getToolChainRequest;
  API.BeginToolChainMutation = beginToolChainMutation;
  API.SetToolChainTriple = setToolChainTriple;
  API.SetToolChainCPU = setToolChainCPU;
  API.SetToolChainFeatures = setToolChainFeatures;
  API.CommitToolChainMutation = commitToolChainMutation;
  API.AbortToolChainMutation = abortToolChainMutation;
  API.CreateToolChainSelection = createToolChainSelection;
  API.GetToolChainSelection = getToolChainSelection;
  API.GetDriverInputCount = getDriverInputCount;
  API.GetDriverInput = getDriverInput;
  API.GetActionNodeCount = getActionNodeCount;
  API.GetActionNode = getActionNode;
  API.GetActionNodeInput = getActionNodeInput;
  API.GetActionRootCount = getActionRootCount;
  API.GetActionRoot = getActionRoot;
  API.CreateActionGraphBuilder = createActionGraphBuilder;
  API.BeginActionGraphMutation = beginActionGraphMutation;
  API.AddActionNode = addActionNode;
  API.RemoveActionNode = removeActionNode;
  API.ReplaceActionNodeInputs = replaceActionNodeInputs;
  API.SetActionNodeOutputType = setActionNodeOutputType;
  API.SetActionNodeBindArch = setActionNodeBindArch;
  API.SetActionRoots = setActionRoots;
  API.PublishActionGraph = publishActionGraph;
  API.CommitActionGraphMutation = commitActionGraphMutation;
  API.AbortActionGraphEdit = abortActionGraphEdit;
  API.GetJobCount = getJobCount;
  API.GetJob = getJob;
  API.GetJobDependency = getJobDependency;
  API.GetJobArgument = getJobArgument;
  API.GetJobEnvironment = getJobEnvironment;
  API.GetJobInput = getJobInput;
  API.GetJobOutput = getJobOutput;
  API.CreateJobGraphBuilder = createJobGraphBuilder;
  API.BeginJobGraphMutation = beginJobGraphMutation;
  API.AddJob = addJob;
  API.RemoveJob = removeJob;
  API.MoveJobBefore = moveJobBefore;
  API.ReplaceJob = replaceJob;
  API.SetJobArgument = setJobArgument;
  API.SetJobEnvironment = setJobEnvironment;
  API.SetJobInput = setJobInput;
  API.SetJobOutput = setJobOutput;
  API.ReplaceJobDependencies = replaceJobDependencies;
  API.PublishJobGraph = publishJobGraph;
  API.CommitJobGraphMutation = commitJobGraphMutation;
  API.AbortJobGraphEdit = abortJobGraphEdit;
  API.GetJobExecutionRequest = getJobExecutionRequest;
  API.CreateJobResult = createJobResult;
  API.GetJobResult = getJobResult;
}

Error DriverAPIBridge::registerInterface(
    plugin::PluginInterfaceRegistry &Interfaces) {
  plugin::OwnedCompatibilityKey Compatibility;
  return Interfaces.registerInterface(
      {NEVERC_INTERFACE_DRIVER_HIGH, NEVERC_INTERFACE_DRIVER_LOW},
      NEVERC_INTERFACE_STABLE, &API, std::move(Compatibility));
}

Error DriverAPIBridge::bind(plugin::PluginPhaseExecutor &Executor,
                            plugin::PluginTaskContext &Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (ActiveExecutor || ActiveTask)
    return createStringError(inconvertibleErrorCode(),
                             "Driver API bridge is already bound");
  ActiveExecutor = &Executor;
  ActiveTask = &Task;
  return Error::success();
}

void DriverAPIBridge::unbind() {
  std::lock_guard<std::mutex> Lock(Mutex);
  ActiveExecutor = nullptr;
  ActiveTask = nullptr;
}

NevercStatus NEVERC_CALL DriverAPIBridge::getArgumentCount(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t *OutCount) {
  if (!Context || !OutCount)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverRawArgumentsPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Arguments, driverRawArgumentsArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount =
      static_cast<const DriverRawArgumentsArtifact *>(Payload)->tokens().size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getArgument(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t Index, NevercStringView *OutValue,
    NevercArgumentOrigin *OutOrigin, NevercStringView *OutSource,
    uint64_t *OutPosition) {
  if (!Context || !OutValue)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = {};
  if (OutOrigin)
    *OutOrigin = NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE;
  if (OutSource)
    *OutSource = {};
  if (OutPosition)
    *OutPosition = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverRawArgumentsPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Arguments, driverRawArgumentsArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ArrayRef<DriverArgumentToken> Tokens =
      static_cast<const DriverRawArgumentsArtifact *>(Payload)->tokens();
  if (Index >= Tokens.size())
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const DriverArgumentToken &Token = Tokens[static_cast<size_t>(Index)];
  *OutValue = {Token.Value.data(), Token.Value.size()};
  if (OutOrigin)
    *OutOrigin = Token.Origin;
  if (OutSource)
    *OutSource = {Token.Source.data(), Token.Source.size()};
  if (OutPosition)
    *OutPosition = Token.Position;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::beginArgumentMutation(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Arguments,
    NevercArgumentMutationHandle *OutMutation) {
  if (!Context || !OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverRawArgumentsPhaseID()) ||
      !Bridge.ActiveExecutor->isActiveContinuation(Frame, Continuation))
    return bridgeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Arguments, driverRawArgumentsArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Mutation =
      DriverArgumentMutation::create(*const_cast<DriverRawArgumentsArtifact *>(
          static_cast<const DriverRawArgumentsArtifact *>(Payload)));
  if (!Mutation)
    return mutationError(Mutation.takeError(), NEVERC_STATUS_INVALID_STATE);
  auto *Bound = new (std::nothrow)
      BoundArgumentMutation{std::move(*Mutation), Frame, Continuation};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverArgumentMutationHandleKind, Bound,
      [](void *Raw) { delete static_cast<BoundArgumentMutation *>(Raw); });
  if (!Handle) {
    delete Bound;
    return mutationError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMutation = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::insertArgument(
    void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index,
    NevercStringView Value) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef Text;
  if (!viewToStringRef(Value, Text))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveMutation(*Bridge.ActiveExecutor,
                                        *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->insert(Index, Text))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::replaceArgument(
    void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index,
    NevercStringView Value) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef Text;
  if (!viewToStringRef(Value, Text))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveMutation(*Bridge.ActiveExecutor,
                                        *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->replace(Index, Text))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::eraseArgument(
    void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveMutation(*Bridge.ActiveExecutor,
                                        *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->erase(Index))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::commitArgumentMutation(
    void *Context, NevercArgumentMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveMutation(*Bridge.ActiveExecutor,
                                        *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->commit())
    return mutationError(std::move(E), NEVERC_STATUS_VERIFICATION_FAILED);
  return Bridge.ActiveTask->handles().release(Mutation,
                                              DriverArgumentMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::abortArgumentMutation(
    void *Context, NevercArgumentMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveTask->handles().resolve(
      Mutation, DriverArgumentMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Bound = static_cast<BoundArgumentMutation *>(Payload);
  if (Bound && Bound->Mutation)
    Bound->Mutation->abort();
  return Bridge.ActiveTask->handles().release(Mutation,
                                              DriverArgumentMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::getOptionOccurrenceCount(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t *OutCount) {
  if (!Context || !OutCount)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverParsedArgumentsPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Arguments, driverParsedArgumentsArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = static_cast<const DriverParsedArgumentsArtifact *>(Payload)
                  ->occurrences()
                  .size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getOptionOccurrence(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t Index,
    NevercOptionOccurrence *OutOccurrence) {
  if (!Context || !OutOccurrence ||
      OutOccurrence->Header.StructSize < sizeof(*OutOccurrence) ||
      OutOccurrence->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOccurrence = {};
  OutOccurrence->Header = {sizeof(*OutOccurrence), NEVERC_DRIVER_API_MAJOR,
                           NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverParsedArgumentsPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Arguments, driverParsedArgumentsArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ArrayRef<DriverParsedOptionOccurrence> Occurrences =
      static_cast<const DriverParsedArgumentsArtifact *>(Payload)
          ->occurrences();
  if (Index >= Occurrences.size())
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const DriverParsedOptionOccurrence &Occurrence =
      Occurrences[static_cast<size_t>(Index)];
  OutOccurrence->Occurrence = Occurrence.ID;
  OutOccurrence->Spelling = {Occurrence.Spelling.data(),
                             Occurrence.Spelling.size()};
  OutOccurrence->Values = {Occurrence.ValueViews.data(),
                           Occurrence.ValueViews.size(),
                           sizeof(NevercStringView)};
  OutOccurrence->Origin = Occurrence.Origin;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::beginParsedArgumentMutation(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Arguments,
    NevercParsedArgumentMutationHandle *OutMutation) {
  if (!Context || !OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverParsedArgumentsPhaseID()) ||
      !Bridge.ActiveExecutor->isActiveContinuation(Frame, Continuation))
    return bridgeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Arguments, driverParsedArgumentsArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Mutation = DriverParsedArgumentMutation::create(
      *const_cast<DriverParsedArgumentsArtifact *>(
          static_cast<const DriverParsedArgumentsArtifact *>(Payload)));
  if (!Mutation)
    return mutationError(Mutation.takeError(), NEVERC_STATUS_INVALID_STATE);
  auto *Bound = new (std::nothrow)
      BoundParsedArgumentMutation{std::move(*Mutation), Frame, Continuation};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverParsedArgumentMutationHandleKind, Bound, [](void *Raw) {
        delete static_cast<BoundParsedArgumentMutation *>(Raw);
      });
  if (!Handle) {
    delete Bound;
    return mutationError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMutation = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::addOptionOccurrence(
    void *Context, NevercParsedArgumentMutationHandle Mutation,
    NevercStringView Spelling, NevercStringList Values) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef SpellingText;
  std::vector<StringRef> ValueTexts;
  if (!viewToStringRef(Spelling, SpellingText) ||
      !listToStringRefs(Values, ValueTexts))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundParsedArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveParsedMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->add(SpellingText, ValueTexts))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::removeOptionOccurrence(
    void *Context, NevercParsedArgumentMutationHandle Mutation,
    uint64_t Occurrence) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundParsedArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveParsedMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->remove(Occurrence))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::replaceOptionOccurrence(
    void *Context, NevercParsedArgumentMutationHandle Mutation,
    uint64_t Occurrence, NevercStringView Spelling, NevercStringList Values) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef SpellingText;
  std::vector<StringRef> ValueTexts;
  if (!viewToStringRef(Spelling, SpellingText) ||
      !listToStringRefs(Values, ValueTexts))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundParsedArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveParsedMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->replace(Occurrence, SpellingText, ValueTexts))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::commitParsedArgumentMutation(
    void *Context, NevercParsedArgumentMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundParsedArgumentMutation *Bound = nullptr;
  NevercStatus Status = resolveParsedMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->commit())
    return mutationError(std::move(E), NEVERC_STATUS_VERIFICATION_FAILED);
  return Bridge.ActiveTask->handles().release(
      Mutation, DriverParsedArgumentMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::abortParsedArgumentMutation(
    void *Context, NevercParsedArgumentMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveTask->handles().resolve(
      Mutation, DriverParsedArgumentMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Bound = static_cast<BoundParsedArgumentMutation *>(Payload);
  if (Bound && Bound->Mutation)
    Bound->Mutation->abort();
  return Bridge.ActiveTask->handles().release(
      Mutation, DriverParsedArgumentMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::getToolChainRequest(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Request,
    NevercToolChainRequest *OutRequest) {
  if (!Context || !OutRequest ||
      OutRequest->Header.StructSize < sizeof(*OutRequest) ||
      OutRequest->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRequest = {};
  OutRequest->Header = {sizeof(*OutRequest), NEVERC_DRIVER_API_MAJOR,
                        NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverSelectToolChainPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Request, driverToolChainRequestArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  static_cast<const DriverToolChainRequestArtifact *>(Payload)->describe(
      *OutRequest);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::beginToolChainMutation(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Request,
    NevercToolChainMutationHandle *OutMutation) {
  if (!Context || !OutMutation)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMutation = {};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverSelectToolChainPhaseID()) ||
      !Bridge.ActiveExecutor->isActiveContinuation(Frame, Continuation))
    return bridgeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Request, driverToolChainRequestArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Mutation =
      const_cast<DriverToolChainRequestArtifact *>(
          static_cast<const DriverToolChainRequestArtifact *>(Payload))
          ->beginMutation();
  if (!Mutation)
    return mutationError(Mutation.takeError(), NEVERC_STATUS_INVALID_STATE);
  auto *Bound = new (std::nothrow)
      BoundToolChainMutation{std::move(*Mutation), Frame, Continuation};
  if (!Bound)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.ActiveTask->handles().create(
      DriverToolChainMutationHandleKind, Bound,
      [](void *Raw) { delete static_cast<BoundToolChainMutation *>(Raw); });
  if (!Handle) {
    delete Bound;
    return mutationError(Handle.takeError(), NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMutation = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setToolChainTriple(
    void *Context, NevercToolChainMutationHandle Mutation,
    NevercStringView Triple) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef TripleText;
  if (!viewToStringRef(Triple, TripleText))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundToolChainMutation *Bound = nullptr;
  NevercStatus Status = resolveToolChainMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->setTriple(TripleText))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setToolChainCPU(
    void *Context, NevercToolChainMutationHandle Mutation,
    NevercStringView CPU) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef CPUText;
  if (!viewToStringRef(CPU, CPUText))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundToolChainMutation *Bound = nullptr;
  NevercStatus Status = resolveToolChainMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->setCPU(CPUText))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::setToolChainFeatures(
    void *Context, NevercToolChainMutationHandle Mutation,
    NevercStringList Features) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::vector<StringRef> FeatureTexts;
  if (!listToStringRefs(Features, FeatureTexts))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundToolChainMutation *Bound = nullptr;
  NevercStatus Status = resolveToolChainMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->setFeatures(FeatureTexts))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::commitToolChainMutation(
    void *Context, NevercToolChainMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  BoundToolChainMutation *Bound = nullptr;
  NevercStatus Status = resolveToolChainMutation(
      *Bridge.ActiveExecutor, *Bridge.ActiveTask, Mutation, &Bound);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Bound->Mutation->commit())
    return mutationError(std::move(E), NEVERC_STATUS_VERIFICATION_FAILED);
  return Bridge.ActiveTask->handles().release(
      Mutation, DriverToolChainMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::abortToolChainMutation(
    void *Context, NevercToolChainMutationHandle Mutation) {
  if (!Context)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask)
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveTask->handles().resolve(
      Mutation, DriverToolChainMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Bound = static_cast<BoundToolChainMutation *>(Payload);
  if (Bound && Bound->Mutation)
    Bound->Mutation->abort();
  return Bridge.ActiveTask->handles().release(
      Mutation, DriverToolChainMutationHandleKind);
}

NevercStatus NEVERC_CALL DriverAPIBridge::createToolChainSelection(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Request,
    const NevercToolChainSelectionDescriptor *Descriptor,
    NevercArtifactHandle *OutSelection) {
  if (!Context || !Descriptor || !OutSelection ||
      Descriptor->Header.StructSize < sizeof(*Descriptor) ||
      Descriptor->Header.Major != NEVERC_DRIVER_API_MAJOR ||
      Descriptor->Header.Flags != 0)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSelection = {};
  StringRef ToolChainID;
  StringRef TargetKey;
  StringRef TargetTriple;
  StringRef CPU;
  std::vector<StringRef> FeatureTexts;
  if (!viewToStringRef(Descriptor->ToolChainID, ToolChainID) ||
      !viewToStringRef(Descriptor->TargetKey, TargetKey) ||
      !viewToStringRef(Descriptor->TargetTriple, TargetTriple) ||
      !viewToStringRef(Descriptor->CPU, CPU) ||
      !listToStringRefs(Descriptor->Features, FeatureTexts))
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverSelectToolChainPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *RequestPayload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Request, driverToolChainRequestArtifactID(),
      &RequestPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  std::vector<std::string> Features;
  Features.reserve(FeatureTexts.size());
  for (StringRef Feature : FeatureTexts)
    Features.push_back(Feature.str());
  auto *Payload = new (std::nothrow) DriverToolChainSelectionArtifact();
  if (!Payload)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Payload->set(ToolChainID.str(), TargetKey.str(), TargetTriple.str(),
               CPU.str(), std::move(Features), Descriptor->Provider, false);
  auto Candidate = Bridge.ActiveExecutor->createCandidate(
      *Bridge.ActiveTask, driverToolChainSelectionArtifactID(), Payload);
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutSelection = *Candidate;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getToolChainSelection(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Selection, NevercToolChainSelection *OutSelection) {
  if (!Context || !OutSelection ||
      OutSelection->Header.StructSize < sizeof(*OutSelection) ||
      OutSelection->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSelection = {};
  OutSelection->Header = {sizeof(*OutSelection), NEVERC_DRIVER_API_MAJOR,
                          NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask, driverSelectToolChainPhaseID()))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Selection, driverToolChainSelectionArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Artifact =
      *static_cast<const DriverToolChainSelectionArtifact *>(Payload);
  OutSelection->ToolChainID = {Artifact.ToolChainID.data(),
                               Artifact.ToolChainID.size()};
  OutSelection->TargetKey = {Artifact.TargetKey.data(),
                             Artifact.TargetKey.size()};
  OutSelection->TargetTriple = {Artifact.TargetTriple.data(),
                                Artifact.TargetTriple.size()};
  OutSelection->CPU = {Artifact.CPU.data(), Artifact.CPU.size()};
  OutSelection->Features = {Artifact.FeatureViews.data(),
                            Artifact.FeatureViews.size(),
                            sizeof(NevercStringView)};
  OutSelection->Provider = Artifact.Provider;
  OutSelection->BuiltinProviderUsed =
      Artifact.BuiltinProviderUsed ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

} // namespace neverc::driver
