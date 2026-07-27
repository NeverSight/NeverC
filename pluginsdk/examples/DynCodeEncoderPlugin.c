/*===-- DynCodeEncoderPlugin.c - dyncode charset-encode example --*- C -*-===*\
|*                                                                            *|
|* Example that intercepts the dyncode charset-encode transition, the point    *|
|* where a target-specific bad-byte/charset transform runs on the extracted    *|
|* position-independent bytes. This skeleton passes the stage through           *|
|* unchanged (calls InvokeNext and returns CONTINUE) and emits a remark, so it  *|
|* is a faithful starting point for a real encoder that would either mutate     *|
|* the candidate through the stage's transaction or register a charset encoder  *|
|* via NevercDynCodeRegistrarAPI::RegisterCharsetEncoder.                       *|
|*                                                                            *|
|* Build (independent C compiler + NeverC SDK header):                         *|
|*   neverc --target=<host> -shared -I<sdk>/include \                          *|
|*          -o DynCodeEncoderPlugin.<ext> DynCodeEncoderPlugin.c                *|
\*===----------------------------------------------------------------------===*/

#include "neverc/Plugin/NevercPluginAPI.h"

#define DYNCODE_ENCODER_PLUGIN_ID "org.neverc.example.dyncode-encoder"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

typedef struct DynCodeEncoderProcessState {
  const NevercCoreAPI *Core;
} DynCodeEncoderProcessState;

typedef struct DynCodeEncoderSessionState {
  uint64_t Callbacks;
} DynCodeEncoderSessionState;

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStringView plugin_id(void) {
  NevercStringView Result = STRING_VIEW_LITERAL(DYNCODE_ENCODER_PLUGIN_ID);
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
  DynCodeEncoderProcessState *State = NULL;
  NevercStatus Status;
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DynCodeEncoderProcessState), (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Core = Core;
  *OutProcessState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *ProcessState, void **OutSessionState) {
  DynCodeEncoderSessionState *State = NULL;
  NevercStatus Status;
  (void)Session;
  (void)ProcessState;
  if (Core == NULL || OutSessionState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSessionState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(DynCodeEncoderSessionState), (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Callbacks = 0;
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
                          sizeof(DynCodeEncoderSessionState),
                          _Alignof(DynCodeEncoderSessionState));
}

static NevercStatus NEVERC_CALL
intercept_charset_encode(const NevercPhaseFrame *Frame,
                         NevercPhaseContinuation *Continuation,
                         NevercPhaseResult *OutResult, void *UserData) {
  DynCodeEncoderProcessState *Process = (DynCodeEncoderProcessState *)UserData;
  DynCodeEncoderSessionState *Session = NULL;
  NevercPhaseResult Downstream = {0};
  NevercStatus Status;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL ||
      Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(Process->Core->Context, Frame->Session,
                                          plugin_id(), (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->Callbacks;

  /* Pass the charset-encode stage through unchanged. */
  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutResult = (NevercPhaseResult){0};
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  DynCodeEncoderProcessState *Process =
      (DynCodeEncoderProcessState *)ProcessState;
  NevercInterceptorDescriptor Interceptor = {0};
  (void)Core;
  if (Registrar == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase.High = NEVERC_PHASE_DYNCODE_BINARY_CHARSET_ENCODE_HIGH;
  Interceptor.Phase.Low = NEVERC_PHASE_DYNCODE_BINARY_CHARSET_ENCODE_LOW;
  Interceptor.Callback = intercept_charset_encode;
  Interceptor.UserData = Process;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  if (Core == NULL || ProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, ProcessState,
                          sizeof(DynCodeEncoderProcessState),
                          _Alignof(DynCodeEncoderProcessState));
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
      (NevercStringView)STRING_VIEW_LITERAL("DynCode Charset Encoder Example");
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
