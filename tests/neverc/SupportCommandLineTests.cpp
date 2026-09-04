//===- SupportCommandLineTests.cpp - Command-line boundary contracts ------===//
//
// Command-line input is externally supplied and has no private implementation
// size limit.  Tokenization must preserve every byte, and no-copy tokenization
// must still give transformed tokens stable storage.
//
//===----------------------------------------------------------------------===//

#include "csupport/lcommand_lline.h"
#include "csupport/stringref.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/WithColor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <string>

using namespace llvm;

namespace {

constexpr size_t LongInputSize = 20000;

struct TokenStorage {
  BumpPtrAllocator Alloc;
  StringSaver Saver{Alloc};
};

struct CapturedGnuToken {
  const char *Data = nullptr;
  std::string Text;
  bool Transient = false;
};

struct GnuTokenCapture {
  SmallVector<CapturedGnuToken, 16> Tokens;
  size_t EndOfLineCount = 0;
};

void captureGnuToken(const char *Data, size_t Length, int Transient,
                     void *Opaque) {
  auto &Capture = *static_cast<GnuTokenCapture *>(Opaque);
  Capture.Tokens.push_back({Data, std::string(Data, Length), Transient != 0});
}

void captureGnuEndOfLine(void *Opaque) {
  ++static_cast<GnuTokenCapture *>(Opaque)->EndOfLineCount;
}

uint64_t extendTokenDigest(uint64_t Digest, StringRef Token) {
  constexpr uint64_t FnvPrime = 1099511628211ULL;
  for (unsigned Byte = 0; Byte != sizeof(size_t); ++Byte) {
    Digest ^= (Token.size() >> (Byte * 8)) & 0xff;
    Digest *= FnvPrime;
  }
  for (unsigned char Byte : Token.bytes()) {
    Digest ^= Byte;
    Digest *= FnvPrime;
  }
  return Digest;
}

uint64_t capturedTokenDigest(ArrayRef<CapturedGnuToken> Tokens) {
  uint64_t Digest = 14695981039346656037ULL;
  for (const CapturedGnuToken &Token : Tokens)
    Digest = extendTokenDigest(Digest, Token.Text);
  return Digest;
}

uint64_t expectedTokenDigest(ArrayRef<std::string> Tokens) {
  uint64_t Digest = 14695981039346656037ULL;
  for (const std::string &Token : Tokens)
    Digest = extendTokenDigest(Digest, Token);
  return Digest;
}

} // namespace

TEST(SupportCommandLineTest, LazyColorOptionHasAValidPolymorphicLifetime) {
  initWithColorOptions();

  auto &Options = cl::getRegisteredOptions();
  auto Color = Options.find("color");
  ASSERT_NE(Color, Options.end());
  EXPECT_EQ(Color->second->ArgStr, "color");
  EXPECT_EQ(Color->second->getValueExpectedFlag(), cl::ValueOptional);
}

TEST(SupportCommandLineTest, GNUTokenizerPreservesLongAndEmptyArguments) {
  TokenStorage Storage;
  const std::string LongArgument(LongInputSize, 'g');
  const std::string CommandLine = LongArgument + R"( "" "")";
  SmallVector<const char *, 4> Arguments;

  cl::TokenizeGNUCommandLine(CommandLine, Storage.Saver, Arguments,
                             /*MarkEOLs=*/false);

  ASSERT_EQ(Arguments.size(), 3u);
  EXPECT_EQ(StringRef(Arguments[0]), LongArgument);
  EXPECT_EQ(StringRef(Arguments[1]), "");
  EXPECT_EQ(StringRef(Arguments[2]), "");
}

