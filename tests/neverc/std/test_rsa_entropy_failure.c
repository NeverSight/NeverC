#include "neverc/std/crypto/rsa.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_RSA_RANDOM entropy_fails
#include "../../../std/src/crypto/rsa/rsa.c"
#undef NCI_RSA_RANDOM

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    neverc_rsa_private_key_t private_key;
    neverc_rsa_private_key_init(&private_key);
    neverc_bigint_set_int64(&private_key.d, 3);
    neverc_bigint_set_int64(&private_key.p, 5);
    neverc_bigint_set_int64(&private_key.q, 7);

    CHECK(neverc_rsa_generate_key(&private_key, 512) == -1);
    CHECK(neverc_bigint_is_zero(&private_key.pub.n));
    CHECK(neverc_bigint_is_zero(&private_key.d));
    CHECK(neverc_bigint_is_zero(&private_key.p));
    CHECK(neverc_bigint_is_zero(&private_key.q));
    neverc_rsa_private_key_free(&private_key);

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    CHECK(neverc_bigint_set_string(
              &public_key.n, "ffffffffffffffffffffffff", 16) == 0);
    neverc_bigint_set_int64(&public_key.e, 3);

    const unsigned char message = 0x42;
    unsigned char ciphertext[12];
    memset(ciphertext, 0x5a, sizeof(ciphertext));
    size_t ciphertext_len = 99;
    CHECK(neverc_rsa_encrypt_pkcs1v15(
              &public_key, &message, 1, ciphertext, sizeof(ciphertext),
              &ciphertext_len) == -1);
    CHECK(ciphertext_len == 0);
    CHECK(ciphertext[0] == 0x5a);

    neverc_rsa_public_key_free(&public_key);
    puts("passed");
    return 0;
}
