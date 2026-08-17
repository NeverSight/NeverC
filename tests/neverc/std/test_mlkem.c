/*
 * Test suite for ML-KEM (NIST FIPS 203) — neverc std crypto/mlkem.
 *
 * Tests:
 *   1. ML-KEM-768: keygen + encapsulate + decapsulate round-trip
 *   2. ML-KEM-768: seed-based deterministic keygen
 *   3. ML-KEM-768: encapsulation key encode/decode round-trip
 *   4. ML-KEM-768: wrong key produces different shared secret (implicit rejection)
 *   5. ML-KEM-768: multiple encapsulations produce different ciphertexts
 *   6. ML-KEM-1024: keygen + encapsulate + decapsulate round-trip
 *   7. ML-KEM-1024: seed-based deterministic keygen
 */
#include <stdio.h>
#include <string.h>
#include "neverc/std/crypto/mlkem.h"
#include "neverc/std/crypto/sha3.h"

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

static void test_768_roundtrip(void) {
    printf("  768 keygen+encaps+decaps ... ");
    neverc_mlkem768_dk_t dk;
    ASSERT(neverc_mlkem768_generate_key(&dk) == 0, "keygen");

    neverc_mlkem768_ek_t ek;
    neverc_mlkem768_dk_encapsulation_key(&dk, &ek);

    uint8_t shared_key_s[32], ciphertext[NEVERC_MLKEM768_CT_SIZE];
    ASSERT(neverc_mlkem768_encapsulate(&ek, shared_key_s, ciphertext) == 0, "encaps");

    uint8_t shared_key_r[32];
    ASSERT(neverc_mlkem768_decapsulate(&dk, ciphertext, shared_key_r) == 0, "decaps");

    ASSERT(memcmp(shared_key_s, shared_key_r, 32) == 0, "shared keys match");
    printf("ok\n");
}

static void test_768_seed_deterministic(void) {
    printf("  768 seed deterministic ... ");
    uint8_t seed[64];
    for (int i = 0; i < 64; i++) seed[i] = (uint8_t)(i * 7 + 3);

    neverc_mlkem768_dk_t dk1, dk2;
    ASSERT(neverc_mlkem768_new_dk(&dk1, seed) == 0, "new_dk 1");
    ASSERT(neverc_mlkem768_new_dk(&dk2, seed) == 0, "new_dk 2");

    neverc_mlkem768_ek_t ek1, ek2;
    neverc_mlkem768_dk_encapsulation_key(&dk1, &ek1);
    neverc_mlkem768_dk_encapsulation_key(&dk2, &ek2);
    ASSERT(memcmp(ek1.ek, ek2.ek, NEVERC_MLKEM768_EK_SIZE) == 0,
           "same seed → same ek");

    uint8_t seed_out[64];
    neverc_mlkem768_dk_bytes(&dk1, seed_out);
    ASSERT(memcmp(seed, seed_out, 64) == 0, "dk_bytes round-trip");
    printf("ok\n");
}

static void test_768_ek_encode_decode(void) {
    printf("  768 ek encode/decode ... ");
    neverc_mlkem768_dk_t dk;
    neverc_mlkem768_generate_key(&dk);

    neverc_mlkem768_ek_t ek;
    neverc_mlkem768_dk_encapsulation_key(&dk, &ek);

    uint8_t encoded[NEVERC_MLKEM768_EK_SIZE];
    size_t encoded_len;
    neverc_mlkem768_ek_bytes(&ek, encoded, &encoded_len);
    ASSERT(encoded_len == NEVERC_MLKEM768_EK_SIZE, "ek size");

    neverc_mlkem768_ek_t ek2;
    ASSERT(neverc_mlkem768_new_ek(&ek2, encoded, encoded_len) == 0, "parse ek");
    ASSERT(memcmp(ek.ek, ek2.ek, NEVERC_MLKEM768_EK_SIZE) == 0, "ek match");

    /* Encapsulate with decoded key should work */
    uint8_t sk[32], ct[NEVERC_MLKEM768_CT_SIZE];
    ASSERT(neverc_mlkem768_encapsulate(&ek2, sk, ct) == 0, "encaps with decoded ek");

    uint8_t sk2[32];
    ASSERT(neverc_mlkem768_decapsulate(&dk, ct, sk2) == 0, "decaps");
    ASSERT(memcmp(sk, sk2, 32) == 0, "shared keys match");

    encoded[0] = 0xFF;
    encoded[1] = (uint8_t)((encoded[1] & 0xF0) | 0x0F);
    memset(&ek2, 0x5A, sizeof(ek2));
    ASSERT(neverc_mlkem768_new_ek(
               &ek2, encoded, sizeof(encoded)) == -1,
           "reject non-canonical coefficient");
    ASSERT(all_zero(ek2.ek, sizeof(ek2.ek)),
           "failed key parse clears output");
    memset(sk, 0x5A, sizeof(sk));
    memset(ct, 0x5A, sizeof(ct));
    ASSERT(neverc_mlkem768_encapsulate(&ek2, sk, ct) == -1,
           "reject malformed encapsulation key");
    ASSERT(all_zero(sk, sizeof(sk)) && all_zero(ct, sizeof(ct)),
           "malformed key clears encapsulation outputs");

    memset(sk, 0x5A, sizeof(sk));
    memset(ct, 0x5A, sizeof(ct));
    ASSERT(neverc_mlkem768_encapsulate(NULL, sk, ct) == -1,
           "NULL ek is invalid input");
    ASSERT(all_zero(sk, sizeof(sk)) && all_zero(ct, sizeof(ct)),
           "NULL ek clears encapsulation outputs");

    neverc_mlkem1024_ek_t ek1024;
    memset(&ek1024, 0x5A, sizeof(ek1024));
    ASSERT(neverc_mlkem1024_new_ek(
               &ek1024, encoded, NEVERC_MLKEM768_EK_SIZE) == -1,
           "1024 parser rejects 768 encapsulation key");
    ASSERT(all_zero(ek1024.ek, sizeof(ek1024.ek)),
           "wrong parameter set clears 1024 ek");
    printf("ok\n");
}

