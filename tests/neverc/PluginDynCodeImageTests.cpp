// Extraction plan, DynCodeImage and DynCodeReport.
//
// These tests exercise the typed host data structures directly: typed
// generation handles (stale / wrong-kind), the bounded checked bytes builder
// (bounds, budget), overlapping-mapping rejection, deterministic image digests
// and the report's stable canonical-JSON key ordering.

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"

#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::dyncode;

namespace {

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

std::vector<uint8_t> bytesOf(std::initializer_list<uint8_t> B) {
  return std::vector<uint8_t>(B);
}

DynCodeSectionFragment selectedFragment(std::string Name, uint64_t Off,
                                        uint64_t Size) {
  DynCodeSectionFragment F;
  F.SourceName = std::move(Name);
  F.Disposition = DynCodeSectionDisposition::Selected;
  F.OutputOffset = Off;
  F.OutputSize = Size;
  F.Alignment = 4;
  return F;
}

TEST(PluginDynCodeImageTest, PlanRejectsOverlappingSelectedFragments) {
  DynCodeExtractionPlan Plan;
  auto A = Plan.addSectionFragment(selectedFragment("a", 0, 16));
  ASSERT_TRUE(static_cast<bool>(A)) << errorText(A.takeError());
  // Overlaps [0,16).
  auto B = Plan.addSectionFragment(selectedFragment("b", 8, 16));
  EXPECT_FALSE(static_cast<bool>(B));
  if (!B)
    consumeError(B.takeError());
  // Abutting range [16,32) is fine.
  auto C = Plan.addSectionFragment(selectedFragment("c", 16, 16));
  EXPECT_TRUE(static_cast<bool>(C)) << (C ? "" : errorText(C.takeError()));
}

TEST(PluginDynCodeImageTest, PlanRejectsDuplicateAndSecondEntrySymbol) {
  DynCodeExtractionPlan Plan;
  DynCodeSymbolMapping S;
  S.Name = "main";
  S.OutputOffset = 0;
  S.IsEntry = true;
  ASSERT_TRUE(static_cast<bool>(Plan.addSymbolMapping(S)));

  DynCodeSymbolMapping Dup;
  Dup.Name = "main";
  Dup.OutputOffset = 8;
  auto DupResult = Plan.addSymbolMapping(Dup);
  EXPECT_FALSE(static_cast<bool>(DupResult));
  if (!DupResult)
    consumeError(DupResult.takeError());

  DynCodeSymbolMapping Second;
  Second.Name = "other";
  Second.OutputOffset = 8;
  Second.IsEntry = true;
  auto SecondResult = Plan.addSymbolMapping(Second);
  EXPECT_FALSE(static_cast<bool>(SecondResult));
  if (!SecondResult)
    consumeError(SecondResult.takeError());
}

TEST(PluginDynCodeImageTest, HandlesStaleAfterRebuildAndWrongKindRejected) {
  DynCodeExtractionPlan Plan;
  auto Frag = Plan.addSectionFragment(selectedFragment("a", 0, 16));
  ASSERT_TRUE(static_cast<bool>(Frag)) << errorText(Frag.takeError());
  DynCodeHandle FragHandle = *Frag;

  // A section handle resolved as a symbol mapping is wrong-kind.
  auto Wrong = Plan.resolve(FragHandle, DynCodeHandleKind::SymbolMapping);
  EXPECT_FALSE(static_cast<bool>(Wrong));
  if (!Wrong)
    consumeError(Wrong.takeError());

  // It resolves fine before a rebuild.
  auto Ok = Plan.resolve(FragHandle, DynCodeHandleKind::SectionFragment);
  ASSERT_TRUE(static_cast<bool>(Ok)) << errorText(Ok.takeError());

  // After a plan replacement the old handle is stale.
  Plan.rebuild();
  auto Stale = Plan.resolve(FragHandle, DynCodeHandleKind::SectionFragment);
  EXPECT_FALSE(static_cast<bool>(Stale));
  if (!Stale)
    consumeError(Stale.takeError());
  EXPECT_EQ(Plan.lookupSectionFragment(FragHandle), nullptr);
}

TEST(PluginDynCodeImageTest, ImageBuilderBoundsAndBudgetChecked) {
  DynCodeImage Image;
  ASSERT_FALSE(Image.append(bytesOf({1, 2, 3, 4})));
  EXPECT_EQ(Image.size(), 4U);

  // Write within range succeeds; past the end fails.
  ASSERT_FALSE(Image.write(1, bytesOf({9, 9})));
  {
    // Must bind the returned Error and consume it: checking a failure Error via
    // a discarded temporary aborts in assertions/ABI-breaking-checks builds.
    auto E = Image.write(3, bytesOf({9, 9}));
    EXPECT_TRUE(static_cast<bool>(E));
    consumeError(std::move(E));
  }

  // Read past the end fails.
  auto R = Image.read(2, 10);
  EXPECT_FALSE(static_cast<bool>(R));
  if (!R)
    consumeError(R.takeError());

  // Budget caps growth.
  Image.setBudget(6);
  {
    auto E = Image.append(bytesOf({5, 6})); // 4 -> 6, exactly at budget
    EXPECT_FALSE(static_cast<bool>(E));
    if (E)
      consumeError(std::move(E));
  }
  {
    auto E = Image.append(bytesOf({7})); // 6 -> 7, over budget
    EXPECT_TRUE(static_cast<bool>(E));
    consumeError(std::move(E));
  }
  {
    auto E = Image.resize(100); // over budget
    EXPECT_TRUE(static_cast<bool>(E));
    consumeError(std::move(E));
  }

  // replaceRange past the end fails.
  {
    auto E = Image.replaceRange(4, 10, bytesOf({0}));
    EXPECT_TRUE(static_cast<bool>(E));
    consumeError(std::move(E));
  }
}

TEST(PluginDynCodeImageTest, DigestIsDeterministicAndRepublishBumpsGeneration) {
  DynCodeImage A;
  DynCodeImage B;
  ASSERT_FALSE(A.append(bytesOf({1, 2, 3})));
  ASSERT_FALSE(B.append(bytesOf({1, 2, 3})));
  EXPECT_EQ(A.digest(), B.digest());

  ASSERT_FALSE(B.append(bytesOf({4})));
  EXPECT_NE(A.digest(), B.digest());

  uint64_t Gen = A.generation();
  A.republish();
  EXPECT_EQ(A.generation(), Gen + 1);
}

TEST(PluginDynCodeImageTest, ImageStateMachineGuardsMutation) {
  DynCodeImage Image;
  ASSERT_FALSE(Image.append(bytesOf({1, 2})));
  ASSERT_FALSE(Image.markVerified());

  // A verified image is no longer mutable.
  auto E = Image.append(bytesOf({3}));
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));

  ASSERT_FALSE(Image.markCommitted());
  // A committed image cannot be aborted.
  auto Abort = Image.markAborted();
  EXPECT_TRUE(static_cast<bool>(Abort));
  consumeError(std::move(Abort));
}

