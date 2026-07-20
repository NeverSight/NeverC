#include "Link/BuiltinObjectMergeAdapter.h"
#include "Link/LinkGraph.h"
#include "Link/ObjectGraphImporter.h"
#include "Link/ObjectMergeProvider.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e43504d45524745),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{
    UINT64_C(0x4e43504d45524746), UINT64_C(1)};
constexpr NevercInterfaceID TestProductID{
    UINT64_C(0x4e43504d45524750), UINT64_C(1)};

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

Expected<OwnedTargetKey> makeTargetKey(
    NevercTargetID TargetID = TestTargetID,
    NevercObjectFormatID FormatID = TestFormatID) {
  return TargetKeyBuilder()
      .setTargetID(TargetID)
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat(FormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

std::unique_ptr<PluginObjectGraph> makeObject(unsigned SectionCount) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph =
      std::make_unique<PluginObjectGraph>(std::move(*Target));
  for (unsigned I = 0; I != SectionCount; ++I) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = ".input." + std::to_string(I);
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Section.Alignment = 1;
    Section.Data = {static_cast<uint8_t>(I)};
    Graph->sections().push_back(std::move(Section));
  }
  return Graph;
}

void initializeBuiltinTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
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
          {UINT64_C(0x4e434f424a4d4343), Route.TargetID.Low})
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

std::unique_ptr<PluginObjectGraph>
makeBuiltinObject(const BuiltinTargetRoute &Route, StringRef SymbolName) {
  auto Target = makeBuiltinTargetKey(Route);
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return nullptr;
  }
  auto Graph =
      std::make_unique<PluginObjectGraph>(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph->allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0xc3)};
  const uint64_t SectionID = Section.ID;
  Graph->sections().push_back(std::move(Section));

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph->allocateEntityID();
  Symbol.Name = SymbolName.str();
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = SectionID;
  Symbol.Size = 1;
  Symbol.Alignment = 1;
  Graph->symbols().push_back(std::move(Symbol));
  Graph->issueLayoutProof();
  return Graph;
}

class LinkTaskScope {
public:
  LinkTaskScope()
      : Services("neverc-plugin-object-merge-tests",
                 LLVM_VERSION_MAJOR) {}

