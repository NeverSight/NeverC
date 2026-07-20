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
#include "llvm/MC/TargetRegistry.h"
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

} // namespace
