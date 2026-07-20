#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e43505244525401),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{
    UINT64_C(0x4e43505244464d54), UINT64_C(1)};

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

class ObjectReaderTaskScope {
public:
  ObjectReaderTaskScope()
      : Services("neverc-plugin-object-reader-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
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

  ~ObjectReaderTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

Expected<OwnedTargetKey> makeTargetKey(NevercObjectFormatID FormatID) {
  TargetKeyBuilder Builder;
  Builder.setTargetID(TestTargetID)
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43505244414249), UINT64_C(1)})
      .setCallingConvention(
          {UINT64_C(0x4e43505244434349), UINT64_C(1)})
      .setObjectFormat(FormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

void initializeBuiltinTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
  });
}

Expected<std::vector<uint8_t>>
emitBuiltinObject(const BuiltinTargetRoute &Route) {
  auto LLVMTarget = lookupBuiltinLLVMTarget(Route);
  if (!LLVMTarget)
    return LLVMTarget.takeError();
  llvm::TargetOptions Options;
  std::unique_ptr<TargetMachine> Machine((*LLVMTarget)->createTargetMachine(
      Route.CanonicalTriple, Route.DefaultCPU, "", Options, std::nullopt,
      CodeGenOptLevel::None));
  if (!Machine)
    return createStringError(inconvertibleErrorCode(),
                             "failed to create LLVM TargetMachine");

  LLVMContext Context;
  Module ObjectModule("object-reader-test", Context);
  ObjectModule.setTargetTriple(Route.CanonicalTriple);
  ObjectModule.setDataLayout(Machine->createDataLayout());
  Function *FunctionValue = Function::Create(
      FunctionType::get(Type::getInt32Ty(Context), false),
      GlobalValue::ExternalLinkage, "object_reader_probe", ObjectModule);
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", FunctionValue);
  IRBuilder<> Builder(Entry);
  Builder.CreateRet(ConstantInt::get(Type::getInt32Ty(Context), 42));

  SmallVector<char, 0> Bytes;
  raw_svector_ostream Stream(Bytes);
  legacy::PassManager Passes;
  if (Machine->addPassesToEmitFile(
          Passes, Stream, nullptr, CodeGenFileType::ObjectFile))
    return createStringError(inconvertibleErrorCode(),
                             "TargetMachine cannot emit object files");
  Passes.run(ObjectModule);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

Expected<OwnedTargetKey>
makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  TargetKeyBuilder Builder;
  Builder.setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention(
          {UINT64_C(0x4e43505244434349), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

NevercStatus NEVERC_CALL probeTestObject(
    void *, const NevercObjectProbeRequest *Request,
    NevercObjectProbeResult *Result) {
  if (!Request || !Result)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  Result->ConsumedMinimum = 4;
  if (Request->Input.Length >= 4 &&
      std::memcmp(Request->Input.Data, "NOBJ", 4) == 0) {
    Result->Confidence = 900;
    Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_RELOCATABLE;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL readTestObject(
    void *, const NevercObjectReadRequest *Request) {
  if (!Request || !Request->Object)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  static const std::array<uint8_t, 2> Code{{0x2a, 0xc3}};
  NevercObjectSectionDescriptor Section{};
  Section.Header = {sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                    NEVERC_OBJECT_API_MINOR, 0};
  Section.Name = view(".text");
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {Code.data(), Code.size()};
  NevercObjectSectionHandle Handle{};
  return Request->Object->CreateSection(
      Request->Object->Context, Request->Task, Request->Mutation, &Section,
      &Handle);
}

TEST(PluginObjectReaderTest, CustomReaderBuildsVerifiedGraphTransactionally) {
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR,
                   NEVERC_OBJECT_FORMAT_API_MINOR, 0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags =
      NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Format.Probe = probeTestObject;
  Format.Reader = readTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-reader";
  Registration.ObjectFormats =
      ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot =
      PluginTargetRegistry::freeze(ArrayRef(Registration),
                                   PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider))
      << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 8> Input{
      {'N', 'O', 'B', 'J', 1, 0, 0, 0}};
  auto Graph =
      (*Provider)->read(Scope.task(), Input, "answer.nobj", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  EXPECT_EQ((*Graph)->sectionCount(), 1U);
  ASSERT_EQ((*Graph)->sections().size(), 1U);
  const PluginObjectSection &Section = (*Graph)->sections().front();
  EXPECT_EQ(Section.Name, ".text");
  EXPECT_EQ(Section.Kind, NEVERC_OBJECT_SECTION_KIND_TEXT);
  EXPECT_EQ(Section.Data,
            (std::vector<uint8_t>{UINT8_C(0x2a), UINT8_C(0xc3)}));
  EXPECT_EQ((*Graph)->generation(), 2U);
  EXPECT_FALSE((*Graph)->hasLayoutProof());
  EXPECT_FALSE(verifyPluginObjectGraph(**Graph));
}

TEST(PluginObjectReaderTest, EqualConfidenceProbeIsRejectedAsAmbiguous) {
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Formats[2]{};
  Formats[0].Header = {sizeof(Formats[0]),
                       NEVERC_OBJECT_FORMAT_API_MAJOR,
                       NEVERC_OBJECT_FORMAT_API_MINOR, 0};
  Formats[0].FormatID = TestFormatID;
  Formats[0].CanonicalName = view("nobj.first");
  Formats[0].Flags =
      NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Formats[0].Probe = probeTestObject;
  Formats[0].Reader = readTestObject;
  Formats[1] = Formats[0];
  Formats[1].FormatID = {TestFormatID.High, UINT64_C(2)};
  Formats[1].CanonicalName = view("nobj.second");

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-probe-conflict";
  Registration.ObjectFormats = Formats;
  auto Snapshot =
      PluginTargetRegistry::freeze(ArrayRef(Registration),
                                   PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider))
      << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 4> Input{{'N', 'O', 'B', 'J'}};
  auto Match = (*Provider)->registry().probe(
      Scope.task(), Input, "ambiguous.nobj", Target->view());
  ASSERT_FALSE(static_cast<bool>(Match));
  EXPECT_NE(errorText(Match.takeError()).find("ambiguous object format probe"),
            std::string::npos);
}

TEST(PluginObjectReaderTest,
     TruncatedProbeAndArchiveReturnPreciseErrors) {
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR,
                   NEVERC_OBJECT_FORMAT_API_MINOR, 0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.Flags =
      NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Format.Probe = probeTestObject;
  Format.Reader = readTestObject;
  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-reader-errors";
  Registration.ObjectFormats =
      ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot =
      PluginTargetRegistry::freeze(ArrayRef(Registration),
                                   PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider))
      << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 2> Truncated{{'N', 'O'}};
  auto TruncatedGraph = (*Provider)->read(
      Scope.task(), Truncated, "short.nobj", *Target);
  ASSERT_FALSE(static_cast<bool>(TruncatedGraph));
  EXPECT_NE(errorText(TruncatedGraph.takeError()).find(
                "requires at least 4 bytes"),
            std::string::npos);

  const std::array<uint8_t, 8> Archive{
      {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'}};
  auto ArchiveGraph = (*Provider)->read(
      Scope.task(), Archive, "library.a", *Target);
  ASSERT_FALSE(static_cast<bool>(ArchiveGraph));
  const std::string ArchiveError = errorText(ArchiveGraph.takeError());
  EXPECT_NE(ArchiveError.find("artifact-kind mismatch"),
            std::string::npos);
  EXPECT_NE(ArchiveError.find("archive"), std::string::npos);
}

TEST(PluginObjectReaderTest, BuiltinLLVMReaderMapsELFCOFFAndMachO) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider))
      << errorText(Provider.takeError());

  std::set<BuiltinObjectFormat> Seen;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    if (!Seen.insert(Route.ObjectFormat).second)
      continue;
    SCOPED_TRACE(Route.CanonicalTriple.str());
    auto Bytes = emitBuiltinObject(Route);
    ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());
    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
    auto Graph = (*Provider)->read(
        Scope.task(), *Bytes, "builtin-object.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
    EXPECT_GT((*Graph)->sectionCount(), 0U);
    EXPECT_GT((*Graph)->symbolCount(), 0U);
    EXPECT_FALSE(verifyPluginObjectGraph(**Graph));
  }
  EXPECT_EQ(Seen.size(), 3U);
}

} // namespace
