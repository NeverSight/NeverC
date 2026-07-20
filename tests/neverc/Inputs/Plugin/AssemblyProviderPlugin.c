#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginTarget.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value) {(Value), (uint64_t)(sizeof(Value) - 1)}

static const NevercTargetID TestTargetID = {
    UINT64_C(0x7800), UINT64_C(1)};
static const NevercInterfaceID TestSchemaID = {
    UINT64_C(0x7801), UINT64_C(1)};
static const char SchemaDigest[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static const NevercAssemblyProviderAPI *AssemblyAPI;

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int same_text(NevercStringView Value, const char *Expected,
                     size_t Length) {
  return Value.Length == Length && Value.Data != NULL &&
         memcmp(Value.Data, Expected, Length) == 0;
}

static NevercStatus NEVERC_CALL
parse_assembly(const NevercPhaseFrame *Frame,
               NevercPhaseResult *OutResult, void *UserData) {
  NevercAssemblyParseInputInfo Input;
  const NevercMCAPI *MC = NULL;
  NevercMCUnitHandle Unit = {0, 0};
  NevercMCSchemaTokenHandle SchemaToken = {0, 0};
  NevercMCMutationHandle Mutation = {0, 0};
  NevercMCInstHandle Instruction = {0, 0};
  NevercArtifactHandle Output = {0, 0};
  NevercStatus Status;
  (void)UserData;

  if (Frame == NULL || OutResult == NULL || AssemblyAPI == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
  Status = AssemblyAPI->GetParseInput(
      AssemblyAPI->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!same_text(Input.Source.Buffer, ".plugin_opcode\n", 15))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Input.Source.Preprocessed == NEVERC_TRUE) {
    NevercAssemblyTokenInfo Token;
    memset(&Token, 0, sizeof(Token));
    Token.Header = (NevercABITableHeader){
        sizeof(Token), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
        NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
    Status = AssemblyAPI->PeekSourceToken(
        AssemblyAPI->Context, Frame, Input.Source.Cursor, &Token);
    if (Status.Code != NEVERC_STATUS_OK ||
        !same_text(Token.Spelling, ".plugin_opcode", 14) ||
        Token.FileID != 7 || Token.ByteOffset != 100 ||
        Token.Line != 3 || Token.Column != 5 ||
        Token.StartOfLine != NEVERC_TRUE)
      return status_code(NEVERC_STATUS_INVALID_STATE);
    Status = AssemblyAPI->AdvanceSourceToken(
        AssemblyAPI->Context, Frame, Input.Source.Cursor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = AssemblyAPI->PeekSourceToken(
        AssemblyAPI->Context, Frame, Input.Source.Cursor, &Token);
    if (Status.Code != NEVERC_STATUS_NOT_FOUND)
      return status_code(NEVERC_STATUS_INVALID_STATE);
  }

  Status = AssemblyAPI->GetParseMCBuilder(
      AssemblyAPI->Context, Frame, &MC, &Unit);
  if (Status.Code != NEVERC_STATUS_OK || MC == NULL)
    return Status.Code == NEVERC_STATUS_OK
               ? status_code(NEVERC_STATUS_INVALID_STATE)
               : Status;
  Status = MC->GetSchemaToken(
      MC->Context, Frame->Task, Unit, &SchemaToken);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = MC->BeginMutation(
      MC->Context, Frame->Task, Unit, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = MC->CreateInstruction(
      MC->Context, Frame->Task, Mutation, SchemaToken, 10,
      &Instruction);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)MC->AbandonMutation(
        MC->Context, Frame->Task, Mutation);
    return Status;
  }
  Status = MC->AppendInstruction(
      MC->Context, Frame->Task, Mutation, Unit, Instruction);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)MC->AbandonMutation(
        MC->Context, Frame->Task, Mutation);
    return Status;
  }
  Status = MC->CommitMutation(MC->Context, Frame->Task, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = AssemblyAPI->PublishParsedMCUnit(
      AssemblyAPI->Context, Frame, &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
print_assembly(const NevercPhaseFrame *Frame,
               NevercPhaseResult *OutResult, void *UserData) {
  NevercAssemblyPrintInputInfo Input;
  NevercAssemblyOutputMetadata Metadata;
  NevercMCInstructionInfo InstructionInfo;
  const NevercMCAPI *MC = NULL;
  NevercMCUnitHandle Unit = {0, 0};
  NevercMCInstHandle Instruction = {0, 0};
  NevercArtifactHandle Output = {0, 0};
  NevercStatus Status;
  static const char Text[] = ".byte 0\n";
  (void)UserData;

  if (Frame == NULL || OutResult == NULL || AssemblyAPI == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
  Status = AssemblyAPI->GetPrintInput(
      AssemblyAPI->Context, Frame, Frame->Input, &Input, &MC, &Unit);
  if (Status.Code != NEVERC_STATUS_OK || MC == NULL)
    return Status.Code == NEVERC_STATUS_OK
               ? status_code(NEVERC_STATUS_INVALID_STATE)
               : Status;
  Status = MC->GetFirstInstruction(
      MC->Context, Frame->Task, Unit, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&InstructionInfo, 0, sizeof(InstructionInfo));
  InstructionInfo.Header = (NevercABITableHeader){
      sizeof(InstructionInfo), NEVERC_MC_API_MAJOR,
      NEVERC_MC_API_MINOR, 0};
  Status = MC->GetInstructionInfo(
      MC->Context, Frame->Task, Instruction, &InstructionInfo);
  if (Status.Code != NEVERC_STATUS_OK || InstructionInfo.Opcode != 10)
    return Status.Code == NEVERC_STATUS_OK
               ? status_code(NEVERC_STATUS_INVALID_STATE)
               : Status;
  Status = AssemblyAPI->WritePrintOutput(
      AssemblyAPI->Context, Frame,
      (NevercStringView){Text, sizeof(Text) - 1});
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Metadata, 0, sizeof(Metadata));
  Metadata.Header = (NevercABITableHeader){
      sizeof(Metadata), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
  Metadata.Syntax = (NevercStringView)STRING_VIEW("test");
  Status = AssemblyAPI->PublishAssemblyOutput(
      AssemblyAPI->Context, Frame, &Metadata, &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();
}

static NevercStatus query_interface(
    const NevercCoreAPI *Core, NevercInterfaceID ID,
    uint16_t Major, uint16_t Minor, const void **OutTable,
    uint64_t RequiredSize) {
  uint16_t NegotiatedMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Core->QueryInterface(
      Core->Context, ID, Major, Minor, OutTable, &NegotiatedMinor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (*OutTable == NULL || StructSize < RequiredSize)
    return status_code(NEVERC_STATUS_INVALID_STATE);
  return neverc_status_ok();
}

static NevercStatus register_target_and_schema(
    const NevercCoreAPI *Core, void *RegistrarContext) {
  const NevercTargetAPI *TargetAPI = NULL;
  const NevercMCAPI *MC = NULL;
  const void *Table = NULL;
  NevercTargetDescriptor Target;
  NevercMCSchemaDescriptor Schema;
  static NevercMCSchemaValueDescriptor Opcode;
  NevercStatus Status;

  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_TARGET_HIGH,
                          NEVERC_INTERFACE_TARGET_LOW},
      NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, &Table,
      sizeof(NevercTargetAPI));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  TargetAPI = (const NevercTargetAPI *)Table;
  Table = NULL;
  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_MC_HIGH,
                          NEVERC_INTERFACE_MC_LOW},
      NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, &Table,
      sizeof(NevercMCAPI));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MC = (const NevercMCAPI *)Table;

  memset(&Target, 0, sizeof(Target));
  Target.Header = (NevercABITableHeader){
      sizeof(Target), NEVERC_TARGET_API_MAJOR,
      NEVERC_TARGET_API_MINOR, 0};
  Target.TargetID = TestTargetID;
  Target.CanonicalName =
      (NevercStringView)STRING_VIEW("test.assembly");
  Target.MCSchemaID = TestSchemaID;
  Target.Machine.Header = (NevercABITableHeader){
      sizeof(Target.Machine), NEVERC_TARGET_API_MAJOR,
      NEVERC_TARGET_API_MINOR, 0};
  Target.Machine.RawTriple =
      (NevercStringView)STRING_VIEW("x86_64-unknown-linux-gnu");
  Target.Machine.Architecture =
      (NevercStringView)STRING_VIEW("x86_64");
  Target.Machine.DataLayout = (NevercStringView)STRING_VIEW(
      "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
      "f80:128-n8:16:32:64-S128");
  Target.Machine.DefaultCPU =
      (NevercStringView)STRING_VIEW("generic");
  Target.Machine.SchemaDigest =
      (NevercStringView){SchemaDigest, sizeof(SchemaDigest) - 1};
  Target.Machine.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC;
  Target.Machine.SupportedCodeModels =
      NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
  Target.Machine.DefaultRelocationModel =
      NEVERC_TARGET_RELOCATION_STATIC;
  Target.Machine.DefaultCodeModel = NEVERC_TARGET_CODE_MODEL_SMALL;
  Target.Machine.ExceptionModel = NEVERC_TARGET_EXCEPTION_NONE;
  Target.Machine.UnwindModel = NEVERC_TARGET_UNWIND_NONE;
  Target.Machine.Endianness = NEVERC_TARGET_ENDIAN_LITTLE;
  Target.Machine.PointerWidth = 64;
  Target.Machine.IntWidth = 32;
  Target.Machine.LongWidth = 64;
  Target.Machine.LongLongWidth = 64;
  Target.Machine.StackAlignment = 128;
  Target.Machine.MaximumAtomicWidth = 64;
  Target.Machine.MaximumVectorAlignment = 128;
  Target.Machine.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Target.Machine.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.DefaultExecutionLevel =
      NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.TLSSupported = NEVERC_TRUE;
  Status = TargetAPI->RegisterTarget(
      TargetAPI->Context, RegistrarContext, &Target);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Opcode, 0, sizeof(Opcode));
  Opcode.Header = (NevercABITableHeader){
      sizeof(Opcode), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  Opcode.StableID = 10;
  Opcode.BackendValue = 100;
  Opcode.CanonicalName =
      (NevercStringView)STRING_VIEW("test.opcode");
  memset(&Schema, 0, sizeof(Schema));
  Schema.Header = (NevercABITableHeader){
      sizeof(Schema), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  Schema.SchemaID = TestSchemaID;
  Schema.TargetID = TestTargetID;
  Schema.CanonicalName =
      (NevercStringView)STRING_VIEW("test.mc");
  Schema.Digest =
      (NevercStringView){SchemaDigest, sizeof(SchemaDigest) - 1};
  Schema.Opcodes =
      (NevercStructArrayView){&Opcode, 1, sizeof(Opcode)};
  return MC->RegisterSchema(MC->Context, RegistrarContext, &Schema);
}

static NevercStatus register_provider(
    const NevercRegistrarAPI *Registrar, void *RegistrarContext,
    NevercInterfaceID Phase, const char *ProviderName,
    uint64_t ProviderNameLength, NevercPhaseProviderFn Callback) {
  NevercProviderDescriptor Provider;
  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase = Phase;
  Provider.ProviderID =
      (NevercStringView){ProviderName, ProviderNameLength};
  Provider.Route.Header = (NevercABITableHeader){
      sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Cacheable = NEVERC_FALSE;
  Provider.FallbackSafe = NEVERC_FALSE;
  Provider.Callback = Callback;
  return Registrar->RegisterProvider(RegistrarContext, &Provider);
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core,
                const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  NevercStatus Status;
  (void)ProcessState;

  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_ASSEMBLY_PROVIDER_HIGH,
                          NEVERC_INTERFACE_ASSEMBLY_PROVIDER_LOW},
      NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR, &Table,
      sizeof(NevercAssemblyProviderAPI));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  AssemblyAPI = (const NevercAssemblyProviderAPI *)Table;
  Status = register_target_and_schema(Core, RegistrarContext);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = register_provider(
      Registrar, RegistrarContext,
      (NevercInterfaceID){NEVERC_PHASE_ASSEMBLY_PARSE_HIGH,
                          NEVERC_PHASE_ASSEMBLY_PARSE_LOW},
      "test.assembly.parser", sizeof("test.assembly.parser") - 1,
      parse_assembly);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return register_provider(
      Registrar, RegistrarContext,
      (NevercInterfaceID){NEVERC_PHASE_ASSEMBLY_PRINT_HIGH,
                          NEVERC_PHASE_ASSEMBLY_PRINT_LOW},
      "test.assembly.printer", sizeof("test.assembly.printer") - 1,
      print_assembly);
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  if (OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  AssemblyAPI = NULL;
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;
  (void)Bootstrap;
  if (OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.assembly-provider");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Assembly Provider Test");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
