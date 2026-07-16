#include "neverc/Plugin/PluginPrep.h"
#include <stddef.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercPrepAPI *PrepAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus build_identifier(const NevercPhaseFrame *Frame,
                                     NevercSourceLocation Location,
                                     NevercTokenFlags Flags,
                                     NevercStringView Name,
                                     NevercTokenHandle *OutToken) {
  NevercIdentifierHandle Identifier = {0, 0};
  NevercTokenBuilderHandle Builder = {0, 0};
  NevercStatus Status = PrepAPI->GetOrCreateIdentifier(
      PrepAPI->Context, Frame->Task, Name, &Identifier);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        PrepAPI->CreateTokenBuilder(PrepAPI->Context, Frame->Task, &Builder);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetIdentifier(PrepAPI->Context, Frame->Task,
                                                Builder, Identifier);
#if !defined(NEVERC_TEST_PREP_PROVIDER_INVALID_MAPPING)
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetLocation(PrepAPI->Context, Frame->Task,
                                              Builder, Location);
#else
  (void)Location;
#endif
  if (Status.Code == NEVERC_STATUS_OK && Flags != 0)
    Status = PrepAPI->TokenBuilderSetFlags(PrepAPI->Context, Frame->Task,
                                           Builder, Flags);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderCommit(PrepAPI->Context, Frame->Task, Builder,
                                         OutToken);
  if (!neverc_handle_is_null(Builder))
    (void)PrepAPI->DestroyTokenBuilder(PrepAPI->Context, Frame->Task, Builder);
  return Status;
}

static NevercStatus build_kind(const NevercPhaseFrame *Frame,
                               NevercSourceLocation Location,
                               NevercTokenKind Kind,
                               NevercTokenHandle *OutToken) {
  NevercTokenBuilderHandle Builder = {0, 0};
  NevercStatus Status =
      PrepAPI->CreateTokenBuilder(PrepAPI->Context, Frame->Task, &Builder);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetKind(PrepAPI->Context, Frame->Task,
                                          Builder, Kind);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetLocation(PrepAPI->Context, Frame->Task,
                                              Builder, Location);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderCommit(PrepAPI->Context, Frame->Task, Builder,
                                         OutToken);
  if (!neverc_handle_is_null(Builder))
    (void)PrepAPI->DestroyTokenBuilder(PrepAPI->Context, Frame->Task, Builder);
  return Status;
}

static NevercStatus build_literal(const NevercPhaseFrame *Frame,
                                  NevercSourceLocation Location,
                                  NevercStringView Spelling,
                                  NevercTokenHandle *OutToken) {
  NevercTokenBuilderHandle Builder = {0, 0};
  NevercStatus Status =
      PrepAPI->CreateTokenBuilder(PrepAPI->Context, Frame->Task, &Builder);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetLiteral(
        PrepAPI->Context, Frame->Task, Builder, NEVERC_TOKEN_NUMERIC_CONSTANT,
        Spelling);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetLocation(PrepAPI->Context, Frame->Task,
                                              Builder, Location);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderCommit(PrepAPI->Context, Frame->Task, Builder,
                                         OutToken);
  if (!neverc_handle_is_null(Builder))
    (void)PrepAPI->DestroyTokenBuilder(PrepAPI->Context, Frame->Task, Builder);
  return Status;
}

static NevercStatus NEVERC_CALL
provide_token_stream(const NevercPhaseFrame *Frame,
                     NevercPhaseResult *OutResult, void *UserData) {
  NevercPrepTokenStreamPhaseInput Input;
  NevercTokenStreamBuilderHandle Builder = {0, 0};
  NevercTokenHandle Tokens[11];
  NevercArtifactHandle Output = {0, 0};
  uint64_t TokenCount = 11;
  NevercStatus Status;
  (void)UserData;

  if (!Frame || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
#if defined(NEVERC_TEST_PREP_PROVIDER_ERROR)
  return failure(NEVERC_STATUS_PLUGIN_FAILURE);
#endif
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PREP_API_MAJOR,
                                        NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetTokenStreamPhaseInput(PrepAPI->Context, Frame,
                                             Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Status = build_identifier(Frame, Input.StartLocation,
                            NEVERC_TOKEN_FLAG_START_OF_LINE, STRING_VIEW("int"),
                            &Tokens[0]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_identifier(Frame, Input.StartLocation, 0,
                              STRING_VIEW("plugin_provider"), &Tokens[1]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_kind(Frame, Input.StartLocation, NEVERC_TOKEN_L_PAREN,
                        &Tokens[2]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_identifier(Frame, Input.StartLocation, 0,
                              STRING_VIEW("void"), &Tokens[3]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_kind(Frame, Input.StartLocation, NEVERC_TOKEN_R_PAREN,
                        &Tokens[4]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_kind(Frame, Input.StartLocation, NEVERC_TOKEN_L_BRACE,
                        &Tokens[5]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_identifier(Frame, Input.StartLocation, 0,
                              STRING_VIEW("return"), &Tokens[6]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        build_literal(Frame, Input.StartLocation, STRING_VIEW("0"), &Tokens[7]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        build_kind(Frame, Input.StartLocation, NEVERC_TOKEN_SEMI, &Tokens[8]);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = build_kind(Frame, Input.StartLocation, NEVERC_TOKEN_R_BRACE,
                        &Tokens[9]);
#if defined(NEVERC_TEST_PREP_PROVIDER_OMIT_EOF)
  TokenCount = 10;
#else
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        build_kind(Frame, Input.EndLocation, NEVERC_TOKEN_EOF, &Tokens[10]);
#endif
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Status = PrepAPI->CreateTokenStreamBuilder(PrepAPI->Context, Frame->Task,
                                             &Builder);
#if defined(NEVERC_TEST_PREP_PROVIDER_OVER_LIMIT)
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenStreamBuilderAppend(PrepAPI->Context, Frame->Task,
                                               Builder, Tokens,
                                               Input.MaximumTokenCount + 1);
#else
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenStreamBuilderAppend(PrepAPI->Context, Frame->Task,
                                               Builder, Tokens, 6);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenStreamBuilderAppend(
        PrepAPI->Context, Frame->Task, Builder, Tokens + 6, TokenCount - 6);
#endif
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenStreamBuilderCommit(PrepAPI->Context, Frame, Builder,
                                               &Output);
  if (!neverc_handle_is_null(Builder))
    (void)PrepAPI->DestroyTokenStreamBuilder(PrepAPI->Context, Frame->Task,
                                             Builder);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
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
  NevercProviderDescriptor Provider;
  (void)Core;
  (void)PluginProcessState;
  if (!Registrar || !Registrar->RegisterProvider)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase =
      (NevercInterfaceID){NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_HIGH,
                          NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_LOW};
  Provider.ProviderID = STRING_VIEW("neverc.test.preprocessor-provider");
  Provider.Route.Header =
      (NevercABITableHeader){sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = provide_token_stream;
  return Registrar->RegisterProvider(RegistrarContext, &Provider);
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
  NevercInterfaceID Interface = {NEVERC_INTERFACE_PREP_HIGH,
                                 NEVERC_INTERFACE_PREP_LOW};
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
      Bootstrap->Context, Interface, NEVERC_PREP_API_MAJOR,
      NEVERC_PREP_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize < offsetof(NevercPrepAPI, DestroyTokenStreamBuilder) +
                       sizeof(((NevercPrepAPI *)0)->DestroyTokenStreamBuilder))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PrepAPI = (const NevercPrepAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.preprocessor-provider");
  Descriptor.DisplayName = STRING_VIEW("NeverC preprocessor provider test");
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
