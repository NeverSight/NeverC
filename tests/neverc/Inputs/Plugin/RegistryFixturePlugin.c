#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginTarget.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NEVERC_TEST_PLUGIN_ID
#define NEVERC_TEST_PLUGIN_ID "org.neverc.test.minimal"
#endif

#ifndef NEVERC_TEST_PLUGIN_DISPLAY_NAME
#define NEVERC_TEST_PLUGIN_DISPLAY_NAME "NeverC Registry Test Plugin"
#endif

#ifndef NEVERC_TEST_PLUGIN_ABI_MAJOR
#define NEVERC_TEST_PLUGIN_ABI_MAJOR NEVERC_PLUGIN_ABI_MAJOR
#endif

#ifndef NEVERC_TEST_PLUGIN_VERSION_MAJOR
#define NEVERC_TEST_PLUGIN_VERSION_MAJOR 1
#endif

#ifndef NEVERC_TEST_PLUGIN_VERSION_MINOR
#define NEVERC_TEST_PLUGIN_VERSION_MINOR 2
#endif

#ifndef NEVERC_TEST_PLUGIN_VERSION_PATCH
#define NEVERC_TEST_PLUGIN_VERSION_PATCH 3
#endif

#ifndef NEVERC_TEST_CONCURRENCY
#define NEVERC_TEST_CONCURRENCY NEVERC_CONCURRENCY_SESSION_SERIAL
#endif

#ifndef NEVERC_TEST_REENTRANCY
#define NEVERC_TEST_REENTRANCY NEVERC_REENTRANCY_NONE
#endif

#define NEVERC_TEST_STRING_VIEW(value)                                        \
  { (value), (uint64_t)(sizeof(value) - 1) }

#if defined(NEVERC_TEST_REQUIRED_INTERFACE_HIGH)
static const NevercInterfaceRequirement RequiredInterfaces[] = {{
    .Header = {sizeof(NevercInterfaceRequirement), NEVERC_PLUGIN_ABI_MAJOR,
               NEVERC_PLUGIN_ABI_MINOR, 0},
    .Interface = {NEVERC_TEST_REQUIRED_INTERFACE_HIGH,
                  NEVERC_TEST_REQUIRED_INTERFACE_LOW},
    .Major = NEVERC_TEST_REQUIRED_INTERFACE_MAJOR,
    .MinimumMinor = NEVERC_TEST_REQUIRED_INTERFACE_MINOR,
    .Required = NEVERC_TRUE,
    .Stability = NEVERC_INTERFACE_STABLE,
    .Compatibility =
        {
            .Header = {sizeof(NevercCompatibilityKey),
                       NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0},
        },
}};
#endif

#if defined(NEVERC_TEST_DEPENDENCY_ID)
static const NevercPluginDependency Dependencies[] = {{
    .Header = {sizeof(NevercPluginDependency), NEVERC_PLUGIN_ABI_MAJOR,
               NEVERC_PLUGIN_ABI_MINOR, 0},
    .PluginID =
        (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_DEPENDENCY_ID),
    .Version =
        {
            .MinimumInclusive =
                {
                    .Major = NEVERC_TEST_DEPENDENCY_MIN_MAJOR,
                    .Minor = NEVERC_TEST_DEPENDENCY_MIN_MINOR,
                    .Patch = NEVERC_TEST_DEPENDENCY_MIN_PATCH,
                },
#if defined(NEVERC_TEST_DEPENDENCY_MAX_MAJOR)
            .MaximumExclusive =
                {
                    .Major = NEVERC_TEST_DEPENDENCY_MAX_MAJOR,
                    .Minor = NEVERC_TEST_DEPENDENCY_MAX_MINOR,
                    .Patch = NEVERC_TEST_DEPENDENCY_MAX_PATCH,
                },
            .HasMaximum = NEVERC_TRUE,
#endif
            .AllowPrerelease = NEVERC_FALSE,
        },
    .Kind = NEVERC_TEST_DEPENDENCY_KIND,
}};
#endif

static int ProcessStateStorage;

static void trace_event(const char *Event) {
  const char *Path = getenv("NEVERC_PLUGIN_TRACE_FILE");
  FILE *Trace;
  if (Path == NULL || Path[0] == '\0')
    return;
  Trace = fopen(Path, "ab");
  if (Trace == NULL)
    return;
  fprintf(Trace, "%s:%s\n", NEVERC_TEST_PLUGIN_ID, Event);
  fclose(Trace);
}

static NevercStatus failed_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
  return Status;
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  (void)Core;
  trace_event("process_begin");
  if (OutProcessState == NULL)
    return failed_status();
  *OutProcessState = NULL;
#if defined(NEVERC_TEST_PROCESS_BEGIN_FAILURE)
  return failed_status();
#else
  *OutProcessState = &ProcessStateStorage;
  return neverc_status_ok();
#endif
}

#if defined(NEVERC_TEST_SCOPE_CALLBACKS)
typedef struct FixtureScopeState {
  const NevercCoreAPI *Core;
  uint64_t Marker;
} FixtureScopeState;

static NevercStatus allocate_scope_state(const NevercCoreAPI *Core,
                                         uint64_t Marker, void **OutState) {
  FixtureScopeState *State = NULL;
  NevercStatus Status;
  if (OutState == NULL)
    return failed_status();
  *OutState = NULL;
  Status = Core->Allocate(Core->Context, sizeof(*State), UINT64_C(8),
                          (void **)&State);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  State->Core = Core;
  State->Marker = Marker;
  *OutState = State;
  return neverc_status_ok();
}

static NevercStatus release_scope_state(void *State) {
  FixtureScopeState *ScopeState = (FixtureScopeState *)State;
  if (ScopeState == NULL)
    return neverc_status_ok();
  return ScopeState->Core->Deallocate(ScopeState->Core->Context, ScopeState,
                                      sizeof(*ScopeState), UINT64_C(8));
}

static NevercStatus NEVERC_CALL
session_begin(const NevercCoreAPI *Core, NevercSessionHandle Session,
              void *ProcessState, void **OutSessionState) {
  (void)Session;
  (void)ProcessState;
  trace_event("session_begin");
#if defined(NEVERC_TEST_SESSION_BEGIN_FAILURE)
  if (OutSessionState != NULL)
    *OutSessionState = NULL;
  return failed_status();
#else
  return allocate_scope_state(Core, UINT64_C(0x53455353494f4e), OutSessionState);
#endif
}

