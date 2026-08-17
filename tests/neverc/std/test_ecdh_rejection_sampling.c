#include "neverc/std/crypto/ecdh.h"
#include <stdio.h>
#include <string.h>

static int random_calls;

static int controlled_crypto_rand_read(uint8_t *buffer, size_t length) {
    random_calls++;
    memset(buffer, 0, length);
    if (random_calls % 3 == 1)
        memset(buffer, 0xff, length); /* >= n */
    else if (random_calls % 3 == 0)
        buffer[length - 1] = 1; /* accepted in [1, n-1] */
    /* % 3 == 2: all-zero, rejected */
    return 0;
}

#define NEVERC_CRYPTO_RAND_H
#define neverc_crypto_rand_read controlled_crypto_rand_read
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

static int private_key_is_one(const neverc_ecdh_key_t *key) {
    for (int i = 0; i < key->privkey_len - 1; i++) {
        if (key->private_key[i] != 0) return 0;
    }
    return key->private_key[key->privkey_len - 1] == 1;
}

int main(void) {
    neverc_ecdh_key_t key;

    CHECK(neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P256, &key) == 0);
    CHECK(random_calls == 3);
    CHECK(private_key_is_one(&key));

    CHECK(neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P384, &key) == 0);
    CHECK(random_calls == 6);
    CHECK(private_key_is_one(&key));

    puts("passed");
    return 0;
}
