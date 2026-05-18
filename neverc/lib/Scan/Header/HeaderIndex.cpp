#include "neverc/Scan/HeaderIndex.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Scan/HeaderIndexTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <optional>

#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

using namespace neverc;

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
unsigned computeHeaderIndexHash(llvm::StringRef Str) {
  unsigned Result = 0;
  const char *Data = Str.data();
  size_t Len = Str.size();
  size_t I = 0;

#if defined(__aarch64__) && defined(__ARM_NEON)
  if (Len >= 16) {
    uint32x4_t Acc = vdupq_n_u32(0);
    const uint8x16_t UpperA = vdupq_n_u8('A');
    const uint8x16_t UpperZ = vdupq_n_u8('Z');
    const uint8x16_t CaseBit = vdupq_n_u8(0x20);
    const uint16x8_t Mul13 = vdupq_n_u16(13);
    for (; I + 16 <= Len; I += 16) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Data + I));
      uint8x16_t IsUp = vandq_u8(vcgeq_u8(V, UpperA), vcleq_u8(V, UpperZ));
      V = veorq_u8(V, vandq_u8(IsUp, CaseBit));
      uint16x8_t Lo = vmovl_u8(vget_low_u8(V));
      uint16x8_t Hi = vmovl_u8(vget_high_u8(V));
      Lo = vmulq_u16(Lo, Mul13);
      Hi = vmulq_u16(Hi, Mul13);
      Acc = vaddq_u32(Acc, vaddl_u16(vget_low_u16(Lo), vget_high_u16(Lo)));
      Acc = vaddq_u32(Acc, vaddl_u16(vget_low_u16(Hi), vget_high_u16(Hi)));
    }
    Result = vaddvq_u32(Acc);
  }
#elif defined(__SSE2__)
  if (Len >= 16) {
    __m128i AccLo = _mm_setzero_si128();
    __m128i AccHi = _mm_setzero_si128();
    const __m128i UpperA = _mm_set1_epi8('A' - 1);
    const __m128i UpperZp1 = _mm_set1_epi8('Z' + 1);
    const __m128i CaseBit = _mm_set1_epi8(0x20);
    const __m128i Mul13Lo = _mm_set1_epi16(13);
    for (; I + 16 <= Len; I += 16) {
      __m128i V = _mm_loadu_si128((const __m128i *)(Data + I));
      __m128i IsUp =
          _mm_and_si128(_mm_cmpgt_epi8(V, UpperA), _mm_cmpgt_epi8(UpperZp1, V));
      V = _mm_xor_si128(V, _mm_and_si128(IsUp, CaseBit));
      __m128i Zero = _mm_setzero_si128();
      __m128i Lo16 = _mm_unpacklo_epi8(V, Zero);
      __m128i Hi16 = _mm_unpackhi_epi8(V, Zero);
      Lo16 = _mm_mullo_epi16(Lo16, Mul13Lo);
      Hi16 = _mm_mullo_epi16(Hi16, Mul13Lo);
      AccLo = _mm_add_epi32(AccLo, _mm_unpacklo_epi16(Lo16, Zero));
      AccLo = _mm_add_epi32(AccLo, _mm_unpackhi_epi16(Lo16, Zero));
      AccHi = _mm_add_epi32(AccHi, _mm_unpacklo_epi16(Hi16, Zero));
      AccHi = _mm_add_epi32(AccHi, _mm_unpackhi_epi16(Hi16, Zero));
    }
    __m128i Sum = _mm_add_epi32(AccLo, AccHi);
    Sum = _mm_add_epi32(Sum, _mm_srli_si128(Sum, 8));
    Sum = _mm_add_epi32(Sum, _mm_srli_si128(Sum, 4));
    Result = static_cast<unsigned>(_mm_cvtsi128_si32(Sum));
  }
#endif

  for (; I < Len; ++I)
    Result += toLowercase(Data[I]) * 13;
  return Result;
}
} // namespace

// ===----------------------------------------------------------------------===
// Verification & construction
// ===----------------------------------------------------------------------===

