#include "neverc/std/crypto/rsa.h"
#include <stdio.h>
#include <string.h>

#include "../../../std/src/crypto/rsa/rsa.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int encrypt_encoded_message(const neverc_rsa_public_key_t *public_key,
                                   const unsigned char *encoded,
                                   unsigned char *ciphertext, int key_size) {
    neverc_bigint_t message;
    neverc_bigint_t encrypted;
    neverc_bigint_init(&message);
    neverc_bigint_init(&encrypted);
    int result = -1;

    if (bytes_to_bigint(&message, encoded, (size_t)key_size) != 0)
        goto cleanup;
    neverc_bigint_exp(
        &encrypted, &message, &public_key->e, &public_key->n);
    result = bigint_to_bytes(&encrypted, ciphertext, key_size);

cleanup:
    rsa_bigint_secure_free(&message);
    neverc_bigint_free(&encrypted);
    return result;
}

int main(void) {
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    CHECK(neverc_rsa_generate_key(&key, 512) == 0);

    int key_size = neverc_rsa_key_size(&key.pub);
    CHECK(key_size == 64);
    unsigned char encoded[64] = {0};
    unsigned char ciphertext[64];
    unsigned char plaintext[64];
    size_t plaintext_len = 99;

    encoded[0] = 0;
    encoded[1] = 2;
    encoded[2] = 0;
    encoded[3] = 0x42;
    CHECK(encrypt_encoded_message(
              &key.pub, encoded, ciphertext, key_size) == 0);
    CHECK(neverc_rsa_decrypt_pkcs1v15(
              &key, ciphertext, sizeof(ciphertext), plaintext,
              sizeof(plaintext), &plaintext_len) == -1);
    CHECK(plaintext_len == 0);

    memset(encoded, 0, sizeof(encoded));
    encoded[1] = 2;
    memset(encoded + 2, 0x7f, 7);
    encoded[9] = 0;
    encoded[10] = 0x42;
    CHECK(encrypt_encoded_message(
              &key.pub, encoded, ciphertext, key_size) == 0);
    plaintext_len = 99;
    CHECK(neverc_rsa_decrypt_pkcs1v15(
              &key, ciphertext, sizeof(ciphertext), plaintext,
              sizeof(plaintext), &plaintext_len) == -1);
    CHECK(plaintext_len == 0);

    neverc_rsa_private_key_free(&key);
    puts("passed");
    return 0;
}
