#include "Link/LinkInputReader.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e434c494e505454),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{
    UINT64_C(0x4e434c494e50464d), UINT64_C(1)};

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
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

Expected<OwnedTargetKey> makeTargetKey() {
  return TargetKeyBuilder()
      .setTargetID(TestTargetID)
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e434c494e504142), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e434c494e504343), UINT64_C(1)})
      .setObjectFormat(TestFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

void initializeBuiltinTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
  });
}

Expected<OwnedTargetKey>
makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  return TargetKeyBuilder()
      .setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention(
          {UINT64_C(0x4e434c494e504243), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

Expected<std::vector<uint8_t>>
assembleBuiltinObject(const BuiltinTargetRoute &Route) {
  auto Target = lookupBuiltinLLVMTarget(Route);
  if (!Target)
    return Target.takeError();
  const std::string Assembly =
      "\t.text\n"
      "\t.globl\tlink_input_elf\n"
      "link_input_elf:\n"
      "\t.byte\t0xc3\n";
  SmallVector<char, 0> Bytes;
  raw_svector_ostream Output(Bytes);
  BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple =
      Triple(Triple::normalize(Route.CanonicalTriple));
  Request.CPU = Route.DefaultCPU;
  Request.Input =
      MemoryBufferRef(Assembly, "<link-input-elf-assembly>");
  Request.Output = &Output;
  if (Error E = runBuiltinLLVMAsmParser(Request))
    return std::move(E);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

void appendArchiveField(std::string &Archive, StringRef Value,
                        size_t Width) {
  ASSERT_LE(Value.size(), Width);
  Archive.append(Value.data(), Value.size());
  Archive.append(Width - Value.size(), ' ');
}

void appendArchiveMemberHeader(std::string &Archive, StringRef Name,
                               uint64_t Size) {
  const std::string ArchiveName = (Name + "/").str();
  appendArchiveField(Archive, ArchiveName, 16);
  appendArchiveField(Archive, "0", 12);
  appendArchiveField(Archive, "0", 6);
  appendArchiveField(Archive, "0", 6);
  appendArchiveField(Archive, "100644", 8);
  appendArchiveField(Archive, std::to_string(Size), 10);
  Archive.append("`\n", 2);
}

void appendArchiveMember(std::string &Archive, StringRef Name,
                         StringRef Bytes) {
  appendArchiveMemberHeader(Archive, Name, Bytes.size());
  Archive.append(Bytes.data(), Bytes.size());
  if ((Bytes.size() & 1U) != 0)
    Archive.push_back('\n');
}

std::vector<uint8_t> makeArchive() {
  std::string Archive = "!<arch>\n";
  appendArchiveMember(Archive, "duplicate.o", "NOBJ-one");
  appendArchiveMember(Archive, "duplicate.o", "NOBJ-two");
  return std::vector<uint8_t>(Archive.begin(), Archive.end());
}

void appendBigEndian32(std::string &Bytes, uint32_t Value) {
  Bytes.push_back(static_cast<char>((Value >> 24) & 0xff));
  Bytes.push_back(static_cast<char>((Value >> 16) & 0xff));
  Bytes.push_back(static_cast<char>((Value >> 8) & 0xff));
  Bytes.push_back(static_cast<char>(Value & 0xff));
}

std::vector<uint8_t> makeIndexedArchive(StringRef SymbolName) {
  const uint64_t IndexSize = 8 + SymbolName.size() + 1;
  const uint64_t MemberOffset =
      8 + 60 + IndexSize + (IndexSize & 1U);
  std::string Index;
  appendBigEndian32(Index, 1);
  appendBigEndian32(Index, static_cast<uint32_t>(MemberOffset));
  Index.append(SymbolName.data(), SymbolName.size());
  Index.push_back('\0');

  std::string Archive = "!<arch>\n";
  appendArchiveMember(Archive, "", Index);
  appendArchiveMember(Archive, "indexed.o", "NOBJ-indexed");
  return std::vector<uint8_t>(Archive.begin(), Archive.end());
}

std::vector<uint8_t> makeCOFFImportArchive() {
  const std::string Symbol = "imported_answer";
  const std::string DLL = "answer.dll";
  object::coff_import_header Header{};
  Header.Sig1 = 0;
  Header.Sig2 = UINT16_MAX;
  Header.Version = 0;
  Header.Machine = COFF::IMAGE_FILE_MACHINE_AMD64;
  Header.SizeOfData = Symbol.size() + 1 + DLL.size() + 1;
  Header.TypeInfo = COFF::IMPORT_DATA;

  std::string Member(reinterpret_cast<const char *>(&Header),
                     sizeof(Header));
  Member.append(Symbol);
  Member.push_back('\0');
  Member.append(DLL);
  Member.push_back('\0');
  std::string Archive = "!<arch>\n";
  appendArchiveMember(Archive, "answer.obj", Member);
  return std::vector<uint8_t>(Archive.begin(), Archive.end());
}

class PluginLinkInputTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_FALSE(registerPluginIOInterface(Services));
    ASSERT_FALSE(Services.interfaces().freeze());
    auto Loaded =
        Services.registry().load(NEVERC_TEST_VIRTUAL_ARCHIVE_PLUGIN);
    ASSERT_TRUE(static_cast<bool>(Loaded))
        << errorText(Loaded.takeError());
    const std::array<StringRef, 1> Selected = {
        (*Loaded)->descriptor().PluginID};
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(CreatedPlan))
        << errorText(CreatedPlan.takeError());
    Plan.emplace(std::move(*CreatedPlan));
    ASSERT_FALSE(activatePluginPlan(Services, *Plan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(CreatedSession))
        << errorText(CreatedSession.takeError());
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_LINK);
    ASSERT_TRUE(static_cast<bool>(CreatedTask))
        << errorText(CreatedTask.takeError());
    Task = std::move(*CreatedTask);

    NevercObjectFormatDescriptor Format{};
    Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR,
                     UINT16_C(0), 0};
    Format.FormatID = TestFormatID;
    Format.CanonicalName = view("nobj");
    Format.DefaultExtension = view(".nobj");
    Format.Flags =
        NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
    Format.Probe = probeTestObject;
    Format.Reader = readTestObject;
    PluginTargetRegistrationView Registration;
    Registration.PluginID = "org.neverc.test.link-input";
    Registration.ObjectFormats =
        ArrayRef<NevercObjectFormatDescriptor>(Format);
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef(Registration), PluginTargetRequest{});
    ASSERT_TRUE(static_cast<bool>(Snapshot))
        << errorText(Snapshot.takeError());
    auto CreatedProvider = ObjectReaderProvider::create(*Snapshot);
    ASSERT_TRUE(static_cast<bool>(CreatedProvider))
        << errorText(CreatedProvider.takeError());
    Objects = std::move(*CreatedProvider);

    auto CreatedTarget = makeTargetKey();
    ASSERT_TRUE(static_cast<bool>(CreatedTarget))
        << errorText(CreatedTarget.takeError());
    Target.emplace(std::move(*CreatedTarget));
    FileSystem = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
  }

  void TearDown() override {
    Objects.reset();
    FileSystem.reset();
    Target.reset();
    if (Task && !Task->isEnded())
      EXPECT_FALSE(Task->end());
    Task.reset();
    if (Session)
      EXPECT_FALSE(Session->end());
    Session.reset();
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  Expected<std::shared_ptr<const LinkRequest>>
  request(std::vector<OwnedRawLinkInput> Inputs) {
    LinkRequestData Data;
    Data.Task = Task->handle();
    Data.Target = *Target;
    Data.InputFormat = TestFormatID;
    Data.OutputFormat = TestFormatID;
    Data.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
    Data.OutputURI = "/out/program";
    Data.Inputs = std::move(Inputs);
    return LinkRequest::create(std::move(Data));
  }

  OwnedRawLinkInput blobInput(NevercLinkInputKind Kind,
                              std::vector<uint8_t> Bytes,
                              NevercLinkInputFlags Flags = 0) {
    OwnedRawLinkInput Input;
    Input.Kind = Kind;
    Input.Flags = Flags;
    Input.Ordinal = 0;
    Input.LogicalURI = "/inputs/value";
    Input.AuthorizedBlob = std::move(Bytes);
    return Input;
  }

  PluginProcessServices Services{"neverc-plugin-link-input-tests",
                                 LLVM_VERSION_MAJOR};
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::unique_ptr<ObjectReaderProvider> Objects;
  std::optional<OwnedTargetKey> Target;
  IntrusiveRefCntPtr<vfs::InMemoryFileSystem> FileSystem;
};

