// The intra-image relocation executor centralises the branch26 / page21 / lo12
// / rel32 / rel64 patch dispatch the per-format extractors used to open-code.
// These tests pin the FinalAddr/PCDisp math and the per-kind dispatch: concrete
// byte checks for branch26 / x86 rel32 / prel64, equivalence with the raw
// encoders for the AArch64 page21 + lo12 pair, the out-of-range error, and the
// DynCodeImage write-back path.

#include "Extractor/DynCodeRelocationExecutor.h"
#include "Extractor/DynCodeRelocationProvider.h"
#include "Extractor/DynCodeRelocationVerifier.h"
#include "Extractor/ExtractorCommon.h"

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
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

TargetDesc target(DynCodeArch Arch, ObjectFormat Fmt) {
  TargetDesc T;
  T.Arch = Arch;
  T.Format = Fmt;
  return T;
}

TEST(PluginDynCodeRelocationTest, MapsElfAArch64NativeTypes) {
  TargetDesc T = target(DynCodeArch::AArch64, ObjectFormat::ELF);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_CALL26).Kind,
            DynCodeRelocApplyKind::AArch64Branch26);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_JUMP26).Kind,
            DynCodeRelocApplyKind::AArch64Branch26);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_ADR_PREL_PG_HI21).Kind,
            DynCodeRelocApplyKind::AArch64Page21);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_ADD_ABS_LO12_NC).Kind,
            DynCodeRelocApplyKind::AArch64AddLo12);

  auto Ldst64 = mapDynCodeRelocation(T, ELF::R_AARCH64_LDST64_ABS_LO12_NC);
  EXPECT_EQ(Ldst64.Kind, DynCodeRelocApplyKind::AArch64LdstLo12);
  EXPECT_EQ(Ldst64.LdstShift, 3u);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_LDST128_ABS_LO12_NC).LdstShift,
            4u);

  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_PREL64).Kind,
            DynCodeRelocApplyKind::AArch64Prel64);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_AARCH64_ABS64).Class,
            DynCodeRelocationClass::Unsupported);
}

TEST(PluginDynCodeRelocationTest, MapsElfX86AndGOT) {
  TargetDesc T = target(DynCodeArch::X86_64, ObjectFormat::ELF);
  auto PC32 = mapDynCodeRelocation(T, ELF::R_X86_64_PC32);
  EXPECT_EQ(PC32.Class, DynCodeRelocationClass::IntraImage);
  EXPECT_EQ(PC32.Kind, DynCodeRelocApplyKind::X86Rel32);
  EXPECT_EQ(PC32.AddendAdjust, 0); // ELF carries the -4 bias in the addend
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_X86_64_PLT32).Kind,
            DynCodeRelocApplyKind::X86Rel32);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_X86_64_GOTPCREL).Class,
            DynCodeRelocationClass::ExternalGOT);
  EXPECT_EQ(mapDynCodeRelocation(T, ELF::R_X86_64_REX_GOTPCRELX).Class,
            DynCodeRelocationClass::ExternalGOT);
}

TEST(PluginDynCodeRelocationTest, MapsCoffFoldsFieldBias) {
  TargetDesc A64 = target(DynCodeArch::AArch64, ObjectFormat::COFF);
  EXPECT_EQ(mapDynCodeRelocation(A64, COFF::IMAGE_REL_ARM64_BRANCH26).Kind,
            DynCodeRelocApplyKind::AArch64Branch26);
  EXPECT_EQ(mapDynCodeRelocation(A64, COFF::IMAGE_REL_ARM64_PAGEBASE_REL21).Kind,
            DynCodeRelocApplyKind::AArch64Page21);
  EXPECT_EQ(mapDynCodeRelocation(A64, COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A).Kind,
            DynCodeRelocApplyKind::AArch64AddLo12);
  EXPECT_EQ(mapDynCodeRelocation(A64, COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L).Kind,
            DynCodeRelocApplyKind::AArch64Lo12Auto);
  auto Rel32 = mapDynCodeRelocation(A64, COFF::IMAGE_REL_ARM64_REL32);
  EXPECT_EQ(Rel32.Kind, DynCodeRelocApplyKind::X86Rel32);
  EXPECT_EQ(Rel32.AddendAdjust, -4);

  TargetDesc X64 = target(DynCodeArch::X86_64, ObjectFormat::COFF);
  EXPECT_EQ(mapDynCodeRelocation(X64, COFF::IMAGE_REL_AMD64_REL32).AddendAdjust,
            -4);
  EXPECT_EQ(mapDynCodeRelocation(X64, COFF::IMAGE_REL_AMD64_REL32_1).AddendAdjust,
            -5);
  EXPECT_EQ(mapDynCodeRelocation(X64, COFF::IMAGE_REL_AMD64_REL32_5).AddendAdjust,
            -9);
  EXPECT_EQ(mapDynCodeRelocation(X64, COFF::IMAGE_REL_AMD64_ADDR64).Class,
            DynCodeRelocationClass::Unsupported);
}

