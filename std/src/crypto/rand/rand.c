#include "neverc/std/crypto/rand.h"
#include "neverc/std/_platform.h"

int neverc_crypto_rand_read(uint8_t *buf, size_t len) {
    return neverc_platform_random(buf, len);
}

int neverc_crypto_rand_int(uint64_t *out, uint64_t max) {
    if (!out || max == 0) return -1;
    uint64_t threshold = -max % max;
    for (;;) {
        uint64_t val;
        if (neverc_crypto_rand_read((uint8_t *)&val, sizeof(val)) != 0)
            return -1;
        if (val >= threshold) {
            *out = val % max;
            return 0;
        }
    }
}

static uint64_t mulmod64(uint64_t a, uint64_t b, uint64_t m) {
#if !defined(__SIZEOF_INT128__)
    uint64_t result = 0;
    a %= m;
    while (b > 0) {
        if (b & 1) {
            result = result >= m - a ? result - (m - a) : result + a;
        }
        a = a >= m - a ? a - (m - a) : a + a;
        b >>= 1;
    }
    return result;
#else
    return (uint64_t)((__uint128_t)a * b % m);
#endif
}

static int miller_rabin_small(uint64_t n, uint64_t a) {
    if (n < 2) return 0;
    if (n == 2 || n == 3) return 1;
    if (n % 2 == 0) return 0;

    uint64_t d = n - 1;
    int r = 0;
    while ((d & 1) == 0) { d >>= 1; r++; }

    uint64_t xval = 1;
    uint64_t base = a % n;
    uint64_t exp = d;
    while (exp > 0) {
        if (exp & 1) xval = mulmod64(xval, base, n);
        base = mulmod64(base, base, n);
        exp >>= 1;
    }

    if (xval == 1 || xval == n - 1) return 1;
    for (int i = 0; i < r - 1; i++) {
        xval = mulmod64(xval, xval, n);
        if (xval == n - 1) return 1;
    }
    return 0;
}

static int is_probably_prime(uint64_t n) {
    if (n < 2) return 0;
    if (n == 2 || n == 3 || n == 5) return 1;
    if (n % 2 == 0 || n % 3 == 0 || n % 5 == 0) return 0;

    /* This seven-base set is deterministic for every unsigned 64-bit input. */
    static const uint64_t witnesses[] = {
        2, 325, 9375, 28178, 450775, 9780504, 1795265022
    };
    for (size_t i = 0; i < sizeof(witnesses) / sizeof(witnesses[0]); i++) {
        if (witnesses[i] % n == 0) continue;
        if (!miller_rabin_small(n, witnesses[i])) return 0;
    }
    return 1;
}

int neverc_crypto_rand_prime(uint8_t *out, size_t bits) {
    if (!out || bits < 2 || bits > 64) return -1;
    size_t bytes = (bits + 7) / 8;

    for (int attempts = 0; attempts < 10000; attempts++) {
        uint64_t val = 0;
        uint8_t random_bytes[8] = {0};
        if (neverc_crypto_rand_read(random_bytes, bytes) != 0) {
            neverc_platform_secure_zero(
                random_bytes, sizeof(random_bytes));
            return -1;
        }
        for (size_t i = 0; i < bytes; i++)
            val |= (uint64_t)random_bytes[i] << (8 * i);
        neverc_platform_secure_zero(random_bytes, sizeof(random_bytes));

        val |= (1ULL << (bits - 1));
        val |= 1;
        if (bits < 64) val &= (1ULL << bits) - 1;

        if (is_probably_prime(val)) {
            for (size_t i = 0; i < bytes; i++)
                out[i] = (uint8_t)(val >> (i * 8));
            neverc_platform_secure_zero(&val, sizeof(val));
            return 0;
        }
        neverc_platform_secure_zero(&val, sizeof(val));
    }
    return -1;
}
