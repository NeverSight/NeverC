// SourceScannerSIMDHelpers.inc — shared SIMD scan helpers
// Textually included by SourceScanner.cpp, SourceScannerTokens.cpp,
// and SourceScannerComment.cpp to avoid duplicating SIMD code while
// keeping anonymous-namespace internal linkage.
//
// Prerequisites: SIMD headers must be included before this file.

#if defined(__aarch64__) && defined(__ARM_NEON)
namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
inline unsigned neonFirstSetByte_WS(uint8x16_t Mask) {
  uint64x2_t AsU64 = vreinterpretq_u64_u8(Mask);
  uint64_t Lo = vgetq_lane_u64(AsU64, 0);
  uint64_t Hi = vgetq_lane_u64(AsU64, 1);
  if (Lo)
    return __builtin_ctzll(Lo) >> 3;
  return 8 + (__builtin_ctzll(Hi) >> 3);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline bool neonAnySet_WS(uint8x16_t V) {
  return vmaxvq_u8(V) != 0;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline unsigned neonFirstSetByte32_WS(uint8x16_t MaskLo,
                                      uint8x16_t MaskHi) {
  uint64x2_t Lo64 = vreinterpretq_u64_u8(MaskLo);
  uint64_t L0 = vgetq_lane_u64(Lo64, 0);
  uint64_t L1 = vgetq_lane_u64(Lo64, 1);
  if (L0) return __builtin_ctzll(L0) >> 3;
  if (L1) return 8 + (__builtin_ctzll(L1) >> 3);
  uint64x2_t Hi64 = vreinterpretq_u64_u8(MaskHi);
  uint64_t H0 = vgetq_lane_u64(Hi64, 0);
  uint64_t H1 = vgetq_lane_u64(Hi64, 1);
  if (H0) return 16 + (__builtin_ctzll(H0) >> 3);
  return 24 + (__builtin_ctzll(H1) >> 3);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline uint8x16_t neonTblClassify_WS(uint8x16_t V, uint8x16_t LoLUT,
                                     uint8x16_t HiLUT) {
  uint8x16_t LoNib = vandq_u8(V, vdupq_n_u8(0x0F));
  uint8x16_t HiNib = vshrq_n_u8(V, 4);
  return vandq_u8(vqtbl1q_u8(LoLUT, LoNib), vqtbl1q_u8(HiLUT, HiNib));
}
} // namespace

template <typename ClassifierNeon>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScanNeonQuad_WS(const char *Ptr, const char *End, ClassifierNeon Classify) {
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
    if (neonAnySet_WS(vorrq_u8(Any01, Any23))) {
      if (neonAnySet_WS(Any01)) {
        if (neonAnySet_WS(S0))
          return Ptr + neonFirstSetByte_WS(S0);
        return Ptr + 16 + neonFirstSetByte_WS(S1);
      }
      if (neonAnySet_WS(S2))
        return Ptr + 32 + neonFirstSetByte_WS(S2);
      return Ptr + 48 + neonFirstSetByte_WS(S3);
    }
    Ptr += 64;
  }
  return Ptr;
}
#endif

#ifdef __AVX512BW__
template <typename Classifier512>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan512_WS(const char *Ptr, const char *End, Classifier512 Classify) {
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
simdScan512x2_WS(const char *Ptr, const char *End, Classifier512 Classify) {
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
  return simdScan512_WS(Ptr, End, Classify);
}
#endif

#ifdef __AVX2__
template <typename Classifier256>
LLVM_ATTRIBUTE_ALWAYS_INLINE static const char *
simdScan256_WS(const char *Ptr, const char *End, Classifier256 Classify) {
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
simdScan256x2_WS(const char *Ptr, const char *End, Classifier256 Classify) {
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
  return simdScan256_WS(Ptr, End, Classify);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
__m256i vpshufbClassify256_WS(__m256i V, __m256i LoLUT, __m256i HiLUT) {
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
simdScan128_WS(const char *Ptr, const char *End, Classifier128 Classify) {
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
__m128i pshufbClassify128_WS(__m128i V, __m128i LoLUT, __m128i HiLUT) {
  __m128i NibMask = _mm_set1_epi8(0x0F);
  __m128i LoNib = _mm_and_si128(V, NibMask);
  __m128i HiNib = _mm_and_si128(_mm_srli_epi16(V, 4), NibMask);
  return _mm_and_si128(_mm_shuffle_epi8(LoLUT, LoNib),
                       _mm_shuffle_epi8(HiLUT, HiNib));
}
} // namespace
#endif

namespace {
__attribute__((always_inline))
inline const char *
skipWhitespaceRun(const char *CurPtr, const char *BufferEnd) {
#ifdef __AVX512BW__
  CurPtr = simdScan512_WS(CurPtr, BufferEnd, [](__m512i V) -> __mmask64 {
    return ~(_mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8(' ')) |
             _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('\t')) |
             _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('\f')) |
             _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('\v')));
  });
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  {
    const __m256i WSLoLUT = _mm256_setr_epi8(
        4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0,
        4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0);
    const __m256i WSHiLUT = _mm256_setr_epi8(
        1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CurPtr = simdScan256_WS(CurPtr, BufferEnd,
                         [WSLoLUT, WSHiLUT](__m256i V) -> unsigned {
      __m256i Hit = vpshufbClassify256_WS(V, WSLoLUT, WSHiLUT);
      return ~static_cast<unsigned>(
          _mm256_movemask_epi8(_mm256_cmpeq_epi8(Hit, _mm256_setzero_si256())));
    });
  }
#elif defined(__SSSE3__)
  {
    const __m128i WSLoLUT = _mm_setr_epi8(
        4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0);
    const __m128i WSHiLUT = _mm_setr_epi8(
        1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CurPtr = simdScan128_WS(CurPtr, BufferEnd,
                         [WSLoLUT, WSHiLUT](__m128i V) -> unsigned {
      __m128i Hit = pshufbClassify128_WS(V, WSLoLUT, WSHiLUT);
      return static_cast<unsigned>(
          ~_mm_movemask_epi8(_mm_cmpeq_epi8(Hit, _mm_setzero_si128()))) & 0xFFFFu;
    });
  }
#elif defined(__SSE2__)
  CurPtr = simdScan128_WS(CurPtr, BufferEnd, [](__m128i V) -> unsigned {
    __m128i IsWS = _mm_or_si128(
        _mm_or_si128(_mm_cmpeq_epi8(V, _mm_set1_epi8(' ')),
                     _mm_cmpeq_epi8(V, _mm_set1_epi8('\t'))),
        _mm_or_si128(_mm_cmpeq_epi8(V, _mm_set1_epi8('\f')),
                     _mm_cmpeq_epi8(V, _mm_set1_epi8('\v'))));
    return static_cast<unsigned>(~_mm_movemask_epi8(IsWS)) & 0xFFFFu;
  });
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    alignas(16) static constexpr uint8_t WSLoData[16] = {
        4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0};
    alignas(16) static constexpr uint8_t WSHiData[16] = {
        1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8x16_t LoLUT = vld1q_u8(WSLoData);
    uint8x16_t HiLUT = vld1q_u8(WSHiData);
    if (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Hit = neonTblClassify_WS(V, LoLUT, HiLUT);
      uint8x16_t IsNonWS = vceqq_u8(Hit, vdupq_n_u8(0));
      if (neonAnySet_WS(IsNonWS))
        return CurPtr + neonFirstSetByte_WS(IsNonWS);
      CurPtr += 16;
    }
    CurPtr = simdScanNeonQuad_WS(CurPtr, BufferEnd,
                              [LoLUT, HiLUT](uint8x16_t V) -> uint8x16_t {
      uint8x16_t Hit = neonTblClassify_WS(V, LoLUT, HiLUT);
      return vceqq_u8(Hit, vdupq_n_u8(0));
    });
    while (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Hit = neonTblClassify_WS(V, LoLUT, HiLUT);
      uint8x16_t IsNonWS = vceqq_u8(Hit, vdupq_n_u8(0));
      if (neonAnySet_WS(IsNonWS))
        return CurPtr + neonFirstSetByte_WS(IsNonWS);
      CurPtr += 16;
    }
  }
#else
  {
    constexpr uint64_t Broadcast = 0x0101010101010101ULL;
    constexpr uint64_t HiBitMask = 0x8080808080808080ULL;
    auto hasZeroByte = [](uint64_t V) -> uint64_t {
      return (V - Broadcast) & ~V & HiBitMask;
    };
    while (CurPtr + 8 <= BufferEnd) {
      uint64_t Word;
      std::memcpy(&Word, CurPtr, 8);
      uint64_t MatchSP  = hasZeroByte(Word ^ (Broadcast * ' '));
      uint64_t MatchTab = hasZeroByte(Word ^ (Broadcast * '\t'));
      uint64_t MatchFF  = hasZeroByte(Word ^ (Broadcast * '\f'));
      uint64_t MatchVT  = hasZeroByte(Word ^ (Broadcast * '\v'));
      uint64_t AllWS = MatchSP | MatchTab | MatchFF | MatchVT;
      uint64_t NonWS = ~AllWS & HiBitMask;
      if (NonWS != 0)
        return CurPtr + (llvm::countr_zero(NonWS) >> 3);
      CurPtr += 8;
    }
  }
#endif
  while (isHorizontalWhitespace(*CurPtr))
    ++CurPtr;
  return CurPtr;
}

__attribute__((always_inline))
inline const char *
scanToLineBreak(const char *CurPtr, const char *BufferEnd) {
#ifdef __AVX512BW__
  CurPtr = simdScan512x2_WS(CurPtr, BufferEnd, [](__m512i V) -> __mmask64 {
    __mmask64 HighBits = _mm512_movepi8_mask(V);
    __mmask64 StopBits = _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('\n')) |
                         _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('\r')) |
                         _mm512_cmpeq_epi8_mask(V, _mm512_setzero_si512());
    return HighBits | StopBits;
  });
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  CurPtr = simdScan256x2_WS(CurPtr, BufferEnd, [](__m256i V) -> unsigned {
    unsigned HighBits = static_cast<unsigned>(_mm256_movemask_epi8(V));
    __m256i StopV = _mm256_or_si256(
        _mm256_cmpeq_epi8(V, _mm256_set1_epi8('\n')),
        _mm256_or_si256(_mm256_cmpeq_epi8(V, _mm256_set1_epi8('\r')),
                        _mm256_cmpeq_epi8(V, _mm256_setzero_si256())));
    return HighBits | static_cast<unsigned>(_mm256_movemask_epi8(StopV));
  });
#elif defined(__SSE2__)
  CurPtr = simdScan128_WS(CurPtr, BufferEnd, [](__m128i V) -> unsigned {
    unsigned HighBits = static_cast<unsigned>(_mm_movemask_epi8(V));
    __m128i StopV = _mm_or_si128(
        _mm_cmpeq_epi8(V, _mm_set1_epi8('\n')),
        _mm_or_si128(_mm_cmpeq_epi8(V, _mm_set1_epi8('\r')),
                     _mm_cmpeq_epi8(V, _mm_setzero_si128())));
    return HighBits | static_cast<unsigned>(_mm_movemask_epi8(StopV));
  });
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    auto classify = [](uint8x16_t V) -> uint8x16_t {
      return vorrq_u8(
          vorrq_u8(vceqq_u8(V, vdupq_n_u8('\n')),
                   vceqq_u8(V, vdupq_n_u8('\r'))),
          vorrq_u8(vceqq_u8(V, vdupq_n_u8(0)),
                   vcgeq_u8(V, vdupq_n_u8(0x80))));
    };
    if (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Stops = classify(V);
      if (neonAnySet_WS(Stops))
        return CurPtr + neonFirstSetByte_WS(Stops);
      CurPtr += 16;
    }
    CurPtr = simdScanNeonQuad_WS(CurPtr, BufferEnd, classify);
    while (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Stops = classify(V);
      if (neonAnySet_WS(Stops))
        return CurPtr + neonFirstSetByte_WS(Stops);
      CurPtr += 16;
    }
  }
#else
  {
    constexpr uint64_t Broadcast = 0x0101010101010101ULL;
    constexpr uint64_t HiBitMask = 0x8080808080808080ULL;
    auto hasZeroByte = [](uint64_t V) -> uint64_t {
      return (V - 0x0101010101010101ULL) & ~V & 0x8080808080808080ULL;
    };
    while (CurPtr + 16 <= BufferEnd) {
      uint64_t W0, W1;
      std::memcpy(&W0, CurPtr, 8);
      std::memcpy(&W1, CurPtr + 8, 8);
      uint64_t Stop0 = (W0 & HiBitMask) | hasZeroByte(W0) |
                       hasZeroByte(W0 ^ (Broadcast * '\n')) |
                       hasZeroByte(W0 ^ (Broadcast * '\r'));
      if (Stop0 != 0)
        return CurPtr + (llvm::countr_zero(Stop0) >> 3);
      uint64_t Stop1 = (W1 & HiBitMask) | hasZeroByte(W1) |
                       hasZeroByte(W1 ^ (Broadcast * '\n')) |
                       hasZeroByte(W1 ^ (Broadcast * '\r'));
      if (Stop1 != 0)
        return CurPtr + 8 + (llvm::countr_zero(Stop1) >> 3);
      CurPtr += 16;
    }
    if (CurPtr + 8 <= BufferEnd) {
      uint64_t Word;
      std::memcpy(&Word, CurPtr, 8);
      uint64_t StopMask = (Word & HiBitMask) | hasZeroByte(Word) |
                          hasZeroByte(Word ^ (Broadcast * '\n')) |
                          hasZeroByte(Word ^ (Broadcast * '\r'));
      if (StopMask != 0)
        return CurPtr + (llvm::countr_zero(StopMask) >> 3);
      CurPtr += 8;
    }
  }
#endif
  while (isASCII(*CurPtr) && *CurPtr != 0 &&
         *CurPtr != '\n' && *CurPtr != '\r')
    ++CurPtr;
  return CurPtr;
}

__attribute__((always_inline))
inline const char *
scanBlockBody(const char *CurPtr, const char *BufferEnd) {
#ifdef __AVX512BW__
  CurPtr = simdScan512x2_WS(CurPtr, BufferEnd, [](__m512i V) -> __mmask64 {
    return _mm512_movepi8_mask(V) |
           _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('/')) |
           _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('*')) |
           _mm512_cmpeq_epi8_mask(V, _mm512_set1_epi8('\n'));
  });
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  CurPtr = simdScan256x2_WS(CurPtr, BufferEnd, [](__m256i V) -> unsigned {
    __m256i StopV = _mm256_or_si256(
        _mm256_cmpeq_epi8(V, _mm256_set1_epi8('/')),
        _mm256_or_si256(_mm256_cmpeq_epi8(V, _mm256_set1_epi8('*')),
                        _mm256_cmpeq_epi8(V, _mm256_set1_epi8('\n'))));
    return static_cast<unsigned>(_mm256_movemask_epi8(V)) |
           static_cast<unsigned>(_mm256_movemask_epi8(StopV));
  });