TEST(PluginDynCodeRelocationTest, MapsMachOFoldsFieldBias) {
  TargetDesc A64 = target(DynCodeArch::AArch64, ObjectFormat::MachO);
  EXPECT_EQ(mapDynCodeRelocation(A64, /*BRANCH26=*/2).Kind,
            DynCodeRelocApplyKind::AArch64Branch26);
  EXPECT_EQ(mapDynCodeRelocation(A64, /*PAGE21=*/3).Kind,
            DynCodeRelocApplyKind::AArch64Page21);
  EXPECT_EQ(mapDynCodeRelocation(A64, /*PAGEOFF12=*/4).Kind,
            DynCodeRelocApplyKind::AArch64Lo12Auto);
  EXPECT_EQ(mapDynCodeRelocation(A64, /*UNSIGNED=*/0).Class,
            DynCodeRelocationClass::ExternalAbsolute);
  EXPECT_EQ(mapDynCodeRelocation(A64, /*GOT_LOAD_PAGE21=*/5).Class,
            DynCodeRelocationClass::ExternalGOT);

  TargetDesc X64 = target(DynCodeArch::X86_64, ObjectFormat::MachO);
  EXPECT_EQ(mapDynCodeRelocation(X64, /*SIGNED=*/1).AddendAdjust, -4);
  EXPECT_EQ(mapDynCodeRelocation(X64, /*BRANCH=*/2).AddendAdjust, -4);
  EXPECT_EQ(mapDynCodeRelocation(X64, /*SIGNED_1=*/6).AddendAdjust, -5);
  EXPECT_EQ(mapDynCodeRelocation(X64, /*SIGNED_4=*/8).AddendAdjust, -8);
  EXPECT_EQ(mapDynCodeRelocation(X64, /*GOT=*/4).Class,
            DynCodeRelocationClass::ExternalGOT);
}

TEST(PluginDynCodeRelocationTest, DecodesNCRLExtension) {
  neverc::plugin::PluginObjectExtension Ext;
  Ext.Bytes = {'N', 'C', 'R', 'L', 1, 0, 0, 0,
               8,   7,   6,   5,   4, 3, 2, 1, 0, 0, 0, 0};
  std::optional<uint64_t> Type = decodeNativeRelocationType(Ext);
  ASSERT_TRUE(Type.has_value());
  EXPECT_EQ(*Type, UINT64_C(0x0102030405060708));

  neverc::plugin::PluginObjectExtension Bad;
  Bad.Bytes = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_FALSE(decodeNativeRelocationType(Bad).has_value());

  neverc::plugin::PluginObjectExtension Empty;
  EXPECT_FALSE(decodeNativeRelocationType(Empty).has_value());

  // The blob's fields are fixed-width and never reordered, so a version added
  // after this decoder was written still carries the native type in the same
  // place. Refusing anything but the version current at the time turned the
  // next field appended to the blob into a decode failure -- and a failure
  // here leaves the type at 0, which on Mach-O is the plain pointer form and
  // so reads back as a relocation the input never held.
  neverc::plugin::PluginObjectExtension Newer;
  Newer.Bytes = {'N', 'C', 'R', 'L', 7, 0, 0, 0, 8,   7,   6, 5,
                 4,   3,   2,   1,   0, 0, 0, 0, 'x', 'y', 0, 0};
  std::optional<uint64_t> FromNewer = decodeNativeRelocationType(Newer);
  ASSERT_TRUE(FromNewer.has_value())
      << "a newer blob version was refused although its fields are in place";
  EXPECT_EQ(*FromNewer, UINT64_C(0x0102030405060708));

  // A blob that stops before the field it is being asked for has nothing to
  // read, whatever its version says.
  neverc::plugin::PluginObjectExtension Short;
  Short.Bytes = {'N', 'C', 'R', 'L', 1, 0, 0, 0, 8, 7, 6};
  EXPECT_FALSE(decodeNativeRelocationType(Short).has_value());
}

