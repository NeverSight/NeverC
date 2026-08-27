#include "neverc/std/hash/fnv.h"

#define FNV_WIDE_MAX_WORDS 16
#define FNV_WIDE_MAX_LIMBS (FNV_WIDE_MAX_WORDS * 2)

/* Each wide prime is represented as (1 << shift) + factor. */
#define FNV_PRIME_256_FACTOR UINT32_C(0x163)
#define FNV_PRIME_256_SHIFT 168
#define FNV_PRIME_512_FACTOR UINT32_C(0x157)
#define FNV_PRIME_512_SHIFT 344
#define FNV_PRIME_1024_FACTOR UINT32_C(0x18d)
#define FNV_PRIME_1024_SHIFT 680

typedef enum { FNV_WIDE_ONE, FNV_WIDE_ONE_A } fnv_wide_order_t;

static void unpack_words(uint32_t *limbs, const uint64_t *words,
                         size_t word_count) {
  for (size_t i = 0; i < word_count; ++i) {
    uint64_t word = words[word_count - i - 1];
    limbs[2 * i] = (uint32_t)word;
    limbs[2 * i + 1] = (uint32_t)(word >> 32);
  }
}

static void pack_words(uint64_t *words, const uint32_t *limbs,
                       size_t word_count) {
  for (size_t i = 0; i < word_count; ++i) {
    uint64_t word = (uint64_t)limbs[2 * i];
    word |= (uint64_t)limbs[2 * i + 1] << 32;
    words[word_count - i - 1] = word;
  }
}

static void multiply_sparse(uint32_t *result, const uint32_t *value,
                            size_t limb_count, uint32_t factor,
                            unsigned shift) {
  uint64_t carry = 0;
  for (size_t i = 0; i < limb_count; ++i) {
    uint64_t product = (uint64_t)value[i] * factor + carry;
    result[i] = (uint32_t)product;
    carry = product >> 32;
  }

  const size_t word_shift = shift / 32;
  const unsigned bit_shift = shift % 32;
  carry = 0;
  for (size_t i = 0; i < limb_count; ++i) {
    uint32_t shifted = 0;
    if (i >= word_shift) {
      size_t source = i - word_shift;
      shifted = value[source] << bit_shift;
      if (bit_shift != 0 && source != 0)
        shifted |= value[source - 1] >> (32 - bit_shift);
    }
    uint64_t sum = (uint64_t)result[i] + shifted + carry;
    result[i] = (uint32_t)sum;
    carry = sum >> 32;
  }
}

static void update_wide(uint64_t *words, size_t word_count, uint32_t factor,
                        unsigned shift, fnv_wide_order_t order,
                        const void *data, size_t len) {
  if (!data || len == 0)
    return;

  uint32_t buffers[2][FNV_WIDE_MAX_LIMBS];
  uint32_t *state = buffers[0];
  uint32_t *next = buffers[1];
  const size_t limb_count = word_count * 2;
  const uint8_t *bytes = (const uint8_t *)data;
  unpack_words(state, words, word_count);

  for (size_t i = 0; i < len; ++i) {
    if (order == FNV_WIDE_ONE_A)
      state[0] ^= bytes[i];
    multiply_sparse(next, state, limb_count, factor, shift);
    if (order == FNV_WIDE_ONE)
      next[0] ^= bytes[i];
    uint32_t *swap = state;
    state = next;
    next = swap;
  }

  pack_words(words, state, word_count);
}

static void store_words_be(uint8_t *out, const uint64_t *words,
                           size_t word_count) {
  if (!out)
    return;
  for (size_t i = 0; i < word_count; ++i) {
    for (size_t j = 0; j < 8; ++j)
      out[8 * i + j] = (uint8_t)(words[i] >> (56 - 8 * j));
  }
}

static void store_words_le(uint8_t *out, const uint64_t *words,
                           size_t word_count) {
  if (!out)
    return;
  for (size_t i = 0; i < word_count; ++i) {
    uint64_t word = words[word_count - i - 1];
    for (size_t j = 0; j < 8; ++j)
      out[8 * i + j] = (uint8_t)(word >> (8 * j));
  }
}

neverc_fnv_256_t neverc_fnv_update256(neverc_fnv_256_t hash, const void *data,
                                      size_t len) {
  update_wide(hash.words, 4, FNV_PRIME_256_FACTOR, FNV_PRIME_256_SHIFT,
              FNV_WIDE_ONE, data, len);
  return hash;
}

neverc_fnv_256_t neverc_fnv_update256a(neverc_fnv_256_t hash, const void *data,
                                       size_t len) {
  update_wide(hash.words, 4, FNV_PRIME_256_FACTOR, FNV_PRIME_256_SHIFT,
              FNV_WIDE_ONE_A, data, len);
  return hash;
}

