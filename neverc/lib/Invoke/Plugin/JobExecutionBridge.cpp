#include "Plugin/JobExecutionBridge.h"
#include "Plugin/DriverAPIBridge.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/JSON.h"
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>

using namespace llvm;

namespace neverc::driver {
namespace {

constexpr uint64_t MaximumJobExecutionStringBytes = UINT64_C(1) << 20;
constexpr uint64_t MaximumJobOutputSeals = UINT64_C(1) << 16;

Error jobExecutionError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus bridgeStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validText(StringRef Text, bool AllowEmpty) {
  return (AllowEmpty || !Text.empty()) &&
         Text.size() <= MaximumJobExecutionStringBytes &&
         !Text.contains('\0') && json::isUTF8(Text);
}

bool viewToString(NevercStringView View, std::string &Out) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  StringRef Text(View.Data ? View.Data : "",
                 static_cast<size_t>(View.Length));
  if (!validText(Text, true))
    return false;
  Out = Text.str();
  return true;
}

bool validFrame(const NevercPhaseFrame *Frame,
                const plugin::PluginTaskContext &Task) {
  return Frame && Frame->Header.StructSize >= sizeof(*Frame) &&
         Frame->Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Frame->Header.Flags == 0 &&
         sameHandle(Frame->Task, Task.handle()) &&
         plugin::samePluginInterfaceID(Frame->Phase,
                                       driverExecuteJobPhaseID());
}

NevercJobFile makeFileView(const DriverJobFileRecord &File) {
  NevercJobFile Result{};
  Result.Header = {sizeof(Result), NEVERC_DRIVER_API_MAJOR,
                   NEVERC_DRIVER_API_MINOR, 0};
  Result.Path = {File.Path.data(), File.Path.size()};
  Result.Type = File.PublicType;
  return Result;
}

struct ParsedJobResultDescriptor {
  DriverJobExecutionOutcome Outcome;
  std::vector<NevercOutputSealHandle> OutputSeals;
};

Expected<ParsedJobResultDescriptor>
parseResultDescriptor(const NevercJobResultDescriptor &Descriptor) {
  if (Descriptor.Header.StructSize < sizeof(Descriptor) ||
      Descriptor.Header.Major != NEVERC_DRIVER_API_MAJOR ||
      Descriptor.Header.Minor > NEVERC_DRIVER_API_MINOR ||
      Descriptor.Header.Flags != 0 || Descriptor.Reserved != 0 ||
      (Descriptor.ExecutionFailed != NEVERC_FALSE &&
       Descriptor.ExecutionFailed != NEVERC_TRUE) ||
      (Descriptor.HasProcessStatistics != NEVERC_FALSE &&
       Descriptor.HasProcessStatistics != NEVERC_TRUE))
    return jobExecutionError("job result descriptor is invalid");

  ParsedJobResultDescriptor Parsed;
  Parsed.Outcome.ExitCode = Descriptor.ExitCode;
  Parsed.Outcome.ExecutionFailed =
      Descriptor.ExecutionFailed == NEVERC_TRUE;
  Parsed.Outcome.HasProcessStatistics =
      Descriptor.HasProcessStatistics == NEVERC_TRUE;
  Parsed.Outcome.TotalTimeMicroseconds =
      Descriptor.TotalTimeMicroseconds;
  Parsed.Outcome.UserTimeMicroseconds =
      Descriptor.UserTimeMicroseconds;
  Parsed.Outcome.PeakMemoryKiB = Descriptor.PeakMemoryKiB;
  if (!viewToString(Descriptor.ErrorMessage,
                    Parsed.Outcome.ErrorMessage))
    return jobExecutionError("job result error message is invalid");

  const NevercOutputSealList &List = Descriptor.OutputSeals;
  if (List.Count > MaximumJobOutputSeals)
    return jobExecutionError("job result has too many output seals");
  if (List.Count == 0) {
    if (List.Data != nullptr)
      return jobExecutionError(
          "empty job output seal list must have null data");
    return Parsed;
  }
  if (!List.Data ||
      List.ElementStride < sizeof(NevercOutputSealHandle) ||
      List.ElementStride > std::numeric_limits<size_t>::max() ||
      List.Count > std::numeric_limits<size_t>::max())
    return jobExecutionError("job output seal list is malformed");
  const size_t Count = static_cast<size_t>(List.Count);
  const size_t Stride = static_cast<size_t>(List.ElementStride);
  if (Count - 1 >
      (std::numeric_limits<size_t>::max() -
       sizeof(NevercOutputSealHandle)) /
          Stride)
    return jobExecutionError("job output seal list size overflows");

  const auto *Bytes =
      reinterpret_cast<const uint8_t *>(List.Data);
  Parsed.OutputSeals.reserve(Count);
  for (size_t Index = 0; Index != Count; ++Index) {
    NevercOutputSealHandle Seal{};
    std::memcpy(&Seal, Bytes + Index * Stride, sizeof(Seal));
    if (Seal.Owner == 0 || Seal.Value == 0)
      return jobExecutionError(
          "job output seal list contains an invalid handle");
    Parsed.OutputSeals.push_back(Seal);
  }
  return Parsed;
}

} // namespace