TEST(PluginDynCodeRelocationTest, Lo12AutoHandlesAddAndLdst) {
  // ADD x0, x0, #imm at offset 0; LDR x0, [x0, #imm] (64-bit) at offset 4.
  std::vector<uint8_t> Bytes = le32(0x91000000);
  for (uint8_t B : le32(0xF9400000))
    Bytes.push_back(B);
  Bytes.resize(0x40, 0);
  const uint64_t Target = 0x18;

  std::vector<uint8_t> ViaExecutor = Bytes;
  DynCodeRelocationWork Add;
  Add.SiteOffset = 0;
  Add.TargetOffset = Target;
  Add.Kind = DynCodeRelocApplyKind::AArch64Lo12Auto;
  DynCodeRelocationWork Load;
  Load.SiteOffset = 4;
  Load.TargetOffset = Target;
  Load.Kind = DynCodeRelocApplyKind::AArch64Lo12Auto;
  ASSERT_FALSE(applyDynCodeRelocation(ViaExecutor, Add));
  ASSERT_FALSE(applyDynCodeRelocation(ViaExecutor, Load));

  std::vector<uint8_t> ViaRaw = Bytes;
  ASSERT_TRUE(patchArm64Lo12WithShift(ViaRaw, 0, Target, 0));    // ADD -> shift 0
  ASSERT_TRUE(patchArm64Lo12AutoShift(ViaRaw, 4, Target, true)); // LDR -> auto
  EXPECT_EQ(ViaExecutor, ViaRaw);
}

TEST(PluginDynCodeRelocationTest, ResolveAndApplyPatchesIntraImageX86) {
  DynCodeImage Image;
  std::vector<uint8_t> Init(0x40, 0x90);
  Init[0] = 0xE8; // call rel32; operand at offset 1
  ASSERT_FALSE(Image.append(Init));

  DynCodeExtractionPlan Plan;
  DynCodeRelocationEntry E;
  E.SiteOffset = 1;
  E.TargetOffset = 0x20;
  E.Addend = -4; // ELF PC32 end-relative addend
  E.Width = 4;
  E.NativeType = ELF::R_X86_64_PC32;
  E.Resolved = true;
  ASSERT_TRUE(static_cast<bool>(Plan.addRelocation(E)));

  TargetDesc T = target(DynCodeArch::X86_64, ObjectFormat::ELF);
  DynCodeReport Report;
  ASSERT_FALSE(resolveAndApplyDynCodeRelocations(Plan, T, Image, Report));

  // FinalAddr = 0x20 + (-4) = 0x1c; PCDisp = 0x1c - 1 = 0x1b at offset 1.
  int32_t Disp = 0;
  std::memcpy(&Disp, &Image.bytes()[1], 4);
  EXPECT_EQ(Disp, 0x1b);

  EXPECT_FALSE(verifyDynCodeRelocations(Plan, T, Image));
}

TEST(PluginDynCodeRelocationTest, ResolveAndApplyRejectsUnresolved) {
  DynCodeImage Image;
  ASSERT_FALSE(Image.append(std::vector<uint8_t>(0x20, 0x90)));

  DynCodeExtractionPlan Plan;
  DynCodeRelocationEntry E;
  E.SiteOffset = 4;
  E.Width = 4;
  E.NativeType = ELF::R_X86_64_PC32;
  E.Resolved = false; // external reference the extractor could not resolve
  ASSERT_TRUE(static_cast<bool>(Plan.addRelocation(E)));

  TargetDesc T = target(DynCodeArch::X86_64, ObjectFormat::ELF);
  DynCodeReport Report;
  auto Err = resolveAndApplyDynCodeRelocations(Plan, T, Image, Report);
  EXPECT_TRUE(static_cast<bool>(Err));
  consumeError(std::move(Err));
}

TEST(PluginDynCodeRelocationTest, ResolveAndApplyRejectsGOT) {
  DynCodeImage Image;
  ASSERT_FALSE(Image.append(std::vector<uint8_t>(0x40, 0x90)));

  DynCodeExtractionPlan Plan;
  DynCodeRelocationEntry E;
  E.SiteOffset = 1;
  E.TargetOffset = 0x20;
  E.Width = 4;
  E.NativeType = ELF::R_X86_64_GOTPCREL;
  E.Resolved = true; // resolved offset, but a GOT form dyncode cannot encode
  ASSERT_TRUE(static_cast<bool>(Plan.addRelocation(E)));

  TargetDesc T = target(DynCodeArch::X86_64, ObjectFormat::ELF);
  DynCodeReport Report;
  auto Err = resolveAndApplyDynCodeRelocations(Plan, T, Image, Report);
  EXPECT_TRUE(static_cast<bool>(Err));
  consumeError(std::move(Err));
}

TEST(PluginDynCodeRelocationTest, VerifierRejectsUnresolvedExternal) {
  DynCodeImage Image;
  ASSERT_FALSE(Image.append(std::vector<uint8_t>(16, 0x90)));

  DynCodeExtractionPlan Plan;
  DynCodeExternalContract C;
  C.Symbol = "ext";
  C.Disposition = DynCodeExternalDisposition::Unresolved;
  ASSERT_TRUE(static_cast<bool>(Plan.addExternalContract(C)));

  TargetDesc T = target(DynCodeArch::X86_64, ObjectFormat::ELF);
  auto Err = verifyDynCodeRelocations(Plan, T, Image);
  EXPECT_TRUE(static_cast<bool>(Err));
  consumeError(std::move(Err));
}

} // namespace
