#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

#ifdef __AVX512BW__
#include <immintrin.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// ===----------------------------------------------------------------------===
// SIMD scan infrastructure (templates & NEON helpers)
// ===----------------------------------------------------------------------===

#if defined(__aarch64__) && defined(__ARM_NEON)
namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
inline unsigned neonFirstSetByte(uint8x16_t Mask) {
  uint64x2_t AsU64 = vreinterpretq_u64_u8(Mask);
  uint64_t Lo = vgetq_lane_u64(AsU64, 0);
  uint64_t Hi = vgetq_lane_u64(AsU64, 1);
  if (Lo)
    return __builtin_ctzll(Lo) >> 3;
  return 8 + (__builtin_ctzll(Hi) >> 3);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline bool neonAnySet(uint8x16_t V) { return vmaxvq_u8(V) != 0; }

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline unsigned neonFirstSetByte32(uint8x16_t MaskLo, uint8x16_t MaskHi) {
  uint64x2_t Lo64 = vreinterpretq_u64_u8(MaskLo);
  uint64_t L0 = vgetq_lane_u64(Lo64, 0);
  uint64_t L1 = vgetq_lane_u64(Lo64, 1);
  if (L0)
    return __builtin_ctzll(L0) >> 3;
  if (L1)
    return 8 + (__builtin_ctzll(L1) >> 3);
  uint64x2_t Hi64 = vreinterpretq_u64_u8(MaskHi);
  uint64_t H0 = vgetq_lane_u64(Hi64, 0);
  uint64_t H1 = vgetq_lane_u64(Hi64, 1);
  if (H0)
    return 16 + (__builtin_ctzll(H0) >> 3);
  return 24 + (__builtin_ctzll(H1) >> 3);
}
} // namespace

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
inline uint8x16_t neonTblClassify(uint8x16_t V, uint8x16_t LoLUT,
                                  uint8x16_t HiLUT) {
  uint8x16_t LoNib = vandq_u8(V, vdupq_n_u8(0x0F));
  uint8x16_t HiNib = vshrq_n_u8(V, 4);
  return vandq_u8(vqtbl1q_u8(LoLUT, LoNib), vqtbl1q_u8(HiLUT, HiNib));
}
} // namespace

template <typename ClassifierNeon>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScanNeonQuad(const char *Ptr, const char *End, ClassifierNeon Classify) {
  while (LLVM_LIKELY(End - Ptr >= 64)) {
    uint8x16_t V0 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr));
    uint8x16_t V1 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr + 16));
    uint8x16_t V2 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr + 32));
    uint8x16_t V3 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr + 48));
    uint8x16_t S0 = Classify(V0);
    uint8x16_t S1 = Classify(V1);
    uint8x16_t S2 = Classify(V2);
    uint8x16_t S3 = Classify(V3);
    uint8x16_t Any01 = vorrq_u8(S0, S1);
    uint8x16_t Any23 = vorrq_u8(S2, S3);
    if (neonAnySet(vorrq_u8(Any01, Any23))) {
      if (neonAnySet(Any01)) {
        if (neonAnySet(S0))
          return Ptr + neonFirstSetByte(S0);
        return Ptr + 16 + neonFirstSetByte(S1);
      }
      if (neonAnySet(S2))
        return Ptr + 32 + neonFirstSetByte(S2);
      return Ptr + 48 + neonFirstSetByte(S3);
    }
    Ptr += 64;
  }
  return Ptr;
}
#endif

#ifdef __AVX512BW__

template <typename Classifier512>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan512(const char *Ptr, const char *End, Classifier512 Classify) {
  while (LLVM_LIKELY(End - Ptr >= 64)) {
    __m512i V = _mm512_loadu_si512(Ptr);
    __mmask64 Stops = Classify(V);
    if (Stops != 0)
      return Ptr + _tzcnt_u64(Stops);
    Ptr += 64;
  }
  return Ptr;
}

template <typename Classifier512>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan512x2(const char *Ptr, const char *End, Classifier512 Classify) {
  while (LLVM_LIKELY(End - Ptr >= 128)) {
    __m512i V0 = _mm512_loadu_si512(Ptr);
    __m512i V1 = _mm512_loadu_si512(Ptr + 64);
    __mmask64 S0 = Classify(V0);
    __mmask64 S1 = Classify(V1);
    if (LLVM_UNLIKELY(S0 != 0))
      return Ptr + _tzcnt_u64(S0);
    if (LLVM_UNLIKELY(S1 != 0))
      return Ptr + 64 + _tzcnt_u64(S1);
    Ptr += 128;
  }
  return simdScan512(Ptr, End, Classify);
}
#endif

#ifdef __AVX2__
template <typename Classifier256>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan256(const char *Ptr, const char *End, Classifier256 Classify) {
  while (LLVM_LIKELY(End - Ptr >= 32)) {
    __m256i V = _mm256_loadu_si256((const __m256i *)Ptr);
    unsigned Stops = Classify(V);
    if (Stops != 0)
      return Ptr + llvm::countr_zero(Stops);
    Ptr += 32;
  }
  return Ptr;
}

