#include "PluginLinkTestSupport.h"
#include "Inputs/Plugin/SymbolResolutionPlugin.h"
#include "Link/LinkPhaseExecutor.h"
#include "Link/ResolutionVerifier.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "gtest/gtest.h"

using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

std::shared_ptr<PluginLinkGraph> makeConflictingGraph(
    uint64_t &WeakSymbolID, uint64_t &StrongSymbolID,
    uint64_t &EdgeID) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_LTO_GENERATED);

  PluginLinkInput WeakInput;
  WeakInput.Kind = NEVERC_LINK_INPUT_OBJECT;
  WeakInput.Ordinal = 1;
  WeakInput.LogicalURI = "vfs:///weak.o";
  const uint64_t WeakInputID =
      Graph->addInput(std::move(WeakInput)).ID;

  PluginLinkInput StrongInput;
  StrongInput.Kind = NEVERC_LINK_INPUT_OBJECT;
  StrongInput.Ordinal = 2;
  StrongInput.LogicalURI = "vfs:///strong.o";
  const uint64_t StrongInputID =
      Graph->addInput(std::move(StrongInput)).ID;

  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 16;
  Section.Size = 16;
  Section.Origin.InputID = WeakInputID;
  const uint64_t SectionID =
      Graph->addSection(std::move(Section)).ID;

  PluginLinkAtom WeakAtom;
  WeakAtom.SectionID = SectionID;
  WeakAtom.Name = "weak";
  WeakAtom.Alignment = 1;
  WeakAtom.Content.assign(8, 0);
  WeakAtom.Origin.InputID = WeakInputID;
  const uint64_t WeakAtomID =
      Graph->addAtom(std::move(WeakAtom)).ID;

  PluginLinkAtom StrongAtom;
  StrongAtom.SectionID = SectionID;
  StrongAtom.Name = "strong";
  StrongAtom.Alignment = 1;
  StrongAtom.Content.assign(8, 0);
  StrongAtom.Origin.InputID = StrongInputID;
  const uint64_t StrongAtomID =
      Graph->addAtom(std::move(StrongAtom)).ID;

  PluginLinkSymbol Weak;
  Weak.Name = "selected";
  Weak.Binding = NEVERC_LINK_SYMBOL_BINDING_WEAK;
  Weak.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Weak.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Weak.AtomID = WeakAtomID;
  Weak.IsPrevailing = true;
  Weak.Origin.InputID = WeakInputID;
  WeakSymbolID = Graph->addSymbol(std::move(Weak)).ID;

  PluginLinkSymbol Strong;
  Strong.Name = "selected";
  Strong.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Strong.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Strong.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Strong.AtomID = StrongAtomID;
  Strong.Origin.InputID = StrongInputID;
  StrongSymbolID = Graph->addSymbol(std::move(Strong)).ID;

  PluginLinkEdge Edge;
  Edge.SourceAtomID = WeakAtomID;
  Edge.Offset = 0;
  Edge.Width = 32;
  Edge.TargetSymbolID = WeakSymbolID;
  Edge.Origin.InputID = WeakInputID;
  EdgeID = Graph->addEdge(std::move(Edge)).ID;
  return Graph;
}

