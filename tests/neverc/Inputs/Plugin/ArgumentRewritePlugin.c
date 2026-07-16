#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(value)                                                     \
  (NevercStringView) { (value), (uint64_t)(sizeof(value) - 1) }

static const NevercDriverAPI *DriverAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int view_equals(NevercStringView View, const char *Text) {
  size_t Length = strlen(Text);
  return View.Length == (uint64_t)Length &&
         (Length == 0 || memcmp(View.Data, Text, Length) == 0);
}

static NevercStatus abort_mutation(NevercArgumentMutationHandle Mutation,
                                   NevercStatus Status) {
  (void)DriverAPI->AbortArgumentMutation(DriverAPI->Context, Mutation);
  return Status;
}

static NevercStatus invoke_next(const NevercPhaseFrame *Frame,
                                NevercPhaseContinuation *Continuation) {
  NevercPhaseResult Downstream;
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  return Continuation->InvokeNext(Continuation, Frame, &Downstream);
}

static NevercStatus NEVERC_CALL observe_arguments(const NevercPhaseFrame *Frame,
                                                  NevercObserverPoint Point,
                                                  void *UserData) {
  NevercArgumentMutationHandle Mutation = {0, 0};
  NevercStatus Status;
  (void)Point;
  (void)UserData;
  Status = DriverAPI->BeginArgumentMutation(DriverAPI->Context, Frame, NULL,
                                            Frame->Input, &Mutation);
  if (Status.Code != NEVERC_STATUS_POLICY_VIOLATION)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL rewrite_arguments(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercArgumentMutationHandle Mutation = {0, 0};
  NevercStatus Status;
  uint64_t Count = 0;
  uint64_t OptimizationIndex = UINT64_MAX;
  uint64_t WerrorIndex = UINT64_MAX;
  uint64_t Index;
  int InvalidMutationMode = 0;
  int ConfigOriginSeen = 0;
  int CommandOriginSeen = 0;
  (void)UserData;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  OutResult->Output = (NevercArtifactHandle){0, 0};
  OutResult->Proof = (NevercProofHandle){0, 0};

  Status = DriverAPI->GetArgumentCount(DriverAPI->Context, Frame, Frame->Input,
                                       &Count);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (Index = 0; Index < Count; ++Index) {
    NevercStringView Value = {0, 0};
    NevercStringView Source = {0, 0};
    NevercArgumentOrigin Origin = NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE;
    uint64_t Position = 0;
    Status = DriverAPI->GetArgument(DriverAPI->Context, Frame, Frame->Input,
                                    Index, &Value, &Origin, &Source, &Position);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (view_equals(Value, "-O0"))
      OptimizationIndex = Index;
    else if (view_equals(Value, "-Werror"))
      WerrorIndex = Index;
    else if (view_equals(Value, "-DNEVERC_TEST_INVALID_ARGUMENT_MUTATION=1"))
      InvalidMutationMode = 1;
    else if (view_equals(Value, "-DNEVERC_TEST_CONFIG_ORIGIN=1")) {
      if (Origin != NEVERC_ARGUMENT_ORIGIN_CONFIGURATION || Source.Length == 0)
        return failure(NEVERC_STATUS_PLUGIN_FAILURE);
      ConfigOriginSeen = 1;
    } else if (view_equals(Value, "-DNEVERC_TEST_COMMAND_ORIGIN=1")) {
      if (Origin != NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE || Position == 0)
        return failure(NEVERC_STATUS_PLUGIN_FAILURE);
      CommandOriginSeen = 1;
    }
  }
  if (ConfigOriginSeen || CommandOriginSeen) {
    if (!ConfigOriginSeen || !CommandOriginSeen)
      return failure(NEVERC_STATUS_PLUGIN_FAILURE);
    return invoke_next(Frame, Continuation);
  }
  if (InvalidMutationMode) {
    Status = DriverAPI->BeginArgumentMutation(
        DriverAPI->Context, Frame, Continuation, Frame->Input, &Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status =
        DriverAPI->InsertArgument(DriverAPI->Context, Mutation, 1,
                                  STRING_VIEW("-fplugin=/forged/plugin/path"));
    if (Status.Code == NEVERC_STATUS_OK)
      Status = DriverAPI->CommitArgumentMutation(DriverAPI->Context, Mutation);
    if (Status.Code == NEVERC_STATUS_OK)
      return failure(NEVERC_STATUS_PLUGIN_FAILURE);
    (void)DriverAPI->AbortArgumentMutation(DriverAPI->Context, Mutation);
    return invoke_next(Frame, Continuation);
  }
  if (OptimizationIndex == UINT64_MAX || WerrorIndex == UINT64_MAX)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = DriverAPI->BeginArgumentMutation(
      DriverAPI->Context, Frame, Continuation, Frame->Input, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = DriverAPI->ReplaceArgument(DriverAPI->Context, Mutation,
                                      OptimizationIndex, STRING_VIEW("-O2"));
  if (Status.Code != NEVERC_STATUS_OK)
    return abort_mutation(Mutation, Status);
  Status = DriverAPI->EraseArgument(DriverAPI->Context, Mutation, WerrorIndex);
  if (Status.Code != NEVERC_STATUS_OK)
    return abort_mutation(Mutation, Status);
  Status =
      DriverAPI->InsertArgument(DriverAPI->Context, Mutation, 1,
                                STRING_VIEW("-DNEVERC_ARGUMENT_REWRITTEN=1"));
  if (Status.Code != NEVERC_STATUS_OK)
    return abort_mutation(Mutation, Status);
  Status = DriverAPI->CommitArgumentMutation(DriverAPI->Context, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return abort_mutation(Mutation, Status);

  return invoke_next(Frame, Continuation);
}

static NevercStatus NEVERC_CALL process_begin(const NevercCoreAPI *Core,
                                              void **OutProcessState) {
  (void)Core;
  if (OutProcessState == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *PluginProcessState) {
  NevercObserverDescriptor Observer;
#if !defined(NEVERC_TEST_ARGUMENT_OBSERVER_ONLY)
  NevercInterceptorDescriptor Interceptor;
#endif
  NevercStatus Status;
  (void)Core;
  (void)PluginProcessState;
  if (Registrar == NULL || Registrar->RegisterInterceptor == NULL)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

  memset(&Observer, 0, sizeof(Observer));
  Observer.Header = (NevercABITableHeader){
      sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                                       NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = observe_arguments;
  Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

#if defined(NEVERC_TEST_ARGUMENT_OBSERVER_ONLY)
  return neverc_status_ok();
#else
  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
  Interceptor.Callback = rewrite_arguments;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
#endif
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                               void *PluginProcessState) {
  (void)Core;
  (void)PluginProcessState;
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  NevercInterfaceID DriverInterface = {NEVERC_INTERFACE_DRIVER_HIGH,
                                       NEVERC_INTERFACE_DRIVER_LOW};
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;

  if (Bootstrap == NULL || Bootstrap->QueryInterface == NULL ||
      OutPlugin == NULL ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, DriverInterface, NEVERC_DRIVER_API_MAJOR,
      NEVERC_DRIVER_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Table == NULL ||
      StructSize < offsetof(NevercDriverAPI, AbortArgumentMutation) +
                       sizeof(((NevercDriverAPI *)0)->AbortArgumentMutation))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  DriverAPI = (const NevercDriverAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.argument-rewrite");
  Descriptor.DisplayName = STRING_VIEW("NeverC argument rewrite test plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;

  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