  bool initialize(StringRef PluginPath = {}) {
    if (Error E = registerPluginIOInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginObjectPhaseInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginLinkInterfaces(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    std::vector<StringRef> Selected;
    if (!PluginPath.empty()) {
      auto Loaded = Services.registry().load(PluginPath);
      if (!Loaded) {
        ADD_FAILURE() << errorText(Loaded.takeError());
        return false;
      }
      Selected.push_back((*Loaded)->descriptor().PluginID);
    }
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), Selected);
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

  ~LinkTaskScope() {
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
  PluginSession &session() { return *Session; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

struct MergeCallbackState {
  unsigned Calls = 0;
  bool ReturnForeignObject = false;
};

NevercStatus NEVERC_CALL mergeObjects(
    void *UserData, NevercTaskHandle Task,
    const NevercObjectMergeRequest *Request,
    NevercObjectMergeCandidate *Candidate) {
  auto *State = static_cast<MergeCallbackState *>(UserData);
  ++State->Calls;
  if (!Request || !Candidate ||
      Request->Objects.ElementStride != sizeof(NevercObjectMergeInput) ||
      !Request->OutputObject)
    return {NEVERC_STATUS_INVALID_ARGUMENT, NEVERC_STATUS_FLAG_NONE, 0};

  auto *Inputs =
      static_cast<const NevercObjectMergeInput *>(Request->Objects.Data);
  uint8_t SectionCount = 0;
  for (uint64_t I = 0; I != Request->Objects.Count; ++I) {
    NevercObjectGraphInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                   NEVERC_OBJECT_API_MINOR, 0};
    NevercStatus Status = Inputs[I].Object->GetGraphInfo(
        Inputs[I].Object->Context, Task, Inputs[I].Graph, &Info);
    if (!neverc_status_is_ok(Status))
      return Status;
    SectionCount += static_cast<uint8_t>(Info.SectionCount);
  }

  const char Name[] = ".merged";
  NevercObjectSectionDescriptor Section{};
  Section.Header = {sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                    NEVERC_OBJECT_API_MINOR, 0};
  Section.Name = {Name, sizeof(Name) - 1};
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Section.Alignment = 1;
  Section.Data = {&SectionCount, 1};
  NevercObjectSectionHandle Created{};
  NevercStatus Status = Request->OutputObject->CreateSection(
      Request->OutputObject->Context, Task, Request->OutputMutation,
      &Section, &Created);
  if (!neverc_status_is_ok(Status))
    return Status;

  Candidate->Object = State->ReturnForeignObject
                          ? NevercObjectGraphHandle{UINT64_C(99),
                                                   UINT64_C(101)}
                          : Request->OutputGraph;
  Candidate->ProductID = TestProductID;
  Candidate->ProducerRouteDigest[0] = 0x42;
  return neverc_status_ok();
}

TEST(PluginObjectGraphImportTest,
     PreservesNormalizedEntitiesExtensionsAndOrigins) {
  auto SourceTarget = makeTargetKey();
  auto LinkTarget = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(SourceTarget))
      << errorText(SourceTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(LinkTarget))
      << errorText(LinkTarget.takeError());
  PluginObjectGraph Source(std::move(*SourceTarget));

  PluginObjectComdat Comdat;
  Comdat.ID = Source.allocateEntityID();
  Comdat.Name = "answer";
  Comdat.Selection = NEVERC_OBJECT_COMDAT_EXACT_MATCH;
  Comdat.Extension.Owner = TestFormatID;
  Comdat.Extension.Version = 3;
  Comdat.Extension.Bytes = {0xaa, 0xbb};
  const uint64_t ObjectComdatID = Comdat.ID;
  Source.comdats().push_back(std::move(Comdat));

  PluginObjectSection Text;
  Text.ID = Source.allocateEntityID();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
               NEVERC_OBJECT_SECTION_EXECUTABLE |
               NEVERC_OBJECT_SECTION_RETAIN;
  Text.Alignment = 16;
  Text.Data = {0, 0, 0, 0, 0, 0, 0, 0};
  Text.ComdatID = ObjectComdatID;
  Text.Extension.Owner = TestFormatID;
  Text.Extension.Version = 7;
  Text.Extension.Bytes = {1, 2, 3};
  const uint64_t ObjectSectionID = Text.ID;
  Source.sections().push_back(std::move(Text));

  PluginObjectSymbol Defined;
  Defined.ID = Source.allocateEntityID();
  Defined.Name = "answer";
  Defined.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Defined.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED;
  Defined.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Defined.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Defined.SectionID = ObjectSectionID;
  Defined.Value = 0;
  Defined.Size = 8;
  Defined.Alignment = 1;
  Defined.ComdatID = ObjectComdatID;
  Defined.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  const uint64_t DefinedID = Defined.ID;
  Source.symbols().push_back(std::move(Defined));

  PluginObjectSymbol Undefined;
  Undefined.ID = Source.allocateEntityID();
  Undefined.Name = "external";
  Undefined.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Undefined.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  Undefined.Alignment = 1;
  Undefined.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
  const uint64_t UndefinedID = Undefined.ID;
  Source.symbols().push_back(std::move(Undefined));

  PluginObjectRelocation Relocation;
  Relocation.ID = Source.allocateEntityID();
  Relocation.SectionID = ObjectSectionID;
  Relocation.Offset = 0;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 32;
  Relocation.IsPCRelative = true;
  Relocation.IsSigned = true;
  Relocation.Addend = -4;
  Relocation.TargetSymbolID = UndefinedID;
  const uint64_t RelocationID = Relocation.ID;
  Source.relocations().push_back(std::move(Relocation));

  ASSERT_FALSE(verifyPluginObjectGraph(Source));

  PluginLinkGraph Link(std::move(*LinkTarget));
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///answer.o";
  const uint64_t InputID = Link.addInput(std::move(Input)).ID;
  ObjectGraphImportOptions Options;
  Options.InputID = InputID;
  Options.ObjectGraph = {UINT64_C(7), UINT64_C(11)};
  auto Imported = importObjectGraph(Link, Source, Options);
  ASSERT_TRUE(static_cast<bool>(Imported))
      << errorText(Imported.takeError());
  ASSERT_FALSE(verifyPluginLinkGraph(Link));

  const PluginLinkSection *Section =
      Link.findSection(Imported->Sections.at(ObjectSectionID));
  ASSERT_NE(Section, nullptr);
  EXPECT_EQ(Section->Kind, NEVERC_OBJECT_SECTION_KIND_TEXT);
  EXPECT_EQ(Section->Flags, NEVERC_OBJECT_SECTION_ALLOCATED |
                                NEVERC_OBJECT_SECTION_EXECUTABLE |
                                NEVERC_OBJECT_SECTION_RETAIN);
  EXPECT_EQ(Section->ComdatID,
            Imported->Comdats.at(ObjectComdatID));
  ASSERT_EQ(Section->Extensions.values().size(), 1u);
  EXPECT_EQ(Section->Extensions.values()[0].Payload,
            (std::vector<uint8_t>{1, 2, 3}));

  const PluginLinkAtom *Atom =
      Link.findAtom(Imported->Atoms.at(ObjectSectionID));
  ASSERT_NE(Atom, nullptr);
  EXPECT_EQ(Atom->Content.size(), 8u);
  EXPECT_NE(Atom->Flags & NEVERC_LINK_ATOM_ROOT, 0u);
  EXPECT_EQ(Atom->Origin.InputID, InputID);
  EXPECT_EQ(Atom->Origin.ObjectEntityID, ObjectSectionID);
  EXPECT_EQ(Atom->Origin.ObjectGraph.Owner, UINT64_C(7));

  const PluginLinkSymbol *DefinedLink =
      Link.findSymbol(Imported->Symbols.at(DefinedID));
  ASSERT_NE(DefinedLink, nullptr);
  EXPECT_EQ(DefinedLink->AtomID, Atom->ID);
  EXPECT_EQ(DefinedLink->Type,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION);
  EXPECT_TRUE(DefinedLink->IsExported);
  const PluginLinkSymbol *UndefinedLink =
      Link.findSymbol(Imported->Symbols.at(UndefinedID));
  ASSERT_NE(UndefinedLink, nullptr);
  EXPECT_TRUE(UndefinedLink->IsImported);
  ASSERT_EQ(Link.imports().size(), 1u);
  ASSERT_EQ(Link.exports().size(), 1u);

  const PluginLinkEdge *Edge =
      Link.findEdge(Imported->Relocations.at(RelocationID));
  ASSERT_NE(Edge, nullptr);
  EXPECT_EQ(Edge->SourceAtomID, Atom->ID);
  EXPECT_EQ(Edge->TargetSymbolID, UndefinedLink->ID);
  EXPECT_EQ(Edge->RelocationKind,
            NEVERC_OBJECT_RELOCATION_PC_RELATIVE);
  EXPECT_TRUE(Edge->IsPCRelative);
  EXPECT_TRUE(Edge->IsSigned);
  EXPECT_EQ(Edge->Addend, -4);
}

TEST(PluginObjectGraphImportTest, RejectsForeignTargetWithoutMutation) {
  auto SourceTarget = makeTargetKey();
  auto OtherTarget =
      makeTargetKey({UINT64_C(0xdead), UINT64_C(0xbeef)});
  ASSERT_TRUE(static_cast<bool>(SourceTarget))
      << errorText(SourceTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(OtherTarget))
      << errorText(OtherTarget.takeError());
  PluginObjectGraph Source(std::move(*SourceTarget));
  PluginLinkGraph Link(std::move(*OtherTarget));

  auto Imported = importObjectGraph(Link, Source);
  ASSERT_FALSE(Imported);
  EXPECT_NE(errorText(Imported.takeError()).find("does not match"),
            std::string::npos);
  EXPECT_TRUE(Link.sections().empty());
  EXPECT_TRUE(Link.symbols().empty());
}

TEST(PluginObjectMergeProviderTest,
     ExposesEveryInputAndCommitsHostOwnedOutput) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto First = makeObject(1);
  auto Second = makeObject(2);
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

  MergeCallbackState State;
  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = "org.neverc.builtin.test";
  Provider.ProviderID = "merge";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeObjects;
  Provider.UserData = &State;
  Provider.Builtin = true;
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name, ".merged");
  EXPECT_EQ(Merged->Object->sections().front().Data,
            (std::vector<uint8_t>{3}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x42);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest, RejectsForeignOutputHandle) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Input = makeObject(1);
  auto Target = makeTargetKey();
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