DriverJobRequestArtifact::DriverJobRequestArtifact(
    DriverJobGraphNode JobValue)
    : Job(std::move(JobValue)) {
  Job.Original = nullptr;
  rebuildViews();
}

void DriverJobRequestArtifact::rebuildViews() {
  ArgumentViews.clear();
  ArgumentViews.reserve(Job.Arguments.size());
  for (const std::string &Argument : Job.Arguments)
    ArgumentViews.push_back({Argument.data(), Argument.size()});

  EnvironmentViews.clear();
  EnvironmentViews.reserve(Job.Environment.size());
  for (const std::string &Entry : Job.Environment)
    EnvironmentViews.push_back({Entry.data(), Entry.size()});

  InputViews.clear();
  InputViews.reserve(Job.Inputs.size());
  for (const DriverJobFileRecord &Input : Job.Inputs)
    InputViews.push_back(makeFileView(Input));

  OutputViews.clear();
  OutputViews.reserve(Job.Outputs.size());
  for (const DriverJobFileRecord &Output : Job.Outputs)
    OutputViews.push_back(makeFileView(Output));
}

Error DriverJobRequestArtifact::verify() const {
  if (Job.ID == 0 || Job.SourceAction == 0)
    return jobExecutionError("job execution request identity is invalid");
  if (Job.Kind < NEVERC_JOB_COMMAND || Job.Kind > NEVERC_JOB_DYNCODE)
    return jobExecutionError("job execution request kind is invalid");
  if (Job.Kind == NEVERC_JOB_PLUGIN) {
    if (!Job.InProcess || !Job.Callback ||
        !validText(Job.PluginID, false) ||
        !validText(Job.CallbackID, false))
      return jobExecutionError(
          "plugin job execution request is invalid");
  } else if (!validText(Job.Executable, false)) {
    return jobExecutionError(
        "builtin job execution request has no executable");
  }
  for (const std::string &Argument : Job.Arguments)
    if (!validText(Argument, true))
      return jobExecutionError(
          "job execution request argument is invalid");
  for (const std::string &Entry : Job.Environment)
    if (!validText(Entry, false))
      return jobExecutionError(
          "job execution request environment is invalid");
  auto VerifyFiles = [](ArrayRef<DriverJobFileRecord> Files) -> Error {
    for (const DriverJobFileRecord &File : Files) {
      if (!validText(File.Path, false) ||
          File.PublicType == NEVERC_DRIVER_TYPE_INVALID)
        return jobExecutionError(
            "job execution request file is invalid");
    }
    return Error::success();
  };
  if (Error E = VerifyFiles(Job.Inputs))
    return E;
  if (Error E = VerifyFiles(Job.Outputs))
    return E;
  DenseSet<NevercJobID> Dependencies;
  for (NevercJobID Dependency : Job.Dependencies)
    if (Dependency == 0 || Dependency == Job.ID ||
        !Dependencies.insert(Dependency).second)
      return jobExecutionError(
          "job execution request dependency is invalid");
  return Error::success();
}

