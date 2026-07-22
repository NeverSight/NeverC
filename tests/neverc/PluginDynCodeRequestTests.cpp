// Volume 6 task 2: Session-level dyncode registry, request freeze and target
// descriptor. These tests exercise the built-in dyncode target adapters over
// all eight first-version triples in both user and kernel contexts, the
// registry conflict/validation rules, and deterministic request freezing.

#include "DynCodeRegistry.h"
#include "DynCodeRequest.h"

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include "gtest/gtest.h"

#include <cstring>

using namespace neverc;
using namespace neverc::dyncode;

namespace {

struct TripleExpectation {
  const char *Triple;
  const char *Section;
};

const TripleExpectation kTriples[] = {
    {"x86_64-apple-macosx", "__text"},
    {"aarch64-apple-macosx", "__text"},
    {"x86_64-unknown-linux-gnu", ".text"},
    {"aarch64-unknown-linux-gnu", ".text"},
    {"x86_64-unknown-linux-android29", ".text"},
    {"aarch64-unknown-linux-android29", ".text"},
    {"x86_64-pc-windows-msvc", ".text"},
    {"aarch64-pc-windows-msvc", ".text"},
};

bool consumeIfError(llvm::Error E) {
  const bool WasError = static_cast<bool>(E);
  if (E)
    llvm::consumeError(std::move(E));
  return WasError;
}

bool svEquals(NevercStringView V, llvm::StringRef S) {
  return llvm::StringRef(V.Data, V.Length) == S;
}

NevercObjectFormatID formatFor(const char *Triple) {
  const plugin::BuiltinTargetRoute *Route =
      plugin::findBuiltinTargetRoute(Triple);
  return Route ? Route->ObjectFormatID : NevercObjectFormatID{};
}

plugin::OwnedTargetKey makeKey(const char *Triple,
                               NevercTargetExecutionLevel Level) {
  const plugin::BuiltinTargetRoute *Route =
      plugin::findBuiltinTargetRoute(Triple);
  if (!Route) {
    ADD_FAILURE() << "no built-in route for " << Triple;
    return {};
  }
  llvm::Expected<plugin::OwnedTargetKey> Or = plugin::createBuiltinTargetKey(
      *Route, Triple, "", NEVERC_TARGET_RELOCATION_PIC,
      NEVERC_TARGET_CODE_MODEL_SMALL, Level);
  if (!Or) {
    llvm::consumeError(Or.takeError());
    ADD_FAILURE() << "createBuiltinTargetKey failed for " << Triple;
    return {};
  }
  return std::move(*Or);
}

DynCodeOptions baseOptions() {
  DynCodeOptions Opts;
  Opts.Enabled = true;
  Opts.Align = 16;
  return Opts;
}

TEST(PluginDynCodeRequestTargetTest, BuildsAllBuiltinTargetsUserAndKernel) {
  for (const TripleExpectation &T : kTriples) {
    for (NevercDynCodeExecutionLevel Level :
         {NEVERC_DYNCODE_LEVEL_USER, NEVERC_DYNCODE_LEVEL_KERNEL}) {
      llvm::Expected<OwnedDynCodeTargetDescriptor> DescOr =
          buildBuiltinDynCodeTarget(T.Triple, Level);
      ASSERT_TRUE(static_cast<bool>(DescOr))
          << "triple=" << T.Triple << " level=" << Level;
      const NevercDynCodeTargetDescriptor V = DescOr->view();

      EXPECT_TRUE(idNonzero(V.DynCodeTargetID)) << T.Triple;
      EXPECT_TRUE(idNonzero(V.ObjectFormat)) << T.Triple;
      EXPECT_TRUE(svEquals(V.CodeSectionName, T.Section)) << T.Triple;
      EXPECT_TRUE(svEquals(V.CodeSectionRole, "text")) << T.Triple;
      EXPECT_EQ(V.TargetSchemaDigest.Length, 64u) << T.Triple;
      EXPECT_GT(V.Target.RawTriple.Length, 0u) << T.Triple;
      EXPECT_NE(V.Flags & NEVERC_DYNCODE_TARGET_SUPPORTS_USER, 0u) << T.Triple;
      EXPECT_NE(V.Flags & NEVERC_DYNCODE_TARGET_SUPPORTS_KERNEL, 0u) << T.Triple;
      EXPECT_NE(V.PICConstraints & NEVERC_DYNCODE_PIC_REQUIRE_ENTRY_AT_ZERO, 0u)
          << T.Triple;
    }
  }
}

TEST(PluginDynCodeRequestTargetTest, RejectsUnsupportedTriple) {
  llvm::Expected<OwnedDynCodeTargetDescriptor> Or =
      buildBuiltinDynCodeTarget("riscv64-unknown-linux-gnu",
                                NEVERC_DYNCODE_LEVEL_USER);
  ASSERT_FALSE(static_cast<bool>(Or));
  llvm::consumeError(Or.takeError());
}

TEST(PluginDynCodeRequestRegistryTest, RegistersEightBuiltinTargets) {
  DynCodeRegistry Registry;
  ASSERT_FALSE(consumeIfError(Registry.registerBuiltinTargets()));
  EXPECT_EQ(Registry.targets().size(), 8u);

  for (const OwnedDynCodeTargetDescriptor &Desc : Registry.targets()) {
    EXPECT_NE(Registry.findTargetByDynCodeID(Desc.DynCodeTargetID), nullptr);
    EXPECT_NE(
        Registry.findTargetByKey(Desc.UnderlyingTargetID, Desc.ObjectFormat),
        nullptr);
  }
}

TEST(PluginDynCodeRequestRegistryTest, RejectsDuplicateAndConflictingTargets) {
  DynCodeRegistry Registry;

  llvm::Expected<OwnedDynCodeTargetDescriptor> First =
      buildBuiltinDynCodeTarget("x86_64-unknown-linux-gnu",
                                NEVERC_DYNCODE_LEVEL_USER);
  ASSERT_TRUE(static_cast<bool>(First));
  ASSERT_FALSE(consumeIfError(Registry.registerTarget(std::move(*First))));

  // Same dyncode target ID -> duplicate.
  llvm::Expected<OwnedDynCodeTargetDescriptor> Dup =
      buildBuiltinDynCodeTarget("x86_64-unknown-linux-gnu",
                                NEVERC_DYNCODE_LEVEL_USER);
  ASSERT_TRUE(static_cast<bool>(Dup));
  EXPECT_TRUE(consumeIfError(Registry.registerTarget(std::move(*Dup))));

  // Different dyncode target ID but same (TargetKey, format) -> conflict.
  llvm::Expected<OwnedDynCodeTargetDescriptor> Conflict =
      buildBuiltinDynCodeTarget("x86_64-unknown-linux-gnu",
                                NEVERC_DYNCODE_LEVEL_USER);
  ASSERT_TRUE(static_cast<bool>(Conflict));
  Conflict->DynCodeTargetID.Low ^= UINT64_C(0xABCD);
  EXPECT_TRUE(consumeIfError(Registry.registerTarget(std::move(*Conflict))));

  EXPECT_EQ(Registry.targets().size(), 1u);
}

TEST(PluginDynCodeRequestRegistryTest, RejectsInvalidSchemaDigest) {
  OwnedDynCodeTargetDescriptor Bad;
  Bad.DynCodeTargetID = {UINT64_C(1), UINT64_C(1)};
  Bad.UnderlyingTargetID = {UINT64_C(2), UINT64_C(2)};
  Bad.ObjectFormat = {UINT64_C(3), UINT64_C(3)};
  // Default OwnedTargetKey has an empty (invalid) schema digest.
  DynCodeRegistry Registry;
  EXPECT_TRUE(consumeIfError(Registry.registerTarget(std::move(Bad))));
  EXPECT_EQ(Registry.targets().size(), 0u);
}

TEST(PluginDynCodeRequestTest, FreezeIsDeterministicAndSensitive) {
  const char *Triple = "x86_64-unknown-linux-gnu";
  const NevercObjectFormatID Fmt = formatFor(Triple);
  const DynCodeOptions Opts = baseOptions();

  llvm::Expected<FrozenDynCodeRequest> R1 = freezeDynCodeRequest(
      Opts, makeKey(Triple, NEVERC_TARGET_EXECUTION_USER), Fmt);
  llvm::Expected<FrozenDynCodeRequest> R2 = freezeDynCodeRequest(
      Opts, makeKey(Triple, NEVERC_TARGET_EXECUTION_USER), Fmt);
  ASSERT_TRUE(static_cast<bool>(R1));
  ASSERT_TRUE(static_cast<bool>(R2));
  EXPECT_EQ(R1->Digest, R2->Digest);

  DynCodeOptions Entry = Opts;
  Entry.EntrySymbol = "custom_entry";
  llvm::Expected<FrozenDynCodeRequest> R3 = freezeDynCodeRequest(
      Entry, makeKey(Triple, NEVERC_TARGET_EXECUTION_USER), Fmt);
  ASSERT_TRUE(static_cast<bool>(R3));
  EXPECT_NE(R1->Digest, R3->Digest);

  DynCodeOptions Kernel = Opts;
  Kernel.Level = ExecutionLevel::Kernel;
  llvm::Expected<FrozenDynCodeRequest> R4 = freezeDynCodeRequest(
      Kernel, makeKey(Triple, NEVERC_TARGET_EXECUTION_KERNEL), Fmt);
  ASSERT_TRUE(static_cast<bool>(R4));
  EXPECT_NE(R1->Digest, R4->Digest);
}

TEST(PluginDynCodeRequestTest, FillsRequestInfoView) {
  const char *Triple = "aarch64-apple-macosx";
  const NevercObjectFormatID Fmt = formatFor(Triple);
  DynCodeOptions Opts = baseOptions();
  Opts.BadBytes = {0x0A, 0x00, 0x0A}; // duplicate + unsorted on purpose

  llvm::Expected<FrozenDynCodeRequest> Req = freezeDynCodeRequest(
      Opts, makeKey(Triple, NEVERC_TARGET_EXECUTION_USER), Fmt);
  ASSERT_TRUE(static_cast<bool>(Req));

  NevercDynCodeRequestInfo Info;
  std::vector<NevercStringView> Scratch;
  fillRequestInfo(*Req, Info, Scratch);

  EXPECT_EQ(Info.Header.StructSize, sizeof(NevercDynCodeRequestInfo));
  EXPECT_TRUE(idEqual(Info.ObjectFormat, Fmt));
  EXPECT_EQ(Info.ExecutionLevel, NEVERC_DYNCODE_LEVEL_USER);
  EXPECT_EQ(Info.Alignment, 16u);
  EXPECT_EQ(Info.Entry.Kind, NEVERC_DYNCODE_ENTRY_CANDIDATE_LIST);
  EXPECT_EQ(Info.BadByteCount, 2u); // deduplicated to {0x00, 0x0A}
  EXPECT_EQ(std::memcmp(Info.RequestDigest, Req->Digest.data(), 32), 0);
}

TEST(PluginDynCodeRequestTest, RejectsInconsistentOptions) {
  const char *Triple = "x86_64-pc-windows-msvc";
  const NevercObjectFormatID Fmt = formatFor(Triple);

  DynCodeOptions PadIsBad = baseOptions();
  PadIsBad.PadByte = 0x00;
  PadIsBad.BadBytes = {0x00};
  {
    llvm::Expected<FrozenDynCodeRequest> Or = freezeDynCodeRequest(
        PadIsBad, makeKey(Triple, NEVERC_TARGET_EXECUTION_USER), Fmt);
    ASSERT_FALSE(static_cast<bool>(Or));
    llvm::consumeError(Or.takeError());
  }

  DynCodeOptions BadAlign = baseOptions();
  BadAlign.Align = 3; // not a power of two
  {
    llvm::Expected<FrozenDynCodeRequest> Or = freezeDynCodeRequest(
        BadAlign, makeKey(Triple, NEVERC_TARGET_EXECUTION_USER), Fmt);
    ASSERT_FALSE(static_cast<bool>(Or));
    llvm::consumeError(Or.takeError());
  }
}

} // namespace
