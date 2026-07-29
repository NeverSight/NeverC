#include "PluginLinkTestSupport.h"
#include "Inputs/Plugin/LinkLayoutPlugin.h"
#include "Link/LayoutVerifier.h"
#include "Link/LinkPhaseExecutor.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/BinaryFormat/ELF.h"
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
makeRelocationGraph(RelocationGraphIDs &IDs,
                    llvm::Expected<OwnedTargetKey> Target = makeTargetKey()) {
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

// An AArch64 `bl` with the relocation that patches its offset field. The
// field is 26 bits inside the instruction word, so the graph calls it 32 bits
// wide -- that is the size of the word holding it, not of a value that can be
// written over the word.
std::shared_ptr<PluginLinkGraph>
makeInstructionFieldGraph(RelocationGraphIDs &IDs) {
  // A relocation type number says nothing without a target to read it
  // through: 283 is AArch64's CALL26 and no relocation at all on x86_64.
  auto Graph = makeRelocationGraph(
      IDs, TargetKeyBuilder()
               .setTargetID({UINT64_C(0x4e43504c47524150), UINT64_C(1)})
               .setTriple("aarch64-unknown-linux-gnu", "aarch64", "unknown",
                          "linux", "gnu")
               .setCPU("generic", "generic")
               .setFeatures({})
               .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
               .setCallingConvention(
                   {UINT64_C(0x4e43504343495401), UINT64_C(1)})
               .setObjectFormat({UINT64_C(0x4e43504f424a5446), UINT64_C(1)})
               .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                                  NEVERC_TARGET_CODE_MODEL_SMALL)
               .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                             NEVERC_TARGET_ENDIAN_LITTLE)
               .setSchemaDigest("0123456789abcdef0123456789abcdef"
                                "0123456789abcdef0123456789abcdef")
               .build());
  if (!Graph)
    return Graph;
  PluginLinkAtom *Source = Graph->findAtom(IDs.SourceAtom);
  // bl #0 -- opcode 0x94 in the top byte, offset field zero below it.
  Source->Content = {0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x94};
  PluginLinkEdge *Edge = Graph->findEdge(IDs.Edge);
  Edge->RelocationKind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  Edge->Width = 32;
  Edge->Addend = 0;
  Edge->IsPCRelative = true;
  Edge->IsSigned = true;

  namespace ext = neverc::plugin::builtinext;
  PluginLinkExtensionData Extension;
  Extension.NamespaceID = Graph->targetKey().ObjectFormatID;
  Extension.Version = ext::RelocationVersion;
  llvm::SmallVector<uint8_t, 32> Bytes;
  ext::appendHeader(Bytes, ext::RelocationTag, ext::RelocationVersion);
  ext::appendU64(Bytes, llvm::ELF::R_AARCH64_CALL26);
  ext::appendU32(Bytes, 16);
  ext::appendBytes(Bytes, "R_AARCH64_CALL26");
  Extension.Payload.assign(Bytes.begin(), Bytes.end());
  Edge->Extensions.values().push_back(std::move(Extension));
  return Graph;
}