  MergeCallbackState State;
  State.ReturnForeignObject = true;
  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = "org.neverc.builtin.test";
  Provider.ProviderID = "foreign-output";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeObjects;
  Provider.UserData = &State;
  Provider.Builtin = true;
  PluginObjectGraph *InputPointer = Input.get();

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target),
      ArrayRef<PluginObjectGraph *>(&InputPointer, 1));
  ASSERT_FALSE(Merged);
  EXPECT_NE(errorText(Merged.takeError()).find("foreign output"),
            std::string::npos);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest,
     DispatchesRegisteredPluginThroughPlannedRoute) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));
  auto First = makeObject(2);
  auto Second = makeObject(3);
  auto Target = makeTargetKey();
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

  auto Snapshot = PluginLinkRegistry::freeze(Scope.session().plugins());
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  ASSERT_EQ((*Snapshot)->objectMergeProviders().size(), 1u);

  LinkRouteRequest Request;
  Request.TargetID = TestTargetID;
  Request.InputFormat = TestFormatID;
  Request.OutputFormat = TestFormatID;
  Request.OutputKind = NEVERC_LINK_OUTPUT_RELOCATABLE;
  auto Route = LinkRoutePlanner::plan(
      (*Snapshot)->linkerProviders(),
      (*Snapshot)->objectMergeProviders(), Request);
  ASSERT_TRUE(static_cast<bool>(Route))
      << errorText(Route.takeError());
  ASSERT_EQ(Route->kind(), PlannedLinkRoute::Kind::ObjectMerge);
  ASSERT_NE(Route->objectMergeProvider(), nullptr);

  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeObjectMergeProvider(
      Scope.task(), *Route->objectMergeProvider(), std::move(*Target),
      Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name,
            ".plugin-merged");
  EXPECT_EQ(Merged->Object->sections().front().Data,
            (std::vector<uint8_t>{5}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x63);
  EXPECT_EQ(Merged->PluginID, "org.neverc.test.object-merge");
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAdapterRoundTripsTypedGraphsThroughRelocatableMerge) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::x86_64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto First = makeBuiltinObject(*ELFRoute, "merge_first");
  auto Second = makeBuiltinObject(*ELFRoute, "merge_second");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target))
      << errorText(Target.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot))
      << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged))
      << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  EXPECT_EQ(Merged->PluginID, "neverc.builtin");
  EXPECT_EQ(Merged->ProviderID, "neverc.builtin.object-merge");

  const auto HasSymbol = [&](StringRef Name) {
    const auto &Symbols = Merged->Object->symbols();
    return std::any_of(Symbols.begin(), Symbols.end(),
                       [&](const PluginObjectSymbol &Symbol) {
                         return Symbol.Name == Name;
                       });
  };
  EXPECT_TRUE(HasSymbol("merge_first"));
  EXPECT_TRUE(HasSymbol("merge_second"));
}

} // namespace