TEST_F(PluginLinkInputTest, ReadsObjectThroughVFSAndFreezesDigestAndRoute) {
  const std::string Bytes = "NOBJ-vfs";
  ASSERT_TRUE(FileSystem->addFile(
      "/work/input.nobj", 1,
      MemoryBuffer::getMemBufferCopy(Bytes, "/work/input.nobj")));
  OwnedRawLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "/work/input.nobj";
  Input.Ordinal = 0;
  auto Request = request({Input});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().inputs().size(), 1U);
  const PluginLinkInput &Stored = (*Inputs)->graph().inputs().front();
  EXPECT_EQ(Stored.Kind, NEVERC_LINK_INPUT_OBJECT);
  EXPECT_EQ(Stored.ReaderRoute, "org.neverc.test.link-input:nobj");
  EXPECT_EQ(Stored.ContentDigest,
            SHA256::hash(ArrayRef<uint8_t>(
                reinterpret_cast<const uint8_t *>(Bytes.data()),
                Bytes.size())));
  ASSERT_NE((*Inputs)->objectGraphForInput(Stored.ID), nullptr);
  EXPECT_EQ((*Inputs)->objectGraphForInput(Stored.ID)->sectionCount(), 1U);
}

TEST_F(PluginLinkInputTest, ReadsELFArchiveWithBuiltinObjectReader) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    Triple RouteTriple(Triple::normalize(Route.CanonicalTriple));
    if (Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        RouteTriple.getArch() == Triple::x86_64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);
  auto Object = assembleBuiltinObject(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Object))
      << errorText(Object.takeError());
  StringRef ObjectBytes(reinterpret_cast<const char *>(Object->data()),
                        Object->size());
  std::string Archive = "!<arch>\n";
  appendArchiveMember(Archive, "builtin.o", ObjectBytes);

  auto BuiltinTarget = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(BuiltinTarget))
      << errorText(BuiltinTarget.takeError());
  OwnedRawLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_ARCHIVE;
  Input.Flags = NEVERC_LINK_INPUT_FLAG_WHOLE_ARCHIVE;
  Input.Ordinal = 0;
  Input.LogicalURI = "/inputs/libbuiltin.a";
  Input.AuthorizedBlob.assign(Archive.begin(), Archive.end());
  LinkRequestData Data;
  Data.Task = Task->handle();
  Data.Target = *BuiltinTarget;
  Data.InputFormat = ELFRoute->ObjectFormatID;
  Data.OutputFormat = ELFRoute->ObjectFormatID;
  Data.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  Data.OutputURI = "/out/elf-program";
  Data.Inputs.push_back(std::move(Input));
  auto Request = LinkRequest::create(std::move(Data));
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().archiveMembers().size(), 1U);
  const PluginLinkArchiveMember &Member =
      (*Inputs)->graph().archiveMembers().front();
  EXPECT_TRUE(Member.Materialized);
  PluginObjectGraph *Graph =
      (*Inputs)->objectGraphForInput(Member.ID);
  ASSERT_NE(Graph, nullptr);
  EXPECT_GT(Graph->sectionCount(), 0U);
  EXPECT_GT(Graph->symbolCount(), 0U);
}