#elif defined(__SSE2__)
  CurPtr = simdScan128_WS(CurPtr, BufferEnd, [](__m128i V) -> unsigned {
    __m128i StopV = _mm_or_si128(
        _mm_cmpeq_epi8(V, _mm_set1_epi8('/')),
        _mm_or_si128(_mm_cmpeq_epi8(V, _mm_set1_epi8('*')),
                     _mm_cmpeq_epi8(V, _mm_set1_epi8('\n'))));
    return static_cast<unsigned>(_mm_movemask_epi8(V)) |
           static_cast<unsigned>(_mm_movemask_epi8(StopV));
  });
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    auto classify = [](uint8x16_t V) -> uint8x16_t {
      return vorrq_u8(
          vorrq_u8(vcgeq_u8(V, vdupq_n_u8(0x80)),
                   vceqq_u8(V, vdupq_n_u8('/'))),
          vorrq_u8(vceqq_u8(V, vdupq_n_u8('*')),
                   vceqq_u8(V, vdupq_n_u8('\n'))));
    };
    if (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Stops = classify(V);
      if (neonAnySet_WS(Stops))
        return CurPtr + neonFirstSetByte_WS(Stops);
      CurPtr += 16;
    }
    CurPtr = simdScanNeonQuad_WS(CurPtr, BufferEnd, classify);
    while (LLVM_LIKELY(BufferEnd - CurPtr >= 16)) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(CurPtr));
      uint8x16_t Stops = classify(V);
      if (neonAnySet_WS(Stops))
        return CurPtr + neonFirstSetByte_WS(Stops);
      CurPtr += 16;
    }
  }
