//===- SupportBufferTests.cpp - Output that outgrows a caller's buffer --===//
//
// This fork moved the body of many llvm::Support routines into C, leaving the
// C++ side to supply a stack buffer for the answer.  That split needs one
// rule to be safe -- a filler must report the length its output needs, not
// the length that happened to fit -- and until now there was no such rule.
// Fillers disagreed three ways: some counted past the buffer, some clamped,
// some gave up and returned zero.  Callers guessed, and the ones that guessed
// wrong could not tell a complete answer from a cut one, because a truncated
// answer is a perfectly ordinary string.
//
// The contract is stated once, in csupport_obuf_t, and checked once, by
// FillerAnswersTheSameLengthAtEveryCapacity below: at every capacity a filler
// must report the same length and write that answer's leading bytes.  The
// tests after it are the callers, each one an output longer than the buffer
// that caller used to reserve.  Nothing else in the suite reaches those
// lengths, which is why all of them were wrong at once: a 4096-byte stack
// buffer is large enough that the inputs a test writes by hand always fit,
// and small enough that the inputs a compiler meets -- a minified line, a
// mangled name, a generated path -- do not.
//
//===--------------------------------------------------------------------===//

#include "csupport/la_lp_lfloat.h"
#include "csupport/lapint.h"
#include "csupport/lchrono.h"
#include "csupport/lgraph_lwriter.h"
#include "csupport/lj_ls_lo_ln.h"
#include "csupport/lnative_lformatting.h"
#include "csupport/lpath.h"
#include "csupport/lregex.h"
#include "csupport/lscaled_lnumber.h"
#include "csupport/lsource_lmgr.h"
#include "csupport/lstring_lextras.h"
#include "csupport/ltar_lwriter.h"
#include "csupport/ly_la_lm_ll_lparser.h"
#include "csupport/raw_ostream.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/NativeFormatting.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

using namespace llvm;

namespace {

// Comfortably past every stack buffer these routines reserve, the largest of
// which is 8 KiB.
constexpr size_t Oversized = 20000;

std::string repeated(char C, size_t N) { return std::string(N, C); }

// Bytes on both sides of the capacity a filler is given, so that writing one
// byte too many is a failure here rather than a corruption somewhere else.
constexpr char Fence = '\x7f';
constexpr size_t FenceWidth = 8;

using Filler = std::function<size_t(char *, size_t)>;

// The one property every csupport filler owes its callers: the length it
// reports does not depend on the buffer it was given, and what it writes is
// that answer's leading bytes, terminated.  A filler that clamps its report
// passes every test written against a buffer large enough to hold the answer
// and fails only here, at the capacities a hand-written test never picks.
void checkFillerContract(const char *Name, Filler Fill) {
  SCOPED_TRACE(Name);
  std::vector<char> Roomy(1 << 16);
  const size_t Needed = Fill(Roomy.data(), Roomy.size());
  ASSERT_LT(Needed, Roomy.size()) << "test input outgrew the reference buffer";
  const std::string Whole(Roomy.data(), Needed);

  for (size_t Cap = 0; Cap <= Needed + 2; ++Cap) {
    SCOPED_TRACE(Cap);
    std::vector<char> Fenced(Cap + 2 * FenceWidth, Fence);
    char *Buf = Fenced.data() + FenceWidth;

    EXPECT_EQ(Fill(Buf, Cap), Needed);

    EXPECT_EQ(StringRef(Fenced.data(), FenceWidth),
              StringRef(std::string(FenceWidth, Fence)));
    EXPECT_EQ(StringRef(Buf + Cap, FenceWidth),
              StringRef(std::string(FenceWidth, Fence)));

    if (Cap == 0)
      continue;
    // A byte of capacity goes to the terminator, so the content that fits is
    // one shorter than the buffer.
    const size_t Fits = std::min(Needed, Cap - 1);
    EXPECT_EQ(StringRef(Buf, Fits), StringRef(Whole).take_front(Fits));
    EXPECT_EQ(Buf[Fits], '\0');
  }
}

} // namespace