TEST(PluginLinkLayoutTest, RelocationRefusesAFieldInsideAnInstruction) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  RelocationGraphIDs IDs;
  auto Graph = makeInstructionFieldGraph(IDs);
  ASSERT_TRUE(static_cast<bool>(Graph));

  // Writing the displacement over the four bytes the relocation covers takes
  // the opcode with it: what was a `bl` becomes whatever the displacement
  // happens to encode. The value fits the field and every check downstream is
  // satisfied, so nothing says the code was replaced.
  auto Output = (*Pipeline)->execute(
      Graph, NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
  if (!Output) {
    llvm::consumeError(Output.takeError());
    return;
  }
  const PluginLinkAtom *Source = (*Output)->findAtom(IDs.SourceAtom);
  ASSERT_NE(Source, nullptr);
  EXPECT_EQ(Source->Content[3], 0x94)
      << "the relocation overwrote the instruction it was meant to patch";
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

// The shape a linker hands back once an image is laid out: .data occupies file
// bytes, the section after it does or does not depending on its kind.  Nothing
// pads the file cursor for a section that takes no file space, so the tail
// keeps whatever offset .data left behind -- here deliberately not a multiple
// of the alignment the tail's memory image is held to.
std::shared_ptr<PluginLinkGraph>
makeLaidOutImageGraph(NevercObjectSectionKind TailKind, uint64_t &TailID) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///image.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;

  PluginLinkSection Data;
  Data.Name = ".data";
  Data.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Data.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Data.Alignment = 8;
  Data.Address = 0x1000;
  Data.FileOffset = 0x1000;
  Data.Size = 8;
  Data.Origin.InputID = InputID;
  const uint64_t DataID = Graph->addSection(std::move(Data)).ID;

  PluginLinkSection Tail;
  Tail.Name = ".bss";
  Tail.Kind = TailKind;
  Tail.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  Tail.Alignment = 64;
  Tail.Address = 0x1040;
  Tail.FileOffset = 0x1008;
  Tail.Size = 64;
  Tail.Origin.InputID = InputID;
  TailID = Graph->addSection(std::move(Tail)).ID;

  PluginLinkAtom Value;
  Value.SectionID = DataID;
  Value.Name = "value";
  Value.Flags = NEVERC_LINK_ATOM_LIVE;
  Value.Alignment = 8;
  Value.Address = 0x1000;
  Value.FileOffset = 0x1000;
  Value.Content.assign(8, 0x5a);
  Value.Origin.InputID = InputID;
  const uint64_t ValueAtom = Graph->addAtom(std::move(Value)).ID;

  PluginLinkAtom Buffer;
  Buffer.SectionID = TailID;
  Buffer.Name = "buffer";
  Buffer.Flags = NEVERC_LINK_ATOM_LIVE;
  Buffer.Alignment = 64;
  Buffer.Address = 0x1040;
  Buffer.FileOffset = 0x1008;
  if (TailKind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL)
    Buffer.ZeroFillSize = 64;
  else
    Buffer.Content.assign(64, 0);
  Buffer.Origin.InputID = InputID;
  Graph->addAtom(std::move(Buffer));

  PluginLinkSymbol Symbol;
  Symbol.Name = "value";
  Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Symbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Symbol.AtomID = ValueAtom;
  Symbol.IsPrevailing = true;
  Symbol.IsRoot = true;
  Symbol.Origin.InputID = InputID;
  Graph->addSymbol(std::move(Symbol));
  return Graph;
}

TEST(PluginLinkLayoutTest, LayoutAcceptsUnpaddedZeroFillFileOffset) {
  uint64_t TailID = 0;
  auto Graph =
      makeLaidOutImageGraph(NEVERC_OBJECT_SECTION_KIND_ZERO_FILL, TailID);
  ASSERT_TRUE(static_cast<bool>(Graph));
  llvm::Error Result = verifyLinkLayout(*Graph);
  const bool Rejected = static_cast<bool>(Result);
  EXPECT_FALSE(Rejected) << errorText(std::move(Result));
}

TEST(PluginLinkLayoutTest, LayoutRejectsUnalignedFileBackedSection) {
  uint64_t TailID = 0;
  auto Graph = makeLaidOutImageGraph(NEVERC_OBJECT_SECTION_KIND_DATA, TailID);
  ASSERT_TRUE(static_cast<bool>(Graph));
  llvm::Error Result = verifyLinkLayout(*Graph);
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_NE(errorText(std::move(Result)).find("section alignment is invalid"),
            std::string::npos);
}

