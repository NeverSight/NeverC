#include "neverc/std/crypto/ecdh.h"
#include <stdio.h>
#include <string.h>

static int test_crypto_rand_read(uint8_t *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NEVERC_CRYPTO_RAND_H
#define neverc_crypto_rand_read test_crypto_rand_read
#include "../../../std/src/crypto/ecdh/ecdh.c"
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
    neverc_ecdh_key_t key;

    memset(&key, 0x5a, sizeof(key));
    CHECK(neverc_ecdh_generate_key(
              NEVERC_ECDH_CURVE_X25519, &key) == -1);
    CHECK(all_zero(&key, sizeof(key)));

    memset(&key, 0x5a, sizeof(key));
    CHECK(neverc_ecdh_generate_key(
              NEVERC_ECDH_CURVE_P256, &key) == -1);
    CHECK(all_zero(&key, sizeof(key)));

    puts("passed");
    return 0;
}
