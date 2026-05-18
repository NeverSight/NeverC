/*
 *  xxHash - Fast Hash algorithm
 *  Copyright (C) 2012-2021, Yann Collet
 *  BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 *  C port for LLVM CSupport.
 */

#include "include/csupport/xxhash.h"
#include "include/csupport/endian.h"
#include <assert.h>
#include <string.h>

#if __has_attribute(always_inline)
#define CSUPPORT_ALWAYS_INLINE __attribute__((always_inline)) static inline
#else
#define CSUPPORT_ALWAYS_INLINE static inline
#endif

#if __has_attribute(noinline)
#define CSUPPORT_NOINLINE __attribute__((noinline))
#else
#define CSUPPORT_NOINLINE
#endif

#if __has_builtin(__builtin_expect)
#define CSUPPORT_LIKELY(x) __builtin_expect(!!(x), 1)
#else
#define CSUPPORT_LIKELY(x) (x)
#endif

static uint64_t rotl64(uint64_t X, unsigned R) {
  return (X << R) | (X >> (64 - R));
}

#define PRIME32_1 0x9E3779B1U
#define PRIME32_2 0x85EBCA77U
#define PRIME32_3 0xC2B2AE3DU

static const uint64_t PRIME64_1 = 11400714785074694791ULL;
static const uint64_t PRIME64_2 = 14029467366897019727ULL;
static const uint64_t PRIME64_3 = 1609587929392839161ULL;
static const uint64_t PRIME64_4 = 9650029242287828579ULL;
static const uint64_t PRIME64_5 = 2870177450012600261ULL;

static uint64_t xxh_round(uint64_t Acc, uint64_t Input) {
  Acc += Input * PRIME64_2;
  Acc = rotl64(Acc, 31);
  Acc *= PRIME64_1;
  return Acc;
}

static uint64_t mergeRound(uint64_t Acc, uint64_t Val) {
  Val = xxh_round(0, Val);
  Acc ^= Val;
  Acc = Acc * PRIME64_1 + PRIME64_4;
  return Acc;
}

static uint64_t XXH64_avalanche(uint64_t hash) {
  hash ^= hash >> 33;
  hash *= PRIME64_2;
  hash ^= hash >> 29;
  hash *= PRIME64_3;
  hash ^= hash >> 32;
  return hash;
}

uint64_t csupport_xxHash64(const uint8_t *data, size_t len) {
  uint64_t Seed = 0;
  const unsigned char *P = data;
  const unsigned char *const BEnd = data + len;
  uint64_t H64;

  if (len >= 32) {
    const unsigned char *const Limit = BEnd - 32;
    uint64_t V1 = Seed + PRIME64_1 + PRIME64_2;
    uint64_t V2 = Seed + PRIME64_2;
    uint64_t V3 = Seed + 0;
    uint64_t V4 = Seed - PRIME64_1;

    do {
      V1 = xxh_round(V1, csupport_read64le(P));
      P += 8;
      V2 = xxh_round(V2, csupport_read64le(P));
      P += 8;
      V3 = xxh_round(V3, csupport_read64le(P));
      P += 8;
      V4 = xxh_round(V4, csupport_read64le(P));
      P += 8;
    } while (P <= Limit);

    H64 = rotl64(V1, 1) + rotl64(V2, 7) + rotl64(V3, 12) + rotl64(V4, 18);
    H64 = mergeRound(H64, V1);
    H64 = mergeRound(H64, V2);
    H64 = mergeRound(H64, V3);
    H64 = mergeRound(H64, V4);
  } else {
    H64 = Seed + PRIME64_5;
  }

  H64 += (uint64_t)len;

  while (P + 8 <= BEnd) {
    uint64_t const K1 = xxh_round(0, csupport_read64le(P));
    H64 ^= K1;
    H64 = rotl64(H64, 27) * PRIME64_1 + PRIME64_4;
    P += 8;
  }

  if (P + 4 <= BEnd) {
    H64 ^= (uint64_t)(csupport_read32le(P)) * PRIME64_1;
    H64 = rotl64(H64, 23) * PRIME64_2 + PRIME64_3;
    P += 4;
  }

  while (P < BEnd) {
    H64 ^= (*P) * PRIME64_5;
    H64 = rotl64(H64, 11) * PRIME64_1;
    P++;
  }

  return XXH64_avalanche(H64);
}

/* ---- XXH3 ---- */

#define XXH3_SECRETSIZE_MIN 136
#define XXH_SECRET_DEFAULT_SIZE 192
#define PRIME_MX1 0x165667919E3779F9ULL
#define PRIME_MX2 0x9FB21C651E98DF25ULL
#define XXH_STRIPE_LEN 64
#define XXH_SECRET_CONSUME_RATE 8
#define XXH_ACC_NB (XXH_STRIPE_LEN / 8)
#define XXH3_MIDSIZE_MAX 240

static const uint8_t kSecret[XXH_SECRET_DEFAULT_SIZE] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

