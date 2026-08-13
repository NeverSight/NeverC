#include "neverc/std/crypto/ecdh.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { \
    tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s\n", __LINE__, #expr); } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    int _a = (a), _b = (b); tests_run++; \
    if (_a == _b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

static void test_p256_keygen(void) {
    printf("[P256 keygen]\n");
    neverc_ecdh_key_t key1, key2;
    ASSERT_EQ(neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P256, &key1), 0);
    ASSERT_EQ(neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P256, &key2), 0);
    ASSERT_EQ(key1.pubkey_len, 65);
    ASSERT_EQ(key1.privkey_len, 32);
    ASSERT_TRUE(key1.public_key[0] == 0x04);
    ASSERT_TRUE(memcmp(key1.public_key, key2.public_key, 65) != 0);
}

static void test_p256_ecdh(void) {
    printf("[P256 ECDH]\n");
    neverc_ecdh_key_t alice, bob;
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P256, &alice);
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P256, &bob);

    unsigned char shared_a[32], shared_b[32];
    int len_a = neverc_ecdh_compute(&alice, bob.public_key, (size_t)bob.pubkey_len, shared_a, sizeof(shared_a));
    int len_b = neverc_ecdh_compute(&bob, alice.public_key, (size_t)alice.pubkey_len, shared_b, sizeof(shared_b));

    ASSERT_EQ(len_a, 32);
    ASSERT_EQ(len_b, 32);
    ASSERT_TRUE(memcmp(shared_a, shared_b, 32) == 0);
}

static void test_p384_ecdh(void) {
    printf("[P384 ECDH]\n");
    neverc_ecdh_key_t alice, bob;
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P384, &alice);
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P384, &bob);

    ASSERT_EQ(alice.pubkey_len, 97);
    ASSERT_EQ(alice.privkey_len, 48);
    ASSERT_TRUE(alice.public_key[0] == 0x04);

    unsigned char shared_a[48], shared_b[48];
    int len_a = neverc_ecdh_compute(&alice, bob.public_key, (size_t)bob.pubkey_len, shared_a, sizeof(shared_a));
    int len_b = neverc_ecdh_compute(&bob, alice.public_key, (size_t)alice.pubkey_len, shared_b, sizeof(shared_b));

    ASSERT_EQ(len_a, 48);
    ASSERT_EQ(len_b, 48);
    ASSERT_TRUE(memcmp(shared_a, shared_b, 48) == 0);
}

static void test_x25519_keygen(void) {
    printf("[X25519 keygen]\n");
    neverc_ecdh_key_t key;
    ASSERT_EQ(neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519, &key), 0);
    ASSERT_EQ(key.pubkey_len, 32);
    ASSERT_EQ(key.privkey_len, 32);
}

static void test_x25519_ecdh(void) {
    printf("[X25519 ECDH]\n");
    neverc_ecdh_key_t alice, bob;
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519, &alice);
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519, &bob);

    unsigned char shared_a[32], shared_b[32];
    int len_a = neverc_ecdh_compute(&alice, bob.public_key, 32, shared_a, sizeof(shared_a));
    int len_b = neverc_ecdh_compute(&bob, alice.public_key, 32, shared_b, sizeof(shared_b));

    ASSERT_EQ(len_a, 32);
    ASSERT_EQ(len_b, 32);
    ASSERT_TRUE(memcmp(shared_a, shared_b, 32) == 0);
}