std::shared_ptr<PluginLinkGraph> makeComdatGraph(
    uint64_t &SmallComdatID, uint64_t &LargeComdatID,
    uint64_t &SmallSymbolID, uint64_t &LargeSymbolID) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_SYMBOLS_RESOLVED);

  PluginLinkInput FirstInput;
  FirstInput.Kind = NEVERC_LINK_INPUT_OBJECT;
  FirstInput.Ordinal = 1;
  FirstInput.LogicalURI = "vfs:///small.o";
  const uint64_t FirstInputID =
      Graph->addInput(std::move(FirstInput)).ID;
  PluginLinkInput SecondInput;
  SecondInput.Kind = NEVERC_LINK_INPUT_OBJECT;
  SecondInput.Ordinal = 2;
  SecondInput.LogicalURI = "vfs:///large.o";
  const uint64_t SecondInputID =
      Graph->addInput(std::move(SecondInput)).ID;

  PluginLinkSection Section;
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Size = 12;
  Section.Origin.InputID = FirstInputID;
  const uint64_t SectionID =
      Graph->addSection(std::move(Section)).ID;

  PluginLinkComdat SmallComdat;
  SmallComdat.Name = "choice";
  SmallComdat.Selection = NEVERC_LINK_COMDAT_LARGEST;
  SmallComdat.Origin.InputID = FirstInputID;
  SmallComdatID =
      Graph->addComdat(std::move(SmallComdat)).ID;
  PluginLinkComdat LargeComdat;
  LargeComdat.Name = "choice";
  LargeComdat.Selection = NEVERC_LINK_COMDAT_LARGEST;
  LargeComdat.Origin.InputID = SecondInputID;
  LargeComdatID =
      Graph->addComdat(std::move(LargeComdat)).ID;

  PluginLinkAtom SmallAtom;
  SmallAtom.SectionID = SectionID;
  SmallAtom.ComdatID = SmallComdatID;
  SmallAtom.Name = "small";
  SmallAtom.Alignment = 1;
  SmallAtom.Content.assign(4, 0);
  SmallAtom.Origin.InputID = FirstInputID;
  const uint64_t SmallAtomID =
      Graph->addAtom(std::move(SmallAtom)).ID;
  PluginLinkAtom LargeAtom;
  LargeAtom.SectionID = SectionID;
  LargeAtom.ComdatID = LargeComdatID;
  LargeAtom.Name = "large";
  LargeAtom.Alignment = 1;
  LargeAtom.Content.assign(8, 0);
  LargeAtom.Origin.InputID = SecondInputID;
  const uint64_t LargeAtomID =
      Graph->addAtom(std::move(LargeAtom)).ID;

  PluginLinkSymbol Small;
  Small.Name = "coalesced";
  Small.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Small.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Small.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Small.AtomID = SmallAtomID;
  Small.IsPrevailing = true;
  Small.Origin.InputID = FirstInputID;
  SmallSymbolID = Graph->addSymbol(std::move(Small)).ID;
  PluginLinkSymbol Large;
  Large.Name = "coalesced";
  Large.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Large.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Large.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Large.AtomID = LargeAtomID;
  Large.Origin.InputID = SecondInputID;
  LargeSymbolID = Graph->addSymbol(std::move(Large)).ID;
  return Graph;
}

TEST(PluginSymbolResolutionTest,
     BuiltinTypedPhaseSelectsStrongDefinitionAndRetargetsEdges) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  uint64_t WeakSymbolID = 0;
  uint64_t StrongSymbolID = 0;
  uint64_t EdgeID = 0;
  auto Graph =
      makeConflictingGraph(WeakSymbolID, StrongSymbolID, EdgeID);
  ASSERT_NE(Graph, nullptr);

  auto Output = (*Pipeline)->execute(
      Graph, NEVERC_LINK_STATE_SYMBOLS_RESOLVED);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_FALSE((*Output)->findSymbol(WeakSymbolID)->IsPrevailing);
  EXPECT_TRUE((*Output)->findSymbol(StrongSymbolID)->IsPrevailing);
  EXPECT_EQ((*Output)->findEdge(EdgeID)->TargetSymbolID,
            StrongSymbolID);
}

TEST(PluginSymbolResolutionTest,
     PureCInterceptorCanOverrideTheVerifiedResolutionOutcome) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  NevercTestSymbolResolutionTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {
      NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_HIGH,
      NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_LOW};
  Interceptor.Callback =
      neverc_test_symbol_resolution_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  uint64_t WeakSymbolID = 0;
  uint64_t StrongSymbolID = 0;
  uint64_t EdgeID = 0;
  auto Graph =
      makeConflictingGraph(WeakSymbolID, StrongSymbolID, EdgeID);
  ASSERT_NE(Graph, nullptr);
  auto Output = (*Pipeline)->execute(
      Graph, NEVERC_LINK_STATE_SYMBOLS_RESOLVED);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());

  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_TRUE((*Output)->findSymbol(WeakSymbolID)->IsPrevailing);
  EXPECT_FALSE((*Output)->findSymbol(StrongSymbolID)->IsPrevailing);
  EXPECT_EQ((*Output)->findEdge(EdgeID)->TargetSymbolID,
            WeakSymbolID);
}

