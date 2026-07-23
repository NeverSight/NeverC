// The dyncode final verifier (sealed dyncode.verify gate).
//
// These tests pin the composed checklist: a well-formed plan/image pair passes
// and leaves the image Verified, while a surviving external reference, a
// forbidden byte, or an off-zero entry each fail the single gate.  This is the
// gate a byte-level transform cannot bypass -- there is no "verified, now edit
// bytes" path.

#include "Extractor/DynCodeFinalVerifier.h"

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <vector>

using namespace llvm;
using namespace neverc::dyncode;

namespace {

DynCodeOptions elfX86() {
  DynCodeOptions Opts;
  Opts.Target.Arch = DynCodeArch::X86_64;
  Opts.Target.Format = ObjectFormat::ELF;
  return Opts;
}

DynCodeExtractionPlan entryPlan() {
  DynCodeExtractionPlan Plan;
  DynCodeSymbolMapping M;
  M.Name = "main";
  M.OutputOffset = 0;
  M.IsEntry = true;
  cantFail(Plan.addSymbolMapping(M).takeError());
  cantFail(Plan.setEntry(DynCodeEntryPolicy::Explicit, "main", 0));
  return Plan;
}

DynCodeImage entryImage(std::vector<uint8_t> Bytes) {
  DynCodeImage Image;
  cantFail(Image.append(Bytes));
  Image.setEntry(0, "main");
  return Image;
}

TEST(PluginDynCodeFinalizeTest, AcceptsWellFormedImage) {
  DynCodeOptions Opts = elfX86();
  DynCodeExtractionPlan Plan = entryPlan();
  DynCodeImage Image = entryImage({0x90, 0x90, 0x90, 0x90});
  DynCodeReport Report;

  ASSERT_FALSE(verifyDynCodeFinalImage(Plan, Image, Report, Opts));
  EXPECT_EQ(Image.state(), DynCodeImageState::Verified);
}

TEST(PluginDynCodeFinalizeTest, RejectsSurvivingExternal) {
  DynCodeOptions Opts = elfX86();
  DynCodeExtractionPlan Plan = entryPlan();
  DynCodeExternalContract C;
  C.Symbol = "printf";
  C.Disposition = DynCodeExternalDisposition::Unresolved;
  cantFail(Plan.addExternalContract(C).takeError());

  DynCodeImage Image = entryImage({0x90, 0x90});
  DynCodeReport Report;
  auto E = verifyDynCodeFinalImage(Plan, Image, Report, Opts);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
  EXPECT_NE(Image.state(), DynCodeImageState::Verified);
}

TEST(PluginDynCodeFinalizeTest, RejectsForbiddenByte) {
  DynCodeOptions Opts = elfX86();
  Opts.BadBytes = {0x00};
  DynCodeExtractionPlan Plan = entryPlan();
  DynCodeImage Image = entryImage({0x90, 0x00, 0x90});
  DynCodeReport Report;
  auto E = verifyDynCodeFinalImage(Plan, Image, Report, Opts);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeFinalizeTest, RejectsOffZeroEntry) {
  DynCodeOptions Opts = elfX86();
  DynCodeExtractionPlan Plan;
  DynCodeSymbolMapping M;
  M.Name = "main";
  M.OutputOffset = 4;
  M.IsEntry = true;
  cantFail(Plan.addSymbolMapping(M).takeError());
  cantFail(Plan.setEntry(DynCodeEntryPolicy::Explicit, "main", 4));

  DynCodeImage Image;
  cantFail(Image.append(std::vector<uint8_t>{0x90, 0x90, 0x90, 0x90, 0x90}));
  Image.setEntry(4, "main");
  DynCodeReport Report;
  auto E = verifyDynCodeFinalImage(Plan, Image, Report, Opts);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

} // namespace