static void test_x25519_rfc7748_vector(void) {
    printf("[X25519 RFC 7748 test vector]\n");
    /* RFC 7748 Section 6.1 test vector */
    unsigned char alice_priv[32] = {
        0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d,
        0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
        0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a,
        0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a,
    };
    unsigned char bob_priv[32] = {
        0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b,
        0x79, 0xe1, 0x7f, 0x8b, 0x83, 0x80, 0x0e, 0xe6,
        0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18, 0xb6, 0xfd,
        0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb,
    };
    unsigned char expected_shared[32] = {
        0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1,
        0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25,
        0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33,
        0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42,
    };

    neverc_ecdh_key_t alice, bob;
    ASSERT_EQ(neverc_ecdh_new_private_key(NEVERC_ECDH_CURVE_X25519, alice_priv, 32, &alice), 0);
    ASSERT_EQ(neverc_ecdh_new_private_key(NEVERC_ECDH_CURVE_X25519, bob_priv, 32, &bob), 0);
    unsigned char exported[32];
    ASSERT_EQ(neverc_ecdh_private_key_bytes(
                  &alice, exported, SIZE_MAX), 32);
    ASSERT_TRUE(memcmp(exported, alice_priv, sizeof(exported)) == 0);
    ASSERT_EQ(neverc_ecdh_public_key_bytes(
                  &alice, exported, SIZE_MAX), 32);

    unsigned char shared_a[32], shared_b[32];
    int len_a = neverc_ecdh_compute(&alice, bob.public_key, 32, shared_a, sizeof(shared_a));
    int len_b = neverc_ecdh_compute(&bob, alice.public_key, 32, shared_b, sizeof(shared_b));

    ASSERT_EQ(len_a, 32);
    ASSERT_EQ(len_b, 32);
    ASSERT_TRUE(memcmp(shared_a, shared_b, 32) == 0);
    ASSERT_TRUE(memcmp(shared_a, expected_shared, 32) == 0);
}

static void test_import_export(void) {
    printf("[import/export keys]\n");
    neverc_ecdh_key_t key;
    neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_P256, &key);

    unsigned char priv[32], pub[65];
    int plen = neverc_ecdh_private_key_bytes(&key, priv, sizeof(priv));
    int qlen = neverc_ecdh_public_key_bytes(&key, pub, sizeof(pub));
    ASSERT_EQ(plen, 32);
    ASSERT_EQ(qlen, 65);

    neverc_ecdh_key_t reimported;
    ASSERT_EQ(neverc_ecdh_new_private_key(NEVERC_ECDH_CURVE_P256, priv, (size_t)plen, &reimported), 0);
    ASSERT_TRUE(memcmp(reimported.public_key, pub, 65) == 0);

    neverc_ecdh_key_t pub_only;
    ASSERT_EQ(neverc_ecdh_new_public_key(NEVERC_ECDH_CURVE_P256, pub, (size_t)qlen, &pub_only), 0);
    ASSERT_TRUE(memcmp(pub_only.public_key, pub, 65) == 0);
}

static void test_invalid_inputs(void) {
    printf("[invalid inputs]\n");
    neverc_ecdh_key_t key;
    ASSERT_EQ(neverc_ecdh_generate_key((neverc_ecdh_curve_t)99, &key), -1);
    ASSERT_EQ(neverc_ecdh_new_private_key(NEVERC_ECDH_CURVE_P256, NULL, 32, &key), -1);
    ASSERT_EQ(neverc_ecdh_new_private_key(NEVERC_ECDH_CURVE_X25519, (unsigned char*)"x", 1, &key), -1);

    unsigned char zero_priv[32] = {0};
    ASSERT_EQ(neverc_ecdh_new_private_key(NEVERC_ECDH_CURVE_P256, zero_priv, 32, &key), -1);

    unsigned char basepoint[32] = {9};
    ASSERT_EQ(neverc_ecdh_new_public_key(
                  NEVERC_ECDH_CURVE_X25519, basepoint,
                  sizeof(basepoint), &key), 0);
    unsigned char shared[32];
    ASSERT_EQ(neverc_ecdh_compute(
                  &key, basepoint, sizeof(basepoint),
                  shared, sizeof(shared)), -1);
}

int main(void) {
    printf("=== NeverC crypto/ecdh Tests ===\n");
    test_x25519_keygen();
    test_x25519_ecdh();
    test_x25519_rfc7748_vector();
    test_invalid_inputs();
#ifdef NEVERC_TEST_SLOW
    /* P-256/P-384 tests use bigint-based scalar multiplication which is slow.
       Run with -DNEVERC_TEST_SLOW to enable. */
    test_p256_keygen();
    test_p256_ecdh();
    test_p384_ecdh();
    test_import_export();
#else
    printf("  [P-256/P-384 tests skipped — enable with -DNEVERC_TEST_SLOW]\n");
#endif
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
