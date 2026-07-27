#include "neverc/Plugin/NevercPluginAPI.h"

#define DRIVER_TRACE_PLUGIN_ID "org.neverc.example.driver-trace"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

typedef struct DriverTraceProcessState {
  const NevercCoreAPI *Core;
  const NevercDriverAPI *Driver;
} DriverTraceProcessState;

typedef struct DriverTraceSessionState {
  uint64_t ArgumentCallbacks;
  uint64_t JobCallbacks;
  NevercBool Announced;
} DriverTraceSessionState;

typedef struct DriverTraceTaskState {
  NevercTaskKind Kind;
  uint64_t Callbacks;
} DriverTraceTaskState;

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStringView plugin_id(void) {
  NevercStringView Result = STRING_VIEW_LITERAL(DRIVER_TRACE_PLUGIN_ID);
  return Result;
}

static NevercInterfaceID phase_id(uint64_t High, uint64_t Low) {
  NevercInterfaceID Result;
  Result.High = High;
  Result.Low = Low;
  return Result;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus emit_trace_remark(
    DriverTraceProcessState *Process, const NevercPhaseFrame *Frame,
    const char *Message, uint64_t MessageLength, uint32_t Code) {
  NevercDiagnosticDescriptor Diagnostic = {0};
  NevercDiagnosticHandle Handle = {0};
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_REMARK;
  Diagnostic.Code = Code;
  Diagnostic.PluginID = plugin_id();
  Diagnostic.PhaseID =
      (NevercStringView){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_NAME,
                        sizeof(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_NAME) - 1};
  if (Frame != NULL &&
      Frame->Phase.High == NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH &&
      Frame->Phase.Low == NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW)
    Diagnostic.PhaseID =
        (NevercStringView){NEVERC_PHASE_DRIVER_EXECUTE_JOB_NAME,
                          sizeof(NEVERC_PHASE_DRIVER_EXECUTE_JOB_NAME) - 1};
  Diagnostic.Message = (NevercStringView){Message, MessageLength};
  return Process->Core->EmitDiagnostic(Process->Core->Context, &Diagnostic,
                                       &Handle);
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  DriverTraceProcessState *State = NULL;
  NevercInterfaceID DriverInterface;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t TableSize = 0;
  NevercStatus Status;
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  DriverInterface.High = NEVERC_INTERFACE_DRIVER_HIGH;
  DriverInterface.Low = NEVERC_INTERFACE_DRIVER_LOW;
  Status = Core->QueryInterface(
      Core->Context, DriverInterface, NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      TableSize < offsetof(NevercDriverAPI, GetJobResult) +
                      sizeof(((NevercDriverAPI *)0)->GetJobResult))
    return status_code(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  (void)Minor;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DriverTraceProcessState),
                          (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Core = Core;
  State->Driver = (const NevercDriverAPI *)Table;
  *OutProcessState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *ProcessState, void **OutSessionState) {
  DriverTraceSessionState *State = NULL;
  NevercStatus Status;
  (void)Session;
  if (Core == NULL || ProcessState == NULL || OutSessionState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSessionState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DriverTraceSessionState),
                          (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->ArgumentCallbacks = 0;
  State->JobCallbacks = 0;
  State->Announced = NEVERC_FALSE;
  *OutSessionState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_end(const NevercCoreAPI *Core, NevercSessionHandle Session,
            void *ProcessState, void *SessionState) {
  (void)Session;
  (void)ProcessState;
  if (Core == NULL || SessionState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, SessionState,
                          sizeof(DriverTraceSessionState),
                          _Alignof(DriverTraceSessionState));
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task,
           NevercTaskKind Kind, void *ProcessState, void *SessionState,
           void **OutTaskState) {
  DriverTraceTaskState *State = NULL;
  NevercStatus Status;
  (void)Task;
  (void)ProcessState;
  if (Core == NULL || SessionState == NULL || OutTaskState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutTaskState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DriverTraceTaskState),
                          (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Kind = Kind;
  State->Callbacks = 0;
  *OutTaskState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task,
         NevercTaskKind Kind, void *ProcessState, void *SessionState,
         void *TaskState) {
  DriverTraceTaskState *State = (DriverTraceTaskState *)TaskState;
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  if (Core == NULL || SessionState == NULL || State == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, State, sizeof(*State),
                          _Alignof(DriverTraceTaskState));
}

static NevercStatus NEVERC_CALL
observe_arguments(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                  void *UserData) {
  DriverTraceProcessState *Process =
      (DriverTraceProcessState *)UserData;
  DriverTraceSessionState *Session = NULL;
  DriverTraceTaskState *Task = NULL;
  NevercStatus Status;
  uint64_t ArgumentCount = 0;
  static const char Message[] = "driver argument phase observed";
  if (Frame == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(
      Process->Core->Context, Frame->Session, plugin_id(),
      (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Core->GetTaskState(
      Process->Core->Context, Frame->Task, plugin_id(), (void **)&Task);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Driver->GetArgumentCount(
      Process->Driver->Context, Frame, Frame->Input, &ArgumentCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  (void)ArgumentCount;
  ++Session->ArgumentCallbacks;
  ++Task->Callbacks;
  if (Point == NEVERC_OBSERVER_BEFORE &&
      Session->Announced == NEVERC_FALSE) {
    Session->Announced = NEVERC_TRUE;
    return emit_trace_remark(Process, Frame, Message, sizeof(Message) - 1,
                             1001);
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
intercept_job(const NevercPhaseFrame *Frame,
              NevercPhaseContinuation *Continuation,
              NevercPhaseResult *OutResult, void *UserData) {
  DriverTraceProcessState *Process =
      (DriverTraceProcessState *)UserData;
  DriverTraceSessionState *Session = NULL;
  DriverTraceTaskState *Task = NULL;
  NevercJobExecutionRequest Request = {0};
  NevercPhaseResult Downstream = {0};
  NevercStatus Status;
  static const char Message[] = "driver job execution intercepted";
  if (Frame == NULL || Continuation == NULL || OutResult == NULL ||
      Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(
      Process->Core->Context, Frame->Session, plugin_id(),
      (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Core->GetTaskState(
      Process->Core->Context, Frame->Task, plugin_id(), (void **)&Task);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Request.Header =
      (NevercABITableHeader){sizeof(Request), NEVERC_DRIVER_API_MAJOR,
                            NEVERC_DRIVER_API_MINOR, 0};
  Status = Process->Driver->GetJobExecutionRequest(
      Process->Driver->Context, Frame, Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->JobCallbacks;
  ++Task->Callbacks;
  Status = emit_trace_remark(Process, Frame, Message, sizeof(Message) - 1,
                             1002);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Downstream.Header =
      (NevercABITableHeader){sizeof(Downstream),
                            NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutResult = (NevercPhaseResult){0};
  OutResult->Header =
      (NevercABITableHeader){sizeof(*OutResult),
                            NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core,
                const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  DriverTraceProcessState *Process =
      (DriverTraceProcessState *)ProcessState;
  NevercOptionDescriptor Option = {0};
  NevercObserverDescriptor Observer = {0};
  NevercInterceptorDescriptor Interceptor = {0};
  NevercStatus Status;
  (void)Core;
  if (Registrar == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);

  Option.Header =
      (NevercABITableHeader){sizeof(Option), NEVERC_DRIVER_API_MAJOR,
                            NEVERC_DRIVER_API_MINOR, 0};
  Option.Spelling =
      (NevercStringView)STRING_VIEW_LITERAL("--driver-trace");
  Option.Form = NEVERC_OPTION_FLAG;
  Option.ValueType = NEVERC_OPTION_BOOL;
  Option.Multiplicity = NEVERC_OPTION_SINGLE;
  Option.Help = (NevercStringView)STRING_VIEW_LITERAL(
      "enable the driver trace example plugin");
  Status = Registrar->RegisterOption(RegistrarContext, &Option);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Observer.Header =
      (NevercABITableHeader){sizeof(Observer),
                            NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = phase_id(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                            NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW);
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = observe_arguments;
  Observer.UserData = Process;
  Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Interceptor.Header =
      (NevercABITableHeader){sizeof(Interceptor),
                            NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      phase_id(NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
               NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW);
  Interceptor.Callback = intercept_job;
  Interceptor.UserData = Process;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  if (Core == NULL || ProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, ProcessState,
                          sizeof(DriverTraceProcessState),
                          _Alignof(DriverTraceProcessState));
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  NevercInterfaceID DriverInterface;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t TableSize = 0;
  uint32_t Capacity;
  uint64_t BytesToWrite;
  NevercStatus Status;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);

  DriverInterface.High = NEVERC_INTERFACE_DRIVER_HIGH;
  DriverInterface.Low = NEVERC_INTERFACE_DRIVER_LOW;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, DriverInterface, NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      TableSize < offsetof(NevercDriverAPI, GetJobResult) +
                      sizeof(((NevercDriverAPI *)0)->GetJobResult))
    return status_code(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  (void)Minor;

  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor),
                            NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = plugin_id();
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Driver Trace Example");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
