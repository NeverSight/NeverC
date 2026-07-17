#include "neverc/Plugin/PluginSema.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                     \
  (NevercStringView) { (Value), (uint64_t)(sizeof(Value) - 1) }

static const NevercSemaAPI *SemaAPI;
static int ProcessState;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void initialize_result(NevercPhaseResult *Result) {
  memset(Result, 0, sizeof(*Result));
  Result->Header = (NevercABITableHeader){
      sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_CONTINUE;
}

static NevercStatus lookup_global(const NevercPhaseFrame *Frame,
                                  NevercStringView Name,
                                  NevercDeclHandle *OutDeclaration) {
  NevercSemaScopeHandle Scope;
  NevercSemaScopeInfo ScopeInfo;
  NevercSemaLookupRequest Request;
  NevercSemaLookupResultInfo ResultInfo;
  NevercLookupResultHandle Result;
  NevercStatus Status;
  if (!Frame || !OutDeclaration)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDeclaration = (NevercDeclHandle){0, 0};
  Status =
      SemaAPI->GetCurrentScope(SemaAPI->Context, Frame->Task, &Scope);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (;;) {
    memset(&ScopeInfo, 0, sizeof(ScopeInfo));
    ScopeInfo.Header = (NevercABITableHeader){
        sizeof(ScopeInfo), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
    Status = SemaAPI->GetScopeInfo(SemaAPI->Context, Frame->Task, Scope,
                                   &ScopeInfo);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (neverc_handle_is_null(ScopeInfo.Parent))
      break;
    Scope = ScopeInfo.Parent;
  }

  memset(&Request, 0, sizeof(Request));
  Request.Header = (NevercABITableHeader){
      sizeof(Request), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Request.Scope = Scope;
  Request.Name = Name;
  Request.Kind = NEVERC_SEMA_LOOKUP_ORDINARY;
  Status =
      SemaAPI->LookupName(SemaAPI->Context, Frame->Task, &Request, &Result);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&ResultInfo, 0, sizeof(ResultInfo));
  ResultInfo.Header = (NevercABITableHeader){
      sizeof(ResultInfo), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetLookupResultInfo(SemaAPI->Context, Frame->Task, Result,
                                        &ResultInfo);
  if (Status.Code == NEVERC_STATUS_OK && ResultInfo.CandidateCount == 1)
    Status = SemaAPI->GetLookupCandidate(SemaAPI->Context, Frame->Task, Result,
                                         0, OutDeclaration);
  else if (Status.Code == NEVERC_STATUS_OK)
    Status = failure(NEVERC_STATUS_VERIFICATION_FAILED);
  {
    NevercStatus DestroyStatus =
        SemaAPI->DestroyLookupResult(SemaAPI->Context, Frame->Task, Result);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = DestroyStatus;
  }
  return Status;
}

static NevercStatus NEVERC_CALL intercept_expression(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercSemaExpressionExtensionInput Input;
  NevercSemaExpressionExtensionOutput Output;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetExpressionExtensionInput(
      SemaAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (neverc_handle_is_null(Input.Left) ||
      neverc_handle_is_null(Input.Right))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);

  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Output.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;
  Output.Expression = Input.Left;
  initialize_result(OutResult);
  Status = SemaAPI->CreateExpressionExtensionOutput(
      SemaAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_statement(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercSemaStatementExtensionInput Input;
  NevercSemaStatementExtensionOutput Output;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetStatementExtensionInput(
      SemaAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Input.Statements || Input.StatementCount == 0)
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);

  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Output.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;
  Output.Statement = Input.Statements[Input.StatementCount - 1];
  initialize_result(OutResult);
  Status = SemaAPI->CreateStatementExtensionOutput(
      SemaAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_declaration(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercSemaDeclarationExtensionInput Input;
  NevercSemaDeclarationExtensionOutput Output;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetDeclarationExtensionInput(
      SemaAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (neverc_handle_is_null(Input.Declaration))
    return failure(NEVERC_STATUS_VERIFICATION_FAILED);

  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Output.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;
  Status = lookup_global(Frame, STRING_VIEW("sema_decl_replacement"),
                         &Output.Declaration);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  initialize_result(OutResult);
  Status = SemaAPI->CreateDeclarationExtensionOutput(
      SemaAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_type(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  static const char PluginTypeName[] = "plugin_int";
  NevercSemaTypeExtensionInput Input;
  NevercSemaTypeExtensionOutput Output;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetTypeExtensionInput(SemaAPI->Context, Frame, Frame->Input,
                                          &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Output.Disposition = NEVERC_SEMA_EXTENSION_UNHANDLED;
  if (Input.Name.Length == sizeof(PluginTypeName) - 1 &&
      Input.Name.Data &&
      memcmp(Input.Name.Data, PluginTypeName, sizeof(PluginTypeName) - 1) ==
          0) {
    Output.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;
    Status = SemaAPI->GetBuiltinType(SemaAPI->Context, Frame->Task,
                                     NEVERC_BUILTIN_TYPE_INT, &Output.Type);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }

  initialize_result(OutResult);
  Status = SemaAPI->CreateTypeExtensionOutput(
      SemaAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_lookup(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  static const char AliasName[] = "sema_lookup_alias";
  NevercSemaLookupExtensionInput Input;
  NevercSemaLookupExtensionOutput Output;
  NevercDeclHandle Target;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetLookupExtensionInput(
      SemaAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Output.Disposition = NEVERC_SEMA_EXTENSION_UNHANDLED;
  if (Input.Name.Length == sizeof(AliasName) - 1 && Input.Name.Data &&
      memcmp(Input.Name.Data, AliasName, sizeof(AliasName) - 1) == 0) {
    Status = lookup_global(Frame, STRING_VIEW("sema_lookup_target"), &Target);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Output.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;
    Output.Candidates = &Target;
    Output.CandidateCount = 1;
  }

  initialize_result(OutResult);
  Status = SemaAPI->CreateLookupExtensionOutput(
      SemaAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
}

static NevercStatus NEVERC_CALL intercept_conversion(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercSemaConversionExtensionInput Input;
  NevercSemaConversionExtensionOutput Output;
  NevercConversionSequenceHandle Sequence;
  NevercSemaConversionSequenceInfo SequenceInfo;
  NevercSemaMutationLeaseHandle Lease;
  NevercStatus Status;
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Status = SemaAPI->GetConversionExtensionInput(
      SemaAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Output, 0, sizeof(Output));
  Output.Header = (NevercABITableHeader){
      sizeof(Output), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Output.Disposition = NEVERC_SEMA_EXTENSION_UNHANDLED;
  if (Input.Context == NEVERC_SEMA_CONVERSION_RETURN) {
    memset(&Sequence, 0, sizeof(Sequence));
    Status = SemaAPI->ClassifyImplicitConversion(
        SemaAPI->Context, Frame->Task, Input.SourceType, Input.DestinationType,
        &Sequence);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;

    memset(&SequenceInfo, 0, sizeof(SequenceInfo));
    SequenceInfo.Header = (NevercABITableHeader){
        sizeof(SequenceInfo), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
    Status = SemaAPI->GetConversionSequenceInfo(
        SemaAPI->Context, Frame->Task, Sequence, &SequenceInfo);
    if (Status.Code == NEVERC_STATUS_OK &&
        SequenceInfo.Kind == NEVERC_SEMA_CONVERSION_INTEGER_TO_POINTER) {
      memset(&Lease, 0, sizeof(Lease));
      Status = SemaAPI->AcquireMutationLease(SemaAPI->Context, Frame->Task,
                                             &Lease);
      if (Status.Code == NEVERC_STATUS_OK) {
        NevercStatus ReleaseStatus;
        Status = SemaAPI->CreateExplicitCast(
            SemaAPI->Context, Frame->Task, Lease, Input.Expression,
            Input.DestinationType, &Output.Expression);
        ReleaseStatus = SemaAPI->ReleaseMutationLease(
            SemaAPI->Context, Frame->Task, Lease);
        if (Status.Code == NEVERC_STATUS_OK)
          Status = ReleaseStatus;
      }
      if (Status.Code == NEVERC_STATUS_OK)
        Output.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;
    }
    {
      NevercStatus DestroyStatus = SemaAPI->DestroyConversionSequence(
          SemaAPI->Context, Frame->Task, Sequence);
      if (Status.Code == NEVERC_STATUS_OK)
        Status = DestroyStatus;
    }
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }

  initialize_result(OutResult);
  Status = SemaAPI->CreateConversionExtensionOutput(
      SemaAPI->Context, Frame, Continuation, &Output, &OutResult->Output);
  if (Status.Code == NEVERC_STATUS_OK)
    OutResult->Action = NEVERC_PHASE_REPLACE;
  return Status;
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
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_EXTENSION_EXPRESSION_HIGH,
                          NEVERC_PHASE_SEMA_EXTENSION_EXPRESSION_LOW};
  Descriptor.Callback = intercept_expression;
  {
    NevercStatus Status =
        Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_EXTENSION_STATEMENT_HIGH,
                          NEVERC_PHASE_SEMA_EXTENSION_STATEMENT_LOW};
  Descriptor.Callback = intercept_statement;
  {
    NevercStatus Status =
        Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_EXTENSION_DECLARATION_HIGH,
                          NEVERC_PHASE_SEMA_EXTENSION_DECLARATION_LOW};
  Descriptor.Callback = intercept_declaration;
  {
    NevercStatus Status =
        Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_EXTENSION_TYPE_HIGH,
                          NEVERC_PHASE_SEMA_EXTENSION_TYPE_LOW};
  Descriptor.Callback = intercept_type;
  {
    NevercStatus Status =
        Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_EXTENSION_LOOKUP_HIGH,
                          NEVERC_PHASE_SEMA_EXTENSION_LOOKUP_LOW};
  Descriptor.Callback = intercept_lookup;
  {
    NevercStatus Status =
        Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_SEMA_EXTENSION_CONVERSION_HIGH,
                          NEVERC_PHASE_SEMA_EXTENSION_CONVERSION_LOW};
  Descriptor.Callback = intercept_conversion;
  return Registrar->RegisterInterceptor(RegistrarContext, &Descriptor);
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
      (NevercInterfaceID){NEVERC_INTERFACE_SEMA_HIGH,
                          NEVERC_INTERFACE_SEMA_LOW},
      NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, &Table,
      offsetof(NevercSemaAPI, CreateConversionExtensionOutput) +
          sizeof(((NevercSemaAPI *)0)->CreateConversionExtensionOutput));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SemaAPI = (const NevercSemaAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.sema");
  Descriptor.DisplayName = STRING_VIEW("NeverC Sema interceptor test plugin");
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
