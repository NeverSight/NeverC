#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginDriver.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NEVERC_TEST_PLUGIN_ID
#define NEVERC_TEST_PLUGIN_ID "org.neverc.test.minimal"
#endif

#ifndef NEVERC_TEST_PLUGIN_DISPLAY_NAME
#define NEVERC_TEST_PLUGIN_DISPLAY_NAME "NeverC Registry Test Plugin"
#endif

#ifndef NEVERC_TEST_PLUGIN_ABI_MAJOR
#define NEVERC_TEST_PLUGIN_ABI_MAJOR NEVERC_PLUGIN_ABI_MAJOR
#endif

#ifndef NEVERC_TEST_PLUGIN_VERSION_MAJOR
#define NEVERC_TEST_PLUGIN_VERSION_MAJOR 1
#endif

#ifndef NEVERC_TEST_PLUGIN_VERSION_MINOR
#define NEVERC_TEST_PLUGIN_VERSION_MINOR 2
#endif

#ifndef NEVERC_TEST_PLUGIN_VERSION_PATCH
#define NEVERC_TEST_PLUGIN_VERSION_PATCH 3
#endif

#ifndef NEVERC_TEST_CONCURRENCY
#define NEVERC_TEST_CONCURRENCY NEVERC_CONCURRENCY_SESSION_SERIAL
#endif

#ifndef NEVERC_TEST_REENTRANCY
#define NEVERC_TEST_REENTRANCY NEVERC_REENTRANCY_NONE
#endif

#define NEVERC_TEST_STRING_VIEW(value)                                        \
  { (value), (uint64_t)(sizeof(value) - 1) }

#if defined(NEVERC_TEST_REQUIRED_INTERFACE_HIGH)
static const NevercInterfaceRequirement RequiredInterfaces[] = {{
    .Header = {sizeof(NevercInterfaceRequirement), NEVERC_PLUGIN_ABI_MAJOR,
               NEVERC_PLUGIN_ABI_MINOR, 0},
    .Interface = {NEVERC_TEST_REQUIRED_INTERFACE_HIGH,
                  NEVERC_TEST_REQUIRED_INTERFACE_LOW},
    .Major = NEVERC_TEST_REQUIRED_INTERFACE_MAJOR,
    .MinimumMinor = NEVERC_TEST_REQUIRED_INTERFACE_MINOR,
    .Required = NEVERC_TRUE,
    .Stability = NEVERC_INTERFACE_STABLE,
    .Compatibility =
        {
            .Header = {sizeof(NevercCompatibilityKey),
                       NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0},
        },
}};
#endif

#if defined(NEVERC_TEST_DEPENDENCY_ID)
static const NevercPluginDependency Dependencies[] = {{
    .Header = {sizeof(NevercPluginDependency), NEVERC_PLUGIN_ABI_MAJOR,
               NEVERC_PLUGIN_ABI_MINOR, 0},
    .PluginID =
        (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_DEPENDENCY_ID),
    .Version =
        {
            .MinimumInclusive =
                {
                    .Major = NEVERC_TEST_DEPENDENCY_MIN_MAJOR,
                    .Minor = NEVERC_TEST_DEPENDENCY_MIN_MINOR,
                    .Patch = NEVERC_TEST_DEPENDENCY_MIN_PATCH,
                },
#if defined(NEVERC_TEST_DEPENDENCY_MAX_MAJOR)
            .MaximumExclusive =
                {
                    .Major = NEVERC_TEST_DEPENDENCY_MAX_MAJOR,
                    .Minor = NEVERC_TEST_DEPENDENCY_MAX_MINOR,
                    .Patch = NEVERC_TEST_DEPENDENCY_MAX_PATCH,
                },
            .HasMaximum = NEVERC_TRUE,
#endif
            .AllowPrerelease = NEVERC_FALSE,
        },
    .Kind = NEVERC_TEST_DEPENDENCY_KIND,
}};
#endif

static int ProcessStateStorage;

