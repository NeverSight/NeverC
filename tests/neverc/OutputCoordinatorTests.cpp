#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Foundation/Core/OutputTransaction.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace llvm;
using namespace neverc;

namespace {

class OutputCoordinatorTest : public testing::Test {
protected:
  void SetUp() override {
    SmallString<256> Temporary;
    ASSERT_FALSE(
        sys::fs::createUniqueDirectory("neverc-output-path", Temporary));
    Root = Temporary;
    SmallString<256> Canonical;
    ASSERT_FALSE(sys::fs::real_path(Root, Canonical));
    Root = Canonical;
  }

  void TearDown() override {
    if (!Root.empty())
      EXPECT_FALSE(sys::fs::remove_directories(Root));
  }

  std::string path(StringRef Name) const {
    SmallString<256> Result(Root);
    sys::path::append(Result, Name);
    return Result.str().str();
  }

  std::vector<std::string> rootEntries() const {
    std::vector<std::string> Entries;
    std::error_code Error;
    for (sys::fs::directory_iterator I(Root, Error), End;
         !Error && I != End; I.increment(Error))
      Entries.push_back(I->path());
    EXPECT_FALSE(Error) << Error.message();
    std::sort(Entries.begin(), Entries.end());
    return Entries;
  }

  SmallString<256> Root;
  OutputCoordinator Coordinator;
};

TEST_F(OutputCoordinatorTest, ResolvesParentDotsForANewOutput) {
  ASSERT_FALSE(sys::fs::create_directory(path("nested")));
  auto Canonical = Coordinator.canonicalize(path("nested/../artifact.o"));
  ASSERT_TRUE(static_cast<bool>(Canonical))
      << toString(Canonical.takeError()).str().str();
  EXPECT_EQ(*Canonical, path("artifact.o"));
}

TEST_F(OutputCoordinatorTest, RejectsEmptyAndNulPaths) {
  for (StringRef Path : {StringRef(), StringRef("a\0b", 3)}) {
    auto Canonical = Coordinator.canonicalize(Path);
    ASSERT_FALSE(static_cast<bool>(Canonical));
    consumeError(Canonical.takeError());
  }
}

class OutputCoordinatorDirectoryTest
    : public OutputCoordinatorTest,
      public testing::WithParamInterface<const char *> {};

TEST_P(OutputCoordinatorDirectoryTest, CanonicalDirectoryAliasesShareLease) {
  ASSERT_FALSE(sys::fs::create_directories(path("nested/child")));
  const std::string Spelling = path(GetParam());
  SmallString<256> Native;
  ASSERT_FALSE(sys::fs::real_path(Spelling, Native));
  auto Canonical = Coordinator.canonicalize(Spelling);
  ASSERT_TRUE(static_cast<bool>(Canonical))
      << toString(Canonical.takeError()).str().str();
  EXPECT_EQ(*Canonical, Native.str());

  const OutputLeaseOwner Owner{2, 1};
  auto First = Coordinator.acquire(Native, {}, Owner);
  ASSERT_TRUE(static_cast<bool>(First))
      << toString(First.takeError()).str().str();
  auto Alias = Coordinator.acquire(Spelling, {}, Owner);
  ASSERT_FALSE(static_cast<bool>(Alias));
  EXPECT_EQ(errorToErrorCode(Alias.takeError()),
            std::make_error_code(std::errc::file_exists));
}

INSTANTIATE_TEST_SUITE_P(
    DirectorySpellings, OutputCoordinatorDirectoryTest,
    testing::Values(".", "nested/..", "nested/", "nested/.",
                    "nested/child/.."));

TEST_F(OutputCoordinatorTest, MissingTerminalDirectoryUsesNativeLookup) {
  const std::string Spelling = path("missing/");
  SmallString<256> Native;
  const std::error_code NativeError = sys::fs::real_path(Spelling, Native);
  ASSERT_TRUE(NativeError);
  auto Canonical = Coordinator.canonicalize(Spelling);
  ASSERT_FALSE(static_cast<bool>(Canonical));
  EXPECT_EQ(errorToErrorCode(Canonical.takeError()), NativeError);
}

TEST_F(OutputCoordinatorTest, DirectoryLeaseDoesNotPermitFileReplacement) {
  ASSERT_FALSE(sys::fs::create_directory(path("nested")));
  const auto Before = rootEntries();
  auto Output = OutputTransaction::createFile(Coordinator, path("nested/."), 16);
  ASSERT_TRUE(static_cast<bool>(Output))
      << toString(Output.takeError()).str().str();
  const uint8_t Contents[] = {'o', 'k'};
  ASSERT_EQ((*Output)->write(Contents), OutputTransactionResult::Success);
  ASSERT_EQ((*Output)->finish(), OutputTransactionResult::Success);
  auto Committed = (*Output)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());
  EXPECT_TRUE(sys::fs::is_directory(path("nested")));
  EXPECT_EQ(rootEntries(), Before);
}

