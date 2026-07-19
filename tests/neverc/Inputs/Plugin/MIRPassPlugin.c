#include "neverc/Plugin/PluginMIR.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

typedef struct ProcessState {
  uint32_t Seen;
  uint32_t CurrentBit;
} ProcessState;

typedef struct PassState {
  ProcessState *Process;
  uint32_t Bit;
  NevercInterfaceID Phase;
} PassState;

static const NevercMIRPassAPI *PassAPI;
static PassState Passes[9];

#ifdef NEVERC_TEST_MIR_MODULE_LEVEL
#define NEVERC_TEST_MIR_PASS_LEVEL NEVERC_MIR_PASS_LEVEL_MODULE
#elif defined(NEVERC_TEST_MIR_BASIC_BLOCK_LEVEL)
#define NEVERC_TEST_MIR_PASS_LEVEL NEVERC_MIR_PASS_LEVEL_BASIC_BLOCK
#else
#define NEVERC_TEST_MIR_PASS_LEVEL NEVERC_MIR_PASS_LEVEL_FUNCTION
#endif

static NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

static int same_interface(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

static NevercStatus NEVERC_CALL run_pass(
    const NevercMIRPassInvocation *Invocation,
    NevercMIRPreservedAnalyses *OutPreserved, void *UserData) {
  PassState *State = (PassState *)UserData;
  if (!Invocation || !OutPreserved || !State || !State->Process ||
      Invocation->Level != NEVERC_TEST_MIR_PASS_LEVEL ||
      !Invocation->Core || !Invocation->Analyses ||
      !same_interface(Invocation->Phase, State->Phase))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
#ifdef NEVERC_TEST_MIR_BASIC_BLOCK_LEVEL
  if (neverc_handle_is_null(Invocation->BasicBlock))
    return status(NEVERC_STATUS_POLICY_VIOLATION);
#else
  if (!neverc_handle_is_null(Invocation->BasicBlock))
    return status(NEVERC_STATUS_POLICY_VIOLATION);
#endif

#ifdef NEVERC_TEST_MIR_MODULE_LEVEL
  if (State->Process->CurrentBit == 0)
    State->Process->CurrentBit = State->Bit;
  else if (State->Process->CurrentBit != State->Bit) {
    if (State->Bit != (State->Process->CurrentBit << 1))
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    State->Process->CurrentBit = State->Bit;
  }
#endif
  State->Process->Seen |= State->Bit;
  uint64_t BlockCount = 0;
  NevercStatus Result = Invocation->Core->GetBasicBlockCount(
      Invocation->Core->Context, Invocation->Task, Invocation->Function,
      &BlockCount);
  if (Result.Code != NEVERC_STATUS_OK || BlockCount == 0)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  NevercMachineBasicBlockHandle Block = {0, 0};
  Result = Invocation->Core->GetFirstBasicBlock(
      Invocation->Core->Context, Invocation->Task, Invocation->Function,
      &Block);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;

  NevercMIRAnalysisResultHandle Dominators = {0, 0};
  Result = Invocation->Analyses->QueryBuiltin(
      Invocation->Analyses->Context, Invocation->Task,
      NEVERC_MIR_ANALYSIS_DOMINATOR_TREE, Invocation->Function, &Dominators);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  NevercBool Dominates = NEVERC_FALSE;
  Result = Invocation->Analyses->DominatorTreeDominates(
      Invocation->Analyses->Context, Invocation->Task, Dominators, Block,
      Block, &Dominates);
  if (Result.Code != NEVERC_STATUS_OK || Dominates != NEVERC_TRUE)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  NevercMIRAnalysisResultHandle Pressure = {0, 0};
  Result = Invocation->Analyses->QueryBuiltin(
      Invocation->Analyses->Context, Invocation->Task,
      NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE, Invocation->Function, &Pressure);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  uint64_t PressureSetCount = 0;
  Result = Invocation->Analyses->GetRegisterPressureSetCount(
      Invocation->Analyses->Context, Invocation->Task, Pressure, Block,
      &PressureSetCount);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;

  NevercMIRAnalysisResultHandle Intervals = {0, 0};
  Result = Invocation->Analyses->QueryBuiltin(
      Invocation->Analyses->Context, Invocation->Task,
      NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Invocation->Function, &Intervals);
#ifndef NEVERC_TEST_MIR_MODULE_LEVEL
  if (State->Bit == (UINT32_C(1) << 4)) {
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    const NevercMIRBuiltinAnalysis Additional[] = {
        NEVERC_MIR_ANALYSIS_SLOT_INDEXES,
        NEVERC_MIR_ANALYSIS_LOOP_INFO,
    };
    for (uint32_t Index = 0;
         Index != (uint32_t)(sizeof(Additional) / sizeof(Additional[0]));
         ++Index) {
      NevercMIRAnalysisResultHandle Handle = {0, 0};
      Result = Invocation->Analyses->QueryBuiltin(
          Invocation->Analyses->Context, Invocation->Task, Additional[Index],
          Invocation->Function, &Handle);
      if (Result.Code != NEVERC_STATUS_OK)
        return Result;
    }
  } else if (Result.Code != NEVERC_STATUS_CAPABILITY_UNAVAILABLE) {
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  }
#else
  if (Result.Code != NEVERC_STATUS_CAPABILITY_UNAVAILABLE)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
#endif
  if (State->Bit == (UINT32_C(1) << 2)) {
    NevercMIRAnalysisResultHandle Variables = {0, 0};
    Result = Invocation->Analyses->QueryBuiltin(
        Invocation->Analyses->Context, Invocation->Task,
        NEVERC_MIR_ANALYSIS_LIVE_VARIABLES, Invocation->Function, &Variables);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
  }

  if (State->Bit == (UINT32_C(1) << 8)) {
    NevercBool Original = NEVERC_FALSE;
    Result = Invocation->Core->GetMachineProperty(
        Invocation->Core->Context, Invocation->Task, Invocation->Function,
        NEVERC_MIR_PROPERTY_NO_PH_IS, &Original);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    NevercMIRMutationHandle Mutation = {0, 0};
    Result = Invocation->Core->BeginMutation(
        Invocation->Core->Context, Invocation->Task, Invocation->Function,
        &Mutation);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    NevercMIRPropertyProof Proof;
    memset(&Proof, 0, sizeof(Proof));
    Proof.Header =
        (NevercABITableHeader){sizeof(Proof), NEVERC_MIR_API_MAJOR,
                              NEVERC_MIR_API_MINOR, 0};
    Proof.Property = NEVERC_MIR_PROPERTY_NO_PH_IS;
    Proof.Kind = Original == NEVERC_TRUE
                     ? NEVERC_MIR_PROPERTY_PROOF_INVALIDATION
                     : NEVERC_MIR_PROPERTY_PROOF_STRUCTURAL_CHECK;
    Proof.Value = Original == NEVERC_TRUE ? NEVERC_FALSE : NEVERC_TRUE;
    Result = Invocation->Core->SetMachinePropertyWithProof(
        Invocation->Core->Context, Invocation->Task, Mutation, &Proof);
    if (Result.Code == NEVERC_STATUS_OK)
      Result = Invocation->Core->CommitMutation(
          Invocation->Core->Context, Invocation->Task, Mutation);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    Result = Invocation->Analyses->DominatorTreeDominates(
        Invocation->Analyses->Context, Invocation->Task, Dominators, Block,
        Block, &Dominates);
    if (Result.Code != NEVERC_STATUS_STALE_HANDLE)
      return status(NEVERC_STATUS_VERIFICATION_FAILED);

    Result = Invocation->Core->BeginMutation(
        Invocation->Core->Context, Invocation->Task, Invocation->Function,
        &Mutation);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
    Proof.Kind = Original == NEVERC_TRUE
                     ? NEVERC_MIR_PROPERTY_PROOF_STRUCTURAL_CHECK
                     : NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
    Proof.Value = Original;
    Result = Invocation->Core->SetMachinePropertyWithProof(
        Invocation->Core->Context, Invocation->Task, Mutation, &Proof);
    if (Result.Code == NEVERC_STATUS_OK)
      Result = Invocation->Core->CommitMutation(
          Invocation->Core->Context, Invocation->Task, Mutation);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
  }

  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved), NEVERC_MIR_PASS_API_MAJOR,
                            NEVERC_MIR_PASS_API_MINOR, 0};
