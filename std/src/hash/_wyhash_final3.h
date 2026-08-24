/*
 * Private wyhash final-v3 core shared by std hash-table implementations.
 *
 * This is the canonical final-v3 algorithm from wangyi-fudan/wyhash's wyhash.h
 * (reference commit 261d102491d7c8bd77121ea5780e6d313bbc981f).  Loads are
 * always little-endian so the result is independent of host endianness. Callers
 * must pass a non-NULL key when len is non-zero; NULL is valid for empty input.
 *
 * Kept under std/src rather than std/include: this is an implementation detail,
 * not part of NeverC's public API.
 */
#ifndef NEVERC_STD_SRC_HASH_WYHASH_FINAL3_H
#define NEVERC_STD_SRC_HASH_WYHASH_FINAL3_H

#include <stddef.h>
#include <stdint.h>

#define NCI_WYHASH_P0 0xa0761d6478bd642fULL
#define NCI_WYHASH_P1 0xe7037ed1a0b428dbULL
#define NCI_WYHASH_P2 0x8ebc6af09c88c6e3ULL
#define NCI_WYHASH_P3 0x589965cc75374cc3ULL

static inline uint64_t nci_wyhash_read8(const uint8_t *p) {
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32)
         | ((uint64_t)p[5] << 40)
         | ((uint64_t)p[6] << 48)
         | ((uint64_t)p[7] << 56);
}

static inline uint64_t nci_wyhash_read4(const uint8_t *p) {
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24);
}

static inline uint64_t nci_wyhash_mix(uint64_t a, uint64_t b) {
#ifdef __SIZEOF_INT128__
    __uint128_t product = (__uint128_t)a * b;
    return (uint64_t)product ^ (uint64_t)(product >> 64);
#else
    uint64_t ha = a >> 32, la = (uint32_t)a;
    uint64_t hb = b >> 32, lb = (uint32_t)b;
    uint64_t rh = ha * hb, rl = la * lb;
    uint64_t rm0 = ha * lb, rm1 = hb * la;
    uint64_t t = rl + (rm0 << 32);
    uint64_t carry = t < rl;
    uint64_t lo = t + (rm1 << 32);
    carry += lo < t;
    uint64_t hi = rh + (rm0 >> 32) + (rm1 >> 32) + carry;
    return lo ^ hi;
#endif
}

static inline uint64_t nci_wyhash_final3(const void *key, size_t len,
                                         uint64_t seed) {
    const uint8_t *p = (const uint8_t *)key;
    uint64_t a, b;
    seed ^= NCI_WYHASH_P0;

    if (len <= 16) {
        if (len >= 4) {
            a = (nci_wyhash_read4(p) << 32)
              | nci_wyhash_read4(p + ((len >> 3) << 2));
            b = (nci_wyhash_read4(p + len - 4) << 32)
              | nci_wyhash_read4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = ((uint64_t)p[0] << 16)
              | ((uint64_t)p[len >> 1] << 8)
              | (uint64_t)p[len - 1];
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t remaining = len;
        if (remaining > 48) {
            uint64_t seed1 = seed, seed2 = seed;
            do {
                seed = nci_wyhash_mix(nci_wyhash_read8(p) ^ NCI_WYHASH_P1,
                                      nci_wyhash_read8(p + 8) ^ seed);
                seed1 = nci_wyhash_mix(nci_wyhash_read8(p + 16) ^ NCI_WYHASH_P2,
                                       nci_wyhash_read8(p + 24) ^ seed1);
                seed2 = nci_wyhash_mix(nci_wyhash_read8(p + 32) ^ NCI_WYHASH_P3,
                                       nci_wyhash_read8(p + 40) ^ seed2);
                p += 48;
                remaining -= 48;
            } while (remaining > 48);
            seed ^= seed1 ^ seed2;
        }
        while (remaining > 16) {
            seed = nci_wyhash_mix(nci_wyhash_read8(p) ^ NCI_WYHASH_P1,
                                  nci_wyhash_read8(p + 8) ^ seed);
            p += 16;
            remaining -= 16;
        }
        a = nci_wyhash_read8(p + remaining - 16);
        b = nci_wyhash_read8(p + remaining - 8);
    }

    return nci_wyhash_mix(NCI_WYHASH_P1 ^ (uint64_t)len,
                          nci_wyhash_mix(a ^ NCI_WYHASH_P1, b ^ seed));
}

#undef NCI_WYHASH_P0
#undef NCI_WYHASH_P1
#undef NCI_WYHASH_P2
#undef NCI_WYHASH_P3

#endif /* NEVERC_STD_SRC_HASH_WYHASH_FINAL3_H */
