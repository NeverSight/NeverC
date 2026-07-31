#include "neverc/std/crypto/rsa.h"
#include "neverc/std/crypto/sha256.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_keygen(void) {
    printf("[keygen]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 512), 0);
    ASSERT_TRUE(!neverc_bigint_is_zero(&key.pub.n));
    ASSERT_TRUE(!neverc_bigint_is_zero(&key.d));
    ASSERT_TRUE(neverc_bigint_int64(&key.pub.e) == 65537);

    int ks = neverc_rsa_key_size(&key.pub);
    ASSERT_TRUE(ks == 64);

    neverc_rsa_private_key_free(&key);
}

static void test_encrypt_decrypt(void) {
    printf("[encrypt_decrypt]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 512), 0);

    const char *msg = "Hello RSA!";
    size_t msg_len = strlen(msg);
    unsigned char ct[256], pt[256];
    size_t ct_len, pt_len;

    ASSERT_INT_EQ(neverc_rsa_encrypt_pkcs1v15(&key.pub,
        (const unsigned char *)msg, msg_len, ct, sizeof(ct), &ct_len), 0);
    ASSERT_TRUE(ct_len == 64);

    ASSERT_INT_EQ(neverc_rsa_decrypt_pkcs1v15(&key,
        ct, ct_len, pt, sizeof(pt), &pt_len), 0);
    ASSERT_TRUE(pt_len == msg_len);
    ASSERT_TRUE(memcmp(pt, msg, msg_len) == 0);

    neverc_rsa_private_key_free(&key);
}

static void test_sign_verify(void) {
    printf("[sign_verify]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 1024), 0);

    const char *msg = "Sign this message";
    unsigned char hash[32];
    neverc_sha256_sum((const unsigned char *)msg, strlen(msg), hash);

    unsigned char sig[256];
    size_t sig_len;
    ASSERT_INT_EQ(neverc_rsa_sign_pkcs1v15_sha256(&key, hash, 32,
        sig, sizeof(sig), &sig_len), 0);
    ASSERT_TRUE(sig_len == 128);

    ASSERT_INT_EQ(neverc_rsa_verify_pkcs1v15_sha256(&key.pub, hash, 32,
        sig, sig_len), 0);

    hash[0] ^= 0xFF;
    ASSERT_TRUE(neverc_rsa_verify_pkcs1v15_sha256(&key.pub, hash, 32,
        sig, sig_len) != 0);

    neverc_rsa_private_key_free(&key);
}

static void test_verify_pss_sha256(void) {
    printf("[verify_pss_sha256]\n");
    static const unsigned char signature[128] = {
        0x57, 0x12, 0xed, 0xba, 0xdf, 0x6b, 0x26, 0x0b,
        0x78, 0x88, 0x3d, 0xd9, 0x6a, 0x04, 0x77, 0x79,
        0xab, 0x48, 0xf3, 0x0f, 0x83, 0xa9, 0x60, 0x89,
        0x81, 0x61, 0x92, 0x90, 0x4e, 0xc6, 0x73, 0x1e,
        0x92, 0xd6, 0x48, 0xfa, 0xc2, 0x9d, 0x16, 0xd7,
        0x9c, 0xb9, 0xeb, 0xf0, 0xbe, 0xa3, 0xb6, 0x1b,
        0x95, 0x3d, 0x2e, 0x02, 0xb8, 0xb2, 0xe9, 0xb5,
        0x0f, 0x82, 0xc1, 0xdf, 0x58, 0xca, 0xa6, 0x03,
        0x96, 0x41, 0xbe, 0x27, 0xd5, 0xe1, 0xa2, 0xee,
        0x63, 0xa9, 0x25, 0xc6, 0xf1, 0x6f, 0xdb, 0x70,
        0xaa, 0x74, 0xa6, 0x72, 0xa9, 0xcb, 0x04, 0x52,
        0x3e, 0x40, 0x7e, 0xc2, 0xb7, 0x3b, 0x05, 0xad,
        0x6d, 0x11, 0x9e, 0xd2, 0x2b, 0x53, 0x08, 0x98,
        0xf3, 0x76, 0x55, 0x47, 0x50, 0x3c, 0xce, 0x3f,
        0xa1, 0xe2, 0x3c, 0x63, 0xbc, 0xcf, 0x47, 0xf8,
        0x0c, 0xa5, 0x24, 0x31, 0xd4, 0x69, 0x71, 0x4e,
    };
    static const char modulus[] =
        "E46058153C0023B64C659D555395C044D1DDCA1D823FB43047305775E3C8E5F1"
        "A414F555E50EE5CD2DC1B21C5F3FBA992FBD4F6592981DFD2D08BE9E514496B"
        "B16D8A6C4841561D3027780119E392601D7E662EBE2B60E2A8663E7A70BE4AE1"
        "0DF3BF095200C86CD61BFF253627CC0A0EF20A91219A737A323FB3110B061BC4B";
    static const unsigned char message[] = "NeverC RSA-PSS test vector";

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    ASSERT_INT_EQ(
        neverc_bigint_set_string(&public_key.n, modulus, 16), 0);
    neverc_bigint_set_uint64(&public_key.e, 65537);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE];
    neverc_sha256_sum(message, sizeof(message) - 1, hash);
    ASSERT_INT_EQ(
        neverc_rsa_verify_pss_sha256(
            &public_key, hash, sizeof(hash),
            signature, sizeof(signature)),
        0);
    hash[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pss_sha256(
            &public_key, hash, sizeof(hash),
            signature, sizeof(signature)) != 0);
    neverc_rsa_public_key_free(&public_key);
}

static void test_init_free(void) {
    printf("[init_free]\n");
    neverc_rsa_public_key_t pub;
    neverc_rsa_public_key_init(&pub);
    neverc_rsa_public_key_free(&pub);

    neverc_rsa_private_key_t priv;
    neverc_rsa_private_key_init(&priv);
    neverc_rsa_private_key_free(&priv);
    tests_run++; tests_passed++;
}

int main(void) {
    printf("=== NeverC crypto/rsa Tests ===\n");
    test_init_free();
    test_keygen();
    test_encrypt_decrypt();
    test_sign_verify();
    test_verify_pss_sha256();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
