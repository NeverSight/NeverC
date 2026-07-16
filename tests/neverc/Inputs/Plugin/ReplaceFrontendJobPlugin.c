#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const NevercDriverAPI *DriverAPI;
static int ProcessState;

static NevercStringView sv(const char *Text) {
  NevercStringView View;
  View.Data = Text;
  View.Length = (uint64_t)strlen(Text);
  return View;
}

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void trace(const char *Event) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *File;
  if (!Path || !*Path)
    return;
  File = fopen(Path, "ab");
  if (!File)
    return;
  fprintf(File, "%s\n", Event);
  fclose(File);
}

static NevercStatus NEVERC_CALL replace_frontend_job(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  NevercJobExecutionRequest Request;
  NevercJobResultDescriptor Descriptor;
  NevercArtifactHandle Result;
  NevercStatus Status;
  (void)UserData;

  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Request, 0, sizeof(Request));
  Request.Header.StructSize = sizeof(Request);
  Request.Header.Major = NEVERC_DRIVER_API_MAJOR;
  Request.Header.Minor = NEVERC_DRIVER_API_MINOR;
  Status = DriverAPI->GetJobExecutionRequest(
      DriverAPI->Context, Frame, Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Request.Job.Kind != NEVERC_JOB_FRONTEND)
    return failure(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

  trace("execute-job:replacement");
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_DRIVER_API_MAJOR;
  Descriptor.Header.Minor = NEVERC_DRIVER_API_MINOR;
  Descriptor.ExitCode = 0;
  Descriptor.ExecutionFailed = NEVERC_FALSE;
  Status = DriverAPI->CreateJobResult(
      DriverAPI->Context, Frame, Frame->Input, &Descriptor, &Result);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Result;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(
    const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  if (!OutProcessState)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *State) {
  NevercProviderDescriptor Provider;
  (void)Core;
  (void)State;
  if (!Registrar)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header.StructSize = sizeof(Provider);
  Provider.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.Phase.High = NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH;
  Provider.Phase.Low = NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW;
  Provider.ProviderID = sv("neverc.test.replace-frontend-job");
  Provider.Route.Header.StructSize = sizeof(Provider.Route);
  Provider.Route.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Route.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = replace_frontend_job;
  return Registrar->RegisterProvider(RegistrarContext, &Provider);
}

static NevercStatus NEVERC_CALL destroy_plugin(
    const NevercCoreAPI *Core, void *State) {
  (void)Core;
  (void)State;
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercInterfaceID Interface;
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;

  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Interface.High = NEVERC_INTERFACE_DRIVER_HIGH;
  Interface.Low = NEVERC_INTERFACE_DRIVER_LOW;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize <
          offsetof(NevercDriverAPI, CreateJobResult) +
              sizeof(((NevercDriverAPI *)0)->CreateJobResult))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  DriverAPI = (const NevercDriverAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID = sv("org.neverc.test.replace-frontend-job");
  Descriptor.DisplayName = sv("NeverC replace frontend job test plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
