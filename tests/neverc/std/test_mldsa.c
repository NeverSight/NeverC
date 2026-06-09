/*
 * Test suite for ML-DSA-44 (NIST FIPS 204) — neverc std crypto/mldsa.
 */
#include <stdio.h>
#include <string.h>
#include "neverc/std/crypto/mldsa.h"

static int tests_run   = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do {                                \
    tests_run++;                                              \
    if (!(cond)) { printf("  FAIL: %s\n", msg); return; }    \
    tests_passed++;                                           \
} while(0)

static void test_keygen_sign_verify(void) {
    printf("  keygen + sign + verify ... ");
    neverc_mldsa44_sk_t sk;
    ASSERT(neverc_mldsa44_generate_key(&sk) == 0, "keygen");

    neverc_mldsa44_pk_t pk;
    neverc_mldsa44_sk_public_key(&sk, &pk);

    const uint8_t msg[] = "Hello, ML-DSA!";
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    ASSERT(neverc_mldsa44_sign(&sk, msg, sizeof(msg)-1, sig) == 0, "sign");
    ASSERT(neverc_mldsa44_verify(&pk, msg, sizeof(msg)-1, sig) == 0, "verify");
    printf("ok\n");
}

static void test_wrong_message(void) {
    printf("  wrong message rejection ... ");
    neverc_mldsa44_sk_t sk;
    neverc_mldsa44_generate_key(&sk);
    neverc_mldsa44_pk_t pk;
    neverc_mldsa44_sk_public_key(&sk, &pk);

    const uint8_t msg1[] = "correct message";
    const uint8_t msg2[] = "wrong message!!";
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    neverc_mldsa44_sign(&sk, msg1, sizeof(msg1)-1, sig);

    int rc = neverc_mldsa44_verify(&pk, msg2, sizeof(msg2)-1, sig);
    ASSERT(rc != 0, "wrong message should fail");
    printf("ok\n");
}

static void test_wrong_key(void) {
    printf("  wrong key rejection ... ");
    neverc_mldsa44_sk_t sk1, sk2;
    neverc_mldsa44_generate_key(&sk1);
    neverc_mldsa44_generate_key(&sk2);
    neverc_mldsa44_pk_t pk2;
    neverc_mldsa44_sk_public_key(&sk2, &pk2);

    const uint8_t msg[] = "signed by sk1";
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    neverc_mldsa44_sign(&sk1, msg, sizeof(msg)-1, sig);

    int rc = neverc_mldsa44_verify(&pk2, msg, sizeof(msg)-1, sig);
    ASSERT(rc != 0, "wrong pk should fail");
    printf("ok\n");
}

static void test_seed_deterministic(void) {
    printf("  seed deterministic keygen ... ");
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t)(i * 11 + 5);

    neverc_mldsa44_sk_t sk1, sk2;
    neverc_mldsa44_new_sk(&sk1, seed);
    neverc_mldsa44_new_sk(&sk2, seed);

    neverc_mldsa44_pk_t pk1, pk2;
    neverc_mldsa44_sk_public_key(&sk1, &pk1);
    neverc_mldsa44_sk_public_key(&sk2, &pk2);
    ASSERT(memcmp(pk1.pk, pk2.pk, NEVERC_MLDSA44_PK_SIZE) == 0,
           "same seed → same pk");

    uint8_t seed_out[32];
    neverc_mldsa44_sk_bytes(&sk1, seed_out);
    ASSERT(memcmp(seed, seed_out, 32) == 0, "sk_bytes round-trip");
    printf("ok\n");
}

static void test_pk_encode_decode(void) {
    printf("  pk encode/decode ... ");
    neverc_mldsa44_sk_t sk;
    neverc_mldsa44_generate_key(&sk);
    neverc_mldsa44_pk_t pk;
    neverc_mldsa44_sk_public_key(&sk, &pk);

    uint8_t encoded[NEVERC_MLDSA44_PK_SIZE];
    size_t encoded_len;
    neverc_mldsa44_pk_bytes(&pk, encoded, &encoded_len);
    ASSERT(encoded_len == NEVERC_MLDSA44_PK_SIZE, "pk size");

    neverc_mldsa44_pk_t pk2;
    ASSERT(neverc_mldsa44_new_pk(&pk2, encoded, encoded_len) == 0, "parse pk");
    ASSERT(memcmp(pk.pk, pk2.pk, NEVERC_MLDSA44_PK_SIZE) == 0, "pk match");
    printf("ok\n");
}

static void test_tampered_signature(void) {
    printf("  tampered signature rejection ... ");
    neverc_mldsa44_sk_t sk;
    neverc_mldsa44_generate_key(&sk);
    neverc_mldsa44_pk_t pk;
    neverc_mldsa44_sk_public_key(&sk, &pk);

    const uint8_t msg[] = "integrity test";
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    neverc_mldsa44_sign(&sk, msg, sizeof(msg)-1, sig);

    sig[50] ^= 0xFF;
    int rc = neverc_mldsa44_verify(&pk, msg, sizeof(msg)-1, sig);
    ASSERT(rc != 0, "tampered sig should fail");
    printf("ok\n");
}

int main(void) {
    printf("crypto/mldsa tests:\n");
    test_keygen_sign_verify();
    test_wrong_message();
    test_wrong_key();
    test_seed_deterministic();
    test_pk_encode_decode();
    test_tampered_signature();
    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
