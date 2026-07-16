#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRING_VIEW(value)                                                     \
  (NevercStringView) { (value), (uint64_t)(sizeof(value) - 1) }

#ifndef NEVERC_TEST_ACTION_GRAPH_PLUGIN_ID
#define NEVERC_TEST_ACTION_GRAPH_PLUGIN_ID                                    \
  "org.neverc.test.action-graph-intercept"
#endif

static const NevercDriverAPI *DriverAPI;
static int ProcessState;

static void trace_event(const char *Event) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  fprintf(Trace, "%s:%s\n", NEVERC_TEST_ACTION_GRAPH_PLUGIN_ID, Event);
  fclose(Trace);
}

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void initialize_result(NevercPhaseResult *Result) {
  memset(Result, 0, sizeof(*Result));
  Result->Header = (NevercABITableHeader){
      sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_CONTINUE;
}

static NevercStatus initialize_node(NevercActionNode *Node) {
  if (Node == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(Node, 0, sizeof(*Node));
  Node->Header = (NevercABITableHeader){
      sizeof(*Node), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  return neverc_status_ok();
}

static NevercStatus find_node(NevercArtifactHandle Graph,
                              const NevercPhaseFrame *Frame,
                              NevercActionNodeID ID,
                              NevercActionNode *OutNode) {
  uint64_t Count = 0;
  uint64_t Index;
  NevercStatus Status = DriverAPI->GetActionNodeCount(
      DriverAPI->Context, Frame, Graph, &Count);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (Index = 0; Index != Count; ++Index) {
    NevercActionNode Node;
    initialize_node(&Node);
    Status = DriverAPI->GetActionNode(DriverAPI->Context, Frame, Graph, Index,
                                      &Node);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Node.Node == ID) {
      *OutNode = Node;
      return neverc_status_ok();
    }
  }
  return failure(NEVERC_STATUS_INVALID_ARGUMENT);
}

static NevercStatus add_node(NevercActionGraphBuilderHandle Builder,
                             NevercActionKind Kind,
                             NevercDriverType OutputType,
                             NevercDriverInputID DriverInput,
                             const NevercActionNodeID *Inputs,
                             uint64_t InputCount,
                             NevercActionNodeID *OutNode) {
  NevercActionNodeDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_DRIVER_API_MAJOR,
                             NEVERC_DRIVER_API_MINOR, 0};
  Descriptor.Kind = Kind;
  Descriptor.OutputType = OutputType;
  Descriptor.DriverInput = DriverInput;
  Descriptor.Inputs =
      (NevercActionNodeIDList){Inputs, InputCount, sizeof(NevercActionNodeID)};
  return DriverAPI->AddActionNode(DriverAPI->Context, Builder, &Descriptor,
                                  OutNode);
}

static NevercStatus NEVERC_CALL
observe_action_graph(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                     void *UserData) {
  uint32_t SeenKinds = 0;
  uint64_t Count = 0;
  uint64_t Index;
  NevercStatus Status;
  const uint32_t RequiredKinds =
      (UINT32_C(1) << NEVERC_ACTION_INPUT) |
      (UINT32_C(1) << NEVERC_ACTION_PREPROCESS) |
      (UINT32_C(1) << NEVERC_ACTION_COMPILE) |
      (UINT32_C(1) << NEVERC_ACTION_BACKEND) |
      (UINT32_C(1) << NEVERC_ACTION_ASSEMBLE) |
      (UINT32_C(1) << NEVERC_ACTION_LINK);
  (void)UserData;

  if (Frame == NULL || Point != NEVERC_OBSERVER_AFTER ||
      neverc_handle_is_null(Frame->CurrentOutput))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = DriverAPI->GetActionNodeCount(
      DriverAPI->Context, Frame, Frame->CurrentOutput, &Count);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (Index = 0; Index != Count; ++Index) {
    NevercActionNode Node;
    initialize_node(&Node);
    Status = DriverAPI->GetActionNode(DriverAPI->Context, Frame,
                                      Frame->CurrentOutput, Index, &Node);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Node.Kind <= NEVERC_ACTION_STATIC_LIB)
      SeenKinds |= UINT32_C(1) << Node.Kind;
  }
  if ((SeenKinds & RequiredKinds) != RequiredKinds)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  trace_event("observer:compile-link");
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL replace_action_graph(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  NevercDriverInput Input;
  NevercActionGraphBuilderHandle Builder = {0, 0};
  NevercActionNodeID InputNode = 0;
  NevercActionNodeID PreprocessNode = 0;
  NevercActionNodeID CompileNode = 0;
  NevercActionNodeID BackendNode = 0;
  NevercActionNodeID AssembleNode = 0;
  NevercArtifactHandle Graph = {0, 0};
  NevercStatus Status;
  uint64_t InputCount = 0;
  (void)UserData;

  if (Frame == NULL || OutResult == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  trace_event("provider:replacement");

  Status = DriverAPI->GetDriverInputCount(DriverAPI->Context, Frame,
                                          Frame->Input, &InputCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (InputCount != 1)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  Status = DriverAPI->GetDriverInput(DriverAPI->Context, Frame, Frame->Input,
                                     0, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Input.Type != NEVERC_DRIVER_TYPE_C)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

  Status = DriverAPI->CreateActionGraphBuilder(
      DriverAPI->Context, Frame, Frame->Input, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = add_node(Builder, NEVERC_ACTION_INPUT, Input.Type, Input.Input,
                    NULL, 0, &InputNode);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = add_node(Builder, NEVERC_ACTION_PREPROCESS,
                      NEVERC_DRIVER_TYPE_PP_C, 0, &InputNode, 1,
                      &PreprocessNode);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = add_node(Builder, NEVERC_ACTION_COMPILE,
                      NEVERC_DRIVER_TYPE_LLVM_BC, 0, &PreprocessNode, 1,
                      &CompileNode);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = add_node(Builder, NEVERC_ACTION_BACKEND,
                      NEVERC_DRIVER_TYPE_PP_ASM, 0, &CompileNode, 1,
                      &BackendNode);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = add_node(Builder, NEVERC_ACTION_ASSEMBLE,
                      NEVERC_DRIVER_TYPE_OBJECT, 0, &BackendNode, 1,
                      &AssembleNode);
  if (Status.Code == NEVERC_STATUS_OK) {
    NevercActionNodeIDList Roots = {
        &AssembleNode, 1, sizeof(NevercActionNodeID)};
    Status =
        DriverAPI->SetActionRoots(DriverAPI->Context, Builder, Roots);
  }
  if (Status.Code == NEVERC_STATUS_OK)
    Status = DriverAPI->PublishActionGraph(DriverAPI->Context, Frame, Builder,
                                           &Graph);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)DriverAPI->AbortActionGraphEdit(DriverAPI->Context, Builder);
    return Status;
  }
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Graph;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL intercept_action_graph(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercPhaseResult Downstream;
  NevercActionGraphMutationHandle Mutation = {0, 0};
  NevercActionNode Root;
  NevercActionNode ObjectNode;
  NevercActionNodeID RootID = 0;
  NevercActionNodeID WrapperID = 0;
  NevercActionNodeID ObjectID = 0;
  NevercActionNodeID SourceID = 0;
  NevercStatus Status;
  uint64_t NodeCount = 0;
  uint64_t Index;
  (void)UserData;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  initialize_result(&Downstream);
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Downstream.Action != NEVERC_PHASE_REPLACE)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

  Status = DriverAPI->GetActionRoot(DriverAPI->Context, Frame,
                                    Downstream.Output, 0, &RootID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  initialize_node(&Root);
  Status = find_node(Downstream.Output, Frame, RootID, &Root);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Root.Kind == NEVERC_ACTION_BIND_ARCH) {
    if (Root.InputCount != 1)
      return failure(NEVERC_STATUS_PLUGIN_FAILURE);
    WrapperID = RootID;
    Status = DriverAPI->GetActionNodeInput(
        DriverAPI->Context, Frame, Downstream.Output, WrapperID, 0, &RootID);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    initialize_node(&Root);
    Status = find_node(Downstream.Output, Frame, RootID, &Root);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#if !defined(NEVERC_TEST_ACTION_GRAPH_CYCLE) &&                             \
    !defined(NEVERC_TEST_ACTION_GRAPH_TYPE)
  if (Root.Kind != NEVERC_ACTION_LINK || Root.InputCount != 1)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  Status = DriverAPI->GetActionNodeInput(
      DriverAPI->Context, Frame, Downstream.Output, RootID, 0, &ObjectID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  initialize_node(&ObjectNode);
  Status = find_node(Downstream.Output, Frame, ObjectID, &ObjectNode);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (ObjectNode.Kind != NEVERC_ACTION_ASSEMBLE ||
      ObjectNode.OutputType != NEVERC_DRIVER_TYPE_OBJECT)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
#else
  if (Root.Kind != NEVERC_ACTION_ASSEMBLE ||
      Root.OutputType != NEVERC_DRIVER_TYPE_OBJECT)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  ObjectID = RootID;
#endif

  Status = DriverAPI->GetActionNodeCount(DriverAPI->Context, Frame,
                                         Downstream.Output, &NodeCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (Index = 0; Index != NodeCount; ++Index) {
    NevercActionNode Node;
    initialize_node(&Node);
    Status = DriverAPI->GetActionNode(DriverAPI->Context, Frame,
                                      Downstream.Output, Index, &Node);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Node.Kind == NEVERC_ACTION_INPUT)
      SourceID = Node.Node;
  }
  if (SourceID == 0)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

  Status = DriverAPI->BeginActionGraphMutation(
      DriverAPI->Context, Frame, Continuation, Downstream.Output, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

#if defined(NEVERC_TEST_ACTION_GRAPH_CYCLE)
  {
    NevercActionNodeIDList Inputs = {&RootID, 1,
                                     sizeof(NevercActionNodeID)};
    Status = DriverAPI->ReplaceActionNodeInputs(
        DriverAPI->Context, Mutation, RootID, Inputs);
  }
#elif defined(NEVERC_TEST_ACTION_GRAPH_TYPE)
  {
    Status = DriverAPI->SetActionNodeOutputType(
        DriverAPI->Context, Mutation, ObjectID, NEVERC_DRIVER_TYPE_IMAGE);
  }
#else
  {
    NevercActionNodeIDList Roots = {&ObjectID, 1,
                                    sizeof(NevercActionNodeID)};
    Status =
        DriverAPI->SetActionRoots(DriverAPI->Context, Mutation, Roots);
    if (Status.Code == NEVERC_STATUS_OK && WrapperID != 0)
      Status = DriverAPI->RemoveActionNode(DriverAPI->Context, Mutation,
                                           WrapperID);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = DriverAPI->RemoveActionNode(DriverAPI->Context, Mutation,
                                           RootID);
  }
#endif

#if defined(NEVERC_TEST_ACTION_GRAPH_CYCLE) ||                              \
    defined(NEVERC_TEST_ACTION_GRAPH_TYPE)
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)DriverAPI->AbortActionGraphEdit(DriverAPI->Context, Mutation);
    return Status;
  }
  Status =
      DriverAPI->CommitActionGraphMutation(DriverAPI->Context, Mutation);
  if (Status.Code == NEVERC_STATUS_OK)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  Status = DriverAPI->AbortActionGraphEdit(DriverAPI->Context, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  initialize_result(OutResult);
  return neverc_status_ok();
#else
  if (Status.Code == NEVERC_STATUS_OK)
    Status = DriverAPI->CommitActionGraphMutation(DriverAPI->Context,
                                                  Mutation);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)DriverAPI->AbortActionGraphEdit(DriverAPI->Context, Mutation);
    return Status;
  }
  initialize_result(OutResult);
  return neverc_status_ok();
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
  (void)Core;
  (void)PluginProcessState;
  if (Registrar == NULL)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
#if defined(NEVERC_TEST_ACTION_GRAPH_OBSERVE)
  {
    NevercObserverDescriptor Observer;
    memset(&Observer, 0, sizeof(Observer));
    Observer.Header =
        (NevercABITableHeader){sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                               NEVERC_PLUGIN_ABI_MINOR, 0};
    Observer.Phase =
        (NevercInterfaceID){NEVERC_PHASE_DRIVER_BUILD_ACTIONS_HIGH,
                            NEVERC_PHASE_DRIVER_BUILD_ACTIONS_LOW};
    Observer.Points = NEVERC_OBSERVER_AFTER;
    Observer.Callback = observe_action_graph;
    return Registrar->RegisterObserver(RegistrarContext, &Observer);
  }
#elif defined(NEVERC_TEST_ACTION_GRAPH_REPLACE)
  {
    NevercProviderDescriptor Provider;
    memset(&Provider, 0, sizeof(Provider));
    Provider.Header =
        (NevercABITableHeader){sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
                               NEVERC_PLUGIN_ABI_MINOR, 0};
    Provider.Phase =
        (NevercInterfaceID){NEVERC_PHASE_DRIVER_BUILD_ACTIONS_HIGH,
                            NEVERC_PHASE_DRIVER_BUILD_ACTIONS_LOW};
    Provider.ProviderID = STRING_VIEW(NEVERC_TEST_ACTION_GRAPH_PLUGIN_ID);
    Provider.Route.Header =
        (NevercABITableHeader){sizeof(Provider.Route),
                               NEVERC_PLUGIN_ABI_MAJOR,
                               NEVERC_PLUGIN_ABI_MINOR, 0};
    Provider.Deterministic = NEVERC_TRUE;
    Provider.Callback = replace_action_graph;
    return Registrar->RegisterProvider(RegistrarContext, &Provider);
  }
#else
  {
    NevercInterceptorDescriptor Interceptor;
    memset(&Interceptor, 0, sizeof(Interceptor));
    Interceptor.Header =
        (NevercABITableHeader){sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                               NEVERC_PLUGIN_ABI_MINOR, 0};
    Interceptor.Phase =
        (NevercInterfaceID){NEVERC_PHASE_DRIVER_BUILD_ACTIONS_HIGH,
                            NEVERC_PHASE_DRIVER_BUILD_ACTIONS_LOW};
    Interceptor.Callback = intercept_action_graph;
    return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
  }
#endif
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
      StructSize < offsetof(NevercDriverAPI, AbortActionGraphEdit) +
                       sizeof(((NevercDriverAPI *)0)->AbortActionGraphEdit))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  DriverAPI = (const NevercDriverAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW(NEVERC_TEST_ACTION_GRAPH_PLUGIN_ID);
  Descriptor.DisplayName = STRING_VIEW("NeverC action graph test plugin");
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
