#include "neverc/std/crypto/rand.h"
#include "neverc/std/_platform.h"
#include <string.h>

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
#if defined(_WIN32)
    uint64_t result = 0;
    a %= m;
    while (b > 0) {
        if (b & 1) { result = result > m - a ? result - (m - a) : result + a; }
        a = a > m - a ? a - (m - a) : a + a;
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

    uint64_t witnesses[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (int i = 0; i < 12; i++) {
        if (witnesses[i] >= n) continue;
        if (!miller_rabin_small(n, witnesses[i])) return 0;
    }
    return 1;
}

int neverc_crypto_rand_prime(uint8_t *out, size_t bits) {
    if (!out || bits < 2 || bits > 64) return -1;
    size_t bytes = (bits + 7) / 8;

    for (int attempts = 0; attempts < 10000; attempts++) {
        uint64_t val = 0;
        if (neverc_crypto_rand_read((uint8_t *)&val, bytes) != 0) return -1;

        val |= (1ULL << (bits - 1));
        val |= 1;
        if (bits < 64) val &= (1ULL << bits) - 1;

        if (is_probably_prime(val)) {
            for (size_t i = 0; i < bytes; i++)
                out[i] = (uint8_t)(val >> (i * 8));
            return 0;
        }
    }
    return -1;
}
