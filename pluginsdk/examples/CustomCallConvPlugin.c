/*
 * CustomCallConvPlugin.c -- first-release typed IR plugin example.
 *
 * The plugin assigns NeverC's data-driven custom calling convention either
 * from source-level `custom_attr("neverc-callconv", "...")` attributes or
 * from namespaced plugin options:
 *
 *   -fplugin=./CustomCallConvPlugin.so
 *   -fplugin-arg=org.neverc.example.custom-callconv:cc-all
 *   -fplugin-arg=org.neverc.example.custom-callconv:ccspec=gpr:rcx;ret:rax
 */

#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginIR.h"
#include <stddef.h>
#include <string.h>

#define PLUGIN_ID "org.neverc.example.custom-callconv"
#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

typedef struct ProcessState {
  const NevercCoreAPI *Core;
} ProcessState;

typedef struct SessionState {
  NevercStringView Spec;
  NevercStringView Prefix;
  NevercBool ApplyAll;
  NevercBool Shuffle;
} SessionState;

typedef struct TextSlice {
  const char *Data;
  uint64_t Length;
} TextSlice;

static const NevercIRPassAPI *PassAPI;

static const char *const Variants[] = {
    "gpr:rcx,rdx,r8,r9;ret:rax",
    "gpr:rdi,rsi,rdx,rcx;ret:rax",
    "gpr:r9,r8,r10,r11;ret:rdx",
    "gpr:rsi,rdi,rcx,rdx;ret:rcx",
};

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercBool equal_text(NevercStringView Value, const char *Text,
                             uint64_t Length) {
  return Value.Length == Length && Value.Data != NULL &&
                 memcmp(Value.Data, Text, (size_t)Length) == 0
             ? NEVERC_TRUE
             : NEVERC_FALSE;
}

static NevercBool starts_with(NevercStringView Value,
                              NevercStringView Prefix) {
  return Prefix.Length == 0 ||
                 (Value.Data != NULL && Prefix.Data != NULL &&
                  Value.Length >= Prefix.Length &&
                  memcmp(Value.Data, Prefix.Data, (size_t)Prefix.Length) == 0)
             ? NEVERC_TRUE
             : NEVERC_FALSE;
}

static NevercBool bool_value(NevercStringView Value) {
  return equal_text(Value, "1", 1) || equal_text(Value, "true", 4)
             ? NEVERC_TRUE
             : NEVERC_FALSE;
}

static char lower_ascii(char Value) {
  return Value >= 'A' && Value <= 'Z' ? (char)(Value + ('a' - 'A')) : Value;
}

static TextSlice trim_slice(TextSlice Value) {
  while (Value.Length != 0 &&
         (Value.Data[0] == ' ' || Value.Data[0] == '\t')) {
    ++Value.Data;
    --Value.Length;
  }
  while (Value.Length != 0 &&
         (Value.Data[Value.Length - 1] == ' ' ||
          Value.Data[Value.Length - 1] == '\t'))
    --Value.Length;
  return Value;
}

static NevercBool slice_equals(TextSlice Left, TextSlice Right) {
  uint64_t Index;
  Left = trim_slice(Left);
  Right = trim_slice(Right);
  if (Left.Length != Right.Length)
    return NEVERC_FALSE;
  for (Index = 0; Index != Left.Length; ++Index)
    if (lower_ascii(Left.Data[Index]) != lower_ascii(Right.Data[Index]))
      return NEVERC_FALSE;
  return NEVERC_TRUE;
}

static NevercBool find_segment(NevercStringView Spec, const char *Name,
                               uint64_t NameLength, TextSlice *OutValue) {
  uint64_t Begin = 0;
  while (Begin < Spec.Length) {
    uint64_t End = Begin;
    uint64_t Colon;
    TextSlice Key;
    while (End < Spec.Length && Spec.Data[End] != ';')
      ++End;
    Colon = Begin;
    while (Colon < End && Spec.Data[Colon] != ':')
      ++Colon;
    Key = (TextSlice){Spec.Data + Begin, Colon - Begin};
    if (Colon < End &&
        slice_equals(Key, (TextSlice){Name, NameLength}) == NEVERC_TRUE) {
      *OutValue = trim_slice(
          (TextSlice){Spec.Data + Colon + 1, End - Colon - 1});
      return NEVERC_TRUE;
    }
    Begin = End + 1;
  }
  return NEVERC_FALSE;
}

