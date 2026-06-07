#include "neverc/hash/fnv.h"

/*
 * FNV-1 and FNV-1a hash functions.
 * See https://en.wikipedia.org/wiki/Fowler-Noll-Vo_hash_function
 */

#define FNV_OFFSET_32 2166136261U
#define FNV_PRIME_32  16777619U
#define FNV_OFFSET_64 14695981039346656037ULL
#define FNV_PRIME_64  1099511628211ULL

uint32_t neverc_fnv_32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = FNV_OFFSET_32;
    for (size_t i = 0; i < len; i++) {
        hash *= FNV_PRIME_32;
        hash ^= p[i];
    }
    return hash;
}

uint32_t neverc_fnv_32a(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = FNV_OFFSET_32;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= FNV_PRIME_32;
    }
    return hash;
}

uint64_t neverc_fnv_64(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t hash = FNV_OFFSET_64;
    for (size_t i = 0; i < len; i++) {
        hash *= FNV_PRIME_64;
        hash ^= p[i];
    }
    return hash;
}

uint64_t neverc_fnv_64a(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t hash = FNV_OFFSET_64;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= FNV_PRIME_64;
    }
    return hash;
}
