#include "../../neverc/lib/Translate/ArtifactPlatform.h"
#include "../../neverc/lib/Translate/ArtifactWriter.h"
#include "NeverCTestFixture.h"

#include "llvm/Support/Error.h"
#include <type_traits>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using neverc::translate::Artifact;
using neverc::translate::ArtifactFileIdentity;
using neverc::translate::ArtifactOptions;
using neverc::translate::ArtifactWriter;

static_assert(!std::is_copy_constructible_v<ArtifactFileIdentity>);
static_assert(!std::is_copy_assignable_v<ArtifactFileIdentity>);
static_assert(std::is_nothrow_move_constructible_v<ArtifactFileIdentity>);
static_assert(std::is_nothrow_move_assignable_v<ArtifactFileIdentity>);
static_assert(std::is_nothrow_destructible_v<ArtifactFileIdentity>);

class TranslateArtifactTest : public NeverCTest {
protected:
  std::unique_ptr<ArtifactWriter> create(const ArtifactOptions &Options) {
    const auto Source = tmpFile("input.cpp");
    writeFile(Source, "int main() { return 0; }\n");
    auto Writer = ArtifactWriter::create(Options, Source.string());
    if (!Writer) {
      ADD_FAILURE() << llvm::toString(Writer.takeError()).str().str();
      return nullptr;
    }
    return std::move(*Writer);
  }

  std::vector<Artifact> files(const ArtifactWriter &Writer) {
    return {{Writer.sourceName().str(), "int main() { return 0; }\n"},
            {Writer.mapName().str(), "{\"map\":1}\n"},
            {Writer.manifestName().str(), "{\"manifest\":1}\n"}};
  }

  void success(llvm::Error Error) {
    if (Error)
      ADD_FAILURE() << llvm::toString(std::move(Error)).str().str();
  }

  void collision(llvm::Error Error) {
    ASSERT_TRUE(static_cast<bool>(Error));
    EXPECT_NE(llvm::toString(std::move(Error)).str().str().find("TR0501"),
              std::string::npos);
  }

  void expectNoArtifactTemporaryDirectories() {
    for (const auto &Entry : fs::directory_iterator(tmp())) {
      const auto Name = Entry.path().filename().u8string();
      EXPECT_NE(Name.find(".neverc-report"), 0u) << Entry.path();
      EXPECT_NE(Name.find(".neverc-translate"), 0u) << Entry.path();
    }
  }
};

TEST_F(TranslateArtifactTest, LateManifestCollisionRollsBackOnlyOwnedFiles) {
  const auto Output = tmpFile("translated.nc");
  const auto Map = fs::path(Output.string() + ".map.json");
  const auto Manifest = fs::path(Output.string() + ".manifest.json");
  std::string Stage;
  {
    auto Writer = create({Output.string(), "", ""});
    ASSERT_NE(Writer, nullptr);
    Stage = Writer->stagingDirectory().str();
    writeFile(Manifest, "created concurrently\n");
    collision(Writer->publish(files(*Writer), "{}"));
  }
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_FALSE(fs::exists(Map));
  EXPECT_FALSE(fs::exists(Stage));
  EXPECT_EQ(readFile(Manifest), "created concurrently\n");
}

TEST_F(TranslateArtifactTest, FailureReportCannotCommitPartialGeneratedOutput) {
  const auto Output = tmpFile("translated.nc");
  const auto Report = tmpFile("failure.json");
  const auto Manifest = fs::path(Output.string() + ".manifest.json");
  {
    auto Writer = create({Output.string(), "", Report.string()});
    ASSERT_NE(Writer, nullptr);
    writeFile(Manifest, "concurrent manifest\n");
    collision(Writer->publish(files(*Writer), "{\"status\":\"success\"}"));
    success(Writer->publishFailureReport("{\"status\":\"failure\"}"));
  }
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_FALSE(fs::exists(Output.string() + ".map.json"));
  EXPECT_EQ(readFile(Manifest), "concurrent manifest\n");
  EXPECT_EQ(readFile(Report), "{\"status\":\"failure\"}");
  expectNoArtifactTemporaryDirectories();
}