template <typename Classifier256>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan256x2(const char *Ptr, const char *End, Classifier256 Classify) {
  while (LLVM_LIKELY(End - Ptr >= 64)) {
    __m256i V0 = _mm256_loadu_si256((const __m256i *)Ptr);
    __m256i V1 = _mm256_loadu_si256((const __m256i *)(Ptr + 32));
    unsigned S0 = Classify(V0);
    unsigned S1 = Classify(V1);
    if (LLVM_UNLIKELY(S0 != 0))
      return Ptr + llvm::countr_zero(S0);
    if (LLVM_UNLIKELY(S1 != 0))
      return Ptr + 32 + llvm::countr_zero(S1);
    Ptr += 64;
  }
  return simdScan256(Ptr, End, Classify);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
__m256i vpshufbClassify256(__m256i V, __m256i LoLUT, __m256i HiLUT) {
  __m256i NibMask = _mm256_set1_epi8(0x0F);
  __m256i LoNib = _mm256_and_si256(V, NibMask);
  __m256i HiNib = _mm256_and_si256(_mm256_srli_epi16(V, 4), NibMask);
  return _mm256_and_si256(_mm256_shuffle_epi8(LoLUT, LoNib),
                          _mm256_shuffle_epi8(HiLUT, HiNib));
}
} // namespace
#endif

#ifdef __SSE2__
template <typename Classifier128>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan128(const char *Ptr, const char *End, Classifier128 Classify) {
  while (LLVM_LIKELY(End - Ptr >= 16)) {
    __m128i V = _mm_loadu_si128((const __m128i *)Ptr);
    unsigned Stops = Classify(V);
    if (Stops != 0)
      return Ptr + llvm::countr_zero(Stops);
    Ptr += 16;
  }
  return Ptr;
}
#endif

#ifdef __SSSE3__
#include <tmmintrin.h>
namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
__m128i pshufbClassify128(__m128i V, __m128i LoLUT, __m128i HiLUT) {
  __m128i NibMask = _mm_set1_epi8(0x0F);
  __m128i LoNib = _mm_and_si128(V, NibMask);
  __m128i HiNib = _mm_and_si128(_mm_srli_epi16(V, 4), NibMask);
  return _mm_and_si128(_mm_shuffle_epi8(LoLUT, LoNib),
                       _mm_shuffle_epi8(HiLUT, HiNib));
}
} // namespace
#endif

#if defined(__aarch64__) && defined(__ARM_NEON)
template <typename ClassifierNeon>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScanNeon(const char *Ptr, const char *End, ClassifierNeon Classify) {
  while (LLVM_LIKELY(End - Ptr >= 32)) {
    uint8x16_t V0 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr));
    uint8x16_t V1 = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr + 16));
    uint8x16_t S0 = Classify(V0);
    uint8x16_t S1 = Classify(V1);
    if (neonAnySet(vorrq_u8(S0, S1)))
      return Ptr + neonFirstSetByte32(S0, S1);
    Ptr += 32;
  }
  if (End - Ptr >= 16) {
    uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Ptr));
    uint8x16_t StopV = Classify(V);
    if (neonAnySet(StopV))
      return Ptr + neonFirstSetByte(StopV);
    Ptr += 16;
  }
  return Ptr;
}
#endif

using namespace neverc;

// ===----------------------------------------------------------------------===
// SIMD-accelerated scan tables
// ===----------------------------------------------------------------------===

namespace {
constexpr uint8_t LitTermTable[256] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, // \0 \n \r
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, // " '
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, // > ?
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, // backslash
};

constexpr uint8_t PPNumberCharTable[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
};

