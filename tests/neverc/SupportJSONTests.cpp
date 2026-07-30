#include "csupport/lj_ls_lo_ln.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

TEST(SupportJSONTest, ParseDoubleConsumesLongNumber) {
  const std::string Number = "1." + std::string(96, '0') + "1e2";
  double Value = 0.0;

  EXPECT_EQ(csupport_json_parse_double(Number.data(), Number.size(), &Value),
            Number.size());
  EXPECT_DOUBLE_EQ(Value, 100.0);
}

TEST(SupportJSONTest, ParseInt64ConsumesLongLeadingZeroSequence) {
  const std::string Number = std::string(96, '0') + "42";
  int64_t Value = 0;

  EXPECT_EQ(csupport_json_parse_int64(Number.data(), Number.size(), &Value),
            Number.size());
  EXPECT_EQ(Value, 42);
}

TEST(SupportJSONTest, ParseNumberExDoesNotClassifyTruncatedPrefix) {
  const std::string Number(128, '9');
  int64_t Signed = 0;
  uint64_t Unsigned = 0;
  double Floating = 0.0;
  int Type = 0;

  EXPECT_EQ(csupport_json_parse_number_ex(Number.data(), Number.size(), &Signed,
                                         &Unsigned, &Floating, &Type),
            Number.size());
  EXPECT_EQ(Type, 3);
  EXPECT_TRUE(std::isfinite(Floating));
}

TEST(SupportJSONTest, LongNumberStillStopsAtFirstNonNumber) {
  const std::string Number = "1." + std::string(96, '0') + "1 trailing";
  double Value = 0.0;
  const size_t Expected = Number.find(' ');

  EXPECT_EQ(csupport_json_parse_double(Number.data(), Number.size(), &Value),
            Expected);
}

TEST(SupportJSONTest, FailedConversionsDoNotPublishPartialValues) {
  double Floating = 7.0;
  int64_t Integer = 7;

  EXPECT_EQ(csupport_json_parse_double("1e99999", 7, &Floating), 0u);
  EXPECT_DOUBLE_EQ(Floating, 7.0);

  constexpr char Overflow[] = "999999999999999999999999999999999999";
  EXPECT_EQ(csupport_json_parse_int64(Overflow, sizeof(Overflow) - 1, &Integer),
            0u);
  EXPECT_EQ(Integer, 7);

  uint64_t Unsigned = 7;
  int Type = -1;
  EXPECT_EQ(csupport_json_parse_number_ex("+", 1, &Integer, &Unsigned,
                                         &Floating, &Type),
            0u);
  EXPECT_EQ(Type, 0);
  EXPECT_EQ(Integer, 7);
  EXPECT_EQ(Unsigned, 7u);
  EXPECT_DOUBLE_EQ(Floating, 7.0);
}

TEST(SupportJSONTest, UnicodeHelpersRejectNonScalarAndMalformedUTF8) {
  char Encoded[4] = {};
  EXPECT_EQ(csupport_json_encode_utf8(0xd800, Encoded, sizeof(Encoded)), 0);
  EXPECT_EQ(csupport_json_encode_utf8(0x110000, Encoded, sizeof(Encoded)), 0);

  uint16_t Hex = 0x1234;
  EXPECT_EQ(csupport_json_decode_hex4("12x4", &Hex), -1);
  EXPECT_EQ(Hex, 0x1234);

  const char Overlong[] = {char(0xc0), char(0x80)};
  const char Surrogate[] = {char(0xed), char(0xa0), char(0x80)};
  const char TooLarge[] = {char(0xf4), char(0x90), char(0x80), char(0x80)};
  EXPECT_FALSE(csupport_json_validate_utf8(Overlong, sizeof(Overlong)));
  EXPECT_FALSE(csupport_json_validate_utf8(Surrogate, sizeof(Surrogate)));
  EXPECT_FALSE(csupport_json_is_valid_utf8(TooLarge, sizeof(TooLarge)));
  EXPECT_TRUE(csupport_json_validate_utf8("\xf0\x9f\x98\x80", 4));
}

TEST(SupportJSONTest, ValidatorChecksStructureNumbersAndEscapes) {
  const std::string Valid =
      R"({"name":"NeverC","values":[0,-1.5e+2,true,null]})";
  EXPECT_TRUE(csupport_json_validate(Valid.data(), Valid.size()));
  EXPECT_EQ(csupport_json_count_keys(Valid.data(), Valid.size()), 2u);

  for (const std::string Invalid :
       {"", "}{", R"({"a":})", R"({"a":1,})", "[01]",
        R"({"a":"\q"})", R"([true false])"})
    EXPECT_FALSE(csupport_json_validate(Invalid.data(), Invalid.size()))
        << Invalid;
}

TEST(SupportJSONTest, JSONPointerTraversesDecodedObjectKeysAndArrays) {
  const std::string JSON = R"( {"a/b":{"~key":[10,{"x":"ok"}]}} )";
  const char *Value = nullptr;
  constexpr char Pointer[] = "/a~1b/~0key/1/x";

  size_t Length = csupport_json_pointer_get(
      JSON.data(), JSON.size(), Pointer, sizeof(Pointer) - 1, &Value);
  ASSERT_NE(Value, nullptr);
  EXPECT_EQ(std::string(Value, Length), R"("ok")");

  Value = nullptr;
  EXPECT_EQ(csupport_json_pointer_get(JSON.data(), JSON.size(), "/a~2b", 5,
                                     &Value),
            0u);
  EXPECT_EQ(Value, nullptr);
}

TEST(SupportJSONTest, BufferWritersReportCompleteLengthWithoutOverflow) {
  char Small[4];
  const size_t Needed =
      csupport_json_quote_string("a\nb", 3, Small, sizeof(Small));
  EXPECT_EQ(Needed, 6u);
  EXPECT_STREQ(Small, "\"a\\");

  char Pretty[8];
  EXPECT_EQ(csupport_json_prettify("}", 1, Pretty, sizeof(Pretty), 2),
            std::numeric_limits<size_t>::max());
  EXPECT_EQ(Pretty[sizeof(Pretty) - 1], '\0');
}
