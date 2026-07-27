#include "neverc/Plugin/Host/AssemblyArtifacts.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/MCAsmParserProvider.h"
#include "neverc/Plugin/Host/MCAsmPrinterProvider.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginAssemblyPipeline.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x7800), UINT64_C(1)};
constexpr NevercInterfaceID TestSchemaID{UINT64_C(0x7801), UINT64_C(1)};
constexpr char TestSchemaDigest[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::char_traits<char>::length(Text))};
}

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class AssemblyTaskScope {
public:
  AssemblyTaskScope()
      : Services("neverc-plugin-assembly-provider-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize(StringRef PluginPath = {}) {
    SmallVector<StringRef, 1> SelectedPlugins;
    if (Error E = registerPluginTargetInterfaces(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginAssemblyProviderInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (!PluginPath.empty()) {
      auto Loaded = Services.registry().load(PluginPath);
      if (!Loaded) {
        ADD_FAILURE() << errorText(Loaded.takeError());
        return false;
      }
      SelectedPlugins.push_back((*Loaded)->descriptor().PluginID);
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), SelectedPlugins);
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~AssemblyTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

  Expected<std::shared_ptr<const PluginTargetSnapshot>>
  targetSnapshot() {
    auto Snapshot =
        findPluginTargetSnapshot(Services, Session->handle());
    if (!Snapshot)
      return createStringError(inconvertibleErrorCode(),
                               "target snapshot is unavailable");
    return Snapshot;
  }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

Expected<std::shared_ptr<const PluginTargetSnapshot>> createSnapshot() {
  NevercTargetDescriptor Target{};
  Target.Header = {sizeof(Target), NEVERC_TARGET_API_MAJOR,
                   NEVERC_TARGET_API_MINOR, 0};
  Target.TargetID = TestTargetID;
  Target.CanonicalName = view("test.assembly");
  Target.MCSchemaID = TestSchemaID;
  Target.Machine.Header = {sizeof(Target.Machine), NEVERC_TARGET_API_MAJOR,
                           NEVERC_TARGET_API_MINOR, 0};
  Target.Machine.RawTriple = view("test-unknown-none-none");
  Target.Machine.Architecture = view("test");
  Target.Machine.DataLayout = view("e-p:64:64-i64:64-n32:64-S128");
  Target.Machine.DefaultCPU = view("generic");
  Target.Machine.SchemaDigest = view(TestSchemaDigest);
  Target.Machine.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC;
  Target.Machine.SupportedCodeModels = NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
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

  NevercMCSchemaValueDescriptor Opcode{};
  Opcode.Header = {sizeof(Opcode), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Opcode.StableID = 10;
  Opcode.BackendValue = 100;
  Opcode.CanonicalName = view("test.opcode");

  NevercMCSchemaDescriptor Schema{};
  Schema.Header = {sizeof(Schema), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Schema.SchemaID = TestSchemaID;
  Schema.TargetID = TestTargetID;
  Schema.CanonicalName = view("test.mc");
  Schema.Digest = view(TestSchemaDigest);
  Schema.Opcodes = {&Opcode, 1, sizeof(Opcode)};

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.assembly";
  Registration.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  Registration.MCSchemas = ArrayRef<NevercMCSchemaDescriptor>(Schema);
  return PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Registration),
      PluginTargetRequest{});
}

Error appendTestInstruction(MCPluginBridge &Bridge,
                            PluginTaskContext &Task) {
  auto Unit = Bridge.unit();
  if (!Unit)
    return Unit.takeError();
  auto Schema = Bridge.schemaToken();
  if (!Schema)
    return Schema.takeError();
  const NevercMCAPI &API = Bridge.api();
  NevercMCMutationHandle Mutation{};
  NevercStatus Status = API.BeginMutation(
      API.Context, Task.handle(), *Unit, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to begin MC mutation");
  NevercMCInstHandle Instruction{};
  Status = API.CreateInstruction(
      API.Context, Task.handle(), Mutation, *Schema, 10, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to create MC instruction");
  Status = API.AppendInstruction(
      API.Context, Task.handle(), Mutation, *Unit, Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to append MC instruction");
  Status = API.CommitMutation(API.Context, Task.handle(), Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to commit MC mutation");
  return Error::success();
}

TEST(PluginAssemblerProviderTest,
     PublishesScopedAssemblyProviderInterface) {
  PluginProcessServices Services{"neverc-assembly-interface-tests",
                                 LLVM_VERSION_MAJOR};
  ASSERT_FALSE(registerPluginAssemblyProviderInterface(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Interface = Services.interfaces().query(
      {NEVERC_INTERFACE_ASSEMBLY_PROVIDER_HIGH,
       NEVERC_INTERFACE_ASSEMBLY_PROVIDER_LOW},
      NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
      NEVERC_ASSEMBLY_PROVIDER_API_MINOR);
  ASSERT_TRUE(static_cast<bool>(Interface))
      << errorText(Interface.takeError());
  const auto *API =
      static_cast<const NevercAssemblyProviderAPI *>(Interface->Table);
  ASSERT_NE(API, nullptr);
  EXPECT_NE(API->GetParseInput, nullptr);
  EXPECT_NE(API->GetParseMCBuilder, nullptr);
  EXPECT_NE(API->PublishParsedMCUnit, nullptr);
  EXPECT_NE(API->GetPrintInput, nullptr);
  EXPECT_NE(API->WritePrintOutput, nullptr);
  EXPECT_NE(API->PublishAssemblyOutput, nullptr);
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginAssemblerProviderTest,
     PipelineUsesBuiltinOnlyWhenNoReplacementIsSelected) {
  AssemblyTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Pipeline = PluginAssemblyPipelineRuntime::create(
      Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  EXPECT_FALSE((*Pipeline)->replacesParser());
  EXPECT_FALSE((*Pipeline)->replacesPrinter());

  AssemblySourceArtifact Source;
  Source.Identifier = "builtin.s";
  Source.Buffer = ".byte 0\n";
  unsigned ParseCalls = 0;
  auto Unit = (*Pipeline)->parse(
      Source, TestTargetID,
      [&]() -> Expected<std::unique_ptr<PluginMCUnit>> {
        ++ParseCalls;
        auto Result = std::make_unique<PluginMCUnit>();
        Result->setTargetIdentity(TestTargetID, TestSchemaDigest);
        return Result;
      });
  ASSERT_TRUE(static_cast<bool>(Unit))
      << errorText(Unit.takeError());
  EXPECT_EQ(ParseCalls, 1U);

  unsigned PrintCalls = 0;
  auto Output = (*Pipeline)->print(
      **Unit, [&]() -> Expected<std::string> {
        ++PrintCalls;
        return std::string(".byte 0\n");
      });
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ(PrintCalls, 1U);
  EXPECT_EQ(Output->Text, ".byte 0\n");
  EXPECT_TRUE(Output->Finished);
}

TEST(PluginAssemblerProviderTest,
     PipelineDispatchesThroughRegisteredCProviders) {
  AssemblyTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_ASSEMBLY_PROVIDER_PLUGIN));
  auto Snapshot = Scope.targetSnapshot();
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Pipeline = PluginAssemblyPipelineRuntime::create(
      Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  EXPECT_TRUE((*Pipeline)->replacesParser());
  EXPECT_TRUE((*Pipeline)->replacesPrinter());

  AssemblySourceArtifact Source;
  Source.Identifier = "plugin.s";
  Source.Buffer = ".plugin_opcode\n";
  Source.Preprocessed = true;
  Source.SourceMap.push_back(
      {0, Source.Buffer.size(), 7, 100, 3, 5});
  bool UsedBuiltinParser = false;
  auto Unit = (*Pipeline)->parse(
      Source, TestTargetID,
      [&]() -> Expected<std::unique_ptr<PluginMCUnit>> {
        UsedBuiltinParser = true;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin parser must not run");
      });
  ASSERT_TRUE(static_cast<bool>(Unit))
      << errorText(Unit.takeError());
  EXPECT_FALSE(UsedBuiltinParser);
  ASSERT_EQ((*Unit)->instructions().size(), 1U);
  EXPECT_EQ((*Unit)->instructions().front()->getOpcode(), 100U);

  bool UsedBuiltinPrinter = false;
  auto Output = (*Pipeline)->print(
      **Unit, [&]() -> Expected<std::string> {
        UsedBuiltinPrinter = true;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin printer must not run");
      });
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_FALSE(UsedBuiltinPrinter);
  EXPECT_EQ(Output->Text, ".byte 0\n");
  EXPECT_EQ(Output->Syntax, "test");
  EXPECT_TRUE(Output->Finished);
}

TEST(PluginAssemblerProviderTest,
     ReplacementParserBuildsMCUnitAndSkipsBuiltin) {
  AssemblyTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());

  AssemblySourceArtifact Source;
  Source.Identifier = "plugin-test.s";
  Source.Buffer = ".test_opcode\n";
  Source.Generation = 7;

  AssemblyParseExecutionRequest Request;
  Request.Task = &Scope.task();
  Request.Snapshot = Snapshot->get();
  Request.Source = &Source;
  Request.TargetID = TestTargetID;

  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = MCAsmParserProviderRuntime::execute(
      Request,
      [&](const AssemblySourceArtifact &Input,
          MCPluginBridge &Bridge) -> Error {
        ++ReplacementCalls;
        if (Input.Buffer != ".test_opcode\n")
          return createStringError(inconvertibleErrorCode(),
                                   "unexpected assembly source");
        return appendTestInstruction(Bridge, Scope.task());
      },
      [&]() -> Expected<std::unique_ptr<PluginMCUnit>> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  EXPECT_EQ(ReplacementCalls, 1U);
  EXPECT_EQ(BuiltinCalls, 0U);
  ASSERT_EQ((*Result)->instructionCount(), 1U);
  ASSERT_NE((*Result)->at(0), nullptr);
  EXPECT_EQ((*Result)->at(0)->getOpcode(), 100U);
}

TEST(PluginAssemblerProviderTest,
     RenderedTokenSourcePreservesDiagnosticLocation) {
  AssemblySourceArtifact Source;
  Source.Identifier = "expanded.S";
  Source.Buffer = "nop\nbad\n";
  Source.Preprocessed = true;
  Source.Generation = 2;
  Source.SourceMap.push_back(
      {4, 7, 42, 100, 9, 3});

  auto Location = Source.locate(5);
  ASSERT_TRUE(static_cast<bool>(Location))
      << errorText(Location.takeError());
  EXPECT_EQ(Location->FileID, 42U);
  EXPECT_EQ(Location->ByteOffset, 101U);
  EXPECT_EQ(Location->Line, 9U);
  EXPECT_EQ(Location->Column, 4U);
}

TEST(PluginAssemblerProviderTest,
     ReplacementPrinterStagesFinishedOutputAndSkipsBuiltin) {
  AssemblyTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());

  PluginMCUnit Unit;
  Unit.setTargetIdentity(TestTargetID, TestSchemaDigest);
  auto Instruction = std::make_unique<MCInst>();
  Instruction->setOpcode(100);
  Unit.append(std::move(Instruction));

  AssemblyPrintExecutionRequest Request;
  Request.Task = &Scope.task();
  Request.Snapshot = Snapshot->get();
  Request.Unit = &Unit;
  Request.MaximumOutputBytes = 1024;

  unsigned ReplacementCalls = 0;
  unsigned BuiltinCalls = 0;
  auto Result = MCAsmPrinterProviderRuntime::execute(
      Request,
      [&](MCPluginBridge &Bridge,
          AssemblyOutputBuilder &Output) -> Error {
        ++ReplacementCalls;
        auto UnitHandle = Bridge.unit();
        if (!UnitHandle)
          return UnitHandle.takeError();
        NevercMCInstHandle First{};
        NevercStatus Status = Bridge.api().GetFirstInstruction(
            Bridge.api().Context, Scope.task().handle(), *UnitHandle,
            &First);
        if (Status.Code != NEVERC_STATUS_OK)
          return createStringError(inconvertibleErrorCode(),
                                   "missing print input instruction");
        NevercMCInstructionInfo Info{};
        Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR,
                       NEVERC_MC_API_MINOR, 0};
        Status = Bridge.api().GetInstructionInfo(
            Bridge.api().Context, Scope.task().handle(), First, &Info);
        if (Status.Code != NEVERC_STATUS_OK || Info.Opcode != 10)
          return createStringError(inconvertibleErrorCode(),
                                   "printer saw the wrong stable opcode");
        return Output.write(".test_opcode\n");
      },
      [&]() -> Expected<std::string> {
        ++BuiltinCalls;
        return createStringError(inconvertibleErrorCode(),
                                 "builtin must not execute");
      });

  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  EXPECT_EQ(ReplacementCalls, 1U);
  EXPECT_EQ(BuiltinCalls, 0U);
  EXPECT_TRUE(Result->Finished);
  EXPECT_EQ(Result->Text, ".test_opcode\n");
  EXPECT_EQ(Result->TargetSchemaDigest, TestSchemaDigest);
}

TEST(PluginAssemblerProviderTest,
     FailedPrinterDoesNotPublishPartialOutputOrFallback) {
  AssemblyTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = createSnapshot();
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());

  PluginMCUnit Unit;
  Unit.setTargetIdentity(TestTargetID, TestSchemaDigest);
  AssemblyPrintExecutionRequest Request;
  Request.Task = &Scope.task();
  Request.Snapshot = Snapshot->get();
  Request.Unit = &Unit;
  Request.MaximumOutputBytes = 1024;

  unsigned BuiltinCalls = 0;
  auto Result = MCAsmPrinterProviderRuntime::execute(
      Request,
      [](MCPluginBridge &, AssemblyOutputBuilder &Output) -> Error {
        if (Error E = Output.write("partial"))
          return E;
        return createStringError(inconvertibleErrorCode(),
                                 "printer rejected the unit");
      },
      [&]() -> Expected<std::string> {
        ++BuiltinCalls;
        return std::string("fallback");
      });

  ASSERT_FALSE(static_cast<bool>(Result));
  const std::string Message = errorText(Result.takeError());
  EXPECT_NE(Message.find("printer rejected the unit"), std::string::npos)
      << Message;
  EXPECT_EQ(BuiltinCalls, 0U);
}

TEST(PluginAssemblerProviderTest,
     BuiltinPrinterRoundTripsDataForX86AndAArch64) {
  static const bool Initialized = [] {
    InitializeAllTargetInfos();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
    return true;
  }();
  (void)Initialized;

  for (const char *TripleName :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"}) {
    std::string LookupError;
    const Target *TheTarget =
        TargetRegistry::lookupTarget(TripleName, LookupError);
    ASSERT_NE(TheTarget, nullptr) << LookupError;

    PluginMCUnit Unit;
    auto Section = std::make_unique<PluginMCSection>();
    Section->Name = ".text";
    Section->Alignment = 4;
    Section->Flags =
        NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
    auto Fragment = std::make_unique<PluginMCFragment>();
    Fragment->Parent = Section.get();
    Fragment->Kind = NEVERC_MC_FRAGMENT_DATA;
    Fragment->Contents = {0};
    Section->Fragments.push_back(std::move(Fragment));
    Unit.sections().push_back(std::move(Section));

    SmallVector<char, 128> AssemblyBytes;
    raw_svector_ostream Assembly(AssemblyBytes);
    Error PrintError = BuiltinLLVMAsmPrinter::print(
        *TheTarget, Triple(TripleName), "", "", Unit, Assembly);
    ASSERT_FALSE(static_cast<bool>(PrintError))
        << errorText(std::move(PrintError));

    SmallVector<char, 128> ObjectBytes;
    raw_svector_ostream Object(ObjectBytes);
    BuiltinLLVMAsmParserRequest ParseRequest{
        TheTarget, Triple(TripleName), "", "", VersionTuple(),
        MemoryBufferRef(Assembly.str(), "roundtrip.s"), &Object};
    Error ParseError = runBuiltinLLVMAsmParser(ParseRequest);
    ASSERT_FALSE(static_cast<bool>(ParseError))
        << TripleName << ": " << errorText(std::move(ParseError));
    EXPECT_FALSE(ObjectBytes.empty()) << TripleName;
  }
}

void initializeAssemblyTargets() {
  static const bool Initialized = [] {
    InitializeAllTargetInfos();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
    return true;
  }();
  (void)Initialized;
}

// The printer's output goes straight back into the assembler, so anything it
// writes has to parse there. The round trip above only ever exercised ELF, a
// section reached by its shorthand directive, and a unit with no symbols in it.
struct PrinterCase {
  const char *Triple;
  const char *SectionName;
};

const std::array<PrinterCase, 3> PrinterCases = {
    {{"x86_64-unknown-linux-gnu", ".mysection"},
     {"x86_64-pc-windows-msvc", ".mysection"},
     {"arm64-apple-macosx", "__mysection"}}};

Expected<std::string> printUnit(const char *TripleName,
                                const PluginMCUnit &Unit) {
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TripleName, LookupError);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), LookupError);
  std::string Text;
  raw_string_ostream Stream(Text);
  if (Error E = BuiltinLLVMAsmPrinter::print(*TheTarget, Triple(TripleName), "",
                                             "", Unit, Stream))
    return std::move(E);
  Stream.flush();
  return Text;
}

Error reassemble(const char *TripleName, StringRef Text) {
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TripleName, LookupError);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), LookupError);
  SmallVector<char, 256> ObjectBytes;
  raw_svector_ostream Object(ObjectBytes);
  BuiltinLLVMAsmParserRequest ParseRequest{
      TheTarget,     Triple(TripleName), "", "", VersionTuple(),
      MemoryBufferRef(Text, "printed.s"), &Object};
  return runBuiltinLLVMAsmParser(ParseRequest);
}