neverc_fnv_256_t neverc_fnv_sum256(const void *data, size_t len) {
  neverc_fnv_256_t hash = NEVERC_FNV256_OFFSET_BASIS_INITIALIZER;
  return neverc_fnv_update256(hash, data, len);
}

neverc_fnv_256_t neverc_fnv_sum256a(const void *data, size_t len) {
  neverc_fnv_256_t hash = NEVERC_FNV256_OFFSET_BASIS_INITIALIZER;
  return neverc_fnv_update256a(hash, data, len);
}

neverc_fnv_256_t neverc_fnv0_sum256(const void *data, size_t len) {
  neverc_fnv_256_t hash = {{0}};
  return neverc_fnv_update256(hash, data, len);
}

void neverc_fnv_store256_be(uint8_t out[32], neverc_fnv_256_t hash) {
  store_words_be(out, hash.words, 4);
}

void neverc_fnv_store256_le(uint8_t out[32], neverc_fnv_256_t hash) {
  store_words_le(out, hash.words, 4);
}

neverc_fnv_512_t neverc_fnv_update512(neverc_fnv_512_t hash, const void *data,
                                      size_t len) {
  update_wide(hash.words, 8, FNV_PRIME_512_FACTOR, FNV_PRIME_512_SHIFT,
              FNV_WIDE_ONE, data, len);
  return hash;
}

neverc_fnv_512_t neverc_fnv_update512a(neverc_fnv_512_t hash, const void *data,
                                       size_t len) {
  update_wide(hash.words, 8, FNV_PRIME_512_FACTOR, FNV_PRIME_512_SHIFT,
              FNV_WIDE_ONE_A, data, len);
  return hash;
}

neverc_fnv_512_t neverc_fnv_sum512(const void *data, size_t len) {
  neverc_fnv_512_t hash = NEVERC_FNV512_OFFSET_BASIS_INITIALIZER;
  return neverc_fnv_update512(hash, data, len);
}

neverc_fnv_512_t neverc_fnv_sum512a(const void *data, size_t len) {
  neverc_fnv_512_t hash = NEVERC_FNV512_OFFSET_BASIS_INITIALIZER;
  return neverc_fnv_update512a(hash, data, len);
}

neverc_fnv_512_t neverc_fnv0_sum512(const void *data, size_t len) {
  neverc_fnv_512_t hash = {{0}};
  return neverc_fnv_update512(hash, data, len);
}

void neverc_fnv_store512_be(uint8_t out[64], neverc_fnv_512_t hash) {
  store_words_be(out, hash.words, 8);
}

void neverc_fnv_store512_le(uint8_t out[64], neverc_fnv_512_t hash) {
  store_words_le(out, hash.words, 8);
}

neverc_fnv_1024_t neverc_fnv_update1024(neverc_fnv_1024_t hash,
                                        const void *data, size_t len) {
  update_wide(hash.words, 16, FNV_PRIME_1024_FACTOR, FNV_PRIME_1024_SHIFT,
              FNV_WIDE_ONE, data, len);
  return hash;
}

neverc_fnv_1024_t neverc_fnv_update1024a(neverc_fnv_1024_t hash,
                                         const void *data, size_t len) {
  update_wide(hash.words, 16, FNV_PRIME_1024_FACTOR, FNV_PRIME_1024_SHIFT,
              FNV_WIDE_ONE_A, data, len);
  return hash;
}

neverc_fnv_1024_t neverc_fnv_sum1024(const void *data, size_t len) {
  neverc_fnv_1024_t hash = NEVERC_FNV1024_OFFSET_BASIS_INITIALIZER;
  return neverc_fnv_update1024(hash, data, len);
}

neverc_fnv_1024_t neverc_fnv_sum1024a(const void *data, size_t len) {
  neverc_fnv_1024_t hash = NEVERC_FNV1024_OFFSET_BASIS_INITIALIZER;
  return neverc_fnv_update1024a(hash, data, len);
}

neverc_fnv_1024_t neverc_fnv0_sum1024(const void *data, size_t len) {
  neverc_fnv_1024_t hash = {{0}};
  return neverc_fnv_update1024(hash, data, len);
}

void neverc_fnv_store1024_be(uint8_t out[128], neverc_fnv_1024_t hash) {
  store_words_be(out, hash.words, 16);
}

void neverc_fnv_store1024_le(uint8_t out[128], neverc_fnv_1024_t hash) {
  store_words_le(out, hash.words, 16);
}
