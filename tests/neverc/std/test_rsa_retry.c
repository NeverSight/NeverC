/*
 * Coverage for the key generator's retry on an unusable prime pair.
 *
 * A prime with p = 1 (mod e) leaves e non-invertible modulo phi = (p-1)(q-1),
 * so no private exponent exists and the pair must be discarded. With the real
 * exponent that happens on roughly one draw in 65537 -- far too rare to reach
 * from a test, and exactly why it once shipped as a key whose d was silently
 * garbage. StdLibTests.cpp compiles this file and rsa.c with a small
 * NCI_RSA_PUBLIC_EXPONENT, which makes about three of every four draws
 * unusable, so every assertion below runs against a generator that had to
 * retry.
 */
#include "neverc/std/crypto/rsa.h"
#include "neverc/std/crypto/sha256.h"
#include <stdio.h>
#include <string.h>

#ifndef NCI_RSA_PUBLIC_EXPONENT
#define NCI_RSA_PUBLIC_EXPONENT 65537
#endif

/* 512-bit keys keep the run short; the retry rate depends only on e. */
#define KEY_BITS 512
#define KEY_COUNT 24

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

/* Without this the file would silently degrade into a slow duplicate of
 * test_rsa.c: the draws would all succeed on the first try and the retry would
 * never run, so a regression in it would go unnoticed. */
static void test_small_exponent_is_in_effect(void) {
    printf("[small_exponent]\n");
    ASSERT_TRUE(NCI_RSA_PUBLIC_EXPONENT < 65537);

    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_TRUE(neverc_rsa_generate_key(&key, KEY_BITS) == 0);
    ASSERT_TRUE(neverc_bigint_int64(&key.pub.e) == NCI_RSA_PUBLIC_EXPONENT);
    neverc_rsa_private_key_free(&key);
}

static void test_keygen_rejects_unusable_primes(void) {
    printf("[retry_until_usable]\n");

    unsigned char hash[32];
    neverc_sha256_sum((const unsigned char *)"retry path", 10, hash);

    for (int i = 0; i < KEY_COUNT; i++) {
        neverc_rsa_private_key_t key;
        neverc_rsa_private_key_init(&key);
        ASSERT_TRUE(neverc_rsa_generate_key(&key, KEY_BITS) == 0);

        neverc_bigint_t one, pm1, qm1, phi, t;
        neverc_bigint_init(&one); neverc_bigint_init(&pm1);
        neverc_bigint_init(&qm1); neverc_bigint_init(&phi);
        neverc_bigint_init(&t);

        neverc_bigint_set_int64(&one, 1);
        neverc_bigint_sub(&pm1, &key.p, &one);
        neverc_bigint_sub(&qm1, &key.q, &one);
        neverc_bigint_mul(&phi, &pm1, &qm1);

        /* Every pair sharing a factor with e must have been thrown away. */
        neverc_bigint_gcd(&t, &key.pub.e, &phi);
        ASSERT_TRUE(neverc_bigint_cmp(&t, &one) == 0);

        /* d really inverts e, rather than being the leftover Bezout term. */
        neverc_bigint_mul(&t, &key.pub.e, &key.d);
        neverc_bigint_mod(&t, &t, &phi);
        ASSERT_TRUE(neverc_bigint_cmp(&t, &one) == 0);

        /* The CRT coefficient belongs to the pair that was kept. */
        neverc_bigint_mul(&t, &key.qinv, &key.q);
        neverc_bigint_mod(&t, &t, &key.p);
        ASSERT_TRUE(neverc_bigint_cmp(&t, &one) == 0);

        unsigned char sig[128];
        size_t sig_len = 0;
        ASSERT_TRUE(neverc_rsa_sign_pkcs1v15_sha256(&key, hash, 32,
                                                    sig, sizeof(sig), &sig_len) == 0);
        ASSERT_TRUE(neverc_rsa_verify_pkcs1v15_sha256(&key.pub, hash, 32,
                                                      sig, sig_len) == 0);

        neverc_bigint_free(&one); neverc_bigint_free(&pm1);
        neverc_bigint_free(&qm1); neverc_bigint_free(&phi);
        neverc_bigint_free(&t);
        neverc_rsa_private_key_free(&key);
    }
}

int main(void) {
    printf("=== NeverC crypto/rsa keygen retry Tests ===\n");
    test_small_exponent_is_in_effect();
    test_keygen_rejects_unusable_primes();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
