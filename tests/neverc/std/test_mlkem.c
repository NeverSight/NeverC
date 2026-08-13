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
    printf("ok\n");
}

int main(void) {
    printf("crypto/mlkem tests:\n");
    test_768_roundtrip();
    test_768_seed_deterministic();
    test_768_ek_encode_decode();
    test_768_wrong_key();
    test_768_implicit_rejection_value();
    test_768_multiple_encaps();
    test_1024_roundtrip();
    test_1024_seed_deterministic();
    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