std::unique_ptr<HeaderIndex> HeaderIndex::Create(FileEntryRef FE,
                                                 FileManager &FM) {
  // If the file is too small to be a header map, ignore it.
  unsigned FileSize = FE.getSize();
  if (FileSize <= sizeof(HMapHeader))
    return nullptr;

  auto FileBuffer = FM.getBufferForFile(FE);
  if (!FileBuffer || !*FileBuffer)
    return nullptr;
  bool NeedsByteSwap;
  if (!checkHeader(**FileBuffer, NeedsByteSwap))
    return nullptr;
  return std::unique_ptr<HeaderIndex>(
      new HeaderIndex(std::move(*FileBuffer), NeedsByteSwap));
}

bool HeaderIndexImpl::checkHeader(const llvm::MemoryBuffer &File,
                                  bool &NeedsByteSwap) {
  if (File.getBufferSize() <= sizeof(HMapHeader))
    return false;
  const char *FileStart = File.getBufferStart();

  // We know the file is at least as big as the header, check it now.
  const HMapHeader *Header = reinterpret_cast<const HMapHeader *>(FileStart);

  // Sniff it to see if it's a headermap by checking the magic number and
  // version.
  if (Header->Magic == HMAP_HeaderMagicNumber &&
      Header->Version == HMAP_HeaderVersion)
    NeedsByteSwap = false;
  else if (Header->Magic == llvm::byteswap<uint32_t>(HMAP_HeaderMagicNumber) &&
           Header->Version == llvm::byteswap<uint16_t>(HMAP_HeaderVersion))
    NeedsByteSwap = true; // Mixed endianness headermap.
  else
    return false; // Not a header map.

  if (Header->Reserved != 0)
    return false;

  // Check the number of buckets.  It should be a power of two, and there
  // should be enough space in the file for all of them.
  uint32_t NumBuckets =
      NeedsByteSwap ? llvm::byteswap(Header->NumBuckets) : Header->NumBuckets;
  if (!llvm::isPowerOf2_32(NumBuckets))
    return false;
  if (File.getBufferSize() <
      sizeof(HMapHeader) + sizeof(HMapBucket) * NumBuckets)
    return false;

  // Okay, everything looks good.
  return true;
}

// ===----------------------------------------------------------------------===
// Utility methods
// ===----------------------------------------------------------------------===

llvm::StringRef HeaderIndexImpl::getFileName() const {
  return FileBuffer->getBufferIdentifier();
}

unsigned HeaderIndexImpl::getEndianAdjustedWord(unsigned X) const {
  if (!NeedsBSwap)
    return X;
  return llvm::byteswap<uint32_t>(X);
}

const HMapHeader &HeaderIndexImpl::getHeader() const {
  // We know the file is at least as big as the header.  Return it.
  return *reinterpret_cast<const HMapHeader *>(FileBuffer->getBufferStart());
}

HMapBucket HeaderIndexImpl::getBucket(unsigned BucketNo) const {
  assert(FileBuffer->getBufferSize() >=
             sizeof(HMapHeader) + sizeof(HMapBucket) * BucketNo &&
         "Expected bucket to be in range");

  HMapBucket Result;
  Result.Key = HMAP_EmptyBucketKey;

  const HMapBucket *BucketArray = reinterpret_cast<const HMapBucket *>(
      FileBuffer->getBufferStart() + sizeof(HMapHeader));
  const HMapBucket *BucketPtr = BucketArray + BucketNo;

  // Load the values, bswapping as needed.
  Result.Key = getEndianAdjustedWord(BucketPtr->Key);
  Result.Prefix = getEndianAdjustedWord(BucketPtr->Prefix);
  Result.Suffix = getEndianAdjustedWord(BucketPtr->Suffix);
  return Result;
}

std::optional<llvm::StringRef>
HeaderIndexImpl::getString(unsigned StrTabIdx) const {
  // Add the start of the string table to the idx.
  StrTabIdx += getEndianAdjustedWord(getHeader().StringsOffset);

  // Check for invalid index.
  if (StrTabIdx >= FileBuffer->getBufferSize())
    return std::nullopt;

  const char *Data = FileBuffer->getBufferStart() + StrTabIdx;
  unsigned MaxLen = FileBuffer->getBufferSize() - StrTabIdx;
  unsigned Len = strnlen(Data, MaxLen);

  // Check whether the buffer is null-terminated.
  if (Len == MaxLen && Data[Len - 1])
    return std::nullopt;

  return llvm::StringRef(Data, Len);
}

