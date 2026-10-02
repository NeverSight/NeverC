#include "neverc/std/crypto/rand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* By default the source reports failure after scribbling on the buffer.
 * A stuck source instead "succeeds" with all-ones bytes every time. */
static int entropy_stuck;
static int fail_allocation;

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (entropy_stuck) {
        if (buffer) memset(buffer, 0xff, length);
        return 0;
    }
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

static void *controlled_calloc(size_t count, size_t size) {
    return fail_allocation ? NULL : calloc(count, size);
}

#define NCI_CRYPTO_RAND_RANDOM entropy_fails
#define NCI_CRYPTO_RAND_CALLOC controlled_calloc
#include "../../../std/src/crypto/rand/rand.c"
#undef NCI_CRYPTO_RAND_RANDOM
#undef NCI_CRYPTO_RAND_CALLOC

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int all_zero(const void *value, size_t length) {
    const unsigned char *bytes = (const unsigned char *)value;
    unsigned char combined = 0;
    for (size_t i = 0; i < length; i++) combined |= bytes[i];
    return combined == 0;
}

/* Runs the multi-word primality test on the k-word value n; -2 if the
 * test itself cannot allocate. */
static int probably_prime_words(const uint32_t *n, size_t k) {
    uint32_t *work = (uint32_t *)calloc(prime_mw_work_words(k),
                                        sizeof(uint32_t));
    if (!work) return -2;
    prime_mw_t c;
    prime_mw_bind(&c, work, k);
    memcpy(c.n, n, k * sizeof(*n));
    int result = prime_mw_probably_prime(&c);
    free(work);
    return result;
}

static int probably_prime_mersenne(size_t p) {
    uint32_t n[4];
    memset(n, 0xff, sizeof(n));
    mw_truncate(n, p, 4);
    return probably_prime_words(n, (p + 31) / 32);
}

int main(void) {
    uint8_t buf[16];
    memset(buf, 0x5a, sizeof(buf));
    CHECK(neverc_crypto_rand_read(buf, sizeof(buf)) == -1);
    CHECK(all_zero(buf, sizeof(buf)));

    uint64_t n = 0x1111111111111111ULL;
    CHECK(neverc_crypto_rand_int(&n, 100) == -1);
    CHECK(n == 0);

    uint8_t prime[8];
    memset(prime, 0x5a, sizeof(prime));
    CHECK(neverc_crypto_rand_prime(prime, 32) == -1);
    CHECK(all_zero(prime, 4));
    CHECK(prime[4] == 0x5a);

    /* Sizes above 64 bits wipe exactly their (bits + 7) / 8 output bytes. */
    uint8_t big_prime[16];
    memset(big_prime, 0x5a, sizeof(big_prime));
    CHECK(neverc_crypto_rand_prime(big_prime, 100) == -1);
    CHECK(all_zero(big_prime, 13));
    CHECK(big_prime[13] == 0x5a);

    /* So does a failed workspace allocation. */
    fail_allocation = 1;
    memset(big_prime, 0x5a, sizeof(big_prime));
    CHECK(neverc_crypto_rand_prime(big_prime, 100) == -1);
    CHECK(all_zero(big_prime, 13));
    CHECK(big_prime[13] == 0x5a);
    fail_allocation = 0;

    /* Only the random-base rounds consume entropy: 2^127 - 1 is prime and
     * reaches them, while 2^67 - 1 (rejected by the Lucas test) and
     * 1351739 = 1039 * 1301 (no factor below 1024; an extra strong Lucas
     * pseudoprime rejected by the base-2 test) never do. */
    CHECK(probably_prime_mersenne(127) == -1);
    CHECK(probably_prime_mersenne(67) == 0);
    uint32_t lucas_pseudoprime = 1351739;
    CHECK(probably_prime_words(&lucas_pseudoprime, 1) == 0);

    /* A source stuck on one value must not hang generation: 0xff bytes
     * always yield a candidate divisible by 3, or a base equal to n. */
    entropy_stuck = 1;
    memset(prime, 0x5a, sizeof(prime));
    CHECK(neverc_crypto_rand_prime(prime, 32) == -1);
    CHECK(all_zero(prime, 4));
    memset(big_prime, 0x5a, sizeof(big_prime));
    CHECK(neverc_crypto_rand_prime(big_prime, 100) == -1);
    CHECK(all_zero(big_prime, 13));
    CHECK(big_prime[13] == 0x5a);
    CHECK(probably_prime_mersenne(127) == -1);
    entropy_stuck = 0;

    puts("passed");
    return 0;
}
