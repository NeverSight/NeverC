#include "neverc/std/crypto/ecdsa.h"
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
    neverc_ecdsa_private_key_t key;
    neverc_ecdsa_private_key_init(&key);

    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    ASSERT_INT_EQ(neverc_ecdsa_generate_key(&key, c), 0);
    ASSERT_TRUE(!neverc_bigint_is_zero(&key.d));
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &key.pub.pub));

    neverc_ecdsa_private_key_free(&key);
}

static void test_sign_verify(void) {
    printf("[sign_verify]\n");
    neverc_ecdsa_private_key_t key;
    neverc_ecdsa_private_key_init(&key);
    neverc_ecdsa_generate_key(&key, neverc_elliptic_p256());

    const char *msg = "ECDSA test message";
    unsigned char hash[32];
    neverc_sha256_sum((const unsigned char *)msg, strlen(msg), hash);

    neverc_ecdsa_signature_t sig;
    neverc_ecdsa_signature_init(&sig);
    ASSERT_INT_EQ(neverc_ecdsa_sign(&key, hash, 32, &sig), 0);
    ASSERT_TRUE(!neverc_bigint_is_zero(&sig.r));
    ASSERT_TRUE(!neverc_bigint_is_zero(&sig.s));

    ASSERT_INT_EQ(neverc_ecdsa_verify(&key.pub, hash, 32, &sig), 0);

    hash[0] ^= 0xFF;
    ASSERT_TRUE(neverc_ecdsa_verify(&key.pub, hash, 32, &sig) != 0);

    neverc_ecdsa_signature_free(&sig);
    neverc_ecdsa_private_key_free(&key);
}

static void test_different_messages(void) {
    printf("[different_messages]\n");
    neverc_ecdsa_private_key_t key;
    neverc_ecdsa_private_key_init(&key);
    neverc_ecdsa_generate_key(&key, neverc_elliptic_p256());

    unsigned char hash1[32], hash2[32];
    neverc_sha256_sum((const unsigned char *)"msg1", 4, hash1);
    neverc_sha256_sum((const unsigned char *)"msg2", 4, hash2);

    neverc_ecdsa_signature_t sig1, sig2;
    neverc_ecdsa_signature_init(&sig1);
    neverc_ecdsa_signature_init(&sig2);

    neverc_ecdsa_sign(&key, hash1, 32, &sig1);
    neverc_ecdsa_sign(&key, hash2, 32, &sig2);

    ASSERT_INT_EQ(neverc_ecdsa_verify(&key.pub, hash1, 32, &sig1), 0);
    ASSERT_INT_EQ(neverc_ecdsa_verify(&key.pub, hash2, 32, &sig2), 0);

    ASSERT_TRUE(neverc_ecdsa_verify(&key.pub, hash1, 32, &sig2) != 0);
    ASSERT_TRUE(neverc_ecdsa_verify(&key.pub, hash2, 32, &sig1) != 0);

    neverc_ecdsa_signature_free(&sig1);
    neverc_ecdsa_signature_free(&sig2);
    neverc_ecdsa_private_key_free(&key);
}

static void test_init_free(void) {
    printf("[init_free]\n");
    neverc_ecdsa_public_key_t pub;
    neverc_ecdsa_public_key_init(&pub);
    neverc_ecdsa_public_key_free(&pub);

    neverc_ecdsa_signature_t sig;
    neverc_ecdsa_signature_init(&sig);
    neverc_ecdsa_signature_free(&sig);
    tests_run++; tests_passed++;
}

static void test_invalid_inputs(void) {
    printf("[invalid_inputs]\n");
    const neverc_elliptic_curve_t *curve = neverc_elliptic_p256();
    neverc_ecdsa_private_key_t key;
    neverc_ecdsa_private_key_init(&key);
    neverc_ecdsa_signature_t sig;
    neverc_ecdsa_signature_init(&sig);
    unsigned char hash[32] = {0};

    ASSERT_INT_EQ(neverc_ecdsa_generate_key(NULL, curve), -1);
    ASSERT_INT_EQ(neverc_ecdsa_generate_key(&key, NULL), -1);
    ASSERT_INT_EQ(neverc_ecdsa_sign(NULL, hash, sizeof(hash), &sig), -1);
    ASSERT_INT_EQ(neverc_ecdsa_sign(&key, hash, sizeof(hash), &sig), -1);
    ASSERT_INT_EQ(neverc_ecdsa_verify(NULL, hash, sizeof(hash), &sig), -1);

    ASSERT_INT_EQ(neverc_ecdsa_generate_key(&key, curve), 0);
    ASSERT_INT_EQ(neverc_ecdsa_sign(&key, NULL, 0, &sig), -1);
    ASSERT_INT_EQ(neverc_ecdsa_sign(
                      &key, hash, sizeof(hash), NULL), -1);
    ASSERT_INT_EQ(neverc_ecdsa_verify(&key.pub, NULL, 0, &sig), -1);
    ASSERT_INT_EQ(neverc_ecdsa_verify(
                      &key.pub, hash, sizeof(hash), NULL), -1);

    neverc_ecdsa_signature_free(&sig);
    neverc_ecdsa_private_key_free(&key);
}

int main(void) {
    printf("=== NeverC crypto/ecdsa Tests ===\n");
    test_init_free();
    test_keygen();
    test_sign_verify();
    test_different_messages();
    test_invalid_inputs();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