// ===----------------------------------------------------------------------===
// Main drivers
// ===----------------------------------------------------------------------===

LLVM_DUMP_METHOD void HeaderIndexImpl::dump() const {
  const HMapHeader &Hdr = getHeader();
  unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);

  llvm::dbgs() << "Header Map " << getFileName() << ":\n  " << NumBuckets
               << ", " << getEndianAdjustedWord(Hdr.NumEntries) << "\n";

  auto getStringOrInvalid = [this](unsigned Id) -> llvm::StringRef {
    if (std::optional<llvm::StringRef> S = getString(Id))
      return *S;
    return "<invalid>";
  };

  for (unsigned i = 0; i != NumBuckets; ++i) {
    HMapBucket B = getBucket(i);
    if (B.Key == HMAP_EmptyBucketKey)
      continue;

    llvm::StringRef Key = getStringOrInvalid(B.Key);
    llvm::StringRef Prefix = getStringOrInvalid(B.Prefix);
    llvm::StringRef Suffix = getStringOrInvalid(B.Suffix);
    llvm::dbgs() << "  " << i << ". " << Key << " -> '" << Prefix << "' '"
                 << Suffix << "'\n";
  }
}

llvm::StringRef
HeaderIndexImpl::lookupFilename(llvm::StringRef Filename,
                                llvm::SmallVectorImpl<char> &DestPath) const {
  const HMapHeader &Hdr = getHeader();
  unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);

  // Don't probe infinitely.  This should be checked before constructing.
  assert(llvm::isPowerOf2_32(NumBuckets) && "Expected power of 2");

  // Linearly probe the hash table.
  for (unsigned Bucket = computeHeaderIndexHash(Filename);; ++Bucket) {
    HMapBucket B = getBucket(Bucket & (NumBuckets - 1));
    if (B.Key == HMAP_EmptyBucketKey)
      return llvm::StringRef(); // Hash miss.

    // See if the key matches.  If not, probe on.
    std::optional<llvm::StringRef> Key = getString(B.Key);
    if (LLVM_UNLIKELY(!Key))
      continue;
    if (!Filename.equals_insensitive(*Key))
      continue;

    // If so, we have a match in the hash table.  Construct the destination
    // path.
    std::optional<llvm::StringRef> Prefix = getString(B.Prefix);
    std::optional<llvm::StringRef> Suffix = getString(B.Suffix);

    DestPath.clear();
    if (LLVM_LIKELY(Prefix && Suffix)) {
      DestPath.append(Prefix->begin(), Prefix->end());
      DestPath.append(Suffix->begin(), Suffix->end());
    }
    return llvm::StringRef(DestPath.begin(), DestPath.size());
  }
}

llvm::StringRef
HeaderIndexImpl::reverseLookupFilename(llvm::StringRef DestPath) const {
  if (!ReverseMap.empty())
    return ReverseMap.lookup(DestPath);

  const HMapHeader &Hdr = getHeader();
  unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);
  llvm::StringRef RetKey;
  for (unsigned i = 0; i != NumBuckets; ++i) {
    HMapBucket B = getBucket(i);
    if (B.Key == HMAP_EmptyBucketKey)
      continue;

    std::optional<llvm::StringRef> Key = getString(B.Key);
    std::optional<llvm::StringRef> Prefix = getString(B.Prefix);
    std::optional<llvm::StringRef> Suffix = getString(B.Suffix);
    if (LLVM_LIKELY(Key && Prefix && Suffix)) {
      llvm::SmallVector<char, 1024> Buf;
      Buf.append(Prefix->begin(), Prefix->end());
      Buf.append(Suffix->begin(), Suffix->end());
      llvm::StringRef Value(Buf.begin(), Buf.size());
      ReverseMap[Value] = *Key;

      if (DestPath == Value)
        RetKey = *Key;
    }
  }
  return RetKey;
}