void DriverJobRequestArtifact::describe(
    NevercJobExecutionRequest &OutRequest) const {
  OutRequest.Job = {};
  OutRequest.Job.Header = {sizeof(OutRequest.Job),
                           NEVERC_DRIVER_API_MAJOR,
                           NEVERC_DRIVER_API_MINOR, 0};
  OutRequest.Job.Job = Job.ID;
  OutRequest.Job.Kind = Job.Kind;
  OutRequest.Job.ResponseFileKind = Job.ResponseFileKind;
  OutRequest.Job.ResponseFileEncoding = Job.ResponseFileEncoding;
  OutRequest.Job.InProcess =
      Job.InProcess ? NEVERC_TRUE : NEVERC_FALSE;
  OutRequest.Job.SourceAction = Job.SourceAction;
  OutRequest.Job.LinkerFlavor = Job.LinkerFlavor;
  OutRequest.Job.Executable = {Job.Executable.data(),
                               Job.Executable.size()};
  OutRequest.Job.CallbackID = {Job.CallbackID.data(),
                               Job.CallbackID.size()};
  OutRequest.Job.ArgumentCount = ArgumentViews.size();
  OutRequest.Job.EnvironmentCount = EnvironmentViews.size();
  OutRequest.Job.InputCount = InputViews.size();
  OutRequest.Job.OutputCount = OutputViews.size();
  OutRequest.Job.DependencyCount = Job.Dependencies.size();
  OutRequest.Arguments = {ArgumentViews.data(), ArgumentViews.size(),
                          sizeof(NevercStringView)};
  OutRequest.Environment = {
      EnvironmentViews.data(), EnvironmentViews.size(),
      sizeof(NevercStringView)};
  OutRequest.Inputs = {InputViews.data(), InputViews.size(),
                       sizeof(NevercJobFile)};
  OutRequest.Outputs = {OutputViews.data(), OutputViews.size(),
                        sizeof(NevercJobFile)};
  OutRequest.Dependencies = {
      Job.Dependencies.data(), Job.Dependencies.size(),
      sizeof(NevercJobID)};
}

DriverJobResultArtifact::DriverJobResultArtifact(
    const DriverJobRequestArtifact &Request,
    DriverJobExecutionOutcome OutcomeValue, bool BuiltinProviderUsedValue,
    std::vector<NevercOutputSealHandle> OutputSealsValue)
    : Job(Request.job().ID),
      DeclaredOutputs(Request.job().Outputs),
      OutputSeals(std::move(OutputSealsValue)),
      Outcome(std::move(OutcomeValue)),
      BuiltinProviderUsed(BuiltinProviderUsedValue) {}

Error DriverJobResultArtifact::verify() const {
  if (Job == 0)
    return jobExecutionError("job result has no request identity");
  if (Outcome.ExecutionFailed && Outcome.ExitCode == 0)
    return jobExecutionError(
        "execution-failed job result requires a nonzero exit code");
  if (!Outcome.ErrorMessage.empty() && Outcome.ExitCode == 0)
    return jobExecutionError(
        "job result error message requires a nonzero exit code");
  if (!validText(Outcome.ErrorMessage, true))
    return jobExecutionError("job result error message is invalid");
  if (!Outcome.HasProcessStatistics &&
      (Outcome.TotalTimeMicroseconds != 0 ||
       Outcome.UserTimeMicroseconds != 0 ||
       Outcome.PeakMemoryKiB != 0))
    return jobExecutionError(
        "job result contains undeclared process statistics");
  if (BuiltinProviderUsed && !OutputSeals.empty())
    return jobExecutionError(
        "builtin job result must not publish plugin output seals");
  const bool Succeeded =
      Outcome.ExitCode == 0 && !Outcome.ExecutionFailed;
  if (!Succeeded && !OutputSeals.empty())
    return jobExecutionError(
        "failed job result must not publish output seals");
  if (!BuiltinProviderUsed && Succeeded &&
      OutputSeals.size() != DeclaredOutputs.size())
    return jobExecutionError(
        "successful replacement must finish exactly the declared outputs");

  std::set<std::pair<uint64_t, uint64_t>> UniqueSeals;
  for (NevercOutputSealHandle Seal : OutputSeals) {
    if (Seal.Owner == 0 || Seal.Value == 0 ||
        !UniqueSeals.emplace(Seal.Owner, Seal.Value).second)
      return jobExecutionError(
          "job result contains an invalid or duplicate output seal");
  }
  return Error::success();
}