TEST(SupportCommandLineTest, CGNUTokenizerBorrowsLargeSimpleArguments) {
  constexpr size_t TokenCount = 4096;
  std::string Input;
  SmallVector<std::string, 0> Expected;
  SmallVector<size_t, 0> Offsets;
  size_t ExpectedEndOfLines = 0;
  Expected.reserve(TokenCount);
  Offsets.reserve(TokenCount);

  for (size_t I = 0; I != TokenCount; ++I) {
    if (I != 0) {
      const bool UseNewline = I % 17 == 0;
      Input.push_back(UseNewline ? '\n' : ' ');
      ExpectedEndOfLines += UseNewline;
    }
    Offsets.push_back(Input.size());
    Expected.push_back("argument-" + std::to_string(I) + "=" +
                       std::string(24, static_cast<char>('a' + I % 26)));
    Input += Expected.back();
  }

  GnuTokenCapture Capture;
  ASSERT_TRUE(csupport_cl_tokenize_gnu_impl(Input.data(), Input.size(),
                                            captureGnuToken,
                                            captureGnuEndOfLine, &Capture));

  ASSERT_EQ(Capture.Tokens.size(), TokenCount);
  EXPECT_EQ(Capture.EndOfLineCount, ExpectedEndOfLines);
  EXPECT_EQ(capturedTokenDigest(Capture.Tokens), expectedTokenDigest(Expected));
  for (size_t I = 0; I != TokenCount; ++I) {
    EXPECT_EQ(Capture.Tokens[I].Text, Expected[I]);
    ASSERT_FALSE(Capture.Tokens[I].Transient);
    ASSERT_EQ(Capture.Tokens[I].Data, Input.data() + Offsets[I]);
  }
}

TEST(SupportCommandLineTest,
     CGNUTokenizerOnlyCopiesArgumentsThatNeedTransformation) {
  std::string Input =
      "plain \"two words\" '' escaped\\ value # ignored \"'\\\\\n"
      "next raw#hash continued\\\nline";
  Input.push_back('\0');
  Input += "after-nul \"unterminated";

  GnuTokenCapture Capture;
  ASSERT_TRUE(csupport_cl_tokenize_gnu_impl(Input.data(), Input.size(),
                                            captureGnuToken,
                                            captureGnuEndOfLine, &Capture));

  const StringRef Expected[] = {"plain",         "two words", "",
                                "escaped value", "next",      "raw#hash",
                                "continuedline", "after-nul", "unterminated"};
  const bool ExpectedTransient[] = {false, true, true,  true, false,
                                    false, true, false, true};
  ASSERT_EQ(Capture.Tokens.size(), std::size(Expected));
  EXPECT_EQ(Capture.EndOfLineCount, 1U)
      << "a backslash-newline continuation is not an end-of-line marker";
  for (size_t I = 0; I != std::size(Expected); ++I) {
    EXPECT_EQ(Capture.Tokens[I].Text, Expected[I]);
    EXPECT_EQ(Capture.Tokens[I].Transient, ExpectedTransient[I]);
  }

  ASSERT_FALSE(Capture.Tokens[0].Transient);
  ASSERT_FALSE(Capture.Tokens[4].Transient);
  ASSERT_FALSE(Capture.Tokens[5].Transient);
  ASSERT_FALSE(Capture.Tokens[7].Transient);
  EXPECT_EQ(Capture.Tokens[0].Data, Input.data() + Input.find("plain"));
  EXPECT_EQ(Capture.Tokens[4].Data, Input.data() + Input.find("next"));
  EXPECT_EQ(Capture.Tokens[5].Data, Input.data() + Input.find("raw#hash"));
  EXPECT_EQ(Capture.Tokens[7].Data, Input.data() + Input.find("after-nul"));
}

TEST(SupportCommandLineTest, CGNUTokenizerBorrowsAnUnescapedTrailingBackslash) {
  const std::string Input = "literal-backslash\\";
  GnuTokenCapture Capture;

  ASSERT_TRUE(csupport_cl_tokenize_gnu_impl(Input.data(), Input.size(),
                                            captureGnuToken,
                                            /*mark_eol=*/nullptr, &Capture));

  ASSERT_EQ(Capture.Tokens.size(), 1U);
  EXPECT_EQ(Capture.Tokens.front().Text, Input);
  ASSERT_FALSE(Capture.Tokens.front().Transient);
  EXPECT_EQ(Capture.Tokens.front().Data, Input.data());
}