static void test_768_nist_acvp_keygen_vector(void) {
    printf("  768 NIST ACVP keygen vector ... ");
    static const uint8_t seed[NEVERC_MLKEM_SEED_SIZE] = {
        /* d */
        0xE5,0x82,0xB7,0xD7,0x5E,0x6C,0x80,0xB0,
        0x5A,0xE3,0x92,0xA1,0xFC,0x9F,0x71,0x53,
        0xB1,0x23,0x90,0xFD,0x99,0x93,0x03,0x68,
        0xCC,0x67,0xA7,0x68,0xBA,0xEB,0xC8,0xA0,
        /* z */
        0x1C,0xDA,0xCB,0x87,0x40,0xC0,0xB8,0x7C,
        0x4A,0x37,0x95,0x75,0xF1,0x87,0xB3,0x67,
        0xCB,0xFA,0x3B,0x30,0x0B,0xF5,0x91,0xB1,
        0x09,0xF7,0x98,0x16,0xE9,0xCB,0xE8,0xF0
    };
    static const uint8_t expected_ek_prefix[64] = {
        0x28,0xC7,0x93,0x77,0x87,0x41,0xB8,0x0B,
        0x02,0xB4,0x33,0x9F,0x2A,0xA4,0x34,0x72,
        0x55,0xB0,0x99,0xF1,0x72,0x64,0xE1,0xB8,
        0xCC,0x0A,0x2C,0x7C,0x2A,0x1A,0x79,0xF7,
        0x99,0x7B,0x90,0x7F,0xD0,0x49,0x6C,0x6E,
        0x6C,0x8A,0xD7,0x71,0x4F,0x5F,0x33,0x9D,
        0x75,0xF1,0x1F,0x62,0x55,0x91,0xA8,0x69,
        0xBE,0x11,0x75,0xAE,0x47,0xF0,0x5F,0xD4
    };
    neverc_mlkem768_dk_t dk;
    neverc_mlkem768_ek_t ek;
    ASSERT(neverc_mlkem768_new_dk(&dk, seed) == 0, "derive ACVP key");
    neverc_mlkem768_dk_encapsulation_key(&dk, &ek);
    ASSERT(memcmp(ek.ek, expected_ek_prefix, sizeof(expected_ek_prefix)) == 0,
           "encapsulation key matches NIST ACVP tcId 26");
    neverc_mlkem768_ek_t parsed;
    ASSERT(neverc_mlkem768_new_ek(
               &parsed, ek.ek, NEVERC_MLKEM768_EK_SIZE) == 0,
           "ACVP ek coefficients are in Z_q");
    printf("ok\n");
}

static void test_768_wrong_key(void) {
    printf("  768 wrong key implicit rejection ... ");
    neverc_mlkem768_dk_t dk1, dk2;
    neverc_mlkem768_generate_key(&dk1);
    neverc_mlkem768_generate_key(&dk2);

    neverc_mlkem768_ek_t ek1;
    neverc_mlkem768_dk_encapsulation_key(&dk1, &ek1);

    uint8_t shared_key[32], ct[NEVERC_MLKEM768_CT_SIZE];
    neverc_mlkem768_encapsulate(&ek1, shared_key, ct);

    uint8_t wrong_key[32];
    neverc_mlkem768_decapsulate(&dk2, ct, wrong_key);

    ASSERT(memcmp(shared_key, wrong_key, 32) != 0,
           "wrong dk produces different shared key");
    printf("ok\n");
}