constexpr uint8_t IdentCharTable[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
};
LLVM_ATTRIBUTE_ALWAYS_INLINE
const char *fastParseASCIIIdentifier(const char *CurPtr,
                                     [[maybe_unused]] const char *BufferEnd) {
#ifdef __AVX512BW__
  CurPtr = simdScan512(CurPtr, BufferEnd, [](__m512i V) -> __mmask64 {
    const __m512i Bias = _mm512_set1_epi8((char)0x80u);
    __m512i B = _mm512_xor_si512(V, Bias);
    __mmask64 IsUpper = _mm512_cmpgt_epi8_mask(
                            B, _mm512_set1_epi8((char)(('@' ^ 0x80u) & 0xFF))) &
                        _mm512_cmpgt_epi8_mask(
                            _mm512_set1_epi8((char)(('[' ^ 0x80u) & 0xFF)), B);
    __mmask64 IsLower = _mm512_cmpgt_epi8_mask(
                            B, _mm512_set1_epi8((char)(('`' ^ 0x80u) & 0xFF))) &
                        _mm512_cmpgt_epi8_mask(
                            _mm512_set1_epi8((char)(('{' ^ 0x80u) & 0xFF)), B);
    __mmask64 IsDigit = _mm512_cmpgt_epi8_mask(
                            B, _mm512_set1_epi8((char)(('/' ^ 0x80u) & 0xFF))) &
                        _mm512_cmpgt_epi8_mask(
                            _mm512_set1_epi8((char)((':' ^ 0x80u) & 0xFF)), B);
    __mmask64 IsUnder = _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('_'));
    return ~(IsUpper | IsLower | IsDigit | IsUnder);
  });
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  CurPtr = simdScan256(CurPtr, BufferEnd, [](__m256i V) -> unsigned {
    const __m256i Bias = _mm256_set1_epi8((char)0x80u);
    __m256i B = _mm256_xor_si256(V, Bias);
    __m256i IsIdent = _mm256_or_si256(
        _mm256_or_si256(
            _mm256_cmpeq_epi8(V, _mm256_set1_epi8('_')),
            _mm256_and_si256(
                _mm256_cmpgt_epi8(
                    B, _mm256_set1_epi8((char)(('@' ^ 0x80u) & 0xFF))),
                _mm256_cmpgt_epi8(
                    _mm256_set1_epi8((char)(('[' ^ 0x80u) & 0xFF)), B))),
        _mm256_or_si256(
            _mm256_and_si256(
                _mm256_cmpgt_epi8(
                    B, _mm256_set1_epi8((char)(('`' ^ 0x80u) & 0xFF))),
                _mm256_cmpgt_epi8(
                    _mm256_set1_epi8((char)(('{' ^ 0x80u) & 0xFF)), B)),
            _mm256_and_si256(
                _mm256_cmpgt_epi8(
                    B, _mm256_set1_epi8((char)(('/' ^ 0x80u) & 0xFF))),
                _mm256_cmpgt_epi8(
                    _mm256_set1_epi8((char)((':' ^ 0x80u) & 0xFF)), B))));
    return ~static_cast<unsigned>(_mm256_movemask_epi8(IsIdent));
  });
#elif defined(__SSE4_2__)
  alignas(16) static constexpr char IdentRanges[16] = {
      '_', '_', 'A', 'Z', 'a', 'z', '0', '9',
  };
  __m128i RangeV = _mm_load_si128((const __m128i *)IdentRanges);
  CurPtr = simdScan128(CurPtr, BufferEnd, [RangeV](__m128i V) -> unsigned {
    int Idx = _mm_cmpistri(RangeV, V,
                           _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES |
                               _SIDD_UBYTE_OPS | _SIDD_NEGATIVE_POLARITY);
    return (Idx < 16) ? (1u << Idx) : 0u;
  });
#elif defined(__SSE2__)
  CurPtr = simdScan128(CurPtr, BufferEnd, [](__m128i V) -> unsigned {
    const __m128i Bias = _mm_set1_epi8((char)0x80u);
    __m128i B = _mm_xor_si128(V, Bias);
    __m128i IsIdent = _mm_or_si128(
        _mm_or_si128(
            _mm_cmpeq_epi8(V, _mm_set1_epi8('_')),
            _mm_and_si128(
                _mm_cmpgt_epi8(B, _mm_set1_epi8((char)(('@' ^ 0x80u) & 0xFF))),
                _mm_cmpgt_epi8(_mm_set1_epi8((char)(('[' ^ 0x80u) & 0xFF)),
                               B))),
        _mm_or_si128(
            _mm_and_si128(
                _mm_cmpgt_epi8(B, _mm_set1_epi8((char)(('`' ^ 0x80u) & 0xFF))),
                _mm_cmpgt_epi8(_mm_set1_epi8((char)(('{' ^ 0x80u) & 0xFF)), B)),
            _mm_and_si128(
                _mm_cmpgt_epi8(B, _mm_set1_epi8((char)(('/' ^ 0x80u) & 0xFF))),
                _mm_cmpgt_epi8(_mm_set1_epi8((char)((':' ^ 0x80u) & 0xFF)),
                               B))));
    return static_cast<unsigned>(~_mm_movemask_epi8(IsIdent)) & 0xFFFFu;
  });
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    alignas(16) static constexpr uint8_t IdLoData[16] = {
        0x15, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1E, 0x0A, 0x0A, 0x0A, 0x0A, 0x0E};
    alignas(16) static constexpr uint8_t IdHiData[16] = {
        0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8x16_t LoLUT = vld1q_u8(IdLoData);
    uint8x16_t HiLUT = vld1q_u8(IdHiData);
    if (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Hit = neonTblClassify(V, LoLUT, HiLUT);
      uint8x16_t IsNonIdent = vceqq_u8(Hit, vdupq_n_u8(0));
      if (neonAnySet(IsNonIdent))
        return CurPtr + neonFirstSetByte(IsNonIdent);
      CurPtr += 16;
    }
    CurPtr = simdScanNeonQuad(
        CurPtr, BufferEnd, [LoLUT, HiLUT](uint8x16_t V) -> uint8x16_t {
          uint8x16_t Hit = neonTblClassify(V, LoLUT, HiLUT);
          return vceqq_u8(Hit, vdupq_n_u8(0));
        });
    while (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Hit = neonTblClassify(V, LoLUT, HiLUT);
      uint8x16_t IsNonIdent = vceqq_u8(Hit, vdupq_n_u8(0));
      if (neonAnySet(IsNonIdent))
        return CurPtr + neonFirstSetByte(IsNonIdent);
      CurPtr += 16;
    }
  }