TEST_F(PluginLinkInputTest,
       KeepsDuplicateArchiveMembersLazyAndEnforcesBudget) {
  const std::vector<uint8_t> Archive = makeArchive();
  auto Request = request(
      {blobInput(NEVERC_LINK_INPUT_ARCHIVE, Archive)});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  LinkInputReader Reader(
      *Task, *FileSystem, *Objects,
      LinkInputReaderOptions{/*MaterializationBudgetBytes=*/8});
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().archiveMembers().size(), 2U);
  auto It = (*Inputs)->graph().archiveMembers().begin();
  const uint64_t FirstID = It->ID;
  EXPECT_EQ(It->Name, "duplicate.o");
  EXPECT_FALSE(It->Materialized);
  ++It;
  const uint64_t SecondID = It->ID;
  EXPECT_EQ(It->Name, "duplicate.o");
  EXPECT_NE(FirstID, SecondID);

  EXPECT_FALSE((*Inputs)->materializeArchiveMember(FirstID, "unit-test"));
  const PluginLinkArchiveMember *First =
      (*Inputs)->graph().findArchiveMember(FirstID);
  ASSERT_NE(First, nullptr);
  EXPECT_TRUE(First->Materialized);
  EXPECT_EQ(First->MaterializationReason, "unit-test");
  EXPECT_EQ((*Inputs)->materializedBytes(), 8U);
  EXPECT_NE((*Inputs)->objectGraphForInput(FirstID), nullptr);

  Error BudgetError =
      (*Inputs)->materializeArchiveMember(SecondID, "unit-test");
  ASSERT_TRUE(static_cast<bool>(BudgetError));
  EXPECT_NE(errorText(std::move(BudgetError)).find(
                "materialization budget exceeded"),
            std::string::npos);
  EXPECT_FALSE(
      (*Inputs)->graph().findArchiveMember(SecondID)->Materialized);
}

