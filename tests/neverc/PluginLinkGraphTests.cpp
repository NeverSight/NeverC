#include "Link/LinkGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

Expected<OwnedTargetKey> makeTargetKey() {
  return TargetKeyBuilder()
      .setTargetID({UINT64_C(0x4e43504c47524150), UINT64_C(1)})
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat({UINT64_C(0x4e43504f424a5446), UINT64_C(1)})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

class LinkTaskScope {
public:
  LinkTaskScope()
      : Services("neverc-plugin-link-graph-tests", LLVM_VERSION_MAJOR) {}

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
    auto CreatedTask = Session->createTask(NEVERC_TASK_LINK);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~LinkTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }
  Expected<std::unique_ptr<PluginTaskContext>> createSiblingTask() {
    return Session->createTask(NEVERC_TASK_LINK);
  }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

void populateGraph(PluginLinkGraph &Graph, bool Reverse) {
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.Ordinal = 0;
  Input.LogicalURI = "vfs:///input.o";
  Input.ReaderRoute = "org.neverc.builtin.object";
  Input.ContentDigest[0] = 0x42;
  Input.Extensions.values().push_back(
      {{UINT64_C(0x9000), UINT64_C(1)}, 1, false, {1, 2, 3}, "abc"});
  uint64_t InputID = Graph.addInput(std::move(Input)).ID;

  PluginLinkArchive Archive;
  Archive.InputID = InputID;
  Archive.Name = "libanswer.a";
  Archive.Thin = true;
  uint64_t ArchiveID = Graph.addArchive(std::move(Archive)).ID;

  PluginLinkArchiveMember Member;
  Member.InputID = InputID;
  Member.ArchiveID = ArchiveID;
  Member.Name = "answer.o";
  Member.Ordinal = 0;
  Member.ContentDigest[0] = 0x11;
  Member.Materialized = true;
  Member.MaterializationReason = "entry symbol";
  uint64_t MemberID = Graph.addArchiveMember(std::move(Member)).ID;

  PluginLinkSharedLibrary Shared;
  Shared.InputID = InputID;
  Shared.Name = "libanswer.so";
  Shared.InstallName = "libanswer.so.1";
  Shared.ContentDigest[0] = 0x22;
  uint64_t SharedID = Graph.addSharedLibrary(std::move(Shared)).ID;

  PluginLinkBitcodeModule Module;
  Module.InputID = InputID;
  Module.Name = "answer.bc";
  Module.ContentDigest[0] = 0x33;
  uint64_t ModuleID = Graph.addBitcodeModule(std::move(Module)).ID;

  Graph.findInput(InputID)->ArchiveID = ArchiveID;
  Graph.findInput(InputID)->SharedLibraryID = SharedID;
  Graph.findInput(InputID)->BitcodeModuleID = ModuleID;

  PluginLinkComdat Comdat;
  Comdat.Name = "group";
  uint64_t ComdatID = Graph.addComdat(std::move(Comdat)).ID;

  PluginLinkSection Text;
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Alignment = 16;
  Text.Size = 3;
  Text.ComdatID = ComdatID;
  Text.Origin.InputID = InputID;

  PluginLinkSection Data;
  Data.Name = ".data";
  Data.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Data.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Data.Alignment = 8;
  Data.Size = 1;
  Data.Origin.InputID = InputID;

  uint64_t TextID = 0;
  uint64_t DataID = 0;
  if (Reverse) {
    DataID = Graph.addSection(std::move(Data)).ID;
    TextID = Graph.addSection(std::move(Text)).ID;
  } else {
    TextID = Graph.addSection(std::move(Text)).ID;
    DataID = Graph.addSection(std::move(Data)).ID;
  }

  PluginLinkAtom Entry;
  Entry.SectionID = TextID;
  Entry.Name = "entry";
  Entry.Alignment = 16;
  Entry.Content = {0x90, 0x90, 0xc3};
  Entry.ComdatID = ComdatID;
  Entry.Origin.InputID = InputID;
  uint64_t AtomID = Graph.addAtom(std::move(Entry)).ID;

  PluginLinkAtom Global;
  Global.SectionID = DataID;
  Global.Name = "global";
  Global.Alignment = 8;
  Global.Content = {0};
  Global.Origin.InputID = InputID;
  uint64_t GlobalAtomID = Graph.addAtom(std::move(Global)).ID;

  PluginLinkSymbol EntrySymbol;
  EntrySymbol.Name = "entry";
  EntrySymbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  EntrySymbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  EntrySymbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  EntrySymbol.AtomID = AtomID;
  EntrySymbol.IsPrevailing = true;
  EntrySymbol.IsRoot = true;
  EntrySymbol.Origin.InputID = InputID;
  uint64_t EntrySymbolID = Graph.addSymbol(std::move(EntrySymbol)).ID;

  PluginLinkSymbol GlobalSymbol;
  GlobalSymbol.Name = "global";
  GlobalSymbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  GlobalSymbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  GlobalSymbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  GlobalSymbol.AtomID = GlobalAtomID;
  GlobalSymbol.IsPrevailing = true;
  GlobalSymbol.Origin.InputID = InputID;
  uint64_t GlobalSymbolID = Graph.addSymbol(std::move(GlobalSymbol)).ID;

  PluginLinkEdge Edge;
  Edge.SourceAtomID = AtomID;
  Edge.Offset = 1;
  Edge.Width = 32;
  Edge.IsPCRelative = true;
  Edge.IsSigned = true;
  Edge.TargetSymbolID = GlobalSymbolID;
  Edge.Origin.InputID = InputID;
  Graph.addEdge(std::move(Edge));

  PluginLinkImport Import;
  Import.Name = "puts";
  Import.Library = "libc";
  Import.SymbolID = GlobalSymbolID;
  Import.Origin.InputID = InputID;
  Graph.addImport(std::move(Import));

  PluginLinkExport Export;
  Export.Name = "entry";
  Export.SymbolID = EntrySymbolID;
  Export.Origin.InputID = InputID;
  Graph.addExport(std::move(Export));

  PluginLinkUnwindRecord Unwind;
  Unwind.AtomID = AtomID;
  Unwind.PersonalitySymbolID = GlobalSymbolID;
  Unwind.Origin.InputID = InputID;
  Graph.addUnwind(std::move(Unwind));

  PluginLinkSynthetic Synthetic;
  Synthetic.Role = "entry-table";
  Synthetic.SectionID = TextID;
  Synthetic.AtomID = AtomID;
  Synthetic.Origin.InputID = InputID;
  Graph.addSynthetic(std::move(Synthetic));

  PluginLinkConstraint Constraint;
  Constraint.Kind = "minimum-address";
  Constraint.SubjectID = AtomID;
  Constraint.Value = 0x1000;
  Constraint.Required = true;
  Constraint.Origin.InputID = InputID;
  Graph.addConstraint(std::move(Constraint));

  Graph.findArchiveMember(MemberID)->Origin.InputID = InputID;
}

TEST(PluginLinkGraphTest, PagedQueriesReturnStableTypedHandles) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  populateGraph(Graph, false);
  LinkGraphPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());

  NevercLinkGraphInfo GraphInfo{};
  GraphInfo.Header = {sizeof(GraphInfo), NEVERC_LINK_API_MAJOR,
                      NEVERC_LINK_API_MINOR, 0};
  ASSERT_EQ(Bridge.api()
                .GetGraphInfo(Bridge.api().Context, Scope.task().handle(),
                              *GraphHandle, &GraphInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(GraphInfo.SectionCount, 2U);
  EXPECT_EQ(GraphInfo.AtomCount, 2U);
  EXPECT_EQ(GraphInfo.SymbolCount, 2U);
  EXPECT_EQ(GraphInfo.EdgeCount, 1U);
  EXPECT_EQ(GraphInfo.ArchiveCount, 1U);
  EXPECT_EQ(GraphInfo.ArchiveMemberCount, 1U);
  EXPECT_EQ(GraphInfo.SharedLibraryCount, 1U);
  EXPECT_EQ(GraphInfo.BitcodeModuleCount, 1U);
  EXPECT_EQ(GraphInfo.ImportCount, 1U);
  EXPECT_EQ(GraphInfo.ExportCount, 1U);
  EXPECT_EQ(GraphInfo.UnwindCount, 1U);
  EXPECT_EQ(GraphInfo.SyntheticCount, 1U);
  EXPECT_EQ(GraphInfo.ConstraintCount, 1U);

  std::array<NevercLinkSectionInfo, 1> PageStorage{};
  NevercLinkEntityPage Page{};
  Page.Header = {sizeof(Page), NEVERC_LINK_API_MAJOR,
                 NEVERC_LINK_API_MINOR, 0};
  Page.Data = PageStorage.data();
  Page.ElementCapacity = PageStorage.size();
  Page.ElementStride = sizeof(PageStorage[0]);
  ASSERT_EQ(Bridge.api()
                .GetSectionPage(Bridge.api().Context, Scope.task().handle(),
                                *GraphHandle, 0, &Page)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Page.OutCount, 1U);
  EXPECT_TRUE(Page.HasMore);
  EXPECT_EQ(Page.NextCursor, 1U);
  EXPECT_EQ(std::string(PageStorage[0].Name.Data,
                        PageStorage[0].Name.Length),
            ".text");

  NevercLinkSectionInfo Direct{};
  Direct.Header = {sizeof(Direct), NEVERC_LINK_API_MAJOR,
                   NEVERC_LINK_API_MINOR, 0};
  ASSERT_EQ(Bridge.api()
                .GetSectionInfo(Bridge.api().Context, Scope.task().handle(),
                                PageStorage[0].Section, &Direct)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Direct.Section.Owner, PageStorage[0].Section.Owner);
  EXPECT_EQ(Direct.Section.Value, PageStorage[0].Section.Value);
}

TEST(PluginLinkGraphTest, ExposesEveryNormalizedEntityKindThroughPagedAPI) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  populateGraph(Graph, false);
  LinkGraphPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());

  auto ReadOne = [&](auto &Record, auto PageFunction) {
    NevercLinkEntityPage Page{};
    Page.Header = {sizeof(Page), NEVERC_LINK_API_MAJOR,
                   NEVERC_LINK_API_MINOR, 0};
    Page.Data = &Record;
    Page.ElementCapacity = 1;
    Page.ElementStride = sizeof(Record);
    NevercStatus Status = PageFunction(
        Bridge.api().Context, Scope.task().handle(), *GraphHandle, 0, &Page);
    EXPECT_EQ(Status.Code, NEVERC_STATUS_OK);
    EXPECT_EQ(Page.OutCount, 1U);
  };

  NevercLinkArchiveInfo Archive{};
  NevercLinkArchiveMemberInfo Member{};
  NevercLinkSharedLibraryInfo Shared{};
  NevercLinkBitcodeModuleInfo Module{};
  NevercLinkImportInfo Import{};
  NevercLinkExportInfo Export{};
  NevercLinkUnwindInfo Unwind{};
  NevercLinkSyntheticInfo Synthetic{};
  NevercLinkConstraintInfo Constraint{};
  ReadOne(Archive, Bridge.api().GetArchivePage);
  ReadOne(Member, Bridge.api().GetArchiveMemberPage);
  ReadOne(Shared, Bridge.api().GetSharedLibraryPage);
  ReadOne(Module, Bridge.api().GetBitcodeModulePage);
  ReadOne(Import, Bridge.api().GetImportPage);
  ReadOne(Export, Bridge.api().GetExportPage);
  ReadOne(Unwind, Bridge.api().GetUnwindPage);
  ReadOne(Synthetic, Bridge.api().GetSyntheticPage);
  ReadOne(Constraint, Bridge.api().GetConstraintPage);

  EXPECT_EQ(std::string(Archive.Name.Data, Archive.Name.Length),
            "libanswer.a");
  EXPECT_EQ(std::string(Member.Name.Data, Member.Name.Length), "answer.o");
  EXPECT_EQ(std::string(Shared.InstallName.Data, Shared.InstallName.Length),
            "libanswer.so.1");
  EXPECT_EQ(std::string(Module.Name.Data, Module.Name.Length), "answer.bc");
  EXPECT_EQ(std::string(Import.Name.Data, Import.Name.Length), "puts");
  EXPECT_EQ(std::string(Export.Name.Data, Export.Name.Length), "entry");
  EXPECT_EQ(std::string(Synthetic.Role.Data, Synthetic.Role.Length),
            "entry-table");
  EXPECT_EQ(std::string(Constraint.Kind.Data, Constraint.Kind.Length),
            "minimum-address");
}