Error DriverJobResultArtifact::commitReplacementOutputs(
    plugin::PluginTaskContext &Task) const {
  bool OutputsCommitted = false;
  auto AbortUncommittedOutputs = make_scope_exit([&] {
    if (OutputsCommitted || BuiltinProviderUsed)
      return;
    for (NevercOutputSealHandle Seal : OutputSeals) {
      auto Snapshot = plugin::inspectPluginOutputSeal(Task, Seal);
      if (!Snapshot) {
        consumeError(Snapshot.takeError());
        continue;
      }
      if (Snapshot->State != NEVERC_OUTPUT_OPEN &&
          Snapshot->State != NEVERC_OUTPUT_FINISHED)
        continue;
      auto Aborted = plugin::hostAbortPluginOutput(Task, Seal);
      if (!Aborted)
        consumeError(Aborted.takeError());
    }
  });
  if (Error E = verify())
    return E;
  if (BuiltinProviderUsed || Outcome.ExitCode != 0 ||
      Outcome.ExecutionFailed)
    return Error::success();
  if (OutputSeals.empty())
    return Error::success();

  struct ExpectedOutput {
    NevercOutputKind Kind = 0;
    std::string Destination;
  };
  std::vector<ExpectedOutput> Expected;
  Expected.reserve(DeclaredOutputs.size());
  std::map<std::pair<NevercOutputKind, std::string>, size_t>
      ExpectedIndices;
  for (const DriverJobFileRecord &Output : DeclaredOutputs) {
    ExpectedOutput Entry;
    if (Output.Path == "-") {
      Entry.Kind = NEVERC_OUTPUT_STREAM;
      Entry.Destination = "stdout";
    } else {
      Entry.Kind = NEVERC_OUTPUT_FILE;
      auto Canonical =
          plugin::canonicalizePluginOutputPath(Task, Output.Path);
      if (!Canonical)
        return jobExecutionError(
            "cannot canonicalize declared job output '" + Output.Path +
            "': " + toString(Canonical.takeError()).str().str());
      Entry.Destination = std::move(*Canonical);
    }
    const auto Key =
        std::make_pair(Entry.Kind, Entry.Destination);
    if (!ExpectedIndices.emplace(Key, Expected.size()).second)
      return jobExecutionError(
          "job declares duplicate canonical output destinations");
    Expected.push_back(std::move(Entry));
  }

  std::vector<NevercOutputSealHandle> OrderedSeals(
      Expected.size(), NevercOutputSealHandle{});
  for (NevercOutputSealHandle Seal : OutputSeals) {
    auto Snapshot = plugin::inspectPluginOutputSeal(Task, Seal);
    if (!Snapshot)
      return jobExecutionError(
          "job result references an invalid output seal: " +
          toString(Snapshot.takeError()).str().str());
    if (Snapshot->State != NEVERC_OUTPUT_FINISHED ||
        Snapshot->Flags != NEVERC_OUTPUT_FLAG_NONE ||
        Snapshot->PublicationGeneration != 0)
      return jobExecutionError(
          "job result output seal is not an unpublished finished output");

    auto It = ExpectedIndices.find(
        std::make_pair(Snapshot->Kind, Snapshot->Destination));
    if (It == ExpectedIndices.end())
      return jobExecutionError(
          "job result contains an undeclared output destination");
    NevercOutputSealHandle &Matched = OrderedSeals[It->second];
    if (Matched.Owner != 0 || Matched.Value != 0)
      return jobExecutionError(
          "job result contains multiple seals for one output");
    Matched = Seal;
  }

  for (NevercOutputSealHandle Seal : OrderedSeals)
    if (Seal.Owner == 0 || Seal.Value == 0)
      return jobExecutionError(
          "job result did not finish every declared output");

  for (NevercOutputSealHandle Seal : OrderedSeals) {
    auto Summary = plugin::hostCommitPluginOutput(Task, Seal);
    if (!Summary)
      return jobExecutionError(
          "sealed output commit failed: " +
          toString(Summary.takeError()).str().str());
    if (Summary->State != NEVERC_OUTPUT_COMMITTED ||
        (Summary->Flags & NEVERC_OUTPUT_FLAG_PUBLISHED) == 0)
      return jobExecutionError(
          "sealed output commit did not publish the output");
  }
  OutputsCommitted = true;
  return Error::success();
}