#else
  {
    constexpr uint64_t Broadcast = 0x0101010101010101ULL;
    constexpr uint64_t HiBitMask = 0x8080808080808080ULL;
    auto inRange = [Broadcast, HiBitMask](uint64_t Word, char Lo,
                                          char Hi) -> uint64_t {
      uint64_t A = Word + Broadcast * (127 - Hi);
      uint64_t B = Word + Broadcast * (127 - Lo + 1);
      return (A ^ B) & HiBitMask;
    };
    while (CurPtr + 8 <= BufferEnd) {
      uint64_t Word;
      std::memcpy(&Word, CurPtr, 8);
      uint64_t Upper = inRange(Word, 'A', 'Z');
      uint64_t Lower = inRange(Word, 'a', 'z');
      uint64_t Digit = inRange(Word, '0', '9');
      auto hasZeroByte = [](uint64_t V) -> uint64_t {
        return (V - 0x0101010101010101ULL) & ~V & 0x8080808080808080ULL;
      };
      uint64_t Under = hasZeroByte(Word ^ (Broadcast * '_'));
      uint64_t IsIdent = Upper | Lower | Digit | Under;
      if (IsIdent != HiBitMask) {
        uint64_t NotIdent = ~IsIdent & HiBitMask;
        return CurPtr + (llvm::countr_zero(NotIdent) >> 3);
      }
      CurPtr += 8;
    }
  }
#endif

  while (IdentCharTable[static_cast<unsigned char>(*CurPtr)])
    ++CurPtr;
  return CurPtr;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
const char *fastScanPPNumberBody(const char *CurPtr,
                                 [[maybe_unused]] const char *BufferEnd) {
#ifdef __AVX512BW__
  CurPtr = simdScan512(CurPtr, BufferEnd, [](__m512i V) -> __mmask64 {
    const __m512i Bias = _mm512_set1_epi8((char)0x80u);
    __m512i B = _mm512_xor_si512(V, Bias);
    __mmask64 IsAlpha =
        (_mm512_cmpgt_epi8_mask(
             B, _mm512_set1_epi8((char)(('@' ^ 0x80u) & 0xFF))) &
         _mm512_cmpgt_epi8_mask(_mm512_set1_epi8((char)(('[' ^ 0x80u) & 0xFF)),
                                B)) |
        (_mm512_cmpgt_epi8_mask(
             B, _mm512_set1_epi8((char)(('`' ^ 0x80u) & 0xFF))) &
         _mm512_cmpgt_epi8_mask(_mm512_set1_epi8((char)(('{' ^ 0x80u) & 0xFF)),
                                B));
    __mmask64 IsDigit = _mm512_cmpgt_epi8_mask(
                            B, _mm512_set1_epi8((char)(('/' ^ 0x80u) & 0xFF))) &
                        _mm512_cmpgt_epi8_mask(
                            _mm512_set1_epi8((char)((':' ^ 0x80u) & 0xFF)), B);
    __mmask64 IsExtra = _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('_')) |
                        _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('.'));
    return ~(IsAlpha | IsDigit | IsExtra);
  });
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  CurPtr = simdScan256(CurPtr, BufferEnd, [](__m256i V) -> unsigned {
    const __m256i Bias = _mm256_set1_epi8((char)0x80u);
    __m256i B = _mm256_xor_si256(V, Bias);
    __m256i IsPPN = _mm256_or_si256(
        _mm256_or_si256(_mm256_cmpeq_epi8(V, _mm256_set1_epi8('_')),
                        _mm256_cmpeq_epi8(V, _mm256_set1_epi8('.'))),
        _mm256_or_si256(
            _mm256_or_si256(
                _mm256_and_si256(
                    _mm256_cmpgt_epi8(
                        B, _mm256_set1_epi8((char)(('@' ^ 0x80u) & 0xFF))),
                    _mm256_cmpgt_epi8(
                        _mm256_set1_epi8((char)(('[' ^ 0x80u) & 0xFF)), B)),
                _mm256_and_si256(
                    _mm256_cmpgt_epi8(
                        B, _mm256_set1_epi8((char)(('`' ^ 0x80u) & 0xFF))),
                    _mm256_cmpgt_epi8(
                        _mm256_set1_epi8((char)(('{' ^ 0x80u) & 0xFF)), B))),
            _mm256_and_si256(
                _mm256_cmpgt_epi8(
                    B, _mm256_set1_epi8((char)(('/' ^ 0x80u) & 0xFF))),
                _mm256_cmpgt_epi8(
                    _mm256_set1_epi8((char)((':' ^ 0x80u) & 0xFF)), B))));
    return ~static_cast<unsigned>(_mm256_movemask_epi8(IsPPN));
  });
