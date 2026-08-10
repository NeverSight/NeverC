#include "neverc/Plugin/PluginIR.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercIRPassAPI *PassAPI;
static const NevercIRAnalysisAPI *AnalysisAPI;
static int ProcessState;
#if defined(NEVERC_TEST_IR_PASS_LTO_LATE_NVK_REFERENCE)
static uint64_t LateNvkPassInvocationCount;
#endif

typedef struct AnalysisResult {
  uint64_t ComputeCount;
  uint64_t InvalidateCount;
  uint64_t DestroyCount;
  NevercIRAnalysisInvalidationReason LastInvalidationReason;
} AnalysisResult;

static AnalysisResult TestAnalysisResult;
#if defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
static AnalysisResult TestDependentAnalysisResult;
static uint32_t LifecycleEvents[4];
static uint32_t LifecycleEventCount;
#endif

static const NevercInterfaceID TestAnalysisID = {
    UINT64_C(0x4e43505445535441), UINT64_C(0x0000000000000001)};
static const NevercInterfaceID TestAnalysisDependencyID = {
    UINT64_C(0x4e43505445535441), UINT64_C(0x0000000000000002)};

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus collect_values(const NevercIRPassInvocation *Invocation,
                                   NevercHandle Container,
                                   NevercIRValueCollection Collection,
                                   NevercIRValueHandle *Values,
                                   uint64_t Capacity, uint64_t *OutCount) {
  NevercIRValueCursor Cursor;
  NevercStatus Status;
  memset(&Cursor, 0, sizeof(Cursor));
  Status = Invocation->Core->BeginValueCursor(
      Invocation->Core->Context, Invocation->Task, Container, Collection,
      &Cursor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Invocation->Core->CollectValueCursor(
      Invocation->Core->Context, Invocation->Task, &Cursor, Values, Capacity,
      OutCount);
}

static NevercStatus NEVERC_CALL
compute_analysis(const NevercIRPassInvocation *Invocation,
                 void **OutResult, void *UserData) {
  AnalysisResult *State =
      UserData ? (AnalysisResult *)UserData : &TestAnalysisResult;
  if (!Invocation || !OutResult ||
      Invocation->Level != NEVERC_IR_PASS_LEVEL_MODULE ||
      neverc_handle_is_null(Invocation->Module) || !Invocation->Core ||
      Invocation->Builder)
    return failure(NEVERC_STATUS_POLICY_VIOLATION);
#if defined(NEVERC_TEST_IR_ANALYSIS_RECURSIVE)
  {
    NevercIRAnalysisResultHandle Recursive = {0, 0};
    return Invocation->Analyses->QueryCustom(
        Invocation->Analyses->Context, Invocation->Task, TestAnalysisID,
        &Recursive);
  }
#elif defined(NEVERC_TEST_IR_ANALYSIS_ERROR)
  return failure(NEVERC_STATUS_PLUGIN_FAILURE);
#else
#if defined(NEVERC_TEST_IR_ANALYSIS_MUTATION)
  {
    NevercStatus Mutation = Invocation->Core->SetModuleIdentifier(
        Invocation->Core->Context, Invocation->Task,
        STRING_VIEW("analysis-must-not-mutate"));
    if (Mutation.Code != NEVERC_STATUS_POLICY_VIOLATION)
      return failure(NEVERC_STATUS_VERIFICATION_FAILED);
  }
#endif
  ++State->ComputeCount;
  *OutResult = State;
  return neverc_status_ok();
#endif
}

static NevercStatus NEVERC_CALL query_analysis(const void *Result,
                                               NevercByteView *OutData,
                                               void *UserData) {
  (void)UserData;
  if (!Result || !OutData)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  OutData->Data = (const uint8_t *)Result;
  OutData->Length = sizeof(AnalysisResult);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL invalidate_analysis(
    void *Result, NevercIRAnalysisInvalidationReason Reason, void *UserData) {
  AnalysisResult *State = (AnalysisResult *)Result;
  (void)UserData;
  if (!State)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  ++State->InvalidateCount;
  State->LastInvalidationReason = Reason;
#if defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
  if (LifecycleEventCount < 4)
    LifecycleEvents[LifecycleEventCount++] =
        State == &TestDependentAnalysisResult ? 1U : 3U;
#endif
  return neverc_status_ok();
}

static void NEVERC_CALL destroy_analysis(void *Result, void *UserData) {
  AnalysisResult *State = (AnalysisResult *)Result;
  (void)UserData;
  if (State) {
    ++State->DestroyCount;
#if defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
    if (LifecycleEventCount < 4)
      LifecycleEvents[LifecycleEventCount++] =
          State == &TestDependentAnalysisResult ? 2U : 4U;
#endif
  }
}

static NevercBool string_equals(NevercStringView Value, const char *Text,
                                size_t Length) {
  return Value.Length == (uint64_t)Length && Value.Data &&
         memcmp(Value.Data, Text, Length) == 0
             ? NEVERC_TRUE
             : NEVERC_FALSE;
}

#if defined(NEVERC_TEST_IR_PASS_LATE_LITERAL)
static NevercStatus inject_late_literal_argument(
    const NevercIRPassInvocation *Invocation) {
  NevercIRValueHandle Functions[32];
  NevercIRValueHandle SourceFunction = {0, 0};
  NevercIRValueHandle TargetFunction = {0, 0};
  NevercIRValueHandle SourceReturn = {0, 0};
  NevercIRValueHandle LiteralPointer = {0, 0};
  NevercIRValueHandle TargetCall = {0, 0};
  uint64_t FunctionCount = 0;
  uint64_t I;
  NevercStatus Status = collect_values(
      Invocation, Invocation->Module, NEVERC_IR_COLLECTION_MODULE_FUNCTIONS,
      Functions, 32, &FunctionCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  for (I = 0; I != FunctionCount; ++I) {
    NevercStringView Name = {0, 0};
    Status = Invocation->Core->GetValueName(
        Invocation->Core->Context, Invocation->Task, Functions[I], &Name);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (string_equals(Name, "plugin_tail_source",
                      sizeof("plugin_tail_source") - 1) == NEVERC_TRUE)
      SourceFunction = Functions[I];
    else if (string_equals(Name, "plugin_tail_target",
                           sizeof("plugin_tail_target") - 1) == NEVERC_TRUE)
      TargetFunction = Functions[I];
  }
  if (neverc_handle_is_null(SourceFunction) ||
      neverc_handle_is_null(TargetFunction))
    return failure(NEVERC_STATUS_NOT_FOUND);

  {
    NevercIRValueHandle Blocks[16];
    uint64_t BlockCount = 0;
    uint64_t B;
    Status = collect_values(Invocation, SourceFunction,
                            NEVERC_IR_COLLECTION_FUNCTION_BLOCKS, Blocks, 16,
                            &BlockCount);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    for (B = 0; B != BlockCount && neverc_handle_is_null(LiteralPointer); ++B) {
      NevercIRValueHandle Instructions[64];
      uint64_t InstructionCount = 0;
      uint64_t J;
      Status = collect_values(Invocation, Blocks[B],
                              NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS,
                              Instructions, 64, &InstructionCount);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      for (J = 0; J != InstructionCount; ++J) {
        NevercIROpcode Opcode = 0;
        uint64_t OperandCount = 0;
        Status = Invocation->Core->GetInstructionOpcode(
            Invocation->Core->Context, Invocation->Task, Instructions[J],
            &Opcode);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Opcode != NEVERC_IR_OPCODE_RET)
          continue;
        Status = Invocation->Core->GetOperandCount(
            Invocation->Core->Context, Invocation->Task, Instructions[J],
            &OperandCount);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (OperandCount == 1) {
          Status = Invocation->Core->GetOperand(
              Invocation->Core->Context, Invocation->Task, Instructions[J], 0,
              &LiteralPointer);
          SourceReturn = Instructions[J];
        }
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
      }
    }
  }

  {
    NevercIRValueHandle Blocks[16];
    uint64_t BlockCount = 0;
    uint64_t B;
    Status = collect_values(Invocation, TargetFunction,
                            NEVERC_IR_COLLECTION_FUNCTION_BLOCKS, Blocks, 16,
                            &BlockCount);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    for (B = 0; B != BlockCount && neverc_handle_is_null(TargetCall); ++B) {
      NevercIRValueHandle Instructions[128];
      uint64_t InstructionCount = 0;
      uint64_t J;
      Status = collect_values(Invocation, Blocks[B],
                              NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS,
                              Instructions, 128, &InstructionCount);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      for (J = 0; J != InstructionCount; ++J) {
        NevercIROpcode Opcode = 0;
        uint64_t OperandCount = 0;
        NevercIRValueHandle Callee = {0, 0};
        NevercStringView CalleeName = {0, 0};
        Status = Invocation->Core->GetInstructionOpcode(
            Invocation->Core->Context, Invocation->Task, Instructions[J],
            &Opcode);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (Opcode != NEVERC_IR_OPCODE_CALL)
          continue;
        Status = Invocation->Core->GetOperandCount(
            Invocation->Core->Context, Invocation->Task, Instructions[J],
            &OperandCount);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (OperandCount != 2)
          continue;
        Status = Invocation->Core->GetOperand(
            Invocation->Core->Context, Invocation->Task, Instructions[J], 1,
            &Callee);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Status = Invocation->Core->GetValueName(
            Invocation->Core->Context, Invocation->Task, Callee, &CalleeName);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        if (string_equals(CalleeName, "consume", sizeof("consume") - 1) !=
            NEVERC_TRUE)
          continue;
        Status = Invocation->Core->SetOperand(
            Invocation->Core->Context, Invocation->Task, Instructions[J], 0,
            LiteralPointer);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        TargetCall = Instructions[J];
      }
    }
  }

  if (neverc_handle_is_null(LiteralPointer) || neverc_handle_is_null(TargetCall))
    return failure(NEVERC_STATUS_NOT_FOUND);
  {
    NevercIRTypeHandle PointerType = {0, 0};
    NevercIRValueHandle NullPointer = {0, 0};
    Status = Invocation->Core->GetValueType(
        Invocation->Core->Context, Invocation->Task, LiteralPointer,
        &PointerType);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Core->GetNullConstant(
          Invocation->Core->Context, Invocation->Task, PointerType,
          &NullPointer);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Core->SetOperand(
          Invocation->Core->Context, Invocation->Task, SourceReturn, 0,
          NullPointer);
    return Status;
  }
}
#endif

#if defined(NEVERC_TEST_IR_PASS_LATE_NVK_REFERENCE) ||                        \
    defined(NEVERC_TEST_IR_PASS_LTO_LATE_NVK_REFERENCE)
static NevercStatus inject_late_nvk_runtime_reference(
    const NevercIRPassInvocation *Invocation) {
  NevercIRValueHandle Functions[128];
  uint64_t FunctionCount = 0;
  uint64_t I;
  NevercStatus Status = collect_values(
      Invocation, Invocation->Module, NEVERC_IR_COLLECTION_MODULE_FUNCTIONS,
      Functions, 128, &FunctionCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  for (I = 0; I != FunctionCount; ++I) {
    NevercStringView Name = {0, 0};
    Status = Invocation->Core->GetValueName(
        Invocation->Core->Context, Invocation->Task, Functions[I], &Name);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (string_equals(Name, "neverc_krt_fmt_init",
                      sizeof("neverc_krt_fmt_init") - 1) == NEVERC_TRUE)
      return neverc_status_ok();
    if (string_equals(Name, "plugin_late_nvk_runtime",
                      sizeof("plugin_late_nvk_runtime") - 1) != NEVERC_TRUE)
      continue;
    return Invocation->Core->SetValueName(
        Invocation->Core->Context, Invocation->Task, Functions[I],
        STRING_VIEW("neverc_krt_fmt_init"));
  }
  return failure(NEVERC_STATUS_NOT_FOUND);
}
#endif

static NevercStatus check_call_graph(
    const NevercIRPassInvocation *Invocation) {
  NevercIRAnalysisResultHandle Result = {0, 0};
  NevercIRValueHandle Functions[16];
  uint64_t FunctionCount = 0;
  uint64_t CalleeCount = 0;
  NevercStatus Status = Invocation->Analyses->QueryBuiltin(
      Invocation->Analyses->Context, Invocation->Task,
      NEVERC_IR_ANALYSIS_CALL_GRAPH, (NevercIRValueHandle){0, 0}, &Result);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = collect_values(Invocation, Invocation->Module,
                          NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, Functions,
                          16, &FunctionCount);
  if (Status.Code != NEVERC_STATUS_OK || FunctionCount == 0)
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_NOT_FOUND)
               : Status;
  Status = Invocation->Analyses->GetDirectCalleeCount(
      Invocation->Analyses->Context, Invocation->Task, Result, Functions[0],
      &CalleeCount);
  if (Status.Code == NEVERC_STATUS_OK && CalleeCount != 0) {
    NevercIRValueHandle Callee = {0, 0};
    Status = Invocation->Analyses->GetDirectCallee(
        Invocation->Analyses->Context, Invocation->Task, Result, Functions[0],
        0, &Callee);
  }
  return Status;
}

static NevercStatus check_function_analyses(
    const NevercIRPassInvocation *Invocation) {
  NevercIRAnalysisResultHandle DomTree = {0, 0};
  NevercIRAnalysisResultHandle PostDomTree = {0, 0};
  NevercIRAnalysisResultHandle LoopInfo = {0, 0};
  NevercIRAnalysisResultHandle ScalarEvolution = {0, 0};
  NevercIRAnalysisResultHandle MemorySSA = {0, 0};
  NevercIRAnalysisResultHandle Alias = {0, 0};
  NevercIRValueHandle Blocks[16];
  NevercIRValueHandle Instructions[32];
  NevercIRValueHandle Pointer = {0, 0};
  uint64_t BlockCount = 0;
  uint64_t InstructionCount = 0;
  uint64_t LoopCount = 0;
  uint64_t I;
  NevercStatus Status;

#define QUERY_ANALYSIS(Kind, Output)                                          \
  do {                                                                         \
    Status = Invocation->Analyses->QueryBuiltin(                               \
        Invocation->Analyses->Context, Invocation->Task, (Kind),               \
        Invocation->Function, &(Output));                                      \
    if (Status.Code != NEVERC_STATUS_OK)                                       \
      return Status;                                                           \
  } while (0)

  QUERY_ANALYSIS(NEVERC_IR_ANALYSIS_DOMINATOR_TREE, DomTree);
  QUERY_ANALYSIS(NEVERC_IR_ANALYSIS_POST_DOMINATOR_TREE, PostDomTree);
  QUERY_ANALYSIS(NEVERC_IR_ANALYSIS_LOOP_INFO, LoopInfo);
  QUERY_ANALYSIS(NEVERC_IR_ANALYSIS_SCALAR_EVOLUTION, ScalarEvolution);
  if (Invocation->Level != NEVERC_IR_PASS_LEVEL_LOOP)
    QUERY_ANALYSIS(NEVERC_IR_ANALYSIS_MEMORY_SSA, MemorySSA);
  QUERY_ANALYSIS(NEVERC_IR_ANALYSIS_ALIAS, Alias);
#undef QUERY_ANALYSIS

  Status = collect_values(Invocation, Invocation->Function,
                          NEVERC_IR_COLLECTION_FUNCTION_BLOCKS, Blocks, 16,
                          &BlockCount);
  if (Status.Code != NEVERC_STATUS_OK || BlockCount == 0)
    return Status;
  {
    NevercBool Dominates = NEVERC_FALSE;
    Status = Invocation->Analyses->DominatorTreeDominates(
        Invocation->Analyses->Context, Invocation->Task, DomTree, Blocks[0],
        Blocks[0], &Dominates);
    if (Status.Code != NEVERC_STATUS_OK || Dominates != NEVERC_TRUE)
      return Status.Code == NEVERC_STATUS_OK
                 ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
                 : Status;
    Dominates = NEVERC_FALSE;
    Status = Invocation->Analyses->DominatorTreeDominates(
        Invocation->Analyses->Context, Invocation->Task, PostDomTree, Blocks[0],
        Blocks[0], &Dominates);
    if (Status.Code != NEVERC_STATUS_OK || Dominates != NEVERC_TRUE)
      return Status.Code == NEVERC_STATUS_OK
                 ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
                 : Status;
  }

  Status = Invocation->Analyses->GetLoopCount(
      Invocation->Analyses->Context, Invocation->Task, LoopInfo, &LoopCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (LoopCount != 0) {
    NevercIRValueHandle Header = {0, 0};
    NevercIRValueHandle ContainingHeader = {0, 0};
    NevercBool Known = NEVERC_FALSE;
    uint32_t Depth = 0;
    uint64_t TripCount = 0;
    Status = Invocation->Analyses->GetLoopHeader(
        Invocation->Analyses->Context, Invocation->Task, LoopInfo, 0, &Header);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Analyses->GetLoopForBlock(
          Invocation->Analyses->Context, Invocation->Task, LoopInfo, Header,
          &ContainingHeader, &Depth);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Analyses->GetScalarEvolutionConstantTripCount(
          Invocation->Analyses->Context, Invocation->Task, ScalarEvolution,
          Header, &Known, &TripCount);
    if (Status.Code != NEVERC_STATUS_OK || Depth == 0 ||
        neverc_handle_is_null(ContainingHeader))
      return Status.Code == NEVERC_STATUS_OK
                 ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
                 : Status;
  }

  Status = collect_values(Invocation, Blocks[0],
                          NEVERC_IR_COLLECTION_BLOCK_INSTRUCTIONS,
                          Instructions, 32, &InstructionCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (I = 0; I != InstructionCount; ++I) {
    NevercIRTypeHandle Type = {0, 0};
    NevercIRTypeKind Kind = NEVERC_IR_TYPE_UNKNOWN;
    NevercIRMemoryAccessKind AccessKind = NEVERC_IR_MEMORY_ACCESS_NONE;
    if (!neverc_handle_is_null(MemorySSA)) {
      Status = Invocation->Analyses->GetMemoryAccessKind(
          Invocation->Analyses->Context, Invocation->Task, MemorySSA,
          Instructions[I], &AccessKind);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    Status = Invocation->Core->GetValueType(
        Invocation->Core->Context, Invocation->Task, Instructions[I], &Type);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Core->GetTypeKind(
          Invocation->Core->Context, Invocation->Task, Type, &Kind);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Kind == NEVERC_IR_TYPE_POINTER && neverc_handle_is_null(Pointer))
      Pointer = Instructions[I];
  }
  if (!neverc_handle_is_null(Pointer)) {
    NevercIRAliasResult AliasResult = NEVERC_IR_ALIAS_MAY;
    Status = Invocation->Analyses->Alias(
        Invocation->Analyses->Context, Invocation->Task, Alias, Pointer, 1,
        Pointer, 1, &AliasResult);
    if (Status.Code != NEVERC_STATUS_OK ||
        AliasResult != NEVERC_IR_ALIAS_MUST)
      return Status.Code == NEVERC_STATUS_OK
                 ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
                 : Status;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
run_pass(const NevercIRPassInvocation *Invocation,
         NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  NevercIRMetadataHandle Text = {0, 0};
  NevercIRMetadataHandle Node = {0, 0};
  NevercIRNamedMetadataHandle Named = {0, 0};
  NevercStringView Marker;
  NevercStatus Status;
  (void)UserData;

  if (!Invocation || !OutPreserved || !Invocation->Core)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_MODULE &&
      (!neverc_handle_is_null(Invocation->Function) ||
       Invocation->SCCFunctionCount != 0 ||
       !neverc_handle_is_null(Invocation->LoopHeader)))
    return failure(NEVERC_STATUS_POLICY_VIOLATION);
  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_CGSCC &&
      (Invocation->SCCFunctionCount == 0 ||
       !neverc_handle_is_null(Invocation->Function)))
    return failure(NEVERC_STATUS_POLICY_VIOLATION);
  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_FUNCTION &&
      neverc_handle_is_null(Invocation->Function))
    return failure(NEVERC_STATUS_POLICY_VIOLATION);
  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_LOOP &&
      (neverc_handle_is_null(Invocation->Function) ||
       neverc_handle_is_null(Invocation->LoopHeader)))
    return failure(NEVERC_STATUS_POLICY_VIOLATION);
  if (!Invocation->Analyses)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

#if defined(NEVERC_TEST_IR_PASS_LATE_LITERAL) ||                              \
    defined(NEVERC_TEST_IR_PASS_LATE_NVK_REFERENCE) ||                        \
    defined(NEVERC_TEST_IR_PASS_LTO_LATE_NVK_REFERENCE)
  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_MODULE &&
      string_equals(Invocation->PassID, "neverc.test.pre_codegen",
                    sizeof("neverc.test.pre_codegen") - 1) == NEVERC_TRUE) {
#if defined(NEVERC_TEST_IR_PASS_LATE_LITERAL)
    Status = inject_late_literal_argument(Invocation);
#elif defined(NEVERC_TEST_IR_PASS_LATE_NVK_REFERENCE)
    Status = inject_late_nvk_runtime_reference(Invocation);
#else
    ++LateNvkPassInvocationCount;
    Status = LateNvkPassInvocationCount >= 2
                 ? inject_late_nvk_runtime_reference(Invocation)
                 : neverc_status_ok();
#endif
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    memset(OutPreserved, 0, sizeof(*OutPreserved));
    OutPreserved->Header =
        (NevercABITableHeader){sizeof(*OutPreserved),
                              NEVERC_IR_PASS_API_MAJOR,
                              NEVERC_IR_PASS_API_MINOR, 0};
    OutPreserved->Flags = NEVERC_IR_PRESERVE_NONE;
    return neverc_status_ok();
  }
#endif

#if defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
  if (string_equals(Invocation->PassID, "neverc.test.cgscc",
                    sizeof("neverc.test.cgscc") - 1) == NEVERC_TRUE &&
      (LifecycleEventCount != 4 || LifecycleEvents[0] != 1 ||
       LifecycleEvents[1] != 2 || LifecycleEvents[2] != 3 ||
       LifecycleEvents[3] != 4))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);
  if (string_equals(Invocation->PassID, "neverc.test.module",
                    sizeof("neverc.test.module") - 1) == NEVERC_TRUE) {
    NevercIRAnalysisResultHandle Result = {0, 0};
    NevercByteView Data = {0, 0};
    Status = Invocation->Analyses->QueryCustom(
        Invocation->Analyses->Context, Invocation->Task,
        TestAnalysisDependencyID, &Result);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Analyses->GetCustomResultData(
          Invocation->Analyses->Context, Invocation->Task, Result, &Data);
    if (Status.Code != NEVERC_STATUS_OK ||
        Data.Data != (const uint8_t *)&TestDependentAnalysisResult)
      return Status.Code == NEVERC_STATUS_OK
                 ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
                 : Status;
  }
#else
  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_MODULE &&
      (string_equals(Invocation->PassID, "neverc.test.module",
                     sizeof("neverc.test.module") - 1) == NEVERC_TRUE ||
       string_equals(Invocation->PassID, "neverc.test.pipeline_start",
                     sizeof("neverc.test.pipeline_start") - 1) ==
           NEVERC_TRUE ||
       string_equals(Invocation->PassID, "neverc.test.post_opt",
                     sizeof("neverc.test.post_opt") - 1) == NEVERC_TRUE ||
       string_equals(Invocation->PassID, "neverc.test.pre_codegen",
                     sizeof("neverc.test.pre_codegen") - 1) == NEVERC_TRUE)) {
    NevercIRAnalysisResultHandle First = {0, 0};
    NevercIRAnalysisResultHandle Second = {0, 0};
    NevercByteView Data = {0, 0};
    const AnalysisResult *Observed;
    uint64_t ExpectedComputes =
        string_equals(Invocation->PassID, "neverc.test.pre_codegen",
                      sizeof("neverc.test.pre_codegen") - 1) == NEVERC_TRUE
            ? UINT64_C(2)
            : UINT64_C(1);
    uint64_t ExpectedInvalidates = ExpectedComputes - UINT64_C(1);
    Status = Invocation->Analyses->QueryCustom(
        Invocation->Analyses->Context, Invocation->Task, TestAnalysisID,
        &First);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Analyses->QueryCustom(
          Invocation->Analyses->Context, Invocation->Task, TestAnalysisID,
          &Second);
    if (Status.Code == NEVERC_STATUS_OK &&
        (First.Owner != Second.Owner || First.Value != Second.Value))
      Status = failure(NEVERC_STATUS_VERIFICATION_FAILED);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Invocation->Analyses->GetCustomResultData(
          Invocation->Analyses->Context, Invocation->Task, First, &Data);
    Observed = (const AnalysisResult *)Data.Data;
    if (Status.Code != NEVERC_STATUS_OK ||
        Data.Length != sizeof(AnalysisResult) || !Observed ||
        Observed->ComputeCount != ExpectedComputes ||
        Observed->InvalidateCount != ExpectedInvalidates ||
        Observed->DestroyCount != ExpectedInvalidates ||
        (ExpectedInvalidates != 0 &&
         Observed->LastInvalidationReason !=
             NEVERC_IR_ANALYSIS_INVALIDATED_BY_PASS))
      return Status.Code == NEVERC_STATUS_OK
                 ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
                 : Status;
  }
