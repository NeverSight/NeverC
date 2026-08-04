#include "neverc/Release/ReleaseClient.h"
#include "neverc/Runtime/RuntimeManager.h"
#include "neverc/Update/UpdateManager.h"
#include "neverc/Update/UpdateTransaction.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <string>
#include <system_error>

using namespace llvm;
using namespace neverc;

namespace {

std::string errorText(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return std::string(Message.begin(), Message.end());
}

template <typename T> T takeValue(Expected<T> Value) {
  if (!Value) {
    ADD_FAILURE() << errorText(Value.takeError());
    return {};
  }
  return std::move(*Value);
}

class ScratchDirectory {
public:
  SmallString<256> Path;

  ScratchDirectory() {
    EXPECT_FALSE(sys::fs::createUniqueDirectory("neverc-update-test", Path));
    SmallString<256> Canonical;
    std::error_code EC = sys::fs::real_path(Path, Canonical);
    EXPECT_FALSE(EC);
    if (!EC)
      Path = Canonical;
  }
  ~ScratchDirectory() { (void)sys::fs::remove_directories(Path); }
};

void writeFile(StringRef Path, StringRef Contents) {
  SmallString<256> Parent(sys::path::parent_path(Path));
  ASSERT_FALSE(sys::fs::create_directories(Parent));
  std::error_code EC;
  raw_fd_ostream Output(Path, EC);
  ASSERT_FALSE(EC);
  Output << Contents;
}

std::string readFile(StringRef Path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer = MemoryBuffer::getFile(Path);
  EXPECT_TRUE(Buffer);
  if (!Buffer)
    return {};
  return Buffer.get()->getBuffer().str();
}

void expectSuccess(Error Result) {
  if (Result)
    FAIL() << errorText(std::move(Result));
}

TEST(ReleaseClientTest, ParsesStandardAndBinaryChecksumLines) {
  std::string Hash(64, 'a');
  std::string Manifest = Hash + "  neverc-linux-x64.zip\n" +
                         std::string(64, 'b') + " *other.zip\n";

  EXPECT_EQ(takeValue(release::parseChecksumManifest(Manifest,
                                                     "neverc-linux-x64.zip")),
            Hash);
  EXPECT_EQ(takeValue(release::parseChecksumManifest(Manifest, "other.zip")),
            std::string(64, 'b'));
}

TEST(ReleaseClientTest, RejectsMissingAndMalformedChecksums) {
  Expected<std::string> Missing = release::parseChecksumManifest(
      std::string(64, 'a') + "  other.zip\n", "neverc-linux-x64.zip");
  EXPECT_FALSE(Missing);
  consumeError(Missing.takeError());

  Expected<std::string> Malformed = release::parseChecksumManifest(
      std::string(63, 'a') + "z  neverc-linux-x64.zip\n",
      "neverc-linux-x64.zip");
  EXPECT_FALSE(Malformed);
  consumeError(Malformed.takeError());
}

TEST(ReleaseClientTest, NormalizesOnlySupportedReleaseTags) {
  EXPECT_EQ(release::normalizeReleaseTag("3389.1.2"), "v3389.1.2");
  EXPECT_EQ(release::normalizeReleaseTag(" V3389.1.2 "), "v3389.1.2");
  EXPECT_EQ(release::normalizeReleaseTag("latest"), "latest");
  EXPECT_TRUE(release::normalizeReleaseTag("v3389.1").empty());
  EXPECT_TRUE(release::normalizeReleaseTag("v3389.1.2-rc1").empty());
}

TEST(ReleaseClientTest, HashesTheExactDownloadedArchiveBytes) {
  ScratchDirectory Scratch;
  SmallString<256> Archive(Scratch.Path);
  sys::path::append(Archive, "asset.zip");
  SmallString<256> Manifest(Scratch.Path);
  sys::path::append(Manifest, "SHA256SUMS");
  StringRef Bytes("exact archive bytes");
  writeFile(Archive, Bytes);
  ArrayRef<uint8_t> Data(reinterpret_cast<const uint8_t *>(Bytes.data()),
                         Bytes.size());
  std::string Hash = toHex(SHA256::hash(Data), /*LowerCase=*/true);
  writeFile(Manifest, Hash + "  asset.zip\n");
  expectSuccess(release::verifyReleaseAsset(Archive, Manifest, "asset.zip"));

  writeFile(Archive, "changed archive bytes");
  Error Mismatch = release::verifyReleaseAsset(Archive, Manifest, "asset.zip");
  EXPECT_TRUE(static_cast<bool>(Mismatch));
  consumeError(std::move(Mismatch));
}

TEST(ReleaseClientTest, MapsTheCurrentHostToAPublishedAsset) {
  Expected<release::HostDistribution> Host = release::getHostDistribution();
  if (!Host)
    FAIL() << errorText(Host.takeError());
  EXPECT_FALSE(Host->Platform.empty());
  EXPECT_TRUE(StringRef(Host->CompilerAsset).ends_with(".zip"));
  EXPECT_TRUE(StringRef(Host->ExecutableRelativePath)
                  .ends_with(
#ifdef _WIN32
                      "neverc.exe"
#else
                      "neverc"
#endif
                      ));
}

TEST(RuntimeCatalogTest, HasOneCanonicalDefinitionForPublishedTargets) {
  ArrayRef<runtime::RuntimeTarget> Targets = runtime::getRuntimeTargets();
  ASSERT_EQ(Targets.size(), 7u);
  EXPECT_EQ(Targets[0].Name, "windows-x64");
  EXPECT_EQ(Targets[0].CheckDir, "windows/x64");
  EXPECT_EQ(Targets[0].SharedDir, "windows/shared");
  EXPECT_EQ(Targets[1].Name, "windows-arm64");
  EXPECT_EQ(Targets[1].SharedDir, "windows/shared");
  EXPECT_EQ(Targets[2].Name, "linux-x64");
  EXPECT_TRUE(Targets[2].SharedDir.empty());
  EXPECT_EQ(Targets.back().Name, "android-kernel-arm64");
  EXPECT_EQ(Targets.back().CheckDir, "android/kernel");
  EXPECT_EQ(runtime::findRuntimeTarget("linux-arm64"), &Targets[3]);
  EXPECT_EQ(runtime::findRuntimeTarget("not-a-target"), nullptr);
}

TEST(UpdateTransactionTest, RejectsPathsOutsideTheStagingAndInstallRoots) {
  ScratchDirectory Scratch;
  SmallString<256> Stage(Scratch.Path);
  sys::path::append(Stage, ".neverc-update-paths");
  ASSERT_FALSE(sys::fs::create_directories(Stage));

  Expected<update::UpdateTransaction> Transaction =
      update::UpdateTransaction::create(Scratch.Path, Stage, "v3389.1.1");
  if (!Transaction)
    FAIL() << errorText(Transaction.takeError());

  Error Parent = Transaction->addEntry("compiler/bin/neverc", "../bin/neverc");
  EXPECT_TRUE(static_cast<bool>(Parent));
  consumeError(std::move(Parent));
  Error Absolute = Transaction->addEntry("/tmp/neverc", "bin/neverc");
  EXPECT_TRUE(static_cast<bool>(Absolute));
  consumeError(std::move(Absolute));
}

TEST(UpdateTransactionTest, ReplacesExactItemsAndPreservesSiblings) {
  ScratchDirectory Scratch;
  SmallString<256> Stage(Scratch.Path);
  sys::path::append(Stage, ".neverc-update-commit");
  ASSERT_FALSE(sys::fs::create_directories(Stage));

  SmallString<256> OldCompiler(Scratch.Path);
  sys::path::append(OldCompiler, "bin", "neverc");
  SmallString<256> CustomTool(Scratch.Path);
  sys::path::append(CustomTool, "bin", "custom-tool");
  SmallString<256> NewCompiler(Stage);
  sys::path::append(NewCompiler, "compiler", "bin", "neverc");
  SmallString<256> OldRuntime(Scratch.Path);
  sys::path::append(OldRuntime, "runtime", "linux", "x64", "marker");
  SmallString<256> NewRuntime(Stage);
  sys::path::append(NewRuntime, "runtimes", "linux", "x64", "marker");
  writeFile(OldCompiler, "old compiler");
  writeFile(CustomTool, "custom");
  writeFile(NewCompiler, "new compiler");
  writeFile(OldRuntime, "old runtime");
  writeFile(NewRuntime, "new runtime");

  Expected<update::UpdateTransaction> Transaction =
      update::UpdateTransaction::create(Scratch.Path, Stage, "v3389.1.1");
  if (!Transaction)
    FAIL() << errorText(Transaction.takeError());
  expectSuccess(Transaction->addEntry("compiler/bin/neverc", "bin/neverc"));
  expectSuccess(
      Transaction->addEntry("runtimes/linux/x64", "runtime/linux/x64"));
  expectSuccess(Transaction->apply());

  EXPECT_EQ(readFile(OldCompiler), "new compiler");
  EXPECT_EQ(readFile(CustomTool), "custom");
  EXPECT_EQ(readFile(OldRuntime), "new runtime");
}

TEST(UpdateTransactionTest, RollsBackAfterAnInjectedRenameFailure) {
  ScratchDirectory Scratch;
  SmallString<256> Stage(Scratch.Path);
  sys::path::append(Stage, ".neverc-update-rollback");
  ASSERT_FALSE(sys::fs::create_directories(Stage));

  SmallString<256> OldOne(Scratch.Path);
  sys::path::append(OldOne, "bin", "one");
  SmallString<256> OldTwo(Scratch.Path);
  sys::path::append(OldTwo, "bin", "two");
  SmallString<256> NewOne(Stage);
  sys::path::append(NewOne, "compiler", "bin", "one");
  SmallString<256> NewTwo(Stage);
  sys::path::append(NewTwo, "compiler", "bin", "two");
  writeFile(OldOne, "old one");
  writeFile(OldTwo, "old two");
  writeFile(NewOne, "new one");
  writeFile(NewTwo, "new two");

  unsigned RenameCount = 0;
  update::UpdateTransaction::RenameFunction Rename =
      [&](StringRef From, StringRef To) -> std::error_code {
    if (++RenameCount == 4)
      return std::make_error_code(std::errc::permission_denied);
    return sys::fs::rename(From, To);
  };
  Expected<update::UpdateTransaction> Transaction =
      update::UpdateTransaction::create(Scratch.Path, Stage, "v3389.1.1",
                                        std::move(Rename));
  if (!Transaction)
    FAIL() << errorText(Transaction.takeError());
  expectSuccess(Transaction->addEntry("compiler/bin/one", "bin/one"));
  expectSuccess(Transaction->addEntry("compiler/bin/two", "bin/two"));
  Error Apply = Transaction->apply();
  EXPECT_TRUE(static_cast<bool>(Apply));
  consumeError(std::move(Apply));

  EXPECT_EQ(readFile(OldOne), "old one");
  EXPECT_EQ(readFile(OldTwo), "old two");
  EXPECT_EQ(readFile(NewOne), "new one");
  EXPECT_EQ(readFile(NewTwo), "new two");
}

TEST(UpdateTransactionTest, PersistsAndValidatesItsRelativePathPlan) {
  ScratchDirectory Scratch;
  SmallString<256> Stage(Scratch.Path);
  sys::path::append(Stage, ".neverc-update-journal");
  ASSERT_FALSE(sys::fs::create_directories(Stage));

  Expected<update::UpdateTransaction> Transaction =
      update::UpdateTransaction::create(Scratch.Path, Stage, "v3389.1.1");
  if (!Transaction)
    FAIL() << errorText(Transaction.takeError());
  expectSuccess(Transaction->addEntry("compiler/bin/neverc", "bin/neverc"));
  expectSuccess(Transaction->writePlan());

  Expected<update::UpdateTransaction> Read =
      update::UpdateTransaction::readPlan(Stage);
  if (!Read)
    FAIL() << errorText(Read.takeError());
  EXPECT_EQ(Read->root(), Scratch.Path);
  EXPECT_EQ(Read->stage(), Stage);
  EXPECT_EQ(Read->targetTag(), "v3389.1.1");
  ASSERT_EQ(Read->entries().size(), 1u);
  EXPECT_EQ(Read->entries()[0].StagedRelativePath, "compiler/bin/neverc");
  EXPECT_EQ(Read->entries()[0].LiveRelativePath, "bin/neverc");
}

#ifndef _WIN32
TEST(UpdateTransactionTest, RejectsSymlinkedParentsThatCouldEscapeTheRoot) {
  ScratchDirectory Scratch;
  SmallString<256> Stage(Scratch.Path);
  sys::path::append(Stage, ".neverc-update-symlink-parent");
  SmallString<256> StagedRuntime(Stage);
  sys::path::append(StagedRuntime, "runtimes", "linux", "x64", "marker");
  writeFile(StagedRuntime, "new runtime");

  SmallString<256> Outside(Scratch.Path);
  sys::path::append(Outside, "outside");
  SmallString<256> OutsideRuntime(Outside);
  sys::path::append(OutsideRuntime, "x64", "marker");
  writeFile(OutsideRuntime, "must remain untouched");
  SmallString<256> RuntimeDirectory(Scratch.Path);
  sys::path::append(RuntimeDirectory, "runtime");
  ASSERT_FALSE(sys::fs::create_directories(RuntimeDirectory));
  SmallString<256> Symlink(RuntimeDirectory);
  sys::path::append(Symlink, "linux");
  ASSERT_FALSE(sys::fs::create_link(Outside, Symlink));

  Expected<update::UpdateTransaction> Transaction =
      update::UpdateTransaction::create(Scratch.Path, Stage, "v3389.1.1");
  if (!Transaction)
    FAIL() << errorText(Transaction.takeError());
  expectSuccess(
      Transaction->addEntry("runtimes/linux/x64", "runtime/linux/x64"));
  Error Apply = Transaction->apply();
  EXPECT_TRUE(static_cast<bool>(Apply));
  consumeError(std::move(Apply));
  EXPECT_EQ(readFile(OutsideRuntime), "must remain untouched");
  EXPECT_EQ(readFile(StagedRuntime), "new runtime");
}
#endif

TEST(UpdateManagerTest, ComparesReleaseComponentsNumerically) {
  EXPECT_EQ(takeValue(update::compareReleaseTags("v3389.1.2", "v3389.1.1")),
            update::VersionRelation::Older);
  EXPECT_EQ(takeValue(update::compareReleaseTags("3389.1.2", "v3389.1.2")),
            update::VersionRelation::Same);
  EXPECT_EQ(takeValue(update::compareReleaseTags("v3389.9.9", "v3389.10.0")),
            update::VersionRelation::Newer);
}

TEST(UpdateManagerTest, IgnoresEmbeddedRuntimeInAWindowsShapedInstallTree) {
  ScratchDirectory Scratch;
  SmallString<256> Install(Scratch.Path);
  sys::path::append(Install, "install");
  SmallString<256> Compiler(Install);
  sys::path::append(Compiler, "bin", "neverc.exe");
  SmallString<256> Library(Install);
  sys::path::append(Library, "lib", "neverc.lib");
  SmallString<256> SDK(Install);
  sys::path::append(SDK, "pluginsdk", "include", "api.h");
  SmallString<256> EmbeddedRuntime(Install);
  sys::path::append(EmbeddedRuntime, "runtime", "windows", "x64", "marker");
  writeFile(Compiler, "compiler");
  writeFile(Library, "library");
  writeFile(SDK, "sdk");
  writeFile(EmbeddedRuntime, "must be ignored");

  Expected<std::vector<std::string>> Files =
      update::collectCompilerInstallFiles(Install);
  if (!Files)
    FAIL() << errorText(Files.takeError());
  EXPECT_EQ(*Files,
            (std::vector<std::string>{"bin/neverc.exe", "lib/neverc.lib",
                                      "pluginsdk/include/api.h"}));
}

#ifndef _WIN32
TEST(NeverCUpdateIntegrationTest,
     DowngradeLatestChecksumFailureAndSameVersionRepair) {
  StringRef Shell("/bin/sh");
  StringRef Args[] = {Shell, NEVERC_UPDATE_TEST_SCRIPT, NEVERC_BINARY, "all"};
  SmallString<256> Message;
  bool ExecutionFailed = false;
  int RC = sys::ExecuteAndWait(Shell, Args, /*Env=*/std::nullopt,
                               /*Redirects=*/{}, /*SecondsToWait=*/150,
                               /*MemoryLimit=*/0, &Message, &ExecutionFailed);
  EXPECT_FALSE(ExecutionFailed) << Message.c_str();
  EXPECT_EQ(RC, 0) << Message.c_str();
}
#endif

} // namespace