// The shape a linker hands back for a thread-local image: .tdata holds the
// initialized part of the TLS template, .tbss the zero-fill part, and an
// ordinary section follows them in the same segment.  .tbss takes no segment
// space, so the linker places that section at an address .tbss also spans --
// \p ZeroFillAddress is where the caller puts .tbss to choose which overlap
// the graph exhibits.
std::shared_ptr<PluginLinkGraph>
makeThreadLocalImageGraph(uint64_t ZeroFillAddress) {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_LAYOUT_COMPLETE);

  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///tls.o";
  const uint64_t InputID = Graph->addInput(std::move(Input)).ID;

  auto addSection = [&](std::string Name, NevercObjectSectionKind Kind,
                        uint64_t Flags, uint64_t Alignment, uint64_t Address,
                        uint64_t FileOffset, uint64_t Size) {
    PluginLinkSection Section;
    Section.Name = std::move(Name);
    Section.Kind = Kind;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Address = Address;
    Section.FileOffset = FileOffset;
    Section.Size = Size;
    Section.Origin.InputID = InputID;
    return Graph->addSection(std::move(Section)).ID;
  };
  const uint64_t Writable =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  const uint64_t ThreadLocal = Writable | NEVERC_OBJECT_SECTION_TLS;

  const uint64_t DataID =
      addSection(".tdata", NEVERC_OBJECT_SECTION_KIND_TLS_DATA, ThreadLocal, 8,
                 0x1000, 0x1000, 8);
  const uint64_t ZeroFillID =
      addSection(".tbss", NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL, ThreadLocal,
                 4, ZeroFillAddress, 0x1008, 64);
  const uint64_t TailID =
      addSection(".fini_array", NEVERC_OBJECT_SECTION_KIND_DATA, Writable, 8,
                 0x1008, 0x1008, 8);

  PluginLinkAtom Initialized;
  Initialized.SectionID = DataID;
  Initialized.Name = "tls_value";
  Initialized.Flags = NEVERC_LINK_ATOM_LIVE | NEVERC_LINK_ATOM_TLS;
  Initialized.Alignment = 8;
  Initialized.Address = 0x1000;
  Initialized.FileOffset = 0x1000;
  Initialized.Content.assign(8, 0x5a);
  Initialized.Origin.InputID = InputID;
  const uint64_t InitializedAtom = Graph->addAtom(std::move(Initialized)).ID;

  PluginLinkAtom Scratch;
  Scratch.SectionID = ZeroFillID;
  Scratch.Name = "tls_scratch";
  Scratch.Flags = NEVERC_LINK_ATOM_LIVE | NEVERC_LINK_ATOM_TLS;
  Scratch.Alignment = 4;
  Scratch.Address = ZeroFillAddress;
  Scratch.FileOffset = 0x1008;
  Scratch.ZeroFillSize = 64;
  Scratch.Origin.InputID = InputID;
  Graph->addAtom(std::move(Scratch));

  PluginLinkAtom Destructors;
  Destructors.SectionID = TailID;
  Destructors.Name = "fini";
  Destructors.Flags = NEVERC_LINK_ATOM_LIVE;
  Destructors.Alignment = 8;
  Destructors.Address = 0x1008;
  Destructors.FileOffset = 0x1008;
  Destructors.Content.assign(8, 0);
  Destructors.Origin.InputID = InputID;
  Graph->addAtom(std::move(Destructors));

  PluginLinkSymbol Symbol;
  Symbol.Name = "tls_value";
  Symbol.Binding = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
  Symbol.Definition = NEVERC_LINK_SYMBOL_DEFINED;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_TLS;
  Symbol.AtomID = InitializedAtom;
  Symbol.IsPrevailing = true;
  Symbol.IsRoot = true;
  Symbol.Origin.InputID = InputID;
  Graph->addSymbol(std::move(Symbol));
  return Graph;
}

// .tbss lives in the TLS template rather than in the process image, so the
// section placed after it legitimately holds the same addresses.  Proving the
// image and the template as one address space rejected every thread-local
// link.
TEST(PluginLinkLayoutTest, LayoutAcceptsThreadLocalZeroFillOverAnImageSection) {
  auto Graph = makeThreadLocalImageGraph(/*ZeroFillAddress=*/0x1008);
  ASSERT_TRUE(static_cast<bool>(Graph));
  llvm::Error Result = verifyLinkLayout(*Graph);
  const bool Rejected = static_cast<bool>(Result);
  EXPECT_FALSE(Rejected) << errorText(std::move(Result));
}

// The other half of that trade: within the template the two parts still each
// own their range, so a .tbss reaching back into .tdata would give one
// thread-local variable's storage to another and is still a layout error.
TEST(PluginLinkLayoutTest, LayoutRejectsThreadLocalZeroFillOverItsOwnTemplate) {
  auto Graph = makeThreadLocalImageGraph(/*ZeroFillAddress=*/0x1004);
  ASSERT_TRUE(static_cast<bool>(Graph));
  llvm::Error Result = verifyLinkLayout(*Graph);
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_NE(
      errorText(std::move(Result)).find("thread-local address ranges overlap"),
      std::string::npos);
}

} // namespace
