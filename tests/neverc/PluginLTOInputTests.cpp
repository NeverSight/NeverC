#include "LTO/LTOInputSet.h"
#include "Link/LinkInputReader.h"
#include "Link/LinkRequest.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{
    UINT64_C(0x4e434c544f494e50), UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{
    UINT64_C(0x4e434c544f464d54), UINT64_C(1)};

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

Expected<OwnedTargetKey> makeTargetKey() {
  return TargetKeyBuilder()
      .setTargetID(TestTargetID)
      .setTriple("x86_64-unknown-linux-gnu", "x86_64", "unknown",
                 "linux", "gnu")
      .setCPU("x86-64", "x86-64")
      .setFeatures({})
      .setABI({UINT64_C(0x4e434c544f414249), UINT64_C(1)})
      .setCallingConvention(
          {UINT64_C(0x4e434c544f434349), UINT64_C(1)})
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

std::vector<uint8_t> makeBitcode(StringRef Identifier,
                                 bool DefineEntry) {
  LLVMContext Context;
  Module ModuleValue(Identifier, Context);
  ModuleValue.setTargetTriple("x86_64-unknown-linux-gnu");
  ModuleValue.setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  FunctionType *Type =
      FunctionType::get(Type::getInt32Ty(Context), false);
  Function *External = Function::Create(
      Type, GlobalValue::ExternalLinkage, "external_ref", ModuleValue);

  auto define = [&](StringRef Name) {
    Function *Value = Function::Create(
        Type, GlobalValue::ExternalLinkage, Name, ModuleValue);
    BasicBlock *Entry =
        BasicBlock::Create(Context, "entry", Value);
    IRBuilder<> Builder(Entry);
    if (Name == "entry")
      Builder.CreateCall(External);
    Builder.CreateRet(ConstantInt::get(Type->getReturnType(), 0));
  };
  if (DefineEntry)
    define("entry");
  define("native_override");

  SmallVector<char, 0> Bytes;
  raw_svector_ostream Output(Bytes);
  WriteBitcodeToFile(ModuleValue, Output);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

class LTOInputTaskScope {
public:
  LTOInputTaskScope()
      : Services("neverc-plugin-lto-input-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = registerPluginLinkInterfaces(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    if (Error E = activatePluginPlan(Services, *Plan)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_LINK);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~LTOInputTaskScope() {
    if (Task && !Task->isEnded())
      if (Error E = Task->end())
        ADD_FAILURE() << errorText(std::move(E));
    if (Session && !Session->isEnded())
      if (Error E = Session->end())
        ADD_FAILURE() << errorText(std::move(E));
    Plan.reset();
    if (Error E = Services.shutdown())
      ADD_FAILURE() << errorText(std::move(E));
  }

  PluginTaskContext &task() { return *Task; }
  PluginProcessServices &services() { return Services; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

Expected<std::shared_ptr<const LinkRequest>>
makeRequest(PluginTaskContext &Task,
            std::vector<std::vector<uint8_t>> BitcodeInputs) {
  auto Target = makeTargetKey();
  if (!Target)
    return Target.takeError();
  LinkRequestData Data;
  Data.Task = Task.handle();
  Data.Target = std::move(*Target);
  Data.InputFormat = TestFormatID;
  Data.OutputFormat = TestFormatID;
  Data.OutputKind = NEVERC_LINK_OUTPUT_EXECUTABLE;
  Data.OutputURI = "/out/lto-input";
  for (size_t Index = 0; Index != BitcodeInputs.size(); ++Index) {
    OwnedRawLinkInput Input;
    Input.Kind = NEVERC_LINK_INPUT_BITCODE;
    Input.Ordinal = Index;
    Input.LogicalURI =
        "/inputs/module-" + std::to_string(Index) + ".bc";
    Input.AuthorizedBlob = std::move(BitcodeInputs[Index]);
    Data.Inputs.push_back(std::move(Input));
  }
  return LinkRequest::create(std::move(Data));
}

const LTOSymbolResolutionRecord *
findResolution(const LTOInputSet &Inputs, StringRef Name) {
  auto It = llvm::find_if(
      Inputs.resolutions(), [&](const LTOSymbolResolutionRecord &Value) {
        return Value.SymbolName == Name;
      });
  return It == Inputs.resolutions().end() ? nullptr : &*It;
}

TEST(PluginLTOInputTest,
     BuildsTypedModuleMetadataAndRegularSymbolResolutions) {
  LTOInputTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Request = makeRequest(
      Scope.task(), {makeBitcode("first", true)});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Objects = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Objects))
      << errorText(Objects.takeError());
  auto FileSystem = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
  LinkInputReader Reader(Scope.task(), *FileSystem, **Objects);
  auto LinkInputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(LinkInputs))
      << errorText(LinkInputs.takeError());

  PluginLinkSymbol Reference;
  Reference.Name = "entry";
  Reference.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Reference.Definition = NEVERC_LINK_SYMBOL_UNDEFINED;
  LinkInputs->get()->graph().addSymbol(std::move(Reference));
  PluginLinkSection Section;
  Section.Name = ".text.native";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Size = 1;
  const uint64_t SectionID =
      LinkInputs->get()->graph().addSection(std::move(Section)).ID;
  PluginLinkAtom Atom;
  Atom.SectionID = SectionID;
  Atom.Name = "native_override";
  Atom.Content = {UINT8_C(0xc3)};
  const uint64_t AtomID =
      LinkInputs->get()->graph().addAtom(std::move(Atom)).ID;
  PluginLinkSymbol Override;
  Override.Name = "native_override";
  Override.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Override.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Override.AtomID = AtomID;
  LinkInputs->get()->graph().addSymbol(std::move(Override));

  auto LTOInputs = LTOInputSet::create(
      Scope.task(), *Request, std::move(*LinkInputs));
  ASSERT_TRUE(static_cast<bool>(LTOInputs))
      << errorText(LTOInputs.takeError());
  ASSERT_EQ((*LTOInputs)->modules().size(), 1U);
  const PluginLinkBitcodeModule &Module =
      *(*LTOInputs)->modules().front().Module;
  EXPECT_EQ(Module.ModuleIdentifier, "/inputs/module-0.bc");
  EXPECT_EQ(Module.TargetTriple, "x86_64-unknown-linux-gnu");
  EXPECT_FALSE(Module.DataLayout.empty());
  EXPECT_FALSE(Module.ProducerBuild.empty());
  EXPECT_GE(Module.Symbols.size(), 3U);

  const LTOSymbolResolutionRecord *Entry =
      findResolution(**LTOInputs, "entry");
  ASSERT_NE(Entry, nullptr);
  EXPECT_NE(Entry->Flags & NEVERC_LTO_SYMBOL_PREVAILING, 0U);
  EXPECT_NE(
      Entry->Flags &
          NEVERC_LTO_SYMBOL_VISIBLE_TO_REGULAR_OBJECT,
      0U);
  EXPECT_NE(
      Entry->Flags &
          NEVERC_LTO_SYMBOL_REFERENCED_BY_REGULAR_OBJECT,
      0U);
  const LTOSymbolResolutionRecord *OverrideResolution =
      findResolution(**LTOInputs, "native_override");
  ASSERT_NE(OverrideResolution, nullptr);
  EXPECT_EQ(OverrideResolution->Flags &
                NEVERC_LTO_SYMBOL_PREVAILING,
            0U);
}

TEST(PluginLTOInputTest,
     ExposesCallerBufferedCAPIAndRejectsInvalidResolutionMutation) {
  LTOInputTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Request = makeRequest(
      Scope.task(), {makeBitcode("c-api", true)});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Objects = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Objects))
      << errorText(Objects.takeError());
  auto FileSystem = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
  LinkInputReader Reader(Scope.task(), *FileSystem, **Objects);
  auto LinkInputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(LinkInputs))
      << errorText(LinkInputs.takeError());
  auto LTOInputs = LTOInputSet::create(
      Scope.task(), *Request, std::move(*LinkInputs));
  ASSERT_TRUE(static_cast<bool>(LTOInputs))
      << errorText(LTOInputs.takeError());

  auto Service = findLTOProcessService(Scope.services());
  ASSERT_NE(Service, nullptr);
  NevercLTORequest RequestInfo{};
  RequestInfo.Header = {sizeof(RequestInfo), NEVERC_LTO_API_MAJOR,
                        NEVERC_LTO_API_MINOR, 0};
  NevercStatus Status = Service->api().GetRequest(
      Service->api().Context, Scope.task().handle(),
      (*LTOInputs)->requestHandle(), &RequestInfo);
  ASSERT_EQ(Status.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(RequestInfo.Modules.Count, 1U);
  EXPECT_EQ(RequestInfo.Resolutions.Count,
            (*LTOInputs)->resolutions().size());

  std::vector<NevercLTOSymbolResolution> PageStorage(
      (*LTOInputs)->resolutions().size());
  NevercLinkEntityPage Page{};
  Page.Header = {sizeof(Page), NEVERC_LTO_API_MAJOR,
                 NEVERC_LTO_API_MINOR, 0};
  Page.Data = PageStorage.data();
  Page.ElementStride = sizeof(NevercLTOSymbolResolution);
  Page.ElementCapacity = PageStorage.size();
  Status = Service->api().GetResolutionPage(
      Service->api().Context, Scope.task().handle(),
      (*LTOInputs)->requestHandle(), 0, &Page);
  ASSERT_EQ(Status.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Page.OutCount, PageStorage.size());

  const LTOSymbolResolutionRecord *Entry =
      findResolution(**LTOInputs, "entry");
  ASSERT_NE(Entry, nullptr);
  std::array<uint8_t, 32> DigestBefore{};
  std::copy((*LTOInputs)->resolutionDigest().begin(),
            (*LTOInputs)->resolutionDigest().end(),
            DigestBefore.begin());
  Error Mutation = (*LTOInputs)->setResolutionFlags(
      Entry->Handle,
      Entry->Flags | NEVERC_LTO_SYMBOL_CAN_INTERNALIZE |
          NEVERC_LTO_SYMBOL_VISIBLE_TO_REGULAR_OBJECT);
  ASSERT_TRUE(static_cast<bool>(Mutation));
  EXPECT_NE(errorText(std::move(Mutation)).find("internalization"),
            std::string::npos);
  EXPECT_TRUE(std::equal(
      (*LTOInputs)->resolutionDigest().begin(),
      (*LTOInputs)->resolutionDigest().end(),
      DigestBefore.begin()));
}

