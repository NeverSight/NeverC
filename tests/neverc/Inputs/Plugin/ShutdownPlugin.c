#include "neverc/Plugin/PluginCore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int SessionState;

static int should_fail(const char *Callback) {
  const char *Value = getenv("NEVERC_PLUGIN_SHUTDOWN_FAIL");
  return Value != NULL && strcmp(Value, Callback) == 0;
}

static void trace_event(const char *Event, NevercTaskKind Kind) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  if (Kind == 0)
    fprintf(Trace, "%s\n", Event);
  else
    fprintf(Trace, "%s:%u\n", Event, Kind);
  fclose(Trace);
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  *OutProcessState = NULL;
  trace_event("process_begin", 0);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core,
                const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)Registrar;
  (void)RegistrarContext;
  (void)ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *ProcessState, void **OutSessionState) {
  (void)Core;
  (void)Session;
  (void)ProcessState;
  *OutSessionState = &SessionState;
  trace_event("session_begin", 0);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_end(const NevercCoreAPI *Core, NevercSessionHandle Session,
            void *ProcessState, void *State) {
  (void)Core;
  (void)Session;
  (void)ProcessState;
  if (State != &SessionState)
    return (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
  trace_event("session_end", 0);
  if (should_fail("session_end"))
    return (NevercStatus){NEVERC_STATUS_PLUGIN_FAILURE, 0, 0};
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task,
           NevercTaskKind Kind, void *ProcessState, void *State,
           void **OutTaskState) {
  (void)Core;
  (void)Task;
  (void)ProcessState;
  if (State != &SessionState)
    return (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
  *OutTaskState = NULL;
  trace_event("task_begin", Kind);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task,
         NevercTaskKind Kind, void *ProcessState, void *State,
         void *TaskState) {
  (void)Core;
  (void)Task;
  (void)ProcessState;
  (void)TaskState;
  if (State != &SessionState)
    return (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
  trace_event("task_end", Kind);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                                void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  trace_event("destroy", 0);
  if (should_fail("destroy"))
    return (NevercStatus){NEVERC_STATUS_PLUGIN_FAILURE, 0, 0};
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  uint32_t BytesToWrite;
  static const char PluginID[] = "org.neverc.test.shutdown";
  static const char DisplayName[] = "Shutdown Test Plugin";
  (void)Bootstrap;
  if (OutPlugin == NULL)
    return (NevercStatus){NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID =
      (NevercStringView){PluginID, sizeof(PluginID) - 1};
  Descriptor.DisplayName =
      (NevercStringView){DisplayName, sizeof(DisplayName) - 1};
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_PROCESS_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