static uint64_t XXH3_mul128_fold64(uint64_t lhs, uint64_t rhs) {
#if defined(__SIZEOF_INT128__)
  __uint128_t product = (__uint128_t)lhs * (__uint128_t)rhs;
  return (uint64_t)(product) ^ (uint64_t)(product >> 64);
#else
  const uint64_t lo_lo = (lhs & 0xFFFFFFFF) * (rhs & 0xFFFFFFFF);
  const uint64_t hi_lo = (lhs >> 32) * (rhs & 0xFFFFFFFF);
  const uint64_t lo_hi = (lhs & 0xFFFFFFFF) * (rhs >> 32);
  const uint64_t hi_hi = (lhs >> 32) * (rhs >> 32);
  const uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFF) + lo_hi;
  const uint64_t upper = (hi_lo >> 32) + (cross >> 32) + hi_hi;
  const uint64_t lower = (cross << 32) | (lo_lo & 0xFFFFFFFF);
  return upper ^ lower;
#endif
}

static uint64_t XXH3_avalanche(uint64_t hash) {
  hash ^= hash >> 37;
  hash *= PRIME_MX1;
  hash ^= hash >> 32;
  return hash;
}

static uint64_t XXH3_len_1to3_64b(const uint8_t *input, size_t len,
                                  const uint8_t *secret, uint64_t seed) {
  const uint8_t c1 = input[0];
  const uint8_t c2 = input[len >> 1];
  const uint8_t c3 = input[len - 1];
  uint32_t combined = ((uint32_t)c1 << 16) | ((uint32_t)c2 << 24) |
                      ((uint32_t)c3 << 0) | ((uint32_t)len << 8);
  uint64_t bitflip =
      (uint64_t)(csupport_read32le(secret) ^ csupport_read32le(secret + 4)) +
      seed;
  return XXH64_avalanche((uint64_t)(combined) ^ bitflip);
}

static uint64_t XXH3_len_4to8_64b(const uint8_t *input, size_t len,
                                  const uint8_t *secret, uint64_t seed) {
  seed ^= (uint64_t)csupport_byteswap32((uint32_t)seed) << 32;
  const uint32_t input1 = csupport_read32le(input);
  const uint32_t input2 = csupport_read32le(input + len - 4);
  uint64_t acc =
      (csupport_read64le(secret + 8) ^ csupport_read64le(secret + 16)) - seed;
  const uint64_t input64 = (uint64_t)input2 | ((uint64_t)input1 << 32);
  acc ^= input64;
  acc ^= rotl64(acc, 49) ^ rotl64(acc, 24);
  acc *= PRIME_MX2;
  acc ^= (acc >> 35) + (uint64_t)len;
  acc *= PRIME_MX2;
  return acc ^ (acc >> 28);
}

static uint64_t XXH3_len_9to16_64b(const uint8_t *input, size_t len,
                                   const uint8_t *secret, uint64_t seed) {
  uint64_t input_lo =
      (csupport_read64le(secret + 24) ^ csupport_read64le(secret + 32)) + seed;
  uint64_t input_hi =
      (csupport_read64le(secret + 40) ^ csupport_read64le(secret + 48)) - seed;
  input_lo ^= csupport_read64le(input);
  input_hi ^= csupport_read64le(input + len - 8);
  uint64_t acc = (uint64_t)len + csupport_byteswap64(input_lo) + input_hi +
                 XXH3_mul128_fold64(input_lo, input_hi);
  return XXH3_avalanche(acc);
}

CSUPPORT_ALWAYS_INLINE
uint64_t XXH3_len_0to16_64b(const uint8_t *input, size_t len,
                            const uint8_t *secret, uint64_t seed) {
  if (CSUPPORT_LIKELY(len > 8))
    return XXH3_len_9to16_64b(input, len, secret, seed);
  if (CSUPPORT_LIKELY(len >= 4))
    return XXH3_len_4to8_64b(input, len, secret, seed);
  if (len != 0)
    return XXH3_len_1to3_64b(input, len, secret, seed);
  return XXH64_avalanche(seed ^ csupport_read64le(secret + 56) ^
                         csupport_read64le(secret + 64));
}

static uint64_t XXH3_mix16B(const uint8_t *input, uint8_t const *secret,
                            uint64_t seed) {
  uint64_t lhs = seed;
  uint64_t rhs = 0U - seed;
  lhs += csupport_read64le(secret);
  rhs += csupport_read64le(secret + 8);
  lhs ^= csupport_read64le(input);
  rhs ^= csupport_read64le(input + 8);
  return XXH3_mul128_fold64(lhs, rhs);
}

CSUPPORT_ALWAYS_INLINE
uint64_t XXH3_len_17to128_64b(const uint8_t *input, size_t len,
                              const uint8_t *secret, uint64_t seed) {
  uint64_t acc = len * PRIME64_1, acc_end;
  acc += XXH3_mix16B(input + 0, secret + 0, seed);
  acc_end = XXH3_mix16B(input + len - 16, secret + 16, seed);
  if (len > 32) {
    acc += XXH3_mix16B(input + 16, secret + 32, seed);
    acc_end += XXH3_mix16B(input + len - 32, secret + 48, seed);
    if (len > 64) {
      acc += XXH3_mix16B(input + 32, secret + 64, seed);
      acc_end += XXH3_mix16B(input + len - 48, secret + 80, seed);
      if (len > 96) {
        acc += XXH3_mix16B(input + 48, secret + 96, seed);
        acc_end += XXH3_mix16B(input + len - 64, secret + 112, seed);
      }
    }
  }
  return XXH3_avalanche(acc + acc_end);
}

