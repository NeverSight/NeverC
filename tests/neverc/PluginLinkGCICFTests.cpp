#include "PluginLinkTestSupport.h"
#include "Inputs/Plugin/GCICFPlugin.h"
#include "Inputs/Plugin/LinkPhaseTracePlugin.h"
#include "Link/ICFProvider.h"
#include "Link/LinkPhaseExecutor.h"
#include "Link/SectionGCProvider.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "gtest/gtest.h"

using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

struct GCGraphIDs {
  uint64_t RootAtom = 0;
  uint64_t ReachableAtom = 0;
  uint64_t DeadAtom = 0;
};

std::shared_ptr<PluginLinkGraph> makeGCGraph(GCGraphIDs &IDs) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_COMDAT_SELECTED);

  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///gc.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;

  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Size = 12;
  Section.Origin.InputID = InputID;
  const uint64_t SectionID =
      Graph->addSection(std::move(Section)).ID;

  auto AddAtom = [&](llvm::StringRef Name) {
    PluginLinkAtom Atom;
    Atom.SectionID = SectionID;
    Atom.Name = Name.str();
    Atom.Alignment = 1;
    Atom.Content.assign(4, 0);
    Atom.Origin.InputID = InputID;
    return Graph->addAtom(std::move(Atom)).ID;
  };
  IDs.RootAtom = AddAtom("root");
  IDs.ReachableAtom = AddAtom("reachable");
  IDs.DeadAtom = AddAtom("dead");

  PluginLinkSymbol Root;
  Root.Name = "entry";
  Root.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Root.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Root.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Root.AtomID = IDs.RootAtom;
  Root.IsPrevailing = true;
  Root.IsRoot = true;
  Root.Origin.InputID = InputID;
  Graph->addSymbol(std::move(Root));

  PluginLinkEdge Edge;
  Edge.Kind = NEVERC_LINK_EDGE_RELOCATION;
  Edge.SourceAtomID = IDs.RootAtom;
  Edge.TargetAtomID = IDs.ReachableAtom;
  Edge.Width = 32;
  Edge.Origin.InputID = InputID;
  Graph->addEdge(std::move(Edge));
  return Graph;
}

struct ICFGraphIDs {
  uint64_t Leader = 0;
  uint64_t Candidate = 0;
  uint64_t AddressSignificant = 0;
  uint64_t Different = 0;
};

std::shared_ptr<PluginLinkGraph> makeICFGraph(ICFGraphIDs &IDs) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_GC_COMPLETE);
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///icf.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;
  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Size = 8;
  Section.Origin.InputID = InputID;
  const uint64_t SectionID =
      Graph->addSection(std::move(Section)).ID;
  auto AddAtom = [&](llvm::StringRef Name,
                     std::vector<uint8_t> Content,
                     NevercLinkAtomFlags Flags) {
    PluginLinkAtom Atom;
    Atom.SectionID = SectionID;
    Atom.Name = Name.str();
    Atom.Flags = Flags | NEVERC_LINK_ATOM_LIVE;
    Atom.Alignment = 1;
    Atom.Content = std::move(Content);
    Atom.Origin.InputID = InputID;
    return Graph->addAtom(std::move(Atom)).ID;
  };
  IDs.Leader = AddAtom("leader", {0x90, 0xc3}, 0);
  IDs.Candidate = AddAtom("candidate", {0x90, 0xc3}, 0);
  IDs.AddressSignificant =
      AddAtom("address-significant", {0x90, 0xc3},
              NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT);
  IDs.Different = AddAtom("different", {0xcc, 0xc3}, 0);
  return Graph;
}

TEST(PluginLinkGCICFTest,
     BuiltinGCMarksRootsAndReachableAtomsButDropsDeadAtoms) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  GCGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeGCGraph(IDs), NEVERC_LINK_STATE_GC_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_NE((*Output)->findAtom(IDs.RootAtom)->Flags &
                NEVERC_LINK_ATOM_LIVE,
            0U);
  EXPECT_NE((*Output)->findAtom(IDs.ReachableAtom)->Flags &
                NEVERC_LINK_ATOM_LIVE,
            0U);
  EXPECT_EQ((*Output)->findAtom(IDs.DeadAtom)->Flags &
                NEVERC_LINK_ATOM_LIVE,
            0U);
}

TEST(PluginLinkGCICFTest,
     BuiltinICFFoldsEquivalentEligibleAtomsOnly) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  ICFGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeICFGraph(IDs), NEVERC_LINK_STATE_ICF_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ((*Output)->findAtom(IDs.Candidate)->FoldLeaderID,
            IDs.Leader);
  EXPECT_NE((*Output)->findAtom(IDs.Candidate)->Flags &
                NEVERC_LINK_ATOM_FOLDED,
            0U);
  EXPECT_EQ(
      (*Output)->findAtom(IDs.AddressSignificant)->FoldLeaderID, 0U);
  EXPECT_EQ((*Output)->findAtom(IDs.Different)->FoldLeaderID, 0U);
}