static NevercStatus NEVERC_CALL
session_end(const NevercCoreAPI *Core, NevercSessionHandle Session,
            void *ProcessState, void *SessionState) {
  (void)Core;
  (void)Session;
  (void)ProcessState;
  trace_event("session_end");
  return release_scope_state(SessionState);
}

static NevercStatus NEVERC_CALL
task_begin(const NevercCoreAPI *Core, NevercTaskHandle Task,
           NevercTaskKind Kind, void *ProcessState, void *SessionState,
           void **OutTaskState) {
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  (void)SessionState;
  trace_event("task_begin");
#if defined(NEVERC_TEST_TASK_BEGIN_FAILURE)
  if (OutTaskState != NULL)
    *OutTaskState = NULL;
  return failed_status();
#else
  return allocate_scope_state(Core, UINT64_C(0x5441534b), OutTaskState);
#endif
}

static NevercStatus NEVERC_CALL
task_end(const NevercCoreAPI *Core, NevercTaskHandle Task,
         NevercTaskKind Kind, void *ProcessState, void *SessionState,
         void *TaskState) {
  (void)Core;
  (void)Task;
  (void)Kind;
  (void)ProcessState;
  (void)SessionState;
  trace_event("task_end");
  return release_scope_state(TaskState);
}
#endif

static NevercStatus NEVERC_CALL
fixture_observer(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                 void *UserData) {
  (void)Frame;
  (void)Point;
  (void)UserData;
  return neverc_status_ok();
}

static void NEVERC_CALL destroy_fixture_userdata(void *UserData) {
  trace_event((const char *)UserData);
}

#if defined(NEVERC_TEST_REGISTER_TARGET)
#ifndef NEVERC_TEST_TARGET_ID_LOW
#define NEVERC_TEST_TARGET_ID_LOW UINT64_C(1)
#endif
#ifndef NEVERC_TEST_TARGET_NAME
#define NEVERC_TEST_TARGET_NAME "test.fixture-target"
#endif
#ifndef NEVERC_TEST_TARGET_RAW_TRIPLE
#define NEVERC_TEST_TARGET_RAW_TRIPLE "test-unknown-none-none"
#endif
#ifndef NEVERC_TEST_TARGET_ARCHITECTURE
#define NEVERC_TEST_TARGET_ARCHITECTURE "test"
#endif

static const NevercTargetID FixtureTargetID = {
    UINT64_C(0x4e43545445535401), NEVERC_TEST_TARGET_ID_LOW};
static const NevercTargetABIID FixtureABIID = {
    UINT64_C(0x4e43544142495401), NEVERC_TEST_TARGET_ID_LOW};
static const NevercCallingConventionID FixtureCallingConventionID = {
    UINT64_C(0x4e43544343495401), NEVERC_TEST_TARGET_ID_LOW};

#if defined(NEVERC_TEST_REGISTER_NOBJ_BACKEND)
static const NevercObjectFormatID FixtureNObjFormatID = {
    UINT64_C(0x4e434e4f424a0001), NEVERC_TEST_TARGET_ID_LOW};
static const NevercInterfaceID FixtureNObjEdgeID = {
    UINT64_C(0x4e434e4f424a4544), NEVERC_TEST_TARGET_ID_LOW};
static const NevercInterfaceID FixtureNObjProductID = {
    UINT64_C(0x4e434e4f424a5052), NEVERC_TEST_TARGET_ID_LOW};
static const NevercInterfaceID FixtureNObjSchemaID = {
    UINT64_C(0x4e434e4f424a5343), NEVERC_TEST_TARGET_ID_LOW};
