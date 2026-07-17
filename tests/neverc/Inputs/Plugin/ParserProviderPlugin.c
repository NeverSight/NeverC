#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPrep.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercASTAPI *ASTAPI;
static const NevercParserAPI *ParserAPI;
static const NevercPrepAPI *PrepAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
provide_ast_unit(const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
                 void *UserData) {
  NevercParserPhaseInput Input;
  NevercParserASTUnitDescriptor Descriptor;
  NevercTokenViewList Tokens;
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
      offsetof(NevercASTAPI, GetTranslationUnit) +
          sizeof(((NevercASTAPI *)0)->GetTranslationUnit),
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
      offsetof(NevercPrepAPI, GetTokenStreamView) +
          sizeof(((NevercPrepAPI *)0)->GetTokenStreamView),
      &Table);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PrepAPI = (const NevercPrepAPI *)Table;

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