#ifdef NEVERC_TEST_MIR_INVALID_PRESERVE
  OutPreserved->Flags = NEVERC_MIR_PRESERVE_ALL;
#else
  OutPreserved->Flags = State->Bit == (UINT32_C(1) << 8)
                            ? NEVERC_MIR_PRESERVE_NONE
                            : NEVERC_MIR_PRESERVE_ALL;
#endif
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(const NevercCoreAPI *,
                                               void **OutProcessState) {
  static ProcessState State;
  if (!OutProcessState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  State.Seen = 0;
  State.CurrentBit = 0;
  *OutProcessState = &State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *, const NevercRegistrarAPI *, void *RegistrarContext,
    void *ProcessStateValue) {
  static const NevercInterfaceID Phases[9] = {
      {NEVERC_PHASE_MIR_PASS_POST_LEGALIZE_HIGH,
       NEVERC_PHASE_MIR_PASS_POST_LEGALIZE_LOW},
      {NEVERC_PHASE_MIR_PASS_POST_ISEL_HIGH,
       NEVERC_PHASE_MIR_PASS_POST_ISEL_LOW},
      {NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_HIGH,
       NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_LOW},
      {NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_HIGH,
       NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_LOW},
      {NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_HIGH,
       NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_LOW},
      {NEVERC_PHASE_MIR_PASS_POST_REGALLOC_HIGH,
       NEVERC_PHASE_MIR_PASS_POST_REGALLOC_LOW},
      {NEVERC_PHASE_MIR_PASS_POST_PROLOG_EPILOG_HIGH,
       NEVERC_PHASE_MIR_PASS_POST_PROLOG_EPILOG_LOW},
      {NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
       NEVERC_PHASE_MIR_PASS_PREEMIT_LOW},
      {NEVERC_PHASE_MIR_PASS_FINAL_HIGH, NEVERC_PHASE_MIR_PASS_FINAL_LOW},
  };
  static const char *Names[9] = {
      "neverc.test.mir.post_legalize", "neverc.test.mir.post_isel",
      "neverc.test.mir.pre_scheduler", "neverc.test.mir.post_scheduler",
      "neverc.test.mir.pre_regalloc", "neverc.test.mir.post_regalloc",
      "neverc.test.mir.post_prolog_epilog", "neverc.test.mir.preemit",
      "neverc.test.mir.final",
  };
  static const NevercMIRBuiltinAnalysis PreRegAllocAnalyses[] = {
      NEVERC_MIR_ANALYSIS_LIVE_INTERVALS,
      NEVERC_MIR_ANALYSIS_SLOT_INDEXES,
      NEVERC_MIR_ANALYSIS_DOMINATOR_TREE,
      NEVERC_MIR_ANALYSIS_LOOP_INFO,
  };
  static const NevercMIRBuiltinAnalysis PreSchedulerAnalyses[] = {
      NEVERC_MIR_ANALYSIS_LIVE_VARIABLES,
  };
  ProcessState *Process = (ProcessState *)ProcessStateValue;
  if (!PassAPI || !Process)
    return status(NEVERC_STATUS_INVALID_STATE);
  for (uint32_t Index = 0; Index != 9; ++Index) {
    Passes[Index].Process = Process;
    Passes[Index].Bit = UINT32_C(1) << Index;
    Passes[Index].Phase = Phases[Index];
    NevercMIRPassDescriptor Descriptor;
    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Header =
        (NevercABITableHeader){sizeof(Descriptor), NEVERC_MIR_PASS_API_MAJOR,
                              NEVERC_MIR_PASS_API_MINOR, 0};
    Descriptor.PassID.Data = Names[Index];
    Descriptor.PassID.Length = (uint64_t)strlen(Names[Index]);
    Descriptor.Phase = Phases[Index];
    Descriptor.Level = NEVERC_TEST_MIR_PASS_LEVEL;
    Descriptor.Deterministic = NEVERC_TRUE;
    if (Index != 8) {
      Descriptor.PreservedAnalyses = PreRegAllocAnalyses;
      Descriptor.PreservedAnalysisCount =
          sizeof(PreRegAllocAnalyses) / sizeof(PreRegAllocAnalyses[0]);
    }
#ifndef NEVERC_TEST_MIR_MODULE_LEVEL
    if (Index == 4) {
      Descriptor.RequiredAnalyses = PreRegAllocAnalyses;
      Descriptor.RequiredAnalysisCount =
          sizeof(PreRegAllocAnalyses) / sizeof(PreRegAllocAnalyses[0]);
    } else if (Index == 2) {
      Descriptor.RequiredAnalyses = PreSchedulerAnalyses;
      Descriptor.RequiredAnalysisCount =
          sizeof(PreSchedulerAnalyses) / sizeof(PreSchedulerAnalyses[0]);
    }
#endif
    Descriptor.Run = run_pass;
    Descriptor.UserData = &Passes[Index];
    NevercStatus Result = PassAPI->RegisterPass(
        PassAPI->Context, RegistrarContext, &Descriptor);
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *,
                                                void *ProcessStateValue) {
  ProcessState *Process = (ProcessState *)ProcessStateValue;
  if (!Process || Process->Seen != UINT32_C(0x1ff))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  return neverc_status_ok();
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
      (NevercInterfaceID){NEVERC_INTERFACE_MIR_PASS_HIGH,
                          NEVERC_INTERFACE_MIR_PASS_LOW},
      NEVERC_MIR_PASS_API_MAJOR, NEVERC_MIR_PASS_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Result.Code != NEVERC_STATUS_OK)
    return Result;
  if (!Table || StructSize < sizeof(NevercMIRPassAPI))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercMIRPassAPI *)Table;

  uint32_t Capacity = OutPlugin->Header.StructSize;
  NevercPluginDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.mir-pass");
  Descriptor.DisplayName = STRING_VIEW("NeverC MIR pass test");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_PROCESS_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;
  size_t Writable =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
