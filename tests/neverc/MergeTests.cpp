//===- MergeTests.cpp - Fuzzing & edge-case tests for object mergers -----===//
//
// Exercises the ELF, COFF, and MachO merger paths with adversarial and
// boundary inputs.  Each test feeds raw byte buffers through the merger
// and asserts it either produces valid output or returns false — never
// crashes.  The fuzz-style helpers use deterministic seeds so these run
// in CI without a fuzzer harness.
//
//===--------------------------------------------------------------------===//

#include "Common/DwarfRebase.h"
#include "Common/MergerCommon.h"
#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Merge/Merger.h"
#include "neverc/Plugin/Host/NativeRelocationFacts.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/raw_ostream.h"

// Used only by the NEVERC_BINARY-gated differential suite at end of file, but
// harmless to include unconditionally.
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <gtest/gtest.h>

#include <optional>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::merge;

TEST(AndroidKernelModuleReleaseNames, FormatsCanonicalSpelling) {
  using NameKind = neverc::ReleaseNameKind;

  EXPECT_EQ(neverc::formatReleaseName(NameKind::Function, 0xC000, 0),
            "fn_C000");
  EXPECT_EQ(neverc::formatReleaseName(NameKind::Object, 0x28680, 8),
            "obj_28680");
  EXPECT_EQ(neverc::formatReleaseName(NameKind::Absolute, 0x2A, 0), "abs_2A");
  EXPECT_EQ(neverc::formatReleaseName(NameKind::Function, 0, 0), "fn_0");
  EXPECT_EQ(neverc::formatReleaseName(NameKind::ExecutableLabel, 8, 0),
            "code_8");
}

TEST(AndroidKernelReleaseSymbolMap, SerializesVersionedSortedMap) {
  neverc::AndroidKernelReleaseSymbolMap Map;
  for (size_t I = 0; I != Map.ImageSHA256.size(); ++I)
    Map.ImageSHA256[I] = static_cast<uint8_t>(I);
  Map.Symbols.push_back({"second_original", "fn_20"});
  Map.Symbols.push_back({"first_original", "fn_10"});

  auto Serialized = neverc::serializeAndroidKernelReleaseSymbolMap(Map);
  ASSERT_TRUE(static_cast<bool>(Serialized));
  EXPECT_TRUE(StringRef(*Serialized).ends_with("\n"));

  auto Parsed = json::parse(*Serialized);
  ASSERT_TRUE(static_cast<bool>(Parsed));
  const json::Object *Root = Parsed->getAsObject();
  ASSERT_NE(Root, nullptr);
  EXPECT_EQ(Root->getString("format"), "neverc.android-kernel-symbol-map");
  int64_t Version = 0;
  ASSERT_TRUE(Root->getInteger("version", Version));
  EXPECT_EQ(Version, 2);
  EXPECT_EQ(Root->getString("image_sha256"),
            "000102030405060708090a0b0c0d0e0f"
            "101112131415161718191a1b1c1d1e1f");

  const json::Array *Symbols = Root->getArray("symbols");
  ASSERT_NE(Symbols, nullptr);
  ASSERT_EQ(Symbols->size(), 2u);
  const json::Object *First = (*Symbols)[0].getAsObject();
  const json::Object *Second = (*Symbols)[1].getAsObject();
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  EXPECT_EQ(First->getString("original"), "first_original");
  EXPECT_EQ(First->getString("release"), "fn_10");
  EXPECT_EQ(Second->getString("original"), "second_original");
  EXPECT_EQ(Second->getString("release"), "fn_20");
}

TEST(AndroidKernelReleaseSymbolMap, RejectsAmbiguousReleaseNames) {
  neverc::AndroidKernelReleaseSymbolMap Map;
  Map.Symbols.push_back({"first_original", "fn_10"});
  Map.Symbols.push_back({"second_original", "fn_10"});
  auto Serialized = neverc::serializeAndroidKernelReleaseSymbolMap(Map);
  ASSERT_FALSE(static_cast<bool>(Serialized));
  consumeError(Serialized.takeError());
}

TEST(AndroidKernelReleaseSymbolMap, EncodesNonUTF8OriginalNamesLosslessly) {
  neverc::AndroidKernelReleaseSymbolMap Map;
  Map.Symbols.push_back({std::string("invalid_\xff", 9), "fn_10"});
  auto Serialized = neverc::serializeAndroidKernelReleaseSymbolMap(Map);
  ASSERT_TRUE(static_cast<bool>(Serialized))
      << toString(Serialized.takeError()).str().str();
  auto Parsed = json::parse(*Serialized);
  ASSERT_TRUE(static_cast<bool>(Parsed));
  const json::Object *Root = Parsed->getAsObject();
  ASSERT_NE(Root, nullptr);
  const json::Array *Symbols = Root->getArray("symbols");
  ASSERT_NE(Symbols, nullptr);
  ASSERT_EQ(Symbols->size(), 1u);
  const json::Object *Symbol = Symbols->front().getAsObject();
  ASSERT_NE(Symbol, nullptr);
  EXPECT_EQ(Symbol->getString("original"), "aW52YWxpZF//");
  EXPECT_EQ(Symbol->getString("original_encoding"), "base64");
  EXPECT_EQ(Symbol->getString("release"), "fn_10");
}

TEST(AndroidKernelReleaseSymbolMap, ClearsStateBeforeNonELFMerge) {
  neverc::AndroidKernelReleaseSymbolMap Map;
  Map.ImageSHA256[0] = 1;
  Map.Symbols.push_back({"stale_original", "fn_10"});
  Options Opts;
  Opts.releaseSymbolMap = &Map;
  SmallVector<SmallVector<char, 0>, 1> Buffers;
  SmallVector<char, 0> Output;
  raw_svector_ostream OS(Output);
  (void)mergeObjects(Buffers, OS, Format::COFF, Opts);
  const std::array<uint8_t, 32> EmptyDigest{};
  EXPECT_EQ(Map.ImageSHA256, EmptyDigest);
  EXPECT_TRUE(Map.Symbols.empty());

  Map.ImageSHA256[0] = 1;
  Map.Symbols.push_back({"stale_original", "fn_10"});
  (void)mergeObjects(Buffers, OS, Format::MachO64, Opts);
  EXPECT_EQ(Map.ImageSHA256, EmptyDigest);
  EXPECT_TRUE(Map.Symbols.empty());
}

TEST(AndroidKernelModuleReleaseNames, RecognizesOnlyCanonicalSpellingShape) {
  for (StringRef Name : {"fn_0", "fn_C000", "fn_C000_1", "obj_28680", "code_F",
                         "sym_50", "sym_S0_20", "sym_SA_20_1", "abs_2A"})
    EXPECT_TRUE(neverc::hasCanonicalReleaseNameShape(Name)) << Name.str();

  for (StringRef Name :
       {"sub_0", "loc_F", "fn_", "fn_00", "fn_c000", "fn_C000_0", "fn_C000_01",
        "fn_C000_1_2", "sym_S_20", "sym_SA_020", "0123456789abcdef"})
    EXPECT_FALSE(neverc::hasCanonicalReleaseNameShape(Name)) << Name.str();
}

TEST(AndroidKernelModuleReleaseNames, DerivesNoTypeNameFromFinalSectionFlags) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolType = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 1, 0x10, true, true},
      {2, 2, 1, 0x10, true, false},
      {3, 3, 1, 0x10, false, true},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {1, "exec_label", SymbolClass::Defined, SymbolType::NoType, 1, 0, 0, 0, 0,
       false},
      {2, "data_label", SymbolClass::Defined, SymbolType::NoType, 2, 0, 0, 0, 0,
       false},
      {3, "metadata_label", SymbolClass::Defined, SymbolType::NoType, 3, 0, 0,
       0, 0, false},
  };

  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  ASSERT_EQ(Plan->size(), 3U);
  EXPECT_EQ((*Plan)[0].OutputName, "code_0");
  EXPECT_EQ((*Plan)[1].OutputName, "sym_10");
  EXPECT_EQ((*Plan)[2].OutputName, "sym_S3_0");
}

TEST(AndroidKernelModuleReleaseNames, ComputesCanonicalSampleLayout) {
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 0x1000, 0x22048, true, true}, {2, 2, 1, 0xC4, true, false},
      {3, 3, 8, 0xEE8, true, false},       {4, 4, 1, 1, true, false},
      {5, 5, 1, 1, true, false},           {6, 6, 1, 1, true, false},
      {7, 7, 8, 0x54C0, true, false},      {8, 8, 8, 0x278, true, false},
  };

  auto Layout = neverc::computeAndroidKernelReleaseSectionLayout(Sections);
  if (!Layout)
    FAIL() << toString(Layout.takeError()).str().str();
  ASSERT_EQ(Layout->size(), std::size(Sections));
  EXPECT_EQ((*Layout)[0].Base, 0U);
  EXPECT_EQ((*Layout)[1].Base, 0x22048U);
  EXPECT_EQ((*Layout)[2].Base, 0x22110U);
  EXPECT_EQ((*Layout)[6].Base, 0x23000U);
  EXPECT_EQ((*Layout)[7].Base, 0x284C0U);
}

TEST(AndroidKernelModuleReleaseNames, EmptyAllocatedSectionReservesOneByte) {
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {10, 10, 1, 0, true, false},
      {11, 11, 8, 4, true, false},
  };

  auto Layout = neverc::computeAndroidKernelReleaseSectionLayout(Sections);
  if (!Layout)
    FAIL() << toString(Layout.takeError()).str().str();
  ASSERT_EQ(Layout->size(), 2U);
  EXPECT_EQ((*Layout)[0].Base, 0U);
  EXPECT_EQ((*Layout)[1].Base, 8U);
}

TEST(AndroidKernelModuleReleaseNames, RejectsCanonicalLayoutOverflow) {
  const uint64_t Max = std::numeric_limits<uint64_t>::max();
  const neverc::ReleaseSectionDescriptor SizeOverflow[] = {
      {1, 1, 1, 1, true, false},
      {2, 2, 1, Max, true, false},
  };
  auto SizeLayout =
      neverc::computeAndroidKernelReleaseSectionLayout(SizeOverflow);
  if (SizeLayout)
    ADD_FAILURE() << "accepted section-size addition overflow";
  else
    consumeError(SizeLayout.takeError());

  const neverc::ReleaseSectionDescriptor AlignmentOverflow[] = {
      {1, 1, 1, Max - 1, true, false},
      {2, 2, 4, 1, true, false},
  };
  auto AlignmentLayout =
      neverc::computeAndroidKernelReleaseSectionLayout(AlignmentOverflow);
  if (AlignmentLayout)
    ADD_FAILURE() << "accepted section-alignment overflow";
  else
    consumeError(AlignmentLayout.takeError());

  const neverc::ReleaseSectionDescriptor EmptyAdvanceOverflow[] = {
      {1, 1, 1, Max, true, false},
      {2, 2, 1, 0, true, false},
  };
  auto EmptyLayout =
      neverc::computeAndroidKernelReleaseSectionLayout(EmptyAdvanceOverflow);
  if (EmptyLayout)
    ADD_FAILURE() << "accepted empty-section reservation overflow";
  else
    consumeError(EmptyLayout.takeError());
}

TEST(AndroidKernelModuleReleaseNames, RejectsDuplicateSectionIdentity) {
  auto ExpectExchangeClassError =
      [](ArrayRef<neverc::ReleaseSectionDescriptor> Sections,
         StringRef Description) {
        auto Classes = neverc::computeAndroidKernelReleaseNameExchangeClasses(
            Sections, {});
        if (Classes)
          ADD_FAILURE() << "exchange-class API accepted " << Description.str();
        else
          consumeError(Classes.takeError());
      };

  const neverc::ReleaseSectionDescriptor DuplicateID[] = {
      {7, 1, 1, 1, true, false},
      {7, 2, 1, 1, true, false},
  };
  auto IDLayout = neverc::computeAndroidKernelReleaseSectionLayout(DuplicateID);
  if (IDLayout)
    ADD_FAILURE() << "accepted duplicate section IDs";
  else
    consumeError(IDLayout.takeError());
  ExpectExchangeClassError(DuplicateID, "duplicate section IDs");

  const neverc::ReleaseSectionDescriptor DuplicateOrdinal[] = {
      {7, 3, 1, 1, true, false},
      {8, 3, 1, 1, false, false},
  };
  auto OrdinalLayout =
      neverc::computeAndroidKernelReleaseSectionLayout(DuplicateOrdinal);
  if (OrdinalLayout)
    ADD_FAILURE() << "accepted duplicate final section ordinals";
  else
    consumeError(OrdinalLayout.takeError());
  ExpectExchangeClassError(DuplicateOrdinal,
                           "duplicate final section ordinals");
}

TEST(AndroidKernelModuleReleaseNames,
     RejectsNonPowerOfTwoAlignmentAtEveryPublicEntryPoint) {
  const neverc::ReleaseSectionDescriptor Invalid[] = {
      {1, 1, 3, 1, true, false},
  };
  auto Layout = neverc::computeAndroidKernelReleaseSectionLayout(Invalid);
  if (Layout)
    ADD_FAILURE() << "layout API accepted non-power-of-two alignment";
  else
    consumeError(Layout.takeError());

  auto Plan = neverc::planAndroidKernelReleaseNames(Invalid, {});
  if (Plan)
    ADD_FAILURE() << "planner API accepted non-power-of-two alignment";
  else
    consumeError(Plan.takeError());

  auto Classes =
      neverc::computeAndroidKernelReleaseNameExchangeClasses(Invalid, {});
  if (Classes)
    ADD_FAILURE() << "exchange-class API accepted non-power-of-two alignment";
  else
    consumeError(Classes.takeError());

  const neverc::ReleaseSectionDescriptor ZeroMeansOne[] = {
      {1, 1, 0, 1, true, false},
  };
  auto ZeroLayout =
      neverc::computeAndroidKernelReleaseSectionLayout(ZeroMeansOne);
  if (!ZeroLayout)
    FAIL() << toString(ZeroLayout.takeError()).str().str();
  ASSERT_EQ(ZeroLayout->size(), 1U);
  EXPECT_EQ((*ZeroLayout)[0].Base, 0U);
}

TEST(AndroidKernelModuleReleaseNames, PlansNamesFromFinalCoordinates) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolKind = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {10, 1, 1, 0x20, true, true},
      {20, 2, 0x10, 0x30, true, false},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {100, "function", SymbolClass::Defined, SymbolKind::Function, 10, 0xC, 0,
       1, 0, false},
      {101, "object", SymbolClass::Defined, SymbolKind::Object, 20, 8, 8, 1, 0,
       false},
      {102, "label", SymbolClass::Defined, SymbolKind::NoType, 10, 4, 0, 0, 0,
       false},
      {103, "other", SymbolClass::Defined, SymbolKind::NoType, 20, 0x10, 0, 0,
       0, false},
      {104, "constant", SymbolClass::Absolute, SymbolKind::NoType, 0, 0x2A, 0,
       1, 0, false},
      {105, "required_import", SymbolClass::Undefined, SymbolKind::NoType, 0, 0,
       0, 1, 0, false},
  };

  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  ASSERT_EQ(Plan->size(), std::size(Symbols));
  auto OutputFor = [&](uint64_t ID) -> StringRef {
    for (const neverc::ReleaseSymbolRename &Rename : *Plan)
      if (Rename.SymbolID == ID)
        return Rename.OutputName;
    return {};
  };
  EXPECT_EQ(OutputFor(100), "fn_C");
  EXPECT_EQ(OutputFor(101), "obj_28");
  EXPECT_EQ(OutputFor(102), "code_4");
  EXPECT_EQ(OutputFor(103), "sym_30");
  EXPECT_EQ(OutputFor(104), "abs_2A");
  EXPECT_EQ(OutputFor(105), "required_import");
}

TEST(AndroidKernelModuleReleaseNames, UsesNonAllocatedSectionFallback) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {70, 0xA, 4, 0x40, false, false},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {700, "metadata", SymbolClass::Defined, neverc::ReleaseSymbolType::Object,
       70, 0x20, 8, 1, 0, false},
  };

  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  ASSERT_EQ(Plan->size(), 1U);
  EXPECT_EQ((*Plan)[0].OutputName, "sym_SA_20");
}

TEST(AndroidKernelModuleReleaseNames, RejectsMalformedDefinitionCoordinates) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  const neverc::ReleaseSectionDescriptor Allocated[] = {
      {1, 1, 1, 0x10, true, false},
  };
  const neverc::ReleaseSymbolDescriptor PastEnd[] = {
      {1, "past_end", SymbolClass::Defined, neverc::ReleaseSymbolType::Object,
       1, 0x11, 1, 1, 0, false},
  };
  auto PastEndPlan = neverc::planAndroidKernelReleaseNames(Allocated, PastEnd);
  if (PastEndPlan)
    ADD_FAILURE() << "accepted st_value greater than section size";
  else
    consumeError(PastEndPlan.takeError());

  const neverc::ReleaseSymbolDescriptor ExtendsPastEnd[] = {
      {2, "extent_past_end", SymbolClass::Defined,
       neverc::ReleaseSymbolType::Object, 1, 0xF, 2, 1, 0, false},
  };
  auto ExtentPlan =
      neverc::planAndroidKernelReleaseNames(Allocated, ExtendsPastEnd);
  if (ExtentPlan)
    ADD_FAILURE() << "accepted st_size extending past section end";
  else
    consumeError(ExtentPlan.takeError());

  const neverc::ReleaseSymbolDescriptor EndMarker[] = {
      {3, "end_marker", SymbolClass::Defined,
       neverc::ReleaseSymbolType::Function, 1, 0x10, 0, 1, 0, false},
  };
  auto EndMarkerPlan =
      neverc::planAndroidKernelReleaseNames(Allocated, EndMarker);
  if (!EndMarkerPlan)
    FAIL() << toString(EndMarkerPlan.takeError()).str().str();
  ASSERT_EQ(EndMarkerPlan->size(), 1U);
  EXPECT_EQ((*EndMarkerPlan)[0].OutputName, "fn_10");

  const neverc::ReleaseSectionDescriptor NonAllocated[] = {
      {2, 2, 1, 0x10, false, true},
  };
  const neverc::ReleaseSymbolDescriptor Function[] = {
      {2, "malformed_function", SymbolClass::Defined,
       neverc::ReleaseSymbolType::Function, 2, 0, 4, 1, 0, false},
  };
  auto FunctionPlan =
      neverc::planAndroidKernelReleaseNames(NonAllocated, Function);
  if (FunctionPlan)
    ADD_FAILURE() << "accepted a function in a non-allocated section";
  else
    consumeError(FunctionPlan.takeError());
}

TEST(AndroidKernelModuleReleaseNames,
     PreservedNamesDoNotBypassDefinitionValidation) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolType = neverc::ReleaseSymbolType;
  auto ExpectError = [](ArrayRef<neverc::ReleaseSectionDescriptor> Sections,
                        ArrayRef<neverc::ReleaseSymbolDescriptor> Symbols,
                        StringRef Description) {
    auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
    if (Plan)
      ADD_FAILURE() << "accepted malformed preserved " << Description.str();
    else
      consumeError(Plan.takeError());

    auto Classes = neverc::computeAndroidKernelReleaseNameExchangeClasses(
        Sections, Symbols);
    if (Classes)
      ADD_FAILURE() << "exchange-class API accepted malformed preserved "
                    << Description.str();
    else
      consumeError(Classes.takeError());
  };

  const neverc::ReleaseSymbolDescriptor MissingLoaderSection[] = {
      {1, "init_module", SymbolClass::Defined, SymbolType::Function, 99, 0, 0,
       1, 0, false},
  };
  ExpectError({}, MissingLoaderSection, "loader symbol with no section");

  const neverc::ReleaseSectionDescriptor Allocated[] = {
      {1, 1, 1, 4, true, false},
  };
  const neverc::ReleaseSymbolDescriptor ProtectedExtentPastEnd[] = {
      {2, "protected_section_symbol", SymbolClass::Defined, SymbolType::Object,
       1, 3, 2, 1, 0, true},
  };
  ExpectError(Allocated, ProtectedExtentPastEnd,
              "protected symbol extent past its section end");

  const neverc::ReleaseSectionDescriptor NonAllocated[] = {
      {2, 2, 1, 4, false, true},
  };
  const neverc::ReleaseSymbolDescriptor EmptyFunction[] = {
      {3, "", SymbolClass::Defined, SymbolType::Function, 2, 0, 1, 0, 0, false},
  };
  ExpectError(NonAllocated, EmptyFunction,
              "empty-name function in a non-allocated section");
}

TEST(AndroidKernelModuleReleaseNames, GroupsCandidateCollisionsGlobally) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {10, 1, 1, 0x10, true, true},
      {20, 2, 1, 0x10, true, true},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {100, "next_start", SymbolClass::Defined,
       neverc::ReleaseSymbolType::Function, 20, 0, 0, 1, 0, false},
      {900, "previous_end", SymbolClass::Defined,
       neverc::ReleaseSymbolType::Function, 10, 0x10, 0, 1, 0, false},
  };

  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  ASSERT_EQ(Plan->size(), 2U);
  auto OutputFor = [&](uint64_t ID) -> StringRef {
    for (const neverc::ReleaseSymbolRename &Rename : *Plan)
      if (Rename.SymbolID == ID)
        return Rename.OutputName;
    return {};
  };
  EXPECT_EQ(OutputFor(900), "fn_10");
  EXPECT_EQ(OutputFor(100), "fn_10_1");
}

TEST(AndroidKernelModuleReleaseNames, EnforcesReservedNameNamespace) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolKind = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 1, 1, true, true},
  };
  const neverc::ReleaseSymbolDescriptor BaseCollision[] = {
      {1, "fn_0", SymbolClass::Undefined, SymbolKind::NoType, 0, 0, 0, 1, 0,
       false},
      {2, "function", SymbolClass::Defined, SymbolKind::Function, 1, 0, 0, 1, 0,
       false},
  };
  auto BasePlan =
      neverc::planAndroidKernelReleaseNames(Sections, BaseCollision);
  if (BasePlan)
    ADD_FAILURE() << "accepted a generated/import base-name collision";
  else
    consumeError(BasePlan.takeError());

  const neverc::ReleaseSymbolDescriptor SuffixCollision[] = {
      {1, "first", SymbolClass::Defined, SymbolKind::Function, 1, 0, 0, 0, 0,
       false},
      {2, "second", SymbolClass::Defined, SymbolKind::Function, 1, 0, 0, 1, 0,
       false},
      {3, "fn_0_1", SymbolClass::Undefined, SymbolKind::NoType, 0, 0, 0, 1, 0,
       false},
  };
  auto SuffixPlan =
      neverc::planAndroidKernelReleaseNames(Sections, SuffixCollision);
  if (SuffixPlan)
    ADD_FAILURE() << "accepted a generated/import alias-suffix collision";
  else
    consumeError(SuffixPlan.takeError());

  const neverc::ReleaseSymbolDescriptor DuplicateImports[] = {
      {1, "same_import", SymbolClass::Undefined, SymbolKind::NoType, 0, 0, 0, 1,
       0, false},
      {2, "same_import", SymbolClass::Undefined, SymbolKind::NoType, 0, 0, 0, 2,
       0, false},
  };
  auto DuplicatePlan =
      neverc::planAndroidKernelReleaseNames({}, DuplicateImports);
  if (!DuplicatePlan)
    FAIL() << toString(DuplicatePlan.takeError()).str().str();
  ASSERT_EQ(DuplicatePlan->size(), 2U);
  EXPECT_EQ((*DuplicatePlan)[0].OutputName, "same_import");
  EXPECT_EQ((*DuplicatePlan)[1].OutputName, "same_import");
}

TEST(AndroidKernelModuleReleaseNames, RejectsDuplicateProducerSymbolIDs) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {42, "first_import", SymbolClass::Undefined,
       neverc::ReleaseSymbolType::NoType, 0, 0, 0, 1, 0, false},
      {42, "second_import", SymbolClass::Undefined,
       neverc::ReleaseSymbolType::NoType, 0, 0, 0, 1, 0, false},
  };

  auto Plan = neverc::planAndroidKernelReleaseNames({}, Symbols);
  if (Plan)
    ADD_FAILURE() << "accepted duplicate producer-local SymbolIDs";
  else
    consumeError(Plan.takeError());
}

TEST(AndroidKernelModuleReleaseNames, ExactStructuralTiesOwnNameMultiset) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolKind = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 1, 1, true, true},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {30, "third_local_name", SymbolClass::Defined, SymbolKind::Function, 1, 0,
       0, 1, 0, false},
      {10, "first_local_name", SymbolClass::Defined, SymbolKind::Function, 1, 0,
       0, 1, 0, false},
      {20, "second_local_name", SymbolClass::Defined, SymbolKind::Function, 1,
       0, 0, 1, 0, false},
      {40, "distinguishable", SymbolClass::Defined, SymbolKind::Function, 1, 0,
       0, 0, 0, false},
  };
  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  std::vector<std::string> NameMultiset;
  for (const neverc::ReleaseSymbolRename &Rename : *Plan)
    NameMultiset.push_back(Rename.OutputName);
  llvm::sort(NameMultiset);
  EXPECT_EQ(NameMultiset,
            (std::vector<std::string>{"fn_0", "fn_0_1", "fn_0_2", "fn_0_3"}));

  auto ExchangeClasses =
      neverc::computeAndroidKernelReleaseNameExchangeClasses(Sections, Symbols);
  if (!ExchangeClasses)
    FAIL() << toString(ExchangeClasses.takeError()).str().str();
  std::vector<size_t> ClassSizes;
  for (const neverc::ReleaseSymbolExchangeClass &Class : *ExchangeClasses)
    ClassSizes.push_back(Class.SymbolIDs.size());
  llvm::sort(ClassSizes);
  EXPECT_EQ(ClassSizes, (std::vector<size_t>{1, 3}));

  const neverc::ReleaseSymbolDescriptor ShuffledIDs[] = {
      {202, "second_local_name", SymbolClass::Defined, SymbolKind::Function, 1,
       0, 0, 1, 0, false},
      {303, "third_local_name", SymbolClass::Defined, SymbolKind::Function, 1,
       0, 0, 1, 0, false},
      {101, "first_local_name", SymbolClass::Defined, SymbolKind::Function, 1,
       0, 0, 1, 0, false},
      {404, "distinguishable", SymbolClass::Defined, SymbolKind::Function, 1, 0,
       0, 0, 0, false},
  };
  auto ShuffledPlan =
      neverc::planAndroidKernelReleaseNames(Sections, ShuffledIDs);
  if (!ShuffledPlan)
    FAIL() << toString(ShuffledPlan.takeError()).str().str();
  std::vector<std::string> ShuffledMultiset;
  for (const neverc::ReleaseSymbolRename &Rename : *ShuffledPlan)
    ShuffledMultiset.push_back(Rename.OutputName);
  llvm::sort(ShuffledMultiset);
  EXPECT_EQ(ShuffledMultiset, NameMultiset);

  const neverc::ReleaseSymbolRename EquivalentOwnership[] = {
      {10, "fn_0_3"},
      {20, "fn_0_1"},
      {30, "fn_0_2"},
      {40, "fn_0"},
  };
  if (Error Err = neverc::auditAndroidKernelReleaseNames(Sections, Symbols,
                                                         EquivalentOwnership))
    FAIL() << toString(std::move(Err)).str().str();

  const neverc::ReleaseSymbolRename WrongMultiset[] = {
      {10, "fn_0_1"},
      {20, "fn_0_1"},
      {30, "fn_0_3"},
      {40, "fn_0"},
  };
  if (Error Err = neverc::auditAndroidKernelReleaseNames(Sections, Symbols,
                                                         WrongMultiset))
    consumeError(std::move(Err));
  else
    ADD_FAILURE() << "accepted the wrong exact-tie name multiset";
}

TEST(AndroidKernelModuleReleaseNames,
     AbsoluteAliasSuffixOwnershipIncludesSerializedType) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolType = neverc::ReleaseSymbolType;
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {10, "function_alias", SymbolClass::Absolute, SymbolType::Function, 0,
       0x2A, 0, 1, 0, false},
      {90, "object_alias", SymbolClass::Absolute, SymbolType::Object, 0, 0x2A,
       0, 1, 0, false},
  };

  auto Plan = neverc::planAndroidKernelReleaseNames({}, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  ASSERT_EQ(Plan->size(), 2U);
  auto OutputFor = [&](uint64_t ID) -> StringRef {
    for (const neverc::ReleaseSymbolRename &Rename : *Plan)
      if (Rename.SymbolID == ID)
        return Rename.OutputName;
    return {};
  };
  EXPECT_EQ(OutputFor(90), "abs_2A");
  EXPECT_EQ(OutputFor(10), "abs_2A_1");

  const neverc::ReleaseSymbolRename CrossTypeSwap[] = {
      {10, "abs_2A"},
      {90, "abs_2A_1"},
  };
  if (Error Err =
          neverc::auditAndroidKernelReleaseNames({}, Symbols, CrossTypeSwap))
    consumeError(std::move(Err));
  else
    ADD_FAILURE() << "allowed absolute suffixes to cross serialized types";
}

TEST(AndroidKernelModuleReleaseNames,
     RejectsUnsupportedAndIncoherentSerializedTypes) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolType = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 1, 0x10, true, false},
  };
  auto ExpectError = [&](ArrayRef<neverc::ReleaseSymbolDescriptor> Symbols,
                         StringRef Description) {
    auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
    if (Plan)
      ADD_FAILURE() << "accepted " << Description.str();
    else
      consumeError(Plan.takeError());

    auto Classes = neverc::computeAndroidKernelReleaseNameExchangeClasses(
        Sections, Symbols);
    if (Classes)
      ADD_FAILURE() << "exchange-class API accepted " << Description.str();
    else
      consumeError(Classes.takeError());
  };

  const neverc::ReleaseSymbolDescriptor InvalidEnum[] = {
      {1, "preserved_invalid", SymbolClass::Absolute,
       static_cast<SymbolType>(0xFF), 0, 0, 0, 1, 0, true},
  };
  ExpectError(InvalidEnum, "an invalid serialized symbol type enum value");

  const neverc::ReleaseSymbolDescriptor TLS[] = {
      {2, "tls_definition", SymbolClass::Defined, SymbolType::TLS, 1, 0, 8, 1,
       0, false},
  };
  ExpectError(TLS, "a retained TLS definition");

  const neverc::ReleaseSymbolDescriptor GNUIndirectFunction[] = {
      {3, "ifunc_alias", SymbolClass::Absolute, SymbolType::GNUIFunc, 0, 7, 0,
       1, 0, false},
  };
  ExpectError(GNUIndirectFunction, "a retained GNU IFUNC");

  const neverc::ReleaseSymbolDescriptor NonEmptyFile[] = {
      {4, "source.c", SymbolClass::Absolute, SymbolType::File, 0, 0, 0, 0, 0,
       false},
  };
  ExpectError(NonEmptyFile, "a retained non-empty FILE symbol");

  const neverc::ReleaseSymbolDescriptor FormatExtension[] = {
      {5, "extension_import", SymbolClass::Undefined,
       SymbolType::FormatExtension, 0, 0, 0, 1, 0, false},
  };
  ExpectError(FormatExtension, "a retained format-extension symbol");

  const neverc::ReleaseSymbolDescriptor UndefinedSection[] = {
      {6, "section_import", SymbolClass::Undefined, SymbolType::Section, 0, 0,
       0, 0, 0, false},
  };
  ExpectError(UndefinedSection, "an undefined SECTION symbol");

  const neverc::ReleaseSymbolDescriptor DefinedSection[] = {
      {7, "named_section", SymbolClass::Defined, SymbolType::Section, 1, 0, 0,
       0, 0, false},
  };
  auto SectionPlan =
      neverc::planAndroidKernelReleaseNames(Sections, DefinedSection);
  if (!SectionPlan)
    FAIL() << toString(SectionPlan.takeError()).str().str();
  ASSERT_EQ(SectionPlan->size(), 1U);
  EXPECT_EQ((*SectionPlan)[0].OutputName, "named_section");

  const neverc::ReleaseSymbolDescriptor Common[] = {
      {8, "", SymbolClass::Common, SymbolType::NoType, 0, 0, 0, 1, 0, false},
  };
  ExpectError(Common, "an unsupported common symbol class");
}

TEST(AndroidKernelModuleReleaseNames,
     IsDeterministicAcrossShuffledHashMapInsertion) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolKind = neverc::ReleaseSymbolType;
  StringMap<neverc::ReleaseSectionDescriptor> FirstInsertion;
  FirstInsertion.try_emplace(
      "text_a", neverc::ReleaseSectionDescriptor{10, 1, 1, 0x10, true, true});
  FirstInsertion.try_emplace(
      "text_b", neverc::ReleaseSectionDescriptor{20, 2, 1, 0x10, true, true});
  FirstInsertion.try_emplace("metadata", neverc::ReleaseSectionDescriptor{
                                             30, 3, 1, 0x10, false, false});

  StringMap<neverc::ReleaseSectionDescriptor> SecondInsertion;
  SecondInsertion.try_emplace("metadata", neverc::ReleaseSectionDescriptor{
                                              30, 3, 1, 0x10, false, false});
  SecondInsertion.try_emplace(
      "text_b", neverc::ReleaseSectionDescriptor{20, 2, 1, 0x10, true, true});
  SecondInsertion.try_emplace(
      "text_a", neverc::ReleaseSectionDescriptor{10, 1, 1, 0x10, true, true});

  SmallVector<neverc::ReleaseSectionDescriptor, 3> FirstSections;
  SmallVector<neverc::ReleaseSectionDescriptor, 3> SecondSections;
  for (const auto &Entry : FirstInsertion)
    FirstSections.push_back(Entry.getValue());
  for (const auto &Entry : SecondInsertion)
    SecondSections.push_back(Entry.getValue());
  std::reverse(SecondSections.begin(), SecondSections.end());

  const neverc::ReleaseSymbolDescriptor FirstSymbols[] = {
      {1, "same_relative_a", SymbolClass::Defined, SymbolKind::Function, 10, 4,
       0, 1, 0, false},
      {2, "same_relative_b", SymbolClass::Defined, SymbolKind::Function, 20, 4,
       0, 1, 0, false},
      {3, "end_a", SymbolClass::Defined, SymbolKind::Function, 10, 0x10, 0, 1,
       0, false},
      {4, "start_b", SymbolClass::Defined, SymbolKind::Function, 20, 0, 0, 1, 0,
       false},
      {5, "metadata", SymbolClass::Defined, SymbolKind::NoType, 30, 2, 0, 0, 0,
       false},
  };
  const neverc::ReleaseSymbolDescriptor SecondSymbols[] = {
      FirstSymbols[4], FirstSymbols[2], FirstSymbols[0],
      FirstSymbols[3], FirstSymbols[1],
  };

  auto FirstPlan =
      neverc::planAndroidKernelReleaseNames(FirstSections, FirstSymbols);
  if (!FirstPlan)
    FAIL() << toString(FirstPlan.takeError()).str().str();
  auto SecondPlan =
      neverc::planAndroidKernelReleaseNames(SecondSections, SecondSymbols);
  if (!SecondPlan)
    FAIL() << toString(SecondPlan.takeError()).str().str();
  ASSERT_EQ(FirstPlan->size(), SecondPlan->size());
  for (size_t I = 0; I != FirstPlan->size(); ++I) {
    EXPECT_EQ((*FirstPlan)[I].SymbolID, (*SecondPlan)[I].SymbolID);
    EXPECT_EQ((*FirstPlan)[I].OutputName, (*SecondPlan)[I].OutputName);
  }
  EXPECT_EQ((*FirstPlan)[0].OutputName, "fn_4");
  EXPECT_EQ((*FirstPlan)[1].OutputName, "fn_14");
  EXPECT_EQ((*FirstPlan)[2].OutputName, "fn_10");
  EXPECT_EQ((*FirstPlan)[3].OutputName, "fn_10_1");
  EXPECT_EQ((*FirstPlan)[4].OutputName, "sym_S3_2");
}

TEST(AndroidKernelModuleReleaseNames,
     OrdersAliasClassesByObservableStructuralKey) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolKind = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 1, 8, true, true},
      {2, 2, 1, 1, false, false},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {1000, "later_binding", SymbolClass::Defined, SymbolKind::Function, 1, 0,
       0, 2, 0, false},
      {10, "later_other", SymbolClass::Defined, SymbolKind::Function, 1, 0, 8,
       1, 1, false},
      {900, "larger_size", SymbolClass::Defined, SymbolKind::Function, 1, 0, 8,
       1, 0, false},
      {800, "smaller_size", SymbolClass::Defined, SymbolKind::Function, 1, 0, 4,
       1, 0, false},
      {600, "nonalloc_other", SymbolClass::Defined, SymbolKind::NoType, 2, 0, 0,
       1, 0, false},
      {700, "nonalloc_object", SymbolClass::Defined, SymbolKind::Object, 2, 0,
       0, 1, 0, false},
  };
  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  auto OutputFor = [&](uint64_t ID) -> StringRef {
    for (const neverc::ReleaseSymbolRename &Rename : *Plan)
      if (Rename.SymbolID == ID)
        return Rename.OutputName;
    return {};
  };
  EXPECT_EQ(OutputFor(800), "fn_0");
  EXPECT_EQ(OutputFor(900), "fn_0_1");
  EXPECT_EQ(OutputFor(10), "fn_0_2");
  EXPECT_EQ(OutputFor(1000), "fn_0_3");
  EXPECT_EQ(OutputFor(600), "sym_S2_0");
  EXPECT_EQ(OutputFor(700), "sym_S2_0_1");

  const neverc::ReleaseSymbolRename WrongClassOwnership[] = {
      {10, "fn_0_2"},   {800, "fn_0_1"},     {900, "fn_0"},
      {1000, "fn_0_3"}, {600, "sym_S2_0_1"}, {700, "sym_S2_0"},
  };
  if (Error Err = neverc::auditAndroidKernelReleaseNames(Sections, Symbols,
                                                         WrongClassOwnership))
    consumeError(std::move(Err));
  else
    ADD_FAILURE() << "allowed names to cross structural alias classes";
}

TEST(AndroidKernelModuleReleaseNames, KeepsEveryExactNameClassByteExact) {
  using SymbolClass = neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;
  using SymbolKind = neverc::ReleaseSymbolType;
  const neverc::ReleaseSectionDescriptor Sections[] = {
      {1, 1, 1, 0x20, true, true},
  };
  const neverc::ReleaseSymbolDescriptor Symbols[] = {
      {1, "weak_import", SymbolClass::Undefined, SymbolKind::NoType, 0, 0, 0, 2,
       0, false},
      {2, "init_module", SymbolClass::Defined, SymbolKind::Function, 1, 0, 0, 1,
       0, false},
      {3, "__typeid__sample_global_addr", SymbolClass::Absolute,
       SymbolKind::NoType, 0, 7, 0, 1, 0, false},
      {4, "__kcfi_typeid_sample", SymbolClass::Defined, SymbolKind::Object, 1,
       8, 4, 1, 0, false},
      {5, "protected_section_symbol", SymbolClass::Defined, SymbolKind::Object,
       1, 0x10, 4, 1, 0, true},
      {6, "", SymbolClass::Defined, SymbolKind::NoType, 1, 0x18, 0, 0, 0,
       false},
  };
  auto Plan = neverc::planAndroidKernelReleaseNames(Sections, Symbols);
  if (!Plan)
    FAIL() << toString(Plan.takeError()).str().str();
  ASSERT_EQ(Plan->size(), std::size(Symbols));
  const StringRef Expected[] = {
      "weak_import",
      "init_module",
      "__typeid__sample_global_addr",
      "__kcfi_typeid_sample",
      "protected_section_symbol",
      "",
  };
  for (size_t I = 0; I != Plan->size(); ++I)
    EXPECT_EQ((*Plan)[I].OutputName, Expected[I]);
}

TEST(DwarfRebaseTest, RecognizesSupportedMachOAndELFSectionNames) {
  EXPECT_EQ(classifyDwarfSection("__debug_aranges"), DwarfSection::Aranges);
  EXPECT_EQ(classifyDwarfSection("__debug_pubnames"), DwarfSection::PubNames);
  EXPECT_EQ(classifyDwarfSection("__debug_gnu_pubt"), DwarfSection::PubTypes);
  EXPECT_EQ(classifyDwarfSection("__debug_str_offs"), DwarfSection::StrOffsets);
  EXPECT_EQ(classifyDwarfSection(".debug_types"), DwarfSection::Types);
  EXPECT_EQ(classifyDwarfSection(".debug_frame"), DwarfSection::Frame);
  EXPECT_EQ(classifyDwarfSection(".debug_rnglists"), DwarfSection::RngLists);
  EXPECT_EQ(classifyDwarfSection(".debug_info.dwo"), DwarfSection::Info);
  EXPECT_EQ(classifyDwarfSection(".debug_str_offsets.dwo"),
            DwarfSection::StrOffsets);
  EXPECT_EQ(classifyDwarfSection("__apple_names"), DwarfSection::Count);
}

TEST(DwarfRebaseTest, IdentifiesEverySectionWhoseBytesAreRewritten) {
  constexpr DwarfSection Rewritten[] = {
      DwarfSection::Info,     DwarfSection::Types,      DwarfSection::Aranges,
      DwarfSection::PubNames, DwarfSection::PubTypes,   DwarfSection::Line,
      DwarfSection::Frame,    DwarfSection::StrOffsets, DwarfSection::Macro,
      DwarfSection::Names,
  };
  for (DwarfSection Section : Rewritten)
    EXPECT_TRUE(dwarfSectionContentsAreRebased(Section));

  constexpr DwarfSection CopiedVerbatim[] = {
      DwarfSection::Abbrev,   DwarfSection::Str,      DwarfSection::LineStr,
      DwarfSection::Ranges,   DwarfSection::RngLists, DwarfSection::Loc,
      DwarfSection::LocLists, DwarfSection::Addr,     DwarfSection::MacInfo,
      DwarfSection::Count,
  };
  for (DwarfSection Section : CopiedVerbatim)
    EXPECT_FALSE(dwarfSectionContentsAreRebased(Section));
}

TEST(DwarfRebaseTest, MissingAbbreviationsStillEnterValidation) {
  PartitionDwarf Part;
  Part.record("__debug_info", 0, 0, 1);
  EXPECT_TRUE(Part.needsRebase());
  const PartitionDwarf Parts[] = {Part};
  char InvalidInfo = 0;
  EXPECT_FALSE(rebaseMergedDwarf(
      ArrayRef<PartitionDwarf>(Parts),
      [&](unsigned) { return MutableArrayRef<char>(&InvalidInfo, 1); },
      /*IsLittleEndian=*/true));
}

TEST(DwarfRebaseTest, TypeUnitsWithoutAbbreviationsEnterValidation) {
  PartitionDwarf Part;
  Part.record(DwarfSection::Types, 0, 0, 1);

  EXPECT_TRUE(Part.needsRebase());
}

TEST(DwarfRebaseTest, RejectsOverflowingContributionBounds) {
  PartitionDwarf Part;
  Part.record("__debug_info", 0, std::numeric_limits<uint64_t>::max(), 2);
  Part.record("__debug_abbrev", 1, 0, 1);
  const PartitionDwarf Parts[] = {Part};
  char Data = 0;

  EXPECT_FALSE(rebaseMergedDwarf(
      ArrayRef<PartitionDwarf>(Parts),
      [&](unsigned) { return MutableArrayRef<char>(&Data, 1); },
      /*IsLittleEndian=*/true));
}

TEST(DwarfRebaseTest, RewritesOffsetsEncodedThroughIndirectForms) {
  std::array<char, 17> Info = {
      0x0d, 0, 0, 0, // unit_length
      4,    0,       // version
      0,    0, 0, 0, // abbreviation offset
      8,             // address size
      1,             // abbreviation code
      0x0e,          // indirect form resolves to DW_FORM_strp
      0,    0, 0, 0, // string offset
  };
  std::array<char, 8> Abbrev = {
      1, 0x11, 0, // code, DW_TAG_compile_unit, no children
      3, 0x16,    // DW_AT_name, DW_FORM_indirect
      0, 0,       // end of attributes
      0,          // end of declarations
  };

  PartitionDwarf Part;
  Part.record("__debug_info", 0, 100, Info.size());
  Part.record("__debug_abbrev", 1, 8, Abbrev.size());
  Part.record("__debug_str", 2, 32, 1);
  DwarfSlices Slices;
  Slices[dwarfSectionIndex(DwarfSection::Info)] = Info;
  Slices[dwarfSectionIndex(DwarfSection::Abbrev)] = Abbrev;

  ASSERT_TRUE(rebasePartitionDwarf(Slices, Part, /*IsLittleEndian=*/true));
  EXPECT_EQ(static_cast<unsigned char>(Info[6]), 8u);
  EXPECT_EQ(static_cast<unsigned char>(Info[13]), 32u);
}

TEST(DwarfRebaseTest, RewritesPreDwarf4DataFormSectionOffsets) {
  std::array<char, 16> Info = {
      0x0c, 0, 0, 0, // unit_length
      3,    0,       // version
      0,    0, 0, 0, // abbreviation offset
      8,             // address size
      1,             // abbreviation code
      0,    0, 0, 0, // DW_AT_stmt_list, encoded as DW_FORM_data4
  };
  std::array<char, 8> Abbrev = {
      1,    0x11, 0, // code, DW_TAG_compile_unit, no children
      0x10, 0x06,    // DW_AT_stmt_list, DW_FORM_data4
      0,    0,       // end of attributes
      0,             // end of declarations
  };

  PartitionDwarf Part;
  Part.record("__debug_info", 0, 100, Info.size());
  Part.record("__debug_abbrev", 1, 8, Abbrev.size());
  Part.record("__debug_line", 2, 64, 1);
  DwarfSlices Slices;
  Slices[dwarfSectionIndex(DwarfSection::Info)] = Info;
  Slices[dwarfSectionIndex(DwarfSection::Abbrev)] = Abbrev;

  ASSERT_TRUE(rebasePartitionDwarf(Slices, Part, /*IsLittleEndian=*/true));
  EXPECT_EQ(static_cast<unsigned char>(Info[12]), 64u);
}

TEST(DwarfRebaseTest, RewritesDwarf4TypeUnitAbbreviationOffsets) {
  std::array<char, 24> Types = {
      0x14, 0, 0, 0,             // unit_length
      4,    0,                   // version
      0,    0, 0, 0,             // abbreviation offset
      8,                         // address size
      1,    2, 3, 4, 5, 6, 7, 8, // type signature
      0x17, 0, 0, 0,             // type DIE offset
      1,                         // abbreviation code
  };
  std::array<char, 6> Abbrev = {
      1, 0x41, 0, // code, DW_TAG_type_unit, no children
      0, 0,       // end of attributes
      0,          // end of declarations
  };

  PartitionDwarf Part;
  Part.record("__debug_types", 0, 96, Types.size());
  Part.record("__debug_abbrev", 1, 32, Abbrev.size());
  DwarfSlices Slices;
  Slices[dwarfSectionIndex(DwarfSection::Types)] = Types;
  Slices[dwarfSectionIndex(DwarfSection::Abbrev)] = Abbrev;

  ASSERT_TRUE(rebasePartitionDwarf(Slices, Part, /*IsLittleEndian=*/true));
  EXPECT_EQ(static_cast<unsigned char>(Types[6]), 32u);
}

TEST(DwarfRebaseTest, RewritesPublicNameAndFrameReferences) {
  std::array<char, 18> PubNames = {
      0x0e, 0, 0, 0, // unit_length
      2,    0,       // version
      0,    0, 0, 0, // compile-unit offset
      4,    0, 0, 0, // compile-unit length
      0,    0, 0, 0, // end of entries
  };
  std::array<char, 16> Frame = {
      4,          0,          0,          0,          // CIE length
      char(0xff), char(0xff), char(0xff), char(0xff), // CIE marker
      4,          0,          0,          0,          // FDE length
      0,          0,          0,          0, // FDE's CIE section offset
  };

  PartitionDwarf Part;
  Part.record("__debug_pubnames", 0, 20, PubNames.size());
  Part.record("__debug_info", 1, 48, 4);
  Part.record("__debug_frame", 2, 80, Frame.size());
  DwarfSlices Slices;
  Slices[dwarfSectionIndex(DwarfSection::PubNames)] = PubNames;
  Slices[dwarfSectionIndex(DwarfSection::Frame)] = Frame;

  ASSERT_TRUE(rebasePartitionDwarf(Slices, Part, /*IsLittleEndian=*/true));
  EXPECT_EQ(static_cast<unsigned char>(PubNames[6]), 48u);
  EXPECT_EQ(static_cast<unsigned char>(Frame[12]), 80u);
}

TEST(DwarfRebaseTest, RejectsTruncatedLebWithoutLosingParserProgress) {
  std::array<char, 12> Info = {
      8,          0, 0, 0, // unit_length
      4,          0,       // version
      0,          0, 0, 0, // abbreviation offset
      8,                   // address size
      char(0x80),          // unterminated abbreviation-code ULEB128
  };
  std::array<char, 1> Abbrev = {0};

  PartitionDwarf Part;
  Part.record("__debug_info", 0, 100, Info.size());
  Part.record("__debug_abbrev", 1, 8, Abbrev.size());
  DwarfSlices Slices;
  Slices[dwarfSectionIndex(DwarfSection::Info)] = Info;
  Slices[dwarfSectionIndex(DwarfSection::Abbrev)] = Abbrev;

  EXPECT_FALSE(rebasePartitionDwarf(Slices, Part, /*IsLittleEndian=*/true));
}

TEST(DwarfRebaseTest, RewritesLengthlessDwarf5MacroUnits) {
  std::array<char, 50> Macro = {
      // DWARF32 unit: version, flags, line offset, define_strp, import, end.
      5,
      0,
      0x02,
      0x10,
      0,
      0,
      0,
      0x05,
      1,
      0x20,
      0,
      0,
      0,
      0x07,
      0x30,
      0,
      0,
      0,
      0,
      // DWARF64 unit. Macro units are delimited by opcode 0, not unit_length.
      5,
      0,
      0x03,
      0x40,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0x06,
      2,
      0x50,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0x07,
      0x60,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
  };

  PartitionDwarf Part;
  Part.record(DwarfSection::Macro, 0, 0x300, Macro.size());
  Part.record(DwarfSection::Line, 1, 0x100, 1);
  Part.record(DwarfSection::Str, 2, 0x200, 1);
  DwarfSlices Slices;
  Slices[dwarfSectionIndex(DwarfSection::Macro)] = Macro;

  ASSERT_TRUE(rebasePartitionDwarf(Slices, Part, /*IsLittleEndian=*/true));
  EXPECT_EQ(static_cast<unsigned char>(Macro[3]), 0x10u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[4]), 0x01u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[9]), 0x20u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[10]), 0x02u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[14]), 0x30u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[15]), 0x03u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[22]), 0x40u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[23]), 0x01u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[32]), 0x50u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[33]), 0x02u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[41]), 0x60u);
  EXPECT_EQ(static_cast<unsigned char>(Macro[42]), 0x03u);
}

// ---------------------------------------------------------------------------
// Helpers: minimal valid object file builders
// ---------------------------------------------------------------------------

namespace {

Options androidKernelReleaseOptions() {
  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.finalizeAndroidKernelModule = true;
  Opts.stripUnneededSymbols = true;
  return Opts;
}

/// Build a minimal valid ELF64LE relocatable object with:
///   - 1 .text section (SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR)
///   - N defined symbols + M undefined symbols
///   - 1 relocation per undefined symbol (R_X86_64_64 → addend 0)
SmallVector<char, 0> buildMinimalELF(ArrayRef<std::string> DefinedSyms,
                                     ArrayRef<std::string> UndefinedSyms,
                                     ArrayRef<uint8_t> TextContent = {0xcc},
                                     bool DefinedAsGlobal = false) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rela = Elf64_Rela;

  // String tables
  SmallVector<char, 256> ShStrTab, SymStrTab;
  ShStrTab.push_back('\0');
  SymStrTab.push_back('\0');

  auto addStr = [](SmallVector<char, 256> &Tab, StringRef S) -> uint32_t {
    uint32_t Off = Tab.size();
    Tab.append(S.begin(), S.end());
    Tab.push_back('\0');
    return Off;
  };

  uint32_t TextNameOff = addStr(ShStrTab, ".text");
  uint32_t SymTabNameOff = addStr(ShStrTab, ".symtab");
  uint32_t StrTabNameOff = addStr(ShStrTab, ".strtab");
  uint32_t ShStrTabNameOff = addStr(ShStrTab, ".shstrtab");
  uint32_t RelaNameOff = addStr(ShStrTab, ".rela.text");

  // Symbols: [0]=null, [1..N]=defined (LOCAL), [N+1..N+M]=undefined (GLOBAL)
  SmallVector<Sym, 16> Syms;
  Sym NullSym;
  memset(&NullSym, 0, sizeof(NullSym));
  Syms.push_back(NullSym);

  SmallVector<Sym, 8> DeferredGlobalDefs;
  for (const auto &Name : DefinedSyms) {
    Sym S;
    memset(&S, 0, sizeof(S));
    S.st_name = addStr(SymStrTab, Name);
    S.st_shndx = 1; // .text
    S.st_value = 0;
    S.st_size = TextContent.size();
    if (DefinedAsGlobal) {
      S.st_info = (STB_GLOBAL << 4) | STT_FUNC;
      DeferredGlobalDefs.push_back(S);
    } else {
      S.st_info = (STB_LOCAL << 4) | STT_FUNC;
      Syms.push_back(S);
    }
  }
  unsigned FirstGlobal = Syms.size();
  Syms.append(DeferredGlobalDefs.begin(), DeferredGlobalDefs.end());
  for (const auto &Name : UndefinedSyms) {
    Sym S;
    memset(&S, 0, sizeof(S));
    S.st_name = addStr(SymStrTab, Name);
    S.st_info = (STB_GLOBAL << 4) | STT_NOTYPE;
    S.st_shndx = SHN_UNDEF;
    S.st_value = 0;
    S.st_size = 0;
    Syms.push_back(S);
  }

  // Relocations: one per undefined symbol, targeting .text
  SmallVector<Rela, 8> Relas;
  for (unsigned i = 0; i < UndefinedSyms.size(); ++i) {
    Rela R;
    R.r_offset = 0;
    R.r_info = ((uint64_t)(FirstGlobal + i) << 32) | R_X86_64_64;
    R.r_addend = 0;
    Relas.push_back(R);
  }

  // Sections: [0]=null, [1]=.text, [2]=.symtab, [3]=.strtab,
  //           [4]=.shstrtab, [5]=.rela.text (if relas)
  bool HasRela = !Relas.empty();
  unsigned NumSections = HasRela ? 6 : 5;

  // Layout
  uint64_t Off = sizeof(Ehdr);
  uint64_t TextOff = Off;
  Off += TextContent.size();
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t SymTabOff = Off;
  Off += Syms.size() * sizeof(Sym);
  uint64_t StrTabOff = Off;
  Off += SymStrTab.size();
  uint64_t ShStrTabOff = Off;
  Off += ShStrTab.size();
  uint64_t RelaOff = Off;
  if (HasRela)
    Off += Relas.size() * sizeof(Rela);
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t ShOff = Off;

  SmallVector<char, 0> Buf(ShOff + NumSections * sizeof(Shdr), 0);

  // ELF header
  auto *H = reinterpret_cast<Ehdr *>(Buf.data());
  memcpy(H->e_ident, ElfMagic, 4);
  H->e_ident[EI_CLASS] = ELFCLASS64;
  H->e_ident[EI_DATA] = ELFDATA2LSB;
  H->e_ident[EI_VERSION] = EV_CURRENT;
  H->e_type = ET_REL;
  H->e_machine = EM_X86_64;
  H->e_version = EV_CURRENT;
  H->e_ehsize = sizeof(Ehdr);
  H->e_shentsize = sizeof(Shdr);
  H->e_shoff = ShOff;
  H->e_shnum = NumSections;
  H->e_shstrndx = 4;

  // Section headers
  auto *Sec = reinterpret_cast<Shdr *>(Buf.data() + ShOff);
  // [0] null — already zeroed
  // [1] .text
  Sec[1].sh_name = TextNameOff;
  Sec[1].sh_type = SHT_PROGBITS;
  Sec[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  Sec[1].sh_offset = TextOff;
  Sec[1].sh_size = TextContent.size();
  Sec[1].sh_addralign = 16;
  // [2] .symtab
  Sec[2].sh_name = SymTabNameOff;
  Sec[2].sh_type = SHT_SYMTAB;
  Sec[2].sh_offset = SymTabOff;
  Sec[2].sh_size = Syms.size() * sizeof(Sym);
  Sec[2].sh_link = 3; // .strtab
  Sec[2].sh_info = FirstGlobal;
  Sec[2].sh_entsize = sizeof(Sym);
  Sec[2].sh_addralign = 8;
  // [3] .strtab
  Sec[3].sh_name = StrTabNameOff;
  Sec[3].sh_type = SHT_STRTAB;
  Sec[3].sh_offset = StrTabOff;
  Sec[3].sh_size = SymStrTab.size();
  Sec[3].sh_addralign = 1;
  // [4] .shstrtab
  Sec[4].sh_name = ShStrTabNameOff;
  Sec[4].sh_type = SHT_STRTAB;
  Sec[4].sh_offset = ShStrTabOff;
  Sec[4].sh_size = ShStrTab.size();
  Sec[4].sh_addralign = 1;
  // [5] .rela.text
  if (HasRela) {
    Sec[5].sh_name = RelaNameOff;
    Sec[5].sh_type = SHT_RELA;
    Sec[5].sh_offset = RelaOff;
    Sec[5].sh_size = Relas.size() * sizeof(Rela);
    Sec[5].sh_link = 2; // .symtab
    Sec[5].sh_info = 1; // applies to .text
    Sec[5].sh_entsize = sizeof(Rela);
    Sec[5].sh_addralign = 8;
  }

  // Write data
  memcpy(Buf.data() + TextOff, TextContent.data(), TextContent.size());
  memcpy(Buf.data() + SymTabOff, Syms.data(), Syms.size() * sizeof(Sym));
  memcpy(Buf.data() + StrTabOff, SymStrTab.data(), SymStrTab.size());
  memcpy(Buf.data() + ShStrTabOff, ShStrTab.data(), ShStrTab.size());
  if (HasRela)
    memcpy(Buf.data() + RelaOff, Relas.data(), Relas.size() * sizeof(Rela));

  return Buf;
}

/// Validate that a buffer looks like a well-formed ELF64LE relocatable object.
/// Uses raw header checks instead of LLVM's ELFObjectFile to avoid RTTI
/// dependencies (LLVM is built with -fno-rtti).
bool isValidELF64LE(ArrayRef<char> Buf) {
  if (Buf.size() < sizeof(ELF::Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<const ELF::Elf64_Ehdr *>(Buf.data());
  return memcmp(H->e_ident, ELF::ElfMagic, 4) == 0 &&
         H->e_ident[ELF::EI_CLASS] == ELF::ELFCLASS64 &&
         H->e_ident[ELF::EI_DATA] == ELF::ELFDATA2LSB &&
         H->e_type == ELF::ET_REL && H->e_shoff > 0 && H->e_shoff < Buf.size();
}

/// Merge helper: returns (success, output_buffer).
std::pair<bool, SmallVector<char, 0>>
mergeELF(ArrayRef<SmallVector<char, 0>> Bufs, Options Opts = {}) {
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  bool OK = mergeELF64LEObjects(Bufs, OS, Opts);
  return {OK, std::move(Out)};
}

// ---------------------------------------------------------------------------
// Semantic-correctness helpers
//
// The fuzz/edge tests above only prove the merger "produces a parseable ELF
// and never crashes".  They would happily pass the real-world bug where every
// merged-section symbol value and relocation offset collapsed to 0 — that
// output is a perfectly valid ELF that loads and then jumps to the wrong
// address.  The builder + raw parser below let tests assert what the merged
// object *means*: each symbol's final st_value, each relocation's r_offset and
// target symbol, and each merged section's size — exactly the invariants that
// bug violated.  The parser uses raw struct reads (no ELFObjectFile) to keep
// the check independent of the same library the merger relies on.
// ---------------------------------------------------------------------------

struct SecSpec {
  std::string Name;
  uint64_t Size;
  uint32_t Align = 16;
  uint32_t Type = ELF::SHT_PROGBITS;
  uint64_t Flags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  uint8_t Fill = 0; // non-zero => fill PROGBITS content with this byte
  int Link = -1; // >=0: sh_link to that 0-based user section (SHF_LINK_ORDER)
  uint64_t Entsize = 0;
};

struct SymSpec {
  std::string Name;
  int SecIdx;     // 0-based index into the SecSpec list; -1 => undefined
  uint64_t Value; // section-relative value for defined symbols
  bool Global = true;
  bool Weak = false;
  std::optional<uint16_t> RawSectionIndex;
  uint8_t Type = ELF::STT_FUNC;
  uint64_t Size = 0;
  uint8_t Other = 0;
};

struct RelSpec {
  int SecIdx;      // 0-based user section the relocation applies to
  uint64_t Offset; // section-relative offset of the relocation site
  std::string
      SymName; // symbol referenced (by name); ignored if TargetSecSym>=0
  uint32_t Type = ELF::R_X86_64_64;
  int64_t Addend = 0;
  // When >= 0, the relocation targets the STT_SECTION symbol of that 0-based
  // section instead of a named symbol — i.e. a "section base + addend"
  // (section-relative) relocation, the kind LLVM emits for local .rodata/jump
  // tables.  The builder auto-creates the section symbol and routes the reloc.
  int TargetSecSym = -1;
};

/// Build an ELF64LE relocatable object with caller-controlled sections,
/// symbols (at known section-relative offsets), and relocations.
SmallVector<char, 0> buildSectionedELF(ArrayRef<SecSpec> Secs,
                                       ArrayRef<SymSpec> Syms,
                                       ArrayRef<RelSpec> Rels,
                                       uint16_t Machine = ELF::EM_X86_64) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rela = Elf64_Rela;

  SmallVector<char, 256> ShStr, SymStr;
  ShStr.push_back('\0');
  SymStr.push_back('\0');
  auto addStr = [](SmallVector<char, 256> &T, StringRef S) -> uint32_t {
    uint32_t Off = T.size();
    T.append(S.begin(), S.end());
    T.push_back('\0');
    return Off;
  };

  unsigned K = Secs.size();

  // Which user sections carry relocations (→ get a .rela.<name> section).
  SmallVector<bool, 8> HasRel(K, false);
  for (auto &R : Rels)
    if (R.SecIdx >= 0 && (unsigned)R.SecIdx < K)
      HasRel[R.SecIdx] = true;

  SmallVector<uint32_t, 8> SecNameOff(K);
  for (unsigned i = 0; i < K; ++i)
    SecNameOff[i] = addStr(ShStr, Secs[i].Name);
  uint32_t SymTabNameOff = addStr(ShStr, ".symtab");
  uint32_t StrTabNameOff = addStr(ShStr, ".strtab");
  uint32_t ShStrTabNameOff = addStr(ShStr, ".shstrtab");
  SmallVector<uint32_t, 8> RelaNameOff(K, 0);
  for (unsigned i = 0; i < K; ++i)
    if (HasRel[i])
      RelaNameOff[i] = addStr(ShStr, std::string(".rela") + Secs[i].Name);

  // Symbols: [0]=null, then locals, then globals (ELF requires this order).
  SmallVector<Sym, 16> OutSyms;
  Sym Null;
  memset(&Null, 0, sizeof(Null));
  OutSyms.push_back(Null);
  StringMap<unsigned> SymIndex;
  auto emitSym = [&](const SymSpec &S) {
    Sym E;
    memset(&E, 0, sizeof(E));
    E.st_name = addStr(SymStr, S.Name);
    if (S.RawSectionIndex) {
      E.st_shndx = *S.RawSectionIndex;
      E.st_value = S.Value;
    } else if (S.SecIdx < 0) {
      E.st_shndx = SHN_UNDEF;
      E.st_value = 0;
    } else {
      E.st_shndx = 1 + S.SecIdx;
      E.st_value = S.Value;
    }
    uint8_t Binding = S.Weak ? STB_WEAK : (S.Global ? STB_GLOBAL : STB_LOCAL);
    E.st_info = (Binding << 4) | S.Type;
    E.st_other = S.Other;
    E.st_size = S.Size;
    SymIndex[S.Name] = OutSyms.size();
    OutSyms.push_back(E);
  };
  // Section symbols (STT_SECTION, empty name, value 0) for any section targeted
  // by a section-relative relocation.  They are local, so emitted before the
  // named locals/globals; record their index for the reloc table below.
  SmallVector<int, 8> SecSymIndex(K, -1);
  for (auto &R : Rels)
    if (R.TargetSecSym >= 0 && (unsigned)R.TargetSecSym < K &&
        SecSymIndex[R.TargetSecSym] < 0) {
      unsigned Si = R.TargetSecSym;
      Sym E;
      memset(&E, 0, sizeof(E));
      E.st_name = 0; // STT_SECTION symbols are nameless
      E.st_shndx = 1 + Si;
      E.st_value = 0;
      E.st_info = (STB_LOCAL << 4) | STT_SECTION;
      SecSymIndex[Si] = (int)OutSyms.size();
      OutSyms.push_back(E);
    }
  for (auto &S : Syms)
    if (!S.Global)
      emitSym(S);
  unsigned FirstGlobal = OutSyms.size();
  for (auto &S : Syms)
    if (S.Global)
      emitSym(S);

  // Per-target-section relocation tables.
  SmallVector<SmallVector<Rela, 4>, 8> RelTab(K);
  for (auto &R : Rels) {
    if (R.SecIdx < 0 || (unsigned)R.SecIdx >= K)
      continue;
    Rela RE;
    RE.r_offset = R.Offset;
    unsigned SymIdx;
    if (R.TargetSecSym >= 0 && (unsigned)R.TargetSecSym < K &&
        SecSymIndex[R.TargetSecSym] >= 0) {
      SymIdx = (unsigned)SecSymIndex[R.TargetSecSym];
    } else {
      auto It = SymIndex.find(R.SymName);
      SymIdx = It != SymIndex.end() ? It->second : 0;
    }
    RE.r_info = ((uint64_t)SymIdx << 32) | R.Type;
    RE.r_addend = R.Addend;
    RelTab[R.SecIdx].push_back(RE);
  }

  // File layout: Ehdr, section contents, symtab, strtab, shstrtab, relas,
  // shdrs.
  uint64_t Off = sizeof(Ehdr);
  SmallVector<uint64_t, 8> SecOff(K, 0);
  for (unsigned i = 0; i < K; ++i) {
    if (Secs[i].Type == SHT_NOBITS) {
      SecOff[i] = Off; // NOBITS occupies no file space
      continue;
    }
    uint32_t A = Secs[i].Align ? Secs[i].Align : 1;
    Off = (Off + A - 1) & ~(uint64_t)(A - 1);
    SecOff[i] = Off;
    Off += Secs[i].Size;
  }
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t SymTabOff = Off;
  Off += OutSyms.size() * sizeof(Sym);
  uint64_t StrTabOff = Off;
  Off += SymStr.size();
  uint64_t ShStrOff = Off;
  Off += ShStr.size();
  SmallVector<uint64_t, 8> RelaOff(K, 0);
  unsigned NumRela = 0;
  for (unsigned i = 0; i < K; ++i)
    if (HasRel[i]) {
      Off = (Off + 7) & ~(uint64_t)7;
      RelaOff[i] = Off;
      Off += RelTab[i].size() * sizeof(Rela);
      NumRela++;
    }
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t ShOff = Off;

  unsigned NumSec = 1 + K + 3 + NumRela; // null + user + symtab/strtab/shstrtab
  unsigned SymTabIdx = 1 + K;
  unsigned StrTabIdx = SymTabIdx + 1;
  unsigned ShStrIdx = StrTabIdx + 1;

  SmallVector<char, 0> Buf(ShOff + NumSec * sizeof(Shdr), 0);

  auto *H = reinterpret_cast<Ehdr *>(Buf.data());
  memcpy(H->e_ident, ElfMagic, 4);
  H->e_ident[EI_CLASS] = ELFCLASS64;
  H->e_ident[EI_DATA] = ELFDATA2LSB;
  H->e_ident[EI_VERSION] = EV_CURRENT;
  H->e_type = ET_REL;
  H->e_machine = Machine;
  H->e_version = EV_CURRENT;
  H->e_ehsize = sizeof(Ehdr);
  H->e_shentsize = sizeof(Shdr);
  H->e_shoff = ShOff;
  H->e_shnum = NumSec;
  H->e_shstrndx = ShStrIdx;

  auto *Sec = reinterpret_cast<Shdr *>(Buf.data() + ShOff);
  for (unsigned i = 0; i < K; ++i) {
    Shdr &S = Sec[1 + i];
    S.sh_name = SecNameOff[i];
    S.sh_type = Secs[i].Type;
    S.sh_flags = Secs[i].Flags;
    S.sh_offset = SecOff[i];
    S.sh_size = Secs[i].Size;
    S.sh_addralign = Secs[i].Align;
    S.sh_entsize = Secs[i].Entsize;
    if (Secs[i].Link >= 0 && (unsigned)Secs[i].Link < K)
      S.sh_link = 1 + Secs[i].Link; // +1 for the leading null section
  }
  {
    Shdr &S = Sec[SymTabIdx];
    S.sh_name = SymTabNameOff;
    S.sh_type = SHT_SYMTAB;
    S.sh_offset = SymTabOff;
    S.sh_size = OutSyms.size() * sizeof(Sym);
    S.sh_link = StrTabIdx;
    S.sh_info = FirstGlobal;
    S.sh_entsize = sizeof(Sym);
    S.sh_addralign = 8;
  }
  {
    Shdr &S = Sec[StrTabIdx];
    S.sh_name = StrTabNameOff;
    S.sh_type = SHT_STRTAB;
    S.sh_offset = StrTabOff;
    S.sh_size = SymStr.size();
    S.sh_addralign = 1;
  }
  {
    Shdr &S = Sec[ShStrIdx];
    S.sh_name = ShStrTabNameOff;
    S.sh_type = SHT_STRTAB;
    S.sh_offset = ShStrOff;
    S.sh_size = ShStr.size();
    S.sh_addralign = 1;
  }
  unsigned RIdx = ShStrIdx + 1;
  for (unsigned i = 0; i < K; ++i) {
    if (!HasRel[i])
      continue;
    Shdr &S = Sec[RIdx++];
    S.sh_name = RelaNameOff[i];
    S.sh_type = SHT_RELA;
    S.sh_offset = RelaOff[i];
    S.sh_size = RelTab[i].size() * sizeof(Rela);
    S.sh_link = SymTabIdx;
    S.sh_info = 1 + i;
    S.sh_entsize = sizeof(Rela);
    S.sh_addralign = 8;
  }

  for (unsigned i = 0; i < K; ++i)
    if (Secs[i].Type != SHT_NOBITS && Secs[i].Fill != 0)
      memset(Buf.data() + SecOff[i], Secs[i].Fill, Secs[i].Size);
  memcpy(Buf.data() + SymTabOff, OutSyms.data(), OutSyms.size() * sizeof(Sym));
  memcpy(Buf.data() + StrTabOff, SymStr.data(), SymStr.size());
  memcpy(Buf.data() + ShStrOff, ShStr.data(), ShStr.size());
  for (unsigned i = 0; i < K; ++i)
    if (HasRel[i])
      memcpy(Buf.data() + RelaOff[i], RelTab[i].data(),
             RelTab[i].size() * sizeof(Rela));

  return Buf;
}

struct ParsedSec {
  std::string Name;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t Align = 0;
  uint64_t Entsize = 0;
  uint32_t Link = 0;
  std::vector<uint8_t> Data; // on-disk bytes (empty for NOBITS)
};
struct ParsedSym {
  std::string Name;
  uint64_t Value = 0;
  uint16_t Shndx = 0;
  uint8_t Bind = 0;
  uint8_t Type = 0;
  uint8_t Other = 0;
  uint64_t Size = 0;
};
struct ParsedRela {
  uint64_t Offset = 0;
  uint32_t Sym = 0;
  uint32_t Type = 0;
  int64_t Addend = 0;
  uint32_t TargetSec = 0; // 1-based section index the relocation applies to
};

/// Minimal raw ELF64LE reader for semantic assertions.
struct ElfView {
  bool Ok = false;
  std::vector<ParsedSec> Secs;
  std::vector<ParsedSym> Syms;
  std::vector<ParsedRela> Relas;

  const ParsedSym *findSym(StringRef N) const {
    for (auto &S : Syms)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
  int findSec(StringRef N) const {
    for (unsigned i = 0; i < Secs.size(); ++i)
      if (Secs[i].Name == N)
        return (int)i;
    return -1;
  }
};

ElfView parseELF(ArrayRef<char> Buf) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rela = Elf64_Rela;

  ElfView V;
  if (Buf.size() < sizeof(Ehdr))
    return V;
  auto *H = reinterpret_cast<const Ehdr *>(Buf.data());
  if (memcmp(H->e_ident, ElfMagic, 4) != 0)
    return V;
  uint64_t ShOff = H->e_shoff;
  unsigned ShNum = H->e_shnum;
  if (ShOff == 0 || ShOff + (uint64_t)ShNum * sizeof(Shdr) > Buf.size())
    return V;
  const Shdr *Secs = reinterpret_cast<const Shdr *>(Buf.data() + ShOff);
  if (H->e_shstrndx >= ShNum)
    return V;
  const Shdr &ShStr = Secs[H->e_shstrndx];
  if (ShStr.sh_offset + ShStr.sh_size > Buf.size())
    return V;
  const char *ShStrData = Buf.data() + ShStr.sh_offset;

  auto nameAt = [](const char *Base, uint64_t Size,
                   uint32_t Off) -> std::string {
    if (Off >= Size)
      return "";
    return std::string(Base + Off);
  };

  for (unsigned i = 0; i < ShNum; ++i) {
    ParsedSec PS;
    PS.Name = nameAt(ShStrData, ShStr.sh_size, Secs[i].sh_name);
    PS.Type = Secs[i].sh_type;
    PS.Flags = Secs[i].sh_flags;
    PS.Size = Secs[i].sh_size;
    PS.Align = Secs[i].sh_addralign;
    PS.Entsize = Secs[i].sh_entsize;
    PS.Link = Secs[i].sh_link;
    if (Secs[i].sh_type != SHT_NOBITS && Secs[i].sh_size > 0 &&
        Secs[i].sh_offset + Secs[i].sh_size <= Buf.size()) {
      const uint8_t *D =
          reinterpret_cast<const uint8_t *>(Buf.data() + Secs[i].sh_offset);
      PS.Data.assign(D, D + Secs[i].sh_size);
    }
    V.Secs.push_back(std::move(PS));
  }

  for (unsigned i = 0; i < ShNum; ++i) {
    if (Secs[i].sh_type != SHT_SYMTAB)
      continue;
    unsigned StrIdx = Secs[i].sh_link;
    if (StrIdx >= ShNum)
      continue;
    const Shdr &Str = Secs[StrIdx];
    if (Str.sh_offset + Str.sh_size > Buf.size())
      continue;
    if (Secs[i].sh_offset + Secs[i].sh_size > Buf.size())
      continue;
    const char *StrData = Buf.data() + Str.sh_offset;
    const Sym *S =
        reinterpret_cast<const Sym *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Sym);
    for (unsigned k = 0; k < N; ++k) {
      ParsedSym PSym;
      PSym.Name = nameAt(StrData, Str.sh_size, S[k].st_name);
      PSym.Value = S[k].st_value;
      PSym.Shndx = S[k].st_shndx;
      PSym.Bind = S[k].st_info >> 4;
      PSym.Type = S[k].st_info & 0xf;
      PSym.Other = S[k].st_other;
      PSym.Size = S[k].st_size;
      V.Syms.push_back(std::move(PSym));
    }
  }

  for (unsigned i = 0; i < ShNum; ++i) {
    if (Secs[i].sh_type != SHT_RELA)
      continue;
    if (Secs[i].sh_offset + Secs[i].sh_size > Buf.size())
      continue;
    const Rela *R =
        reinterpret_cast<const Rela *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Rela);
    for (unsigned k = 0; k < N; ++k) {
      ParsedRela PR;
      PR.Offset = R[k].r_offset;
      PR.Sym = (uint32_t)(R[k].r_info >> 32);
      PR.Type = (uint32_t)(R[k].r_info & 0xffffffff);
      PR.Addend = R[k].r_addend;
      PR.TargetSec = Secs[i].sh_info;
      V.Relas.push_back(std::move(PR));
    }
  }

  V.Ok = true;
  return V;
}

std::optional<SmallVector<uint8_t, 0>>
decompressELFSection(const ParsedSec &Section,
                     DebugCompressionType CompressionType) {
  using Chdr = ELF::Elf64_Chdr;
  if (!(Section.Flags & ELF::SHF_COMPRESSED) ||
      Section.Data.size() < sizeof(Chdr))
    return std::nullopt;

  Chdr Header{};
  memcpy(&Header, Section.Data.data(), sizeof(Header));
  const uint32_t ExpectedType = CompressionType == DebugCompressionType::Zlib
                                    ? ELF::ELFCOMPRESS_ZLIB
                                    : ELF::ELFCOMPRESS_ZSTD;
  if (Header.ch_type != ExpectedType)
    return std::nullopt;

  const compression::Format Format = compression::formatFor(CompressionType);
  ArrayRef<uint8_t> Payload(Section.Data.data() + sizeof(Header),
                            Section.Data.size() - sizeof(Header));
  SmallVector<uint8_t, 0> Decompressed;
  if (Error E = compression::decompress(Format, Payload, Decompressed,
                                        Header.ch_size)) {
    consumeError(std::move(E));
    return std::nullopt;
  }
  if (Decompressed.size() != Header.ch_size)
    return std::nullopt;
  return Decompressed;
}

/// Overwrite a named symbol's st_value in a merged ELF, in place.  Used to
/// *simulate* the historical "offset collapse" corruption so a test can prove
/// the verifier rejects it (the merger no longer produces such output, so the
/// only way to test the rejection path is to inject the bug after the fact).
bool patchSymValue(SmallVectorImpl<char> &Buf, StringRef Name,
                   uint64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  for (unsigned i = 0; i < H->e_shnum; ++i) {
    if (Secs[i].sh_type != SHT_SYMTAB)
      continue;
    if (Secs[i].sh_link >= H->e_shnum)
      return false;
    const char *StrD = Buf.data() + Secs[Secs[i].sh_link].sh_offset;
    auto *Sy = reinterpret_cast<Elf64_Sym *>(Buf.data() + Secs[i].sh_offset);
    unsigned Cnt = Secs[i].sh_size / sizeof(Elf64_Sym);
    for (unsigned k = 0; k < Cnt; ++k)
      if (Name == StrD + Sy[k].st_name) {
        Sy[k].st_value = NewVal;
        return true;
      }
  }
  return false;
}

ELF::Elf64_Shdr *findELFSectionHeader(SmallVectorImpl<char> &Buf,
                                      StringRef Name, unsigned Occurrence = 0) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return nullptr;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff > Buf.size() ||
      (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size() - H->e_shoff ||
      H->e_shstrndx >= H->e_shnum)
    return nullptr;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  const Elf64_Shdr &ShStr = Secs[H->e_shstrndx];
  if (ShStr.sh_offset > Buf.size() ||
      ShStr.sh_size > Buf.size() - ShStr.sh_offset)
    return nullptr;
  StringRef Names(Buf.data() + ShStr.sh_offset, ShStr.sh_size);
  for (unsigned I = 0; I < H->e_shnum; ++I) {
    if (Secs[I].sh_name >= Names.size() ||
        Names.drop_front(Secs[I].sh_name).split('\0').first != Name)
      continue;
    if (Occurrence == 0)
      return &Secs[I];
    --Occurrence;
  }
  return nullptr;
}

bool overwriteELFSectionContents(SmallVectorImpl<char> &Buf, StringRef Name,
                                 StringRef Contents) {
  ELF::Elf64_Shdr *Section = findELFSectionHeader(Buf, Name);
  if (!Section || Section->sh_type == ELF::SHT_NOBITS ||
      Section->sh_offset > Buf.size() ||
      Section->sh_size > Buf.size() - Section->sh_offset ||
      Contents.size() > Section->sh_size)
    return false;
  memset(Buf.data() + Section->sh_offset, 0, Section->sh_size);
  memcpy(Buf.data() + Section->sh_offset, Contents.data(), Contents.size());
  return true;
}

bool patchELFSymbolNameSameLength(SmallVectorImpl<char> &Buf, StringRef From,
                                  StringRef To) {
  using namespace ELF;
  if (From.size() != To.size() || Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Symtab = Sections[I];
    if (Symtab.sh_type != SHT_SYMTAB || Symtab.sh_link >= Header->e_shnum ||
        Symtab.sh_offset > Buf.size() ||
        Symtab.sh_size > Buf.size() - Symtab.sh_offset)
      continue;
    Elf64_Shdr &Strtab = Sections[Symtab.sh_link];
    if (Strtab.sh_offset > Buf.size() ||
        Strtab.sh_size > Buf.size() - Strtab.sh_offset)
      return false;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Symtab.sh_offset);
    const unsigned Count = Symtab.sh_size / sizeof(Elf64_Sym);
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= Strtab.sh_size)
        continue;
      char *Name = Buf.data() + Strtab.sh_offset + Symbols[K].st_name;
      const size_t Available = Strtab.sh_size - Symbols[K].st_name;
      if (strnlen(Name, Available) != From.size() ||
          StringRef(Name, From.size()) != From)
        continue;
      memcpy(Name, To.data(), To.size());
      return true;
    }
  }
  return false;
}

bool swapELFSymbolNameOffsets(SmallVectorImpl<char> &Buf, StringRef First,
                              StringRef Second) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Symtab = Sections[I];
    if (Symtab.sh_type != SHT_SYMTAB || Symtab.sh_link >= Header->e_shnum ||
        Symtab.sh_offset > Buf.size() ||
        Symtab.sh_size > Buf.size() - Symtab.sh_offset)
      continue;
    Elf64_Shdr &Strtab = Sections[Symtab.sh_link];
    if (Strtab.sh_offset > Buf.size() ||
        Strtab.sh_size > Buf.size() - Strtab.sh_offset)
      return false;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Symtab.sh_offset);
    const unsigned Count = Symtab.sh_size / sizeof(Elf64_Sym);
    Elf64_Sym *FirstSymbol = nullptr;
    Elf64_Sym *SecondSymbol = nullptr;
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= Strtab.sh_size)
        continue;
      StringRef Name(Buf.data() + Strtab.sh_offset + Symbols[K].st_name);
      if (Name == First)
        FirstSymbol = &Symbols[K];
      else if (Name == Second)
        SecondSymbol = &Symbols[K];
    }
    if (!FirstSymbol || !SecondSymbol)
      return false;
    std::swap(FirstSymbol->st_name, SecondSymbol->st_name);
    return true;
  }
  return false;
}

/// Point one symbol at an already-serialized string owned by another symbol.
/// This mutates only st_name, so corruption tests can exercise non-canonical
/// spellings of a different length without rebuilding the string table.
bool retargetELFSymbolNameOffset(SmallVectorImpl<char> &Buf, StringRef From,
                                 StringRef Existing) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Symtab = Sections[I];
    if (Symtab.sh_type != SHT_SYMTAB || Symtab.sh_link >= Header->e_shnum ||
        Symtab.sh_offset > Buf.size() ||
        Symtab.sh_size > Buf.size() - Symtab.sh_offset)
      continue;
    Elf64_Shdr &Strtab = Sections[Symtab.sh_link];
    if (Strtab.sh_offset > Buf.size() ||
        Strtab.sh_size > Buf.size() - Strtab.sh_offset)
      return false;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Symtab.sh_offset);
    const unsigned Count = Symtab.sh_size / sizeof(Elf64_Sym);
    Elf64_Sym *Source = nullptr;
    std::optional<uint32_t> ExistingOffset;
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= Strtab.sh_size)
        continue;
      const char *Begin = Buf.data() + Strtab.sh_offset + Symbols[K].st_name;
      const size_t Available = Strtab.sh_size - Symbols[K].st_name;
      const size_t Length = strnlen(Begin, Available);
      if (Length == Available)
        continue;
      StringRef Name(Begin, Length);
      if (Name == From)
        Source = &Symbols[K];
      if (Name == Existing)
        ExistingOffset = Symbols[K].st_name;
    }
    if (!Source || !ExistingOffset)
      return false;
    Source->st_name = *ExistingOffset;
    return true;
  }
  return false;
}

bool convertFirstELFRelaSectionToRel(SmallVectorImpl<char> &Buf) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Section = Sections[I];
    if (Section.sh_type != SHT_RELA || Section.sh_size < sizeof(Elf64_Rela) ||
        Section.sh_offset > Buf.size() ||
        sizeof(Elf64_Rela) > Buf.size() - Section.sh_offset)
      continue;
    Elf64_Rela WithAddend;
    memcpy(&WithAddend, Buf.data() + Section.sh_offset, sizeof(WithAddend));
    Elf64_Rel Implicit{WithAddend.r_offset, WithAddend.r_info};
    memcpy(Buf.data() + Section.sh_offset, &Implicit, sizeof(Implicit));
    Section.sh_type = SHT_REL;
    Section.sh_size = sizeof(Implicit);
    Section.sh_entsize = sizeof(Implicit);
    return true;
  }
  return false;
}

bool patchELFSectionMergeMetadata(SmallVectorImpl<char> &Buf, StringRef Name,
                                  uint64_t Flags, uint64_t Entsize) {
  if (ELF::Elf64_Shdr *Section = findELFSectionHeader(Buf, Name)) {
    Section->sh_flags = Flags;
    Section->sh_entsize = Entsize;
    return true;
  }
  return false;
}

bool patchELFSectionName(SmallVectorImpl<char> &Buf, StringRef From,
                         StringRef To) {
  ELF::Elf64_Shdr *Source = findELFSectionHeader(Buf, From);
  ELF::Elf64_Shdr *Target = findELFSectionHeader(Buf, To);
  if (!Source || !Target)
    return false;
  Source->sh_name = Target->sh_name;
  return true;
}

bool patchELFCompressedSize(SmallVectorImpl<char> &Buf, StringRef Name,
                            uint64_t NewSize) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff > Buf.size() ||
      (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size() - H->e_shoff ||
      H->e_shstrndx >= H->e_shnum)
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  const Elf64_Shdr &ShStr = Secs[H->e_shstrndx];
  if (ShStr.sh_offset > Buf.size() ||
      ShStr.sh_size > Buf.size() - ShStr.sh_offset)
    return false;
  StringRef Names(Buf.data() + ShStr.sh_offset, ShStr.sh_size);
  for (unsigned I = 0; I < H->e_shnum; ++I) {
    if (Secs[I].sh_name >= Names.size() ||
        Names.drop_front(Secs[I].sh_name).split('\0').first != Name)
      continue;
    if (!(Secs[I].sh_flags & SHF_COMPRESSED) ||
        Secs[I].sh_offset > Buf.size() ||
        sizeof(Elf64_Chdr) > Buf.size() - Secs[I].sh_offset)
      return false;
    auto *Header =
        reinterpret_cast<Elf64_Chdr *>(Buf.data() + Secs[I].sh_offset);
    Header->ch_size = NewSize;
    return true;
  }
  return false;
}

bool corruptSymbolContentByte(SmallVectorImpl<char> &Buf, StringRef Name) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  for (unsigned I = 0; I < H->e_shnum; ++I) {
    if (Secs[I].sh_type != SHT_SYMTAB || Secs[I].sh_link >= H->e_shnum ||
        Secs[I].sh_offset + Secs[I].sh_size > Buf.size())
      continue;
    const Elf64_Shdr &StrSec = Secs[Secs[I].sh_link];
    if (StrSec.sh_offset + StrSec.sh_size > Buf.size())
      continue;
    const char *StrData = Buf.data() + StrSec.sh_offset;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Secs[I].sh_offset);
    unsigned Count = Secs[I].sh_size / sizeof(Elf64_Sym);
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= StrSec.sh_size ||
          Name != StrData + Symbols[K].st_name ||
          Symbols[K].st_shndx == SHN_UNDEF || Symbols[K].st_shndx >= H->e_shnum)
        continue;
      const Elf64_Shdr &DataSec = Secs[Symbols[K].st_shndx];
      if (DataSec.sh_type == SHT_NOBITS ||
          Symbols[K].st_value >= DataSec.sh_size)
        return false;
      uint64_t Offset = DataSec.sh_offset + Symbols[K].st_value;
      if (Offset >= Buf.size())
        return false;
      Buf[(size_t)Offset] ^= 0x5a;
      return true;
    }
  }
  return false;
}

/// Overwrite *every* symbol named Name (not just the first) — the faithful
/// shape of the historical bug, which collapsed all symbol values at once.
/// patchSymValue stops at the first match, so it cannot reproduce a collapse of
/// duplicate-named symbols (two file-local statics that share a name); this
/// can.
bool patchAllSymValues(SmallVectorImpl<char> &Buf, StringRef Name,
                       uint64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  bool Any = false;
  for (unsigned i = 0; i < H->e_shnum; ++i) {
    if (Secs[i].sh_type != SHT_SYMTAB)
      continue;
    if (Secs[i].sh_link >= H->e_shnum)
      return false;
    const char *StrD = Buf.data() + Secs[Secs[i].sh_link].sh_offset;
    auto *Sy = reinterpret_cast<Elf64_Sym *>(Buf.data() + Secs[i].sh_offset);
    unsigned Cnt = Secs[i].sh_size / sizeof(Elf64_Sym);
    for (unsigned k = 0; k < Cnt; ++k)
      if (Name == StrD + Sy[k].st_name) {
        Sy[k].st_value = NewVal;
        Any = true;
      }
  }
  return Any;
}

/// Force every relocation's r_offset to NewVal — simulates the reloc half of
/// the offset-collapse bug without touching symbol values.
bool patchAllRelaOffsets(SmallVectorImpl<char> &Buf, uint64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  bool Any = false;
  for (unsigned i = 0; i < H->e_shnum; ++i) {
    if (Secs[i].sh_type != SHT_RELA)
      continue;
    auto *R = reinterpret_cast<Elf64_Rela *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Elf64_Rela);
    for (unsigned k = 0; k < N; ++k) {
      R[k].r_offset = NewVal;
      Any = true;
    }
  }
  return Any;
}

/// Force every relocation's r_addend to NewVal — simulates an addend being
/// corrupted while the site offset stays correct (the reloc points at the
/// right slot but resolves to the wrong target address).
bool patchAllRelaAddends(SmallVectorImpl<char> &Buf, int64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  bool Any = false;
  for (unsigned i = 0; i < H->e_shnum; ++i) {
    if (Secs[i].sh_type != SHT_RELA)
      continue;
    auto *R = reinterpret_cast<Elf64_Rela *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Elf64_Rela);
    for (unsigned k = 0; k < N; ++k) {
      R[k].r_addend = NewVal;
      Any = true;
    }
  }
  return Any;
}

bool patchFirstRelaSymbolIndex(SmallVectorImpl<char> &Buf,
                               uint32_t NewSymbolIndex) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Section = Sections[I];
    if (Section.sh_type != SHT_RELA || Section.sh_offset > Buf.size() ||
        sizeof(Elf64_Rela) > Buf.size() - Section.sh_offset ||
        Section.sh_size < sizeof(Elf64_Rela))
      continue;
    auto *Relocation =
        reinterpret_cast<Elf64_Rela *>(Buf.data() + Section.sh_offset);
    Relocation->r_info =
        (uint64_t(NewSymbolIndex) << 32) | uint32_t(Relocation->r_info);
    return true;
  }
  return false;
}

bool patchELFSymbolInfo(SmallVectorImpl<char> &Buf, StringRef Name,
                        uint8_t NewInfo) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Symtab = Sections[I];
    if (Symtab.sh_type != SHT_SYMTAB || Symtab.sh_link >= Header->e_shnum ||
        Symtab.sh_offset > Buf.size() ||
        Symtab.sh_size > Buf.size() - Symtab.sh_offset)
      continue;
    Elf64_Shdr &Strtab = Sections[Symtab.sh_link];
    if (Strtab.sh_offset > Buf.size() ||
        Strtab.sh_size > Buf.size() - Strtab.sh_offset)
      return false;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Symtab.sh_offset);
    const unsigned Count = Symtab.sh_size / sizeof(Elf64_Sym);
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= Strtab.sh_size)
        continue;
      const char *Begin = Buf.data() + Strtab.sh_offset + Symbols[K].st_name;
      const size_t Available = Strtab.sh_size - Symbols[K].st_name;
      const size_t Length = strnlen(Begin, Available);
      if (Length != Available && StringRef(Begin, Length) == Name) {
        Symbols[K].st_info = NewInfo;
        return true;
      }
    }
  }
  return false;
}

bool patchELFSymbolSectionIndex(SmallVectorImpl<char> &Buf, StringRef Name,
                                uint16_t NewSectionIndex) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Symtab = Sections[I];
    if (Symtab.sh_type != SHT_SYMTAB || Symtab.sh_link >= Header->e_shnum)
      continue;
    Elf64_Shdr &Strtab = Sections[Symtab.sh_link];
    if (Symtab.sh_offset > Buf.size() ||
        Symtab.sh_size > Buf.size() - Symtab.sh_offset ||
        Strtab.sh_offset > Buf.size() ||
        Strtab.sh_size > Buf.size() - Strtab.sh_offset)
      return false;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Symtab.sh_offset);
    const unsigned Count = Symtab.sh_size / sizeof(Elf64_Sym);
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= Strtab.sh_size)
        continue;
      const char *Begin = Buf.data() + Strtab.sh_offset + Symbols[K].st_name;
      const size_t Available = Strtab.sh_size - Symbols[K].st_name;
      const size_t Length = strnlen(Begin, Available);
      if (Length != Available && StringRef(Begin, Length) == Name) {
        Symbols[K].st_shndx = NewSectionIndex;
        return true;
      }
    }
  }
  return false;
}

bool patchELFSymbolNameOffset(SmallVectorImpl<char> &Buf, StringRef Name,
                              uint32_t NewNameOffset) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (Header->e_shoff > Buf.size() ||
      (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr) >
          Buf.size() - Header->e_shoff)
    return false;
  auto *Sections = reinterpret_cast<Elf64_Shdr *>(Buf.data() + Header->e_shoff);
  for (unsigned I = 0; I < Header->e_shnum; ++I) {
    Elf64_Shdr &Symtab = Sections[I];
    if (Symtab.sh_type != SHT_SYMTAB || Symtab.sh_link >= Header->e_shnum)
      continue;
    Elf64_Shdr &Strtab = Sections[Symtab.sh_link];
    if (Symtab.sh_offset > Buf.size() ||
        Symtab.sh_size > Buf.size() - Symtab.sh_offset ||
        Strtab.sh_offset > Buf.size() ||
        Strtab.sh_size > Buf.size() - Strtab.sh_offset)
      return false;
    auto *Symbols =
        reinterpret_cast<Elf64_Sym *>(Buf.data() + Symtab.sh_offset);
    const unsigned Count = Symtab.sh_size / sizeof(Elf64_Sym);
    for (unsigned K = 0; K < Count; ++K) {
      if (Symbols[K].st_name >= Strtab.sh_size)
        continue;
      const char *Begin = Buf.data() + Strtab.sh_offset + Symbols[K].st_name;
      const size_t Available = Strtab.sh_size - Symbols[K].st_name;
      const size_t Length = strnlen(Begin, Available);
      if (Length != Available && StringRef(Begin, Length) == Name) {
        Symbols[K].st_name = NewNameOffset;
        return true;
      }
    }
  }
  return false;
}

// Rewrite the sh_info (first-global boundary) of the first SHT_SYMTAB section
// in an ELF, to exercise the verifier's symbol-ordering invariant.
bool patchElfSymtabShInfo(SmallVectorImpl<char> &Buf, uint32_t NewInfo) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  for (unsigned i = 0; i < H->e_shnum; ++i)
    if (Secs[i].sh_type == SHT_SYMTAB) {
      Secs[i].sh_info = NewInfo;
      return true;
    }
  return false;
}

// ---------------------------------------------------------------------------
// COFF semantic helpers.  coff_symbol16 is an 18-byte on-disk record but the
// C++ struct can be padded, so build/parse with explicit little-endian byte
// I/O (the merger itself writes symbols as raw 18-byte records for the same
// reason).
// ---------------------------------------------------------------------------

void putU16(SmallVectorImpl<char> &B, uint16_t V) {
  B.push_back((char)(V & 0xff));
  B.push_back((char)((V >> 8) & 0xff));
}
void putU32(SmallVectorImpl<char> &B, uint32_t V) {
  for (int i = 0; i < 4; ++i)
    B.push_back((char)((V >> (8 * i)) & 0xff));
}
uint16_t getU16(const char *P) {
  return (uint16_t)((uint8_t)P[0] | ((uint8_t)P[1] << 8));
}
uint32_t getU32(const char *P) {
  return (uint32_t)((uint8_t)P[0]) | ((uint32_t)(uint8_t)P[1] << 8) |
         ((uint32_t)(uint8_t)P[2] << 16) | ((uint32_t)(uint8_t)P[3] << 24);
}

struct CoffSecSpec {
  std::string Name; // <= 8 chars for these tests
  uint32_t Size;
  uint32_t Characteristics;
  uint8_t Fill = 0; // non-zero => fill non-BSS content with this byte
};
struct CoffSymSpec {
  std::string Name; // <= 8 chars
  uint32_t Value;
  int16_t SectionNumber; // 0 = undefined, 1-based otherwise
  uint8_t StorageClass;
  // >=0 marks a WeakExternal symbol that carries one coff_aux_weak_external
  // record whose TagIndex names Syms[WeakDefTag]'s on-disk slot.  -1 (the
  // default) is an ordinary symbol with no aux, so existing call sites keep
  // emitting exactly one slot per symbol and stay byte-identical.
  int WeakDefTag = -1;
};
struct CoffRelSpec {
  int SecIdx;
  uint32_t VA;
  std::string SymName;
  uint16_t Type;
};

SmallVector<char, 0> buildCOFF(uint16_t Machine, ArrayRef<CoffSecSpec> Secs,
                               ArrayRef<CoffSymSpec> Syms,
                               ArrayRef<CoffRelSpec> Rels) {
  using namespace COFF;
  unsigned N = Secs.size();
  unsigned M = Syms.size();

  // On-disk symbol slots: a weak external occupies two (itself + one aux
  // record), every other symbol occupies one.  Relocations and weak-aux
  // TagIndex fields reference these slots, not the CoffSymSpec array index, so
  // map names to slots.  With no weak externals SlotOf[i]==i, so ordinary
  // call sites are unchanged.
  SmallVector<unsigned, 16> SlotOf(M);
  unsigned TotalSlots = 0;
  for (unsigned i = 0; i < M; ++i) {
    SlotOf[i] = TotalSlots;
    TotalSlots += (Syms[i].WeakDefTag >= 0) ? 2 : 1;
  }

  StringMap<unsigned> SymIndex;
  for (unsigned i = 0; i < M; ++i)
    SymIndex[Syms[i].Name] = SlotOf[i];

  SmallVector<SmallVector<CoffRelSpec, 4>, 8> RelTab(N);
  for (auto &R : Rels)
    if (R.SecIdx >= 0 && (unsigned)R.SecIdx < N)
      RelTab[R.SecIdx].push_back(R);

  // On-disk COFF record sizes are fixed by the spec (header 20, section header
  // 40, symbol 18, relocation 10); use the literals, not sizeof, so struct
  // padding cannot skew the layout.
  uint32_t Off = 20 + N * 40;
  SmallVector<uint32_t, 8> DataPtr(N, 0), RelPtr(N, 0);
  for (unsigned i = 0; i < N; ++i) {
    bool IsBSS =
        (Secs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    if (!IsBSS && Secs[i].Size > 0) {
      DataPtr[i] = Off;
      Off += Secs[i].Size;
    }
  }
  for (unsigned i = 0; i < N; ++i)
    if (!RelTab[i].empty()) {
      RelPtr[i] = Off;
      Off += RelTab[i].size() * 10; // coff_relocation is 10 bytes on disk
    }
  uint32_t SymPtr = Off;
  Off += TotalSlots * 18;
  // String table (just the mandatory 4-byte length).

  SmallVector<char, 0> Buf;
  putU16(Buf, Machine);
  putU16(Buf, (uint16_t)N);
  putU32(Buf, 0);          // TimeDateStamp
  putU32(Buf, SymPtr);     // PointerToSymbolTable
  putU32(Buf, TotalSlots); // NumberOfSymbols (includes aux records)
  putU16(Buf, 0);          // SizeOfOptionalHeader
  putU16(Buf, 0);          // Characteristics

  for (unsigned i = 0; i < N; ++i) {
    char Name[8] = {0};
    memcpy(Name, Secs[i].Name.data(), std::min<size_t>(Secs[i].Name.size(), 8));
    Buf.append(Name, Name + 8);
    bool IsBSS =
        (Secs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    putU32(Buf, 0);                          // VirtualSize
    putU32(Buf, 0);                          // VirtualAddress
    putU32(Buf, Secs[i].Size);               // SizeOfRawData
    putU32(Buf, IsBSS ? 0 : DataPtr[i]);     // PointerToRawData
    putU32(Buf, RelPtr[i]);                  // PointerToRelocations
    putU32(Buf, 0);                          // PointerToLinenumbers
    putU16(Buf, (uint16_t)RelTab[i].size()); // NumberOfRelocations
    putU16(Buf, 0);                          // NumberOfLinenumbers
    putU32(Buf, Secs[i].Characteristics);
  }

  for (unsigned i = 0; i < N; ++i) {
    bool IsBSS =
        (Secs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    if (!IsBSS && Secs[i].Size > 0)
      Buf.append(Secs[i].Size, (char)Secs[i].Fill);
  }
  for (unsigned i = 0; i < N; ++i)
    for (auto &R : RelTab[i]) {
      auto It = SymIndex.find(R.SymName);
      uint32_t SymIdx = It != SymIndex.end() ? It->second : 0;
      putU32(Buf, R.VA);
      putU32(Buf, SymIdx);
      putU16(Buf, R.Type);
    }

  for (unsigned i = 0; i < M; ++i) {
    const auto &S = Syms[i];
    char Name[8] = {0};
    memcpy(Name, S.Name.data(), std::min<size_t>(S.Name.size(), 8));
    Buf.append(Name, Name + 8);
    putU32(Buf, S.Value);
    putU16(Buf, (uint16_t)S.SectionNumber);
    putU16(Buf, 0); // Type
    Buf.push_back((char)S.StorageClass);
    bool IsWeak = S.WeakDefTag >= 0;
    Buf.push_back(IsWeak ? 1 : 0); // NumberOfAuxSymbols
    if (IsWeak) {
      // coff_aux_weak_external (18 bytes): TagIndex(4), Characteristics(4),
      // Unused(10).  TagIndex is the default definition's on-disk slot.
      uint32_t Tag =
          (unsigned)S.WeakDefTag < M ? SlotOf[(unsigned)S.WeakDefTag] : 0u;
      putU32(Buf, Tag);
      putU32(Buf, (uint32_t)IMAGE_WEAK_EXTERN_SEARCH_ALIAS);
      Buf.append(10, (char)0);
    }
  }

  putU32(Buf, 4); // empty string table (length field counts itself)
  return Buf;
}

struct CoffParsedSym {
  std::string Name;
  uint32_t Value = 0;
  int16_t SectionNumber = 0;
  uint8_t StorageClass = 0;
};
struct CoffParsedRel {
  uint32_t VA = 0;
  uint32_t SymIdx = 0;
  uint16_t Type = 0;
  unsigned SecIdx = 0; // 0-based section the relocation belongs to
};
struct CoffView {
  bool Ok = false;
  std::vector<std::string> SecNames;
  std::vector<uint32_t> SecSizes;
  std::vector<CoffParsedSym> Syms;
  std::vector<CoffParsedRel> Rels;

  const CoffParsedSym *findSym(StringRef N) const {
    for (auto &S : Syms)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
};

CoffView parseCOFF(ArrayRef<char> Buf) {
  CoffView V;
  if (Buf.size() < 20)
    return V;
  const char *P = Buf.data();
  unsigned N = getU16(P + 2);
  uint32_t SymPtr = getU32(P + 8);
  uint32_t NumSym = getU32(P + 12);

  uint32_t SecHdrOff = 20;
  if (SecHdrOff + (uint64_t)N * 40 > Buf.size())
    return V;

  const char *StrTab = nullptr;
  uint32_t StrTabSize = 0;
  uint32_t StrTabOff = SymPtr + NumSym * 18;
  if (SymPtr != 0 && StrTabOff + 4 <= Buf.size()) {
    StrTab = Buf.data() + StrTabOff;
    StrTabSize = getU32(StrTab);
  }
  auto resolveName = [&](const char *Field) -> std::string {
    if (getU32(Field) == 0) {
      uint32_t SOff = getU32(Field + 4);
      if (StrTab && SOff < StrTabSize)
        return std::string(StrTab + SOff);
      return "";
    }
    char Tmp[9] = {0};
    memcpy(Tmp, Field, 8);
    return std::string(Tmp);
  };

  struct SecInfo {
    uint32_t RelPtr, NRel;
  };
  std::vector<SecInfo> SI(N);
  for (unsigned i = 0; i < N; ++i) {
    const char *H = Buf.data() + SecHdrOff + i * 40;
    V.SecNames.push_back(resolveName(H));
    V.SecSizes.push_back(getU32(H + 16)); // SizeOfRawData
    SI[i] = {getU32(H + 24), getU16(H + 32)};
  }

  if (SymPtr != 0 && SymPtr + (uint64_t)NumSym * 18 <= Buf.size()) {
    unsigned k = 0;
    while (k < NumSym) {
      const char *S = Buf.data() + SymPtr + k * 18;
      CoffParsedSym PS;
      PS.Name = resolveName(S);
      PS.Value = getU32(S + 8);
      PS.SectionNumber = (int16_t)getU16(S + 12);
      PS.StorageClass = (uint8_t)S[16];
      uint8_t NAux = (uint8_t)S[17];
      V.Syms.push_back(std::move(PS));
      k += 1 + NAux; // aux records keep their absolute index slots
      for (uint8_t a = 0; a < NAux; ++a)
        V.Syms.push_back(CoffParsedSym{}); // placeholder to preserve indices
    }
  }

  for (unsigned i = 0; i < N; ++i) {
    if (SI[i].NRel == 0 || SI[i].RelPtr == 0)
      continue;
    if (SI[i].RelPtr + (uint64_t)SI[i].NRel * 10 > Buf.size())
      continue;
    for (unsigned r = 0; r < SI[i].NRel; ++r) {
      const char *R = Buf.data() + SI[i].RelPtr + r * 10;
      CoffParsedRel PR;
      PR.VA = getU32(R);
      PR.SymIdx = getU32(R + 4);
      PR.Type = getU16(R + 8);
      PR.SecIdx = i;
      V.Rels.push_back(PR);
    }
  }

  V.Ok = true;
  return V;
}

/// Overwrite a named COFF symbol's Value in place — the COFF analogue of
/// patchSymValue, used to simulate offset-collapse corruption for the verifier.
bool patchCoffSymValue(SmallVectorImpl<char> &Buf, StringRef Name,
                       uint32_t NewVal) {
  if (Buf.size() < 20)
    return false;
  uint32_t SymOff = getU32(Buf.data() + 8);
  uint32_t NSym = getU32(Buf.data() + 12);
  if (SymOff == 0 || SymOff + (uint64_t)NSym * 18 > Buf.size())
    return false;
  uint32_t StrOff = SymOff + NSym * 18;
  const char *StrTab =
      (StrOff + 4 <= Buf.size()) ? Buf.data() + StrOff : nullptr;
  uint32_t StrSize = StrTab ? getU32(StrTab) : 0;
  unsigned k = 0;
  while (k < NSym) {
    char *S = Buf.data() + SymOff + k * 18;
    std::string Nm;
    if (getU32(S) == 0) {
      uint32_t O = getU32(S + 4);
      if (StrTab && O < StrSize)
        Nm = std::string(StrTab + O);
    } else {
      char T[9] = {0};
      memcpy(T, S, 8);
      Nm = std::string(T);
    }
    uint8_t NAux = (uint8_t)S[17];
    if (Name == Nm) {
      S[8] = (char)(NewVal & 0xff);
      S[9] = (char)((NewVal >> 8) & 0xff);
      S[10] = (char)((NewVal >> 16) & 0xff);
      S[11] = (char)((NewVal >> 24) & 0xff);
      return true;
    }
    k += 1u + NAux;
  }
  return false;
}

/// Overwrite *every* COFF symbol named Name (patchCoffSymValue stops at the
/// first), so a collapse of duplicate-named statics can be simulated.
bool patchAllCoffSymValues(SmallVectorImpl<char> &Buf, StringRef Name,
                           uint32_t NewVal) {
  if (Buf.size() < 20)
    return false;
  uint32_t SymOff = getU32(Buf.data() + 8);
  uint32_t NSym = getU32(Buf.data() + 12);
  if (SymOff == 0 || SymOff + (uint64_t)NSym * 18 > Buf.size())
    return false;
  uint32_t StrOff = SymOff + NSym * 18;
  const char *StrTab =
      (StrOff + 4 <= Buf.size()) ? Buf.data() + StrOff : nullptr;
  uint32_t StrSize = StrTab ? getU32(StrTab) : 0;
  unsigned k = 0;
  bool Any = false;
  while (k < NSym) {
    char *S = Buf.data() + SymOff + k * 18;
    std::string Nm;
    if (getU32(S) == 0) {
      uint32_t O = getU32(S + 4);
      if (StrTab && O < StrSize)
        Nm = std::string(StrTab + O);
    } else {
      char T[9] = {0};
      memcpy(T, S, 8);
      Nm = std::string(T);
    }
    uint8_t NAux = (uint8_t)S[17];
    if (Name == Nm) {
      S[8] = (char)(NewVal & 0xff);
      S[9] = (char)((NewVal >> 8) & 0xff);
      S[10] = (char)((NewVal >> 16) & 0xff);
      S[11] = (char)((NewVal >> 24) & 0xff);
      Any = true;
    }
    k += 1u + NAux;
  }
  return Any;
}

/// Force every COFF relocation's VirtualAddress to NewVal — simulates the
/// relocation half of the offset-collapse bug for the COFF verifier.
bool patchAllCoffRelocVAs(SmallVectorImpl<char> &Buf, uint32_t NewVal) {
  if (Buf.size() < 20)
    return false;
  unsigned N = getU16(Buf.data() + 2);
  uint16_t OptSize = getU16(Buf.data() + 16);
  uint64_t SecBase = 20ull + OptSize;
  if (SecBase + (uint64_t)N * 40 > Buf.size())
    return false;
  bool Any = false;
  for (unsigned i = 0; i < N; ++i) {
    const char *H = Buf.data() + SecBase + i * 40;
    uint32_t RelPtr = getU32(H + 24);
    uint16_t NRel = getU16(H + 32);
    if (RelPtr == 0 || NRel == 0)
      continue;
    if ((uint64_t)RelPtr + (uint64_t)NRel * 10 > Buf.size())
      continue;
    for (unsigned r = 0; r < NRel; ++r) {
      char *R = Buf.data() + RelPtr + r * 10;
      R[0] = (char)(NewVal & 0xff);
      R[1] = (char)((NewVal >> 8) & 0xff);
      R[2] = (char)((NewVal >> 16) & 0xff);
      R[3] = (char)((NewVal >> 24) & 0xff);
      Any = true;
    }
  }
  return Any;
}

// ---------------------------------------------------------------------------
// Mach-O semantic helpers.  The Mach-O structs in BinaryFormat/MachO.h match
// the on-disk layout, and host + target are little-endian here, so the merger
// (and this builder) write them directly.
// ---------------------------------------------------------------------------

struct MachoSecSpec {
  std::string Seg;  // e.g. "__TEXT"
  std::string Sect; // e.g. "__text"
  uint64_t Size;
  uint32_t AlignExp; // power-of-two exponent (4 => 16-byte)
  uint32_t Flags;
  uint8_t Fill = 0; // non-zero => fill section content with this byte
};
struct MachoSymSpec {
  std::string Name;
  uint8_t Type;   // e.g. N_SECT | N_EXT
  uint8_t Sect;   // 1-based section index, 0 = none
  uint64_t Value; // section-relative offset for defined symbols
  uint16_t Desc;
};
struct MachoRelSpec {
  int SecIdx;          // 0-based section the relocation applies to
  uint32_t Address;    // section-relative offset of the relocation site
  std::string SymName; // target symbol (extern relocation); ignored if !Extern
  uint8_t Type;
  uint8_t Length;     // log2 byte size (2 => 4 bytes, 3 => 8 bytes)
  bool Extern = true; // false => section-relative (non-extern) relocation
  int TargetSec = -1; // 0-based target section when !Extern (r_symbolnum=sec+1)
};

SmallVector<char, 0> buildMachO(uint32_t CpuType, uint32_t CpuSubType,
                                ArrayRef<MachoSecSpec> Secs,
                                ArrayRef<MachoSymSpec> Syms,
                                ArrayRef<MachoRelSpec> Rels = {}) {
  namespace MO = llvm::MachO;
  unsigned N = Secs.size();
  uint32_t SegCmdSize =
      sizeof(MO::segment_command_64) + N * sizeof(MO::section_64);
  uint32_t SymCmdSize = sizeof(MO::symtab_command);
  uint32_t SizeOfCmds = SegCmdSize + SymCmdSize;
  uint64_t DataStart = sizeof(MO::mach_header_64) + SizeOfCmds;

  uint64_t Off = DataStart;
  SmallVector<uint64_t, 8> SecOff(N), SecAddr(N);
  for (unsigned i = 0; i < N; ++i) {
    uint64_t Align = 1ULL << std::min(Secs[i].AlignExp, 20u);
    Off = (Off + Align - 1) & ~(Align - 1);
    SecOff[i] = Off;
    SecAddr[i] = Off - DataStart;
    Off += Secs[i].Size;
  }
  uint64_t FileSizeSecs = Off - DataStart;

  // Per-section relocation tables (8 bytes each), grouped by section and laid
  // out after section content (link-edit data, outside the segment vmsize).
  SmallVector<SmallVector<MachoRelSpec, 4>, 8> RelBySec(N);
  for (auto &R : Rels)
    if (R.SecIdx >= 0 && (unsigned)R.SecIdx < N)
      RelBySec[R.SecIdx].push_back(R);
  SmallVector<uint32_t, 8> RelOff(N, 0);
  for (unsigned i = 0; i < N; ++i)
    if (!RelBySec[i].empty()) {
      Off = (Off + 3) & ~(uint64_t)3;
      RelOff[i] = (uint32_t)Off;
      Off += RelBySec[i].size() * 8;
    }

  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t SymOff = Off;

  SmallVector<char, 0> StrTab;
  StrTab.push_back('\0');
  SmallVector<MO::nlist_64, 16> NList;
  for (auto &S : Syms) {
    MO::nlist_64 NL;
    memset(&NL, 0, sizeof(NL));
    NL.n_strx = StrTab.size();
    StrTab.append(S.Name.begin(), S.Name.end());
    StrTab.push_back('\0');
    NL.n_type = S.Type;
    NL.n_sect = S.Sect;
    NL.n_desc = S.Desc;
    NL.n_value =
        (S.Sect >= 1 && S.Sect <= N) ? SecAddr[S.Sect - 1] + S.Value : S.Value;
    NList.push_back(NL);
  }
  Off += NList.size() * sizeof(MO::nlist_64);
  uint64_t StrOff = Off;
  Off += StrTab.size();

  SmallVector<char, 0> Buf;
  Buf.resize(Off, 0);

  // Section content (so the verifier's content anchor is meaningful).
  for (unsigned i = 0; i < N; ++i)
    if (Secs[i].Fill != 0)
      memset(Buf.data() + SecOff[i], Secs[i].Fill, Secs[i].Size);

  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  MH->magic = MO::MH_MAGIC_64;
  MH->cputype = CpuType;
  MH->cpusubtype = CpuSubType;
  MH->filetype = MO::MH_OBJECT;
  MH->ncmds = 2;
  MH->sizeofcmds = SizeOfCmds;
  MH->flags = MO::MH_SUBSECTIONS_VIA_SYMBOLS;
  MH->reserved = 0;

  char *Cmd = Buf.data() + sizeof(MO::mach_header_64);
  auto *Seg = reinterpret_cast<MO::segment_command_64 *>(Cmd);
  memset(Seg, 0, sizeof(MO::segment_command_64));
  Seg->cmd = MO::LC_SEGMENT_64;
  Seg->cmdsize = SegCmdSize;
  Seg->vmaddr = 0;
  Seg->vmsize = FileSizeSecs;
  Seg->fileoff = DataStart;
  Seg->filesize = FileSizeSecs;
  Seg->maxprot = 7;
  Seg->initprot = 7;
  Seg->nsects = N;
  Seg->flags = 0;

  auto *SH =
      reinterpret_cast<MO::section_64 *>(Cmd + sizeof(MO::segment_command_64));
  for (unsigned i = 0; i < N; ++i) {
    memset(&SH[i], 0, sizeof(MO::section_64));
    memcpy(SH[i].sectname, Secs[i].Sect.data(),
           std::min<size_t>(Secs[i].Sect.size(), 16));
    memcpy(SH[i].segname, Secs[i].Seg.data(),
           std::min<size_t>(Secs[i].Seg.size(), 16));
    SH[i].addr = SecAddr[i];
    SH[i].size = Secs[i].Size;
    SH[i].offset = (uint32_t)SecOff[i];
    SH[i].align = Secs[i].AlignExp;
    SH[i].reloff = RelOff[i];
    SH[i].nreloc = (uint32_t)RelBySec[i].size();
    SH[i].flags = Secs[i].Flags;
  }

  Cmd += SegCmdSize;
  auto *SymCmd = reinterpret_cast<MO::symtab_command *>(Cmd);
  memset(SymCmd, 0, sizeof(MO::symtab_command));
  SymCmd->cmd = MO::LC_SYMTAB;
  SymCmd->cmdsize = SymCmdSize;
  SymCmd->symoff = (uint32_t)SymOff;
  SymCmd->nsyms = NList.size();
  SymCmd->stroff = (uint32_t)StrOff;
  SymCmd->strsize = (uint32_t)StrTab.size();

  // Relocation entries (non-scattered: r_address i32, then packed word
  // symbolnum:24, pcrel:1, length:2, extern:1, type:4 — always extern here).
  {
    StringMap<unsigned> SymIdx;
    for (unsigned i = 0; i < Syms.size(); ++i)
      SymIdx[Syms[i].Name] = i;
    for (unsigned i = 0; i < N; ++i)
      for (unsigned r = 0; r < RelBySec[i].size(); ++r) {
        const MachoRelSpec &R = RelBySec[i][r];
        char *P = Buf.data() + RelOff[i] + r * 8;
        uint32_t Addr = R.Address;
        memcpy(P, &Addr, 4);
        uint32_t SymOrSec, ExtBit;
        if (R.Extern) {
          unsigned Sym = 0;
          auto It = SymIdx.find(R.SymName);
          if (It != SymIdx.end())
            Sym = It->second;
          SymOrSec = Sym & 0xFFFFFFu;
          ExtBit = 1u << 27;
        } else {
          // Non-extern: r_symbolnum is a 1-based section number, extern bit 0.
          SymOrSec =
              (uint32_t)((R.TargetSec >= 0 ? R.TargetSec + 1 : 0) & 0xFFFFFFu);
          ExtBit = 0u;
        }
        uint32_t W = SymOrSec | (((uint32_t)R.Length & 0x3u) << 25) | ExtBit |
                     ((uint32_t)R.Type << 28);
        memcpy(P + 4, &W, 4);
      }
  }

  if (!NList.empty())
    memcpy(Buf.data() + SymOff, NList.data(),
           NList.size() * sizeof(MO::nlist_64));
  memcpy(Buf.data() + StrOff, StrTab.data(), StrTab.size());
  return Buf;
}

struct MachoParsedSec {
  std::string Seg, Sect;
  uint64_t Addr = 0;
  uint64_t Size = 0;
};
struct MachoParsedSym {
  std::string Name;
  uint8_t Sect = 0;
  uint64_t Value = 0;
};
struct MachoView {
  bool Ok = false;
  std::vector<MachoParsedSec> Secs;
  std::vector<MachoParsedSym> Syms;

  const MachoParsedSym *findSym(StringRef N) const {
    for (auto &S : Syms)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
  const MachoParsedSec *findSec(StringRef Sg, StringRef St) const {
    for (auto &S : Secs)
      if (S.Seg == Sg && S.Sect == St)
        return &S;
    return nullptr;
  }
};

MachoView parseMachO(ArrayRef<char> Buf) {
  namespace MO = llvm::MachO;
  MachoView V;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return V;
  auto *MH = reinterpret_cast<const MO::mach_header_64 *>(Buf.data());
  if (MH->magic != MO::MH_MAGIC_64)
    return V;

  auto cstr16 = [](const char *P) -> std::string {
    char Tmp[17] = {0};
    memcpy(Tmp, P, 16);
    return std::string(Tmp);
  };

  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return V;
    auto *LC = reinterpret_cast<const MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SEGMENT_64) {
      auto *Seg =
          reinterpret_cast<const MO::segment_command_64 *>(Buf.data() + Cmd);
      const char *SP = Buf.data() + Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg->nsects; ++i) {
        auto *S = reinterpret_cast<const MO::section_64 *>(
            SP + i * sizeof(MO::section_64));
        MachoParsedSec PS;
        PS.Seg = cstr16(S->segname);
        PS.Sect = cstr16(S->sectname);
        PS.Addr = S->addr;
        PS.Size = S->size;
        V.Secs.push_back(std::move(PS));
      }
    } else if (LC->cmd == MO::LC_SYMTAB) {
      auto *SymCmd =
          reinterpret_cast<const MO::symtab_command *>(Buf.data() + Cmd);
      if ((uint64_t)SymCmd->stroff + SymCmd->strsize > Buf.size())
        return V;
      if ((uint64_t)SymCmd->symoff + (uint64_t)SymCmd->nsyms * 16 > Buf.size())
        return V;
      const char *Str = Buf.data() + SymCmd->stroff;
      for (unsigned i = 0; i < SymCmd->nsyms; ++i) {
        auto *NL = reinterpret_cast<const MO::nlist_64 *>(
            Buf.data() + SymCmd->symoff + i * sizeof(MO::nlist_64));
        MachoParsedSym PS;
        if (NL->n_strx < SymCmd->strsize)
          PS.Name = std::string(Str + NL->n_strx);
        PS.Sect = NL->n_sect;
        PS.Value = NL->n_value;
        V.Syms.push_back(std::move(PS));
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  V.Ok = true;
  return V;
}

/// Overwrite a named Mach-O symbol's n_value in place — the Mach-O analogue of
/// patchSymValue, used to simulate offset-collapse corruption for the verifier.
bool patchMachoSymValue(SmallVectorImpl<char> &Buf, StringRef Name,
                        uint64_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return false;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SYMTAB) {
      auto *SC = reinterpret_cast<MO::symtab_command *>(Buf.data() + Cmd);
      const char *Str = Buf.data() + SC->stroff;
      for (unsigned i = 0; i < SC->nsyms; ++i) {
        auto *NL = reinterpret_cast<MO::nlist_64 *>(Buf.data() + SC->symoff +
                                                    i * sizeof(MO::nlist_64));
        if (NL->n_strx < SC->strsize && Name == (Str + NL->n_strx)) {
          NL->n_value = NewVal;
          return true;
        }
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return false;
}

/// Overwrite *every* Mach-O symbol named Name (patchMachoSymValue stops at the
/// first), so a collapse of duplicate-named local symbols can be simulated.
bool patchAllMachoSymValues(SmallVectorImpl<char> &Buf, StringRef Name,
                            uint64_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  bool Any = false;
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return Any;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SYMTAB) {
      auto *SC = reinterpret_cast<MO::symtab_command *>(Buf.data() + Cmd);
      const char *Str = Buf.data() + SC->stroff;
      for (unsigned i = 0; i < SC->nsyms; ++i) {
        auto *NL = reinterpret_cast<MO::nlist_64 *>(Buf.data() + SC->symoff +
                                                    i * sizeof(MO::nlist_64));
        if (NL->n_strx < SC->strsize && Name == (Str + NL->n_strx)) {
          NL->n_value = NewVal;
          Any = true;
        }
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return Any;
}

bool corruptMachoSymbolContentByte(SmallVectorImpl<char> &Buf, StringRef Name) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  MO::segment_command_64 *Seg = nullptr;
  MO::symtab_command *Symtab = nullptr;
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned C = 0; C < MH->ncmds; ++C) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return false;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmdsize == 0 || Cmd + LC->cmdsize > Buf.size())
      return false;
    if (LC->cmd == MO::LC_SEGMENT_64)
      Seg = reinterpret_cast<MO::segment_command_64 *>(LC);
    else if (LC->cmd == MO::LC_SYMTAB)
      Symtab = reinterpret_cast<MO::symtab_command *>(LC);
    Cmd += LC->cmdsize;
  }
  if (!Seg || !Symtab ||
      (uint64_t)Symtab->stroff + Symtab->strsize > Buf.size() ||
      (uint64_t)Symtab->symoff +
              (uint64_t)Symtab->nsyms * sizeof(MO::nlist_64) >
          Buf.size())
    return false;

  const char *Strings = Buf.data() + Symtab->stroff;
  auto *Sections = reinterpret_cast<MO::section_64 *>(
      reinterpret_cast<char *>(Seg) + sizeof(MO::segment_command_64));
  for (unsigned I = 0; I < Symtab->nsyms; ++I) {
    auto *NL = reinterpret_cast<MO::nlist_64 *>(Buf.data() + Symtab->symoff +
                                                I * sizeof(MO::nlist_64));
    if (NL->n_strx >= Symtab->strsize ||
        Name != StringRef(Strings + NL->n_strx,
                          strnlen(Strings + NL->n_strx,
                                  Symtab->strsize - NL->n_strx)))
      continue;
    if ((NL->n_type & MO::N_TYPE) != MO::N_SECT || NL->n_sect == 0 ||
        NL->n_sect > Seg->nsects)
      return false;
    const MO::section_64 &S = Sections[NL->n_sect - 1];
    if (NL->n_value < S.addr)
      return false;
    uint64_t Rel = NL->n_value - S.addr;
    if (Rel >= S.size || (uint64_t)S.offset + Rel >= Buf.size())
      return false;
    Buf[S.offset + Rel] ^= 0xFF;
    return true;
  }
  return false;
}

/// Force every Mach-O relocation's r_address to NewVal — simulates the reloc
/// half of the offset-collapse bug for the Mach-O verifier.
bool patchAllMachoRelocAddrs(SmallVectorImpl<char> &Buf, uint32_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  bool Any = false;
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      break;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SEGMENT_64) {
      auto *Seg = reinterpret_cast<MO::segment_command_64 *>(Buf.data() + Cmd);
      char *SP = Buf.data() + Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg->nsects; ++i) {
        auto *S =
            reinterpret_cast<MO::section_64 *>(SP + i * sizeof(MO::section_64));
        if (S->reloff == 0 || S->nreloc == 0)
          continue;
        if ((uint64_t)S->reloff + (uint64_t)S->nreloc * 8 > Buf.size())
          continue;
        for (unsigned r = 0; r < S->nreloc; ++r)
          memcpy(Buf.data() + S->reloff + r * 8, &NewVal, 4);
        Any = true;
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return Any;
}

/// Overwrite the six LC_DYSYMTAB range fields of a (merged) Mach-O object so a
/// test can corrupt the local/extdef/undef partition the verifier audits.
bool patchMachoDysymtab(SmallVectorImpl<char> &Buf, uint32_t ILocal,
                        uint32_t NLocal, uint32_t IExtdef, uint32_t NExtdef,
                        uint32_t IUndef, uint32_t NUndef) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      break;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_DYSYMTAB &&
        Cmd + sizeof(MO::dysymtab_command) <= Buf.size()) {
      auto *DC = reinterpret_cast<MO::dysymtab_command *>(Buf.data() + Cmd);
      DC->ilocalsym = ILocal;
      DC->nlocalsym = NLocal;
      DC->iextdefsym = IExtdef;
      DC->nextdefsym = NExtdef;
      DC->iundefsym = IUndef;
      DC->nundefsym = NUndef;
      return true;
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return false;
}

/// File offset of (Seg,Sect)+SecRelOff in a Mach-O object, or nothing when the
/// section is absent or the offset is outside the buffer.
std::optional<uint64_t> machoSecFileOffset(ArrayRef<char> Buf, StringRef Seg,
                                           StringRef Sect, uint32_t SecRelOff,
                                           uint64_t Width) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return std::nullopt;
  auto *MH = reinterpret_cast<const MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      break;
    auto *LC = reinterpret_cast<const MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SEGMENT_64) {
      auto *Seg64 =
          reinterpret_cast<const MO::segment_command_64 *>(Buf.data() + Cmd);
      const char *SP = Buf.data() + Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg64->nsects; ++i) {
        auto *S = reinterpret_cast<const MO::section_64 *>(
            SP + i * sizeof(MO::section_64));
        StringRef Sn(S->sectname, strnlen(S->sectname, 16));
        StringRef Sg(S->segname, strnlen(S->segname, 16));
        if (Sg == Seg && Sn == Sect) {
          uint64_t Fo = (uint64_t)S->offset + SecRelOff;
          if (Fo + Width > Buf.size())
            return std::nullopt;
          return Fo;
        }
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return std::nullopt;
}

/// Overwrite the 8-byte little-endian word at (Seg,Sect)+SecRelOff in a Mach-O
/// object's *section data* (not the relocation table).  Used to plant a real
/// pointer at a non-extern relocation site and, post-merge, to corrupt the
/// in-place-rewritten pointer so the verifier's value check is exercised.
bool patchMachoSecQword(SmallVectorImpl<char> &Buf, StringRef Seg,
                        StringRef Sect, uint32_t SecRelOff, uint64_t NewVal) {
  std::optional<uint64_t> Fo = machoSecFileOffset(
      ArrayRef<char>(Buf.data(), Buf.size()), Seg, Sect, SecRelOff, 8);
  if (!Fo)
    return false;
  memcpy(Buf.data() + *Fo, &NewVal, 8);
  return true;
}

/// The 32-bit little-endian word at (Seg,Sect)+SecRelOff in a Mach-O object's
/// section data.
std::optional<uint32_t> readMachoSecWord(ArrayRef<char> Buf, StringRef Seg,
                                         StringRef Sect, uint32_t SecRelOff) {
  std::optional<uint64_t> Fo = machoSecFileOffset(Buf, Seg, Sect, SecRelOff, 4);
  if (!Fo)
    return std::nullopt;
  uint32_t Value = 0;
  memcpy(&Value, Buf.data() + *Fo, 4);
  return Value;
}

} // namespace

// ---------------------------------------------------------------------------
// Edge-case tests: ELF merger
// ---------------------------------------------------------------------------

static void
expectDeterministicDebugCompression(DebugCompressionType CompressionType) {
  using namespace ELF;

  SecSpec Debug0{".debug_str", 4096, 16, SHT_PROGBITS, 0, 0x41};
  SecSpec AllocatedDebug0{".debug_alloc", 4096,      16,
                          SHT_PROGBITS,   SHF_ALLOC, 0x51};
  SecSpec Text0{".text", 64, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x91};
  SecSpec Debug1{".debug_str", 4096, 16, SHT_PROGBITS, 0, 0x42};
  SecSpec AllocatedDebug1{".debug_alloc", 4096,      16,
                          SHT_PROGBITS,   SHF_ALLOC, 0x52};
  SecSpec Text1{".text", 64, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x92};
  // AArch64 object writers emit local mapping symbols into data-like debug
  // sections. Keep explicit anchors here so the independent verifier must
  // compare logical (decompressed) bytes instead of the on-disk compression
  // header/payload.
  auto Object0 = buildSectionedELF(
      {Debug0, AllocatedDebug0, Text0},
      {SymSpec{"debug_anchor0", 0, 0}, SymSpec{"f0", 2, 0}}, {});
  auto Object1 = buildSectionedELF(
      {Debug1, AllocatedDebug1, Text1},
      {SymSpec{"debug_anchor1", 0, 0}, SymSpec{"f1", 2, 0}}, {});

  SmallVector<SmallVector<char, 0>, 2> Buffers;
  Buffers.push_back(std::move(Object0));
  Buffers.push_back(std::move(Object1));
  Options Opts;
  Opts.debugCompression = CompressionType;

  auto [FirstOK, First] = mergeELF(Buffers, Opts);
  auto [SecondOK, Second] = mergeELF(Buffers, Opts);
  ASSERT_TRUE(FirstOK);
  ASSERT_TRUE(SecondOK);
  std::string VerifyError;
  ASSERT_TRUE(verifyMerge(Buffers, First, Format::ELF64LE, Opts, &VerifyError))
      << VerifyError;
  EXPECT_EQ(ArrayRef<char>(First), ArrayRef<char>(Second));

  ElfView View = parseELF(First);
  ASSERT_TRUE(View.Ok);
  const int DebugIndex = View.findSec(".debug_str");
  const int AllocatedDebugIndex = View.findSec(".debug_alloc");
  const int TextIndex = View.findSec(".text");
  ASSERT_GE(DebugIndex, 0);
  ASSERT_GE(AllocatedDebugIndex, 0);
  ASSERT_GE(TextIndex, 0);
  const ParsedSec &Debug = View.Secs[DebugIndex];
  const ParsedSec &AllocatedDebug = View.Secs[AllocatedDebugIndex];
  const ParsedSec &Text = View.Secs[TextIndex];
  EXPECT_NE(Debug.Flags & SHF_COMPRESSED, 0u);
  EXPECT_EQ(Debug.Align, alignof(Elf64_Chdr));
  EXPECT_EQ(AllocatedDebug.Flags & SHF_COMPRESSED, 0u);
  EXPECT_EQ(Text.Flags & SHF_COMPRESSED, 0u);

  ASSERT_GE(Debug.Data.size(), sizeof(Elf64_Chdr));
  Elf64_Chdr Header{};
  memcpy(&Header, Debug.Data.data(), sizeof(Header));
  EXPECT_EQ(Header.ch_addralign, 16u);
  EXPECT_EQ(Header.ch_size, 8192u);

  std::optional<SmallVector<uint8_t, 0>> Decompressed =
      decompressELFSection(Debug, CompressionType);
  ASSERT_TRUE(Decompressed);
  ASSERT_EQ(Decompressed->size(), 8192u);
  EXPECT_TRUE(std::all_of(Decompressed->begin(), Decompressed->begin() + 4096,
                          [](uint8_t Byte) { return Byte == 0x41; }));
  EXPECT_TRUE(std::all_of(Decompressed->begin() + 4096, Decompressed->end(),
                          [](uint8_t Byte) { return Byte == 0x42; }));
}

TEST(MergeELFCompression, ZlibRoundTripsAndIsDeterministic) {
  const compression::Format Format =
      compression::formatFor(DebugCompressionType::Zlib);
  if (const char *Reason = compression::getReasonIfUnsupported(Format))
    GTEST_SKIP() << Reason;
  expectDeterministicDebugCompression(DebugCompressionType::Zlib);
}

TEST(MergeELFCompression, ZstdRoundTripsAndIsDeterministic) {
  const compression::Format Format =
      compression::formatFor(DebugCompressionType::Zstd);
  if (const char *Reason = compression::getReasonIfUnsupported(Format))
    GTEST_SKIP() << Reason;
  expectDeterministicDebugCompression(DebugCompressionType::Zstd);
}

TEST(MergeELFCompression, HugeDeclaredSizeIsRejectedBeforeAllocation) {
  using namespace ELF;
  const compression::Format CompressionFormat =
      compression::formatFor(DebugCompressionType::Zstd);
  if (const char *Reason =
          compression::getReasonIfUnsupported(CompressionFormat))
    GTEST_SKIP() << Reason;

  auto Object = buildSectionedELF(
      {SecSpec{".debug_str", 4096, 16, SHT_PROGBITS, 0, 0x41}},
      {SymSpec{"debug_anchor", 0, 0}}, {});
  SmallVector<SmallVector<char, 0>, 1> Buffers;
  Buffers.push_back(std::move(Object));
  Options Opts;
  Opts.debugCompression = DebugCompressionType::Zstd;
  auto [OK, Output] = mergeELF(Buffers, Opts);
  ASSERT_TRUE(OK);

  ASSERT_TRUE(patchELFCompressedSize(Output, ".debug_str", 0xffffffff1100ull));
  std::string VerifyError;
  EXPECT_FALSE(
      verifyMerge(Buffers, Output, Format::ELF64LE, Opts, &VerifyError));
}

TEST(MergeELFCompression, RefusesPreCompressedInputWithVerifyOff) {
  using namespace ELF;
  SecSpec Compressed{".debug_info", 64, 1, SHT_PROGBITS, SHF_COMPRESSED, 0x11};
  SmallVector<SmallVector<char, 0>, 1> Buffers;
  Buffers.push_back(buildSectionedELF({Compressed}, {}, {}));
  Options Opts;
  Opts.verify = false;
  auto [OK, Output] = mergeELF(Buffers, Opts);
  EXPECT_FALSE(OK);
  (void)Output;
}

// Standalone Split-DWARF contributions must arrive uncompressed. Concatenating
// independent compressed frames would hide every contribution after the first.
TEST(MergeELFSplitDwarf, RefusesPreCompressedDebugSections) {
  using namespace ELF;
  SecSpec Compressed{".debug_info.dwo", 64,  1, SHT_PROGBITS,
                     SHF_COMPRESSED,    0x11};
  auto Object = buildSectionedELF({Compressed}, {}, {});
  SmallVector<SmallVector<char, 0>, 1> Buffers;
  Buffers.push_back(std::move(Object));
  Options Opts;
  Opts.artifact = ArtifactKind::SplitDwarf;
  auto [OK, Out] = mergeELF(Buffers, Opts);
  EXPECT_FALSE(OK);
  (void)Out;
}

// Package-index construction refuses duplicate DWO signatures: two split
// compile units with the same ID cannot share one `.debug_cu_index`.
TEST(DwarfPackageTest, RejectsDuplicateSplitCompileSignatures) {
  // DWARF32 split_compile header: length, version=5, unit_type=5,
  // address_size=8, abbrev_offset=0, dwo_id.
  auto MakeUnit = [](uint64_t Signature) {
    SmallVector<char, 24> Unit(21, 0);
    const uint32_t Length = 17; // header plus the null DIE abbreviation code
    memcpy(Unit.data(), &Length, 4);
    Unit[4] = 5;
    Unit[5] = 0;
    Unit[6] = 5; // DW_UT_split_compile
    Unit[7] = 8;
    memcpy(Unit.data() + 12, &Signature, 8);
    return Unit;
  };

  auto Unit0 = MakeUnit(0x1111222233334444ull);
  auto Unit1 = MakeUnit(0x1111222233334444ull);
  SmallVector<char, 0> Info;
  Info.append(Unit0.begin(), Unit0.end());
  Info.append(Unit1.begin(), Unit1.end());
  SmallVector<char, 0> Abbrev(1, 0);

  PartitionDwarf Part0;
  Part0.record(DwarfSection::Info, 0, 0, Unit0.size());
  Part0.record(DwarfSection::Abbrev, 1, 0, Abbrev.size());
  PartitionDwarf Part1;
  Part1.record(DwarfSection::Info, 0, Unit0.size(), Unit1.size());
  Part1.record(DwarfSection::Abbrev, 1, 0, Abbrev.size());

  MutableArrayRef<char> Sections[] = {Info, Abbrev};
  DwarfPackageIndexes Indexes;
  EXPECT_FALSE(finalizeDwarfPackage(
      ArrayRef<PartitionDwarf>({Part0, Part1}),
      [&](unsigned Idx) {
        return Idx < 2 ? Sections[Idx] : MutableArrayRef<char>();
      },
      /*IsLittleEndian=*/true, Indexes));
}

TEST(DwarfPackageTest, BuildsTypeIndexForDwarf5InfoSectionTypeUnits) {
  // DWARF 5 stores split type units in .debug_info.dwo, not
  // .debug_types.dwo. Each row must therefore be keyed by the type signature
  // while its DW_SECT_INFO contribution names that unit's exact byte range.
  auto MakeTypeUnit = [](uint64_t Signature) {
    SmallVector<char, 32> Unit(25, 0);
    const uint32_t Length = 21;
    const uint32_t TypeOffset = 24;
    memcpy(Unit.data(), &Length, 4);
    Unit[4] = 5;
    Unit[5] = 0;
    Unit[6] = 6; // DW_UT_split_type
    Unit[7] = 8;
    memcpy(Unit.data() + 12, &Signature, 8);
    memcpy(Unit.data() + 20, &TypeOffset, 4);
    return Unit;
  };

  constexpr uint64_t Signature0 = 0x1020304050607080ull;
  constexpr uint64_t Signature1 = 0x8877665544332211ull;
  auto Unit0 = MakeTypeUnit(Signature0);
  auto Unit1 = MakeTypeUnit(Signature1);
  SmallVector<char, 0> Info;
  Info.append(Unit0.begin(), Unit0.end());
  Info.append(Unit1.begin(), Unit1.end());
  SmallVector<char, 0> Abbrev(2, 0);

  PartitionDwarf Part0;
  Part0.record(DwarfSection::Info, 0, 0, Unit0.size());
  Part0.record(DwarfSection::Abbrev, 1, 0, 1);
  PartitionDwarf Part1;
  Part1.record(DwarfSection::Info, 0, Unit0.size(), Unit1.size());
  Part1.record(DwarfSection::Abbrev, 1, 1, 1);

  MutableArrayRef<char> Sections[] = {Info, Abbrev};
  DwarfPackageIndexes Indexes;
  ASSERT_TRUE(finalizeDwarfPackage(
      ArrayRef<PartitionDwarf>({Part0, Part1}),
      [&](unsigned Idx) {
        return Idx < 2 ? Sections[Idx] : MutableArrayRef<char>();
      },
      /*IsLittleEndian=*/true, Indexes));
  EXPECT_TRUE(Indexes.CompileUnits.empty());
  ASSERT_FALSE(Indexes.TypeUnits.empty());

  DWARFUnitIndex Index(DW_SECT_INFO);
  DataExtractor Data(
      StringRef(Indexes.TypeUnits.data(), Indexes.TypeUnits.size()),
      /*IsLittleEndian=*/true, /*AddressSize=*/0);
  ASSERT_TRUE(Index.parse(Data));
  EXPECT_EQ(Index.getVersion(), 5u);

  const DWARFUnitIndex::Entry *Entry0 = Index.getFromHash(Signature0);
  const DWARFUnitIndex::Entry *Entry1 = Index.getFromHash(Signature1);
  ASSERT_NE(Entry0, nullptr);
  ASSERT_NE(Entry1, nullptr);
  const auto *Info0 = Entry0->getContribution(DW_SECT_INFO);
  const auto *Info1 = Entry1->getContribution(DW_SECT_INFO);
  ASSERT_NE(Info0, nullptr);
  ASSERT_NE(Info1, nullptr);
  EXPECT_EQ(Info0->getOffset(), 0u);
  EXPECT_EQ(Info0->getLength(), Unit0.size());
  EXPECT_EQ(Info1->getOffset(), Unit0.size());
  EXPECT_EQ(Info1->getLength(), Unit1.size());
}

TEST(MergeELF, EmptyBufferArray) {
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  auto [OK, Out] = mergeELF(Bufs);
  (void)OK;
  // No crash is the success criterion; merger may produce minimal output
}

TEST(MergeELF, AllEmptyBuffers) {
  SmallVector<SmallVector<char, 0>, 4> Bufs(3);
  auto [OK, Out] = mergeELF(Bufs);
  (void)OK;
  // No crash is the success criterion
}

TEST(MergeELF, SingleValidBuffer) {
  auto Obj = buildMinimalELF({"main"}, {});
  ASSERT_TRUE(isValidELF64LE(Obj));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, TwoPartitionsWithUndefinedSymbols) {
  // Partition 0: defines "foo", references "bar" (undefined)
  auto P0 = buildMinimalELF({"foo"}, {"bar"});
  // Partition 1: defines "bar", references "foo" (undefined)
  auto P1 = buildMinimalELF({"bar"}, {"foo"});
  ASSERT_TRUE(isValidELF64LE(P0));
  ASSERT_TRUE(isValidELF64LE(P1));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, ManyUndefinedSymbols) {
  // Stress test: many undefined symbols per partition
  std::vector<std::string> Defs0, Undefs0, Defs1, Undefs1;
  for (int i = 0; i < 100; ++i) {
    std::string Name = "sym" + std::to_string(i);
    if (i % 2 == 0) {
      Defs0.push_back(Name);
      Undefs1.push_back(Name);
    } else {
      Defs1.push_back(Name);
      Undefs0.push_back(Name);
    }
  }
  auto P0 = buildMinimalELF(Defs0, Undefs0);
  auto P1 = buildMinimalELF(Defs1, Undefs1);

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, PartitionGapEmptyMiddle) {
  // [valid, empty, valid] — tests Maps.resize(p+1) with gap
  auto P0 = buildMinimalELF({"a"}, {});
  auto P2 = buildMinimalELF({"b"}, {});

  SmallVector<SmallVector<char, 0>, 4> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.emplace_back(); // empty partition 1
  Bufs.push_back(std::move(P2));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, OnlyUndefinedSymbols) {
  // All symbols are undefined — the scenario that caused the original crash
  auto P0 = buildMinimalELF({}, {"ext1", "ext2", "ext3"});
  auto P1 = buildMinimalELF({}, {"ext4", "ext5"});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, PcgSymbolDemotion) {
  // Symbols with .__pcg marker should be demoted to local.
  // PCG symbols are always GLOBAL in real parallel codegen output.
  auto P0 = buildMinimalELF({"helper.__pcg12345678"}, {}, {0xcc},
                            /*DefinedAsGlobal=*/true);
  auto P1 = buildMinimalELF({}, {"helper.__pcg12345678"});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

// ---------------------------------------------------------------------------
// Semantic-correctness tests: assert the *meaning* of the merged object.
// Every test here fails on the historical "merged offsets collapse to 0" bug.
// ---------------------------------------------------------------------------

TEST(MergeELFSemantic, SectionMergeSymbolOffsets) {
  // One partition with two function sections that collapse into .text when
  // mergeSections is on (the Android-kernel-module case).  Symbols in the
  // *second* merged section must be shifted past the first — the exact
  // invariant the PartOffsets/SecOff bug violated by leaving them all at 0.
  SecSpec SA{".text.a", 0x34, 16};
  SecSpec SB{".text.b", 0x20, 16};
  SymSpec A{"a", 0, 0};           // start of .text.a
  SymSpec AMid{"a_mid", 0, 0x10}; // inside .text.a
  SymSpec B{"b", 1, 0};           // start of .text.b
  SymSpec BMid{"b_mid", 1, 0x8};  // inside .text.b

  auto Obj = buildSectionedELF({SA, SB}, {A, AMid, B, BMid}, {});
  ASSERT_TRUE(isValidELF64LE(Obj));

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  EXPECT_GE(V.findSec(".text"), 0);
  EXPECT_LT(V.findSec(".text.a"), 0);
  EXPECT_LT(V.findSec(".text.b"), 0);

  const ParsedSym *PA = V.findSym("a");
  const ParsedSym *PAMid = V.findSym("a_mid");
  const ParsedSym *PB = V.findSym("b");
  const ParsedSym *PBMid = V.findSym("b_mid");
  ASSERT_NE(PA, nullptr);
  ASSERT_NE(PAMid, nullptr);
  ASSERT_NE(PB, nullptr);
  ASSERT_NE(PBMid, nullptr);

  // .text.a at [0, 0x34); .text.b padded to align 16 → starts at 0x40.
  EXPECT_EQ(PA->Value, 0x0u);
  EXPECT_EQ(PAMid->Value, 0x10u);
  EXPECT_EQ(PB->Value, 0x40u);    // the bug made this 0
  EXPECT_EQ(PBMid->Value, 0x48u); // the bug made this 0x8

  // All four resolve into the same merged section.
  EXPECT_EQ(PA->Shndx, PB->Shndx);
  EXPECT_EQ(PA->Shndx, PAMid->Shndx);
  EXPECT_EQ(PA->Shndx, PBMid->Shndx);
}

TEST(MergeELFSemantic, CrossPartitionSymbolAndRelocOffsets) {
  // Two partitions each carrying their own .text.  After merge, partition 1's
  // symbols *and* relocations must be shifted by partition 0's .text size.
  SecSpec S0{".text", 0x40, 16};
  SecSpec S1{".text", 0x20, 16};
  SymSpec P0{"p0", 0, 0};
  SymSpec P1{"p1", 0, 0};
  SymSpec Ext{"ext", -1, 0}; // undefined, referenced by partition 1's reloc
  RelSpec R1{0, 0, "ext", ELF::R_X86_64_64, 0}; // at offset 0 of P1's .text

  auto Obj0 = buildSectionedELF({S0}, {P0}, {});
  auto Obj1 = buildSectionedELF({S1}, {P1, Ext}, {R1});
  ASSERT_TRUE(isValidELF64LE(Obj0));
  ASSERT_TRUE(isValidELF64LE(Obj1));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  const ParsedSym *PP0 = V.findSym("p0");
  const ParsedSym *PP1 = V.findSym("p1");
  ASSERT_NE(PP0, nullptr);
  ASSERT_NE(PP1, nullptr);
  EXPECT_EQ(PP0->Value, 0x0u);
  EXPECT_EQ(PP1->Value, 0x40u); // shifted by partition 0's .text size

  // The relocation from partition 1 keeps pointing at "ext" but its r_offset
  // moves to 0x40 in the merged .text.
  ASSERT_EQ(V.Relas.size(), 1u);
  EXPECT_EQ(V.Relas[0].Offset, 0x40u); // the bug made this 0
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string("ext"));
}

TEST(MergeELFSemantic, MergedSectionSizeIsPaddedSum) {
  // A higher-aligned second section forces real padding; the merged section
  // size and alignment must reflect it.
  SecSpec SA{".text.a", 0x34, 16};
  SecSpec SB{".text.b", 0x20, 32};
  auto Obj = buildSectionedELF({SA, SB}, {}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int Idx = V.findSec(".text");
  ASSERT_GE(Idx, 0);
  // .text.a [0,0x34) padded to align 32 → 0x40, then + 0x20 = 0x60.
  EXPECT_EQ(V.Secs[Idx].Size, 0x60u);
  EXPECT_EQ(V.Secs[Idx].Align, 32u);
}

TEST(MergeELFSemantic, BssSectionsMergeByVirtualSize) {
  // NOBITS sections have no file content; offsets come from a running virtual
  // size, and the merged section must stay NOBITS with the summed size.
  SecSpec BA{".bss.a", 0x30, 16, ELF::SHT_NOBITS,
             ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec BB{".bss.b", 0x10, 16, ELF::SHT_NOBITS,
             ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SymSpec VA{"va", 0, 0};
  SymSpec VB{"vb", 1, 0};
  auto Obj = buildSectionedELF({BA, BB}, {VA, VB}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int Idx = V.findSec(".bss");
  ASSERT_GE(Idx, 0);
  EXPECT_EQ(V.Secs[Idx].Type, (uint32_t)ELF::SHT_NOBITS);
  EXPECT_EQ(V.Secs[Idx].Size, 0x40u); // 0x30 + 0x10
  const ParsedSym *PVB = V.findSym("vb");
  ASSERT_NE(PVB, nullptr);
  EXPECT_EQ(PVB->Value, 0x30u); // shifted past .bss.a
}

TEST(MergeELFSemantic, KernelModuleAllFamiliesFoldOffsets) {
  using namespace ELF;
  // The full Android-kernel-module -r shape in one test: two partitions, each
  // with per-symbol sections in *all four* foldable families
  // (.text.* / .rodata.* / .data.* / .bss.*), plus a preserved .text.* section
  // (.text.ftrace_trampoline, the real ftrace .ko keeps it un-folded even
  // though it shares the .text. prefix) and a cross-partition relocation.  This
  // locks three things at once that the per-family tests above check only in
  // isolation:
  //   1) every family folds with the *same* PartOffset math — in particular
  //      .data.* folding, which had no direct offset assertion before;
  //   2) a preserved .text.* section overrides the fold (stays its own section)
  //      while its sibling .text.* still collapse into .text;
  //   3) a cross-partition symbol reference re-lands at the shifted offset.
  // Every offset below was 0 under the historical SecOff collapse.
  SecSpec TInit{".text.init", 0x30, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                0xA0};
  SecSpec Ftrace{".text.ftrace_trampoline", 0x10, 16, SHT_PROGBITS,
                 SHF_ALLOC | SHF_EXECINSTR, 0xE0}; // preserved → must NOT fold
  SecSpec Rk0{".rodata.k0", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xB0};
  SecSpec Dg0{".data.g0", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xC0};
  SecSpec Bb0{".bss.b0", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec Init{"init", 0, 0, true};
  SymSpec Ftr{"ftrace_tramp", 1, 0, true};
  SymSpec K0{"k0", 2, 0, true};
  SymSpec G0{"g0", 3, 0, true};
  SymSpec B0{"b0", 4, 0, true};
  auto Obj0 = buildSectionedELF({TInit, Ftrace, Rk0, Dg0, Bb0},
                                {Init, Ftr, K0, G0, B0}, {});

  SecSpec TExit{".text.exit", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                0xA1};
  SecSpec Rk1{".rodata.k1", 0x18, 16, SHT_PROGBITS, SHF_ALLOC, 0xB1};
  SecSpec Dg1{".data.g1", 0x8, 16, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xC1};
  SecSpec Bb1{".bss.b1", 0x20, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec Exit{"exit", 0, 0, true};
  SymSpec K1{"k1", 1, 0, true};
  SymSpec G1{"g1", 2, 0, true};
  SymSpec B1{"b1", 3, 0, true};
  SymSpec G0Undef{"g0", -1, 0, true}; // cross-partition ref to partition 0's g0
  // exit() references g0 (defined in partition 0's .data) at its entry.
  RelSpec R{0, 0, "g0", R_X86_64_64, 0};
  auto Obj1 = buildSectionedELF({TExit, Rk1, Dg1, Bb1},
                                {Exit, K1, G1, B1, G0Undef}, {R});

  ASSERT_TRUE(isValidELF64LE(Obj0));
  ASSERT_TRUE(isValidELF64LE(Obj1));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  Options Opts;
  Opts.mergeSections = true;
  Opts.preservedSections.push_back(".text.ftrace_trampoline");
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK); // internal independent verify (mergeSections) must accept

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  // Each family folded to its umbrella; the per-symbol inputs are gone.
  EXPECT_GE(V.findSec(".text"), 0);
  EXPECT_GE(V.findSec(".rodata"), 0);
  EXPECT_GE(V.findSec(".data"), 0);
  EXPECT_GE(V.findSec(".bss"), 0);
  EXPECT_LT(V.findSec(".text.init"), 0);
  EXPECT_LT(V.findSec(".text.exit"), 0);
  EXPECT_LT(V.findSec(".rodata.k0"), 0);
  EXPECT_LT(V.findSec(".data.g0"), 0);
  EXPECT_LT(V.findSec(".bss.b0"), 0);
  // The preserved .text.* section survives un-folded.
  EXPECT_GE(V.findSec(".text.ftrace_trampoline"), 0);

  auto value = [&](StringRef N) -> uint64_t {
    const ParsedSym *S = V.findSym(N);
    EXPECT_NE(S, nullptr) << N.str();
    return S ? S->Value : ~0ull;
  };
  // .text: init [0,0x30), exit padded to 16 → 0x30.
  EXPECT_EQ(value("init"), 0x0u);
  EXPECT_EQ(value("exit"), 0x30u);
  // .rodata: k0 [0,0x20), k1 → 0x20.
  EXPECT_EQ(value("k0"), 0x0u);
  EXPECT_EQ(value("k1"), 0x20u);
  // .data: g0 [0,0x10), g1 → 0x10  (the family that lacked a direct assertion).
  EXPECT_EQ(value("g0"), 0x0u);
  EXPECT_EQ(value("g1"), 0x10u);
  // .bss: b0 [0,0x40), b1 → 0x40.
  EXPECT_EQ(value("b0"), 0x0u);
  EXPECT_EQ(value("b1"), 0x40u);
  // The preserved trampoline keeps its own offset 0 (own section, not .text).
  EXPECT_EQ(value("ftrace_tramp"), 0x0u);

  // g0/g1 share the merged .data; ftrace_tramp is NOT in .text.
  const ParsedSym *PG0 = V.findSym("g0");
  const ParsedSym *PG1 = V.findSym("g1");
  const ParsedSym *PInit = V.findSym("init");
  const ParsedSym *PFtr = V.findSym("ftrace_tramp");
  ASSERT_NE(PG0, nullptr);
  ASSERT_NE(PG1, nullptr);
  ASSERT_NE(PInit, nullptr);
  ASSERT_NE(PFtr, nullptr);
  EXPECT_EQ(PG0->Shndx, PG1->Shndx);
  EXPECT_NE(PInit->Shndx, PFtr->Shndx);

  // Merged section sizes/types.
  int DIdx = V.findSec(".data");
  int BIdx = V.findSec(".bss");
  ASSERT_GE(DIdx, 0);
  ASSERT_GE(BIdx, 0);
  EXPECT_EQ(V.Secs[DIdx].Type, (uint32_t)SHT_PROGBITS);
  EXPECT_EQ(V.Secs[DIdx].Size, 0x18u); // 0x10 + 0x8
  EXPECT_EQ(V.Secs[BIdx].Type, (uint32_t)SHT_NOBITS);
  EXPECT_EQ(V.Secs[BIdx].Size, 0x60u); // 0x40 + 0x20

  // The cross-partition relocation re-lands at exit's shifted .text offset and
  // still names g0 (resolved onto partition 0's definition).
  ASSERT_EQ(V.Relas.size(), 1u);
  EXPECT_EQ(V.Relas[0].Offset, 0x30u); // the SecOff collapse made this 0
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string("g0"));
}

TEST(MergeELFSemantic,
     AndroidKernelModulePreservesExplicitMergeableRodataPool) {
  using namespace ELF;
  SecSpec String{".rodata.str1.1",
                 0x18,
                 1,
                 SHT_PROGBITS,
                 SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                 0xBB};
  String.Entsize = 1;
  auto Obj =
      buildSectionedELF({String}, {SymSpec{"lit", 0, 0}}, {}, EM_AARCH64);

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.preservedSections = {".rodata.str1.1"};
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  const int StringIdx = V.findSec(".rodata.str1.1");
  ASSERT_GE(StringIdx, 0);
  EXPECT_EQ(V.Secs[StringIdx].Type, (uint32_t)SHT_PROGBITS);
  EXPECT_EQ(V.Secs[StringIdx].Flags,
            (uint64_t)(SHF_ALLOC | SHF_MERGE | SHF_STRINGS));
  EXPECT_EQ(V.Secs[StringIdx].Entsize, 1u);
  EXPECT_EQ(V.Secs[StringIdx].Align, 1u);
}

TEST(MergeELFSemantic, NonAndroidMergeKeepsMergeableRodataPool) {
  using namespace ELF;
  SecSpec String{".rodata.str1.1",
                 0x18,
                 1,
                 SHT_PROGBITS,
                 SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                 0xBB};
  String.Entsize = 1;
  auto Obj = buildSectionedELF({String}, {SymSpec{"lit", 0, 0}}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  const int StringIdx = V.findSec(".rodata.str1.1");
  ASSERT_GE(StringIdx, 0);
  EXPECT_EQ(V.Secs[StringIdx].Type, (uint32_t)SHT_PROGBITS);
  EXPECT_EQ(V.Secs[StringIdx].Flags,
            (uint64_t)(SHF_ALLOC | SHF_MERGE | SHF_STRINGS));
  EXPECT_EQ(V.Secs[StringIdx].Entsize, 1u);
  EXPECT_EQ(V.Secs[StringIdx].Align, 1u);
}

TEST(MergeELFSemantic, AndroidKernelModuleFoldsMergeableRodataPools) {
  using namespace ELF;
  // Clang emits string literals into SHF_MERGE|SHF_STRINGS sections such as
  // .rodata.str1.1.  For Android .ko folding the merger demotes those pools
  // (clears MERGE|STRINGS, entsize) and concatenates them into ordinary
  // .rodata — one section name, one flag set.  Renaming without demoting
  // would produce two incompatible `.rodata` outputs and break 6.12+ sysfs
  // section export; keeping the `str1.1` name leaks compiler pool metadata.
  SecSpec Regular{".rodata.value", 0x20, 8, SHT_PROGBITS, SHF_ALLOC, 0xAA};
  SecSpec String{".rodata.str1.1",
                 0x18,
                 1,
                 SHT_PROGBITS,
                 SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                 0xBB};
  String.Entsize = 1;
  SecSpec Constant{".rodata.cst8",        0x10, 8, SHT_PROGBITS,
                   SHF_ALLOC | SHF_MERGE, 0xCC};
  Constant.Entsize = 8;
  SymSpec Value{"value", 0, 0, true};
  SymSpec Lit{"lit", 1, 0, true};
  SymSpec Number{"number", 2, 0, true};
  auto Obj = buildSectionedELF({Regular, String, Constant},
                               {Value, Lit, Number}, {}, EM_AARCH64);

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_EQ(V.findSec(".rodata.str1.1"), -1);
  EXPECT_EQ(V.findSec(".rodata.cst8"), -1);
  int RodataIdx = V.findSec(".rodata");
  ASSERT_GE(RodataIdx, 0);
  unsigned RodataCount = 0;
  for (const ParsedSec &S : V.Secs)
    if (S.Name == ".rodata")
      ++RodataCount;
  EXPECT_EQ(RodataCount, 1u);
  EXPECT_EQ(V.Secs[RodataIdx].Type, (uint32_t)SHT_PROGBITS);
  EXPECT_EQ(V.Secs[RodataIdx].Flags, (uint64_t)SHF_ALLOC);
  EXPECT_EQ(V.Secs[RodataIdx].Entsize, 0u);
  EXPECT_EQ(V.Secs[RodataIdx].Align, 8u);
  EXPECT_EQ(V.Secs[RodataIdx].Size, 0x48u);
  const ParsedSym *PV = V.findSym("value");
  const ParsedSym *PL = V.findSym("lit");
  const ParsedSym *PN = V.findSym("number");
  ASSERT_NE(PV, nullptr);
  ASSERT_NE(PL, nullptr);
  ASSERT_NE(PN, nullptr);
  EXPECT_EQ(PV->Shndx, (uint16_t)RodataIdx);
  EXPECT_EQ(PL->Shndx, (uint16_t)RodataIdx);
  EXPECT_EQ(PN->Shndx, (uint16_t)RodataIdx);
  EXPECT_EQ(PV->Value, 0u);
  EXPECT_EQ(PL->Value, 0x20u);
  EXPECT_EQ(PN->Value, 0x38u);
  ASSERT_EQ(V.Secs[RodataIdx].Data.size(), 0x48u);
  EXPECT_EQ(V.Secs[RodataIdx].Data[0], 0xAA);
  EXPECT_EQ(V.Secs[RodataIdx].Data[0x20], 0xBB);
  EXPECT_EQ(V.Secs[RodataIdx].Data[0x38], 0xCC);
}

TEST(MergeELFSemantic,
     AndroidKernelModuleFoldClearsStaleOrdinaryRodataEntsize) {
  using namespace ELF;
  // Section grouping ignores sh_entsize.  The fold policy must clear it for
  // both exact `.rodata` and `.rodata.*` so a stale first contribution cannot
  // stick — with or without a later demoted pool joining the umbrella.
  for (const char *RegularName : {".rodata.value", ".rodata"}) {
    SCOPED_TRACE(RegularName);
    SecSpec Regular{RegularName, 0x10, 8, SHT_PROGBITS, SHF_ALLOC, 0xAA};
    Regular.Entsize = 8;

    {
      auto Obj = buildSectionedELF({Regular}, {SymSpec{"value", 0, 0}}, {},
                                   EM_AARCH64);
      SmallVector<SmallVector<char, 0>, 1> Bufs;
      Bufs.push_back(std::move(Obj));
      Options Opts;
      Opts.mergeSections = true;
      Opts.androidKernelModule = true;
      auto [OK, Out] = mergeELF(Bufs, Opts);
      ASSERT_TRUE(OK);
      ElfView V = parseELF(Out);
      ASSERT_TRUE(V.Ok);
      const int RodataIdx = V.findSec(".rodata");
      ASSERT_GE(RodataIdx, 0);
      EXPECT_EQ(V.Secs[RodataIdx].Entsize, 0u);
      EXPECT_EQ(V.Secs[RodataIdx].Flags, (uint64_t)SHF_ALLOC);
    }

    SecSpec String{".rodata.str1.1",
                   0x10,
                   1,
                   SHT_PROGBITS,
                   SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                   0xBB};
    String.Entsize = 1;
    auto Obj = buildSectionedELF({Regular, String},
                                 {SymSpec{"value", 0, 0}, SymSpec{"lit", 1, 0}},
                                 {}, EM_AARCH64);

    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(std::move(Obj));
    Options Opts;
    Opts.mergeSections = true;
    Opts.androidKernelModule = true;
    auto [OK, Out] = mergeELF(Bufs, Opts);
    ASSERT_TRUE(OK);

    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    const int RodataIdx = V.findSec(".rodata");
    ASSERT_GE(RodataIdx, 0);
    EXPECT_EQ(V.findSec(".rodata.str1.1"), -1);
    EXPECT_EQ(V.Secs[RodataIdx].Type, (uint32_t)SHT_PROGBITS);
    EXPECT_EQ(V.Secs[RodataIdx].Flags, (uint64_t)SHF_ALLOC);
    EXPECT_EQ(V.Secs[RodataIdx].Entsize, 0u);
    EXPECT_EQ(V.Secs[RodataIdx].Align, 8u);
    EXPECT_EQ(V.Secs[RodataIdx].Size, 0x20u);
  }
}

TEST(MergeELFSemantic,
     AndroidKernelModuleKeepsUnrecognizedMergeableRodataSections) {
  using namespace ELF;
  struct PoolCase {
    const char *Name;
    uint32_t Type;
    uint64_t Flags;
    uint64_t Entsize;
    uint32_t Align;
  };
  const PoolCase Cases[] = {
      // Only allocated read-only SHT_PROGBITS pools are eligible.
      {".rodata.str1.1", SHT_NOTE, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1, 1},
      {".rodata.str1.1", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 0,
       1},
      // String and constant pools have different exact flag shapes.
      {".rodata.str1.1", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE, 1, 1},
      {".rodata.cst8", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 8, 8},
      // strN.M encodes both sh_entsize and sh_addralign.
      {".rodata.str2.2", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1,
       2},
      {".rodata.str1.1", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1,
       2},
      // Prefix matches are insufficient: the numeric grammar consumes all.
      {".rodata.str1.1.trailing", SHT_PROGBITS,
       SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1, 1},
      {".rodata.str+1.1", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1,
       1},
      {".rodata.str1.+1", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1,
       1},
      {".rodata.cst8trailing", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE, 8, 8},
      {".rodata.cst+8", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE, 8, 8},
      // Unusual flags must not be silently laundered into ordinary rodata.
      {".rodata.str1.1", SHT_PROGBITS,
       SHF_ALLOC | SHF_WRITE | SHF_MERGE | SHF_STRINGS, 1, 1},
  };

  unsigned CaseIndex = 0;
  for (const PoolCase &C : Cases) {
    // Several cases reuse the compiler pool name; include the row index so a
    // failure names the exact malformed shape under test.
    SCOPED_TRACE(testing::Message()
                 << CaseIndex++ << ": " << C.Name << " type=" << C.Type
                 << " flags=" << C.Flags << " entsize=" << C.Entsize
                 << " align=" << C.Align);
    SecSpec Pool{C.Name, 0x10, C.Align, C.Type, C.Flags, 0xD1};
    Pool.Entsize = C.Entsize;
    auto Obj =
        buildSectionedELF({Pool}, {SymSpec{"pool", 0, 0}}, {}, EM_AARCH64);
    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(std::move(Obj));
    Options Opts;
    Opts.mergeSections = true;
    Opts.androidKernelModule = true;
    auto [OK, Out] = mergeELF(Bufs, Opts);
    ASSERT_TRUE(OK);

    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    const int PoolIdx = V.findSec(C.Name);
    ASSERT_GE(PoolIdx, 0);
    EXPECT_EQ(V.Secs[PoolIdx].Type, C.Type);
    EXPECT_EQ(V.Secs[PoolIdx].Flags, C.Flags);
    EXPECT_EQ(V.Secs[PoolIdx].Entsize, C.Entsize);
    EXPECT_EQ(V.Secs[PoolIdx].Align, C.Align);
  }
}

TEST(MergeELFSemantic,
     AndroidKernelModuleFoldsEligiblePoolBesideMalformedSameName) {
  using namespace ELF;
  SecSpec Eligible{".rodata.str1.1",
                   0x10,
                   1,
                   SHT_PROGBITS,
                   SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                   0xE1};
  Eligible.Entsize = 1;
  SecSpec Malformed{".rodata.str1.1",
                    0x10,
                    2,
                    SHT_PROGBITS,
                    SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                    0xE2};
  Malformed.Entsize = 1; // name says alignment 1, header says 2
  auto Obj = buildSectionedELF(
      {Eligible, Malformed},
      {SymSpec{"eligible", 0, 0}, SymSpec{"malformed", 1, 0}}, {}, EM_AARCH64);

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  const int FoldedIdx = V.findSec(".rodata");
  const int RetainedIdx = V.findSec(".rodata.str1.1");
  ASSERT_GE(FoldedIdx, 0);
  ASSERT_GE(RetainedIdx, 0);
  EXPECT_EQ(V.Secs[FoldedIdx].Flags, (uint64_t)SHF_ALLOC);
  EXPECT_EQ(V.Secs[FoldedIdx].Entsize, 0u);
  EXPECT_EQ(V.Secs[FoldedIdx].Align, 1u);
  EXPECT_EQ(V.Secs[RetainedIdx].Flags,
            (uint64_t)(SHF_ALLOC | SHF_MERGE | SHF_STRINGS));
  EXPECT_EQ(V.Secs[RetainedIdx].Entsize, 1u);
  EXPECT_EQ(V.Secs[RetainedIdx].Align, 2u);
  ASSERT_EQ(V.Secs[FoldedIdx].Data.size(), 0x10u);
  ASSERT_EQ(V.Secs[RetainedIdx].Data.size(), 0x10u);
  EXPECT_EQ(V.Secs[FoldedIdx].Data.front(), 0xE1);
  EXPECT_EQ(V.Secs[RetainedIdx].Data.front(), 0xE2);

  const ParsedSym *FoldedSym = V.findSym("eligible");
  const ParsedSym *RetainedSym = V.findSym("malformed");
  ASSERT_NE(FoldedSym, nullptr);
  ASSERT_NE(RetainedSym, nullptr);
  EXPECT_EQ(FoldedSym->Shndx, (uint16_t)FoldedIdx);
  EXPECT_EQ(RetainedSym->Shndx, (uint16_t)RetainedIdx);
}

TEST(MergeELFSemantic,
     AndroidKernelModuleFoldedRodataRebasesCrossPartitionRelocation) {
  using namespace ELF;
  SecSpec Base{".rodata.base", 0x10, 8, SHT_PROGBITS, SHF_ALLOC, 0xA1};
  SecSpec String0{".rodata.str1.1",
                  0x10,
                  1,
                  SHT_PROGBITS,
                  SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                  0xB1};
  String0.Entsize = 1;
  auto Obj0 = buildSectionedELF({Base, String0},
                                {SymSpec{"base", 0, 0}, SymSpec{"lit0", 1, 0}},
                                {}, EM_AARCH64);

  SecSpec Constant{".rodata.cst8",        0x10, 8, SHT_PROGBITS,
                   SHF_ALLOC | SHF_MERGE, 0xC1};
  Constant.Entsize = 8;
  SecSpec String1{".rodata.str1.1",
                  0x10,
                  1,
                  SHT_PROGBITS,
                  SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                  0xD1};
  String1.Entsize = 1;
  RelSpec Reference{1, 8, "base", R_AARCH64_ABS64, 4};
  auto Obj1 = buildSectionedELF(
      {Constant, String1},
      {SymSpec{"number", 0, 0}, SymSpec{"lit1", 1, 0}, SymSpec{"base", -1, 0}},
      {Reference}, EM_AARCH64);

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  const int RodataIdx = V.findSec(".rodata");
  ASSERT_GE(RodataIdx, 0);
  EXPECT_EQ(V.Secs[RodataIdx].Size, 0x40u);
  const ParsedSym *BaseSym = V.findSym("base");
  const ParsedSym *Lit0 = V.findSym("lit0");
  const ParsedSym *Number = V.findSym("number");
  const ParsedSym *Lit1 = V.findSym("lit1");
  ASSERT_NE(BaseSym, nullptr);
  ASSERT_NE(Lit0, nullptr);
  ASSERT_NE(Number, nullptr);
  ASSERT_NE(Lit1, nullptr);
  EXPECT_EQ(BaseSym->Value, 0u);
  EXPECT_EQ(Lit0->Value, 0x10u);
  EXPECT_EQ(Number->Value, 0x20u);
  EXPECT_EQ(Lit1->Value, 0x30u);
  EXPECT_EQ(BaseSym->Shndx, (uint16_t)RodataIdx);
  EXPECT_EQ(Lit1->Shndx, (uint16_t)RodataIdx);

  ASSERT_EQ(V.Relas.size(), 1u);
  EXPECT_EQ(V.Relas[0].TargetSec, (uint32_t)RodataIdx);
  EXPECT_EQ(V.Relas[0].Offset, 0x38u);
  EXPECT_EQ(V.Relas[0].Type, (uint32_t)R_AARCH64_ABS64);
  EXPECT_EQ(V.Relas[0].Addend, 4);
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string("base"));
}

TEST(MergeELFSemantic, DistinctNotesConcatenatedIdenticalDeduped) {
  using namespace ELF;
  // This merger is also the linker's general `-r` path over arbitrary user
  // objects, where two same-named SHT_NOTE sections can carry *different* bytes
  // (distinct .note.gnu.property feature sets, build-ids, ...).  The historical
  // "keep the first copy unconditionally" dedup silently dropped the later
  // note — invisible to verifyMerge, which excludes NOTE sections.  The merger
  // must now dedup only byte-identical notes and concatenate distinct ones, so
  // no data is lost on a heterogeneous -r while same-source partitions (which
  // re-emit byte-identical notes) still collapse to one copy.

  // Distinct content across the two partitions → both must survive.
  {
    SecSpec N0{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xAA};
    SecSpec N1{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xBB};
    auto O0 = buildSectionedELF({N0}, {}, {});
    auto O1 = buildSectionedELF({N1}, {}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(O0));
    Bufs.push_back(std::move(O1));
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK);
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec(".note.x");
    ASSERT_GE(Idx, 0);
    EXPECT_EQ(V.Secs[Idx].Size, 0x20u)
        << "distinct .note.x from two inputs must concatenate, not drop one";
    // Both fill patterns must be present in the merged note bytes.
    const auto &D = V.Secs[Idx].Data;
    ASSERT_EQ(D.size(), 0x20u);
    bool SawAA = false, SawBB = false;
    for (uint8_t B : D) {
      SawAA |= (B == 0xAA);
      SawBB |= (B == 0xBB);
    }
    EXPECT_TRUE(SawAA) << "first input's note bytes were dropped";
    EXPECT_TRUE(SawBB) << "second input's note bytes were dropped";
  }

  // Byte-identical content across partitions (the same-source case) → dedup to
  // a single copy, exactly as before.
  {
    SecSpec N0{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xAA};
    SecSpec N1{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xAA};
    auto O0 = buildSectionedELF({N0}, {}, {});
    auto O1 = buildSectionedELF({N1}, {}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(O0));
    Bufs.push_back(std::move(O1));
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK);
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec(".note.x");
    ASSERT_GE(Idx, 0);
    EXPECT_EQ(V.Secs[Idx].Size, 0x10u)
        << "byte-identical .note.x must dedup to one copy";
  }
}

TEST(MergeELFSemantic, NobitsAndProgbitsSameNameFoldWithoutOverlap) {
  // A NOBITS section and a PROGBITS section that share a name + flag set are
  // merge-compatible (canMergeToProgbits), so they collapse into one PROGBITS
  // output.  The NOBITS contribution must be materialized as zero bytes so the
  // other partition's bytes (and symbols) land *after* it; otherwise both
  // partitions restart at offset 0, the NOBITS reserve vanishes from the
  // output, and the two symbols alias the same address.  The self-verifier
  // skips NOBITS content windows, so it cannot catch this — assert it directly.
  // Both orderings are exercised because the promotion path differs
  // (NOBITS-first vs PROGBITS-first).
  auto check = [](bool NobitsFirst) {
    SecSpec Nb{"X", 0x40, 16, ELF::SHT_NOBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE};
    SecSpec Pb{
        "X", 0x40, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
        0xBB};
    SymSpec A{"a", 0, 0}; // defined in the NOBITS partition
    SymSpec B{"b", 0, 0}; // defined in the PROGBITS partition
    auto ObjNb = buildSectionedELF({Nb}, {A}, {});
    auto ObjPb = buildSectionedELF({Pb}, {B}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    if (NobitsFirst) {
      Bufs.push_back(std::move(ObjNb));
      Bufs.push_back(std::move(ObjPb));
    } else {
      Bufs.push_back(std::move(ObjPb));
      Bufs.push_back(std::move(ObjNb));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "NobitsFirst=" << NobitsFirst;
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec("X");
    ASSERT_GE(Idx, 0);
    // Mixed NOBITS+PROGBITS must become PROGBITS holding both contributions.
    EXPECT_EQ(V.Secs[Idx].Type, (uint32_t)ELF::SHT_PROGBITS)
        << "NobitsFirst=" << NobitsFirst;
    EXPECT_EQ(V.Secs[Idx].Size, 0x80u)
        << "NobitsFirst=" << NobitsFirst; // 0x40 zero-fill + 0x40 progbits
    const ParsedSym *PA = V.findSym("a");
    const ParsedSym *PB = V.findSym("b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    // The two partitions' symbols must occupy distinct, non-overlapping
    // offsets: one at 0, the other at 0x40 (order depends on which came first).
    EXPECT_NE(PA->Value, PB->Value)
        << "NobitsFirst=" << NobitsFirst << ": symbols collapsed onto offset 0";
    EXPECT_TRUE((PA->Value == 0 && PB->Value == 0x40) ||
                (PA->Value == 0x40 && PB->Value == 0))
        << "NobitsFirst=" << NobitsFirst << ": a=" << PA->Value
        << " b=" << PB->Value;
  };
  check(/*NobitsFirst=*/true);
  check(/*NobitsFirst=*/false);
}

TEST(MergeELFSemantic, RandomizedNobitsProgbitsMixNoCollapse) {
  // Property mirror of the NOBITS+PROGBITS fix: any random mix of same-named
  // NOBITS/PROGBITS partitions must fold into one section whose per-partition
  // symbols occupy distinct, non-overlapping offsets — never the offset-0
  // collapse the merger produced when a NOBITS run and a PROGBITS run each kept
  // their own offset counter.  An all-NOBITS draw must stay NOBITS; any
  // PROGBITS contributor promotes the whole section to PROGBITS.
  std::mt19937 Rng(0xB1775EEDu);
  for (int Trial = 0; Trial < 200; ++Trial) {
    unsigned NP = 2 + (Rng() % 4); // 2..5 partitions
    SmallVector<SmallVector<char, 0>, 5> Bufs;
    SmallVector<std::string, 8> SymNames;
    bool AnyProgbits = false;
    for (unsigned p = 0; p < NP; ++p) {
      bool Nobits = (Rng() & 1u) != 0;
      if (!Nobits)
        AnyProgbits = true;
      uint64_t Size = 0x10 + (Rng() % 0x40);
      SecSpec S{"X",
                Size,
                16,
                Nobits ? ELF::SHT_NOBITS : ELF::SHT_PROGBITS,
                ELF::SHF_ALLOC | ELF::SHF_WRITE,
                Nobits ? (uint8_t)0 : (uint8_t)(0x10 + p)};
      std::string Nm = "s" + std::to_string(p);
      SymNames.push_back(Nm);
      Bufs.push_back(buildSectionedELF({S}, {SymSpec{Nm, 0, 0}}, {}));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "trial " << Trial;
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec("X");
    ASSERT_GE(Idx, 0) << "trial " << Trial;
    EXPECT_EQ(V.Secs[Idx].Type,
              (uint32_t)(AnyProgbits ? ELF::SHT_PROGBITS : ELF::SHT_NOBITS))
        << "trial " << Trial;
    SmallVector<uint64_t, 8> Vals;
    for (auto &Nm : SymNames) {
      const ParsedSym *S = V.findSym(Nm);
      ASSERT_NE(S, nullptr) << "trial " << Trial << " sym " << Nm;
      Vals.push_back(S->Value);
    }
    std::sort(Vals.begin(), Vals.end());
    for (unsigned i = 1; i < Vals.size(); ++i)
      EXPECT_NE(Vals[i - 1], Vals[i])
          << "trial " << Trial
          << ": two partition symbols collapsed onto offset " << Vals[i];
  }
}

TEST(MergeELFSemantic, HugeNobitsSizeRefusedNotMaterialized) {
  // Regression for the merge fuzzer's allocation-size-too-big abort at
  // ELF/MergerELF.cpp's NOBITS materialization: a SHT_NOBITS section declares
  // an sh_size backed by *no* file bytes, so a 64-byte section header can claim
  // a ~7.6 EB size.  When that section folds into a same-named PROGBITS output
  // the NOBITS contribution must be materialized as real zero bytes, and an
  // unbounded resize then aborts under ASan (or OOMs in production).  The
  // merger must refuse such an input instead of attempting the allocation. Both
  // partition orderings are exercised because the materialization happens at a
  // different site for each (the accumulated-fill resize when the NOBITS input
  // comes first, the this-input resize when the PROGBITS input comes first),
  // and both verify on/off so the guard is proven to live in the raw merge
  // path, not the verifier.  The size is the exact value the fuzzer found
  // (bytes spelling
  // "\1__mod_i").
  const uint64_t Huge = 0x695f646f6d5f5f01ull;
  for (bool NobitsFirst : {true, false}) {
    for (bool Verify : {true, false}) {
      SecSpec Nb{"X", Huge, 16, ELF::SHT_NOBITS,
                 ELF::SHF_ALLOC | ELF::SHF_WRITE};
      SecSpec Pb{
          "X", 0x40, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
          0xBB};
      auto ObjNb = buildSectionedELF({Nb}, {SymSpec{"a", 0, 0}}, {});
      auto ObjPb = buildSectionedELF({Pb}, {SymSpec{"b", 0, 0}}, {});
      SmallVector<SmallVector<char, 0>, 2> Bufs;
      if (NobitsFirst) {
        Bufs.push_back(std::move(ObjNb));
        Bufs.push_back(std::move(ObjPb));
      } else {
        Bufs.push_back(std::move(ObjPb));
        Bufs.push_back(std::move(ObjNb));
      }
      Options Opts;
      Opts.verify = Verify;
      auto [OK, Out] = mergeELF(Bufs, Opts);
      EXPECT_FALSE(OK) << "NobitsFirst=" << NobitsFirst << " Verify=" << Verify
                       << ": a NOBITS section larger than all inputs must be "
                          "refused, never materialized";
    }
  }
}

TEST(MergeELFSemantic, PreservedSectionsNotMerged) {
  // Kernel-module mode: .text.* collapses to .text, but a preserved section
  // (e.g. .modinfo) keeps its name and is never folded away.
  SecSpec T{".text.foo", 0x20, 16};
  SecSpec Modinfo{".modinfo", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  auto Obj = buildSectionedELF({T, Modinfo}, {}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  Opts.preservedSections.push_back(".modinfo");
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_GE(V.findSec(".text"), 0);
  EXPECT_GE(V.findSec(".modinfo"), 0);
  EXPECT_LT(V.findSec(".text.foo"), 0);
}

TEST(MergeELFSemantic, AndroidKernelModuleSynthesizesLoaderSections) {
  SecSpec Text{".text", 0x20, 16};
  auto Obj = buildSectionedELF({Text},
                               {SymSpec{"__start_alloc_tags", -1, 0},
                                SymSpec{"__stop_alloc_tags", -1, 0}},
                               {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.finalizeAndroidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  const int VersionsIdx = V.findSec("__versions");
  ASSERT_GE(VersionsIdx, 0);
  EXPECT_EQ(V.Secs[VersionsIdx].Type, ELF::SHT_PROGBITS);
  EXPECT_NE(V.Secs[VersionsIdx].Flags & ELF::SHF_ALLOC, 0u);
  EXPECT_EQ(V.Secs[VersionsIdx].Size, 0u);
  EXPECT_GE(V.Secs[VersionsIdx].Align, 8u);

  const int AllocTagsIdx = V.findSec(".codetag.alloc_tags");
  ASSERT_GE(AllocTagsIdx, 0);
  EXPECT_EQ(V.Secs[AllocTagsIdx].Type, ELF::SHT_PROGBITS);
  EXPECT_NE(V.Secs[AllocTagsIdx].Flags & ELF::SHF_ALLOC, 0u);
  EXPECT_NE(V.Secs[AllocTagsIdx].Flags & ELF::SHF_WRITE, 0u);
  EXPECT_EQ(V.Secs[AllocTagsIdx].Size, 0u);
  EXPECT_GE(V.Secs[AllocTagsIdx].Align, 8u);

  const ParsedSym *Start = V.findSym("__start_alloc_tags");
  const ParsedSym *Stop = V.findSym("__stop_alloc_tags");
  ASSERT_NE(Start, nullptr);
  ASSERT_NE(Stop, nullptr);
  EXPECT_EQ(Start->Bind, ELF::STB_GLOBAL);
  EXPECT_EQ(Stop->Bind, ELF::STB_GLOBAL);
  EXPECT_EQ(Start->Type, ELF::STT_NOTYPE);
  EXPECT_EQ(Stop->Type, ELF::STT_NOTYPE);
  EXPECT_EQ(Start->Shndx, static_cast<unsigned>(AllocTagsIdx));
  EXPECT_EQ(Stop->Shndx, static_cast<unsigned>(AllocTagsIdx));
  EXPECT_EQ(Start->Value, 0u);
  EXPECT_EQ(Stop->Value, 0u);
}

TEST(MergeELFSemantic, AndroidKernelModuleCollectsAllocTags) {
  SecSpec Tags{
      "alloc_tags", 24, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x5a};
  SecSpec Names{".rodata", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC, 0x6e};
  RelSpec TagRel{0, 0, "tag_name", ELF::R_AARCH64_ABS64, 0};
  auto Obj = buildSectionedELF(
      {Tags, Names},
      {SymSpec{"real_alloc_tag", 0, 8}, SymSpec{"tag_name", 1, 0}}, {TagRel},
      ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.finalizeAndroidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_LT(V.findSec("alloc_tags"), 0);
  const int AllocTagsIdx = V.findSec(".codetag.alloc_tags");
  ASSERT_GE(AllocTagsIdx, 0);
  ASSERT_EQ(V.Secs[AllocTagsIdx].Size, 24u);
  EXPECT_EQ(V.Secs[AllocTagsIdx].Data,
            std::vector<uint8_t>(24, static_cast<uint8_t>(0x5a)));

  const ParsedSym *Tag = V.findSym("real_alloc_tag");
  const ParsedSym *Start = V.findSym("__start_alloc_tags");
  const ParsedSym *Stop = V.findSym("__stop_alloc_tags");
  ASSERT_NE(Tag, nullptr);
  ASSERT_NE(Start, nullptr);
  ASSERT_NE(Stop, nullptr);
  EXPECT_EQ(Tag->Shndx, static_cast<unsigned>(AllocTagsIdx));
  EXPECT_EQ(Tag->Value, 8u);
  EXPECT_EQ(Start->Value, 0u);
  EXPECT_EQ(Stop->Value, 24u);
  EXPECT_EQ(Start->Type, ELF::STT_NOTYPE);
  EXPECT_EQ(Stop->Type, ELF::STT_NOTYPE);
  ASSERT_EQ(V.Relas.size(), 1u);
  EXPECT_EQ(V.Relas[0].TargetSec, static_cast<unsigned>(AllocTagsIdx));
  EXPECT_EQ(V.Relas[0].Type, ELF::R_AARCH64_ABS64);
}

TEST(MergeELFVerify, AndroidKernelModuleRejectsCorruptTagBoundary) {
  SecSpec Text{".text", 0x20, 16};
  auto Obj = buildSectionedELF({Text}, {}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  ASSERT_TRUE(patchSymValue(Out, "__stop_alloc_tags", 1));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err));
  EXPECT_NE(Err.find("__stop_alloc_tags"), std::string::npos) << Err;
}

TEST(MergeELFVerify, AndroidKernelModuleRejectsMalformedFoldedRodataHeader) {
  using namespace ELF;
  SecSpec Regular{".rodata.value", 0x10, 8, SHT_PROGBITS, SHF_ALLOC, 0xA5};
  SecSpec String{".rodata.str1.1",
                 0x10,
                 1,
                 SHT_PROGBITS,
                 SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
                 0xB5};
  String.Entsize = 1;
  auto Obj = buildSectionedELF({Regular, String},
                               {SymSpec{"value", 0, 0}, SymSpec{"lit", 1, 0}},
                               {}, EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);
  ASSERT_TRUE(patchELFSectionMergeMetadata(
      Out, ".rodata", SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1));

  std::string Err;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err));
  EXPECT_NE(Err.find("folded Android mergeable rodata '.rodata' must be "
                     "SHT_PROGBITS with SHF_ALLOC and sh_entsize 0"),
            std::string::npos)
      << Err;
}

TEST(MergeELFVerify, AndroidKernelModuleRejectsDuplicateLoadedSectionNames) {
  using namespace ELF;
  SecSpec Text{".text", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xA7};
  SecSpec Data{".data", 0x10, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xB7};
  auto Obj = buildSectionedELF({Text, Data}, {}, {}, EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);
  ASSERT_TRUE(patchELFSectionName(Out, ".data", ".text"));

  std::string Err;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err));
  EXPECT_NE(Err.find("duplicate loaded section name '.text'"),
            std::string::npos)
      << Err;
}

TEST(MergeELFSemantic,
     AndroidKernelModuleRejectsDuplicateLoadedSectionNamesWithVerifyDisabled) {
  using namespace ELF;
  SecSpec ReadOnly{".duplicate", 0x10, 8, SHT_PROGBITS, SHF_ALLOC, 0xA6};
  SecSpec Writable{".duplicate",          0x10, 8, SHT_PROGBITS,
                   SHF_ALLOC | SHF_WRITE, 0xB6};
  auto Obj = buildSectionedELF({ReadOnly, Writable}, {}, {}, EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.verify = false;
  EXPECT_FALSE(mergeELF(Bufs, Opts).first);
}

TEST(MergeELFSemantic, AndroidKernelModuleAllowsDuplicateEmptySectionNames) {
  using namespace ELF;
  SecSpec ReadOnly{".duplicate", 0, 8, SHT_PROGBITS, SHF_ALLOC};
  SecSpec Writable{".duplicate", 0, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE};
  auto Obj = buildSectionedELF({ReadOnly, Writable}, {}, {}, EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.verify = false;
  EXPECT_TRUE(mergeELF(Bufs, Opts).first);
}

TEST(MergeELFSemantic, AndroidKernelModuleRejectsConflictingTagBoundaries) {
  SecSpec Tags{".codetag.alloc_tags", 8, 8, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE};
  auto Obj = buildSectionedELF(
      {Tags},
      {SymSpec{"__start_alloc_tags", 0, 4}, SymSpec{"__stop_alloc_tags", 0, 8}},
      {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  EXPECT_FALSE(mergeELF(Bufs, Opts).first);
}

TEST(MergeELFSemantic, AndroidKernelModuleAcceptsMatchingTagBoundaries) {
  SecSpec Tags{".codetag.alloc_tags", 8, 8, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE};
  auto Obj = buildSectionedELF(
      {Tags},
      {SymSpec{"__start_alloc_tags", 0, 0}, SymSpec{"__stop_alloc_tags", 0, 8}},
      {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  const ParsedSym *Start = V.findSym("__start_alloc_tags");
  const ParsedSym *Stop = V.findSym("__stop_alloc_tags");
  ASSERT_NE(Start, nullptr);
  ASSERT_NE(Stop, nullptr);
  EXPECT_EQ(Start->Type, ELF::STT_NOTYPE);
  EXPECT_EQ(Stop->Type, ELF::STT_NOTYPE);
}

TEST(MergeELFSemantic, AndroidKernelModuleRejectsLocalTagBoundaries) {
  SecSpec Tags{".codetag.alloc_tags", 8, 8, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SymSpec LocalStart{"__start_alloc_tags", 0, 0};
  LocalStart.Global = false;
  auto Obj = buildSectionedELF({Tags}, {LocalStart}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  EXPECT_FALSE(mergeELF(Bufs, Opts).first);
}

TEST(MergeELFSemantic, NonAndroidMergeDoesNotSynthesizeLoaderSections) {
  SecSpec Text{".text", 0x20, 16};
  auto Obj = buildSectionedELF({Text}, {}, {});
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_LT(V.findSec("__versions"), 0);
  EXPECT_LT(V.findSec(".codetag.alloc_tags"), 0);
  EXPECT_EQ(V.findSym("__start_alloc_tags"), nullptr);
  EXPECT_EQ(V.findSym("__stop_alloc_tags"), nullptr);
}

TEST(MergeELFSemantic, AndroidKernelPartialLinkPreservesProfileContract) {
  SecSpec Text{".text", 0x20, 16, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SecSpec Contract{".neverc.android.kernel.profile",
                   8,
                   8,
                   ELF::SHT_PROGBITS,
                   ELF::SHF_ALLOC,
                   0xab};
  SymSpec ContractSym{"__neverc_android_kernel_profile_contract", 1, 0};
  ContractSym.Global = false;
  auto Obj =
      buildSectionedELF({Text, Contract}, {ContractSym}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_GE(V.findSec(".neverc.android.kernel.profile"), 0);
  EXPECT_NE(V.findSym("__neverc_android_kernel_profile_contract"), nullptr);
}

TEST(MergeELFSemantic, AndroidKernelModuleDropsProfileContractFingerprint) {
  SecSpec Text{".text", 0x20, 16, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SecSpec Contract{".neverc.android.kernel.profile",
                   8,
                   8,
                   ELF::SHT_PROGBITS,
                   ELF::SHF_ALLOC,
                   0xab};
  SymSpec ContractSym{"__neverc_android_kernel_profile_contract", 1, 0};
  ContractSym.Global = false;
  auto Obj =
      buildSectionedELF({Text, Contract}, {ContractSym}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.finalizeAndroidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_LT(V.findSec(".neverc.android.kernel.profile"), 0);
  EXPECT_EQ(V.findSym("__neverc_android_kernel_profile_contract"), nullptr);
  EXPECT_GE(V.findSec("__versions"), 0);
  EXPECT_GE(V.findSec(".codetag.alloc_tags"), 0);
}

TEST(MergeELFSemantic,
     AndroidKernelModuleSafeStripKeepsRelocationRequiredSymbols) {
  SecSpec Text{".text",
               0x100,
               16,
               ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
               0x5a};
  SecSpec Comment{".comment", 0x20, 1, ELF::SHT_PROGBITS, 0, 0x43};
  SecSpec ModInfo{".modinfo", 0x20, 1, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  SecSpec Ftrace{".text.ftrace_trampoline", 0x20, 16, ELF::SHT_PROGBITS,
                 ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SecSpec ThisModule{".gnu.linkonce.this_module", 0x40, 64, ELF::SHT_PROGBITS,
                     ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec Versions{"__versions", 0x40, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  SecSpec AllocTags{".codetag.alloc_tags", 8, 8, ELF::SHT_PROGBITS,
                    ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec Rodata{".rodata", 0x20, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  SecSpec Data{".data", 0x20, 8, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec Bss{".bss", 0x20, 8, ELF::SHT_NOBITS,
              ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec Plt{".plt", 0x20, 4, ELF::SHT_PROGBITS,
              ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SecSpec InitPlt{".init.plt", 0x20, 4, ELF::SHT_PROGBITS,
                  ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};

  SymSpec NeededLocal{"release_needed_local", 0, 0};
  NeededLocal.Global = false;
  SymSpec UnneededLocal{"release_unneeded_local", 0, 16};
  UnneededLocal.Global = false;
  SymSpec PublicDefinition{"release_public_definition", 0, 32};
  SymSpec NeededImport{"release_needed_import", -1, 0};
  SymSpec UnneededImport{"release_unneeded_import", -1, 0};
  SymSpec ModInfoSymbol{"release_modinfo_name", 2, 0};
  SymSpec FtraceSymbol{"release_ftrace_name", 3, 0};
  SymSpec ThisModuleSymbol{"__this_module", 4, 0};
  SymSpec VersionsSymbol{"release_versions_name", 5, 0};
  SymSpec AllocTagsSymbol{"release_alloc_tags_name", 6, 0};
  SymSpec InitModule{"init_module", 0, 48};
  SymSpec CleanupModule{"cleanup_module", 0, 64};
  SymSpec CFICheck{"__cfi_check", 0, 80};
  SymSpec CFICheckFail{"__cfi_check_fail", 0, 96};
  SymSpec CFIInitJumpTable{"__cfi_jt_init_module", 0, 112};
  SymSpec CFIExitJumpTable{"__cfi_jt_cleanup_module", 0, 128};
  SymSpec RodataSymbol{"release_rodata_name", 7, 0};
  RodataSymbol.Type = ELF::STT_OBJECT;
  SymSpec DataSymbol{"release_data_name", 8, 0};
  DataSymbol.Type = ELF::STT_OBJECT;
  SymSpec BssSymbol{"release_bss_name", 9, 0};
  BssSymbol.Type = ELF::STT_OBJECT;
  SymSpec PltSymbol{"release_plt_name", 10, 0};
  SymSpec InitPltSymbol{"release_init_plt_name", 11, 0};
  SymSpec TypeID{"__typeid__sample_global_addr", -1, 0x2a};
  TypeID.RawSectionIndex = ELF::SHN_ABS;
  TypeID.Type = ELF::STT_NOTYPE;
  SymSpec KCFITypeID{"__kcfi_typeid_sample", 0, 144};
  KCFITypeID.Type = ELF::STT_NOTYPE;

  RelSpec LocalReference{0, 0, "release_needed_local", ELF::R_AARCH64_ABS64, 3};
  RelSpec ImportReference{0, 8, "release_needed_import", ELF::R_AARCH64_ABS64,
                          7};
  RelSpec KCFIReference{0, 16, "__kcfi_typeid_sample", ELF::R_AARCH64_ABS64,
                        11};
  auto Obj = buildSectionedELF(
      {Text, Comment, ModInfo, Ftrace, ThisModule, Versions, AllocTags, Rodata,
       Data, Bss, Plt, InitPlt},
      {NeededLocal,    UnneededLocal,   PublicDefinition, NeededImport,
       UnneededImport, ModInfoSymbol,   FtraceSymbol,     ThisModuleSymbol,
       VersionsSymbol, AllocTagsSymbol, InitModule,       CleanupModule,
       CFICheck,       CFICheckFail,    CFIInitJumpTable, CFIExitJumpTable,
       RodataSymbol,   DataSymbol,      BssSymbol,        PltSymbol,
       InitPltSymbol,  TypeID,          KCFITypeID},
      {LocalReference, ImportReference, KCFIReference}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts = androidKernelReleaseOptions();
  neverc::AndroidKernelReleaseSymbolMap SymbolMap;
  Opts.releaseSymbolMap = &SymbolMap;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);
  EXPECT_EQ(SymbolMap.ImageSHA256,
            SHA256::hash(ArrayRef<uint8_t>(
                reinterpret_cast<const uint8_t *>(Out.data()), Out.size())));
  const auto HasMapEntry = [&](StringRef Original, StringRef Release) {
    return std::any_of(
        SymbolMap.Symbols.begin(), SymbolMap.Symbols.end(),
        [&](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
          return Entry.OriginalName == Original &&
                 Entry.ReleaseName == Release;
        });
  };
  EXPECT_TRUE(HasMapEntry("release_needed_local", "fn_0"));
  EXPECT_TRUE(HasMapEntry("release_public_definition", "fn_20"));
  EXPECT_FALSE(HasMapEntry("release_unneeded_local", "fn_10"));
  EXPECT_FALSE(HasMapEntry("release_needed_import", "release_needed_import"));

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_GE(V.findSec(".symtab"), 0);
  EXPECT_GE(V.findSec(".strtab"), 0);
  EXPECT_LT(V.findSec(".comment"), 0);
  EXPECT_EQ(V.findSym("release_needed_local"), nullptr);
  EXPECT_NE(V.findSym("fn_0"), nullptr);
  EXPECT_EQ(V.findSym("release_unneeded_local"), nullptr);
  EXPECT_EQ(V.findSym("release_public_definition"), nullptr);
  EXPECT_NE(V.findSym("fn_20"), nullptr);
  EXPECT_NE(V.findSym("release_needed_import"), nullptr);
  EXPECT_EQ(V.findSym("release_unneeded_import"), nullptr);
  EXPECT_NE(V.findSym("release_modinfo_name"), nullptr);
  EXPECT_NE(V.findSym("release_ftrace_name"), nullptr);
  EXPECT_NE(V.findSym("__this_module"), nullptr);
  EXPECT_NE(V.findSym("release_versions_name"), nullptr);
  EXPECT_NE(V.findSym("release_alloc_tags_name"), nullptr);
  EXPECT_NE(V.findSym("init_module"), nullptr);
  EXPECT_NE(V.findSym("cleanup_module"), nullptr);
  EXPECT_NE(V.findSym("__cfi_check"), nullptr);
  EXPECT_NE(V.findSym("__cfi_check_fail"), nullptr);
  EXPECT_NE(V.findSym("__cfi_jt_init_module"), nullptr);
  EXPECT_NE(V.findSym("__cfi_jt_cleanup_module"), nullptr);
  EXPECT_NE(V.findSym("__typeid__sample_global_addr"), nullptr);
  EXPECT_NE(V.findSym("__kcfi_typeid_sample"), nullptr);
  EXPECT_NE(V.findSym("obj_1C8"), nullptr);
  EXPECT_NE(V.findSym("obj_1E8"), nullptr);
  EXPECT_NE(V.findSym("obj_208"), nullptr);
  EXPECT_NE(V.findSym("fn_228"), nullptr);
  EXPECT_NE(V.findSym("fn_248"), nullptr);
  EXPECT_NE(V.findSym("__start_alloc_tags"), nullptr);
  EXPECT_NE(V.findSym("__stop_alloc_tags"), nullptr);
  ASSERT_EQ(V.Relas.size(), 3u);
  for (const ParsedRela &Relocation : V.Relas) {
    ASSERT_LT(Relocation.Sym, V.Syms.size());
    const StringRef Target = V.Syms[Relocation.Sym].Name;
    EXPECT_TRUE(Target == "fn_0" || Target == "release_needed_import" ||
                Target == "__kcfi_typeid_sample");
  }

  const StringRef Bytes(Out.data(), Out.size());
  EXPECT_FALSE(Bytes.contains("release_unneeded_local"));
  EXPECT_FALSE(Bytes.contains("release_unneeded_import"));
  EXPECT_FALSE(Bytes.contains("release_public_definition"));
  for (StringRef Original :
       {"release_rodata_name", "release_data_name", "release_bss_name",
        "release_plt_name", "release_init_plt_name"})
    EXPECT_FALSE(Bytes.contains(Original));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseNamesPartitionFunctionsFromFinalLayout) {
  SecSpec FirstText{".text.first",
                    0x10,
                    16,
                    ELF::SHT_PROGBITS,
                    ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
                    0x11};
  SecSpec SecondText{".text.second",
                     0x10,
                     16,
                     ELF::SHT_PROGBITS,
                     ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
                     0x22};
  SymSpec FirstFunction{"first_partition_function", 0, 0};
  SymSpec SecondFunction{"second_partition_function", 0, 0};

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(
      buildSectionedELF({FirstText}, {FirstFunction}, {}, ELF::EM_AARCH64));
  Inputs.push_back(
      buildSectionedELF({SecondText}, {SecondFunction}, {}, ELF::EM_AARCH64));

  Options Opts = androidKernelReleaseOptions();
  ASSERT_TRUE(Opts.verify);
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  const ParsedSym *First = View.findSym("fn_0");
  const ParsedSym *Second = View.findSym("fn_10");
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  EXPECT_EQ(First->Value, 0u);
  EXPECT_EQ(Second->Value, 0x10u);
  EXPECT_EQ(View.findSym("first_partition_function"), nullptr);
  EXPECT_EQ(View.findSym("second_partition_function"), nullptr);
}

TEST(MergeELFSemantic, AndroidKernelReleaseUsesTypeAwareFinalCoordinates) {
  SecSpec Text{
      ".text", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x11};
  SecSpec Rodata{".rodata", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC, 0x22};
  SecSpec Data{
      ".data", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x33};
  SecSpec Bss{".bss", 0x20, 16, ELF::SHT_NOBITS,
              ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec Metadata{".meta", 0x20, 1, ELF::SHT_PROGBITS, 0, 0x44};

  SymSpec Function{"release_function", 0, 0};
  SymSpec ExecutableLabel{"release_exec_label", 0, 8};
  ExecutableLabel.Type = ELF::STT_NOTYPE;
  SymSpec RodataObject{"release_rodata_object", 1, 0x10};
  RodataObject.Type = ELF::STT_OBJECT;
  SymSpec DataLabel{"release_data_label", 2, 0x10};
  DataLabel.Type = ELF::STT_NOTYPE;
  SymSpec DataObject{"release_data_object", 2, 0x10};
  DataObject.Type = ELF::STT_OBJECT;
  SymSpec BssObject{"release_bss_object", 3, 0x10};
  BssObject.Type = ELF::STT_OBJECT;
  SymSpec NonAllocatedLabel{"release_metadata_label", 4, 0x10};
  NonAllocatedLabel.Type = ELF::STT_NOTYPE;

  auto Input =
      buildSectionedELF({Text, Rodata, Data, Bss, Metadata},
                        {Function, ExecutableLabel, RodataObject, DataLabel,
                         DataObject, BssObject, NonAllocatedLabel},
                        {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(std::move(Input));

  Options Opts = androidKernelReleaseOptions();
  ASSERT_TRUE(Opts.verify);
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);

  for (StringRef Expected :
       {"fn_0", "code_8", "obj_30", "sym_50", "obj_50", "obj_70", "sym_S5_10"})
    EXPECT_NE(View.findSym(Expected), nullptr) << Expected.str();
  const StringRef OutputBytes(Output.data(), Output.size());
  for (StringRef Original :
       {"release_function", "release_exec_label", "release_rodata_object",
        "release_data_label", "release_data_object", "release_bss_object",
        "release_metadata_label"}) {
    EXPECT_EQ(View.findSym(Original), nullptr) << Original.str();
    EXPECT_FALSE(OutputBytes.contains(Original)) << Original.str();
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleasePlansBoundaryAliasesWithoutReorderingRelocations) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x11};
  SecSpec InitText{".init.text",
                   0x10,
                   16,
                   ELF::SHT_PROGBITS,
                   ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
                   0x22};
  SecSpec Data{
      ".data", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x33};
  SymSpec EndMarker{"release_text_end", 0, 0x10};
  EndMarker.Other = ELF::STV_HIDDEN;
  SymSpec StartMarker{"release_init_start", 1, 0};
  StartMarker.Other = ELF::STV_PROTECTED;
  RelSpec EndReference{2, 0, "release_text_end", ELF::R_AARCH64_ABS64, 3};
  RelSpec StartReference{2, 8, "release_init_start", ELF::R_AARCH64_ABS64, 7};

  auto Input =
      buildSectionedELF({Text, InitText, Data}, {EndMarker, StartMarker},
                        {EndReference, StartReference}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(std::move(Input));

  auto [OK, Output] = mergeELF(Inputs, androidKernelReleaseOptions());
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  const ParsedSym *End = View.findSym("fn_10");
  const ParsedSym *Start = View.findSym("fn_10_1");
  ASSERT_NE(End, nullptr);
  ASSERT_NE(Start, nullptr);
  EXPECT_EQ(End->Value, 0x10u);
  EXPECT_EQ(Start->Value, 0u);
  EXPECT_EQ(End->Bind, ELF::STB_GLOBAL);
  EXPECT_EQ(Start->Bind, ELF::STB_GLOBAL);
  EXPECT_EQ(End->Type, ELF::STT_FUNC);
  EXPECT_EQ(Start->Type, ELF::STT_FUNC);
  EXPECT_EQ(End->Other, ELF::STV_HIDDEN);
  EXPECT_EQ(Start->Other, ELF::STV_PROTECTED);

  ASSERT_EQ(View.Relas.size(), 2u);
  EXPECT_EQ(View.Relas[0].Offset, 0u);
  EXPECT_EQ(View.Relas[1].Offset, 8u);
  EXPECT_NE(View.Relas[0].Sym, View.Relas[1].Sym);
  ASSERT_LT(View.Relas[0].Sym, View.Syms.size());
  ASSERT_LT(View.Relas[1].Sym, View.Syms.size());
  EXPECT_EQ(View.Syms[View.Relas[0].Sym].Name, "fn_10");
  EXPECT_EQ(View.Syms[View.Relas[1].Sym].Name, "fn_10_1");
  EXPECT_FALSE(
      StringRef(Output.data(), Output.size()).contains("release_text_end"));
  EXPECT_FALSE(
      StringRef(Output.data(), Output.size()).contains("release_init_start"));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseAcceptsNameExchangeWithinExactObservableTie) {
  SecSpec Text{
      ".text", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x41};
  SecSpec Data{
      ".data", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x52};
  SymSpec First{"release_tied_first", 0, 0};
  First.Size = 8;
  First.Other = 0x80 | ELF::STV_HIDDEN;
  SymSpec Second{"release_tied_second", 0, 0};
  Second.Size = First.Size;
  Second.Other = First.Other;
  RelSpec FirstReference{1, 0, First.Name, ELF::R_AARCH64_ABS64, 3};
  RelSpec SecondReference{1, 8, Second.Name, ELF::R_AARCH64_ABS64, 7};
  auto Input =
      buildSectionedELF({Text, Data}, {First, Second},
                        {FirstReference, SecondReference}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(std::move(Input));
  Options Opts = androidKernelReleaseOptions();

  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  const ParsedSym *Base = View.findSym("fn_0");
  const ParsedSym *Alias = View.findSym("fn_0_1");
  ASSERT_NE(Base, nullptr);
  ASSERT_NE(Alias, nullptr);
  EXPECT_EQ(Base->Value, 0u);
  EXPECT_EQ(Alias->Value, 0u);
  EXPECT_EQ(Base->Size, 8u);
  EXPECT_EQ(Alias->Size, 8u);
  EXPECT_EQ(Base->Other, First.Other);
  EXPECT_EQ(Alias->Other, Second.Other);
  ASSERT_EQ(View.Relas.size(), 2u);
  ASSERT_LT(View.Relas[0].Sym, View.Syms.size());
  ASSERT_LT(View.Relas[1].Sym, View.Syms.size());
  EXPECT_EQ(View.Syms[View.Relas[0].Sym].Name, "fn_0");
  EXPECT_EQ(View.Syms[View.Relas[1].Sym].Name, "fn_0_1");

  // The final image cannot observe which source spelling owned which suffix:
  // all raw symbol fields are equal.  Both verifier entry points therefore
  // validate the canonical multiset and relocation targets existentially.
  SmallVector<char, 0> Exchanged(Output.begin(), Output.end());
  ASSERT_TRUE(swapELFSymbolNameOffsets(Exchanged, "fn_0", "fn_0_1"));
  std::string Error;
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Exchanged, Opts, &Error)) << Error;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Exchanged,
                          Format::ELF64LE, Opts, &Error))
      << Error;
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsNameExchangeAcrossDistinctStOther) {
  SecSpec Text{
      ".text", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x43};
  SecSpec Data{
      ".data", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x54};
  SymSpec Ordinary{"release_other_ordinary", 0, 0};
  Ordinary.Size = 8;
  Ordinary.Other = ELF::STV_HIDDEN;
  SymSpec VariantPCS{"release_other_variant_pcs", 0, 0};
  VariantPCS.Size = Ordinary.Size;
  VariantPCS.Other = ELF::STO_AARCH64_VARIANT_PCS | ELF::STV_HIDDEN;
  RelSpec OrdinaryReference{1, 0, Ordinary.Name, ELF::R_AARCH64_ABS64, 3};
  RelSpec VariantReference{1, 8, VariantPCS.Name, ELF::R_AARCH64_ABS64, 7};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text, Data}, {Ordinary, VariantPCS},
                                     {OrdinaryReference, VariantReference},
                                     ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();

  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  const ParsedSym *Base = View.findSym("fn_0");
  const ParsedSym *Alias = View.findSym("fn_0_1");
  ASSERT_NE(Base, nullptr);
  ASSERT_NE(Alias, nullptr);
  EXPECT_EQ(Base->Other & 3, Alias->Other & 3);
  EXPECT_NE(Base->Other, Alias->Other);

  SmallVector<char, 0> Exchanged(Output.begin(), Output.end());
  ASSERT_TRUE(swapELFSymbolNameOffsets(Exchanged, "fn_0", "fn_0_1"));
  std::string Error;
  EXPECT_FALSE(verifyAndroidKernelModuleImage(Exchanged, Opts, &Error));
  Error.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Exchanged,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseKeepsExactImportOwnershipOutsideGeneratedTies) {
  SecSpec Data{
      ".data", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x61};
  SymSpec FirstImport{"release_import_alpha", -1, 0};
  FirstImport.Type = ELF::STT_NOTYPE;
  SymSpec SecondImport{"release_import_bravo", -1, 0};
  SecondImport.Type = ELF::STT_NOTYPE;
  RelSpec FirstReference{0, 0, FirstImport.Name, ELF::R_AARCH64_ABS64, 3};
  RelSpec SecondReference{0, 8, SecondImport.Name, ELF::R_AARCH64_ABS64, 7};
  auto Input =
      buildSectionedELF({Data}, {FirstImport, SecondImport},
                        {FirstReference, SecondReference}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(std::move(Input));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  SmallVector<char, 0> Exchanged(Output.begin(), Output.end());
  ASSERT_TRUE(swapELFSymbolNameOffsets(Exchanged, "release_import_alpha",
                                       "release_import_bravo"));
  std::string Error;
  // An output-only audit can prove that both exact imports exist but has no
  // source provenance with which to assign them to otherwise-identical slots.
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Exchanged, Opts, &Error)) << Error;
  // Input-aware replay keeps every exact-name entry singleton and rejects the
  // ownership swap (including the corresponding relocation-target change).
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Exchanged,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseReplayTracksExactRelocationSymbolIndex) {
  SecSpec Text{
      ".text", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x71};
  SecSpec Data{
      ".data", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x72};
  SymSpec First{"__typeid__same_protected_name", 0, 0};
  First.Global = false;
  SymSpec Second = First;
  Second.Value = 8;
  RelSpec FirstReference{1, 0, First.Name, ELF::R_AARCH64_ABS64, 5};
  RelSpec SecondReference{1, 8, First.Name, ELF::R_AARCH64_ABS64, 6};

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(buildSectionedELF({Text, Data}, {First},
                                     {FirstReference, SecondReference},
                                     ELF::EM_AARCH64));
  Inputs.push_back(buildSectionedELF({Text, Data}, {Second}, {FirstReference},
                                     ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  ASSERT_EQ(View.Relas.size(), 3u);
  ASSERT_NE(View.Relas[0].Sym, View.Relas[2].Sym);

  SmallVector<char, 0> Retargeted(Output.begin(), Output.end());
  ASSERT_TRUE(patchFirstRelaSymbolIndex(Retargeted, View.Relas[2].Sym));
  std::string Error;
  // The spelling is unchanged because both local targets intentionally have
  // the same exact protected name. Only provenance-aware final-index replay
  // can observe the retarget.
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Retargeted, Opts, &Error))
      << Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Retargeted,
                           Format::ELF64LE, Opts, &Error))
      << Error;
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseReplayTracksEmptyNonSectionRelocationTarget) {
  SecSpec Text{
      ".text", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x73};
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0x74};
  SymSpec EmptyTarget{"", 0, 0};
  EmptyTarget.Global = true;
  EmptyTarget.Type = ELF::STT_NOTYPE;
  RelSpec Reference{1, 0, "", ELF::R_AARCH64_ABS64, 9};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text, Data}, {EmptyTarget}, {Reference},
                                     ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  ASSERT_EQ(View.Relas.size(), 1u);
  ASSERT_NE(View.Relas[0].Sym, 0u);

  SmallVector<char, 0> Retargeted(Output.begin(), Output.end());
  ASSERT_TRUE(patchFirstRelaSymbolIndex(Retargeted, 0));
  std::string Error;
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Retargeted, Opts, &Error))
      << Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Retargeted,
                           Format::ELF64LE, Opts, &Error))
      << Error;
}

TEST(MergeELFSemantic, AndroidKernelReleaseScalesOneLargeExactTieClass) {
  constexpr unsigned AliasCount = 2048;
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x63};
  SecSpec Data{".data",
               AliasCount * 8,
               8,
               ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE,
               0x64};
  std::vector<SymSpec> Aliases;
  std::vector<RelSpec> References;
  Aliases.reserve(AliasCount);
  References.reserve(AliasCount);
  for (unsigned I = 0; I < AliasCount; ++I) {
    Aliases.push_back({"release_stress_alias_" + std::to_string(I), 0, 0});
    References.push_back(
        {1, I * 8, Aliases.back().Name, ELF::R_AARCH64_ABS64, I});
  }
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data}, Aliases, References, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  EXPECT_NE(View.findSym("fn_0"), nullptr);
  EXPECT_NE(View.findSym("fn_0_2047"), nullptr);
  ASSERT_EQ(View.Relas.size(), AliasCount);
  std::string Error;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                          Format::ELF64LE, Opts, &Error))
      << Error;
  // This large single-class shape exercises shared class storage and direct
  // final-index relocation replay without making correctness depend on a
  // machine-specific wall-clock threshold. The verifier implementation keeps
  // one sorted name vector per class, never one copy per member.
}

TEST(MergeELFSemantic, AndroidKernelReleaseNamesAbsoluteSymbolsFromValue) {
  SecSpec Text{
      ".text", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x90};
  SymSpec Absolute{"release_absolute_name", -1, 0x1234};
  Absolute.RawSectionIndex = ELF::SHN_ABS;
  auto Obj = buildSectionedELF({Text}, {Absolute}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts = androidKernelReleaseOptions();
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Out);
  ASSERT_TRUE(View.Ok);
  EXPECT_EQ(View.findSym("release_absolute_name"), nullptr);
  const ParsedSym *Symbol = View.findSym("abs_1234");
  ASSERT_NE(Symbol, nullptr);
  EXPECT_EQ(Symbol->Shndx, ELF::SHN_ABS);
  EXPECT_EQ(Symbol->Value, 0x1234u);
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseInputReplayRejectsSynchronizedAbsoluteTamper) {
  SecSpec Text{
      ".text", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x72};
  SymSpec Absolute{"release_absolute_tamper", -1, 0x1234};
  Absolute.RawSectionIndex = ELF::SHN_ABS;
  auto Input = buildSectionedELF({Text}, {Absolute}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(std::move(Input));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  SmallVector<char, 0> Tampered(Output.begin(), Output.end());
  ASSERT_TRUE(patchSymValue(Tampered, "abs_1234", 0x5678));
  ASSERT_TRUE(patchELFSymbolNameSameLength(Tampered, "abs_1234", "abs_5678"));
  std::string Error;
  // The serialized image is internally self-consistent: its exact structural
  // name follows its (tampered) absolute value.
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Tampered, Opts, &Error)) << Error;
  // Independent input replay must additionally preserve the source value.
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Tampered,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic, AndroidKernelReleaseRejectsUnsupportedSymbolIndices) {
  struct Case {
    const char *Name;
    uint16_t Index;
  };
  constexpr Case Cases[] = {
      {"release_common", ELF::SHN_COMMON},
      {"release_livepatch",
       neverc::AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex},
      {"release_xindex", ELF::SHN_XINDEX},
      {"release_unknown_reserved", UINT16_C(0xff30)},
  };

  for (const Case &Entry : Cases) {
    SCOPED_TRACE(Entry.Name);
    SecSpec Text{".text", 0x20, 16, ELF::SHT_PROGBITS,
                 ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
    SymSpec Special{Entry.Name, -1, 0};
    Special.RawSectionIndex = Entry.Index;
    auto Obj = buildSectionedELF({Text}, {Special}, {}, ELF::EM_AARCH64);
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Bufs;
      Bufs.push_back(Obj);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Bufs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }

  SecSpec Text{".text", 0x20, 16, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SymSpec Function{"release_output_xindex", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ASSERT_TRUE(patchELFSymbolSectionIndex(Output, "fn_0", ELF::SHN_XINDEX));
  std::string Error;
  EXPECT_FALSE(verifyAndroidKernelModuleImage(Output, Opts, &Error));
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic, AndroidKernelReleaseRejectsLivepatchSections) {
  SecSpec Text{".text", 0x20, 16, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SecSpec NamedLivepatch{".klp.rela.vmlinux.text.target", 0, 8, ELF::SHT_RELA,
                         0};
  SecSpec FlaggedLivepatch{
      ".rela.livepatch", 0, 8, ELF::SHT_RELA,
      neverc::AndroidKernelModuleSymbolPolicy::LivePatchRelocationSectionFlag};

  for (const SecSpec &Livepatch : {NamedLivepatch, FlaggedLivepatch}) {
    SCOPED_TRACE(Livepatch.Name);
    auto Obj = buildSectionedELF({Text, Livepatch}, {}, {}, ELF::EM_AARCH64);
    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(std::move(Obj));
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = false;
    EXPECT_FALSE(mergeELF(Bufs, Opts).first);
  }

  // `.modinfo` is the authoritative module-class marker. A livepatch module
  // may legitimately have no `.klp.*` relocation section yet, so the marker
  // alone must still make release structural renaming fail closed.
  SecSpec ModInfo{".modinfo", 0x20, 1, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  auto Marked = buildSectionedELF({Text, ModInfo}, {}, {}, ELF::EM_AARCH64);
  ASSERT_TRUE(overwriteELFSectionContents(Marked, ".modinfo", "livepatch=Y"));
  SmallVector<SmallVector<char, 0>, 1> MarkedBuffers;
  MarkedBuffers.push_back(std::move(Marked));
  Options MarkedOptions = androidKernelReleaseOptions();
  MarkedOptions.verify = false;
  std::string AuditError;
  EXPECT_FALSE(verifyAndroidKernelModuleImage(MarkedBuffers.front(),
                                              MarkedOptions, &AuditError));
  EXPECT_NE(AuditError.find(".modinfo"), std::string::npos) << AuditError;
  EXPECT_NE(AuditError.find("livepatch"), std::string::npos) << AuditError;
  EXPECT_FALSE(mergeELF(MarkedBuffers, MarkedOptions).first);
}

TEST(MergeELFSemantic, AndroidKernelReleasePreservesIndependentMetadataNames) {
  SecSpec Text{".text", 0x20, 16, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_EXECINSTR};
  SecSpec BTF{".BTF", 0x40, 1, ELF::SHT_PROGBITS, 0};
  SecSpec Versions{"__versions", 0x40, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  SecSpec ExportNames{"__ksymtab_strings", 0x40, 1, ELF::SHT_PROGBITS,
                      ELF::SHF_ALLOC};
  SecSpec ModInfo{".modinfo", 0x40, 1, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  SymSpec Definition{"release_metadata_anchor", 0, 0};
  auto Obj = buildSectionedELF({Text, BTF, Versions, ExportNames, ModInfo},
                               {Definition}, {}, ELF::EM_AARCH64);

  struct Payload {
    const char *Section;
    const char *Bytes;
  };
  constexpr Payload Payloads[] = {
      {".BTF", "btf_original_function_name"},
      {"__versions", "versioned_original_import"},
      {"__ksymtab_strings", "exported_original_symbol"},
      {".modinfo", "description=original module name"},
  };
  for (const Payload &Entry : Payloads)
    ASSERT_TRUE(overwriteELFSectionContents(Obj, Entry.Section, Entry.Bytes));

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  auto [OK, Out] = mergeELF(Bufs, androidKernelReleaseOptions());
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Out);
  ASSERT_TRUE(View.Ok);
  for (const Payload &Entry : Payloads) {
    SCOPED_TRACE(Entry.Section);
    const int Index = View.findSec(Entry.Section);
    ASSERT_GE(Index, 0);
    const ParsedSec &Section = View.Secs[(unsigned)Index];
    StringRef Bytes(reinterpret_cast<const char *>(Section.Data.data()),
                    Section.Data.size());
    EXPECT_TRUE(Bytes.starts_with(Entry.Bytes));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseVerifiersRejectStructuralNameTamper) {
  SecSpec Padding{".release.padding", 0xC000,         1,
                  ELF::SHT_PROGBITS,  ELF::SHF_ALLOC, 0x81};
  SecSpec Text{
      ".text", 0x20, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x82};
  SecSpec Data{
      ".data", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE,
      0x83};
  SymSpec FirstDefinition{"release_tamper_first", 1, 0};
  SymSpec AliasDefinition{"release_tamper_alias", 1, 0};
  SymSpec LeadingZeroSpelling{"fn_0C000", -1, 0};
  LeadingZeroSpelling.Type = ELF::STT_NOTYPE;
  SymSpec RawValueSpelling{"fn_0", -1, 0};
  RawValueSpelling.Type = ELF::STT_NOTYPE;
  RelSpec KeepLeadingZero{2, 0, LeadingZeroSpelling.Name, ELF::R_AARCH64_ABS64,
                          3};
  RelSpec KeepRawValue{2, 8, RawValueSpelling.Name, ELF::R_AARCH64_ABS64, 7};
  auto Obj = buildSectionedELF(
      {Padding, Text, Data},
      {FirstDefinition, AliasDefinition, LeadingZeroSpelling, RawValueSpelling},
      {KeepLeadingZero, KeepRawValue}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  std::string Error;
  ASSERT_TRUE(verifyAndroidKernelModuleImage(Out, Opts, &Error)) << Error;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs), Out,
                          Format::ELF64LE, Opts, &Error))
      << Error;

  auto ExpectBothReject = [&](ArrayRef<char> Corrupt, StringRef Case) {
    SCOPED_TRACE(Case.str());
    Error.clear();
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Corrupt, Opts, &Error));
    Error.clear();
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs), Corrupt,
                             Format::ELF64LE, Opts, &Error));
  };

  SmallVector<char, 0> WrongAddress(Out.begin(), Out.end());
  ASSERT_TRUE(patchELFSymbolNameSameLength(WrongAddress, "fn_C000", "fn_C001"));
  ExpectBothReject(WrongAddress, "wrong canonical EA");

  SmallVector<char, 0> Lowercase(Out.begin(), Out.end());
  ASSERT_TRUE(patchELFSymbolNameSameLength(Lowercase, "fn_C000", "fn_c000"));
  ExpectBothReject(Lowercase, "lowercase hexadecimal");

  SmallVector<char, 0> LeadingZero(Out.begin(), Out.end());
  ASSERT_TRUE(retargetELFSymbolNameOffset(LeadingZero, "fn_C000", "fn_0C000"));
  ExpectBothReject(LeadingZero, "redundant leading zero");

  SmallVector<char, 0> RawValueName(Out.begin(), Out.end());
  ASSERT_TRUE(retargetELFSymbolNameOffset(RawValueName, "fn_C000", "fn_0"));
  ExpectBothReject(RawValueName, "raw st_value instead of canonical EA");

  SmallVector<char, 0> SkippedAlias(Out.begin(), Out.end());
  ASSERT_TRUE(
      patchELFSymbolNameSameLength(SkippedAlias, "fn_C000_1", "fn_C000_2"));
  ExpectBothReject(SkippedAlias, "skipped alias suffix");

  SmallVector<char, 0> DuplicateAlias(Out.begin(), Out.end());
  ASSERT_TRUE(
      retargetELFSymbolNameOffset(DuplicateAlias, "fn_C000_1", "fn_C000"));
  ExpectBothReject(DuplicateAlias, "duplicate alias suffix");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseInputReplayRejectsWritableFlagRemoval) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x84};
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0x85};
  SymSpec Function{"release_flag_anchor", 0, 0};
  SymSpec Object{"release_writable_object", 1, 0};
  Object.Type = ELF::STT_OBJECT;
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data}, {Function, Object}, {}, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  SmallVector<char, 0> Tampered(Output.begin(), Output.end());
  ELF::Elf64_Shdr *DataHeader = findELFSectionHeader(Tampered, ".data");
  ASSERT_NE(DataHeader, nullptr);
  DataHeader->sh_flags &= ~uint64_t(ELF::SHF_WRITE);
  std::string Error;
  // Output-only verification has no source section contract from which to
  // infer writability. Input-aware replay must compare the complete folded
  // header flags and reject this ABI-changing mutation.
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Tampered, Opts, &Error)) << Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Tampered,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseVerifiersRejectUnsupportedSerializedSymbolTypes) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x86};
  SymSpec Function{"release_serialized_type", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  struct TypeCase {
    const char *Name;
    uint8_t Type;
  };
  constexpr TypeCase Cases[] = {
      {"TLS", ELF::STT_TLS},
      {"GNU IFUNC", ELF::STT_GNU_IFUNC},
      {"nonempty FILE", ELF::STT_FILE},
      {"format extension", UINT8_C(0x0f)},
  };
  for (const TypeCase &Entry : Cases) {
    SCOPED_TRACE(Entry.Name);
    SmallVector<char, 0> Tampered(Output.begin(), Output.end());
    ASSERT_TRUE(patchELFSymbolInfo(
        Tampered, "fn_0", uint8_t((ELF::STB_GLOBAL << 4) | Entry.Type)));
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Tampered, Opts, &Error));
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Tampered,
                             Format::ELF64LE, Opts, &Error));
  }
}

TEST(MergeELFSemantic, AndroidKernelReleasePlanningFailuresCommitNoOutput) {
  auto ExpectAtomicFailure = [&](SmallVector<char, 0> Input, StringRef Case) {
    SCOPED_TRACE(Case.str());
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  };

  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0x91};
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0x92};

  SymSpec PastEnd{"release_past_end", 0, 0x11};
  ExpectAtomicFailure(buildSectionedELF({Text}, {PastEnd}, {}, ELF::EM_AARCH64),
                      "invalid section-relative value");

  SymSpec ExtendsPastEnd{"release_extent_past_end", 0, 0xF};
  ExtendsPastEnd.Size = 2;
  ExpectAtomicFailure(
      buildSectionedELF({Text}, {ExtendsPastEnd}, {}, ELF::EM_AARCH64),
      "invalid section-relative extent");

  SymSpec BaseDefinition{"release_base_collision", 0, 0};
  SymSpec BaseImport{"fn_0", -1, 0};
  BaseImport.Type = ELF::STT_NOTYPE;
  RelSpec KeepBaseImport{1, 0, BaseImport.Name, ELF::R_AARCH64_ABS64, 0};
  ExpectAtomicFailure(buildSectionedELF({Text, Data},
                                        {BaseDefinition, BaseImport},
                                        {KeepBaseImport}, ELF::EM_AARCH64),
                      "reserved generated base collision");

  SymSpec FirstAlias{"release_alias_first", 0, 0};
  SymSpec SecondAlias{"release_alias_second", 0, 0};
  SymSpec AliasImport{"fn_0_1", -1, 0};
  AliasImport.Type = ELF::STT_NOTYPE;
  RelSpec KeepAliasImport{1, 0, AliasImport.Name, ELF::R_AARCH64_ABS64, 0};
  ExpectAtomicFailure(buildSectionedELF({Text, Data},
                                        {FirstAlias, SecondAlias, AliasImport},
                                        {KeepAliasImport}, ELF::EM_AARCH64),
                      "reserved generated alias collision");

  struct UnsupportedType {
    const char *Name;
    uint8_t Type;
  };
  constexpr UnsupportedType Unsupported[] = {
      {"TLS", ELF::STT_TLS},
      {"GNU_IFUNC", ELF::STT_GNU_IFUNC},
      {"FILE", ELF::STT_FILE},
      {"format extension", UINT8_C(0x0f)},
  };
  for (const UnsupportedType &Entry : Unsupported) {
    SymSpec Symbol{"release_unsupported_type", 0, 0};
    Symbol.Type = Entry.Type;
    ExpectAtomicFailure(
        buildSectionedELF({Text}, {Symbol}, {}, ELF::EM_AARCH64), Entry.Name);
  }

  SecSpec HugeBss{".bss.huge", std::numeric_limits<uint64_t>::max(), 1,
                  ELF::SHT_NOBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SymSpec FollowingFunction{"release_after_layout_overflow", 1, 0};
  ExpectAtomicFailure(buildSectionedELF({HugeBss, Text}, {FollowingFunction},
                                        {}, ELF::EM_AARCH64),
                      "canonical section-layout overflow");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsUnrecognizedInputStringTablesAtomically) {
  using namespace ELF;

  auto ExpectAtomicFailure = [](SmallVector<char, 0> Input, StringRef Case) {
    SCOPED_TRACE(Case.str());
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      testing::internal::CaptureStderr();
      auto [OK, Output] = mergeELF(Inputs, Opts);
      const std::string Diagnostic = testing::internal::GetCapturedStderr();
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
      EXPECT_NE(Diagnostic.find("additional string table"), std::string::npos)
          << Diagnostic;
    }
  };

  auto ExpectValidAllZeroStringTable = [](ArrayRef<char> Input,
                                          StringRef Name) {
    ElfView View = parseELF(Input);
    ASSERT_TRUE(View.Ok);
    const int Index = View.findSec(Name);
    ASSERT_GE(Index, 0);
    const ParsedSec &Section = View.Secs[Index];
    ASSERT_EQ(Section.Type, ELF::SHT_STRTAB);
    ASSERT_FALSE(Section.Data.empty());
    EXPECT_EQ(Section.Data.front(), 0u);
    EXPECT_EQ(Section.Data.back(), 0u);
    EXPECT_TRUE(
        llvm::all_of(Section.Data, [](uint8_t Byte) { return Byte == 0; }));
  };

  // A third string table is not one of the two metadata tables identified by
  // e_shstrndx and the selected symtab's sh_link. The canonical release format
  // cannot serialize it, so even an otherwise unreferenced table must fail
  // closed instead of disappearing from the output.
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x93};
  SecSpec ExtraStrings{".custom_strings", 8, 1, SHT_STRTAB, 0, 0};
  SymSpec Function{"release_extra_strtab", 0, 0};
  const auto ExtraInput =
      buildSectionedELF({Text, ExtraStrings}, {Function}, {}, EM_AARCH64);
  ExpectValidAllZeroStringTable(ExtraInput, ExtraStrings.Name);
  ExpectAtomicFailure(ExtraInput, "unreferenced third SHT_STRTAB");

  SmallVector<SmallVector<char, 0>, 1> ReferenceInputs;
  ReferenceInputs.push_back(
      buildSectionedELF({Text}, {Function}, {}, EM_AARCH64));
  Options AuditOptions = androidKernelReleaseOptions();
  auto [ReferenceOK, ReferenceOutput] = mergeELF(ReferenceInputs, AuditOptions);
  ASSERT_TRUE(ReferenceOK);
  SmallVector<SmallVector<char, 0>, 1> ExtraAuditInputs;
  ExtraAuditInputs.push_back(ExtraInput);
  std::string Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(ExtraAuditInputs),
                           ReferenceOutput, Format::ELF64LE, AuditOptions,
                           &Error));

  // This is the dangerous form of the same schema violation: a defined object
  // lives in an allocated custom string-table section and a retained .data
  // relocation targets it. Treating every SHT_STRTAB as regenerated used to
  // drop the section and silently re-home the definition to SHN_UNDEF.
  SecSpec AllocatedStrings{".allocated_strings", 8,         1,
                           SHT_STRTAB,           SHF_ALLOC, 0};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0x96};
  SymSpec Object{"release_allocated_strtab_object", 0, 0};
  Object.Type = STT_OBJECT;
  Object.Size = 8;
  RelSpec Reference{1, 0, Object.Name, R_AARCH64_ABS64, 0};
  const auto AllocatedInput = buildSectionedELF(
      {AllocatedStrings, Data}, {Object}, {Reference}, EM_AARCH64);
  ExpectValidAllZeroStringTable(AllocatedInput, AllocatedStrings.Name);
  const ElfView AllocatedView = parseELF(AllocatedInput);
  ASSERT_TRUE(AllocatedView.Ok);
  const int AllocatedSection = AllocatedView.findSec(AllocatedStrings.Name);
  const int DataSection = AllocatedView.findSec(Data.Name);
  ASSERT_GE(AllocatedSection, 0);
  ASSERT_GE(DataSection, 0);
  const ParsedSym *AllocatedObject = AllocatedView.findSym(Object.Name);
  ASSERT_NE(AllocatedObject, nullptr);
  EXPECT_EQ(AllocatedObject->Shndx, static_cast<uint16_t>(AllocatedSection));
  EXPECT_EQ(AllocatedObject->Type, STT_OBJECT);
  ASSERT_EQ(AllocatedView.Relas.size(), 1u);
  EXPECT_EQ(AllocatedView.Relas[0].TargetSec,
            static_cast<uint32_t>(DataSection));
  EXPECT_EQ(AllocatedView.Relas[0].Type, R_AARCH64_ABS64);
  ASSERT_LT(AllocatedView.Relas[0].Sym, AllocatedView.Syms.size());
  EXPECT_EQ(AllocatedView.Syms[AllocatedView.Relas[0].Sym].Name, Object.Name);
  ExpectAtomicFailure(
      AllocatedInput,
      "allocated third SHT_STRTAB with a referenced definition");

  SymSpec UndefinedObject = Object;
  UndefinedObject.SecIdx = -1;
  RelSpec UndefinedReference = Reference;
  UndefinedReference.SecIdx = 0;
  SmallVector<SmallVector<char, 0>, 1> UndefinedInputs;
  UndefinedInputs.push_back(buildSectionedELF(
      {Data}, {UndefinedObject}, {UndefinedReference}, EM_AARCH64));
  auto [UndefinedOK, UndefinedOutput] = mergeELF(UndefinedInputs, AuditOptions);
  ASSERT_TRUE(UndefinedOK);
  SmallVector<SmallVector<char, 0>, 1> AllocatedAuditInputs;
  AllocatedAuditInputs.push_back(AllocatedInput);
  Error.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(AllocatedAuditInputs),
                           UndefinedOutput, Format::ELF64LE, AuditOptions,
                           &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseCanonicalizesOneSharedInputStringTable) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x9e};
  SmallVector<char, 0> SharedInput =
      buildSectionedELF({Text}, {}, {}, EM_AARCH64);

  auto *InputHeader = reinterpret_cast<Elf64_Ehdr *>(SharedInput.data());
  auto *InputSections =
      reinterpret_cast<Elf64_Shdr *>(SharedInput.data() + InputHeader->e_shoff);
  Elf64_Shdr *InputSymtab = findELFSectionHeader(SharedInput, ".symtab");
  Elf64_Shdr *OldSymbolStrings = findELFSectionHeader(SharedInput, ".strtab");
  ASSERT_NE(InputSymtab, nullptr);
  ASSERT_NE(OldSymbolStrings, nullptr);
  ASSERT_LT(InputHeader->e_shstrndx, InputHeader->e_shnum);
  ASSERT_EQ(InputSections[InputHeader->e_shstrndx].sh_type, SHT_STRTAB);

  // With only symbol[0], the section-name pool is also a valid symbol-name
  // pool. Retire the builder's otherwise-extra .strtab slot and point the sole
  // symtab at e_shstrndx, yielding one input SHT_STRTAB with both identities.
  OldSymbolStrings->sh_type = SHT_NULL;
  InputSymtab->sh_link = InputHeader->e_shstrndx;
  ASSERT_EQ(InputSymtab->sh_link, InputHeader->e_shstrndx);
  ASSERT_EQ(InputSymtab->sh_entsize, sizeof(Elf64_Sym));
  ASSERT_EQ(InputSymtab->sh_size, sizeof(Elf64_Sym));
  ASSERT_LE(InputSymtab->sh_offset, SharedInput.size() - sizeof(Elf64_Sym));
  const auto *NullSymbol = reinterpret_cast<const Elf64_Sym *>(
      SharedInput.data() + InputSymtab->sh_offset);
  EXPECT_EQ(NullSymbol->st_name, 0u);
  const Elf64_Shdr &SharedStrings = InputSections[InputHeader->e_shstrndx];
  ASSERT_GT(SharedStrings.sh_size, 0u);
  ASSERT_LE(SharedStrings.sh_offset,
            SharedInput.size() - SharedStrings.sh_size);
  EXPECT_EQ(SharedInput[SharedStrings.sh_offset], '\0');
  EXPECT_EQ(SharedInput[SharedStrings.sh_offset + SharedStrings.sh_size - 1],
            '\0');
  EXPECT_EQ(
      llvm::count_if(ArrayRef<Elf64_Shdr>(InputSections, InputHeader->e_shnum),
                     [](const Elf64_Shdr &Section) {
                       return Section.sh_type == SHT_STRTAB;
                     }),
      1u);

  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(SharedInput);
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    ASSERT_TRUE(OK);

    auto *OutputHeader = reinterpret_cast<Elf64_Ehdr *>(Output.data());
    auto *OutputSections =
        reinterpret_cast<Elf64_Shdr *>(Output.data() + OutputHeader->e_shoff);
    Elf64_Shdr *OutputSymtab = findELFSectionHeader(Output, ".symtab");
    Elf64_Shdr *OutputStrtab = findELFSectionHeader(Output, ".strtab");
    Elf64_Shdr *OutputShstrtab = findELFSectionHeader(Output, ".shstrtab");
    ASSERT_NE(OutputSymtab, nullptr);
    ASSERT_NE(OutputStrtab, nullptr);
    ASSERT_NE(OutputShstrtab, nullptr);
    const unsigned OutputStrtabIndex = OutputStrtab - OutputSections;
    const unsigned OutputShstrtabIndex = OutputShstrtab - OutputSections;
    EXPECT_NE(OutputStrtabIndex, OutputShstrtabIndex);
    EXPECT_EQ(OutputSymtab->sh_link, OutputStrtabIndex);
    EXPECT_EQ(OutputHeader->e_shstrndx, OutputShstrtabIndex);
    EXPECT_EQ(llvm::count_if(
                  ArrayRef<Elf64_Shdr>(OutputSections, OutputHeader->e_shnum),
                  [](const Elf64_Shdr &Section) {
                    return Section.sh_type == SHT_STRTAB;
                  }),
              2u);

    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
    Error.clear();
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                            Format::ELF64LE, Opts, &Error))
        << Error;
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsMalformedVersionsInputShapesAtomically) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xa0};
  SymSpec Function{"release_bad_versions_shape", 0, 0};

  auto BuildCanonicalOutput = [&](uint64_t Size) {
    SecSpec Versions{"__versions", Size, 8, SHT_PROGBITS, SHF_ALLOC, 0xa1};
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(
        buildSectionedELF({Text, Versions}, {Function}, {}, EM_AARCH64));
    return mergeELF(Inputs, androidKernelReleaseOptions());
  };
  auto [CanonicalZeroOK, CanonicalZeroOutput] = BuildCanonicalOutput(0);
  auto [Canonical64OK, Canonical64Output] = BuildCanonicalOutput(64);
  ASSERT_TRUE(CanonicalZeroOK);
  ASSERT_TRUE(Canonical64OK);
  Options AuditOptions = androidKernelReleaseOptions();
  std::string Error;
  ASSERT_TRUE(
      verifyAndroidKernelModuleImage(CanonicalZeroOutput, AuditOptions, &Error))
      << Error;
  Error.clear();
  ASSERT_TRUE(
      verifyAndroidKernelModuleImage(Canonical64Output, AuditOptions, &Error))
      << Error;

  auto ExpectRejected = [&](SecSpec Versions, ArrayRef<char> CanonicalOutput,
                            StringRef Case, StringRef ProducerDiagnostic,
                            StringRef VerifierDiagnostic) {
    SCOPED_TRACE(Case.str());
    SmallVector<char, 0> Input =
        buildSectionedELF({Text, Versions}, {Function}, {}, EM_AARCH64);
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      testing::internal::CaptureStderr();
      auto [OK, Output] = mergeELF(Inputs, Opts);
      const std::string Diagnostic = testing::internal::GetCapturedStderr();
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
      EXPECT_NE(Diagnostic.find(ProducerDiagnostic.str()), std::string::npos)
          << Diagnostic;
    }

    SmallVector<SmallVector<char, 0>, 1> AuditInputs;
    AuditInputs.push_back(std::move(Input));
    Error.clear();
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(AuditInputs),
                             CanonicalOutput, Format::ELF64LE, AuditOptions,
                             &Error));
    EXPECT_NE(Error.find(VerifierDiagnostic.str()), std::string::npos) << Error;
  };

  constexpr StringLiteral ShapeDiagnostic =
      "input __versions must be an allocated, uncompressed SHT_PROGBITS "
      "section";
  constexpr StringLiteral AlignmentDiagnostic =
      "input __versions alignment must be a power of two >= 8";

  // A named inactive slot used to disappear with regenerated SHT_NULL
  // metadata, after which finalization synthesized a valid empty table. All
  // non-type attributes are canonical so this isolates the type contract.
  ExpectRejected(SecSpec{"__versions", 0, 8, SHT_NULL, SHF_ALLOC, 0},
                 CanonicalZeroOutput, "wrong type", ShapeDiagnostic,
                 ShapeDiagnostic);

  // EnsureSection historically ORed SHF_ALLOC into this contribution and
  // raised each underspecified alignment to 8, hiding malformed input.
  ExpectRejected(SecSpec{"__versions", 64, 8, SHT_PROGBITS, 0, 0xa1},
                 Canonical64Output, "missing SHF_ALLOC", ShapeDiagnostic,
                 ShapeDiagnostic);
  for (uint32_t Alignment : {0u, 1u, 4u}) {
    SCOPED_TRACE(Alignment);
    ExpectRejected(
        SecSpec{"__versions", 64, Alignment, SHT_PROGBITS, SHF_ALLOC, 0xa1},
        Canonical64Output, "alignment below 8", AlignmentDiagnostic,
        AlignmentDiagnostic);
  }

  // Non-power-of-two sh_addralign is not a legal ELF input shape and is
  // already covered at the generic strict-parser boundary by
  // AndroidKernelReleaseRejectsInvalidSectionAlignmentAtomically. Do not make
  // this named-contract matrix pass for the wrong (unparseable-input) reason.
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsCompressedVersionsInputAtomically) {
  using namespace ELF;
  const compression::Format CompressionFormat = compression::Format::Zlib;
  if (const char *Reason =
          compression::getReasonIfUnsupported(CompressionFormat))
    GTEST_SKIP() << Reason;

  constexpr uint64_t VersionsSize = 64;
  constexpr uint8_t VersionsFill = 0xa2;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xa3};
  SymSpec Function{"release_compressed_versions", 0, 0};
  SmallVector<uint8_t, 0> LogicalVersions(VersionsSize, VersionsFill);
  SmallVector<uint8_t, 0> Compressed;
  compression::compress(compression::Params(CompressionFormat), LogicalVersions,
                        Compressed);
  ASSERT_LE(sizeof(Elf64_Chdr) + Compressed.size(), VersionsSize);

  SecSpec CompressedVersions{"__versions",
                             VersionsSize,
                             8,
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_COMPRESSED,
                             0};
  SmallVector<char, 0> Input =
      buildSectionedELF({Text, CompressedVersions}, {Function}, {}, EM_AARCH64);
  Elf64_Shdr *VersionsHeader = findELFSectionHeader(Input, "__versions");
  ASSERT_NE(VersionsHeader, nullptr);
  ASSERT_LE(VersionsHeader->sh_offset, Input.size() - VersionsHeader->sh_size);
  char *Encoded = Input.data() + VersionsHeader->sh_offset;
  memset(Encoded, 0, VersionsHeader->sh_size);
  Elf64_Chdr CompressionHeader{};
  CompressionHeader.ch_type = ELFCOMPRESS_ZLIB;
  CompressionHeader.ch_size = VersionsSize;
  CompressionHeader.ch_addralign = 8;
  memcpy(Encoded, &CompressionHeader, sizeof(CompressionHeader));
  memcpy(Encoded + sizeof(CompressionHeader), Compressed.data(),
         Compressed.size());

  // Prove the padded 64-byte on-disk payload is a valid ELF compression frame
  // whose logical contribution is also exactly one 64-byte version record.
  SmallVector<uint8_t, 0> Decoded;
  ArrayRef<uint8_t> EncodedPayload(
      reinterpret_cast<const uint8_t *>(Encoded) + sizeof(CompressionHeader),
      VersionsHeader->sh_size - sizeof(CompressionHeader));
  if (Error E = compression::decompress(CompressionFormat, EncodedPayload,
                                        Decoded, VersionsSize)) {
    ADD_FAILURE() << toString(std::move(E)).str().str();
    return;
  }
  ASSERT_EQ(Decoded.size(), LogicalVersions.size());
  EXPECT_TRUE(
      std::equal(Decoded.begin(), Decoded.end(), LogicalVersions.begin()));

  SecSpec CanonicalVersions{"__versions", VersionsSize, 8,
                            SHT_PROGBITS, SHF_ALLOC,    VersionsFill};
  SmallVector<SmallVector<char, 0>, 1> CanonicalInputs;
  CanonicalInputs.push_back(
      buildSectionedELF({Text, CanonicalVersions}, {Function}, {}, EM_AARCH64));
  Options AuditOptions = androidKernelReleaseOptions();
  auto [CanonicalOK, CanonicalOutput] = mergeELF(CanonicalInputs, AuditOptions);
  ASSERT_TRUE(CanonicalOK);
  std::string Error;
  ASSERT_TRUE(
      verifyAndroidKernelModuleImage(CanonicalOutput, AuditOptions, &Error))
      << Error;

  constexpr StringLiteral ShapeDiagnostic =
      "input __versions must be an allocated, uncompressed SHT_PROGBITS "
      "section";
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(Input);
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    testing::internal::CaptureStderr();
    auto [OK, Output] = mergeELF(Inputs, Opts);
    const std::string Diagnostic = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
    EXPECT_NE(Diagnostic.find(ShapeDiagnostic.str()), std::string::npos)
        << Diagnostic;
  }

  SmallVector<SmallVector<char, 0>, 1> AuditInputs;
  AuditInputs.push_back(std::move(Input));
  Error.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(AuditInputs),
                           CanonicalOutput, Format::ELF64LE, AuditOptions,
                           &Error));
  EXPECT_NE(Error.find(ShapeDiagnostic.str()), std::string::npos) << Error;
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsMalformedVersionsEntryWidthsAtomically) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x97};
  SymSpec Function{"release_bad_versions_size", 0, 0};

  for (uint64_t Size : {uint64_t(1), uint64_t(63), uint64_t(65)}) {
    SCOPED_TRACE(Size);
    SecSpec Versions{"__versions", Size, 8, SHT_PROGBITS, SHF_ALLOC, 0x98};
    const auto Input =
        buildSectionedELF({Text, Versions}, {Function}, {}, EM_AARCH64);
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }

  // The final byte count alone is insufficient: two malformed contributions
  // can become one apparent 64-byte record after canonical alignment. Both
  // inputs otherwise have the required shape; 1 byte + 7 bytes of alignment
  // padding + 56 bytes reaches 64. Preserve each input's record boundary by
  // rejecting both partitions before merge.
  SecSpec OneByteVersions{"__versions", 1, 8, SHT_PROGBITS, SHF_ALLOC, 0x9b};
  SecSpec FiftySixByteVersions{"__versions", 56,        8,
                               SHT_PROGBITS, SHF_ALLOC, 0x9c};
  SmallVector<SmallVector<char, 0>, 2> SplitInputs;
  SplitInputs.push_back(
      buildSectionedELF({OneByteVersions}, {}, {}, EM_AARCH64));
  SplitInputs.push_back(
      buildSectionedELF({FiftySixByteVersions}, {}, {}, EM_AARCH64));
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "split verify-on" : "split verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(SplitInputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }

  SecSpec CanonicalVersions{"__versions", 64, 8, SHT_PROGBITS, SHF_ALLOC, 0x9d};
  SmallVector<SmallVector<char, 0>, 1> CanonicalInputs;
  CanonicalInputs.push_back(
      buildSectionedELF({CanonicalVersions}, {}, {}, EM_AARCH64));
  Options AuditOptions = androidKernelReleaseOptions();
  auto [CanonicalOK, CanonicalOutput] = mergeELF(CanonicalInputs, AuditOptions);
  ASSERT_TRUE(CanonicalOK);
  std::string Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(SplitInputs),
                           CanonicalOutput, Format::ELF64LE, AuditOptions,
                           &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseAcceptsCanonicalVersionsEntryWidthsAndAuditsThem) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x99};
  SymSpec Function{"release_good_versions_size", 0, 0};

  for (uint64_t Size : {uint64_t(0), uint64_t(64)}) {
    SCOPED_TRACE(Size);
    SecSpec Versions{"__versions", Size, 8, SHT_PROGBITS, SHF_ALLOC, 0x9a};
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(
        buildSectionedELF({Text, Versions}, {Function}, {}, EM_AARCH64));
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      ASSERT_TRUE(OK);
      ElfView View = parseELF(Output);
      ASSERT_TRUE(View.Ok);
      const int VersionsIndex = View.findSec("__versions");
      ASSERT_GE(VersionsIndex, 0);
      const ParsedSec &OutputVersions = View.Secs[VersionsIndex];
      EXPECT_EQ(OutputVersions.Type, SHT_PROGBITS);
      EXPECT_NE(OutputVersions.Flags & SHF_ALLOC, 0u);
      EXPECT_EQ(OutputVersions.Flags & SHF_COMPRESSED, 0u);
      EXPECT_GE(OutputVersions.Align, 8u);
      EXPECT_EQ(OutputVersions.Align & (OutputVersions.Align - 1), 0u);
      EXPECT_EQ(OutputVersions.Size, Size);

      std::string Error;
      EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error))
          << Error;
      Error.clear();
      EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                              Format::ELF64LE, Opts, &Error))
          << Error;

      if (Size == 64) {
        SmallVector<char, 0> Truncated(Output.begin(), Output.end());
        Elf64_Shdr *VersionsHeader =
            findELFSectionHeader(Truncated, "__versions");
        ASSERT_NE(VersionsHeader, nullptr);
        VersionsHeader->sh_size = 63;
        Error.clear();
        EXPECT_FALSE(verifyAndroidKernelModuleImage(Truncated, Opts, &Error));
        Error.clear();
        EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                                 Truncated, Format::ELF64LE, Opts, &Error));
      }
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsSameSectionNobitsContributionOverflow) {
  SecSpec FirstBss{".bss.wrap", std::numeric_limits<uint64_t>::max() - 7, 1,
                   ELF::SHT_NOBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec SecondBss{".bss.wrap", 8, 16, ELF::SHT_NOBITS,
                    ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SymSpec FirstObject{"release_before_nobits_wrap", 0, 0};
  FirstObject.Type = ELF::STT_OBJECT;
  SymSpec SecondObject{"release_after_nobits_wrap", 0, 0};
  SecondObject.Type = ELF::STT_OBJECT;

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(
      buildSectionedELF({FirstBss}, {FirstObject}, {}, ELF::EM_AARCH64));
  Inputs.push_back(
      buildSectionedELF({SecondBss}, {SecondObject}, {}, ELF::EM_AARCH64));
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsSplitDwarfWithVerificationOnOrOff) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xa1};
  SymSpec Function{"release_split_dwarf", 0, 0};
  auto Input = buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(std::move(Input));

  Options ValidOptions = androidKernelReleaseOptions();
  auto [Valid, Output] = mergeELF(Inputs, ValidOptions);
  ASSERT_TRUE(Valid);

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options SplitOptions = androidKernelReleaseOptions();
    SplitOptions.artifact = ArtifactKind::SplitDwarf;
    SplitOptions.verify = Verify;
    auto [OK, RejectedOutput] = mergeELF(Inputs, SplitOptions);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(RejectedOutput.empty());

    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Output, SplitOptions, &Error));
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                             Format::ELF64LE, SplitOptions, &Error));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsRetainedImplicitAddendRelocations) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xb1};
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0xb2};
  SymSpec Function{"release_rela_target", 0, 0};
  RelSpec Reference{1, 0, Function.Name, ELF::R_AARCH64_ABS64, 7};
  auto RelaInput =
      buildSectionedELF({Text, Data}, {Function}, {Reference}, ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> RelaInputs;
  RelaInputs.push_back(RelaInput);
  Options Opts = androidKernelReleaseOptions();
  auto [Valid, ValidOutput] = mergeELF(RelaInputs, Opts);
  ASSERT_TRUE(Valid);

  SmallVector<char, 0> RelInput(RelaInput.begin(), RelaInput.end());
  ASSERT_TRUE(convertFirstELFRelaSectionToRel(RelInput));
  SmallVector<SmallVector<char, 0>, 1> RelInputs;
  RelInputs.push_back(std::move(RelInput));
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options ProducerOptions = androidKernelReleaseOptions();
    ProducerOptions.verify = Verify;
    auto [OK, Output] = mergeELF(RelInputs, ProducerOptions);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }

  std::string Error;
  EXPECT_TRUE(verifyAndroidKernelModuleImage(ValidOutput, Opts, &Error))
      << Error;
  SmallVector<char, 0> RelOutput(ValidOutput.begin(), ValidOutput.end());
  ASSERT_TRUE(convertFirstELFRelaSectionToRel(RelOutput));
  EXPECT_FALSE(verifyAndroidKernelModuleImage(RelOutput, Opts, &Error));
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(RelaInputs),
                           RelOutput, Format::ELF64LE, Opts, &Error));
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(RelInputs),
                           ValidOutput, Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsMalformedRetainedRelocationsAtomically) {
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0xd1};
  SymSpec Import{"release_malformed_relocation_import", -1, 0};
  Import.Type = ELF::STT_NOTYPE;
  RelSpec Reference{0, 0, Import.Name, ELF::R_AARCH64_ABS64, 3};
  const auto Valid =
      buildSectionedELF({Data}, {Import}, {Reference}, ELF::EM_AARCH64);

  auto ExpectAtomicFailure = [&](SmallVector<char, 0> Input, StringRef Case) {
    SCOPED_TRACE(Case.str());
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  };

  SmallVector<char, 0> BadSymbol(Valid.begin(), Valid.end());
  ASSERT_TRUE(patchFirstRelaSymbolIndex(BadSymbol, UINT32_C(0xffffff00)));
  ExpectAtomicFailure(std::move(BadSymbol), "out-of-range r_sym");

  SmallVector<char, 0> BadSite(Valid.begin(), Valid.end());
  ASSERT_TRUE(patchAllRelaOffsets(BadSite, Data.Size));
  ExpectAtomicFailure(std::move(BadSite), "out-of-range r_offset");

  SmallVector<char, 0> BadLink(Valid.begin(), Valid.end());
  ELF::Elf64_Shdr *Relocations = findELFSectionHeader(BadLink, ".rela.data");
  ASSERT_NE(Relocations, nullptr);
  Relocations->sh_link = 1; // .data, not SHT_SYMTAB
  ExpectAtomicFailure(std::move(BadLink), "relocation sh_link type");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsMalformedSymbolsAndTablesAtomically) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd2};
  SymSpec Function{"release_malformed_symbol", 0, 0};
  const auto Valid = buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64);

  auto ExpectAtomicFailure = [&](SmallVector<char, 0> Input, StringRef Case) {
    SCOPED_TRACE(Case.str());
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  };

  SmallVector<char, 0> BadSectionIndex(Valid.begin(), Valid.end());
  auto *BadIndexHeader =
      reinterpret_cast<ELF::Elf64_Ehdr *>(BadSectionIndex.data());
  ASSERT_TRUE(patchELFSymbolSectionIndex(BadSectionIndex, Function.Name,
                                         BadIndexHeader->e_shnum));
  ExpectAtomicFailure(std::move(BadSectionIndex), "out-of-range st_shndx");

  SmallVector<char, 0> BadSymtabLink(Valid.begin(), Valid.end());
  ELF::Elf64_Shdr *Symtab = findELFSectionHeader(BadSymtabLink, ".symtab");
  ASSERT_NE(Symtab, nullptr);
  Symtab->sh_link = 1; // .text, not SHT_STRTAB
  ExpectAtomicFailure(std::move(BadSymtabLink), "symtab sh_link type");

  SmallVector<char, 0> BadNameOffset(Valid.begin(), Valid.end());
  Symtab = findELFSectionHeader(BadNameOffset, ".symtab");
  ELF::Elf64_Shdr *Strtab = findELFSectionHeader(BadNameOffset, ".strtab");
  ASSERT_NE(Symtab, nullptr);
  ASSERT_NE(Strtab, nullptr);
  ASSERT_TRUE(
      patchELFSymbolNameOffset(BadNameOffset, Function.Name, Strtab->sh_size));
  ExpectAtomicFailure(std::move(BadNameOffset), "out-of-range st_name");

  SmallVector<char, 0> UnterminatedName(Valid.begin(), Valid.end());
  Strtab = findELFSectionHeader(UnterminatedName, ".strtab");
  ASSERT_NE(Strtab, nullptr);
  ASSERT_GT(Strtab->sh_size, 0u);
  ASSERT_TRUE(patchELFSymbolNameOffset(UnterminatedName, Function.Name,
                                       Strtab->sh_size - 1));
  UnterminatedName[Strtab->sh_offset + Strtab->sh_size - 1] = 'X';
  ExpectAtomicFailure(std::move(UnterminatedName), "unterminated st_name");

  SmallVector<char, 0> BadSymtabShape(Valid.begin(), Valid.end());
  Symtab = findELFSectionHeader(BadSymtabShape, ".symtab");
  ASSERT_NE(Symtab, nullptr);
  Symtab->sh_entsize = 1;
  ExpectAtomicFailure(std::move(BadSymtabShape), "symtab sh_entsize");

  SmallVector<char, 0> BadSymtabSize(Valid.begin(), Valid.end());
  Symtab = findELFSectionHeader(BadSymtabSize, ".symtab");
  ASSERT_NE(Symtab, nullptr);
  --Symtab->sh_size;
  ExpectAtomicFailure(std::move(BadSymtabSize), "partial symtab record");

  SmallVector<char, 0> TruncatedPayload(Valid.begin(), Valid.end());
  ELF::Elf64_Shdr *TextHeader = findELFSectionHeader(TruncatedPayload, ".text");
  ASSERT_NE(TextHeader, nullptr);
  TextHeader->sh_offset = TruncatedPayload.size() - 4;
  TextHeader->sh_size = 0x10;
  ExpectAtomicFailure(std::move(TruncatedPayload),
                      "truncated ordinary section payload");

  SmallVector<char, 0> BadSectionHeaderSize(Valid.begin(), Valid.end());
  auto *Header =
      reinterpret_cast<ELF::Elf64_Ehdr *>(BadSectionHeaderSize.data());
  Header->e_shentsize = 1;
  ExpectAtomicFailure(std::move(BadSectionHeaderSize), "e_shentsize");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsUnsupportedIndexMetadataInputsAtomically) {
  using namespace ELF;
  struct Case {
    const char *Name;
    uint32_t Type;
  };
  constexpr Case Cases[] = {
      {".group", SHT_GROUP},
      {".symtab_shndx", SHT_SYMTAB_SHNDX},
  };

  SecSpec Text{".text", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xd3};
  SymSpec Function{"release_unsupported_index_metadata", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> ValidInputs;
  ValidInputs.push_back(buildSectionedELF({Text}, {Function}, {}, EM_AARCH64));
  Options AuditOptions = androidKernelReleaseOptions();
  auto [Valid, CanonicalOutput] = mergeELF(ValidInputs, AuditOptions);
  ASSERT_TRUE(Valid);

  for (const Case &Entry : Cases) {
    SCOPED_TRACE(Entry.Name);
    SecSpec Unsupported{Entry.Name, 4, 4, Entry.Type, 0, 0xa7};
    Unsupported.Entsize = 4;
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(
        buildSectionedELF({Text, Unsupported}, {Function}, {}, EM_AARCH64));
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }

    std::string Error;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                             CanonicalOutput, Format::ELF64LE, AuditOptions,
                             &Error));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseAcceptsAndDropsRegeneratedMetadataInputs) {
  using namespace ELF;
  struct Case {
    const char *Name;
    uint32_t Type;
    uint64_t Size;
  };
  constexpr Case Cases[] = {
      {".inactive", SHT_NULL, 0},
      {".llvm_addrsig", SHT_LLVM_ADDRSIG, 4},
      {".llvm.call-graph-profile", SHT_LLVM_CALL_GRAPH_PROFILE, 8},
  };

  SecSpec Text{".text", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xd4};
  SymSpec Function{"release_regenerated_metadata", 0, 0};
  for (const Case &Entry : Cases) {
    SCOPED_TRACE(Entry.Name);
    SecSpec Metadata{Entry.Name, Entry.Size, 1, Entry.Type, 0, 0xa8};
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(
        buildSectionedELF({Text, Metadata}, {Function}, {}, EM_AARCH64));
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      ASSERT_TRUE(OK);
      ElfView View = parseELF(Output);
      ASSERT_TRUE(View.Ok);
      EXPECT_LT(View.findSec(Entry.Name), 0);

      std::string Error;
      EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error))
          << Error;
      EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                              Format::ELF64LE, Opts, &Error))
          << Error;
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseVerifiersRejectForbiddenOutputMetadataTypes) {
  using namespace ELF;
  struct Case {
    const char *Name;
    uint32_t Type;
  };
  constexpr Case Cases[] = {
      {"SHT_NULL outside section zero", SHT_NULL},
      {"SHT_GROUP", SHT_GROUP},
      {"SHT_SYMTAB_SHNDX", SHT_SYMTAB_SHNDX},
      {"SHT_LLVM_ADDRSIG", SHT_LLVM_ADDRSIG},
      {"SHT_LLVM_CALL_GRAPH_PROFILE", SHT_LLVM_CALL_GRAPH_PROFILE},
  };

  SecSpec Text{".text", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xd5};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xd6};
  SymSpec Function{"release_forbidden_output_metadata", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text, Data}, {Function}, {}, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, CanonicalOutput] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  for (const Case &Entry : Cases) {
    SCOPED_TRACE(Entry.Name);
    SmallVector<char, 0> Mutated(CanonicalOutput.begin(),
                                 CanonicalOutput.end());
    Elf64_Shdr *DataHeader = findELFSectionHeader(Mutated, ".data");
    ASSERT_NE(DataHeader, nullptr);
    DataHeader->sh_type = Entry.Type;
    DataHeader->sh_link = 0;
    DataHeader->sh_info = 0;

    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Mutated, Opts, &Error));
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Mutated,
                             Format::ELF64LE, Opts, &Error));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsNonRelocatableInputHeadersAtomically) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd6};
  SymSpec Function{"release_nonrelocatable_header", 0, 0};
  const auto Relocatable =
      buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64);

  for (uint16_t Type : {uint16_t(ELF::ET_EXEC), uint16_t(ELF::ET_DYN)}) {
    SCOPED_TRACE(Type == ELF::ET_EXEC ? "ET_EXEC" : "ET_DYN");
    SmallVector<char, 0> Input(Relocatable.begin(), Relocatable.end());
    auto *Header = reinterpret_cast<ELF::Elf64_Ehdr *>(Input.data());
    Header->e_type = Type;
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }

  SmallVector<char, 0> BadVersion(Relocatable.begin(), Relocatable.end());
  reinterpret_cast<ELF::Elf64_Ehdr *>(BadVersion.data())->e_version = 0;
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "bad-version verify-on" : "bad-version verify-off");
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(BadVersion);
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }

  SmallVector<char, 0> ExtendedCount(Relocatable.begin(), Relocatable.end());
  auto *ExtendedCountHeader =
      reinterpret_cast<ELF::Elf64_Ehdr *>(ExtendedCount.data());
  auto *ExtendedCountSections = reinterpret_cast<ELF::Elf64_Shdr *>(
      ExtendedCount.data() + ExtendedCountHeader->e_shoff);
  ExtendedCountSections[0].sh_size = ExtendedCountHeader->e_shnum;
  ExtendedCountHeader->e_shnum = 0;

  SmallVector<char, 0> ExtendedShstrndx(Relocatable.begin(), Relocatable.end());
  auto *ExtendedShstrndxHeader =
      reinterpret_cast<ELF::Elf64_Ehdr *>(ExtendedShstrndx.data());
  auto *ExtendedShstrndxSections = reinterpret_cast<ELF::Elf64_Shdr *>(
      ExtendedShstrndx.data() + ExtendedShstrndxHeader->e_shoff);
  ExtendedShstrndxSections[0].sh_link = ExtendedShstrndxHeader->e_shstrndx;
  ExtendedShstrndxHeader->e_shstrndx = ELF::SHN_XINDEX;

  for (const auto &Input : {ExtendedCount, ExtendedShstrndx}) {
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "extended-numbering verify-on"
                          : "extended-numbering verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsNonzeroInputSectionAddressAtomically) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd7};
  SymSpec Function{"release_nonzero_section_address", 0, 0};
  SmallVector<char, 0> Input =
      buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64);
  ELF::Elf64_Shdr *TextHeader = findELFSectionHeader(Input, ".text");
  ASSERT_NE(TextHeader, nullptr);
  TextHeader->sh_addr = 0x1000;

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(Input);
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsMismatchedInputAbiHeadersAtomically) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd9};
  SymSpec First{"release_abi_first", 0, 0};
  SymSpec Second{"release_abi_second", 0, 0};
  const auto FirstInput =
      buildSectionedELF({Text}, {First}, {}, ELF::EM_AARCH64);
  const auto BaseSecond =
      buildSectionedELF({Text}, {Second}, {}, ELF::EM_AARCH64);

  enum class Mutation { Flags, OSABI, ABIVersion };
  for (Mutation Kind :
       {Mutation::Flags, Mutation::OSABI, Mutation::ABIVersion}) {
    SmallVector<char, 0> SecondInput(BaseSecond.begin(), BaseSecond.end());
    auto *Header = reinterpret_cast<ELF::Elf64_Ehdr *>(SecondInput.data());
    switch (Kind) {
    case Mutation::Flags:
      Header->e_flags = 1;
      break;
    case Mutation::OSABI:
      Header->e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_LINUX;
      break;
    case Mutation::ABIVersion:
      Header->e_ident[ELF::EI_ABIVERSION] = 1;
      break;
    }
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 2> Inputs;
      Inputs.push_back(FirstInput);
      Inputs.push_back(SecondInput);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseInputAwareVerifierChecksOutputAbiHeaderIdentity) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd9};
  SymSpec First{"release_output_abi_first", 0, 0};
  SymSpec Second{"release_output_abi_second", 0, 0};
  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(buildSectionedELF({Text}, {First}, {}, ELF::EM_AARCH64));
  Inputs.push_back(buildSectionedELF({Text}, {Second}, {}, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  enum class Mutation { Flags, OSABI, ABIVersion };
  for (Mutation Kind :
       {Mutation::Flags, Mutation::OSABI, Mutation::ABIVersion}) {
    SmallVector<char, 0> Mutated(Output.begin(), Output.end());
    auto *Header = reinterpret_cast<ELF::Elf64_Ehdr *>(Mutated.data());
    switch (Kind) {
    case Mutation::Flags:
      Header->e_flags = 1;
      break;
    case Mutation::OSABI:
      Header->e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_LINUX;
      break;
    case Mutation::ABIVersion:
      Header->e_ident[ELF::EI_ABIVERSION] = 1;
      break;
    }
    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Mutated, Opts, &Error)) << Error;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Mutated,
                             Format::ELF64LE, Opts, &Error));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsInvalidSectionAlignmentAtomically) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xda};
  SymSpec Function{"release_invalid_alignment", 0, 0};
  SmallVector<char, 0> Input =
      buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64);
  ELF::Elf64_Shdr *TextHeader = findELFSectionHeader(Input, ".text");
  ASSERT_NE(TextHeader, nullptr);
  TextHeader->sh_addralign = 3;

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(Input);
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseProducerRejectsWrongInputELFHeaderSizeVerifyOff) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xda};
  SymSpec Function{"release_bad_ehsize", 0, 0};
  const auto GoodInput = buildSectionedELF({Text}, {Function}, {}, EM_AARCH64);
  SmallVector<char, 0> BadInput(GoodInput.begin(), GoodInput.end());
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(BadInput.data());
  Header->e_ehsize = sizeof(Elf64_Ehdr) - 1;

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(BadInput);
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }

  SmallVector<SmallVector<char, 0>, 1> GoodInputs;
  GoodInputs.push_back(GoodInput);
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(GoodInputs, Opts);
  ASSERT_TRUE(OK);
  SmallVector<SmallVector<char, 0>, 1> BadInputs;
  BadInputs.push_back(BadInput);
  std::string Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(BadInputs), Output,
                           Format::ELF64LE, Opts, &Error));
  Error.clear();
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
}

TEST(MergeELFSemantic,
     AndroidKernelReleasePreservesLargeSectionAlignmentInEitherOrder) {
  constexpr uint32_t LargeAlignment = uint32_t(2) << 20;
  for (uint32_t Type :
       {uint32_t(ELF::SHT_PROGBITS), uint32_t(ELF::SHT_NOBITS)}) {
    const std::string Name = Type == ELF::SHT_NOBITS ? ".bss" : ".data";
    const uint64_t Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE;
    SecSpec Small{Name, 1, 16, Type, Flags, 0xdb};
    SecSpec Large{Name, 1, LargeAlignment, Type, Flags, 0xdc};
    SymSpec SmallSymbol{"release_small_alignment",
                        0,
                        0,
                        true,
                        false,
                        std::nullopt,
                        ELF::STT_OBJECT};
    SymSpec LargeSymbol{"release_large_alignment",
                        0,
                        0,
                        true,
                        false,
                        std::nullopt,
                        ELF::STT_OBJECT};
    const auto SmallInput =
        buildSectionedELF({Small}, {SmallSymbol}, {}, ELF::EM_AARCH64);
    const auto LargeInput =
        buildSectionedELF({Large}, {LargeSymbol}, {}, ELF::EM_AARCH64);

    for (bool LargeFirst : {false, true}) {
      for (bool Verify : {false, true}) {
        SCOPED_TRACE((Twine(Type) +
                      (LargeFirst ? " large-first" : " large-last") +
                      (Verify ? " verify-on" : " verify-off"))
                         .str());
        SmallVector<SmallVector<char, 0>, 2> Inputs;
        if (LargeFirst) {
          Inputs.push_back(LargeInput);
          Inputs.push_back(SmallInput);
        } else {
          Inputs.push_back(SmallInput);
          Inputs.push_back(LargeInput);
        }
        Options Opts = androidKernelReleaseOptions();
        Opts.verify = Verify;
        auto [OK, Output] = mergeELF(Inputs, Opts);
        ASSERT_TRUE(OK);
        const ELF::Elf64_Shdr *Header = findELFSectionHeader(Output, Name);
        ASSERT_NE(Header, nullptr);
        EXPECT_EQ(Header->sh_addralign, LargeAlignment);
        EXPECT_EQ(Header->sh_size, uint64_t(LargeAlignment) + 1);

        std::string Error;
        EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error))
            << Error;
        EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                                Format::ELF64LE, Opts, &Error))
            << Error;
      }
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseUsesOneGlobalMaterializationLedgerAcrossSections) {
  const compression::Format Format =
      compression::formatFor(DebugCompressionType::Zlib);
  if (const char *Reason = compression::getReasonIfUnsupported(Format))
    GTEST_SKIP() << Reason;

  using namespace ELF;
  constexpr uint64_t LargeAlignment = uint64_t(1) << 26;
  SecSpec DebugA0{".debug_budget_a", 1, 1, SHT_PROGBITS, 0, 0x31};
  SecSpec DebugA1{".debug_budget_a", 1, 1, SHT_PROGBITS, 0, 0x32};
  SecSpec DebugB0{".debug_budget_b", 1, 1, SHT_PROGBITS, 0, 0x33};
  SecSpec DebugB1{".debug_budget_b", 1, 1, SHT_PROGBITS, 0, 0x34};
  SmallVector<char, 0> Input = buildSectionedELF(
      {DebugA0, DebugA1, DebugB0, DebugB1}, {}, {}, EM_AARCH64);
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Input.data());
  auto *Sections =
      reinterpret_cast<Elf64_Shdr *>(Input.data() + Header->e_shoff);
  ASSERT_GT(Header->e_shnum, 4u);
  Sections[2].sh_addralign = LargeAlignment;
  Sections[4].sh_addralign = LargeAlignment;

  // Each individual merged debug section stays within input-bytes + 64 MiB,
  // and compression would make the final file tiny. Only a single operation-
  // wide ledger notices that the two simultaneous materializations cumulatively
  // exceed the allowance before allocating both.
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(Input);
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.debugCompression = DebugCompressionType::Zlib;
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseVerifierBoundsAllReconstructionPaddingGlobally) {
  using namespace ELF;
  constexpr uint64_t LargeAlignment = uint64_t(1) << 26;
  for (uint32_t Type : {uint32_t(SHT_NOTE), uint32_t(SHT_PROGBITS)}) {
    SCOPED_TRACE(Type == SHT_NOTE ? "NOTE reconstruction"
                                  : "debug reconstruction");
    const std::string Prefix =
        Type == SHT_NOTE ? ".note.budget_" : ".debug_budget_";
    SecSpec A0{Prefix + "a", 1, 1, Type, 0, 0x35};
    SecSpec A1{Prefix + "a", 1, 1, Type, 0, 0x36};
    SecSpec B0{Prefix + "b", 1, 1, Type, 0, 0x37};
    SecSpec B1{Prefix + "b", 1, 1, Type, 0, 0x38};
    const auto GoodInput =
        buildSectionedELF({A0, A1, B0, B1}, {}, {}, EM_AARCH64);
    SmallVector<SmallVector<char, 0>, 1> GoodInputs;
    GoodInputs.push_back(GoodInput);
    Options Opts = androidKernelReleaseOptions();
    auto [OK, Output] = mergeELF(GoodInputs, Opts);
    ASSERT_TRUE(OK);

    SmallVector<char, 0> BadInput(GoodInput.begin(), GoodInput.end());
    auto *Header = reinterpret_cast<Elf64_Ehdr *>(BadInput.data());
    auto *Sections =
        reinterpret_cast<Elf64_Shdr *>(BadInput.data() + Header->e_shoff);
    ASSERT_GT(Header->e_shnum, 4u);
    Sections[2].sh_addralign = LargeAlignment;
    Sections[4].sh_addralign = LargeAlignment;
    SmallVector<SmallVector<char, 0>, 1> BadInputs;
    BadInputs.push_back(std::move(BadInput));
    std::string Error;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(BadInputs), Output,
                             Format::ELF64LE, Opts, &Error));
    EXPECT_NE(Error.find("budget"), std::string::npos) << Error;
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsMergedLinkOrderWithDistinctTargetIdentities) {
  using namespace ELF;
  // Both target names fold to `.rodata`, but their flags keep them as two
  // distinct (non-loaded) output candidates. Name-only remapping would choose
  // the first candidate for both PFE contributions.
  SecSpec ExecutableText{".rodata.first", 0x10, 16, SHT_PROGBITS, 0, 0xdd};
  SecSpec WritableText{".rodata.second", 0x10,      16,
                       SHT_PROGBITS,     SHF_WRITE, 0xde};
  SecSpec FirstPFE{
      "__patchable_function_entries", 8,    8,         SHT_PROGBITS,
      SHF_ALLOC | SHF_LINK_ORDER,     0xdf, /*Link=*/0};
  SecSpec SecondPFE{
      "__patchable_function_entries", 8,    8,         SHT_PROGBITS,
      SHF_ALLOC | SHF_LINK_ORDER,     0xe0, /*Link=*/0};
  SymSpec First{"release_link_identity_first",
                1,
                0,
                true,
                false,
                std::nullopt,
                STT_OBJECT};
  SymSpec Second{"release_link_identity_second",
                 1,
                 0,
                 true,
                 false,
                 std::nullopt,
                 STT_OBJECT};
  const auto FirstInput =
      buildSectionedELF({ExecutableText, FirstPFE}, {First}, {}, EM_AARCH64);
  const auto SecondInput =
      buildSectionedELF({WritableText, SecondPFE}, {Second}, {}, EM_AARCH64);

  for (bool Reverse : {false, true}) {
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(std::string(Reverse ? "reverse" : "forward") +
                   (Verify ? " verify-on" : " verify-off"));
      SmallVector<SmallVector<char, 0>, 2> Inputs;
      if (Reverse) {
        Inputs.push_back(SecondInput);
        Inputs.push_back(FirstInput);
      } else {
        Inputs.push_back(FirstInput);
        Inputs.push_back(SecondInput);
      }
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseOrdersLinkOrderContributionsByTargetPlacement) {
  using namespace ELF;
  // The first metadata contribution describes the *second* text contribution,
  // while the later metadata contribution describes the first. Input section
  // order therefore disagrees with the final linked-target offset order.
  SecSpec FirstText{
      ".text.first", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x41};
  SecSpec MetadataForSecond{
      ".target_order", 4, 4, SHT_PROGBITS, SHF_ALLOC | SHF_LINK_ORDER, 0x22,
      /*Link=*/2};
  SecSpec SecondText{
      ".text.second", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x42};
  SecSpec MetadataForFirst{
      ".target_order", 4, 4, SHT_PROGBITS, SHF_ALLOC | SHF_LINK_ORDER, 0x11,
      /*Link=*/0};
  // Undefined import names survive release pruning verbatim, so relocation
  // identity remains independently observable after contribution reordering.
  SymSpec FirstImport{"release_link_order_import_first", -1, 0};
  FirstImport.Type = STT_NOTYPE;
  SymSpec SecondImport{"release_link_order_import_second", -1, 0};
  SecondImport.Type = STT_NOTYPE;
  RelSpec MetadataForSecondRelocation{1, 0, SecondImport.Name, R_AARCH64_ABS32,
                                      0};
  RelSpec MetadataForFirstRelocation{3, 0, FirstImport.Name, R_AARCH64_ABS32,
                                     0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF(
      {FirstText, MetadataForSecond, SecondText, MetadataForFirst},
      {FirstImport, SecondImport},
      {MetadataForSecondRelocation, MetadataForFirstRelocation}, EM_AARCH64));

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    ASSERT_TRUE(OK);
    ElfView View = parseELF(Output);
    ASSERT_TRUE(View.Ok);
    const int MetadataIndex = View.findSec(".target_order");
    ASSERT_GE(MetadataIndex, 0);
    const ParsedSec &Metadata = View.Secs[(unsigned)MetadataIndex];
    ASSERT_EQ(Metadata.Data.size(), 8u);
    EXPECT_TRUE(llvm::all_of(ArrayRef<uint8_t>(Metadata.Data).take_front(4),
                             [](uint8_t Byte) { return Byte == 0x11; }));
    EXPECT_TRUE(llvm::all_of(ArrayRef<uint8_t>(Metadata.Data).drop_front(4),
                             [](uint8_t Byte) { return Byte == 0x22; }));

    unsigned CheckedRelocations = 0;
    for (const ParsedRela &Relocation : View.Relas) {
      if (Relocation.TargetSec != (unsigned)MetadataIndex)
        continue;
      ASSERT_LT(Relocation.Sym, View.Syms.size());
      const ParsedSym &Target = View.Syms[Relocation.Sym];
      if (Target.Name == "release_link_order_import_first") {
        EXPECT_EQ(Relocation.Offset, 0u);
        ++CheckedRelocations;
      } else if (Target.Name == "release_link_order_import_second") {
        EXPECT_EQ(Relocation.Offset, 4u);
        ++CheckedRelocations;
      }
    }
    EXPECT_EQ(CheckedRelocations, 2u);

    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                            Format::ELF64LE, Opts, &Error))
        << Error;
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsUnsupportedOrdinarySectionLinkMetadata) {
  using namespace ELF;
  SecSpec Text{".text", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xe1};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xe2};
  SymSpec Function{"release_ordinary_link_metadata", 0, 0};
  const auto Base = buildSectionedELF({Text, Data}, {Function}, {}, EM_AARCH64);

  enum class Mutation { Link, Info, InfoLink };
  for (Mutation Kind : {Mutation::Link, Mutation::Info, Mutation::InfoLink}) {
    SmallVector<char, 0> Input(Base.begin(), Base.end());
    ELF::Elf64_Shdr *DataHeader = findELFSectionHeader(Input, ".data");
    ASSERT_NE(DataHeader, nullptr);
    switch (Kind) {
    case Mutation::Link:
      DataHeader->sh_link = 1;
      break;
    case Mutation::Info:
      DataHeader->sh_info = 1;
      break;
    case Mutation::InfoLink:
      DataHeader->sh_flags |= SHF_INFO_LINK;
      DataHeader->sh_info = 1;
      break;
    }
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseInputAwareVerifierChecksExactLinkOrderTarget) {
  using namespace ELF;
  SecSpec Text{".text", 0x10, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xe3};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xe4};
  SecSpec PFE{"__patchable_function_entries", 8,    8,         SHT_PROGBITS,
              SHF_ALLOC | SHF_LINK_ORDER,     0xe5, /*Link=*/0};
  SymSpec Function{"release_exact_link_order", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data, PFE}, {Function}, {}, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  const int DataIndex = View.findSec(".data");
  ASSERT_GT(DataIndex, 0);
  ELF::Elf64_Shdr *PFEHeader =
      findELFSectionHeader(Output, "__patchable_function_entries");
  ASSERT_NE(PFEHeader, nullptr);
  ASSERT_NE(PFEHeader->sh_link, static_cast<uint32_t>(DataIndex));
  PFEHeader->sh_link = DataIndex;

  std::string Error;
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsIncompatibleSectionEntrySizesInEitherOrder) {
  using namespace ELF;
  struct Case {
    const char *Name;
    uint32_t Type;
    uint64_t Flags;
    uint64_t FirstEntsize;
    uint64_t SecondEntsize;
  };
  const Case Cases[] = {
      {".data.entsize", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 4, 8},
      {".init_array", SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE, 8, 16},
      {".literal_pool", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1,
       2},
  };

  for (const Case &C : Cases) {
    SecSpec FirstSection{C.Name,      16,
                         8,           C.Type,
                         C.Flags,     0xe6,
                         /*Link=*/-1, C.FirstEntsize};
    SecSpec SecondSection{C.Name,      16,
                          8,           C.Type,
                          C.Flags,     0xe7,
                          /*Link=*/-1, C.SecondEntsize};
    SymSpec First{
        "release_entsize_first", 0, 0, true, false, std::nullopt, STT_OBJECT};
    SymSpec Second{
        "release_entsize_second", 0, 0, true, false, std::nullopt, STT_OBJECT};
    const auto FirstInput =
        buildSectionedELF({FirstSection}, {First}, {}, EM_AARCH64);
    const auto SecondInput =
        buildSectionedELF({SecondSection}, {Second}, {}, EM_AARCH64);

    for (bool Reverse : {false, true}) {
      for (bool Verify : {false, true}) {
        SCOPED_TRACE(std::string(C.Name) + (Reverse ? " reverse" : " forward") +
                     (Verify ? " verify-on" : " verify-off"));
        SmallVector<SmallVector<char, 0>, 2> Inputs;
        if (Reverse) {
          Inputs.push_back(SecondInput);
          Inputs.push_back(FirstInput);
        } else {
          Inputs.push_back(FirstInput);
          Inputs.push_back(SecondInput);
        }
        Options Opts = androidKernelReleaseOptions();
        Opts.verify = Verify;
        auto [OK, Output] = mergeELF(Inputs, Opts);
        EXPECT_FALSE(OK);
        EXPECT_TRUE(Output.empty());
      }
    }
  }
}

static void expectAndroidKernelReleaseDebugCompression(
    DebugCompressionType CompressionType) {
  using namespace ELF;
  SecSpec Debug0{".debug_str", 4096, 32, SHT_PROGBITS, 0, 0x41};
  SecSpec Small0{".debug_abbrev", 1, 1, SHT_PROGBITS, 0, 0x31};
  SecSpec Text0{".text", 16, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xe8};
  SecSpec Debug1{".debug_str", 4096, 32, SHT_PROGBITS, 0, 0x42};
  SecSpec Small1{".debug_abbrev", 1, 1, SHT_PROGBITS, 0, 0x32};
  SecSpec Text1{".text", 16, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xe9};
  SymSpec First{"release_compressed_debug_first", 2, 0};
  SymSpec Second{"release_compressed_debug_second", 2, 0};
  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(
      buildSectionedELF({Debug0, Small0, Text0}, {First}, {}, EM_AARCH64));
  Inputs.push_back(
      buildSectionedELF({Debug1, Small1, Text1}, {Second}, {}, EM_AARCH64));

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.debugCompression = CompressionType;
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    ASSERT_TRUE(OK);

    ElfView View = parseELF(Output);
    ASSERT_TRUE(View.Ok);
    const int DebugIndex = View.findSec(".debug_str");
    const int SmallIndex = View.findSec(".debug_abbrev");
    ASSERT_GE(DebugIndex, 0);
    ASSERT_GE(SmallIndex, 0);
    const ParsedSec &Debug = View.Secs[DebugIndex];
    const ParsedSec &Small = View.Secs[SmallIndex];
    EXPECT_NE(Debug.Flags & SHF_COMPRESSED, 0u);
    EXPECT_EQ(Debug.Align, alignof(Elf64_Chdr));
    EXPECT_EQ(Small.Flags & SHF_COMPRESSED, 0u)
        << "compression must be skipped when its header would make the "
           "section larger";

    ASSERT_GE(Debug.Data.size(), sizeof(Elf64_Chdr));
    Elf64_Chdr Header{};
    memcpy(&Header, Debug.Data.data(), sizeof(Header));
    EXPECT_EQ(Header.ch_type, CompressionType == DebugCompressionType::Zlib
                                  ? uint32_t(ELFCOMPRESS_ZLIB)
                                  : uint32_t(ELFCOMPRESS_ZSTD));
    EXPECT_EQ(Header.ch_reserved, 0u);
    EXPECT_EQ(Header.ch_size, 8192u);
    EXPECT_EQ(Header.ch_addralign, 32u);

    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                            Format::ELF64LE, Opts, &Error))
        << Error;
  }

  Options DropOpts = androidKernelReleaseOptions();
  DropOpts.debugCompression = CompressionType;
  DropOpts.dropDebugInfo = true;
  for (bool Verify : {false, true}) {
    DropOpts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, DropOpts);
    ASSERT_TRUE(OK);
    ElfView View = parseELF(Output);
    ASSERT_TRUE(View.Ok);
    EXPECT_LT(View.findSec(".debug_str"), 0);
    EXPECT_LT(View.findSec(".debug_abbrev"), 0);
    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, DropOpts, &Error))
        << Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                            Format::ELF64LE, DropOpts, &Error))
        << Error;
  }
}

TEST(MergeELFSemantic,
     GenericDropDebugVerifierRejectsEveryRetainedDebugSpelling) {
  using namespace ELF;
  const SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                     0xe8};
  const SymSpec Function{"generic_drop_debug_candidate", 0, 0};

  for (StringRef DebugName :
       {StringRef(".debug_info"), StringRef(".zdebug_info"),
        StringRef(".gdb_index")}) {
    SCOPED_TRACE(DebugName.str());
    const SecSpec Debug{DebugName.str(), 16, 4, SHT_PROGBITS, 0, 0xe9};
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(
        buildSectionedELF({Text, Debug}, {Function}, {}, EM_AARCH64));

    Options RetainOptions;
    auto [RetainOK, RetainedOutput] = mergeELF(Inputs, RetainOptions);
    ASSERT_TRUE(RetainOK);
    ASSERT_GE(parseELF(RetainedOutput).findSec(DebugName), 0);

    Options DropOptions;
    DropOptions.dropDebugInfo = true;
    ASSERT_FALSE(DropOptions.androidKernelModule);
    ASSERT_FALSE(DropOptions.finalizeAndroidKernelModule);
    ASSERT_FALSE(DropOptions.stripUnneededSymbols);
    std::string Error;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                             RetainedOutput, Format::ELF64LE, DropOptions,
                             &Error));
    EXPECT_NE(Error.find(DebugName.str()), std::string::npos) << Error;
  }
}

TEST(MergeELFSemantic, GenericDropDebugProducerRemovesGDBIndex) {
  using namespace ELF;
  const SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                     0xea};
  const SecSpec DebugIndex{".gdb_index", 16, 4, SHT_PROGBITS, 0, 0xeb};
  const SymSpec Function{"generic_drop_gdb_index", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, DebugIndex}, {Function}, {}, EM_AARCH64));

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options DropOptions;
    DropOptions.dropDebugInfo = true;
    DropOptions.verify = Verify;
    ASSERT_FALSE(DropOptions.androidKernelModule);
    ASSERT_FALSE(DropOptions.finalizeAndroidKernelModule);
    ASSERT_FALSE(DropOptions.stripUnneededSymbols);

    auto [DropOK, DroppedOutput] = mergeELF(Inputs, DropOptions);
    ASSERT_TRUE(DropOK);
    EXPECT_LT(parseELF(DroppedOutput).findSec(".gdb_index"), 0);
    std::string Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                            DroppedOutput, Format::ELF64LE, DropOptions,
                            &Error))
        << Error;
  }
}

TEST(MergeELFSemantic, GenericDropDebugRejectsAllocatedGDBIndexAtomically) {
  using namespace ELF;
  const SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                     0xec};
  const SecSpec AllocatedDebugIndex{".gdb_index", 16,        4,
                                    SHT_PROGBITS, SHF_ALLOC, 0xed};
  const SymSpec Function{"generic_allocated_gdb_index", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text, AllocatedDebugIndex}, {Function},
                                     {}, EM_AARCH64));

  Options DropOptions;
  DropOptions.dropDebugInfo = true;
  ASSERT_FALSE(DropOptions.androidKernelModule);
  ASSERT_FALSE(DropOptions.finalizeAndroidKernelModule);
  ASSERT_FALSE(DropOptions.stripUnneededSymbols);
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    DropOptions.verify = Verify;
    auto [DropOK, DroppedOutput] = mergeELF(Inputs, DropOptions);
    EXPECT_FALSE(DropOK);
    EXPECT_TRUE(DroppedOutput.empty());
  }

  SmallVector<SmallVector<char, 0>, 1> TextOnlyInputs;
  TextOnlyInputs.push_back(
      buildSectionedELF({Text}, {Function}, {}, EM_AARCH64));
  DropOptions.verify = false;
  auto [CandidateOK, CandidateOutput] = mergeELF(TextOnlyInputs, DropOptions);
  ASSERT_TRUE(CandidateOK);
  std::string Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                           CandidateOutput, Format::ELF64LE, DropOptions,
                           &Error));
  EXPECT_NE(Error.find("allocated"), std::string::npos) << Error;
  EXPECT_NE(Error.find(".gdb_index"), std::string::npos) << Error;
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsLegacyZdebugUnlessDebugIsDropped) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xe8};
  SecSpec LegacyDebug{".zdebug_info", 16, 1, SHT_PROGBITS, 0, 0xe9};
  SymSpec Function{"release_legacy_zdebug", 0, 0};
  const auto LegacyInput =
      buildSectionedELF({Text, LegacyDebug}, {Function}, {}, EM_AARCH64);

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "retain verify-on" : "retain verify-off");
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(LegacyInput);
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }

  Options DropOpts = androidKernelReleaseOptions();
  DropOpts.dropDebugInfo = true;
  SmallVector<SmallVector<char, 0>, 1> LegacyInputs;
  LegacyInputs.push_back(LegacyInput);
  auto [DropOK, DroppedOutput] = mergeELF(LegacyInputs, DropOpts);
  ASSERT_TRUE(DropOK);
  ASSERT_LT(parseELF(DroppedOutput).findSec(".zdebug_info"), 0);

  Options RetainOpts = androidKernelReleaseOptions();
  std::string Error;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(LegacyInputs),
                           DroppedOutput, Format::ELF64LE, RetainOpts, &Error));
  EXPECT_NE(Error.find(".zdebug"), std::string::npos) << Error;

  // Exercise the standalone audit independently of the producer/input replay:
  // rename an otherwise canonical retained section in place to the same-length
  // legacy spelling, keeping every other byte and header field valid.
  SecSpec Retained{".release_pad", 16, 1, SHT_PROGBITS, 0, 0xea};
  SmallVector<SmallVector<char, 0>, 1> RetainedInputs;
  RetainedInputs.push_back(
      buildSectionedELF({Text, Retained}, {Function}, {}, EM_AARCH64));
  auto [RetainedOK, Tampered] = mergeELF(RetainedInputs, RetainOpts);
  ASSERT_TRUE(RetainedOK);
  Elf64_Shdr *RetainedHeader = findELFSectionHeader(Tampered, ".release_pad");
  ASSERT_NE(RetainedHeader, nullptr);
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Tampered.data());
  auto *Sections =
      reinterpret_cast<Elf64_Shdr *>(Tampered.data() + Header->e_shoff);
  ASSERT_LT(Header->e_shstrndx, Header->e_shnum);
  const Elf64_Shdr &Shstrtab = Sections[Header->e_shstrndx];
  ASSERT_LE(Shstrtab.sh_offset + RetainedHeader->sh_name +
                StringRef(".zdebug_info").size(),
            Tampered.size());
  ASSERT_EQ(StringRef(".release_pad").size(), StringRef(".zdebug_info").size());
  memcpy(Tampered.data() + Shstrtab.sh_offset + RetainedHeader->sh_name,
         ".zdebug_info", StringRef(".zdebug_info").size());
  Error.clear();
  EXPECT_FALSE(verifyAndroidKernelModuleImage(Tampered, RetainOpts, &Error));
  EXPECT_NE(Error.find(".zdebug"), std::string::npos) << Error;

  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "drop verify-on" : "drop verify-off");
    Options Opts = DropOpts;
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(LegacyInputs, Opts);
    ASSERT_TRUE(OK);
    EXPECT_LT(parseELF(Output).findSec(".zdebug_info"), 0);
    Error.clear();
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(LegacyInputs),
                            Output, Format::ELF64LE, Opts, &Error))
        << Error;
  }
}

TEST(MergeELFSemantic, AndroidKernelReleaseDropDebugRemovesAndRejectsGDBIndex) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xeb};
  SecSpec DebugIndex{".gdb_index", 16, 4, SHT_PROGBITS, 0, 0xec};
  SymSpec Function{"release_gdb_index", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, DebugIndex}, {Function}, {}, EM_AARCH64));

  Options RetainOptions = androidKernelReleaseOptions();
  auto [RetainOK, RetainedOutput] = mergeELF(Inputs, RetainOptions);
  ASSERT_TRUE(RetainOK);
  ASSERT_GE(parseELF(RetainedOutput).findSec(".gdb_index"), 0);

  Options DropOptions = androidKernelReleaseOptions();
  DropOptions.dropDebugInfo = true;
  DropOptions.verify = true;
  std::string Error;
  EXPECT_FALSE(
      verifyAndroidKernelModuleImage(RetainedOutput, DropOptions, &Error));

  auto [DropOK, DroppedOutput] = mergeELF(Inputs, DropOptions);
  ASSERT_TRUE(DropOK);
  EXPECT_LT(parseELF(DroppedOutput).findSec(".gdb_index"), 0);
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseDropDebugRejectsAllocatedGDBIndexAtomically) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xed};
  SecSpec DebugIndex{".gdb_index", 16, 4, SHT_PROGBITS, SHF_ALLOC, 0xee};
  SymSpec Function{"release_allocated_gdb_index", 0, 0};

  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, DebugIndex}, {Function}, {}, EM_AARCH64));

  Options DropOptions = androidKernelReleaseOptions();
  DropOptions.dropDebugInfo = true;
  DropOptions.verify = false;
  auto [DropOK, DroppedOutput] = mergeELF(Inputs, DropOptions);
  EXPECT_FALSE(DropOK);
  EXPECT_TRUE(DroppedOutput.empty());

  SmallVector<SmallVector<char, 0>, 1> TextOnlyInputs;
  TextOnlyInputs.push_back(
      buildSectionedELF({Text}, {Function}, {}, EM_AARCH64));
  auto [CandidateOK, CandidateOutput] = mergeELF(TextOnlyInputs, DropOptions);
  ASSERT_TRUE(CandidateOK);
  std::string Error;
  EXPECT_FALSE(verifyMerge(Inputs, CandidateOutput, Format::ELF64LE,
                           DropOptions, &Error));
  EXPECT_NE(Error.find("allocated"), std::string::npos) << Error;
  EXPECT_NE(Error.find(".gdb_index"), std::string::npos) << Error;
}

TEST(MergeELFSemantic, AndroidKernelReleaseZlibDebugCompressionIsAuditable) {
  const compression::Format Format =
      compression::formatFor(DebugCompressionType::Zlib);
  if (const char *Reason = compression::getReasonIfUnsupported(Format))
    GTEST_SKIP() << Reason;
  expectAndroidKernelReleaseDebugCompression(DebugCompressionType::Zlib);
}

TEST(MergeELFSemantic, AndroidKernelReleaseZstdDebugCompressionIsAuditable) {
  const compression::Format Format =
      compression::formatFor(DebugCompressionType::Zstd);
  if (const char *Reason = compression::getReasonIfUnsupported(Format))
    GTEST_SKIP() << Reason;
  expectAndroidKernelReleaseDebugCompression(DebugCompressionType::Zstd);
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseAcceptsLoaderRelocationWidthsAtExactBoundary) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xea};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xeb};
  SymSpec Function{"release_relocation_boundary", 0, 0};
  const RelSpec Relocations[] = {
      {1, 8, Function.Name, R_AARCH64_NONE, 0},
      {1, 6, Function.Name, R_AARCH64_ABS16, 0},
      {1, 4, Function.Name, R_AARCH64_ABS32, 0},
      {1, 0, Function.Name, R_AARCH64_ABS64, 0},
      {0, 4, Function.Name, R_AARCH64_CALL26, 0},
  };
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data}, {Function}, Relocations, EM_AARCH64));
  for (bool Verify : {false, true}) {
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    ASSERT_TRUE(OK);
    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                            Format::ELF64LE, Opts, &Error))
        << Error;
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRejectsUnsupportedOrOverrunningRelocations) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xec};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xed};
  SymSpec Function{"release_bad_relocation_width", 0, 0};
  const RelSpec Invalid[] = {
      {1, 7, Function.Name, R_AARCH64_ABS16, 0},
      {1, 5, Function.Name, R_AARCH64_ABS32, 0},
      {1, 1, Function.Name, R_AARCH64_ABS64, 0},
      {0, 5, Function.Name, R_AARCH64_CALL26, 0},
      {1, 0, Function.Name, UINT32_C(0x7fffffff), 0},
      {1, 9, Function.Name, R_AARCH64_NONE, 0},
  };
  for (const RelSpec &Relocation : Invalid) {
    const auto Input =
        buildSectionedELF({Text, Data}, {Function}, {Relocation}, EM_AARCH64);
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(
          (Twine(Relocation.Type) + (Verify ? " verify-on" : " verify-off"))
              .str());
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(Input);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseStandaloneAuditChecksRelocationWidthAndType) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xee};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xef};
  SymSpec Function{"release_relocation_audit", 0, 0};
  RelSpec Reference{1, 0, Function.Name, R_AARCH64_ABS64, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data}, {Function}, {Reference}, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  auto ExpectBothReject = [&](SmallVector<char, 0> Image, StringRef Case) {
    SCOPED_TRACE(Case.str());
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Image, Opts, &Error));
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Image,
                             Format::ELF64LE, Opts, &Error));
  };

  SmallVector<char, 0> Overrun(Output.begin(), Output.end());
  ELF::Elf64_Shdr *RelaHeader = findELFSectionHeader(Overrun, ".rela.data");
  ASSERT_NE(RelaHeader, nullptr);
  auto *Relocation = reinterpret_cast<ELF::Elf64_Rela *>(Overrun.data() +
                                                         RelaHeader->sh_offset);
  Relocation->r_offset = 1;
  ExpectBothReject(std::move(Overrun), "ABS64 overruns target");

  SmallVector<char, 0> Unknown(Output.begin(), Output.end());
  RelaHeader = findELFSectionHeader(Unknown, ".rela.data");
  ASSERT_NE(RelaHeader, nullptr);
  Relocation = reinterpret_cast<ELF::Elf64_Rela *>(Unknown.data() +
                                                   RelaHeader->sh_offset);
  Relocation->setSymbolAndType(Relocation->getSymbol(), UINT32_C(0x7fffffff));
  ExpectBothReject(std::move(Unknown), "unsupported relocation type");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseChecksAllLoaderRelocationBoundariesIndependently) {
  using namespace ELF;
  struct RelocationExpectation {
    const char *Name;
    uint32_t Type;
    uint8_t Width;
  };
  // Authoritative test oracle copied from the Linux AArch64 module loader's
  // static RELA cases. Deliberately do not call writeWidth() here: a missing,
  // extra, or wrongly-sized production policy entry must disagree with this
  // table instead of teaching the test the same bug.
  constexpr RelocationExpectation Cases[] = {
      {"NONE", R_AARCH64_NONE, 0},
      {"ABS16", R_AARCH64_ABS16, 2},
      {"PREL16", R_AARCH64_PREL16, 2},
      {"ABS32", R_AARCH64_ABS32, 4},
      {"PREL32", R_AARCH64_PREL32, 4},
      {"ABS64", R_AARCH64_ABS64, 8},
      {"PREL64", R_AARCH64_PREL64, 8},
      {"MOVW_UABS_G0", R_AARCH64_MOVW_UABS_G0, 4},
      {"MOVW_UABS_G0_NC", R_AARCH64_MOVW_UABS_G0_NC, 4},
      {"MOVW_UABS_G1", R_AARCH64_MOVW_UABS_G1, 4},
      {"MOVW_UABS_G1_NC", R_AARCH64_MOVW_UABS_G1_NC, 4},
      {"MOVW_UABS_G2", R_AARCH64_MOVW_UABS_G2, 4},
      {"MOVW_UABS_G2_NC", R_AARCH64_MOVW_UABS_G2_NC, 4},
      {"MOVW_UABS_G3", R_AARCH64_MOVW_UABS_G3, 4},
      {"MOVW_SABS_G0", R_AARCH64_MOVW_SABS_G0, 4},
      {"MOVW_SABS_G1", R_AARCH64_MOVW_SABS_G1, 4},
      {"MOVW_SABS_G2", R_AARCH64_MOVW_SABS_G2, 4},
      {"MOVW_PREL_G0", R_AARCH64_MOVW_PREL_G0, 4},
      {"MOVW_PREL_G0_NC", R_AARCH64_MOVW_PREL_G0_NC, 4},
      {"MOVW_PREL_G1", R_AARCH64_MOVW_PREL_G1, 4},
      {"MOVW_PREL_G1_NC", R_AARCH64_MOVW_PREL_G1_NC, 4},
      {"MOVW_PREL_G2", R_AARCH64_MOVW_PREL_G2, 4},
      {"MOVW_PREL_G2_NC", R_AARCH64_MOVW_PREL_G2_NC, 4},
      {"MOVW_PREL_G3", R_AARCH64_MOVW_PREL_G3, 4},
      {"LD_PREL_LO19", R_AARCH64_LD_PREL_LO19, 4},
      {"ADR_PREL_LO21", R_AARCH64_ADR_PREL_LO21, 4},
      {"ADR_PREL_PG_HI21", R_AARCH64_ADR_PREL_PG_HI21, 4},
      {"ADR_PREL_PG_HI21_NC", R_AARCH64_ADR_PREL_PG_HI21_NC, 4},
      {"ADD_ABS_LO12_NC", R_AARCH64_ADD_ABS_LO12_NC, 4},
      {"LDST8_ABS_LO12_NC", R_AARCH64_LDST8_ABS_LO12_NC, 4},
      {"LDST16_ABS_LO12_NC", R_AARCH64_LDST16_ABS_LO12_NC, 4},
      {"LDST32_ABS_LO12_NC", R_AARCH64_LDST32_ABS_LO12_NC, 4},
      {"LDST64_ABS_LO12_NC", R_AARCH64_LDST64_ABS_LO12_NC, 4},
      {"LDST128_ABS_LO12_NC", R_AARCH64_LDST128_ABS_LO12_NC, 4},
      {"TSTBR14", R_AARCH64_TSTBR14, 4},
      {"CONDBR19", R_AARCH64_CONDBR19, 4},
      {"JUMP26", R_AARCH64_JUMP26, 4},
      {"CALL26", R_AARCH64_CALL26, 4},
  };
  static_assert(sizeof(Cases) / sizeof(Cases[0]) == 38,
                "keep the loader relocation oracle exhaustive");

  constexpr uint64_t TargetSize = 16;
  SecSpec Text{".text", TargetSize, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xf8};
  SymSpec Function{"release_all_relocation_widths", 0, 0};
  for (const RelocationExpectation &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const uint64_t ExactBoundary = TargetSize - Case.Width;
    RelSpec Good{0, ExactBoundary, Function.Name, Case.Type, 0};
    const auto GoodInput =
        buildSectionedELF({Text}, {Function}, {Good}, EM_AARCH64);
    SmallVector<char, 0> CanonicalOutput;
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "exact verify-on" : "exact verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(GoodInput);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      ASSERT_TRUE(OK);
      if (!Verify)
        CanonicalOutput = Output;
      std::string Error;
      EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error))
          << Error;
      EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                              Format::ELF64LE, Opts, &Error))
          << Error;
    }

    RelSpec Overrun = Good;
    ++Overrun.Offset;
    const auto BadInput =
        buildSectionedELF({Text}, {Function}, {Overrun}, EM_AARCH64);
    for (bool Verify : {false, true}) {
      SCOPED_TRACE(Verify ? "overrun verify-on" : "overrun verify-off");
      SmallVector<SmallVector<char, 0>, 1> Inputs;
      Inputs.push_back(BadInput);
      Options Opts = androidKernelReleaseOptions();
      Opts.verify = Verify;
      auto [OK, Output] = mergeELF(Inputs, Opts);
      EXPECT_FALSE(OK);
      EXPECT_TRUE(Output.empty());
    }

    ASSERT_FALSE(CanonicalOutput.empty());
    Elf64_Shdr *Rela = findELFSectionHeader(CanonicalOutput, ".rela.text");
    ASSERT_NE(Rela, nullptr);
    ASSERT_GE(Rela->sh_size, sizeof(Elf64_Rela));
    auto *Entry = reinterpret_cast<Elf64_Rela *>(CanonicalOutput.data() +
                                                 Rela->sh_offset);
    ++Entry->r_offset;
    Options Opts = androidKernelReleaseOptions();
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(CanonicalOutput, Opts, &Error));
    SmallVector<SmallVector<char, 0>, 1> Inputs;
    Inputs.push_back(GoodInput);
    Error.clear();
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                             CanonicalOutput, Format::ELF64LE, Opts, &Error));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseStandaloneAuditRejectsUnreachableStrtabPrefix) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xf0};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xf1};
  SymSpec Import{"release_exact_import_name", -1, 0};
  RelSpec Reference{1, 0, Import.Name, R_AARCH64_ABS64, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data}, {Import}, {Reference}, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  ELF::Elf64_Shdr *Symtab = findELFSectionHeader(Output, ".symtab");
  ASSERT_NE(Symtab, nullptr);
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Output.data());
  auto *Sections =
      reinterpret_cast<Elf64_Shdr *>(Output.data() + Header->e_shoff);
  ASSERT_LT(Symtab->sh_link, Header->e_shnum);
  Elf64_Shdr &Strtab = Sections[Symtab->sh_link];
  auto *Symbols =
      reinterpret_cast<Elf64_Sym *>(Output.data() + Symtab->sh_offset);
  const unsigned Count = Symtab->sh_size / sizeof(Elf64_Sym);
  Elf64_Sym *ImportSymbol = nullptr;
  for (unsigned I = 0; I < Count; ++I) {
    ASSERT_LT(Symbols[I].st_name, Strtab.sh_size);
    if (StringRef(Output.data() + Strtab.sh_offset + Symbols[I].st_name) ==
        Import.Name)
      ImportSymbol = &Symbols[I];
  }
  ASSERT_NE(ImportSymbol, nullptr);
  ++ImportSymbol->st_name;

  std::string Error;
  EXPECT_FALSE(verifyAndroidKernelModuleImage(Output, Opts, &Error));
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseStandaloneAuditAllowsReferencedSuffixSharing) {
  using namespace ELF;
  constexpr StringLiteral LongImport("prefix_suffix_import");
  constexpr StringLiteral SuffixImport("suffix_import");
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xf2};
  SecSpec Data{".data", 16, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xf3};
  SymSpec Long{LongImport.str(), -1, 0};
  SymSpec Suffix{SuffixImport.str(), -1, 0};
  RelSpec LongReference{1, 0, Long.Name, R_AARCH64_ABS64, 0};
  RelSpec SuffixReference{1, 8, Suffix.Name, R_AARCH64_ABS64, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text, Data}, {Long, Suffix},
                                     {LongReference, SuffixReference},
                                     EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  ELF::Elf64_Shdr *Symtab = findELFSectionHeader(Output, ".symtab");
  ASSERT_NE(Symtab, nullptr);
  auto *Header = reinterpret_cast<Elf64_Ehdr *>(Output.data());
  auto *Sections =
      reinterpret_cast<Elf64_Shdr *>(Output.data() + Header->e_shoff);
  ASSERT_LT(Symtab->sh_link, Header->e_shnum);
  Elf64_Shdr &Strtab = Sections[Symtab->sh_link];
  auto *Symbols =
      reinterpret_cast<Elf64_Sym *>(Output.data() + Symtab->sh_offset);
  const unsigned Count = Symtab->sh_size / sizeof(Elf64_Sym);
  Elf64_Sym *SuffixSymbol = nullptr;
  uint32_t LongOffset = 0;
  uint32_t OldSuffixOffset = 0;
  for (unsigned I = 0; I < Count; ++I) {
    ASSERT_LT(Symbols[I].st_name, Strtab.sh_size);
    StringRef Name(Output.data() + Strtab.sh_offset + Symbols[I].st_name);
    if (Name == LongImport)
      LongOffset = Symbols[I].st_name;
    if (Name == SuffixImport) {
      SuffixSymbol = &Symbols[I];
      OldSuffixOffset = Symbols[I].st_name;
    }
  }
  ASSERT_NE(LongOffset, 0u);
  ASSERT_NE(SuffixSymbol, nullptr);
  constexpr uint32_t PrefixLength = LongImport.size() - SuffixImport.size();
  SuffixSymbol->st_name = LongOffset + PrefixLength;
  memset(Output.data() + Strtab.sh_offset + OldSuffixOffset, 0,
         SuffixImport.size());

  std::string Error;
  EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                          Format::ELF64LE, Opts, &Error))
      << Error;
}

TEST(MergeELFSemantic, AndroidKernelReleaseRequiresCanonicalTwoStringTables) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xf4};
  SecSpec Debug{".debug_extra", 4, 1, SHT_PROGBITS, 0, 0xf5};
  SymSpec Function{"release_canonical_strtabs", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Debug}, {Function}, {}, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  auto ExpectBothReject = [&](SmallVector<char, 0> Image, StringRef Case) {
    SCOPED_TRACE(Case.str());
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Image, Opts, &Error));
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Image,
                             Format::ELF64LE, Opts, &Error));
  };

  SmallVector<char, 0> Extra(Output.begin(), Output.end());
  Elf64_Shdr *DebugHeader = findELFSectionHeader(Extra, ".debug_extra");
  ASSERT_NE(DebugHeader, nullptr);
  DebugHeader->sh_type = SHT_STRTAB;
  ExpectBothReject(std::move(Extra), "extra SHT_STRTAB");

  SmallVector<char, 0> BadShstrtab(Output.begin(), Output.end());
  auto *BadHeader = reinterpret_cast<Elf64_Ehdr *>(BadShstrtab.data());
  auto *BadSections =
      reinterpret_cast<Elf64_Shdr *>(BadShstrtab.data() + BadHeader->e_shoff);
  ASSERT_LT(BadHeader->e_shstrndx, BadHeader->e_shnum);
  BadSections[BadHeader->e_shstrndx].sh_name = 0;
  ExpectBothReject(std::move(BadShstrtab), "noncanonical .shstrtab name");

  SmallVector<char, 0> BadStrtabFlags(Output.begin(), Output.end());
  Elf64_Shdr *StrtabHeader = findELFSectionHeader(BadStrtabFlags, ".strtab");
  ASSERT_NE(StrtabHeader, nullptr);
  StrtabHeader->sh_flags = SHF_ALLOC;
  ExpectBothReject(std::move(BadStrtabFlags), "noncanonical .strtab flags");

  SmallVector<char, 0> BadShstrtabShape(Output.begin(), Output.end());
  BadHeader = reinterpret_cast<Elf64_Ehdr *>(BadShstrtabShape.data());
  BadSections = reinterpret_cast<Elf64_Shdr *>(BadShstrtabShape.data() +
                                               BadHeader->e_shoff);
  ASSERT_LT(BadHeader->e_shstrndx, BadHeader->e_shnum);
  BadSections[BadHeader->e_shstrndx].sh_entsize = 1;
  BadSections[BadHeader->e_shstrndx].sh_addralign = 2;
  ExpectBothReject(std::move(BadShstrtabShape),
                   "noncanonical .shstrtab header shape");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRequiresOneOrderedCanonicalMetadataSuffix) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xf6};
  SecSpec Data{".data", 8, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xf7};
  SymSpec Function{"release_metadata_schema", 0, 0};
  const RelSpec Relocations[] = {
      {0, 4, Function.Name, R_AARCH64_CALL26, 0},
      {1, 0, Function.Name, R_AARCH64_ABS64, 0},
  };
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(
      buildSectionedELF({Text, Data}, {Function}, Relocations, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  auto HeaderAndSections = [](SmallVectorImpl<char> &Image) {
    auto *Header = reinterpret_cast<Elf64_Ehdr *>(Image.data());
    auto *Sections =
        reinterpret_cast<Elf64_Shdr *>(Image.data() + Header->e_shoff);
    return std::pair{Header, Sections};
  };
  auto SectionIndex = [&](SmallVectorImpl<char> &Image,
                          StringRef Name) -> unsigned {
    auto [Header, Sections] = HeaderAndSections(Image);
    Elf64_Shdr *Section = findELFSectionHeader(Image, Name);
    EXPECT_NE(Section, nullptr) << Name.str();
    return Section ? static_cast<unsigned>(Section - Sections)
                   : Header->e_shnum;
  };
  auto ExpectBothReject = [&](SmallVector<char, 0> Image, StringRef Case) {
    SCOPED_TRACE(Case.str());
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Image, Opts, &Error));
    Error.clear();
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Image,
                             Format::ELF64LE, Opts, &Error));
  };

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    auto [Header, Sections] = HeaderAndSections(Image);
    ASSERT_GT(Header->e_shnum, 0u);
    Sections[0].sh_offset = 1;
    ExpectBothReject(std::move(Image), "section zero is not entirely zero");
  }

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    auto [Header, Sections] = HeaderAndSections(Image);
    const unsigned Symtab = SectionIndex(Image, ".symtab");
    const unsigned Strtab = SectionIndex(Image, ".strtab");
    ASSERT_LT(Symtab, Header->e_shnum);
    ASSERT_LT(Strtab, Header->e_shnum);
    ASSERT_EQ(Strtab, Symtab + 1);
    std::swap(Sections[Symtab], Sections[Strtab]);
    Sections[Strtab].sh_link = Symtab;
    for (unsigned I = 0; I < Header->e_shnum; ++I)
      if (Sections[I].sh_type == SHT_RELA)
        Sections[I].sh_link = Strtab;
    ExpectBothReject(std::move(Image), ".strtab precedes .symtab");
  }

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    auto [Header, Sections] = HeaderAndSections(Image);
    const unsigned TextRela = SectionIndex(Image, ".rela.text");
    const unsigned DataRela = SectionIndex(Image, ".rela.data");
    ASSERT_LT(TextRela, Header->e_shnum);
    ASSERT_LT(DataRela, Header->e_shnum);
    ASSERT_LT(TextRela, DataRela);
    std::swap(Sections[TextRela], Sections[DataRela]);
    ExpectBothReject(std::move(Image),
                     "relocation metadata is not target-ordinal ordered");
  }

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    Elf64_Shdr *Rela = findELFSectionHeader(Image, ".rela.data");
    ASSERT_NE(Rela, nullptr);
    Rela->sh_flags = SHF_ALLOC;
    ExpectBothReject(std::move(Image), "relocation metadata has flags");
  }

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    Elf64_Shdr *Rela = findELFSectionHeader(Image, ".rela.data");
    ASSERT_NE(Rela, nullptr);
    Rela->sh_size = 0;
    ExpectBothReject(std::move(Image), "empty relocation metadata");
  }

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    auto [Header, Sections] = HeaderAndSections(Image);
    const unsigned Rela = SectionIndex(Image, ".rela.data");
    const unsigned Shstrtab = SectionIndex(Image, ".shstrtab");
    ASSERT_LT(Rela, Header->e_shnum);
    ASSERT_EQ(Shstrtab + 1, Header->e_shnum);
    std::swap(Sections[Rela], Sections[Shstrtab]);
    Header->e_shstrndx = Rela;
    ExpectBothReject(std::move(Image), ".shstrtab is not the last section");
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRequiresAnEntirelyZeroNullSymbolRecord) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xf9};
  SymSpec Function{"release_zero_null_symbol", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text}, {Function}, {}, EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  enum class Field { Name, Value, Size, Section, Info, Other };
  for (Field Mutated : {Field::Name, Field::Value, Field::Size, Field::Section,
                        Field::Info, Field::Other}) {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    Elf64_Shdr *Symtab = findELFSectionHeader(Image, ".symtab");
    ASSERT_NE(Symtab, nullptr);
    ASSERT_GE(Symtab->sh_size, sizeof(Elf64_Sym));
    auto *Null =
        reinterpret_cast<Elf64_Sym *>(Image.data() + Symtab->sh_offset);
    switch (Mutated) {
    case Field::Name:
      Null->st_name = 1;
      break;
    case Field::Value:
      Null->st_value = 1;
      break;
    case Field::Size:
      Null->st_size = 1;
      break;
    case Field::Section:
      Null->st_shndx = 1;
      break;
    case Field::Info:
      Null->st_info = 1;
      break;
    case Field::Other:
      Null->st_other = 1;
      break;
    }
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Image, Opts, &Error));
    Error.clear();
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Image,
                             Format::ELF64LE, Opts, &Error));
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseRebuildsAndAuditsReachableSectionNames) {
  using namespace ELF;
  SecSpec Text{".text", 8, 4, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xfa};
  SecSpec Padding{".release_pad", 8, 1, SHT_PROGBITS, 0, 0xfb};
  SymSpec Function{"release_reachable_shstrtab", 0, 0};
  const auto CanonicalInput =
      buildSectionedELF({Text, Padding}, {Function}, {}, EM_AARCH64);

  // The apparent input name starts one byte into an older string. A canonical
  // producer must serialize only the referenced suffix, not copy the stale
  // leading byte from the input section-name table.
  SmallVector<char, 0> PrefixInput(CanonicalInput.begin(),
                                   CanonicalInput.end());
  Elf64_Shdr *PrefixHeader = findELFSectionHeader(PrefixInput, ".release_pad");
  ASSERT_NE(PrefixHeader, nullptr);
  ++PrefixHeader->sh_name;
  SmallVector<SmallVector<char, 0>, 1> PrefixInputs;
  PrefixInputs.push_back(PrefixInput);
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "producer verify-on" : "producer verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(PrefixInputs, Opts);
    ASSERT_TRUE(OK);
    EXPECT_NE(findELFSectionHeader(Output, "release_pad"), nullptr);
    EXPECT_EQ(findELFSectionHeader(Output, ".release_pad"), nullptr);
    auto *Header = reinterpret_cast<Elf64_Ehdr *>(Output.data());
    auto *Sections =
        reinterpret_cast<Elf64_Shdr *>(Output.data() + Header->e_shoff);
    ASSERT_LT(Header->e_shstrndx, Header->e_shnum);
    const Elf64_Shdr &Shstrtab = Sections[Header->e_shstrndx];
    StringRef Names(Output.data() + Shstrtab.sh_offset, Shstrtab.sh_size);
    EXPECT_EQ(Names.find(".release_pad"), StringRef::npos);
    std::string Error;
    EXPECT_TRUE(verifyAndroidKernelModuleImage(Output, Opts, &Error)) << Error;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(PrefixInputs),
                            Output, Format::ELF64LE, Opts, &Error))
        << Error;
  }

  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(CanonicalInput);
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  auto ExpectBothRejectAsShstrtab = [&](SmallVector<char, 0> Image,
                                        StringRef Case) {
    SCOPED_TRACE(Case.str());
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Image, Opts, &Error));
    EXPECT_NE(Error.find(".shstrtab"), std::string::npos) << Error;
    Error.clear();
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Image,
                             Format::ELF64LE, Opts, &Error));
    EXPECT_NE(Error.find(".shstrtab"), std::string::npos) << Error;
  };

  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    Elf64_Shdr *Section = findELFSectionHeader(Image, ".release_pad");
    ASSERT_NE(Section, nullptr);
    ++Section->sh_name;
    ExpectBothRejectAsShstrtab(
        std::move(Image),
        "stale section-name prefix is unreachable from every sh_name");
  }
  {
    SmallVector<char, 0> Image(Output.begin(), Output.end());
    auto *Header = reinterpret_cast<Elf64_Ehdr *>(Image.data());
    auto *Sections =
        reinterpret_cast<Elf64_Shdr *>(Image.data() + Header->e_shoff);
    ASSERT_LT(Header->e_shstrndx, Header->e_shnum);
    const Elf64_Shdr &Shstrtab = Sections[Header->e_shstrndx];
    ASSERT_LT(Shstrtab.sh_offset, Image.size());
    Image[Shstrtab.sh_offset] = 'X';
    ExpectBothRejectAsShstrtab(std::move(Image),
                               ".shstrtab does not start with NUL");
  }
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseVerifiersRejectTruncatedPayloadAndRelocationSite) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd3};
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0xd4};
  SymSpec Function{"release_output_range", 0, 0};
  RelSpec Reference{1, 0, Function.Name, ELF::R_AARCH64_ABS64, 4};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text, Data}, {Function}, {Reference},
                                     ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  auto ExpectBothReject = [&](ArrayRef<char> Image, StringRef Case) {
    SCOPED_TRACE(Case.str());
    std::string Error;
    EXPECT_FALSE(verifyAndroidKernelModuleImage(Image, Opts, &Error));
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Image,
                             Format::ELF64LE, Opts, &Error));
  };

  SmallVector<char, 0> Truncated(Output.begin(), Output.end());
  ELF::Elf64_Shdr *DataHeader = findELFSectionHeader(Truncated, ".data");
  ASSERT_NE(DataHeader, nullptr);
  DataHeader->sh_offset = Truncated.size() - 4;
  DataHeader->sh_size = 8;
  ExpectBothReject(Truncated, "truncated section payload");

  SmallVector<char, 0> BadSite(Output.begin(), Output.end());
  ElfView View = parseELF(BadSite);
  ASSERT_TRUE(View.Ok);
  ASSERT_EQ(View.Relas.size(), 1u);
  ASSERT_LT(View.Relas[0].TargetSec, View.Secs.size());
  ASSERT_TRUE(
      patchAllRelaOffsets(BadSite, View.Secs[View.Relas[0].TargetSec].Size));
  ExpectBothReject(BadSite, "output relocation site past target section");

  SmallVector<char, 0> BadName(Output.begin(), Output.end());
  ELF::Elf64_Shdr *OutputStrtab = findELFSectionHeader(BadName, ".strtab");
  ASSERT_NE(OutputStrtab, nullptr);
  ASSERT_TRUE(patchELFSymbolNameOffset(BadName, "fn_0", OutputStrtab->sh_size));
  ExpectBothReject(BadName, "output st_name past string table");

  SmallVector<char, 0> BadSymtabLink(Output.begin(), Output.end());
  ELF::Elf64_Shdr *OutputSymtab =
      findELFSectionHeader(BadSymtabLink, ".symtab");
  ASSERT_NE(OutputSymtab, nullptr);
  OutputSymtab->sh_link = 1;
  ExpectBothReject(BadSymtabLink, "output symtab links a non-string section");

  SmallVector<char, 0> BadHeaderSize(Output.begin(), Output.end());
  auto *OutputHeader =
      reinterpret_cast<ELF::Elf64_Ehdr *>(BadHeaderSize.data());
  OutputHeader->e_shentsize = 1;
  ExpectBothReject(BadHeaderSize, "output e_shentsize");

  SmallVector<char, 0> ExtendedCount(Output.begin(), Output.end());
  OutputHeader = reinterpret_cast<ELF::Elf64_Ehdr *>(ExtendedCount.data());
  auto *OutputSections = reinterpret_cast<ELF::Elf64_Shdr *>(
      ExtendedCount.data() + OutputHeader->e_shoff);
  OutputSections[0].sh_size = OutputHeader->e_shnum;
  OutputHeader->e_shnum = 0;
  ExpectBothReject(ExtendedCount, "output extended e_shnum");

  SmallVector<char, 0> ExtendedShstrndx(Output.begin(), Output.end());
  OutputHeader = reinterpret_cast<ELF::Elf64_Ehdr *>(ExtendedShstrndx.data());
  OutputSections = reinterpret_cast<ELF::Elf64_Shdr *>(ExtendedShstrndx.data() +
                                                       OutputHeader->e_shoff);
  OutputSections[0].sh_link = OutputHeader->e_shstrndx;
  OutputHeader->e_shstrndx = ELF::SHN_XINDEX;
  ExpectBothReject(ExtendedShstrndx, "output extended e_shstrndx");
}

TEST(MergeELFSemantic,
     AndroidKernelReleaseVerifiersRejectNonzeroOutputSectionAddress) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xd8};
  SymSpec Function{"release_output_section_address", 0, 0};
  SmallVector<SmallVector<char, 0>, 1> Inputs;
  Inputs.push_back(buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);

  SmallVector<char, 0> Tampered(Output.begin(), Output.end());
  ELF::Elf64_Shdr *TextHeader = findELFSectionHeader(Tampered, ".text");
  ASSERT_NE(TextHeader, nullptr);
  TextHeader->sh_addr = 0x1000;
  std::string Error;
  EXPECT_FALSE(verifyAndroidKernelModuleImage(Tampered, Opts, &Error));
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Tampered,
                           Format::ELF64LE, Opts, &Error));
}

TEST(MergeELFSemantic,
     AndroidKernelReleasePrefersGlobalUndefinedOverWeakUndefined) {
  SecSpec Data{
      ".data", 8, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE, 0xd5};
  SymSpec Weak{"release_binding_import", -1, 0};
  Weak.Type = ELF::STT_NOTYPE;
  Weak.Weak = true;
  SymSpec Global = Weak;
  Global.Weak = false;
  RelSpec Reference{0, 0, Weak.Name, ELF::R_AARCH64_ABS64, 0};
  const auto WeakInput =
      buildSectionedELF({Data}, {Weak}, {Reference}, ELF::EM_AARCH64);
  const auto GlobalInput =
      buildSectionedELF({Data}, {Global}, {Reference}, ELF::EM_AARCH64);

  for (bool WeakFirst : {false, true}) {
    SCOPED_TRACE(WeakFirst ? "weak-first" : "global-first");
    SmallVector<SmallVector<char, 0>, 2> Inputs;
    Inputs.push_back(WeakFirst ? WeakInput : GlobalInput);
    Inputs.push_back(WeakFirst ? GlobalInput : WeakInput);
    auto [OK, Output] = mergeELF(Inputs, androidKernelReleaseOptions());
    ASSERT_TRUE(OK);
    ElfView View = parseELF(Output);
    ASSERT_TRUE(View.Ok);
    const ParsedSym *Import = View.findSym(Weak.Name);
    ASSERT_NE(Import, nullptr);
    EXPECT_EQ(Import->Bind, ELF::STB_GLOBAL);
  }
}

TEST(MergeELFSemantic, AndroidKernelReleaseRejectsAllEmptyPartitions) {
  SmallVector<SmallVector<char, 0>, 2> Inputs(2);
  for (bool Verify : {false, true}) {
    SCOPED_TRACE(Verify ? "verify-on" : "verify-off");
    Options Opts = androidKernelReleaseOptions();
    Opts.verify = Verify;
    auto [OK, Output] = mergeELF(Inputs, Opts);
    EXPECT_FALSE(OK);
    EXPECT_TRUE(Output.empty());
  }
}

TEST(MergeELFSemantic, AndroidKernelReleaseReplaysMixedEmptyAndRealPartitions) {
  SecSpec Text{
      ".text", 0x10, 16, ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
      0xc1};
  SymSpec Function{"release_after_empty_partition", 0, 0};
  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.emplace_back();
  Inputs.push_back(buildSectionedELF({Text}, {Function}, {}, ELF::EM_AARCH64));
  Options Opts = androidKernelReleaseOptions();
  ASSERT_TRUE(Opts.verify);
  auto [OK, Output] = mergeELF(Inputs, Opts);
  ASSERT_TRUE(OK);
  ElfView View = parseELF(Output);
  ASSERT_TRUE(View.Ok);
  EXPECT_NE(View.findSym("fn_0"), nullptr);
  std::string Error;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs), Output,
                          Format::ELF64LE, Opts, &Error))
      << Error;
}

TEST(MergeELFSemantic,
     AndroidKernelModuleRejectsRelocationToDroppedProfileContract) {
  SecSpec Data{".data", 8, 8, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec Contract{".neverc.android.kernel.profile",
                   8,
                   8,
                   ELF::SHT_PROGBITS,
                   ELF::SHF_ALLOC,
                   0xab};
  SymSpec Anchor{"data_anchor", 0, 0};
  RelSpec ContractReference{0,
                            0,
                            "",
                            ELF::R_AARCH64_ABS64,
                            0,
                            /*TargetSecSym=*/1};
  auto Obj = buildSectionedELF({Data, Contract}, {Anchor}, {ContractReference},
                               ELF::EM_AARCH64);
  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));

  Options Opts;
  Opts.mergeSections = true;
  Opts.androidKernelModule = true;
  Opts.finalizeAndroidKernelModule = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  EXPECT_FALSE(OK);
  (void)Out;
}

TEST(MergeELFSemantic, GlobalSymbolDedupKeepsDefinition) {
  // Partition 0 defines "shared"; partition 1 references it (undefined) and
  // relocates against it.  The merged object must contain a single defined
  // "shared" and the relocation must resolve onto that defined slot.
  SecSpec S0{".text", 0x20, 16};
  SecSpec S1{".text", 0x20, 16};
  SymSpec Def{"shared", 0, 0};    // defined in partition 0
  SymSpec Undef{"shared", -1, 0}; // undefined in partition 1
  RelSpec R1{0, 0, "shared", ELF::R_X86_64_PLT32, 0};

  auto Obj0 = buildSectionedELF({S0}, {Def}, {});
  auto Obj1 = buildSectionedELF({S1}, {Undef}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  unsigned NShared = 0, NDefined = 0;
  for (auto &S : V.Syms)
    if (S.Name == "shared") {
      NShared++;
      if (S.Shndx != ELF::SHN_UNDEF)
        NDefined++;
    }
  EXPECT_EQ(NShared, 1u);  // deduped to a single entry
  EXPECT_EQ(NDefined, 1u); // and it is the definition, not the undef

  ASSERT_EQ(V.Relas.size(), 1u);
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string("shared"));
  EXPECT_NE(V.Syms[V.Relas[0].Sym].Shndx, ELF::SHN_UNDEF);
}

TEST(MergeELFSemantic, PcgSymbolResolvesToLocalDefinition) {
  // The parallel-codegen hot path: a module-local symbol is externalized with
  // the ".__pcg<hash>" suffix so a cross-partition reference resolves.  After
  // merge it must (1) dedup to a single entry, (2) be demoted back to LOCAL so
  // it never leaks into the final binary's exports, (3) keep its correct merged
  // offset, and (4) be the symbol the cross-partition relocation points at.
  const char *PcgName = "helper.__pcg12345678";
  SecSpec S0{".text", 0x40, 16};
  SecSpec S1{".text", 0x20, 16};
  SymSpec Def{PcgName, 0, 0x10, true}; // defined GLOBAL in partition 0
  SymSpec Ref{PcgName, -1, 0, true};   // undefined reference in partition 1
  RelSpec R1{0, 0, PcgName, ELF::R_X86_64_PLT32, 0};

  auto Obj0 = buildSectionedELF({S0}, {Def}, {});
  auto Obj1 = buildSectionedELF({S1}, {Ref}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  unsigned NPcg = 0;
  const ParsedSym *Pcg = nullptr;
  for (auto &S : V.Syms)
    if (S.Name == PcgName) {
      NPcg++;
      Pcg = &S;
    }
  ASSERT_EQ(NPcg, 1u); // deduped to one entry
  ASSERT_NE(Pcg, nullptr);
  EXPECT_EQ(Pcg->Bind, (uint8_t)ELF::STB_LOCAL); // demoted from GLOBAL
  EXPECT_NE(Pcg->Shndx, ELF::SHN_UNDEF);         // it is the definition
  EXPECT_EQ(Pcg->Value, 0x10u);                  // partition 0 base 0 + 0x10

  ASSERT_EQ(V.Relas.size(), 1u);
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string(PcgName));
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Bind, (uint8_t)ELF::STB_LOCAL);
}

TEST(MergeELFSemantic, SectionContentBytesPlacedCorrectly) {
  // Symbol offsets can be right while the *content* bytes are copied to the
  // wrong place (or dropped).  Fill each input section with a distinct byte
  // pattern and verify it lands at the expected offset in the merged section.
  SecSpec SA{".text.a",
             0x20,
             16,
             ELF::SHT_PROGBITS,
             ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
             0xAA};
  SecSpec SB{".text.b",
             0x20,
             16,
             ELF::SHT_PROGBITS,
             ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
             0xBB};
  auto Obj0 = buildSectionedELF({SA, SB}, {}, {});

  SecSpec SC{".text.c",
             0x10,
             16,
             ELF::SHT_PROGBITS,
             ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
             0xCC};
  auto Obj1 = buildSectionedELF({SC}, {}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int Idx = V.findSec(".text");
  ASSERT_GE(Idx, 0);
  const auto &D = V.Secs[Idx].Data;
  // Layout: [0x00,0x20) = 0xAA, [0x20,0x40) = 0xBB, [0x40,0x50) = 0xCC.
  ASSERT_EQ(D.size(), 0x50u);
  for (unsigned i = 0x00; i < 0x20; ++i)
    ASSERT_EQ(D[i], 0xAA) << "offset " << i;
  for (unsigned i = 0x20; i < 0x40; ++i)
    ASSERT_EQ(D[i], 0xBB) << "offset " << i;
  for (unsigned i = 0x40; i < 0x50; ++i)
    ASSERT_EQ(D[i], 0xCC) << "offset " << i;
}

TEST(MergeELFSemantic, RandomizedLayoutOracle) {
  // Property test: generate random multi-partition, multi-section layouts and
  // independently predict every symbol's merged offset with a from-scratch
  // re-implementation of the concatenate-and-align algorithm.  Any divergence
  // in the merger's offset math (for *any* layout, not just hand-picked ones)
  // makes this fail.  This is the strongest guard against silent offset bugs.
  struct Group {
    const char *Name;
    uint32_t Type;
    uint64_t Flags;
  };
  const Group Groups[4] = {
      {".text", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR},
      {".data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE},
      {".rodata", ELF::SHT_PROGBITS, ELF::SHF_ALLOC},
      {".bss", ELF::SHT_NOBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE},
  };

  std::mt19937 Rng(0xC0FFEEu);
  for (int Trial = 0; Trial < 300; ++Trial) {
    unsigned NP = 1 + (Rng() % 3);
    uint64_t CurSize[4] = {0, 0, 0, 0};
    uint32_t CurAlign[4] = {1, 1, 1, 1};
    std::vector<std::pair<std::string, uint64_t>> Expected;
    std::vector<uint64_t> ExpectedRelOffs;

    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      std::vector<SecSpec> Secs;
      std::vector<SymSpec> Syms;
      std::vector<RelSpec> Rels;
      unsigned NS = 1 + (Rng() % 4);
      for (unsigned s = 0; s < NS; ++s) {
        unsigned g = Rng() % 4;
        uint32_t Align = 1u << (Rng() % 7); // 1..64, power of two
        uint64_t Size = 1 + (Rng() % 0x200);
        std::string Nm = std::string(Groups[g].Name) + "." + std::to_string(p) +
                         "_" + std::to_string(s);
        unsigned SecIdx = Secs.size();
        // Distinct non-zero fill per content section so the verifier's
        // content anchor is meaningful (a mis-shifted symbol would read a
        // different section's fill byte).
        uint8_t Fill = Groups[g].Type == ELF::SHT_NOBITS
                           ? 0
                           : (uint8_t)(1 + ((p * 7 + s * 3 + g) & 0x7e));
        Secs.push_back(
            SecSpec{Nm, Size, Align, Groups[g].Type, Groups[g].Flags, Fill});

        // Oracle: mirror the merger's running-max-align + pad + append.
        if (Align > CurAlign[g])
          CurAlign[g] = Align;
        uint64_t Pad = (CurAlign[g] - (CurSize[g] % CurAlign[g])) % CurAlign[g];
        uint64_t Base = CurSize[g] + Pad;
        CurSize[g] = Base + Size;

        unsigned NSym = 1 + (Rng() % 3);
        for (unsigned k = 0; k < NSym; ++k) {
          uint64_t SOff = Rng() % Size;
          std::string SN = "s_" + std::to_string(p) + "_" + std::to_string(s) +
                           "_" + std::to_string(k);
          Syms.push_back(SymSpec{SN, (int)SecIdx, SOff, true});
          Expected.push_back({SN, Base + SOff});
        }

        // Optionally drop a relocation into this content-bearing section and
        // predict its merged offset, so reloc-offset collapse is caught too.
        if (Groups[g].Type != ELF::SHT_NOBITS && (Rng() % 2)) {
          uint64_t RO = Rng() % Size;
          std::string Anchor =
              "s_" + std::to_string(p) + "_" + std::to_string(s) + "_0";
          Rels.push_back(RelSpec{(int)SecIdx, RO, Anchor, ELF::R_X86_64_64, 0});
          ExpectedRelOffs.push_back(Base + RO);
        }
      }
      Bufs.push_back(buildSectionedELF(Secs, Syms, Rels));
    }

    Options Opts;
    Opts.mergeSections = true;
    auto [OK, Out] = mergeELF(Bufs, Opts);
    ASSERT_TRUE(OK) << "trial " << Trial;
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok) << "trial " << Trial;
    for (auto &E : Expected) {
      const ParsedSym *PS = V.findSym(E.first);
      ASSERT_NE(PS, nullptr) << "trial " << Trial << " sym " << E.first;
      EXPECT_EQ(PS->Value, E.second) << "trial " << Trial << " sym " << E.first;
    }

    // Every relocation's merged offset must equal its independently predicted
    // base+offset; the historical bug collapsed these to (near) zero.
    std::vector<uint64_t> ActualRelOffs;
    for (auto &R : V.Relas)
      ActualRelOffs.push_back(R.Offset);
    std::sort(ExpectedRelOffs.begin(), ExpectedRelOffs.end());
    std::sort(ActualRelOffs.begin(), ActualRelOffs.end());
    ASSERT_EQ(ActualRelOffs.size(), ExpectedRelOffs.size())
        << "trial " << Trial;
    for (size_t i = 0; i < ExpectedRelOffs.size(); ++i)
      EXPECT_EQ(ActualRelOffs[i], ExpectedRelOffs[i])
          << "trial " << Trial << " reloc " << i;

    // The independent verifier must accept every random valid layout (proves
    // it has no false positives across hundreds of shapes).
    std::string VErr;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::ELF64LE, Opts, &VErr))
        << "trial " << Trial << ": " << VErr;
  }
}

// ---------------------------------------------------------------------------
// Verifier tests — prove the independent post-merge self-check accepts a sound
// merge and *rejects* the exact corruption class that shipped before (symbol
// offset collapse), which the merger now refuses to emit.
// ---------------------------------------------------------------------------

TEST(MergeELFSemantic, MergeIsDeterministic) {
  using namespace ELF;
  // Reproducible builds and the per-partition object cache both require the
  // merge to be a pure function of its inputs: identical inputs must yield
  // byte-identical output every time (StringMap lookups must never leak their
  // hash-iteration order into section/symbol/string-table ordering).  Exercise
  // a non-trivial .text/.data/.bss + cross-partition relocated merge twice.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec D0{".data", 0x20, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xCC};
  SecSpec B0{".bss", 0x30, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SecSpec S1{".text", 0x30, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec G0{"g0", 1, 0, true};
  SymSpec V0{"v0", 2, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  SymSpec Ext{"ext", -1, 0, true};
  RelSpec R1{0, 0x10, "ext", R_X86_64_64, 7};
  auto O0 = buildSectionedELF({S0, D0, B0}, {F0, G0, V0}, {});
  auto O1 = buildSectionedELF({S1}, {F1, Ext}, {R1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK1, Out1] = mergeELF(Bufs);
  auto [OK2, Out2] = mergeELF(Bufs);
  ASSERT_TRUE(OK1);
  ASSERT_TRUE(OK2);
  ASSERT_EQ(Out1.size(), Out2.size());
  EXPECT_EQ(0, std::memcmp(Out1.data(), Out2.data(), Out1.size()))
      << "ELF merge is not deterministic — breaks reproducible builds and the "
         "per-partition object cache";
}

TEST(MergeELFVerify, AcceptsGoodMergeRejectsCollapse) {
  using namespace ELF;
  // Distinct fill bytes so a mis-placed symbol reads the wrong section's code.
  SecSpec S0{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec FA{"fa", 0, 0, true};
  SymSpec FB{"fb", 0, 0, true};
  auto O0 = buildSectionedELF({S0}, {FA}, {});
  auto O1 = buildSectionedELF({S1}, {FB}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must already pass
  ASSERT_TRUE(OK);

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Sanity: fb really landed past partition 0's .text.
  {
    ElfView V = parseELF(Out);
    const ParsedSym *PFB = V.findSym("fb");
    ASSERT_NE(PFB, nullptr);
    EXPECT_EQ(PFB->Value, 0x20u);
  }

  // Collapse fb to offset 0 (the historical bug): its content window now reads
  // partition 0's 0xAA bytes instead of its own 0xBB → must be rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "fb", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {},
                           &Err))
      << "verifier accepted a collapsed symbol offset";

  // Out-of-bounds value → the other rejection path.
  auto OOB = Out;
  ASSERT_TRUE(patchSymValue(OOB, "fb", 0x9999));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(OOB), Format::ELF64LE, {}, &Err))
      << "verifier accepted an out-of-bounds symbol value";
}

TEST(MergeELFVerify, AcceptsCoalescedWeakSymbolInMergedSection) {
  using namespace ELF;
  // CFI-enabled translation units each carry a weak __cfi_check definition in
  // the same .text section as ordinary functions. The weak symbol legitimately
  // resolves to one output definition; it therefore does not share the
  // per-input section shift of the ordinary function in later inputs.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SymSpec F0{"f0", 0, 0x10};
  SymSpec F1{"f1", 0, 0x10};
  SymSpec Cfi0{"__cfi_check", 0, 0, /*Global=*/true, /*Weak=*/true};
  SymSpec Cfi1{"__cfi_check", 0, 0, /*Global=*/true, /*Weak=*/true};

  auto O0 = buildSectionedELF({S0}, {F0, Cfi0}, {});
  auto O1 = buildSectionedELF({S1}, {F1, Cfi1}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(O0));
  Bufs.push_back(std::move(O1));

  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  auto Corrupt = Out;
  ASSERT_TRUE(corruptSymbolContentByte(Corrupt, "__cfi_check"));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Corrupt), Format::ELF64LE, {}, &Err))
      << "verifier accepted a corrupted surviving weak definition";
}

TEST(MergeELFVerify, CatchesCollapsedDuplicateNamedSymbol) {
  using namespace ELF;
  // Two partitions each define a *file-local* symbol with the SAME name "dup"
  // (the legitimate two-statics-share-a-name case).  In the merged symtab the
  // name is therefore ambiguous, which the verifier's unique-name anchor skips
  // wholesale — historically a blind spot where an offset collapse of exactly
  // these symbols would sail through.  The duplicate-name content anchor closes
  // it: a correct merge places each "dup" at a same-named output symbol whose
  // bytes match, so collapsing them all to 0 (the faithful shape of the bug)
  // must be rejected because partition 1's "dup" no longer matches any 0xBB
  // window.  Distinct fills make a mis-placed symbol read the wrong code.
  SecSpec S0{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec D0{"dup", 0, 0, /*Global=*/false};
  SymSpec D1{"dup", 0, 0, /*Global=*/false};
  auto O0 = buildSectionedELF({S0}, {D0}, {});
  auto O1 = buildSectionedELF({S1}, {D1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must accept the good merge
  ASSERT_TRUE(OK);

  // Sanity: the output really does carry two same-named "dup" symbols, so this
  // exercises the ambiguous (not the unique) verify path.
  {
    ElfView V = parseELF(Out);
    unsigned NDup = 0;
    for (const auto &S : V.Syms)
      if (S.Name == "dup")
        ++NDup;
    EXPECT_EQ(NDup, 2u) << "test setup expected two duplicate-named symbols";
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse every "dup" to offset 0 (the historical bug).  Partition 1's dup
  // then reads partition 0's 0xAA bytes instead of its own 0xBB → rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchAllSymValues(Collapsed, "dup", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {},
                           &Err))
      << "verifier accepted a collapsed duplicate-named symbol offset";
}

TEST(MergeELFVerify, CatchesCollapsedDuplicateNamedSymbolMergeSections) {
  using namespace ELF;
  // The exact Android-kernel-module shape that the historical bug crashed: two
  // translation units each define a file-local helper of the *same* name in its
  // own per-function section (.text.dup), and mergeSections folds both into one
  // .text.  The merged name is therefore ambiguous AND re-homed across the
  // section rename — the precise intersection of the two features the bug lived
  // in.  A correct merge places each "dup" over its own bytes; collapsing them
  // all to 0 must be rejected because partition 1's "dup" then reads 0xAA.
  SecSpec T0{".text.dup", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xAA};
  SecSpec T1{".text.dup", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xBB};
  SymSpec D0{"dup", 0, 0, /*Global=*/false};
  SymSpec D1{"dup", 0, 0, /*Global=*/false};
  auto O0 = buildSectionedELF({T0}, {D0}, {});
  auto O1 = buildSectionedELF({T1}, {D1}, {});

  Options Opts;
  Opts.mergeSections = true;

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK); // internal verify (with mergeSections) must accept

  // Both "dup" folded into one .text and the per-function sections are gone.
  {
    ElfView V = parseELF(Out);
    EXPECT_GE(V.findSec(".text"), 0);
    EXPECT_LT(V.findSec(".text.dup"), 0);
    unsigned NDup = 0;
    for (const auto &S : V.Syms)
      if (S.Name == "dup")
        ++NDup;
    EXPECT_EQ(NDup, 2u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchAllSymValues(Collapsed, "dup", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, Opts,
                           &Err))
      << "verifier accepted a collapsed duplicate-named symbol under "
         "mergeSections (the kernel-module configuration)";
}

TEST(MergeELFVerify, CatchesCollapsedBssSymbolOffset) {
  using namespace ELF;
  // The .bss twin of the historical .text collapse.  Two uninitialized globals
  // share one input .bss at distinct offsets.  A NOBITS section has no bytes,
  // so the verifier's byte-content anchor *must* skip it — the same-section
  // relative-distance invariant is the only check that can see a collapse here.
  // Before that invariant existed, collapsing bss_b onto bss_a silently passed
  // verification (st_value is what the final linker resolves the symbol to, so
  // the merged module would access the wrong .bss slot at run time).
  SecSpec B{".bss", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec VA{"bss_a", 0, 0x0, /*Global=*/true};
  SymSpec VB{"bss_b", 0, 0x20, /*Global=*/true};
  auto Obj = buildSectionedELF({B}, {VA, VB}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must accept the good merge
  ASSERT_TRUE(OK);

  // Sanity: both globals kept their distinct .bss offsets.
  {
    ElfView V = parseELF(Out);
    const ParsedSym *PA = V.findSym("bss_a");
    const ParsedSym *PB = V.findSym("bss_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PA->Value, 0x0u);
    EXPECT_EQ(PB->Value, 0x20u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse bss_b onto bss_a's offset.  No bytes exist to compare, so only the
  // relative-distance invariant can reject this — and it must.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "bss_b", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {},
                           &Err))
      << "verifier accepted a collapsed .bss symbol offset (NOBITS blind spot)";
}

TEST(MergeELFVerify, CatchesCollapsedSingletonBssDistinctSections) {
  using namespace ELF;
  // The ordinary multi-file .bss blind spot: two translation units each define
  // exactly *one* uninitialized global, each in its own input .bss.  A lone
  // symbol per input section gives the same-input-section relative-distance
  // invariant no sibling to compare, and NOBITS denies the byte-content anchor
  // — so before the disjoint-range invariant existed, collapsing g1 onto g0's
  // slot (the historical bug's shape) passed verification on perfectly ordinary
  // code, not just kernel modules.  The disjoint-range invariant reconstructs
  // each input .bss's merged base from its single symbol and forbids the
  // overlap.
  SecSpec B0{".bss", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SecSpec B1{".bss", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec V0{"g0", 0, 0x0, /*Global=*/true};
  SymSpec V1{"g1", 0, 0x0, /*Global=*/true};
  auto O0 = buildSectionedELF({B0}, {V0}, {});
  auto O1 = buildSectionedELF({B1}, {V1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must accept the good merge
  ASSERT_TRUE(OK);

  // p0's .bss lands at 0, p1's after it at 0x40 — distinct, disjoint ranges.
  {
    ElfView V = parseELF(Out);
    const ParsedSym *P0 = V.findSym("g0");
    const ParsedSym *P1 = V.findSym("g1");
    ASSERT_NE(P0, nullptr);
    ASSERT_NE(P1, nullptr);
    EXPECT_EQ(P0->Value, 0x0u);
    EXPECT_EQ(P1->Value, 0x40u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse p1's g1 onto p0's slot: its input .bss range [0,0x40) now overlaps
  // p0's [0,0x40) → must be rejected even though no bytes exist to compare.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "g1", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {},
                           &Err))
      << "verifier accepted a collapsed singleton .bss symbol across "
         "partitions "
         "(the ordinary multi-file NOBITS blind spot)";
}

TEST(MergeELFVerify, CatchesCollapsedSingletonBssMergeSections) {
  using namespace ELF;
  // The kernel-module (.ko) shape that scared us: -fdata-sections puts each
  // global in its own .bss.<name>, and mergeSections folds them all into one
  // .bss.  Each input .bss.<name> holds exactly one symbol, so this is the
  // singleton case the relative-distance invariant cannot see; NOBITS denies
  // the content anchor.  The disjoint-range invariant is the only line of
  // defense — exactly the gap the historical offset-collapse bug would have
  // hidden in on a real .ko, where there is no execution fallback to catch it
  // later.
  SecSpec BA{".bss.a", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SecSpec BB{".bss.b", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec VA{"var_a", 0, 0x0, /*Global=*/true};
  SymSpec VB{"var_b", 1, 0x0, /*Global=*/true};
  auto Obj = buildSectionedELF({BA, BB}, {VA, VB}, {});

  Options Opts;
  Opts.mergeSections = true;

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  auto [OK, Out] =
      mergeELF(Bufs, Opts); // internal verify (mergeSections) accepts
  ASSERT_TRUE(OK);

  // Both .bss.* folded into one .bss; var_a at 0, var_b after it.
  {
    ElfView V = parseELF(Out);
    EXPECT_GE(V.findSec(".bss"), 0);
    EXPECT_LT(V.findSec(".bss.a"), 0);
    const ParsedSym *PA = V.findSym("var_a");
    const ParsedSym *PB = V.findSym("var_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PA->Value, 0x0u);
    EXPECT_EQ(PB->Value, 0x40u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  // Collapse var_b onto var_a's slot.  .bss.b's reconstructed range now
  // overlaps .bss.a's → rejected, closing the NOBITS singleton blind spot on
  // the .ko path.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "var_b", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, Opts,
                           &Err))
      << "verifier accepted a collapsed singleton .bss under mergeSections "
         "(the kernel-module NOBITS blind spot)";
}

TEST(MergeELFVerify, CatchesCollapsedRelocOffset) {
  using namespace ELF;
  // Each partition defines a function spanning its .text and relocates against
  // an undefined "ext" at offset 0x10.  After merge the relocations sit at
  // 0x10 (p0) and 0x50 (p1); collapsing them to 0 must be caught even though
  // the symbols themselves are still correct.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  SymSpec Ext{"ext", -1, 0, true};
  RelSpec R0{0, 0x10, "ext", R_X86_64_PLT32, 0};
  RelSpec R1{0, 0x10, "ext", R_X86_64_PLT32, 0};
  auto O0 = buildSectionedELF({S0}, {F0, Ext}, {R0});
  auto O1 = buildSectionedELF({S1}, {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted collapsed relocation offsets";
}

TEST(MergeELFVerify, CatchesCorruptedRelocAddend) {
  using namespace ELF;
  // A relocation against a *named* symbol carries an explicit addend the -r
  // merge must copy verbatim (only the site offset shifts past earlier .text).
  // The offset anchor already proves the reloc re-lands in the right slot; this
  // proves its addend survived too.  A corrupted addend resolves to the wrong
  // target address even when the site offset is perfect — a "loads fine, reads
  // the wrong place" miscompile the offset check alone cannot catch.  The two
  // partitions use *distinct* non-zero addends so collapsing both to one value
  // cannot accidentally still match.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec F0{"f0", 0, 0, true};    // anchors p0's .text at offset 0
  SymSpec F1{"f1", 0, 0, true};    // anchors p1's .text (shifts to 0x40)
  SymSpec Ext{"ext", -1, 0, true}; // shared undefined named target
  RelSpec R0{0, 0x10, "ext", R_X86_64_64, 0x1234};
  RelSpec R1{0, 0x10, "ext", R_X86_64_64, 0x5678};
  auto O0 = buildSectionedELF({S0}, {F0, Ext}, {R0});
  auto O1 = buildSectionedELF({S1}, {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllRelaAddends(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted a corrupted relocation addend";
}

TEST(MergeELFVerify, CatchesCollapsedSectionRelativeReloc) {
  using namespace ELF;
  // Section-relative relocation: each partition's .text references ".rodata +
  // addend" via an STT_SECTION target (no symbol name) — the class the verifier
  // used to skip entirely.  f0/f1 anchor each .text so the merged site offset
  // is predictable; after merge they sit at 0x10 (p0) and 0x50 (p1).  A
  // collapse of these offsets must now be rejected too.
  SecSpec T0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec D0{".rodata", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xCC};
  SecSpec T1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SecSpec D1{".rodata", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xDD};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  // Applies in .text (sec 0), targets the STT_SECTION symbol of .rodata (sec
  // 1).
  RelSpec R0{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  RelSpec R1{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  auto O0 = buildSectionedELF({T0, D0}, {F0}, {R0});
  auto O1 = buildSectionedELF({T1, D1}, {F1}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted a collapsed section-relative relocation offset";
}

TEST(MergeELFVerify, SectionRelativeRelocWithMergeSections) {
  using namespace ELF;
  // The Android-kernel -r path (mergeSections=true): per-function/per-object
  // sections (.text.fN, .rodata.rN) fold into .text/.rodata, and section-
  // relative relocations must re-anchor across the rename without the verifier
  // raising a false positive (which would hard-fail the .ko link).  A real
  // collapse must still be caught.
  SecSpec T0{".text.f0", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xAA};
  SecSpec D0{".rodata.r0", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xCC};
  SecSpec T1{".text.f1", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xBB};
  SecSpec D1{".rodata.r1", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xDD};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  RelSpec R0{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  RelSpec R1{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  auto O0 = buildSectionedELF({T0, D0}, {F0}, {R0});
  auto O1 = buildSectionedELF({T1, D1}, {F1}, {R1});

  Options Opts;
  Opts.mergeSections = true;

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK); // internal verify (with mergeSections) must accept

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << "verifier accepted a collapsed section-relative reloc under "
         "mergeSections";
}

TEST(MergeELFVerify, RandomizedCollapseAlwaysRejected) {
  using namespace ELF;
  // No-false-*negatives* property test, the mirror of RandomizedLayoutOracle's
  // no-false-*positives* sweep.  Each partition contributes a .text whose
  // single function symbol is pinned at offset 0 (so every relocation is
  // anchorable — exactly the shape real codegen emits, where each reloc site
  // lives inside a function that has a symbol at its entry) and one relocation
  // at a non-zero offset against a shared undefined extern.  Merging >=2 such
  // partitions shifts later relocs past earlier .text, so collapsing every
  // reloc offset to 0 — the precise shape of the shipped bug — must ALWAYS be
  // rejected by the independent verifier, for arbitrary random sizes/offsets,
  // not just the hand-built cases above.
  std::mt19937 Rng(0x5EED1234u);
  for (int Trial = 0; Trial < 200; ++Trial) {
    unsigned NP = 2 + (Rng() % 3); // >=2 so merged offsets actually shift
    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      uint64_t Size = 0x40 + (Rng() % 0x80);
      uint8_t Fill = (uint8_t)(0x11 + p); // distinct, non-zero per partition
      SecSpec T{".text", Size, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                Fill};
      SymSpec F{"fn_" + std::to_string(p), 0, 0, true}; // pinned at offset 0
      SymSpec E{"ext", 0, 0, false};                    // shared undefined ref
      uint64_t RO = 1 + (Rng() % (Size - 1));           // strictly inside, > 0
      RelSpec R{0, RO, "ext", R_X86_64_PC32, 0};
      Bufs.push_back(buildSectionedELF({T}, {F, E}, {R}));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "trial " << Trial;
    std::string VErr;
    ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::ELF64LE, {}, &VErr))
        << "trial " << Trial << ": good merge unexpectedly rejected: " << VErr;
    ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0)) << "trial " << Trial;
    std::string CErr;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                             ArrayRef<char>(Out), Format::ELF64LE, {}, &CErr))
        << "trial " << Trial << ": verifier accepted collapsed reloc offsets";
  }
}

TEST(MergeELFVerify, RandomizedAddendCorruptionAlwaysRejected) {
  using namespace ELF;
  // Property mirror of RandomizedCollapseAlwaysRejected for the *addend* half
  // of a relocation.  Each partition pins one function at offset 0 (so its
  // reloc is anchorable) and emits a relocation against a shared named symbol
  // with a distinct, non-zero addend.  A faithful -r merge copies every addend
  // verbatim, so the good merge must verify; collapsing all addends to 0 must
  // then ALWAYS be rejected (every partition's non-zero addend now mismatches),
  // for arbitrary random addends/offsets — not just the hand-built case above.
  std::mt19937 Rng(0xADDE6D00u);
  for (int Trial = 0; Trial < 200; ++Trial) {
    unsigned NP = 2 + (Rng() % 3); // >=2 so merged offsets actually shift
    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      uint64_t Size = 0x40 + (Rng() % 0x80);
      uint8_t Fill = (uint8_t)(0x11 + p);
      SecSpec T{".text", Size, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                Fill};
      SymSpec F{"fn_" + std::to_string(p), 0, 0, true}; // pinned at offset 0
      SymSpec E{"ext", -1, 0, true};                    // shared named target
      uint64_t RO = 1 + (Rng() % (Size - 8));           // 8-byte slot, inside
      int64_t Add = (int64_t)(1 + (Rng() % 0x7FFF));    // distinct, non-zero
      RelSpec R{0, RO, "ext", R_X86_64_64, Add};
      Bufs.push_back(buildSectionedELF({T}, {F, E}, {R}));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "trial " << Trial;
    std::string VErr;
    ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::ELF64LE, {}, &VErr))
        << "trial " << Trial << ": good merge unexpectedly rejected: " << VErr;
    ASSERT_TRUE(patchAllRelaAddends(Out, 0)) << "trial " << Trial;
    std::string CErr;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                             ArrayRef<char>(Out), Format::ELF64LE, {}, &CErr))
        << "trial " << Trial << ": verifier accepted corrupted reloc addends";
  }
}

// ---------------------------------------------------------------------------
// Fail-loud guards: the merger must REFUSE inputs whose features it does not
// fully model, rather than silently dropping them and shipping a wrong object.
// On the kernel/-r path a refusal becomes a loud `error("relocatable merge
// failed")`; on the parallel-codegen path it falls back to serial codegen.
// Either way the device never sees a miscompile.
// ---------------------------------------------------------------------------

TEST(MergeELFLinkOrder, MergedWithRemappedShLink) {
  using namespace ELF;
  // __patchable_function_entries (emitted by -fpatchable-function-entry for
  // ftrace) is SHF_LINK_ORDER with sh_link → its code section.  Two partitions
  // each contribute a .text plus a PFE pointing at their own .text; after -r
  // merge both .text fold into one and both PFE fold into one whose sh_link
  // must be remapped to the merged .text.  (This is the ftrace .ko path the
  // merger used to refuse outright, forcing a serial-codegen fallback.)
  SecSpec Text0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                0xAA};
  SecSpec Pfe0{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER,     0xBB, /*Link=*/0};
  SymSpec F{"f", 0, 0, true};
  auto O0 = buildSectionedELF({Text0, Pfe0}, {F}, {});
  SecSpec Text1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                0xCC};
  SecSpec Pfe1{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER,     0xDD, /*Link=*/0};
  SymSpec G{"g", 0, 0, true};
  auto O1 = buildSectionedELF({Text1, Pfe1}, {G}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK) << "merger refused a well-formed SHF_LINK_ORDER merge";

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int PfeIdx = V.findSec("__patchable_function_entries");
  int TextIdx = V.findSec(".text");
  ASSERT_GE(PfeIdx, 0) << "merged PFE section missing";
  ASSERT_GE(TextIdx, 0) << "merged .text section missing";
  const ParsedSec &Pfe = V.Secs[PfeIdx];
  EXPECT_TRUE(Pfe.Flags & SHF_LINK_ORDER);
  EXPECT_EQ(Pfe.Link, (uint32_t)TextIdx)
      << "merged SHF_LINK_ORDER sh_link not remapped to the merged .text";
  EXPECT_EQ(Pfe.Size, 0x20u) << "both PFE contributions must be concatenated";

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;
}

TEST(MergeELFLinkOrder, MergeSectionsFoldsTargetsConsistently) {
  using namespace ELF;
  // The kernel-module path (mergeSections=true) folds per-function
  // .text.foo/.text.bar into one .text, so both PFE link targets canonicalize
  // to ".text" — consistent — and the merged PFE sh_link points at the single
  // merged .text.
  SecSpec TextFoo{
      ".text.foo", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec Pfe0{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER,     0xBB, /*Link=*/0};
  SymSpec F{"f", 0, 0, true};
  auto O0 = buildSectionedELF({TextFoo, Pfe0}, {F}, {});
  SecSpec TextBar{
      ".text.bar", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SecSpec Pfe1{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER,     0xDD, /*Link=*/0};
  SymSpec G{"g", 0, 0, true};
  auto O1 = buildSectionedELF({TextBar, Pfe1}, {G}, {});

  Options Opts;
  Opts.mergeSections = true;
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK) << "merger refused a consistent mergeSections PFE merge";

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int PfeIdx = V.findSec("__patchable_function_entries");
  int TextIdx = V.findSec(".text");
  ASSERT_GE(PfeIdx, 0);
  ASSERT_GE(TextIdx, 0);
  EXPECT_EQ(V.Secs[PfeIdx].Link, (uint32_t)TextIdx);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;
}

TEST(MergeELFLinkOrder, InconsistentTargetsRefused) {
  using namespace ELF;
  // Without mergeSections, per-function .text.foo/.text.bar are NOT folded, so
  // two PFE inputs pointing at different code sections would need one output
  // sh_link to name two targets — impossible.  The merger must refuse rather
  // than silently pick one (which would drop the other's ordering dependency).
  SecSpec TextFoo{
      ".text.foo", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec Pfe0{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER,     0xBB, /*Link=*/0};
  SymSpec F{"f", 0, 0, true};
  auto O0 = buildSectionedELF({TextFoo, Pfe0}, {F}, {});
  SecSpec TextBar{
      ".text.bar", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SecSpec Pfe1{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER,     0xDD, /*Link=*/0};
  SymSpec G{"g", 0, 0, true};
  auto O1 = buildSectionedELF({TextBar, Pfe1}, {G}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // default: mergeSections=false
  EXPECT_FALSE(OK)
      << "merger accepted SHF_LINK_ORDER inputs with inconsistent link targets";
}

TEST(MergeELFLinkOrder, VerifyCatchesCollapsedPfeRelocOffset) {
  using namespace ELF;
  // PFE relocations have no defined symbol to anchor on, so the symbol-anchored
  // reloc check skips them; the SHF_LINK_ORDER conservation check (distinct
  // offsets) is their guard.  Merge two partitions whose PFE carries
  // relocations into their own functions, then collapse every reloc offset to 0
  // (the PFE half of the historical offset-collapse bug) and confirm the
  // independent verifier rejects it.
  SecSpec T0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec P0{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
             SHF_ALLOC | SHF_LINK_ORDER,     0,    /*Link=*/0};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec G0{"g0", 0, 0x20, true};
  RelSpec R0a{1, 0, "f0", R_X86_64_64, 0};
  RelSpec R0b{1, 8, "g0", R_X86_64_64, 0};
  auto O0 = buildSectionedELF({T0, P0}, {F0, G0}, {R0a, R0b});

  SecSpec T1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SecSpec P1{"__patchable_function_entries", 0x10, 8,         SHT_PROGBITS,
             SHF_ALLOC | SHF_LINK_ORDER,     0,    /*Link=*/0};
  SymSpec F1{"f1", 0, 0, true};
  SymSpec G1{"g1", 0, 0x20, true};
  RelSpec R1a{1, 0, "f1", R_X86_64_64, 0};
  RelSpec R1b{1, 8, "g1", R_X86_64_64, 0};
  auto O1 = buildSectionedELF({T1, P1}, {F1, G1}, {R1a, R1b});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK) << "merger refused a well-formed PFE-with-relocs merge";
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse every relocation offset to 0: the merged PFE now has 4 relocations
  // aliased onto one offset — the exact shape the conservation check guards.
  ASSERT_TRUE(patchAllRelaOffsets(Out, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted collapsed PFE relocation offsets";
}

TEST(MergeELFVerify, CatchesCorruptedSymtabShInfo) {
  using namespace ELF;
  // The __pcg demotion reorders the symbol table; a bug there would mis-set
  // sh_info (the local/global boundary) and silently corrupt how every binding
  // is read.  The structural check rejects any output whose locals are not all
  // before sh_info — independent of the merger's own bookkeeping.
  SecSpec T0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec T1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SymSpec La{"la", 0, 0, /*Global=*/false};
  SymSpec Ga{"ga", 0, 0, /*Global=*/true};
  SymSpec Lb{"lb", 0, 0, /*Global=*/false};
  SymSpec Gb{"gb", 0, 0, /*Global=*/true};
  auto O0 = buildSectionedELF({T0}, {La, Ga}, {});
  auto O1 = buildSectionedELF({T1}, {Lb, Gb}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // sh_info=1 places the real local defs (la/lb, at indices >= 1) after the
  // claimed boundary — exactly the binding-order corruption to catch.
  ASSERT_TRUE(patchElfSymtabShInfo(Out, 1));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted a corrupted symtab sh_info (local/global boundary)";
}

TEST(MergeELFVerify, CatchesLinkOrderSectionWithZeroShLink) {
  using namespace ELF;
  // verifyMerge also audits objects produced by other linkers (the differential
  // suite feeds it real LLD -r output).  A SHF_LINK_ORDER section is correct
  // only with a real sh_link; the sh_link=0 shape (which the merger's own
  // sh_link remap never produces) must be rejected by the independent audit
  // too, so a wrong object can never slip through whatever produced it.
  SecSpec Text{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               0xAA};
  SecSpec Pfe{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
              SHF_ALLOC | SHF_LINK_ORDER,     0xBB};
  SymSpec F{"f", 0, 0, true};
  // buildSectionedELF leaves content-section sh_link at 0.
  auto Obj = buildSectionedELF({Text, Pfe}, {F}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  std::string Err;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Obj), Format::ELF64LE, {}, &Err))
      << "verifier accepted a SHF_LINK_ORDER section with sh_link=0";
}

// ---------------------------------------------------------------------------
// COFF semantic tests — same invariant on the Windows object path.
// ---------------------------------------------------------------------------

// P0 arch-consistency guard: two ELF64LE objects of different e_machine both
// parse cleanly as ELF64LE but describe incompatible code.  The merger must
// refuse rather than emit one header (the first input's e_machine) over a
// cross-ISA body — a silent miscompile no content/offset anchor can see.  A
// real `ld -r` refuses this too, so refusing never regresses a legitimate link.
TEST(MergeELF, RefusesMixedEMachine) {
  using namespace ELF;
  auto A = buildMinimalELF({"a"}, {});
  auto B = buildMinimalELF({"b"}, {});

  // Positive control: same machine still merges (guards against a false-reject
  // regression that would silently disable parallel codegen).
  {
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(A);
    Bufs.push_back(B);
    EXPECT_TRUE(mergeELF(Bufs).first);
  }

  // Flip the second object's e_machine to AArch64 -> mixed arch -> must refuse.
  ASSERT_GE(B.size(), sizeof(Elf64_Ehdr));
  reinterpret_cast<Elf64_Ehdr *>(B.data())->e_machine = EM_AARCH64;
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(A));
  Bufs.push_back(std::move(B));
  EXPECT_FALSE(mergeELF(Bufs).first)
      << "merger accepted mixed e_machine inputs";
}

// P0 arch-consistency guard, verifier leg: even if a future merger bug wrote a
// wrong output header, the independent verifier must catch an output e_machine
// that disagrees with the inputs (every section byte still anchors to its
// input; only the architecture lies).
TEST(MergeELFVerify, CatchesMismatchedOutputMachine) {
  using namespace ELF;
  SecSpec S0{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec FA{"fa", 0, 0, true};
  SymSpec FB{"fb", 0, 0, true};
  auto O0 = buildSectionedELF({S0}, {FA}, {});
  auto O1 = buildSectionedELF({S1}, {FB}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  auto Bad = Out;
  ASSERT_GE(Bad.size(), sizeof(Elf64_Ehdr));
  reinterpret_cast<Elf64_Ehdr *>(Bad.data())->e_machine = EM_AARCH64;
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Bad), Format::ELF64LE, {}, &Err))
      << "verifier accepted an output with a mismatched e_machine";
}

// P1: two strong (STB_GLOBAL, defined) definitions of one symbol is an ODR
// violation the ELF dedup would silently resolve by dropping one.  Refuse it
// (a real `ld -r` also rejects/defers; the parallel-codegen path never makes
// dup strong defs, so this never false-rejects that path).
TEST(MergeELF, RefusesMultipleStrongDefinitions) {
  auto A = buildMinimalELF({"dup"}, {}, {0xcc}, /*DefinedAsGlobal=*/true);
  auto B = buildMinimalELF({"dup"}, {}, {0xdd}, /*DefinedAsGlobal=*/true);
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(A));
  Bufs.push_back(std::move(B));
  EXPECT_FALSE(mergeELF(Bufs).first)
      << "merger accepted two strong definitions of the same symbol";

  // Control: distinct strong globals merge fine (no false-reject regression).
  auto C = buildMinimalELF({"g0"}, {}, {0xcc}, /*DefinedAsGlobal=*/true);
  auto D = buildMinimalELF({"g1"}, {}, {0xdd}, /*DefinedAsGlobal=*/true);
  SmallVector<SmallVector<char, 0>, 2> Bufs2;
  Bufs2.push_back(std::move(C));
  Bufs2.push_back(std::move(D));
  EXPECT_TRUE(mergeELF(Bufs2).first);
}

TEST(MergeCOFFSemantic, CrossPartitionSymbolAndRelocOffsets) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x40, TextChars};
  CoffSecSpec S1{".text", 0x20, TextChars};
  CoffSymSpec P0{"p0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec P1{"p1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Ext{"ext", 0, 0, IMAGE_SYM_CLASS_EXTERNAL}; // undefined external
  CoffRelSpec R1{0, 0, "ext", (uint16_t)IMAGE_REL_AMD64_REL32};

  auto Obj0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {P0}, {});
  auto Obj1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {P1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));

  CoffView V = parseCOFF(Out);
  ASSERT_TRUE(V.Ok);

  const CoffParsedSym *PP0 = V.findSym("p0");
  const CoffParsedSym *PP1 = V.findSym("p1");
  ASSERT_NE(PP0, nullptr);
  ASSERT_NE(PP1, nullptr);
  EXPECT_EQ(PP0->Value, 0x0u);
  EXPECT_EQ(PP1->Value, 0x40u); // shifted past partition 0's .text

  ASSERT_EQ(V.Rels.size(), 1u);
  EXPECT_EQ(V.Rels[0].VA, 0x40u); // relocation site moved too
  ASSERT_LT(V.Rels[0].SymIdx, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Rels[0].SymIdx].Name, std::string("ext"));
}

TEST(MergeCOFFSemantic, BssSectionsMergeByVirtualSize) {
  using namespace COFF;
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec B0{".bss", 0x30, BssChars};
  CoffSecSpec B1{".bss", 0x10, BssChars};
  CoffSymSpec V0{"v0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec V1{"v1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};

  auto Obj0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B0}, {V0}, {});
  auto Obj1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B1}, {V1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));

  CoffView V = parseCOFF(Out);
  ASSERT_TRUE(V.Ok);
  const CoffParsedSym *PV1 = V.findSym("v1");
  ASSERT_NE(PV1, nullptr);
  EXPECT_EQ(PV1->Value, 0x30u); // shifted past partition 0's .bss
}

TEST(MergeCOFFSemantic, RandomizedLayoutOracle) {
  // COFF analogue of MergeELFSemantic.RandomizedLayoutOracle: random
  // multi-partition layouts with an independent concatenate-and-align oracle
  // predicting every symbol's merged Value.  COFF folds sections by
  // (name, content-class) and tracks a running max alignment, so the oracle
  // mirrors exactly that.  Any divergence in the merger's offset math for any
  // shape — not just the hand-picked ones above — fails here.
  using namespace COFF;
  struct Group {
    const char *Name;
    uint32_t Chars;
    bool IsBSS;
  };
  const Group Groups[4] = {
      {".text", IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ,
       false},
      {".data",
       IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
           IMAGE_SCN_MEM_WRITE,
       false},
      {".rdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ, false},
      {".bss",
       IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
           IMAGE_SCN_MEM_WRITE,
       true},
  };

  std::mt19937 Rng(0xB0BACAFEu);
  for (int Trial = 0; Trial < 300; ++Trial) {
    unsigned NP = 1 + (Rng() % 3);
    uint64_t CurSize[4] = {0, 0, 0, 0};
    uint32_t CurAlign[4] = {1, 1, 1, 1};
    std::vector<std::pair<std::string, uint64_t>> Expected;

    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      std::vector<CoffSecSpec> Secs;
      std::vector<CoffSymSpec> Syms;
      for (unsigned g = 0; g < 4; ++g) {
        if (Rng() % 3 == 0)
          continue;                    // a partition may lack a group
        unsigned AlignExp = Rng() % 7; // 1..64
        uint32_t Align = 1u << AlignExp;
        uint64_t Size = 1 + (Rng() % 0x200);
        uint32_t Chars = Groups[g].Chars | ((AlignExp + 1) << 20);
        unsigned SecIdx = Secs.size();
        uint8_t Fill =
            Groups[g].IsBSS ? 0 : (uint8_t)(1 + ((p * 7 + g * 3) & 0x7e));
        Secs.push_back(
            CoffSecSpec{Groups[g].Name, (uint32_t)Size, Chars, Fill});

        if (Align > CurAlign[g])
          CurAlign[g] = Align;
        uint64_t Pad = (CurAlign[g] - (CurSize[g] % CurAlign[g])) % CurAlign[g];
        uint64_t Base = CurSize[g] + Pad;
        CurSize[g] = Base + Size;

        unsigned NSym = 1 + (Rng() % 3);
        for (unsigned k = 0; k < NSym; ++k) {
          uint64_t SOff = Rng() % Size;
          std::string SN = "s_" + std::to_string(p) + "_" + std::to_string(g) +
                           "_" + std::to_string(k);
          Syms.push_back(CoffSymSpec{SN, (uint32_t)SOff, (int16_t)(1 + SecIdx),
                                     IMAGE_SYM_CLASS_EXTERNAL});
          Expected.push_back({SN, Base + SOff});
        }
      }
      Bufs.push_back(buildCOFF(IMAGE_FILE_MACHINE_AMD64, Secs, Syms, {}));
    }

    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)) << "trial " << Trial;
    CoffView V = parseCOFF(Out);
    ASSERT_TRUE(V.Ok) << "trial " << Trial;
    for (auto &E : Expected) {
      const CoffParsedSym *PS = V.findSym(E.first);
      ASSERT_NE(PS, nullptr) << "trial " << Trial << " sym " << E.first;
      EXPECT_EQ(PS->Value, E.second) << "trial " << Trial << " sym " << E.first;
    }
    std::string VErr;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::COFF, {}, &VErr))
        << "trial " << Trial << ": " << VErr;
  }
}

TEST(MergeCOFFSemantic, MergeIsDeterministic) {
  using namespace COFF;
  // Identical inputs must produce byte-identical output (see the ELF analogue);
  // the merger zeroes TimeDateStamp so only ordering bugs could break this.
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  uint32_t DataChars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                       IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_8BYTES;
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec D0{".data", 0x20, DataChars, 0xCC};
  CoffSecSpec B0{".bss", 0x30, BssChars};
  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec F0{"f0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec G0{"g0", 0, 2, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec V0{"v0", 0, 3, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec F1{"f1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0, D0, B0}, {F0, G0, V0}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {F1}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out1, Out2;
  {
    raw_svector_ostream OS(Out1);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }
  {
    raw_svector_ostream OS(Out2);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }
  ASSERT_EQ(Out1.size(), Out2.size());
  EXPECT_EQ(0, std::memcmp(Out1.data(), Out2.data(), Out1.size()))
      << "COFF merge is not deterministic";
}

TEST(MergeCOFFSemantic, WeakExternalAuxTagIndexRemapped) {
  using namespace COFF;
  // A COFF weak external symbol carries a coff_aux_weak_external whose TagIndex
  // is a *symbol-table index* naming the default definition.  The merge appends
  // every partition's symbols, shifting all indices, so a copied-verbatim
  // TagIndex aliases the weak symbol onto an unrelated definition — a silent
  // miscompile the content/offset anchors cannot see (aux records carry no
  // section bytes).  Each partition defines its own default and a weak external
  // pointing at it; after merge each weak aux must still name *its* default.
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars, 0xAA};
  CoffSymSpec Def0{"wdef0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak0{"wfn0", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak0.WeakDefTag = 0; // -> Def0 (index 0 within partition 0's specs)
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {Def0, Weak0}, {});

  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec Def1{"wdef1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak1{"wfn1", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak1.WeakDefTag = 0; // -> Def1 (index 0 within partition 1's specs)
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {Def1, Weak1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  {
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }

  // Walk the merged symbol table; each weak external's aux TagIndex must name
  // the matching default definition by content, not a stale partition-local
  // index.
  ASSERT_GE(Out.size(), 20u);
  uint32_t SymPtr = getU32(Out.data() + 8);
  uint32_t NSym = getU32(Out.data() + 12);
  ASSERT_LE(SymPtr + (uint64_t)NSym * 18, Out.size());
  uint64_t StrBase = (uint64_t)SymPtr + (uint64_t)NSym * 18;
  auto slotName = [&](uint32_t Slot) -> std::string {
    const char *P = Out.data() + SymPtr + (uint64_t)Slot * 18;
    if (getU32(P) == 0) { // long-name escape: 4 zero bytes, then strtab offset
      uint32_t O = getU32(P + 4);
      if (StrBase + O >= Out.size())
        return {};
      const char *S = Out.data() + StrBase + O;
      return std::string(S, strnlen(S, Out.size() - (StrBase + O)));
    }
    return std::string(P, strnlen(P, 8));
  };
  bool SawW0 = false, SawW1 = false;
  for (uint32_t k = 0; k < NSym;) {
    const char *P = Out.data() + SymPtr + (uint64_t)k * 18;
    std::string Nm = slotName(k);
    uint8_t Storage = (uint8_t)P[16];
    uint8_t NAux = (uint8_t)P[17];
    if (Storage == IMAGE_SYM_CLASS_WEAK_EXTERNAL && NAux >= 1 && k + 1 < NSym) {
      uint32_t Tag = getU32(Out.data() + SymPtr + (uint64_t)(k + 1) * 18);
      ASSERT_LT(Tag, NSym) << "weak '" << Nm << "' aux TagIndex out of range";
      std::string TagName = slotName(Tag);
      if (Nm == "wfn0") {
        EXPECT_EQ(TagName, "wdef0")
            << "weak external wfn0 aux TagIndex not remapped to its default";
        SawW0 = true;
      } else if (Nm == "wfn1") {
        EXPECT_EQ(TagName, "wdef1")
            << "weak external wfn1 aux TagIndex not remapped to its default";
        SawW1 = true;
      }
    }
    k += 1u + NAux;
  }
  EXPECT_TRUE(SawW0) << "wfn0 missing from merged output";
  EXPECT_TRUE(SawW1) << "wfn1 missing from merged output";
}

TEST(MergeCOFFVerify, CatchesWeakExternalTagIndexNotRemapped) {
  using namespace COFF;
  // Independent proof that the verifier closes the aux blind spot: build a good
  // merge, then corrupt wfn1's aux TagIndex back to the historical "copied
  // verbatim" value (0, which names wdef0 instead of wdef1) and confirm
  // verifyMerge rejects it even though every section byte still matches.
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars, 0xAA};
  CoffSymSpec Def0{"wdef0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak0{"wfn0", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak0.WeakDefTag = 0;
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {Def0, Weak0}, {});
  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec Def1{"wdef1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak1{"wfn1", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak1.WeakDefTag = 0;
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {Def1, Weak1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  {
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  uint32_t SymPtr = getU32(Out.data() + 8);
  uint32_t NSym = getU32(Out.data() + 12);
  bool Patched = false;
  for (uint32_t k = 0; k + 1 < NSym;) {
    char *P = Out.data() + SymPtr + (uint64_t)k * 18;
    std::string Nm(P, strnlen(P, 8));
    uint8_t Storage = (uint8_t)P[16];
    uint8_t NAux = (uint8_t)P[17];
    if (Nm == "wfn1" && Storage == IMAGE_SYM_CLASS_WEAK_EXTERNAL && NAux >= 1) {
      char *Aux = Out.data() + SymPtr + (uint64_t)(k + 1) * 18;
      Aux[0] = Aux[1] = Aux[2] = Aux[3] = 0; // TagIndex = 0 -> wrong default
      Patched = true;
      break;
    }
    k += 1u + NAux;
  }
  ASSERT_TRUE(Patched) << "could not locate wfn1 weak external to corrupt";

  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << "verifier accepted a weak external whose aux TagIndex points at the "
         "wrong default (the aux blind spot)";
}

TEST(MergeCOFFVerify, AcceptsGoodMergeRejectsCollapse) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec FA{"fa", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec FB{"fb", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {FA}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {FB}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)); // internal verify passes

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;
  {
    CoffView V = parseCOFF(Out);
    const CoffParsedSym *PFB = V.findSym("fb");
    ASSERT_NE(PFB, nullptr);
    EXPECT_EQ(PFB->Value, 0x40u);
  }

  // Collapse fb to 0: content at 0 is partition 0's 0xAA, not fb's 0xBB.
  auto Collapsed = Out;
  ASSERT_TRUE(patchCoffSymValue(Collapsed, "fb", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "COFF verifier accepted a collapsed symbol offset";

  auto OOB = Out;
  ASSERT_TRUE(patchCoffSymValue(OOB, "fb", 0x9999));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(OOB), Format::COFF, {}, &Err))
      << "COFF verifier accepted an out-of-bounds symbol value";
}

TEST(MergeCOFFVerify, CatchesCollapsedDuplicateNamedSymbol) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  // Two partitions each define a file-local (STATIC) symbol named "dup" — the
  // COFF analogue of two same-named statics.  Ambiguous by name, so the unique
  // anchor skips it; the duplicate-name content anchor must still reject a
  // collapse of every "dup" to 0 (partition 1's then reads 0xAA, not 0xBB).
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec S1{".text", 0x40, TextChars, 0xBB};
  CoffSymSpec D0{"dup", 0, 1, IMAGE_SYM_CLASS_STATIC};
  CoffSymSpec D1{"dup", 0, 1, IMAGE_SYM_CLASS_STATIC};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {D0}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {D1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchAllCoffSymValues(Collapsed, "dup", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "COFF verifier accepted a collapsed duplicate-named symbol offset";
}

TEST(MergeCOFFVerify, CatchesCollapsedBssSymbolOffset) {
  using namespace COFF;
  // COFF .bss twin of the historical collapse.  Two uninitialized externals
  // share one input .bss (IMAGE_SCN_CNT_UNINITIALIZED_DATA, no on-disk bytes,
  // so the content anchor skips it).  Only the same-section relative-distance
  // invariant can catch collapsing bss_b onto bss_a.
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec B{".bss", 0x40, BssChars};
  CoffSymSpec VA{"bss_a", 0x0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec VB{"bss_b", 0x20, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto Obj = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B}, {VA, VB}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)); // internal verify accepts

  {
    CoffView V = parseCOFF(Out);
    const CoffParsedSym *PA = V.findSym("bss_a");
    const CoffParsedSym *PB = V.findSym("bss_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PA->Value, 0x0u);
    EXPECT_EQ(PB->Value, 0x20u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchCoffSymValue(Collapsed, "bss_b", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "verifier accepted a collapsed COFF .bss symbol offset";
}

TEST(MergeCOFFVerify, CatchesCollapsedSingletonBssDistinctSections) {
  using namespace COFF;
  // COFF parity for the singleton .bss blind spot: two objects each with a
  // single uninitialized external in their own .bss.  One symbol per input
  // section starves the relative-distance invariant, and BSS has no on-disk
  // bytes for the content anchor — only the disjoint-range invariant can reject
  // collapsing g1 onto g0's slot.
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec B0{".bss", 0x40, BssChars};
  CoffSecSpec B1{".bss", 0x40, BssChars};
  CoffSymSpec V0{"g0", 0x0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec V1{"g1", 0x0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B0}, {V0}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B1}, {V1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)); // internal verify accepts

  {
    CoffView V = parseCOFF(Out);
    const CoffParsedSym *P0 = V.findSym("g0");
    const CoffParsedSym *P1 = V.findSym("g1");
    ASSERT_NE(P0, nullptr);
    ASSERT_NE(P1, nullptr);
    EXPECT_EQ(P0->Value, 0x0u);
    EXPECT_EQ(P1->Value, 0x40u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchCoffSymValue(Collapsed, "g1", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "verifier accepted a collapsed singleton COFF .bss symbol across "
         "partitions";
}

TEST(MergeCOFFVerify, CatchesCollapsedRelocOffset) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  // Each partition: a function spanning its .text and a relocation against the
  // undefined "ext" at offset 0x10.  After merge the relocs sit at 0x10 (p0)
  // and 0x50 (p1, shifted past p0's 0x40 .text); collapsing them to 0 must be
  // caught even though the symbols themselves stay correct.
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec S1{".text", 0x40, TextChars, 0xBB};
  CoffSymSpec F0{"f0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec F1{"f1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Ext{"ext", 0, 0, IMAGE_SYM_CLASS_EXTERNAL};
  CoffRelSpec R0{0, 0x10, "ext", (uint16_t)IMAGE_REL_AMD64_REL32};
  CoffRelSpec R1{0, 0x10, "ext", (uint16_t)IMAGE_REL_AMD64_REL32};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {F0, Ext}, {R0});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllCoffRelocVAs(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << "COFF verifier accepted collapsed relocation offsets";
}

TEST(MergeCOFFVerify, CatchesCollapsedSectionRelativeReloc) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  uint32_t DataChars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                       IMAGE_SCN_ALIGN_16BYTES;
  // Section-relative relocation: each partition's .text references the .rdata
  // *section symbol* — a STATIC symbol named after the section (".rdata"), not
  // a function — at offset 0x10.  This is the COFF analogue of an ELF
  // STT_SECTION target: the name is *not unique* in the merged output (every
  // partition contributes its own ".rdata" section symbol), so the reloc is
  // keyed only by the section-name string.  f0/f1 anchor each .text; after
  // merge the sites sit at 0x10 (p0) and 0x50 (p1, shifted past p0's 0x40
  // .text).  Collapsing the reloc offsets to 0 must be caught even though the
  // target symbol is ambiguous by name — the gap the ELF/MachO verifiers just
  // closed, asserted here too so all three object paths reject this class
  // symmetrically.
  CoffSecSpec T0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec D0{".rdata", 0x20, DataChars, 0xCC};
  CoffSecSpec T1{".text", 0x40, TextChars, 0xBB};
  CoffSecSpec D1{".rdata", 0x20, DataChars, 0xDD};
  CoffSymSpec F0{"f0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec F1{"f1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  // Defined STATIC section symbol for .rdata (section 2) — the reloc target.
  CoffSymSpec SD0{".rdata", 0, 2, IMAGE_SYM_CLASS_STATIC};
  CoffSymSpec SD1{".rdata", 0, 2, IMAGE_SYM_CLASS_STATIC};
  CoffRelSpec R0{0, 0x10, ".rdata", (uint16_t)IMAGE_REL_AMD64_REL32};
  CoffRelSpec R1{0, 0x10, ".rdata", (uint16_t)IMAGE_REL_AMD64_REL32};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {T0, D0}, {F0, SD0}, {R0});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {T1, D1}, {F1, SD1}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllCoffRelocVAs(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << "COFF verifier accepted a collapsed section-relative reloc offset";
}

// ---------------------------------------------------------------------------
// Mach-O semantic tests — same invariant on the Darwin object path, where the
// symbol n_value fix-up (section-relative → segment-relative) lived.
// ---------------------------------------------------------------------------

// P0 arch-consistency guard (COFF): mixing IMAGE_FILE_MACHINE values must be
// refused.  COFF's only caller today is parallel codegen (always one machine),
// so this guards a future general path against a cross-ISA object.
TEST(MergeCOFF, RefusesMixedMachine) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars};
  CoffSecSpec S1{".text", 0x20, TextChars};
  CoffSymSpec P0{"p0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec P1{"p1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto Obj0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {P0}, {});

  // Positive control: same machine merges.
  {
    auto C1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {P1}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(Obj0);
    Bufs.push_back(std::move(C1));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }

  auto Obj1 = buildCOFF(IMAGE_FILE_MACHINE_ARM64, {S1}, {P1}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::COFF))
      << "merger accepted mixed COFF machine inputs";
}

// Robustness regressions found by neverc-merge-fuzzer: a malformed COFF input
// must be refused gracefully (mergeObjects returns false), never crash the
// process.  Production only feeds well-formed codegen output, but the merger is
// also the linker's general relocatable path, and the fuzzer reached both of
// these in seconds.

// A section that has relocations AND a non-zero VirtualAddress makes LLVM's
// COFFObjectFile::section_rel_begin() call report_fatal_error (a hard process
// abort) the moment the merger iterates that section's relocations.  The merger
// now refuses such an input up front; without the guard this test aborts the
// whole test binary.
TEST(MergeCOFF, RefusesNonZeroVirtualAddressGracefully) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars, 0x90};
  CoffSymSpec P0{"p0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec U0{"ext", 0, 0,
                 IMAGE_SYM_CLASS_EXTERNAL}; // undefined reloc target
  CoffRelSpec R0{0, 0x8, "ext", (uint16_t)IMAGE_REL_AMD64_ADDR64};
  auto Obj = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {P0, U0}, {R0});

  // Patch section 0's VirtualAddress: file header (20) + sec*40 + name(8) +
  // VirtualSize(4) = offset 32.
  ASSERT_GT(Obj.size(), (size_t)36);
  Obj[32] = 0x00;
  Obj[33] = 0x10;
  Obj[34] = 0x00;
  Obj[35] = 0x00; // VirtualAddress = 0x1000

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::COFF))
      << "merger accepted a COFF section with a non-zero VirtualAddress";
}

// A section whose 8-byte name is a "/<offset>" long-name escape pointing past
// the string table makes LLVM's COFFObjectFile::getSectionName return an
// Expected error ("invalid section name").  The merger consulted the value but
// never *consumed* that error, so in an assertions / ABI-breaking-checks build
// the Expected's destructor aborted the whole process ("Expected<T> must be
// checked before access or destruction") at the next scope exit — a crash on
// hostile -r input the merge fuzzer found in seconds.  The merger now consumes
// the error and refuses.  EXPECT_FALSE holds in both build modes: with the fix
// the merge is refused; without it, an assertions build aborts (test fails) and
// a release build silently mis-named the section "" (also wrong).
TEST(MergeCOFF, RefusesInvalidSectionNameWithoutAbort) {
  using namespace COFF;
  uint32_t TextChars =
      IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
  // "/9999999": leading '/' marks a long-name escape; 9999999 is a string-table
  // offset far beyond the 4-byte table buildCOFF emits, so getSectionName
  // errors.
  CoffSecSpec S0{"/9999999", 0x10, TextChars};
  auto Obj = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::COFF))
      << "a section with an out-of-range long-name escape must be refused, not "
         "leak an unchecked Expected";
}

// A symbol whose NumberOfAuxSymbols runs past the end of the symbol table makes
// the merger's manual aux-record indexing (getRawPtr() + 18*(a+1)) walk off the
// input buffer — an out-of-bounds heap read that copies uninitialized bytes
// into the output (non-deterministic, info-leaking) and can fault.  The merger
// now bounds-checks each aux record against the input.
TEST(MergeCOFF, RefusesOutOfBoundsAuxRecordGracefully) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars, 0x90};
  CoffSymSpec P0{"p0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec P1{"p1", 0x4, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto Obj = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {P0, P1}, {});

  // The symbol table is the last real structure (only a 4-byte string-table
  // length follows it).  Bump the LAST symbol's NumberOfAuxSymbols (byte 17 of
  // its 18-byte record) so its claimed aux slots extend past the buffer.
  uint32_t SymPtr = getU32(Obj.data() + 8);
  uint32_t NumSyms = getU32(Obj.data() + 12);
  ASSERT_GE(NumSyms, 2u);
  size_t LastAuxCountOff = (size_t)SymPtr + (size_t)(NumSyms - 1) * 18 + 17;
  ASSERT_LT(LastAuxCountOff, Obj.size());
  Obj[LastAuxCountOff] = (char)100; // 100 aux records that do not exist

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::COFF))
      << "merger accepted a COFF symbol whose aux records run past the object";
}

TEST(MergeMachOSemantic, CrossPartitionSymbolOffsets) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec P0{"_p0", DefExt, 1, 0, 0};
  MachoSymSpec P1{"_p1", DefExt, 1, 0, 0};

  auto Obj0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {P0});
  auto Obj1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {P1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));

  MachoView V = parseMachO(Out);
  ASSERT_TRUE(V.Ok);
  const MachoParsedSec *Text = V.findSec("__TEXT", "__text");
  ASSERT_NE(Text, nullptr);
  const MachoParsedSym *PP0 = V.findSym("_p0");
  const MachoParsedSym *PP1 = V.findSym("_p1");
  ASSERT_NE(PP0, nullptr);
  ASSERT_NE(PP1, nullptr);
  // n_value is segment-relative in the merged object; subtracting the merged
  // section address recovers the section-relative offset.
  EXPECT_EQ(PP0->Value - Text->Addr, 0x0u);
  EXPECT_EQ(PP1->Value - Text->Addr, 0x40u); // shifted past partition 0
}

TEST(MergeMachOSemantic, ZerofillSectionsMergeByVirtualSize) {
  namespace MO = llvm::MachO;
  MachoSecSpec B0{"__DATA", "__bss", 0x30, 4, MO::S_ZEROFILL};
  MachoSecSpec B1{"__DATA", "__bss", 0x10, 4, MO::S_ZEROFILL};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec V0{"_v0", DefExt, 1, 0, 0};
  MachoSymSpec V1{"_v1", DefExt, 1, 0, 0};

  auto Obj0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B0}, {V0});
  auto Obj1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B1}, {V1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));

  MachoView V = parseMachO(Out);
  ASSERT_TRUE(V.Ok);
  const MachoParsedSec *Bss = V.findSec("__DATA", "__bss");
  ASSERT_NE(Bss, nullptr);
  const MachoParsedSym *PV1 = V.findSym("_v1");
  ASSERT_NE(PV1, nullptr);
  EXPECT_EQ(PV1->Value - Bss->Addr,
            0x30u); // shifted past partition 0's zerofill
}

// nlist_64::n_sect is a uint8_t, so a Mach-O object can address at most 255
// sections (0 == NO_SECT).  The merger must refuse to emit more rather than
// silently truncate every section number past 255 — the Mach-O twin of the ELF
// e_shnum guard and the COFF NumberOfSections guard.  255 distinct
// (segment, section) sections is the boundary that must still merge; 256 must
// be refused (and that refusal lets the parallel-codegen caller fall back to
// serial codegen instead of emitting a wrong object). P0 arch-consistency guard
// (Mach-O): mixing cputype must be refused — besides the cross-ISA body, it
// would corrupt the IsARM64-gated ARM64_RELOC_ADDEND / in-place fixup logic.
TEST(MergeMachO, RefusesMixedCpuType) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x20, 4, TextFlags};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec P0{"_p0", DefExt, 1, 0, 0};
  MachoSymSpec P1{"_p1", DefExt, 1, 0, 0};
  auto Obj0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {P0});

  // Positive control: same cputype merges.
  {
    auto C1 =
        buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {P1});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(Obj0);
    Bufs.push_back(std::move(C1));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  }

  auto Obj1 =
      buildMachO(MO::CPU_TYPE_X86_64, MO::CPU_SUBTYPE_X86_64_ALL, {S1}, {P1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
      << "merger accepted mixed Mach-O cputype inputs";
}

// P1 (Mach-O): same ODR-violation refuse as ELF — two strong external defs of
// one name would be silently resolved to one by the priority dedup.
TEST(MergeMachO, RefusesMultipleStrongDefinitions) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x20, 4, TextFlags};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec D0{"_dup", DefExt, 1, 0, 0};
  MachoSymSpec D1{"_dup", DefExt, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {D0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {D1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(O0));
  Bufs.push_back(std::move(O1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
      << "merger accepted two strong Mach-O definitions of the same symbol";
}

TEST(MergeMachO, Accepts255SectionsRefuses256) {
  namespace MO = llvm::MachO;
  auto buildN = [](unsigned NSec) {
    std::vector<MachoSecSpec> Secs;
    for (unsigned i = 0; i < NSec; ++i)
      Secs.push_back({"__TEXT", "__s" + std::to_string(i), 4, 0,
                      (uint32_t)MO::S_REGULAR, (uint8_t)0});
    return buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, Secs, {});
  };

  {
    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(buildN(255));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_TRUE(mergeObjects(Bufs, OS, Format::MachO64))
        << "255 distinct sections is within the n_sect limit and must merge";
  }
  {
    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(buildN(256));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
        << "256 sections overflow the 8-bit n_sect; the merger must refuse "
           "rather than emit a truncated object";
  }
}

// A crafted mach_header whose ncmds dwarfs the file made LLVM's MachOObjectFile
// constructor push one LoadCommandInfo into a SmallVector per *claimed* command
// — a load command with cmdsize 0 never advances the parse cursor, so the loop
// runs ncmds times — ballooning that vector to ~ncmds*16 bytes (a ~640 MB
// allocation from a ~1 KB input) before a single byte was validated against the
// buffer.  The merge fuzzer hit this as an out-of-memory.  The merger now
// bounds ncmds against the object size before handing the bytes to the eager
// parser: a conformant object's load commands each occupy >=
// sizeof(load_command) bytes after the header, so it can hold at most (size -
// header)/8 of them.  A real merge is unaffected (the bound holds for every
// valid Mach-O); a header claiming far more is refused promptly instead of
// exhausting memory.
TEST(MergeMachO, RefusesHugeNcmdsWithoutOOM) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x10, 4, TextFlags, 0x90};
  auto Obj =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {});
  ASSERT_GE(Obj.size(), (size_t)20);

  // Overwrite mach_header_64.ncmds (little-endian, offset 16) with a value far
  // larger than the file could ever hold.  Without the guard this OOMs; with it
  // the merge is refused before the eager parser allocates ncmds entries.
  Obj[16] = 0x00;
  Obj[17] = 0x00;
  Obj[18] = 0x00;
  Obj[19] = 0x10; // 0x10000000 = 268M claimed load commands

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
      << "a header claiming more load commands than the object can hold must "
         "be "
         "refused before the Mach-O parser allocates one entry per command";
}

// An S_ZEROFILL section declares a size backed by *no* file bytes, so an
// 80-byte section header can claim a ~7 EB size.  The merger laid zerofill out
// on the same cursor as on-disk sections, so the symbol/string tables and the
// output buffer itself (SmallVector Out(CurOff)) were pushed out by that size —
// a crafted __bss drove the allocation to an allocation-size-too-big abort /
// OOM (the merge fuzzer found this; sibling of the ELF NOBITS crash).  Zerofill
// now advances a separate vm cursor and occupies no file space, so a huge __bss
// is emitted as a huge-sized, file-less section (legal, like ELF .bss) and the
// merge succeeds with a tiny output instead of crashing.  Built by hand because
// buildMachO would itself allocate `size` file bytes for the section.
TEST(MergeMachO, HandlesHugeZerofillSectionWithoutOOM) {
  namespace MO = llvm::MachO;
  const uint64_t Huge = 0x6000000000000000ull;

  uint32_t HdrSize = sizeof(MO::mach_header_64);
  uint32_t SegCmdSize = sizeof(MO::segment_command_64) + sizeof(MO::section_64);
  uint32_t SymCmdSize = sizeof(MO::symtab_command);
  uint32_t SizeOfCmds = SegCmdSize + SymCmdSize;
  uint32_t DataStart = HdrSize + SizeOfCmds;

  SmallVector<char, 0> Obj;
  Obj.resize(DataStart, 0);

  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Obj.data());
  MH->magic = MO::MH_MAGIC_64;
  MH->cputype = MO::CPU_TYPE_X86_64;
  MH->cpusubtype = MO::CPU_SUBTYPE_X86_64_ALL;
  MH->filetype = MO::MH_OBJECT;
  MH->ncmds = 2;
  MH->sizeofcmds = SizeOfCmds;
  MH->flags = MO::MH_SUBSECTIONS_VIA_SYMBOLS;

  char *Cmd = Obj.data() + HdrSize;
  auto *Seg = reinterpret_cast<MO::segment_command_64 *>(Cmd);
  Seg->cmd = MO::LC_SEGMENT_64;
  Seg->cmdsize = SegCmdSize;
  Seg->maxprot = 7;
  Seg->initprot = 7;
  Seg->nsects = 1;

  auto *SH =
      reinterpret_cast<MO::section_64 *>(Cmd + sizeof(MO::segment_command_64));
  memcpy(SH->sectname, "__bss", 5);
  memcpy(SH->segname, "__DATA", 6);
  SH->addr = 0;
  SH->size = Huge; // attacker-controlled, backed by no file bytes
  SH->offset = 0;  // zerofill: no file offset
  SH->align = 4;
  SH->flags = MO::S_ZEROFILL;

  Cmd += SegCmdSize;
  auto *SymCmd = reinterpret_cast<MO::symtab_command *>(Cmd);
  SymCmd->cmd = MO::LC_SYMTAB;
  SymCmd->cmdsize = SymCmdSize;

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // Reaching the assertion at all proves no OOM/abort.  With zerofill kept
  // off-disk the merge succeeds and the output stays tiny (no ~7 EB of zeros).
  bool OK = mergeMachO64Objects(Bufs, OS);
  EXPECT_LT(Out.size(), (size_t)0x10000)
      << "zerofill must not be materialized on disk (output ballooned to "
      << Out.size() << " bytes)";
  EXPECT_TRUE(OK) << "a valid Mach-O with a huge __bss must merge, not crash";
}

// A symbol whose name runs to the end of a non-NUL-terminated string table made
// the merger build StringRef(const char *) -> strlen() off the end of the input
// buffer (an out-of-bounds heap read; MachOObjectFile::getStringTableData does
// not validate a trailing NUL the way the ELF reader does).  The merger now
// strnlen-bounds the read to strsize, like the independent verifier.  The
// over-read is only a fault under a sanitizer, so this is most meaningful in
// the ASan build; everywhere it must simply not crash.
TEST(MergeMachO, HandlesNonNulTerminatedStringTableGracefully) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0x90};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec P0{"_p0", DefExt, 1, 0, 0};
  auto Obj =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {P0});

  // The string table is the last structure in the file; its final byte is the
  // NUL terminating "_p0".  Overwrite it so the table is non-NUL-terminated and
  // "_p0" runs to the buffer end with no terminator in between.
  ASSERT_FALSE(Obj.empty());
  Obj.back() = (char)0x41; // 'A'

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // Either outcome is acceptable; the invariant under test is "no OOB read".
  mergeMachO64Objects(Bufs, OS); // must not read out of bounds
}

// A non-external relocation whose r_address is within `len` bytes of UINT32_MAX
// overflowed the merger's 32-bit `addr + len > Data.size()` bounds check,
// wrapped to a small value that passed the guard, and then indexed the section
// data ~4 GiB out of bounds in the in-place fixup.  The merger now computes the
// bound in 64-bit.  Meaningful under ASan; everywhere it must not crash.
TEST(MergeMachO, HandlesHugeRelocAddressGracefully) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0x90};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec P0{"_p0", DefExt, 1, 0, 0};
  // Non-extern reloc (x86_64 so the ARM64_RELOC_ADDEND pseudo-reloc path is not
  // taken), targeting section 0, length 3 (8 bytes), r_address near UINT32_MAX.
  MachoRelSpec R{0,
                 0xFFFFFFF8u,
                 "",
                 (uint8_t)MO::X86_64_RELOC_UNSIGNED,
                 3,
                 /*Extern=*/false,
                 /*TargetSec=*/0};
  auto Obj = buildMachO(MO::CPU_TYPE_X86_64, MO::CPU_SUBTYPE_X86_64_ALL, {S0},
                        {P0}, {R});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // Either outcome is acceptable; the invariant under test is "no OOB access".
  mergeMachO64Objects(Bufs, OS); // must not access out of bounds
}

TEST(MergeMachOSemantic, RandomizedLayoutOracle) {
  // Mach-O analogue of the ELF/COFF randomized oracle.  Mach-O folds by
  // (segment, section) and tracks a running max align *exponent*; n_value in
  // the merged object is segment-relative, so the check subtracts the merged
  // section address to recover the predicted section-relative offset.
  namespace MO = llvm::MachO;
  struct Group {
    const char *Seg;
    const char *Sect;
    uint32_t Flags;
    bool IsZf;
  };
  const Group Groups[4] = {
      {"__TEXT", "__text",
       MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS, false},
      {"__DATA", "__data", 0, false},
      {"__TEXT", "__const", 0, false},
      {"__DATA", "__bss", MO::S_ZEROFILL, true},
  };
  struct ExpSym {
    std::string Name;
    unsigned Group;
    uint64_t Rel;
  };

  std::mt19937 Rng(0xD00DFEEDu);
  for (int Trial = 0; Trial < 300; ++Trial) {
    unsigned NP = 1 + (Rng() % 3);
    uint64_t CurSize[4] = {0, 0, 0, 0};
    uint32_t CurAlignExp[4] = {0, 0, 0, 0};
    std::vector<ExpSym> Expected;

    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      std::vector<MachoSecSpec> Secs;
      std::vector<MachoSymSpec> Syms;
      for (unsigned g = 0; g < 4; ++g) {
        if (Rng() % 3 == 0)
          continue;
        uint32_t AlignExp = Rng() % 7;
        uint64_t Size = 1 + (Rng() % 0x200);
        unsigned SecIdx = Secs.size();
        uint8_t Fill =
            Groups[g].IsZf ? 0 : (uint8_t)(1 + ((p * 7 + g * 3) & 0x7e));
        Secs.push_back(MachoSecSpec{Groups[g].Seg, Groups[g].Sect, Size,
                                    AlignExp, Groups[g].Flags, Fill});

        if (AlignExp > CurAlignExp[g])
          CurAlignExp[g] = AlignExp;
        uint64_t A = 1ull << CurAlignExp[g];
        uint64_t Pad = (A - (CurSize[g] % A)) % A;
        uint64_t Base = CurSize[g] + Pad;
        CurSize[g] = Base + Size;

        unsigned NSym = 1 + (Rng() % 3);
        for (unsigned k = 0; k < NSym; ++k) {
          uint64_t SOff = Rng() % Size;
          std::string SN = "_s_" + std::to_string(p) + "_" + std::to_string(g) +
                           "_" + std::to_string(k);
          Syms.push_back(MachoSymSpec{SN, (uint8_t)(MO::N_SECT | MO::N_EXT),
                                      (uint8_t)(1 + SecIdx), SOff, 0});
          Expected.push_back({SN, g, Base + SOff});
        }
      }
      Bufs.push_back(buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL,
                                Secs, Syms));
    }

    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64)) << "trial " << Trial;
    MachoView V = parseMachO(Out);
    ASSERT_TRUE(V.Ok) << "trial " << Trial;
    for (auto &E : Expected) {
      const MachoParsedSym *PS = V.findSym(E.Name);
      ASSERT_NE(PS, nullptr) << "trial " << Trial << " sym " << E.Name;
      const MachoParsedSec *Sec =
          V.findSec(Groups[E.Group].Seg, Groups[E.Group].Sect);
      ASSERT_NE(Sec, nullptr) << "trial " << Trial << " sec for " << E.Name;
      EXPECT_EQ(PS->Value - Sec->Addr, E.Rel)
          << "trial " << Trial << " sym " << E.Name;
    }
    std::string VErr;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::MachO64, {}, &VErr))
        << "trial " << Trial << ": " << VErr;
  }
}

TEST(MergeMachOSemantic, MergeIsDeterministic) {
  namespace MO = llvm::MachO;
  // Identical inputs must produce byte-identical output (see the ELF analogue).
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__data", 0x20, 3, 0u, 0xCC};
  MachoSecSpec B0{"__DATA", "__bss", 0x30, 4, MO::S_ZEROFILL};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec G0{"_g0", DefExt, 2, 0, 0};
  MachoSymSpec V0{"_v0", DefExt, 3, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL,
                       {S0, C0, B0}, {F0, G0, V0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {F1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out1, Out2;
  {
    raw_svector_ostream OS(Out1);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  }
  {
    raw_svector_ostream OS(Out2);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  }
  ASSERT_EQ(Out1.size(), Out2.size());
  EXPECT_EQ(0, std::memcmp(Out1.data(), Out2.data(), Out1.size()))
      << "Mach-O merge is not deterministic";
}

TEST(MergeMachOVerify, AcceptsGoodMergeRejectsCollapse) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec FA{"_fa", DefExt, 1, 0, 0};
  MachoSymSpec FB{"_fb", DefExt, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {FA});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {FB});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(
      mergeObjects(Bufs, OS, Format::MachO64)); // internal verify passes

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  MachoView V = parseMachO(Out);
  const MachoParsedSym *PFA = V.findSym("_fa");
  const MachoParsedSym *PFB = V.findSym("_fb");
  ASSERT_NE(PFA, nullptr);
  ASSERT_NE(PFB, nullptr);
  EXPECT_EQ(PFB->Value - PFA->Value, 0x40u);

  // Collapse _fb onto _fa's location: its content window now reads 0xAA, not
  // its own 0xBB → must be rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchMachoSymValue(Collapsed, "_fb", PFA->Value));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {},
                           &Err))
      << "Mach-O verifier accepted a collapsed symbol offset";

  // Past the end of the merged section.
  auto OOB = Out;
  ASSERT_TRUE(patchMachoSymValue(OOB, "_fb", PFA->Value + 0x9999));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(OOB), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted an out-of-bounds symbol value";
}

TEST(MergeMachOVerify, AcceptsIndependentlyCoalescedWeakDefinitions) {
  namespace MO = llvm::MachO;
  // Every weak definition is coalesced independently.  The output may select
  // _weak_a and _weak_b from one input while another input placed those names
  // at different relative offsets in the same section.  Weak definitions need
  // not have identical bodies; the survivor only has to match one input copy,
  // and no weak copy can serve as a fixed section-shift anchor.
  MachoSecSpec S0{"__TEXT", "__const", 0x40, 4, MO::S_REGULAR, 0xA5};
  MachoSecSpec S1{"__TEXT", "__const", 0x40, 4, MO::S_REGULAR, 0x3C};
  uint8_t DefWeak = MO::N_SECT | MO::N_EXT | MO::N_PEXT;
  uint16_t WeakDesc = MO::N_WEAK_DEF;
  MachoSymSpec A0{"_weak_a", DefWeak, 1, 0x00, WeakDesc};
  MachoSymSpec B0{"_weak_b", DefWeak, 1, 0x20, WeakDesc};
  MachoSymSpec A1{"_weak_a", DefWeak, 1, 0x10, WeakDesc};
  MachoSymSpec B1{"_weak_b", DefWeak, 1, 0x20, WeakDesc};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {A0, B0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {A1, B1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(O0));
  Bufs.push_back(std::move(O1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  auto Corrupt = Out;
  ASSERT_TRUE(corruptMachoSymbolContentByte(Corrupt, "_weak_a"));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Corrupt), Format::MachO64, {}, &Err))
      << "verifier accepted a corrupted surviving weak definition";
}

TEST(MergeMachOVerify, CatchesCollapsedDuplicateNamedSymbol) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  // Local (no N_EXT) symbols sharing the name "_dup": the Darwin analogue of
  // two file-local statics.  Ambiguous by name → skipped by the unique anchor;
  // the duplicate-name content anchor must still reject a collapse onto one
  // location (partition 1's then reads 0xAA instead of its own 0xBB).
  uint8_t DefLocal = MO::N_SECT;
  MachoSymSpec D0{"_dup", DefLocal, 1, 0, 0};
  MachoSymSpec D1{"_dup", DefLocal, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {D0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {D1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  // The first "_dup" (partition 0) sits at the merged __text base over 0xAA.
  MachoView V = parseMachO(Out);
  const MachoParsedSym *PD = V.findSym("_dup");
  ASSERT_NE(PD, nullptr);

  // Collapse every "_dup" onto partition 0's location: partition 1's window now
  // reads 0xAA instead of its own 0xBB → must be rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchAllMachoSymValues(Collapsed, "_dup", PD->Value));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {},
                           &Err))
      << "Mach-O verifier accepted a collapsed duplicate-named symbol offset";
}

TEST(MergeMachOVerify, CatchesCorruptedDysymtabRanges) {
  namespace MO = llvm::MachO;
  // The merger sorts symbols into local | external-defined | undefined and
  // writes the LC_DYSYMTAB ranges describing that partition.  A bug there would
  // silently mislead consumers about which symbols are exported vs. undefined
  // without touching any byte the content anchor inspects, so the verifier
  // audits the ranges directly.  Two defined externals merge into a table of
  // exactly two external-defined symbols (no locals, no undefs).
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec FA{"_fa", DefExt, 1, 0, 0};
  MachoSymSpec FB{"_fb", DefExt, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {FA});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {FB});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  // (a) Non-contiguous ranges: claim one local without shifting iextdefsym, so
  // the local and external-defined ranges overlap at symbol 0.
  auto BadContig = Out;
  ASSERT_TRUE(patchMachoDysymtab(BadContig, 0, 1, 0, 2, 2, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(BadContig), Format::MachO64, {},
                           &Err))
      << "verifier accepted non-contiguous LC_DYSYMTAB ranges";

  // (b) Contiguous but mis-classified: an external-defined symbol parked in the
  // local range (local[0,1), extdef[1,2), undef[2,2)).
  auto BadClass = Out;
  ASSERT_TRUE(patchMachoDysymtab(BadClass, 0, 1, 1, 1, 2, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(BadClass), Format::MachO64, {}, &Err))
      << "verifier accepted an external symbol inside the local range";
}

TEST(MergeMachOVerify, CatchesCollapsedZerofillSymbolOffset) {
  namespace MO = llvm::MachO;
  // Mach-O __bss twin of the historical collapse.  Two S_ZEROFILL globals share
  // one input section (no on-disk bytes, so the content anchor skips them).
  // Only the same-section relative-distance invariant can catch collapsing
  // _bss_b onto _bss_a.  n_value is segment-relative, so "collapse" means
  // setting _bss_b's n_value back to the section base (= _bss_a's value).
  MachoSecSpec B{"__DATA", "__bss", 0x40, 4, MO::S_ZEROFILL};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec VA{"_bss_a", DefExt, 1, 0x0, 0};
  MachoSymSpec VB{"_bss_b", DefExt, 1, 0x20, 0};
  auto Obj =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B}, {VA, VB});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(
      mergeObjects(Bufs, OS, Format::MachO64)); // internal verify accepts

  uint64_t BaseVal = 0;
  {
    MachoView V = parseMachO(Out);
    const MachoParsedSym *PA = V.findSym("_bss_a");
    const MachoParsedSym *PB = V.findSym("_bss_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PB->Value - PA->Value, 0x20u);
    BaseVal = PA->Value;
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchMachoSymValue(Collapsed, "_bss_b", BaseVal));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {},
                           &Err))
      << "verifier accepted a collapsed Mach-O zerofill symbol offset";
}

TEST(MergeMachOVerify, CatchesCollapsedSingletonZerofillDistinctSections) {
  namespace MO = llvm::MachO;
  // Mach-O parity for the singleton blind spot: two objects each with a single
  // S_ZEROFILL global in their own __bss.  One symbol per input section starves
  // the relative-distance invariant, and zerofill has no on-disk bytes — only
  // the disjoint-range invariant catches collapsing _g1 back onto the shared
  // section base (the segment-relative form of an offset collapse).
  MachoSecSpec B0{"__DATA", "__bss", 0x40, 4, MO::S_ZEROFILL};
  MachoSecSpec B1{"__DATA", "__bss", 0x40, 4, MO::S_ZEROFILL};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec V0{"_g0", DefExt, 1, 0x0, 0};
  MachoSymSpec V1{"_g1", DefExt, 1, 0x0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B0}, {V0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B1}, {V1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(
      mergeObjects(Bufs, OS, Format::MachO64)); // internal verify accepts

  uint64_t BaseVal = 0;
  {
    MachoView V = parseMachO(Out);
    const MachoParsedSym *P0 = V.findSym("_g0");
    const MachoParsedSym *P1 = V.findSym("_g1");
    ASSERT_NE(P0, nullptr);
    ASSERT_NE(P1, nullptr);
    EXPECT_EQ(P1->Value - P0->Value, 0x40u); // p1's __bss shifted past p0's
    BaseVal = P0->Value;
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchMachoSymValue(Collapsed, "_g1", BaseVal));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {},
                           &Err))
      << "verifier accepted a collapsed singleton Mach-O zerofill symbol "
         "across "
         "partitions";
}

TEST(MergeMachOVerify, AcceptsDuplicateLocalLabelWithRelocInFirstWindow) {
  namespace MO = llvm::MachO;
  // Regression for a verifier *false positive* found by the randomized
  // execution fuzzer (MergeFuzzExecution).  LLVM's Mach-O backend emits a local
  // label 'ltmp0' at the start of every object's __text, so merging N partition
  // objects yields N same-named locals — the ambiguous (duplicate-name) verify
  // path.  When a later module's true 'ltmp0' begins with a relocated
  // instruction (a call here), its content window overlaps a relocation site
  // and is correctly skipped as undecidable, while the *earlier* module's
  // 'ltmp0' is decidable but holds different bytes.  The verifier used to read
  // that as "no copy matches → collapse" and reject a perfectly correct merge,
  // forcing a spurious fallback to serial codegen on macOS.  It must now ACCEPT
  // this shape. (Proven correct independently: with verify off, every such
  // merge executes byte-identically to a plain link.)
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  uint8_t DefLocal = MO::N_SECT;
  uint8_t UndefExt = MO::N_EXT;
  MachoSymSpec L0{"ltmp0", DefLocal, 1, 0, 0}; // module 0's start label
  MachoSymSpec L1{"ltmp0", DefLocal, 1, 0, 0}; // module 1's start label (dup)
  MachoSymSpec Ext{"_ext", UndefExt, 0, 0, 0};
  // A relocation in module 1's first 16 bytes: after merge its 'ltmp0' lands at
  // 0x40 and that window overlaps the reloc site, so it is skipped — the exact
  // trigger for the old false positive.
  MachoRelSpec R1{0, 0, "_ext", (uint8_t)MO::ARM64_RELOC_BRANCH26, 2, true, -1};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {L0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1},
                       {L1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // Internal verify (default on) must ACCEPT — pre-fix it rejected here, which
  // would make the real merger return false and fall back to serial codegen.
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64))
      << "merger false-rejected a correct merge of duplicate local labels "
         "whose "
         "true home overlaps a relocation site";
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;
}

TEST(MergeMachOVerify, CatchesCollapsedRelocOffset) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // Each partition: a function spanning its __text and an extern relocation
  // against undefined "_ext" at section offset 0x10.  After merge the relocs
  // sit at 0x10 (p0) and 0x50 (p1, past p0's 0x40 __text); collapsing them to
  // 0 must be caught even though the symbols stay correct.
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  uint8_t UndefExt = MO::N_EXT; // N_UNDF (0) | N_EXT
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  MachoSymSpec Ext{"_ext", UndefExt, 0, 0, 0};
  MachoRelSpec R0{0, 0x10, "_ext", (uint8_t)MO::ARM64_RELOC_BRANCH26, 2};
  MachoRelSpec R1{0, 0x10, "_ext", (uint8_t)MO::ARM64_RELOC_BRANCH26, 2};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0},
                       {F0, Ext}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1},
                       {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllMachoRelocAddrs(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted collapsed relocation offsets";
}

TEST(MergeMachOVerify, CatchesCollapsedSectionRelativeReloc) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // Section-relative (non-extern) relocation: __text holds an 8-byte pointer
  // into __const, which the merger rewrites *in place* via its own offset
  // arithmetic (a separate code path from extern relocs, previously unchecked).
  // f0/f1 anchor each __text; merged sites land at 0x10 (p0) and 0x50 (p1).
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__const", 0x20, 4, 0u, 0xCC};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  MachoSecSpec C1{"__DATA", "__const", 0x20, 4, 0u, 0xDD};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  // Non-extern UNSIGNED pointer in __text (sec 0) → __const (sec 1), 8 bytes.
  MachoRelSpec R0{0,
                  0x10,
                  "",
                  (uint8_t)MO::ARM64_RELOC_UNSIGNED,
                  3,
                  /*Extern=*/false,
                  /*TargetSec=*/1};
  MachoRelSpec R1{0,
                  0x10,
                  "",
                  (uint8_t)MO::ARM64_RELOC_UNSIGNED,
                  3,
                  /*Extern=*/false,
                  /*TargetSec=*/1};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0, C0},
                       {F0}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1, C1},
                       {F1}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllMachoRelocAddrs(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted a collapsed section-relative reloc offset";
}

TEST(MergeMachOVerify, CatchesWrongInPlacePointerDelta) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // Distinct __const fills so a mis-targeted pointer reads the wrong bytes.
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__const", 0x20, 4, 0u, 0xCC};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  MachoSecSpec C1{"__DATA", "__const", 0x20, 4, 0u, 0xDD};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  // 8-byte absolute (UNSIGNED) non-extern pointer in __text → __const.
  MachoRelSpec R0{0,
                  0x10,
                  "",
                  (uint8_t)MO::ARM64_RELOC_UNSIGNED,
                  3,
                  /*Extern=*/false,
                  /*TargetSec=*/1};
  MachoRelSpec R1{0,
                  0x10,
                  "",
                  (uint8_t)MO::ARM64_RELOC_UNSIGNED,
                  3,
                  /*Extern=*/false,
                  /*TargetSec=*/1};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0, C0},
                       {F0}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1, C1},
                       {F1}, {R1});

  // Plant a real pointer at each __text reloc site pointing at that partition's
  // __const base, so the merger's in-place fixup has something to relocate (and
  // the verifier's value check engages instead of skipping a garbage pointer).
  {
    MachoView V0 = parseMachO(O0);
    const MachoParsedSec *Cs0 = V0.findSec("__DATA", "__const");
    ASSERT_NE(Cs0, nullptr);
    ASSERT_TRUE(patchMachoSecQword(O0, "__TEXT", "__text", 0x10, Cs0->Addr));
    MachoView V1 = parseMachO(O1);
    const MachoParsedSec *Cs1 = V1.findSec("__DATA", "__const");
    ASSERT_NE(Cs1, nullptr);
    ASSERT_TRUE(patchMachoSecQword(O1, "__TEXT", "__text", 0x10, Cs1->Addr));
  }

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // The internal verify runs the value check too; a good merge must pass it.
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  // Corrupt p0's merged in-place pointer (at __text+0x10) to 0 — now it points
  // into __text rather than __const.  The site is unchanged, so only the value
  // check can catch this.
  auto Bad = Out;
  ASSERT_TRUE(patchMachoSecQword(Bad, "__TEXT", "__text", 0x10, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Bad), Format::MachO64, {}, &Err))
      << "value check missed a mis-targeted in-place pointer";
}

TEST(MergeMachO, DoesNotRewriteAnInstructionAsIfItWereAWord) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // A section-relative relocation is adjusted by adding the layout delta to
  // the whole field it covers, on the grounds that a relocation which is not
  // PC-relative addresses a word of its own. ARM64_RELOC_PAGEOFF12 is not
  // PC-relative either, and its field is the twelve-bit immediate inside an
  // `add` -- bits 10 through 21 of the instruction word, with the destination
  // and source registers below it and the opcode above. Adding a delta to the
  // word writes through all of that: a small delta lands in the register
  // fields and changes which registers the instruction reads, a larger one
  // reaches the immediate but at the wrong bit position. The object stays
  // well-formed and the self-check skips relocation sites, so neither notices.
  const uint32_t AddInstruction = 0x91000000; // add x0, x0, #0
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__const", 0x20, 4, 0u, 0xCC};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  MachoSecSpec C1{"__DATA", "__const", 0x20, 4, 0u, 0xDD};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  MachoRelSpec R0{0,
                  0x10,
                  "",
                  (uint8_t)MO::ARM64_RELOC_PAGEOFF12,
                  2,
                  /*Extern=*/false,
                  /*TargetSec=*/1};
  MachoRelSpec R1{0,
                  0x10,
                  "",
                  (uint8_t)MO::ARM64_RELOC_PAGEOFF12,
                  2,
                  /*Extern=*/false,
                  /*TargetSec=*/1};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0, C0},
                       {F0}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1, C1},
                       {F1}, {R1});
  // Put a real instruction at each relocation site. The high half of the
  // qword is padding beyond the four bytes the relocation covers.
  ASSERT_TRUE(patchMachoSecQword(O0, "__TEXT", "__text", 0x10, AddInstruction));
  ASSERT_TRUE(patchMachoSecQword(O1, "__TEXT", "__text", 0x10, AddInstruction));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  if (!mergeObjects(Bufs, OS, Format::MachO64))
    return; // Refusing what it cannot adjust is the other acceptable answer.

  // Everything except the immediate field: the registers below it and the
  // opcode above have to survive whatever the relocation did.
  const uint32_t OutsideImmediate = ~(UINT32_C(0xFFF) << 10);
  for (uint32_t Site : {UINT32_C(0x10), UINT32_C(0x50)}) {
    std::optional<uint32_t> Word =
        readMachoSecWord(ArrayRef<char>(Out), "__TEXT", "__text", Site);
    ASSERT_TRUE(Word.has_value()) << "site " << Site;
    EXPECT_EQ(*Word & OutsideImmediate, AddInstruction & OutsideImmediate)
        << "the merge rewrote the instruction at __text+" << Site;
  }
}

TEST(MergeMachO, WholeWordFactAgreesWithTheObjectGraphLayer) {
  namespace MO = llvm::MachO;
  // "Does this relocation cover a word of its own" is stated twice: here for
  // the merger, which is kept free of the plugin ABI, and in
  // Plugin/Host/NativeRelocationFacts.h for the object graph. Nothing in
  // either place would notice the two drifting apart -- each answers its own
  // caller correctly right up until one of them is updated alone -- so the
  // agreement is checked rather than assumed.
  const std::array<std::pair<uint32_t, const char *>, 2> Targets = {
      {{MO::CPU_TYPE_ARM64, "arm64-apple-macosx"},
       {MO::CPU_TYPE_X86_64, "x86_64-apple-macosx"}}};
  for (const auto &[CpuType, TripleName] : Targets) {
    SCOPED_TRACE(TripleName);
    const llvm::Triple Target(TripleName);
    for (unsigned Type = 0; Type <= 0xF; ++Type) {
      std::optional<bool> Graph =
          neverc::plugin::nativeRelocationFieldIsWholeBytes(Target, Type);
      const bool Merge =
          neverc::merge::detail::machOFieldIsWholeWord(CpuType, Type);
      ASSERT_TRUE(Graph.has_value()) << "type " << Type;
      EXPECT_EQ(*Graph, Merge) << "the two answers for relocation type " << Type
                               << " have drifted apart";
    }
  }
}

// ---------------------------------------------------------------------------
// Fuzz-style: random corruption of valid objects
// ---------------------------------------------------------------------------

TEST(MergeELF, FuzzCorruptedHeaders) {
  // Take a valid ELF, corrupt random bytes, verify no crash
  std::mt19937 Rng(42);
  auto Base = buildMinimalELF({"func"}, {"ext"});

  for (int Trial = 0; Trial < 200; ++Trial) {
    auto Copy = Base;
    unsigned NumCorruptions = 1 + (Rng() % 5);
    for (unsigned c = 0; c < NumCorruptions; ++c) {
      unsigned Pos = Rng() % Copy.size();
      Copy[Pos] = Rng() & 0xFF;
    }
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(Copy));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeELF64LEObjects(Bufs, OS); // must not crash
  }
}

TEST(MergeELF, FuzzRandomGarbage) {
  // Completely random buffers — parser should reject, not crash
  std::mt19937 Rng(1337);
  for (int Trial = 0; Trial < 100; ++Trial) {
    SmallVector<char, 0> Garbage;
    unsigned Sz = 64 + (Rng() % 4096);
    Garbage.resize(Sz);
    for (unsigned i = 0; i < Sz; ++i)
      Garbage[i] = Rng() & 0xFF;

    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(Garbage));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeELF64LEObjects(Bufs, OS); // must not crash
  }
}

TEST(MergeELF, FuzzTwoCorruptedPartitions) {
  std::mt19937 Rng(9999);
  auto Base0 = buildMinimalELF({"a", "b"}, {"c"});
  auto Base1 = buildMinimalELF({"c"}, {"a", "b"});

  for (int Trial = 0; Trial < 100; ++Trial) {
    auto C0 = Base0, C1 = Base1;
    // Corrupt one of the two
    auto &Target = (Rng() % 2 == 0) ? C0 : C1;
    unsigned Pos = Rng() % Target.size();
    Target[Pos] = Rng() & 0xFF;

    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(C0));
    Bufs.push_back(std::move(C1));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeELF64LEObjects(Bufs, OS); // must not crash
  }
}

// Regression for the recurring nightly merge-fuzz crash: a relocation section
// (SHT_RELA/SHT_REL) whose sh_info — the target section index, read straight
// from the input — is one of llvm::DenseMap's reserved sentinels (~0u empty key
// or ~0u-1 tombstone).  The merger fed sh_info directly into a
// DenseSet<unsigned> (SectionHasRelocTarget), and inserting a reserved key
// trips DenseMap's "Empty/Tombstone value shouldn't be inserted" assertion
// (abort under LLVM_ENABLE_ASSERTIONS, undefined behavior otherwise).
// buildMinimalELF emits a .rela.text section for its undefined symbol; we
// repoint that section's sh_info at each reserved key and require the merge to
// refuse-or-succeed without crashing.  ELFObjectFile::create does not
// re-validate the magic, so this is reached on exactly the kind of object the
// fuzzer synthesized.
TEST(MergeELF, FuzzRelocSectionReservedShInfo) {
  using namespace ELF;
  auto patchFirstRelocShInfo = [](SmallVectorImpl<char> &Buf,
                                  uint32_t NewInfo) -> bool {
    if (Buf.size() < sizeof(Elf64_Ehdr))
      return false;
    auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
    if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
      return false;
    auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
    for (unsigned i = 0; i < H->e_shnum; ++i)
      if (Secs[i].sh_type == SHT_RELA || Secs[i].sh_type == SHT_REL) {
        Secs[i].sh_info = NewInfo;
        return true;
      }
    return false;
  };

  for (uint32_t Reserved : {~0u, ~0u - 1u}) {
    auto Obj = buildMinimalELF({"func"}, {"ext"});
    ASSERT_TRUE(patchFirstRelocShInfo(Obj, Reserved))
        << "test object has no relocation section to patch";
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(Obj));
    for (bool Verify : {true, false}) {
      Options Opts;
      Opts.verify = Verify;
      SmallVector<char, 0> Out;
      raw_svector_ostream OS(Out);
      (void)mergeELF64LEObjects(Bufs, OS,
                                Opts); // must not crash (was an abort)
    }
  }
}

// ---------------------------------------------------------------------------
// Edge-case tests: dispatch layer
// ---------------------------------------------------------------------------

TEST(MergeDispatch, InvalidFormatNoCrash) {
  auto Elf = buildMinimalELF({"x"}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Elf));

  {
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    // Feed an ELF to the MachO merger — should return false, not crash
    bool OK = mergeObjects(Bufs, OS, Format::MachO64);
    EXPECT_FALSE(OK);
  }
  {
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    // Feed an ELF to the COFF merger — should return false, not crash
    bool OK = mergeObjects(Bufs, OS, Format::COFF);
    EXPECT_FALSE(OK);
  }
}

TEST(MergeDispatch, EmptyInputAllFormats) {
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  for (auto Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF}) {
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeObjects(Bufs, OS, Fmt); // must not crash
  }
}

// ---------------------------------------------------------------------------
// Fuzz-style: random garbage to all three merger formats
// ---------------------------------------------------------------------------

TEST(MergeFuzz, AllFormatsRandomGarbage) {
  std::mt19937 Rng(0xDEAD);
  for (auto Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF}) {
    for (int Trial = 0; Trial < 50; ++Trial) {
      SmallVector<char, 0> Garbage;
      unsigned Sz = 128 + (Rng() % 2048);
      Garbage.resize(Sz);
      for (unsigned i = 0; i < Sz; ++i)
        Garbage[i] = Rng() & 0xFF;

      SmallVector<SmallVector<char, 0>, 2> Bufs;
      Bufs.push_back(std::move(Garbage));
      SmallVector<char, 0> Out;
      raw_svector_ostream OS(Out);
      mergeObjects(Bufs, OS, Fmt); // must not crash
    }
  }
}

TEST(MergeFuzz, AllFormatsMultipleRandomGarbage) {
  std::mt19937 Rng(0xBEEF);
  for (auto Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF}) {
    for (int Trial = 0; Trial < 30; ++Trial) {
      unsigned NumBufs = 2 + (Rng() % 4);
      SmallVector<SmallVector<char, 0>, 8> Bufs;
      for (unsigned b = 0; b < NumBufs; ++b) {
        SmallVector<char, 0> G;
        unsigned Sz = 64 + (Rng() % 1024);
        G.resize(Sz);
        for (unsigned i = 0; i < Sz; ++i)
          G[i] = Rng() & 0xFF;
        Bufs.push_back(std::move(G));
      }
      SmallVector<char, 0> Out;
      raw_svector_ostream OS(Out);
      mergeObjects(Bufs, OS, Fmt); // must not crash
    }
  }
}

#ifdef TEST_SOURCE_DIR
// Split a corpus blob into 1..8 sub-buffers exactly the way the libFuzzer entry
// (MergeFuzzer.cpp) does, so a saved crash artifact reproduces byte-for-byte.
static std::vector<SmallVector<char, 0>> carveCorpus(ArrayRef<uint8_t> Data) {
  std::vector<SmallVector<char, 0>> Bufs;
  size_t Pos = 0, Size = Data.size();
  while (Pos + 2 <= Size && Bufs.size() < 8) {
    size_t Len = (size_t)Data[Pos] | ((size_t)Data[Pos + 1] << 8);
    Pos += 2;
    Len = std::min(Len, Size - Pos);
    SmallVector<char, 0> B;
    B.append(reinterpret_cast<const char *>(Data.data() + Pos),
             reinterpret_cast<const char *>(Data.data() + Pos + Len));
    Bufs.push_back(std::move(B));
    Pos += Len;
  }
  if (Bufs.empty()) {
    SmallVector<char, 0> B;
    B.append(reinterpret_cast<const char *>(Data.data()),
             reinterpret_cast<const char *>(Data.data() + Size));
    Bufs.push_back(std::move(B));
  }
  return Bufs;
}

// Replays the merge-fuzzer crash artifacts that exposed (and now guard against)
// real memory-safety bugs in the merger/verifier. The corpus covers malformed
// COFF records and names, reserved DenseMap keys, invalid ELF virtual sizes,
// eager Mach-O parser allocations, misaligned reads, dangling names, and huge
// file-less sections.
// Each is carved exactly as the fuzzer does and pushed through every format
// with verify on and off, plus the ELF kernel-module section-folding path.  The
// invariant is simply "the merger never crashes on these inputs" — most
// meaningful in the sanitizer (ASan/UBSan) build, a fast smoke test otherwise.
// Keeping the artifacts in-tree turns one-off fuzzer finds into permanent CI
// regression coverage without needing the fuzzer harness itself.
TEST(MergeFuzzCorpus, NoCrashOnSavedRegressions) {
  const char *Names[] = {
      "coff-invalid-section-name-unchecked",
      "coff-pe-lfanew-oob-read",
      "coff-reloc-reserved-densekey",
      "coff-reloc-symbol-oob",
      "coff-symbol-record-oob",
      "elf-nobits-shsize-alloc-oom",
      "elf-reloc-shinfo-reserved-densekey",
      "macho-huge-ncmds-oom",
      "macho-nlist-misaligned",
      "macho-section-name-uaf",
      "macho-zerofill-size-alloc-oom",
  };
  for (const char *N : Names) {
    SmallString<256> Path(TEST_SOURCE_DIR);
    sys::path::append(Path, "merge-corpus", N);
    auto BufOrErr = MemoryBuffer::getFile(Path);
    ASSERT_TRUE((bool)BufOrErr)
        << "missing merge regression corpus file: " << Path.c_str();
    StringRef Data = (*BufOrErr)->getBuffer();
    auto Bufs = carveCorpus(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Data.data()), Data.size()));
    for (Format Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF})
      for (bool Verify : {true, false}) {
        Options Opts;
        Opts.verify = Verify;
        SmallVector<char, 0> Out;
        raw_svector_ostream OS(Out);
        mergeObjects(Bufs, OS, Fmt, Opts); // must not crash
      }
    Options Folded;
    Folded.mergeSections = true;
    Folded.preservedSections = {".modinfo", "__versions"};
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeObjects(Bufs, OS, Format::ELF64LE, Folded); // must not crash
  }
}

TEST(MergeFuzzCorpus, RejectsHugeCompressedELFWithoutAllocation) {
  // Exact crash-c01ac293... artifact from the nightly merge-fuzz job. A tiny
  // malformed input reached the ELF verifier with an SHF_COMPRESSED section
  // declaring a 0xffffffff1100-byte logical size, which made zstd decompression
  // attempt that allocation before validating the payload.
  const uint8_t Crash[] = {
      0x23, 0x0a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
      0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x16, 0xfa, 0xce, 0x05, 0x00, 0x00,
      0x00, 0x02, 0x18, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x00,
      0x04, 0x19, 0x00, 0x00, 0x00, 0x88, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x4f, 0x00,
      0x40, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x5f, 0x5f, 0x74, 0x65, 0x78, 0x74, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x5f, 0x5f, 0x33, 0x45,
      0x10, 0x58, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x00, 0x00, 0x66, 0x75,
      0x65, 0x66, 0x49, 0x11};
  auto Bufs = carveCorpus(Crash);

  for (bool Verify : {true, false}) {
    Options Opts;
    Opts.verify = Verify;
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_FALSE(mergeObjects(Bufs, OS, Format::ELF64LE, Opts))
        << "Verify=" << Verify;
  }
}
#endif // TEST_SOURCE_DIR

// ===========================================================================
// Differential tests against the bundled LLD relocatable linker (`-r`).
//
// The synthetic suites above prove the merger and its self-verifier agree on
// hand-built objects.  This suite raises the bar: it compiles *real* objects
// with the bundled frontend, then uses the bundled, battle-tested LLD `-r`
// path as an INDEPENDENT producer of a known-good relocatable merge of the
// exact same inputs.  Both the in-process merger's output and LLD's output
// must satisfy verifyMerge(), which anchors every output symbol/relocation
// back to the input bytes it came from.  Consequences:
//
//   * verifyMerge() must never false-reject a real, correct merge — a false
//     reject would make the auto-LTO pipeline spuriously fall back to serial
//     codegen on perfectly good objects.  Feeding it LLD's output proves this
//     on a second, independent linker.
//   * Because both outputs are proven faithful to the *same* inputs, they are
//     transitively semantically equivalent — without a brittle byte/structure
//     diff between two linkers that legitimately lay sections out differently.
//   * The historical offset-collapse bug is shown to be caught on real,
//     linker-shaped objects, not just synthetic ones.
//
// Only ELF and MachO are exercised: the bundled COFF driver requires a full PE
// link (it errors "subsystem must be defined") and exposes no clean `-r`, so
// COFF keeps the synthetic verify suite above.  Any target whose toolchain is
// unavailable in the running environment GTEST_SKIPs instead of failing, so the
// suite contributes coverage wherever it can (e.g. ELF+MachO on a macOS dev
// box, ELF on Linux CI) and never breaks a host that lacks a cross sysroot.
// ===========================================================================
#ifdef NEVERC_BINARY
namespace {

// A unique scratch directory that removes itself on scope exit.
struct ScratchDir {
  SmallString<128> Path;
  bool Ok = false;
  ScratchDir() { Ok = !sys::fs::createUniqueDirectory("nvk-merge-diff", Path); }
  ~ScratchDir() {
    if (Ok)
      sys::fs::remove_directories(Path);
  }
  std::string file(const Twine &Name) const {
    SmallString<160> P(Path);
    sys::path::append(P, Name);
    return std::string(P.str());
  }
};

// Spawn the bundled neverc with Args (argv[0] is prepended automatically).
// stdout+stderr are routed to a scratch log so a skipped/failed cross-compile
// does not pollute test output.  Returns the child exit code, or -1 if the
// process could not be launched at all.
int runNeverc(const ScratchDir &Dir, ArrayRef<StringRef> Args) {
  SmallVector<StringRef, 24> Argv;
  Argv.push_back(StringRef(NEVERC_BINARY));
  Argv.append(Args.begin(), Args.end());
  std::string LogPath = Dir.file("spawn.log");
  // {stdin, stdout, stderr}: null StringRef = inherit, real path = redirect.
  StringRef Redirects[3] = {StringRef(), StringRef(LogPath),
                            StringRef(LogPath)};
  bool Failed = false;
  int RC = sys::ExecuteAndWait(StringRef(NEVERC_BINARY), Argv, /*Env=*/{},
                               Redirects, /*SecondsToWait=*/120,
                               /*MemoryLimit=*/0, /*ErrMsg=*/nullptr, &Failed);
  return Failed ? -1 : RC;
}

bool readObj(StringRef Path, SmallVectorImpl<char> &Out) {
  auto MB = MemoryBuffer::getFile(Path, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false);
  if (!MB)
    return false;
  StringRef D = (*MB)->getBuffer();
  Out.assign(D.begin(), D.end());
  return !Out.empty();
}

// Compile `Src` to a real (non-bitcode) relocatable object for `Target`.
// `-fno-lto` is essential: the default auto-LTO path emits a bitcode wrapper,
// not the machine-code object the merger consumes.
bool compileRealObj(const ScratchDir &Dir, StringRef Stem, StringRef Src,
                    StringRef Target, std::string &ObjPath,
                    SmallVectorImpl<char> &Bytes) {
  std::string CPath = Dir.file(Stem + ".c");
  {
    std::error_code EC;
    raw_fd_ostream OS(CPath, EC);
    if (EC)
      return false;
    OS << Src;
  }
  ObjPath = Dir.file(Stem + ".o");
  SmallVector<StringRef, 12> Args;
  if (!Target.empty()) {
    Args.push_back("-target");
    Args.push_back(Target);
  }
  Args.push_back("-fno-lto");
  // The allocator has no bearing on where the merger puts a symbol, but
  // injecting it hands every one of these small modules several hundred more
  // functions to codegen -- and -fno-lto pays that per translation unit.
  Args.push_back("-fno-builtin-mimalloc");
  Args.push_back("-O2");
  Args.push_back("-c");
  Args.push_back(CPath);
  Args.push_back("-o");
  Args.push_back(ObjPath);
  if (runNeverc(Dir, Args) != 0)
    return false;
  return readObj(ObjPath, Bytes);
}

// Relocatable (`-r`) merge of ObjPaths via the bundled LLD.
bool lldRelocatable(const ScratchDir &Dir, ArrayRef<std::string> ObjPaths,
                    StringRef Target, SmallVectorImpl<char> &Out) {
  std::string OutPath = Dir.file("lld_r.o");
  SmallVector<StringRef, 12> Args;
  if (!Target.empty()) {
    Args.push_back("-target");
    Args.push_back(Target);
  }
  Args.push_back("-r");
  for (const std::string &P : ObjPaths)
    Args.push_back(P);
  Args.push_back("-o");
  Args.push_back(OutPath);
  if (runNeverc(Dir, Args) != 0)
    return false;
  return readObj(OutPath, Out);
}

// Two translation units whose merge stresses the offset arithmetic that
// historically collapsed: several symbols share one .text (default codegen,
// no -ffunction-sections), so B's functions land at a non-zero offset past
// A's, and the cross-TU calls emit relocations whose offsets must re-land
// exactly at their shifted positions.  Beyond plain int code + a global array
// + a .bss array, the bodies deliberately exercise the section/relocation
// shapes most prone to offset bugs, each landing in its own merge-compatible
// output section so the merger's per-section offset tracking is tested broadly:
//   * double constants  -> a .rodata constant pool referenced PC/section-rel
//   * string literals    -> .rodata.str (SHF_MERGE|SHF_STRINGS) of differing
//   len
//   * a static const table indexed at runtime -> .rodata + section-relative
//   reloc
//   * a const function-pointer table          -> .data.rel.ro with absolute
//                                                relocations onto both a global
//                                                and a static (local) function
// LLD `-r` and the in-process merger must agree (via verifyMerge) on every one.
static const char DiffSrcA[] =
    "int shared_helper(int x){ return x*3+1; }\n"
    "static int a_local(int x){ return (x^0x5a5a) + shared_helper(x); }\n"
    "int a_entry(int x){ return a_local(x) + shared_helper(x*2); }\n"
    "int a_data[4] = {11,22,33,44};\n"
    "double a_scale(double x){ return x*3.14159265358979 + 2.71828; }\n"
    "const char *a_name(void){ return \"neverc-merger-A\"; }\n"
    "static const int a_tbl[6] = {2,3,5,7,11,13};\n"
    "int a_pick(int i){ return a_tbl[(unsigned)i % 6u]; }\n"
    "static int (*const a_fns[2])(int) = {a_entry, a_local};\n"
    "int a_dispatch(int i, int x){ return a_fns[i & 1](x); }\n";
static const char DiffSrcB[] =
    "int shared_helper(int);\n"
    "int a_entry(int);\n"
    "int b_entry(int x){ return shared_helper(x) + a_entry(x) + 9; }\n"
    "int b_more(int x){ return b_entry(x) ^ a_entry(x + 1); }\n"
    "long b_bss[8];\n"
    "double b_mix(double x){ return x*1.4142135623 - 0.5772156649; }\n"
    "const char *b_name(void){ return \"neverc-merger-B-longer-string\"; }\n"
    "static const long b_tbl[4] = {100,200,300,400};\n"
    "long b_pick(int i){ return b_tbl[(unsigned)i & 3u]; }\n";

// Shared body: compile the two TUs for `Target`, then assert the merger and
// the bundled LLD `-r` both produce merges the verifier accepts, and that a
// collapsed-offset corruption of LLD's real output is rejected.
void runLldDifferential(StringRef Target, Format Fmt) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  std::string PA, PB;
  SmallVector<char, 0> OA, OB;
  if (!compileRealObj(Dir, "ta", DiffSrcA, Target, PA, OA) ||
      !compileRealObj(Dir, "tb", DiffSrcB, Target, PB, OB))
    GTEST_SKIP() << "frontend for target '" << Target.str()
                 << "' unavailable in this environment";

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(OA);
  Inputs.push_back(OB);

  // (1) The in-process merger's output is a faithful merge of the inputs.
  SmallVector<char, 0> NvkOut;
  {
    raw_svector_ostream OS(NvkOut);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt))
        << "merger failed on real objects for target '" << Target.str() << "'";
  }
  std::string NvkErr;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(NvkOut), Fmt, {}, &NvkErr))
      << "verifier rejected the merger's own output: " << NvkErr;

  // (2) The bundled LLD `-r` merge of the SAME inputs must ALSO verify.  This
  //     is the differential heart: an independent linker's correct relocatable
  //     output proves verifyMerge() does not false-reject real merges, and
  //     hence that the merger's output is semantically equivalent to LLD's.
  SmallVector<char, 0> LldOut;
  if (!lldRelocatable(Dir, {PA, PB}, Target, LldOut))
    GTEST_SKIP() << "bundled -r unavailable for target '" << Target.str()
                 << "'";
  std::string LldErr;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(LldOut), Fmt, {}, &LldErr))
      << "verifier false-rejected the bundled LLD -r output — either verify is "
         "too strict or it fails to model a real -r transform: "
      << LldErr;

  // (3) Collapsing every relocation offset in LLD's known-good output (the
  //     reloc half of the historical bug) must be caught on this real,
  //     linker-shaped object.  Only asserted when the patch actually changed
  //     bytes, so a layout where every offset was already 0 cannot misfire.
  SmallVector<char, 0> Collapsed(LldOut.begin(), LldOut.end());
  bool Patched = (Fmt == Format::ELF64LE)
                     ? patchAllRelaOffsets(Collapsed, 0)
                     : patchAllMachoRelocAddrs(Collapsed, 0);
  bool ReallyChanged =
      Patched && Collapsed.size() == LldOut.size() &&
      std::memcmp(Collapsed.data(), LldOut.data(), LldOut.size()) != 0;
  if (ReallyChanged) {
    std::string CErr;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                             ArrayRef<char>(Collapsed), Fmt, {}, &CErr))
        << "verifier accepted a collapsed-offset corruption of real LLD output";
  }
}

// ===========================================================================
// End-to-end execution equivalence (native host only).
//
// verifyMerge proves the merged object is *structurally* faithful; the LLD
// differential proves verify does not false-reject.  This raises the bar one
// final notch: it LINKS the in-process merger's output into a real executable
// and RUNS it, asserting byte-identical stdout to a plain link of the same
// inputs.  It is the execution-level analogue of the .ko insmod check — the
// historical offset-collapse bug would surface here as wrong output or a crash,
// fully automated, no device required.  Native target only (we execute it).
// ===========================================================================

bool writeBytes(StringRef Path, ArrayRef<char> Bytes) {
  std::error_code EC;
  raw_fd_ostream OS(Path, EC);
  if (EC)
    return false;
  OS.write(Bytes.data(), Bytes.size());
  OS.flush();
  return !OS.has_error();
}

// Link object files into a native executable with the bundled driver.
bool linkExe(const ScratchDir &Dir, ArrayRef<std::string> Objs,
             StringRef OutExe) {
  SmallVector<StringRef, 8> Args;
  for (const std::string &O : Objs)
    Args.push_back(O);
  Args.push_back("-o");
  Args.push_back(OutExe);
  return runNeverc(Dir, Args) == 0;
}

// Run an executable, capturing stdout into Out.  Returns the exit code, or -1
// if the process could not be launched.
int runExeCapture(const ScratchDir &Dir, StringRef Exe, std::string &Out) {
  std::string OutPath = Dir.file("run.out");
  StringRef Redirects[3] = {StringRef(), StringRef(OutPath), StringRef()};
  bool Failed = false;
  int RC = sys::ExecuteAndWait(Exe, {Exe}, /*Env=*/{}, Redirects,
                               /*SecondsToWait=*/60, /*MemoryLimit=*/0,
                               /*ErrMsg=*/nullptr, &Failed);
  if (Failed)
    return -1;
  SmallVector<char, 0> Bytes;
  if (readObj(OutPath, Bytes))
    Out.assign(Bytes.begin(), Bytes.end());
  return RC;
}

// A main TU that calls across the A/B merge boundary in many shapes (direct
// calls, a dispatch through a function-pointer table, .rodata table reads, and
// string-literal returns), so a mis-placed symbol or relocation changes the
// printed checksum or crashes.
static const char DiffSrcMain[] =
    "#include <stdio.h>\n"
    "int a_entry(int); int a_dispatch(int,int); int a_pick(int);\n"
    "int b_entry(int); int b_more(int); long b_pick(int);\n"
    "const char *a_name(void); const char *b_name(void);\n"
    "int main(void){\n"
    "  long acc=0;\n"
    "  for(int i=0;i<50;i++)\n"
    "    acc += a_entry(i)+b_entry(i)+a_dispatch(i&1,i)+a_pick(i)\n"
    "         + (int)b_pick(i)+b_more(i);\n"
    "  printf(\"%ld|%s|%s\\n\", acc, a_name(), b_name());\n"
    "  return 0;\n"
    "}\n";

void runMergedExecutionEquivalence(Format Fmt) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  std::string PA, PB, PMain;
  SmallVector<char, 0> OA, OB, OMain;
  if (!compileRealObj(Dir, "ea", DiffSrcA, /*Target=*/"", PA, OA) ||
      !compileRealObj(Dir, "eb", DiffSrcB, /*Target=*/"", PB, OB) ||
      !compileRealObj(Dir, "emain", DiffSrcMain, /*Target=*/"", PMain, OMain))
    GTEST_SKIP() << "native frontend unavailable in this environment";

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(OA);
  Inputs.push_back(OB);

  SmallVector<char, 0> Merged;
  {
    raw_svector_ostream OS(Merged);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt))
        << "merger failed on real objects";
  }
  // The merge must still self-verify, then prove itself at runtime.
  std::string VErr;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(Merged), Fmt, {}, &VErr))
      << VErr;

  std::string MergedPath = Dir.file("merged.o");
  ASSERT_TRUE(writeBytes(MergedPath, Merged));

  std::string ExeMerged = Dir.file("exe_merged");
  std::string ExePlain = Dir.file("exe_plain");
  if (!linkExe(Dir, {MergedPath, PMain}, ExeMerged))
    GTEST_SKIP() << "native link of the merged object unavailable";
  ASSERT_TRUE(linkExe(Dir, {PA, PB, PMain}, ExePlain))
      << "plain link of the same inputs failed";

  std::string OutMerged, OutPlain;
  int RCm = runExeCapture(Dir, ExeMerged, OutMerged);
  int RCp = runExeCapture(Dir, ExePlain, OutPlain);
  ASSERT_EQ(RCp, 0) << "plain-link executable did not exit cleanly";
  ASSERT_EQ(RCm, 0) << "merged-object executable did not exit cleanly (the "
                       "merge produced a loadable but wrong object)";
  EXPECT_FALSE(OutMerged.empty());
  EXPECT_EQ(OutMerged, OutPlain)
      << "merged-object program output diverged from the plain link — the "
         "merge mis-placed a symbol or relocation";
}

// ===========================================================================
// Duplicate-named-static `-r` execution differential.
//
// Every generator above gives each module globally-unique symbol names, so the
// merged object's symbol table has no name collisions and the verifier can
// content-anchor every defined symbol.  Real translation units are not like
// that: each file has its own file-local `static int cmp`, `static char buf[]`,
// `static const ... tab[]`, etc., so merging real .o files (the linker's `-r`
// path) produces MANY local symbols that share a base name.  Those are the
// merger's least-anchored symbols — the self-verifier skips content-anchoring a
// name that is not unique and falls back to the weaker disjoint-interval /
// relative-displacement invariants — and they are produced only on the `-r`
// path (the auto-LTO path IR-merges first, so IRMover uniquifies the names
// before codegen).  This test compiles several modules that each define the
// SAME-named statics (.text helper, .data table, .bss scratch, .rodata
// constants) with module-specific values, links them two ways, and proves the
// merged-object run matches the plain link.  Because each module's exported
// entry reads/writes only its OWN statics, a merge that mis-remaps one
// duplicate-named local's symbol index or mis-shifts its offset makes an entry
// touch the wrong copy and the printed checksum diverges — catching exactly the
// class the unique-name content anchor cannot see.
std::string genDupStaticObj(unsigned Idx, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  // Statics with IDENTICAL names across every module, module-specific values.
  OS << "static uint64_t s_tab[4] = {";
  for (unsigned j = 0; j < 4; ++j)
    OS << (j ? "," : "")
       << ((uint64_t)(Idx * 4u + j) * 0x9e3779b97f4a7c15ULL + 1u) << "ULL";
  OS << "};\n";
  OS << "static uint64_t s_bss[4];\n";
  OS << "static const uint64_t s_ro[4] = {";
  for (unsigned j = 0; j < 4; ++j)
    OS << (j ? "," : "") << ((uint64_t)(Idx * 4u + j) * 0x100000001b3ULL + 7u)
       << "ULL";
  OS << "};\n";
  OS << "__attribute__((noinline)) static uint64_t s_mix(uint64_t x){\n"
     << "  uint64_t a = x ^ s_ro[x & 3] ^ " << (Idx * 131u + 1u) << "ULL;\n"
     << "  for (int i=0;i<9;i++){\n"
     << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
     << "    a ^= s_tab[i & 3];\n"
     << "  }\n"
     << "  s_bss[x & 3] += a;\n"
     << "  return a ^ s_bss[(x + 1) & 3];\n}\n";
  // Unique exported entries; each touches only its own module's statics.
  for (unsigned f = 0; f < NumFns; ++f)
    OS << "uint64_t dent_" << Idx << "_" << f << "(uint64_t x){\n"
       << "  return s_mix(x + " << f
       << ") ^ s_tab[x & 3] ^ s_bss[(x >> 2) & 3];"
       << "\n}\n";
  return S;
}

std::string genDupStaticMain(unsigned NumMods, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "uint64_t dent_" << m << "_" << f << "(uint64_t);\n";
  OS << "int main(void){\n  uint64_t s = 0;\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "  s += dent_" << m << "_" << f << "(" << (i++) << "ULL);\n";
  OS << "  printf(\"%llu\\n\", (unsigned long long)s);\n  return 0;\n}\n";
  return S;
}

void runDupStaticRMergeEquivalence(Format Fmt) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  const unsigned NumMods = 6, NumFns = 3;
  SmallVector<std::string, 8> ObjPaths;
  SmallVector<SmallVector<char, 0>, 8> Inputs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string P;
    SmallVector<char, 0> O;
    if (!compileRealObj(Dir, ("ds" + Twine(m)).str(),
                        genDupStaticObj(m, NumFns),
                        /*Target=*/"", P, O))
      GTEST_SKIP() << "native frontend unavailable in this environment";
    ObjPaths.push_back(P);
    Inputs.push_back(std::move(O));
  }
  std::string PMain;
  SmallVector<char, 0> OMain;
  if (!compileRealObj(Dir, "dsmain", genDupStaticMain(NumMods, NumFns),
                      /*Target=*/"", PMain, OMain))
    GTEST_SKIP() << "native frontend unavailable in this environment";

  // `-r`-style merge of the module objects (NOT main, which stays a separate
  // input to the final link, exactly like a real partial-link build).
  SmallVector<char, 0> Merged;
  {
    raw_svector_ostream OS(Merged);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt))
        << "merger failed on duplicate-named-static objects";
  }
  std::string VErr;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(Merged), Fmt, {}, &VErr))
      << "verifier rejected the merge of duplicate-named statics: " << VErr;

  std::string MergedPath = Dir.file("ds_merged.o");
  ASSERT_TRUE(writeBytes(MergedPath, Merged));

  std::string ExeMerged = Dir.file("ds_exe_merged");
  std::string ExePlain = Dir.file("ds_exe_plain");
  if (!linkExe(Dir, {MergedPath, PMain}, ExeMerged))
    GTEST_SKIP() << "native link of the merged object unavailable";
  SmallVector<std::string, 8> PlainObjs(ObjPaths.begin(), ObjPaths.end());
  PlainObjs.push_back(PMain);
  ASSERT_TRUE(linkExe(Dir, PlainObjs, ExePlain))
      << "plain link of the same inputs failed";

  std::string OutMerged, OutPlain;
  int RCm = runExeCapture(Dir, ExeMerged, OutMerged);
  int RCp = runExeCapture(Dir, ExePlain, OutPlain);
  ASSERT_EQ(RCp, 0) << "plain-link executable did not exit cleanly";
  ASSERT_EQ(RCm, 0)
      << "merged-object executable did not exit cleanly (the merge "
         "produced a loadable but wrong object)";
  EXPECT_FALSE(OutMerged.empty());
  EXPECT_EQ(OutMerged, OutPlain)
      << "merged-object program output diverged from the plain link — the "
         "merge "
         "mis-remapped a duplicate-named local static's symbol index or "
         "mis-shifted its offset (the case the unique-name content anchor in "
         "verifyMerge cannot see)";
}

// ===========================================================================
// Randomized cross-module execution differential ("the fuzzer").
//
// The fixed DiffSrc* suite above proves one hand-tuned shape; this generates
// *randomized* multi-module programs and runs the same merge-vs-plain-link
// execution equivalence on each, so unknown offset/relocation blind spots are
// driven out instead of having to be foreseen.  Every seed emits N modules
// whose objects exercise the merger's whole risk surface at once:
//   * many functions per .text (default codegen, no -ffunction-sections), so
//     later functions land at non-zero merged offsets;
//   * cross-module calls to a shared symbol and to module 0's first function,
//     forcing the global-symbol dedup + relocation remap paths;
//   * an initialized array (.data) and an *uninitialized* array (.bss) per
//     module that the bodies read AND write, so a collapsed .bss offset (the
//     class P0's verifier hardening targets) becomes a wrong runtime checksum.
// The merged object must self-verify, load, and print byte-identical output to
// a plain link of the same objects — because both link the identical compiled
// bodies, any divergence is unambiguously a merge bug.  Deterministic seeds
// keep it reproducible in CI; the native frontend/link gate it via GTEST_SKIP.
// ===========================================================================

std::string genFuzzModule(unsigned Seed, unsigned Idx, unsigned NumFns,
                          unsigned ArrLen) {
  std::mt19937 R(Seed * 7919u + Idx);
  std::string S;
  raw_string_ostream OS(S);
  OS << "int shared(int);\n";
  if (Idx != 0)
    OS << "int mod0_f0(int);\n";
  else
    OS << "int shared(int x){ return x * 3 + 1; }\n";
  OS << "int g" << Idx << "[" << ArrLen << "] = {";
  for (unsigned k = 0; k < ArrLen; ++k)
    OS << (k ? "," : "") << (int)(R() % 97);
  OS << "};\n";
  OS << "long b" << Idx << "[" << ArrLen << "];\n";
  for (unsigned f = 0; f < NumFns; ++f) {
    OS << "int mod" << Idx << "_f" << f << "(int x){ unsigned u=(unsigned)x; ";
    OS << "int t = g" << Idx << "[u % " << ArrLen << "u]; ";
    if (f == 0)
      OS << "t += shared(x)";
    else
      OS << "t += mod" << Idx << "_f" << (f - 1) << "(x & 0x3ff)";
    if (Idx != 0)
      OS << " + mod0_f0(x & 0x1ff)";
    for (int op = 0; op < 3; ++op) {
      switch (R() % 4) {
      case 0:
        OS << " + " << (int)(R() % 1000);
        break;
      case 1:
        OS << " ^ " << (int)(R() % 255);
        break;
      case 2:
        OS << " + (int)(u >> " << (1 + R() % 7) << ")";
        break;
      default:
        OS << " - " << (int)(R() % 500);
        break;
      }
    }
    OS << "; return t; }\n";
  }
  OS << "long mod" << Idx << "_sum(int x){ long s = 0; ";
  for (unsigned f = 0; f < NumFns; ++f)
    OS << "s += mod" << Idx << "_f" << f << "(x + " << f << "); ";
  OS << "b" << Idx << "[(unsigned)x % " << ArrLen << "u] = s; ";
  OS << "s += b" << Idx << "[(unsigned)(x + 1) % " << ArrLen << "u]; ";
  OS << "return s; }\n";
  return S;
}

std::string genFuzzMain(unsigned NumMods) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdio.h>\n";
  for (unsigned i = 0; i < NumMods; ++i)
    OS << "long mod" << i << "_sum(int);\n";
  OS << "int main(void){ long acc = 0; for (int i = 0; i < 40; i++){ ";
  for (unsigned i = 0; i < NumMods; ++i)
    OS << "acc += mod" << i << "_sum(i + " << i << "); ";
  OS << "} printf(\"%ld\\n\", acc); return 0; }\n";
  return S;
}

void runMergeFuzzExecution(Format Fmt, unsigned Seed) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";
  std::mt19937 R(Seed);
  unsigned NumMods = 2 + (R() % 3); // 2..4 mergeable modules

  SmallVector<std::string, 6> ObjPaths;
  SmallVector<SmallVector<char, 0>, 6> Inputs;
  for (unsigned i = 0; i < NumMods; ++i) {
    unsigned NumFns = 3 + (R() % 5); // 3..7 functions
    unsigned ArrLen = 4 + (R() % 12);
    std::string Src = genFuzzModule(Seed, i, NumFns, ArrLen);
    std::string P;
    SmallVector<char, 0> O;
    if (!compileRealObj(Dir, ("m" + Twine(i)).str(), Src, /*Target=*/"", P, O))
      GTEST_SKIP() << "native frontend unavailable in this environment";
    ObjPaths.push_back(P);
    Inputs.push_back(std::move(O));
  }
  std::string PMain;
  SmallVector<char, 0> OMain;
  if (!compileRealObj(Dir, "mmain", genFuzzMain(NumMods), /*Target=*/"", PMain,
                      OMain))
    GTEST_SKIP() << "native frontend unavailable in this environment";

  SmallVector<char, 0> Merged;
  {
    raw_svector_ostream OS(Merged);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt))
        << "seed " << Seed << ": merger failed on " << NumMods << " modules";
  }
  std::string VErr;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(Merged), Fmt, {}, &VErr))
      << "seed " << Seed << ": " << VErr;

  std::string MergedPath = Dir.file("merged.o");
  ASSERT_TRUE(writeBytes(MergedPath, Merged));
  std::string ExeMerged = Dir.file("exe_merged");
  std::string ExePlain = Dir.file("exe_plain");
  if (!linkExe(Dir, {MergedPath, PMain}, ExeMerged))
    GTEST_SKIP() << "native link of the merged object unavailable";
  SmallVector<std::string, 7> PlainObjs(ObjPaths.begin(), ObjPaths.end());
  PlainObjs.push_back(PMain);
  ASSERT_TRUE(linkExe(Dir, PlainObjs, ExePlain))
      << "seed " << Seed << ": plain link of the same inputs failed";

  std::string OutMerged, OutPlain;
  int RCm = runExeCapture(Dir, ExeMerged, OutMerged);
  int RCp = runExeCapture(Dir, ExePlain, OutPlain);
  ASSERT_EQ(RCp, 0) << "seed " << Seed << ": plain-link executable crashed";
  ASSERT_EQ(RCm, 0)
      << "seed " << Seed
      << ": merged-object executable crashed (loadable but wrong)";
  EXPECT_FALSE(OutMerged.empty());
  EXPECT_EQ(OutMerged, OutPlain)
      << "seed " << Seed
      << ": merged-object output diverged from the plain link — the merge "
         "mis-placed a symbol or relocation";
}

// ===========================================================================
// End-to-end guard for the parallel-codegen -> merger path under STRICT mode.
//
// Every suite above calls the merger in-process on -fno-lto objects.  This one
// drives the *default auto-LTO link*, which is the production path that
// partitions the post-IPO module, codegen's the partitions in parallel, and
// stitches them back with the in-process merger.  NEVERC_PCG_STRICT makes that
// path abort (non-zero exit) on any merge/self-verify failure instead of
// silently falling back to serial codegen — so a reintroduced offset-collapse
// bug becomes a hard CI failure here, rather than a build that merely compiles
// slower while quietly never exercising the merger.  Output is checked against
// a -fno-lto build of the identical sources, so a merge that loads but is wrong
// is also caught.  Native host only; POSIX only (injects the env knob via
// setenv, which MSVC lacks — Windows would GTEST_SKIP at runtime regardless).
// ===========================================================================
#ifndef _WIN32
// Restore an environment variable to its prior value on scope exit, so the
// strict-mode knob never leaks into other tests when the whole gtest binary is
// run in one process.
struct ScopedEnv {
  std::string Name;
  std::string Old;
  bool HadOld;
  ScopedEnv(const char *N, const char *V) : Name(N) {
    const char *Prev = ::getenv(N);
    HadOld = Prev != nullptr;
    if (HadOld)
      Old = Prev;
    ::setenv(N, V, 1);
  }
  ~ScopedEnv() {
    if (HadOld)
      ::setenv(Name.c_str(), Old.c_str(), 1);
    else
      ::unsetenv(Name.c_str());
  }
};

// Temporarily remove an environment variable, restoring it on scope exit.
// Needed because CI sets NEVERC_PCG_STRICT=1 globally (presence, not value,
// enables strict mode), yet the serial-fallback test must run with strict off
// so the forced merge failure is allowed to fall back instead of aborting.
struct ScopedUnsetEnv {
  std::string Name;
  std::string Old;
  bool HadOld;
  explicit ScopedUnsetEnv(const char *N) : Name(N) {
    const char *Prev = ::getenv(N);
    HadOld = Prev != nullptr;
    if (HadOld)
      Old = Prev;
    ::unsetenv(N);
  }
  ~ScopedUnsetEnv() {
    if (HadOld)
      ::setenv(Name.c_str(), Old.c_str(), 1);
  }
};

// One module of `NumFns` noinline, deliberately heavy functions.  noinline
// keeps them distinct after whole-program inlining, so the post-IPO module
// still clears the parallel thresholds (FuncCount>=8, TotalWeight>=10000) and
// the partitioner actually engages.
std::string genHeavyModule(unsigned Idx, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  for (unsigned f = 0; f < NumFns; ++f) {
    OS << "__attribute__((noinline)) uint64_t heavy_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a=x, b=x^0x9e3779b97f4a7c15ULL, c=" << (Idx * 131u + f)
       << "ULL;\n"
       << "  for (uint64_t i=0;i<61;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    b ^= (a>>13); b += (a<<7);\n"
       << "    c += (a^b) + (a&b) - (a|b);\n"
       << "    c = (c<<5) | (c>>59);\n"
       << "    a ^= c*0xff51afd7ed558ccdULL;\n"
       << "    b = b*0x100000001b3ULL ^ (c>>17);\n"
       << "  }\n"
       << "  return a^b^c;\n}\n";
  }
  return S;
}

std::string genHeavyMain(unsigned NumMods, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "uint64_t heavy_" << m << "_" << f << "(uint64_t);\n";
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f) {
      OS << "  s += heavy_" << m << "_" << f << "(" << i << "ULL+k) ^ heavy_"
         << m << "_" << f << "(" << (i * 7u + 1u) << "ULL+k);\n";
      i++;
    }
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}

// Compile+link several sources in ONE neverc invocation; the auto-LTO link
// happens here when -fno-lto is absent.  ExtraArgs (e.g. -fno-lto -O2) precede
// the sources.  Returns true only on a clean (exit 0) compile+link.
bool compileLinkMulti(const ScratchDir &Dir, ArrayRef<std::string> Srcs,
                      ArrayRef<StringRef> ExtraArgs, StringRef OutExe) {
  SmallVector<StringRef, 40> Args;
  // Same reasoning as compileRealObj: these builds are sized to engage the
  // partitioner on their own generated functions, and the -fno-lto reference
  // leg would otherwise codegen the allocator once per source file.
  Args.push_back("-fno-builtin-mimalloc");
  Args.append(ExtraArgs.begin(), ExtraArgs.end());
  for (const std::string &S : Srcs)
    Args.push_back(S);
  Args.push_back("-o");
  Args.push_back(OutExe);
  return runNeverc(Dir, Args) == 0;
}

// A module that, beyond heavy .text, defines a cross-module-referenced
// initialized global array (.data) and an uninitialized one (.bss), and whose
// functions both read and write them.  This is deliberately the shape the
// historical offset-collapse bug corrupted: in auto-LTO the partitioner pins
// every global *initializer* to partition 0 and references it as external from
// the others, so the merge must shift each global's symbol value and every
// cross-partition relocation into .data/.bss by exactly its merged section
// offset — the arithmetic that once collapsed to 0 and produced a loadable but
// wrong .ko.  noinline keeps the functions distinct post-IPO so the partitioner
// engages.
std::string genGlobalsModule(unsigned Idx, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  OS << "uint64_t gdata_" << Idx << "[8] = {";
  for (unsigned j = 0; j < 8; ++j) {
    uint64_t V = (uint64_t)(Idx * 8u + j) * 0x9e3779b97f4a7c15ULL + 1u;
    OS << (j ? "," : "") << V << "ULL";
  }
  OS << "};\n";
  OS << "uint64_t gbss_" << Idx << "[8];\n";
  for (unsigned f = 0; f < NumFns; ++f) {
    OS << "__attribute__((noinline)) uint64_t gfn_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a = x ^ gdata_" << Idx << "[" << (f % 8) << "];\n"
       << "  for (uint64_t i=0;i<53;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    a ^= gbss_" << Idx << "[i & 7] + (a>>11);\n"
       << "    a = (a<<7) | (a>>57);\n"
       << "  }\n"
       << "  gbss_" << Idx << "[" << (f % 8) << "] += a;\n"
       << "  return a ^ gbss_" << Idx << "[" << ((f + 1) % 8) << "];\n}\n";
  }
  return S;
}

std::string genGlobalsMain(unsigned NumMods, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m) {
    OS << "extern uint64_t gdata_" << m << "[8];\n";
    OS << "extern uint64_t gbss_" << m << "[8];\n";
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "uint64_t gfn_" << m << "_" << f << "(uint64_t);\n";
  }
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  // Write .bss at runtime so it is genuinely uninitialized storage (not
  // constant-foldable) and cross-module addressed.
  for (unsigned m = 0; m < NumMods; ++m)
    OS << "  gbss_" << m << "[k & 7] ^= (uint64_t)(" << (m * 7u + 1u)
       << "ULL + k);\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f) {
      OS << "  s += gfn_" << m << "_" << f << "(" << i << "ULL+k) ^ gdata_" << m
         << "[" << (f % 8) << "];\n";
      i++;
    }
  // Read .bss across every module after the calls mutated it.
  for (unsigned m = 0; m < NumMods; ++m)
    OS << "  s += gbss_" << m << "[(k+1) & 7];\n";
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}

// A module mixing plain heavy functions (weight + multi-partition spread, so
// the merger actually runs) with computed-goto "interpreters".  Each
// interpreter holds a function-local `static const void *tab[]` of label
// addresses — the exact blockaddress-in-a-global-initializer shape Lua's
// luaV_execute / CPython's ceval use.  Global initializers all live in
// partition 0, so if parallel codegen bins such a function into a partition !=
// 0 its blockaddress constants in p0 collapse to inttoptr(1) and the program
// jumps to address 1 (the real lua_lto SIGTRAP).  The fix pins every
// address-taken-block function to partition 0; this generator exists so a
// regression of that pinning turns this test red, because the object
// self-verifier *cannot* catch it (the partition object is already wrong before
// the merge).
std::string genComputedGotoModule(unsigned Idx, unsigned NumHeavy,
                                  unsigned NumCG) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  // Heavy body matches genHeavyModule's static instruction count (weight is the
  // sum of BB sizes, independent of the loop trip count), so a handful of these
  // per module clears the parallel threshold (TotalWeight>=10000) and the
  // partitioner actually engages — without that the whole thing compiles
  // serially and never exercises the merger or the pin.
  for (unsigned f = 0; f < NumHeavy; ++f)
    OS << "__attribute__((noinline)) uint64_t cgheavy_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a=x, b=x^0x9e3779b97f4a7c15ULL, c=" << (Idx * 131u + f)
       << "ULL;\n"
       << "  for (uint64_t i=0;i<61;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    b ^= (a>>13); b += (a<<7);\n"
       << "    c += (a^b) + (a&b) - (a|b);\n"
       << "    c = (c<<5) | (c>>59);\n"
       << "    a ^= c*0xff51afd7ed558ccdULL;\n"
       << "    b = b*0x100000001b3ULL ^ (c>>17);\n"
       << "  }\n"
       << "  return a^b^c;\n}\n";
  // The dispatch index (st & 3) is driven by a runtime-evolving PRNG state, not
  // a compile-time-constant program — otherwise -O2 would devirtualize the
  // indirectbr into direct branches and delete the blockaddress table, hiding
  // the bug.  Lua's real luaV_execute is exactly this shape: the next opcode is
  // runtime bytecode, so the table must survive to runtime.
  for (unsigned f = 0; f < NumCG; ++f)
    OS << "__attribute__((noinline)) uint64_t cgvm_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  static const void *const tab[] = {&&A,&&B,&&C,&&D,&&E};\n"
       << "  uint64_t acc=x, st=x^" << (Idx * 2654435761u + f + 1u)
       << "ULL; int steps=0;\n"
       << "  goto *tab[st & 3];\n"
       << "A: acc+=st; st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "B: acc^=(acc>>13); "
          "st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "C: acc+=(acc<<7); "
          "st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "D: acc=(acc<<5)|(acc>>59);"
          " st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "E: return acc;\n}\n";
  return S;
}

std::string genComputedGotoMain(unsigned NumMods, unsigned NumHeavy,
                                unsigned NumCG) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m) {
    for (unsigned f = 0; f < NumHeavy; ++f)
      OS << "uint64_t cgheavy_" << m << "_" << f << "(uint64_t);\n";
    for (unsigned f = 0; f < NumCG; ++f)
      OS << "uint64_t cgvm_" << m << "_" << f << "(uint64_t);\n";
  }
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m) {
    for (unsigned f = 0; f < NumHeavy; ++f)
      OS << "  s += cgheavy_" << m << "_" << f << "(" << i++ << "ULL+k);\n";
    for (unsigned f = 0; f < NumCG; ++f)
      OS << "  s += cgvm_" << m << "_" << f << "(" << i++ << "ULL+k);\n";
  }
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}

// A module shaped like a real-world translation unit: it carries file-local
// `static` symbols whose names are IDENTICAL across every module (the way every
// real C file has its own `static int cmp`, `static char buf[]`, `static const
// char *names[]`, ...), but whose values are module-specific.  After the
// auto-LTO IR merge these collide and get uniquified (s_tab, s_tab.1, ...),
// then the partition split externalizes them with the `.__pcg` suffix and the
// merger demotes them back to many same-base-named *local* symbols in the final
// object. That is the merger's least-anchored path: the self-verifier
// content-anchors only *uniquely* named defined symbols, so these
// duplicate-named statics fall back to the weaker disjoint-interval /
// relative-displacement invariants — the exact blind spot where a reintroduced
// offset-collapse could hide longest.  To turn any such collapse into a visible
// divergence, every static's value is derived from the module index and the
// functions both read and write them, so aliasing two modules' copies (a
// mis-shifted offset) changes the printed sum. Each module also folds in the
// other adversarial shapes a real interpreter has — a function-local
// computed-goto dispatch table (blockaddress in a global initializer) and a
// cross-module .data global — and every exported entry owns a loop so the
// post-IPO weight clears the parallel threshold and the partitioner actually
// engages.  noinline keeps the statics distinct post-IPO.
std::string genRealisticModule(unsigned Idx, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  // Duplicate-named statics (same name every module, module-specific values).
  OS << "static uint64_t s_tab[6] = {";
  for (unsigned j = 0; j < 6; ++j)
    OS << (j ? "," : "")
       << ((uint64_t)(Idx * 6u + j) * 0x9e3779b97f4a7c15ULL + 1u) << "ULL";
  OS << "};\n";
  OS << "static uint64_t s_bss[6];\n";
  OS << "static const uint64_t s_ro[4] = {";
  for (unsigned j = 0; j < 4; ++j)
    OS << (j ? "," : "") << ((uint64_t)(Idx * 4u + j) * 0x100000001b3ULL + 7u)
       << "ULL";
  OS << "};\n";
  // Duplicate-named static helper (.text); module-specific constant inside.
  OS << "__attribute__((noinline)) static uint64_t s_mix(uint64_t x){\n"
     << "  uint64_t a = x ^ s_ro[x & 3] ^ " << (Idx * 131u + 1u) << "ULL;\n"
     << "  for (int i=0;i<31;i++){\n"
     << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
     << "    a ^= s_tab[i % 6];\n"
     << "  }\n"
     << "  s_bss[x % 6] += a;\n"
     << "  return a ^ s_bss[(x + 1) % 6];\n}\n";
  // Duplicate-named static computed-goto VM (blockaddress dispatch table).  The
  // dispatch index is runtime-evolving so -O2 cannot devirtualize the table
  // away.
  OS << "__attribute__((noinline)) static uint64_t s_vm(uint64_t x){\n"
     << "  static const void *const tab[] = {&&A,&&B,&&C,&&D,&&E};\n"
     << "  uint64_t acc=x, st=x ^ " << (Idx * 2654435761u + 3u)
     << "ULL; int steps=0;\n"
     << "  goto *tab[st & 3];\n"
     << "A: acc += s_mix(st); "
        "st=st*6364136223846793005ULL+1442695040888963407ULL;"
        " if(++steps<48) goto *tab[st & 3]; goto E;\n"
     << "B: acc ^= (acc>>13); "
        "st=st*6364136223846793005ULL+1442695040888963407ULL;"
        " if(++steps<48) goto *tab[st & 3]; goto E;\n"
     << "C: acc += (acc<<7); "
        "st=st*6364136223846793005ULL+1442695040888963407ULL;"
        " if(++steps<48) goto *tab[st & 3]; goto E;\n"
     << "D: acc = (acc<<5)|(acc>>59);"
        " st=st*6364136223846793005ULL+1442695040888963407ULL;"
        " if(++steps<48) goto *tab[st & 3]; goto E;\n"
     << "E: return acc;\n}\n";
  // Cross-module .data global (unique name) so the merge also shifts a normal
  // cross-partition reference, alongside the duplicate-named statics above.
  OS << "uint64_t gx_" << Idx << "[4] = {";
  for (unsigned j = 0; j < 4; ++j)
    OS << (j ? "," : "")
       << ((uint64_t)(Idx * 4u + j) * 0xff51afd7ed558ccdULL + 5u) << "ULL";
  OS << "};\n";
  // Exported entries: each owns a loop (weight to engage the partitioner) and
  // exercises the duplicate-named statics + VM + cross-module global.
  for (unsigned f = 0; f < NumFns; ++f)
    OS << "__attribute__((noinline)) uint64_t rentry_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a = x ^ s_tab[" << (f % 6) << "];\n"
       << "  for (uint64_t i=0;i<53;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    a ^= s_bss[i % 6] + (a>>11);\n"
       << "    a = (a<<7) | (a>>57);\n"
       << "  }\n"
       << "  return a ^ s_mix(x) ^ s_vm(x) ^ gx_" << Idx << "[x & 3];\n}\n";
  return S;
}

std::string genRealisticMain(unsigned NumMods, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m) {
    OS << "extern uint64_t gx_" << m << "[4];\n";
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "uint64_t rentry_" << m << "_" << f << "(uint64_t);\n";
  }
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f) {
      OS << "  s += rentry_" << m << "_" << f << "(" << i << "ULL+k) ^ gx_" << m
         << "[" << (f % 4) << "];\n";
      i++;
    }
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}
#endif // _WIN32

} // namespace

TEST(MergeDifferentialLLD, ElfArm64FaithfulVsBundledLinker) {
  runLldDifferential("aarch64-linux-gnu", Format::ELF64LE);
}
TEST(MergeDifferentialLLD, ElfX8664FaithfulVsBundledLinker) {
  runLldDifferential("x86_64-linux-gnu", Format::ELF64LE);
}
TEST(MergeDifferentialLLD, MachOArm64FaithfulVsBundledLinker) {
  runLldDifferential("arm64-apple-darwin", Format::MachO64);
}
TEST(MergeDifferentialLLD, MachOX8664FaithfulVsBundledLinker) {
  runLldDifferential("x86_64-apple-darwin", Format::MachO64);
}

// Execution-level proof on the native host: merge -> link -> run must match a
// plain link byte-for-byte.  Picks the host's object format; skips on any host
// whose format the merger does not target.
TEST(MergeDifferentialLLD, NativeMergedExecutionMatchesPlainLink) {
  Triple Host(sys::getProcessTriple());
  if (Host.isOSBinFormatMachO())
    runMergedExecutionEquivalence(Format::MachO64);
  else if (Host.isOSBinFormatELF())
    runMergedExecutionEquivalence(Format::ELF64LE);
  else
    GTEST_SKIP() << "host object format not exercised by this test";
}

// Object-level `-r` merge of modules that share file-local `static` names (the
// real-repository shape) must run identically to a plain link.  This is the one
// path that produces genuinely duplicate-named local symbols in the merged
// object — the auto-LTO path IR-merges first and uniquifies them — so it is the
// only place verifyMerge's non-unique-name fallback is exercised end to end.
TEST(MergeDifferentialLLD, DuplicateNamedStaticsRMergeMatchesPlainLink) {
  Triple Host(sys::getProcessTriple());
  if (Host.isOSBinFormatMachO())
    runDupStaticRMergeEquivalence(Format::MachO64);
  else if (Host.isOSBinFormatELF())
    runDupStaticRMergeEquivalence(Format::ELF64LE);
  else
    GTEST_SKIP() << "host object format not exercised by this test";
}

// Randomized differential: several seeds, each a fresh multi-module program,
// merged-and-run vs plain-linked-and-run.  Native host only (it executes the
// result).  Skips cleanly where the frontend/linker is unavailable.
TEST(MergeFuzzExecution, RandomCrossModuleNativeMatchesPlainLink) {
  Triple Host(sys::getProcessTriple());
  Format Fmt;
  if (Host.isOSBinFormatMachO())
    Fmt = Format::MachO64;
  else if (Host.isOSBinFormatELF())
    Fmt = Format::ELF64LE;
  else
    GTEST_SKIP() << "host object format not exercised by this test";
  // Default to a CI-friendly seed count; NEVERC_MERGE_FUZZ_SEEDS cranks it for
  // soak testing (e.g. =1000 overnight).  Each seed is an independent program.
  unsigned NumSeeds = 6;
  if (const char *E = ::getenv("NEVERC_MERGE_FUZZ_SEEDS")) {
    unsigned V = (unsigned)strtoul(E, nullptr, 10);
    if (V > 0)
      NumSeeds = V;
  }
  for (unsigned Seed = 1; Seed <= NumSeeds; ++Seed) {
    runMergeFuzzExecution(Fmt, Seed);
    if (::testing::Test::IsSkipped())
      return;
  }
}

// The strict-mode tripwire (see ScopedEnv/genHeavyModule above).  A merger
// regression makes the auto-LTO link abort under NEVERC_PCG_STRICT; a merge
// that loads but is wrong makes the output diverge from the -fno-lto build.
#ifndef _WIN32
TEST(MergeParallelCodegenStrict, MultiFileAutoLtoLinkUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 24 modules x 16 functions = 384 noinline heavy functions + main, sized to
  // clear the parallel thresholds with margin even after IPO.
  const unsigned NumMods = 24, NumFns = 16;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("hm" + Twine(m) + ".c").str());
    std::string Src = genHeavyModule(m, NumFns);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("hmain.c");
    std::string Src = genHeavyMain(NumMods, NumFns);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  // Reference: -fno-lto build (serial per-TU codegen, no parallel merger).  Its
  // failure means the host frontend/linker is unavailable -> skip, not fail.
  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  // Auto-LTO build with STRICT on (caches off so the partitions are really
  // codegen'd + merged on this run).  A merge/self-verify failure now aborts
  // the compiler -> non-zero exit -> this ASSERT fails loudly, instead of the
  // silent serial fallback that would mask the regression.
  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link failed under NEVERC_PCG_STRICT — the "
           "parallel-codegen "
           "merger failed self-verify or could not emit a merged object (a "
           "merger regression); see the scratch spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO executable did not exit cleanly (the merge produced a "
         "loadable but wrong object)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — the "
         "parallel-codegen merge mis-placed a symbol or relocation";
}

// Same strict-mode tripwire, but the modules carry cross-module-referenced
// .data (initialized) and .bss (uninitialized) globals.  This is the precise
// shape of the historical offset-collapse bug, which mis-shifted .bss/.data
// symbol values and cross-partition relocations to produce a loadable but wrong
// object (the .ko that crashed).  Driving it through the *real* auto-LTO merge
// under STRICT means a reintroduced collapse either aborts the link or makes
// the program diverge from the -fno-lto reference — never a silent pass.
TEST(MergeParallelCodegenStrict, MultiFileAutoLtoGlobalsUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 20 modules x 12 functions, each touching its own .data + .bss globals,
  // sized to clear the parallel thresholds with margin even after IPO.
  const unsigned NumMods = 20, NumFns = 12;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("gm" + Twine(m) + ".c").str());
    std::string Src = genGlobalsModule(m, NumFns);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("gmain.c");
    std::string Src = genGlobalsMain(NumMods, NumFns);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link of cross-module .data/.bss globals failed under "
           "NEVERC_PCG_STRICT — the parallel-codegen merger failed self-verify "
           "or could not emit a merged object (a merger regression); see the "
           "scratch spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO executable did not exit cleanly (the merge produced a "
         "loadable but wrong object)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — the "
         "parallel-codegen merge mis-placed a .data/.bss symbol or a "
         "cross-partition relocation (the historical offset-collapse shape)";
}

// Same strict-mode tripwire for the computed-goto / blockaddress shape — the
// real lua_lto SIGTRAP.  Lua's luaV_execute keeps a `static const void *[]`
// dispatch table of `&&label` addresses; that table's initializer lives in
// partition 0, so binning luaV_execute into any other partition rewrites its
// blockaddress constants to inttoptr(1) and the interpreter jumps to address 1.
// The merger's self-verifier *cannot* see this (the partition object is wrong
// before it is merged), so the only guard is pinning address-taken-block
// functions to partition 0 — and the only regression alarm is an end-to-end
// run. Without the pin, the cgvm_* interpreters binned outside partition 0 jump
// to 1 and this program crashes (non-zero exit) or diverges from the -fno-lto
// build.
TEST(MergeParallelCodegenStrict, MultiFileAutoLtoComputedGotoUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 24 modules x (16 heavy + 4 computed-goto): 384 plain heavy functions (the
  // proven scale that clears TotalWeight>=10000 so the partitioner engages and
  // the merger runs) plus 96 computed-goto interpreters.  With 96
  // address-taken- block functions across several partitions, a dropped pin
  // lands at least one outside partition 0 with overwhelming probability, so
  // the regression is caught essentially deterministically.
  const unsigned NumMods = 24, NumHeavy = 16, NumCG = 4;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("cg" + Twine(m) + ".c").str());
    std::string Src = genComputedGotoModule(m, NumHeavy, NumCG);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("cgmain.c");
    std::string Src = genComputedGotoMain(NumMods, NumHeavy, NumCG);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link of computed-goto interpreters failed under "
           "NEVERC_PCG_STRICT (a merger/pinning regression); see scratch "
           "spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO computed-goto executable crashed — a blockaddress dispatch "
         "table collapsed to inttoptr(1) because an address-taken-block "
         "function was not pinned to partition 0 (the lua_lto SIGTRAP shape)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — a "
         "computed-goto dispatch table was corrupted by the partition split";
}

// Strict-mode tripwire over a *real-world-shaped* program: every module carries
// file-local `static` symbols whose names are identical across all modules
// (.text helper, .data table, .bss scratch, .rodata constants) plus a
// computed-goto VM and a cross-module .data global.  Duplicate-named statics
// are the merger's least-anchored case — the self-verifier content-anchors only
// uniquely-named defined symbols, so these many same-base-named locals exercise
// the weaker disjoint-interval / relative-displacement invariants that are the
// last line of defense against the historical offset-collapse.  The prior
// strict tests each isolate one shape with globally-unique names; this one
// combines them with the duplicate-static naming real repositories (Lua,
// sqlite, ...) actually have, so a regression that mishandles a duplicate-named
// local's offset — invisible to the unique-name content anchor — still diverges
// from the -fno-lto reference here.
TEST(MergeParallelCodegenStrict,
     MultiFileAutoLtoRealisticMixedUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 20 modules x 12 loop-bearing exported entries (240 heavy functions) plus
  // per-module duplicate-named static helper + VM, sized to clear the parallel
  // thresholds with margin even after IPO so the partitioner + merger engage.
  const unsigned NumMods = 20, NumFns = 12;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("rm" + Twine(m) + ".c").str());
    std::string Src = genRealisticModule(m, NumFns);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("rmain.c");
    std::string Src = genRealisticMain(NumMods, NumFns);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link of a duplicate-named-statics program failed under "
           "NEVERC_PCG_STRICT — the parallel-codegen merger failed self-verify "
           "or could not emit a merged object (a merger regression); see the "
           "scratch spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO executable did not exit cleanly (the merge produced a "
         "loadable but wrong object)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — the merge "
         "mis-placed a duplicate-named local static's symbol value or a "
         "cross-partition relocation (the offset-collapse shape the "
         "unique-name "
         "content anchor cannot see)";
}

// The other half of the merger's safety contract.  Every test above proves the
// merge is correct (or that strict mode catches a regression).  This proves
// that when the merge genuinely cannot be produced, the pipeline falls back to
// serial codegen and STILL emits a correct binary — i.e. a merger bug degrades
// to "slower", never "wrong".  NEVERC_PCG_FORCE_MERGE_FAIL makes
// mergePartitionObjects() return false on every partitioned link, simulating an
// arbitrary merge/self-verify failure; with strict mode off the link must
// succeed via the fallback and match the -fno-lto reference.  This also
// exercises restoreLinkage(), whose job is to scrub the externalized ".__pcg"
// linkage/visibility/name rewrites back to the originals before the serial
// codegen runs — a bug there would emit what should be local symbols as
// leaked globals or diverge at runtime.
TEST(MergeParallelCodegenStrict,
     MultiFileAutoLtoSerialFallbackProducesCorrectBinary) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  const unsigned NumMods = 24, NumFns = 16;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("fb" + Twine(m) + ".c").str());
    std::string Src = genHeavyModule(m, NumFns);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("fbmain.c");
    std::string Src = genHeavyMain(NumMods, NumFns);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  std::string ExeLto = Dir.file("exe_lto");
  {
    // Strict OFF (CI sets it globally, so explicitly unset for the duration)
    // so the forced failure is allowed to fall back; caches OFF so the
    // partitioned merge path is really entered and then forced to fail.
    ScopedUnsetEnv NoStrict("NEVERC_PCG_STRICT");
    ScopedEnv ForceFail("NEVERC_PCG_FORCE_MERGE_FAIL", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link did not recover via serial codegen when the merge "
           "was forced to fail — the safety net is broken";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "serial-fallback executable did not exit cleanly";
  EXPECT_EQ(OutLto, OutRef)
      << "serial-fallback program output diverged from the -fno-lto build — "
         "restoreLinkage() left the module polluted after the forced merge "
         "failure";
}
#endif // _WIN32

namespace {

// One input's worth of call graph profile: two defined symbols and an edge
// between them.  Written in assembly because that is the only way to hand the
// merger a profile -- neverc's own driver has no PGO switch (`PGOOpt` in
// BackendUtil.cpp is always empty), so nothing it compiles from C carries the
// entry counts CGProfilePass needs to add the "CG Profile" module flag.  The
// `.cg_profile` directive is understood by the ELF, COFF and Mach-O assembly
// parsers alike, and `.byte` bodies keep the fixture free of any instruction
// encoding, so one text serves every target.
std::string callGraphProfileAsm(unsigned Index) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "\t.text\n";
  // Distinct names per input: two strong definitions of one name is a merge
  // failure in its own right, and would stop this test short of the profile.
  OS << "\t.globl\tcgp_from" << Index << "\n";
  OS << "cgp_from" << Index << ":\n\t.byte\t0\n";
  OS << "\t.globl\tcgp_to" << Index << "\n";
  OS << "cgp_to" << Index << ":\n\t.byte\t0\n";
  OS << "\t.cg_profile cgp_from" << Index << ", cgp_to" << Index << ", 1234\n";
  return S;
}

// Whether these object bytes carry a call graph profile.  ELF and COFF name
// the section the same way; Mach-O calls it __cg_profile in the __LLVM
// segment, and the section name alone identifies it.  A merge concatenates
// same-named sections, so several inputs' worth of profile arrives as one
// oversized section rather than several sections -- presence, not count, is
// what separates a dropped profile from a kept one.
bool hasCallGraphProfile(ArrayRef<char> Bytes) {
  auto ObjOrErr = object::ObjectFile::createObjectFile(
      MemoryBufferRef(StringRef(Bytes.data(), Bytes.size()), "merge-test"));
  if (!ObjOrErr) {
    consumeError(ObjOrErr.takeError());
    return false;
  }
  for (const object::SectionRef &S : (*ObjOrErr)->sections()) {
    Expected<StringRef> NameOrErr = S.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    if (*NameOrErr == ".llvm.call-graph-profile" ||
        *NameOrErr == "__cg_profile")
      return true;
  }
  return false;
}

// A call graph profile names its two functions by symbol table index on COFF
// and Mach-O, and a merge builds one symbol table out of all its inputs -- so
// a profile carried through would describe a different call graph than the one
// it was measured for, and the linker would order the image by it.  Every
// format drops it instead (see Common/MergerCommon.h).
void runCallGraphProfileDroppedByMerge(StringRef Target, Format Fmt) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  for (unsigned i = 0; i != 2; ++i) {
    std::string AsmPath = Dir.file(("cgp" + Twine(i) + ".s").str());
    std::string Src = callGraphProfileAsm(i);
    if (!writeBytes(AsmPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write assembly fixture";
    std::string ObjPath = Dir.file(("cgp" + Twine(i) + ".o").str());
    StringRef Args[] = {"-target", Target, "-c", AsmPath, "-o", ObjPath};
    if (runNeverc(Dir, Args) != 0)
      GTEST_SKIP() << "neverc cannot assemble for this target here";

    SmallVector<char, 0> Bytes;
    ASSERT_TRUE(readObj(ObjPath, Bytes));
    // Without this the merged object having no profile would prove nothing:
    // it could just as well mean the inputs never had one.
    ASSERT_TRUE(hasCallGraphProfile(Bytes))
        << "input " << i << " carries no call graph profile to drop";
    Inputs.push_back(std::move(Bytes));
  }

  SmallVector<char, 0> Merged;
  raw_svector_ostream OS(Merged);
  ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt));
  EXPECT_FALSE(hasCallGraphProfile(Merged))
      << "the merged object kept a call graph profile whose symbol indices no "
         "longer name the functions it was measured for";
}

} // namespace

TEST(MergeCallGraphProfile, ElfDroppedByMerge) {
  runCallGraphProfileDroppedByMerge("x86_64-unknown-linux-gnu",
                                    Format::ELF64LE);
}
TEST(MergeCallGraphProfile, MachODroppedByMerge) {
  runCallGraphProfileDroppedByMerge("arm64-apple-macos", Format::MachO64);
}
TEST(MergeCallGraphProfile, CoffDroppedByMerge) {
  runCallGraphProfileDroppedByMerge("x86_64-pc-windows-msvc", Format::COFF);
}
#endif // NEVERC_BINARY
