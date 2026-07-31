//===- SupportCachePruningTests.cpp - Policy parsing regressions ----------===//

#include "csupport/lcache_lpruning.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Error.h"

#include <gtest/gtest.h>

using namespace llvm;

namespace {

void expectInvalidPolicy(StringRef Text) {
  auto Policy = parseCachePruningPolicy(Text);
  EXPECT_FALSE(Policy) << Text.str();
  if (!Policy)
    consumeError(Policy.takeError());
}

} // namespace

TEST(SupportCachePruningTest, RejectsOverflowingDurations) {
  expectInvalidPolicy("prune_interval=18446744073709551616s");
  expectInvalidPolicy("prune_interval=2562047788015216h");
  expectInvalidPolicy("prune_after=153722867280912931m");
}

TEST(SupportCachePruningTest, RejectsEmptyValues) {
  expectInvalidPolicy("prune_interval=");
  expectInvalidPolicy("prune_after=");
  expectInvalidPolicy("cache_size=");
  expectInvalidPolicy("cache_size_bytes=");
  expectInvalidPolicy("cache_size_files=");
}

TEST(SupportCachePruningTest, PercentageRequiresAtLeastOneDigit) {
  unsigned Percentage = 37;
  EXPECT_FALSE(
      csupport_cache_pruning_parse_percentage("%", 1, &Percentage));
  EXPECT_EQ(Percentage, 37u);
  EXPECT_FALSE(
      csupport_cache_pruning_parse_percentage(nullptr, 0, &Percentage));
  EXPECT_EQ(Percentage, 37u);
}

TEST(SupportCachePruningTest, RejectsOverflowingByteSizes) {
  expectInvalidPolicy("cache_size_bytes=18446744073709551616");
  expectInvalidPolicy("cache_size_bytes=18014398509481984k");
  expectInvalidPolicy("cache_size_bytes=17592186044416m");
  expectInvalidPolicy("cache_size_bytes=17179869184g");
}
