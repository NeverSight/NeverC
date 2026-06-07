#include "neverc/crypto/ed25519.h"
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
    unsigned char pub[32], priv[64];
    ASSERT_INT_EQ(neverc_ed25519_generate_key(pub, priv), 0);

    int pub_nonzero = 0, priv_nonzero = 0;
    for (int i = 0; i < 32; i++) if (pub[i]) pub_nonzero = 1;
    for (int i = 0; i < 64; i++) if (priv[i]) priv_nonzero = 1;
    ASSERT_TRUE(pub_nonzero);
    ASSERT_TRUE(priv_nonzero);

    ASSERT_TRUE(memcmp(priv + 32, pub, 32) == 0);
}

static void test_seed_roundtrip(void) {
    printf("[seed_roundtrip]\n");
    unsigned char seed[32] = {
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
    };
    unsigned char pub[32], priv[64];
    neverc_ed25519_new_key_from_seed(seed, pub, priv);

    unsigned char seed2[32];
    neverc_ed25519_seed(priv, seed2);
    ASSERT_TRUE(memcmp(seed, seed2, 32) == 0);
}

static void test_deterministic_keygen(void) {
    printf("[deterministic_keygen]\n");
    unsigned char seed[32] = {0};
    seed[0] = 42;
    unsigned char pub1[32], priv1[64];
    unsigned char pub2[32], priv2[64];

    neverc_ed25519_new_key_from_seed(seed, pub1, priv1);
    neverc_ed25519_new_key_from_seed(seed, pub2, priv2);

    ASSERT_TRUE(memcmp(pub1, pub2, 32) == 0);
    ASSERT_TRUE(memcmp(priv1, priv2, 64) == 0);
}

static void test_sign_verify(void) {
    printf("[sign_verify]\n");
    unsigned char pub[32], priv[64];
    neverc_ed25519_generate_key(pub, priv);

    const char *msg = "Ed25519 test message";
    unsigned char sig[64];
    ASSERT_INT_EQ(neverc_ed25519_sign(priv, (const unsigned char *)msg, strlen(msg), sig), 0);
    ASSERT_INT_EQ(neverc_ed25519_verify(pub, (const unsigned char *)msg, strlen(msg), sig), 0);
}

static void test_wrong_message(void) {
    printf("[wrong_message]\n");
    unsigned char pub[32], priv[64];
    neverc_ed25519_generate_key(pub, priv);

    unsigned char sig[64];
    neverc_ed25519_sign(priv, (const unsigned char *)"msg1", 4, sig);
    ASSERT_TRUE(neverc_ed25519_verify(pub, (const unsigned char *)"msg2", 4, sig) != 0);
}

static void test_wrong_key(void) {
    printf("[wrong_key]\n");
    unsigned char pub1[32], priv1[64];
    unsigned char pub2[32], priv2[64];
    neverc_ed25519_generate_key(pub1, priv1);
    neverc_ed25519_generate_key(pub2, priv2);

    unsigned char sig[64];
    neverc_ed25519_sign(priv1, (const unsigned char *)"test", 4, sig);
    ASSERT_TRUE(neverc_ed25519_verify(pub2, (const unsigned char *)"test", 4, sig) != 0);
}

static void test_empty_message(void) {
    printf("[empty_message]\n");
    unsigned char pub[32], priv[64];
    neverc_ed25519_generate_key(pub, priv);

    unsigned char sig[64];
    ASSERT_INT_EQ(neverc_ed25519_sign(priv, NULL, 0, sig), 0);
    ASSERT_INT_EQ(neverc_ed25519_verify(pub, NULL, 0, sig), 0);
}

static void test_deterministic_signature(void) {
    printf("[deterministic_signature]\n");
    unsigned char seed[32] = {0};
    seed[0] = 99;
    unsigned char pub[32], priv[64];
    neverc_ed25519_new_key_from_seed(seed, pub, priv);

    const char *msg = "deterministic";
    unsigned char sig1[64], sig2[64];
    neverc_ed25519_sign(priv, (const unsigned char *)msg, strlen(msg), sig1);
    neverc_ed25519_sign(priv, (const unsigned char *)msg, strlen(msg), sig2);
    ASSERT_TRUE(memcmp(sig1, sig2, 64) == 0);
}

int main(void) {
    printf("=== NeverC crypto/ed25519 Tests ===\n");
    test_keygen();
    test_seed_roundtrip();
    test_deterministic_keygen();
    test_sign_verify();
    test_wrong_message();
    test_wrong_key();
    test_empty_message();
    test_deterministic_signature();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
