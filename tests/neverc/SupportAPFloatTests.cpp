//===- SupportAPFloatTests.cpp - Arbitrary-precision float boundaries ------===//
//
// APFloat accepts the full exponent range described by its semantics.  Its
// CSupport arithmetic must size temporary storage from that range rather than
// from the common double-precision case.
//
//===----------------------------------------------------------------------===//

#include "csupport/la_lp_lfloat.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using namespace llvm;

TEST(SupportAPFloatTest, DecimalExponentMustBeComplete) {
  APFloat Value(APFloat::IEEEquad());

  for (StringRef Input : {"1e", "1e+", "1e24000x"}) {
    Expected<APFloat::opStatus> Status =
        Value.convertFromString(Input, APFloat::rmNearestTiesToEven);
    EXPECT_FALSE(Status) << Input.str();
    if (!Status)
      consumeError(Status.takeError());
  }
}

TEST(SupportAPFloatTest, HexadecimalExponentMustBeComplete) {
  APFloat Value(APFloat::IEEEquad());
  Expected<APFloat::opStatus> Status =
      Value.convertFromString("0x1p40000x", APFloat::rmNearestTiesToEven);

  EXPECT_FALSE(Status);
  if (!Status)
    consumeError(Status.takeError());
}

TEST(SupportAPFloatTest, HugeDecimalExponentOverflowsWithoutCorruptingMemory) {
  APFloat Value(APFloat::IEEEquad());
  Expected<APFloat::opStatus> Status =
      Value.convertFromString("1e16000", APFloat::rmNearestTiesToEven);

  ASSERT_TRUE(static_cast<bool>(Status));
  EXPECT_TRUE(*Status & APFloat::opOverflow);
  EXPECT_TRUE(Value.isInfinity());
}

TEST(SupportAPFloatTest, PowerOfFiveSupportsLargestBuiltInSemanticExponent) {
  constexpr unsigned Power = 16495;
  constexpr unsigned BitWidth = 40000;
  constexpr unsigned WordCount = BitWidth / 64;

  APInt Expected(BitWidth, 1);
  APInt Base(BitWidth, 5);
  for (unsigned Remaining = Power; Remaining != 0; Remaining >>= 1) {
    if (Remaining & 1)
      Expected *= Base;
    if (Remaining > 1)
      Base *= Base;
  }

  std::vector<uint64_t> Words(WordCount);
  const unsigned UsedWords =
      csupport_apfloat_power_of5(Words.data(), Words.size(), Power);
  ASSERT_LE(UsedWords, Words.size());
  const APInt Actual(BitWidth, ArrayRef<uint64_t>(Words));

  EXPECT_EQ(Actual, Expected);
}

TEST(SupportAPFloatTest, PowerOfFiveRejectsInsufficientStorage) {
  uint64_t Word = UINT64_C(0xfeedface);

  EXPECT_EQ(csupport_apfloat_power_of5(&Word, /*dst_parts=*/1,
                                      /*power=*/100),
            0u);
  EXPECT_EQ(Word, UINT64_C(0xfeedface));
}

TEST(SupportAPFloatTest, FormattingHandlesMinimumExponentWithoutOverflow) {
  char Text[32] = {};
  const size_t Length = csupport_apfloat_format_to_string(
      "1", 1, std::numeric_limits<int>::min(), /*format_precision=*/0,
      /*format_max_padding=*/1, /*truncate_zero=*/1, /*is_negative=*/0, Text,
      sizeof(Text));

  EXPECT_EQ(StringRef(Text, Length), "1.0E-2147483648");
}

TEST(SupportAPFloatTest, HexFormattingHandlesMinimumExponentWithoutOverflow) {
  const uint64_t Significand = UINT64_C(1) << 3;
  char Text[32] = {};
  const size_t Length = csupport_apfloat_format_hex(
      &Significand, /*part_count=*/1, /*precision=*/4,
      std::numeric_limits<int>::min(), /*sign=*/0, /*uppercase=*/0, Text,
      sizeof(Text));

  EXPECT_EQ(StringRef(Text, Length), "0x8p-2147483648");
}

TEST(SupportAPFloatTest, SignificandMultiplicationSizesTheFullProduct) {
  uint64_t Left[] = {0, 0, 1};
  const uint64_t Right[] = {0, 0, 1};
  int Exponent = 0;

  EXPECT_EQ(csupport_apfloat_multiply_significand_simple(
                Left, Right, /*parts_count=*/3, /*precision=*/129, &Exponent),
            CSUPPORT_LF_EXACTLY_ZERO);
  EXPECT_EQ(ArrayRef<uint64_t>(Left), ArrayRef<uint64_t>(Right));
  EXPECT_EQ(Exponent, 0);

  uint64_t Word[] = {UINT64_C(1) << 63, 0};
  const uint64_t Other[] = {UINT64_C(1) << 63, 0};
  EXPECT_EQ(csupport_apfloat_multiply_significand_simple(
                Word, Other, /*parts_count=*/2, /*precision=*/64, &Exponent),
            CSUPPORT_LF_EXACTLY_ZERO);
  EXPECT_EQ(ArrayRef<uint64_t>(Word), ArrayRef<uint64_t>(Other));
  EXPECT_EQ(Exponent, 0);
}

TEST(SupportAPFloatTest, AllOnesExceptLsbChecksEveryMultiwordFractionBit) {
  // IEEE quad has 112 fraction bits.  Bit 0 is the one exception; bit 64 is
  // the first bit in the second word and must not be mistaken for another LSB.
  uint64_t Significand[] = {
      UINT64_MAX & ~UINT64_C(1),
      (UINT64_C(1) << 48) - 1,
  };
  EXPECT_TRUE(csupport_apfloat_is_significand_all_ones_except_lsb(
      Significand, /*precision=*/113));

  Significand[1] &= ~UINT64_C(1);
  EXPECT_FALSE(csupport_apfloat_is_significand_all_ones_except_lsb(
      Significand, /*precision=*/113));
}
