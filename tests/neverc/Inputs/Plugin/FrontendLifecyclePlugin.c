#include "neverc/Plugin/PluginAST.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercASTAPI *ASTAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static const char *event_name(NevercASTLifecycleEventKind Kind) {
  switch (Kind) {
  case NEVERC_AST_LIFECYCLE_TREE_INITIALIZE:
    return "tree_initialize";
  case NEVERC_AST_LIFECYCLE_SEMA_BEGIN:
    return "sema_begin";
  case NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL:
    return "top_level_decl";
  case NEVERC_AST_LIFECYCLE_INLINE_FUNCTION_DEFINITION:
    return "inline_function_definition";
  case NEVERC_AST_LIFECYCLE_INTERESTING_DECL:
    return "interesting_decl";
  case NEVERC_AST_LIFECYCLE_TAG_DEFINITION:
    return "tag_definition";
  case NEVERC_AST_LIFECYCLE_TAG_REQUIRED_DEFINITION:
    return "tag_required_definition";
  case NEVERC_AST_LIFECYCLE_TENTATIVE_DEFINITION:
    return "tentative_definition";
  case NEVERC_AST_LIFECYCLE_EXTERNAL_DECLARATION:
    return "external_declaration";
  case NEVERC_AST_LIFECYCLE_TRANSLATION_UNIT:
    return "translation_unit";
  case NEVERC_AST_LIFECYCLE_SEMA_END:
    return "sema_end";
  default:
    return "invalid";
  }
}

static void trace_event(NevercASTLifecycleEventKind Kind) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (!Path || !*Path)
    return;
  Trace = fopen(Path, "ab");
  if (!Trace)
    return;
  fprintf(Trace, "%s\n", event_name(Kind));
  fclose(Trace);
}

static int mode_is(const char *Expected) {
  const char *Mode = getenv("NEVERC_TEST_FRONTEND_LIFECYCLE_MODE");
  return Mode && strcmp(Mode, Expected) == 0;
}

static NevercStatus NEVERC_CALL
observe_lifecycle(NevercTaskHandle Task,
                  const NevercASTLifecycleEvent *Event, void *UserData) {
  NevercASTNodeInfo Info;
  NevercStatus Status;
  (void)UserData;
  if (!Event || Event->Header.StructSize < sizeof(*Event) ||
      Event->Header.Major != NEVERC_AST_API_MAJOR ||
      Event->Header.Minor > NEVERC_AST_API_MINOR || Event->Header.Flags != 0 ||
      Event->Kind == 0 || Event->Kind > NEVERC_AST_LIFECYCLE_EVENT_COUNT ||
      neverc_handle_is_null(Event->TranslationUnit))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);

  memset(&Info, 0, sizeof(Info));
  Info.Header = (NevercABITableHeader){
      sizeof(Info), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Status = ASTAPI->GetNodeInfo(ASTAPI->Context, Task,
                               Event->TranslationUnit, &Info);
  if (Status.Code != NEVERC_STATUS_OK ||
      Info.Domain != NEVERC_AST_SCHEMA_DOMAIN_DECL)
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;

  if ((Event->Kind == NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL ||
       Event->Kind == NEVERC_AST_LIFECYCLE_INTERESTING_DECL) &&
      (!Event->Declarations || Event->DeclarationCount == 0))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);
  if (Event->DeclarationCount != 0 &&
      neverc_handle_is_null(Event->Declaration))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);

  if (Event->Kind == NEVERC_AST_LIFECYCLE_TREE_INITIALIZE) {
    NevercASTMutationHandle Mutation = {0, 0};
    Status = ASTAPI->BeginASTMutation(ASTAPI->Context, Task, &Mutation);
    if (Status.Code != NEVERC_STATUS_INVALID_STATE ||
        !neverc_handle_is_null(Mutation))
      return failure(NEVERC_STATUS_VERIFICATION_FAILED);
  }

  trace_event(Event->Kind);
  if (Event->Kind == NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL &&
      mode_is("fail-top-level"))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);
  if (Event->Kind == NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL &&
      mode_is("cancel-top-level"))
    return failure(NEVERC_STATUS_CANCELLED);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
install_observer(const NevercPhaseFrame *Frame,
                 NevercPhaseContinuation *Continuation,
                 NevercPhaseResult *OutResult, void *UserData) {
  NevercASTLifecycleObserverDescriptor Descriptor;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !Continuation->InvokeNext || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Descriptor.Events = NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL;
  Descriptor.Callback = observe_lifecycle;
  Status = ASTAPI->RegisterLifecycleObserver(ASTAPI->Context, Frame->Task,
                                             &Descriptor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Continuation->InvokeNext(Continuation, Frame, OutResult);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
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
  NevercInterceptorDescriptor Interceptor;
  (void)Core;
  (void)PluginProcessState;
  if (!Registrar || !Registrar->RegisterInterceptor)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SOURCE_OPEN_HIGH,
                          NEVERC_PHASE_SOURCE_OPEN_LOW};
  Interceptor.Callback = install_observer;
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
      (NevercInterfaceID){NEVERC_INTERFACE_AST_HIGH, NEVERC_INTERFACE_AST_LOW},
      NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR,
      offsetof(NevercASTAPI, RegisterLifecycleObserver) +
          sizeof(((NevercASTAPI *)0)->RegisterLifecycleObserver),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ASTAPI = (const NevercASTAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.frontend-lifecycle");
  Descriptor.DisplayName = STRING_VIEW("NeverC frontend lifecycle test");
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
