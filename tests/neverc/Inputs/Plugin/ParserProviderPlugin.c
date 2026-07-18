#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercASTAPI *ASTAPI;
static const NevercParserAPI *ParserAPI;
static const NevercPrepAPI *PrepAPI;
static const NevercSemaAPI *SemaAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus set_range(NevercTaskHandle Task,
                              NevercASTBuilderHandle Builder,
                              NevercSourceRange Range) {
  NevercASTValue Value;
  memset(&Value, 0, sizeof(Value));
  Value.Header = (NevercABITableHeader){sizeof(Value), NEVERC_AST_API_MAJOR,
                                        NEVERC_AST_API_MINOR, 0};
  Value.Type = NEVERC_AST_VALUE_SOURCE_RANGE;
  Value.SourceRangeValue = Range;
  return ASTAPI->ASTBuilderSetProperty(
      ASTAPI->Context, Task, Builder, NEVERC_AST_PROPERTY_AST_SOURCE_RANGE,
      &Value);
}

static NevercStatus set_node_property(NevercTaskHandle Task,
                                      NevercASTBuilderHandle Builder,
                                      NevercASTPropertyID Property,
                                      NevercASTValueType Type,
                                      NevercASTNodeHandle Node) {
  NevercASTValue Value;
  memset(&Value, 0, sizeof(Value));
  Value.Header = (NevercABITableHeader){sizeof(Value), NEVERC_AST_API_MAJOR,
                                        NEVERC_AST_API_MINOR, 0};
  Value.Type = Type;
  Value.NodeValue = Node;
  return ASTAPI->ASTBuilderSetProperty(ASTAPI->Context, Task, Builder, Property,
                                       &Value);
}

static NevercStatus finish_builder(NevercTaskHandle Task,
                                   NevercASTBuilderHandle Builder,
                                   NevercASTNodeHandle *OutNode) {
  NevercStatus Status =
      ASTAPI->ASTBuilderCommit(ASTAPI->Context, Task, Builder, OutNode);
  NevercStatus Destroy =
      ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
  return Status.Code == NEVERC_STATUS_OK ? Destroy : Status;
}

static NevercStatus build_integer(NevercTaskHandle Task,
                                  NevercSourceRange Range,
                                  NevercTypeHandle IntType, uint32_t BitWidth,
                                  uint64_t IntegerValue,
                                  NevercExprHandle *OutExpression) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercAPIntView Integer;
  uint64_t Word = IntegerValue;
  NevercStatus Status = ASTAPI->CreateASTBuilder(
      ASTAPI->Context, Task, NEVERC_STMT_KIND_INTEGER_LITERAL, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = set_range(Task, Builder, Range);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = set_node_property(Task, Builder,
                               NEVERC_AST_PROPERTY_STMT_EXPR_TYPE,
                               NEVERC_AST_VALUE_TYPE, IntType);
  memset(&Integer, 0, sizeof(Integer));
  Integer.Header = (NevercABITableHeader){
      sizeof(Integer), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Integer.Words = &Word;
  Integer.WordCount = 1;
  Integer.BitWidth = BitWidth;
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetIntegerValue(ASTAPI->Context, Task, Builder,
                                               &Integer);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
    return Status;
  }
  return finish_builder(Task, Builder, OutExpression);
}

static NevercStatus build_binary_add(NevercTaskHandle Task,
                                     NevercSourceRange Range,
                                     NevercTypeHandle IntType,
                                     NevercExprHandle Left,
                                     NevercExprHandle Right,
                                     NevercExprHandle *OutExpression) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercStatus Status = ASTAPI->CreateASTBuilder(
      ASTAPI->Context, Task, NEVERC_STMT_KIND_BINARY_OPERATOR, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = set_range(Task, Builder, Range);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = set_node_property(Task, Builder,
                               NEVERC_AST_PROPERTY_STMT_EXPR_TYPE,
                               NEVERC_AST_VALUE_TYPE, IntType);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetBinaryOperatorKind(
        ASTAPI->Context, Task, Builder, NEVERC_BINARY_OPERATOR_ADD);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetChild(
        ASTAPI->Context, Task, Builder,
        NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_LHS, 0, Left);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetChild(
        ASTAPI->Context, Task, Builder,
        NEVERC_AST_CHILD_SLOT_STMT_BINARY_OPERATOR_RHS, 0, Right);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
    return Status;
  }
  return finish_builder(Task, Builder, OutExpression);
}

