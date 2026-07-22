/*===-- LifecyclePlugin.c - full lifecycle conformance fixture ----*- C -*-===*\
|*                                                                            *|
|* Implements every optional lifecycle callback and threads a distinct magic   *|
|* value through process -> session -> task state. Each callback appends an     *|
|* ordered line to the conformance log, and later callbacks confirm the state   *|
|* handed back to them is the one they created, proving user data survives      *|
|* across callbacks and that Destroy runs on unload.                           *|
\*===----------------------------------------------------------------------===*/

#include "ConformanceFixture.h"

#define PLUGIN_ID "com.neverc.conformance.lifecycle"

#define NCF_PROCESS_MAGIC 0xABCD1234u
#define NCF_SESSION_MAGIC 0x5E5510Au
#define NCF_TASK_MAGIC 0x7A5C0DEu

static NevercStatus NEVERC_CALL ncf_process_begin(const NevercCoreAPI *Core,
                                                  void **OutProcessState) {
  unsigned *State;
  (void)Core;
  if (OutProcessState == NULL)
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  State = (unsigned *)malloc(sizeof(unsigned));
  if (State == NULL)
    return ncf_status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  *State = NCF_PROCESS_MAGIC;
  *OutProcessState = State;
  ncf_log("process_begin");
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL ncf_register(const NevercCoreAPI *Core,
                                             const NevercRegistrarAPI *Registrar,
                                             void *RegistrarContext,
                                             void *ProcessState) {
  (void)Core;
  (void)Registrar;
  (void)RegistrarContext;
  if (ProcessState != NULL && *(unsigned *)ProcessState == NCF_PROCESS_MAGIC)
    ncf_log("register:state_ok");
  else
    ncf_log("register:state_bad");
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL ncf_session_begin(const NevercCoreAPI *Core,
                                                  NevercSessionHandle Session,
                                                  void *ProcessState,
                                                  void **OutSessionState) {
  unsigned *State;
  (void)Core;
  (void)Session;
  if (OutSessionState == NULL)
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (ProcessState == NULL || *(unsigned *)ProcessState != NCF_PROCESS_MAGIC) {
    ncf_log("session_begin:state_bad");
    return ncf_status(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  State = (unsigned *)malloc(sizeof(unsigned));
  if (State == NULL)
    return ncf_status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  *State = NCF_SESSION_MAGIC;
  *OutSessionState = State;
  ncf_log("session_begin:state_ok");
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL ncf_task_begin(const NevercCoreAPI *Core,
                                               NevercTaskHandle Task,
                                               NevercTaskKind Kind,
                                               void *ProcessState,
                                               void *SessionState,
                                               void **OutTaskState) {
  unsigned *State;
  (void)Core;
  (void)Task;
  (void)Kind;
  if (OutTaskState == NULL)
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (ProcessState == NULL || *(unsigned *)ProcessState != NCF_PROCESS_MAGIC ||
      SessionState == NULL || *(unsigned *)SessionState != NCF_SESSION_MAGIC) {
    ncf_log("task_begin:state_bad");
    return ncf_status(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  State = (unsigned *)malloc(sizeof(unsigned));
  if (State == NULL)
    return ncf_status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  *State = NCF_TASK_MAGIC;
  *OutTaskState = State;
  ncf_log("task_begin:state_ok");
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL ncf_task_end(const NevercCoreAPI *Core,
                                             NevercTaskHandle Task,
                                             NevercTaskKind Kind,
                                             void *ProcessState,
                                             void *SessionState,
                                             void *TaskState) {
  (void)Core;
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  (void)SessionState;
  if (TaskState != NULL && *(unsigned *)TaskState == NCF_TASK_MAGIC)
    ncf_log("task_end:state_ok");
  else
    ncf_log("task_end:state_bad");
  free(TaskState);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL ncf_session_end(const NevercCoreAPI *Core,
                                                NevercSessionHandle Session,
                                                void *ProcessState,
                                                void *SessionState) {
  (void)Core;
  (void)Session;
  (void)ProcessState;
  if (SessionState != NULL && *(unsigned *)SessionState == NCF_SESSION_MAGIC)
    ncf_log("session_end:state_ok");
  else
    ncf_log("session_end:state_bad");
  free(SessionState);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL ncf_destroy(const NevercCoreAPI *Core,
                                            void *ProcessState) {
  (void)Core;
  if (ProcessState != NULL && *(unsigned *)ProcessState == NCF_PROCESS_MAGIC)
    ncf_log("destroy:state_ok");
  else
    ncf_log("destroy:state_bad");
  free(ProcessState);
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      (uint32_t)sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = NCF_SV(PLUGIN_ID);
  Descriptor.DisplayName = NCF_SV("Conformance Lifecycle Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0, {0, 0}, {0, 0}};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = ncf_process_begin;
  Descriptor.Register = ncf_register;
  Descriptor.SessionBegin = ncf_session_begin;
  Descriptor.SessionEnd = ncf_session_end;
  Descriptor.TaskBegin = ncf_task_begin;
  Descriptor.TaskEnd = ncf_task_end;
  Descriptor.Destroy = ncf_destroy;
  ncf_write_descriptor(OutPlugin, &Descriptor);
  return neverc_status_ok();
}