std::unique_ptr<PluginMCSection> makeDataSection(StringRef Name) {
  auto Section = std::make_unique<PluginMCSection>();
  Section->Name = Name.str();
  Section->Alignment = 4;
  Section->Flags = NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_WRITABLE;
  auto Fragment = std::make_unique<PluginMCFragment>();
  Fragment->Parent = Section.get();
  Fragment->Kind = NEVERC_MC_FRAGMENT_DATA;
  Fragment->Contents = {1, 2, 3, 4, 5, 6, 7, 8};
  Section->Fragments.push_back(std::move(Fragment));
  return Section;
}

Expected<std::vector<uint8_t>> reassembleToObject(const char *TripleName,
                                                  StringRef Text) {
  std::string LookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TripleName, LookupError);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), LookupError);
  SmallVector<char, 256> ObjectBytes;
  raw_svector_ostream Object(ObjectBytes);
  BuiltinLLVMAsmParserRequest ParseRequest{
      TheTarget,     Triple(TripleName), "", "", VersionTuple(),
      MemoryBufferRef(Text, "printed.s"), &Object};
  if (Error E = runBuiltinLLVMAsmParser(ParseRequest))
    return std::move(E);
  return std::vector<uint8_t>(ObjectBytes.begin(), ObjectBytes.end());
}