#endif

  if (Invocation->Level == NEVERC_IR_PASS_LEVEL_MODULE)
    Status = check_call_graph(Invocation);
  else if (Invocation->Level == NEVERC_IR_PASS_LEVEL_FUNCTION ||
           Invocation->Level == NEVERC_IR_PASS_LEVEL_LOOP)
    Status = check_function_analyses(Invocation);
  else
    Status = neverc_status_ok();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Marker = Invocation->PassID;
  if (!Marker.Data)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Invocation->Core->CreateMetadataString(
      Invocation->Core->Context, Invocation->Task, Marker, &Text);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->CreateMetadataNode(
        Invocation->Core->Context, Invocation->Task, &Text, 1, NEVERC_FALSE,
        &Node);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->GetOrInsertNamedMetadata(
        Invocation->Core->Context, Invocation->Task, Marker, &Named);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->AppendNamedMetadata(
        Invocation->Core->Context, Invocation->Task, Named, Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved),
                            NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
#if defined(NEVERC_TEST_IR_PASS_INVALID_PRESERVE)
  OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;
#else
  OutPreserved->Flags = NEVERC_IR_PRESERVE_NONE;
#if !defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
  if (string_equals(Invocation->PassID, "neverc.test.post_opt",
                    sizeof("neverc.test.post_opt") - 1) != NEVERC_TRUE) {
    OutPreserved->CustomAnalyses = &TestAnalysisID;
    OutPreserved->CustomAnalysisCount = 1;
  }