#elif defined(__SSE4_2__)
  alignas(16) static constexpr char PPNumRanges[16] = {
      '.', '.', '0', '9', 'A', 'Z', '_', '_', 'a', 'z',
  };
  __m128i RangeV = _mm_load_si128((const __m128i *)PPNumRanges);
  CurPtr = simdScan128(CurPtr, BufferEnd, [RangeV](__m128i V) -> unsigned {
    int Idx = _mm_cmpistri(RangeV, V,
                           _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES |
                               _SIDD_UBYTE_OPS | _SIDD_NEGATIVE_POLARITY);
    return (Idx < 16) ? (1u << Idx) : 0u;
  });
#elif defined(__SSE2__)
  CurPtr = simdScan128(CurPtr, BufferEnd, [](__m128i V) -> unsigned {
    const __m128i Bias = _mm_set1_epi8((char)0x80u);
    __m128i B = _mm_xor_si128(V, Bias);
    __m128i IsPPN = _mm_or_si128(
        _mm_or_si128(_mm_cmpeq_epi8(V, _mm_set1_epi8('_')),
                     _mm_cmpeq_epi8(V, _mm_set1_epi8('.'))),
        _mm_or_si128(
            _mm_or_si128(
                _mm_and_si128(
                    _mm_cmpgt_epi8(B,
                                   _mm_set1_epi8((char)(('@' ^ 0x80u) & 0xFF))),
                    _mm_cmpgt_epi8(_mm_set1_epi8((char)(('[' ^ 0x80u) & 0xFF)),
                                   B)),
                _mm_and_si128(
                    _mm_cmpgt_epi8(B,
                                   _mm_set1_epi8((char)(('`' ^ 0x80u) & 0xFF))),
                    _mm_cmpgt_epi8(_mm_set1_epi8((char)(('{' ^ 0x80u) & 0xFF)),
                                   B))),
            _mm_and_si128(
                _mm_cmpgt_epi8(B, _mm_set1_epi8((char)(('/' ^ 0x80u) & 0xFF))),
                _mm_cmpgt_epi8(_mm_set1_epi8((char)((':' ^ 0x80u) & 0xFF)),
                               B))));
    return static_cast<unsigned>(~_mm_movemask_epi8(IsPPN)) & 0xFFFFu;
  });
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    alignas(16) static constexpr uint8_t PPNLoData[16] = {
        0x15, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
        0x1F, 0x1F, 0x1E, 0x0A, 0x0A, 0x0A, 0x2A, 0x0E};
    alignas(16) static constexpr uint8_t PPNHiData[16] = {
        0x00, 0x00, 0x20, 0x01, 0x02, 0x04, 0x08, 0x10,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8x16_t LoLUT = vld1q_u8(PPNLoData);
    uint8x16_t HiLUT = vld1q_u8(PPNHiData);
    if (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Hit = neonTblClassify(V, LoLUT, HiLUT);
      uint8x16_t IsNonPPN = vceqq_u8(Hit, vdupq_n_u8(0));
      if (neonAnySet(IsNonPPN))
        return CurPtr + neonFirstSetByte(IsNonPPN);
      CurPtr += 16;
    }
    CurPtr = simdScanNeonQuad(
        CurPtr, BufferEnd, [LoLUT, HiLUT](uint8x16_t V) -> uint8x16_t {
          uint8x16_t Hit = neonTblClassify(V, LoLUT, HiLUT);
          return vceqq_u8(Hit, vdupq_n_u8(0));
        });
    while (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Hit = neonTblClassify(V, LoLUT, HiLUT);
      uint8x16_t IsNonPPN = vceqq_u8(Hit, vdupq_n_u8(0));
      if (neonAnySet(IsNonPPN))
        return CurPtr + neonFirstSetByte(IsNonPPN);
      CurPtr += 16;
    }
  }
#else
  {
    constexpr uint64_t Broadcast = 0x0101010101010101ULL;
    constexpr uint64_t HiBitMask = 0x8080808080808080ULL;
    auto inRange = [Broadcast, HiBitMask](uint64_t Word, char Lo,
                                          char Hi) -> uint64_t {
      uint64_t A = Word + Broadcast * (127 - Hi);
      uint64_t B = Word + Broadcast * (127 - Lo + 1);
      return (A ^ B) & HiBitMask;
    };
    auto hasZeroByte = [](uint64_t V) -> uint64_t {
      return (V - 0x0101010101010101ULL) & ~V & 0x8080808080808080ULL;
    };
    while (CurPtr + 16 <= BufferEnd) {
      uint64_t W0, W1;
      std::memcpy(&W0, CurPtr, 8);
      std::memcpy(&W1, CurPtr + 8, 8);
      uint64_t IsPPN0 = inRange(W0, 'A', 'Z') | inRange(W0, 'a', 'z') |
                        inRange(W0, '0', '9') |
                        hasZeroByte(W0 ^ (Broadcast * '_')) |
                        hasZeroByte(W0 ^ (Broadcast * '.'));
      if (IsPPN0 != HiBitMask)
        return CurPtr + (llvm::countr_zero(~IsPPN0 & HiBitMask) >> 3);
      uint64_t IsPPN1 = inRange(W1, 'A', 'Z') | inRange(W1, 'a', 'z') |
                        inRange(W1, '0', '9') |
                        hasZeroByte(W1 ^ (Broadcast * '_')) |
                        hasZeroByte(W1 ^ (Broadcast * '.'));
      if (IsPPN1 != HiBitMask)
        return CurPtr + 8 + (llvm::countr_zero(~IsPPN1 & HiBitMask) >> 3);
      CurPtr += 16;
    }
    if (CurPtr + 8 <= BufferEnd) {
      uint64_t Word;
      std::memcpy(&Word, CurPtr, 8);
      uint64_t IsPPN = inRange(Word, 'A', 'Z') | inRange(Word, 'a', 'z') |
                       inRange(Word, '0', '9') |
                       hasZeroByte(Word ^ (Broadcast * '_')) |
                       hasZeroByte(Word ^ (Broadcast * '.'));
      if (IsPPN != HiBitMask)
        return CurPtr + (llvm::countr_zero(~IsPPN & HiBitMask) >> 3);
      CurPtr += 8;
    }
  }
