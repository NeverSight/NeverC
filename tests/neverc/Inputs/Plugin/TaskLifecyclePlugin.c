#include "neverc/Plugin/PluginSource.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TaskLifecycleSessionState {
  const NevercCoreAPI *Core;
  NevercSessionHandle Session;
} TaskLifecycleSessionState;

typedef struct TaskLifecycleTaskState {
  NevercOutputSinkHandle Output;
} TaskLifecycleTaskState;

static const NevercIOAPI *IOAPI;

static void trace_line(const char *Format, uint32_t Kind,
                       NevercHandle Session, NevercHandle Task) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  fprintf(Trace, Format, Kind, Session.Owner, Session.Value, Task.Owner,
          Task.Value);
  fputc('\n', Trace);
  fclose(Trace);
}

static void trace_event(const char *Event) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  fprintf(Trace, "%s\n", Event);
  fclose(Trace);
}

static void trace_output_status(const char *Event, NevercStatus Status,
                                NevercOutputState State) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  fprintf(Trace, "%s:%" PRId32 ":%" PRIu32 "\n", Event, Status.Code, State);
  fclose(Trace);
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  *OutProcessState = NULL;
  trace_event("process_begin");
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
  TaskLifecycleSessionState *State = NULL;
  NevercStatus Status;
  (void)ProcessState;
  Status = Core->Allocate(Core->Context, sizeof(*State), UINT64_C(8),
                          (void **)&State);
  if (!neverc_status_is_ok(Status))
    return Status;
  State->Core = Core;
  State->Session = Session;
  *OutSessionState = State;
  trace_line("session_begin:%u:%" PRIu64 ":%" PRIu64, 0, Session,
             (NevercHandle){0, 0});
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_end(const NevercCoreAPI *Core, NevercSessionHandle Session,
            void *ProcessState, void *SessionState) {
  TaskLifecycleSessionState *State =
      (TaskLifecycleSessionState *)SessionState;
  (void)ProcessState;
  trace_line("session_end:%u:%" PRIu64 ":%" PRIu64, 0, Session,
             (NevercHandle){0, 0});
  if (State == NULL)
    return neverc_status_ok();
  return Core->Deallocate(Core->Context, State, sizeof(*State), UINT64_C(8));
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task,
           NevercTaskKind Kind, void *ProcessState, void *SessionState,
           void **OutTaskState) {
  TaskLifecycleSessionState *State =
      (TaskLifecycleSessionState *)SessionState;
  TaskLifecycleTaskState *TaskState = NULL;
  static const char OutputName[] = "task-lifecycle";
  static const uint8_t OutputBytes[] = {'x'};
  NevercStatus Status;
  (void)Core;
  (void)ProcessState;
  if (State == NULL)
    return (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
  *OutTaskState = NULL;
  if (getenv("NEVERC_TEST_OUTPUT_LIFECYCLE") != NULL) {
    if (IOAPI == NULL)
      return (NevercStatus){NEVERC_STATUS_MISSING_INTERFACE, 0, 0};
    Status = Core->Allocate(Core->Context, sizeof(*TaskState), UINT64_C(8),
                            (void **)&TaskState);
    if (!neverc_status_is_ok(Status))
      return Status;
    memset(TaskState, 0, sizeof(*TaskState));
    Status = IOAPI->BeginMemoryOutput(
        IOAPI->Context, Task,
        (NevercStringView){OutputName, sizeof(OutputName) - 1},
        UINT64_C(16), &TaskState->Output);
    if (neverc_status_is_ok(Status))
      Status = IOAPI->OutputWrite(
          IOAPI->Context, Task, TaskState->Output,
          (NevercByteView){OutputBytes, sizeof(OutputBytes)});
    if (!neverc_status_is_ok(Status)) {
      (void)Core->Deallocate(Core->Context, TaskState, sizeof(*TaskState),
                             UINT64_C(8));
      return Status;
    }
    *OutTaskState = TaskState;
  }
  trace_line("task_begin:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64,
             Kind, State->Session, Task);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task,
         NevercTaskKind Kind, void *ProcessState, void *SessionState,
         void *TaskState) {
  TaskLifecycleSessionState *State =
      (TaskLifecycleSessionState *)SessionState;
  TaskLifecycleTaskState *LifecycleTaskState =
      (TaskLifecycleTaskState *)TaskState;
  (void)ProcessState;
  if (State == NULL)
    return (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
  if (LifecycleTaskState != NULL) {
    NevercOutputSummary Summary;
    static const uint8_t MoreBytes[] = {'y'};
    NevercStatus Status;
    memset(&Summary, 0, sizeof(Summary));
    Summary.Header.StructSize = sizeof(Summary);
    Status = IOAPI->OutputGetSummary(
        IOAPI->Context, Task, LifecycleTaskState->Output, &Summary);
    trace_output_status("output_summary", Status, Summary.State);
    Status = IOAPI->OutputWrite(
        IOAPI->Context, Task, LifecycleTaskState->Output,
        (NevercByteView){MoreBytes, sizeof(MoreBytes)});
    trace_output_status("output_write", Status, 0);
    Status = Core->Deallocate(Core->Context, LifecycleTaskState,
                              sizeof(*LifecycleTaskState), UINT64_C(8));
    if (!neverc_status_is_ok(Status))
      return Status;
  }
  trace_line("task_end:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64,
             Kind, State->Session, Task);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                                void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  trace_event("destroy");
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  NevercInterfaceID IOInterface;
  const void *Table = NULL;
  uint16_t NegotiatedMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status;
  uint32_t Capacity;
  uint32_t BytesToWrite;
  static const char PluginID[] = "org.neverc.test.task-lifecycle";
  static const char DisplayName[] = "Task Lifecycle Test Plugin";
  if (Bootstrap == NULL || OutPlugin == NULL)
    return (NevercStatus){NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  IOInterface.High = NEVERC_INTERFACE_IO_HIGH;
  IOInterface.Low = NEVERC_INTERFACE_IO_LOW;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, IOInterface, NEVERC_IO_API_MAJOR,
      NEVERC_IO_API_MINOR, &Table, &NegotiatedMinor, &StructSize);
  if (!neverc_status_is_ok(Status))
    return Status;
  if (Table == NULL ||
      StructSize < offsetof(NevercIOAPI, OutputGetSummary) +
                       sizeof(((NevercIOAPI *)0)->OutputGetSummary))
    return (NevercStatus){NEVERC_STATUS_ABI_MISMATCH, 0, 0};
  IOAPI = (const NevercIOAPI *)Table;
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
