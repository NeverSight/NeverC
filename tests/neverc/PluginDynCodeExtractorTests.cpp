// Volume 6 task 11: the format-agnostic dyncode extractor.
//
// These tests drive ObjectGraphExtractor directly over in-memory
// plugin::PluginObjectGraph inputs (no file on disk, no per-format switch):
// graph validation, code selection / forbidden-section discard, entry-first
// layout, the symbol output map and relocation worklist (dispositions left
// Pending, no bytes patched), the surviving external ledger, entry-policy
// errors, and the shared plan verifier that a plugin replacement of
// object.graph -> extraction.plan must also pass.

#include "Extractor/ObjectGraphExtractor.h"

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/Schema/PluginObjectSchema.inc"

#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;
using namespace neverc::dyncode;

namespace {

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

Expected<OwnedTargetKey> makeTargetKey() {
  return TargetKeyBuilder()
      .setTargetID({UINT64_C(0x4e4344584543544f), UINT64_C(1)})
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43504142495401), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43504343495401), UINT64_C(1)})
      .setObjectFormat({UINT64_C(0x4e4344584f424a01), UINT64_C(1)})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest("0123456789abcdef0123456789abcdef"
                       "0123456789abcdef0123456789abcdef")
      .build();
}

TargetDesc linuxX86User() {
  TargetDesc T;
  T.OS = DynCodeOS::Linux;
  T.Arch = DynCodeArch::X86_64;
  T.Format = ObjectFormat::ELF;
  T.Level = ExecutionLevel::User;
  return T;
}

struct GraphBuilder {
  std::unique_ptr<PluginObjectGraph> G;

  explicit GraphBuilder(OwnedTargetKey Key)
      : G(std::make_unique<PluginObjectGraph>(std::move(Key))) {}

  uint64_t addCodeSection(StringRef Name, ArrayRef<uint8_t> Data,
                          uint64_t Align = 16) {
    PluginObjectSection S;
    S.ID = G->allocateEntityID();
    S.Name = Name.str();
    S.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    S.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    S.Alignment = Align;
    S.Data.assign(Data.begin(), Data.end());
    uint64_t ID = S.ID;
    G->sections().push_back(std::move(S));
    return ID;
  }

  uint64_t addDataSection(StringRef Name, ArrayRef<uint8_t> Data) {
    PluginObjectSection S;
    S.ID = G->allocateEntityID();
    S.Name = Name.str();
    S.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    S.Flags = NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    S.Alignment = 1;
    S.Data.assign(Data.begin(), Data.end());
    uint64_t ID = S.ID;
    G->sections().push_back(std::move(S));
    return ID;
  }

  uint64_t addFunc(StringRef Name, uint64_t SecID, uint64_t Value,
                   uint64_t Size,
                   uint32_t Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL) {
    PluginObjectSymbol Sym;
    Sym.ID = G->allocateEntityID();
    Sym.Name = Name.str();
    Sym.Binding = Binding;
    Sym.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Sym.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Sym.SectionID = SecID;
    Sym.Value = Value;
    Sym.Size = Size;
    Sym.Alignment = 1;
    uint64_t ID = Sym.ID;
    G->symbols().push_back(std::move(Sym));
    return ID;
  }

  uint64_t addExtern(StringRef Name) {
    PluginObjectSymbol Sym;
    Sym.ID = G->allocateEntityID();
    Sym.Name = Name.str();
    Sym.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Sym.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Sym.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    Sym.SectionID = 0;
    Sym.Alignment = 1;
    uint64_t ID = Sym.ID;
    G->symbols().push_back(std::move(Sym));
    return ID;
  }

  uint64_t addSymbolReloc(uint64_t SecID, uint64_t Offset, uint32_t WidthBits,
                          uint64_t TargetSymID) {
    PluginObjectRelocation R;
    R.ID = G->allocateEntityID();
    R.SectionID = SecID;
    R.Offset = Offset;
    R.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    R.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    R.Width = WidthBits;
    R.TargetSymbolID = TargetSymID;
    uint64_t ID = R.ID;
    G->relocations().push_back(std::move(R));
    return ID;
  }

  void finalize() { G->issueLayoutProof(); }
};

std::vector<uint8_t> range(uint8_t Base, uint64_t N) {
  std::vector<uint8_t> Out;
  for (uint64_t I = 0; I < N; ++I)
    Out.push_back(static_cast<uint8_t>(Base + I));
  return Out;
}

const DynCodeSectionFragment *
findSelected(const DynCodeExtractionPlan &Plan) {
  for (const DynCodeSectionFragment &F : Plan.sectionFragments())
    if (F.Disposition == DynCodeSectionDisposition::Selected)
      return &F;
  return nullptr;
}

