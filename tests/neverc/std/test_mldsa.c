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

static int all_zero(const uint8_t *bytes, size_t length) {
    uint8_t combined = 0;
    for (size_t i = 0; i < length; i++) combined |= bytes[i];
    return combined == 0;
}

static void test_keygen_sign_verify(void) {
    printf("  keygen + sign + verify ... ");
    neverc_mldsa44_sk_t sk;
    ASSERT(neverc_mldsa44_generate_key(&sk) == 0, "keygen");

    neverc_mldsa44_pk_t pk;
    neverc_mldsa44_sk_public_key(&sk, &pk);

    const uint8_t msg[] = "Hello, ML-DSA!";
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    neverc_mldsa44_sk_t original_sk = sk;
    ASSERT(neverc_mldsa44_sign(&sk, msg, sizeof(msg)-1, sig) == 0, "sign");
    ASSERT(memcmp(&sk, &original_sk, sizeof(sk)) == 0,
           "sign leaves const secret key unchanged");
    ASSERT(neverc_mldsa44_verify(&pk, msg, sizeof(msg)-1, sig) == 0, "verify");

    uint8_t cleared_sig[NEVERC_MLDSA44_SIG_SIZE];
    memset(cleared_sig, 0x5A, sizeof(cleared_sig));
    ASSERT(neverc_mldsa44_sign(NULL, msg, sizeof(msg) - 1, cleared_sig) == -1,
           "NULL sk rejected");
    ASSERT(all_zero(cleared_sig, sizeof(cleared_sig)),
           "NULL sk clears signature");
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

static void test_nist_acvp_keygen_vector(void) {
    printf("  NIST ACVP keygen vector ... ");
    static const uint8_t seed[32] = {
        0x71,0x94,0xB1,0x3C,0x95,0x23,0x10,0x10,
        0xAF,0xD2,0xC9,0x09,0x99,0x2B,0xD2,0x00,
        0x3B,0xA6,0xF4,0x37,0xC3,0x88,0x6B,0xDB,
        0xE3,0xF6,0xB8,0x67,0xA1,0x4B,0xA1,0x61
    };
    static const uint8_t expected_pk_prefix[64] = {
        0x0B,0x89,0x80,0x6F,0x0E,0xEC,0x39,0xF2,
        0x89,0x11,0x16,0x15,0x2E,0xD4,0x31,0x9D,
        0x42,0x60,0xDF,0xB8,0xAC,0x07,0x10,0x76,
        0x5B,0xD4,0x97,0xE6,0xE1,0xDE,0x17,0x78,
        0x3C,0xF8,0x1E,0x43,0x5A,0x41,0x2E,0xAB,
        0xEF,0x5D,0xB3,0xAF,0x5D,0x15,0x86,0x7B,
        0xBB,0x4C,0x60,0xF8,0xCF,0x98,0xBA,0x31,
        0xBA,0xD6,0xD4,0x1A,0x5F,0x8E,0xB0,0xC1
    };
    neverc_mldsa44_sk_t sk;
    neverc_mldsa44_pk_t pk;
    ASSERT(neverc_mldsa44_new_sk(&sk, seed) == 0, "derive ACVP key");
    neverc_mldsa44_sk_public_key(&sk, &pk);
    ASSERT(memcmp(pk.pk, expected_pk_prefix, sizeof(expected_pk_prefix)) == 0,
           "public key matches NIST ACVP tcId 1");
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
    memset(&pk2, 0x5A, sizeof(pk2));
    ASSERT(neverc_mldsa44_new_pk(
               &pk2, encoded, NEVERC_MLDSA65_PK_SIZE) != 0,
           "reject ML-DSA-65 public-key length");
    ASSERT(all_zero(pk2.pk, sizeof(pk2.pk)),
           "wrong public-key length clears pk");
    memset(&pk2, 0x5A, sizeof(pk2));
    ASSERT(neverc_mldsa44_new_pk(
               &pk2, encoded, NEVERC_MLDSA44_PK_SIZE - 1) != 0,
           "reject truncated public key");
    ASSERT(all_zero(pk2.pk, sizeof(pk2.pk)),
           "truncated public key clears pk");
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

static void test_noncanonical_hint_rejected(void) {
    printf("  noncanonical hint rejection ... ");
    neverc_mldsa44_sk_t sk;
    neverc_mldsa44_pk_t pk;
    ASSERT(neverc_mldsa44_generate_key(&sk) == 0, "keygen");
    neverc_mldsa44_sk_public_key(&sk, &pk);

    const uint8_t msg[] = "canonical encoding";
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    ASSERT(neverc_mldsa44_sign(
               &sk, msg, sizeof(msg) - 1, sig) == 0,
           "sign");
    uint8_t *hint = sig + 32 + 4 * 576;
    int used = hint[80 + 3];
    ASSERT(used >= 0 && used < 80, "signature has an unused hint byte");
    hint[used] = 1;
    ASSERT(neverc_mldsa44_verify(
               &pk, msg, sizeof(msg) - 1, sig) != 0,
           "nonzero unused hint byte must be rejected");
    printf("ok\n");
}

static void test_empty_message(void) {
    printf("  empty message ... ");
    neverc_mldsa44_sk_t sk;
    neverc_mldsa44_pk_t pk;
    ASSERT(neverc_mldsa44_generate_key(&sk) == 0, "keygen");
    neverc_mldsa44_sk_public_key(&sk, &pk);
    uint8_t sig[NEVERC_MLDSA44_SIG_SIZE];
    ASSERT(neverc_mldsa44_sign(&sk, NULL, 0, sig) == 0, "sign empty");
    ASSERT(neverc_mldsa44_verify(&pk, NULL, 0, sig) == 0, "verify empty");
    ASSERT(neverc_mldsa44_verify(
               &pk, (const uint8_t *)"", 0, sig) == 0,
           "verify empty pointer-or-empty");
    printf("ok\n");
}

int main(void) {
    printf("crypto/mldsa tests:\n");
    test_keygen_sign_verify();
    test_wrong_message();
    test_wrong_key();
    test_seed_deterministic();
    test_nist_acvp_keygen_vector();
    test_pk_encode_decode();
    test_tampered_signature();
    test_noncanonical_hint_rejected();
    test_empty_message();
    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
