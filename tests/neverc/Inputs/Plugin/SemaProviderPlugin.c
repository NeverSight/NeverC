#include "neverc/Plugin/PluginSema.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercSemaAPI *SemaAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercInterfaceID semantic_product(void) {
#if defined(NEVERC_TEST_SEMA_PROVIDER_CUSTOM_PRODUCT)
  return (NevercInterfaceID){UINT64_C(0x4e435053454d50ff),
                             UINT64_C(0x0000000000000001)};
#else
  return (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                             NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
#endif
}

static NevercStatus NEVERC_CALL
provide_semantic_unit(const NevercPhaseFrame *Frame,
                      NevercPhaseResult *OutResult, void *UserData) {
  NevercSemaPhaseInput Input;
  NevercSemanticUnitDescriptor Descriptor;
  NevercArtifactHandle Output = {0, 0};
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_SEMA_API_MAJOR,
                                        NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetAnalyzePhaseInput(SemaAPI->Context, Frame, Frame->Input,
                                         &Input);
  if (Status.Code != NEVERC_STATUS_OK ||
      neverc_handle_is_null(Input.TranslationUnit) ||
      neverc_handle_is_null(Input.ASTUnit))
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Descriptor.Product = semantic_product();
  Descriptor.TranslationUnit = Input.TranslationUnit;
  Descriptor.SemanticComplete = NEVERC_TRUE;
  Status = SemaAPI->CreateSemanticUnit(SemaAPI->Context, Frame, &Descriptor,
                                       &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
verify_semantic_unit(const NevercPhaseFrame *Frame,
                     NevercPhaseContinuation *Continuation,
                     NevercPhaseResult *OutResult, void *UserData) {
  NevercSemanticUnitInfo Info;
  NevercInterfaceID Expected = semantic_product();
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !Continuation->InvokeNext || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = Continuation->InvokeNext(Continuation, Frame, OutResult);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Info, 0, sizeof(Info));
  Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_SEMA_API_MAJOR,
                                       NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetSemanticUnitInfo(SemaAPI->Context, Frame,
                                        OutResult->Output, &Info);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Info.Product.High != Expected.High || Info.Product.Low != Expected.Low ||
      Info.DiagnosticState != NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN ||
      Info.Replayed != NEVERC_FALSE || Info.VerifierSummary.Length == 0 ||
      Info.SourceIdentity.Length == 0 || Info.SourceDigest.Length != 32)
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(const NevercCoreAPI *Core,
                                              void **OutProcessState) {
  (void)Core;
  if (!OutProcessState)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *PluginProcessState) {
  NevercProviderDescriptor Provider;
  NevercInterceptorDescriptor Interceptor;
  NevercStatus Status;
  (void)Core;
  (void)PluginProcessState;
  if (!Registrar || !Registrar->RegisterProvider ||
      !Registrar->RegisterInterceptor)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_ANALYZE_HIGH,
                          NEVERC_PHASE_SEMA_ANALYZE_LOW};
  Provider.ProviderID = STRING_VIEW("neverc.test.sema-provider");
  Provider.Route.Header =
      (NevercABITableHeader){sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = provide_semantic_unit;
  Status = Registrar->RegisterProvider(RegistrarContext, &Provider);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = Provider.Phase;
  Interceptor.Callback = verify_semantic_unit;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                               void *PluginProcessState) {
  (void)Core;
  (void)PluginProcessState;
  return neverc_status_ok();
}

static NevercStatus query_interface(const NevercBootstrapAPI *Bootstrap,
                                    NevercInterfaceID Interface,
                                    uint16_t Major, uint16_t Minor,
                                    size_t RequiredSize,
                                    const void **OutTable) {
  uint16_t ActualMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, Major, Minor, OutTable, &ActualMinor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!*OutTable || StructSize < RequiredSize)
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;

  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_SEMA_HIGH, NEVERC_INTERFACE_SEMA_LOW},
      NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR,
      offsetof(NevercSemaAPI, GetSemanticUnitInfo) +
          sizeof(((NevercSemaAPI *)0)->GetSemanticUnitInfo),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SemaAPI = (const NevercSemaAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.sema-provider");
  Descriptor.DisplayName = STRING_VIEW("NeverC Sema provider test");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;

  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
