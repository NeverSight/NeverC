// The intra-image relocation executor centralises the branch26 / page21 / lo12
// / rel32 / rel64 patch dispatch the per-format extractors used to open-code.
// These tests pin the FinalAddr/PCDisp math and the per-kind dispatch: concrete
// byte checks for branch26 / x86 rel32 / prel64, equivalence with the raw
// encoders for the AArch64 page21 + lo12 pair, the out-of-range error, and the
// DynCodeImage write-back path.

#include "Extractor/DynCodeRelocationExecutor.h"
#include "Extractor/ExtractorCommon.h"

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <vector>

using namespace llvm;
using namespace neverc::dyncode;

namespace {

std::vector<uint8_t> le32(uint32_t V) {
  return {static_cast<uint8_t>(V), static_cast<uint8_t>(V >> 8),
          static_cast<uint8_t>(V >> 16), static_cast<uint8_t>(V >> 24)};
}

TEST(PluginDynCodeRelocationTest, AArch64Branch26EncodesDisplacement) {
  // BL <undefined> at offset 0; target function at offset 8.
  std::vector<uint8_t> Bytes = le32(0x94000000);
  Bytes.resize(16, 0);
  DynCodeRelocationWork W;
  W.SiteOffset = 0;
  W.TargetOffset = 8;
  W.Kind = DynCodeRelocApplyKind::AArch64Branch26;
  ASSERT_FALSE(applyDynCodeRelocation(Bytes, W));
  // imm26 = (8 - 0) / 4 = 2  ->  0x94000002
  EXPECT_EQ(Bytes[0], 0x02);
  EXPECT_EQ(Bytes[1], 0x00);
  EXPECT_EQ(Bytes[2], 0x00);
  EXPECT_EQ(Bytes[3], 0x94);
}

TEST(PluginDynCodeRelocationTest, X86Rel32UsesEndRelativeAddend) {
  std::vector<uint8_t> Bytes(0x80, 0);
  DynCodeRelocationWork W;
  W.SiteOffset = 4;
  W.TargetOffset = 0x40;
  W.Addend = -4; // x86 PC32 addend accounts for the end of the field
  W.Kind = DynCodeRelocApplyKind::X86Rel32;
  ASSERT_FALSE(applyDynCodeRelocation(Bytes, W));
  // FinalAddr = 0x40 - 4 = 0x3c; PCDisp = 0x3c - 4 = 0x38
  EXPECT_EQ(Bytes[4], 0x38);
  EXPECT_EQ(Bytes[5], 0x00);
  EXPECT_EQ(Bytes[6], 0x00);
  EXPECT_EQ(Bytes[7], 0x00);
}

TEST(PluginDynCodeRelocationTest, Prel64WritesEightByteDisplacement) {
  std::vector<uint8_t> Bytes(0x200, 0);
  DynCodeRelocationWork W;
  W.SiteOffset = 0;
  W.TargetOffset = 0x100;
  W.Kind = DynCodeRelocApplyKind::AArch64Prel64;
  ASSERT_FALSE(applyDynCodeRelocation(Bytes, W));
  EXPECT_EQ(Bytes[0], 0x00);
  EXPECT_EQ(Bytes[1], 0x01);
  for (int I = 2; I < 8; ++I)
    EXPECT_EQ(Bytes[I], 0x00);
}

TEST(PluginDynCodeRelocationTest, Page21AndLo12MatchRawEncoders) {
  // An ADRP + ADD pair pointing at a target in the same image.
  std::vector<uint8_t> Bytes = le32(0x90000000); // ADRP x0, ...
  for (uint8_t B : le32(0x91000000))             // ADD x0, x0, #...
    Bytes.push_back(B);
  Bytes.resize(64, 0);
  const uint64_t Target = 0x30;

  std::vector<uint8_t> ViaExecutor = Bytes;
  DynCodeRelocationWork Page;
  Page.SiteOffset = 0;
  Page.TargetOffset = Target;
  Page.Kind = DynCodeRelocApplyKind::AArch64Page21;
  DynCodeRelocationWork Add;
  Add.SiteOffset = 4;
  Add.TargetOffset = Target;
  Add.Kind = DynCodeRelocApplyKind::AArch64AddLo12;
  ASSERT_FALSE(applyDynCodeRelocation(ViaExecutor, Page));
  ASSERT_FALSE(applyDynCodeRelocation(ViaExecutor, Add));

  // Reference: call the raw encoders with the same FinalAddr the executor uses.
  std::vector<uint8_t> ViaRaw = Bytes;
  ASSERT_TRUE(patchArm64Page21(ViaRaw, 0, static_cast<int64_t>(Target), 0));
  ASSERT_TRUE(patchArm64Lo12WithShift(ViaRaw, 4, Target, 0));

  EXPECT_EQ(ViaExecutor, ViaRaw);
}

TEST(PluginDynCodeRelocationTest, OutOfRangeSiteFails) {
  std::vector<uint8_t> Bytes(4, 0);
  DynCodeRelocationWork W;
  W.SiteOffset = 8; // past the end
  W.TargetOffset = 0;
  W.Kind = DynCodeRelocApplyKind::X86Rel32;
  auto E = applyDynCodeRelocation(Bytes, W);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeRelocationTest, TargetOutsideImageFails) {
  std::vector<uint8_t> Bytes(16, 0);
  DynCodeRelocationWork W;
  W.SiteOffset = 0;
  W.TargetOffset = 0x1000; // way past the end
  W.Kind = DynCodeRelocApplyKind::AArch64Branch26;
  auto E = applyDynCodeRelocation(Bytes, W);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeRelocationTest, ExecuteWritesBackThroughImage) {
  DynCodeImage Image;
  std::vector<uint8_t> Init = le32(0x94000000);
  Init.resize(16, 0);
  ASSERT_FALSE(Image.append(Init));

  DynCodeRelocationWork W;
  W.SiteOffset = 0;
  W.TargetOffset = 8;
  W.Kind = DynCodeRelocApplyKind::AArch64Branch26;
  ASSERT_FALSE(executeDynCodeRelocations(Image, {W}));

  EXPECT_EQ(Image.bytes()[0], 0x02);
  EXPECT_EQ(Image.bytes()[3], 0x94);
  EXPECT_EQ(Image.state(), DynCodeImageState::Candidate);
}

} // namespace
