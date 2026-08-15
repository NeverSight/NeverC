#include "neverc/Plugin/PluginIR.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

typedef struct ProcessState {
  NevercTaskHandle LTOTask;
  uint32_t Seen;
  uint32_t Calls[5];
  NevercBool LTOEnded;
} ProcessState;

typedef struct PassState {
  ProcessState *Process;
  uint32_t Bit;
  NevercIRPassLevel Level;
  uint64_t ModuleFingerprint;
  uint32_t InvocationCount;
  NevercBool HasModuleFingerprint;
  NevercBool ModuleMismatch;
} PassState;

static const NevercIRPassAPI *PassAPI;
static ProcessState Process;
static PassState Passes[8];

static NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

static int same_handle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

static NevercStatus collect_values(const NevercIRPassInvocation *Invocation,
                                   NevercHandle Container,
                                   NevercIRValueCollection Collection,
                                   NevercIRValueHandle *Values,
                                   uint64_t Capacity, uint64_t *OutCount) {
  NevercIRValueCursor Cursor;
  NevercStatus Result;
  memset(&Cursor, 0, sizeof(Cursor));
  Result = Invocation->Core->BeginValueCursor(Invocation->Core->Context,
                                              Invocation->Task, Container,
                                              Collection, &Cursor);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  return Invocation->Core->CollectValueCursor(Invocation->Core->Context,
                                              Invocation->Task, &Cursor, Values,
                                              Capacity, OutCount);
}

static NevercStatus
module_definition_fingerprint(const NevercIRPassInvocation *Invocation,
                              uint64_t *OutFingerprint) {
  NevercIRValueHandle Functions[128];
  uint64_t FunctionCount = 0;
  uint64_t Hash = UINT64_C(1469598103934665603);
  NevercStatus Result = collect_values(Invocation, Invocation->Module,
                                       NEVERC_IR_COLLECTION_MODULE_FUNCTIONS,
                                       Functions, 128, &FunctionCount);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  for (uint64_t Index = 0; Index != FunctionCount; ++Index) {
    NevercIRValueHandle Blocks[256];
    NevercStringView Name = {0, 0};
    uint64_t BlockCount = 0;
    Result = collect_values(Invocation, Functions[Index],
                            NEVERC_IR_COLLECTION_FUNCTION_BLOCKS, Blocks, 256,
                            &BlockCount);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    if (BlockCount == 0)
      continue;
    Result = Invocation->Core->GetValueName(
        Invocation->Core->Context, Invocation->Task, Functions[Index], &Name);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    for (uint64_t Byte = 0; Byte != Name.Length; ++Byte) {
      Hash ^= (uint8_t)Name.Data[Byte];
      Hash *= UINT64_C(1099511628211);
    }
    Hash ^= UINT64_C(0xff);
    Hash *= UINT64_C(1099511628211);
  }
  *OutFingerprint = Hash;
  return neverc_status_ok();
}

static NevercStatus
create_module_helper(const NevercIRPassInvocation *Invocation) {
  NevercIRTypeHandle VoidType = {0, 0};
  NevercIRTypeHandle FunctionType = {0, 0};
  NevercIRMutationHandle Mutation = {0, 0};
  NevercIRBuilderHandle Builder = {0, 0};
  NevercIRValueHandle Function = {0, 0};
  NevercIRValueHandle Entry = {0, 0};
  NevercIRValueHandle Return = {0, 0};
  NevercStatus Result;

  if (!Invocation->Builder)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  Result = Invocation->Core->GetPrimitiveType(Invocation->Core->Context,
                                              Invocation->Task,
                                              NEVERC_IR_TYPE_VOID, &VoidType);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Core->GetFunctionType(Invocation->Core->Context,
                                               Invocation->Task, VoidType, NULL,
                                               0, NEVERC_FALSE, &FunctionType);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->BeginMutation(
        Invocation->Builder->Context, Invocation->Task,
        NEVERC_IR_MUTATION_SCOPE_MODULE, Invocation->Module, &Mutation);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->CreateFunction(
        Invocation->Builder->Context, Invocation->Task, Mutation, FunctionType,
        STRING_VIEW("neverc_test_lto_plugin_helper"), &Function);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->CreateBasicBlock(
        Invocation->Builder->Context, Invocation->Task, Mutation, Function,
        STRING_VIEW("entry"), &Entry);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->CreateBuilder(
        Invocation->Builder->Context, Invocation->Task, Mutation, &Builder);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->SetInsertBlock(
        Invocation->Builder->Context, Invocation->Task, Builder, Entry);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->BuildReturnVoid(
        Invocation->Builder->Context, Invocation->Task, Builder, &Return);
  if (Result.Code == NEVERC_STATUS_OK)
    Result = Invocation->Builder->CommitMutation(Invocation->Builder->Context,
                                                 Invocation->Task, Mutation);
  else if (!neverc_handle_is_null(Mutation))
    (void)Invocation->Builder->AbortMutation(Invocation->Builder->Context,
                                             Invocation->Task, Mutation);
  if (!neverc_handle_is_null(Builder))
    (void)Invocation->Builder->DestroyBuilder(Invocation->Builder->Context,
                                              Invocation->Task, Builder);
  if (!neverc_handle_is_null(Mutation))
    (void)Invocation->Builder->DestroyMutation(Invocation->Builder->Context,
                                               Invocation->Task, Mutation);
  return Result;
}