TEST_F(PluginLinkInputTest, ArchiveMaterializationObservesCancellation) {
  auto Request =
      request({blobInput(NEVERC_LINK_INPUT_ARCHIVE, makeArchive())});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  const uint64_t MemberID =
      (*Inputs)->graph().archiveMembers().front().ID;
  Session->cancel();
  Error Cancelled =
      (*Inputs)->materializeArchiveMember(MemberID, "cancelled");
  ASSERT_TRUE(static_cast<bool>(Cancelled));
  EXPECT_NE(errorText(std::move(Cancelled)).find("cancelled"),
            std::string::npos);
  EXPECT_FALSE(
      (*Inputs)->graph().findArchiveMember(MemberID)->Materialized);
}

TEST_F(PluginLinkInputTest, WholeArchiveMaterializesEveryMember) {
  auto Request = request({blobInput(
      NEVERC_LINK_INPUT_ARCHIVE, makeArchive(),
      NEVERC_LINK_INPUT_FLAG_WHOLE_ARCHIVE)});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  for (const PluginLinkArchiveMember &Member :
       (*Inputs)->graph().archiveMembers()) {
    EXPECT_TRUE(Member.Materialized);
    EXPECT_EQ(Member.MaterializationReason, "whole-archive");
  }
}

TEST_F(PluginLinkInputTest, ArchiveSymbolExtractionReachesFixedPoint) {
  auto Request = request({blobInput(
      NEVERC_LINK_INPUT_ARCHIVE, makeIndexedArchive("wanted_symbol"),
      NEVERC_LINK_INPUT_FLAG_START_GROUP |
          NEVERC_LINK_INPUT_FLAG_END_GROUP)});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());

  const std::array<StringRef, 1> Undefined = {"wanted_symbol"};
  auto Count = (*Inputs)->materializeArchiveSymbols(Undefined);
  ASSERT_TRUE(static_cast<bool>(Count))
      << errorText(Count.takeError());
  EXPECT_EQ(*Count, 1U);
  ASSERT_EQ((*Inputs)->graph().archiveMembers().size(), 1U);
  const PluginLinkArchiveMember &Member =
      (*Inputs)->graph().archiveMembers().front();
  EXPECT_TRUE(Member.Materialized);
  EXPECT_EQ(Member.MaterializationReason,
            "undefined symbol wanted_symbol");
  auto Repeated = (*Inputs)->materializeArchiveSymbols(Undefined);
  ASSERT_TRUE(static_cast<bool>(Repeated))
      << errorText(Repeated.takeError());
  EXPECT_EQ(*Repeated, 0U);
}

