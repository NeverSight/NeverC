/* Add a deterministic section through the transactional ObjectGraph API. */
#include "neverc/Plugin/PluginObject.h"
#include <stddef.h>
#include <string.h>

#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus fail(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
rewrite_object(const NevercPhaseFrame *Frame,
               NevercPhaseContinuation *Continuation,
               NevercPhaseResult *OutResult, void *UserData) {
  const NevercObjectPhaseAPI *Phase =
      (const NevercObjectPhaseAPI *)UserData;
  static const uint8_t Marker[] = "NeverC object rewrite example";
  NevercObjectPhaseGraphInfo Graph;
  NevercObjectSectionDescriptor Section;
  NevercObjectSectionHandle Created = {0};
  NevercObjectMutationHandle Mutation = {0};
  NevercPhaseResult Downstream;
  NevercStatus Status;

  if (!Frame || !Continuation || !OutResult || !Phase)
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Graph, 0, sizeof(Graph));
  Graph.Header =
      (NevercABITableHeader){sizeof(Graph), NEVERC_OBJECT_PHASE_API_MAJOR,
                            NEVERC_OBJECT_PHASE_API_MINOR, 0};
  Status = Phase->GetGraph(Phase->Context, Frame, Frame->Input, &Graph);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Status = Graph.Object->BeginMutation(Graph.Object->Context, Frame->Task,
                                       Graph.Graph, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Section, 0, sizeof(Section));
  Section.Header =
      (NevercABITableHeader){sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                            NEVERC_OBJECT_API_MINOR, 0};
  Section.Name = SV(".neverc_ex");
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  Section.Alignment = 1;
  Section.Data =
      (NevercByteView){Marker, (uint64_t)(sizeof(Marker) - 1)};
  Status = Graph.Object->CreateSection(
      Graph.Object->Context, Frame->Task, Mutation, &Section, &Created);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)Graph.Object->AbandonMutation(
        Graph.Object->Context, Frame->Task, Mutation);
    return Status;
  }
  Status = Graph.Object->CommitMutation(
      Graph.Object->Context, Frame->Task, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header =
      (NevercABITableHeader){sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header =
      (NevercABITableHeader){sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercInterceptorDescriptor Interceptor;
  NevercStatus Status;
  (void)ProcessState;
  if (!Core || !Registrar || !Registrar->RegisterInterceptor)
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_OBJECT_PHASE_HIGH,
                          NEVERC_INTERFACE_OBJECT_PHASE_LOW},
      NEVERC_OBJECT_PHASE_API_MAJOR, NEVERC_OBJECT_PHASE_API_MINOR, &Table,
      &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercObjectPhaseAPI))
    return fail(NEVERC_STATUS_ABI_MISMATCH);

  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header =
      (NevercABITableHeader){sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                          NEVERC_PHASE_OBJECT_PRE_WRITE_LOW};
  Interceptor.Callback = rewrite_object;
  Interceptor.UserData = (void *)Table;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Plugin;
  uint32_t Capacity;
  size_t Writable;
  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Plugin, 0, sizeof(Plugin));
  Plugin.Header =
      (NevercABITableHeader){sizeof(Plugin), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Plugin.PluginID = SV("org.neverc.example.object-rewrite");
  Plugin.DisplayName = SV("NeverC ObjectGraph rewrite example");
  Plugin.Version.Major = 1;
  Plugin.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Plugin.Reentrancy = NEVERC_REENTRANCY_NONE;
  Plugin.Register = register_plugin;
  Writable = Capacity < sizeof(Plugin) ? Capacity : sizeof(Plugin);
  memcpy(OutPlugin, &Plugin, Writable);
  OutPlugin->Header.StructSize = sizeof(Plugin);
  return neverc_status_ok();
}
