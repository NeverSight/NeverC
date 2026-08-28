//===- SupportFileOutputBufferTests.cpp - Output buffer policies ---------===//

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace llvm;

namespace {

constexpr size_t OutputSize = 2 * 1024 * 1024 + 257;

uint8_t patternByte(size_t Index) {
  return static_cast<uint8_t>((Index * 131U + Index / 251U + 17U) & 0xffU);
}

void writeAndVerifyOutput(unsigned Flags) {
  SmallString<128> Directory;
  ASSERT_FALSE(
      sys::fs::createUniqueDirectory("neverc-file-output-buffer", Directory));
  auto Cleanup =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });

  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "output.bin");

  {
    bool FileBacked = true;
    auto BufferOrError = FileOutputBuffer::createWithFileBacking(
        OutputPath, OutputSize, Flags, FileBacked);
    ASSERT_TRUE(static_cast<bool>(BufferOrError))
        << toString(BufferOrError.takeError()).str().str();
    ASSERT_EQ((*BufferOrError)->getBufferSize(), OutputSize);

    if (Flags & FileOutputBuffer::F_no_mmap)
      EXPECT_FALSE(FileBacked);

    for (size_t Index = 0; Index != OutputSize; ++Index)
      (*BufferOrError)->getBufferStart()[Index] = patternByte(Index);

    Error CommitError = (*BufferOrError)->commit();
    ASSERT_FALSE(CommitError) << toString(std::move(CommitError)).str().str();
  }

  auto ContentsOrError = MemoryBuffer::getFile(OutputPath);
  ASSERT_TRUE(static_cast<bool>(ContentsOrError))
      << ContentsOrError.getError().message();
  StringRef Contents = (*ContentsOrError)->getBuffer();
  ASSERT_EQ(Contents.size(), OutputSize);
  for (size_t Index = 0; Index != OutputSize; ++Index) {
    const unsigned Actual = static_cast<unsigned char>(Contents[Index]);
    ASSERT_EQ(Actual, static_cast<unsigned>(patternByte(Index)))
        << "mismatch at byte " << Index;
  }
}

} // namespace

TEST(SupportFileOutputBufferTest, PreallocationPolicyCommitsExactBytes) {
  writeAndVerifyOutput(FileOutputBuffer::F_preallocate);
}

TEST(SupportFileOutputBufferTest,
     NoMmapPreallocationPolicyUsesMemoryAndCommitsExactBytes) {
  writeAndVerifyOutput(FileOutputBuffer::F_preallocate |
                       FileOutputBuffer::F_no_mmap);
}
