#include "neverc/std/crypto/rand.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_CRYPTO_RAND_RANDOM entropy_fails
#include "../../../std/src/crypto/rand/rand.c"
#undef NCI_CRYPTO_RAND_RANDOM

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

    puts("passed");
    return 0;
}
