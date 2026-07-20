#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
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

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

class ObjectTaskScope {
public:
  ObjectTaskScope()
      : Services("neverc-plugin-object-builder-tests",
                 LLVM_VERSION_MAJOR) {}

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

  ~ObjectTaskScope() {
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

Expected<OwnedTargetKey> makeTargetKey() {
  TargetKeyBuilder Builder;
  Builder.setTargetID({UINT64_C(0x4e43505445535401), UINT64_C(1)})
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention(
          {UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat(
          {UINT64_C(0x4e43504f424a5446), UINT64_C(1)})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

struct ObjectBuilderContext {
  ObjectTaskScope Scope;
  std::unique_ptr<PluginObjectGraph> Graph;
  std::unique_ptr<ObjectPluginBridge> Bridge;

  bool initialize(bool AddLayoutProof = false) {
    if (!Scope.initialize())
      return false;
    auto Target = makeTargetKey();
    if (!Target) {
      ADD_FAILURE() << errorText(Target.takeError());
      return false;
    }
    Graph =
        std::make_unique<PluginObjectGraph>(std::move(*Target));
    if (AddLayoutProof)
      Graph->issueLayoutProof();
    Bridge = std::make_unique<ObjectPluginBridge>(Scope.task(), *Graph);
    return true;
  }

  Expected<NevercObjectGraphHandle> graph() { return Bridge->graph(); }
  const NevercObjectAPI &api() const { return Bridge->api(); }

  NevercObjectMutationHandle beginMutation() {
    NevercObjectMutationHandle Mutation{};
    auto GraphHandle = graph();
    if (!GraphHandle) {
      ADD_FAILURE() << errorText(GraphHandle.takeError());
      return Mutation;
    }
    EXPECT_EQ(api()
                  .BeginMutation(api().Context, Scope.task().handle(),
                                 *GraphHandle, &Mutation)
                  .Code,
              NEVERC_STATUS_OK);
    return Mutation;
  }
};

TEST(PluginObjectBuilderTest, BuildsCompleteGraphAtomically) {
  ObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target));
  PluginObjectGraph Graph(std::move(*Target));
  Graph.issueLayoutProof();
  ObjectPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle));
  const NevercObjectAPI &API = Bridge.api();

  NevercObjectGraphInfo InitialInfo{};
  InitialInfo.Header = {sizeof(InitialInfo), NEVERC_OBJECT_API_MAJOR,
                        NEVERC_OBJECT_API_MINOR, 0};
  ASSERT_EQ(API.GetGraphInfo(API.Context, Scope.task().handle(),
                             *GraphHandle, &InitialInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(InitialInfo.ObjectSchemaDigest.Data,
                        InitialInfo.ObjectSchemaDigest.Length),
            NEVERC_OBJECT_SCHEMA_DIGEST);
  EXPECT_EQ(InitialInfo.Generation, 1U);
  EXPECT_EQ(InitialInfo.HasLayoutProof, NEVERC_TRUE);
  NevercObjectLayoutProofHandle InitialProof{};
  ASSERT_EQ(API.GetLayoutProof(API.Context, Scope.task().handle(),
                               *GraphHandle, &InitialProof)
                .Code,
            NEVERC_STATUS_OK);
  NevercObjectLayoutProofInfo ProofInfo{};
  ProofInfo.Header = {sizeof(ProofInfo), NEVERC_OBJECT_API_MAJOR,
                      NEVERC_OBJECT_API_MINOR, 0};
  ASSERT_EQ(API.GetLayoutProofInfo(API.Context, Scope.task().handle(),
                                   InitialProof, &ProofInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ProofInfo.GraphGeneration, 1U);

  NevercObjectMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(),
                              *GraphHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectComdatDescriptor ComdatDescriptor{};
  ComdatDescriptor.Header = {sizeof(ComdatDescriptor),
                             NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};
  ComdatDescriptor.Name = view("answer");
  ComdatDescriptor.Selection = NEVERC_OBJECT_COMDAT_ANY;
  NevercObjectComdatHandle Comdat{};
  ASSERT_EQ(API.CreateComdat(API.Context, Scope.task().handle(), Mutation,
                             &ComdatDescriptor, &Comdat)
                .Code,
            NEVERC_STATUS_OK);

  std::array<uint8_t, 4> Bytes = {0x2a, 0, 0, 0};
  NevercObjectSectionDescriptor SectionDescriptor{};
  SectionDescriptor.Header = {sizeof(SectionDescriptor),
                              NEVERC_OBJECT_API_MAJOR,
                              NEVERC_OBJECT_API_MINOR, 0};
  SectionDescriptor.Name = view(".text");
  SectionDescriptor.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  SectionDescriptor.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  SectionDescriptor.Alignment = 4;
  SectionDescriptor.Data = {Bytes.data(), Bytes.size()};
  SectionDescriptor.Comdat = Comdat;
  NevercObjectSectionHandle Text{};
  ASSERT_EQ(API.CreateSection(API.Context, Scope.task().handle(), Mutation,
                              &SectionDescriptor, &Text)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectSymbolDescriptor SymbolDescriptor{};
  SymbolDescriptor.Header = {sizeof(SymbolDescriptor),
                             NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};
  SymbolDescriptor.Name = view("answer");
  SymbolDescriptor.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  SymbolDescriptor.Visibility =
      NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  SymbolDescriptor.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  SymbolDescriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  SymbolDescriptor.Section = Text;
  SymbolDescriptor.Size = Bytes.size();
  SymbolDescriptor.Alignment = 4;
  SymbolDescriptor.Comdat = Comdat;
  SymbolDescriptor.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  NevercObjectSymbolHandle Symbol{};
  ASSERT_EQ(API.CreateSymbol(API.Context, Scope.task().handle(), Mutation,
                             &SymbolDescriptor, &Symbol)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectRelocationDescriptor RelocationDescriptor{};
  RelocationDescriptor.Header = {
      sizeof(RelocationDescriptor), NEVERC_OBJECT_API_MAJOR,
      NEVERC_OBJECT_API_MINOR, 0};
  RelocationDescriptor.Section = Text;
  RelocationDescriptor.Offset = 0;
  RelocationDescriptor.Kind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  RelocationDescriptor.TargetKind =
      NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  RelocationDescriptor.Width = 32;
  RelocationDescriptor.IsPCRelative = NEVERC_TRUE;
  RelocationDescriptor.IsSigned = NEVERC_TRUE;
  RelocationDescriptor.Addend = -4;
  RelocationDescriptor.TargetSymbol = Symbol;
  NevercObjectRelocationHandle Relocation{};
  ASSERT_EQ(API.CreateRelocation(
                API.Context, Scope.task().handle(), Mutation,
                &RelocationDescriptor, &Relocation)
                .Code,
            NEVERC_STATUS_OK);

  SectionDescriptor.Name = view(".debug");
  SectionDescriptor.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  SectionDescriptor.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  SectionDescriptor.Alignment = 1;
  SectionDescriptor.Data = {};
  SectionDescriptor.Comdat = {};
  NevercObjectSectionHandle Debug{};
  ASSERT_EQ(API.CreateSection(API.Context, Scope.task().handle(), Mutation,
                              &SectionDescriptor, &Debug)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.MoveSectionBefore(API.Context, Scope.task().handle(),
                                  Mutation, Debug, Text)
                .Code,
            NEVERC_STATUS_OK);

  Bytes.fill(0xff);
  EXPECT_EQ(Graph.sectionCount(), 0U);
  EXPECT_TRUE(Graph.hasLayoutProof());
  ASSERT_EQ(API.CommitMutation(API.Context, Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(Graph.sectionCount(), 2U);
  EXPECT_EQ(Graph.symbolCount(), 1U);
  EXPECT_EQ(Graph.relocationCount(), 1U);
  EXPECT_EQ(Graph.comdatCount(), 1U);
  EXPECT_FALSE(Graph.hasLayoutProof());
  EXPECT_EQ(API.GetLayoutProofInfo(API.Context, Scope.task().handle(),
                                   InitialProof, &ProofInfo)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  NevercObjectSectionInfo Stale{};
  Stale.Header = {sizeof(Stale), NEVERC_OBJECT_API_MAJOR,
                  NEVERC_OBJECT_API_MINOR, 0};
  EXPECT_EQ(API.GetSectionInfo(API.Context, Scope.task().handle(), Text,
                               &Stale)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  GraphHandle = Bridge.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle));
  NevercObjectSectionHandle First{};
  ASSERT_EQ(API.GetFirstSection(API.Context, Scope.task().handle(),
                                *GraphHandle, &First)
                .Code,
            NEVERC_STATUS_OK);
  NevercObjectSectionInfo FirstInfo{};
  FirstInfo.Header = {sizeof(FirstInfo), NEVERC_OBJECT_API_MAJOR,
                      NEVERC_OBJECT_API_MINOR, 0};
  ASSERT_EQ(API.GetSectionInfo(API.Context, Scope.task().handle(), First,
                               &FirstInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(FirstInfo.Name.Data, FirstInfo.Name.Length),
            ".debug");

  NevercObjectSectionHandle Second{};
  ASSERT_EQ(API.GetNextSection(API.Context, Scope.task().handle(), First,
                               &Second)
                .Code,
            NEVERC_STATUS_OK);
  NevercObjectSectionInfo SecondInfo{};
  SecondInfo.Header = {sizeof(SecondInfo), NEVERC_OBJECT_API_MAJOR,
                       NEVERC_OBJECT_API_MINOR, 0};
  ASSERT_EQ(API.GetSectionInfo(API.Context, Scope.task().handle(), Second,
                               &SecondInfo)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(SecondInfo.Data.Length, 4U);
  EXPECT_EQ(SecondInfo.Data.Data[0], 0x2a);
}

TEST(PluginObjectBuilderTest,
     RejectsDuplicateStrongDefinitionsAndRollsBack) {
  ObjectTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target));
  PluginObjectGraph Graph(std::move(*Target));
  Graph.issueLayoutProof();
  ObjectPluginBridge Bridge(Scope.task(), Graph);
  auto GraphHandle = Bridge.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle));
  const NevercObjectAPI &API = Bridge.api();
  NevercObjectMutationHandle Mutation{};
  ASSERT_EQ(API.BeginMutation(API.Context, Scope.task().handle(),
                              *GraphHandle, &Mutation)
                .Code,
            NEVERC_STATUS_OK);

  std::array<uint8_t, 4> Bytes{};
  NevercObjectSectionDescriptor SectionDescriptor{};
  SectionDescriptor.Header = {sizeof(SectionDescriptor),
                              NEVERC_OBJECT_API_MAJOR,
                              NEVERC_OBJECT_API_MINOR, 0};
  SectionDescriptor.Name = view(".text");
  SectionDescriptor.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  SectionDescriptor.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  SectionDescriptor.Alignment = 4;
  SectionDescriptor.Data = {Bytes.data(), Bytes.size()};
  NevercObjectSectionHandle Section{};
  ASSERT_EQ(API.CreateSection(API.Context, Scope.task().handle(), Mutation,
                              &SectionDescriptor, &Section)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectSymbolDescriptor SymbolDescriptor{};
  SymbolDescriptor.Header = {sizeof(SymbolDescriptor),
                             NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};
  SymbolDescriptor.Name = view("duplicate");
  SymbolDescriptor.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  SymbolDescriptor.Visibility =
      NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  SymbolDescriptor.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  SymbolDescriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  SymbolDescriptor.Section = Section;
  SymbolDescriptor.Size = Bytes.size();
  SymbolDescriptor.Alignment = 4;
  NevercObjectSymbolHandle First{};
  NevercObjectSymbolHandle Second{};
  ASSERT_EQ(API.CreateSymbol(API.Context, Scope.task().handle(), Mutation,
                             &SymbolDescriptor, &First)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CreateSymbol(API.Context, Scope.task().handle(), Mutation,
                             &SymbolDescriptor, &Second)
                .Code,
            NEVERC_STATUS_OK);

  EXPECT_EQ(API.CommitMutation(API.Context, Scope.task().handle(), Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(Graph.sectionCount(), 0U);
  EXPECT_EQ(Graph.symbolCount(), 0U);
  EXPECT_TRUE(Graph.hasLayoutProof());

  NevercObjectSymbolInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR,
                 NEVERC_OBJECT_API_MINOR, 0};
  EXPECT_EQ(API.GetSymbolInfo(API.Context, Scope.task().handle(), First,
                              &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

TEST(PluginObjectBuilderTest,
     RejectsDanglingRelocationAndPreservesCommittedGraph) {
  ObjectBuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercObjectAPI &API = State.api();
  NevercObjectMutationHandle Mutation = State.beginMutation();
  ASSERT_FALSE(neverc_handle_is_null(Mutation));

  std::array<uint8_t, 4> Bytes{};
  NevercObjectSectionDescriptor SectionDescriptor{};
  SectionDescriptor.Header = {sizeof(SectionDescriptor),
                              NEVERC_OBJECT_API_MAJOR,
                              NEVERC_OBJECT_API_MINOR, 0};
  SectionDescriptor.Name = view(".text");
  SectionDescriptor.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  SectionDescriptor.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  SectionDescriptor.Alignment = 4;
  SectionDescriptor.Data = {Bytes.data(), Bytes.size()};
  NevercObjectSectionHandle Section{};
  ASSERT_EQ(API.CreateSection(API.Context, State.Scope.task().handle(),
                              Mutation, &SectionDescriptor, &Section)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectSymbolDescriptor SymbolDescriptor{};
  SymbolDescriptor.Header = {sizeof(SymbolDescriptor),
                             NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};
  SymbolDescriptor.Name = view("target");
  SymbolDescriptor.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  SymbolDescriptor.Visibility =
      NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  SymbolDescriptor.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  SymbolDescriptor.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  SymbolDescriptor.Section = Section;
  SymbolDescriptor.Size = Bytes.size();
  SymbolDescriptor.Alignment = 4;
  NevercObjectSymbolHandle Symbol{};
  ASSERT_EQ(API.CreateSymbol(API.Context, State.Scope.task().handle(),
                             Mutation, &SymbolDescriptor, &Symbol)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectRelocationDescriptor RelocationDescriptor{};
  RelocationDescriptor.Header = {
      sizeof(RelocationDescriptor), NEVERC_OBJECT_API_MAJOR,
      NEVERC_OBJECT_API_MINOR, 0};
  RelocationDescriptor.Section = Section;
  RelocationDescriptor.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  RelocationDescriptor.TargetKind =
      NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  RelocationDescriptor.Width = 32;
  RelocationDescriptor.TargetSymbol = Symbol;
  NevercObjectRelocationHandle Relocation{};
  ASSERT_EQ(API.CreateRelocation(
                API.Context, State.Scope.task().handle(), Mutation,
                &RelocationDescriptor, &Relocation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(State.Graph->symbolCount(), 1U);
  ASSERT_EQ(State.Graph->relocationCount(), 1U);

  auto GraphHandle = State.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle));
  ASSERT_EQ(API.GetFirstSymbol(API.Context, State.Scope.task().handle(),
                               *GraphHandle, &Symbol)
                .Code,
            NEVERC_STATUS_OK);
  Mutation = State.beginMutation();
  ASSERT_FALSE(neverc_handle_is_null(Mutation));
  ASSERT_EQ(API.EraseSymbol(API.Context, State.Scope.task().handle(),
                            Mutation, Symbol)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(State.Graph->symbolCount(), 1U);
  EXPECT_EQ(State.Graph->relocationCount(), 1U);
  EXPECT_NE(dumpPluginObjectGraph(*State.Graph).find("target"),
            std::string::npos);
}

TEST(PluginObjectBuilderTest,
     ReplacesByDeepCopyAndAbandonRestoresCommittedGraph) {
  ObjectBuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercObjectAPI &API = State.api();
  NevercObjectMutationHandle Mutation = State.beginMutation();

  std::array<uint8_t, 2> OriginalBytes = {1, 2};
  NevercObjectSectionDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                       NEVERC_OBJECT_API_MINOR, 0};
  Descriptor.Name = view(".old");
  Descriptor.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Descriptor.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Descriptor.Alignment = 2;
  Descriptor.Data = {OriginalBytes.data(), OriginalBytes.size()};
  NevercObjectSectionHandle Section{};
  ASSERT_EQ(API.CreateSection(API.Context, State.Scope.task().handle(),
                              Mutation, &Descriptor, &Section)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);
  const std::string Before = dumpPluginObjectGraph(*State.Graph);

  auto GraphHandle = State.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle));
  ASSERT_EQ(API.GetFirstSection(API.Context, State.Scope.task().handle(),
                                *GraphHandle, &Section)
                .Code,
            NEVERC_STATUS_OK);
  Mutation = State.beginMutation();
  std::array<uint8_t, 3> ReplacementBytes = {3, 4, 5};
  Descriptor.Name = view(".new");
  Descriptor.Alignment = 1;
  Descriptor.Data = {ReplacementBytes.data(), ReplacementBytes.size()};
  ASSERT_EQ(API.ReplaceSection(API.Context, State.Scope.task().handle(),
                               Mutation, Section, &Descriptor)
                .Code,
            NEVERC_STATUS_OK);
  ReplacementBytes.fill(0xff);

  NevercObjectSectionInfo CandidateInfo{};
  CandidateInfo.Header = {sizeof(CandidateInfo), NEVERC_OBJECT_API_MAJOR,
                          NEVERC_OBJECT_API_MINOR, 0};
  ASSERT_EQ(API.GetSectionInfo(API.Context, State.Scope.task().handle(),
                               Section, &CandidateInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(CandidateInfo.Name.Data,
                        CandidateInfo.Name.Length),
            ".new");
  ASSERT_EQ(CandidateInfo.Data.Length, 3U);
  EXPECT_EQ(CandidateInfo.Data.Data[0], 3U);

  ASSERT_EQ(API.AbandonMutation(API.Context, State.Scope.task().handle(),
                                Mutation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(dumpPluginObjectGraph(*State.Graph), Before);

  GraphHandle = State.graph();
  ASSERT_TRUE(static_cast<bool>(GraphHandle));
  ASSERT_EQ(API.GetFirstSection(API.Context, State.Scope.task().handle(),
                                *GraphHandle, &Section)
                .Code,
            NEVERC_STATUS_OK);
  Mutation = State.beginMutation();
  ReplacementBytes = {3, 4, 5};
  Descriptor.Data = {ReplacementBytes.data(), ReplacementBytes.size()};
  ASSERT_EQ(API.ReplaceSection(API.Context, State.Scope.task().handle(),
                               Mutation, Section, &Descriptor)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(dumpPluginObjectGraph(*State.Graph).find(".new"),
            std::string::npos);
  EXPECT_EQ(State.Graph->sections().front().Data[0], 3U);
}

TEST(PluginObjectBuilderTest,
     RejectsInvalidSectionAndComdatMetadata) {
  ObjectBuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercObjectAPI &API = State.api();

  const auto ExpectRejectedSection =
      [&](NevercObjectSectionDescriptor Descriptor) {
        NevercObjectMutationHandle Mutation = State.beginMutation();
        NevercObjectSectionHandle Section{};
        ASSERT_EQ(API.CreateSection(
                      API.Context, State.Scope.task().handle(), Mutation,
                      &Descriptor, &Section)
                      .Code,
                  NEVERC_STATUS_OK);
        EXPECT_EQ(API.CommitMutation(API.Context,
                                     State.Scope.task().handle(), Mutation)
                      .Code,
                  NEVERC_STATUS_VERIFICATION_FAILED);
        EXPECT_EQ(State.Graph->sectionCount(), 0U);
      };

  NevercObjectSectionDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                       NEVERC_OBJECT_API_MINOR, 0};
  Descriptor.Name = view(".bad-align");
  Descriptor.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Descriptor.Alignment = 3;
  ExpectRejectedSection(Descriptor);

  Descriptor.Name = view(".tdata");
  Descriptor.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
  Descriptor.Alignment = 8;
  Descriptor.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  ExpectRejectedSection(Descriptor);

  Descriptor.Name = view(".format");
  Descriptor.Kind = NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION;
  Descriptor.Alignment = 1;
  Descriptor.Flags = 0;
  ExpectRejectedSection(Descriptor);

  std::array<uint8_t, 1> Extension = {1};
  Descriptor.Name = view(".foreign");
  Descriptor.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Descriptor.ExtensionOwner = {
      UINT64_C(0x1111111111111111), UINT64_C(0x2222222222222222)};
  Descriptor.ExtensionVersion = 1;
  Descriptor.Extension = {Extension.data(), Extension.size()};
  ExpectRejectedSection(Descriptor);

  NevercObjectMutationHandle Mutation = State.beginMutation();
  NevercObjectComdatDescriptor ComdatDescriptor{};
  ComdatDescriptor.Header = {sizeof(ComdatDescriptor),
                             NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};
  ComdatDescriptor.Name = view("self");
  ComdatDescriptor.Selection = NEVERC_OBJECT_COMDAT_ANY;
  NevercObjectComdatHandle Comdat{};
  ASSERT_EQ(API.CreateComdat(API.Context, State.Scope.task().handle(),
                             Mutation, &ComdatDescriptor, &Comdat)
                .Code,
            NEVERC_STATUS_OK);
  ComdatDescriptor.Selection = NEVERC_OBJECT_COMDAT_ASSOCIATIVE;
  ComdatDescriptor.AssociatedComdat = Comdat;
  ASSERT_EQ(API.ReplaceComdat(API.Context, State.Scope.task().handle(),
                              Mutation, Comdat, &ComdatDescriptor)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(State.Graph->comdatCount(), 0U);
}

TEST(PluginObjectBuilderTest, RejectsOutOfBoundsRelocation) {
  ObjectBuilderContext State;
  ASSERT_TRUE(State.initialize());
  const NevercObjectAPI &API = State.api();
  NevercObjectMutationHandle Mutation = State.beginMutation();

  std::array<uint8_t, 4> Bytes{};
  NevercObjectSectionDescriptor SectionDescriptor{};
  SectionDescriptor.Header = {sizeof(SectionDescriptor),
                              NEVERC_OBJECT_API_MAJOR,
                              NEVERC_OBJECT_API_MINOR, 0};
  SectionDescriptor.Name = view(".data");
  SectionDescriptor.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  SectionDescriptor.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  SectionDescriptor.Alignment = 4;
  SectionDescriptor.Data = {Bytes.data(), Bytes.size()};
  NevercObjectSectionHandle Section{};
  ASSERT_EQ(API.CreateSection(API.Context, State.Scope.task().handle(),
                              Mutation, &SectionDescriptor, &Section)
                .Code,
            NEVERC_STATUS_OK);

  NevercObjectRelocationDescriptor RelocationDescriptor{};
  RelocationDescriptor.Header = {
      sizeof(RelocationDescriptor), NEVERC_OBJECT_API_MAJOR,
      NEVERC_OBJECT_API_MINOR, 0};
  RelocationDescriptor.Section = Section;
  RelocationDescriptor.Offset = Bytes.size();
  RelocationDescriptor.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  RelocationDescriptor.TargetKind =
      NEVERC_OBJECT_RELOCATION_TARGET_ABSOLUTE;
  RelocationDescriptor.Width = 8;
  RelocationDescriptor.TargetValue = 42;
  NevercObjectRelocationHandle Relocation{};
  ASSERT_EQ(API.CreateRelocation(
                API.Context, State.Scope.task().handle(), Mutation,
                &RelocationDescriptor, &Relocation)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.CommitMutation(API.Context, State.Scope.task().handle(),
                               Mutation)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(State.Graph->sectionCount(), 0U);
  EXPECT_EQ(State.Graph->relocationCount(), 0U);
}

} // namespace