static void test_768_implicit_rejection_value(void) {
    printf("  768 implicit rejection value ... ");
    uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
    for (int i = 0; i < NEVERC_MLKEM_SEED_SIZE; i++)
        seed[i] = (uint8_t)(i * 19 + 1);
    neverc_mlkem768_dk_t dk;
    ASSERT(neverc_mlkem768_new_dk(&dk, seed) == 0, "new deterministic dk");

    uint8_t ciphertext[NEVERC_MLKEM768_CT_SIZE] = {0};
    uint8_t shared_key[NEVERC_MLKEM_SHARED_KEY_SIZE];
    uint8_t expected[NEVERC_MLKEM_SHARED_KEY_SIZE];
    neverc_sha3_ctx hash;
    neverc_shake256_init(&hash);
    neverc_shake256_update(&hash, seed + 32, 32);
    neverc_shake256_update(&hash, ciphertext, sizeof(ciphertext));
    neverc_shake256_squeeze(&hash, expected, sizeof(expected));

    ASSERT(neverc_mlkem768_decapsulate(
               &dk, ciphertext, shared_key) == 0,
           "decapsulate malformed ciphertext");
    ASSERT(memcmp(shared_key, expected, sizeof(expected)) == 0,
           "implicit rejection derives J(z || ciphertext)");
    printf("ok\n");
}

static void test_768_multiple_encaps(void) {
    printf("  768 multiple encapsulations differ ... ");
    neverc_mlkem768_dk_t dk;
    neverc_mlkem768_generate_key(&dk);
    neverc_mlkem768_ek_t ek;
    neverc_mlkem768_dk_encapsulation_key(&dk, &ek);

    uint8_t sk1[32], ct1[NEVERC_MLKEM768_CT_SIZE];
    uint8_t sk2[32], ct2[NEVERC_MLKEM768_CT_SIZE];
    neverc_mlkem768_encapsulate(&ek, sk1, ct1);
    neverc_mlkem768_encapsulate(&ek, sk2, ct2);

    ASSERT(memcmp(ct1, ct2, NEVERC_MLKEM768_CT_SIZE) != 0,
           "ciphertexts differ");
    ASSERT(memcmp(sk1, sk2, 32) != 0, "shared keys differ");

    uint8_t dec1[32], dec2[32];
    neverc_mlkem768_decapsulate(&dk, ct1, dec1);
    neverc_mlkem768_decapsulate(&dk, ct2, dec2);
    ASSERT(memcmp(sk1, dec1, 32) == 0, "first decaps matches");
    ASSERT(memcmp(sk2, dec2, 32) == 0, "second decaps matches");
    printf("ok\n");
}

static void test_1024_roundtrip(void) {
    printf("  1024 keygen+encaps+decaps ... ");
    neverc_mlkem1024_dk_t dk;
    ASSERT(neverc_mlkem1024_generate_key(&dk) == 0, "keygen");

    neverc_mlkem1024_ek_t ek;
    neverc_mlkem1024_dk_encapsulation_key(&dk, &ek);

    uint8_t shared_key_s[32], ciphertext[NEVERC_MLKEM1024_CT_SIZE];
    ASSERT(neverc_mlkem1024_encapsulate(&ek, shared_key_s, ciphertext) == 0, "encaps");

    uint8_t shared_key_r[32];
    ASSERT(neverc_mlkem1024_decapsulate(&dk, ciphertext, shared_key_r) == 0, "decaps");

    ASSERT(memcmp(shared_key_s, shared_key_r, 32) == 0, "shared keys match");
    printf("ok\n");
}