TEST(PluginLinkGraphTest, SemanticDigestDoesNotDependOnInsertionOrder) {
  auto FirstTarget = makeTargetKey();
  auto SecondTarget = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(FirstTarget))
      << errorText(FirstTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(SecondTarget))
      << errorText(SecondTarget.takeError());
  PluginLinkGraph First(std::move(*FirstTarget));
  PluginLinkGraph Second(std::move(*SecondTarget));
  populateGraph(First, false);
  populateGraph(Second, true);
  EXPECT_EQ(First.semanticDigest(), Second.semanticDigest());
}

TEST(PluginLinkGraphTest, WrongTaskCannotUseGraphHandle) {
  LinkTaskScope FirstScope;
  ASSERT_TRUE(FirstScope.initialize());
  auto SecondTask = FirstScope.createSiblingTask();
  ASSERT_TRUE(static_cast<bool>(SecondTask))
      << errorText(SecondTask.takeError());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginLinkGraph Graph(std::move(*Target));
  LinkGraphPluginBridge Bridge(FirstScope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle))
      << errorText(GraphHandle.takeError());

  NevercLinkGraphInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_LINK_API_MAJOR,
                 NEVERC_LINK_API_MINOR, 0};
  EXPECT_EQ(Bridge.api()
                .GetGraphInfo(Bridge.api().Context,
                              (*SecondTask)->handle(), *GraphHandle, &Info)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_FALSE((*SecondTask)->end());
}

} // namespace
