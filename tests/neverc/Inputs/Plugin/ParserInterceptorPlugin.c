#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercASTAPI *ASTAPI;
static const NevercPrepAPI *PrepAPI;
static const NevercParserAPI *ParserAPI;
static const NevercSemaAPI *SemaAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int view_equals(NevercStringView View, const char *Text) {
  const size_t Length = strlen(Text);
  return View.Length == (uint64_t)Length &&
         (Length == 0 || memcmp(View.Data, Text, Length) == 0);
}

static void initialize_result(NevercPhaseResult *Result) {
  memset(Result, 0, sizeof(*Result));
  Result->Header = (NevercABITableHeader){
      sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_CONTINUE;
}

static NevercStatus invoke_next(const NevercPhaseFrame *Frame,
                                NevercPhaseContinuation *Continuation,
                                NevercPhaseResult *OutResult) {
  NevercPhaseResult Downstream;
  initialize_result(OutResult);
  initialize_result(&Downstream);
  return Continuation->InvokeNext(Continuation, Frame, &Downstream);
}

static NevercStatus get_input(const NevercPhaseFrame *Frame,
                              NevercParserExtensionInput *OutInput) {
  memset(OutInput, 0, sizeof(*OutInput));
  OutInput->Header = (NevercABITableHeader){
      sizeof(*OutInput), NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR, 0};
  return ParserAPI->GetExtensionInput(ParserAPI->Context, Frame, Frame->Input,
                                      OutInput);
}

static NevercStatus get_token_info(const NevercPhaseFrame *Frame,
                                   NevercParserTokenCursorHandle Cursor,
                                   uint64_t Offset, NevercTokenInfo *OutInfo) {
  NevercTokenHandle Token = {0, 0};
  NevercStatus Status = ParserAPI->CursorPeek(ParserAPI->Context, Frame->Task,
                                              Cursor, Offset, &Token);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutInfo, 0, sizeof(*OutInfo));
  OutInfo->Header = (NevercABITableHeader){
      sizeof(*OutInfo), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  return PrepAPI->GetTokenInfo(PrepAPI->Context, Frame->Task, Token, OutInfo);
}

static NevercStatus consume(const NevercPhaseFrame *Frame,
                            NevercParserTokenCursorHandle Cursor) {
  NevercTokenHandle Consumed = {0, 0};
  return ParserAPI->CursorConsume(ParserAPI->Context, Frame->Task, Cursor,
                                  &Consumed);
}

static NevercStatus verify_sema_lifecycle(const NevercPhaseFrame *Frame) {
  NevercSemaScopeHandle Scope = {0, 0};
  NevercSemaScopeInfo Info;
  NevercStatus Status = SemaAPI->GetCurrentScope(
      SemaAPI->Context, Frame->Task, &Scope);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Info, 0, sizeof(Info));
  Info.Header = (NevercABITableHeader){
      sizeof(Info), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  return SemaAPI->GetScopeInfo(SemaAPI->Context, Frame->Task, Scope, &Info);
}

static NevercStatus build_simple_node(const NevercPhaseFrame *Frame,
                                      NevercASTNodeKind Kind,
                                      NevercSourceRange Range, uint64_t Number,
                                      NevercASTNodeHandle *OutNode) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercStatus Status =
      ASTAPI->CreateASTBuilder(ASTAPI->Context, Frame->Task, Kind, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercASTValue RangeValue;
  memset(&RangeValue, 0, sizeof(RangeValue));
  RangeValue.Header = (NevercABITableHeader){
      sizeof(RangeValue), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  RangeValue.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
  RangeValue.SourceRangeValue = Range;
  Status = ASTAPI->ASTBuilderSetProperty(ASTAPI->Context, Frame->Task, Builder,
                                         NEVERC_AST_PROPERTY_AST_SOURCE_RANGE,
                                         &RangeValue);

  if (Status.Code == NEVERC_STATUS_OK &&
      Kind == NEVERC_STMT_KIND_INTEGER_LITERAL) {
    NevercTypeHandle IntType = {0, 0};
    Status = ASTAPI->GetBuiltinType(ASTAPI->Context, Frame->Task,
                                    NEVERC_BUILTIN_TYPE_INT, &IntType);
    if (Status.Code == NEVERC_STATUS_OK) {
      NevercASTValue TypeValue;
      memset(&TypeValue, 0, sizeof(TypeValue));
      TypeValue.Header = (NevercABITableHeader){
          sizeof(TypeValue), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
      TypeValue.Type = NEVERC_AST_VALUE_TYPE;
      TypeValue.NodeValue = IntType;
      Status = ASTAPI->ASTBuilderSetProperty(
          ASTAPI->Context, Frame->Task, Builder,
          NEVERC_AST_PROPERTY_STMT_EXPR_TYPE, &TypeValue);
    }
    if (Status.Code == NEVERC_STATUS_OK) {
      NevercAPIntView Integer;
      memset(&Integer, 0, sizeof(Integer));
      Integer.Header = (NevercABITableHeader){
          sizeof(Integer), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
      Integer.Words = &Number;
      Integer.WordCount = 1;
      Integer.BitWidth = 32;
      Status = ASTAPI->ASTBuilderSetIntegerValue(ASTAPI->Context, Frame->Task,
                                                 Builder, &Integer);
    }
  }

  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderCommit(ASTAPI->Context, Frame->Task, Builder,
                                      OutNode);
  {
    NevercStatus Destroy =
        ASTAPI->DestroyASTBuilder(ASTAPI->Context, Frame->Task, Builder);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Destroy;
  }
  return Status;
}

static NevercStatus return_node(const NevercPhaseFrame *Frame,
                                NevercPhaseContinuation *Continuation,
                                NevercParserResultKind Kind,
                                NevercASTNodeHandle Node,
                                NevercPhaseResult *OutResult) {
  NevercParserExtensionOutput Output;
  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR, 0};
  Output.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Output.ResultKind = Kind;
  Output.Node = Node;
  initialize_result(OutResult);
  {
    NevercStatus Status = ParserAPI->CreateExtensionOutput(
        ParserAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
    if (Status.Code == NEVERC_STATUS_OK)
      OutResult->Action = NEVERC_PHASE_REPLACE;
    return Status;
  }
}

static NevercStatus NEVERC_CALL intercept_declaration(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserExtensionInput Input;
  NevercTokenInfo First;
  NevercASTNodeHandle Node = {0, 0};
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = get_input(Frame, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = get_token_info(Frame, Input.Cursor, 0, &First);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!view_equals(First.Spelling, "__neverc_test_decl"))
    return invoke_next(Frame, Continuation, OutResult);
  Status = verify_sema_lifecycle(Frame);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        build_simple_node(Frame, NEVERC_DECL_KIND_EMPTY, First.Range, 0, &Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return return_node(Frame, Continuation, NEVERC_PARSER_RESULT_DECL, Node,
                     OutResult);
}

static NevercStatus NEVERC_CALL intercept_statement(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserExtensionInput Input;
  NevercTokenInfo First;
  NevercASTNodeHandle Node = {0, 0};
  NevercStatus Status;
  (void)UserData;
  Status = get_input(Frame, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = get_token_info(Frame, Input.Cursor, 0, &First);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!view_equals(First.Spelling, "__neverc_test_stmt"))
    return invoke_next(Frame, Continuation, OutResult);
  Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_simple_node(Frame, NEVERC_STMT_KIND_NULL_STMT, First.Range,
                               0, &Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return return_node(Frame, Continuation, NEVERC_PARSER_RESULT_STMT, Node,
                     OutResult);
}

static NevercStatus NEVERC_CALL intercept_expression(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserExtensionInput Input;
  NevercParserCheckpointHandle Checkpoint = {0, 0};
  NevercTokenInfo First;
  NevercASTNodeHandle Node = {0, 0};
  NevercStatus Status;
  (void)UserData;
  Status = get_input(Frame, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = get_token_info(Frame, Input.Cursor, 0, &First);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  if (view_equals(First.Spelling, "__neverc_test_probe")) {
    Status = ParserAPI->CursorCheckpoint(ParserAPI->Context, Frame->Task,
                                         Input.Cursor, &Checkpoint);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = consume(Frame, Input.Cursor);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = ParserAPI->CursorRollback(ParserAPI->Context, Frame->Task,
                                         Input.Cursor, Checkpoint);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return invoke_next(Frame, Continuation, OutResult);
  }

  if (view_equals(First.Spelling, "__neverc_test_error")) {
    Status = consume(Frame, Input.Cursor);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = consume(Frame, Input.Cursor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return failure(NEVERC_STATUS_CANCELLED);
  }

  if (!view_equals(First.Spelling, "__neverc_test_expr"))
    return invoke_next(Frame, Continuation, OutResult);
  Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_simple_node(Frame, NEVERC_STMT_KIND_INTEGER_LITERAL,
                               First.Range, 40, &Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return return_node(Frame, Continuation, NEVERC_PARSER_RESULT_EXPR, Node,
                     OutResult);
}

static NevercStatus NEVERC_CALL intercept_type(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserExtensionInput Input;
  NevercTokenInfo First;
  NevercTypeHandle Type = {0, 0};
  NevercStatus Status;
  (void)UserData;
  Status = get_input(Frame, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = get_token_info(Frame, Input.Cursor, 0, &First);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!view_equals(First.Spelling, "__neverc_test_type"))
    return invoke_next(Frame, Continuation, OutResult);
  Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->GetBuiltinType(ASTAPI->Context, Frame->Task,
                                    NEVERC_BUILTIN_TYPE_INT, &Type);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return return_node(Frame, Continuation, NEVERC_PARSER_RESULT_TYPE, Type,
                     OutResult);
}

static NevercStatus NEVERC_CALL intercept_keyword(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserExtensionInput Input;
  NevercTokenInfo First;
  NevercASTNodeHandle Node = {0, 0};
  NevercStatus Status;
  (void)UserData;
  Status = get_input(Frame, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = get_token_info(Frame, Input.Cursor, 0, &First);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Input.ExpectedResult != NEVERC_PARSER_RESULT_EXPR ||
      !view_equals(First.Spelling, "__neverc_test_keyword"))
    return invoke_next(Frame, Continuation, OutResult);
  Status = consume(Frame, Input.Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_simple_node(Frame, NEVERC_STMT_KIND_INTEGER_LITERAL,
                               First.Range, 2, &Node);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return return_node(Frame, Continuation, NEVERC_PARSER_RESULT_EXPR, Node,
                     OutResult);
}

static NevercStatus expect_kind(const NevercPhaseFrame *Frame,
                                NevercParserTokenCursorHandle Cursor,
                                NevercTokenKind Kind, const char *Spelling) {
  NevercTokenInfo Info;
  NevercStatus Status = get_token_info(Frame, Cursor, 0, &Info);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Info.Kind != Kind || (Spelling && !view_equals(Info.Spelling, Spelling)))
    return failure(NEVERC_STATUS_NOT_FOUND);
  return consume(Frame, Cursor);
}

static NevercStatus NEVERC_CALL intercept_attribute(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserExtensionInput Input;
  NevercParserCheckpointHandle Checkpoint = {0, 0};
  NevercTokenInfo First;
  NevercParserParsedAttributeDescriptor Attribute;
  NevercAttrHandle Parsed = {0, 0};
  NevercStatus Status;
  (void)UserData;
  Status = get_input(Frame, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = get_token_info(Frame, Input.Cursor, 0, &First);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = ParserAPI->CursorCheckpoint(ParserAPI->Context, Frame->Task,
                                       Input.Cursor, &Checkpoint);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_L_SQUARE, NULL);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_L_SQUARE, NULL);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_IDENTIFIER, "neverc");
  if (Status.Code == NEVERC_STATUS_OK)
    Status = expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_COLONCOLON, NULL);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_IDENTIFIER,
                         "plugin_unused");
  if (Status.Code == NEVERC_STATUS_OK)
    Status = expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_R_SQUARE, NULL);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = expect_kind(Frame, Input.Cursor, NEVERC_TOKEN_R_SQUARE, NULL);

  if (Status.Code == NEVERC_STATUS_NOT_FOUND) {
    Status = ParserAPI->CursorRollback(ParserAPI->Context, Frame->Task,
                                       Input.Cursor, Checkpoint);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return invoke_next(Frame, Continuation, OutResult);
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Attribute, 0, sizeof(Attribute));
  Attribute.Header = (NevercABITableHeader){
      sizeof(Attribute), NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR, 0};
  Attribute.Name = STRING_VIEW("maybe_unused");
  Attribute.Range = First.Range;
  Attribute.Form = NEVERC_PARSER_ATTRIBUTE_C23;
  Status = ParserAPI->CreateParsedAttribute(ParserAPI->Context, Frame->Task,
                                            Input.Cursor, &Attribute, &Parsed);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ParserAPI->CursorCommit(ParserAPI->Context, Frame->Task,
                                     Input.Cursor, Checkpoint);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return return_node(Frame, Continuation, NEVERC_PARSER_RESULT_ATTRIBUTE,
                     Parsed, OutResult);
}

static NevercStatus register_interceptor(const NevercRegistrarAPI *Registrar,
                                         void *RegistrarContext,
                                         NevercInterfaceID Phase,
                                         NevercPhaseInterceptorFn Callback) {
  NevercInterceptorDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = Phase;
  Descriptor.Callback = Callback;
  return Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
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
  NevercStatus Status;
  (void)Core;
  (void)PluginProcessState;
#define REGISTER_PHASE(Symbol, Callback)                                       \
  do {                                                                         \
    Status =                                                                   \
        register_interceptor(Registrar, RegistrarContext,                      \
                             (NevercInterfaceID){NEVERC_PHASE_##Symbol##_HIGH, \
                                                 NEVERC_PHASE_##Symbol##_LOW}, \
                             Callback);                                        \
    if (Status.Code != NEVERC_STATUS_OK)                                       \
      return Status;                                                           \
  } while (0)
  REGISTER_PHASE(SYNTAX_EXTENSION_DECLARATION, intercept_declaration);
  REGISTER_PHASE(SYNTAX_EXTENSION_STATEMENT, intercept_statement);
  REGISTER_PHASE(SYNTAX_EXTENSION_EXPRESSION, intercept_expression);
  REGISTER_PHASE(SYNTAX_EXTENSION_TYPE_NAME, intercept_type);
  REGISTER_PHASE(SYNTAX_EXTENSION_ATTRIBUTE, intercept_attribute);
  REGISTER_PHASE(SYNTAX_EXTENSION_KEYWORD, intercept_keyword);
#undef REGISTER_PHASE
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                               void *PluginProcessState) {
  (void)Core;
  (void)PluginProcessState;
  return neverc_status_ok();
}

static NevercStatus query_interface(const NevercBootstrapAPI *Bootstrap,
                                    NevercInterfaceID Interface, uint16_t Major,
                                    uint16_t Minor, const void **OutTable,
                                    uint64_t RequiredSize) {
  uint16_t NegotiatedMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status =
      Bootstrap->QueryInterface(Bootstrap->Context, Interface, Major, Minor,
                                OutTable, &NegotiatedMinor, &StructSize);
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
  NevercStatus Status;
  uint32_t Capacity;
  size_t BytesToWrite;
  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_AST_HIGH, NEVERC_INTERFACE_AST_LOW},
      NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, &Table,
      offsetof(NevercASTAPI, GetBuiltinType) +
          sizeof(((NevercASTAPI *)0)->GetBuiltinType));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ASTAPI = (const NevercASTAPI *)Table;

  Status = query_interface(Bootstrap,
                           (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH,
                                               NEVERC_INTERFACE_PREP_LOW},
                           NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table,
                           offsetof(NevercPrepAPI, GetTokenInfo) +
                               sizeof(((NevercPrepAPI *)0)->GetTokenInfo));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PrepAPI = (const NevercPrepAPI *)Table;

  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_PARSER_HIGH,
                          NEVERC_INTERFACE_PARSER_LOW},
      NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR, &Table,
      offsetof(NevercParserAPI, CreateParsedAttribute) +
          sizeof(((NevercParserAPI *)0)->CreateParsedAttribute));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ParserAPI = (const NevercParserAPI *)Table;

  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_SEMA_HIGH,
                          NEVERC_INTERFACE_SEMA_LOW},
      NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, &Table,
      offsetof(NevercSemaAPI, GetScopeInfo) +
          sizeof(((NevercSemaAPI *)0)->GetScopeInfo));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SemaAPI = (const NevercSemaAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.parser");
  Descriptor.DisplayName = STRING_VIEW("NeverC parser interceptor test plugin");
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