static const char FixtureNObjSchemaDigest[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static NevercStatus NEVERC_CALL parse_fixture_nobj_assembly(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  const NevercAssemblyProviderAPI *Assembly =
      (const NevercAssemblyProviderAPI *)UserData;
  static const char SourceText[] = ".nobj_answer\n";
  static const uint8_t Code[] = {UINT8_C(0x2a), UINT8_C(0xc3)};
  NevercAssemblyParseInputInfo Input;
  const NevercMCAPI *MC = NULL;
  NevercMCUnitHandle Unit = {0, 0};
  NevercMCMutationHandle Mutation = {0, 0};
  NevercMCSectionHandle Section = {0, 0};
  NevercMCFragmentHandle Fragment = {0, 0};
  NevercMCSymbolHandle Symbol = {0, 0};
  NevercArtifactHandle Output = {0, 0};
  NevercMCSectionDescriptor SectionDescriptor;
  NevercMCFragmentDescriptor FragmentDescriptor;
  NevercMCSymbolDescriptor SymbolDescriptor;
  NevercStatus Status;

  if (Frame == NULL || OutResult == NULL || Assembly == NULL)
    return failed_status();
  memset(&Input, 0, sizeof(Input));
  Input.Header = (NevercABITableHeader){
      sizeof(Input), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
  Status = Assembly->GetParseInput(
      Assembly->Context, Frame, Frame->Input, &Input);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Input.Source.Buffer.Data == NULL ||
      Input.Source.Buffer.Length != sizeof(SourceText) - 1 ||
      memcmp(Input.Source.Buffer.Data, SourceText,
             sizeof(SourceText) - 1) != 0)
    return failed_status();

  Status = Assembly->GetParseMCBuilder(
      Assembly->Context, Frame, &MC, &Unit);
  if (Status.Code != NEVERC_STATUS_OK || MC == NULL)
    return failed_status();
  Status = MC->BeginMutation(
      MC->Context, Frame->Task, Unit, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&SectionDescriptor, 0, sizeof(SectionDescriptor));
  SectionDescriptor.Header = (NevercABITableHeader){
      sizeof(SectionDescriptor), NEVERC_MC_API_MAJOR,
      NEVERC_MC_API_MINOR, 0};
  SectionDescriptor.Name =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(".text");
  SectionDescriptor.Alignment = 1;
  SectionDescriptor.Flags =
      NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
  Status = MC->CreateSection(
      MC->Context, Frame->Task, Mutation, &SectionDescriptor, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abandon;

  memset(&FragmentDescriptor, 0, sizeof(FragmentDescriptor));
  FragmentDescriptor.Header = (NevercABITableHeader){
      sizeof(FragmentDescriptor), NEVERC_MC_API_MAJOR,
      NEVERC_MC_API_MINOR, 0};
  FragmentDescriptor.Kind = NEVERC_MC_FRAGMENT_DATA;
  FragmentDescriptor.ExplicitOffset = 0;
  FragmentDescriptor.Alignment = 1;
  FragmentDescriptor.Contents =
      (NevercByteView){Code, sizeof(Code)};
  Status = MC->CreateFragment(
      MC->Context, Frame->Task, Mutation, Section,
      &FragmentDescriptor, &Fragment);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abandon;

  memset(&SymbolDescriptor, 0, sizeof(SymbolDescriptor));
  SymbolDescriptor.Header = (NevercABITableHeader){
      sizeof(SymbolDescriptor), NEVERC_MC_API_MAJOR,
      NEVERC_MC_API_MINOR, 0};
  SymbolDescriptor.Name =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("answer");
  SymbolDescriptor.Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
  SymbolDescriptor.Visibility = NEVERC_MC_SYMBOL_VISIBILITY_DEFAULT;
  SymbolDescriptor.Type = NEVERC_MC_SYMBOL_TYPE_FUNCTION;
  SymbolDescriptor.Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
  SymbolDescriptor.Section = Section;
  SymbolDescriptor.Size = sizeof(Code);
  SymbolDescriptor.Alignment = 1;
  Status = MC->CreateSymbol(
      MC->Context, Frame->Task, Mutation, &SymbolDescriptor, &Symbol);
  if (Status.Code != NEVERC_STATUS_OK)
    goto abandon;

  Status = MC->CommitMutation(MC->Context, Frame->Task, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Assembly->PublishParsedMCUnit(
      Assembly->Context, Frame, &Output);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_REPLACE;
  OutResult->Output = Output;
  return neverc_status_ok();

abandon:
  (void)MC->AbandonMutation(
      MC->Context, Frame->Task, Mutation);
  return Status;
}

static uint32_t read_u32_le(const uint8_t *Data) {
  return (uint32_t)Data[0] | ((uint32_t)Data[1] << 8) |
         ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24);
}

static void write_u32_le(uint8_t *Data, uint32_t Value) {
  Data[0] = (uint8_t)Value;
  Data[1] = (uint8_t)(Value >> 8);
  Data[2] = (uint8_t)(Value >> 16);
  Data[3] = (uint8_t)(Value >> 24);
}

static NevercStatus NEVERC_CALL probe_fixture_nobj(
    void *UserData, const NevercObjectProbeRequest *Request,
    NevercObjectProbeResult *Result) {
  (void)UserData;
  if (Request == NULL || Result == NULL)
    return failed_status();
  Result->Confidence = 0;
  Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_UNKNOWN;
  Result->ConsumedMinimum = 4;
  if (Request->Input.Length >= 4 &&
      memcmp(Request->Input.Data, "NOBJ", 4) == 0) {
    Result->Confidence = NEVERC_OBJECT_PROBE_MAX_CONFIDENCE;
    Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_RELOCATABLE;
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL read_fixture_nobj(
    void *UserData, const NevercObjectReadRequest *Request) {
  NevercObjectSectionDescriptor Section;
  NevercObjectSymbolDescriptor Symbol;
  NevercObjectSectionHandle SectionHandle;
  NevercObjectSymbolHandle SymbolHandle;
  NevercStatus Status;
  uint32_t TextSize;
  (void)UserData;

  if (Request == NULL || Request->Object == NULL ||
      Request->Input.Length < 19 ||
      memcmp(Request->Input.Data, "NOBJ", 4) != 0 ||
      read_u32_le(Request->Input.Data + 4) != 1)
    return failed_status();
  TextSize = read_u32_le(Request->Input.Data + 8);
  if (TextSize == 0 ||
      Request->Input.Length < (uint64_t)12 + TextSize + 7 ||
      memcmp(Request->Input.Data + 12 + TextSize, "answer", 7) != 0)
    return failed_status();

  memset(&Section, 0, sizeof(Section));
  Section.Header.StructSize = sizeof(Section);
  Section.Header.Major = NEVERC_OBJECT_API_MAJOR;
  Section.Header.Minor = NEVERC_OBJECT_API_MINOR;
  Section.Name = (NevercStringView)NEVERC_TEST_STRING_VIEW(".text");
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data.Data = Request->Input.Data + 12;
  Section.Data.Length = TextSize;
  Status = Request->Object->CreateSection(
      Request->Object->Context, Request->Task, Request->Mutation,
      &Section, &SectionHandle);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Symbol, 0, sizeof(Symbol));
  Symbol.Header.StructSize = sizeof(Symbol);
  Symbol.Header.Major = NEVERC_OBJECT_API_MAJOR;
  Symbol.Header.Minor = NEVERC_OBJECT_API_MINOR;
  Symbol.Name = (NevercStringView)NEVERC_TEST_STRING_VIEW("answer");
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Symbol.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.Section = SectionHandle;
  Symbol.Size = TextSize;
  Symbol.Alignment = 1;
  Symbol.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  return Request->Object->CreateSymbol(
      Request->Object->Context, Request->Task, Request->Mutation,
      &Symbol, &SymbolHandle);
}

static NevercStatus NEVERC_CALL write_fixture_nobj(
    void *UserData, const NevercObjectWriteRequest *Request) {
  NevercObjectSectionHandle Section;
  NevercObjectSectionInfo Info;
  NevercStatus Status;
  uint8_t Header[12] = {'N', 'O', 'B', 'J'};
  static const uint8_t Name[] = "answer";
  (void)UserData;

  if (Request == NULL || Request->Object == NULL ||
      Request->Binary == NULL)
    return failed_status();
  Status = Request->Object->GetFirstSection(
      Request->Object->Context, Request->Task, Request->Graph, &Section);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(&Info, 0, sizeof(Info));
  Info.Header.StructSize = sizeof(Info);
  Info.Header.Major = NEVERC_OBJECT_API_MAJOR;
  Info.Header.Minor = NEVERC_OBJECT_API_MINOR;
  Status = Request->Object->GetSectionInfo(
      Request->Object->Context, Request->Task, Section, &Info);
  if (Status.Code != NEVERC_STATUS_OK || Info.Data.Length > UINT32_MAX)
    return failed_status();

  write_u32_le(Header + 4, 1);
  write_u32_le(Header + 8, (uint32_t)Info.Data.Length);
  Status = Request->Binary->Write(
      Request->Binary->Context, Request->Task, Request->Builder,
      (NevercByteView){Header, sizeof(Header)});
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Request->Binary->Write(
      Request->Binary->Context, Request->Task, Request->Builder,
      Info.Data);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Request->Binary->Write(
      Request->Binary->Context, Request->Task, Request->Builder,
      (NevercByteView){Name, sizeof(Name)});
}

static NevercStatus NEVERC_CALL lower_fixture_nobj(
    void *UserData, NevercTaskHandle Task,
    const NevercCodeGenRequest *Request,
    NevercCodeGenProductCandidate *OutCandidate) {
  const NevercIOAPI *IO = (const NevercIOAPI *)UserData;
  static const uint8_t Image[] = {
      'N', 'O', 'B', 'J', 1, 0, 0, 0, 2, 0, 0, 0,
      UINT8_C(0x2a), UINT8_C(0xc3),
      'a', 'n', 's', 'w', 'e', 'r', 0};
  NevercOutputSinkHandle Sink;
  NevercOutputSeal Seal;
  NevercStatus Status;
  char Name[96];
  int NameLength;

  if (IO == NULL || Request == NULL || OutCandidate == NULL)
    return failed_status();
  NameLength = snprintf(Name, sizeof(Name), "test-target-%llu.nobj",
                        (unsigned long long)Task.Value);
  if (NameLength <= 0 || (size_t)NameLength >= sizeof(Name))
    return failed_status();
  Status = IO->BeginMemoryOutput(
      IO->Context, Task,
      (NevercStringView){Name, (uint64_t)NameLength}, 4096, &Sink);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = IO->OutputWrite(
      IO->Context, Task, Sink,
      (NevercByteView){Image, sizeof(Image)});
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)IO->OutputAbort(IO->Context, Task, Sink);
    return Status;
  }
  memset(&Seal, 0, sizeof(Seal));
  Seal.Header.StructSize = sizeof(Seal);
  Status = IO->OutputFinish(IO->Context, Task, Sink, &Seal);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)IO->OutputAbort(IO->Context, Task, Sink);
    return Status;
  }

  memset(OutCandidate, 0, sizeof(*OutCandidate));
  OutCandidate->Header.StructSize = sizeof(*OutCandidate);
  OutCandidate->Header.Major = NEVERC_TARGET_API_MAJOR;
  OutCandidate->Header.Minor = NEVERC_TARGET_API_MINOR;
  OutCandidate->Kind = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact = Seal.Handle;
  OutCandidate->ProductID = FixtureNObjProductID;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL verify_fixture_nobj_product(
    void *UserData, NevercTaskHandle Task,
    const NevercCodeGenRequest *Request,
    const NevercCodeGenProductCandidate *Candidate,
    NevercCodeGenVerificationObligations Obligations) {
  (void)UserData;
  (void)Task;
  (void)Obligations;
  if (Request == NULL || Candidate == NULL ||
      Candidate->Kind != NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE ||
      neverc_handle_is_null(Candidate->Artifact) ||
      Candidate->ProductID.High != FixtureNObjProductID.High ||
      Candidate->ProductID.Low != FixtureNObjProductID.Low)
    return failed_status();
  return neverc_status_ok();
}

