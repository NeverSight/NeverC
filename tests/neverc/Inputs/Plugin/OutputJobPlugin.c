#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "neverc/Plugin/PluginSource.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef NEVERC_TEST_OUTPUT_JOB_MODE
#define NEVERC_TEST_OUTPUT_JOB_MODE 1
#endif

static const NevercDriverAPI *DriverAPI;
static const NevercIOAPI *IOAPI;
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

static NevercStatus finish_file_output(
    NevercTaskHandle Task, NevercStringView Path, const uint8_t *Bytes,
    uint64_t ByteCount, NevercOutputSinkHandle *OutSink,
    NevercOutputSeal *OutSeal) {
  NevercStatus Status;
  memset(OutSink, 0, sizeof(*OutSink));
  memset(OutSeal, 0, sizeof(*OutSeal));
  OutSeal->Header.StructSize = sizeof(*OutSeal);
  Status = IOAPI->BeginFileOutput(IOAPI->Context, Task, Path,
                                  UINT64_C(1024), OutSink);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = IOAPI->OutputWrite(
      IOAPI->Context, Task, *OutSink,
      (NevercByteView){Bytes, ByteCount});
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)IOAPI->OutputAbort(IOAPI->Context, Task, *OutSink);
    return Status;
  }
  Status =
      IOAPI->OutputFinish(IOAPI->Context, Task, *OutSink, OutSeal);
  if (Status.Code != NEVERC_STATUS_OK)
    (void)IOAPI->OutputAbort(IOAPI->Context, Task, *OutSink);
  return Status;
}

static NevercStatus NEVERC_CALL replace_output_job(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  static const uint8_t OutputBytes[] = "plugin-object";
  static const uint8_t ExtraBytes[] = "undeclared-output";
  NevercJobExecutionRequest Request;
  NevercJobResultDescriptor Descriptor;
  NevercOutputSinkHandle Sinks[2];
  NevercOutputSeal Seals[2];
  NevercOutputSealHandle OutputSeals[2];
  NevercArtifactHandle Result;
  const NevercJobFile *Output;
  NevercStatus Status;
  uint64_t SealCount = 0;
  uint64_t SinkCount = 0;
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
  if (!Request.Outputs.Data || Request.Outputs.Count != 1 ||
      Request.Outputs.ElementStride < sizeof(NevercJobFile))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Output = (const NevercJobFile *)(const void *)Request.Outputs.Data;
#if NEVERC_TEST_OUTPUT_JOB_MODE != 2
  Status = finish_file_output(
      Frame->Task, Output->Path, OutputBytes, sizeof(OutputBytes) - 1,
      &Sinks[SinkCount], &Seals[SealCount]);
  if (Status.Code != NEVERC_STATUS_OK) {
    return Status;
  }
  OutputSeals[SealCount] = Seals[SealCount].Handle;
  ++SealCount;
  ++SinkCount;
#endif

#if NEVERC_TEST_OUTPUT_JOB_MODE == 3
  {
    char ExtraPath[4096];
    NevercStringView ExtraPathView;
    static const char Suffix[] = ".extra";
    if (!Output->Path.Data ||
        Output->Path.Length > sizeof(ExtraPath) - sizeof(Suffix))
      return failure(NEVERC_STATUS_INVALID_ARGUMENT);
    memcpy(ExtraPath, Output->Path.Data, (size_t)Output->Path.Length);
    memcpy(ExtraPath + Output->Path.Length, Suffix, sizeof(Suffix) - 1);
    ExtraPathView.Data = ExtraPath;
    ExtraPathView.Length =
        Output->Path.Length + (uint64_t)sizeof(Suffix) - 1;
    Status = finish_file_output(
        Frame->Task, ExtraPathView, ExtraBytes, sizeof(ExtraBytes) - 1,
        &Sinks[SinkCount], &Seals[SealCount]);
    if (Status.Code != NEVERC_STATUS_OK) {
      uint64_t Index;
      for (Index = 0; Index != SinkCount; ++Index)
        (void)IOAPI->OutputAbort(IOAPI->Context, Frame->Task,
                                 Sinks[Index]);
      return Status;
    }
    OutputSeals[SealCount] = Seals[SealCount].Handle;
    ++SealCount;
    ++SinkCount;
  }
#endif

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_DRIVER_API_MAJOR;
  Descriptor.Header.Minor = NEVERC_DRIVER_API_MINOR;
  Descriptor.ExitCode = 0;
  Descriptor.ExecutionFailed = NEVERC_FALSE;
  if (SealCount != 0) {
    Descriptor.OutputSeals.Data = OutputSeals;
    Descriptor.OutputSeals.Count = SealCount;
    Descriptor.OutputSeals.ElementStride = sizeof(OutputSeals[0]);
  }
  Status = DriverAPI->CreateJobResult(
      DriverAPI->Context, Frame, Frame->Input, &Descriptor, &Result);
  if (Status.Code != NEVERC_STATUS_OK) {
    uint64_t Index;
    for (Index = 0; Index != SinkCount; ++Index)
      (void)IOAPI->OutputAbort(IOAPI->Context, Frame->Task, Sinks[Index]);
    return Status;
  }

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
  Provider.ProviderID = sv("neverc.test.output-job");
  Provider.Route.Header.StructSize = sizeof(Provider.Route);
  Provider.Route.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Route.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = replace_output_job;
  return Registrar->RegisterProvider(RegistrarContext, &Provider);
}

static NevercStatus NEVERC_CALL destroy_plugin(
    const NevercCoreAPI *Core, void *State) {
  (void)Core;
  (void)State;
  return neverc_status_ok();
}

static NevercStatus query_interface(
    const NevercBootstrapAPI *Bootstrap, NevercInterfaceID Interface,
    uint16_t Major, uint16_t Minor, const void **OutTable,
    uint64_t MinimumSize) {
  uint16_t NegotiatedMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, Major, Minor, OutTable,
      &NegotiatedMinor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!*OutTable || StructSize < MinimumSize)
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercInterfaceID DriverInterface;
  NevercInterfaceID IOInterface;
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;

  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  DriverInterface.High = NEVERC_INTERFACE_DRIVER_HIGH;
  DriverInterface.Low = NEVERC_INTERFACE_DRIVER_LOW;
  Status = query_interface(
      Bootstrap, DriverInterface, NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, &Table,
      offsetof(NevercDriverAPI, CreateJobResult) +
          sizeof(((NevercDriverAPI *)0)->CreateJobResult));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  DriverAPI = (const NevercDriverAPI *)Table;

  IOInterface.High = NEVERC_INTERFACE_IO_HIGH;
  IOInterface.Low = NEVERC_INTERFACE_IO_LOW;
  Table = NULL;
  Status = query_interface(
      Bootstrap, IOInterface, NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR,
      &Table, offsetof(NevercIOAPI, OutputGetSummary) +
                  sizeof(((NevercIOAPI *)0)->OutputGetSummary));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  IOAPI = (const NevercIOAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID = sv("org.neverc.test.output-job");
  Descriptor.DisplayName = sv("NeverC output job test plugin");
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
