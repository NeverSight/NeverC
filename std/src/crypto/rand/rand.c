#include "neverc/std/crypto/rand.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>

#ifndef NCI_CRYPTO_RAND_RANDOM
#define NCI_CRYPTO_RAND_RANDOM neverc_platform_random
#endif
#ifndef NCI_CRYPTO_RAND_CALLOC
#define NCI_CRYPTO_RAND_CALLOC calloc
#endif

int neverc_crypto_rand_read(uint8_t *buf, size_t len) {
    if (!buf && len != 0) return -1;
    if (len == 0) return 0;
    int rc = NCI_CRYPTO_RAND_RANDOM(buf, len);
    if (rc != 0) {
        neverc_platform_secure_zero(buf, len);
        return -1;
    }
    return 0;
}

int neverc_crypto_rand_int(uint64_t *out, uint64_t max) {
    if (!out) return -1;
    if (max == 0) {
        *out = 0;
        return -1;
    }
    uint64_t threshold = -max % max;
    for (;;) {
        uint64_t val;
        if (neverc_crypto_rand_read((uint8_t *)&val, sizeof(val)) != 0) {
            *out = 0;
            return -1;
        }
        if (val >= threshold) {
            *out = val % max;
            return 0;
        }
    }
}

static uint64_t mulmod64(uint64_t a, uint64_t b, uint64_t m) {
#if defined(NEVERC_PLATFORM_WINDOWS) || !defined(__SIZEOF_INT128__)
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

/* ================================================================
 * Multi-word primality testing for primes wider than 64 bits.
 *
 * Numbers are arrays of k 32-bit words, least significant word first.
 * Arithmetic modulo the odd candidate n uses Montgomery multiplication
 * with R = 2^(32k), so values live in "Montgomery form" x*R mod n.
 * ================================================================ */

/* A candidate passes a strong probable-prime test to base 2, an extra
 * strong Lucas test (together a Baillie-PSW test) and this many strong
 * tests to random bases -- the checks Go's crypto/rand.Prime applies
 * through big.Int.ProbablyPrime(20). */
#define PRIME_MW_RANDOM_ROUNDS 20
/* Lucas parameters P = 3, 4, ... are tried until Jacobi(P^2 - 4, n) = -1.
 * Only a perfect square never finds one; a non-square almost always does
 * within a few tries, so reaching the bound just rejects the candidate. */
#define PRIME_MW_LUCAS_MAX_P 1000
/* Words of workspace per word of n: ten k-word values, a 16-entry window
 * table and the Montgomery accumulator, which takes two words more. */
#define PRIME_MW_WORK_PER_WORD 27

typedef struct {
    size_t k;
    uint32_t n0inv;   /* -n^-1 mod 2^32 */
    size_t mr_r;      /* n - 1 = 2^mr_r * d, d odd */
    size_t lucas_r;   /* n + 1 = 2^lucas_r * s, s odd */
    uint32_t *n, *one, *minus_one, *d, *s;
    uint32_t *x, *y, *z, *w, *p;
    uint32_t *table;  /* 16 * k words */
    uint32_t *t;      /* k + 2 words */
} prime_mw_t;

static size_t prime_mw_work_words(size_t k) {
    return k * PRIME_MW_WORK_PER_WORD + 2;
}

static void prime_mw_bind(prime_mw_t *c, uint32_t *work, size_t k) {
    c->k = k;
    c->n = work;
    c->one = work + k;
    c->minus_one = work + 2 * k;
    c->d = work + 3 * k;
    c->s = work + 4 * k;
    c->x = work + 5 * k;
    c->y = work + 6 * k;
    c->z = work + 7 * k;
    c->w = work + 8 * k;
    c->p = work + 9 * k;
    c->table = work + 10 * k;
    c->t = work + 26 * k;
}

static int mw_cmp(const uint32_t *a, const uint32_t *b, size_t k) {
    for (size_t i = k; i-- > 0;)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

static int mw_is_zero(const uint32_t *a, size_t k) {
    uint32_t acc = 0;
    for (size_t i = 0; i < k; i++) acc |= a[i];
    return acc == 0;
}

static uint32_t mw_add(uint32_t *r, const uint32_t *a, const uint32_t *b,
                       size_t k) {
    uint64_t carry = 0;
    for (size_t i = 0; i < k; i++) {
        uint64_t sum = (uint64_t)a[i] + b[i] + carry;
        r[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

static uint32_t mw_sub(uint32_t *r, const uint32_t *a, const uint32_t *b,
                       size_t k) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < k; i++) {
        uint64_t diff = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)diff;
        borrow = (diff >> 32) & 1;
    }
    return (uint32_t)borrow;
}

static int mw_bit(const uint32_t *a, size_t i) {
    return (int)((a[i / 32] >> (i % 32)) & 1);
}

/* Clears every bit of a at position bits or above. */
static void mw_truncate(uint32_t *a, size_t bits, size_t k) {
    for (size_t i = bits / 32; i < k; i++) {
        if (i == bits / 32 && bits % 32)
            a[i] &= ((uint32_t)1 << (bits % 32)) - 1;
        else
            a[i] = 0;
    }
}

static size_t mw_bit_len(const uint32_t *a, size_t k) {
    for (size_t i = k; i-- > 0;) {
        if (a[i]) {
            size_t len = 32 * i;
            for (uint32_t v = a[i]; v; v >>= 1) len++;
            return len;
        }
    }
    return 0;
}

static size_t mw_trailing_zeros(const uint32_t *a, size_t k) {
    size_t tz = 0;
    for (size_t i = 0; i < k; i++) {
        if (a[i]) {
            for (uint32_t v = a[i]; !(v & 1); v >>= 1) tz++;
            return tz;
        }
        tz += 32;
    }
    return tz;
}

/* r = a >> shift (shift < 32 * k); r may alias a. */
static void mw_shift_right(uint32_t *r, const uint32_t *a, size_t shift,
                           size_t k) {
    size_t words = shift / 32, bits = shift % 32;
    for (size_t i = 0; i < k; i++) {
        uint32_t lo = i + words < k ? a[i + words] : 0;
        uint32_t hi = i + words + 1 < k ? a[i + words + 1] : 0;
        r[i] = bits ? (lo >> bits) | (hi << (32 - bits)) : lo;
    }
}

static uint32_t mw_mod_word(const uint32_t *a, size_t k, uint32_t m) {
    uint64_t r = 0;
    for (size_t i = k; i-- > 0;)
        r = ((r << 32) | a[i]) % m;
    return (uint32_t)r;
}

/* r = (a + b) mod n and r = (a - b) mod n, for a, b < n. */
static void mw_mod_add(prime_mw_t *c, uint32_t *r, const uint32_t *a,
                       const uint32_t *b) {
    uint32_t carry = mw_add(r, a, b, c->k);
    if (carry || mw_cmp(r, c->n, c->k) >= 0)
        mw_sub(r, r, c->n, c->k);
}

static void mw_mod_sub(prime_mw_t *c, uint32_t *r, const uint32_t *a,
                       const uint32_t *b) {
    if (mw_sub(r, a, b, c->k))
        mw_add(r, r, c->n, c->k);
}

/* r = a * b / R mod n for a, b < n; r may alias a or b. */
static void mw_mont_mul(prime_mw_t *c, uint32_t *r, const uint32_t *a,
                        const uint32_t *b) {
    size_t k = c->k;
    uint32_t *t = c->t;
    const uint32_t *n = c->n;
    memset(t, 0, (k + 2) * sizeof(*t));
    for (size_t i = 0; i < k; i++) {
        uint64_t carry = 0, acc;
        for (size_t j = 0; j < k; j++) {
            acc = (uint64_t)a[j] * b[i] + t[j] + carry;
            t[j] = (uint32_t)acc;
            carry = acc >> 32;
        }
        acc = (uint64_t)t[k] + carry;
        t[k] = (uint32_t)acc;
        t[k + 1] = (uint32_t)(acc >> 32);

        /* Add m*n, which clears the low word, then drop that word. */
        uint32_t m = t[0] * c->n0inv;
        acc = (uint64_t)m * n[0] + t[0];
        carry = acc >> 32;
        for (size_t j = 1; j < k; j++) {
            acc = (uint64_t)m * n[j] + t[j] + carry;
            t[j - 1] = (uint32_t)acc;
            carry = acc >> 32;
        }
        acc = (uint64_t)t[k] + carry;
        t[k - 1] = (uint32_t)acc;
        t[k] = t[k + 1] + (uint32_t)(acc >> 32);
    }
    /* t < 2n here. */
    if (t[k] || mw_cmp(t, n, k) >= 0)
        mw_sub(r, t, n, k);
    else
        memcpy(r, t, k * sizeof(*r));
}

/* Prepares the Montgomery constants and the odd parts of n - 1 and n + 1
 * for an odd n >= 3 stored in c->n. */
static void prime_mw_setup(prime_mw_t *c) {
    size_t k = c->k;
    uint32_t n0 = c->n[0];

    /* n0 is its own inverse mod 8; each Newton step doubles the bits. */
    uint32_t inv = n0;
    for (int i = 0; i < 4; i++) inv *= 2u - n0 * inv;
    c->n0inv = 0u - inv;

    /* R mod n: 2^b - n is 2^b mod n for b = bitlen(n), since n is odd and
     * at least 2^(b-1); doubling it 32k - b times reaches 2^(32k). */
    size_t b = mw_bit_len(c->n, k);
    uint64_t borrow = 1;
    for (size_t i = 0; i < k; i++) {
        uint64_t v = (uint64_t)(uint32_t)~c->n[i] + borrow;
        c->one[i] = (uint32_t)v;
        borrow = v >> 32;
    }
    mw_truncate(c->one, b, k);
    for (size_t i = b; i < 32 * k; i++)
        mw_mod_add(c, c->one, c->one, c->one);
    mw_sub(c->minus_one, c->n, c->one, k);

    /* n - 1 = 2^mr_r * d. */
    memcpy(c->d, c->n, k * sizeof(*c->d));
    c->d[0] &= ~(uint32_t)1;
    c->mr_r = mw_trailing_zeros(c->d, k);
    mw_shift_right(c->d, c->d, c->mr_r, k);

    /* n + 1 = 2 * ((n >> 1) + 1), and (n >> 1) + 1 cannot overflow k words. */
    mw_shift_right(c->s, c->n, 1, k);
    for (size_t i = 0; i < k && ++c->s[i] == 0; i++) {}
    size_t tz = mw_trailing_zeros(c->s, k);
    c->lucas_r = tz + 1;
    mw_shift_right(c->s, c->s, tz, k);
}

/* r = a^e for a in Montgomery form, using 4-bit windows; r may alias a. */
static void mw_mont_exp(prime_mw_t *c, uint32_t *r, const uint32_t *a,
                        const uint32_t *e) {
    size_t k = c->k, bytes = k * sizeof(uint32_t);
    uint32_t *table = c->table;
    memcpy(table, c->one, bytes);
    memcpy(table + k, a, bytes);
    for (size_t i = 2; i < 16; i++)
        mw_mont_mul(c, table + i * k, table + (i - 1) * k, a);

    size_t windows = (mw_bit_len(e, k) + 3) / 4;
    if (windows == 0) {
        memcpy(r, c->one, bytes);
        return;
    }
    /* Four-bit windows never straddle a 32-bit word. */
#define MW_WINDOW(idx) ((e[(idx) / 8] >> (4 * ((idx) % 8))) & 15u)
    memcpy(r, table + MW_WINDOW(windows - 1) * k, bytes);
    for (size_t i = windows - 1; i-- > 0;) {
        for (int j = 0; j < 4; j++) mw_mont_mul(c, r, r, r);
        uint32_t v = MW_WINDOW(i);
        if (v) mw_mont_mul(c, r, r, table + v * k);
    }
#undef MW_WINDOW
}

/* Finishes a strong probable-prime test once x = a^d (Montgomery form).
 * Returns 1 if n is a strong probable prime to base a, else 0. */
static int prime_mw_strong_finish(prime_mw_t *c, uint32_t *x) {
    size_t k = c->k;
    if (mw_cmp(x, c->one, k) == 0 || mw_cmp(x, c->minus_one, k) == 0)
        return 1;
    for (size_t i = 1; i < c->mr_r; i++) {
        mw_mont_mul(c, x, x, x);
        if (mw_cmp(x, c->minus_one, k) == 0) return 1;
        if (mw_cmp(x, c->one, k) == 0) return 0;
    }
    return 0;
}

/* Strong probable-prime test to base 2.  Squaring and doubling (a modular
 * addition) replace general multiplications by the base. */
static int prime_mw_strong_base2(prime_mw_t *c) {
    uint32_t *x = c->x;
    size_t bits = mw_bit_len(c->d, c->k);
    mw_mod_add(c, x, c->one, c->one);
    for (size_t i = bits - 1; i-- > 0;) {
        mw_mont_mul(c, x, x, x);
        if (mw_bit(c->d, i)) mw_mod_add(c, x, x, x);
    }
    return prime_mw_strong_finish(c, x);
}

/* Strong probable-prime tests to independently drawn random bases.
 * Returns 1 if n passes every round, 0 if it is composite, -1 if entropy
 * fails.  Each base a in [2, n - 2] is drawn directly in Montgomery form:
 * x -> x/R mod n is a bijection, so a uniform x outside {0, 1*R, -1*R}
 * gives a uniform base. */
static int prime_mw_strong_random(prime_mw_t *c, int rounds) {
    size_t k = c->k, bits = mw_bit_len(c->n, k);
    uint32_t *a = c->y;
    for (int round = 0; round < rounds; round++) {
        int drawn = 0;
        /* At least half of the masked draws are below n. */
        for (int attempt = 0; attempt < 128 && !drawn; attempt++) {
            if (neverc_crypto_rand_read((uint8_t *)a, k * sizeof(*a)) != 0)
                return -1;
            mw_truncate(a, bits, k);
            drawn = mw_cmp(a, c->n, k) < 0 && !mw_is_zero(a, k) &&
                    mw_cmp(a, c->one, k) != 0 &&
                    mw_cmp(a, c->minus_one, k) != 0;
        }
        if (!drawn) return -1;
        mw_mont_exp(c, c->x, a, c->d);
        if (!prime_mw_strong_finish(c, c->x)) return 0;
    }
    return 1;
}

/* Jacobi symbol (a / m) for word-sized a and odd m. */
static int jacobi_word(uint32_t a, uint32_t m) {
    int result = 1;
    a %= m;
    while (a) {
        while (!(a & 1)) {
            a >>= 1;
            if ((m & 7) == 3 || (m & 7) == 5) result = -result;
        }
        uint32_t tmp = a;
        a = m;
        m = tmp;
        if ((a & 3) == 3 && (m & 3) == 3) result = -result;
        a %= m;
    }
    return m == 1 ? result : 0;
}

/* Jacobi symbol (a / n) for word-sized a > 0 and the odd candidate n. */
static int prime_mw_jacobi(const prime_mw_t *c, uint32_t a) {
    int result = 1;
    uint32_t n_low = c->n[0];
    while (!(a & 1)) {
        a >>= 1;
        if ((n_low & 7) == 3 || (n_low & 7) == 5) result = -result;
    }
    if (a == 1) return result;
    /* Quadratic reciprocity for odd a and n. */
    if ((a & 3) == 3 && (n_low & 3) == 3) result = -result;
    return result * jacobi_word(mw_mod_word(c->n, c->k, a), a);
}

/* Extra strong Lucas probable-prime test with parameters P (the least
 * P >= 3 with Jacobi(P^2 - 4, n) = -1) and Q = 1.  Writing
 * n + 1 = 2^r * s with s odd, n passes if U_s = 0 and V_s = +-2, or if
 * V_(2^t * s) = 0 for some 0 <= t < r - 1 (all mod n).  Returns 1 if n
 * passes, else 0. */
static int prime_mw_lucas(prime_mw_t *c) {
    size_t k = c->k;
    uint32_t P = 3;
    for (;; P++) {
        if (P > PRIME_MW_LUCAS_MAX_P) return 0;
        int j = prime_mw_jacobi(c, P * P - 4);
        if (j == -1) break;
        /* P^2 - 4 = (P - 2)(P + 2) shares a factor with n.  Smaller P all
         * gave a nonzero symbol, so that factor is P + 2. */
        if (j == 0) return mw_bit_len(c->n, k) <= 32 && c->n[0] == P + 2;
    }

    /* Montgomery forms of 2 and P. */
    uint32_t *two = c->w, *pm = c->p, *vk = c->x, *vk1 = c->y, *t = c->z;
    mw_mod_add(c, two, c->one, c->one);
    memset(pm, 0, k * sizeof(*pm));
    for (int bit = 31; bit >= 0; bit--) {
        mw_mod_add(c, pm, pm, pm);
        if ((P >> bit) & 1) mw_mod_add(c, pm, pm, c->one);
    }

    /* Walk (V_j, V_(j+1)) from j = 0 up to j = s using
     * V_2j = V_j^2 - 2 and V_(2j+1) = V_j * V_(j+1) - P. */
    memcpy(vk, two, k * sizeof(*vk));
    memcpy(vk1, pm, k * sizeof(*vk1));
    for (size_t i = mw_bit_len(c->s, k); i-- > 0;) {
        mw_mont_mul(c, t, vk, vk1);
        if (mw_bit(c->s, i)) {
            mw_mod_sub(c, vk, t, pm);
            mw_mont_mul(c, t, vk1, vk1);
            mw_mod_sub(c, vk1, t, two);
        } else {
            mw_mod_sub(c, vk1, t, pm);
            mw_mont_mul(c, t, vk, vk);
            mw_mod_sub(c, vk, t, two);
        }
    }

    /* V_s = +-2: then U_s = 0 exactly when D * U_s = 2 V_(s+1) - P V_s
     * vanishes, D being invertible mod n. */
    mw_sub(t, c->n, two, k);
    if (mw_cmp(vk, two, k) == 0 || mw_cmp(vk, t, k) == 0) {
        mw_mod_add(c, t, vk1, vk1);
        mw_mont_mul(c, vk1, pm, vk);
        if (mw_cmp(t, vk1, k) == 0) return 1;
    }
    for (size_t i = 0; i + 1 < c->lucas_r; i++) {
        if (mw_is_zero(vk, k)) return 1;
        /* 2 is a fixed point of V -> V^2 - 2: no later term is 0. */
        if (mw_cmp(vk, two, k) == 0) return 0;
        mw_mont_mul(c, t, vk, vk);
        mw_mod_sub(c, vk, t, two);
    }
    return 0;
}

/* Odd primes below 1024: one remainder per group of primes whose product
 * fits in a word screens a candidate before the costly tests. */
static const uint16_t prime_small_odd[] = {
    3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
    73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149,
    151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227,
    229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307,
    311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389,
    397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467,
    479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571,
    577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653,
    659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743, 751,
    757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829, 839, 853,
    857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941, 947,
    953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021
};

/* Returns 1 if a prime below 1024 divides n (n itself being larger). */
static int prime_mw_has_small_factor(const prime_mw_t *c) {
    size_t count = sizeof(prime_small_odd) / sizeof(prime_small_odd[0]);
    for (size_t i = 0; i < count;) {
        uint64_t group = 1;
        size_t end = i;
        while (end < count && group * prime_small_odd[end] <= UINT32_MAX)
            group *= prime_small_odd[end++];
        uint32_t r = mw_mod_word(c->n, c->k, (uint32_t)group);
        for (; i < end; i++)
            if (r % prime_small_odd[i] == 0) return 1;
    }
    return 0;
}

/* Returns 1 if the odd n (> 2^64) is probably prime, 0 if it is composite,
 * and -1 if entropy fails. */
static int prime_mw_probably_prime(prime_mw_t *c) {
    if (prime_mw_has_small_factor(c)) return 0;
    prime_mw_setup(c);
    if (!prime_mw_strong_base2(c) || !prime_mw_lucas(c)) return 0;
    return prime_mw_strong_random(c, PRIME_MW_RANDOM_ROUNDS);
}

static int prime_generate_wide(uint8_t *out, size_t bits) {
    size_t bytes = bits / 8 + (bits % 8 != 0);
    size_t k = bits / 32 + (bits % 32 != 0);
    if (k > (SIZE_MAX / sizeof(uint32_t) - 2) / PRIME_MW_WORK_PER_WORD) {
        neverc_platform_secure_zero(out, bytes);
        return -1;
    }
    size_t work_bytes = prime_mw_work_words(k) * sizeof(uint32_t);
    uint32_t *work = (uint32_t *)NCI_CRYPTO_RAND_CALLOC(1, work_bytes);
    if (!work) {
        neverc_platform_secure_zero(out, bytes);
        return -1;
    }
    prime_mw_t c;
    prime_mw_bind(&c, work, k);

    /* A candidate is prime with probability about 2.9 / bits, so this
     * bound fails with probability below e^-90 unless entropy is broken. */
    size_t attempts = bits > SIZE_MAX / 32 ? SIZE_MAX : bits * 32;
    int result = -1;
    for (size_t attempt = 0; attempt < attempts; attempt++) {
        if (neverc_crypto_rand_read((uint8_t *)c.n, k * sizeof(*c.n)) != 0)
            break;
        /* Exactly bits bits with the top two set, as for narrow primes. */
        mw_truncate(c.n, bits, k);
        c.n[(bits - 1) / 32] |= (uint32_t)1 << ((bits - 1) % 32);
        c.n[(bits - 2) / 32] |= (uint32_t)1 << ((bits - 2) % 32);
        c.n[0] |= 1;

        int primality = prime_mw_probably_prime(&c);
        if (primality < 0) break;
        if (primality > 0) {
            for (size_t i = 0; i < bytes; i++)
                out[i] = (uint8_t)(c.n[i / 4] >> (8 * (i % 4)));
            result = 0;
            break;
        }
    }
    if (result != 0) neverc_platform_secure_zero(out, bytes);
    neverc_platform_secure_zero(work, work_bytes);
    free(work);
    return result;
}

int neverc_crypto_rand_prime(uint8_t *out, size_t bits) {
    if (!out || bits < 2) return -1;
    if (bits > 64) return prime_generate_wide(out, bits);
    size_t bytes = (bits + 7) / 8;

    for (int attempts = 0; attempts < 10000; attempts++) {
        uint64_t val = 0;
        uint8_t random_bytes[8] = {0};
        if (neverc_crypto_rand_read(random_bytes, bytes) != 0) {
            neverc_platform_secure_zero(
                random_bytes, sizeof(random_bytes));
            neverc_platform_secure_zero(out, bytes);
            return -1;
        }
        for (size_t i = 0; i < bytes; i++)
            val |= (uint64_t)random_bytes[i] << (8 * i);
        neverc_platform_secure_zero(random_bytes, sizeof(random_bytes));

        /* Set the top two bits, as Go does, so that the product of two
         * bits-bit primes never falls one bit short of 2*bits. */
        val |= (uint64_t)3 << (bits - 2);
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
    neverc_platform_secure_zero(out, bytes);
    return -1;
}
