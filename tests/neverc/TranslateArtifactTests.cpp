#include "../../neverc/lib/Translate/ArtifactPlatform.h"
#include "../../neverc/lib/Translate/ArtifactWriter.h"
#include "NeverCTestFixture.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include <iterator>
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
  constexpr DWORD ShareAll =
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  constexpr DWORD OpenFlags =
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
  auto closeOwned = [](HANDLE &File) {
    if (File == INVALID_HANDLE_VALUE)
      return DWORD(ERROR_SUCCESS);
    if (!::CloseHandle(File)) {
      const DWORD Error = ::GetLastError();
      return Error ? Error : DWORD(ERROR_GEN_FAILURE);
    }
    File = INVALID_HANDLE_VALUE;
    return DWORD(ERROR_SUCCESS);
  };
  auto checkRegularIdentity = [](HANDLE File, FILE_ID_INFO &Identity) {
    ::SetLastError(ERROR_SUCCESS);
    const DWORD Type = ::GetFileType(File);
    const DWORD TypeError = ::GetLastError();
    ASSERT_EQ(Type, DWORD(FILE_TYPE_DISK))
        << "GetFileType error=" << TypeError;
    FILE_ATTRIBUTE_TAG_INFO Attributes = {};
    BOOL Queried = ::GetFileInformationByHandleEx(
        File, FileAttributeTagInfo, &Attributes, sizeof(Attributes));
    DWORD Error = Queried ? ERROR_SUCCESS : ::GetLastError();
    ASSERT_TRUE(Queried) << "FileAttributeTagInfo error=" << Error;
    ASSERT_EQ(Attributes.FileAttributes &
                  (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT),
              DWORD(0));
    Queried = ::GetFileInformationByHandleEx(File, FileIdInfo, &Identity,
                                           sizeof(Identity));
    Error = Queried ? ERROR_SUCCESS : ::GetLastError();
    ASSERT_TRUE(Queried) << "FileIdInfo error=" << Error;
  };
  auto checkDeletePending = [](HANDLE File) {
    FILE_STANDARD_INFO Standard = {};
    const BOOL Queried = ::GetFileInformationByHandleEx(
        File, FileStandardInfo, &Standard, sizeof(Standard));
    const DWORD Error = Queried ? ERROR_SUCCESS : ::GetLastError();
    ASSERT_TRUE(Queried) << "FileStandardInfo error=" << Error;
    ASSERT_TRUE(Standard.DeletePending) << "FileStandardInfo.DeletePending";
  };
  auto markPending = [&](const fs::path &Path,
                         const FILE_ID_INFO *ExpectedIdentity) {
    SCOPED_TRACE("classic setter: " + Path.u8string());
    HANDLE File = ::CreateFileW(Path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                               ShareAll, nullptr, OPEN_EXISTING, OpenFlags,
                               nullptr);
    const DWORD OpenError = File == INVALID_HANDLE_VALUE ? ::GetLastError()
                                                        : ERROR_SUCCESS;
    auto CloseFile = llvm::make_scope_exit([&] {
      EXPECT_EQ(closeOwned(File), DWORD(ERROR_SUCCESS))
          << "setter cleanup CloseHandle";
    });
    ASSERT_NE(File, INVALID_HANDLE_VALUE)
        << "CreateFileW DELETE|FILE_READ_ATTRIBUTES error=" << OpenError;
    FILE_ID_INFO Identity = {};
    ASSERT_NO_FATAL_FAILURE(checkRegularIdentity(File, Identity));
    if (ExpectedIdentity) {
      ASSERT_EQ(Identity.VolumeSerialNumber,
                ExpectedIdentity->VolumeSerialNumber);
      for (unsigned I = 0; I != sizeof(Identity.FileId.Identifier); ++I)
        ASSERT_EQ(Identity.FileId.Identifier[I],
                  ExpectedIdentity->FileId.Identifier[I])
            << "FileIdInfo byte=" << I;
    }
    // DeleteFileW may choose POSIX deletion and remove the name while a token
    // is still alive. Request classic deletion explicitly, without EX flags.
    FILE_DISPOSITION_INFO Disposition = {TRUE};
    const BOOL Marked = ::SetFileInformationByHandle(
        File, FileDispositionInfo, &Disposition, sizeof(Disposition));
    const DWORD Error = Marked ? ERROR_SUCCESS : ::GetLastError();
    ASSERT_TRUE(Marked) << "SetFileInformationByHandle FileDispositionInfo "
                          "DeleteFile=TRUE error="
                       << Error;
    ASSERT_NO_FATAL_FAILURE(checkDeletePending(File));
    ASSERT_EQ(closeOwned(File), DWORD(ERROR_SUCCESS))
        << "setter CloseHandle before namespace probes";
  };
  auto expectBlocked = [&](const fs::path &Path, DWORD Creation) {
    SCOPED_TRACE("held namespace probe: " + Path.u8string());
    const DWORD Access =
        Creation == OPEN_EXISTING ? FILE_READ_ATTRIBUTES : GENERIC_WRITE;
    HANDLE File = ::CreateFileW(Path.c_str(), Access, ShareAll, nullptr,
                               Creation, OpenFlags, nullptr);
    const DWORD Error = File == INVALID_HANDLE_VALUE ? ::GetLastError()
                                                    : ERROR_SUCCESS;
    auto CloseFile = llvm::make_scope_exit([&] {
      EXPECT_EQ(closeOwned(File), DWORD(ERROR_SUCCESS))
          << "unexpected probe cleanup CloseHandle";
    });
    ASSERT_EQ(File, INVALID_HANDLE_VALUE)
        << "CreateFileW creation=" << Creation << " access=" << Access
        << " error=" << Error;
    ASSERT_EQ(Error, DWORD(ERROR_ACCESS_DENIED))
        << "CreateFileW creation=" << Creation << " access=" << Access;
  };
  auto expectReusable = [&](const fs::path &Path) {
    SCOPED_TRACE("released namespace probe: " + Path.u8string());
    HANDLE File = ::CreateFileW(Path.c_str(), FILE_READ_ATTRIBUTES, ShareAll,
                               nullptr, OPEN_EXISTING, OpenFlags, nullptr);
    DWORD Error = File == INVALID_HANDLE_VALUE ? ::GetLastError()
                                              : ERROR_SUCCESS;
    auto CloseFile = llvm::make_scope_exit([&] {
      EXPECT_EQ(closeOwned(File), DWORD(ERROR_SUCCESS))
          << "reuse probe cleanup CloseHandle";
    });
    ASSERT_EQ(File, INVALID_HANDLE_VALUE)
        << "CreateFileW OPEN_EXISTING error=" << Error;
    ASSERT_EQ(Error, DWORD(ERROR_FILE_NOT_FOUND))
        << "CreateFileW OPEN_EXISTING";
    File = ::CreateFileW(Path.c_str(), GENERIC_WRITE, ShareAll, nullptr,
                        CREATE_NEW, OpenFlags, nullptr);
    Error = File == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
    ASSERT_NE(File, INVALID_HANDLE_VALUE)
        << "CreateFileW CREATE_NEW error=" << Error;
    ASSERT_EQ(closeOwned(File), DWORD(ERROR_SUCCESS))
        << "reuse probe CloseHandle";
  };

  // Establish the observable held/closed distinction on this filesystem using
  // the product's exact metadata-only access, sharing and no-follow flags.
  // This prerequisite is a failure, never a skip or an error-2/error-5 union.
  const auto Control = tmpFile("pending-native-control.bin");
  writeFile(Control, "native control\n");
  {
    SCOPED_TRACE("native metadata-handle prerequisite");
    HANDLE Metadata = ::CreateFileW(Control.c_str(), FILE_READ_ATTRIBUTES,
                                   ShareAll, nullptr, OPEN_EXISTING, OpenFlags,
                                   nullptr);
    const DWORD Error = Metadata == INVALID_HANDLE_VALUE ? ::GetLastError()
                                                        : ERROR_SUCCESS;
    auto CloseMetadata = llvm::make_scope_exit([&] {
      EXPECT_EQ(closeOwned(Metadata), DWORD(ERROR_SUCCESS))
          << "native prerequisite cleanup CloseHandle";
    });
    ASSERT_NE(Metadata, INVALID_HANDLE_VALUE)
        << "CreateFileW FILE_READ_ATTRIBUTES error=" << Error;
    FILE_ID_INFO Identity = {};
    ASSERT_NO_FATAL_FAILURE(checkRegularIdentity(Metadata, Identity));
    ASSERT_NO_FATAL_FAILURE(markPending(Control, &Identity));
    // The setter has closed: only Metadata is deliberately retained here.
    ASSERT_NO_FATAL_FAILURE(checkDeletePending(Metadata));
    ASSERT_NO_FATAL_FAILURE(expectBlocked(Control, OPEN_EXISTING));
    ASSERT_NO_FATAL_FAILURE(expectBlocked(Control, CREATE_NEW));
    ASSERT_EQ(closeOwned(Metadata), DWORD(ERROR_SUCCESS))
        << "native prerequisite release CloseHandle";
    ASSERT_NO_FATAL_FAILURE(expectReusable(Control));
  }

  const auto First = tmpFile("pending-first.bin");
  const auto Second = tmpFile("pending-second.bin");
  writeFile(First, "first\n");
  writeFile(Second, "second\n");
  {
    SCOPED_TRACE("product token moves and release");
    ArtifactFileIdentity Source, Destination;
    auto Error = ArtifactFileIdentity::capture(First.u8string(), Source);
    ASSERT_FALSE(Error) << Error.message();
    Error = ArtifactFileIdentity::capture(Second.u8string(), Destination);
    ASSERT_FALSE(Error) << Error.message();
    ArtifactFileIdentity Moved(std::move(Source));
    ASSERT_NO_FATAL_FAILURE(markPending(Second, nullptr));
    ASSERT_NO_FATAL_FAILURE(expectBlocked(Second, OPEN_EXISTING));
    ASSERT_NO_FATAL_FAILURE(expectBlocked(Second, CREATE_NEW));
    Destination = std::move(Moved); // Must close the overwritten second handle.
    ASSERT_NO_FATAL_FAILURE(expectReusable(Second));
    ASSERT_NO_FATAL_FAILURE(markPending(First, nullptr));
    ASSERT_NO_FATAL_FAILURE(expectBlocked(First, OPEN_EXISTING));
    ASSERT_NO_FATAL_FAILURE(expectBlocked(First, CREATE_NEW));
  } // The final destination owner must close the first handle.
  ASSERT_NO_FATAL_FAILURE(expectReusable(First));
}
#endif

