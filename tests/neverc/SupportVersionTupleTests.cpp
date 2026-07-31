//===- SupportVersionTupleTests.cpp - Version parsing regressions ---------===//

#include "llvm/Support/VersionTuple.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

using namespace llvm;

TEST(SupportVersionTupleTest, RejectsComponentsThatOverflowUnsigned) {
  const uint64_t Max = std::numeric_limits<unsigned>::max();
  const std::string Largest = std::to_string(Max);
  const std::string Overflow = std::to_string(Max + 1);

  VersionTuple Parsed;
  EXPECT_FALSE(Parsed.tryParse(Largest));
  EXPECT_EQ(Parsed.getMajor(), std::numeric_limits<unsigned>::max());

  for (const std::string &Text :
       {Overflow, "1." + Overflow, "1.2." + Overflow,
        "1.2.3." + Overflow}) {
    SCOPED_TRACE(Text);
    EXPECT_TRUE(Parsed.tryParse(Text));
  }
}
