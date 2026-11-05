//===- i386_intrinsic_headers.c - x86 header execution
//---------------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
// Check standard header values and the marker's callee-saved register in both
// pointer widths. HRESET and AVX-512 are emitted but are never executed here.
//===----------------------------------------------------------------------===//

#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>

#define NOINLINE __attribute__((noinline))

NOINLINE uintptr_t marker(uintptr_t input) {
#ifdef __x86_64__
  register uintptr_t value __asm__("rbx") = input;
#else
  register uintptr_t value __asm__("ebx") = input;
#endif
  __asm__ volatile("" : "+b"(value));
  __SSC_MARK(0x1234);
  __asm__ volatile("" : "+b"(value));
  return value;
}

NOINLINE void vector128(const unsigned *a, const unsigned *b, unsigned *out) {
  __m128i x = _mm_loadu_si128((const __m128i *)a);
  __m128i y = _mm_loadu_si128((const __m128i *)b);
#ifdef WRONG_ADD
  _mm_storeu_si128((__m128i *)out, _mm_sub_epi32(x, y));
#else
  _mm_storeu_si128((__m128i *)out, _mm_add_epi32(x, y));
#endif
  _mm_storeu_si128((__m128i *)(out + 4), _mm_shuffle_epi32(x, 0x1b));
  out[8] = (unsigned)_mm_extract_ps(_mm_castsi128_ps(x), 2);
}

NOINLINE uint64_t packed64(uint64_t a, uint64_t b) {
  __m64 x, y, sum;
  uint64_t result;
  __builtin_memcpy(&x, &a, sizeof(x));
  __builtin_memcpy(&y, &b, sizeof(y));
  sum = _mm_add_pi16(x, y);
  __builtin_memcpy(&result, &sum, sizeof(result));
  _mm_empty();
  return result;
}

NOINLINE long double x87_after_mmx(unsigned x) { return (long double)x + 0.5L; }

NOINLINE void vector256(const unsigned *a, const unsigned *b, unsigned *out) {
  _mm256_storeu_si256((__m256i *)out,
                      _mm256_add_epi32(_mm256_loadu_si256((const __m256i *)a),
                                       _mm256_loadu_si256((const __m256i *)b)));
}

__attribute__((target("avx512f"))) void
vector512(const unsigned *a, const unsigned *b, unsigned *out) {
  _mm512_storeu_si512(
      out, _mm512_add_epi32(_mm512_loadu_si512(a), _mm512_loadu_si512(b)));
}

__attribute__((target("hreset"))) void history(unsigned x) { _hreset(x); }

static unsigned random_word(unsigned *state) {
  *state ^= *state << 13;
  *state ^= *state >> 17;
  *state ^= *state << 5;
  return *state;
}

static unsigned crc32c(unsigned crc, unsigned word) {
  crc ^= word;
  for (unsigned bit = 0; bit != 32; ++bit)
    crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0x82f63b78u);
  return crc;
}

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      fprintf(stderr, "case %u: %s\n", iteration, #condition);                 \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(void) {
  unsigned state = 0x12345678, checks = 0;
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    unsigned a[8], b[8], out[9];
    for (unsigned lane = 0; lane != 8; ++lane) {
      a[lane] = random_word(&state);
      b[lane] = random_word(&state);
    }
    if (iteration < 4) {
      a[0] = iteration & 1 ? UINT32_MAX : 0;
      b[0] = iteration & 2 ? UINT32_MAX : 0;
    }
    vector128(a, b, out);
    for (unsigned lane = 0; lane != 4; ++lane) {
      CHECK(out[lane] == a[lane] + b[lane]);
      CHECK(out[4 + lane] == a[3 - lane]);
    }
    CHECK(out[8] == a[2]);
    const uint64_t x = ((uint64_t)a[1] << 32) | a[0];
    const uint64_t y = ((uint64_t)b[1] << 32) | b[0];
    const uint64_t sum = packed64(x, y);
    for (unsigned lane = 0; lane != 4; ++lane)
      CHECK((uint16_t)(sum >> (lane * 16)) ==
            (uint16_t)((x >> (lane * 16)) + (y >> (lane * 16))));
    CHECK((unsigned)(x87_after_mmx(a[0] & 0xffff) * 2) ==
          2 * (a[0] & 0xffff) + 1);
    vector256(a, b, out);
    for (unsigned lane = 0; lane != 8; ++lane)
      CHECK(out[lane] == a[lane] + b[lane]);
    unsigned value;
    const unsigned carry = iteration & 1;
    const uint64_t wide = (uint64_t)a[0] + b[0] + carry;
    unsigned flag = _addcarry_u32(carry, a[0], b[0], &value);
    CHECK(value == (unsigned)wide && flag == (wide >> 32));
    flag = _addcarryx_u32(carry, a[0], b[0], &value);
    CHECK(value == (unsigned)wide && flag == (wide >> 32));
    flag = _subborrow_u32(carry, a[0], b[0], &value);
    CHECK(value == a[0] - b[0] - carry &&
          flag == ((uint64_t)a[0] < (uint64_t)b[0] + carry));
    CHECK(_mm_crc32_u32(a[0], b[0]) == crc32c(a[0], b[0]));
    CHECK(marker((uintptr_t)x) == (uintptr_t)x);
  }
  printf("{\"cases\":4096,\"checks\":%u}\n", checks);
  return 0;
}
