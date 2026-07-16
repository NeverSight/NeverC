#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static NevercStatus NEVERC_CALL fail_job(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  (void)Frame;
  (void)OutResult;
  (void)UserData;
  trace("execute-job:failure");
  return failure(NEVERC_STATUS_PLUGIN_FAILURE);
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
  Provider.ProviderID = sv("neverc.test.fail-job");
  Provider.Route.Header.StructSize = sizeof(Provider.Route);
  Provider.Route.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Route.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = fail_job;
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
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;

  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID = sv("org.neverc.test.fail-job");
  Descriptor.DisplayName = sv("NeverC failing job test plugin");
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
