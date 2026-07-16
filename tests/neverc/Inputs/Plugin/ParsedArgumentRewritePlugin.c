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

static int occurrence_is(const NevercOptionOccurrence *Occurrence,
                         const char *Spelling, const char *Value) {
  const NevercStringView *Values;
  if (!view_equals(Occurrence->Spelling, Spelling))
    return 0;
  if (Value == NULL)
    return Occurrence->Values.Count == 0;
  if (Occurrence->Values.Count != 1 ||
      Occurrence->Values.ElementStride < sizeof(NevercStringView))
    return 0;
  Values = (const NevercStringView *)Occurrence->Values.Data;
  return view_equals(Values[0], Value);
}

static NevercStatus invoke_next(const NevercPhaseFrame *Frame,
                                NevercPhaseContinuation *Continuation) {
  NevercPhaseResult Downstream;
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  return Continuation->InvokeNext(Continuation, Frame, &Downstream);
}

static NevercStatus NEVERC_CALL rewrite_parsed_arguments(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParsedArgumentMutationHandle Mutation = {0, 0};
  NevercStringView DefineValue[] = {
      STRING_VIEW("NEVERC_PARSED_ARGUMENT_REWRITTEN=1")};
  NevercStringList O2Values = {NULL, 0, sizeof(NevercStringView)};
  NevercStringList DefineValues = {DefineValue, 1, sizeof(DefineValue[0])};
  NevercStatus Status;
  uint64_t Count = 0;
  uint64_t OptimizationOccurrence = UINT64_MAX;
  uint64_t WerrorOccurrence = UINT64_MAX;
  uint64_t Index;
  (void)UserData;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  OutResult->Output = (NevercArtifactHandle){0, 0};
  OutResult->Proof = (NevercProofHandle){0, 0};

  Status = DriverAPI->GetOptionOccurrenceCount(DriverAPI->Context, Frame,
                                               Frame->Input, &Count);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (Index = 0; Index < Count; ++Index) {
    NevercOptionOccurrence Occurrence;
    memset(&Occurrence, 0, sizeof(Occurrence));
    Occurrence.Header =
        (NevercABITableHeader){sizeof(Occurrence), NEVERC_DRIVER_API_MAJOR,
                               NEVERC_DRIVER_API_MINOR, 0};
    Status = DriverAPI->GetOptionOccurrence(DriverAPI->Context, Frame,
                                            Frame->Input, Index, &Occurrence);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (occurrence_is(&Occurrence, "-O0", NULL))
      OptimizationOccurrence = Occurrence.Occurrence;
    else if (occurrence_is(&Occurrence, "-W", "error"))
      WerrorOccurrence = Occurrence.Occurrence;
  }
  if (OptimizationOccurrence == UINT64_MAX || WerrorOccurrence == UINT64_MAX)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = DriverAPI->BeginParsedArgumentMutation(
      DriverAPI->Context, Frame, Continuation, Frame->Input, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = DriverAPI->ReplaceOptionOccurrence(DriverAPI->Context, Mutation,
                                              OptimizationOccurrence,
                                              STRING_VIEW("-O2"), O2Values);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abort_mutation;
  Status = DriverAPI->RemoveOptionOccurrence(DriverAPI->Context, Mutation,
                                             WerrorOccurrence);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abort_mutation;
  Status = DriverAPI->AddOptionOccurrence(DriverAPI->Context, Mutation,
                                          STRING_VIEW("-D"), DefineValues);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abort_mutation;
  Status =
      DriverAPI->CommitParsedArgumentMutation(DriverAPI->Context, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abort_mutation;
  return invoke_next(Frame, Continuation);

abort_mutation:
  (void)DriverAPI->AbortParsedArgumentMutation(DriverAPI->Context, Mutation);
  return Status;
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
  NevercInterceptorDescriptor Interceptor;
  (void)Core;
  (void)PluginProcessState;
  if (Registrar == NULL || Registrar->RegisterInterceptor == NULL)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_DRIVER_PARSED_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_PARSED_ARGUMENTS_LOW};
  Interceptor.Callback = rewrite_parsed_arguments;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
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
      StructSize <
          offsetof(NevercDriverAPI, AbortParsedArgumentMutation) +
              sizeof(((NevercDriverAPI *)0)->AbortParsedArgumentMutation))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  DriverAPI = (const NevercDriverAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.parsed-argument-rewrite");
  Descriptor.DisplayName =
      STRING_VIEW("NeverC parsed argument rewrite test plugin");
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
