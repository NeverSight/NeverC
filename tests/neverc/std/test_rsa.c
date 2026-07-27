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
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