static void test_1024_seed_deterministic(void) {
    printf("  1024 seed deterministic ... ");
    uint8_t seed[64];
    for (int i = 0; i < 64; i++) seed[i] = (uint8_t)(i * 13 + 42);

    neverc_mlkem1024_dk_t dk1, dk2;
    ASSERT(neverc_mlkem1024_new_dk(&dk1, seed) == 0, "new_dk 1");
    ASSERT(neverc_mlkem1024_new_dk(&dk2, seed) == 0, "new_dk 2");

    neverc_mlkem1024_ek_t ek1, ek2;
    neverc_mlkem1024_dk_encapsulation_key(&dk1, &ek1);
    neverc_mlkem1024_dk_encapsulation_key(&dk2, &ek2);
    ASSERT(memcmp(ek1.ek, ek2.ek, NEVERC_MLKEM1024_EK_SIZE) == 0,
           "same seed → same ek");

    uint8_t seed_out[64];
    neverc_mlkem1024_dk_bytes(&dk1, seed_out);
    ASSERT(memcmp(seed, seed_out, 64) == 0, "dk_bytes round-trip");

    uint8_t encoded[NEVERC_MLKEM1024_EK_SIZE];
    size_t encoded_len = 0;
    neverc_mlkem1024_ek_bytes(&ek1, encoded, &encoded_len);
    ASSERT(encoded_len == NEVERC_MLKEM1024_EK_SIZE, "ek size");
    neverc_mlkem1024_ek_t ek3;
    ASSERT(neverc_mlkem1024_new_ek(&ek3, encoded, encoded_len) == 0, "parse ek");
    ASSERT(memcmp(ek1.ek, ek3.ek, NEVERC_MLKEM1024_EK_SIZE) == 0, "ek match");

    encoded[0] = 0xFF;
    encoded[1] = (uint8_t)((encoded[1] & 0xF0) | 0x0F);
    memset(&ek3, 0x5A, sizeof(ek3));
    ASSERT(neverc_mlkem1024_new_ek(
               &ek3, encoded, sizeof(encoded)) == -1,
           "1024 rejects non-canonical coefficient");
    ASSERT(all_zero(ek3.ek, sizeof(ek3.ek)),
           "failed 1024 key parse clears output");

    neverc_mlkem768_ek_t ek768;
    memset(&ek768, 0x5A, sizeof(ek768));
    ASSERT(neverc_mlkem768_new_ek(
               &ek768, ek1.ek, NEVERC_MLKEM1024_EK_SIZE) == -1,
           "768 parser rejects 1024 encapsulation key");
    ASSERT(all_zero(ek768.ek, sizeof(ek768.ek)),
           "wrong parameter set clears 768 ek");

    uint8_t sk1024[32], ct1024[NEVERC_MLKEM1024_CT_SIZE];
    memset(sk1024, 0x5A, sizeof(sk1024));
    memset(ct1024, 0x5A, sizeof(ct1024));
    ASSERT(neverc_mlkem1024_encapsulate(NULL, sk1024, ct1024) == -1,
           "NULL 1024 ek is invalid input");
    ASSERT(all_zero(sk1024, sizeof(sk1024)) && all_zero(ct1024, sizeof(ct1024)),
           "NULL 1024 ek clears encapsulation outputs");
    printf("ok\n");
}

static void test_768_seed_is_decaps_source_of_truth(void) {
    printf("  768 seed-based dk ignores tampered ek cache ... ");
    uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
    for (int i = 0; i < NEVERC_MLKEM_SEED_SIZE; i++)
        seed[i] = (uint8_t)(i * 11 + 5);
    neverc_mlkem768_dk_t dk;
    ASSERT(neverc_mlkem768_new_dk(&dk, seed) == 0, "new_dk");
    neverc_mlkem768_ek_t ek;
    neverc_mlkem768_dk_encapsulation_key(&dk, &ek);

    uint8_t shared_key[32], ciphertext[NEVERC_MLKEM768_CT_SIZE];
    ASSERT(neverc_mlkem768_encapsulate(&ek, shared_key, ciphertext) == 0,
           "encaps");

    dk.ek[0] ^= 1U;
    uint8_t recovered[32];
    ASSERT(neverc_mlkem768_decapsulate(&dk, ciphertext, recovered) == 0,
           "decaps with tampered cached ek");
    ASSERT(memcmp(shared_key, recovered, 32) == 0,
           "seed regeneration recovers the shared key");
    printf("ok\n");
}

static void test_decaps_clears_on_null(void) {
    printf("  decapsulate clears shared key on NULL ... ");
    uint8_t ct768[NEVERC_MLKEM768_CT_SIZE];
    uint8_t ct1024[NEVERC_MLKEM1024_CT_SIZE];
    uint8_t shared[32];
    memset(ct768, 0x5A, sizeof(ct768));
    memset(ct1024, 0x5A, sizeof(ct1024));
    memset(shared, 0x5A, sizeof(shared));
    ASSERT(neverc_mlkem768_decapsulate(NULL, ct768, shared) == -1,
           "NULL 768 dk rejected");
    ASSERT(all_zero(shared, sizeof(shared)), "NULL 768 dk clears shared key");
    memset(shared, 0x5A, sizeof(shared));
    ASSERT(neverc_mlkem1024_decapsulate(NULL, ct1024, shared) == -1,
           "NULL 1024 dk rejected");
    ASSERT(all_zero(shared, sizeof(shared)), "NULL 1024 dk clears shared key");
    printf("ok\n");
}

int main(void) {
    printf("crypto/mlkem tests:\n");
    test_768_roundtrip();
    test_768_seed_deterministic();
    test_768_ek_encode_decode();
    test_768_nist_acvp_keygen_vector();
    test_768_wrong_key();
    test_768_implicit_rejection_value();
    test_768_multiple_encaps();
    test_1024_roundtrip();
    test_1024_seed_deterministic();
    test_768_seed_is_decaps_source_of_truth();
    test_decaps_clears_on_null();
    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