TEST(SupportCommandLineTest, CTokenizerNeverPublishesTruncatedToken) {
  constexpr StringLiteral Input = "long";
  char Truncated[Input.size()] = {};
  const char *Tokens[1] = {Input.data()};
  size_t TokenCount = 0;

  const size_t Required = csupport_cl_tokenize_gnu(
      Input.data(), Input.size(), Truncated, sizeof(Truncated), Tokens,
      std::size(Tokens), &TokenCount, /*mark_eols=*/0);

  EXPECT_EQ(Required, Input.size() + 1);
  ASSERT_EQ(TokenCount, 1u);
  EXPECT_EQ(Tokens[0], nullptr);

  char Complete[Input.size() + 1] = {};
  EXPECT_EQ(csupport_cl_tokenize_gnu(
                Input.data(), Input.size(), Complete, sizeof(Complete), Tokens,
                std::size(Tokens), &TokenCount, /*mark_eols=*/0),
            sizeof(Complete));
  ASSERT_NE(Tokens[0], nullptr);
  EXPECT_STREQ(Tokens[0], Input.data());
}

TEST(SupportCommandLineTest, WindowsTokenizerPreservesLongAndEmptyArguments) {
  TokenStorage Storage;
  const std::string LongArgument(LongInputSize, 'w');
  const std::string CommandLine = '"' + LongArgument + R"(" "" "")";
  SmallVector<const char *, 4> Arguments;

  cl::TokenizeWindowsCommandLine(CommandLine, Storage.Saver, Arguments,
                                 /*MarkEOLs=*/false);

  ASSERT_EQ(Arguments.size(), 3u);
  EXPECT_EQ(StringRef(Arguments[0]), LongArgument);
  EXPECT_EQ(StringRef(Arguments[1]), "");
  EXPECT_EQ(StringRef(Arguments[2]), "");
}

TEST(SupportCommandLineTest, WindowsNoCopyOwnsTransformedArguments) {
  TokenStorage Storage;
  SmallVector<StringRef, 1> First;
  SmallVector<StringRef, 1> Second;

  cl::TokenizeWindowsCommandLineNoCopy(R"("first value")", Storage.Saver,
                                       First);
  ASSERT_EQ(First.size(), 1u);
  const StringRef FirstToken = First.front();

  cl::TokenizeWindowsCommandLineNoCopy(R"("replacement value")", Storage.Saver,
                                       Second);

  ASSERT_EQ(Second.size(), 1u);
  EXPECT_EQ(FirstToken, "first value");
  EXPECT_EQ(Second.front(), "replacement value");
}

TEST(SupportCommandLineTest, WindowsNoCopyKeepsUnmodifiedArgumentsInPlace) {
  TokenStorage Storage;
  std::string CommandLine = "unmodified";
  SmallVector<StringRef, 1> Arguments;

  cl::TokenizeWindowsCommandLineNoCopy(CommandLine, Storage.Saver, Arguments);

  ASSERT_EQ(Arguments.size(), 1u);
  EXPECT_EQ(Arguments.front(), CommandLine);
  EXPECT_EQ(Arguments.front().data(), CommandLine.data());
}

TEST(SupportCommandLineTest, WindowsTokenizerPreservesLongBackslashRuns) {
  TokenStorage Storage;
  const std::string CommandLine = std::string(600, '\\') + R"("value)";
  const std::string Expected = std::string(300, '\\') + "value";
  SmallVector<const char *, 1> Arguments;

  cl::TokenizeWindowsCommandLine(CommandLine, Storage.Saver, Arguments,
                                 /*MarkEOLs=*/false);

  ASSERT_EQ(Arguments.size(), 1u);
  EXPECT_EQ(StringRef(Arguments.front()), Expected);
}

TEST(SupportCommandLineTest, GNUTokenizerRetainsSyntaxAndLineMarkers) {
  TokenStorage Storage;
  SmallVector<const char *, 8> Arguments;

  cl::TokenizeGNUCommandLine(
      R"(plain "two words" '' escaped\ value # comment
next)",
      Storage.Saver, Arguments, /*MarkEOLs=*/true);

  ASSERT_EQ(Arguments.size(), 6u);
  EXPECT_EQ(StringRef(Arguments[0]), "plain");
  EXPECT_EQ(StringRef(Arguments[1]), "two words");
  EXPECT_EQ(StringRef(Arguments[2]), "");
  EXPECT_EQ(StringRef(Arguments[3]), "escaped value");
  EXPECT_EQ(Arguments[4], nullptr);
  EXPECT_EQ(StringRef(Arguments[5]), "next");
}

