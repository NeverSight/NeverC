#include "neverc/rand.h"

/*
 * xoshiro256** PRNG — fast, high quality, period 2^256-1.
 * Based on https://prng.di.unimi.it/xoshiro256starstar.c
 * by David Blackman and Sebastiano Vigna (public domain).
 */

static uint64_t s[4] = {
    0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
    0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
};

static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t next(void) {
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

/* SplitMix64 for seeding */
static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

void neverc_rand_seed(uint64_t seed) {
    uint64_t sm = seed;
    s[0] = splitmix64(&sm);
    s[1] = splitmix64(&sm);
    s[2] = splitmix64(&sm);
    s[3] = splitmix64(&sm);
}

uint64_t neverc_rand_uint64(void) {
    return next();
}

uint32_t neverc_rand_uint32(void) {
    return (uint32_t)(next() >> 32);
}

int64_t neverc_rand_int63(void) {
    return (int64_t)(next() >> 1);
}

int64_t neverc_rand_intn(int64_t n) {
    if (n <= 0) return 0;
    uint64_t un = (uint64_t)n;
    if ((un & (un - 1)) == 0)
        return (int64_t)((next() >> 1) & (un - 1));
    uint64_t max = (uint64_t)((1ULL << 63) - 1 - (1ULL << 63) % un);
    uint64_t v = next() >> 1;
    while (v > max)
        v = next() >> 1;
    return (int64_t)(v % un);
}

double neverc_rand_float64(void) {
    return (double)(next() >> 11) * 0x1.0p-53;
}

float neverc_rand_float32(void) {
    return (float)(next() >> 40) * 0x1.0p-24f;
}

void neverc_rand_shuffle(int n, void (*swap)(int i, int j)) {
    for (int i = n - 1; i > 0; i--) {
        /* Rejection sampling to eliminate modulo bias, consistent with intn */
        uint64_t bound = (uint64_t)(i + 1);
        uint64_t max = (uint64_t)((1ULL << 63) - 1 - (1ULL << 63) % bound);
        uint64_t v = next() >> 1;
        while (v > max)
            v = next() >> 1;
        swap(i, (int)(v % bound));
    }
}
