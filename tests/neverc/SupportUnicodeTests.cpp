//===- SupportUnicodeTests.cpp - Unicode StringRef boundaries ------------===//

#include "csupport/lconvert_lu_lt_lf_lwrapper.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Unicode.h"

#include <gtest/gtest.h>

#include <limits>
#include <string>

using namespace llvm;

TEST(SupportUnicodeTest, EmptyTrieNodeHasAnEmptyFullName) {
  sys::unicode::Node Root = sys::unicode::createRoot();
  EXPECT_TRUE(Root.fullName().empty());
}

TEST(SupportUnicodeTest, GeneratedNameDoesNotReadPastStringRef) {
  std::string Storage = "CJK UNIFIED IDEOGRAPH-4E00F";
  StringRef Name(Storage.data(), Storage.size() - 1);
  sys::unicode::BufferType CanonicalName;

  EXPECT_EQ(sys::unicode::nameToCodepoint(Name, /*Strict=*/true, CanonicalName),
            U'\u4E00');
}

TEST(SupportUnicodeTest, UTF8HelpersAcceptCanonicalEmptyValue) {
  EXPECT_TRUE(csupport_is_valid_utf8(nullptr, 0));
  EXPECT_EQ(csupport_fix_utf8(nullptr, 0, nullptr, 0), 0u);

  char Previous = '\0';
  EXPECT_EQ(csupport_unicode_starts_with(nullptr, 0, nullptr, 0,
                                         /*strict=*/1, &Previous,
                                         /*is_prefix=*/1),
            0u);

  const unsigned char *Position = nullptr;
  EXPECT_EQ(csupport_decode_utf8(&Position, nullptr), 0xfffdu);
  EXPECT_EQ(Position, nullptr);

  SmallString<4> UTF8("stale");
  EXPECT_TRUE(convertWideToUTF8(nullptr, 0, UTF8));
  EXPECT_TRUE(UTF8.empty());
  EXPECT_FALSE(convertWideToUTF8(nullptr, 1, UTF8));
}

TEST(SupportUnicodeTest, GBKValidationRejectsOnlyTheForbiddenTrailByte) {
  const char ForbiddenTrail[] = {char(0x81), char(0x7f)};
  const char ValidHighTrail[] = {char(0x81), char(0xf7)};

  EXPECT_FALSE(csupport_has_gbk(ForbiddenTrail, sizeof(ForbiddenTrail)));
  EXPECT_TRUE(csupport_has_gbk(ValidHighTrail, sizeof(ValidHighTrail)));
}

TEST(SupportUnicodeTest, GBKConversionProducesUTF8) {
  const char ChineseGBK[] = {char(0xd6), char(0xd0), char(0xce), char(0xc4)};
  SmallString<8> UTF8;

  ASSERT_TRUE(convertGBKToUTF8String(
      StringRef(ChineseGBK, sizeof(ChineseGBK)), UTF8));
  EXPECT_EQ(StringRef(UTF8), StringRef("\xe4\xb8\xad\xe6\x96\x87"));
}

TEST(SupportUnicodeTest, GBKConversionChecksCapacityBeforeReadingInput) {
  SmallString<8> UTF8("unchanged");
  const StringRef ImpossibleInput(
      reinterpret_cast<const char *>(1),
      std::numeric_limits<size_t>::max() / 3 + 1);

  EXPECT_FALSE(convertGBKToUTF8String(ImpossibleInput, UTF8));
  EXPECT_EQ(StringRef(UTF8), "unchanged");

  EXPECT_TRUE(convertGBKToUTF8String(StringRef(), UTF8));
  EXPECT_TRUE(UTF8.empty());
}
