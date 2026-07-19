#include "neverc/Plugin/PluginAST.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Literal)                                                   \
  (NevercStringView) { (Literal), (uint64_t)(sizeof(Literal) - 1) }

typedef struct ASTRewriteState {
  const NevercCoreAPI *Core;
  const NevercASTAPI *AST;
  const NevercParserAPI *Parser;
} ASTRewriteState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int string_equals(NevercStringView Value, const char *Expected) {
  const size_t Length = strlen(Expected);
  return Value.Data && Value.Length == Length &&
         memcmp(Value.Data, Expected, Length) == 0;
}

static void emit_diagnostic(const ASTRewriteState *State,
                            NevercDiagnosticSeverity Severity, uint32_t Code,
                            const char *Message) {
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle Handle = {0, 0};
  if (!State || !State->Core || !State->Core->EmitDiagnostic)
    return;
  memset(&Diagnostic, 0, sizeof(Diagnostic));
  Diagnostic.Header = (NevercABITableHeader){
      sizeof(Diagnostic), NEVERC_CORE_API_MAJOR, NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = Severity;
  Diagnostic.Code = Code;
  Diagnostic.PluginID = STRING_VIEW("org.neverc.example.ast-rewrite");
  Diagnostic.PhaseID = STRING_VIEW(NEVERC_PHASE_SYNTAX_PARSE_NAME);
  Diagnostic.Message =
      (NevercStringView){Message, (uint64_t)strlen(Message)};
  (void)State->Core->EmitDiagnostic(State->Core->Context, &Diagnostic,
                                    &Handle);
}

static NevercStatus report_failure(const ASTRewriteState *State,
                                   NevercStatus Status,
                                   const char *Message) {
  if (Status.Code != NEVERC_STATUS_OK)
    emit_diagnostic(State, NEVERC_DIAGNOSTIC_ERROR, UINT32_C(4201), Message);
  return Status;
}

static NevercStatus query_interface(
    const NevercCoreAPI *Core, NevercInterfaceID Interface, uint16_t Major,
    uint16_t Minor, size_t RequiredSize, const void **OutTable) {
  uint16_t ActualMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status =
      Core->QueryInterface(Core->Context, Interface, Major, Minor, OutTable,
                           &ActualMinor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!*OutTable || StructSize < RequiredSize)
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

static NevercStatus find_rewrite_target(
    const ASTRewriteState *State, NevercTaskHandle Task,
    NevercDeclHandle TranslationUnit, NevercDeclHandle *OutTarget,
    NevercVarDeclInfo *OutVariable, NevercASTNodeInfo *OutNode) {
  uint64_t ChildCount = 0;
  uint64_t Index;
  NevercStatus Status;
  *OutTarget = (NevercDeclHandle){0, 0};

  Status = State->AST->GetChildCount(State->AST->Context, Task,
                                     TranslationUnit, &ChildCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (Index = 0; Index != ChildCount; ++Index) {
    NevercASTNodeHandle Child = {0, 0};
    NevercVarDeclInfo Variable;
    memset(&Variable, 0, sizeof(Variable));
    Variable.Header = (NevercABITableHeader){
        sizeof(Variable), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};

    Status = State->AST->GetChild(State->AST->Context, Task, TranslationUnit,
                                  Index, &Child);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = State->AST->GetVarDeclInfo(State->AST->Context, Task, Child,
                                        &Variable);
    if (Status.Code == NEVERC_STATUS_WRONG_TYPE)
      continue;
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (!string_equals(Variable.Name, "neverc_rewrite_target"))
      continue;

    memset(OutNode, 0, sizeof(*OutNode));
    OutNode->Header = (NevercABITableHeader){
        sizeof(*OutNode), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
    Status =
        State->AST->GetNodeInfo(State->AST->Context, Task, Child, OutNode);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutTarget = Child;
    *OutVariable = Variable;
    return neverc_status_ok();
  }
  return neverc_status_ok();
}

static NevercStatus build_integer_literal(
    const ASTRewriteState *State, NevercTaskHandle Task,
    const NevercVarDeclInfo *Variable, const NevercASTNodeInfo *Node,
    uint64_t Number, NevercExprHandle *OutExpression) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercASTValue Type;
  NevercASTValue Range;
  NevercAPIntView Integer;
  NevercStatus Status;
  NevercStatus DestroyStatus;

  *OutExpression = (NevercExprHandle){0, 0};
  Status = State->AST->CreateASTBuilder(
      State->AST->Context, Task, NEVERC_STMT_KIND_INTEGER_LITERAL, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Type, 0, sizeof(Type));
  Type.Header = (NevercABITableHeader){
      sizeof(Type), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Type.Type = NEVERC_AST_VALUE_TYPE;
  Type.NodeValue = Variable->Type;
  Status = State->AST->ASTBuilderSetProperty(
      State->AST->Context, Task, Builder,
      NEVERC_AST_PROPERTY_STMT_EXPR_TYPE, &Type);
  if (Status.Code != NEVERC_STATUS_OK)
    goto cleanup;

  memset(&Range, 0, sizeof(Range));
  Range.Header = (NevercABITableHeader){
      sizeof(Range), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Range.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
  Range.SourceRangeValue = Node->SourceRange;
  Status = State->AST->ASTBuilderSetProperty(
      State->AST->Context, Task, Builder,
      NEVERC_AST_PROPERTY_AST_SOURCE_RANGE, &Range);
  if (Status.Code != NEVERC_STATUS_OK)
    goto cleanup;

  memset(&Integer, 0, sizeof(Integer));
  Integer.Header = (NevercABITableHeader){
      sizeof(Integer), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Integer.Words = &Number;
  Integer.WordCount = 1;
  Integer.BitWidth = 32;
  Status = State->AST->ASTBuilderSetIntegerValue(
      State->AST->Context, Task, Builder, &Integer);
  if (Status.Code != NEVERC_STATUS_OK)
    goto cleanup;

  Status = State->AST->ASTBuilderCommit(
      State->AST->Context, Task, Builder, OutExpression);

cleanup:
  DestroyStatus = State->AST->DestroyASTBuilder(
      State->AST->Context, Task, Builder);
  return Status.Code == NEVERC_STATUS_OK ? DestroyStatus : Status;
}

static NevercStatus replace_initializer(const ASTRewriteState *State,
                                        NevercTaskHandle Task,
                                        NevercDeclHandle Target,
                                        NevercExprHandle Replacement) {
  NevercASTMutationHandle Mutation = {0, 0};
  NevercStatus Status = State->AST->BeginASTMutation(
      State->AST->Context, Task, &Mutation);
  NevercStatus CleanupStatus;
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Status = State->AST->ASTMutationReplaceChild(
      State->AST->Context, Task, Mutation, Target,
      NEVERC_AST_CHILD_SLOT_DECL_VAR_INITIALIZER, 0, Replacement);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        State->AST->CommitASTMutation(State->AST->Context, Task, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    (void)State->AST->AbortASTMutation(State->AST->Context, Task, Mutation);
  CleanupStatus = State->AST->DestroyASTMutation(
      State->AST->Context, Task, Mutation);
  return Status.Code == NEVERC_STATUS_OK ? CleanupStatus : Status;
}

static NevercStatus verify_initializer(const ASTRewriteState *State,
                                       NevercTaskHandle Task,
                                       NevercDeclHandle Target,
                                       uint64_t Expected) {
  NevercVarDeclInfo Variable;
  NevercIntegerLiteralInfo Literal;
  uint64_t Word = 0;
  NevercStatus Status;
  memset(&Variable, 0, sizeof(Variable));
  Variable.Header = (NevercABITableHeader){
      sizeof(Variable), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Status = State->AST->GetVarDeclInfo(State->AST->Context, Task, Target,
                                      &Variable);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Literal, 0, sizeof(Literal));
  Literal.Header = (NevercABITableHeader){
      sizeof(Literal), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Status = State->AST->GetIntegerLiteralInfo(
      State->AST->Context, Task, Variable.Initializer, &Literal);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = State->AST->GetIntegerLiteralWord(
      State->AST->Context, Task, Variable.Initializer, 0, &Word);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Literal.WordCount == 1 && Word == Expected
             ? neverc_status_ok()
             : failure(NEVERC_STATUS_VERIFICATION_FAILED);
}

static NevercStatus NEVERC_CALL
rewrite_ast(const NevercPhaseFrame *Frame,
            NevercPhaseContinuation *Continuation,
            NevercPhaseResult *OutResult, void *UserData) {
  ASTRewriteState *State = (ASTRewriteState *)UserData;
  NevercParserASTUnitInfo Unit;
  NevercDeclHandle Target;
  NevercVarDeclInfo Variable;
  NevercASTNodeInfo Node;
  NevercExprHandle Replacement;
  NevercStatus Status;
  if (!State || !Frame || !Continuation || !Continuation->InvokeNext ||
      !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = Continuation->InvokeNext(Continuation, Frame, OutResult);
  if (Status.Code != NEVERC_STATUS_OK)
    return report_failure(State, Status,
                          "the built-in parser failed before AST rewrite");

  memset(&Unit, 0, sizeof(Unit));
  Unit.Header = (NevercABITableHeader){
      sizeof(Unit), NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR, 0};
  Status = State->Parser->GetASTUnitInfo(
      State->Parser->Context, Frame, OutResult->Output, &Unit);
  if (Status.Code != NEVERC_STATUS_OK)
    return report_failure(State, Status,
                          "unable to inspect the parser AST unit");

  memset(&Variable, 0, sizeof(Variable));
  memset(&Node, 0, sizeof(Node));
  Status = find_rewrite_target(State, Frame->Task, Unit.TranslationUnit,
                               &Target, &Variable, &Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return report_failure(State, Status,
                          "unable to find the AST rewrite target");
  if (neverc_handle_is_null(Target) ||
      neverc_handle_is_null(Variable.Initializer))
    goto continue_phase;

  Status = build_integer_literal(State, Frame->Task, &Variable, &Node, 42,
                                 &Replacement);
  if (Status.Code != NEVERC_STATUS_OK)
    return report_failure(State, Status,
                          "unable to build the replacement integer literal");
  Status = replace_initializer(State, Frame->Task, Target, Replacement);
  if (Status.Code != NEVERC_STATUS_OK)
    return report_failure(State, Status,
                          "AST mutation verifier rejected the replacement");
  Status = verify_initializer(State, Frame->Task, Target, 42);
  if (Status.Code != NEVERC_STATUS_OK)
    return report_failure(State, Status,
                          "committed AST rewrite could not be verified");
  emit_diagnostic(State, NEVERC_DIAGNOSTIC_REMARK, UINT32_C(4200),
                  "rewrote neverc_rewrite_target initializer to 42");

continue_phase:
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  ASTRewriteState *State = NULL;
  const void *Table = NULL;
  NevercStatus Status;
  if (!Core || !Core->Allocate || !Core->Deallocate || !OutProcessState)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;

  Status = Core->Allocate(Core->Context, sizeof(*State), _Alignof(ASTRewriteState),
                          (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(State, 0, sizeof(*State));
  State->Core = Core;

  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_AST_HIGH, NEVERC_INTERFACE_AST_LOW},
      NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR,
      offsetof(NevercASTAPI, DestroyASTMutation) +
          sizeof(((NevercASTAPI *)0)->DestroyASTMutation),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    goto failure;
  State->AST = (const NevercASTAPI *)Table;

  Table = NULL;
  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_PARSER_HIGH,
                          NEVERC_INTERFACE_PARSER_LOW},
      NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR,
      offsetof(NevercParserAPI, GetASTUnitInfo) +
          sizeof(((NevercParserAPI *)0)->GetASTUnitInfo),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    goto failure;
  State->Parser = (const NevercParserAPI *)Table;
  *OutProcessState = State;
  return neverc_status_ok();

failure:
  (void)Core->Deallocate(Core->Context, State, sizeof(*State),
                         _Alignof(ASTRewriteState));
  return Status;
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  NevercInterceptorDescriptor Interceptor;
  (void)Core;
  if (!Registrar || !Registrar->RegisterInterceptor || !ProcessState)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SYNTAX_PARSE_HIGH,
                          NEVERC_PHASE_SYNTAX_PARSE_LOW};
  Interceptor.Callback = rewrite_ast;
  Interceptor.UserData = ProcessState;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  if (!ProcessState)
    return neverc_status_ok();
  return Core->Deallocate(Core->Context, ProcessState,
                          sizeof(ASTRewriteState), _Alignof(ASTRewriteState));
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;
  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.example.ast-rewrite");
  Descriptor.DisplayName = STRING_VIEW("NeverC safe AST rewrite example");
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