static NevercStatus register_fixture_nobj_backend(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext) {
  const NevercAssemblyProviderAPI *AssemblyAPI;
  const NevercMCAPI *MCAPI;
  const NevercObjectFormatAPI *FormatAPI;
  const NevercTargetAPI *TargetAPI;
  const NevercIOAPI *IO;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercObjectFormatDescriptor Format;
  NevercCodeGenEdgeDescriptor Edge;
  NevercMCSchemaDescriptor Schema;
  NevercProviderDescriptor Provider;
  NevercStatus Status;

  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_OBJECT_FORMAT_HIGH,
                          NEVERC_INTERFACE_OBJECT_FORMAT_LOW},
      NEVERC_OBJECT_FORMAT_API_MAJOR, NEVERC_OBJECT_FORMAT_API_MINOR,
      &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercObjectFormatAPI))
    return failed_status();
  FormatAPI = (const NevercObjectFormatAPI *)Table;
  memset(&Format, 0, sizeof(Format));
  Format.Header.StructSize = sizeof(Format);
  Format.Header.Major = NEVERC_OBJECT_FORMAT_API_MAJOR;
  Format.Header.Minor = NEVERC_OBJECT_FORMAT_API_MINOR;
  Format.FormatID = FixtureNObjFormatID;
  Format.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("nobj");
  Format.DefaultExtension =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(".nobj");
  Format.SupportedTargets.Data = &FixtureTargetID;
  Format.SupportedTargets.Count = 1;
  Format.SupportedTargets.ElementStride = sizeof(FixtureTargetID);
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                 NEVERC_OBJECT_FORMAT_CAN_READ |
                 NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Probe = probe_fixture_nobj;
  Format.Reader = read_fixture_nobj;
  Format.Writer = write_fixture_nobj;
  Status = FormatAPI->RegisterFormat(
      FormatAPI->Context, RegistrarContext, &Format);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Table = NULL;
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_MC_HIGH,
                          NEVERC_INTERFACE_MC_LOW},
      NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercMCAPI))
    return failed_status();
  MCAPI = (const NevercMCAPI *)Table;
  memset(&Schema, 0, sizeof(Schema));
  Schema.Header = (NevercABITableHeader){
      sizeof(Schema), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  Schema.SchemaID = FixtureNObjSchemaID;
  Schema.TargetID = FixtureTargetID;
  Schema.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("nobj.mc");
  Schema.Digest =
      (NevercStringView){FixtureNObjSchemaDigest,
                         sizeof(FixtureNObjSchemaDigest) - 1};
  Status = MCAPI->RegisterSchema(
      MCAPI->Context, RegistrarContext, &Schema);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Table = NULL;
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_ASSEMBLY_PROVIDER_HIGH,
                          NEVERC_INTERFACE_ASSEMBLY_PROVIDER_LOW},
      NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR, &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercAssemblyProviderAPI))
    return failed_status();
  AssemblyAPI = (const NevercAssemblyProviderAPI *)Table;
  memset(&Provider, 0, sizeof(Provider));
  Provider.Header = (NevercABITableHeader){
      sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase = (NevercInterfaceID){
      NEVERC_PHASE_ASSEMBLY_PARSE_HIGH,
      NEVERC_PHASE_ASSEMBLY_PARSE_LOW};
  Provider.ProviderID =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("test.nobj.parser");
  Provider.Route.Header = (NevercABITableHeader){
      sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = parse_fixture_nobj_assembly;
  Provider.UserData = (void *)AssemblyAPI;
  Status = Registrar->RegisterProvider(RegistrarContext, &Provider);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Table = NULL;
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_IO_HIGH,
                          NEVERC_INTERFACE_IO_LOW},
      NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercIOAPI))
    return failed_status();
  IO = (const NevercIOAPI *)Table;

  Table = NULL;
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_TARGET_HIGH,
                          NEVERC_INTERFACE_TARGET_LOW},
      NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercTargetAPI))
    return failed_status();
  TargetAPI = (const NevercTargetAPI *)Table;
  memset(&Edge, 0, sizeof(Edge));
  Edge.Header.StructSize = sizeof(Edge);
  Edge.Header.Major = NEVERC_TARGET_API_MAJOR;
  Edge.Header.Minor = NEVERC_TARGET_API_MINOR;
  Edge.EdgeID = FixtureNObjEdgeID;
  Edge.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("test.ir-to-nobj");
  Edge.TargetID = FixtureTargetID;
  Edge.InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  Edge.OutputKind = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  Edge.Flags = NEVERC_CODEGEN_EDGE_COARSE;
  Edge.UserData = (void *)IO;
  Edge.CompatibilityKey =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("test-nobj-v1");
  Edge.ProviderID =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("test.coarse-nobj");
  Edge.ProductID = FixtureNObjProductID;
  Edge.CoarseLower = lower_fixture_nobj;
  Edge.VerifyProduct = verify_fixture_nobj_product;
  return TargetAPI->RegisterCodeGenEdge(
      TargetAPI->Context, RegistrarContext, &Edge);
}
#endif

