#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(value)                                                     \
  (NevercStringView) { (value), (uint64_t)(sizeof(value) - 1) }

#ifndef NEVERC_TEST_TOOLCHAIN_PLUGIN_ID
#define NEVERC_TEST_TOOLCHAIN_PLUGIN_ID "org.neverc.test.toolchain-rewrite"
#endif

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

static NevercStatus invoke_next(const NevercPhaseFrame *Frame,
                                NevercPhaseContinuation *Continuation) {
  NevercPhaseResult Downstream;
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  return Continuation->InvokeNext(Continuation, Frame, &Downstream);
}

static NevercStatus NEVERC_CALL select_toolchain(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercToolChainRequest Request;
  NevercStatus Status;
  (void)UserData;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;

  memset(&Request, 0, sizeof(Request));
  Request.Header = (NevercABITableHeader){
      sizeof(Request), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  Status = DriverAPI->GetToolChainRequest(DriverAPI->Context, Frame,
                                          Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!view_equals(Request.RequestedTriple, "x86_64-unknown-linux-gnu") ||
      !view_equals(Request.ComputedTriple, "x86_64-unknown-linux-gnu") ||
      Request.DynamicCodeProfile != NEVERC_FALSE)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

#if defined(NEVERC_TEST_TOOLCHAIN_REPLACE) ||                                  \
    defined(NEVERC_TEST_TOOLCHAIN_CUSTOM)
  {
    NevercToolChainSelectionDescriptor Descriptor;
    NevercArtifactHandle Selection = {0, 0};
    NevercStringView Features[] = {STRING_VIEW("+neon")};
    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Header =
        (NevercABITableHeader){sizeof(Descriptor), NEVERC_DRIVER_API_MAJOR,
                               NEVERC_DRIVER_API_MINOR, 0};
#if defined(NEVERC_TEST_TOOLCHAIN_CUSTOM)
    Descriptor.ToolChainID = STRING_VIEW("org.neverc.test.custom");
    Descriptor.Provider = (NevercToolChainProviderHandle){1, 1};
#else
    Descriptor.ToolChainID = STRING_VIEW("neverc.builtin.linux");
#endif
    Descriptor.TargetKey = STRING_VIEW("aarch64-unknown-linux-gnu");
    Descriptor.TargetTriple = STRING_VIEW("aarch64-unknown-linux-gnu");
    Descriptor.CPU = STRING_VIEW("generic");
    Descriptor.Features = (NevercStringList){Features, 1, sizeof(Features[0])};
    Status = DriverAPI->CreateToolChainSelection(
        DriverAPI->Context, Frame, Frame->Input, &Descriptor, &Selection);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    OutResult->Action = NEVERC_PHASE_REPLACE;
    OutResult->Output = Selection;
    return neverc_status_ok();
  }
#else
  {
    NevercToolChainMutationHandle Mutation = {0, 0};
    NevercStringView Features[] = {STRING_VIEW("+neon")};
    NevercStringList FeatureList = {Features, 1, sizeof(Features[0])};
    Status = DriverAPI->BeginToolChainMutation(
        DriverAPI->Context, Frame, Continuation, Frame->Input, &Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
#if defined(NEVERC_TEST_TOOLCHAIN_INVALID)
    Status = DriverAPI->SetToolChainTriple(DriverAPI->Context, Mutation,
                                           STRING_VIEW("not-a-triple"));
#else
    Status = DriverAPI->SetToolChainTriple(
        DriverAPI->Context, Mutation, STRING_VIEW("aarch64-unknown-linux-gnu"));
    if (Status.Code == NEVERC_STATUS_OK)
      Status = DriverAPI->SetToolChainCPU(DriverAPI->Context, Mutation,
                                          STRING_VIEW("generic"));
    if (Status.Code == NEVERC_STATUS_OK)
      Status = DriverAPI->SetToolChainFeatures(DriverAPI->Context, Mutation,
                                               FeatureList);
#endif
    if (Status.Code == NEVERC_STATUS_OK)
      Status = DriverAPI->CommitToolChainMutation(DriverAPI->Context, Mutation);
    if (Status.Code != NEVERC_STATUS_OK) {
      (void)DriverAPI->AbortToolChainMutation(DriverAPI->Context, Mutation);
      return Status;
    }
    return invoke_next(Frame, Continuation);
  }
#endif
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
      (NevercInterfaceID){NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_HIGH,
                          NEVERC_PHASE_DRIVER_SELECT_TOOLCHAIN_LOW};
  Interceptor.Callback = select_toolchain;
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
      StructSize < offsetof(NevercDriverAPI, GetToolChainSelection) +
                       sizeof(((NevercDriverAPI *)0)->GetToolChainSelection))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  DriverAPI = (const NevercDriverAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW(NEVERC_TEST_TOOLCHAIN_PLUGIN_ID);
  Descriptor.DisplayName = STRING_VIEW("NeverC ToolChain rewrite test plugin");
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
