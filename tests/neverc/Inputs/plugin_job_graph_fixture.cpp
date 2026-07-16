#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef NEVERC_JOB_GRAPH_MODE
#define NEVERC_JOB_GRAPH_MODE 0
#endif

enum {
  ModeRemove = 1,
  ModeRewrite = 2,
  ModeInsert = 3,
  ModeReplace = 4,
  ModeCycle = 5,
  ModeDuplicateOutput = 6,
  ModeDanglingInput = 7,
};

static const NevercDriverAPI *DriverAPI;
static int ProcessState;

static NevercStringView sv(const char *Text) {
  return NevercStringView{Text, (uint64_t)std::strlen(Text)};
}

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void initialize_result(NevercPhaseResult *Result) {
  std::memset(Result, 0, sizeof(*Result));
  Result->Header = NevercABITableHeader{
      sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_CONTINUE;
}

static void initialize_job(NevercJob *Job) {
  std::memset(Job, 0, sizeof(*Job));
  Job->Header = NevercABITableHeader{
      sizeof(*Job), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
}

static void initialize_file(NevercJobFile *File) {
  std::memset(File, 0, sizeof(*File));
  File->Header = NevercABITableHeader{
      sizeof(*File), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
}

static void trace(const char *Event) {
  const char *Path = std::getenv("NEVERC_PLUGIN_TRACE_FILE");
  if (!Path || !*Path)
    return;
  if (FILE *File = std::fopen(Path, "ab")) {
    std::fprintf(File, "%s\n", Event);
    std::fclose(File);
  }
}

static bool contains(NevercStringView Value, const char *Needle) {
  const size_t NeedleLength = std::strlen(Needle);
  if (NeedleLength > Value.Length)
    return false;
  for (uint64_t I = 0; I + NeedleLength <= Value.Length; ++I)
    if (std::memcmp(Value.Data + I, Needle, NeedleLength) == 0)
      return true;
  return false;
}

static NevercStatus NEVERC_CALL
plugin_job(const NevercPluginJobContext *, int32_t *OutExitCode,
           void *UserData) {
  if (!OutExitCode || !UserData)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  trace(static_cast<const char *>(UserData));
  *OutExitCode = 0;
  return neverc_status_ok();
}

static NevercStatus add_plugin_job(
    NevercHandle Edit, NevercActionNodeID SourceAction,
    const char *CallbackID, const char *TraceEvent,
    const NevercJobID *Dependencies, uint64_t DependencyCount,
    NevercJobID *OutJob) {
  NevercJobDescriptor Descriptor{};
  Descriptor.Header = NevercABITableHeader{
      sizeof(Descriptor), NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, 0};
  Descriptor.Kind = NEVERC_JOB_PLUGIN;
  Descriptor.ResponseFileKind = NEVERC_RESPONSE_FILE_NONE;
  Descriptor.ResponseFileEncoding = NEVERC_RESPONSE_ENCODING_UTF8;
  Descriptor.InProcess = NEVERC_TRUE;
  Descriptor.SourceAction = SourceAction;
  Descriptor.Dependencies = NevercJobIDList{
      Dependencies, DependencyCount, sizeof(NevercJobID)};
  Descriptor.CallbackID = sv(CallbackID);
  Descriptor.Callback = plugin_job;
  Descriptor.UserData = const_cast<char *>(TraceEvent);
  return DriverAPI->AddJob(DriverAPI->Context, Edit, &Descriptor, OutJob);
}

static NevercStatus NEVERC_CALL intercept_jobs(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *) {
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  NevercPhaseResult Downstream{};
  initialize_result(&Downstream);
  NevercStatus Status =
      Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Downstream.Action != NEVERC_PHASE_REPLACE)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

  NevercJobGraphMutationHandle Mutation{};
  Status = DriverAPI->BeginJobGraphMutation(
      DriverAPI->Context, Frame, Continuation, Downstream.Output, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  uint64_t Count = 0;
  Status = DriverAPI->GetJobCount(
      DriverAPI->Context, Frame, Downstream.Output, &Count);
  if (Status.Code != NEVERC_STATUS_OK) {
    DriverAPI->AbortJobGraphEdit(DriverAPI->Context, Mutation);
    return Status;
  }

  if (NEVERC_JOB_GRAPH_MODE == ModeRemove) {
    for (uint64_t I = 0; I < Count; ++I) {
      NevercJob Job;
      initialize_job(&Job);
      Status = DriverAPI->GetJob(
          DriverAPI->Context, Frame, Downstream.Output, I, &Job);
      if (Status.Code != NEVERC_STATUS_OK)
        break;
      for (uint64_t J = 0; J < Job.InputCount; ++J) {
        NevercJobFile Input;
        initialize_file(&Input);
        Status = DriverAPI->GetJobInput(
            DriverAPI->Context, Frame, Downstream.Output, Job.Job, J, &Input);
        if (Status.Code != NEVERC_STATUS_OK)
          break;
        if (contains(Input.Path, "job_graph_bad.c")) {
          Status =
              DriverAPI->RemoveJob(DriverAPI->Context, Mutation, Job.Job);
          I = Count;
          break;
        }
      }
    }
  } else if (NEVERC_JOB_GRAPH_MODE == ModeRewrite) {
    for (uint64_t I = 0; I < Count; ++I) {
      NevercJob Job;
      initialize_job(&Job);
      Status = DriverAPI->GetJob(
          DriverAPI->Context, Frame, Downstream.Output, I, &Job);
      if (Status.Code != NEVERC_STATUS_OK)
        break;
      for (uint64_t J = 0; J < Job.ArgumentCount; ++J) {
        NevercStringView Argument{};
        Status = DriverAPI->GetJobArgument(
            DriverAPI->Context, Frame, Downstream.Output, Job.Job, J,
            &Argument);
        if (Status.Code != NEVERC_STATUS_OK)
          break;
        if (contains(Argument, "NEVERC_JOB_GRAPH_ORIGINAL")) {
          Status = DriverAPI->SetJobArgument(
              DriverAPI->Context, Mutation, Job.Job, J,
              sv("NEVERC_JOB_GRAPH_REWRITTEN=1"));
          return Status.Code == NEVERC_STATUS_OK
                     ? DriverAPI->CommitJobGraphMutation(
                           DriverAPI->Context, Mutation)
                     : Status;
        }
      }
    }
  } else if (NEVERC_JOB_GRAPH_MODE == ModeInsert) {
    if (Count == 0)
      Status = failure(NEVERC_STATUS_PLUGIN_FAILURE);
    else {
      NevercJob Previous;
      initialize_job(&Previous);
      Status = DriverAPI->GetJob(
          DriverAPI->Context, Frame, Downstream.Output, Count - 1, &Previous);
      if (Status.Code == NEVERC_STATUS_OK) {
        NevercJobID Added = 0;
        Status = add_plugin_job(
            Mutation, Previous.SourceAction, "inserted", "job:inserted",
            &Previous.Job, 1, &Added);
      }
    }
  } else if (NEVERC_JOB_GRAPH_MODE == ModeCycle) {
    if (Count < 2)
      Status = failure(NEVERC_STATUS_PLUGIN_FAILURE);
    else {
      NevercJob First, Second;
      initialize_job(&First);
      initialize_job(&Second);
      Status = DriverAPI->GetJob(
          DriverAPI->Context, Frame, Downstream.Output, 0, &First);
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->GetJob(
            DriverAPI->Context, Frame, Downstream.Output, 1, &Second);
      NevercJobID FirstDependencies[] = {Second.Job};
      NevercJobID SecondDependencies[] = {First.Job};
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->ReplaceJobDependencies(
            DriverAPI->Context, Mutation, First.Job,
            NevercJobIDList{FirstDependencies, 1, sizeof(NevercJobID)});
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->ReplaceJobDependencies(
            DriverAPI->Context, Mutation, Second.Job,
            NevercJobIDList{SecondDependencies, 1, sizeof(NevercJobID)});
    }
  } else if (NEVERC_JOB_GRAPH_MODE == ModeDuplicateOutput) {
    if (Count < 2)
      Status = failure(NEVERC_STATUS_PLUGIN_FAILURE);
    else {
      NevercJob First, Second;
      initialize_job(&First);
      initialize_job(&Second);
      Status = DriverAPI->GetJob(
          DriverAPI->Context, Frame, Downstream.Output, 0, &First);
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->GetJob(
            DriverAPI->Context, Frame, Downstream.Output, 1, &Second);
      NevercJobFile Output;
      initialize_file(&Output);
      Output.Path = sv("job_graph_shared.out");
      Output.Type = NEVERC_DRIVER_TYPE_NOTHING;
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->SetJobOutput(
            DriverAPI->Context, Mutation, First.Job, 0, &Output);
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->SetJobOutput(
            DriverAPI->Context, Mutation, Second.Job, 0, &Output);
    }
  } else if (NEVERC_JOB_GRAPH_MODE == ModeDanglingInput) {
    if (Count == 0)
      Status = failure(NEVERC_STATUS_PLUGIN_FAILURE);
    else {
      NevercJob First;
      initialize_job(&First);
      Status = DriverAPI->GetJob(
          DriverAPI->Context, Frame, Downstream.Output, 0, &First);
      NevercJobFile Input;
      initialize_file(&Input);
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->GetJobInput(
            DriverAPI->Context, Frame, Downstream.Output, First.Job, 0,
            &Input);
      Input.Path = sv("job_graph_unknown_external_input.c");
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DriverAPI->SetJobInput(
            DriverAPI->Context, Mutation, First.Job, 0, &Input);
    }
  }

  if (Status.Code != NEVERC_STATUS_OK) {
    DriverAPI->AbortJobGraphEdit(DriverAPI->Context, Mutation);
    return Status;
  }
  Status =
      DriverAPI->CommitJobGraphMutation(DriverAPI->Context, Mutation);
  if (NEVERC_JOB_GRAPH_MODE == ModeCycle ||
      NEVERC_JOB_GRAPH_MODE == ModeDuplicateOutput ||
      NEVERC_JOB_GRAPH_MODE == ModeDanglingInput) {
    if (Status.Code != NEVERC_STATUS_VERIFICATION_FAILED)
      return failure(NEVERC_STATUS_PLUGIN_FAILURE);
    Status = DriverAPI->AbortJobGraphEdit(DriverAPI->Context, Mutation);
    return Status;
  }
  return Status;
}

static NevercStatus NEVERC_CALL replace_jobs(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult, void *) {
  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  trace("provider:replacement");

  NevercActionNodeID SourceAction = 0;
  NevercStatus Status = DriverAPI->GetActionRoot(
      DriverAPI->Context, Frame, Frame->Input, 0, &SourceAction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercJobGraphBuilderHandle Builder{};
  Status = DriverAPI->CreateJobGraphBuilder(
      DriverAPI->Context, Frame, Frame->Input, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercJobID Added = 0;
  Status = add_plugin_job(
      Builder, SourceAction, "replacement", "job:replacement", nullptr, 0,
      &Added);
  if (Status.Code != NEVERC_STATUS_OK) {
    DriverAPI->AbortJobGraphEdit(DriverAPI->Context, Builder);
    return Status;
  }
  NevercArtifactHandle Graph{};
  Status = DriverAPI->PublishJobGraph(
      DriverAPI->Context, Frame, Builder, &Graph);
  if (Status.Code != NEVERC_STATUS_OK) {
    DriverAPI->AbortJobGraphEdit(DriverAPI->Context, Builder);
    return Status;
  }
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Graph;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(
    const NevercCoreAPI *, void **OutProcessState) {
  if (!OutProcessState)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *) {
  if (!Registrar)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  if (NEVERC_JOB_GRAPH_MODE == ModeReplace) {
    NevercProviderDescriptor Provider{};
    Provider.Header = NevercABITableHeader{
        sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
        NEVERC_PLUGIN_ABI_MINOR, 0};
    Provider.Phase = NevercInterfaceID{
        NEVERC_PHASE_DRIVER_BUILD_JOBS_HIGH,
        NEVERC_PHASE_DRIVER_BUILD_JOBS_LOW};
    Provider.ProviderID = sv("neverc.test.job-graph");
    Provider.Route.Header = NevercABITableHeader{
        sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
        NEVERC_PLUGIN_ABI_MINOR, 0};
    Provider.Deterministic = NEVERC_TRUE;
    Provider.Callback = replace_jobs;
    return Registrar->RegisterProvider(RegistrarContext, &Provider);
  }
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = NevercABITableHeader{
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = NevercInterfaceID{
      NEVERC_PHASE_DRIVER_BUILD_JOBS_HIGH,
      NEVERC_PHASE_DRIVER_BUILD_JOBS_LOW};
  Interceptor.Callback = intercept_jobs;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

static NevercStatus NEVERC_CALL destroy_plugin(
    const NevercCoreAPI *, void *) {
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercInterfaceID DriverInterface{
      NEVERC_INTERFACE_DRIVER_HIGH, NEVERC_INTERFACE_DRIVER_LOW};
  const void *Table = nullptr;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Bootstrap->QueryInterface(
      Bootstrap->Context, DriverInterface, NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize <
          offsetof(NevercDriverAPI, AbortJobGraphEdit) +
              sizeof(((NevercDriverAPI *)0)->AbortJobGraphEdit))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  DriverAPI = static_cast<const NevercDriverAPI *>(Table);

  uint32_t Capacity = OutPlugin->Header.StructSize;
  NevercPluginDescriptor Descriptor{};
  Descriptor.Header = NevercABITableHeader{
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = sv("org.neverc.test.job-graph");
  Descriptor.DisplayName = sv("NeverC job graph test plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;
  size_t BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  std::memcpy(OutPlugin, &Descriptor, BytesToWrite);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