TEST(PluginDynCodeImageTest, ReportCanonicalJSONIsStableAndSorted) {
  DynCodeReport Report;
  DynCodeReportSummary Summary;
  Summary.SelectedSectionCount = 2;
  Summary.ImageSize = 128;
  Summary.Alignment = 16;
  Summary.EntrySymbol = "main";
  Summary.EntryOffset = 0;
  ASSERT_FALSE(Report.setSummary(Summary));

  // Add journal records out of order; freeze must sort them.
  ASSERT_FALSE(Report.addRecord({3, "builtin", "reloc", "applied"}));
  ASSERT_FALSE(Report.addRecord({1, "builtin", "section", "selected"}));
  ASSERT_FALSE(Report.addRecord({1, "builtin", "entry", "main"}));

  // Serialization requires a frozen report.
  auto Unfrozen = Report.toCanonicalJSON();
  EXPECT_FALSE(static_cast<bool>(Unfrozen));
  if (!Unfrozen)
    consumeError(Unfrozen.takeError());

  ASSERT_FALSE(Report.freeze());
  auto First = Report.toCanonicalJSON();
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  auto Second = Report.toCanonicalJSON();
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  EXPECT_EQ(*First, *Second); // deterministic

  const std::string &Json = *First;
  // Top-level keys are lexicographically ordered.
  auto Pos = [&](StringRef Key) {
    std::string Needle = "\"";
    Needle += Key.str();
    Needle += "\"";
    return Json.find(Needle);
  };
  EXPECT_LT(Pos("alignment"), Pos("bad_byte_hit_count"));
  EXPECT_LT(Pos("bad_byte_hit_count"), Pos("image_size"));
  EXPECT_LT(Pos("image_size"), Pos("journal"));
  EXPECT_LT(Pos("journal"), Pos("selected_section_count"));

  // Journal records are sorted (phase 1 entries precede the phase 3 reloc).
  EXPECT_LT(Json.find("\"entry\""), Json.find("\"reloc\""));
  EXPECT_LT(Json.find("\"section\""), Json.find("\"reloc\""));
}

} // namespace