// What the printer writes is handed straight back to the assembler, so a
// section has to come out of that trip with the nature it went in with. The
// printer stated only the name -- with a segment on Mach-O -- and left every
// flag off, and each format fills an unstated flag set in differently: ELF
// derives it from the name, so anything not called ".text..." loses SHF_ALLOC
// and SHF_EXECINSTR; COFF defaults to initialised read/write data, turning
// executable code into a writable data section; and Mach-O without
// "pure_instructions" produces a section that no longer reads back as text.
TEST(PluginAssemblerProviderTest, BuiltinPrinterRoundTripsExecutableSections) {
  initializeAssemblyTargets();
  const std::array<PrinterCase, 3> Cases = {
      {{"x86_64-unknown-linux-gnu", ".mycode"},
       {"x86_64-pc-windows-msvc", ".mycode"},
       {"arm64-apple-macosx", "__mycode"}}};
  for (const auto &[TripleName, SectionName] : Cases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    auto Section = makeDataSection(SectionName);
    Section->Flags =
        NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
    Unit.sections().push_back(std::move(Section));

    auto Text = printUnit(TripleName, Unit);
    ASSERT_TRUE(static_cast<bool>(Text)) << errorText(Text.takeError());
    auto Bytes = reassembleToObject(TripleName, *Text);
    ASSERT_TRUE(static_cast<bool>(Bytes))
        << *Text << "\n"
        << errorText(Bytes.takeError());
    auto Parsed = object::ObjectFile::createObjectFile(MemoryBufferRef(
        StringRef(reinterpret_cast<const char *>(Bytes->data()),
                  Bytes->size()),
        "printed.o"));
    ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
    bool Found = false;
    for (const object::SectionRef &Value : (*Parsed)->sections()) {
      Expected<StringRef> Name = Value.getName();
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      if (*Name != SectionName)
        continue;
      Found = true;
      EXPECT_TRUE(Value.isText())
          << "an executable section came back as data\n"
          << *Text;
    }
    EXPECT_TRUE(Found) << "the printed section is not in the object\n" << *Text;
  }
}

