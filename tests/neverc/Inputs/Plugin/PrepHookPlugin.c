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

static NevercStatus build_identifier(const NevercPhaseFrame *Frame,
                                     NevercSourceLocation Location,
                                     NevercStringView Spelling,
                                     NevercTokenHandle *OutToken) {
  NevercIdentifierHandle Identifier = {0, 0};
  NevercTokenBuilderHandle Builder = {0, 0};
  NevercStatus Status = PrepAPI->GetOrCreateIdentifier(
      PrepAPI->Context, Frame->Task, Spelling, &Identifier);
  if (Status.Code == NEVERC_STATUS_OK)
    Status =
        PrepAPI->CreateTokenBuilder(PrepAPI->Context, Frame->Task, &Builder);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = PrepAPI->TokenBuilderSetIdentifier(PrepAPI->Context, Frame->Task,
                                                Builder, Identifier);
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

static NevercStatus NEVERC_CALL intercept_include(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercPrepIncludePhaseInput Input;
  NevercPrepIncludePhaseOutput Output;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PREP_API_MAJOR,
                                        NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetIncludePhaseInput(PrepAPI->Context, Frame, Frame->Input,
                                         &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){sizeof(Output), NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
  if (view_equals(Input.Filename, "plugin_redirect.h")) {
    Output.Action = NEVERC_PREP_INCLUDE_REDIRECT;
    Output.Filename = STRING_VIEW("plugin_target.h");
    Output.IsAngled = NEVERC_FALSE;
  } else if (view_equals(Input.Filename, "plugin_skip.h")) {
    Output.Action = NEVERC_PREP_INCLUDE_SKIP;
  } else if (view_equals(Input.Filename, "plugin_invalid.h")) {
    Output.Action = NEVERC_PREP_INCLUDE_REDIRECT;
  } else if (view_equals(Input.Filename, "plugin_cancel.h")) {
    return failure(NEVERC_STATUS_CANCELLED);
  } else {
    return invoke_next(Frame, Continuation);
  }
  Status = PrepAPI->CreateIncludePhaseOutput(
      PrepAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_macro(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercPrepMacroPhaseInput Input;
  NevercPrepMacroPhaseOutput Output;
  NevercTokenInfo Name;
  NevercTokenHandle Replacement = {0, 0};
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PREP_API_MAJOR,
                                        NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetMacroPhaseInput(PrepAPI->Context, Frame, Frame->Input,
                                       &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Name, 0, sizeof(Name));
  Name.Header = (NevercABITableHeader){sizeof(Name), NEVERC_PREP_API_MAJOR,
                                       NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetTokenInfo(PrepAPI->Context, Frame->Task, Input.NameToken,
                                 &Name);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){sizeof(Output), NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
  if (Input.Operation == NEVERC_PREP_MACRO_DEFINE &&
      view_equals(Name.Spelling, "PLUGIN_SUPPRESS_DEFINE")) {
    Output.Action = NEVERC_PREP_MACRO_SUPPRESS;
  } else if (Input.Operation == NEVERC_PREP_MACRO_UNDEFINE &&
             view_equals(Name.Spelling, "PLUGIN_KEEP")) {
    Output.Action = NEVERC_PREP_MACRO_SUPPRESS;
  } else if (Input.Operation == NEVERC_PREP_MACRO_EXPAND &&
             view_equals(Name.Spelling, "PLUGIN_VALUE")) {
    Status =
        build_literal(Frame, Name.Location, STRING_VIEW("40"), &Replacement);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Output.Action = NEVERC_PREP_MACRO_REPLACE_TOKENS;
    Output.Tokens = &Replacement;
    Output.TokenCount = 1;
  } else if (Input.Operation == NEVERC_PREP_MACRO_EXPAND_BUILTIN &&
             view_equals(Name.Spelling, "__LINE__")) {
    Status =
        build_literal(Frame, Name.Location, STRING_VIEW("2"), &Replacement);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Output.Action = NEVERC_PREP_MACRO_REPLACE_TOKENS;
    Output.Tokens = &Replacement;
    Output.TokenCount = 1;
  } else {
    return invoke_next(Frame, Continuation);
  }
  Status = PrepAPI->CreateMacroPhaseOutput(
      PrepAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_pragma(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercPrepPragmaPhaseInput Input;
  NevercPrepPragmaPhaseOutput Output;
  NevercTokenHandle First = {0, 0};
  NevercTokenHandle Replacement = {0, 0};
  NevercTokenInfo FirstInfo;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PREP_API_MAJOR,
                                        NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetPragmaPhaseInput(PrepAPI->Context, Frame, Frame->Input,
                                        &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!view_equals(Input.Name, "plugin") &&
      !view_equals(Input.Name, "plugin_replace"))
    return invoke_next(Frame, Continuation);
  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){sizeof(Output), NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
  if (view_equals(Input.Name, "plugin")) {
    Output.Action = NEVERC_PREP_PRAGMA_HANDLED;
  } else {
    Status = PrepAPI->GetTokenStreamToken(PrepAPI->Context, Frame->Task,
                                          Input.Tokens, 0, &First);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    memset(&FirstInfo, 0, sizeof(FirstInfo));
    FirstInfo.Header = (NevercABITableHeader){
        sizeof(FirstInfo), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
    Status =
        PrepAPI->GetTokenInfo(PrepAPI->Context, Frame->Task, First, &FirstInfo);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = build_identifier(Frame, FirstInfo.Location, STRING_VIEW("region"),
                              &Replacement);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Output.Action = NEVERC_PREP_PRAGMA_REPLACE_TOKENS;
    Output.Tokens = &Replacement;
    Output.TokenCount = 1;
  }
  Status = PrepAPI->CreatePragmaPhaseOutput(
      PrepAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_feature_query(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercPrepFeatureQueryPhaseInput Input;
  NevercPrepFeatureQueryPhaseOutput Output;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  initialize_result(OutResult);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){sizeof(Input), NEVERC_PREP_API_MAJOR,
                                        NEVERC_PREP_API_MINOR, 0};
  Status = PrepAPI->GetFeatureQueryPhaseInput(PrepAPI->Context, Frame,
                                              Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!view_equals(Input.Name, "plugin_feature") &&
      !view_equals(Input.Name, "plugin_extension") &&
      !view_equals(Input.Name, "plugin_builtin") &&
      !view_equals(Input.Name, "plugin_header.h"))
    return invoke_next(Frame, Continuation);
  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){sizeof(Output), NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
  Output.Action = NEVERC_PREP_QUERY_REPLACE;
  Output.Value = NEVERC_TRUE;
  Status = PrepAPI->CreateFeatureQueryPhaseOutput(
      PrepAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
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
  if (!Registrar || !Registrar->RegisterInterceptor)
    return failure(NEVERC_STATUS_MISSING_INTERFACE);
  Status =
      register_interceptor(Registrar, RegistrarContext,
                           (NevercInterfaceID){NEVERC_PHASE_PREP_INCLUDE_HIGH,
                                               NEVERC_PHASE_PREP_INCLUDE_LOW},
                           intercept_include);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status =
      register_interceptor(Registrar, RegistrarContext,
                           (NevercInterfaceID){NEVERC_PHASE_PREP_MACRO_HIGH,
                                               NEVERC_PHASE_PREP_MACRO_LOW},
                           intercept_macro);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status =
      register_interceptor(Registrar, RegistrarContext,
                           (NevercInterfaceID){NEVERC_PHASE_PREP_PRAGMA_HIGH,
                                               NEVERC_PHASE_PREP_PRAGMA_LOW},
                           intercept_pragma);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return register_interceptor(
      Registrar, RegistrarContext,
      (NevercInterfaceID){NEVERC_PHASE_PREP_FEATURE_QUERY_HIGH,
                          NEVERC_PHASE_PREP_FEATURE_QUERY_LOW},
      intercept_feature_query);
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
      StructSize <
          offsetof(NevercPrepAPI, CreateFeatureQueryPhaseOutput) +
              sizeof(((NevercPrepAPI *)0)->CreateFeatureQueryPhaseOutput))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PrepAPI = (const NevercPrepAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.prep-hooks");
  Descriptor.DisplayName = STRING_VIEW("NeverC prep hooks test plugin");
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