#else
  if (string_equals(Invocation->PassID, "neverc.test.module",
                    sizeof("neverc.test.module") - 1) == NEVERC_TRUE) {
    OutPreserved->CustomAnalyses = &TestAnalysisDependencyID;
    OutPreserved->CustomAnalysisCount = 1;
  }
#endif
#endif
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL process_begin(const NevercCoreAPI *Core,
                                              void **OutProcessState) {
  (void)Core;
  if (!OutProcessState)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
#if defined(NEVERC_TEST_IR_PASS_LTO_LATE_NVK_REFERENCE)
  LateNvkPassInvocationCount = 0;
#endif
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus register_one(void *RegistrarContext,
                                 NevercStringView PassID,
                                 NevercIRPassLevel Level,
                                 NevercInterfaceID Phase,
                                 NevercBool RequireTestAnalysis) {
  NevercIRPassDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  Descriptor.PassID = PassID;
  Descriptor.Phase = Phase;
  Descriptor.Level = Level;
  Descriptor.Deterministic = NEVERC_TRUE;
  if (RequireTestAnalysis == NEVERC_TRUE) {
#if defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
    Descriptor.RequiredAnalyses = &TestAnalysisDependencyID;
#else
    Descriptor.RequiredAnalyses = &TestAnalysisID;
#endif
    Descriptor.RequiredAnalysisCount = 1;
  }
  Descriptor.Run = run_pass;
  return PassAPI->RegisterPass(PassAPI->Context, RegistrarContext,
                               &Descriptor);
}

static NevercStatus register_analysis(void *RegistrarContext) {
  NevercIRAnalysisDescriptor Descriptor;
  NevercStatus Status;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_ANALYSIS_API_MAJOR,
                            NEVERC_IR_ANALYSIS_API_MINOR, 0};
  Descriptor.AnalysisID = TestAnalysisID;
  Descriptor.Name = STRING_VIEW("neverc.test.analysis");
  Descriptor.Level = NEVERC_IR_PASS_LEVEL_MODULE;
#if defined(NEVERC_TEST_IR_ANALYSIS_CYCLE)
  Descriptor.Dependencies = &TestAnalysisDependencyID;
  Descriptor.DependencyCount = 1;
#elif defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
  Descriptor.UserData = &TestAnalysisResult;
#endif
  Descriptor.Compute = compute_analysis;
  Descriptor.Query = query_analysis;
  Descriptor.Invalidate = invalidate_analysis;
  Descriptor.Destroy = destroy_analysis;
  Status = AnalysisAPI->RegisterAnalysis(AnalysisAPI->Context, RegistrarContext,
                                         &Descriptor);
#if defined(NEVERC_TEST_IR_ANALYSIS_CYCLE)
  if (Status.Code == NEVERC_STATUS_OK) {
    Descriptor.AnalysisID = TestAnalysisDependencyID;
    Descriptor.Name = STRING_VIEW("neverc.test.analysis_dependency");
    Descriptor.Dependencies = &TestAnalysisID;
    Status = AnalysisAPI->RegisterAnalysis(
        AnalysisAPI->Context, RegistrarContext, &Descriptor);
  }
#elif defined(NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER)
  if (Status.Code == NEVERC_STATUS_OK) {
    Descriptor.AnalysisID = TestAnalysisDependencyID;
    Descriptor.Name = STRING_VIEW("neverc.test.analysis_dependency");
    Descriptor.Dependencies = &TestAnalysisID;
    Descriptor.DependencyCount = 1;
    Descriptor.UserData = &TestDependentAnalysisResult;
    Status = AnalysisAPI->RegisterAnalysis(
        AnalysisAPI->Context, RegistrarContext, &Descriptor);
  }
#endif
  return Status;
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *PluginProcessState) {
  NevercStatus Status;
  (void)Core;
  (void)Registrar;
  (void)PluginProcessState;
  Status = register_analysis(RegistrarContext);
#if defined(NEVERC_TEST_IR_PASS_LATE_LITERAL) ||                              \
    defined(NEVERC_TEST_IR_PASS_LATE_NVK_REFERENCE) ||                        \
    defined(NEVERC_TEST_IR_PASS_LTO_LATE_NVK_REFERENCE)
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.pre_codegen"),
        NEVERC_IR_PASS_LEVEL_MODULE,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
                            NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
        NEVERC_FALSE);
  return Status;
