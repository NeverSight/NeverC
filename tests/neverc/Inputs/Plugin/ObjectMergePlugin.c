#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginLink.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
merge_objects(void *UserData, NevercTaskHandle Task,
              const NevercObjectMergeRequest *Request,
              NevercObjectMergeCandidate *OutCandidate) {
  const NevercObjectMergeInput *Inputs;
  NevercObjectSectionDescriptor Section;
  NevercObjectSectionHandle Created;
  NevercObjectGraphInfo Info;
  NevercStatus Status;
  uint8_t SectionCount = 0;
  uint64_t I;
  static const char Name[] = ".plugin-merged";

  if (Request == NULL || OutCandidate == NULL ||
      Request->Header.StructSize < sizeof(*Request) ||
      OutCandidate->Header.StructSize < sizeof(*OutCandidate) ||
      Request->Objects.ElementStride != sizeof(NevercObjectMergeInput) ||
      Request->Objects.Data == NULL || Request->OutputObject == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Inputs = (const NevercObjectMergeInput *)Request->Objects.Data;
  for (I = 0; I != Request->Objects.Count; ++I) {
    memset(&Info, 0, sizeof(Info));
    Info.Header.StructSize = sizeof(Info);
    Info.Header.Major = NEVERC_OBJECT_API_MAJOR;
    Info.Header.Minor = NEVERC_OBJECT_API_MINOR;
    Status = Inputs[I].Object->GetGraphInfo(
        Inputs[I].Object->Context, Task, Inputs[I].Graph, &Info);
    if (!neverc_status_is_ok(Status))
      return Status;
    SectionCount = (uint8_t)(SectionCount + Info.SectionCount);
  }

  memset(&Section, 0, sizeof(Section));
  Section.Header.StructSize = sizeof(Section);
  Section.Header.Major = NEVERC_OBJECT_API_MAJOR;
  Section.Header.Minor = NEVERC_OBJECT_API_MINOR;
  Section.Name = STRING_VIEW(".plugin-merged");
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Section.Alignment = 1;
  Section.Data.Data = &SectionCount;
  Section.Data.Length = 1;
  Status = Request->OutputObject->CreateSection(
      Request->OutputObject->Context, Task, Request->OutputMutation,
      &Section, &Created);
  if (!neverc_status_is_ok(Status))
    return Status;

  OutCandidate->Object = Request->OutputGraph;
  OutCandidate->ProductID.High = UINT64_C(0x4e43504d45524750);
  OutCandidate->ProductID.Low = (uint64_t)(uintptr_t)UserData;
  OutCandidate->ProducerRouteDigest[0] = 0x63;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core,
                const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  const NevercLinkRegistrarAPI *LinkRegistrar;
  NevercObjectMergeProviderDescriptor Descriptor;
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
  if (!neverc_status_is_ok(Status) || Table == NULL ||
      StructSize < sizeof(NevercLinkRegistrarAPI))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  LinkRegistrar = (const NevercLinkRegistrarAPI *)Table;

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_LINK_API_MAJOR;
  Descriptor.Header.Minor = NEVERC_LINK_API_MINOR;
  Descriptor.ProviderID =
      STRING_VIEW("org.neverc.test.object-merge.provider");
  Descriptor.TargetID.High = UINT64_C(0x4e43504d45524745);
  Descriptor.TargetID.Low = UINT64_C(1);
  Descriptor.FormatID.High = UINT64_C(0x4e43504d45524746);
  Descriptor.FormatID.Low = UINT64_C(1);
  Descriptor.Flags =
      NEVERC_LINK_PROVIDER_DETERMINISTIC | NEVERC_LINK_PROVIDER_CACHEABLE;
  Descriptor.ProductID.High = UINT64_C(0x4e43504d45524750);
  Descriptor.ProductID.Low = UINT64_C(1);
  Descriptor.Merge = merge_objects;
  Descriptor.UserData = (void *)(uintptr_t)UINT64_C(1);
  Status = LinkRegistrar->RegisterObjectMergeProvider(
      LinkRegistrar->Context, RegistrarContext, &Descriptor);
  if (!neverc_status_is_ok(Status))
    return Status;

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_LINK_API_MAJOR;
  Descriptor.Header.Minor = NEVERC_LINK_API_MINOR;
  Descriptor.ProviderID =
      STRING_VIEW("org.neverc.test.object-merge.android-aarch64");
  Descriptor.TargetID.High = UINT64_C(0x4e43544255494c54);
  Descriptor.TargetID.Low = UINT64_C(6);
  Descriptor.FormatID.High = UINT64_C(0x4e434f424a464d54);
  Descriptor.FormatID.Low = UINT64_C(1);
  Descriptor.Flags =
      NEVERC_LINK_PROVIDER_DETERMINISTIC | NEVERC_LINK_PROVIDER_CACHEABLE;
  Descriptor.ProductID.High = UINT64_C(0x4e43504d45524750);
  Descriptor.ProductID.Low = UINT64_C(2);
  Descriptor.Merge = merge_objects;
  Descriptor.UserData = (void *)(uintptr_t)UINT64_C(2);
  return LinkRegistrar->RegisterObjectMergeProvider(
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
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.object-merge");
  Descriptor.DisplayName = STRING_VIEW("NeverC Object Merge Test Plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Bytes = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Bytes);
  return neverc_status_ok();
}
