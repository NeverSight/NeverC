#include "csupport/lmemory.h"

#include "gtest/gtest.h"

#include <cstring>
#include <limits>

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