static NevercBool lists_overlap(TextSlice Left, TextSlice Right) {
  uint64_t LeftBegin = 0;
  while (LeftBegin < Left.Length) {
    uint64_t LeftEnd = LeftBegin;
    uint64_t RightBegin = 0;
    TextSlice LeftToken;
    while (LeftEnd < Left.Length && Left.Data[LeftEnd] != ',')
      ++LeftEnd;
    LeftToken =
        trim_slice((TextSlice){Left.Data + LeftBegin, LeftEnd - LeftBegin});
    while (RightBegin < Right.Length) {
      uint64_t RightEnd = RightBegin;
      TextSlice RightToken;
      while (RightEnd < Right.Length && Right.Data[RightEnd] != ',')
        ++RightEnd;
      RightToken = trim_slice(
          (TextSlice){Right.Data + RightBegin, RightEnd - RightBegin});
      if (LeftToken.Length != 0 && RightToken.Length != 0 &&
          slice_equals(LeftToken, RightToken) == NEVERC_TRUE)
        return NEVERC_TRUE;
      RightBegin = RightEnd + 1;
    }
    LeftBegin = LeftEnd + 1;
  }
  return NEVERC_FALSE;
}

static NevercBool has_csr_conflict(NevercStringView Spec) {
  static const struct {
    const char *Name;
    uint64_t Length;
  } ComparedSegments[] = {{"args", 4}, {"gpr", 3}, {"xmm", 3},
                          {"ret", 3},  {"ret_xmm", 7}};
  TextSlice CalleeSaved;
  uint64_t Index;
  if (find_segment(Spec, "csr", 3, &CalleeSaved) == NEVERC_FALSE)
    return NEVERC_FALSE;
  for (Index = 0;
       Index != sizeof(ComparedSegments) / sizeof(ComparedSegments[0]);
       ++Index) {
    TextSlice Values;
    if (find_segment(Spec, ComparedSegments[Index].Name,
                     ComparedSegments[Index].Length, &Values) == NEVERC_TRUE &&
        lists_overlap(CalleeSaved, Values) == NEVERC_TRUE)
      return NEVERC_TRUE;
  }
  return NEVERC_FALSE;
}

static NevercStatus emit_warning(const NevercCoreAPI *Core,
                                 const char *Message) {
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle Handle = {0};
  memset(&Diagnostic, 0, sizeof(Diagnostic));
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_WARNING;
  Diagnostic.PluginID = SV(PLUGIN_ID);
  Diagnostic.Message =
      (NevercStringView){Message, (uint64_t)strlen(Message)};
  return Core->EmitDiagnostic(Core->Context, &Diagnostic, &Handle);
}