static void trace_event(const char *Event) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  fprintf(Trace, "%s:%s\n", NEVERC_TEST_PLUGIN_ID, Event);
  fclose(Trace);
}

static NevercStatus failed_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
  return Status;
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  trace_event("process_begin");
  if (OutProcessState == NULL)
    return failed_status();
  *OutProcessState = NULL;
#if defined(NEVERC_TEST_PROCESS_BEGIN_FAILURE)
  return failed_status();
#else
  *OutProcessState = &ProcessStateStorage;
  return neverc_status_ok();
#endif
}

#if defined(NEVERC_TEST_SCOPE_CALLBACKS)
typedef struct FixtureScopeState {
  const NevercCoreAPI *Core;
  uint64_t Marker;
} FixtureScopeState;

static NevercStatus allocate_scope_state(const NevercCoreAPI *Core,
                                         uint64_t Marker, void **OutState) {
  FixtureScopeState *State = NULL;
  NevercStatus Status;
  if (OutState == NULL)
    return failed_status();
  *OutState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State), UINT64_C(8),
                          (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Core = Core;
  State->Marker = Marker;
  *OutState = State;
  return neverc_status_ok();
}

static NevercStatus release_scope_state(void *State) {
  FixtureScopeState *ScopeState = (FixtureScopeState *)State;
  if (ScopeState == NULL)
    return neverc_status_ok();
  return ScopeState->Core->Deallocate(ScopeState->Core->Context, ScopeState,
                                      sizeof(*ScopeState), UINT64_C(8));
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *ProcessState, void **OutSessionState) {
  (void)Session;
  (void)ProcessState;
  trace_event("session_begin");
#if defined(NEVERC_TEST_SESSION_BEGIN_FAILURE)
  if (OutSessionState != NULL)
    *OutSessionState = NULL;
  return failed_status();
#else
  return allocate_scope_state(Core, UINT64_C(0x53455353494f4e), OutSessionState);
#endif
}

static NevercStatus NEVERC_CALL
session_end(const NevercCoreAPI *Core, NevercSessionHandle Session,
            void *ProcessState, void *SessionState) {
  (void)Core;
  (void)Session;
  (void)ProcessState;
  trace_event("session_end");
  return release_scope_state(SessionState);
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task,
           NevercTaskKind Kind, void *ProcessState, void *SessionState,
           void **OutTaskState) {
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  (void)SessionState;
  trace_event("task_begin");
#if defined(NEVERC_TEST_TASK_BEGIN_FAILURE)
  if (OutTaskState != NULL)
    *OutTaskState = NULL;
  return failed_status();
#else
  return allocate_scope_state(Core, UINT64_C(0x5441534b), OutTaskState);
#endif
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task,
         NevercTaskKind Kind, void *ProcessState, void *SessionState,
         void *TaskState) {
  (void)Core;
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  (void)SessionState;
  trace_event("task_end");
  return release_scope_state(TaskState);
}
#endif

static NevercStatus NEVERC_CALL
fixture_observer(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                 void *UserData) {
  (void)Frame;
  (void)Point;
  (void)UserData;
  return neverc_status_ok();
}

static void NEVERC_CALL destroy_fixture_userdata(void *UserData) {
  trace_event((const char *)UserData);
}

#if defined(NEVERC_TEST_REGISTER_OPTION)
static NevercStatus register_fixture_option(const NevercRegistrarAPI *Registrar,
                                            void *RegistrarContext) {
  NevercOptionDescriptor Option;
  memset(&Option, 0, sizeof(Option));
  Option.Header.StructSize = sizeof(Option);
  Option.Header.Major = NEVERC_DRIVER_API_MAJOR;
  Option.Header.Minor = NEVERC_DRIVER_API_MINOR;
  Option.Spelling =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("--fixture-level");
  Option.Form = NEVERC_OPTION_SEPARATE;
  Option.ValueType = NEVERC_OPTION_UINT;
  Option.Multiplicity = NEVERC_OPTION_SINGLE;
  Option.Help = (NevercStringView)NEVERC_TEST_STRING_VIEW("fixture level");
  Option.Metavar = (NevercStringView)NEVERC_TEST_STRING_VIEW("LEVEL");
  if (Registrar->RegisterOption == NULL)
    return failed_status();
  return Registrar->RegisterOption(RegistrarContext, &Option);
}
#endif

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)Registrar;
  (void)RegistrarContext;
  (void)ProcessState;
  trace_event("register");
