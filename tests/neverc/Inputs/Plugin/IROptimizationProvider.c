#include "neverc/Plugin/PluginIR.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercIROptimizationAPI *OptimizationAPI;
static const NevercIRPassAPI *PassAPI;
static int ProcessState;
#if defined(NEVERC_TEST_IR_OPTIMIZATION_LATE_NVK_REFERENCE)
static uint64_t OptimizationInvocationCount;
#endif

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

#if defined(NEVERC_TEST_IR_OPTIMIZATION_LATE_NVK_REFERENCE)
static NevercBool string_equals(NevercStringView Value, const char *Text,
                                size_t Length) {
  return Value.Length == (uint64_t)Length && Value.Data &&
                 memcmp(Value.Data, Text, Length) == 0
             ? NEVERC_TRUE
             : NEVERC_FALSE;
}

static NevercStatus inject_late_nvk_runtime_reference(
    const NevercPhaseFrame *Frame, const NevercIRCoreAPI *Core) {
  NevercIRModuleHandle Module = {0, 0};
  NevercIRValueCursor Cursor;
  NevercIRValueHandle Functions[128];
  uint64_t FunctionCount = 0;
  uint64_t I;
  NevercStatus Status =
      Core->GetModule(Core->Context, Frame->Task, &Module);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Cursor, 0, sizeof(Cursor));
  Status = Core->BeginValueCursor(
      Core->Context, Frame->Task, Module,
      NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Core->CollectValueCursor(Core->Context, Frame->Task, &Cursor,
                                    Functions, 128, &FunctionCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  for (I = 0; I != FunctionCount; ++I) {
    NevercStringView Name = {0, 0};
    Status = Core->GetValueName(Core->Context, Frame->Task, Functions[I],
                                &Name);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (string_equals(Name, "neverc_krt_fmt_init",
                      sizeof("neverc_krt_fmt_init") - 1) == NEVERC_TRUE)
      return neverc_status_ok();
    if (string_equals(Name, "plugin_late_nvk_runtime",
                      sizeof("plugin_late_nvk_runtime") - 1) != NEVERC_TRUE)
      continue;
    return Core->SetValueName(Core->Context, Frame->Task, Functions[I],
                              STRING_VIEW("neverc_krt_fmt_init"));
  }
  return failure(NEVERC_STATUS_NOT_FOUND);
}
#endif

static NevercStatus NEVERC_CALL fail_if_builtin_pipeline_runs(
    const NevercIRPassInvocation *Invocation,
    NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  (void)Invocation;
  (void)OutPreserved;
  (void)UserData;
  return failure(NEVERC_STATUS_VERIFICATION_FAILED);
}

static NevercStatus NEVERC_CALL
provide_optimized_module(const NevercPhaseFrame *Frame,
                         NevercPhaseResult *OutResult, void *UserData) {
  const NevercIRCoreAPI *Core = NULL;
  const NevercIRBuilderAPI *BuilderAPI = NULL;
  NevercIROptimizationPhaseInput Input;
  NevercIRModuleArtifactDescriptor Descriptor;
  NevercIRModuleHandle Module = {0, 0};
  NevercIRTypeHandle I32 = {0, 0};
  NevercIRTypeHandle MainType = {0, 0};
  NevercIRValueHandle Main = {0, 0};
  NevercIRValueHandle Entry = {0, 0};
  NevercIRValueHandle FortyTwo = {0, 0};
  NevercIRValueHandle Return = {0, 0};
  NevercIRMutationHandle Mutation = {0, 0};
  NevercIRBuilderHandle Builder = {0, 0};
  NevercArtifactHandle Output = {0, 0};
  uint64_t Word = 42;
  NevercStatus Status;
  (void)UserData;

  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_IR_OPTIMIZATION_API_MAJOR,
      NEVERC_IR_OPTIMIZATION_API_MINOR, 0};
  Status = OptimizationAPI->GetOptimizationPhaseInput(
      OptimizationAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK || Input.InputDigest.Length != 32)
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;

#if defined(NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH) ||                       \
    defined(NEVERC_TEST_IR_OPTIMIZATION_LATE_NVK_REFERENCE)
  // Publish the input module without invoking NeverC's builtin optimizer. This
  // fixture exercises invariants that must be sealed after a provider takes
  // ownership of the complete optimization transition.
#if defined(NEVERC_TEST_IR_OPTIMIZATION_LATE_NVK_REFERENCE)
  ++OptimizationInvocationCount;
  if (OptimizationInvocationCount >= 2) {
    Status = OptimizationAPI->GetInputModule(
        OptimizationAPI->Context, Frame, Frame->Input, &Core, &BuilderAPI);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = inject_late_nvk_runtime_reference(Frame, Core);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_IR_OPTIMIZATION_API_MAJOR,
      NEVERC_IR_OPTIMIZATION_API_MINOR, 0};
  Descriptor.Product =
      (NevercInterfaceID){NEVERC_PHASE_IR_OPTIMIZE_OUTPUT_HIGH,
                          NEVERC_PHASE_IR_OPTIMIZE_OUTPUT_LOW};
  Descriptor.DependencyDigest = Input.InputDigest;
  Status = OptimizationAPI->PublishModule(
      OptimizationAPI->Context, Frame, &Descriptor, &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();
#endif

  Status = OptimizationAPI->CreateModule(
      OptimizationAPI->Context, Frame, STRING_VIEW("plugin-optimized-main"),
      &Core, &BuilderAPI);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Core->GetModule(Core->Context, Frame->Task, &Module);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Core->GetIntegerType(Core->Context, Frame->Task, 32, &I32);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Core->GetFunctionType(Core->Context, Frame->Task, I32, NULL, 0,
                                   NEVERC_FALSE, &MainType);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = BuilderAPI->BeginMutation(
        BuilderAPI->Context, Frame->Task, NEVERC_IR_MUTATION_SCOPE_MODULE,
        Module, &Mutation);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = BuilderAPI->CreateFunction(
        BuilderAPI->Context, Frame->Task, Mutation, MainType,
        STRING_VIEW("main"), &Main);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = BuilderAPI->CreateBasicBlock(
        BuilderAPI->Context, Frame->Task, Mutation, Main,
        STRING_VIEW("entry"), &Entry);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Core->CreateIntegerConstant(Core->Context, Frame->Task, I32,
                                         &Word, 1, &FortyTwo);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = BuilderAPI->CreateBuilder(BuilderAPI->Context, Frame->Task,
                                       Mutation, &Builder);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = BuilderAPI->SetInsertBlock(BuilderAPI->Context, Frame->Task,
                                        Builder, Entry);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = BuilderAPI->BuildReturn(BuilderAPI->Context, Frame->Task, Builder,
                                     FortyTwo, &Return);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        BuilderAPI->CommitMutation(BuilderAPI->Context, Frame->Task, Mutation);
  else if (!neverc_handle_is_null(Mutation))
    (void)BuilderAPI->AbortMutation(BuilderAPI->Context, Frame->Task, Mutation);
  if (!neverc_handle_is_null(Builder))
    (void)BuilderAPI->DestroyBuilder(BuilderAPI->Context, Frame->Task, Builder);
  if (!neverc_handle_is_null(Mutation))
    (void)BuilderAPI->DestroyMutation(BuilderAPI->Context, Frame->Task,
                                      Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

#if defined(NEVERC_TEST_IR_OPTIMIZATION_INVALID)
  Status = Core->EraseValue(Core->Context, Frame->Task, Return);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_IR_OPTIMIZATION_API_MAJOR,
      NEVERC_IR_OPTIMIZATION_API_MINOR, 0};
  Descriptor.Product =
      (NevercInterfaceID){NEVERC_PHASE_IR_OPTIMIZE_OUTPUT_HIGH,
                          NEVERC_PHASE_IR_OPTIMIZE_OUTPUT_LOW};
  Descriptor.DependencyDigest = Input.InputDigest;
  Status = OptimizationAPI->PublishModule(
      OptimizationAPI->Context, Frame, &Descriptor, &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(const NevercCoreAPI *Core,
                                              void **OutProcessState) {
  (void)Core;
  if (!OutProcessState)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
#if defined(NEVERC_TEST_IR_OPTIMIZATION_LATE_NVK_REFERENCE)
  OptimizationInvocationCount = 0;
#endif
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *PluginProcessState) {
  NevercProviderDescriptor Provider;
  NevercIRPassDescriptor Pass;
  NevercStatus Status;
  (void)Core;
  (void)PluginProcessState;
  if (!Registrar || !Registrar->RegisterProvider)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase =
      (NevercInterfaceID){NEVERC_PHASE_IR_OPTIMIZE_HIGH,
                          NEVERC_PHASE_IR_OPTIMIZE_LOW};
  Provider.ProviderID = STRING_VIEW("neverc.test.ir-optimization-provider");
  Provider.Route.Header =
      (NevercABITableHeader){sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = provide_optimized_module;
  Status = Registrar->RegisterProvider(RegistrarContext, &Provider);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Pass, 0, sizeof(Pass));
  Pass.Header =
      (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  Pass.PassID = STRING_VIEW("neverc.test.must-not-run");
  Pass.Phase =
      (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                          NEVERC_PHASE_IR_PASS_PRE_OPT_LOW};
  Pass.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Pass.Deterministic = NEVERC_TRUE;
  Pass.Run = fail_if_builtin_pipeline_runs;
  return PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
}

static NevercStatus NEVERC_CALL destroy_plugin(
    const NevercCoreAPI *Core, void *PluginProcessState) {
  (void)Core;
  (void)PluginProcessState;
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;

  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_IR_OPTIMIZATION_HIGH,
                          NEVERC_INTERFACE_IR_OPTIMIZATION_LOW},
      NEVERC_IR_OPTIMIZATION_API_MAJOR, NEVERC_IR_OPTIMIZATION_API_MINOR,
      &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercIROptimizationAPI))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  OptimizationAPI = (const NevercIROptimizationAPI *)Table;

  Table = NULL;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                          NEVERC_INTERFACE_IR_PASS_LOW},
      NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercIRPassAPI))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercIRPassAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID =
      STRING_VIEW("org.neverc.test.ir-optimization-provider");
  Descriptor.DisplayName =
      STRING_VIEW("NeverC IR optimization provider test");
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
