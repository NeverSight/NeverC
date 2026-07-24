#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginTarget.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value)                                                   \
  { (Value), (uint64_t)(sizeof(Value) - 1) }

static int ProcessState;

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static int string_equals(NevercStringView Value, const char *Text) {
  size_t Length = strlen(Text);
  return Value.Length == Length && Value.Data != NULL &&
         memcmp(Value.Data, Text, Length) == 0;
}

static NevercStatus NEVERC_CALL process_begin(
    const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  if (OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL validate_cpu(
    NevercTaskHandle Task, NevercStringView CPU, void *UserData,
    NevercBool *OutValid) {
  (void)Task;
  (void)UserData;
  if (OutValid == NULL || (CPU.Data == NULL && CPU.Length != 0))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValid =
      string_equals(CPU, "generic") || string_equals(CPU, "fast")
          ? NEVERC_TRUE
          : NEVERC_FALSE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL canonicalize_cpu(
    NevercTaskHandle Task, NevercStringView CPU, void *UserData,
    NevercStringView *OutCanonicalCPU) {
  static const NevercStringView Fast = STRING_VIEW("fast");
  (void)Task;
  (void)UserData;
  if (OutCanonicalCPU == NULL ||
      (CPU.Data == NULL && CPU.Length != 0))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCanonicalCPU = string_equals(CPU, "turbo") ? Fast : CPU;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL list_cpus(
    NevercTaskHandle Task, void *UserData,
    NevercStringArrayView *OutCPUs) {
  static const NevercStringView CPUs[] = {
      STRING_VIEW("fast"),
      STRING_VIEW("generic"),
  };
  (void)Task;
  (void)UserData;
  if (OutCPUs == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  OutCPUs->Data = CPUs;
  OutCPUs->Count = sizeof(CPUs) / sizeof(CPUs[0]);
  OutCPUs->ElementStride = sizeof(CPUs[0]);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL resolve_features(
    NevercTaskHandle Task, NevercStringView CPU,
    NevercStringArrayView RequestedFeatures, void *UserData,
    NevercStructArrayView *OutFeatureStates) {
  static const NevercTargetFeatureState DisabledStates[] = {
      {
          .Header = {sizeof(NevercTargetFeatureState),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("base"),
          .Enabled = NEVERC_TRUE,
      },
      {
          .Header = {sizeof(NevercTargetFeatureState),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("simd"),
          .Enabled = NEVERC_FALSE,
      },
  };
  static const NevercTargetFeatureState EnabledStates[] = {
      {
          .Header = {sizeof(NevercTargetFeatureState),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("base"),
          .Enabled = NEVERC_TRUE,
      },
      {
          .Header = {sizeof(NevercTargetFeatureState),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("simd"),
          .Enabled = NEVERC_TRUE,
      },
  };
  const uint8_t *Bytes;
  NevercBool SIMDEnabled = NEVERC_FALSE;
  uint64_t Index;
  (void)Task;
  (void)CPU;
  (void)UserData;
  if (OutFeatureStates == NULL ||
      (RequestedFeatures.Count != 0 &&
       (RequestedFeatures.Data == NULL ||
        RequestedFeatures.ElementStride < sizeof(NevercStringView))))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Bytes = (const uint8_t *)RequestedFeatures.Data;
  for (Index = 0; Index != RequestedFeatures.Count; ++Index) {
    const NevercStringView *Feature =
        (const NevercStringView *)(
            Bytes + Index * RequestedFeatures.ElementStride);
    if (string_equals(*Feature, "+simd"))
      SIMDEnabled = NEVERC_TRUE;
    else if (string_equals(*Feature, "-simd"))
      SIMDEnabled = NEVERC_FALSE;
  }
  OutFeatureStates->Data =
      SIMDEnabled == NEVERC_TRUE ? EnabledStates : DisabledStates;
  OutFeatureStates->Count =
      sizeof(DisabledStates) / sizeof(DisabledStates[0]);
  OutFeatureStates->ElementStride = sizeof(DisabledStates[0]);
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL lower_add_builtin(
    void *UserData,
    const NevercTargetBuiltinLoweringInvocation *Invocation,
    NevercIRValueHandle *OutResult) {
  static const NevercStringView Name = STRING_VIEW("plugin.add");
  (void)UserData;
  if (Invocation == NULL || OutResult == NULL ||
      Invocation->Builder == NULL || Invocation->ArgumentCount != 2)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  return Invocation->Builder->BuildBinary(
      Invocation->Builder->Context, Invocation->Task,
      Invocation->IRBuilder, NEVERC_IR_OPCODE_ADD,
      Invocation->Arguments[0], Invocation->Arguments[1], Name,
      OutResult);
}

static NevercStatus NEVERC_CALL classify_function(
    void *UserData, const NevercABIFunctionQuery *Query,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *Arguments) {
  uint64_t Index;
  (void)UserData;
  if (Query == NULL || ReturnValue == NULL || Arguments == NULL ||
      Arguments->Count != Query->Parameters.Count)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  ReturnValue->Kind =
      Query->ReturnType.Kind == NEVERC_ABI_TYPE_VOID
          ? NEVERC_ABI_ARGUMENT_IGNORE
          : NEVERC_ABI_ARGUMENT_DIRECT;
  for (Index = 0; Index != Arguments->Count; ++Index) {
    NevercABIArgumentClassification *Argument =
        (NevercABIArgumentClassification *)(
            (uint8_t *)Arguments->Data +
            Index * Arguments->ElementStride);
    Argument->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  }
  return neverc_status_ok();
}

static NevercStatus query_interface(
    const NevercCoreAPI *Core, NevercInterfaceID ID, uint16_t Major,
    uint16_t Minor, const void **OutTable, uint64_t MinimumSize) {
  uint16_t ActualMinor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Core->QueryInterface(
      Core->Context, ID, Major, Minor, OutTable, &ActualMinor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (*OutTable == NULL || StructSize < MinimumSize)
    return status_code(NEVERC_STATUS_MISSING_INTERFACE);
  return neverc_status_ok();
}

static NevercStatus register_target(
    const NevercCoreAPI *Core, void *RegistrarContext) {
  static const NevercTargetID TargetID = {
      UINT64_C(0x4e43544c414e4701), UINT64_C(1)};
  static const NevercTargetABIID ABI = {
      UINT64_C(0x4e43544c41424901), UINT64_C(1)};
  static const NevercCallingConventionID CallingConvention = {
      UINT64_C(0x4e43544c43434e01), UINT64_C(1)};
  static const NevercStringView CPUValues[] = {
      STRING_VIEW("fast"),
      STRING_VIEW("generic"),
  };
  static const NevercStringView ImpliedBase[] = {
      STRING_VIEW("base"),
  };
  static const NevercTargetFeatureDescriptor Features[] = {
      {
          .Header = {sizeof(NevercTargetFeatureDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("base"),
          .EnabledByDefault = NEVERC_TRUE,
      },
      {
          .Header = {sizeof(NevercTargetFeatureDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("simd"),
          .Implies = {ImpliedBase, 1, sizeof(ImpliedBase[0])},
      },
  };
  static const NevercTargetMacroDescriptor Macros[] = {
      {
          .Header = {sizeof(NevercTargetMacroDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("__LANG_TARGET__"),
          .Value = STRING_VIEW("42"),
      },
  };
  static const NevercTargetBuiltinDescriptor Builtins[] = {
      {
          .Header = {sizeof(NevercTargetBuiltinDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("__builtin_lang_add"),
          .TypeEncoding = STRING_VIEW("iii"),
          .Attributes = STRING_VIEW("nc"),
          .Languages = NEVERC_TARGET_BUILTIN_LANGUAGE_C,
          .Lower = lower_add_builtin,
      },
  };
  static const NevercStringView RegisterAliases[] = {
      STRING_VIEW("zero"),
  };
  static const NevercStringView AdditionalRegisterNames[] = {
      STRING_VIEW("special-zero"),
  };
  static const NevercTargetRegisterDescriptor Registers[] = {
      {
          .Header = {sizeof(NevercTargetRegisterDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Name = STRING_VIEW("r0"),
          .Aliases = {RegisterAliases, 1, sizeof(RegisterAliases[0])},
          .AdditionalNames = {AdditionalRegisterNames, 1,
                              sizeof(AdditionalRegisterNames[0])},
          .RegisterNumber = 0,
      },
  };
  static const int32_t ImmediateValues[] = {1, 3, 7};
  static const NevercTargetConstraintDescriptor Constraints[] = {
      {
          .Header = {sizeof(NevercTargetConstraintDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Spelling = STRING_VIEW("I"),
          .ConvertedConstraint = STRING_VIEW("I"),
          .Flags = NEVERC_TARGET_CONSTRAINT_IMMEDIATE,
          .ImmediateValues = {ImmediateValues, 3,
                              sizeof(ImmediateValues[0])},
          .MatchingOperand = -1,
      },
      {
          .Header = {sizeof(NevercTargetConstraintDescriptor),
                     NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
          .Spelling = STRING_VIEW("r"),
          .ConvertedConstraint = STRING_VIEW("r"),
          .Flags = NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER,
          .RegisterClassID = UINT32_C(0x61000001),
          .MatchingOperand = -1,
      },
  };
  const void *Table = NULL;
  const NevercTargetAPI *TargetAPI;
  const NevercTargetABIAPI *ABIAPI;
  const NevercCallingConventionAPI *CallingConventionAPI;
  NevercTargetDescriptor Target;
  NevercTargetABIDescriptor ABIDescriptor;
  NevercCallingConventionDescriptor CallingConventionDescriptor;
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

  memset(&Target, 0, sizeof(Target));
  Target.Header = (NevercABITableHeader){
      sizeof(Target), NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0};
  Target.TargetID = TargetID;
  Target.CanonicalName =
      (NevercStringView)STRING_VIEW("test.language-target");
  Target.DefaultABI = ABI;
  Target.DefaultCallingConvention = CallingConvention;
  Target.Machine.Header = (NevercABITableHeader){
      sizeof(Target.Machine), NEVERC_TARGET_API_MAJOR,
      NEVERC_TARGET_API_MINOR, 0};
  Target.Machine.RawTriple =
      (NevercStringView)STRING_VIEW("testlang-unknown-none-none");
  Target.Machine.Architecture =
      (NevercStringView)STRING_VIEW("testlang");
  Target.Machine.DataLayout = (NevercStringView)STRING_VIEW(
      "e-p:64:64-i64:64-n32:64-S128");
  Target.Machine.DefaultCPU =
      (NevercStringView)STRING_VIEW("generic");
  Target.Machine.SchemaDigest = (NevercStringView)STRING_VIEW(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
  Target.Machine.CPUs =
      (NevercStringArrayView){CPUValues, 2, sizeof(CPUValues[0])};
  Target.Machine.Features =
      (NevercStructArrayView){Features, 2, sizeof(Features[0])};
  Target.Machine.ABIs =
      (NevercInterfaceIDArrayView){&ABI, 1, sizeof(ABI)};
  Target.Machine.CallingConventions =
      (NevercInterfaceIDArrayView){&CallingConvention, 1,
                                   sizeof(CallingConvention)};
  Target.Machine.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC;
  Target.Machine.SupportedCodeModels =
      NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
  Target.Machine.DefaultRelocationModel = NEVERC_TARGET_RELOCATION_STATIC;
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
  Target.Machine.BuiltinVaListKind = NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Target.Machine.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.DefaultExecutionLevel = NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.TLSSupported = NEVERC_TRUE;
  Target.Macros = (NevercStructArrayView){Macros, 1, sizeof(Macros[0])};
  Target.Builtins =
      (NevercStructArrayView){Builtins, 1, sizeof(Builtins[0])};
  Target.Registers =
      (NevercStructArrayView){Registers, 1, sizeof(Registers[0])};
  Target.Constraints =
      (NevercStructArrayView){Constraints, 2, sizeof(Constraints[0])};
  Target.Clobbers = (NevercStringView)STRING_VIEW("~{flags}");
  Target.ValidateCPU = validate_cpu;
  Target.CanonicalizeCPU = canonicalize_cpu;
  Target.ListCPUs = list_cpus;
  Target.ResolveFeatures = resolve_features;
  Status = TargetAPI->RegisterTarget(
      TargetAPI->Context, RegistrarContext, &Target);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Table = NULL;
  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_TARGET_ABI_HIGH,
                          NEVERC_INTERFACE_TARGET_ABI_LOW},
      NEVERC_TARGET_ABI_API_MAJOR, NEVERC_TARGET_ABI_API_MINOR, &Table,
      sizeof(NevercTargetABIAPI));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ABIAPI = (const NevercTargetABIAPI *)Table;
  memset(&ABIDescriptor, 0, sizeof(ABIDescriptor));
  ABIDescriptor.Header = (NevercABITableHeader){
      sizeof(ABIDescriptor), NEVERC_TARGET_ABI_API_MAJOR,
      NEVERC_TARGET_ABI_API_MINOR, 0};
  ABIDescriptor.ABIID = ABI;
  ABIDescriptor.TargetID = TargetID;
  ABIDescriptor.CanonicalName =
      (NevercStringView)STRING_VIEW("test.language-abi");
  ABIDescriptor.ClassifyFunction = classify_function;
  ABIDescriptor.VAArg.Header = (NevercABITableHeader){
      sizeof(ABIDescriptor.VAArg), NEVERC_TARGET_ABI_API_MAJOR,
      NEVERC_TARGET_ABI_API_MINOR, 0};
  ABIDescriptor.VAArg.Kind = NEVERC_ABI_VA_ARG_LLVM;
  Status = ABIAPI->RegisterABI(
      ABIAPI->Context, RegistrarContext, &ABIDescriptor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Table = NULL;
  Status = query_interface(
      Core,
      (NevercInterfaceID){NEVERC_INTERFACE_CALLING_CONVENTION_HIGH,
                          NEVERC_INTERFACE_CALLING_CONVENTION_LOW},
      NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, &Table,
      sizeof(NevercCallingConventionAPI));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  CallingConventionAPI = (const NevercCallingConventionAPI *)Table;
  memset(&CallingConventionDescriptor, 0,
         sizeof(CallingConventionDescriptor));
  CallingConventionDescriptor.Header = (NevercABITableHeader){
      sizeof(CallingConventionDescriptor),
      NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, 0};
  CallingConventionDescriptor.CallingConventionID = CallingConvention;
  CallingConventionDescriptor.TargetID = TargetID;
  CallingConventionDescriptor.CanonicalName =
      (NevercStringView)STRING_VIEW("test.language-calling-convention");
  return CallingConventionAPI->RegisterCallingConvention(
      CallingConventionAPI->Context, RegistrarContext,
      &CallingConventionDescriptor);
}

static NevercStatus NEVERC_CALL register_plugin(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *ProcessStateValue) {
  (void)Registrar;
  (void)ProcessStateValue;
  return register_target(Core, RegistrarContext);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap,
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
      (NevercStringView)STRING_VIEW("org.neverc.test.target-language");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Target Language Test Plugin");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