#if defined(NEVERC_TEST_CC_PLAN_X86)
static const NevercTargetRegisterDescriptor FixtureCCRegisters[] = {
    {
        .Header = {sizeof(NevercTargetRegisterDescriptor),
                   NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
        .Name = NEVERC_TEST_STRING_VIEW("rax"),
        .RegisterNumber = 51,
    },
    {
        .Header = {sizeof(NevercTargetRegisterDescriptor),
                   NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
        .Name = NEVERC_TEST_STRING_VIEW("rcx"),
        .RegisterNumber = 54,
    },
    {
        .Header = {sizeof(NevercTargetRegisterDescriptor),
                   NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, 0},
        .Name = NEVERC_TEST_STRING_VIEW("r12"),
        .RegisterNumber = 123,
    },
};
#endif

#if defined(NEVERC_TEST_ABI_COERCE_AND_EXPAND_ARGUMENTS)
static const NevercABICoercionElement FixtureCoerceAndExpandElements[] = {
    {
        .Header = {sizeof(NevercABICoercionElement),
                   NEVERC_TARGET_ABI_API_MAJOR,
                   NEVERC_TARGET_ABI_API_MINOR, 0},
        .Coercion = NEVERC_ABI_COERCE_INTEGER,
        .BitWidth = 32,
        .Offset = 0,
    },
    {
        .Header = {sizeof(NevercABICoercionElement),
                   NEVERC_TARGET_ABI_API_MAJOR,
                   NEVERC_TARGET_ABI_API_MINOR, 0},
        .Coercion = NEVERC_ABI_COERCE_FLOAT,
        .BitWidth = 64,
        .Offset = 8,
    },
};
#endif

static void classify_fixture_argument(
    const NevercABITypeDescriptor *Type,
    NevercABIArgumentClassification *Classification,
    int IsReturnValue) {
  if (Type->Kind == NEVERC_ABI_TYPE_VOID) {
    Classification->Kind = NEVERC_ABI_ARGUMENT_IGNORE;
#if defined(NEVERC_TEST_ABI_FORCE_INDIRECT_ARGUMENTS)
  } else if (!IsReturnValue) {
    Classification->Kind = NEVERC_ABI_ARGUMENT_INDIRECT;
    Classification->Alignment =
        Type->Alignment == 0 ? UINT32_C(1) : Type->Alignment;
    Classification->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
#endif
#if defined(NEVERC_TEST_ABI_COERCE_AND_EXPAND_ARGUMENTS)
  } else if (!IsReturnValue &&
             (Type->Flags & NEVERC_ABI_TYPE_AGGREGATE) != 0 &&
             Type->BitWidth == UINT32_C(128)) {
    Classification->Kind =
        NEVERC_ABI_ARGUMENT_COERCE_AND_EXPAND;
    Classification->CoerceAndExpandSize = UINT32_C(16);
    Classification->CoerceAndExpandElements.Data =
        FixtureCoerceAndExpandElements;
    Classification->CoerceAndExpandElements.Count =
        sizeof(FixtureCoerceAndExpandElements) /
        sizeof(FixtureCoerceAndExpandElements[0]);
    Classification->CoerceAndExpandElements.ElementStride =
        sizeof(FixtureCoerceAndExpandElements[0]);
#endif
#if defined(NEVERC_TEST_ABI_INDIRECT_ALIASED_ARGUMENTS)
  } else if (!IsReturnValue &&
             (Type->Flags & NEVERC_ABI_TYPE_AGGREGATE) != 0) {
    Classification->Kind =
        NEVERC_ABI_ARGUMENT_INDIRECT_ALIASED;
    Classification->Alignment =
        Type->Alignment == 0 ? UINT32_C(1) : Type->Alignment;
#endif
  } else if ((Type->Flags & NEVERC_ABI_TYPE_AGGREGATE) != 0) {
    Classification->Kind = NEVERC_ABI_ARGUMENT_INDIRECT;
    Classification->Alignment =
        Type->Alignment == 0 ? UINT32_C(1) : Type->Alignment;
    Classification->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  } else if ((Type->Kind == NEVERC_ABI_TYPE_BOOLEAN ||
              Type->Kind == NEVERC_ABI_TYPE_INTEGER ||
              Type->Kind == NEVERC_ABI_TYPE_ENUM) &&
             Type->BitWidth < UINT32_C(32)) {
    Classification->Kind = NEVERC_ABI_ARGUMENT_EXTEND;
    if ((Type->Flags & NEVERC_ABI_TYPE_SIGNED) != 0)
      Classification->Flags = NEVERC_ABI_ARGUMENT_SIGN_EXTEND;
  } else {
    Classification->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  }
}

static NevercStatus NEVERC_CALL classify_fixture_function(
    void *UserData, const NevercABIFunctionQuery *Query,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *Arguments) {
  uint64_t Index;
  (void)UserData;
  if (Query == NULL || ReturnValue == NULL || Arguments == NULL ||
      Query->Parameters.Count != Arguments->Count ||
      Query->Parameters.ElementStride <
          sizeof(NevercABITypeDescriptor) ||
      Arguments->ElementStride <
          sizeof(NevercABIArgumentClassification))
    return failed_status();
  classify_fixture_argument(&Query->ReturnType, ReturnValue, 1);
  for (Index = 0; Index != Arguments->Count; ++Index) {
    const NevercABITypeDescriptor *Type =
        (const NevercABITypeDescriptor *)(
            (const uint8_t *)Query->Parameters.Data +
            Index * Query->Parameters.ElementStride);
    NevercABIArgumentClassification *Classification =
        (NevercABIArgumentClassification *)(
            (uint8_t *)Arguments->Data +
            Index * Arguments->ElementStride);
    classify_fixture_argument(Type, Classification, 0);
  }
  return neverc_status_ok();
}

#if defined(NEVERC_TEST_CC_PLAN_X86)
static NevercStatus NEVERC_CALL plan_fixture_calling_convention(
    void *UserData, const NevercCallingConventionQuery *Query,
    NevercCallingConventionPlan *Plan) {
  static NevercCallingConventionLocation ReturnLocations[1];
  static NevercCallingConventionLocation ArgumentLocations[32];
  static const uint32_t CalleeSavedRegisters[] = {123};
  uint32_t StackOffset = 0;
  uint64_t Index;
  (void)UserData;
  if (Query == NULL || Plan == NULL ||
      Query->Function.Parameters.Count > 32 ||
      Query->Function.Parameters.ElementStride <
          sizeof(NevercABITypeDescriptor))
    return failed_status();

  memset(ReturnLocations, 0, sizeof(ReturnLocations));
  memset(ArgumentLocations, 0, sizeof(ArgumentLocations));
  if (Query->Function.ReturnType.Kind != NEVERC_ABI_TYPE_VOID) {
    ReturnLocations[0].Header =
        (NevercABITableHeader){sizeof(ReturnLocations[0]),
                              NEVERC_CALLING_CONVENTION_API_MAJOR,
                              NEVERC_CALLING_CONVENTION_API_MINOR, 0};
    ReturnLocations[0].Kind = NEVERC_CC_LOCATION_REGISTER;
    ReturnLocations[0].Size =
        (Query->Function.ReturnType.BitWidth + 7) / 8;
    ReturnLocations[0].Alignment =
        Query->Function.ReturnType.Alignment;
    ReturnLocations[0].RegisterNumber = 51;
    Plan->ReturnLocations =
        (NevercStructArrayView){ReturnLocations, 1,
                                sizeof(ReturnLocations[0])};
  } else {
    Plan->ReturnLocations = (NevercStructArrayView){0};
  }

  for (Index = 0; Index != Query->Function.Parameters.Count; ++Index) {
    const NevercABITypeDescriptor *Type =
        (const NevercABITypeDescriptor *)(
            (const unsigned char *)Query->Function.Parameters.Data +
            Index * Query->Function.Parameters.ElementStride);
    NevercCallingConventionLocation *Location =
        &ArgumentLocations[Index];
    uint32_t Size = (Type->BitWidth + 7) / 8;
    Location->Header =
        (NevercABITableHeader){sizeof(*Location),
                              NEVERC_CALLING_CONVENTION_API_MAJOR,
                              NEVERC_CALLING_CONVENTION_API_MINOR, 0};
    Location->ValueIndex = (uint32_t)Index;
    Location->Size = Size;
    Location->Alignment = Type->Alignment;
    if (Index == 0) {
      Location->Kind = NEVERC_CC_LOCATION_REGISTER;
      Location->RegisterNumber = 54;
    } else {
      Location->Kind = NEVERC_CC_LOCATION_STACK;
      StackOffset =
          (StackOffset + Location->Alignment - 1) &
          ~(Location->Alignment - 1);
      Location->StackOffset = StackOffset;
      StackOffset += Size;
    }
  }
  Plan->ArgumentLocations =
      (NevercStructArrayView){ArgumentLocations,
                              Query->Function.Parameters.Count,
                              sizeof(ArgumentLocations[0])};
  Plan->CalleeSavedRegisters =
      (NevercUInt32ArrayView){CalleeSavedRegisters, 1,
                              sizeof(CalleeSavedRegisters[0])};
  Plan->StackAlignment = 16;
  return neverc_status_ok();
}
#endif

static NevercStatus register_fixture_target(
    const NevercCoreAPI *Core, void *RegistrarContext) {
  const NevercTargetAPI *TargetAPI = NULL;
  const NevercTargetABIAPI *ABIAPI = NULL;
  const NevercCallingConventionAPI *CallingConventionAPI = NULL;
  const void *Table = NULL;
  NevercTargetDescriptor Target;
  NevercTargetABIDescriptor ABI;
  NevercCallingConventionDescriptor CallingConvention;
  NevercStatus Status;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;

  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_TARGET_HIGH,
                          NEVERC_INTERFACE_TARGET_LOW},
      NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Table == NULL || StructSize < sizeof(NevercTargetAPI))
    return failed_status();
  TargetAPI = (const NevercTargetAPI *)Table;
  if (TargetAPI->RegisterTarget == NULL)
    return failed_status();

  memset(&Target, 0, sizeof(Target));
  Target.Header.StructSize = sizeof(Target);
  Target.Header.Major = NEVERC_TARGET_API_MAJOR;
  Target.Header.Minor = NEVERC_TARGET_API_MINOR;
  Target.TargetID = FixtureTargetID;
  Target.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_TARGET_NAME);
  Target.Machine.Header.StructSize = sizeof(Target.Machine);
  Target.Machine.Header.Major = NEVERC_TARGET_API_MAJOR;
  Target.Machine.Header.Minor = NEVERC_TARGET_API_MINOR;
  Target.Machine.RawTriple =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(
          NEVERC_TEST_TARGET_RAW_TRIPLE);
  Target.Machine.Architecture =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(
          NEVERC_TEST_TARGET_ARCHITECTURE);