CSUPPORT_NOINLINE
static uint64_t XXH3_len_129to240_64b(const uint8_t *input, size_t len,
                                      const uint8_t *secret, uint64_t seed) {
  uint64_t acc = (uint64_t)len * PRIME64_1;
  const unsigned nbRounds = (unsigned)(len / 16);
  unsigned i;
  for (i = 0; i < 8; ++i)
    acc += XXH3_mix16B(input + 16 * i, secret + 16 * i, seed);
  acc = XXH3_avalanche(acc);
  for (i = 8; i < nbRounds; ++i)
    acc += XXH3_mix16B(input + 16 * i, secret + 16 * (i - 8) + 3, seed);
  acc += XXH3_mix16B(input + len - 16,
                     secret + XXH3_SECRETSIZE_MIN - 17, seed);
  return XXH3_avalanche(acc);
}

CSUPPORT_ALWAYS_INLINE
void XXH3_accumulate_512_scalar(uint64_t *acc, const uint8_t *input,
                                const uint8_t *secret) {
  size_t i;
  for (i = 0; i < XXH_ACC_NB; ++i) {
    uint64_t data_val = csupport_read64le(input + 8 * i);
    uint64_t data_key = data_val ^ csupport_read64le(secret + 8 * i);
    acc[i ^ 1] += data_val;
    acc[i] += (uint32_t)(data_key) * (data_key >> 32);
  }
}

CSUPPORT_ALWAYS_INLINE
void XXH3_accumulate_scalar(uint64_t *acc, const uint8_t *input,
                            const uint8_t *secret, size_t nbStripes) {
  size_t n;
  for (n = 0; n < nbStripes; ++n)
    XXH3_accumulate_512_scalar(acc, input + n * XXH_STRIPE_LEN,
                               secret + n * XXH_SECRET_CONSUME_RATE);
}

static void XXH3_scrambleAcc(uint64_t *acc, const uint8_t *secret) {
  size_t i;
  for (i = 0; i < XXH_ACC_NB; ++i) {
    acc[i] ^= acc[i] >> 47;
    acc[i] ^= csupport_read64le(secret + 8 * i);
    acc[i] *= PRIME32_1;
  }
}

static uint64_t XXH3_mix2Accs(const uint64_t *acc, const uint8_t *secret) {
  return XXH3_mul128_fold64(acc[0] ^ csupport_read64le(secret),
                            acc[1] ^ csupport_read64le(secret + 8));
}

static uint64_t XXH3_mergeAccs(const uint64_t *acc, const uint8_t *key,
                               uint64_t start) {
  uint64_t result64 = start;
  size_t i;
  for (i = 0; i < 4; ++i)
    result64 += XXH3_mix2Accs(acc + 2 * i, key + 16 * i);
  return XXH3_avalanche(result64);
}

CSUPPORT_NOINLINE
static uint64_t XXH3_hashLong_64b(const uint8_t *input, size_t len,
                                  const uint8_t *secret, size_t secretSize) {
  const size_t nbStripesPerBlock =
      (secretSize - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
  const size_t block_len = XXH_STRIPE_LEN * nbStripesPerBlock;
  const size_t nb_blocks = (len - 1) / block_len;
  size_t n;
  _Alignas(16) uint64_t acc[XXH_ACC_NB] = {
      PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3,
      PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1,
  };
  for (n = 0; n < nb_blocks; ++n) {
    XXH3_accumulate_scalar(acc, input + n * block_len, secret,
                           nbStripesPerBlock);
    XXH3_scrambleAcc(acc, secret + secretSize - XXH_STRIPE_LEN);
  }
  {
    const size_t nbStripes =
        (len - 1 - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
    assert(nbStripes <= secretSize / XXH_SECRET_CONSUME_RATE);
    XXH3_accumulate_scalar(acc, input + nb_blocks * block_len, secret,
                           nbStripes);
    XXH3_accumulate_512_scalar(acc, input + len - XXH_STRIPE_LEN,
                               secret + secretSize - XXH_STRIPE_LEN - 7);
  }
  return XXH3_mergeAccs(acc, secret + 11, (uint64_t)len * PRIME64_1);
}

uint64_t csupport_xxh3_64bits(const uint8_t *data, size_t len) {
  if (len <= 16)
    return XXH3_len_0to16_64b(data, len, kSecret, 0);
  if (len <= 128)
    return XXH3_len_17to128_64b(data, len, kSecret, 0);
  if (len <= XXH3_MIDSIZE_MAX)
    return XXH3_len_129to240_64b(data, len, kSecret, 0);
  return XXH3_hashLong_64b(data, len, kSecret, sizeof(kSecret));
}