// Every filler reachable from llvm::Support, held to the contract at once.
// A new one that forgets it fails here rather than in whichever caller first
// meets an input long enough to matter.
TEST(SupportBufferTest, FillerAnswersTheSameLengthAtEveryCapacity) {
  const std::string Text = "a.b\tc\"d\\e{f}g" + repeated('h', 300);
  const std::string Path = "/one/./two/../three" + repeated('/', 3) + "four";

  checkFillerContract("regex_escape", [&](char *B, size_t C) {
    return csupport_regex_escape(Text.data(), Text.size(), B, C);
  });
  checkFillerContract("regex_sub", [&](char *B, size_t C) {
    const size_t Start = 0, End = Text.size();
    return csupport_regex_sub("<\\0>", 4, Text.data(), Text.size(), &Start,
                              &End, 1, B, C, nullptr, 0);
  });
  checkFillerContract("dot_escape_string", [&](char *B, size_t C) {
    return csupport_dot_escape_string(Text.data(), Text.size(), B, C);
  });
  checkFillerContract("yaml_escape", [&](char *B, size_t C) {
    return csupport_yaml_escape(Text.data(), Text.size(), /*printable=*/1, B,
                                C);
  });
  checkFillerContract("yaml_write_scalar", [&](char *B, size_t C) {
    return csupport_yaml_write_scalar(Text.data(), Text.size(),
                                      /*force_quote=*/1, B, C);
  });
  checkFillerContract("json_quote_to_buf", [&](char *B, size_t C) {
    return csupport_json_quote_to_buf(Text.data(), Text.size(), B, C);
  });
  checkFillerContract("expand_tabs_to_string", [&](char *B, size_t C) {
    return csupport_expand_tabs_to_string(Text.data(), Text.size(), B, C,
                                          /*tab_stop=*/8);
  });
  checkFillerContract("format_justified", [&](char *B, size_t C) {
    return csupport_format_justified(B, C, Text.data(), Text.size(),
                                     /*width=*/400, /*justify=*/3);
  });
  checkFillerContract("path_canonicalize", [&](char *B, size_t C) {
    return csupport_path_canonicalize(Path.data(), Path.size(), B, C);
  });
  checkFillerContract("tar_format_pax", [&](char *B, size_t C) {
    return csupport_tar_format_pax(B, C, "path", 4, Path.data(), Path.size());
  });
  const APInt Wide = APInt::getAllOnes(4000);
  checkFillerContract("apint_to_string", [&](char *B, size_t C) {
    return csupport_apint_to_string(Wide.getRawData(), Wide.getBitWidth(),
                                    /*is_signed=*/0, /*radix=*/10, B, C);
  });

  // This one parses as it fills, so a retry has to rewind the cursor.
  const std::string Quoted = "the \\\"body\\\" of a string\",";
  checkFillerContract("json_parse_string_body", [&](char *B, size_t C) {
    const char *P = Quoted.data();
    const char *Err = nullptr;
    return csupport_json_parse_string_body(&P, Quoted.data() + Quoted.size(),
                                           B, C, &Err);
  });

  // The number formatters. Each is given the argument that makes it long:
  // a wide column, a magnitude spelled out in full, a value with more digits
  // than a double's default rendering.
  checkFillerContract("format_number_decimal", [](char *B, size_t C) {
    return csupport_format_number_decimal(B, C, -7, /*width=*/400);
  });
  checkFillerContract("format_number_hex", [](char *B, size_t C) {
    return csupport_format_number_hex(B, C, 0xfeedu, /*width=*/400,
                                      /*upper=*/0, /*prefix=*/1);
  });
  checkFillerContract("format_integer_to_buf", [](char *B, size_t C) {
    return csupport_format_integer_to_buf(B, C, 12345u, /*min_digits=*/400,
                                          /*with_commas=*/0,
                                          /*is_negative=*/1);
  });
  checkFillerContract("format_hex_to_buf", [](char *B, size_t C) {
    return csupport_format_hex_to_buf(B, C, 0xfeedu, /*upper=*/0,
                                      /*prefix=*/1, /*min_width=*/400);
  });
  checkFillerContract("format_double_ex/fixed", [](char *B, size_t C) {
    return csupport_format_double_ex(B, C, 1.7e308, /*style=*/2,
                                     /*precision=*/2);
  });
  checkFillerContract("format_double_ex/exponent", [](char *B, size_t C) {
    return csupport_format_double_ex(B, C, -1.5e-300, /*style=*/0,
                                     /*precision=*/300);
  });
  checkFillerContract("scaled_format_digits", [](char *B, size_t C) {
    return csupport_scaled_format_digits(1, UINT64_C(0x123456789abcdef), 0, 0,
                                         /*precision=*/40, /*width=*/64, B, C);
  });

  const SmallVector<char, 64> Digits(400, '7');
  checkFillerContract("apfloat_format_to_string", [&](char *B, size_t C) {
    return csupport_apfloat_format_to_string(
        Digits.data(), Digits.size(), /*exp=*/-3, /*format_precision=*/400,
        /*format_max_padding=*/3, /*truncate_zero=*/0, /*is_negative=*/1, B, C);
  });

  const std::string Identifier = repeated('a', 200) + "API" + repeated('b', 200);
  checkFillerContract("convert_to_snake_case", [&](char *B, size_t C) {
    return csupport_convert_to_snake_case(Identifier.data(), Identifier.size(),
                                          B, C);
  });
  checkFillerContract("convert_to_camel_case", [&](char *B, size_t C) {
    return csupport_convert_to_camel_case(Identifier.data(), Identifier.size(),
                                          /*capitalize_first=*/1, B, C);
  });
  const std::string Style = repeated('x', 400) + "%N";
  checkFillerContract("expand_chrono_format", [&](char *B, size_t C) {
    return csupport_expand_chrono_format(Style.data(), Style.size(), 1, 2, 3, B,
                                         C);
  });
}

