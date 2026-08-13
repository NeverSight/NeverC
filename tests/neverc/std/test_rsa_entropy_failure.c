#include "neverc/std/crypto/rsa.h"
#include "neverc/std/_platform.h"
#include <stdio.h>
#include <string.h>

static int force_entropy_failure;

static int test_random(unsigned char *buffer, size_t length) {
    if (!force_entropy_failure)
        return neverc_platform_random(buffer, length);
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_RSA_RANDOM test_random
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
    force_entropy_failure = 1;
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

    force_entropy_failure = 0;
    neverc_bigint_t candidate;
    neverc_bigint_init(&candidate);
    CHECK(random_bigint(&candidate, 257) == 0);
    CHECK(neverc_bigint_bit_len(&candidate) == 257);
    CHECK(neverc_bigint_bit(&candidate, 255) == 1);
    rsa_bigint_secure_free(&candidate);

    neverc_rsa_private_key_init(&private_key);
    CHECK(neverc_rsa_generate_key(&private_key, 512) == 0);
    const unsigned char plaintext[] = "blind";
    unsigned char encrypted[64];
    ciphertext_len = 0;
    CHECK(neverc_rsa_encrypt_pkcs1v15(
              &private_key.pub, plaintext, sizeof(plaintext) - 1,
              encrypted, sizeof(encrypted), &ciphertext_len) == 0);

    force_entropy_failure = 1;
    unsigned char decrypted[64];
    size_t decrypted_len = 99;
    CHECK(neverc_rsa_decrypt_pkcs1v15(
              &private_key, encrypted, ciphertext_len,
              decrypted, sizeof(decrypted), &decrypted_len) == -1);
    CHECK(decrypted_len == 0);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE] = {0};
    unsigned char signature[64];
    size_t signature_len = 99;
    CHECK(neverc_rsa_sign_pkcs1v15_sha256(
              &private_key, hash, sizeof(hash), signature,
              sizeof(signature), &signature_len) == -1);
    CHECK(signature_len == 0);
    neverc_rsa_private_key_free(&private_key);

    puts("passed");
    return 0;
}