TEST_F(TranslateArtifactTest, ExistingPathsResolveOrderedUnicodeComponents) {
  const auto Root = tmp() / fs::u8path(u8"physical \u8def\u5f84");
  const auto Child = Root / "child";
  const auto Input = Child / fs::u8path(u8"\u6e90 input.cpp");
  fs::create_directories(Child);
  writeFile(Input, "int f() { return 1; }\n");
  ASSERT_EQ(Input.filename().u8string(), u8"\u6e90 input.cpp");
  ASSERT_EQ(fs::u8path(Input.u8string()), Input);
  const auto File = fs::canonical(Input).u8string();
  const auto Directory = fs::canonical(Child).u8string();
  for (const auto &Case : std::vector<std::pair<std::string, std::string>>{
           {Input.u8string(), File},
           {(Child / "." / Input.filename()).u8string(), File},
           {(Child / ".." / "child" / Input.filename()).u8string(), File},
           {(Child / ".." / "child").u8string(), Directory},
           {(Child / ".." / "child").u8string() + "/", Directory}}) {
    SCOPED_TRACE(Case.first);
    llvm::SmallString<256> Resolved("stale result");
    const auto Error =
        neverc::translate::resolveExistingPath(Case.first, Resolved);
    EXPECT_FALSE(Error) << Error.message();
    EXPECT_EQ(Resolved.str().str(), Case.second);
    EXPECT_TRUE(fs::u8path(Resolved.str().str()).is_absolute());
  }
}

