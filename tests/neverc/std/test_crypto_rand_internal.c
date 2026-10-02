#include "neverc/std/crypto/rand.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the platform-specific modular multiplication, the deterministic
 * 64-bit primality helpers and the multi-word tests behind wider primes
 * directly. This test is linked without rand.c because the implementation is
 * included here. */
#include "../../../std/src/crypto/rand/rand.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* Odd composites below SMALL_LIMIT that pass each half of the Baillie-PSW
 * test: strong pseudoprimes to base 2, and extra strong Lucas pseudoprimes
 * for the least P >= 3 with Jacobi(P^2 - 4, n) = -1.  No number is in both. */
#define SMALL_LIMIT 100000u
static const uint32_t strong_base2_pseudoprimes[] = {
    2047, 3277, 4033, 4681, 8321, 15841, 29341, 42799, 49141, 52633, 65281,
    74665, 80581, 85489, 88357, 90751
};
static const uint32_t lucas_pseudoprimes[] = {
    989, 3239, 5777, 10877, 27971, 29681, 30739, 31631, 39059, 72389, 73919,
    75077
};

static int listed(const uint32_t *list, size_t len, uint32_t n) {
    for (size_t i = 0; i < len; i++)
        if (list[i] == n) return 1;
    return 0;
}

static unsigned char small_composite[SMALL_LIMIT];

static void sieve_small(void) {
    small_composite[0] = small_composite[1] = 1;
    for (uint32_t i = 2; i * i < SMALL_LIMIT; i++)
        if (!small_composite[i])
            for (uint32_t j = i * i; j < SMALL_LIMIT; j += i)
                small_composite[j] = 1;
}

static uint32_t *bind_context(prime_mw_t *c, size_t k) {
    uint32_t *work = (uint32_t *)calloc(prime_mw_work_words(k),
                                        sizeof(uint32_t));
    if (work) prime_mw_bind(c, work, k);
    return work;
}

/* Every odd n below SMALL_LIMIT, held in k words (upper words zero), must
 * pass each half exactly when it is prime or listed for that half. */
static int check_small_range(size_t k) {
    prime_mw_t c;
    uint32_t *work = bind_context(&c, k);
    CHECK(work != NULL);
    size_t n_spsp = sizeof(strong_base2_pseudoprimes) /
                    sizeof(strong_base2_pseudoprimes[0]);
    size_t n_lucas = sizeof(lucas_pseudoprimes) / sizeof(lucas_pseudoprimes[0]);
    for (uint32_t n = 3; n < SMALL_LIMIT; n += 2) {
        memset(c.n, 0, k * sizeof(*c.n));
        c.n[0] = n;
        prime_mw_setup(&c);
        int prime = !small_composite[n];
        int base2 = prime_mw_strong_base2(&c);
        int lucas = prime_mw_lucas(&c);
        if (base2 != (prime || listed(strong_base2_pseudoprimes, n_spsp, n)) ||
            lucas != (prime || listed(lucas_pseudoprimes, n_lucas, n))) {
            fprintf(stderr, "k=%zu n=%u: base2=%d lucas=%d prime=%d\n",
                    k, n, base2, lucas, prime);
            free(work);
            return 1;
        }
    }
    free(work);
    return 0;
}

static int mersenne_exponent_is_prime(size_t p) {
    static const size_t exponents[] = {3, 5, 7, 13, 17, 19, 31, 61, 89, 107,
                                       127, 521, 607};
    for (size_t i = 0; i < sizeof(exponents) / sizeof(exponents[0]); i++)
        if (exponents[i] == p) return 1;
    return 0;
}

/* Loads 2^p - 1 into a fresh context sized for it. */
static uint32_t *load_mersenne(prime_mw_t *c, size_t p) {
    uint32_t *work = bind_context(c, (p + 31) / 32);
    if (!work) return NULL;
    memset(c->n, 0xff, c->k * sizeof(*c->n));
    mw_truncate(c->n, p, c->k);
    return work;
}

