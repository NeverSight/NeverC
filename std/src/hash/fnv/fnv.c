#include "neverc/std/hash/fnv.h"

#define FNV_PRIME_32 UINT32_C(16777619)
#define FNV_PRIME_64 UINT64_C(1099511628211)

static const uint8_t *fnv_bytes(const void *data, size_t *len) {
    if (!data) {
        *len = 0;
        return (const uint8_t *)"";
    }
    return (const uint8_t *)data;
}

uint32_t neverc_fnv_update32(uint32_t hash, const void *data, size_t len) {
    const uint8_t *p = fnv_bytes(data, &len);
    size_t i = 0;
    for (; i < len && len - i >= 8; i += 8) {
        hash *= FNV_PRIME_32; hash ^= p[i  ];
        hash *= FNV_PRIME_32; hash ^= p[i+1];
        hash *= FNV_PRIME_32; hash ^= p[i+2];
        hash *= FNV_PRIME_32; hash ^= p[i+3];
        hash *= FNV_PRIME_32; hash ^= p[i+4];
        hash *= FNV_PRIME_32; hash ^= p[i+5];
        hash *= FNV_PRIME_32; hash ^= p[i+6];
        hash *= FNV_PRIME_32; hash ^= p[i+7];
    }
    for (; i < len; i++) {
        hash *= FNV_PRIME_32;
        hash ^= p[i];
    }
    return hash;
}

uint32_t neverc_fnv_update32a(uint32_t hash, const void *data, size_t len) {
    const uint8_t *p = fnv_bytes(data, &len);
    size_t i = 0;
    for (; i < len && len - i >= 8; i += 8) {
        hash ^= p[i  ]; hash *= FNV_PRIME_32;
        hash ^= p[i+1]; hash *= FNV_PRIME_32;
        hash ^= p[i+2]; hash *= FNV_PRIME_32;
        hash ^= p[i+3]; hash *= FNV_PRIME_32;
        hash ^= p[i+4]; hash *= FNV_PRIME_32;
        hash ^= p[i+5]; hash *= FNV_PRIME_32;
        hash ^= p[i+6]; hash *= FNV_PRIME_32;
        hash ^= p[i+7]; hash *= FNV_PRIME_32;
    }
    for (; i < len; i++) {
        hash ^= p[i];
        hash *= FNV_PRIME_32;
    }
    return hash;
}

uint64_t neverc_fnv_update64(uint64_t hash, const void *data, size_t len) {
    const uint8_t *p = fnv_bytes(data, &len);
    size_t i = 0;
    for (; i < len && len - i >= 8; i += 8) {
        hash *= FNV_PRIME_64; hash ^= p[i  ];
        hash *= FNV_PRIME_64; hash ^= p[i+1];
        hash *= FNV_PRIME_64; hash ^= p[i+2];
        hash *= FNV_PRIME_64; hash ^= p[i+3];
        hash *= FNV_PRIME_64; hash ^= p[i+4];
        hash *= FNV_PRIME_64; hash ^= p[i+5];
        hash *= FNV_PRIME_64; hash ^= p[i+6];
        hash *= FNV_PRIME_64; hash ^= p[i+7];
    }
    for (; i < len; i++) {
        hash *= FNV_PRIME_64;
        hash ^= p[i];
    }
    return hash;
}

uint64_t neverc_fnv_update64a(uint64_t hash, const void *data, size_t len) {
    const uint8_t *p = fnv_bytes(data, &len);
    size_t i = 0;
    for (; i < len && len - i >= 8; i += 8) {
        hash ^= p[i  ]; hash *= FNV_PRIME_64;
        hash ^= p[i+1]; hash *= FNV_PRIME_64;
        hash ^= p[i+2]; hash *= FNV_PRIME_64;
        hash ^= p[i+3]; hash *= FNV_PRIME_64;
        hash ^= p[i+4]; hash *= FNV_PRIME_64;
        hash ^= p[i+5]; hash *= FNV_PRIME_64;
        hash ^= p[i+6]; hash *= FNV_PRIME_64;
        hash ^= p[i+7]; hash *= FNV_PRIME_64;
    }
    for (; i < len; i++) {
        hash ^= p[i];
        hash *= FNV_PRIME_64;
    }
    return hash;
}

/*
 * 128-bit FNV — uses 128-bit state stored as (hi, lo) pair.
 * The prime is 2^88 + 2^8 + 0x3b = a 128-bit number where only the
 * lower word matters for the multiply: prime128Lower = 0x13b,
 * and the upper contribution is s[1] << 24.
 */