TEST_F(TranslateArtifactTest,
       ExistingPathsRejectInvalidIntermediateComponents) {
  const auto Root = tmpFile("ordered-paths");
  const auto Input = Root / "input.cpp";
  fs::create_directory(Root);
  writeFile(Input, "int f() { return 1; }\n");
  // A missing component must never disappear through lexical dot removal.
  {
    llvm::SmallString<256> Resolved("stale result");
    const auto Error = neverc::translate::resolveExistingPath(
        (Root / "missing" / ".." / "input.cpp").u8string(), Resolved);
    EXPECT_TRUE(Error);
    EXPECT_TRUE(Resolved.empty());
  }
  for (const auto &Path : std::vector<std::string>{
           (Input / ".").u8string(),
           (Input / ".." / "input.cpp").u8string(), Input.u8string() + "/"}) {
    SCOPED_TRACE(Path);
#ifndef _WIN32
    // POSIX implementations differ on dot/separator suffixes after a file.
    // Use the independent native result rather than imposing Windows rules.
    std::error_code NativeError;
    const auto Native = fs::canonical(fs::u8path(Path), NativeError);
#endif
    llvm::SmallString<256> Resolved("stale result");
    const auto Error = neverc::translate::resolveExistingPath(Path, Resolved);
#ifdef _WIN32
    EXPECT_TRUE(Error);
    EXPECT_TRUE(Resolved.empty());
#else
    if (NativeError) {
      EXPECT_TRUE(Error);
      EXPECT_TRUE(Resolved.empty());
    } else {
      EXPECT_FALSE(Error) << Error.message();
      EXPECT_EQ(Resolved.str().str(), Native.u8string());
      if (!Error) {
        std::error_code IdentityError;
        EXPECT_TRUE(fs::equivalent(fs::u8path(Resolved.str().str()), Input,
                                   IdentityError));
        EXPECT_FALSE(IdentityError) << IdentityError.message();
      }
    }
#endif
  }
  EXPECT_EQ(readFile(Input), "int f() { return 1; }\n");
  EXPECT_FALSE(fs::exists(Root / "missing"));
}