/* For prime p, 2^p - 1 is a strong probable prime to base 2 whether or not
 * it is prime, so only the Lucas half can reject the composite ones. */
static int check_mersenne(void) {
    for (size_t p = 3; p <= 607; p += 2) {
        int p_prime = 1;
        for (size_t f = 3; f * f <= p; f += 2)
            if (p % f == 0) p_prime = 0;
        if (!p_prime) continue;
        prime_mw_t c;
        uint32_t *work = load_mersenne(&c, p);
        CHECK(work != NULL);
        prime_mw_setup(&c);
        int base2 = prime_mw_strong_base2(&c);
        int lucas = prime_mw_lucas(&c);
        int expected = mersenne_exponent_is_prime(p);
        if (!base2 || lucas != expected) {
            fprintf(stderr, "M%zu: base2=%d lucas=%d\n", p, base2, lucas);
            free(work);
            return 1;
        }
        /* Above 64 bits, the full test agrees; 2^67 - 1 has no factor below
         * 1024, so the Lucas half alone must reject it. */
        if (p > 64 && (expected || p == 67)) {
            int full = prime_mw_probably_prime(&c);
            if (full != expected) {
                fprintf(stderr, "M%zu: probably_prime=%d\n", p, full);
                free(work);
                return 1;
            }
        }
        free(work);
    }
    return 0;
}

/* Legendre symbol by Euler's criterion, for an odd prime q. */
static int legendre(uint32_t a, uint32_t q) {
    uint64_t base = a % q, r = 1;
    if (base == 0) return 0;
    for (uint32_t e = (q - 1) / 2; e; e >>= 1) {
        if (e & 1) r = r * base % q;
        base = base * base % q;
    }
    return r == 1 ? 1 : -1;
}

/* The Jacobi symbol as the product of Legendre symbols over the factors
 * of n, independent of the reciprocity-based implementation. */
static int check_jacobi(size_t k) {
    prime_mw_t c;
    uint32_t *work = bind_context(&c, k);
    CHECK(work != NULL);
    for (uint32_t n = 3; n < 3000; n += 2) {
        memset(c.n, 0, k * sizeof(*c.n));
        c.n[0] = n;
        for (uint32_t a = 1; a < 200; a++) {
            int expected = 1;
            uint32_t rest = n;
            for (uint32_t q = 3; rest > 1; q += 2)
                while (rest % q == 0) {
                    expected *= legendre(a, q);
                    rest /= q;
                }
            if (prime_mw_jacobi(&c, a) != expected) {
                fprintf(stderr, "Jacobi(%u, %u) != %d\n", a, n, expected);
                free(work);
                return 1;
            }
        }
    }
    free(work);
    return 0;
}

/* r (ka + kb words) = a * b. */
static void mul_words(uint32_t *r, const uint32_t *a, size_t ka,
                      const uint32_t *b, size_t kb) {
    memset(r, 0, (ka + kb) * sizeof(*r));
    for (size_t i = 0; i < ka; i++)
        for (size_t j = 0; j < kb; j++) {
            uint64_t carry = (uint64_t)a[i] * b[j];
            for (size_t m = i + j; carry; m++) {
                carry += r[m];
                r[m] = (uint32_t)carry;
                carry >>= 32;
            }
        }
}

/* 2^(32 * k) - delta: full-width moduli exercise the carry out of the top
 * word that narrower values never produce. */
static void set_below_power(uint32_t *n, size_t k, uint32_t delta) {
    memset(n, 0xff, k * sizeof(*n));
    n[0] = 0u - delta;
}