TEST_F(TranslateArtifactTest,
       CancellationAfterSourcePublicationRollsBackBeforeManifest) {
  const auto Output = tmpFile("translated.nc");
  const auto Report = tmpFile("cancelled.json");
  {
    auto Writer = create({Output.string(), "", Report.string()});
    ASSERT_NE(Writer, nullptr);
    auto Error = Writer->publish(files(*Writer), "{\"status\":\"success\"}",
                                 [&]() { return fs::exists(Output); });
    ASSERT_TRUE(static_cast<bool>(Error));
    EXPECT_NE(llvm::toString(std::move(Error)).str().str().find("TR0005"),
              std::string::npos);
    success(Writer->publishFailureReport("{\"status\":\"cancelled\"}"));
  }
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_FALSE(fs::exists(Output.string() + ".map.json"));
  EXPECT_FALSE(fs::exists(Output.string() + ".manifest.json"));
  EXPECT_EQ(readFile(Report), "{\"status\":\"cancelled\"}");
}

TEST_F(TranslateArtifactTest,
       RollbackPreservesAConcurrentReplacementAtAPublishedPath) {
  const auto Output = tmpFile("translated.nc");
  const auto Replacement = tmpFile("replacement.nc");
  const auto Report = tmpFile("cancelled.json");
  writeFile(Replacement, "replacement owned by another writer\n");
  bool Replaced = false;
  {
    auto Writer = create({Output.string(), "", Report.string()});
    ASSERT_NE(Writer, nullptr);
    auto Error =
        Writer->publish(files(*Writer), "{\"status\":\"success\"}", [&]() {
          if (!fs::exists(Output))
            return false;
          // Rename a distinct inode into the published name. Removing the
          // original first also works on Windows, whose filesystem rename won't
          // replace it.
          fs::remove(Output);
          fs::rename(Replacement, Output);
          Replaced = true;
          return true;
        });
    ASSERT_TRUE(static_cast<bool>(Error));
    EXPECT_NE(llvm::toString(std::move(Error)).str().str().find("TR0005"),
              std::string::npos);
    success(Writer->publishFailureReport("{\"status\":\"cancelled\"}"));
  }
  ASSERT_TRUE(Replaced);
  EXPECT_EQ(readFile(Output), "replacement owned by another writer\n");
  EXPECT_FALSE(fs::exists(Output.string() + ".map.json"));
  EXPECT_FALSE(fs::exists(Output.string() + ".manifest.json"));
  EXPECT_EQ(readFile(Report), "{\"status\":\"cancelled\"}");
}

TEST_F(TranslateArtifactTest, DirectoryPublicationRefusesLateEmptyDirectory) {
  const auto Output = tmpFile("generated");
  const auto Report = tmpFile("report.json");
  {
    auto Writer = create({"", Output.string(), Report.string()});
    ASSERT_NE(Writer, nullptr);
    fs::create_directory(Output);
    collision(Writer->publish(files(*Writer), "{}"));
  }
  EXPECT_TRUE(fs::is_empty(Output));
  EXPECT_FALSE(fs::exists(Report));
}

TEST_F(TranslateArtifactTest,
       DirectoryPublicationContainsOnlyDeclaredArtifacts) {
  const auto Output = tmpFile("generated");
  std::string Stage;
  {
    auto Writer = create({"", Output.string(), ""});
    ASSERT_NE(Writer, nullptr);
    Stage = Writer->stagingDirectory().str();
    success(Writer->writeStage("request.json", "private request"));
    success(Writer->writeStage("validation.o", "temporary object"));
    success(Writer->publish(files(*Writer), "{}"));
  }
  EXPECT_TRUE(fs::is_regular_file(Output / "input.nc"));
  EXPECT_TRUE(fs::is_regular_file(Output / "translate.map.json"));
  EXPECT_TRUE(fs::is_regular_file(Output / "translate-manifest.json"));
  EXPECT_FALSE(fs::exists(Output / "request.json"));
  EXPECT_FALSE(fs::exists(Output / "validation.o"));
  EXPECT_FALSE(fs::exists(Stage));
}

TEST_F(TranslateArtifactTest,
       PublishedFilesSurviveStagingCleanupAsRegularFiles) {
  const auto Output = tmpFile("translated.nc");
  const auto Report = tmpFile("report.json");
  std::string Stage;
  std::vector<Artifact> Expected;
  {
    auto Writer = create({Output.string(), "", Report.string()});
    ASSERT_NE(Writer, nullptr);
    Stage = Writer->stagingDirectory().str();
    Expected = files(*Writer);
    success(Writer->publish(Expected, "{\"status\":\"success\"}"));
  }
  EXPECT_FALSE(fs::exists(Stage));
  for (const auto &Artifact : Expected) {
    const auto Path = tmpFile(Artifact.Name);
    ASSERT_TRUE(fs::is_regular_file(Path)) << Path;
    EXPECT_FALSE(fs::is_symlink(Path)) << Path;
    EXPECT_EQ(readFile(Path), Artifact.Contents);
  }
  EXPECT_FALSE(fs::is_symlink(Report));
  EXPECT_EQ(readFile(Report), "{\"status\":\"success\"}");
  expectNoArtifactTemporaryDirectories();
}

