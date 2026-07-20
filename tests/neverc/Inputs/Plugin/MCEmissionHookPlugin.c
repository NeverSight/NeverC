#include "neverc/Plugin/PluginMC.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

enum {
  TEST_MODE_MUTATE = 0,
  TEST_MODE_FAIL = 1,
  TEST_MODE_INVOKE_NEXT_TWICE = 2,
};

static const NevercMCEmissionAPI *EmissionAPI;
static uint64_t EventCounts[NEVERC_MC_EMISSION_PRE_OBJECT_WRITE + 1];
static uint64_t LayoutRecordsRead;
static int64_t LastImmediate;
static int32_t DuplicateInvokeStatus;
static uint32_t TestMode;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int is_null_handle(NevercHandle Handle) {
  return Handle.Owner == 0 && Handle.Value == 0;
}

static void initialize_result(NevercPhaseResult *Result) {
  memset(Result, 0, sizeof(*Result));
  Result->Header = (NevercABITableHeader){
      sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_CONTINUE;
}

static NevercArtifactHandle event_artifact(const NevercPhaseFrame *Frame) {
  return is_null_handle(Frame->CurrentOutput) ? Frame->Input
                                               : Frame->CurrentOutput;
}

static NevercStatus read_layout(const NevercPhaseFrame *Frame,
                                NevercArtifactHandle Artifact,
                                const NevercMCEmissionEventInfo *Event) {
  uint64_t Index;
  for (Index = 0; Index != Event->LayoutSectionCount; ++Index) {
    NevercMCEmissionSectionLayoutInfo Info;
    memset(&Info, 0, sizeof(Info));
    Info.Header = (NevercABITableHeader){
        sizeof(Info), NEVERC_MC_EMISSION_API_MAJOR,
        NEVERC_MC_EMISSION_API_MINOR, 0};
    {
      NevercStatus Status = EmissionAPI->GetLayoutSection(
          EmissionAPI->Context, Frame, Artifact, Index, &Info);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    ++LayoutRecordsRead;
  }
  for (Index = 0; Index != Event->LayoutFragmentCount; ++Index) {
    NevercMCEmissionFragmentLayoutInfo Info;
    memset(&Info, 0, sizeof(Info));
    Info.Header = (NevercABITableHeader){
        sizeof(Info), NEVERC_MC_EMISSION_API_MAJOR,
        NEVERC_MC_EMISSION_API_MINOR, 0};
    {
      NevercStatus Status = EmissionAPI->GetLayoutFragment(
          EmissionAPI->Context, Frame, Artifact, Index, &Info);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    ++LayoutRecordsRead;
  }
  for (Index = 0; Index != Event->LayoutSymbolCount; ++Index) {
    NevercMCEmissionSymbolLayoutInfo Info;
    memset(&Info, 0, sizeof(Info));
    Info.Header = (NevercABITableHeader){
        sizeof(Info), NEVERC_MC_EMISSION_API_MAJOR,
        NEVERC_MC_EMISSION_API_MINOR, 0};
    {
      NevercStatus Status = EmissionAPI->GetLayoutSymbol(
          EmissionAPI->Context, Frame, Artifact, Index, &Info);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    ++LayoutRecordsRead;
  }
  for (Index = 0; Index != Event->LayoutFixupCount; ++Index) {
    NevercMCEmissionFixupLayoutInfo Info;
    memset(&Info, 0, sizeof(Info));
    Info.Header = (NevercABITableHeader){
        sizeof(Info), NEVERC_MC_EMISSION_API_MAJOR,
        NEVERC_MC_EMISSION_API_MINOR, 0};
    {
      NevercStatus Status = EmissionAPI->GetLayoutFixup(
          EmissionAPI->Context, Frame, Artifact, Index, &Info);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    ++LayoutRecordsRead;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL observe_emission(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData) {
  NevercArtifactHandle Artifact;
  NevercMCEmissionEventInfo Event;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || Point != NEVERC_OBSERVER_AFTER)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Artifact = event_artifact(Frame);
  memset(&Event, 0, sizeof(Event));
  Event.Header = (NevercABITableHeader){
      sizeof(Event), NEVERC_MC_EMISSION_API_MAJOR,
      NEVERC_MC_EMISSION_API_MINOR, 0};
  Status =
      EmissionAPI->GetEvent(EmissionAPI->Context, Frame, Artifact, &Event);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Event.Kind == 0 ||
      Event.Kind > NEVERC_MC_EMISSION_PRE_OBJECT_WRITE)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  ++EventCounts[Event.Kind];

  if (Event.Kind == NEVERC_MC_EMISSION_POST_LAYOUT) {
    if ((Event.Flags & NEVERC_MC_EMISSION_HAS_LAYOUT) == 0)
      return failure(NEVERC_STATUS_PLUGIN_FAILURE);
    return read_layout(Frame, Artifact, &Event);
  }
  if ((Event.Kind == NEVERC_MC_EMISSION_PRE_INSTRUCTION ||
       Event.Kind == NEVERC_MC_EMISSION_POST_INSTRUCTION) &&
      ((Event.Flags & NEVERC_MC_EMISSION_HAS_INSTRUCTION) == 0 ||
       Event.MC == NULL))
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  if (Event.Kind == NEVERC_MC_EMISSION_POST_ENCODE &&
      ((Event.Flags & NEVERC_MC_EMISSION_HAS_ENCODING) == 0 ||
       Event.EncodedBytes.Length == 0))
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  return neverc_status_ok();
}

static NevercStatus invoke_next_once(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult) {
  initialize_result(OutResult);
  return Continuation->InvokeNext(Continuation, Frame, OutResult);
}

static NevercStatus NEVERC_CALL intercept_instruction(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercMCEmissionEventInfo Event;
  NevercMCOperandHandle Operand = {0, 0};
  NevercMCOperandValue Value;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  if (TestMode == TEST_MODE_FAIL)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);
  if (TestMode == TEST_MODE_INVOKE_NEXT_TWICE) {
    NevercPhaseResult Duplicate;
    Status = invoke_next_once(Frame, Continuation, OutResult);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    initialize_result(&Duplicate);
    Status = Continuation->InvokeNext(Continuation, Frame, &Duplicate);
    DuplicateInvokeStatus = Status.Code;
    return neverc_status_ok();
  }

  memset(&Event, 0, sizeof(Event));
  Event.Header = (NevercABITableHeader){
      sizeof(Event), NEVERC_MC_EMISSION_API_MAJOR,
      NEVERC_MC_EMISSION_API_MINOR, 0};
  Status = EmissionAPI->GetEvent(EmissionAPI->Context, Frame, Frame->Input,
                                 &Event);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Event.Flags & NEVERC_MC_EMISSION_CAN_REPLACE_INSTRUCTION) == 0 ||
      Event.MC == NULL)
    return failure(NEVERC_STATUS_PLUGIN_FAILURE);

  Status = Event.MC->GetInstructionOperand(
      Event.MC->Context, Frame->Task, Event.Instruction, 0, &Operand);
  if (Status.Code != NEVERC_STATUS_OK)
    return invoke_next_once(Frame, Continuation, OutResult);
  memset(&Value, 0, sizeof(Value));
  Value.Header = (NevercABITableHeader){
      sizeof(Value), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  Status = Event.MC->GetOperandValue(Event.MC->Context, Frame->Task, Operand,
                                     &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value.Kind != NEVERC_MC_OPERAND_IMMEDIATE)
    return invoke_next_once(Frame, Continuation, OutResult);

  {
    const NevercMCAPI *MutableMC = NULL;
    NevercMCUnitHandle Unit = {0, 0};
    NevercMCInstHandle Instruction = {0, 0};
    NevercMCMutationHandle Mutation = {0, 0};
    Status = EmissionAPI->BeginInstructionReplacement(
        EmissionAPI->Context, Frame, Continuation, &MutableMC, &Unit,
        &Instruction);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = MutableMC->BeginMutation(MutableMC->Context, Frame->Task, Unit,
                                      &Mutation);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = MutableMC->EraseOperand(MutableMC->Context, Frame->Task,
                                       Mutation, Instruction, 0);
    ++Value.Payload.Immediate;
    LastImmediate = Value.Payload.Immediate;
    if (Status.Code == NEVERC_STATUS_OK)
      Status = MutableMC->AppendOperand(MutableMC->Context, Frame->Task,
                                        Mutation, Instruction, &Value);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = MutableMC->CommitMutation(MutableMC->Context, Frame->Task,
                                         Mutation);
    else if (!is_null_handle(Mutation))
      (void)MutableMC->AbandonMutation(MutableMC->Context, Frame->Task,
                                       Mutation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }

  initialize_result(OutResult);
  Status = EmissionAPI->PublishInstructionReplacement(
      EmissionAPI->Context, Frame, Continuation, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus register_observer(const NevercRegistrarAPI *Registrar,
                                      void *RegistrarContext,
                                      NevercInterfaceID Phase) {
  NevercObserverDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = Phase;
  Descriptor.Points = NEVERC_OBSERVER_AFTER;
  Descriptor.Callback = observe_emission;
  return Registrar->RegisterObserver(RegistrarContext, &Descriptor);
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
  NevercStatus Status;
  (void)Core;
  (void)PluginProcessState;

#define REGISTER_OBSERVER(Symbol)                                              \
  do {                                                                         \
    Status = register_observer(                                                 \
        Registrar, RegistrarContext,                                            \
        (NevercInterfaceID){NEVERC_PHASE_MC_EMISSION_##Symbol##_HIGH,          \
                            NEVERC_PHASE_MC_EMISSION_##Symbol##_LOW});          \
    if (Status.Code != NEVERC_STATUS_OK)                                        \
      return Status;                                                            \
  } while (0)
  REGISTER_OBSERVER(UNIT_BEGIN);
  REGISTER_OBSERVER(UNIT_END);
  REGISTER_OBSERVER(SECTION_CHANGE);
  REGISTER_OBSERVER(PRE_INSTRUCTION);
  REGISTER_OBSERVER(POST_INSTRUCTION);
  REGISTER_OBSERVER(POST_ENCODE);
  REGISTER_OBSERVER(FIXUP);
  REGISTER_OBSERVER(RELAXATION_ROUND);
  REGISTER_OBSERVER(PRE_LAYOUT);
  REGISTER_OBSERVER(POST_LAYOUT);
#undef REGISTER_OBSERVER

  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_HIGH,
                          NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_LOW};
  Interceptor.Callback = intercept_instruction;
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
                                    const void **OutTable,
                                    uint64_t RequiredSize) {
  uint16_t NegotiatedMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, Major, Minor, OutTable,
      &NegotiatedMinor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!*OutTable || StructSize < RequiredSize)
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

NEVERC_EXPORT void NEVERC_CALL
neverc_test_mc_emission_reset(uint32_t Mode) {
  memset(EventCounts, 0, sizeof(EventCounts));
  LayoutRecordsRead = 0;
  LastImmediate = 0;
  DuplicateInvokeStatus = NEVERC_STATUS_OK;
  TestMode = Mode;
}

NEVERC_EXPORT uint64_t NEVERC_CALL
neverc_test_mc_emission_event_count(uint32_t Kind) {
  return Kind <= NEVERC_MC_EMISSION_PRE_OBJECT_WRITE ? EventCounts[Kind] : 0;
}

NEVERC_EXPORT uint64_t NEVERC_CALL
neverc_test_mc_emission_layout_records_read(void) {
  return LayoutRecordsRead;
}

NEVERC_EXPORT int64_t NEVERC_CALL
neverc_test_mc_emission_last_immediate(void) {
  return LastImmediate;
}

NEVERC_EXPORT int32_t NEVERC_CALL
neverc_test_mc_emission_duplicate_invoke_status(void) {
  return DuplicateInvokeStatus;
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  NevercStatus Status;
  uint32_t Capacity;
  size_t BytesToWrite;
  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_MC_EMISSION_HIGH,
                          NEVERC_INTERFACE_MC_EMISSION_LOW},
      NEVERC_MC_EMISSION_API_MAJOR, NEVERC_MC_EMISSION_API_MINOR, &Table,
      sizeof(NevercMCEmissionAPI));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  EmissionAPI = (const NevercMCEmissionAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.mc-emission");
  Descriptor.DisplayName = STRING_VIEW("NeverC MC emission hook test plugin");
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