TEST(PluginDynCodeExtractorTest, RejectsUnverifiedGraph) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  GraphBuilder B(std::move(*Key));
  uint64_t Text = B.addCodeSection(".text", range(0x90, 16));
  B.addFunc("main", Text, 0, 16);
  // Intentionally do NOT issue a layout proof: the graph is unverified.

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  EXPECT_FALSE(static_cast<bool>(Result));
  if (!Result)
    consumeError(Result.takeError());
}

TEST(PluginDynCodeExtractorTest, SelectsCodeAndDiscardsForbiddenData) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  GraphBuilder B(std::move(*Key));
  uint64_t Text = B.addCodeSection(".text", range(0x90, 16));
  B.addFunc("main", Text, 0, 16);
  B.addDataSection(".data", range(0x01, 8));
  B.finalize();

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());

  const DynCodeExtractionPlan &Plan = Result->Plan;
  unsigned Selected = 0, Discarded = 0;
  const DynCodeSectionFragment *DataFrag = nullptr;
  for (const DynCodeSectionFragment &F : Plan.sectionFragments()) {
    if (F.Disposition == DynCodeSectionDisposition::Selected)
      ++Selected;
    else {
      ++Discarded;
      if (F.SourceName == ".data")
        DataFrag = &F;
    }
  }
  EXPECT_EQ(Selected, 1u);
  EXPECT_EQ(Discarded, 1u);
  ASSERT_NE(DataFrag, nullptr);
  EXPECT_EQ(DataFrag->Reason, "forbidden-data");

  EXPECT_EQ(Result->Image.size(), 16u);
  EXPECT_EQ(Result->Image.entryOffset(), 0u);
  EXPECT_EQ(Result->Image.entrySymbol(), "main");
  EXPECT_EQ(Result->Report.summary().SelectedSectionCount, 1u);
  EXPECT_EQ(Result->Report.summary().RejectedSectionCount, 1u);
}

TEST(PluginDynCodeExtractorTest, EntryFunctionMovedToOffsetZero) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  // helper occupies [0,8) with bytes 0xA0.., main occupies [8,16) with 0xB0..
  std::vector<uint8_t> Bytes = range(0xA0, 8);
  for (uint8_t V : range(0xB0, 8))
    Bytes.push_back(V);
  GraphBuilder B(std::move(*Key));
  uint64_t Text = B.addCodeSection(".text", Bytes);
  B.addFunc("helper", Text, 0, 8);
  B.addFunc("main", Text, 8, 8);
  B.finalize();

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());

  // The entry function is pulled to output offset 0, so the image is
  // main-bytes then helper-bytes.
  ArrayRef<uint8_t> Image = Result->Image.bytes();
  ASSERT_EQ(Image.size(), 16u);
  EXPECT_EQ(Image[0], 0xB0);
  EXPECT_EQ(Image[8], 0xA0);
  EXPECT_EQ(Result->Image.entryOffset(), 0u);

  uint64_t MainOff = ~0ull, HelperOff = ~0ull;
  for (const DynCodeSymbolMapping &M : Result->Plan.symbolMappings()) {
    if (M.Name == "main")
      MainOff = M.OutputOffset;
    else if (M.Name == "helper")
      HelperOff = M.OutputOffset;
  }
  EXPECT_EQ(MainOff, 0u);
  EXPECT_EQ(HelperOff, 8u);
}

TEST(PluginDynCodeExtractorTest, RelocationWorklistAndExternalLedger) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  GraphBuilder B(std::move(*Key));
  std::vector<uint8_t> Bytes = range(0x90, 16);
  uint64_t Text = B.addCodeSection(".text", Bytes);
  uint64_t Main = B.addFunc("main", Text, 0, 16);
  uint64_t Ext = B.addExtern("ext");
  B.addSymbolReloc(Text, 4, /*WidthBits=*/32, Ext);  // unresolved external
  B.addSymbolReloc(Text, 8, /*WidthBits=*/32, Main); // intra-image
  B.finalize();

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());

  const DynCodeExtractionPlan &Plan = Result->Plan;
  ASSERT_EQ(Plan.relocations().size(), 2u);
  for (const DynCodeRelocationEntry &R : Plan.relocations()) {
    // The planner records the worklist but never applies it: Pending.
    EXPECT_EQ(R.Disposition, DynCodeRelocDisposition::Pending);
    EXPECT_EQ(R.Width, 4u); // 32 bits -> 4 bytes
    if (R.SiteOffset == 8)
      EXPECT_EQ(R.TargetOffset, 0u); // main is at offset 0
  }
  ASSERT_EQ(Plan.externalContracts().size(), 1u);
  EXPECT_EQ(Plan.externalContracts()[0].Symbol, "ext");
  EXPECT_EQ(Plan.externalContracts()[0].Disposition,
            DynCodeExternalDisposition::Unresolved);
  EXPECT_EQ(Result->Report.summary().RemainingExternalCount, 1u);

  // The planner writes no bytes: the image is the raw concatenation.
  EXPECT_EQ(Result->Image.bytes(), ArrayRef<uint8_t>(Bytes));
}