class OutputCoordinatorFileSuffixTest
    : public OutputCoordinatorTest,
      public testing::WithParamInterface<const char *> {};

TEST_P(OutputCoordinatorFileSuffixTest, NativeLookupPreservesExistingOutputs) {
  std::error_code Error;
  {
    raw_fd_ostream Existing(path("artifact.o"), Error);
    ASSERT_FALSE(Error) << Error.message();
    Existing << "original";
    Existing.close();
    const std::error_code WriteError = Existing.error();
    Existing.clear_error();
    ASSERT_FALSE(WriteError) << WriteError.message();
  }
  const auto Before = rootEntries();
  const std::string Spelling = path(GetParam());
  SmallString<256> Native;
  const std::error_code NativeError = sys::fs::real_path(Spelling, Native);
  auto Canonical = Coordinator.canonicalize(Spelling);
  if (NativeError) {
    ASSERT_FALSE(static_cast<bool>(Canonical));
    EXPECT_EQ(errorToErrorCode(Canonical.takeError()), NativeError);
    auto Output = OutputTransaction::createFile(Coordinator, Spelling, 16);
    ASSERT_FALSE(static_cast<bool>(Output));
    EXPECT_EQ(errorToErrorCode(Output.takeError()), NativeError);
  } else {
    ASSERT_TRUE(static_cast<bool>(Canonical))
        << toString(Canonical.takeError()).str().str();
    EXPECT_EQ(*Canonical, Native.str());
  }
  auto Existing = MemoryBuffer::getFile(path("artifact.o"));
  ASSERT_TRUE(static_cast<bool>(Existing)) << Existing.getError().message();
  EXPECT_EQ((*Existing)->getBuffer(), "original");
  EXPECT_EQ(rootEntries(), Before);
}

INSTANTIATE_TEST_SUITE_P(
    FileSuffixes, OutputCoordinatorFileSuffixTest,
    testing::Values("artifact.o/", "artifact.o/.", "missing.o/", "missing.o/."));

TEST_F(OutputCoordinatorTest, SameOwnerCannotAcquireAnAliasTwice) {
  const OutputLeaseOwner Owner{1, 1};
  auto First = Coordinator.acquire(path("artifact.o"), {}, Owner);
  ASSERT_TRUE(static_cast<bool>(First))
      << toString(First.takeError()).str().str();
  auto Alias = Coordinator.acquire(path("./artifact.o"), {}, Owner);
  ASSERT_FALSE(static_cast<bool>(Alias));
  EXPECT_EQ(errorToErrorCode(Alias.takeError()),
            std::make_error_code(std::errc::file_exists));
  First->release();
  auto Reused = Coordinator.acquire(path("./artifact.o"), {}, Owner);
  EXPECT_TRUE(static_cast<bool>(Reused))
      << toString(Reused.takeError()).str().str();
}