#define FNV_PRIME_128_LO UINT64_C(0x13b)
#define FNV_PRIME_128_SHIFT 24

static inline void mul128(uint64_t *hi, uint64_t *lo) {
    uint64_t s0, s1;
#if defined(__SIZEOF_INT128__)
    __uint128_t prod = (__uint128_t)FNV_PRIME_128_LO * (*lo);
    s1 = (uint64_t)prod;
    s0 = (uint64_t)(prod >> 64);
#else
    uint64_t a = FNV_PRIME_128_LO;
    uint64_t b = *lo;
    uint64_t a_lo = a & 0xFFFFFFFFULL, a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFFULL, b_hi = b >> 32;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t mid = p1 + (p0 >> 32);
    mid += p2;
    if (mid < p2) p3 += 0x100000000ULL;
    s0 = p3 + (mid >> 32);
    s1 = (mid << 32) | (p0 & 0xFFFFFFFFULL);
#endif
    s0 += (*lo << FNV_PRIME_128_SHIFT) + FNV_PRIME_128_LO * (*hi);
    *lo = s1;
    *hi = s0;
}

neverc_fnv_128_t neverc_fnv_update128(neverc_fnv_128_t hash,
                                      const void *data, size_t len) {
    const uint8_t *p = fnv_bytes(data, &len);
    uint64_t hi = hash.hi, lo = hash.lo;
    size_t i = 0;
    for (; i < len && len - i >= 8; i += 8) {
        mul128(&hi, &lo); lo ^= p[i  ];
        mul128(&hi, &lo); lo ^= p[i+1];
        mul128(&hi, &lo); lo ^= p[i+2];
        mul128(&hi, &lo); lo ^= p[i+3];
        mul128(&hi, &lo); lo ^= p[i+4];
        mul128(&hi, &lo); lo ^= p[i+5];
        mul128(&hi, &lo); lo ^= p[i+6];
        mul128(&hi, &lo); lo ^= p[i+7];
    }
    for (; i < len; i++) {
        mul128(&hi, &lo);
        lo ^= p[i];
    }
    return (neverc_fnv_128_t){hi, lo};
}

neverc_fnv_128_t neverc_fnv_update128a(neverc_fnv_128_t hash,
                                       const void *data, size_t len) {
    const uint8_t *p = fnv_bytes(data, &len);
    uint64_t hi = hash.hi, lo = hash.lo;
    size_t i = 0;
    for (; i < len && len - i >= 8; i += 8) {
        lo ^= p[i  ]; mul128(&hi, &lo);
        lo ^= p[i+1]; mul128(&hi, &lo);
        lo ^= p[i+2]; mul128(&hi, &lo);
        lo ^= p[i+3]; mul128(&hi, &lo);
        lo ^= p[i+4]; mul128(&hi, &lo);
        lo ^= p[i+5]; mul128(&hi, &lo);
        lo ^= p[i+6]; mul128(&hi, &lo);
        lo ^= p[i+7]; mul128(&hi, &lo);
    }
    for (; i < len; i++) {
        lo ^= p[i];
        mul128(&hi, &lo);
    }
    return (neverc_fnv_128_t){hi, lo};
}

uint32_t neverc_fnv_sum32(const void *data, size_t len) {
    return neverc_fnv_update32(NEVERC_FNV32_OFFSET_BASIS, data, len);
}

uint32_t neverc_fnv_sum32a(const void *data, size_t len) {
    return neverc_fnv_update32a(NEVERC_FNV32_OFFSET_BASIS, data, len);
}

uint64_t neverc_fnv_sum64(const void *data, size_t len) {
    return neverc_fnv_update64(NEVERC_FNV64_OFFSET_BASIS, data, len);
}

uint64_t neverc_fnv_sum64a(const void *data, size_t len) {
    return neverc_fnv_update64a(NEVERC_FNV64_OFFSET_BASIS, data, len);
}

neverc_fnv_128_t neverc_fnv_sum128(const void *data, size_t len) {
    neverc_fnv_128_t hash = {
        NEVERC_FNV128_OFFSET_BASIS_HI,
        NEVERC_FNV128_OFFSET_BASIS_LO
    };
    return neverc_fnv_update128(hash, data, len);
}

neverc_fnv_128_t neverc_fnv_sum128a(const void *data, size_t len) {
    neverc_fnv_128_t hash = {
        NEVERC_FNV128_OFFSET_BASIS_HI,
        NEVERC_FNV128_OFFSET_BASIS_LO
    };
    return neverc_fnv_update128a(hash, data, len);
}