TEST_F(PluginLinkInputTest, ResolvesThinArchiveMembersThroughTaskVFS) {
  const std::string Object = "NOBJ-vfs";
  std::string ThinArchive = "!<thin>\n";
  appendArchiveMemberHeader(ThinArchive, "member.nobj", Object.size());
  ASSERT_TRUE(FileSystem->addFile(
      "/work/member.nobj", 1,
      MemoryBuffer::getMemBufferCopy(Object, "/work/member.nobj")));
  ASSERT_TRUE(FileSystem->addFile(
      "/work/library.a", 1,
      MemoryBuffer::getMemBufferCopy(ThinArchive, "/work/library.a")));

  OwnedRawLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_ARCHIVE;
  Input.Ordinal = 0;
  Input.LogicalURI = "/work/library.a";
  auto Request = request({Input});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().archiveMembers().size(), 1U);
  const uint64_t MemberID =
      (*Inputs)->graph().archiveMembers().front().ID;
  EXPECT_FALSE((*Inputs)->materializeArchiveMember(MemberID, "thin-vfs"));
  EXPECT_TRUE(
      (*Inputs)->graph().findArchiveMember(MemberID)->Materialized);
  EXPECT_NE((*Inputs)->objectGraphForInput(MemberID), nullptr);
}

TEST_F(PluginLinkInputTest, MaterializesPluginProvidedVirtualArchive) {
  auto PluginFS = createPluginFileSystem(*Task, FileSystem);
  ASSERT_TRUE(static_cast<bool>(PluginFS))
      << errorText(PluginFS.takeError());
  OwnedRawLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_ARCHIVE;
  Input.Ordinal = 0;
  Input.LogicalURI = "/virtual/libanswers.a";
  auto Request = request({Input});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  LinkInputReader Reader(*Task, **PluginFS, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().archives().size(), 1U);
  EXPECT_TRUE((*Inputs)->graph().archives().front().Thin);
  ASSERT_EQ((*Inputs)->graph().archiveMembers().size(), 1U);
  const uint64_t MemberID =
      (*Inputs)->graph().archiveMembers().front().ID;
  EXPECT_FALSE(
      (*Inputs)->materializeArchiveMember(MemberID, "plugin-vfs"));
  EXPECT_TRUE(
      (*Inputs)->graph().findArchiveMember(MemberID)->Materialized);
  EXPECT_NE((*Inputs)->objectGraphForInput(MemberID), nullptr);
}

TEST_F(PluginLinkInputTest, MaterializesCOFFImportLibraryAsSharedDefinitions) {
  auto Request = request({blobInput(
      NEVERC_LINK_INPUT_ARCHIVE, makeCOFFImportArchive())});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().archiveMembers().size(), 1U);
  const uint64_t MemberID =
      (*Inputs)->graph().archiveMembers().front().ID;
  EXPECT_FALSE(
      (*Inputs)->materializeArchiveMember(MemberID, "coff-import"));
  ASSERT_EQ((*Inputs)->graph().sharedLibraries().size(), 1U);
  const PluginLinkSharedLibrary &Library =
      (*Inputs)->graph().sharedLibraries().front();
  EXPECT_EQ(Library.InstallName, "answer.dll");
  EXPECT_TRUE(llvm::is_contained(Library.Exports, "imported_answer"));
  EXPECT_TRUE(llvm::is_contained(Library.Exports,
                                 "__imp_imported_answer"));
}

