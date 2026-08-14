/*
 * NeverC crypto/dsa tests.
 * Uses Go's official DSA test parameters (from crypto/dsa/dsa_test.go).
 */
#include "neverc/std/crypto/dsa.h"
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

static void setup_go_dsa_key(neverc_dsa_private_key_t *key) {
    neverc_bigint_set_string(&key->pub.p,
        "A9B5B793FB4785793D246BAE77E8FF63CA52F442DA763C440259919FE1BC1D60"
        "65A9350637A04F75A2F039401D49F08E066C4D275A5A65DA5684BC563C14289D"
        "7AB8A67163BFBF79D85972619AD2CFF55AB0EE77A9002B0EF96293BDD0F42685"
        "EBB2C66C327079F6C98000FBCB79AACDE1BC6F9D5C7B1A97E3D9D54ED7951FEF", 16);
    neverc_bigint_set_string(&key->pub.q,
        "E1D3391245933D68A0714ED34BBCB7A1F422B9C1", 16);
    neverc_bigint_set_string(&key->pub.g,
        "634364FC25248933D01D1993ECABD0657CC0CB2CEED7ED2E3E8AECDFCDC4A25C"
        "3B15E9E3B163ACA2984B5539181F3EFF1A5E8903D71D5B95DA4F27202B77D2C4"
        "4B430BB53741A8D59A8F86887525C9F2A6A5980A195EAA7F2FF910064301DEF8"
        "9D3AA213E1FAC7768D89365318E370AF54A112EFBA9246D9158386BA1B4EEFDA", 16);
    neverc_bigint_set_string(&key->pub.y,
        "32969E5780CFE1C849A1C276D7AEB4F38A23B591739AA2FE197349AEEBD31366"
        "AEE5EB7E6C6DDB7C57D02432B30DB5AA66D9884299FAA72568944E4EEDC92EA3"
        "FBC6F39F53412FBCC563208F7C15B737AC8910DBC2D9C9B8C001E72FDC40EB69"
        "4AB1F06A5A2DBD18D9E36C66F31F566742F11EC0A52E9F7B89355C02FB5D32D2", 16);
    neverc_bigint_set_string(&key->x,
        "5078D4D29795CBE76D3AACFE48C9AF0BCDBEE91A", 16);
}

static void test_init_free(void) {
    printf("[init_free]\n");
    neverc_dsa_public_key_init(NULL);
    neverc_dsa_public_key_free(NULL);
    neverc_dsa_private_key_init(NULL);
    neverc_dsa_private_key_free(NULL);
    neverc_dsa_signature_init(NULL);
    neverc_dsa_signature_free(NULL);

    neverc_dsa_public_key_t pub;
    neverc_dsa_public_key_init(&pub);
    ASSERT_TRUE(neverc_bigint_is_zero(&pub.p));
    neverc_dsa_public_key_free(&pub);

    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    ASSERT_TRUE(neverc_bigint_is_zero(&key.x));
    ASSERT_TRUE(neverc_bigint_is_zero(&key.pub.p));
    neverc_dsa_private_key_free(&key);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);
    ASSERT_TRUE(neverc_bigint_is_zero(&sig.r));
    neverc_dsa_signature_free(&sig);
}

static void test_sign_verify(void) {
    printf("[sign_verify]\n");
    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    setup_go_dsa_key(&key);

    uint8_t hash[32];
    neverc_sha256_sum((const uint8_t *)"testing", 7, hash);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);

    ASSERT_INT_EQ(neverc_dsa_sign(&key, hash, 32, &sig), 0);
    ASSERT_TRUE(!neverc_bigint_is_zero(&sig.r));
    ASSERT_TRUE(!neverc_bigint_is_zero(&sig.s));

    ASSERT_INT_EQ(neverc_dsa_verify(&key.pub, hash, 32, &sig), 0);

    neverc_dsa_signature_free(&sig);
    neverc_dsa_private_key_free(&key);
}

static void test_verify_tampered(void) {
    printf("[verify_tampered]\n");
    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    setup_go_dsa_key(&key);

    uint8_t hash[32];
    neverc_sha256_sum((const uint8_t *)"message", 7, hash);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);
    neverc_dsa_sign(&key, hash, 32, &sig);

    hash[0] ^= 0xFF;
    ASSERT_TRUE(neverc_dsa_verify(&key.pub, hash, 32, &sig) != 0);

    neverc_dsa_signature_free(&sig);
    neverc_dsa_private_key_free(&key);
}