#if defined(NEVERC_TEST_REGISTER_USERDATA)
  {
    NevercObserverDescriptor Observer;
    NevercStatus Status;
    memset(&Observer, 0, sizeof(Observer));
    Observer.Header.StructSize = sizeof(Observer);
    Observer.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Observer.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Observer.Phase.High = UINT64_C(0x1234567890abcdef);
    Observer.Phase.Low = UINT64_C(0xfedcba0987654321);
    Observer.Points = NEVERC_OBSERVER_BEFORE;
    Observer.Callback = fixture_observer;
    Observer.UserData = (void *)"userdata_first_destroy";
    Observer.DestroyUserData = destroy_fixture_userdata;
    Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Observer.UserData = (void *)"userdata_second_destroy";
    Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#if defined(NEVERC_TEST_REGISTER_OPTION)
  {
    NevercStatus Status =
        register_fixture_option(Registrar, RegistrarContext);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#if defined(NEVERC_TEST_REGISTRATION_FAILURE)
  return failed_status();
#else
  return neverc_status_ok();
#endif
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  trace_event("destroy");
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;

  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t)) {
    NevercStatus Error = neverc_status_ok();
    Error.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Error;
  }

  trace_event("entry");
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
#if defined(NEVERC_TEST_SHORT_DESCRIPTOR)
  Descriptor.Header.StructSize =
      (uint32_t)offsetof(NevercPluginDescriptor, Register);
#elif defined(NEVERC_TEST_PREFIX_ONLY_DESCRIPTOR)
  Descriptor.Header.StructSize =
      (uint32_t)(offsetof(NevercPluginDescriptor, Register) +
                 sizeof(Descriptor.Register));
#elif defined(NEVERC_TEST_LONG_DESCRIPTOR)
  Descriptor.Header.StructSize = (uint32_t)(sizeof(Descriptor) + 64);
#else
  Descriptor.Header.StructSize = (uint32_t)sizeof(Descriptor);
#endif
  Descriptor.Header.Major = NEVERC_TEST_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_PLUGIN_DISPLAY_NAME);
  Descriptor.Version.Major = NEVERC_TEST_PLUGIN_VERSION_MAJOR;
  Descriptor.Version.Minor = NEVERC_TEST_PLUGIN_VERSION_MINOR;
  Descriptor.Version.Patch = NEVERC_TEST_PLUGIN_VERSION_PATCH;
  Descriptor.Concurrency = NEVERC_TEST_CONCURRENCY;
  Descriptor.Reentrancy = NEVERC_TEST_REENTRANCY;
  Descriptor.ProcessBegin = process_begin;
#if defined(NEVERC_TEST_REQUIRED_INTERFACE_HIGH)
  Descriptor.RequiredInterfaces.Data = RequiredInterfaces;
  Descriptor.RequiredInterfaces.Count =
      sizeof(RequiredInterfaces) / sizeof(RequiredInterfaces[0]);
  Descriptor.RequiredInterfaces.ElementStride =
      sizeof(NevercInterfaceRequirement);
#endif
#if defined(NEVERC_TEST_DEPENDENCY_ID)
  Descriptor.Dependencies.Data = Dependencies;
  Descriptor.Dependencies.Count =
      sizeof(Dependencies) / sizeof(Dependencies[0]);
  Descriptor.Dependencies.ElementStride = sizeof(NevercPluginDependency);
#endif
  Descriptor.Register = register_plugin;
#if defined(NEVERC_TEST_SCOPE_CALLBACKS)
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
#endif
  Descriptor.Destroy = destroy_plugin;

  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
