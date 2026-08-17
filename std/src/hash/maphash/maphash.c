/*
 * NeverC hash/maphash — fast non-cryptographic hash for hash tables.
 * Mirrors Go hash/maphash.
 *
 * Uses wyhash v4 internally — passes SMHasher, excellent distribution,
 * and very fast on modern hardware.
 */

#include "neverc/std/hash/maphash.h"
#include "neverc/std/_platform.h"
#include <string.h>

/* ---- wyhash core ---- */

/* Little-endian loads so hashes match across host endianness. */
static uint64_t wy_read8(const uint8_t *p) {
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32)
         | ((uint64_t)p[5] << 40)
         | ((uint64_t)p[6] << 48)
         | ((uint64_t)p[7] << 56);
}

static uint64_t wy_read4(const uint8_t *p) {
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24);
}

static uint64_t wy_mum(uint64_t a, uint64_t b) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)a * b;
    a = (uint64_t)r;
    b = (uint64_t)(r >> 64);
#else
    uint64_t ha = a >> 32, la = (uint32_t)a;
    uint64_t hb = b >> 32, lb = (uint32_t)b;
    uint64_t rh = ha * hb, rl = la * lb;
    uint64_t rm0 = ha * lb, rm1 = hb * la;
    uint64_t t = rl + (rm0 << 32);
    uint64_t c = t < rl;
    uint64_t lo = t + (rm1 << 32);
    c += lo < t;
    uint64_t hi = rh + (rm0 >> 32) + (rm1 >> 32) + c;
    a = lo;
    b = hi;
#endif
    return a ^ b;
}

static uint64_t wy_mix(uint64_t a, uint64_t b) {
    return wy_mum(a, b);
}

static const uint64_t WY_P0 = 0xa0761d6478bd642full;
static const uint64_t WY_P1 = 0xe7037ed1a0b428dbull;
static const uint64_t WY_P2 = 0x8ebc6af09c88c6e3ull;
static const uint64_t WY_P3 = 0x589965cc75374cc3ull;

static uint64_t wyhash(const void *key, size_t len, uint64_t seed) {
    const uint8_t *p = (const uint8_t *)key;
    uint64_t a, b;
    seed ^= WY_P0;

    if (len <= 16) {
        if (len >= 4) {
            a = (wy_read4(p) << 32) | wy_read4(p + ((len >> 3) << 2));
            b = (wy_read4(p + len - 4) << 32) |
                wy_read4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) |
                (uint64_t)p[len - 1];
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t i = len;
        if (i > 48) {
            uint64_t s1 = seed, s2 = seed;
            do {
                seed = wy_mix(wy_read8(p) ^ WY_P1, wy_read8(p + 8) ^ seed);
                s1 = wy_mix(wy_read8(p + 16) ^ WY_P2, wy_read8(p + 24) ^ s1);
                s2 = wy_mix(wy_read8(p + 32) ^ WY_P3, wy_read8(p + 40) ^ s2);
                p += 48;
                i -= 48;
            } while (i > 48);
            seed ^= s1 ^ s2;
        }
        while (i > 16) {
            seed = wy_mix(wy_read8(p) ^ WY_P1, wy_read8(p + 8) ^ seed);
            i -= 16;
            p += 16;
        }
        a = wy_read8(p + i - 16);
        b = wy_read8(p + i - 8);
    }
    return wy_mix(WY_P1 ^ len, wy_mix(a ^ WY_P1, b ^ seed));
}

/* ---- seed generation (LCG, not crypto-secure) ---- */

static uint64_t g_rng_state = 0;

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