#endif
  while (PPNumberCharTable[static_cast<unsigned char>(*CurPtr)])
    ++CurPtr;
  return CurPtr;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
const char *fastScanLiteralBody(const char *CurPtr,
                                [[maybe_unused]] const char *BufferEnd,
                                [[maybe_unused]] char Quote) {
#ifdef __AVX512BW__
  {
    const __m512i VQ = _mm512_set1_epi8(Quote);
    const __m512i VBS = _mm512_set1_epi8('\\');
    const __m512i VQM = _mm512_set1_epi8('?');
    const __m512i VNL = _mm512_set1_epi8('\n');
    const __m512i VCR = _mm512_set1_epi8('\r');
    const __m512i VNul = _mm512_setzero_si512();
    CurPtr = simdScan512(CurPtr, BufferEnd, [&](__m512i V) -> __mmask64 {
      return _mm512_cmpeq_epi8_mask(V, VQ) | _mm512_cmpeq_epi8_mask(V, VBS) |
             _mm512_cmpeq_epi8_mask(V, VQM) | _mm512_cmpeq_epi8_mask(V, VNL) |
             _mm512_cmpeq_epi8_mask(V, VCR) | _mm512_cmpeq_epi8_mask(V, VNul);
    });
  }
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  {
    const __m256i VQ = _mm256_set1_epi8(Quote);
    const __m256i VBS = _mm256_set1_epi8('\\');
    const __m256i VQM = _mm256_set1_epi8('?');
    const __m256i VNL = _mm256_set1_epi8('\n');
    const __m256i VCR = _mm256_set1_epi8('\r');
    const __m256i VNul = _mm256_setzero_si256();
    CurPtr = simdScan256(CurPtr, BufferEnd, [&](__m256i V) -> unsigned {
      __m256i Hit = _mm256_or_si256(
          _mm256_or_si256(_mm256_cmpeq_epi8(V, VQ), _mm256_cmpeq_epi8(V, VBS)),
          _mm256_or_si256(
              _mm256_cmpeq_epi8(V, VQM),
              _mm256_or_si256(_mm256_cmpeq_epi8(V, VNL),
                              _mm256_or_si256(_mm256_cmpeq_epi8(V, VCR),
                                              _mm256_cmpeq_epi8(V, VNul)))));
      return static_cast<unsigned>(_mm256_movemask_epi8(Hit));
    });
  }
#elif defined(__SSE4_2__)
  {
    alignas(16) char StopSet[16] = {};
    StopSet[0] = Quote;
    StopSet[1] = '\\';
    StopSet[2] = '?';
    StopSet[3] = '\n';
    StopSet[4] = '\r';
    __m128i StopV = _mm_load_si128((const __m128i *)StopSet);
    CurPtr = simdScan128(CurPtr, BufferEnd, [StopV](__m128i V) -> unsigned {
      int Idx = _mm_cmpestri(StopV, 6, V, 16,
                             _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_EQUAL_ANY |
                                 _SIDD_UBYTE_OPS);
      return (Idx < 16) ? (1u << Idx) : 0u;
    });
  }
#elif defined(__SSE2__)
  {
    const __m128i VQ = _mm_set1_epi8(Quote);
    const __m128i VBS = _mm_set1_epi8('\\');
    const __m128i VQM = _mm_set1_epi8('?');
    const __m128i VNL = _mm_set1_epi8('\n');
    const __m128i VCR = _mm_set1_epi8('\r');
    const __m128i VNul = _mm_setzero_si128();
    CurPtr = simdScan128(CurPtr, BufferEnd, [&](__m128i V) -> unsigned {
      __m128i Hit = _mm_or_si128(
          _mm_or_si128(_mm_cmpeq_epi8(V, VQ), _mm_cmpeq_epi8(V, VBS)),
          _mm_or_si128(_mm_cmpeq_epi8(V, VQM),
                       _mm_or_si128(_mm_cmpeq_epi8(V, VNL),
                                    _mm_or_si128(_mm_cmpeq_epi8(V, VCR),
                                                 _mm_cmpeq_epi8(V, VNul)))));
      return static_cast<unsigned>(_mm_movemask_epi8(Hit));
    });
  }
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t VQ = vdupq_n_u8(static_cast<uint8_t>(Quote));
    const uint8x16_t VBS = vdupq_n_u8('\\');
    const uint8x16_t VQM = vdupq_n_u8('?');
    const uint8x16_t VNL = vdupq_n_u8('\n');
    const uint8x16_t VCR = vdupq_n_u8('\r');
    const uint8x16_t VNul = vdupq_n_u8(0);
    auto classify = [&](uint8x16_t V) -> uint8x16_t {
      return vorrq_u8(
          vorrq_u8(vceqq_u8(V, VQ), vceqq_u8(V, VBS)),
          vorrq_u8(vceqq_u8(V, VQM),
                   vorrq_u8(vceqq_u8(V, VNL),
                            vorrq_u8(vceqq_u8(V, VCR), vceqq_u8(V, VNul)))));
    };
    if (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Stops = classify(V);
      if (neonAnySet(Stops))
        return CurPtr + neonFirstSetByte(Stops);
      CurPtr += 16;
    }
    CurPtr = simdScanNeonQuad(CurPtr, BufferEnd, classify);
    while (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Stops = classify(V);
      if (neonAnySet(Stops))
        return CurPtr + neonFirstSetByte(Stops);
      CurPtr += 16;
    }
  }
