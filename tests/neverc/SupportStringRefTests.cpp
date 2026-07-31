//===- SupportStringRefTests.cpp - Empty StringRef boundaries ------------===//
//
// A default-constructed StringRef deliberately represents empty data with a
// null pointer.  Support helpers must treat that exactly like every other
// empty string without forming pointers relative to null.
//
//===----------------------------------------------------------------------===//

#include "csupport/stringref.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"

#include <gtest/gtest.h>

#include <string_view>

using namespace llvm;

TEST(SupportStringRefTest, DefaultEmptyValueSupportsSearchAndComparison) {
  StringRef Empty;

  EXPECT_EQ(Empty.end(), Empty.begin());
  EXPECT_EQ(Empty.substr(0).data(), nullptr);
  EXPECT_EQ(Empty.slice(0, 0).data(), nullptr);
  EXPECT_TRUE(Empty.equals(StringRef()));
  EXPECT_EQ(Empty.compare(StringRef()), 0);
  EXPECT_TRUE(Empty.starts_with(StringRef()));
  EXPECT_TRUE(Empty.ends_with(StringRef()));
  EXPECT_EQ(Empty.find('x'), StringRef::npos);
  EXPECT_EQ(Empty.find(StringRef()), 0u);
  EXPECT_EQ(Empty.find("x"), StringRef::npos);
  EXPECT_FALSE(Empty.contains("x"));
  EXPECT_EQ(Empty.rfind(StringRef()), 0u);
  EXPECT_EQ(Empty.find_insensitive(StringRef()), 0u);
  EXPECT_EQ(Empty.rfind_insensitive(StringRef()), 0u);
  EXPECT_TRUE(Empty.starts_with_insensitive(StringRef()));
  EXPECT_TRUE(Empty.ends_with_insensitive(StringRef()));
  EXPECT_EQ(Empty.find_first_of(StringRef()), StringRef::npos);
  EXPECT_EQ(Empty.find_first_not_of(StringRef()), StringRef::npos);
  EXPECT_EQ(Empty.find_last_of(StringRef()), StringRef::npos);
  EXPECT_EQ(Empty.find_last_not_of(StringRef()), StringRef::npos);

  std::string_view View = Empty;
  EXPECT_TRUE(View.empty());

  const char Backing = '\0';
  StringRef BackedEmpty(&Backing, 0);
  std::string_view BackedView = BackedEmpty;
  EXPECT_EQ(BackedView.data(), BackedEmpty.data());

  EXPECT_TRUE(Empty.consume_front(StringRef()));
  EXPECT_EQ(Empty.data(), nullptr);
  EXPECT_TRUE(Empty.consume_back(StringRef()));
  EXPECT_EQ(Empty.data(), nullptr);
}

TEST(SupportStringRefTest, GlobMatchesDefaultEmptyValue) {
  Expected<GlobPattern> Pattern = GlobPattern::create("*");
  ASSERT_TRUE(static_cast<bool>(Pattern));
  EXPECT_TRUE(Pattern->match(StringRef()));
}

TEST(SupportStringRefTest, RegexCompilesDefaultEmptyValueLikeAnyEmptyValue) {
  Regex CanonicalEmpty{StringRef()};
  Regex BackedEmpty{StringRef("", 0)};

  EXPECT_EQ(CanonicalEmpty.isValid(), BackedEmpty.isValid());
}

TEST(SupportStringRefTest, RegexMatchesAndSubstitutesDefaultEmptyValue) {
  Regex OptionalA("a*");
  ASSERT_TRUE(OptionalA.isValid());

  SmallVector<StringRef, 1> Matches;
  EXPECT_TRUE(OptionalA.match(StringRef(), &Matches));
  ASSERT_EQ(Matches.size(), 1u);
  EXPECT_TRUE(Matches[0].empty());
  EXPECT_EQ(OptionalA.sub("replacement", StringRef()), "replacement");
}