static int check_full_width(void) {
    /* The largest primes below 2^64, 2^96, 2^128 and 2^192. */
    static const struct { size_t k; uint32_t delta; } primes[] = {
        {2, 59}, {3, 17}, {4, 159}, {6, 237}
    };
    for (size_t i = 0; i < sizeof(primes) / sizeof(primes[0]); i++) {
        prime_mw_t c;
        uint32_t *work = bind_context(&c, primes[i].k);
        CHECK(work != NULL);
        set_below_power(c.n, primes[i].k, primes[i].delta);
        prime_mw_setup(&c);
        CHECK(prime_mw_strong_base2(&c) == 1);
        CHECK(prime_mw_lucas(&c) == 1);
        CHECK(prime_mw_strong_random(&c, PRIME_MW_RANDOM_ROUNDS) == 1);
        CHECK(prime_mw_probably_prime(&c) == 1);
        free(work);
    }

    /* (2^64 - 59)(2^64 - 83): composite, full width, no small factors. */
    uint32_t p[2], q[2];
    set_below_power(p, 2, 59);
    set_below_power(q, 2, 83);
    prime_mw_t c;
    uint32_t *work = bind_context(&c, 4);
    CHECK(work != NULL);
    mul_words(c.n, p, 2, q, 2);
    CHECK(mw_bit_len(c.n, 4) == 128);
    prime_mw_setup(&c);
    CHECK(prime_mw_strong_base2(&c) == 0);
    CHECK(prime_mw_lucas(&c) == 0);
    /* A composite survives a random round with probability below 1/4. */
    CHECK(prime_mw_strong_random(&c, PRIME_MW_RANDOM_ROUNDS) == 0);
    CHECK(prime_mw_probably_prime(&c) == 0);
    free(work);
    return 0;
}

int main(void) {
    CHECK(mulmod64(2, 3, 3) == 0);
    CHECK(mulmod64(UINT64_MAX - 1, UINT64_MAX - 1, UINT64_MAX) == 1);

    CHECK(is_probably_prime(2));
    CHECK(is_probably_prime(UINT64_C(18446744073709551557)));
    CHECK(!is_probably_prime(UINT64_C(341550071728321)));
    CHECK(!is_probably_prime(UINT64_MAX));

    sieve_small();

    /* The trial-division table is exactly the odd primes below 1024. */
    size_t table_len = sizeof(prime_small_odd) / sizeof(prime_small_odd[0]);
    size_t expected_len = 0;
    for (uint32_t n = 3; n < 1024; n += 2) {
        if (small_composite[n]) continue;
        CHECK(expected_len < table_len);
        CHECK(prime_small_odd[expected_len] == n);
        expected_len++;
    }
    CHECK(expected_len == table_len);

    /* One word, and zero-padded multi-word forms of the same values. */
    CHECK(check_small_range(1) == 0);
    CHECK(check_small_range(2) == 0);
    CHECK(check_small_range(3) == 0);

    CHECK(check_jacobi(1) == 0);
    CHECK(check_jacobi(2) == 0);

    CHECK(check_mersenne() == 0);
    CHECK(check_full_width() == 0);

    /* A perfect square never yields Jacobi(P^2 - 4, n) = -1.  The square
     * of the base-2 Wieferich prime 1093 is a strong pseudoprime to base 2,
     * so here too only the Lucas half rejects it. */
    prime_mw_t c;
    uint32_t *wieferich = bind_context(&c, 1);
    CHECK(wieferich != NULL);
    c.n[0] = 1093u * 1093u;
    prime_mw_setup(&c);
    CHECK(prime_mw_strong_base2(&c) == 1);
    CHECK(prime_mw_lucas(&c) == 0);
    free(wieferich);

    uint32_t m89[3];
    memset(m89, 0xff, sizeof(m89));
    mw_truncate(m89, 89, 3);
    uint32_t *square = bind_context(&c, 6);
    CHECK(square != NULL);
    mul_words(c.n, m89, 3, m89, 3);
    prime_mw_setup(&c);
    CHECK(mw_bit_len(c.n, 6) == 178);
    CHECK(prime_mw_lucas(&c) == 0);
    free(square);

    puts("passed");
    return 0;
}