TEST(PluginLTOInputTest, RejectsIncompatibleBitcodeTarget) {
  LTOInputTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  LLVMContext Context;
  Module ModuleValue("foreign", Context);
  ModuleValue.setTargetTriple("aarch64-unknown-linux-gnu");
  ModuleValue.setDataLayout("e-p:64:64-i64:64-n32:64-S128");
  SmallVector<char, 0> Bytes;
  raw_svector_ostream Output(Bytes);
  WriteBitcodeToFile(ModuleValue, Output);

  auto Request = makeRequest(
      Scope.task(),
      {std::vector<uint8_t>(Bytes.begin(), Bytes.end())});
  ASSERT_TRUE(static_cast<bool>(Request))
      << errorText(Request.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  auto Objects = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Objects))
      << errorText(Objects.takeError());
  auto FileSystem = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
  LinkInputReader Reader(Scope.task(), *FileSystem, **Objects);
  auto LinkInputs = Reader.read(**Request);
  ASSERT_TRUE(static_cast<bool>(LinkInputs))
      << errorText(LinkInputs.takeError());

  auto LTOInputs = LTOInputSet::create(
      Scope.task(), *Request, std::move(*LinkInputs));
  ASSERT_FALSE(LTOInputs);
  EXPECT_NE(errorText(LTOInputs.takeError()).find("architecture"),
            std::string::npos);
}

} // namespace
