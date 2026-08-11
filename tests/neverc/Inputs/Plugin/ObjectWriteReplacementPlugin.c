#include "neverc/Plugin/PluginObject.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value) {(Value), (uint64_t)(sizeof(Value) - 1)}

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

#ifdef NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR
static NevercStatus NEVERC_CALL continue_write_phase(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercPhaseResult Downstream;
  NevercStatus Status;
  (void)UserData;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header.StructSize = sizeof(Downstream);
  Downstream.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Downstream.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
#else
static NevercStatus
    NEVERC_CALL reject_if_write_provider_runs(const NevercPhaseFrame *Frame,
                                              NevercPhaseResult *OutResult,
                                              void *UserData) {
  (void)Frame;
  (void)OutResult;
  (void)UserData;
  return status_code(NEVERC_STATUS_PLUGIN_FAILURE);
}
#endif

static NevercStatus
    NEVERC_CALL register_plugin(const NevercCoreAPI *Core,
                                const NevercRegistrarAPI *Registrar,
                                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
#ifdef NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR
  {
    NevercInterceptorDescriptor Interceptor;
    if (Registrar->RegisterInterceptor == NULL)
      return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
    memset(&Interceptor, 0, sizeof(Interceptor));
    Interceptor.Header.StructSize = sizeof(Interceptor);
    Interceptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Interceptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Interceptor.Phase.High = NEVERC_PHASE_OBJECT_WRITE_HIGH;
    Interceptor.Phase.Low = NEVERC_PHASE_OBJECT_WRITE_LOW;
    Interceptor.Callback = continue_write_phase;
    return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
  }
#else
  {
    NevercProviderDescriptor Provider;
    if (Registrar->RegisterProvider == NULL)
      return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
    memset(&Provider, 0, sizeof(Provider));
    Provider.Header.StructSize = sizeof(Provider);
    Provider.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Provider.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Provider.Phase.High = NEVERC_PHASE_OBJECT_WRITE_HIGH;
    Provider.Phase.Low = NEVERC_PHASE_OBJECT_WRITE_LOW;
    Provider.ProviderID =
        (NevercStringView)STRING_VIEW("test.object.write.provider");
    Provider.Route.Header.StructSize = sizeof(Provider.Route);
    Provider.Route.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Provider.Route.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
#ifdef NEVERC_TEST_OBJECT_WRITE_MISMATCHED_ROUTE
    Provider.Route.TargetTriple =
        (NevercStringView)STRING_VIEW("x86_64-neverc-route-mismatch");
    Provider.Route.CPU =
        (NevercStringView)STRING_VIEW("neverc-route-mismatch-cpu");
    Provider.Route.Features =
        (NevercStringView)STRING_VIEW("+neverc-route-mismatch");
    Provider.Route.ObjectFormat =
        (NevercStringView)STRING_VIEW("neverc-route-mismatch-format");
#elif defined(NEVERC_TEST_OBJECT_WRITE_ELF_ROUTE)
    Provider.Route.ObjectFormat = (NevercStringView)STRING_VIEW("elf");
#endif
    Provider.Deterministic = NEVERC_TRUE;
    Provider.Callback = reject_if_write_provider_runs;
    return Registrar->RegisterProvider(RegistrarContext, &Provider);
  }
#endif
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t Bytes;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
#ifdef NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.object-write-interceptor");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Object Write Interceptor Test");
#else
#ifdef NEVERC_TEST_OBJECT_WRITE_MISMATCHED_ROUTE
  Descriptor.PluginID = (NevercStringView)STRING_VIEW(
      "org.neverc.test.object-write-provider-mismatched-route");
  Descriptor.DisplayName = (NevercStringView)STRING_VIEW(
      "NeverC Object Write Mismatched Route Provider Test");
#elif defined(NEVERC_TEST_OBJECT_WRITE_ELF_ROUTE)
  Descriptor.PluginID = (NevercStringView)STRING_VIEW(
      "org.neverc.test.object-write-provider-elf-route");
  Descriptor.DisplayName = (NevercStringView)STRING_VIEW(
      "NeverC Object Write ELF Route Provider Test");
#else
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.object-write-provider");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Object Write Provider Test");
#endif
#endif
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Bytes = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Bytes);
  return neverc_status_ok();
}