TEST_F(TranslateArtifactTest, ExistingPathsRejectInvalidInputsAndClearResults) {
  auto EmbeddedNul = tmpFile("invalid-input.cpp").u8string();
  EmbeddedNul.push_back('\0');
  EmbeddedNul += "suffix";
  std::vector<std::string> Inputs = {"", EmbeddedNul, "relative/input.cpp"};
#ifdef _WIN32
  // These incomplete/device roots have no server or share to look up.
  Inputs.insert(Inputs.end(), {R"(\\)", R"(\\?\)", "//?/",
                               R"(\\?\UNC\)", "//?/UNC/", R"(\\.\)"});
#endif
  for (const auto &Input : Inputs) {
    SCOPED_TRACE(Input);
    llvm::SmallString<256> Resolved("stale result");
    const auto Error = neverc::translate::resolveExistingPath(Input, Resolved);
    EXPECT_EQ(Error, std::errc::invalid_argument);
    EXPECT_TRUE(Resolved.empty());
  }
}

TEST_F(TranslateArtifactTest, ExistingPathsMayBorrowTheirResultStorage) {
  const auto Input = tmpFile("aliased-input.cpp");
  writeFile(Input, "int f() { return 1; }\n");
  const auto Expected = fs::canonical(Input).u8string();
  llvm::SmallString<256> Resolved(Input.u8string());
  const llvm::StringRef Borrowed = Resolved.str();
  auto Error = neverc::translate::resolveExistingPath(Borrowed, Resolved);
  EXPECT_FALSE(Error) << Error.message();
  EXPECT_EQ(Resolved.str().str(), Expected);

  const auto Missing = Input.parent_path() / "alias-missing" / Input.filename();
  ASSERT_FALSE(fs::exists(Missing.parent_path()));
  Resolved = Missing.u8string();
  const llvm::StringRef MissingBorrowed = Resolved.str();
  Error = neverc::translate::resolveExistingPath(MissingBorrowed, Resolved);
  EXPECT_EQ(Error, std::errc::no_such_file_or_directory);
  EXPECT_TRUE(Resolved.empty());
  EXPECT_FALSE(fs::exists(Missing.parent_path()));
  EXPECT_EQ(readFile(Input), "int f() { return 1; }\n");
}

