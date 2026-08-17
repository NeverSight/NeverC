#include "neverc/std/hash/xxhash.h"

/*
 * xxHash64 implementation.
 * Reference: https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md
 */

#define XXH_PRIME64_1 11400714785074694791ULL
#define XXH_PRIME64_2 14029467366897019727ULL
#define XXH_PRIME64_3  1609587929392839161ULL
#define XXH_PRIME64_4  9650029242287828579ULL
#define XXH_PRIME64_5  2870177450012600261ULL

static inline uint64_t xxh_read64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static inline uint32_t xxh_read32(const uint8_t *p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (uint32_t)p[i] << (i * 8);
    return v;
}

static inline uint64_t xxh_rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline uint64_t xxh_round(uint64_t acc, uint64_t input) {
    acc += input * XXH_PRIME64_2;
    acc = xxh_rotl64(acc, 31);
    acc *= XXH_PRIME64_1;
    return acc;
}

static inline uint64_t xxh_merge_round(uint64_t acc, uint64_t val) {
    val = xxh_round(0, val);
    acc ^= val;
    acc = acc * XXH_PRIME64_1 + XXH_PRIME64_4;
    return acc;
}

uint64_t neverc_xxhash64(const void *data, size_t len, uint64_t seed) {
    if (!data) len = 0;
    const uint8_t *p = (const uint8_t *)data;
    size_t remaining = len;
    uint64_t h64;

    if (remaining >= 32) {
        uint64_t v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = seed + XXH_PRIME64_2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - XXH_PRIME64_1;

        do {
            v1 = xxh_round(v1, xxh_read64(p)); p += 8;
            v2 = xxh_round(v2, xxh_read64(p)); p += 8;
            v3 = xxh_round(v3, xxh_read64(p)); p += 8;
            v4 = xxh_round(v4, xxh_read64(p)); p += 8;
            remaining -= 32;
        } while (remaining >= 32);

        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) +
              xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);
        h64 = xxh_merge_round(h64, v1);
        h64 = xxh_merge_round(h64, v2);
        h64 = xxh_merge_round(h64, v3);
        h64 = xxh_merge_round(h64, v4);
    } else {
        h64 = seed + XXH_PRIME64_5;
    }

    h64 += (uint64_t)len;

    while (remaining >= 8) {
        uint64_t k1 = xxh_round(0, xxh_read64(p));
        p += 8;
        remaining -= 8;
        h64 ^= k1;
        h64 = xxh_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
    }

    if (remaining >= 4) {
        h64 ^= (uint64_t)xxh_read32(p) * XXH_PRIME64_1;
        p += 4;
        remaining -= 4;
        h64 = xxh_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
    }

    while (remaining != 0) {
        h64 ^= (uint64_t)(*p) * XXH_PRIME64_5;
        p++;
        remaining--;
        h64 = xxh_rotl64(h64, 11) * XXH_PRIME64_1;
    }

    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}