static NevercStatus option_value(const NevercCoreAPI *Core,
                                 NevercSessionHandle Session,
                                 NevercStringView Spelling,
                                 NevercStringView *OutValue) {
  NevercStatus Status;
  if (Core == NULL || Core->GetPluginOptionValue == NULL || OutValue == NULL)
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  *OutValue = (NevercStringView){0};
  Status = Core->GetPluginOptionValue(Core->Context, Session, SV(PLUGIN_ID),
                                      Spelling, 0, OutValue);
  if (Status.Code == NEVERC_STATUS_NOT_FOUND)
    return neverc_status_ok();
  return Status;
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  ProcessState *State = NULL;
  NevercStatus Status;
  if (Core == NULL || OutProcessState == NULL ||
      Core->Header.StructSize <
          offsetof(NevercCoreAPI, GetPluginOptionValue) +
              sizeof(Core->GetPluginOptionValue))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  *OutProcessState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(ProcessState), (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Core = Core;
  *OutProcessState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *PluginProcessState, void **OutSessionState) {
  SessionState *State = NULL;
  NevercStringView Value = {0};
  NevercStatus Status;
  (void)PluginProcessState;
  if (Core == NULL || OutSessionState == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSessionState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State),
                          _Alignof(SessionState), (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(State, 0, sizeof(*State));
  State->Spec = SV("gpr:r10,r11,rsi,rdi;ret:rdx");

  Status = option_value(Core, Session, SV("--ccspec"), &Value);
  if (Status.Code == NEVERC_STATUS_OK && Value.Length != 0)
    State->Spec = Value;
  if (Status.Code == NEVERC_STATUS_OK)
    Status = option_value(Core, Session, SV("--ccprefix"), &State->Prefix);
  if (Status.Code == NEVERC_STATUS_OK) {
    Value = (NevercStringView){0};
    Status = option_value(Core, Session, SV("--cc-all"), &Value);
    State->ApplyAll = bool_value(Value);
  }
  if (Status.Code == NEVERC_STATUS_OK) {
    Value = (NevercStringView){0};
    Status = option_value(Core, Session, SV("--ccshuffle"), &Value);
    State->Shuffle = bool_value(Value);
  }
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)Core->Deallocate(Core->Context, State, sizeof(*State),
                           _Alignof(SessionState));
    return Status;
  }
  *OutSessionState = State;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
session_end(const NevercCoreAPI *Core, NevercSessionHandle Session,
            void *PluginProcessState, void *PluginSessionState) {
  (void)Session;
  (void)PluginProcessState;
  if (Core == NULL || PluginSessionState == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, PluginSessionState,
                          sizeof(SessionState), _Alignof(SessionState));
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task, NevercTaskKind Kind,
           void *PluginProcessState, void *PluginSessionState,
           void **OutTaskState) {
  (void)Core;
  (void)Task;
  (void)Kind;
  (void)PluginProcessState;
  if (PluginSessionState == NULL || OutTaskState == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutTaskState = PluginSessionState;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task, NevercTaskKind Kind,
         void *PluginProcessState, void *PluginSessionState,
         void *PluginTaskState) {
  (void)Core;
  (void)Task;
  (void)Kind;
  (void)PluginProcessState;
  return PluginSessionState == PluginTaskState
             ? neverc_status_ok()
             : failure(NEVERC_STATUS_WRONG_SCOPE);
}

static NevercStatus collect_functions(
    ProcessState *Process, const NevercIRPassInvocation *Invocation,
    NevercIRValueHandle **OutFunctions, uint64_t *OutCount,
    uint64_t *OutCapacity) {
  NevercIRValueCursor Cursor = {0};
  NevercIRValueHandle Batch[64];
  NevercStatus Status;
  *OutFunctions = NULL;
  *OutCount = 0;
  *OutCapacity = 0;
  Status = Invocation->Core->BeginValueCursor(
      Invocation->Core->Context, Invocation->Task, Invocation->Module,
      NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  for (;;) {
    uint64_t Count = 0;
    uint64_t Index;
    Status = Invocation->Core->CollectValueCursor(
        Invocation->Core->Context, Invocation->Task, &Cursor, Batch, 64,
        &Count);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (*OutCount + Count > *OutCapacity) {
      uint64_t NewCapacity = *OutCapacity == 0 ? 64 : *OutCapacity * 2;
      void *Replacement = NULL;
      while (NewCapacity < *OutCount + Count)
        NewCapacity *= 2;
      Status = Process->Core->Reallocate(
          Process->Core->Context, *OutFunctions,
          *OutCapacity * sizeof(**OutFunctions),
          NewCapacity * sizeof(**OutFunctions),
          _Alignof(NevercIRValueHandle), &Replacement);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
      *OutFunctions = (NevercIRValueHandle *)Replacement;
      *OutCapacity = NewCapacity;
    }
    for (Index = 0; Index != Count; ++Index)
      (*OutFunctions)[(*OutCount)++] = Batch[Index];
    if (Count != 64)
      return neverc_status_ok();
  }
}

static NevercStatus apply_layout(const NevercIRPassInvocation *Invocation,
                                 NevercIRValueHandle Function,
                                 NevercStringView Spec,
                                 NevercBool *OutAddressTaken) {
  NevercIRAttributeHandle Attribute = {0};
  uint64_t UseCount = 0;
  uint64_t UseIndex;
  NevercStatus Status = Invocation->Core->CreateStringAttribute(
      Invocation->Core->Context, Invocation->Task, SV("neverc-callconv"), Spec,
      &Attribute);
  *OutAddressTaken = NEVERC_FALSE;
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->AddFunctionAttribute(
        Invocation->Core->Context, Invocation->Task, Function,
        NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->SetFunctionCallingConvention(
        Invocation->Core->Context, Invocation->Task, Function,
        NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->GetValueUseCount(
        Invocation->Core->Context, Invocation->Task, Function, &UseCount);
  for (UseIndex = 0;
       Status.Code == NEVERC_STATUS_OK && UseIndex != UseCount; ++UseIndex) {
    NevercIRUseInfo Use;
    NevercIRValueKind Kind = NEVERC_IR_VALUE_UNKNOWN;
    NevercIROpcode Opcode = NEVERC_IR_OPCODE_UNKNOWN;
    uint64_t OperandCount = 0;
    NevercBool IsDirectCall = NEVERC_FALSE;
    memset(&Use, 0, sizeof(Use));
    Use.Header = (NevercABITableHeader){sizeof(Use), NEVERC_IR_CORE_API_MAJOR,
                                       NEVERC_IR_CORE_API_MINOR, 0};
    Status = Invocation->Core->GetValueUse(
        Invocation->Core->Context, Invocation->Task, Function, UseIndex, &Use);
    if (Status.Code != NEVERC_STATUS_OK)
      break;
    Status = Invocation->Core->GetValueKind(
        Invocation->Core->Context, Invocation->Task, Use.User, &Kind);
    if (Status.Code != NEVERC_STATUS_OK)
      break;
    if (Kind == NEVERC_IR_VALUE_INSTRUCTION) {
      Status = Invocation->Core->GetInstructionOpcode(
          Invocation->Core->Context, Invocation->Task, Use.User, &Opcode);
      if (Status.Code != NEVERC_STATUS_OK)
        break;
      if (Opcode == NEVERC_IR_OPCODE_CALL ||
          Opcode == NEVERC_IR_OPCODE_INVOKE ||
          Opcode == NEVERC_IR_OPCODE_CALL_BR) {
        Status = Invocation->Core->GetOperandCount(
            Invocation->Core->Context, Invocation->Task, Use.User,
            &OperandCount);
        if (Status.Code != NEVERC_STATUS_OK)
          break;
        IsDirectCall =
            OperandCount != 0 && Use.OperandIndex == OperandCount - 1
                ? NEVERC_TRUE
                : NEVERC_FALSE;
      }
    }
    if (IsDirectCall == NEVERC_TRUE) {
      NevercIRPropertyValue CallingConvention;
      memset(&CallingConvention, 0, sizeof(CallingConvention));
      CallingConvention.Header =
          (NevercABITableHeader){sizeof(CallingConvention),
                                NEVERC_IR_CORE_API_MAJOR,
                                NEVERC_IR_CORE_API_MINOR, 0};
      CallingConvention.Kind = NEVERC_IR_PROPERTY_VALUE_ENUM;
      CallingConvention.UnsignedValue =
          NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM;
      Status = Invocation->Core->SetInstructionProperty(
          Invocation->Core->Context, Invocation->Task, Use.User,
          NEVERC_IR_PROPERTY_CALLING_CONVENTION, CallingConvention);
    } else {
      *OutAddressTaken = NEVERC_TRUE;
    }
  }
  return Status;
}

static NevercStatus NEVERC_CALL
run_pass(const NevercIRPassInvocation *Invocation,
         NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  ProcessState *Process = (ProcessState *)UserData;
  SessionState *Session = NULL;
  NevercIRValueHandle *Functions = NULL;
  uint64_t FunctionCount = 0;
  uint64_t Capacity = 0;
  uint64_t Index;
  uint64_t Variant = 0;
  NevercBool Changed = NEVERC_FALSE;
  NevercStatus Status;
  if (Invocation == NULL || OutPreserved == NULL || Process == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetTaskState(Process->Core->Context,
                                       Invocation->Task, SV(PLUGIN_ID),
                                       (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = collect_functions(Process, Invocation, &Functions, &FunctionCount,
                             &Capacity);
  if (Status.Code != NEVERC_STATUS_OK)
    goto Cleanup;

  for (Index = 0; Index != FunctionCount; ++Index) {
    NevercStringView Name = {0};
    NevercStringView Spec = {0};
    NevercBool HasAttribute = NEVERC_FALSE;
    NevercBool AddressTaken = NEVERC_FALSE;
    Status = Invocation->Core->GetValueName(
        Invocation->Core->Context, Invocation->Task, Functions[Index], &Name);
    if (Status.Code != NEVERC_STATUS_OK)
      goto Cleanup;
    if (Session->ApplyAll == NEVERC_TRUE &&
        starts_with(Name, Session->Prefix) == NEVERC_TRUE) {
      if (Session->Shuffle == NEVERC_TRUE) {
        const char *Text = Variants[Variant++ % 4];
        Spec = (NevercStringView){Text, (uint64_t)strlen(Text)};
      } else {
        Spec = Session->Spec;
      }
    } else {
      Status = Invocation->Core->HasFunctionAttribute(
          Invocation->Core->Context, Invocation->Task, Functions[Index],
          SV("neverc-callconv"), &HasAttribute);
      if (Status.Code != NEVERC_STATUS_OK)
        goto Cleanup;
      if (HasAttribute == NEVERC_FALSE)
        continue;
      Status = Invocation->Core->GetFunctionStringAttribute(
          Invocation->Core->Context, Invocation->Task, Functions[Index],
          SV("neverc-callconv"), &Spec);
      if (Status.Code != NEVERC_STATUS_OK)
        goto Cleanup;
    }
    if (Spec.Length == 0)
      continue;
    Status = apply_layout(Invocation, Functions[Index], Spec, &AddressTaken);
    if (Status.Code != NEVERC_STATUS_OK)
      goto Cleanup;
    if (AddressTaken == NEVERC_TRUE)
      Status = emit_warning(
          Process->Core,
          "custom calling convention applied to a function whose address is "
          "taken; indirect calls will not use it and may violate the ABI");
    if (Status.Code == NEVERC_STATUS_OK &&
        has_csr_conflict(Spec) == NEVERC_TRUE)
      Status = emit_warning(
          Process->Core,
          "a register is listed as both callee-saved and an argument/return "
          "register; this is likely an ABI mistake");
    if (Status.Code != NEVERC_STATUS_OK)
      goto Cleanup;
    Changed = NEVERC_TRUE;
  }

Cleanup:
  if (Functions != NULL)
    (void)Process->Core->Deallocate(
        Process->Core->Context, Functions,
        Capacity * sizeof(NevercIRValueHandle),
        _Alignof(NevercIRValueHandle));
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  OutPreserved->Flags = Changed == NEVERC_TRUE ? NEVERC_IR_PRESERVE_NONE
                                               : NEVERC_IR_PRESERVE_ALL;
  return neverc_status_ok();
}

static NevercStatus register_option(const NevercRegistrarAPI *Registrar,
                                    void *RegistrarContext,
                                    NevercStringView Spelling,
                                    NevercOptionForm Form,
                                    NevercOptionValueType Type) {
  NevercOptionDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_DRIVER_API_MAJOR,
                            NEVERC_DRIVER_API_MINOR, 0};
  Descriptor.Spelling = Spelling;
  Descriptor.Form = Form;
  Descriptor.ValueType = Type;
  Descriptor.Multiplicity = NEVERC_OPTION_LAST_WINS;
  return Registrar->RegisterOption(RegistrarContext, &Descriptor);
}

static NevercStatus register_pass(void *RegistrarContext,
                                  NevercStringView PassID,
                                  NevercInterfaceID Phase,
                                  ProcessState *Process) {
  NevercIRPassDescriptor Descriptor;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  Descriptor.PassID = PassID;
  Descriptor.Phase = Phase;
  Descriptor.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.Run = run_pass;
  Descriptor.UserData = Process;
  return PassAPI->RegisterPass(PassAPI->Context, RegistrarContext,
                               &Descriptor);
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *PluginProcessState) {
  ProcessState *Process = (ProcessState *)PluginProcessState;
  NevercStatus Status;
  (void)Core;
  if (Registrar == NULL || PassAPI == NULL || Process == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = register_option(Registrar, RegistrarContext, SV("--cc-all"),
                           NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_option(Registrar, RegistrarContext, SV("--ccspec"),
                             NEVERC_OPTION_SEPARATE, NEVERC_OPTION_STRING);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_option(Registrar, RegistrarContext, SV("--ccprefix"),
                             NEVERC_OPTION_SEPARATE, NEVERC_OPTION_STRING);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_option(Registrar, RegistrarContext, SV("--ccshuffle"),
                             NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = register_pass(
        RegistrarContext, SV("customcc.apply"),
        (NevercInterfaceID){NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
                            NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
        Process);
  return Status;
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                               void *PluginProcessState) {
  if (Core == NULL || PluginProcessState == NULL)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  return Core->Deallocate(Core->Context, PluginProcessState,
                          sizeof(ProcessState), _Alignof(ProcessState));
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercInterfaceID Interface = {NEVERC_INTERFACE_IR_PASS_HIGH,
                                 NEVERC_INTERFACE_IR_PASS_LOW};
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t BytesToWrite;
  NevercStatus Status;
  if (Bootstrap == NULL || Bootstrap->QueryInterface == NULL ||
      OutPlugin == NULL ||
      OutPlugin->Header.StructSize < (uint32_t)sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context, Interface, NEVERC_IR_PASS_API_MAJOR,
      NEVERC_IR_PASS_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Table == NULL ||
      StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                       sizeof(((NevercIRPassAPI *)0)->RegisterPass))
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercIRPassAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = SV(PLUGIN_ID);
  Descriptor.DisplayName = SV("NeverC custom calling convention example");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
  Descriptor.Destroy = destroy_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