TEST_F(TranslateArtifactTest, ExistingPathsPreserveControlledLongDirectories) {
  const auto Root = tmpFile("long-path-root");
  fs::create_directory(Root);
  // Resolve the short fixture root independently, then create only ordinary
  // child directories. This does not require a network share or system setting.
  auto Long = fs::canonical(Root);
  for (unsigned I = 0; I != 12; ++I)
    Long /= "component_0123456789abcdef_" + std::to_string(I);
  Long /= fs::u8path(u8"\u8def\u5f84");
  ASSERT_GT(Long.native().size(), 260u);
  ASSERT_LT(Long.u8string().size(), 4096u);
  auto Error = llvm::sys::fs::create_directories(Long.u8string());
  ASSERT_FALSE(Error) << Error.message();
  llvm::SmallString<256> Resolved;
  Error = neverc::translate::resolveExistingPath(Long.u8string(), Resolved);
  EXPECT_FALSE(Error) << Error.message();
  EXPECT_EQ(Resolved.str().str(), Long.u8string());
  EXPECT_TRUE(fs::u8path(Resolved.str().str()).is_absolute());
#ifdef _WIN32
  auto CheckAliases = [&] {
    const auto Ordinary = Long.u8string();
    const bool DrivePath =
        Ordinary.size() >= 3 && Ordinary[1] == ':' &&
        ((Ordinary[0] >= 'A' && Ordinary[0] <= 'Z') ||
         (Ordinary[0] >= 'a' && Ordinary[0] <= 'z')) &&
        (Ordinary[2] == '\\' || Ordinary[2] == '/');
    if (!DrivePath)
      GTEST_SKIP() << "Long-path alias coverage requires an ordinary local "
                      "drive path; no UNC or namespace fixture is created";
    std::error_code CanonicalError;
    const auto Expected = fs::canonical(Long, CanonicalError).u8string();
    if (CanonicalError)
      GTEST_SKIP() << "Independent long-path canonicalization unavailable: "
                   << CanonicalError.message();
    if (llvm::StringRef(Expected).starts_with("\\\\?\\") ||
        llvm::StringRef(Expected).starts_with("//?/"))
      GTEST_SKIP() << "Independent canonical result retains a namespace prefix; "
                      "ordinary long-path comparison is unavailable";
    auto Native = Ordinary;
    for (char &C : Native)
      if (C == '/')
        C = '\\';
    auto Slash = Native;
    for (char &C : Slash)
      if (C == '\\')
        C = '/';
    for (const auto &Alias :
         {std::string(R"(\\?\)") + Native, "//?/" + Slash}) {
      SCOPED_TRACE(Alias);
      llvm::SmallString<256> Whole;
      const auto WholeError = llvm::sys::fs::real_path(Alias, Whole);
      if (WholeError)
        GTEST_SKIP() << "Existing whole-path alias unavailable: "
                     << WholeError.message();
      ASSERT_EQ(Whole.str().str(), Expected);
      llvm::SmallString<256> AliasResolved("stale result");
      const auto AliasError =
          neverc::translate::resolveExistingPath(Alias, AliasResolved);
      EXPECT_FALSE(AliasError) << AliasError.message();
      EXPECT_EQ(AliasResolved.str().str(), Expected);
    }
  };
  // A skip or fatal assertion inside the local check must not bypass cleanup.
  EXPECT_NO_FATAL_FAILURE(CheckAliases());
#endif
  Error = llvm::sys::fs::remove_directories(Root.u8string());
  EXPECT_FALSE(Error) << Error.message();
}