#if defined(NEVERC_TEST_CC_PLAN_X86)
  Target.Machine.DataLayout = (NevercStringView)NEVERC_TEST_STRING_VIEW(
      "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
      "f80:128-n8:16:32:64-S128");
#else
  Target.Machine.DataLayout = (NevercStringView)NEVERC_TEST_STRING_VIEW(
      "e-p:64:64-i64:64-n32:64-S128");
#endif
  Target.Machine.DefaultCPU =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("generic");
  Target.Machine.SchemaDigest = (NevercStringView)NEVERC_TEST_STRING_VIEW(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
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
#if defined(NEVERC_TEST_HAS_MS_VA_LIST)
  Target.Machine.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_X86_64;
#else
  Target.Machine.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_VOID_POINTER;
#endif
  Target.Machine.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.DefaultExecutionLevel =
      NEVERC_TARGET_EXECUTION_USER;
  Target.Machine.TLSSupported = NEVERC_TRUE;
  Target.DefaultABI = FixtureABIID;
  Target.DefaultCallingConvention = FixtureCallingConventionID;
  Target.Machine.ABIs.Data = &FixtureABIID;
  Target.Machine.ABIs.Count = 1;
  Target.Machine.ABIs.ElementStride = sizeof(FixtureABIID);
  Target.Machine.CallingConventions.Data =
      &FixtureCallingConventionID;
  Target.Machine.CallingConventions.Count = 1;
  Target.Machine.CallingConventions.ElementStride =
      sizeof(FixtureCallingConventionID);
#if defined(NEVERC_TEST_REGISTER_NOBJ_BACKEND)
  Target.DefaultObjectFormatID = FixtureNObjFormatID;
  Target.MCSchemaID = FixtureNObjSchemaID;
  Target.Machine.ObjectFormats.Data = &FixtureNObjFormatID;
  Target.Machine.ObjectFormats.Count = 1;
  Target.Machine.ObjectFormats.ElementStride =
      sizeof(FixtureNObjFormatID);
#endif
#if defined(NEVERC_TEST_CC_PLAN_X86)
  Target.Registers.Data = FixtureCCRegisters;
  Target.Registers.Count =
      sizeof(FixtureCCRegisters) / sizeof(FixtureCCRegisters[0]);
  Target.Registers.ElementStride = sizeof(FixtureCCRegisters[0]);
#endif
#if defined(NEVERC_TEST_TARGET_UNKNOWN_FORMAT)
  Target.DefaultObjectFormatID.High = UINT64_C(0xdeadbeef);
  Target.DefaultObjectFormatID.Low = UINT64_C(0xbadf00d);
#endif
  Target.UserData = (void *)"target_userdata_destroy";
  Target.DestroyUserData = destroy_fixture_userdata;
  Status = TargetAPI->RegisterTarget(TargetAPI->Context, RegistrarContext,
                                     &Target);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#if defined(NEVERC_TEST_TARGET_REGISTRATION_FAILURE)
  Target.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("test.duplicate-target");
  return TargetAPI->RegisterTarget(TargetAPI->Context, RegistrarContext,
                                   &Target);
#else
  Table = NULL;
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_TARGET_ABI_HIGH,
                          NEVERC_INTERFACE_TARGET_ABI_LOW},
      NEVERC_TARGET_ABI_API_MAJOR, NEVERC_TARGET_ABI_API_MINOR,
      &Table, &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercTargetABIAPI))
    return failed_status();
  ABIAPI = (const NevercTargetABIAPI *)Table;
  memset(&ABI, 0, sizeof(ABI));
  ABI.Header.StructSize = sizeof(ABI);
  ABI.Header.Major = NEVERC_TARGET_ABI_API_MAJOR;
  ABI.Header.Minor = NEVERC_TARGET_ABI_API_MINOR;
  ABI.ABIID = FixtureABIID;
  ABI.TargetID = FixtureTargetID;
  ABI.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("test.fixture-abi");
  ABI.ClassifyFunction = classify_fixture_function;
  ABI.VAArg.Header.StructSize = sizeof(ABI.VAArg);
  ABI.VAArg.Header.Major = NEVERC_TARGET_ABI_API_MAJOR;
  ABI.VAArg.Header.Minor = NEVERC_TARGET_ABI_API_MINOR;
  ABI.VAArg.Kind = NEVERC_ABI_VA_ARG_LLVM;
  Status = ABIAPI->RegisterABI(ABIAPI->Context, RegistrarContext,
                               &ABI);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Table = NULL;
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){
          NEVERC_INTERFACE_CALLING_CONVENTION_HIGH,
          NEVERC_INTERFACE_CALLING_CONVENTION_LOW},
      NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
      StructSize < sizeof(NevercCallingConventionAPI))
    return failed_status();
  CallingConventionAPI =
      (const NevercCallingConventionAPI *)Table;
  memset(&CallingConvention, 0, sizeof(CallingConvention));
  CallingConvention.Header.StructSize = sizeof(CallingConvention);
  CallingConvention.Header.Major =
      NEVERC_CALLING_CONVENTION_API_MAJOR;
  CallingConvention.Header.Minor =
      NEVERC_CALLING_CONVENTION_API_MINOR;
  CallingConvention.CallingConventionID =
      FixtureCallingConventionID;
  CallingConvention.TargetID = FixtureTargetID;
  CallingConvention.CanonicalName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(
          "test.fixture-calling-convention");
  CallingConvention.LLVMCallingConvention = 0;