TEST_F(PluginLinkInputTest, LinkerScriptUsesTypedProvider) {
  const std::string Script =
      "SEARCH_DIR(\"/sdk/lib\")\n"
      "INPUT(first.o AS_NEEDED(second.so))\n"
      "GROUP(third.a fourth.a)\n"
      "ENTRY(_start)\n"
      "SECTIONS { .text : { *(.text) } }\n";
  auto Request = request({blobInput(
      NEVERC_LINK_INPUT_SCRIPT,
      std::vector<uint8_t>(Script.begin(), Script.end()))});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  const PluginLinkInput &Input = (*Inputs)->graph().inputs().front();
  EXPECT_EQ(Input.ReaderRoute, "builtin.gnu-linker-script");
  const LinkerScriptResult *Result =
      (*Inputs)->scriptResultForInput(Input.ID);
  ASSERT_NE(Result, nullptr);
  ASSERT_EQ(Result->Inputs.size(), 4U);
  EXPECT_EQ(Result->Inputs[0].LogicalURI, "first.o");
  EXPECT_TRUE(Result->Inputs[1].AsNeeded);
  EXPECT_TRUE(Result->Inputs[2].InGroup);
  ASSERT_EQ(Result->LayoutConstraints.size(), 1U);
  EXPECT_EQ(Result->LayoutConstraints[0].Kind, "sections");
  EXPECT_EQ((*Inputs)->graph().constraints().size(), 1U);
}

TEST_F(PluginLinkInputTest, BitcodeIsRegisteredWithoutMaterialization) {
  std::vector<uint8_t> Bitcode{0x42, 0x43, 0xc0, 0xde, 0, 0, 0, 0};
  auto Request =
      request({blobInput(NEVERC_LINK_INPUT_BITCODE, Bitcode)});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  EXPECT_EQ((*Inputs)->graph().bitcodeModules().size(), 1U);
  EXPECT_EQ((*Inputs)->graph().inputs().front().ReaderRoute,
            "llvm-bitcode");
  EXPECT_EQ((*Inputs)->materializedBytes(), 0U);
}

TEST_F(PluginLinkInputTest, ReadsTBDSharedDefinitions) {
  const std::string TBD =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ arm64-macos ]\n"
      "install-name: '/usr/lib/libinput-test.dylib'\n"
      "exports:\n"
      "  - targets: [ arm64-macos ]\n"
      "    symbols: [ _input_test_export ]\n"
      "...\n";
  auto Request = request({blobInput(
      NEVERC_LINK_INPUT_SHARED_LIBRARY,
      std::vector<uint8_t>(TBD.begin(), TBD.end()))});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  LinkInputReader Reader(*Task, *FileSystem, *Objects);
  auto Inputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(Inputs))
      << errorText(Inputs.takeError());
  ASSERT_EQ((*Inputs)->graph().sharedLibraries().size(), 1U);
  const PluginLinkSharedLibrary &Library =
      (*Inputs)->graph().sharedLibraries().front();
  EXPECT_EQ(Library.InstallName, "/usr/lib/libinput-test.dylib");
  ASSERT_EQ(Library.Exports.size(), 1U);
  EXPECT_EQ(Library.Exports.front(), "_input_test_export");
  ASSERT_EQ((*Inputs)->graph().symbols().size(), 1U);
  EXPECT_EQ((*Inputs)->graph().symbols().front().Definition,
            NEVERC_LINK_SYMBOL_SHARED);
}

} // namespace