#ifdef _WIN32
TEST_F(TranslateArtifactTest, ExistingPathsClampParentRevisitsAtTheDriveRoot) {
  const auto Physical = fs::canonical(tmp()).u8string();
  const bool DrivePath =
      Physical.size() >= 3 && Physical[1] == ':' &&
      ((Physical[0] >= 'A' && Physical[0] <= 'Z') ||
       (Physical[0] >= 'a' && Physical[0] <= 'z')) &&
      (Physical[2] == '\\' || Physical[2] == '/');
  if (!DrivePath)
    GTEST_SKIP() << "Root-clamp coverage requires an ordinary local drive path; "
                   "no UNC or namespace fixture is created";
  const auto Root = fs::u8path(Physical.substr(0, 3));
  std::error_code CanonicalError;
  const auto Expected = fs::canonical(Root, CanonicalError).u8string();
  if (CanonicalError)
    GTEST_SKIP() << "Independent drive-root canonicalization unavailable: "
                 << CanonicalError.message();
  // Read the current local root only; all created files remain in tmp().
  for (const auto *Suffix : {"..", "..\\..\\..", ".././../../.."}) {
    const auto Input = Root.u8string() + Suffix;
    SCOPED_TRACE(Input);
    llvm::SmallString<256> Resolved("stale result");
    const auto Error = neverc::translate::resolveExistingPath(Input, Resolved);
    EXPECT_FALSE(Error) << Error.message();
    EXPECT_EQ(Resolved.str().str(), Expected);
  }
}

TEST_F(TranslateArtifactTest, ExistingPathsEnforceWindowsInputBounds) {
  const auto Root = tmpFile("bounded-path-input");
  fs::create_directory(Root);
  const auto Physical = fs::canonical(Root).u8string();
  // This fixture exercises the ordinary component walk, not a verbatim path.
  ASSERT_FALSE(llvm::StringRef(Physical).starts_with("\\\\?\\"));
  ASSERT_FALSE(llvm::StringRef(Physical).starts_with("//?/"));
  constexpr std::size_t MaxBytes = 128u * 1024u;
  constexpr std::size_t MaxComponents = 32768u;
  ASSERT_LT(Physical.size(), MaxBytes);
  // Repeated separators have no named components. This exercises the exact
  // byte boundary with only the short fixture path's native lookups.
  std::string AtLimit = Physical;
  AtLimit.append(MaxBytes - AtLimit.size(), '\\');
  ASSERT_EQ(AtLimit.size(), MaxBytes);
  llvm::SmallString<256> Resolved("stale result");
  auto Error = neverc::translate::resolveExistingPath(AtLimit, Resolved);
  EXPECT_FALSE(Error) << Error.message();
  EXPECT_EQ(Resolved.str().str(), Physical);
  AtLimit.push_back('\\');
  Resolved = "stale result";
  Error = neverc::translate::resolveExistingPath(AtLimit, Resolved);
  EXPECT_EQ(Error, std::errc::filename_too_long);
  EXPECT_TRUE(Resolved.empty());

  // Exceed the component limit while remaining below the byte limit; the
  // parser must reject this before performing tens of thousands of lookups.
  std::string TooMany = Physical;
  for (std::size_t I = 0; I != MaxComponents + 1; ++I)
    TooMany += "/.";
  ASSERT_LT(TooMany.size(), MaxBytes);
  Resolved = "stale result";
  Error = neverc::translate::resolveExistingPath(TooMany, Resolved);
  EXPECT_EQ(Error, std::errc::filename_too_long);
  EXPECT_TRUE(Resolved.empty());
}