#endif
  return CurPtr;
}
} // namespace

// ===----------------------------------------------------------------------===
// Token scanning — identifiers, numbers & literals
// ===----------------------------------------------------------------------===

__attribute__((hot)) bool SourceScanner::scanIdentRest(Token &Result,
                                                       const char *CurPtr) {
  while (true) {
    CurPtr = fastParseASCIIIdentifier(CurPtr, BufferEnd);

    unsigned char Raw = static_cast<unsigned char>(*CurPtr);
    if (LLVM_LIKELY(Raw != '?' && Raw != '\\' && Raw < 0x80 && Raw != '$'))
      break;

    unsigned Size;
    unsigned char C = peekCharSize(CurPtr, Size);
    if (LLVM_UNLIKELY(isAsciiIdentifierContinue(C))) {
      CurPtr = consumeChar(CurPtr, Size, Result);
      continue;
    }
    if (LLVM_UNLIKELY(C == '$')) {
      if (!LangOpts.DollarIdents)
        break;
      if (!isLexingRawMode())
        Diag(CurPtr, diag::ext_dollar_in_identifier);
      CurPtr = consumeChar(CurPtr, Size, Result);
      continue;
    }
    if (LLVM_UNLIKELY(C == '\\') &&
        tryConsumeIdentifierUCN(CurPtr, Size, Result))
      continue;
    if (LLVM_UNLIKELY(!isASCII(C)) &&
        tryConsumeIdentifierUTF8Char(CurPtr, Result))
      continue;
    break;
  }

  const char *IdStart = BufferPtr;
  emitToken(Result, CurPtr, tok::raw_identifier);
  Result.setRawIdentifierData(IdStart);

  if (LLVM_UNLIKELY(LexingRawMode))
    return true;

  const IdentifierInfo *II = PP->ResolveRawIdent(Result);

  if (LLVM_UNLIKELY(II->needsIdentifierProcessing()))
    return PP->ResolveIdentifier(Result);

  return true;
}

bool SourceScanner::hasHexPrefix(const char *Start,
                                 const LangOptions &LangOpts) {
  auto CharAndSize1 = SourceScanner::getCharAndSizeNoWarn(Start, LangOpts);
  char C1 = CharAndSize1.Char;
  if (C1 != '0')
    return false;

  auto CharAndSize2 =
      SourceScanner::getCharAndSizeNoWarn(Start + CharAndSize1.Size, LangOpts);
  char C2 = CharAndSize2.Char;
  return (C2 == 'x' || C2 == 'X');
}

__attribute__((hot)) inline bool
SourceScanner::isExponentSignContinuation(char C, char PrevCh,
                                          const char *CurPtr) {
  unsigned IsSign = (C == '-') | (C == '+');
  if (LLVM_LIKELY(!IsSign))
    return false;
  unsigned IsE = (PrevCh == 'E') | (PrevCh == 'e');
  unsigned IsP = (PrevCh == 'P') | (PrevCh == 'p');
  if (LLVM_LIKELY(IsE))
    return !LangOpts.MicrosoftExt || !hasHexPrefix(BufferPtr, LangOpts);
  if (!IsP)
    return false;
  if (!LangOpts.C99)
    return hasHexPrefix(BufferPtr, LangOpts) &&
           memchr(BufferPtr, '_', CurPtr - BufferPtr) == nullptr;
  return true;
}

