#include "PluginLinkTestSupport.h"
#include "Inputs/Plugin/LinkLayoutPlugin.h"
#include "Link/LinkPhaseExecutor.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "gtest/gtest.h"

#include <algorithm>

using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

struct LayoutGraphIDs {
  uint64_t TextSection = 0;
  uint64_t DataSection = 0;
  uint64_t EntryAtom = 0;
  uint64_t DataAtom = 0;
};

std::shared_ptr<PluginLinkGraph>
makeLayoutGraph(LayoutGraphIDs &IDs) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_THUNKS_RELAXED);
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///layout.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;

  PluginLinkSection Text;
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Alignment = 16;
  Text.Size = 8;
  Text.Origin.InputID = InputID;
  IDs.TextSection = Graph->addSection(std::move(Text)).ID;
  PluginLinkSection Data;
  Data.Name = ".data";
  Data.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Data.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Data.Alignment = 8;
  Data.Size = 16;
  Data.Origin.InputID = InputID;
  IDs.DataSection = Graph->addSection(std::move(Data)).ID;

  PluginLinkAtom Entry;
  Entry.SectionID = IDs.TextSection;
  Entry.Name = "entry";
  Entry.Flags = NEVERC_LINK_ATOM_LIVE;
  Entry.Alignment = 16;
  Entry.Content.assign(8, 0);
  Entry.Origin.InputID = InputID;
  IDs.EntryAtom = Graph->addAtom(std::move(Entry)).ID;
  PluginLinkAtom DataAtom;
  DataAtom.SectionID = IDs.DataSection;
  DataAtom.Name = "data";
  DataAtom.Flags = NEVERC_LINK_ATOM_LIVE;
  DataAtom.Alignment = 8;
  DataAtom.ZeroFillSize = 16;
  DataAtom.Origin.InputID = InputID;
  IDs.DataAtom = Graph->addAtom(std::move(DataAtom)).ID;
  PluginLinkSymbol EntrySymbol;
  EntrySymbol.Name = "entry";
  EntrySymbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  EntrySymbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  EntrySymbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  EntrySymbol.AtomID = IDs.EntryAtom;
  EntrySymbol.IsPrevailing = true;
  EntrySymbol.IsRoot = true;
  EntrySymbol.Origin.InputID = InputID;
  Graph->addSymbol(std::move(EntrySymbol));
  return Graph;
}

struct RelocationGraphIDs {
  uint64_t SourceAtom = 0;
  uint64_t TargetAtom = 0;
  uint64_t Edge = 0;
};

std::shared_ptr<PluginLinkGraph>
makeRelocationGraph(RelocationGraphIDs &IDs) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_THUNKS_RELAXED);
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///relocation.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;
  PluginLinkSection Text;
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Alignment = 8;
  Text.Size = 8;
  Text.Origin.InputID = InputID;
  const uint64_t TextID = Graph->addSection(std::move(Text)).ID;
  PluginLinkSection Data;
  Data.Name = ".data";
  Data.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Data.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Data.Alignment = 8;
  Data.Size = 8;
  Data.Origin.InputID = InputID;
  const uint64_t DataID = Graph->addSection(std::move(Data)).ID;
  PluginLinkAtom Source;
  Source.SectionID = TextID;
  Source.Name = "source";
  Source.Flags = NEVERC_LINK_ATOM_LIVE;
  Source.Alignment = 8;
  Source.Content.assign(8, 0);
  Source.Origin.InputID = InputID;
  IDs.SourceAtom = Graph->addAtom(std::move(Source)).ID;
  PluginLinkAtom TargetAtom;
  TargetAtom.SectionID = DataID;
  TargetAtom.Name = "target";
  TargetAtom.Flags = NEVERC_LINK_ATOM_LIVE;
  TargetAtom.Alignment = 8;
  TargetAtom.Content.assign(8, 0xaa);
  TargetAtom.Origin.InputID = InputID;
  IDs.TargetAtom = Graph->addAtom(std::move(TargetAtom)).ID;
  PluginLinkSymbol Symbol;
  Symbol.Name = "target";
  Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Symbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Symbol.AtomID = IDs.TargetAtom;
  Symbol.IsPrevailing = true;
  Symbol.Origin.InputID = InputID;
  const uint64_t SymbolID = Graph->addSymbol(std::move(Symbol)).ID;
  PluginLinkEdge Edge;
  Edge.SourceAtomID = IDs.SourceAtom;
  Edge.Offset = 0;
  Edge.RelocationKind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Edge.Width = 64;
  Edge.Addend = 4;
  Edge.TargetSymbolID = SymbolID;
  Edge.Origin.InputID = InputID;
  IDs.Edge = Graph->addEdge(std::move(Edge)).ID;
  return Graph;
}