uint64_t neverc_maphash_make_seed(void) {
    uint64_t state = __atomic_load_n(&g_rng_state, __ATOMIC_RELAXED);
    if (state == 0) {
        uint64_t v = 0;
        if (neverc_platform_random((unsigned char *)&v, sizeof(v)) != 0 || v == 0) {
            v = (uint64_t)(uintptr_t)&g_rng_state;
            v ^= 0xdeadbeefcafebabeull;
            if (v == 0) v = 1;
        }
        uint64_t expected = 0;
        if (!__atomic_compare_exchange_n(&g_rng_state, &expected, v, 0,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            state = expected;
        else
            state = v;
    }
    uint64_t s;
    for (;;) {
        uint64_t cur = __atomic_load_n(&g_rng_state, __ATOMIC_RELAXED);
        uint64_t next = cur;
        s = splitmix64(&next);
        if (__atomic_compare_exchange_n(&g_rng_state, &cur, next, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            if (s != 0)
                return s;
        }
    }
}

/* ---- streaming API ---- */

void neverc_maphash_init(neverc_maphash_t *h, uint64_t seed) {
    if (!h) return;
    h->seed = seed;
    h->state = h->seed;
    h->n = 0;
    h->used = 0;
}

void neverc_maphash_reset(neverc_maphash_t *h) {
    if (!h) return;
    h->state = h->seed;
    h->n = 0;
    h->used = 0;
}

static void maphash_flush(neverc_maphash_t *h) {
    h->state = wyhash(h->buf, (size_t)h->n, h->state);
    h->n = 0;
}

size_t neverc_maphash_write_byte(neverc_maphash_t *h, uint8_t b) {
    if (!h) return 0;
    if (h->n == NEVERC_MAPHASH_BUF_SIZE) maphash_flush(h);
    h->buf[h->n++] = b;
    h->used = 1;
    return 1;
}

size_t neverc_maphash_write(neverc_maphash_t *h, const void *data, size_t len) {
    if (!h) return 0;
    if (len == 0) return 0;
    if (!data) return 0;
    const uint8_t *p = (const uint8_t *)data;
    size_t remaining = len;
    h->used = 1;

    if (h->n > 0) {
        size_t space = (size_t)(NEVERC_MAPHASH_BUF_SIZE - h->n);
        size_t k = remaining < space ? remaining : space;
        memcpy(h->buf + h->n, p, k);
        h->n += (int)k;
        if (h->n < NEVERC_MAPHASH_BUF_SIZE) return len;
        p += k;
        remaining -= k;
        maphash_flush(h);
    }

    while (remaining > NEVERC_MAPHASH_BUF_SIZE) {
        h->state = wyhash(p, NEVERC_MAPHASH_BUF_SIZE, h->state);
        p += NEVERC_MAPHASH_BUF_SIZE;
        remaining -= NEVERC_MAPHASH_BUF_SIZE;
    }
    memcpy(h->buf, p, remaining);
    h->n = (int)remaining;
    return len;
}

size_t neverc_maphash_write_string(neverc_maphash_t *h, const char *s) {
    if (!s) return 0;
    return neverc_maphash_write(h, s, strlen(s));
}

uint64_t neverc_maphash_sum64(const neverc_maphash_t *h) {
    if (!h) return 0;
    /* Empty input still mixes the seed so the digest does not leak it.
     * After a full-buffer flush, n==0 but used==1: state already holds the mix. */
    if (!h->used)
        return wyhash(NULL, 0, h->seed);
    if (h->n == 0) return h->state;
    return wyhash(h->buf, (size_t)h->n, h->state);
}

/* ---- one-shot convenience ---- */

uint64_t neverc_maphash_bytes(uint64_t seed, const void *data, size_t len) {
    if (!data) len = 0;
    const uint8_t *p = (const uint8_t *)data;
    uint64_t state = seed;
    while (len > NEVERC_MAPHASH_BUF_SIZE) {
        state = wyhash(p, NEVERC_MAPHASH_BUF_SIZE, state);
        p += NEVERC_MAPHASH_BUF_SIZE;
        len -= NEVERC_MAPHASH_BUF_SIZE;
    }
    return wyhash(p, len, state);
}

uint64_t neverc_maphash_string(uint64_t seed, const char *s) {
    return neverc_maphash_bytes(seed, s, s ? strlen(s) : 0);
}