TEST(PluginSymbolResolutionTest,
     VerifierRejectsPluginWithMultiplePrevailingDefinitions) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  NevercTestSymbolResolutionTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.MakeInvalid = NEVERC_TRUE;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {
      NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_HIGH,
      NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_LOW};
  Interceptor.Callback =
      neverc_test_symbol_resolution_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  uint64_t WeakSymbolID = 0;
  uint64_t StrongSymbolID = 0;
  uint64_t EdgeID = 0;
  auto Output = (*Pipeline)->execute(
      makeConflictingGraph(WeakSymbolID, StrongSymbolID, EdgeID),
      NEVERC_LINK_STATE_SYMBOLS_RESOLVED);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
  EXPECT_EQ(Trace.Mutations, 0U);
  EXPECT_EQ(Trace.MutationStatus,
            NEVERC_STATUS_VERIFICATION_FAILED);
}

TEST(PluginSymbolResolutionTest,
     ComdatTypedPhaseAppliesLargestSelectionRule) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  uint64_t SmallComdatID = 0;
  uint64_t LargeComdatID = 0;
  uint64_t SmallSymbolID = 0;
  uint64_t LargeSymbolID = 0;
  auto Output = (*Pipeline)->execute(
      makeComdatGraph(SmallComdatID, LargeComdatID,
                      SmallSymbolID, LargeSymbolID),
      NEVERC_LINK_STATE_COMDAT_SELECTED);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ((*Output)->findComdat(SmallComdatID)->SelectedID,
            LargeComdatID);
  EXPECT_EQ((*Output)->findComdat(LargeComdatID)->SelectedID,
            LargeComdatID);
  EXPECT_FALSE((*Output)->findSymbol(SmallSymbolID)->IsPrevailing);
  EXPECT_TRUE((*Output)->findSymbol(LargeSymbolID)->IsPrevailing);
}

// COFF projects every same-named COMDAT candidate onto a single group ID and
// emits one file-local `$unwind$f` label per object, so a well-formed selection
// legitimately owns several identically named local definitions.
std::shared_ptr<PluginLinkGraph> makeSharedComdatGraph() {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_COMDAT_SELECTED);

  PluginLinkSection Section;
  Section.Name = ".xdata";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_UNWIND;
  Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  Section.Alignment = 4;
  Section.Size = 8;
  const uint64_t SectionID = Graph->addSection(std::move(Section)).ID;

  PluginLinkComdat Comdat;
  Comdat.Name = "unwind_owner";
  Comdat.Selection = NEVERC_LINK_COMDAT_ANY;
  PluginLinkComdat &Stored = Graph->addComdat(std::move(Comdat));
  Stored.SelectedID = Stored.ID;
  const uint64_t ComdatID = Stored.ID;

  for (const char *URI : {"vfs:///first.o", "vfs:///second.o"}) {
    PluginLinkInput Input;
    Input.Kind = NEVERC_LINK_INPUT_OBJECT;
    Input.LogicalURI = URI;
    const uint64_t InputID = Graph->addInput(std::move(Input)).ID;

    PluginLinkAtom Atom;
    Atom.SectionID = SectionID;
    Atom.ComdatID = ComdatID;
    Atom.Name = ".xdata";
    Atom.Alignment = 4;
    Atom.Content.assign(4, 0);
    Atom.Origin.InputID = InputID;
    const uint64_t AtomID = Graph->addAtom(std::move(Atom)).ID;

    PluginLinkSymbol Symbol;
    Symbol.Name = "$unwind$f";
    Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_LOCAL;
    Symbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
    Symbol.AtomID = AtomID;
    Symbol.IsPrevailing = true;
    Symbol.Origin.InputID = InputID;
    Graph->addSymbol(std::move(Symbol));
  }
  return Graph;
}

TEST(PluginSymbolResolutionTest,
     ComdatSelectionAcceptsRepeatedFileLocalUnwindLabels) {
  auto Graph = makeSharedComdatGraph();
  ASSERT_TRUE(static_cast<bool>(Graph));
  EXPECT_EQ(errorText(verifyLinkComdatSelection(*Graph)), "");
}

} // namespace
