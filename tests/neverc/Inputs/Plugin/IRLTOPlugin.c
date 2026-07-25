#include "neverc/Plugin/PluginIR.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

typedef struct ProcessState {
  NevercTaskHandle LTOTask;
  uint32_t Seen;
  uint32_t PreOptCalls;
  NevercBool LTOEnded;
} ProcessState;

typedef struct PassState {
  ProcessState *Process;
  uint32_t Bit;
} PassState;

static const NevercIRPassAPI *PassAPI;
static ProcessState Process;
static PassState Passes[5];

static NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

static int same_handle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

static NevercStatus NEVERC_CALL
run_pass(const NevercIRPassInvocation *Invocation,
         NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  PassState *State = (PassState *)UserData;
  if (!Invocation || !OutPreserved || !State || !State->Process ||
      Invocation->Level != NEVERC_IR_PASS_LEVEL_MODULE || !Invocation->Core ||
      neverc_handle_is_null(Invocation->Module))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  if (same_handle(Invocation->Task, State->Process->LTOTask)) {
    State->Process->Seen |= State->Bit;
    if (State->Bit == UINT32_C(1))
      ++State->Process->PreOptCalls;
#ifdef NEVERC_TEST_LTO_PLUGIN_ERROR
    if (State->Bit == (UINT32_C(1) << 3))
      return status(NEVERC_STATUS_PLUGIN_FAILURE);
#endif
  }

  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(const NevercCoreAPI *Core,
                                              void **OutProcessState) {
  (void)Core;
  if (!OutProcessState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Process, 0, sizeof(Process));
  *OutProcessState = &Process;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL task_begin(
    const NevercCoreAPI *Core, NevercTaskHandle Task, NevercTaskKind Kind,
    void *ProcessStateValue, void *SessionState, void **OutTaskState) {
  (void)Core;
  (void)SessionState;
  ProcessState *State = (ProcessState *)ProcessStateValue;
  if (!State || !OutTaskState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Kind == NEVERC_TASK_LTO) {
    State->LTOTask = Task;
    State->Seen = 0;
    State->PreOptCalls = 0;
    State->LTOEnded = NEVERC_FALSE;
  }
  *OutTaskState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL task_end(
    const NevercCoreAPI *Core, NevercTaskHandle Task, NevercTaskKind Kind,
    void *ProcessStateValue, void *SessionState, void *TaskState) {
  (void)Core;
  (void)ProcessStateValue;
  (void)SessionState;
  ProcessState *State = (ProcessState *)TaskState;
  if (!State)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Kind == NEVERC_TASK_LTO) {
    if (!same_handle(Task, State->LTOTask) ||
        State->Seen != UINT32_C(0x1f))
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
#ifdef NEVERC_TEST_LTO_REQUIRE_PARTITIONS
    if (State->PreOptCalls < 3)
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
#endif
    State->LTOEnded = NEVERC_TRUE;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *ProcessStateValue) {
  (void)Core;
  (void)Registrar;
  static const NevercInterfaceID Phases[5] = {
      {NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
       NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
       NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW},
      {NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
       NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
      {NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
       NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
       NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
  };
  static const char *Names[5] = {
      "neverc.test.lto.pre_opt", "neverc.test.lto.pipeline_start",
      "neverc.test.lto.optimizer_last", "neverc.test.lto.post_opt",
      "neverc.test.lto.pre_codegen",
  };
  ProcessState *State = (ProcessState *)ProcessStateValue;
  if (!PassAPI || !State)
    return status(NEVERC_STATUS_INVALID_STATE);
  for (uint32_t Index = 0; Index != 5; ++Index) {
    Passes[Index].Process = State;
    Passes[Index].Bit = UINT32_C(1) << Index;
    NevercIRPassDescriptor Descriptor;
    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Header =
        (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_PASS_API_MAJOR,
                              NEVERC_IR_PASS_API_MINOR, 0};
    Descriptor.PassID.Data = Names[Index];
    Descriptor.PassID.Length = (uint64_t)strlen(Names[Index]);
    Descriptor.Phase = Phases[Index];
    Descriptor.Level = NEVERC_IR_PASS_LEVEL_MODULE;
    Descriptor.Deterministic = NEVERC_TRUE;
    Descriptor.Run = run_pass;
    Descriptor.UserData = &Passes[Index];
    NevercStatus Result = PassAPI->RegisterPass(
        PassAPI->Context, RegistrarContext, &Descriptor);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                               void *ProcessStateValue) {
  (void)Core;
  ProcessState *State = (ProcessState *)ProcessStateValue;
#ifdef NEVERC_TEST_LTO_PLUGIN_ERROR
  (void)State;
  return neverc_status_ok();
#else
  if (!State || State->LTOEnded != NEVERC_TRUE)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  return neverc_status_ok();
#endif
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercStatus Result = Bootstrap->QueryInterface(
      Bootstrap->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                          NEVERC_INTERFACE_IR_PASS_LOW},
      NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  if (!Table || StructSize < sizeof(NevercIRPassAPI))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercIRPassAPI *)Table;

  uint32_t Capacity = OutPlugin->Header.StructSize;
  NevercPluginDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.lto-ir-pass");
  Descriptor.DisplayName = STRING_VIEW("NeverC LTO IR pass test");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
  Descriptor.Destroy = destroy_plugin;
  size_t Writable =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
