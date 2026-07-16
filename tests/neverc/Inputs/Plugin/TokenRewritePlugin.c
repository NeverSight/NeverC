#include "neverc/Plugin/PluginPhaseSchema.h"
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

static int view_equals(NevercStringView View, const char *Text) {
  size_t Length = strlen(Text);
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
                                NevercPhaseContinuation *Continuation) {
  NevercPhaseResult Downstream;
  initialize_result(&Downstream);
  return Continuation->InvokeNext(Continuation, Frame, &Downstream);
}

static NevercStatus build_literal(const NevercPhaseFrame *Frame,
                                  NevercSourceLocation Location,
                                  NevercTokenFlags Flags,
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

static NevercStatus build_punctuation(const NevercPhaseFrame *Frame,
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

static NevercStatus build_identifier(const NevercPhaseFrame *Frame,
                                     NevercSourceLocation Location,
                                     NevercTokenFlags Flags,
                                     NevercIdentifierHandle Identifier,
                                     NevercTokenHandle *OutToken) {
  NevercTokenBuilderHandle Builder = {0, 0};
  NevercStatus Status =
      PrepAPI->CreateTokenBuilder(PrepAPI->Context, Frame->Task, &Builder);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetIdentifier(PrepAPI->Context, Frame->Task,
                                                Builder, Identifier);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetLocation(PrepAPI->Context, Frame->Task,
                                              Builder, Location);
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

static NevercStatus NEVERC_CALL rewrite_token(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTokenHandle Input = {0, 0};
  NevercTokenInfo Info;
  NevercTokenHandle Output[3] = {{0, 0}, {0, 0}, {0, 0}};
  uint64_t OutputCount = 0;
  NevercStatus Status;
  NevercTokenFlags PreservedFlags;
  (void)UserData;

  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  Status = PrepAPI->GetTokenPhaseInput(PrepAPI->Context, Frame, Frame->Input,
                                       &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Info, 0, sizeof(Info));
  Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_PREP_API_MAJOR,
                                       NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetTokenInfo(PrepAPI->Context, Frame->Task, Input, &Info);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Info.Kind != NEVERC_TOKEN_IDENTIFIER)
    return invoke_next(Frame, Continuation);

  PreservedFlags = Info.Flags & (NEVERC_TOKEN_FLAG_START_OF_LINE |
                                 NEVERC_TOKEN_FLAG_LEADING_SPACE);
  if (view_equals(Info.Spelling, "PLUGIN_CONST")) {
    Status = build_literal(Frame, Info.Location, PreservedFlags,
                           STRING_VIEW("42"), &Output[0]);
    OutputCount = 1;
  } else if (view_equals(Info.Spelling, "PLUGIN_DROP")) {
    Status = neverc_status_ok();
    OutputCount = 0;
  } else if (view_equals(Info.Spelling, "PLUGIN_PAIR")) {
    Status = build_literal(Frame, Info.Location, PreservedFlags,
                           STRING_VIEW("20"), &Output[0]);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = build_punctuation(Frame, Info.Location, NEVERC_TOKEN_PLUS,
                                 &Output[1]);
    if (Status.Code == NEVERC_STATUS_OK)
      Status =
          build_literal(Frame, Info.Location, 0, STRING_VIEW("22"), &Output[2]);
    OutputCount = 3;
  } else if (view_equals(Info.Spelling, "PLUGIN_REINJECT")) {
    Status = build_identifier(Frame, Info.Location, PreservedFlags,
                              Info.Identifier, &Output[0]);
    OutputCount = 1;
  } else if (view_equals(Info.Spelling, "PLUGIN_OVERFLOW")) {
    NevercTokenHandle TooMany[257];
    uint64_t Index;
    for (Index = 0; Index != 257; ++Index)
      TooMany[Index] = Input;
    Status =
        PrepAPI->CreateTokenPhaseOutput(PrepAPI->Context, Frame, Continuation,
                                        TooMany, 257, &OutResult->Output);
    if (Status.Code == NEVERC_STATUS_OK)
      return failure(NEVERC_STATUS_PLUGIN_FAILURE);
    return Status;
  } else if (view_equals(Info.Spelling, "PLUGIN_CANCEL")) {
    return failure(NEVERC_STATUS_CANCELLED);
  } else {
    return invoke_next(Frame, Continuation);
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status =
      PrepAPI->CreateTokenPhaseOutput(PrepAPI->Context, Frame, Continuation,
                                      Output, OutputCount, &OutResult->Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutResult->Action = NEVERC_PHASE_REPLACE;
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
  NevercInterceptorDescriptor Descriptor;
  (void)Core;
  (void)PluginProcessState;
  if (!Registrar || !Registrar->RegisterInterceptor)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = (NevercInterfaceID){NEVERC_PHASE_PREP_TOKEN_HIGH,
                                         NEVERC_PHASE_PREP_TOKEN_LOW};
  Descriptor.Callback = rewrite_token;
  return Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
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
      StructSize < offsetof(NevercPrepAPI, CreateTokenPhaseOutput) +
                       sizeof(((NevercPrepAPI *)0)->CreateTokenPhaseOutput))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PrepAPI = (const NevercPrepAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.token-rewrite");
  Descriptor.DisplayName = STRING_VIEW("NeverC token rewrite test plugin");
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