void DriverJobResultArtifact::describe(NevercJobResult &OutResult) const {
  OutResult.Job = Job;
  OutResult.ExitCode = Outcome.ExitCode;
  OutResult.ExecutionFailed =
      Outcome.ExecutionFailed ? NEVERC_TRUE : NEVERC_FALSE;
  OutResult.HasProcessStatistics =
      Outcome.HasProcessStatistics ? NEVERC_TRUE : NEVERC_FALSE;
  OutResult.BuiltinProviderUsed =
      BuiltinProviderUsed ? NEVERC_TRUE : NEVERC_FALSE;
  OutResult.OutputSealCount = OutputSeals.size();
  OutResult.ErrorMessage = {Outcome.ErrorMessage.data(),
                            Outcome.ErrorMessage.size()};
  OutResult.TotalTimeMicroseconds = Outcome.TotalTimeMicroseconds;
  OutResult.UserTimeMicroseconds = Outcome.UserTimeMicroseconds;
  OutResult.PeakMemoryKiB = Outcome.PeakMemoryKiB;
}

NevercInterfaceID driverJobRequestArtifactID() {
  return {NEVERC_PHASE_DRIVER_EXECUTE_JOB_INPUT_HIGH,
          NEVERC_PHASE_DRIVER_EXECUTE_JOB_INPUT_LOW};
}

NevercInterfaceID driverJobResultArtifactID() {
  return {NEVERC_PHASE_DRIVER_EXECUTE_JOB_OUTPUT_HIGH,
          NEVERC_PHASE_DRIVER_EXECUTE_JOB_OUTPUT_LOW};
}

NevercInterfaceID driverExecuteJobPhaseID() {
  return {NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
          NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW};
}

Expected<DriverJobExecutionArtifactTypes>
registerDriverJobExecutionArtifacts(
    plugin::PluginArtifactRegistry &Registry) {
  plugin::PluginArtifactTypeDescriptor RequestDescriptor;
  RequestDescriptor.ID = driverJobRequestArtifactID();
  RequestDescriptor.Name = "neverc.driver.job_request";
  RequestDescriptor.Ownership =
      plugin::PluginArtifactOwnership::Borrowed;
  RequestDescriptor.Verify = [](const void *Payload) {
    if (!Payload)
      return jobExecutionError("job request payload is null");
    return static_cast<const DriverJobRequestArtifact *>(Payload)
        ->verify();
  };
  auto Request = Registry.registerType(std::move(RequestDescriptor));
  if (!Request)
    return Request.takeError();

  plugin::PluginArtifactTypeDescriptor ResultDescriptor;
  ResultDescriptor.ID = driverJobResultArtifactID();
  ResultDescriptor.Name = "neverc.driver.job_result";
  ResultDescriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  ResultDescriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverJobResultArtifact *>(Payload);
  };
  ResultDescriptor.Verify = [](const void *Payload) {
    if (!Payload)
      return jobExecutionError("job result payload is null");
    return static_cast<const DriverJobResultArtifact *>(Payload)
        ->verify();
  };
  auto Result = Registry.registerType(std::move(ResultDescriptor));
  if (!Result)
    return Result.takeError();
  return DriverJobExecutionArtifactTypes{*Request, *Result};
}