static void test_verify_zero_sig(void) {
    printf("[zero_signature]\n");
    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    setup_go_dsa_key(&key);

    uint8_t hash[32];
    neverc_sha256_sum((const uint8_t *)"test", 4, hash);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);
    ASSERT_TRUE(neverc_dsa_verify(&key.pub, hash, 32, &sig) != 0);

    neverc_dsa_signature_free(&sig);
    neverc_dsa_private_key_free(&key);
}

static void test_verify_identity_public_key(void) {
    printf("[identity_public_key]\n");
    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    setup_go_dsa_key(&key);

    uint8_t hash[32];
    neverc_sha256_sum((const uint8_t *)"forge", 5, hash);

    /* x=0 forgery: r = (g^1 mod p) mod q, s = z. Verifies iff y=1. */
    neverc_bigint_t z;
    neverc_bigint_init(&z);
    char hex[41];
    for (int i = 0; i < 20; i++)
        sprintf(hex + 2 * i, "%02x", hash[i]);
    neverc_bigint_set_string(&z, hex, 16);
    if (neverc_bigint_cmp(&z, &key.pub.q) >= 0)
        neverc_bigint_mod(&z, &z, &key.pub.q);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);
    neverc_bigint_mod(&sig.r, &key.pub.g, &key.pub.p);
    neverc_bigint_mod(&sig.r, &sig.r, &key.pub.q);
    neverc_bigint_set(&sig.s, &z);

    neverc_bigint_set_int64(&key.pub.y, 1);
    ASSERT_TRUE(neverc_dsa_verify(&key.pub, hash, 32, &sig) != 0);

    neverc_bigint_set_int64(&key.pub.g, 1);
    neverc_bigint_set_int64(&sig.r, 1);
    neverc_bigint_set_int64(&sig.s, 1);
    ASSERT_TRUE(neverc_dsa_verify(&key.pub, hash, 32, &sig) != 0);

    neverc_bigint_free(&z);
    neverc_dsa_signature_free(&sig);
    neverc_dsa_private_key_free(&key);
}

static void test_sign_rejects_weak_private_key(void) {
    printf("[weak_private_key]\n");
    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    setup_go_dsa_key(&key);

    uint8_t hash[32];
    neverc_sha256_sum((const uint8_t *)"x", 1, hash);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);
    neverc_bigint_set_int64(&sig.r, 99);
    neverc_bigint_set_int64(&sig.s, 99);

    neverc_bigint_set_int64(&key.x, 0);
    ASSERT_TRUE(neverc_dsa_sign(&key, hash, 32, &sig) != 0);
    ASSERT_TRUE(neverc_bigint_is_zero(&sig.r));
    ASSERT_TRUE(neverc_bigint_is_zero(&sig.s));

    neverc_bigint_set(&key.x, &key.pub.q);
    neverc_bigint_set_int64(&sig.r, 99);
    neverc_bigint_set_int64(&sig.s, 99);
    ASSERT_TRUE(neverc_dsa_sign(&key, hash, 32, &sig) != 0);
    ASSERT_TRUE(neverc_bigint_is_zero(&sig.r));
    ASSERT_TRUE(neverc_bigint_is_zero(&sig.s));

    neverc_dsa_signature_free(&sig);
    neverc_dsa_private_key_free(&key);
}

static void test_verify_negative_sig(void) {
    printf("[negative_signature]\n");
    neverc_dsa_private_key_t key;
    neverc_dsa_private_key_init(&key);
    setup_go_dsa_key(&key);

    uint8_t hash[32];
    neverc_sha256_sum((const uint8_t *)"test", 4, hash);

    neverc_dsa_signature_t sig;
    neverc_dsa_signature_init(&sig);
    neverc_bigint_set_int64(&sig.r, -1);
    neverc_bigint_set_int64(&sig.s, 1);
    ASSERT_TRUE(neverc_dsa_verify(&key.pub, hash, 32, &sig) != 0);

    neverc_dsa_signature_free(&sig);
    neverc_dsa_private_key_free(&key);
}

int main(void) {
    printf("=== NeverC DSA Tests ===\n");
    test_init_free();
    test_sign_verify();
    test_verify_tampered();
    test_verify_zero_sig();
    test_verify_negative_sig();
    test_verify_identity_public_key();
    test_sign_rejects_weak_private_key();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
