#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginLink.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
link_image(void *UserData, NevercTaskHandle Task,
           const NevercLinkRequest *Request,
           const NevercRawLinkInputSet *Inputs,
           NevercLinkerProductCandidate *OutCandidate) {
  (void)UserData;
  (void)Task;
  (void)Request;
  (void)Inputs;
  if (OutCandidate == NULL ||
      OutCandidate->Header.StructSize < sizeof(*OutCandidate))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  OutCandidate->Image.Owner = UINT64_C(0x9000);
  OutCandidate->Image.Value = UINT64_C(1);
  OutCandidate->ProductID.High = UINT64_C(0x9001);
  OutCandidate->ProductID.Low = UINT64_C(1);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
verify_image(void *UserData, NevercTaskHandle Task,
             const NevercLinkRequest *Request,
             NevercBinaryImageHandle Image) {
  (void)UserData;
  (void)Task;
  (void)Request;
  return neverc_handle_is_null(Image)
             ? failure(NEVERC_STATUS_INVALID_ARGUMENT)
             : neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core,
                const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  const NevercLinkRegistrarAPI *LinkRegistrar;
  NevercLinkerProviderDescriptor Descriptor;
  NevercStatus Status;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  (void)Registrar;
  (void)ProcessState;

  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_LINK_REGISTRAR_HIGH,
                          NEVERC_INTERFACE_LINK_REGISTRAR_LOW},
      NEVERC_LINK_REGISTRAR_API_MAJOR,
      NEVERC_LINK_REGISTRAR_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercLinkRegistrarAPI))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  LinkRegistrar = (const NevercLinkRegistrarAPI *)Table;

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_LINK_API_MAJOR;
  Descriptor.Header.Minor = NEVERC_LINK_API_MINOR;
  Descriptor.ProviderID = STRING_VIEW("org.neverc.test.link-route.provider");
  Descriptor.TargetID.High = UINT64_C(0x8100);
  Descriptor.TargetID.Low = UINT64_C(1);
  Descriptor.InputFormat.High = UINT64_C(0x8200);
  Descriptor.InputFormat.Low = UINT64_C(1);
  Descriptor.OutputFormat = Descriptor.InputFormat;
  Descriptor.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  Descriptor.Flags = NEVERC_LINK_PROVIDER_DETERMINISTIC;
  Descriptor.CompatibilityKey = STRING_VIEW("target-key-a");
  Descriptor.ProductID.High = UINT64_C(0x9001);
  Descriptor.ProductID.Low = UINT64_C(1);
  Descriptor.Link = link_image;
  Descriptor.VerifyImage = verify_image;
  return LinkRegistrar->RegisterLinkerProvider(
      LinkRegistrar->Context, RegistrarContext, &Descriptor);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t Bytes;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.link-route");
  Descriptor.DisplayName = STRING_VIEW("NeverC Link Route Test Plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Bytes = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Bytes);
  return neverc_status_ok();
}