TEST(SupportCommandLineTest, ConfigTokenizerPreservesLongLines) {
  TokenStorage Storage;
  const std::string Argument = "--long=" + std::string(LongInputSize, 'c');
  const std::string Config = "# comment\n" + Argument + "\n";
  SmallVector<const char *, 2> Arguments;

  cl::tokenizeConfigFile(Config, Storage.Saver, Arguments,
                         /*MarkEOLs=*/false);

  ASSERT_EQ(Arguments.size(), 1u);
  EXPECT_EQ(StringRef(Arguments.front()), Argument);
}

TEST(SupportCommandLineTest, ArgumentPrefixHonorsRequestedPadding) {
  constexpr size_t Padding = 80;
  const SmallString<8> Prefix = cl::argPrefix("long-option", Padding);

  EXPECT_EQ(Prefix, std::string(Padding, ' ') + "--");
}

TEST(SupportCommandLineTest, NumericParsersConsumeTheWholeInput) {
  // The integer parsers retain their original C-style radix autodetection, so
  // the long auto-radix spelling uses octal 52 for decimal 42.
  const std::string LongAutoInteger = std::string(96, '0') + "52";
  const std::string LongDecimalInteger = std::string(96, '0') + "42";
  const std::string InvalidInteger = std::string(96, '0') + "8x";
  const std::string LongDouble = "1." + std::string(160, '0') + "1e2";

  int IntValue = -1;
  unsigned UnsignedValue = 0;
  uint64_t UInt64Value = 0;
  int64_t Int64Value = -1;
  long long NumericValue = -1;
  double DoubleValue = 0.0;

  ASSERT_TRUE(csupport_cl_parse_int(LongAutoInteger.data(),
                                    LongAutoInteger.size(), &IntValue));
  EXPECT_EQ(IntValue, 42);
  ASSERT_TRUE(csupport_cl_parse_unsigned(
      LongAutoInteger.data(), LongAutoInteger.size(), &UnsignedValue));
  EXPECT_EQ(UnsignedValue, 42u);
  ASSERT_TRUE(csupport_cl_parse_uint64(LongAutoInteger.data(),
                                       LongAutoInteger.size(), &UInt64Value));
  EXPECT_EQ(UInt64Value, 42u);
  ASSERT_TRUE(csupport_cl_parse_int64(LongAutoInteger.data(),
                                      LongAutoInteger.size(), &Int64Value));
  EXPECT_EQ(Int64Value, 42);
  ASSERT_TRUE(csupport_cl_parse_numeric_option(
      LongDecimalInteger.data(), LongDecimalInteger.size(), &NumericValue));
  EXPECT_EQ(NumericValue, 42);
  ASSERT_TRUE(csupport_cl_parse_double(LongDouble.data(), LongDouble.size(),
                                       &DoubleValue));
  EXPECT_DOUBLE_EQ(DoubleValue, 100.0);
  ASSERT_TRUE(csupport_cl_parse_double_ex(LongDouble.data(), LongDouble.size(),
                                          &DoubleValue));
  EXPECT_DOUBLE_EQ(DoubleValue, 100.0);

  EXPECT_FALSE(csupport_cl_parse_int(InvalidInteger.data(),
                                     InvalidInteger.size(), &IntValue));
  EXPECT_FALSE(csupport_cl_parse_unsigned(
      InvalidInteger.data(), InvalidInteger.size(), &UnsignedValue));
  EXPECT_FALSE(csupport_cl_parse_uint64(InvalidInteger.data(),
                                        InvalidInteger.size(), &UInt64Value));
  EXPECT_FALSE(csupport_cl_parse_numeric_option(
      InvalidInteger.data(), InvalidInteger.size(), &NumericValue));

  const std::string Overflow = "999999999999999999999999999999999999";
  IntValue = 7;
  EXPECT_FALSE(
      csupport_cl_parse_int(Overflow.data(), Overflow.size(), &IntValue));
  EXPECT_EQ(IntValue, 7);

  UnsignedValue = 7;
  EXPECT_FALSE(csupport_cl_parse_unsigned("-1", 2, &UnsignedValue));
  EXPECT_EQ(UnsignedValue, 7u);

  DoubleValue = 7.0;
  EXPECT_FALSE(csupport_cl_parse_double("1e99999", 7, &DoubleValue));
  EXPECT_DOUBLE_EQ(DoubleValue, 7.0);
}

