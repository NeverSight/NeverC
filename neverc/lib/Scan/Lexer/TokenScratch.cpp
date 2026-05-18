#include "neverc/Scan/TokenScratch.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstring>
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
using namespace neverc;

namespace {
constexpr unsigned InitialChunkCapacity = 65536;
constexpr unsigned MaxChunkCapacity = 262144;

LLVM_ATTRIBUTE_ALWAYS_INLINE
void fastCopy(char *__restrict Dst, const char *__restrict Src, unsigned Len) {
  if (LLVM_LIKELY(Len <= 64)) {
    std::memcpy(Dst, Src, Len);
    return;
  }
#if defined(__aarch64__) && defined(__ARM_NEON)
  unsigned Off = 0;
  while (Off + 64 <= Len) {
    uint8x16x4_t Block =
        vld1q_u8_x4(reinterpret_cast<const uint8_t *>(Src + Off));
    vst1q_u8_x4(reinterpret_cast<uint8_t *>(Dst + Off), Block);
    Off += 64;
  }
  if (Off < Len)
    std::memcpy(Dst + Off, Src + Off, Len - Off);
#elif defined(__AVX2__)
  unsigned Off = 0;
  while (Off + 32 <= Len) {
    _mm256_storeu_si256((__m256i *)(Dst + Off),
                        _mm256_loadu_si256((const __m256i *)(Src + Off)));
    Off += 32;
  }
  if (Off < Len)
    std::memcpy(Dst + Off, Src + Off, Len - Off);
#elif defined(__SSE2__)
  unsigned Off = 0;
  while (Off + 16 <= Len) {
    _mm_storeu_si128((__m128i *)(Dst + Off),
                     _mm_loadu_si128((const __m128i *)(Src + Off)));
    Off += 16;
  }
  if (Off < Len)
    std::memcpy(Dst + Off, Src + Off, Len - Off);
#else
  std::memcpy(Dst, Src, Len);
#endif
}
} // namespace

TokenScratch::TokenScratch(SourceManager &SM)
    : SrcMgr(SM), CurBuffer(nullptr),
      CurrentChunkCapacity(InitialChunkCapacity) {
  BytesUsed = InitialChunkCapacity;
}

__attribute__((hot)) SourceLocation
TokenScratch::getToken(const char *Buf, unsigned Len, const char *&DestPtr) {
  BytesUsed = llvm::alignTo(BytesUsed, 4u);

  if (LLVM_UNLIKELY(BytesUsed + Len + 2 > CurrentChunkCapacity))
    allocateChunk(Len + 2);
  else {
    SrcMgr.getSLocEntry(SrcMgr.getFileID(BufferStartLoc))
        .getFile()
        .getContentCache()
        .SourceLineCache = SrcMgr::LineOffsetMapping();
  }

  CurBuffer[BytesUsed++] = '\n';
  DestPtr = CurBuffer + BytesUsed;
  fastCopy(CurBuffer + BytesUsed, Buf, Len);
  BytesUsed += Len + 1;
  CurBuffer[BytesUsed - 1] = '\0';

  return BufferStartLoc.getLocWithOffset(BytesUsed - Len - 1);
}

void TokenScratch::allocateChunk(unsigned RequestLen) {
  unsigned Cap = CurrentChunkCapacity;
  unsigned AllocSize = RequestLen > Cap ? llvm::NextPowerOf2(RequestLen) : Cap;
  if (Cap < MaxChunkCapacity)
    CurrentChunkCapacity = std::min(Cap << 1, MaxChunkCapacity);

  auto OwnBuf =
      llvm::WritableMemoryBuffer::getNewMemBuffer(AllocSize, "<scratch space>");
  CurBuffer = OwnBuf->getBufferStart();
  FileID FID = SrcMgr.createFileID(std::move(OwnBuf));
  BufferStartLoc = SrcMgr.getLocForStartOfFile(FID);
  BytesUsed = 0;
}