static NevercStatus build_return(NevercTaskHandle Task, NevercSourceRange Range,
                                 NevercExprHandle Expression,
                                 NevercStmtHandle *OutStatement) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercStatus Status = ASTAPI->CreateASTBuilder(
      ASTAPI->Context, Task, NEVERC_STMT_KIND_RETURN_STMT, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = set_range(Task, Builder, Range);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetChild(
        ASTAPI->Context, Task, Builder,
        NEVERC_AST_CHILD_SLOT_STMT_RETURN_STMT_VALUE, 0, Expression);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
    return Status;
  }
  return finish_builder(Task, Builder, OutStatement);
}

static NevercStatus build_null(NevercTaskHandle Task, NevercSourceRange Range,
                               NevercStmtHandle *OutStatement) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercStatus Status = ASTAPI->CreateASTBuilder(
      ASTAPI->Context, Task, NEVERC_STMT_KIND_NULL_STMT, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = set_range(Task, Builder, Range);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
    return Status;
  }
  return finish_builder(Task, Builder, OutStatement);
}

static NevercStatus build_compound(NevercTaskHandle Task,
                                   NevercSourceRange Range,
                                   NevercStmtHandle First,
                                   NevercStmtHandle Second,
                                   NevercStmtHandle *OutCompound) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercStatus Status = ASTAPI->CreateASTBuilder(
      ASTAPI->Context, Task, NEVERC_STMT_KIND_COMPOUND_STMT, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = set_range(Task, Builder, Range);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetChild(
        ASTAPI->Context, Task, Builder,
        NEVERC_AST_CHILD_SLOT_STMT_COMPOUND_STMT_BODY, 0, First);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetChild(
        ASTAPI->Context, Task, Builder,
        NEVERC_AST_CHILD_SLOT_STMT_COMPOUND_STMT_BODY, 1, Second);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
    return Status;
  }
  return finish_builder(Task, Builder, OutCompound);
}

static NevercStatus build_main(NevercTaskHandle Task, NevercSourceRange Range,
                               NevercIdentifierHandle MainIdentifier,
                               NevercTypeHandle FunctionType,
                               NevercStmtHandle Body,
                               NevercDeclHandle *OutFunction) {
  NevercASTBuilderHandle Builder = {0, 0};
  NevercStatus Status = ASTAPI->CreateASTBuilder(
      ASTAPI->Context, Task, NEVERC_DECL_KIND_FUNCTION, &Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = set_range(Task, Builder, Range);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = set_node_property(Task, Builder, NEVERC_AST_PROPERTY_DECL_NAME,
                               NEVERC_AST_VALUE_IDENTIFIER, MainIdentifier);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = set_node_property(Task, Builder, NEVERC_AST_PROPERTY_DECL_TYPE,
                               NEVERC_AST_VALUE_TYPE, FunctionType);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = ASTAPI->ASTBuilderSetChild(
        ASTAPI->Context, Task, Builder,
        NEVERC_AST_CHILD_SLOT_DECL_FUNCTION_BODY, 0, Body);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)ASTAPI->DestroyASTBuilder(ASTAPI->Context, Task, Builder);
    return Status;
  }
  return finish_builder(Task, Builder, OutFunction);
}

