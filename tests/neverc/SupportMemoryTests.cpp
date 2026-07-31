#include "csupport/lmemory.h"
#ifdef _WIN32
#include "csupport/lprocess.h"
#endif
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemAlloc.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

TEST(SupportMemoryTest, ZeroSizedBufferAllocationIsStillNonNull) {
  void *Memory = llvm::allocate_buffer(0, alignof(std::max_align_t));
  ASSERT_NE(Memory, nullptr);
  llvm::deallocate_buffer(Memory, 0, alignof(std::max_align_t));
}

TEST(SupportMemoryTest, EmptyInstructionCacheRangeNeedsNoBackingAddress) {
  csupport_invalidate_icache(nullptr, 0);
  llvm::sys::Memory::InvalidateInstructionCache(nullptr, 0);
}

TEST(SupportMemoryTest, RejectsOverflowingMappedAllocationSize) {
  size_t AllocatedSize = 1;
  int Error = 0;
  void *Memory = csupport_mmap_alloc_mapped(
      std::numeric_limits<size_t>::max(), nullptr, 0,
      CSUPPORT_MF_READ | CSUPPORT_MF_WRITE, &AllocatedSize, &Error);

  EXPECT_EQ(Memory, nullptr);
  EXPECT_EQ(AllocatedSize, 0u);
  EXPECT_NE(Error, 0);
}

TEST(SupportMemoryTest, MappedAllocationCanBeProtectedAndReleased) {
  size_t AllocatedSize = 0;
  int Error = 0;
  void *Memory = csupport_mmap_alloc_mapped(
      1, nullptr, 0, CSUPPORT_MF_READ | CSUPPORT_MF_WRITE, &AllocatedSize,
      &Error);
  ASSERT_NE(Memory, nullptr) << Error;
  ASSERT_GE(AllocatedSize, 1u);

  std::memset(Memory, 0x5a, 1);
  EXPECT_EQ(csupport_mmap_protect(Memory, AllocatedSize, CSUPPORT_MF_READ), 0);
  EXPECT_EQ(csupport_mmap_release(Memory, AllocatedSize), 0);
}

#ifdef _WIN32
TEST(SupportMemoryTest, WindowsLockErrorsUseErrnoDomain) {
  EXPECT_EQ(csupport_fd_try_lock_for(-1, 10), EBADF);
}
#endif

#ifndef _WIN32
TEST(SupportMemoryTest, PrivateFileMappingDoesNotModifyTheFile) {
  constexpr llvm::StringLiteral Original = "original";
  llvm::SmallString<128> Path;
  int FD = -1;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile(
      "neverc-private-map", "tmp", FD, Path));
  llvm::FileRemover Cleanup(Path);

  {
    llvm::raw_fd_ostream Output(FD, false);
    Output << Original;
    Output.flush();
    ASSERT_FALSE(Output.has_error());
  }

  std::error_code Error;
  {
    llvm::sys::fs::mapped_file_region Mapping(
        FD, llvm::sys::fs::mapped_file_region::priv, Original.size(), 0,
        Error);
    ASSERT_FALSE(Error);
    ASSERT_TRUE(Mapping);
    Mapping.data()[0] = 'X';
  }
  ASSERT_FALSE(llvm::sys::fs::closeFile(FD));

  auto Contents = llvm::MemoryBuffer::getFile(Path);
  ASSERT_TRUE(static_cast<bool>(Contents));
  EXPECT_EQ((*Contents)->getBuffer(), Original);
}

TEST(SupportMemoryTest, FileLockTimeoutPreservesDurationsWiderThanUnsigned) {
  llvm::SmallString<128> Path;
  int FD = -1;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile(
      "neverc-wide-lock-timeout", "tmp", FD, Path));
  llvm::FileRemover Cleanup(Path);

  int ReadyPipe[2];
  ASSERT_EQ(::pipe(ReadyPipe), 0);

  pid_t Child = ::fork();
  ASSERT_GE(Child, 0);
  if (Child == 0) {
    ::close(ReadyPipe[0]);
    std::error_code Error = llvm::sys::fs::lockFile(FD);
    const char Ready = Error ? '\0' : '\1';
    const bool Notified = ::write(ReadyPipe[1], &Ready, 1) == 1;
    if (!Error) {
      ::usleep(50'000);
      Error = llvm::sys::fs::unlockFile(FD);
    }
    ::close(ReadyPipe[1]);
    ::_exit(!Error && Notified ? 0 : 1);
  }

  ::close(ReadyPipe[1]);
  char Ready = '\0';
  ASSERT_EQ(::read(ReadyPipe[0], &Ready, 1), 1);
  ASSERT_EQ(Ready, '\1');
  ::close(ReadyPipe[0]);

  using Milliseconds = std::chrono::milliseconds;
  const auto Timeout = Milliseconds(
      static_cast<Milliseconds::rep>(std::numeric_limits<unsigned>::max()) + 1);
  std::error_code Error = llvm::sys::fs::tryLockFile(FD, Timeout);
  EXPECT_FALSE(Error) << Error.message();
  if (!Error)
    EXPECT_FALSE(llvm::sys::fs::unlockFile(FD));

  int Status = 0;
  ASSERT_EQ(::waitpid(Child, &Status, 0), Child);
  EXPECT_TRUE(WIFEXITED(Status));
  EXPECT_EQ(WEXITSTATUS(Status), 0);
  EXPECT_FALSE(llvm::sys::fs::closeFile(FD));
}
#endif