TEST(PluginLinkGCICFTest, PureCInterceptorCanKeepAnAdditionalAtomLive) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestGCICFTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.Operation = NEVERC_TEST_GC_KEEP_DEAD;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_GC_HIGH,
                       NEVERC_PHASE_LINK_GC_LOW};
  Interceptor.Callback = neverc_test_gc_icf_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  GCGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeGCGraph(IDs), NEVERC_LINK_STATE_GC_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_NE((*Output)->findAtom(IDs.DeadAtom)->Flags &
                NEVERC_LINK_ATOM_LIVE,
            0U);
}

TEST(PluginLinkGCICFTest, VerifierRejectsPluginThatDropsARequiredRoot) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestGCICFTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.Operation = NEVERC_TEST_GC_DROP_ROOT;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_GC_HIGH,
                       NEVERC_PHASE_LINK_GC_LOW};
  Interceptor.Callback = neverc_test_gc_icf_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  GCGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeGCGraph(IDs), NEVERC_LINK_STATE_GC_COMPLETE);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
  EXPECT_EQ(Trace.MutationStatus,
            NEVERC_STATUS_VERIFICATION_FAILED);
}

TEST(PluginLinkGCICFTest, PureCInterceptorCanPreventAnEligibleFold) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestGCICFTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.Operation = NEVERC_TEST_ICF_PREVENT_FOLD;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_ICF_HIGH,
                       NEVERC_PHASE_LINK_ICF_LOW};
  Interceptor.Callback = neverc_test_gc_icf_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  ICFGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeICFGraph(IDs), NEVERC_LINK_STATE_ICF_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_EQ((*Output)->findAtom(IDs.Candidate)->FoldLeaderID, 0U);
  EXPECT_EQ((*Output)->findAtom(IDs.Candidate)->Flags &
                NEVERC_LINK_ATOM_FOLDED,
            0U);
}

TEST(PluginLinkGCICFTest,
     VerifierRejectsFoldingAnAddressSignificantAtom) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestGCICFTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.Operation = NEVERC_TEST_ICF_INVALID_FOLD;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_ICF_HIGH,
                       NEVERC_PHASE_LINK_ICF_LOW};
  Interceptor.Callback = neverc_test_gc_icf_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  ICFGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeICFGraph(IDs), NEVERC_LINK_STATE_ICF_COMPLETE);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
  EXPECT_EQ(Trace.MutationStatus,
            NEVERC_STATUS_VERIFICATION_FAILED);
}

TEST(PluginLinkGCICFTest, ProvidersRecordKeepAndFoldProvenance) {
  GCGraphIDs GCIDs;
  auto GCGraph = makeGCGraph(GCIDs);
  auto Liveness = markLiveLinkAtoms(*GCGraph);
  ASSERT_TRUE(static_cast<bool>(Liveness))
      << errorText(Liveness.takeError());
  auto Reachable = llvm::find_if(
      *Liveness, [&](const LinkLivenessRecord &Record) {
        return Record.AtomID == GCIDs.ReachableAtom;
      });
  ASSERT_NE(Reachable, Liveness->end());
  EXPECT_NE(llvm::find(Reachable->KeepReasons, "relocation"),
            Reachable->KeepReasons.end());

  ICFGraphIDs ICFIDs;
  auto ICFGraph = makeICFGraph(ICFIDs);
  auto Folding = foldIdenticalLinkAtoms(*ICFGraph);
  ASSERT_TRUE(static_cast<bool>(Folding))
      << errorText(Folding.takeError());
  auto Candidate = llvm::find_if(
      *Folding, [&](const LinkFoldRecord &Record) {
        return Record.AtomID == ICFIDs.Candidate;
      });
  ASSERT_NE(Candidate, Folding->end());
  EXPECT_EQ(Candidate->LeaderID, ICFIDs.Leader);
  EXPECT_EQ(Candidate->Reason, "equivalent");
}

TEST(PluginLinkGCICFTest,
     ReplacementProviderCannotBypassTheLivenessVerifier) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestLinkPhaseTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercProviderDescriptor Provider{};
  Provider.Header = {sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase = {NEVERC_PHASE_LINK_GC_HIGH,
                    NEVERC_PHASE_LINK_GC_LOW};
  Provider.ProviderID = {"invalid-gc", 10};
  Provider.Route.Header = {sizeof(Provider.Route),
                           NEVERC_PLUGIN_ABI_MAJOR,
                           NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = neverc_test_link_provider;
  Provider.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addProvider(LinkTestPluginID, Provider));
  ASSERT_FALSE((*Pipeline)->selectProvider(Provider.Phase,
                                           LinkTestPluginID));

  GCGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeGCGraph(IDs), NEVERC_LINK_STATE_GC_COMPLETE);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
}

} // namespace
