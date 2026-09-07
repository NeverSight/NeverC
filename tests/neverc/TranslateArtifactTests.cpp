#include "../../neverc/lib/Translate/ArtifactWriter.h"
#include "NeverCTestFixture.h"

#include "llvm/Support/Error.h"

namespace {
using neverc::translate::Artifact;
using neverc::translate::ArtifactOptions;
using neverc::translate::ArtifactWriter;

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

} // namespace