static NevercStatus NEVERC_CALL
provide_ast_unit(const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
                 void *UserData) {
  NevercParserPhaseInput Input;
  NevercParserASTUnitDescriptor Descriptor;
  NevercTokenViewList Tokens;
  NevercTokenHandle FirstToken = {0, 0};
  NevercTokenInfo FirstTokenInfo;
  NevercIdentifierHandle MainIdentifier = {0, 0};
  NevercTypeHandle IntType = {0, 0};
  NevercTypeHandle FunctionType = {0, 0};
  NevercTypeInfo IntTypeInfo;
  NevercSemaMutationLeaseHandle Lease = {0, 0};
  NevercSemaFunctionTypeDescriptor FunctionDescriptor;
  NevercExprHandle Integer = {0, 0};
#if defined(NEVERC_TEST_PARSER_PROVIDER_BINARY_REPLAY)
  NevercExprHandle RightInteger = {0, 0};
#endif
  NevercExprHandle ReturnValue = {0, 0};
  NevercStmtHandle Null = {0, 0};
  NevercStmtHandle Return = {0, 0};
  NevercStmtHandle Body = {0, 0};
  NevercDeclHandle MainFunction = {0, 0};
  NevercDeclHandle TranslationUnit = {0, 0};
  NevercArtifactHandle Output = {0, 0};
  NevercStatus Status;
  (void)UserData;

  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PARSER_API_MAJOR,
                                        NEVERC_PARSER_API_MINOR, 0};
  Status = ParserAPI->GetParsePhaseInput(ParserAPI->Context, Frame,
                                         Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK ||
      neverc_handle_is_null(Input.TokenStream))
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;
  memset(&Tokens, 0, sizeof(Tokens));
  Tokens.Header = (NevercABITableHeader){sizeof(Tokens), NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetTokenStreamView(PrepAPI->Context, Frame->Task,
                                       Input.TokenStream, &Tokens);
  if (Status.Code != NEVERC_STATUS_OK || Tokens.Count < 2)
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;

#if !defined(NEVERC_TEST_PARSER_PROVIDER_MISSING_ROOT)
  Status = PrepAPI->GetTokenStreamToken(PrepAPI->Context, Frame->Task,
                                        Input.TokenStream, 0, &FirstToken);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&FirstTokenInfo, 0, sizeof(FirstTokenInfo));
  FirstTokenInfo.Header = (NevercABITableHeader){
      sizeof(FirstTokenInfo), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetTokenInfo(PrepAPI->Context, Frame->Task, FirstToken,
                                 &FirstTokenInfo);
  if (Status.Code != NEVERC_STATUS_OK ||
      neverc_handle_is_null(FirstTokenInfo.Range))
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;

  Status = PrepAPI->GetOrCreateIdentifier(
      PrepAPI->Context, Frame->Task, STRING_VIEW("main"), &MainIdentifier);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = ASTAPI->GetBuiltinType(ASTAPI->Context, Frame->Task,
                                  NEVERC_BUILTIN_TYPE_INT, &IntType);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&IntTypeInfo, 0, sizeof(IntTypeInfo));
  IntTypeInfo.Header = (NevercABITableHeader){
      sizeof(IntTypeInfo), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Status = ASTAPI->GetTypeInfo(ASTAPI->Context, Frame->Task, IntType,
                               &IntTypeInfo);
  if (Status.Code != NEVERC_STATUS_OK || IntTypeInfo.SizeInBits == 0 ||
      IntTypeInfo.SizeInBits > 64)
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)
               : Status;

  Status = SemaAPI->AcquireMutationLease(SemaAPI->Context, Frame->Task, &Lease);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&FunctionDescriptor, 0, sizeof(FunctionDescriptor));
  FunctionDescriptor.Header = (NevercABITableHeader){
      sizeof(FunctionDescriptor), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR,
      0};
  FunctionDescriptor.ResultType = IntType;
  FunctionDescriptor.Variadic = NEVERC_FALSE;
  Status = SemaAPI->CreateFunctionType(SemaAPI->Context, Frame->Task, Lease,
                                       &FunctionDescriptor, &FunctionType);
  {
    NevercStatus Release =
        SemaAPI->ReleaseMutationLease(SemaAPI->Context, Frame->Task, Lease);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Release;
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Status = build_integer(Frame->Task, FirstTokenInfo.Range, IntType,
                         (uint32_t)IntTypeInfo.SizeInBits,
#if defined(NEVERC_TEST_PARSER_PROVIDER_BINARY_REPLAY)
                         UINT64_C(40),
#else
                         UINT64_C(42),
#endif
                         &Integer);
#if defined(NEVERC_TEST_PARSER_PROVIDER_BINARY_REPLAY)
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_integer(Frame->Task, FirstTokenInfo.Range, IntType,
                           (uint32_t)IntTypeInfo.SizeInBits, UINT64_C(2),
                           &RightInteger);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_binary_add(Frame->Task, FirstTokenInfo.Range, IntType,
                              Integer, RightInteger, &ReturnValue);
#else
  ReturnValue = Integer;
#endif
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_null(Frame->Task, FirstTokenInfo.Range, &Null);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        build_return(Frame->Task, FirstTokenInfo.Range, ReturnValue, &Return);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        build_compound(Frame->Task, FirstTokenInfo.Range, Null, Return, &Body);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_main(Frame->Task, FirstTokenInfo.Range, MainIdentifier,
                        FunctionType, Body, &MainFunction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Status = ASTAPI->GetTranslationUnit(ASTAPI->Context, Frame->Task,
                                      &TranslationUnit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif

  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PARSER_API_MAJOR,
                             NEVERC_PARSER_API_MINOR, 0};
  Descriptor.Product =
      (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                          NEVERC_AST_PRODUCT_STANDARD_LOW};
  Descriptor.TranslationUnit = TranslationUnit;
  Status = ParserAPI->CreateASTUnit(ParserAPI->Context, Frame, &Descriptor,
                                    &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL intercept_ast_unit(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercParserPhaseInput Input;
  NevercParserASTUnitInfo Unit;
  NevercTokenViewList Tokens;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !Continuation->InvokeNext || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PARSER_API_MAJOR,
                                        NEVERC_PARSER_API_MINOR, 0};
  Status = ParserAPI->GetParsePhaseInput(ParserAPI->Context, Frame,
                                         Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Tokens, 0, sizeof(Tokens));
  Tokens.Header = (NevercABITableHeader){sizeof(Tokens), NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetTokenStreamView(PrepAPI->Context, Frame->Task,
                                       Input.TokenStream, &Tokens);
  if (Status.Code != NEVERC_STATUS_OK || Tokens.Count < 2)
    return Status.Code == NEVERC_STATUS_OK
               ? failure(NEVERC_STATUS_VERIFICATION_FAILED)
               : Status;

  Status = Continuation->InvokeNext(Continuation, Frame, OutResult);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Unit, 0, sizeof(Unit));
  Unit.Header = (NevercABITableHeader){sizeof(Unit), NEVERC_PARSER_API_MAJOR,
                                       NEVERC_PARSER_API_MINOR, 0};
  Status = ParserAPI->GetASTUnitInfo(ParserAPI->Context, Frame,
                                     OutResult->Output, &Unit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Unit.SemanticState != NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED ||
      Unit.SourceIdentity.Length == 0 || Unit.SourceDigest.Length != 32)
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);
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
#if defined(NEVERC_TEST_PARSER_PHASE_INTERCEPTOR)
  NevercInterceptorDescriptor Interceptor;
#else
  NevercProviderDescriptor Provider;
#endif
  (void)Core;
  (void)PluginProcessState;
  if (!Registrar)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);

#if defined(NEVERC_TEST_PARSER_PHASE_INTERCEPTOR)
  if (!Registrar->RegisterInterceptor)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SYNTAX_PARSE_HIGH,
                          NEVERC_PHASE_SYNTAX_PARSE_LOW};
  Interceptor.Callback = intercept_ast_unit;
  return Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