#if defined(NEVERC_TEST_CC_PLAN_X86)
  CallingConvention.PlanCallingConvention =
      plan_fixture_calling_convention;
#endif
  return CallingConventionAPI->RegisterCallingConvention(
      CallingConventionAPI->Context, RegistrarContext,
      &CallingConvention);
#endif
}
#endif

#if defined(NEVERC_TEST_REGISTER_OPTION)
static NevercStatus register_fixture_option(const NevercRegistrarAPI *Registrar,
                                            void *RegistrarContext) {
  NevercOptionDescriptor Option;
  memset(&Option, 0, sizeof(Option));
  Option.Header.StructSize = sizeof(Option);
  Option.Header.Major = NEVERC_DRIVER_API_MAJOR;
  Option.Header.Minor = NEVERC_DRIVER_API_MINOR;
  Option.Spelling =
      (NevercStringView)NEVERC_TEST_STRING_VIEW("--fixture-level");
  Option.Form = NEVERC_OPTION_SEPARATE;
  Option.ValueType = NEVERC_OPTION_UINT;
  Option.Multiplicity = NEVERC_OPTION_SINGLE;
  Option.Help = (NevercStringView)NEVERC_TEST_STRING_VIEW("fixture level");
  Option.Metavar = (NevercStringView)NEVERC_TEST_STRING_VIEW("LEVEL");
  if (Registrar->RegisterOption == NULL)
    return failed_status();
  return Registrar->RegisterOption(RegistrarContext, &Option);
}
#endif

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)Registrar;
  (void)RegistrarContext;
  (void)ProcessState;
  trace_event("register");