TEST_F(TranslateArtifactTest, CheckModePublishesOnlyItsExplicitReport) {
  const auto Report = tmpFile("check.json");
  std::string Stage;
  {
    auto Writer = create({"", "", Report.string()});
    ASSERT_NE(Writer, nullptr);
    Stage = Writer->stagingDirectory().str();
    success(Writer->writeStage("input.nc", "temporary generated source"));
    success(Writer->publish({}, "{\"status\":\"success\"}"));
  }
  EXPECT_EQ(readFile(Report), "{\"status\":\"success\"}");
  EXPECT_FALSE(fs::exists(tmpFile("input.nc")));
  EXPECT_FALSE(fs::exists(Stage));
  expectNoArtifactTemporaryDirectories();
}

TEST_F(TranslateArtifactTest,
       SuccessReportCanBePublishedWithinTheNewDirectory) {
  const auto Directory = tmpFile("generated");
  const auto Report = Directory / "report.json";
  {
    auto Writer = create({"", Directory.string(), Report.string()});
    ASSERT_NE(Writer, nullptr);
    success(Writer->publish(files(*Writer), "{\"status\":\"success\"}"));
  }
  EXPECT_EQ(readFile(Report), "{\"status\":\"success\"}");
  EXPECT_TRUE(fs::is_regular_file(Directory / "translate-manifest.json"));
}

TEST_F(TranslateArtifactTest, ReportWithinOutputDirectoryCannotNameDotEntries) {
  const auto Source = tmpFile("input.cpp");
  writeFile(Source, "int main() { return 0; }\n");
  const auto Directory = tmpFile("generated");
  for (const auto *Name : {".", ".."}) {
    SCOPED_TRACE(Name);
    auto Writer = ArtifactWriter::create(
        {"", Directory.string(), (Directory / Name).string()}, Source.string());
    ASSERT_FALSE(static_cast<bool>(Writer));
    collision(Writer.takeError());
  }
  EXPECT_FALSE(fs::exists(Directory));
}

TEST_F(TranslateArtifactTest, StageRejectsEscapingNames) {
  auto Writer = create({"", "", ""});
  ASSERT_NE(Writer, nullptr);
  auto Error = Writer->writeStage("../escaped.nc", "bad");
  ASSERT_TRUE(static_cast<bool>(Error));
  EXPECT_NE(llvm::toString(std::move(Error)).str().str().find("TR0502"),
            std::string::npos);
  EXPECT_FALSE(fs::exists(
      fs::path(Writer->stagingDirectory().str()).parent_path() / "escaped.nc"));
}

TEST_F(TranslateArtifactTest, ResolvesParentSymlinksBeforeDotDot) {
  const auto Real = tmpFile("real");
  fs::create_directories(Real / "child");
  std::error_code Error;
  fs::create_directory_symlink(Real / "child", tmpFile("alias"), Error);
  if (Error)
    GTEST_SKIP() << "directory symlinks unavailable: " << Error.message();
  const auto Output = tmpFile("alias") / ".." / "translated.nc";
  {
    auto Writer = create({Output.string(), "", ""});
    ASSERT_NE(Writer, nullptr);
    success(Writer->publish(files(*Writer), "{}"));
  }
  EXPECT_TRUE(fs::exists(Real / "translated.nc"));
  EXPECT_FALSE(fs::exists(tmpFile("translated.nc")));
}

TEST_F(TranslateArtifactTest, BoundedReaderPreservesBytesAndRejectsNonFiles) {
  const auto Input = tmpFile("bytes.bin");
  const std::string Bytes("first\0last", 10);
  writeFile(Input, Bytes);
  auto Exact = neverc::translate::readFile(Input.string(), Bytes.size());
  ASSERT_TRUE(bool(Exact));
  EXPECT_EQ(*Exact, Bytes);
  auto TooSmall = neverc::translate::readFile(Input.string(), Bytes.size() - 1);
  ASSERT_FALSE(bool(TooSmall));
  EXPECT_NE(llvm::toString(TooSmall.takeError()).str().str().find("size limit"),
            std::string::npos);
  auto Directory = neverc::translate::readFile(tmp().string());
  ASSERT_FALSE(bool(Directory));
  llvm::consumeError(Directory.takeError());
  const auto Empty = tmpFile("empty.bin");
  writeFile(Empty, "");
  auto ZeroLimit = neverc::translate::readFile(Empty.string(), 0);
  ASSERT_TRUE(bool(ZeroLimit));
  EXPECT_TRUE(ZeroLimit->empty());
#ifndef _WIN32
  auto Device = neverc::translate::readFile("/dev/null");
  ASSERT_FALSE(bool(Device));
  EXPECT_NE(llvm::toString(Device.takeError()).str().str().find("regular file"),
            std::string::npos);
#endif
}