__attribute__((hot)) bool SourceScanner::scanNumber(Token &Result,
                                                    const char *CurPtr) {
  unsigned Size;
  char PrevCh = 0;

  while (true) {
    {
      const char *FastStart = CurPtr;
      CurPtr = fastScanPPNumberBody(CurPtr, BufferEnd);
      if (CurPtr > FastStart)
        PrevCh = CurPtr[-1];
    }

    {
      unsigned char Raw = static_cast<unsigned char>(*CurPtr);
      if (LLVM_LIKELY(Raw != '?' && Raw != '\\' && Raw < 0x80)) {
        if (!isPreprocessingNumberBody(static_cast<char>(Raw)) && Raw != '\'' &&
            !isExponentSignContinuation(static_cast<char>(Raw), PrevCh, CurPtr))
          break;
      }
    }

    char C = peekCharSize(CurPtr, Size);
    while (isPreprocessingNumberBody(C)) {
      CurPtr = consumeChar(CurPtr, Size, Result);
      PrevCh = C;
      C = peekCharSize(CurPtr, Size);
    }

    if (LLVM_UNLIKELY(isExponentSignContinuation(C, PrevCh, CurPtr))) {
      CurPtr = consumeChar(CurPtr, Size, Result);
      PrevCh = 0;
      continue;
    }

    if (LLVM_UNLIKELY(C == '\'' && LangOpts.C23)) {
      auto [Next, NextSize] = getCharAndSizeNoWarn(CurPtr + Size, LangOpts);
      if (isAsciiIdentifierContinue(Next)) {
        if (!isLexingRawMode())
          Diag(CurPtr, diag::warn_c23_compat_digit_separator);
        CurPtr = consumeChar(CurPtr, Size, Result);
        CurPtr = consumeChar(CurPtr, NextSize, Result);
        PrevCh = 0;
        continue;
      }
    }

    if (LLVM_UNLIKELY(C == '\\' &&
                      tryConsumeIdentifierUCN(CurPtr, Size, Result))) {
      PrevCh = 0;
      continue;
    }
    if (LLVM_UNLIKELY(!isASCII(C) &&
                      tryConsumeIdentifierUTF8Char(CurPtr, Result))) {
      PrevCh = 0;
      continue;
    }

    break;
  }

  const char *TokStart = BufferPtr;
  emitToken(Result, CurPtr, tok::numeric_constant);
  Result.setLiteralData(TokStart);
  return true;
}

__attribute__((hot)) bool
SourceScanner::scanQuotedLiteral(Token &Result, const char *CurPtr, char Quote,
                                 tok::TokenKind Kind) {
  const char *NulCharacter = nullptr;
  const char *FallbackPos = CurPtr;

  if (Quote == '"') {
    if (LLVM_UNLIKELY(!isLexingRawMode() &&
                      (Kind == tok::utf8_string_literal ||
                       Kind == tok::utf16_string_literal ||
                       Kind == tok::utf32_string_literal)))
      Diag(BufferPtr, diag::warn_c99_compat_unicode_literal);
  } else if (Quote == '\'') {
    if (LLVM_UNLIKELY(!isLexingRawMode() && (Kind == tok::utf16_char_constant ||
                                             Kind == tok::utf32_char_constant)))
      Diag(BufferPtr, diag::warn_c99_compat_unicode_literal);
  }

  char C = readChar(CurPtr, Result);

  if (Quote == '\'' && LLVM_UNLIKELY(C == '\'')) {
    if (!isLexingRawMode() && !LangOpts.AsmPreprocessor)
      Diag(BufferPtr, diag::ext_empty_character);
    emitToken(Result, CurPtr, tok::unknown);
    return true;
  }

  while (C != Quote) {
    if (LLVM_UNLIKELY(C == '\\'))
      C = readChar(CurPtr, Result);

    if (Quote == '>') {
      if (LLVM_UNLIKELY(isVerticalWhitespace(C) ||
                        (C == 0 && (CurPtr - 1 == BufferEnd)))) {
        emitToken(Result, FallbackPos, tok::less);
        return true;
      }
    } else {
      if (LLVM_UNLIKELY(C == '\n' || C == '\r' ||
                        (C == 0 && CurPtr - 1 == BufferEnd))) {
        if (!isLexingRawMode() && !LangOpts.AsmPreprocessor)
          Diag(BufferPtr, diag::ext_unterminated_char_or_string)
              << (Quote == '"' ? 1 : 0);
        emitToken(Result, CurPtr - 1, tok::unknown);
        return true;
      }
    }

    if (LLVM_UNLIKELY(C == 0))
      NulCharacter = CurPtr - 1;

    CurPtr = fastScanLiteralBody(CurPtr, BufferEnd, Quote);
    while (!LitTermTable[static_cast<unsigned char>(*CurPtr)])
      ++CurPtr;

    C = readChar(CurPtr, Result);
  }

  if (LLVM_UNLIKELY(NulCharacter && !isLexingRawMode()))
    Diag(NulCharacter, diag::null_in_char_or_string) << (Quote == '\'' ? 0 : 1);

  const char *TokStart = BufferPtr;
  emitToken(Result, CurPtr, Kind);
  Result.setLiteralData(TokStart);
  return true;
}