TEST(PluginDynCodeExtractorTest, MissingEntryFails) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  GraphBuilder B(std::move(*Key));
  uint64_t Text = B.addCodeSection(".text", range(0x90, 8));
  B.addFunc("helper", Text, 0, 8); // not a default entry name
  B.finalize();

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  EXPECT_FALSE(static_cast<bool>(Result));
  if (!Result)
    consumeError(Result.takeError());
}

TEST(PluginDynCodeExtractorTest, UnknownExplicitEntryFails) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  GraphBuilder B(std::move(*Key));
  uint64_t Text = B.addCodeSection(".text", range(0x90, 8));
  B.addFunc("main", Text, 0, 8);
  B.finalize();

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  Opts.EntrySymbol = "does_not_exist";
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  EXPECT_FALSE(static_cast<bool>(Result));
  if (!Result)
    consumeError(Result.takeError());
}

TEST(PluginDynCodeExtractorTest, AmbiguousDefaultEntryFails) {
  auto Key = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Key)) << errorText(Key.takeError());
  GraphBuilder B(std::move(*Key));
  uint64_t Text = B.addCodeSection(".text", range(0x90, 16));
  B.addFunc("main", Text, 0, 8);
  B.addFunc("dyncode_entry", Text, 8, 8); // both are default entry names
  B.finalize();

  DynCodeOptions Opts;
  Opts.Target = linuxX86User();
  ObjectGraphExtractor Extractor(*B.G, Opts);
  auto Result = Extractor.run();
  EXPECT_FALSE(static_cast<bool>(Result));
  if (!Result)
    consumeError(Result.takeError());

  // Disambiguating with an explicit entry succeeds.
  Opts.EntrySymbol = "dyncode_entry";
  ObjectGraphExtractor Extractor2(*B.G, Opts);
  auto Ok = Extractor2.run();
  ASSERT_TRUE(static_cast<bool>(Ok)) << errorText(Ok.takeError());
  EXPECT_EQ(Ok->Image.entrySymbol(), "dyncode_entry");
}

TEST(PluginDynCodeExtractorTest, PlanReplacementMustPassVerifier) {
  DynCodeOptions Opts;
  Opts.Target = linuxX86User();

  // A well-formed replacement: entry at 0, one entry mapping, non-empty image.
  {
    DynCodeExtractionPlan Plan;
    DynCodeSectionFragment F;
    F.SourceName = "custom";
    F.OutputOffset = 0;
    F.OutputSize = 4;
    F.Alignment = 1;
    ASSERT_TRUE(static_cast<bool>(Plan.addSectionFragment(F)));
    DynCodeSymbolMapping M;
    M.Name = "main";
    M.OutputOffset = 0;
    M.IsEntry = true;
    ASSERT_TRUE(static_cast<bool>(Plan.addSymbolMapping(M)));
    ASSERT_FALSE(Plan.setEntry(DynCodeEntryPolicy::Explicit, "main", 0));

    DynCodeImage Image;
    ASSERT_FALSE(Image.append({1, 2, 3, 4}));
    Image.setEntry(0, "main");
    EXPECT_FALSE(verifyDynCodeExtractionPlan(Plan, Image, Opts))
        << "a valid replacement plan must verify";
  }

  // A replacement that puts the entry off offset 0 is rejected (entry-at-zero).
  {
    DynCodeExtractionPlan Plan;
    DynCodeSymbolMapping M;
    M.Name = "main";
    M.OutputOffset = 4;
    M.IsEntry = true;
    ASSERT_TRUE(static_cast<bool>(Plan.addSymbolMapping(M)));
    ASSERT_FALSE(Plan.setEntry(DynCodeEntryPolicy::Explicit, "main", 4));

    DynCodeImage Image;
    ASSERT_FALSE(Image.append({1, 2, 3, 4, 5, 6, 7, 8}));
    Image.setEntry(4, "main");
    auto E = verifyDynCodeExtractionPlan(Plan, Image, Opts);
    EXPECT_TRUE(static_cast<bool>(E));
    consumeError(std::move(E));
  }
}

} // namespace