TEST_F(TranslateArtifactTest, FileIdentityRecognizesHardLinksAndRenames) {
  const auto Original = tmp() / fs::u8path(u8"身份 original.bin");
  const auto Alias = tmp() / fs::u8path(u8"身份 alias.bin");
  const auto Renamed = tmp() / fs::u8path(u8"身份 renamed.bin");
  writeFile(Original, "same file object\n");
  ArtifactFileIdentity Identity;
  auto Error = ArtifactFileIdentity::capture(Original.u8string(), Identity);
  ASSERT_FALSE(Error) << Error.message();
  fs::create_hard_link(Original, Alias, Error);
  ASSERT_FALSE(Error) << Error.message();
  bool Same = false;
  Error = Identity.matches(Alias.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
  fs::rename(Alias, Renamed, Error);
  ASSERT_FALSE(Error) << Error.message();
  Same = false;
  Error = Identity.matches(Renamed.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
  Same = false;
  Error = Identity.matches(Original.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
}

TEST_F(TranslateArtifactTest, FileIdentityRejectsAnIndependentReplacement) {
  const auto Backing = tmpFile("backing.bin");
  const auto Published = tmpFile("published.bin");
  const auto Replacement = tmpFile("replacement.bin");
  // Both objects exist together and have identical bytes. Content or pathname
  // equality must not substitute for the identity of the captured object.
  writeFile(Backing, "identical bytes\n");
  writeFile(Replacement, "identical bytes\n");
  ArtifactFileIdentity Identity;
  auto Error = ArtifactFileIdentity::capture(Backing.u8string(), Identity);
  ASSERT_FALSE(Error) << Error.message();
  fs::create_hard_link(Backing, Published, Error);
  ASSERT_FALSE(Error) << Error.message();
  bool Same = false;
  Error = Identity.matches(Published.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  ASSERT_TRUE(Same);
  // Capture the backing link, as publication does; the published link can be
  // removed while the Windows handle continues to pin the original object.
  ASSERT_TRUE(fs::remove(Published, Error));
  ASSERT_FALSE(Error) << Error.message();
  fs::rename(Replacement, Published, Error);
  ASSERT_FALSE(Error) << Error.message();
  Same = true;
  Error = Identity.matches(Published.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_FALSE(Same);
  Same = false;
  Error = Identity.matches(Backing.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
  EXPECT_EQ(readFile(Published), "identical bytes\n");
}

TEST_F(TranslateArtifactTest, FileIdentityRejectsEmptyMissingAndDirectoryInputs) {
  const auto Input = tmpFile("identity.bin");
  const auto Missing = tmpFile("missing.bin");
  writeFile(Input, "owned\n");
  ArtifactFileIdentity Identity;
  bool Same = true;
  EXPECT_EQ(Identity.matches(Input.u8string(), Same), std::errc::invalid_argument);
  EXPECT_FALSE(Same);
  auto Error = ArtifactFileIdentity::capture(Input.u8string(), Identity);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_EQ(ArtifactFileIdentity::capture("", Identity),
            std::errc::invalid_argument);
  Same = true;
  EXPECT_EQ(Identity.matches("", Same), std::errc::invalid_argument);
  EXPECT_FALSE(Same);
  auto WithNul = Input.u8string();
  WithNul.push_back('\0');
  WithNul += "ignored";
  EXPECT_EQ(ArtifactFileIdentity::capture(WithNul, Identity),
            std::errc::invalid_argument);
  Same = true;
  EXPECT_EQ(Identity.matches(WithNul, Same), std::errc::invalid_argument);
  EXPECT_FALSE(Same);
  Same = true;
  EXPECT_EQ(Identity.matches(Missing.u8string(), Same),
            std::errc::no_such_file_or_directory);
  EXPECT_FALSE(Same);
  EXPECT_EQ(ArtifactFileIdentity::capture(Missing.u8string(), Identity),
            std::errc::no_such_file_or_directory);
  Same = true;
  Error = Identity.matches(tmp().u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_FALSE(Same);
  EXPECT_EQ(ArtifactFileIdentity::capture(tmp().u8string(), Identity),
            std::errc::invalid_argument);
  // A failed capture must retain the previous valid token.
  Same = false;
  Error = Identity.matches(Input.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
}

TEST_F(TranslateArtifactTest, FileIdentityDoesNotFollowALeafSymlink) {
  const auto Input = tmpFile("target.bin");
  const auto Alias = tmpFile("leaf-link.bin");
  writeFile(Input, "symlink target\n");
  std::error_code Error;
  fs::create_symlink(Input, Alias, Error);
  bool Unavailable = Error == std::errc::permission_denied ||
                     Error == std::errc::operation_not_permitted ||
                     Error == std::errc::operation_not_supported ||
                     Error == std::errc::function_not_supported;
#ifdef _WIN32
  Unavailable = Unavailable || Error.value() == ERROR_PRIVILEGE_NOT_HELD;
#endif
  if (Unavailable)
    GTEST_SKIP() << "file symlinks unavailable: " << Error.message();
  ASSERT_FALSE(Error) << Error.message();
  ASSERT_TRUE(fs::is_symlink(Alias));
  ArtifactFileIdentity Identity;
  Error = ArtifactFileIdentity::capture(Input.u8string(), Identity);
  ASSERT_FALSE(Error) << Error.message();
  bool Same = true;
  Error = Identity.matches(Alias.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_FALSE(Same);
  EXPECT_EQ(ArtifactFileIdentity::capture(Alias.u8string(), Identity),
            std::errc::invalid_argument);
  Same = false;
  Error = Identity.matches(Input.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
  EXPECT_EQ(readFile(Input), "symlink target\n");
}

TEST_F(TranslateArtifactTest, FileIdentityMovesTransferOnlyTheCapturedObject) {
  const auto First = tmpFile("first.bin");
  const auto Second = tmpFile("second.bin");
  writeFile(First, "first\n");
  writeFile(Second, "second\n");
  ArtifactFileIdentity Source, Destination;
  auto Error = ArtifactFileIdentity::capture(First.u8string(), Source);
  ASSERT_FALSE(Error) << Error.message();
  Error = ArtifactFileIdentity::capture(Second.u8string(), Destination);
  ASSERT_FALSE(Error) << Error.message();
  ArtifactFileIdentity Moved(std::move(Source));
  bool Same = true;
  EXPECT_EQ(Source.matches(First.u8string(), Same), std::errc::invalid_argument);
  EXPECT_FALSE(Same);
  Same = false;
  Error = Moved.matches(First.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
  Destination = std::move(Moved);
  Same = true;
  EXPECT_EQ(Moved.matches(First.u8string(), Same), std::errc::invalid_argument);
  EXPECT_FALSE(Same);
  Same = false;
  Error = Destination.matches(First.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_TRUE(Same);
  Same = true;
  Error = Destination.matches(Second.u8string(), Same);
  ASSERT_FALSE(Error) << Error.message();
  EXPECT_FALSE(Same);
}

#ifdef _WIN32
TEST_F(TranslateArtifactTest, FileIdentityReleasesDeletePendingHandles) {
  const auto First = tmpFile("pending-first.bin");
  const auto Second = tmpFile("pending-second.bin");
  writeFile(First, "first\n");
  writeFile(Second, "second\n");
  auto expectPending = [](const fs::path &Path) {
    HANDLE File = ::CreateFileW(Path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD Error = ::GetLastError();
    if (File != INVALID_HANDLE_VALUE)
      ::CloseHandle(File);
    EXPECT_EQ(File, INVALID_HANDLE_VALUE);
    EXPECT_EQ(Error, ERROR_ACCESS_DENIED);
  };
  auto expectReusable = [](const fs::path &Path) {
    HANDLE File = ::CreateFileW(Path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    EXPECT_NE(File, INVALID_HANDLE_VALUE) << ::GetLastError();
    if (File != INVALID_HANDLE_VALUE)
      EXPECT_TRUE(::CloseHandle(File));
  };
  {
    ArtifactFileIdentity Source, Destination;
    auto Error = ArtifactFileIdentity::capture(First.u8string(), Source);
    ASSERT_FALSE(Error) << Error.message();
    Error = ArtifactFileIdentity::capture(Second.u8string(), Destination);
    ASSERT_FALSE(Error) << Error.message();
    ArtifactFileIdentity Moved(std::move(Source));
    ASSERT_TRUE(::DeleteFileW(Second.c_str())) << ::GetLastError();
    expectPending(Second);
    Destination = std::move(Moved); // Must close the overwritten second handle.
    expectReusable(Second);
    ASSERT_TRUE(::DeleteFileW(First.c_str())) << ::GetLastError();
    expectPending(First);
  } // The final destination owner must close the first handle.
  expectReusable(First);
}
#endif

} // namespace