DriverJobExecutionRuntime::DriverJobExecutionRuntime(
    DriverAPIBridge &BridgeValue,
    std::shared_ptr<plugin::PluginSession> SessionValue,
    plugin::PluginTaskContext &TaskValue, std::string TargetTripleValue,
    std::string ObjectFormatValue,
    NevercExecutionLevel ExecutionLevelValue)
    : Bridge(BridgeValue), Session(std::move(SessionValue)),
      Task(TaskValue), TargetTriple(std::move(TargetTripleValue)),
      ObjectFormat(std::move(ObjectFormatValue)),
      ExecutionLevel(ExecutionLevelValue) {}

Expected<DriverJobExecutionOutcome>
DriverJobExecutionRuntime::execute(
    const DriverJobGraphNode &RequestNode, const Command &Job,
    ArrayRef<StringRef> Redirects, bool &OutBuiltinProviderInvoked) {
  OutBuiltinProviderInvoked = false;
  if (!Session)
    return jobExecutionError(
        "job execution phase has no plugin session");

  DriverJobRequestArtifact Request(RequestNode);
  if (Error E = Request.verify())
    return std::move(E);

  auto GraphOrError =
      plugin::PluginPhaseGraph::createBuiltinDriverGraph();
  if (!GraphOrError)
    return GraphOrError.takeError();
  plugin::PluginPhaseGraph Graph = std::move(*GraphOrError);
  plugin::PluginArtifactRegistry Artifacts;
  auto Types = registerDriverJobExecutionArtifacts(Artifacts);
  if (!Types)
    return Types.takeError();
  if (Error E = Artifacts.freeze())
    return std::move(E);

  plugin::PluginArtifactSlot OutputSlot(Types->Result);
  plugin::PluginPhaseExecutor Executor(Graph, Artifacts);
  if (Error E = Executor.importSessionRegistrations(*Session))
    return std::move(E);
  std::string BuiltinFailure;
  if (Error E = Executor.setBuiltinProvider(
          driverExecuteJobPhaseID(),
          [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
            OutBuiltinProviderInvoked = true;
            SmallString<256> ErrorMessage;
            bool ExecutionFailed = false;
            llvm::sys::ProcessInfo ProcessInfo;
            DriverJobExecutionOutcome Outcome;
            Outcome.ExitCode = Job.Execute(
                Redirects, &ErrorMessage, &ExecutionFailed, ProcessInfo);
            Outcome.ExecutionFailed = ExecutionFailed;
            Outcome.ErrorMessage.assign(ErrorMessage.begin(),
                                        ErrorMessage.end());
            if (const llvm::sys::ProcessStatistics *Statistics =
                    Job.getProcessStatistics()) {
              Outcome.HasProcessStatistics = true;
              Outcome.TotalTimeMicroseconds =
                  static_cast<uint64_t>(Statistics->TotalTime.count());
              Outcome.UserTimeMicroseconds =
                  static_cast<uint64_t>(Statistics->UserTime.count());
              Outcome.PeakMemoryKiB = Statistics->PeakMemory;
            }
            auto *Payload = new (std::nothrow)
                DriverJobResultArtifact(Request, std::move(Outcome), true);
            if (!Payload) {
              BuiltinFailure =
                  "unable to allocate builtin job result";
              return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
            }
            auto Candidate = Executor.createCandidate(
                Task, driverJobResultArtifactID(), Payload);
            if (!Candidate) {
              BuiltinFailure =
                  toString(Candidate.takeError()).str().str();
              return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
            }
            Result->Action = NEVERC_PHASE_REPLACE;
            Result->Output = *Candidate;
            return neverc_status_ok();
          }))
    return std::move(E);
  if (Error E = Executor.freeze())
    return std::move(E);

  auto Input = Executor.createArtifactView(
      Task, driverJobRequestArtifactID(), &Request, 1);
  if (!Input)
    return Input.takeError();
  bool InputReleased = false;
  auto ReleaseInput = make_scope_exit([&] {
    if (!InputReleased)
      (void)Task.handles().release(
          *Input, plugin::PluginArtifactHandleKind);
  });
  if (Error E = Bridge.bind(Executor, Task))
    return std::move(E);
  auto Unbind = make_scope_exit([&] { Bridge.unbind(); });

  NevercPhaseRoute Route{};
  Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  Route.TargetTriple = {TargetTriple.data(), TargetTriple.size()};
  Route.ObjectFormat = {ObjectFormat.data(), ObjectFormat.size()};
  Route.ExecutionLevel = ExecutionLevel;
  if (Error E = Executor.execute(
          *Session, Task, driverExecuteJobPhaseID(), Route, *Input,
          OutputSlot)) {
    if (!BuiltinFailure.empty()) {
      consumeError(std::move(E));
      return jobExecutionError(BuiltinFailure);
    }
    return jobExecutionError(
        "phase 'neverc.driver.execute_job' failed: " +
        toString(std::move(E)).str().str());
  }
  NevercStatus ReleaseStatus = Task.handles().release(
      *Input, plugin::PluginArtifactHandleKind);
  InputReleased = true;
  if (ReleaseStatus.Code != NEVERC_STATUS_OK)
    return jobExecutionError(
        "failed to retire job execution request artifact");

  const auto *Published =
      static_cast<const DriverJobResultArtifact *>(OutputSlot.payload());
  if (!Published)
    return jobExecutionError(
        "job execution phase published no result");
  if (Error E = Published->commitReplacementOutputs(Task))
    return jobExecutionError(
        "job output sealed gate failed: " +
        toString(std::move(E)).str().str());
  return Published->outcome();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobExecutionRequest(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request,
    NevercJobExecutionRequest *OutRequest) {
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
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Request, driverJobRequestArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  static_cast<const DriverJobRequestArtifact *>(Payload)->describe(
      *OutRequest);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::createJobResult(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request,
    const NevercJobResultDescriptor *Descriptor,
    NevercArtifactHandle *OutResult) {
  if (!Context || !Descriptor || !OutResult)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutResult = {};
  auto Parsed = parseResultDescriptor(*Descriptor);
  if (!Parsed) {
    consumeError(Parsed.takeError());
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask) ||
      !sameHandle(Frame->Input, Request))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Request, driverJobRequestArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Result = new (std::nothrow) DriverJobResultArtifact(
      *static_cast<const DriverJobRequestArtifact *>(Payload),
      std::move(Parsed->Outcome), false,
      std::move(Parsed->OutputSeals));
  if (!Result)
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Candidate = Bridge.ActiveExecutor->createCandidate(
      *Bridge.ActiveTask, driverJobResultArtifactID(), Result);
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return bridgeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutResult = *Candidate;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL DriverAPIBridge::getJobResult(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Result, NevercJobResult *OutResult) {
  if (!Context || !OutResult ||
      OutResult->Header.StructSize < sizeof(*OutResult) ||
      OutResult->Header.Major != NEVERC_DRIVER_API_MAJOR)
    return bridgeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutResult = {};
  OutResult->Header = {sizeof(*OutResult), NEVERC_DRIVER_API_MAJOR,
                       NEVERC_DRIVER_API_MINOR, 0};
  auto &Bridge = *static_cast<DriverAPIBridge *>(Context);
  std::lock_guard<std::mutex> Lock(Bridge.Mutex);
  if (!Bridge.ActiveExecutor || !Bridge.ActiveTask ||
      !validFrame(Frame, *Bridge.ActiveTask))
    return bridgeStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Bridge.ActiveExecutor->resolveArtifactPayload(
      *Bridge.ActiveTask, Result, driverJobResultArtifactID(),
      &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  static_cast<const DriverJobResultArtifact *>(Payload)->describe(
      *OutResult);
  return neverc_status_ok();
}

} // namespace neverc::driver
