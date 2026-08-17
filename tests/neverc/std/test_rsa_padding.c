#include "neverc/std/crypto/rsa.h"
#include "neverc/std/crypto/sha256.h"
#include <stdio.h>
#include <stdlib.h>
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

static int sign_encoded_message(const neverc_rsa_private_key_t *private_key,
                                const unsigned char *encoded,
                                unsigned char *signature, int key_size) {
    neverc_bigint_t message;
    neverc_bigint_t signed_value;
    neverc_bigint_init(&message);
    neverc_bigint_init(&signed_value);
    int result = -1;

    if (bytes_to_bigint(&message, encoded, (size_t)key_size) != 0)
        goto cleanup;
    if (rsa_private_exp(&signed_value, &message, private_key) != 0)
        goto cleanup;
    result = bigint_to_bytes(&signed_value, signature, key_size);

cleanup:
    rsa_bigint_secure_free(&message);
    rsa_bigint_secure_free(&signed_value);
    return result;
}

static int pss_encode_sha256(unsigned char *em, int key_bytes,
                             int modulus_bits,
                             const unsigned char *hash,
                             const unsigned char *salt) {
    const size_t digest_len = NEVERC_SHA256_DIGEST_SIZE;
    const size_t salt_len = digest_len;
    size_t encoded_bits = (size_t)modulus_bits - 1;
    size_t encoded_len = (encoded_bits + 7) / 8;
    if (encoded_len < digest_len + salt_len + 2 ||
        encoded_len > (size_t)key_bytes)
        return -1;

    memset(em, 0, (size_t)key_bytes);
    size_t leading_len = (size_t)key_bytes - encoded_len;
    unsigned char *encoded_message = em + leading_len;
    size_t database_len = encoded_len - digest_len - 1;
    size_t padding_len = encoded_len - digest_len - salt_len - 2;
    encoded_message[padding_len] = 0x01;
    memcpy(encoded_message + padding_len + 1, salt, salt_len);

    unsigned char message_prime[8 + NEVERC_SHA256_DIGEST_SIZE * 2] = {0};
    memcpy(message_prime + 8, hash, digest_len);
    memcpy(message_prime + 8 + digest_len, salt, salt_len);
    unsigned char *encoded_hash = encoded_message + database_len;
    rsa_hash_sum(RSA_HASH_SHA256, message_prime,
                 8 + digest_len + salt_len, encoded_hash);

    unsigned char *database_mask =
        (unsigned char *)malloc(database_len);
    if (!database_mask)
        return -1;
    mgf1(RSA_HASH_SHA256, encoded_hash, digest_len,
         database_mask, database_len);
    for (size_t i = 0; i < database_len; ++i)
        encoded_message[i] ^= database_mask[i];
    free(database_mask);

    unsigned unused_bits = (unsigned)(encoded_len * 8 - encoded_bits);
    if (unused_bits > 0)
        encoded_message[0] &=
            (unsigned char)(0xffu >> unused_bits);
    encoded_message[encoded_len - 1] = 0xbc;
    return 0;
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

    /* Type 1 (signature) padding must not decrypt as a PKCS#1 v1.5 message. */
    memset(encoded, 0, sizeof(encoded));
    encoded[1] = 1;
    memset(encoded + 2, 0xff, 8);
    encoded[10] = 0;
    encoded[11] = 0x42;
    CHECK(encrypt_encoded_message(
              &key.pub, encoded, ciphertext, key_size) == 0);
    plaintext_len = 99;
    CHECK(neverc_rsa_decrypt_pkcs1v15(
              &key, ciphertext, sizeof(ciphertext), plaintext,
              sizeof(plaintext), &plaintext_len) == -1);
    CHECK(plaintext_len == 0);

    /* 00 02 PS with no 0x00 separator. */
    memset(encoded, 0x7f, sizeof(encoded));
    encoded[0] = 0;
    encoded[1] = 2;
    CHECK(encrypt_encoded_message(
              &key.pub, encoded, ciphertext, key_size) == 0);
    plaintext_len = 99;
    CHECK(neverc_rsa_decrypt_pkcs1v15(
              &key, ciphertext, sizeof(ciphertext), plaintext,
              sizeof(plaintext), &plaintext_len) == -1);
    CHECK(plaintext_len == 0);

    neverc_rsa_private_key_free(&key);

    neverc_rsa_private_key_init(&key);
    CHECK(neverc_rsa_generate_key(&key, 1024) == 0);
    key_size = neverc_rsa_key_size(&key.pub);
    CHECK(key_size == 128);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE];
    neverc_sha256_sum((const unsigned char *)"pss padding", 11, hash);

    unsigned char pkcs1[128];
    unsigned char signature[128];
    memset(pkcs1, 0, sizeof(pkcs1));
    pkcs1[1] = 0x01;
    memset(pkcs1 + 2, 0xff, 74);
    pkcs1[76] = 0x00;
    memcpy(pkcs1 + 77, sha256_digest_info, sizeof(sha256_digest_info));
    memcpy(pkcs1 + 96, hash, sizeof(hash));
    CHECK(sign_encoded_message(&key, pkcs1, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pkcs1v15_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) == 0);

    pkcs1[1] = 0x02; /* type 2 (encryption) must not verify as a signature */
    CHECK(sign_encoded_message(&key, pkcs1, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pkcs1v15_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);

    pkcs1[1] = 0x01;
    pkcs1[10] = 0xfe; /* non-FF byte in PS */
    CHECK(sign_encoded_message(&key, pkcs1, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pkcs1v15_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);
    pkcs1[10] = 0xff;

    pkcs1[76] = 0xff; /* missing 00 separator */
    CHECK(sign_encoded_message(&key, pkcs1, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pkcs1v15_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);
    pkcs1[76] = 0x00;

    pkcs1[0] = 0x01; /* missing leading 00 */
    CHECK(sign_encoded_message(&key, pkcs1, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pkcs1v15_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);
    pkcs1[0] = 0x00;

    unsigned char salt[NEVERC_SHA256_DIGEST_SIZE];
    memset(salt, 0xa5, sizeof(salt));
    unsigned char em[128];
    int modulus_bits = neverc_bigint_bit_len(&key.pub.n);
    CHECK(pss_encode_sha256(em, key_size, modulus_bits, hash, salt) == 0);
    CHECK(sign_encoded_message(&key, em, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pss_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) == 0);

    em[key_size - 1] ^= 0x01; /* corrupt 0xbc trailer */
    CHECK(sign_encoded_message(&key, em, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pss_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);

    CHECK(pss_encode_sha256(em, key_size, modulus_bits, hash, salt) == 0);
    em[0] |= 0x80; /* unused high bit of maskedDB must be zero */
    CHECK(sign_encoded_message(&key, em, signature, key_size) == 0);
    CHECK(neverc_rsa_verify_pss_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);

    memset(signature, 0, sizeof(signature));
    CHECK(neverc_rsa_verify_pss_sha256(
              &key.pub, hash, sizeof(hash), signature,
              sizeof(signature)) != 0);

    neverc_rsa_private_key_free(&key);
    puts("passed");
    return 0;
}
