/*===-- DynCodeTracePlugin.c - dyncode phase trace example -------*- C -*-===*\
|*                                                                            *|
|* Read-only example that observes the dyncode code-extraction pipeline.       *|
|* It registers an Observer on the neverc.dyncode.extract.image transition and *|
|* emits a remark the first time it runs, demonstrating how a plugin watches   *|
|* the position-independent code extraction without modifying it.              *|
|*                                                                            *|
|* Build (independent C compiler + NeverC SDK header):                         *|
|*   neverc --target=<host> -shared -I<sdk>/include \                          *|
|*          -o DynCodeTracePlugin.<ext> DynCodeTracePlugin.c                    *|
\*===----------------------------------------------------------------------===*/

#include "neverc/Plugin/NevercPluginAPI.h"

#define DYNCODE_TRACE_PLUGIN_ID "org.neverc.example.dyncode-trace"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

typedef struct DynCodeTraceProcessState {
  const NevercCoreAPI *Core;
} DynCodeTraceProcessState;

typedef struct DynCodeTraceSessionState {
  uint64_t Callbacks;
  NevercBool Announced;
} DynCodeTraceSessionState;

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStringView plugin_id(void) {
  NevercStringView Result = STRING_VIEW_LITERAL(DYNCODE_TRACE_PLUGIN_ID);
  return Result;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  DynCodeTraceProcessState *State = NULL;
  NevercStatus Status;
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DynCodeTraceProcessState), (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Core = Core;
  *OutProcessState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *ProcessState, void **OutSessionState) {
  DynCodeTraceSessionState *State = NULL;
  NevercStatus Status;
  (void)Session;
  (void)ProcessState;
  if (Core == NULL || OutSessionState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSessionState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DynCodeTraceSessionState), (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Callbacks = 0;
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
                          sizeof(DynCodeTraceSessionState),
                          _Alignof(DynCodeTraceSessionState));
}

static NevercStatus NEVERC_CALL
observe_extract_image(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                      void *UserData) {
  DynCodeTraceProcessState *Process = (DynCodeTraceProcessState *)UserData;
  DynCodeTraceSessionState *Session = NULL;
  NevercDiagnosticDescriptor Diagnostic = {0};
  NevercDiagnosticHandle Handle = {0};
  NevercStatus Status;
  static const char Message[] = "dyncode extract-image phase observed";
  if (Frame == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(Process->Core->Context, Frame->Session,
                                          plugin_id(), (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->Callbacks;
  if (Point != NEVERC_OBSERVER_BEFORE || Session->Announced == NEVERC_TRUE)
    return neverc_status_ok();
  Session->Announced = NEVERC_TRUE;
  Diagnostic.Header = (NevercABITableHeader){
      sizeof(Diagnostic), NEVERC_CORE_API_MAJOR, NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_REMARK;
  Diagnostic.Code = 6001;
  Diagnostic.PluginID = plugin_id();
  Diagnostic.PhaseID = (NevercStringView){
      NEVERC_PHASE_DYNCODE_EXTRACT_IMAGE_NAME,
      sizeof(NEVERC_PHASE_DYNCODE_EXTRACT_IMAGE_NAME) - 1};
  Diagnostic.Message = (NevercStringView){Message, sizeof(Message) - 1};
  return Process->Core->EmitDiagnostic(Process->Core->Context, &Diagnostic,
                                       &Handle);
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  DynCodeTraceProcessState *Process = (DynCodeTraceProcessState *)ProcessState;
  NevercObserverDescriptor Observer = {0};
  (void)Core;
  if (Registrar == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Observer.Header = (NevercABITableHeader){
      sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase.High = NEVERC_PHASE_DYNCODE_EXTRACT_IMAGE_HIGH;
  Observer.Phase.Low = NEVERC_PHASE_DYNCODE_EXTRACT_IMAGE_LOW;
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = observe_extract_image;
  Observer.UserData = Process;
  return Registrar->RegisterObserver(RegistrarContext, &Observer);
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  if (Core == NULL || ProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, ProcessState,
                          sizeof(DynCodeTraceProcessState),
                          _Alignof(DynCodeTraceProcessState));
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = plugin_id();
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("DynCode Trace Example");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