static NevercStatus NEVERC_CALL
run_pass(const NevercIRPassInvocation *Invocation,
         NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  PassState *State = (PassState *)UserData;
  if (!Invocation || !OutPreserved || !State || !State->Process ||
      Invocation->Level != State->Level || !Invocation->Core ||
      neverc_handle_is_null(Invocation->Module))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  if (same_handle(Invocation->Task, State->Process->LTOTask)) {
    if (State->Level != NEVERC_IR_PASS_LEVEL_MODULE) {
      uint64_t Fingerprint = 0;
      NevercStatus Result =
          module_definition_fingerprint(Invocation, &Fingerprint);
      if (Result.Code != NEVERC_STATUS_OK)
        return Result;
      if (State->HasModuleFingerprint != NEVERC_TRUE) {
        State->ModuleFingerprint = Fingerprint;
        State->HasModuleFingerprint = NEVERC_TRUE;
      } else if (State->ModuleFingerprint != Fingerprint) {
        State->ModuleMismatch = NEVERC_TRUE;
      }
      ++State->InvocationCount;
    } else {
      State->Process->Seen |= State->Bit;
      for (uint32_t Index = 0; Index != 5; ++Index)
        if (State->Bit == (UINT32_C(1) << Index))
          ++State->Process->Calls[Index];
      if (State->Bit == (UINT32_C(1) << 4)) {
        NevercStatus Result = create_module_helper(Invocation);
        if (Result.Code != NEVERC_STATUS_OK)
          return Result;
      }
    }
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
    memset(State->Calls, 0, sizeof(State->Calls));
    for (uint32_t Index = 0; Index != 8; ++Index) {
      Passes[Index].ModuleFingerprint = 0;
      Passes[Index].InvocationCount = 0;
      Passes[Index].HasModuleFingerprint = NEVERC_FALSE;
      Passes[Index].ModuleMismatch = NEVERC_FALSE;
    }
    State->LTOEnded = NEVERC_FALSE;
  }
  *OutTaskState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL task_end(const NevercCoreAPI *Core,
                                         NevercTaskHandle Task,
                                         NevercTaskKind Kind,
                                         void *ProcessStateValue,
                                         void *SessionState, void *TaskState) {
  (void)Core;
  (void)ProcessStateValue;
  (void)SessionState;
  ProcessState *State = (ProcessState *)TaskState;
  if (!State)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Kind == NEVERC_TASK_LTO) {
    if (!same_handle(Task, State->LTOTask) || State->Seen != UINT32_C(0x1f))
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    for (uint32_t Index = 5; Index != 8; ++Index)
      if (Passes[Index].InvocationCount == 0 ||
          Passes[Index].ModuleMismatch == NEVERC_TRUE)
        return status(NEVERC_STATUS_VERIFICATION_FAILED);
#ifdef NEVERC_TEST_LTO_REQUIRE_PARTITIONS
    for (uint32_t Index = 0; Index != 5; ++Index)
      if (State->Calls[Index] != 1)
        return status(NEVERC_STATUS_VERIFICATION_FAILED);
#endif
    State->LTOEnded = NEVERC_TRUE;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessStateValue) {
  (void)Core;
  (void)Registrar;
  static const NevercInterfaceID Phases[5] = {
      {NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH, NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
       NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW},
      {NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
       NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
      {NEVERC_PHASE_IR_PASS_POST_OPT_HIGH, NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
       NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
  };
  static const char *Names[5] = {
      "neverc.test.lto.pre_opt",        "neverc.test.lto.pipeline_start",
      "neverc.test.lto.optimizer_last", "neverc.test.lto.post_opt",
      "neverc.test.lto.pre_codegen",
  };
  ProcessState *State = (ProcessState *)ProcessStateValue;
  if (!PassAPI || !State)
    return status(NEVERC_STATUS_INVALID_STATE);
  for (uint32_t Index = 0; Index != 5; ++Index) {
    Passes[Index].Process = State;
    Passes[Index].Bit = UINT32_C(1) << Index;
    Passes[Index].Level = NEVERC_IR_PASS_LEVEL_MODULE;
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
    NevercStatus Result =
        PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Descriptor);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
  }

  // Lower-level passes can still create module-owned helpers and globals.  At
  // each late phase, every invocation must therefore see the same reassembled
  // module rather than a sequence of independently optimized PCG partitions.
  static const NevercIRPassLevel SupplementalLevels[3] = {
      NEVERC_IR_PASS_LEVEL_CGSCC,
      NEVERC_IR_PASS_LEVEL_FUNCTION,
      NEVERC_IR_PASS_LEVEL_LOOP,
  };
  static const NevercInterfaceID SupplementalPhases[3] = {
      {NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
       NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
      {NEVERC_PHASE_IR_PASS_POST_OPT_HIGH, NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
      {NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
       NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
  };
  static const char *SupplementalNames[3] = {
      "neverc.test.lto.final_cgscc",
      "neverc.test.lto.final_function",
      "neverc.test.lto.final_loop",
  };
  for (uint32_t Index = 0; Index != 3; ++Index) {
    PassState *Supplemental = &Passes[5 + Index];
    NevercIRPassDescriptor Descriptor;
    Supplemental->Process = State;
    Supplemental->Bit = 0;
    Supplemental->Level = SupplementalLevels[Index];
    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Header =
        (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_PASS_API_MAJOR,
                               NEVERC_IR_PASS_API_MINOR, 0};
    Descriptor.PassID.Data = SupplementalNames[Index];
    Descriptor.PassID.Length = (uint64_t)strlen(SupplementalNames[Index]);
    Descriptor.Phase = SupplementalPhases[Index];
    Descriptor.Level = SupplementalLevels[Index];
    Descriptor.Deterministic = NEVERC_TRUE;
    Descriptor.Run = run_pass;
    Descriptor.UserData = Supplemental;
    NevercStatus Result =
        PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Descriptor);
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
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
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