#ifndef _WIN32
// POSIX resolves '..' after traversing a directory symlink. Windows path
// normalization has a different contract; these cases exercise POSIX lookup.
class OutputCoordinatorSymlinkTest : public OutputCoordinatorTest {
protected:
  void SetUp() override {
    OutputCoordinatorTest::SetUp();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_FALSE(sys::fs::create_directories(path("actual/nested")));
    ASSERT_EQ(::symlink(path("actual/nested").c_str(), path("alias").c_str()),
              0);
  }
};

TEST_F(OutputCoordinatorSymlinkTest, ResolvesSymlinkBeforeParentTraversal) {
  auto Canonical = Coordinator.canonicalize(path("alias/../artifact.o"));
  ASSERT_TRUE(static_cast<bool>(Canonical))
      << toString(Canonical.takeError()).str().str();
  EXPECT_EQ(*Canonical, path("actual/artifact.o"));
}

TEST_F(OutputCoordinatorSymlinkTest, PreservesFinalSymlinkPublicationIdentity) {
  auto Leaf = Coordinator.canonicalize(path("alias"));
  ASSERT_TRUE(static_cast<bool>(Leaf))
      << toString(Leaf.takeError()).str().str();
  EXPECT_EQ(*Leaf, path("alias"));
  auto Directory = Coordinator.canonicalize(path("alias/."));
  ASSERT_TRUE(static_cast<bool>(Directory))
      << toString(Directory.takeError()).str().str();
  EXPECT_EQ(*Directory, path("actual/nested"));
}

TEST_F(OutputCoordinatorSymlinkTest, AliasAndRealParentShareAnOutputLease) {
  const OutputLeaseOwner Owner{1, 2};
  auto First = Coordinator.acquire(path("actual/artifact.o"), {}, Owner);
  ASSERT_TRUE(static_cast<bool>(First))
      << toString(First.takeError()).str().str();
  auto Alias = Coordinator.acquire(path("alias/../artifact.o"), {}, Owner);
  ASSERT_FALSE(static_cast<bool>(Alias));
  EXPECT_EQ(errorToErrorCode(Alias.takeError()),
            std::make_error_code(std::errc::file_exists));
}

TEST_F(OutputCoordinatorSymlinkTest, RejectsAliasesInAnOutputLeaseSet) {
  std::string Real = path("actual/artifact.o");
  std::string Alias = path("alias/../artifact.o");
  auto Leases = Coordinator.acquireAll({Real, Alias});
  ASSERT_FALSE(static_cast<bool>(Leases));
  EXPECT_EQ(errorToErrorCode(Leases.takeError()),
            std::make_error_code(std::errc::file_exists));
}

TEST_F(OutputCoordinatorSymlinkTest, PublishesInTheResolvedParent) {
  auto Output = OutputTransaction::createFile(Coordinator,
                                              path("alias/../artifact.o"), 16);
  ASSERT_TRUE(static_cast<bool>(Output))
      << toString(Output.takeError()).str().str();
  const uint8_t Contents[] = {'o', 'k'};
  ASSERT_EQ((*Output)->write(Contents), OutputTransactionResult::Success);
  ASSERT_EQ((*Output)->finish(), OutputTransactionResult::Success);
  auto Committed = (*Output)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed))
      << toString(Committed.takeError()).str().str();
  auto File = MemoryBuffer::getFile(path("actual/artifact.o"));
  ASSERT_TRUE(static_cast<bool>(File)) << File.getError().message();
  EXPECT_EQ((*File)->getBuffer(), "ok");
  EXPECT_FALSE(sys::fs::exists(path("artifact.o")));
}

TEST_F(OutputCoordinatorTest, DoesNotEraseAMissingParentDuringLookup) {
  auto Canonical = Coordinator.canonicalize(path("missing/../artifact.o"));
  ASSERT_FALSE(static_cast<bool>(Canonical));
  EXPECT_EQ(errorToErrorCode(Canonical.takeError()),
            std::make_error_code(std::errc::no_such_file_or_directory));
}
#endif

} // namespace