TEST(SupportStringRefTest, CRegexAcceptsCanonicalEmptyRanges) {
  llvm_regex_t NullPattern = {};
  NullPattern.re_endp = nullptr;
  const int NullError =
      llvm_regcomp(&NullPattern, nullptr, REG_EXTENDED | REG_PEND);

  const char Backing[] = "";
  llvm_regex_t BackedPattern = {};
  BackedPattern.re_endp = Backing;
  const int BackedError =
      llvm_regcomp(&BackedPattern, Backing, REG_EXTENDED | REG_PEND);
  EXPECT_EQ(NullError, BackedError);

  constexpr char OptionalA[] = "a*";
  llvm_regex_t Compiled = {};
  Compiled.re_endp = OptionalA + 2;
  ASSERT_EQ(llvm_regcomp(&Compiled, OptionalA, REG_EXTENDED | REG_PEND), 0);

  llvm_regmatch_t EmptyRange = {};
  EXPECT_EQ(llvm_regexec(&Compiled, nullptr, 1, &EmptyRange, REG_STARTEND), 0);
  EXPECT_EQ(EmptyRange.rm_so, 0);
  EXPECT_EQ(EmptyRange.rm_eo, 0);

  EmptyRange.rm_so = -1;
  EmptyRange.rm_eo = 0;
  EXPECT_EQ(llvm_regexec(&Compiled, Backing, 1, &EmptyRange, REG_STARTEND),
            REG_INVARG);
  llvm_regfree(&Compiled);
}

TEST(SupportStringRefTest, PathViewsAcceptDefaultEmptyValue) {
  StringRef Empty;

  EXPECT_TRUE(sys::path::starts_with(Empty, Empty, sys::path::Style::posix));
  EXPECT_TRUE(
      sys::path::starts_with(Empty, Empty, sys::path::Style::windows_backslash));

  StringRef Stem = sys::path::stem(Empty);
  EXPECT_TRUE(Stem.empty());
  EXPECT_EQ(Stem.data(), Empty.data());

  StringRef Extension = sys::path::extension(Empty);
  EXPECT_TRUE(Extension.empty());
  EXPECT_EQ(Extension.data(), Empty.data());

  StringRef WithoutDotSlash = sys::path::remove_leading_dotslash(Empty);
  EXPECT_TRUE(WithoutDotSlash.empty());
  EXPECT_EQ(WithoutDotSlash.data(), Empty.data());
}

TEST(SupportStringRefTest, PathStartsWithRequiresAWholeLastComponent) {
  EXPECT_TRUE(
      sys::path::starts_with("/foo", "/foo", sys::path::Style::posix));
  EXPECT_TRUE(
      sys::path::starts_with("/foo/", "/foo", sys::path::Style::posix));
  EXPECT_TRUE(
      sys::path::starts_with("/foo/bar", "/foo", sys::path::Style::posix));
  EXPECT_FALSE(
      sys::path::starts_with("/foo", "/foo/", sys::path::Style::posix));
  EXPECT_FALSE(
      sys::path::starts_with("/fooo", "/foo", sys::path::Style::posix));
  EXPECT_FALSE(
      sys::path::starts_with("/foo/bar", "/foo/b", sys::path::Style::posix));

  EXPECT_TRUE(sys::path::starts_with("C:\\FOO\\bar", "c:/foo",
                                     sys::path::Style::windows_backslash));
  EXPECT_FALSE(sys::path::starts_with("C:\\foobar", "c:/foo",
                                      sys::path::Style::windows_backslash));
}

TEST(SupportStringRefTest, ReplacePathPrefixKeepsRelaxedPrefixSemantics) {
  SmallString<16> Path{StringRef("/fooo")};
  EXPECT_TRUE(sys::path::replace_path_prefix(
      Path, "/foo", "/bar", sys::path::Style::posix));
  EXPECT_EQ(Path, "/baro");
}

