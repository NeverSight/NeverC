#include "neverc/Plugin/PluginSource.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const NevercSourceLocationAPI *SourceAPI;
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

static NevercStatus NEVERC_CALL source_open_provider(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  static const uint8_t Replacement[] =
      "int source_phase_replacement(void) { return 0; }\n";
  NevercSourceInputInfo Input;
  NevercMemorySourceUnitDescriptor Descriptor;
  NevercArtifactHandle Unit;
  NevercStatus Status;
  (void)UserData;

  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Input, 0, sizeof(Input));
  Input.Header.StructSize = sizeof(Input);
  Status = SourceAPI->GetSourceInput(
      SourceAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_SOURCE_API_MAJOR;
  Descriptor.Header.Minor = NEVERC_SOURCE_API_MINOR;
  Descriptor.LogicalPath = Input.Path;
  Descriptor.CanonicalIdentity =
      sv("neverc-memory://org.neverc.test.source-phase/main.c");
  Descriptor.Content.Data = Replacement;
  Descriptor.Content.Length = sizeof(Replacement) - 1;
  Descriptor.ProviderID = sv("org.neverc.test.source-phase");
  Descriptor.System = Input.System;
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.Cacheable = NEVERC_TRUE;
  Status = SourceAPI->CreateMemorySourceUnit(
      SourceAPI->Context, Frame, Frame->Input, &Descriptor, &Unit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Unit;
  trace("source-open-provider");
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL source_after_open(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData) {
  NevercSourceUnitInfo Unit;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || Point != NEVERC_OBSERVER_AFTER)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Unit, 0, sizeof(Unit));
  Unit.Header.StructSize = sizeof(Unit);
  Status = SourceAPI->GetSourceUnit(
      SourceAPI->Context, Frame, Frame->CurrentOutput, &Unit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  trace(Unit.MemoryBacked == NEVERC_TRUE
            ? "source-after-open:memory"
            : "source-after-open:file");
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
  NevercObserverDescriptor Observer;
  NevercStatus Status;
  (void)Core;
  (void)State;
  if (!Registrar)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header.StructSize = sizeof(Provider);
  Provider.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.Phase.High = NEVERC_PHASE_SOURCE_OPEN_HIGH;
  Provider.Phase.Low = NEVERC_PHASE_SOURCE_OPEN_LOW;
  Provider.ProviderID = sv("neverc.test.source-phase");
  Provider.Route.Header.StructSize = sizeof(Provider.Route);
  Provider.Route.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Provider.Route.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = source_open_provider;
  Status = Registrar->RegisterProvider(RegistrarContext, &Provider);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Observer, 0, sizeof(Observer));
  Observer.Header.StructSize = sizeof(Observer);
  Observer.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Observer.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Observer.Phase.High = NEVERC_PHASE_SOURCE_AFTER_OPEN_HIGH;
  Observer.Phase.Low = NEVERC_PHASE_SOURCE_AFTER_OPEN_LOW;
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = source_after_open;
  return Registrar->RegisterObserver(RegistrarContext, &Observer);
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
  Interface.High = NEVERC_INTERFACE_SOURCE_LOCATION_HIGH;
  Interface.Low = NEVERC_INTERFACE_SOURCE_LOCATION_LOW;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, NEVERC_SOURCE_API_MAJOR,
      NEVERC_SOURCE_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize <
          offsetof(NevercSourceLocationAPI, GetSourceUnit) +
              sizeof(((NevercSourceLocationAPI *)0)->GetSourceUnit))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  SourceAPI = (const NevercSourceLocationAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID = sv("org.neverc.test.source-phase");
  Descriptor.DisplayName = sv("NeverC source phase replacement test");
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