#if defined(NEVERC_TEST_REGISTER_TARGET)
  {
    NevercStatus Status =
        register_fixture_target(Core, RegistrarContext);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#if defined(NEVERC_TEST_REGISTER_NOBJ_BACKEND)
  {
    NevercStatus Status =
        register_fixture_nobj_backend(Core, Registrar,
                                      RegistrarContext);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#if defined(NEVERC_TEST_REGISTER_USERDATA)
  {
    NevercObserverDescriptor Observer;
    NevercStatus Status;
    memset(&Observer, 0, sizeof(Observer));
    Observer.Header.StructSize = sizeof(Observer);
    Observer.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Observer.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Observer.Phase.High = UINT64_C(0x1234567890abcdef);
    Observer.Phase.Low = UINT64_C(0xfedcba0987654321);
    Observer.Points = NEVERC_OBSERVER_BEFORE;
    Observer.Callback = fixture_observer;
    Observer.UserData = (void *)"userdata_first_destroy";
    Observer.DestroyUserData = destroy_fixture_userdata;
    Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Observer.UserData = (void *)"userdata_second_destroy";
    Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#if defined(NEVERC_TEST_REGISTER_OPTION)
  {
    NevercStatus Status =
        register_fixture_option(Registrar, RegistrarContext);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#if defined(NEVERC_TEST_REGISTRATION_FAILURE)
  return failed_status();
#else
  return neverc_status_ok();
#endif
}

static NevercStatus NEVERC_CALL
destroy_plugin(const NevercCoreAPI *Core, void *ProcessState) {
  (void)Core;
  (void)ProcessState;
  trace_event("destroy");
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;

  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t)) {
    NevercStatus Error = neverc_status_ok();
    Error.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Error;
  }

  trace_event("entry");
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
#if defined(NEVERC_TEST_SHORT_DESCRIPTOR)
  Descriptor.Header.StructSize =
      (uint32_t)offsetof(NevercPluginDescriptor, Register);
#elif defined(NEVERC_TEST_PREFIX_ONLY_DESCRIPTOR)
  Descriptor.Header.StructSize =
      (uint32_t)(offsetof(NevercPluginDescriptor, Register) +
                 sizeof(Descriptor.Register));
#elif defined(NEVERC_TEST_LONG_DESCRIPTOR)
  Descriptor.Header.StructSize = (uint32_t)(sizeof(Descriptor) + 64);
#else
  Descriptor.Header.StructSize = (uint32_t)sizeof(Descriptor);
#endif
  Descriptor.Header.Major = NEVERC_TEST_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Descriptor.PluginID =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)NEVERC_TEST_STRING_VIEW(NEVERC_TEST_PLUGIN_DISPLAY_NAME);
  Descriptor.Version.Major = NEVERC_TEST_PLUGIN_VERSION_MAJOR;
  Descriptor.Version.Minor = NEVERC_TEST_PLUGIN_VERSION_MINOR;
  Descriptor.Version.Patch = NEVERC_TEST_PLUGIN_VERSION_PATCH;
  Descriptor.Concurrency = NEVERC_TEST_CONCURRENCY;
  Descriptor.Reentrancy = NEVERC_TEST_REENTRANCY;
  Descriptor.ProcessBegin = process_begin;
#if defined(NEVERC_TEST_REQUIRED_INTERFACE_HIGH)
  Descriptor.RequiredInterfaces.Data = RequiredInterfaces;
  Descriptor.RequiredInterfaces.Count =
      sizeof(RequiredInterfaces) / sizeof(RequiredInterfaces[0]);
  Descriptor.RequiredInterfaces.ElementStride =
      sizeof(NevercInterfaceRequirement);
#endif
#if defined(NEVERC_TEST_DEPENDENCY_ID)
  Descriptor.Dependencies.Data = Dependencies;
  Descriptor.Dependencies.Count =
      sizeof(Dependencies) / sizeof(Dependencies[0]);
  Descriptor.Dependencies.ElementStride = sizeof(NevercPluginDependency);
#endif
  Descriptor.Register = register_plugin;
#if defined(NEVERC_TEST_SCOPE_CALLBACKS)
  Descriptor.SessionBegin = session_begin;
  Descriptor.SessionEnd = session_end;
  Descriptor.TaskBegin = task_begin;
  Descriptor.TaskEnd = task_end;
#endif
  Descriptor.Destroy = destroy_plugin;

  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
