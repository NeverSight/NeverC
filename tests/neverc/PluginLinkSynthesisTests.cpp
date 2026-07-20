#include "PluginLinkTestSupport.h"
#include "Inputs/Plugin/SyntheticLinkPlugin.h"
#include "Link/LinkPhaseExecutor.h"
#include "Link/RelaxationExecutor.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "gtest/gtest.h"

using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

struct SynthesisGraphIDs {
  uint64_t Atom = 0;
  uint64_t Synthetic = 0;
};

std::shared_ptr<PluginLinkGraph>
makeSynthesisGraph(SynthesisGraphIDs &IDs) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_ICF_COMPLETE);
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///synthetic.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;
  PluginLinkSection Section;
  Section.Name = ".synthetic";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                  NEVERC_OBJECT_SECTION_WRITABLE;
  Section.Alignment = 8;
  Section.Size = 8;
  Section.Origin.InputID = InputID;
  const uint64_t SectionID =
      Graph->addSection(std::move(Section)).ID;
  PluginLinkAtom Atom;
  Atom.SectionID = SectionID;
  Atom.Name = "__neverc_got";
  Atom.Flags = NEVERC_LINK_ATOM_LIVE;
  Atom.Alignment = 8;
  Atom.Content.assign(8, 0);
  Atom.Origin.InputID = InputID;
  IDs.Atom = Graph->addAtom(std::move(Atom)).ID;
  PluginLinkSynthetic Synthetic;
  Synthetic.Role = "got";
  Synthetic.SectionID = SectionID;
  Synthetic.AtomID = IDs.Atom;
  Synthetic.Origin.InputID = InputID;
  IDs.Synthetic =
      Graph->addSynthetic(std::move(Synthetic)).ID;
  return Graph;
}

struct RelaxationGraphIDs {
  uint64_t NearEdge = 0;
  uint64_t FarEdge = 0;
  uint64_t FarTarget = 0;
};

std::shared_ptr<PluginLinkGraph>
makeRelaxationGraph(RelaxationGraphIDs &IDs) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_SYNTHETICS_READY);
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///relax.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;
  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Size = 312;
  Section.Origin.InputID = InputID;
  const uint64_t SectionID =
      Graph->addSection(std::move(Section)).ID;
  auto AddAtom = [&](llvm::StringRef Name, size_t Size) {
    PluginLinkAtom Atom;
    Atom.SectionID = SectionID;
    Atom.Name = Name.str();
    Atom.Flags = NEVERC_LINK_ATOM_LIVE;
    Atom.Alignment = 1;
    Atom.Content.assign(Size, 0x90);
    Atom.Origin.InputID = InputID;
    return Graph->addAtom(std::move(Atom)).ID;
  };
  const uint64_t Source = AddAtom("source", 4);
  const uint64_t NearTarget = AddAtom("near", 4);
  AddAtom("padding", 300);
  IDs.FarTarget = AddAtom("far", 4);

  PluginLinkEdge Near;
  Near.SourceAtomID = Source;
  Near.TargetAtomID = NearTarget;
  Near.Offset = 0;
  Near.Width = 32;
  Near.IsPCRelative = true;
  Near.IsSigned = true;
  Near.Origin.InputID = InputID;
  IDs.NearEdge = Graph->addEdge(std::move(Near)).ID;

  PluginLinkEdge Far;
  Far.SourceAtomID = Source;
  Far.TargetAtomID = IDs.FarTarget;
  Far.Offset = 1;
  Far.Width = 8;
  Far.IsPCRelative = true;
  Far.IsSigned = true;
  Far.Origin.InputID = InputID;
  IDs.FarEdge = Graph->addEdge(std::move(Far)).ID;
  return Graph;
}

TEST(PluginLinkSynthesisTest,
     BuiltinProviderMarksMaterializedSyntheticAtoms) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  SynthesisGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeSynthesisGraph(IDs),
      NEVERC_LINK_STATE_SYNTHETICS_READY);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_NE((*Output)->findAtom(IDs.Atom)->Flags &
                NEVERC_LINK_ATOM_SYNTHETIC,
            0U);
  EXPECT_NE((*Output)->findAtom(IDs.Atom)->Flags &
                NEVERC_LINK_ATOM_LIVE,
            0U);
}

TEST(PluginLinkSynthesisTest,
     RelaxationConvergesAfterShrinkingNearBranchesAndAddingFarThunks) {
  RelaxationGraphIDs DirectIDs;
  auto DirectGraph = makeRelaxationGraph(DirectIDs);
  auto Direct = executeLinkRelaxation(*DirectGraph);
  ASSERT_TRUE(static_cast<bool>(Direct))
      << errorText(Direct.takeError());
  EXPECT_GE(Direct->RoundCount, 2U);

  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  RelaxationGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeRelaxationGraph(IDs),
      NEVERC_LINK_STATE_THUNKS_RELAXED);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ((*Output)->findEdge(IDs.NearEdge)->Width, 8U);
  EXPECT_NE((*Output)->findEdge(IDs.FarEdge)->TargetAtomID,
            IDs.FarTarget);
  EXPECT_TRUE(llvm::any_of(
      (*Output)->synthetics(),
      [](const PluginLinkSynthetic &Synthetic) {
        return Synthetic.Role == "thunk";
      }));
}

TEST(PluginLinkSynthesisTest,
     PureCInterceptorCanRegisterSyntheticMetadata) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestSyntheticLinkTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_SYNTHESIZE_HIGH,
                       NEVERC_PHASE_LINK_SYNTHESIZE_LOW};
  Interceptor.Callback = neverc_test_synthetic_link_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  SynthesisGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeSynthesisGraph(IDs),
      NEVERC_LINK_STATE_SYNTHETICS_READY);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_TRUE(llvm::any_of(
      (*Output)->synthetics(),
      [](const PluginLinkSynthetic &Synthetic) {
        return Synthetic.Role == "plugin-metadata";
      }));
}

TEST(PluginLinkSynthesisTest,
     VerifierRejectsPluginSyntheticWithEmptyRole) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestSyntheticLinkTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.MakeInvalid = NEVERC_TRUE;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_SYNTHESIZE_HIGH,
                       NEVERC_PHASE_LINK_SYNTHESIZE_LOW};
  Interceptor.Callback = neverc_test_synthetic_link_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  SynthesisGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeSynthesisGraph(IDs),
      NEVERC_LINK_STATE_SYNTHETICS_READY);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
  EXPECT_EQ(Trace.MutationStatus,
            NEVERC_STATUS_VERIFICATION_FAILED);
}

TEST(PluginLinkSynthesisTest,
     RelaxationReportsNonConvergenceAtTheConfiguredLimit) {
  RelaxationGraphIDs IDs;
  auto Graph = makeRelaxationGraph(IDs);
  auto Result = executeLinkRelaxation(*Graph, 1);
  EXPECT_FALSE(static_cast<bool>(Result));
  if (!Result)
    llvm::consumeError(Result.takeError());
}

} // namespace