#else
  if (!Registrar->RegisterProvider)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SYNTAX_PARSE_HIGH,
                          NEVERC_PHASE_SYNTAX_PARSE_LOW};
  Provider.ProviderID = STRING_VIEW("neverc.test.parser-provider");
  Provider.Route.Header =
      (NevercABITableHeader){sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = provide_ast_unit;
  return Registrar->RegisterProvider(RegistrarContext, &Provider);
#endif
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
      offsetof(NevercASTAPI, GetBuiltinType) +
          sizeof(((NevercASTAPI *)0)->GetBuiltinType),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ASTAPI = (const NevercASTAPI *)Table;

  Table = NULL;
  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_PARSER_HIGH,
                          NEVERC_INTERFACE_PARSER_LOW},
      NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR,
      offsetof(NevercParserAPI, CreateASTUnit) +
          sizeof(((NevercParserAPI *)0)->CreateASTUnit),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ParserAPI = (const NevercParserAPI *)Table;

  Table = NULL;
  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH,
                          NEVERC_INTERFACE_PREP_LOW},
      NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR,
      offsetof(NevercPrepAPI, GetOrCreateIdentifier) +
          sizeof(((NevercPrepAPI *)0)->GetOrCreateIdentifier),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PrepAPI = (const NevercPrepAPI *)Table;

  Table = NULL;
  Status = query_interface(
      Bootstrap,
      (NevercInterfaceID){NEVERC_INTERFACE_SEMA_HIGH, NEVERC_INTERFACE_SEMA_LOW},
      NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR,
      offsetof(NevercSemaAPI, CreateFunctionType) +
          sizeof(((NevercSemaAPI *)0)->CreateFunctionType),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SemaAPI = (const NevercSemaAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.parser-provider");
  Descriptor.DisplayName = STRING_VIEW("NeverC parser provider test");
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