// Regex::escape is a safety function: it turns text into a pattern that
// matches that text and nothing else.  A short answer is not a degraded
// result here, it is a different pattern.
TEST(SupportBufferTest, RegexEscapeKeepsEveryCharacterOfALongInput) {
  const std::string Plain = repeated('a', Oversized);
  EXPECT_EQ(StringRef(Regex::escape(Plain)), StringRef(Plain));

  // Metacharacters double in length, so the escaped form outgrows the buffer
  // even for an input that would have fit.
  const std::string Meta = repeated('.', Oversized);
  const SmallString<256> Escaped = Regex::escape(Meta);
  EXPECT_EQ(Escaped.size(), Meta.size() * 2);
  EXPECT_EQ(StringRef(Escaped).find_first_not_of("\\."), StringRef::npos);
}

TEST(SupportBufferTest, RegexSubstitutesIntoALongSubject) {
  const std::string Subject = repeated('a', Oversized) + "b";
  Regex R("b");
  SmallString<64> Error;
  const SmallString<256> Result = R.sub("c", Subject, &Error);
  EXPECT_TRUE(Error.empty());
  EXPECT_EQ(Result.size(), Subject.size());
  EXPECT_TRUE(StringRef(Result).ends_with("ac"));
}

// A truncated JSON string is not a shorter string, it is a syntax error: the
// closing quote goes missing along with the tail.
TEST(SupportBufferTest, JSONQuotesALongStringAsWellFormedJSON) {
  const std::string Long = repeated('x', Oversized);
  std::string Text;
  raw_string_ostream(Text) << json::Value(Long);

  ASSERT_EQ(Text.size(), Long.size() + 2);
  EXPECT_EQ(Text.front(), '"');
  EXPECT_EQ(Text.back(), '"');

  Expected<json::Value> Reparsed = json::parse(Text);
  ASSERT_TRUE(static_cast<bool>(Reparsed));
  EXPECT_EQ(Reparsed->getAsString(), Long);
}

TEST(SupportBufferTest, JSONQuotesALongObjectKeyAsWellFormedJSON) {
  const std::string Key = repeated('k', Oversized);
  std::string Text;
  raw_string_ostream(Text) << json::Value(json::Object{{Key, 1}});

  Expected<json::Value> Reparsed = json::parse(Text);
  ASSERT_TRUE(static_cast<bool>(Reparsed));
  const json::Object *Obj = Reparsed->getAsObject();
  ASSERT_NE(Obj, nullptr);
  EXPECT_NE(Obj->get(Key), nullptr);
}