TEST(SupportStringRefTest, CPathHelpersHonorNamedStyles) {
  constexpr StringLiteral PosixBase = "/base";
  constexpr StringLiteral WindowsBase = "C:\\base";
  constexpr StringLiteral Component = "leaf";
  char Buffer[32] = {};

  EXPECT_EQ(csupport_path_make_absolute_buf(
                PosixBase.data(), PosixBase.size(), Component.data(),
                Component.size(), Buffer, sizeof(Buffer),
                CSUPPORT_PATH_STYLE_POSIX),
            PosixBase.size() + 1 + Component.size());
  EXPECT_STREQ(Buffer, "/base/leaf");

  EXPECT_EQ(csupport_path_make_absolute_buf(
                WindowsBase.data(), WindowsBase.size(), Component.data(),
                Component.size(), Buffer, sizeof(Buffer),
                CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH),
            WindowsBase.size() + 1 + Component.size());
  EXPECT_STREQ(Buffer, "C:\\base\\leaf");

  EXPECT_EQ(csupport_path_make_absolute_buf(
                WindowsBase.data(), WindowsBase.size(), Component.data(),
                Component.size(), Buffer, sizeof(Buffer),
                CSUPPORT_PATH_STYLE_WINDOWS_SLASH),
            WindowsBase.size() + 1 + Component.size());
  EXPECT_STREQ(Buffer, "C:\\base/leaf");

  EXPECT_FALSE(csupport_path_is_separator_char(
      '\\', CSUPPORT_PATH_STYLE_POSIX));
  EXPECT_TRUE(csupport_path_is_separator_char(
      '\\', CSUPPORT_PATH_STYLE_WINDOWS));
  EXPECT_TRUE(csupport_path_is_separator_char(
      '\\', CSUPPORT_PATH_STYLE_WINDOWS_SLASH));
  EXPECT_TRUE(csupport_path_is_separator_char(
      '\\', CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH));

  constexpr StringLiteral DrivePath = "C:\\base";
  EXPECT_FALSE(csupport_path_is_absolute_styled(
      DrivePath.data(), DrivePath.size(), CSUPPORT_PATH_STYLE_POSIX));
  EXPECT_TRUE(csupport_path_is_absolute_styled(
      DrivePath.data(), DrivePath.size(), CSUPPORT_PATH_STYLE_WINDOWS_SLASH));
  EXPECT_TRUE(csupport_path_is_absolute_styled(
      DrivePath.data(), DrivePath.size(),
      CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH));

  char PreferredBackslash[] = "C:/base\\leaf";
  csupport_path_make_preferred(PreferredBackslash,
                               sizeof(PreferredBackslash) - 1,
                               CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH);
  EXPECT_STREQ(PreferredBackslash, "C:\\base\\leaf");

  char PreferredSlash[] = "C:/base\\leaf";
  csupport_path_make_preferred(PreferredSlash, sizeof(PreferredSlash) - 1,
                               CSUPPORT_PATH_STYLE_WINDOWS_SLASH);
  EXPECT_STREQ(PreferredSlash, "C:/base/leaf");

  char Appended[32] = "C:\\base";
  EXPECT_EQ(csupport_path_append_styled(
                Appended, WindowsBase.size(), sizeof(Appended),
                Component.data(), Component.size(),
                CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH),
            WindowsBase.size() + 1 + Component.size());
  EXPECT_STREQ(Appended, "C:\\base\\leaf");

  constexpr StringLiteral MixedPath = "C:\\base/leaf";
  EXPECT_EQ(csupport_path_normalize_separators(
                MixedPath.data(), MixedPath.size(), Buffer, sizeof(Buffer),
                CSUPPORT_PATH_STYLE_WINDOWS_SLASH),
            MixedPath.size());
  EXPECT_STREQ(Buffer, "C:/base/leaf");
  EXPECT_EQ(csupport_path_normalize_separators(
                MixedPath.data(), MixedPath.size(), Buffer, sizeof(Buffer),
                CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH),
            MixedPath.size());
  EXPECT_STREQ(Buffer, "C:\\base\\leaf");

  EXPECT_TRUE(csupport_path_has_root_name(
      DrivePath.data(), DrivePath.size(), CSUPPORT_PATH_STYLE_WINDOWS_SLASH));
  EXPECT_TRUE(csupport_path_has_root_name(
      DrivePath.data(), DrivePath.size(),
      CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH));

  constexpr StringLiteral TrailingSeparators = "C:\\base\\\\";
  const size_t Stripped = csupport_path_strip_trailing_separators(
      TrailingSeparators.data(), TrailingSeparators.size(),
      CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH);
  EXPECT_EQ(TrailingSeparators.take_front(Stripped), WindowsBase);

  constexpr StringLiteral Path = "C:\\ROOT\\leaf";
  constexpr StringLiteral OldPrefix = "c:/root";
  constexpr StringLiteral NewPrefix = "D:\\base";
  EXPECT_EQ(csupport_path_replace_path_prefix(
                Path.data(), Path.size(), OldPrefix.data(), OldPrefix.size(),
                NewPrefix.data(), NewPrefix.size(), Buffer, sizeof(Buffer),
                CSUPPORT_PATH_STYLE_WINDOWS_BACKSLASH),
            NewPrefix.size() + Component.size() + 1);
  EXPECT_STREQ(Buffer, "D:\\base\\leaf");

  EXPECT_TRUE(
      sys::path::starts_with(Path, OldPrefix, sys::path::Style::windows_slash));
  EXPECT_TRUE(sys::path::starts_with(
      Path, OldPrefix, sys::path::Style::windows_backslash));
}