#else
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
      RegistrarContext, STRING_VIEW("neverc.test.module"),
      NEVERC_IR_PASS_LEVEL_MODULE,
      (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                          NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
      NEVERC_TRUE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.cgscc"),
        NEVERC_IR_PASS_LEVEL_CGSCC,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                            NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
        NEVERC_FALSE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.function"),
        NEVERC_IR_PASS_LEVEL_FUNCTION,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                            NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
        NEVERC_FALSE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.loop"),
        NEVERC_IR_PASS_LEVEL_LOOP,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                            NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
        NEVERC_FALSE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.pipeline_start"),
        NEVERC_IR_PASS_LEVEL_MODULE,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                            NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW},
        NEVERC_FALSE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.optimizer_last"),
        NEVERC_IR_PASS_LEVEL_MODULE,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
                            NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
        NEVERC_FALSE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.post_opt"),
        NEVERC_IR_PASS_LEVEL_MODULE,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
                            NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
        NEVERC_FALSE);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_one(
        RegistrarContext, STRING_VIEW("neverc.test.pre_codegen"),
        NEVERC_IR_PASS_LEVEL_MODULE,
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
                            NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
        NEVERC_FALSE);
  return Status;
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
  NevercInterfaceID Interface = {NEVERC_INTERFACE_IR_PASS_HIGH,
                                 NEVERC_INTERFACE_IR_PASS_LOW};
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;

  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, NEVERC_IR_PASS_API_MAJOR,
      NEVERC_IR_PASS_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                       sizeof(((NevercIRPassAPI *)0)->RegisterPass))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercIRPassAPI *)Table;

  Interface = (NevercInterfaceID){NEVERC_INTERFACE_IR_ANALYSIS_HIGH,
                                  NEVERC_INTERFACE_IR_ANALYSIS_LOW};
  Table = NULL;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, NEVERC_IR_ANALYSIS_API_MAJOR,
      NEVERC_IR_ANALYSIS_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize < offsetof(NevercIRAnalysisAPI, GetCustomResultData) +
                       sizeof(((NevercIRAnalysisAPI *)0)->GetCustomResultData))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  AnalysisAPI = (const NevercIRAnalysisAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.ir-pass");
  Descriptor.DisplayName = STRING_VIEW("NeverC IR pass test");
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