// A path too long for a ustar header is exactly the case the PAX header
// exists to carry, so this is the one path length at which losing the name
// cannot be shrugged off.
TEST(SupportBufferTest, TarArchiveKeepsAPathTooLongForAUstarHeader) {
  SmallString<128> ArchivePath;
  ASSERT_FALSE(static_cast<bool>(
      sys::fs::createTemporaryFile("neverc-tar", "tar", ArchivePath)));
  FileRemover Cleanup(ArchivePath);

  const std::string LongPath = repeated('p', 2000);
  {
    Expected<std::unique_ptr<TarWriter>> Writer =
        TarWriter::create(ArchivePath, "base");
    ASSERT_TRUE(static_cast<bool>(Writer));
    (*Writer)->append(LongPath, "contents");
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> Archive =
      MemoryBuffer::getFile(ArchivePath);
  ASSERT_TRUE(static_cast<bool>(Archive));
  EXPECT_NE((*Archive)->getBuffer().find(LongPath), StringRef::npos);
}

TEST(SupportBufferTest, DOTLabelKeepsEveryCharacterOfALongInput) {
  const std::string Label = repeated('a', Oversized);
  EXPECT_EQ(StringRef(DOT::EscapeString(Label)), StringRef(Label));

  // Every quote grows to two characters, so the escaped label outgrows the
  // buffer that the plain one fits in.
  const std::string Quotes = repeated('"', Oversized);
  EXPECT_EQ(DOT::EscapeString(Quotes).size(), Quotes.size() * 2);
}

TEST(SupportBufferTest, YAMLEscapeKeepsEveryCharacterOfALongInput) {
  const std::string Plain = repeated('a', Oversized);
  EXPECT_EQ(StringRef(yaml::escape(Plain, /*EscapePrintable=*/false)),
            StringRef(Plain));

  // Every quote grows to two characters.
  const std::string Quotes = repeated('"', Oversized);
  EXPECT_EQ(yaml::escape(Quotes, /*EscapePrintable=*/false).size(),
            Quotes.size() * 2);
}

TEST(SupportBufferTest, JustifiedOutputKeepsALongString) {
  const std::string Long = repeated('a', Oversized);
  std::string Text;
  raw_string_ostream(Text) << left_justify(Long, 4);
  EXPECT_EQ(Text, Long);

  // Padding counts towards the length too, so a short string justified to a
  // wide column outgrows the buffer just as a long one does.
  std::string Padded;
  raw_string_ostream(Padded) << right_justify("x", Oversized);
  EXPECT_EQ(Padded.size(), Oversized);
  EXPECT_EQ(Padded.back(), 'x');
}

// A number cut short is still a number, and every reader of the IR takes it
// at face value.  Wide integers are the ones that reach past the buffer --
// binary is a digit per bit, so 8192 bits alone doubles the 4096 bytes this
// used to render into, and _BitInt goes three orders of magnitude further.
TEST(SupportBufferTest, WideAPIntPrintsEveryDigit) {
  constexpr unsigned Bits = 8192;
  const APInt Wide = APInt::getAllOnes(Bits);

  SmallString<64> Binary;
  Wide.toString(Binary, 2, /*Signed=*/false);
  EXPECT_EQ(Binary.size(), Bits);
  EXPECT_EQ(StringRef(Binary).find_first_not_of('1'), StringRef::npos);

  SmallString<64> Decimal;
  Wide.toString(Decimal, 10, /*Signed=*/false);
  EXPECT_EQ(StringRef(Decimal).find_first_not_of("0123456789"),
            StringRef::npos);

  // Round-tripping is the check that no digit was lost: a truncated rendering
  // is still valid input, and parses back as a different value.
  EXPECT_EQ(APInt(Bits, Decimal, 10), Wide);
  EXPECT_EQ(APInt(Bits, Binary, 2), Wide);

  // Negative values put the sign ahead of the radix prefix.
  SmallString<64> Hex;
  APInt::getSignedMinValue(Bits).toString(Hex, 16, /*Signed=*/true,
                                          /*formatAsCLiteral=*/true);
  EXPECT_TRUE(StringRef(Hex).starts_with("-0x8"));
  EXPECT_EQ(Hex.size(), 3 + Bits / 4);
}

// A truncated path is not a shorter path, it is a different one, so a
// canonicalizer that cuts its answer sends every lookup somewhere else.
TEST(SupportBufferTest, CanonicalizeKeepsALongPath) {
  std::string Path;
  while (Path.size() < Oversized)
    Path += "/" + repeated('c', 2000);

  EXPECT_EQ(StringRef(vfs::canonicalize(Path)), StringRef(Path));
  EXPECT_EQ(StringRef(vfs::canonicalize("/a/./b/.." + Path)),
            StringRef("/a" + Path));
}

// The other shape of a long path: many components rather than long ones.
// Components have to be held until the path is read, because ".." pops the
// one before it, so a store of them that stops growing loses directories from
// the middle of the answer.
TEST(SupportBufferTest, CanonicalizeKeepsAPathOfManyComponents) {
  std::string Path;
  for (unsigned I = 0; I < 4000; ++I)
    Path += "/d";

  EXPECT_EQ(StringRef(vfs::canonicalize(Path)), StringRef(Path));
  EXPECT_EQ(StringRef(vfs::canonicalize(Path + "/e/..")), StringRef(Path));
}

// Fixed notation spells the magnitude out, so a value near the top of the
// range needs three hundred characters before its fraction begins.  The
// filler reported that length while writing into a buffer a fifth of it, and
// the caller passed the reported length to raw_ostream::write.
TEST(SupportBufferTest, FixedNotationPrintsAWholeLargeDouble) {
  std::string Text;
  raw_string_ostream OS(Text);
  write_double(OS, 1.7e308, FloatStyle::Fixed, /*Precision=*/2);

  // Three hundred and nine digits for the magnitude, then ".00".
  EXPECT_EQ(Text.size(), 309u + 3u);
  EXPECT_TRUE(StringRef(Text).ends_with(".00"));
  EXPECT_EQ(StringRef(Text).find_first_not_of("0123456789."), StringRef::npos);
  // Reading it back is the check that no digit was lost: a truncated
  // rendering is still a valid number, and parses as a different one.
  EXPECT_EQ(strtod(Text.c_str(), nullptr), 1.7e308);

  // Percent multiplies by a hundred before rendering and appends a sign, both
  // of which the reported length has to account for.
  std::string Percent;
  raw_string_ostream PercentOS(Percent);
  write_double(PercentOS, 1.7e306, FloatStyle::Percent, /*Precision=*/2);
  EXPECT_EQ(Percent.size(), 309u + 3u + 1u);
  EXPECT_EQ(Percent.back(), '%');
}

// The column width belongs to the caller, and nothing clamps it.
TEST(SupportBufferTest, FormattedNumbersFillAWideColumn) {
  constexpr unsigned Width = 4000;

  std::string Decimal;
  raw_string_ostream(Decimal) << format_decimal(-7, Width);
  EXPECT_EQ(Decimal.size(), Width);
  EXPECT_TRUE(StringRef(Decimal).ends_with("-7"));
  EXPECT_EQ(StringRef(Decimal).find_first_not_of(' '), Width - 2);

  std::string Integer;
  raw_string_ostream IntegerOS(Integer);
  write_integer(IntegerOS, 12345, Width, IntegerStyle::Integer);
  EXPECT_EQ(Integer.size(), Width);
  EXPECT_TRUE(StringRef(Integer).ends_with("12345"));

  std::string Hex;
  raw_string_ostream HexOS(Hex);
  write_hex(HexOS, 0xfeed, HexPrintStyle::PrefixLower, Width);
  EXPECT_EQ(Hex.size(), Width);
  EXPECT_TRUE(StringRef(Hex).starts_with("0x0"));
  EXPECT_TRUE(StringRef(Hex).ends_with("feed"));
}

// A float's digits and its padding both come from the precision the caller
// asked for, so no fixed buffer bounds the rendering.
TEST(SupportBufferTest, WideAPFloatPrintsEveryDigit) {
  APFloat Value(APFloat::IEEEquad(), "1.0");
  Value.divide(APFloat(APFloat::IEEEquad(), "3.0"), APFloat::rmNearestTiesToEven);

  SmallString<16> Text;
  Value.toString(Text, /*FormatPrecision=*/2000, /*FormatMaxPadding=*/0,
                 /*TruncateZero=*/false);

  // "3." then the fraction padded out to the requested precision, then the
  // exponent: every one of the two thousand places asked for is present.
  EXPECT_TRUE(StringRef(Text).starts_with("3.33333333"));
  EXPECT_EQ(StringRef(Text).find('e'), 2002u);
  EXPECT_TRUE(StringRef(Text).ends_with("e-01"));
}

// Identifier case conversion had no implementation at all: the header
// declared the C entry points and called them, and nothing in the tree
// defined them, so the first translation unit to use one failed to link.
TEST(SupportBufferTest, IdentifierCaseConversionRoundTrips) {
  EXPECT_EQ(StringRef(convertToSnakeFromCamelCase("runtimeAPI")),
            "runtime_api");
  EXPECT_EQ(StringRef(convertToSnakeFromCamelCase("APIRuntime")),
            "api_runtime");
  EXPECT_EQ(StringRef(convertToSnakeFromCamelCase("")), "");

  EXPECT_EQ(StringRef(convertToCamelFromSnakeCase("runtime_api", false)),
            "runtimeApi");
  EXPECT_EQ(StringRef(convertToCamelFromSnakeCase("runtime_api", true)),
            "RuntimeApi");
  // A separator is a word break only when a lowercase letter follows it, so
  // the first '_' here stands for itself and the trailing one does too.
  EXPECT_EQ(StringRef(convertToCamelFromSnakeCase("a__b_", false)), "a_B_");
  EXPECT_EQ(StringRef(convertToCamelFromSnakeCase("", true)), "");

  // Long enough to outgrow the buffer the header used to reserve, and half
  // again as long once every boundary has a separator in it.
  std::string Camel;
  while (Camel.size() < Oversized)
    Camel += "aB";
  const SmallString<256> Snake = convertToSnakeFromCamelCase(Camel);
  EXPECT_EQ(Snake.size(), Camel.size() + Camel.size() / 2);
  EXPECT_EQ(StringRef(convertToCamelFromSnakeCase(Snake, false)),
            StringRef(Camel));
}

TEST(SupportBufferTest, GlobBraceExpansionUsesTheRequestedLimit) {
  std::string Pattern = "{";
  for (unsigned I = 0; I != 65; ++I) {
    if (I)
      Pattern += ',';
    Pattern += "term" + std::to_string(I);
  }
  Pattern += '}';

  Expected<GlobPattern> Glob =
      GlobPattern::create(Pattern, /*MaxSubPatterns=*/65);
  ASSERT_TRUE(static_cast<bool>(Glob));
  for (unsigned I = 0; I != 65; ++I)
    EXPECT_TRUE(Glob->match("term" + std::to_string(I))) << I;
  EXPECT_FALSE(Glob->match("term65"));
}

// The chrono format extensions had no implementation either, and neither did
// the UTC conversion the same header calls.
TEST(SupportBufferTest, ChronoFormatExpandsSubSecondExtensions) {
  using namespace std::chrono;
  const sys::TimePoint<> When =
      sys::toTimePoint(/*seconds=*/0) + nanoseconds(123456789);

  EXPECT_EQ(formatv("{0:%N}", When).str(), "123456789");
  EXPECT_EQ(formatv("{0:%f}", When).str(), "123456");
  EXPECT_EQ(formatv("{0:%L}", When).str(), "123");
  // "%%f" is an escaped percent followed by an f, not a percent followed by
  // the microseconds extension.
  EXPECT_EQ(formatv("{0:%%f}", When).str(), "%f");

  // The UTC conversion is a second entry point the same header declared and
  // never defined.
  const sys::UtcTime<seconds> Utc(seconds(0));
  EXPECT_EQ(formatv("{0:%Y-%m-%d %H:%M:%S}", Utc).str(), "1970-01-01 00:00:00");
}

// The line a diagnostic points at is the evidence for the diagnostic.  Cut it
// and the caret can land past the end of what was printed.
TEST(SupportBufferTest, DiagnosticShowsAWholeLongSourceLine) {
  const std::string Line = repeated('a', Oversized);

  SourceMgr SM;
  const unsigned BufferID = SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBufferCopy(Line, "long.c"), SMLoc());
  const char *Start = SM.getMemoryBuffer(BufferID)->getBufferStart();

  std::string Text;
  raw_string_ostream OS(Text);
  SM.PrintMessage(OS, SMLoc::getFromPointer(Start + Line.size() - 1),
                  SourceMgr::DK_Error, "at the end of a long line");

  EXPECT_NE(StringRef(Text).find(Line), StringRef::npos);
}
