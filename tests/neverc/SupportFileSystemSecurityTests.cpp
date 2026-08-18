//===- SupportFileSystemSecurityTests.cpp - Filesystem race regressions ---===//

#include "csupport/lsignals.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/stat.h>

using namespace llvm;

namespace {

void writeFile(StringRef Path, StringRef Contents) {
  std::error_code Error;
  raw_fd_ostream Output(Path, Error);
  ASSERT_FALSE(Error);
  Output << Contents;
}

} // namespace

TEST(SupportFileSystemSecurityTest, RemoveMatchesPosixForFifo) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory("neverc-remove-fifo", Directory));
  auto Cleanup =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });

  SmallString<160> Fifo(Directory);
  sys::path::append(Fifo, "notifications");
  ASSERT_EQ(::mkfifo(Fifo.c_str(), 0600), 0);

  EXPECT_FALSE(sys::fs::remove(Fifo, /*IgnoreNonExisting=*/false));
  EXPECT_FALSE(sys::fs::exists(Fifo));
}

TEST(SupportFileSystemSecurityTest, RecursiveRemovalDoesNotFollowRootSymlink) {
  SmallString<128> Directory;
  ASSERT_FALSE(
      sys::fs::createUniqueDirectory("neverc-remove-symlink", Directory));
  auto Cleanup =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });

  SmallString<160> Target(Directory);
  sys::path::append(Target, "target");
  ASSERT_FALSE(sys::fs::create_directory(Target));

  SmallString<192> Sentinel(Target);
  sys::path::append(Sentinel, "keep.txt");
  writeFile(Sentinel, "must survive");

  SmallString<160> Link(Directory);
  sys::path::append(Link, "link");
  ASSERT_FALSE(sys::fs::create_link(Target, Link));

  EXPECT_FALSE(sys::fs::remove_directories(Link, /*IgnoreErrors=*/false));
  EXPECT_FALSE(sys::fs::exists(Link));
  EXPECT_TRUE(sys::fs::exists(Target));
  EXPECT_TRUE(sys::fs::exists(Sentinel));
}

TEST(SupportFileSystemSecurityTest,
     RecursiveRemovalDoesNotFollowNestedSymlink) {
  SmallString<128> Directory;
  ASSERT_FALSE(
      sys::fs::createUniqueDirectory("neverc-remove-nested-link", Directory));
  auto Cleanup =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });

  SmallString<160> Outside(Directory);
  sys::path::append(Outside, "outside");
  ASSERT_FALSE(sys::fs::create_directory(Outside));
  SmallString<192> Sentinel(Outside);
  sys::path::append(Sentinel, "keep.txt");
  writeFile(Sentinel, "must survive");

  SmallString<160> Victim(Directory);
  sys::path::append(Victim, "victim");
  ASSERT_FALSE(sys::fs::create_directory(Victim));
  SmallString<192> Link(Victim);
  sys::path::append(Link, "outside-link");
  ASSERT_FALSE(sys::fs::create_link(Outside, Link));

  EXPECT_FALSE(sys::fs::remove_directories(Victim, /*IgnoreErrors=*/false));
  EXPECT_FALSE(sys::fs::exists(Victim));
  EXPECT_TRUE(sys::fs::exists(Outside));
  EXPECT_TRUE(sys::fs::exists(Sentinel));
}

TEST(SupportFileSystemSecurityTest, RecursiveRemovalSupportsSymlinkedParent) {
  SmallString<128> Directory;
  ASSERT_FALSE(
      sys::fs::createUniqueDirectory("neverc-remove-linked-parent", Directory));
  auto Cleanup =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });

  SmallString<160> Parent(Directory);
  sys::path::append(Parent, "parent");
  ASSERT_FALSE(sys::fs::create_directory(Parent));
  SmallString<192> Victim(Parent);
  sys::path::append(Victim, "victim");
  ASSERT_FALSE(sys::fs::create_directory(Victim));

  SmallString<160> ParentLink(Directory);
  sys::path::append(ParentLink, "parent-link");
  ASSERT_FALSE(sys::fs::create_link(Parent, ParentLink));
  SmallString<192> LinkedVictim(ParentLink);
  sys::path::append(LinkedVictim, "victim");

  EXPECT_FALSE(
      sys::fs::remove_directories(LinkedVictim, /*IgnoreErrors=*/false));
  EXPECT_FALSE(sys::fs::exists(Victim));
  EXPECT_TRUE(sys::fs::exists(Parent));
  EXPECT_TRUE(sys::fs::exists(ParentLink));
}

TEST(SupportFileSystemSecurityTest,
     SignalCleanupUnlinksDirectorySymlinkWithoutFollowingIt) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory("neverc-signal-link", Directory));
  auto Cleanup =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });

  SmallString<160> Target(Directory);
  sys::path::append(Target, "target");
  ASSERT_FALSE(sys::fs::create_directory(Target));
  SmallString<192> Sentinel(Target);
  sys::path::append(Sentinel, "keep.txt");
  writeFile(Sentinel, "must survive");

  SmallString<160> Link(Directory);
  sys::path::append(Link, "cleanup-link");
  ASSERT_FALSE(sys::fs::create_link(Target, Link));

  csupport_file_remove_list_insert(Link.c_str());
  auto CleanupRemoveList =
      make_scope_exit([] { csupport_file_remove_list_cleanup(); });
  csupport_file_remove_list_remove_all();

  EXPECT_FALSE(sys::fs::exists(Link));
  EXPECT_TRUE(sys::fs::exists(Target));
  EXPECT_TRUE(sys::fs::exists(Sentinel));
}

#endif // !_WIN32