TEST(PluginLinkLayoutTest,
     BuiltinLayoutAssignsAlignedNonOverlappingRanges) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  LayoutGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeLayoutGraph(IDs), NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  const PluginLinkSection *Text =
      (*Output)->findSection(IDs.TextSection);
  const PluginLinkSection *Data =
      (*Output)->findSection(IDs.DataSection);
  ASSERT_NE(Text, nullptr);
  ASSERT_NE(Data, nullptr);
  EXPECT_EQ(Text->Address % Text->Alignment, 0U);
  EXPECT_EQ(Data->Address % Data->Alignment, 0U);
  EXPECT_GE(Data->Address, Text->Address + Text->Size);
  EXPECT_EQ((*Output)->findAtom(IDs.EntryAtom)->Address,
            Text->Address);
  EXPECT_EQ((*Output)->findAtom(IDs.DataAtom)->Address,
            Data->Address);
}

TEST(PluginLinkLayoutTest,
     BuiltinRelocationWritesTargetAddressUsingTargetEndianness) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  RelocationGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeRelocationGraph(IDs),
      NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  const PluginLinkAtom *Source =
      (*Output)->findAtom(IDs.SourceAtom);
  const PluginLinkAtom *Target =
      (*Output)->findAtom(IDs.TargetAtom);
  ASSERT_NE(Source, nullptr);
  ASSERT_NE(Target, nullptr);
  uint64_t Encoded = 0;
  for (unsigned Index = 0; Index != 8; ++Index)
    Encoded |= static_cast<uint64_t>(Source->Content[Index])
               << (Index * 8);
  EXPECT_EQ(Encoded, Target->Address + 4);
}

TEST(PluginLinkLayoutTest,
     PureCInterceptorCanConstrainLayoutAndObserveProof) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestLinkLayoutTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.DesiredImageBase = UINT64_C(0x400000);
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_LAYOUT_HIGH,
                       NEVERC_PHASE_LINK_LAYOUT_LOW};
  Interceptor.Callback = neverc_test_link_layout_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  LayoutGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeLayoutGraph(IDs), NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_EQ(Trace.ProofSeen, NEVERC_TRUE);
  EXPECT_EQ(Trace.ObservedImageBase, UINT64_C(0x400000));
  EXPECT_EQ((*Output)->findSection(IDs.TextSection)->Address,
            UINT64_C(0x400000));
  EXPECT_EQ(Trace.ObservedEntryAddress,
            (*Output)->findAtom(IDs.EntryAtom)->Address);
}

TEST(PluginLinkLayoutTest,
     InvalidPluginPageConstraintPreventsLayoutPublication) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestLinkLayoutTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.InvalidPageSize = NEVERC_TRUE;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_LAYOUT_HIGH,
                       NEVERC_PHASE_LINK_LAYOUT_LOW};
  Interceptor.Callback = neverc_test_link_layout_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  LayoutGraphIDs IDs;
  auto Output = (*Pipeline)->execute(
      makeLayoutGraph(IDs), NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
}

