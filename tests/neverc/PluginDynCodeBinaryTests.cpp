// The dyncode binary phase subsystem: bad-byte rewrite chain, charset encoder
// selection, structural binary verifier and the fixed byte-phase executor.
//
// These tests pin the topological ordering (and cycle rejection) of the rewrite
// chain, the exact-ID charset selection, the structural verifier's accept/
// reject rules, and the executor's guarantee that disabling the rewrite still
// runs the final audit (a disabled rewrite cannot smuggle a bad byte through).

#include "Binary/BuiltinDynCodeBinaryVerifier.h"
#include "Binary/DynCodeBinaryPhaseExecutor.h"
#include "Binary/DynCodeCharsetRegistry.h"
#include "Binary/DynCodeRewriteRegistry.h"

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::dyncode;

namespace {

DynCodeImage makeImage(std::vector<uint8_t> Bytes) {
  DynCodeImage Image;
  cantFail(Image.append(Bytes));
  Image.setEntry(0, "main");
  return Image;
}

DynCodeRewriteProvider orderTracker(std::string ID, std::string &Log,
                                    std::vector<std::string> After = {}) {
  DynCodeRewriteProvider P;
  P.ID = ID;
  P.After = std::move(After);
  P.Rewrite = [ID, &Log](DynCodeImage &, ArrayRef<uint8_t>) -> Expected<uint64_t> {
    Log += ID;
    return uint64_t{0};
  };
  return P;
}

Expected<uint64_t> stripBadBytes(DynCodeImage &Image, ArrayRef<uint8_t> Bad) {
  std::vector<uint8_t> Buf(Image.bytes().begin(), Image.bytes().end());
  bool Forbidden[256] = {};
  for (uint8_t B : Bad)
    Forbidden[B] = true;
  uint64_t Changed = 0;
  for (uint8_t &C : Buf)
    if (Forbidden[C]) {
      C = 0x90;
      ++Changed;
    }
  if (Changed)
    if (llvm::Error E = Image.write(0, Buf))
      return std::move(E);
  return Changed;
}

TEST(PluginDynCodeBinaryTest, RewriteChainRunsInTopologicalOrder) {
  std::string Log;
  DynCodeRewriteRegistry Reg;
  // Register in order A, B, C but constrain B->A->C via After lists.
  ASSERT_FALSE(Reg.registerProvider(orderTracker("A", Log, {"B"})));
  ASSERT_FALSE(Reg.registerProvider(orderTracker("B", Log, {})));
  ASSERT_FALSE(Reg.registerProvider(orderTracker("C", Log, {"A"})));

  DynCodeImage Image = makeImage({0x90, 0x90});
  uint64_t Changes = 0;
  ASSERT_FALSE(Reg.runChain(Image, {}, Changes));
  EXPECT_EQ(Log, "BAC");
  EXPECT_EQ(Changes, 0u);
}

TEST(PluginDynCodeBinaryTest, RewriteChainRejectsCycle) {
  std::string Log;
  DynCodeRewriteRegistry Reg;
  ASSERT_FALSE(Reg.registerProvider(orderTracker("A", Log, {"B"})));
  ASSERT_FALSE(Reg.registerProvider(orderTracker("B", Log, {"A"})));

  DynCodeImage Image = makeImage({0x90});
  uint64_t Changes = 0;
  auto E = Reg.runChain(Image, {}, Changes);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeBinaryTest, RewriteChainEnforcesMaxGrowth) {
  DynCodeRewriteRegistry Reg;
  DynCodeRewriteProvider P;
  P.ID = "grow";
  P.MaxGrowth = 0; // may not grow the image
  P.Rewrite = [](DynCodeImage &Img, ArrayRef<uint8_t>) -> Expected<uint64_t> {
    std::vector<uint8_t> One = {0x90};
    if (llvm::Error E = Img.append(One))
      return std::move(E);
    return uint64_t{1};
  };
  ASSERT_FALSE(Reg.registerProvider(std::move(P)));

  DynCodeImage Image = makeImage({0x90});
  uint64_t Changes = 0;
  auto E = Reg.runChain(Image, {}, Changes);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeBinaryTest, RewriteRegistryRejectsDuplicateID) {
  std::string Log;
  DynCodeRewriteRegistry Reg;
  ASSERT_FALSE(Reg.registerProvider(orderTracker("dup", Log)));
  auto E = Reg.registerProvider(orderTracker("dup", Log));
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeBinaryTest, CharsetRegistrySelectsByExactID) {
  DynCodeCharsetRegistry Reg;
  DynCodeCharsetProvider P;
  P.ID = "xor";
  P.Encode = [](DynCodeImage &, ArrayRef<uint8_t>) { return Error::success(); };
  ASSERT_FALSE(Reg.registerProvider(std::move(P)));

  EXPECT_NE(Reg.find("xor"), nullptr);
  EXPECT_EQ(Reg.find("XOR"), nullptr); // exact match only
  EXPECT_EQ(Reg.find("missing"), nullptr);

  DynCodeImage Image = makeImage({0x90});
  ASSERT_FALSE(Reg.run("xor", Image, {}));
  auto Unknown = Reg.run("missing", Image, {});
  EXPECT_TRUE(static_cast<bool>(Unknown));
  consumeError(std::move(Unknown));

  DynCodeCharsetProvider Dup;
  Dup.ID = "xor";
  Dup.Encode = [](DynCodeImage &, ArrayRef<uint8_t>) { return Error::success(); };
  auto E = Reg.registerProvider(std::move(Dup));
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeBinaryTest, VerifierAcceptsCleanRejectsBad) {
  DynCodeOptions Opts;
  Opts.BadBytes = {0x00};

  DynCodeImage Clean = makeImage({0x90, 0x90});
  EXPECT_FALSE(verifyDynCodeBinary(Clean, Opts));

  DynCodeImage Bad = makeImage({0x90, 0x00});
  auto E1 = verifyDynCodeBinary(Bad, Opts);
  EXPECT_TRUE(static_cast<bool>(E1));
  consumeError(std::move(E1));

  DynCodeImage OffEntry = makeImage({0x90, 0x90});
  OffEntry.setEntry(1, "main");
  auto E2 = verifyDynCodeBinary(OffEntry, Opts);
  EXPECT_TRUE(static_cast<bool>(E2));
  consumeError(std::move(E2));

  DynCodeOptions Sized;
  Sized.MaxLength = 1;
  DynCodeImage TooBig = makeImage({0x90, 0x90});
  auto E3 = verifyDynCodeBinary(TooBig, Sized);
  EXPECT_TRUE(static_cast<bool>(E3));
  consumeError(std::move(E3));

  DynCodeOptions Aligned;
  Aligned.Align = 4;
  DynCodeImage Misaligned = makeImage({0x90, 0x90});
  auto E4 = verifyDynCodeBinary(Misaligned, Aligned);
  EXPECT_TRUE(static_cast<bool>(E4));
  consumeError(std::move(E4));
}

TEST(PluginDynCodeBinaryTest, FirstBadByteReportsOffset) {
  std::vector<uint8_t> Bytes = {0x90, 0x90, 0x0d, 0x90};
  auto Hit = firstDynCodeBadByte(Bytes, {0x0d});
  ASSERT_TRUE(Hit.has_value());
  EXPECT_EQ(*Hit, 2u);
  EXPECT_FALSE(firstDynCodeBadByte(Bytes, {0x0a}).has_value());
}

TEST(PluginDynCodeBinaryTest, ExecutorRewritesThenVerifies) {
  DynCodeOptions Opts;
  Opts.BadBytes = {0x00};
  Opts.BadByteRewrite = true;

  DynCodeRewriteRegistry Rewrites;
  DynCodeRewriteProvider P;
  P.ID = "strip-nulls";
  P.MaxGrowth = 0;
  P.Rewrite = stripBadBytes;
  ASSERT_FALSE(Rewrites.registerProvider(std::move(P)));
  DynCodeCharsetRegistry Charsets;

  DynCodeImage Image = makeImage({0x90, 0x00, 0x90, 0x00});
  DynCodeReport Report;
  ASSERT_FALSE(
      runDynCodeBinaryPhases(Image, Report, Opts, Rewrites, Charsets));

  EXPECT_FALSE(firstDynCodeBadByte(Image.bytes(), Opts.BadBytes).has_value());
  EXPECT_EQ(Image.state(), DynCodeImageState::Verified);
}

TEST(PluginDynCodeBinaryTest, DisabledRewriteStillAudits) {
  DynCodeOptions Opts;
  Opts.BadBytes = {0x00};
  Opts.BadByteRewrite = false; // disabled -> explicit no-op

  DynCodeRewriteRegistry Rewrites;
  DynCodeRewriteProvider P;
  P.ID = "strip-nulls";
  P.Rewrite = stripBadBytes; // present, but disabled by the option
  ASSERT_FALSE(Rewrites.registerProvider(std::move(P)));
  DynCodeCharsetRegistry Charsets;

  DynCodeImage Image = makeImage({0x90, 0x00, 0x90});
  DynCodeReport Report;
  auto E = runDynCodeBinaryPhases(Image, Report, Opts, Rewrites, Charsets);
  EXPECT_TRUE(static_cast<bool>(E)); // the final audit still catches the 0x00
  consumeError(std::move(E));
  EXPECT_NE(Image.state(), DynCodeImageState::Verified);
}

TEST(PluginDynCodeBinaryTest, ExecutorUnknownCharsetFails) {
  DynCodeOptions Opts;
  Opts.Charset = "does-not-exist";

  DynCodeRewriteRegistry Rewrites;
  DynCodeCharsetRegistry Charsets;
  DynCodeImage Image = makeImage({0x90, 0x90});
  DynCodeReport Report;
  auto E = runDynCodeBinaryPhases(Image, Report, Opts, Rewrites, Charsets);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

TEST(PluginDynCodeBinaryTest, ExecutorPadsToAlignmentAndMaxLength) {
  DynCodeOptions Opts;
  Opts.Align = 8;
  Opts.MaxLength = 16;
  Opts.PadByte = 0x90;

  DynCodeRewriteRegistry Rewrites;
  DynCodeCharsetRegistry Charsets;
  DynCodeImage Image = makeImage({0x90, 0x90, 0x90});
  DynCodeReport Report;
  ASSERT_FALSE(
      runDynCodeBinaryPhases(Image, Report, Opts, Rewrites, Charsets));

  EXPECT_EQ(Image.size(), 16u);
  EXPECT_EQ(Image.paddingSize(), 13u);
  EXPECT_EQ(Image.state(), DynCodeImageState::Verified);
}

TEST(PluginDynCodeBinaryTest, ExecutorRejectsPadByteInBadSet) {
  DynCodeOptions Opts;
  Opts.Align = 8;
  Opts.PadByte = 0x00;
  Opts.BadBytes = {0x00}; // pad byte collides with the bad-byte set

  DynCodeRewriteRegistry Rewrites;
  DynCodeCharsetRegistry Charsets;
  DynCodeImage Image = makeImage({0x90, 0x90, 0x90});
  DynCodeReport Report;
  auto E = runDynCodeBinaryPhases(Image, Report, Opts, Rewrites, Charsets);
  EXPECT_TRUE(static_cast<bool>(E));
  consumeError(std::move(E));
}

} // namespace