// An opcode says how many operands it takes, and the printer generated for it
// reads exactly that many by index. The MC verifier checks that an opcode and
// its registers are in the target schema but says nothing about how many
// operands accompany it, and a plugin builds an instruction in two steps --
// create with an opcode, then append operands one at a time -- so an
// instruction with too few of them is not a malformed graph, it is one the
// plugin simply stopped building. Handed to the printer it reads past the end
// of the operand list, which is the host reading memory it does not own on
// account of its input.
TEST(PluginAssemblerProviderTest, BuiltinPrinterRefusesUnderspecifiedInstruction) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    std::string LookupError;
    const Target *TheTarget =
        TargetRegistry::lookupTarget(TripleName, LookupError);
    ASSERT_NE(TheTarget, nullptr) << LookupError;
    std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
    ASSERT_NE(MCII, nullptr);

    // A real opcode of this target that takes operands, rather than an
    // invented number: the point is an instruction the printer would accept.
    std::optional<unsigned> Opcode;
    for (unsigned Candidate = 0; Candidate != MCII->getNumOpcodes();
         ++Candidate) {
      const MCInstrDesc &Desc = MCII->get(Candidate);
      if (Desc.getNumOperands() >= 2 && !Desc.isVariadic() &&
          !Desc.isPseudo()) {
        Opcode = Candidate;
        break;
      }
    }
    ASSERT_TRUE(Opcode.has_value());

    PluginMCUnit Unit;
    auto Instruction = std::make_unique<MCInst>();
    Instruction->setOpcode(*Opcode);
    Unit.append(std::move(Instruction));

    auto Text = printUnit(TripleName, Unit);
    EXPECT_FALSE(static_cast<bool>(Text))
        << "printed an instruction with no operands as if it had them:\n"
        << *Text;
    if (!Text)
      consumeError(Text.takeError());
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterRoundTripsNamedSections) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    Unit.sections().push_back(makeDataSection(SectionName));

    auto Text = printUnit(TripleName, Unit);
    ASSERT_TRUE(static_cast<bool>(Text)) << errorText(Text.takeError());
    Error ParseError = reassemble(TripleName, *Text);
    EXPECT_FALSE(static_cast<bool>(ParseError))
        << "printed assembly did not parse back\n"
        << *Text << "\n"
        << errorText(std::move(ParseError));
    consumeError(std::move(ParseError));
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterRoundTripsSymbolBindings) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    auto Section = makeDataSection(SectionName);

    // ".weak" and ".hidden" are ELF spellings. Mach-O wants
    // ".weak_definition" and ".private_extern", and COFF has no hidden at all.
    auto Weak = std::make_unique<PluginMCSymbol>();
    Weak->Name = "weak_symbol";
    Weak->Binding = NEVERC_MC_SYMBOL_BINDING_WEAK;
    Weak->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
    Weak->Section = Section.get();
    auto Hidden = std::make_unique<PluginMCSymbol>();
    Hidden->Name = "hidden_symbol";
    Hidden->Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
    Hidden->Visibility = NEVERC_MC_SYMBOL_VISIBILITY_HIDDEN;
    Hidden->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
    Hidden->Section = Section.get();
    Unit.sections().push_back(std::move(Section));
    Unit.symbols().push_back(std::move(Weak));
    Unit.symbols().push_back(std::move(Hidden));

    auto Text = printUnit(TripleName, Unit);
    ASSERT_TRUE(static_cast<bool>(Text)) << errorText(Text.takeError());
    Error ParseError = reassemble(TripleName, *Text);
    EXPECT_FALSE(static_cast<bool>(ParseError))
        << "printed assembly did not parse back\n"
        << *Text << "\n"
        << errorText(std::move(ParseError));
    consumeError(std::move(ParseError));
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterQuotesNamesThatNeedIt) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    auto Section = makeDataSection(SectionName);
    // How MSVC spells `void f(int)`. The assembler reads '@' and '?' as syntax
    // unless the name is quoted.
    auto Symbol = std::make_unique<PluginMCSymbol>();
    Symbol->Name = "?f@@YAXH@Z";
    Symbol->Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
    Symbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
    Symbol->Section = Section.get();
    Unit.sections().push_back(std::move(Section));
    Unit.symbols().push_back(std::move(Symbol));

    auto Text = printUnit(TripleName, Unit);
    ASSERT_TRUE(static_cast<bool>(Text)) << errorText(Text.takeError());
    Error ParseError = reassemble(TripleName, *Text);
    EXPECT_FALSE(static_cast<bool>(ParseError))
        << "printed assembly did not parse back\n"
        << *Text << "\n"
        << errorText(std::move(ParseError));
    consumeError(std::move(ParseError));
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterDoesNotDropSymbolsPastTheStart) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    auto Section = makeDataSection(SectionName);
    // A symbol at a non-zero offset had no label written for it at all, so it
    // left the printer as an undefined symbol with nothing saying so.
    auto Symbol = std::make_unique<PluginMCSymbol>();
    Symbol->Name = "past_the_start";
    Symbol->Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
    Symbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
    Symbol->Section = Section.get();
    Symbol->Value = 4;
    Unit.sections().push_back(std::move(Section));
    Unit.symbols().push_back(std::move(Symbol));

    auto Text = printUnit(TripleName, Unit);
    if (!Text) {
      // Refusing is a fair answer; losing the definition silently is not.
      consumeError(Text.takeError());
      continue;
    }
    EXPECT_NE(Text->find("past_the_start:"), std::string::npos)
        << "the definition was dropped\n"
        << *Text;
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterRefusesSectionNameItCannotWrite) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    // A quote is the one thing quoting cannot carry, and a section name gets
    // quoted the same way a symbol name does. Symbol names are checked before
    // being written; section names went out unchecked, closing the quoted name
    // early and leaving the rest of the line to be read as directive syntax.
    auto Section = makeDataSection(std::string(SectionName) + "\"x");
    Unit.sections().push_back(std::move(Section));

    auto Text = printUnit(TripleName, Unit);
    if (!Text) {
      consumeError(Text.takeError());
      continue;
    }
    Error ParseError = reassemble(TripleName, *Text);
    EXPECT_TRUE(static_cast<bool>(ParseError))
        << "a section name holding a quote was written out as valid assembly\n"
        << *Text;
    consumeError(std::move(ParseError));
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterRefusesDefinitionsItCannotWrite) {
  initializeAssemblyTargets();
  // An absolute symbol is created by ".set" and a common one by ".comm", and
  // the printer writes neither -- it only ever places a label inside a
  // section. Writing just the binding leaves the symbol undefined, so the
  // value or the size it was defined with is gone and nothing says so.
  const std::array<NevercMCSymbolDefinition, 2> Definitions = {
      {NEVERC_MC_SYMBOL_DEFINITION_ABSOLUTE,
       NEVERC_MC_SYMBOL_DEFINITION_COMMON}};
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    for (NevercMCSymbolDefinition Definition : Definitions) {
      SCOPED_TRACE(TripleName);
      SCOPED_TRACE(Definition);
      PluginMCUnit Unit;
      Unit.sections().push_back(makeDataSection(SectionName));
      auto Symbol = std::make_unique<PluginMCSymbol>();
      Symbol->Name = "elsewhere";
      Symbol->Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
      Symbol->Definition = Definition;
      Symbol->Value = 64;
      Symbol->Size = 4;
      Unit.symbols().push_back(std::move(Symbol));

      auto Text = printUnit(TripleName, Unit);
      if (!Text) {
        consumeError(Text.takeError());
        continue;
      }
      ADD_FAILURE() << "a definition the printer cannot write was accepted, "
                       "leaving the symbol undefined\n"
                    << *Text;
    }
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterKeepsWeakUndefinedReferences) {
  initializeAssemblyTargets();
  for (const auto &[TripleName, SectionName] : PrinterCases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    Unit.sections().push_back(makeDataSection(SectionName));

    // An undefined symbol belongs to no section, so the per-section loop that
    // wrote symbol attributes never reached it and its weak binding was lost.
    // A weak reference that comes back strong is a link error where there was
    // none: unresolved, the weak one is zero and the strong one is a failure.
    auto Weak = std::make_unique<PluginMCSymbol>();
    Weak->Name = "weak_undefined";
    Weak->Binding = NEVERC_MC_SYMBOL_BINDING_WEAK;
    Weak->Definition = NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED;
    Unit.symbols().push_back(std::move(Weak));

    auto Text = printUnit(TripleName, Unit);
    ASSERT_TRUE(static_cast<bool>(Text)) << errorText(Text.takeError());
    EXPECT_NE(Text->find("weak_undefined"), std::string::npos)
        << "the weak binding of an undefined symbol was dropped\n"
        << *Text;
    Error ParseError = reassemble(TripleName, *Text);
    EXPECT_FALSE(static_cast<bool>(ParseError))
        << "printed assembly did not parse back\n"
        << *Text << "\n"
        << errorText(std::move(ParseError));
    consumeError(std::move(ParseError));
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterKeepsSymbolsWithAPrivatePrefix) {
  initializeAssemblyTargets();
  struct PrivateCase {
    const char *TripleName;
    const char *SectionName;
    const char *SymbolName;
  };
  // Each format reserves a prefix for labels the assembler invents for itself,
  // and a local symbol spelled that way is taken for one: it is dropped from
  // the symbol table rather than written into it. A name is data, not a
  // request, so a symbol the unit asked to keep has to survive being printed.
  const std::array<PrivateCase, 3> Cases = {
      {{"x86_64-unknown-linux-gnu", ".mycode", ".Lprivate"},
       {"x86_64-pc-windows-msvc", ".mycode", ".Lprivate"},
       {"arm64-apple-macosx", "__mycode", "Lprivate"}}};
  for (const auto &[TripleName, SectionName, SymbolName] : Cases) {
    SCOPED_TRACE(TripleName);
    PluginMCUnit Unit;
    auto Section = makeDataSection(SectionName);
    auto Symbol = std::make_unique<PluginMCSymbol>();
    Symbol->Name = SymbolName;
    Symbol->Binding = NEVERC_MC_SYMBOL_BINDING_LOCAL;
    Symbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
    Symbol->Section = Section.get();
    Symbol->Value = 0;
    Unit.sections().push_back(std::move(Section));
    Unit.symbols().push_back(std::move(Symbol));

    auto Text = printUnit(TripleName, Unit);
    if (!Text) {
      // Refusing a name it cannot carry is a fair answer; dropping the symbol
      // without a word is not.
      consumeError(Text.takeError());
      continue;
    }
    auto Bytes = reassembleToObject(TripleName, *Text);
    ASSERT_TRUE(static_cast<bool>(Bytes))
        << *Text << "\n"
        << errorText(Bytes.takeError());
    auto Parsed = object::ObjectFile::createObjectFile(MemoryBufferRef(
        StringRef(reinterpret_cast<const char *>(Bytes->data()),
                  Bytes->size()),
        "printed.o"));
    ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
    bool Found = false;
    for (const object::SymbolRef &Value : (*Parsed)->symbols()) {
      Expected<StringRef> Name = Value.getName();
      if (!Name) {
        consumeError(Name.takeError());
        continue;
      }
      if (*Name == SymbolName)
        Found = true;
    }
    EXPECT_TRUE(Found) << "the symbol was dropped from the symbol table\n"
                       << *Text;
  }
}

TEST(PluginAssemblerProviderTest, BuiltinPrinterPlacesSymbolsAtTheirOffsets) {
  initializeAssemblyTargets();
  struct TextCase {
    const char *TripleName;
    const char *SectionName;
  };
  // The section the assembler starts in, named the way each format names it.
  const std::array<TextCase, 3> Cases = {
      {{"x86_64-unknown-linux-gnu", ".text"},
       {"x86_64-pc-windows-msvc", ".text"},
       {"arm64-apple-macosx", "__text"}}};
  for (const auto &[TripleName, SectionName] : Cases) {
    SCOPED_TRACE(TripleName);
    // A unit holds instructions of its own beside its sections, and the
    // printer gives those their own ".text". Naming a section the same thing
    // reaches that one again, so the two runs of bytes end up in one section
    // with the unit's instructions first -- and every offset in the section
    // slides by however many bytes those took. A symbol the unit places at
    // offset zero comes out somewhere else, with nothing said.
    std::string LookupError;
    const Target *TheTarget =
        TargetRegistry::lookupTarget(TripleName, LookupError);
    ASSERT_NE(TheTarget, nullptr) << LookupError;
    std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
    ASSERT_NE(MCII, nullptr);
    // Any real instruction of this target will do, so long as it is one the
    // printer accepts and the assembler reads back.
    std::optional<unsigned> Opcode;
    for (unsigned Candidate = 0; Candidate != MCII->getNumOpcodes();
         ++Candidate) {
      const MCInstrDesc &Desc = MCII->get(Candidate);
      if (Desc.getNumOperands() == 0 && !Desc.isVariadic() &&
          !Desc.isPseudo()) {
        Opcode = Candidate;
        break;
      }
    }
    ASSERT_TRUE(Opcode.has_value());

    PluginMCUnit Unit;
    auto Instruction = std::make_unique<MCInst>();
    Instruction->setOpcode(*Opcode);
    Unit.append(std::move(Instruction));

    auto Section = makeDataSection(SectionName);
    Section->Flags =
        NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
    auto Symbol = std::make_unique<PluginMCSymbol>();
    Symbol->Name = "at_the_start";
    Symbol->Binding = NEVERC_MC_SYMBOL_BINDING_GLOBAL;
    Symbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_SECTION;
    Symbol->Section = Section.get();
    Symbol->Value = 0;
    Unit.sections().push_back(std::move(Section));
    Unit.symbols().push_back(std::move(Symbol));

    auto Text = printUnit(TripleName, Unit);
    if (!Text) {
      // Refusing a unit it cannot lay out is a fair answer; placing the
      // symbol somewhere other than where the unit put it is not.
      consumeError(Text.takeError());
      continue;
    }
    auto Bytes = reassembleToObject(TripleName, *Text);
    if (!Bytes) {
      consumeError(Bytes.takeError());
      continue;
    }
    auto Parsed = object::ObjectFile::createObjectFile(MemoryBufferRef(
        StringRef(reinterpret_cast<const char *>(Bytes->data()),
                  Bytes->size()),
        "printed.o"));
    ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
    for (const object::SymbolRef &Value : (*Parsed)->symbols()) {
      Expected<StringRef> Name = Value.getName();
      if (!Name) {
        consumeError(Name.takeError());
        continue;
      }
      if (!Name->ends_with("at_the_start"))
        continue;
      Expected<uint64_t> Address = Value.getAddress();
      ASSERT_TRUE(static_cast<bool>(Address)) << errorText(Address.takeError());
      EXPECT_EQ(*Address, 0u)
          << "the symbol moved by the length of the unit's own instructions\n"
          << *Text;
    }
  }
}

} // namespace