TEST(SupportCommandLineTest, CStringRefIntegerParserDoesNotTruncate) {
  const std::string Number = std::string(96, '0') + "42";
  const csupport_string_ref_t Ref =
      csupport_string_ref(Number.data(), Number.size());
  long long Value = -1;

  ASSERT_TRUE(csupport_str_to_int(Ref, &Value));
  EXPECT_EQ(Value, 42);
}

TEST(SupportCommandLineTest, CStringSignedParserHandlesBothLimits) {
  auto Parse = [](StringRef Text, long long &Value) {
    const char *Cursor = Text.data();
    size_t Length = Text.size();
    const int Failed = csupport_consume_signed(&Cursor, &Length, 10, &Value);
    EXPECT_EQ(Length, Failed ? Text.size() : 0u);
    return Failed;
  };

  long long Value = 0;
  EXPECT_EQ(Parse("9223372036854775807", Value), 0);
  EXPECT_EQ(Value, std::numeric_limits<long long>::max());
  EXPECT_EQ(Parse("-9223372036854775808", Value), 0);
  EXPECT_EQ(Value, std::numeric_limits<long long>::min());
  Value = 17;
  EXPECT_NE(Parse("9223372036854775808", Value), 0);
  EXPECT_EQ(Value, 17);
  EXPECT_NE(Parse("-9223372036854775809", Value), 0);
  EXPECT_EQ(Value, 17);
}

TEST(SupportCommandLineTest, CStringRangesRejectOverflowingOffsets) {
  const csupport_string_ref_t Text = csupport_string_ref("abc", 3);
  const csupport_string_ref_t Needle = csupport_string_ref("a", 1);

  EXPECT_EQ(csupport_str_find(Text, Needle, SIZE_MAX), CSUPPORT_STR_NPOS);
  const csupport_string_ref_t Tail =
      csupport_str_substr(Text, 1, CSUPPORT_STR_NPOS);
  ASSERT_EQ(Tail.length, 2u);
  EXPECT_EQ(StringRef(Tail.data, Tail.length), "bc");
}

TEST(SupportCommandLineTest, CanonicalEmptyCInputsNeedNoBackingByte) {
  char Value[2] = {'x', '\0'};

  EXPECT_EQ(csupport_cl_tokenize_command_line(nullptr, 0, nullptr, 0), 0);
  EXPECT_TRUE(csupport_cl_match_prefix(nullptr, 0, nullptr, 0));
  EXPECT_TRUE(csupport_cl_is_arg_equal(nullptr, 0, nullptr, 0));
  EXPECT_EQ(csupport_cl_extract_option_value(nullptr, 0, Value,
                                             sizeof(Value)),
            0u);
  EXPECT_STREQ(Value, "");
  EXPECT_FALSE(csupport_cl_option_has_value(nullptr, 0));
  EXPECT_EQ(csupport_cl_compare_options(nullptr, 0, nullptr, 0), 0);
}

TEST(SupportCommandLineTest, CStringDistanceHasNoPrivateLengthLimit) {
  std::string Left(512, 'a');
  std::string Right = Left;
  Right.back() = 'b';

  EXPECT_EQ(csupport_cl_string_distance(
                Left.data(), Left.size(), Right.data(), Right.size(),
                /*allow_replacements=*/1, /*max_dist=*/0),
            1);
  EXPECT_EQ(csupport_cl_edit_distance_impl(
                Left.data(), Left.size(), Right.data(), Right.size(),
                /*allow_replacements=*/1, /*max_distance=*/0),
            1);
}

TEST(SupportCommandLineTest, ConfigDirectoryAliasesShareTruncationSemantics) {
  constexpr StringLiteral Argument = "123<CFGDIR>XYZ";
  constexpr StringLiteral Base = "abcdef";
  char Expanded[8] = {};
  char AliasExpanded[8] = {};

  const size_t ExpandedSize = csupport_cl_expand_cfg_dir(
      Argument.data(), Argument.size(), Base.data(), Base.size(), Expanded,
      sizeof(Expanded));
  const size_t AliasExpandedSize = csupport_cl_expand_cfgdir(
      Argument.data(), Argument.size(), Base.data(), Base.size(), AliasExpanded,
      sizeof(AliasExpanded));

  EXPECT_EQ(StringRef(Expanded), "123abcd");
  EXPECT_EQ(StringRef(AliasExpanded), StringRef(Expanded));
  EXPECT_EQ(AliasExpandedSize, ExpandedSize);
}
