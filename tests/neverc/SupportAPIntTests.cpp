//===- SupportAPIntTests.cpp - Arbitrary-width APInt operations -----------===//
//
// APInt promises arbitrary precision.  The CSupport boundary must therefore
// scale with the value's bit width rather than impose a private stack-buffer
// ceiling that is absent from APInt's public interface.
//
//===----------------------------------------------------------------------===//

#include "csupport/lapint.h"
#include "llvm/ADT/APInt.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace llvm;

TEST(SupportAPIntTest, ByteSwapAcceptsValuesWiderThanTwoKibibits) {
  constexpr unsigned BitWidth = 8192;
  const APInt Input = APInt::getOneBitSet(BitWidth, 0);

  const APInt Swapped = Input.byteSwap();

  EXPECT_EQ(Swapped, APInt::getOneBitSet(BitWidth, BitWidth - 8));
  EXPECT_EQ(Swapped.byteSwap(), Input);
}

TEST(SupportAPIntTest, RotationsAcceptValuesWiderThanFourKibibits) {
  constexpr unsigned BitWidth = 8192;
  constexpr unsigned Amount = 4097;
  APInt Input = APInt::getOneBitSet(BitWidth, 0);
  Input.setBit(4095);

  APInt Expected = APInt::getOneBitSet(BitWidth, 0);
  Expected.setBit(Amount);

  const APInt Rotated = Input.rotl(Amount);
  EXPECT_EQ(Rotated, Expected);
  EXPECT_EQ(Rotated.rotr(Amount), Input);
}

TEST(SupportAPIntTest, CSupportRotationsPermitInPlaceResults) {
  constexpr unsigned BitWidth = 130;
  constexpr unsigned Amount = 65;
  uint64_t Words[] = {1, 1, 0};

  csupport_apint_rotl(Words, Words, 3, BitWidth, Amount);
  EXPECT_EQ(Words[0], 0u);
  EXPECT_EQ(Words[1], 2u);
  EXPECT_EQ(Words[2], 2u);

  csupport_apint_rotr(Words, Words, 3, BitWidth, Amount);
  EXPECT_EQ(Words[0], 1u);
  EXPECT_EQ(Words[1], 1u);
  EXPECT_EQ(Words[2], 0u);
}

TEST(SupportAPIntTest, ConcatenationAcceptsResultsWiderThanFourKibibits) {
  constexpr unsigned HalfWidth = 4096;
  APInt High = APInt::getOneBitSet(HalfWidth, 0);
  High.setBit(HalfWidth - 1);
  APInt Low = APInt::getOneBitSet(HalfWidth, 1);
  Low.setBit(HalfWidth - 2);

  APInt Expected(HalfWidth * 2, 0);
  Expected.setBit(HalfWidth);
  Expected.setBit(HalfWidth * 2 - 1);
  Expected.setBit(1);
  Expected.setBit(HalfWidth - 2);

  EXPECT_EQ(High.concat(Low), Expected);
}

TEST(SupportAPIntTest, CSupportConcatenationPermitsAliasedInputs) {
  const uint64_t Low[] = {0x34};
  uint64_t Result[] = {0x12, 0};
  csupport_apint_concat(Result, 2, Result, 64, Low, 64);
  EXPECT_EQ(Result[0], 0x34u);
  EXPECT_EQ(Result[1], 0x12u);

  const uint64_t High[] = {0x56};
  Result[0] = 0x78;
  Result[1] = 0;
  csupport_apint_concat(Result, 2, High, 64, Result, 64);
  EXPECT_EQ(Result[0], 0x78u);
  EXPECT_EQ(Result[1], 0x56u);
}

TEST(SupportAPIntTest, EmptyInsertionAtEndDoesNotTouchStorage) {
  APInt Value(128, {UINT64_C(0x0123456789abcdef),
                    UINT64_C(0xfedcba9876543210)});
  const APInt Original = Value;

  Value.insertBits(UINT64_C(0xffffffffffffffff),
                   /*bitPosition=*/Value.getBitWidth(), /*numBits=*/0);

  EXPECT_EQ(Value, Original);
}

TEST(SupportAPIntTest, EmptyInsertionAtSingleWordEndDoesNotShiftByWordSize) {
  APInt Value(64, UINT64_C(0x0123456789abcdef));
  const APInt Original = Value;

  Value.insertBits(UINT64_MAX, /*bitPosition=*/Value.getBitWidth(),
                   /*numBits=*/0);

  EXPECT_EQ(Value, Original);

  uint64_t Word = Original.getZExtValue();
  csupport_apint_insert_bits64(&Word, /*dst_words=*/1, UINT64_MAX,
                               /*bit_position=*/64, /*num_bits=*/0);
  EXPECT_EQ(Word, Original.getZExtValue());
}

TEST(SupportAPIntTest, GreatestCommonDivisorAcceptsArbitraryWidths) {
  constexpr unsigned BitWidth = 8192;
  APInt Left(BitWidth, 48);
  Left.setBit(7000);
  APInt Right(BitWidth, 18);
  Right.setBit(7000);

  EXPECT_EQ(APIntOps::GreatestCommonDivisor(Left, Right), APInt(BitWidth, 2));
}

TEST(SupportAPIntTest, DoubleConversionAcceptsArbitraryWidths) {
  constexpr unsigned BitWidth = 4096;
  const APInt Positive = APInt::getOneBitSet(BitWidth, 100);
  const double Expected = std::ldexp(1.0, 100);

  EXPECT_EQ(Positive.roundToDouble(), Expected);
  EXPECT_EQ((-Positive).signedRoundToDouble(), -Expected);
}

TEST(SupportAPIntTest, DoubleConversionRoundsMidpointsToEven) {
  constexpr unsigned BitWidth = 256;
  APInt Midpoint = APInt::getOneBitSet(BitWidth, 100);
  Midpoint.setBit(48);
  Midpoint.setBit(47);

  const double Expected = std::ldexp(1.0, 100) + std::ldexp(1.0, 49);
  EXPECT_EQ(Midpoint.roundToDouble(), Expected);
  EXPECT_EQ((-Midpoint).signedRoundToDouble(), -Expected);
}