TEST_F(TranslateArtifactTest, ExistingPathsDoNotRequestAncestorReadSharing) {
  const auto Parent = tmpFile("metadata-sharing-parent");
  const auto Child = Parent / "child";
  fs::create_directories(Child);
  const auto Expected = fs::canonical(Child).u8string();
  constexpr DWORD ShareAll =
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  {
    // Restrict only read sharing on this owned directory. No ACL or token
    // changes are involved; existing children retain their usual access.
    HANDLE Held = ::CreateFileW(
        Parent.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(Held, INVALID_HANDLE_VALUE) << ::GetLastError();
    auto CloseHeld = llvm::make_scope_exit([&] {
      EXPECT_TRUE(::CloseHandle(Held)) << ::GetLastError();
    });
    HANDLE Read = ::CreateFileW(
        Parent.c_str(), GENERIC_READ, ShareAll, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const DWORD ReadError = Read == INVALID_HANDLE_VALUE ? ::GetLastError()
                                                         : ERROR_SUCCESS;
    if (Read != INVALID_HANDLE_VALUE)
      EXPECT_TRUE(::CloseHandle(Read)) << ::GetLastError();
    ASSERT_EQ(Read, INVALID_HANDLE_VALUE);
    ASSERT_EQ(ReadError, DWORD(ERROR_SHARING_VIOLATION));

    // The original whole-child lookup remains usable. Only requesting read
    // access on each ancestor introduces the sharing regression.
    llvm::SmallString<256> Whole;
    auto Error = llvm::sys::fs::real_path(Child.u8string(), Whole);
    ASSERT_FALSE(Error) << Error.message();
    ASSERT_EQ(Whole.str().str(), Expected);
    llvm::SmallString<256> Ordered("stale result");
    Error = neverc::translate::resolveExistingPath(
        (Parent / "child" / ".." / "child").u8string(), Ordered);
    ASSERT_FALSE(Error) << Error.message();
    EXPECT_EQ(Ordered.str().str(), Expected);
  } // Fatal assertions above also release Held before fixture cleanup.
  HANDLE Read = ::CreateFileW(Parent.c_str(), GENERIC_READ, ShareAll, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  ASSERT_NE(Read, INVALID_HANDLE_VALUE) << ::GetLastError();
  EXPECT_TRUE(::CloseHandle(Read)) << ::GetLastError();
  // Attribute-only access is exempt from this sharing restriction too. This
  // fixture does not prove ACL compatibility or distinguish it from access 0.
}
#endif

TEST_F(TranslateArtifactTest, PublicationAcceptsFiniteParentRevisits) {
  const auto Root = tmpFile("publication-paths");
  const auto Child = Root / "child";
  const auto Source = Root / "input.cpp";
  const auto Name = fs::u8path(u8"\u751f\u6210 output.nc");
  fs::create_directories(Child);
  writeFile(Source, "int f() { return 1; }\n");
  const auto Output = Child / ".." / "child" / Name;
  std::string Stage;
  {
    auto Writer = ArtifactWriter::create({Output.u8string(), "", ""},
                                        Source.u8string());
    ASSERT_TRUE(bool(Writer)) << llvm::toString(Writer.takeError()).str().str();
    Stage = (*Writer)->stagingDirectory().str();
    success((*Writer)->publish(files(**Writer), "{}"));
  }
  EXPECT_FALSE(fs::exists(fs::u8path(Stage)));
  const auto Published = Child / Name;
  EXPECT_TRUE(fs::is_regular_file(Published));
  EXPECT_EQ(Published.filename().u8string(), u8"\u751f\u6210 output.nc");
  auto Bytes = neverc::translate::readFile(Published.u8string());
  ASSERT_TRUE(bool(Bytes)) << llvm::toString(Bytes.takeError()).str().str();
  EXPECT_EQ(*Bytes, "int main() { return 0; }\n");
  EXPECT_FALSE(fs::exists(Root / Name));
}

TEST_F(TranslateArtifactTest, PublicationRejectsInvalidParentComponents) {
  const auto Root = tmpFile("invalid-publication-paths");
  const auto Source = Root / "input.cpp";
  fs::create_directory(Root);
  writeFile(Source, "int f() { return 1; }\n");
  auto Reject = [&](const fs::path &Output) {
    SCOPED_TRACE(Output.u8string());
    auto Writer = ArtifactWriter::create({Output.u8string(), "", ""},
                                        Source.u8string());
    ASSERT_FALSE(bool(Writer));
    EXPECT_NE(llvm::toString(Writer.takeError()).str().str().find("TR0502"),
              std::string::npos);
  };
  EXPECT_NO_FATAL_FAILURE(Reject(Root / "missing" / ".." / "output.nc"));
  for (const auto &Output :
       {Source / "." / "output.nc", Source / ".." / "output.nc"}) {
#ifdef _WIN32
    EXPECT_NO_FATAL_FAILURE(Reject(Output));
#else
    auto CheckNative = [&] {
      SCOPED_TRACE(Output.u8string());
      std::error_code NativeError;
      const auto Parent = fs::canonical(Output.parent_path(), NativeError);
      if (NativeError || !fs::is_directory(Parent, NativeError)) {
        Reject(Output);
        return;
      }
      ASSERT_FALSE(NativeError) << NativeError.message();
      ASSERT_EQ(Parent, fs::canonical(Root));
      const auto Name = Output.filename().u8string();
      const std::vector<Artifact> Expected = {
          {Name, "int main() { return 0; }\n"},
          {Name + ".map.json", "{\"map\":1}\n"},
          {Name + ".manifest.json", "{\"manifest\":1}\n"}};
      for (const auto &Artifact : Expected)
        ASSERT_FALSE(fs::exists(Parent / fs::u8path(Artifact.Name)));
      auto Cleanup = llvm::make_scope_exit([&] {
        for (const auto &Artifact : Expected) {
          std::error_code Error;
          fs::remove(Parent / fs::u8path(Artifact.Name), Error);
          EXPECT_FALSE(Error) << Error.message();
        }
      });
      std::string Stage;
      {
        auto Writer = ArtifactWriter::create({Output.u8string(), "", ""},
                                            Source.u8string());
        ASSERT_TRUE(bool(Writer))
            << llvm::toString(Writer.takeError()).str().str();
        Stage = (*Writer)->stagingDirectory().str();
        EXPECT_EQ((*Writer)->sourceName().str(), Expected[0].Name);
        EXPECT_EQ((*Writer)->mapName().str(), Expected[1].Name);
        EXPECT_EQ((*Writer)->manifestName().str(), Expected[2].Name);
        success((*Writer)->publish(Expected, "{}"));
      }
      EXPECT_FALSE(fs::exists(fs::u8path(Stage)));
      for (const auto &Artifact : Expected) {
        const auto Published = Parent / fs::u8path(Artifact.Name);
        EXPECT_TRUE(fs::is_regular_file(Published));
        auto Bytes = neverc::translate::readFile(Published.u8string());
        ASSERT_TRUE(bool(Bytes))
            << llvm::toString(Bytes.takeError()).str().str();
        EXPECT_EQ(*Bytes, Artifact.Contents);
        EXPECT_FALSE(fs::exists(tmp() / fs::u8path(Artifact.Name)));
      }
      EXPECT_EQ(std::distance(fs::directory_iterator(Root),
                              fs::directory_iterator()),
                4);
      EXPECT_EQ(readFile(Source), "int f() { return 1; }\n");
      expectNoArtifactTemporaryDirectories();
    };
    EXPECT_NO_FATAL_FAILURE(CheckNative());
#endif
  }
  EXPECT_FALSE(fs::exists(Root / "output.nc"));
  EXPECT_FALSE(fs::exists(Root / "missing"));
  EXPECT_EQ(std::distance(fs::directory_iterator(Root),
                          fs::directory_iterator()),
            1);
  EXPECT_EQ(readFile(Source), "int f() { return 1; }\n");
}

} // namespace
