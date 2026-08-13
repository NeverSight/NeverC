#include "neverc/std/crypto/mldsa.h"
#include <stdio.h>
#include <string.h>

static int test_crypto_rand_read(uint8_t *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NEVERC_CRYPTO_RAND_H
#define neverc_crypto_rand_read test_crypto_rand_read
#include "../../../std/src/crypto/mldsa/mldsa.c"
#undef neverc_crypto_rand_read

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int all_zero(const void *value, size_t length) {
    const uint8_t *bytes = (const uint8_t *)value;
    uint8_t combined = 0;
    for (size_t i = 0; i < length; i++) combined |= bytes[i];
    return combined == 0;
}

int main(void) {
    neverc_mldsa44_sk_t secret_key;
    memset(&secret_key, 0x5a, sizeof(secret_key));

    CHECK(neverc_mldsa44_generate_key(&secret_key) == -1);
    CHECK(all_zero(&secret_key, sizeof(secret_key)));

    puts("passed");
    return 0;
}