#else
  {
    constexpr uint64_t Broadcast = 0x0101010101010101ULL;
    constexpr uint64_t HiBitMask = 0x8080808080808080ULL;
    auto hasZeroByte = [](uint64_t V) -> uint64_t {
      return (V - 0x0101010101010101ULL) & ~V & 0x8080808080808080ULL;
    };
    while (CurPtr + 16 <= BufferEnd) {
      uint64_t W0, W1;
      std::memcpy(&W0, CurPtr, 8);
      std::memcpy(&W1, CurPtr + 8, 8);
      uint64_t Stop0 = (W0 & HiBitMask) | hasZeroByte(W0 ^ (Broadcast * '/')) |
                       hasZeroByte(W0 ^ (Broadcast * '*')) |
                       hasZeroByte(W0 ^ (Broadcast * '\n'));
      if (Stop0 != 0)
        return CurPtr + (llvm::countr_zero(Stop0 & HiBitMask) >> 3);
      uint64_t Stop1 = (W1 & HiBitMask) | hasZeroByte(W1 ^ (Broadcast * '/')) |
                       hasZeroByte(W1 ^ (Broadcast * '*')) |
                       hasZeroByte(W1 ^ (Broadcast * '\n'));
      if (Stop1 != 0)
        return CurPtr + 8 + (llvm::countr_zero(Stop1 & HiBitMask) >> 3);
      CurPtr += 16;
    }
    if (CurPtr + 8 <= BufferEnd) {
      uint64_t Word;
      std::memcpy(&Word, CurPtr, 8);
      uint64_t StopMask = (Word & HiBitMask) |
                          hasZeroByte(Word ^ (Broadcast * '/')) |
                          hasZeroByte(Word ^ (Broadcast * '*')) |
                          hasZeroByte(Word ^ (Broadcast * '\n'));
      if (StopMask != 0)
        return CurPtr + (llvm::countr_zero(StopMask & HiBitMask) >> 3);
      CurPtr += 8;
    }
  }
#endif
  return CurPtr;
}
} // namespace
