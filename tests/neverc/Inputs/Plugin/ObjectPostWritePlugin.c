#include "neverc/Plugin/PluginObject.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                   \
  { (Value), (uint64_t)(sizeof(Value) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_post_write(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  const NevercObjectPhaseAPI *Phase =
      (const NevercObjectPhaseAPI *)UserData;
  NevercObjectImageInfo Image;
  NevercPhaseResult Downstream;
  NevercStatus Status;
  const uint8_t Marker = UINT8_C(0x42);
  uint64_t MarkerOffset;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL ||
      Phase == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Image, 0, sizeof(Image));
  Image.Header.StructSize = sizeof(Image);
  Image.Header.Major = NEVERC_OBJECT_PHASE_API_MAJOR;
  Image.Header.Minor = NEVERC_OBJECT_PHASE_API_MINOR;
  Status = Phase->GetImage(Phase->Context, Frame, Frame->Input, &Image);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Image.Binary == NULL || neverc_handle_is_null(Image.Builder) ||
      Image.OutputState != NEVERC_OUTPUT_OPEN || Image.Size < 5)
    return status_code(NEVERC_STATUS_INVALID_STATE);
  MarkerOffset = Image.Size > 9 ? 9 : 4;

  Status = Image.Binary->WriteAt(
      Image.Binary->Context, Frame->Task, Image.Builder, MarkerOffset,
      (NevercByteView){&Marker, 1});
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header.StructSize = sizeof(Downstream);
  Downstream.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Downstream.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status =
      Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercInterceptorDescriptor Interceptor;
  NevercStatus Status;
  (void)ProcessState;

  if (Core == NULL || Registrar == NULL ||
      Registrar->RegisterInterceptor == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_OBJECT_PHASE_HIGH,
                          NEVERC_INTERFACE_OBJECT_PHASE_LOW},
      NEVERC_OBJECT_PHASE_API_MAJOR, NEVERC_OBJECT_PHASE_API_MINOR,
      &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Table == NULL || StructSize < sizeof(NevercObjectPhaseAPI))
    return status_code(NEVERC_STATUS_ABI_MISMATCH);

  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header.StructSize = sizeof(Interceptor);
  Interceptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Interceptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Interceptor.Phase.High = NEVERC_PHASE_OBJECT_POST_WRITE_HIGH;
  Interceptor.Phase.Low = NEVERC_PHASE_OBJECT_POST_WRITE_LOW;
  Interceptor.Callback = intercept_post_write;
  Interceptor.UserData = (void *)Table;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;

  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.object-post-write");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Object Post-Write Test");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;

  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