TEST(SupportStringRefTest, CHelpersAcceptCanonicalEmptyValue) {
  csupport_string_ref_t Empty = csupport_string_ref(nullptr, 0);

  EXPECT_EQ(csupport_str_end(Empty), nullptr);
  EXPECT_TRUE(csupport_str_starts_with(Empty, Empty));
  EXPECT_TRUE(csupport_str_ends_with(Empty, Empty));
  EXPECT_EQ(csupport_str_ltrim(Empty).data, nullptr);

  csupport_string_ref_t Consumed = Empty;
  EXPECT_TRUE(csupport_str_consume_front(&Consumed, Empty));
  EXPECT_EQ(Consumed.data, nullptr);
  EXPECT_TRUE(csupport_str_consume_back(&Consumed, Empty));

  csupport_string_ref_t Rest = csupport_string_ref("not empty", 9);
  csupport_string_ref_t First = csupport_str_split(Empty, ',', &Rest);
  EXPECT_EQ(First.data, nullptr);
  EXPECT_EQ(Rest.data, nullptr);
  EXPECT_EQ(Rest.length, 0u);

  const char *Result = reinterpret_cast<const char *>(1);
  EXPECT_EQ(csupport_path_stem(nullptr, 0, &Result,
                               CSUPPORT_PATH_STYLE_NATIVE),
            0u);
  EXPECT_EQ(Result, nullptr);

  Result = reinterpret_cast<const char *>(1);
  EXPECT_EQ(csupport_path_extension(nullptr, 0, &Result,
                                    CSUPPORT_PATH_STYLE_NATIVE),
            0u);
  EXPECT_EQ(Result, nullptr);

  EXPECT_TRUE(csupport_path_has_extension(nullptr, 0, nullptr, 0));
  EXPECT_TRUE(
      csupport_glob_match_advanced(nullptr, 0, nullptr, nullptr, 0, nullptr, 0));
  EXPECT_TRUE(csupport_glob_match_advanced("*", 1, nullptr, nullptr, 0, nullptr,
                                           0));
  EXPECT_FALSE(csupport_glob_match_advanced("?", 1, nullptr, nullptr, 0,
                                            nullptr, 0));
}