TEST(PluginLinkLayoutTest,
     RelocationHandlesPCRelativeAndTLSDynamicPolicies) {
  {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Pipeline = LinkPhasePipeline::create(Scope.task());
    ASSERT_TRUE(static_cast<bool>(Pipeline))
        << errorText(Pipeline.takeError());
    RelocationGraphIDs IDs;
    auto Graph = makeRelocationGraph(IDs);
    PluginLinkEdge *Edge = Graph->findEdge(IDs.Edge);
    Edge->RelocationKind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
    Edge->Width = 32;
    Edge->Addend = 0;
    Edge->IsPCRelative = true;
    Edge->IsSigned = true;
    auto Output = (*Pipeline)->execute(
        Graph, NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
    ASSERT_TRUE(static_cast<bool>(Output))
        << errorText(Output.takeError());
    const PluginLinkAtom *Source =
        (*Output)->findAtom(IDs.SourceAtom);
    const PluginLinkAtom *Target =
        (*Output)->findAtom(IDs.TargetAtom);
    uint32_t Encoded = 0;
    for (unsigned Index = 0; Index != 4; ++Index)
      Encoded |= static_cast<uint32_t>(Source->Content[Index])
                 << (Index * 8);
    EXPECT_EQ(
        static_cast<int32_t>(Encoded),
        static_cast<int32_t>(Target->Address - Source->Address));
  }
  {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Pipeline = LinkPhasePipeline::create(Scope.task());
    ASSERT_TRUE(static_cast<bool>(Pipeline))
        << errorText(Pipeline.takeError());
    RelocationGraphIDs IDs;
    auto Graph = makeRelocationGraph(IDs);
    Graph->findEdge(IDs.Edge)->RelocationKind =
        NEVERC_OBJECT_RELOCATION_TLS;
    auto Output = (*Pipeline)->execute(
        Graph, NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
    ASSERT_TRUE(static_cast<bool>(Output))
        << errorText(Output.takeError());
    EXPECT_TRUE(std::all_of(
        (*Output)->findAtom(IDs.SourceAtom)->Content.begin(),
        (*Output)->findAtom(IDs.SourceAtom)->Content.end(),
        [](uint8_t Byte) { return Byte == 0; }));
  }
}

TEST(PluginLinkLayoutTest, RelocationRejectsThirtyTwoBitOverflow) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  RelocationGraphIDs IDs;
  auto Graph = makeRelocationGraph(IDs);
  Graph->findEdge(IDs.Edge)->Width = 32;
  PluginLinkConstraint Base;
  Base.Kind = "image-base";
  Base.Value = UINT64_C(0x100000000);
  Base.Required = true;
  Graph->addConstraint(std::move(Base));
  auto Output = (*Pipeline)->execute(
      Graph, NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    llvm::consumeError(Output.takeError());
}

TEST(PluginLinkLayoutTest,
     LayoutRejectsOverlappingAndWritableExecutableRanges) {
  {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Pipeline = LinkPhasePipeline::create(Scope.task());
    ASSERT_TRUE(static_cast<bool>(Pipeline))
        << errorText(Pipeline.takeError());
    LayoutGraphIDs IDs;
    auto Graph = makeLayoutGraph(IDs);
    Graph->findSection(IDs.TextSection)->Flags |=
        NEVERC_OBJECT_SECTION_WRITABLE;
    auto Output = (*Pipeline)->execute(
        Graph, NEVERC_LINK_STATE_LAYOUT_COMPLETE);
    EXPECT_FALSE(static_cast<bool>(Output));
    if (!Output)
      llvm::consumeError(Output.takeError());
  }
  {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize());
    auto Pipeline = LinkPhasePipeline::create(Scope.task());
    ASSERT_TRUE(static_cast<bool>(Pipeline))
        << errorText(Pipeline.takeError());
    LayoutGraphIDs IDs;
    auto Graph = makeLayoutGraph(IDs);
    PluginLinkConstraint TextAddress;
    TextAddress.Kind = "section-address";
    TextAddress.SubjectID = IDs.TextSection;
    TextAddress.Value = UINT64_C(0x10000);
    TextAddress.Required = true;
    Graph->addConstraint(std::move(TextAddress));
    PluginLinkConstraint DataAddress;
    DataAddress.Kind = "section-address";
    DataAddress.SubjectID = IDs.DataSection;
    DataAddress.Value = UINT64_C(0x10000);
    DataAddress.Required = true;
    Graph->addConstraint(std::move(DataAddress));
    auto Output = (*Pipeline)->execute(
        Graph, NEVERC_LINK_STATE_LAYOUT_COMPLETE);
    EXPECT_FALSE(static_cast<bool>(Output));
    if (!Output)
      llvm::consumeError(Output.takeError());
  }
}

} // namespace
