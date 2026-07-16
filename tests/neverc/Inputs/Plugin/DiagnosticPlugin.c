#include "neverc/Plugin/PluginCore.h"
#include <stdlib.h>
#include <string.h>

static int SessionState;
static int ObservedCancellation;

static int mode_is(const char *Expected) {
  const char *Mode = getenv("NEVERC_TEST_DIAGNOSTIC_MODE");
  return Mode != NULL && strcmp(Mode, Expected) == 0;
}

static NevercStringView string_view(const char *Text) {
  return (NevercStringView){Text, (uint64_t)strlen(Text)};
}

static NevercStatus emit_diagnostic(const NevercCoreAPI *Core,
                                    NevercDiagnosticSeverity Severity,
                                    uint32_t Code, const char *Phase,
                                    const char *Message, int WithNotes) {
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticNote Notes[2];
  NevercDiagnosticHandle Handle;
  memset(&Diagnostic, 0, sizeof(Diagnostic));
  memset(Notes, 0, sizeof(Notes));
  memset(&Handle, 0, sizeof(Handle));
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = Severity;
  Diagnostic.Code = Code;
  Diagnostic.PluginID = string_view("org.neverc.test.diagnostic");
  Diagnostic.PhaseID = string_view(Phase);
  Diagnostic.Message = string_view(Message);
  if (WithNotes) {
    Notes[0].Header =
        (NevercABITableHeader){sizeof(NevercDiagnosticNote),
                              NEVERC_CORE_API_MAJOR,
                              NEVERC_CORE_API_MINOR, 0};
    Notes[0].Message = string_view("diagnostic note one");
    Notes[1].Header = Notes[0].Header;
    Notes[1].Message = string_view("diagnostic note two");
    Diagnostic.Notes =
        (NevercStructArrayView){Notes, 2, sizeof(NevercDiagnosticNote)};
  }
  return Core->EmitDiagnostic(Core->Context, &Diagnostic, &Handle);
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  *OutProcessState = NULL;
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
  (void)Session;
  (void)ProcessState;
  *OutSessionState = &SessionState;
  ObservedCancellation = 0;
  if (mode_is("warning"))
    return emit_diagnostic(Core, NEVERC_DIAGNOSTIC_WARNING, 7001,
                           "neverc.driver.raw_arguments",
                           "diagnostic warning", 1);
  if (mode_is("error"))
    return emit_diagnostic(Core, NEVERC_DIAGNOSTIC_ERROR, 7002,
                           "neverc.driver.parsed_arguments",
                           "diagnostic error", 1);
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
  if (mode_is("implicit"))
    return (NevercStatus){NEVERC_STATUS_PLUGIN_FAILURE, 0, 0};
  if (mode_is("fatal") && !ObservedCancellation)
    return (NevercStatus){NEVERC_STATUS_PLUGIN_FAILURE, 0, 0};
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task,
           NevercTaskKind Kind, void *ProcessState, void *State,
           void **OutTaskState) {
  (void)Kind;
  (void)ProcessState;
  if (State != &SessionState)
    return (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
  *OutTaskState = NULL;
  if (mode_is("fatal")) {
    NevercStatus Status =
        emit_diagnostic(Core, NEVERC_DIAGNOSTIC_FATAL, 7003,
                        "neverc.driver.execute_job",
                        "diagnostic fatal", 0);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = Core->CheckCancelled(Core->Context, Task);
    ObservedCancellation =
        Status.Code == NEVERC_STATUS_CANCELLED;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task,
         NevercTaskKind Kind, void *ProcessState, void *State,
         void *TaskState) {
  (void)Core;
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  (void)TaskState;
  return State == &SessionState
             ? neverc_status_ok()
             : (NevercStatus){NEVERC_STATUS_INVALID_STATE, 0, 0};
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  uint32_t BytesToWrite;
  static const char PluginID[] = "org.neverc.test.diagnostic";
  static const char DisplayName[] = "Diagnostic Test Plugin";
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
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
