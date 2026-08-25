#include "neverc/std/crypto/des.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_bytes(const char *name, const uint8_t *got, const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) { tests_passed++; }
    else {
        tests_failed++;
        printf("  FAIL: %s: got [", name);
        for (size_t i = 0; i < len; i++) printf("%02x", got[i]);
        printf("], expected [");
        for (size_t i = 0; i < len; i++) printf("%02x", expected[i]);
        printf("]\n");
    }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/*
 * NIST SP 800-67 Appendix B — DES test vectors.
 *
 * Key1 = 0123456789ABCDEF
 * Key2 = 23456789ABCDEF01
 * Key3 = 456789ABCDEF0123
 * Plaintext = "The quic" (5468652071756963)
 */
static void test_des_nist(void) {
    printf("[DES NIST vectors]\n");

    const uint8_t key[] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    const uint8_t pt[]  = {0x54,0x68,0x65,0x20,0x71,0x75,0x69,0x63};

    neverc_des_cipher_t c;
    check_int("des init", neverc_des_init(&c, key), 0);

    uint8_t ct[8], decrypted[8];
    neverc_des_encrypt_block(&c, ct, pt);

    check_true("ciphertext differs from plaintext",
               memcmp(ct, pt, 8) != 0);

    neverc_des_decrypt_block(&c, decrypted, ct);
    check_bytes("decrypt recovers plaintext", decrypted, pt, 8);
}

/*
 * FIPS PUB 81 — DES known answer test.
 * Key: 0123456789ABCDEF
 * Plaintext: 4E6F772069732074 ("Now is t")
 * Ciphertext: 3FA40E8A984D4815
 */
static void test_des_fips81(void) {
    printf("[DES FIPS 81 vector]\n");

    const uint8_t key[] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    const uint8_t pt[]  = {0x4E,0x6F,0x77,0x20,0x69,0x73,0x20,0x74};
    const uint8_t expected_ct[] = {0x3F,0xA4,0x0E,0x8A,0x98,0x4D,0x48,0x15};

    neverc_des_cipher_t c;
    neverc_des_init(&c, key);

    uint8_t ct[8];
    neverc_des_encrypt_block(&c, ct, pt);
    check_bytes("FIPS 81 encrypt", ct, expected_ct, 8);

    uint8_t decrypted[8];
    neverc_des_decrypt_block(&c, decrypted, ct);
    check_bytes("FIPS 81 decrypt", decrypted, pt, 8);
}

/*
 * 3DES consistency: when K1=K2=K3, 3DES reduces to single DES.
 * This is because E_K(D_K(E_K(x))) = E_K(x).
 * This validates the 3DES Feistel round ordering is correct.
 */
static void test_3des_consistency_with_des(void) {
    printf("[3DES consistency with DES]\n");

    const uint8_t single_key[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    const uint8_t triple_key[24] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    };
    const uint8_t pt[] = {0x54,0x68,0x65,0x20,0x71,0x75,0x69,0x63};

    neverc_des_cipher_t des;
    neverc_3des_cipher_t tdes;
    neverc_des_init(&des, single_key);
    neverc_3des_init(&tdes, triple_key);

    uint8_t ct_des[8], ct_3des[8];
    neverc_des_encrypt_block(&des, ct_des, pt);
    neverc_3des_encrypt_block(&tdes, ct_3des, pt);

    check_bytes("3DES(K,K,K) == DES(K)", ct_3des, ct_des, 8);

    uint8_t dec_des[8], dec_3des[8];
    neverc_des_decrypt_block(&des, dec_des, ct_des);
    neverc_3des_decrypt_block(&tdes, dec_3des, ct_3des);
    check_bytes("3DES decrypt == DES decrypt", dec_3des, dec_des, 8);
    check_bytes("3DES decrypt recovers pt", dec_3des, pt, 8);
}

static void test_3des_roundtrip_different_keys(void) {
    printf("[3DES roundtrip with 3 different keys]\n");

    const uint8_t key[24] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01,
        0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01,0x23,
    };
    const uint8_t pt[] = {0x54,0x68,0x65,0x20,0x71,0x75,0x69,0x63};

    neverc_3des_cipher_t c;
    check_int("3des init", neverc_3des_init(&c, key), 0);

    uint8_t ct[8], decrypted[8];
    neverc_3des_encrypt_block(&c, ct, pt);
    check_true("3DES ct differs from pt", memcmp(ct, pt, 8) != 0);

    neverc_3des_decrypt_block(&c, decrypted, ct);
    check_bytes("3DES decrypt recovers pt", decrypted, pt, 8);

    /* OpenSSL / NIST SP 800-67 Appendix B style three-key vector.
     * Round-trip alone cannot catch a matching E/D swap. */
    const uint8_t expected_ct[] = {0x1C,0xCF,0x23,0x86,0x9D,0x09,0x33,0x3E};
    check_bytes("3DES three-key known answer", ct, expected_ct, 8);
}

static void test_3des_roundtrip(void) {
    printf("[3DES roundtrip]\n");

    const uint8_t key[24] = {
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11,
        0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,
    };

    neverc_3des_cipher_t c;
    neverc_3des_init(&c, key);

    uint8_t blocks[8][8] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
        {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08},
        {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE},
        {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01},
        {0x48,0x65,0x6C,0x6C,0x6F,0x21,0x21,0x21},
        {0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA},
    };

    char name[64];
    for (int i = 0; i < 8; i++) {
        uint8_t ct[8], dec[8];
        neverc_3des_encrypt_block(&c, ct, blocks[i]);
        neverc_3des_decrypt_block(&c, dec, ct);
        snprintf(name, sizeof(name), "3DES roundtrip block %d", i);
        check_bytes(name, dec, blocks[i], 8);
    }
}

static void test_des_all_zeros(void) {
    printf("[DES all-zeros key]\n");

    const uint8_t key[8] = {0,0,0,0,0,0,0,0};
    const uint8_t pt[8]  = {0,0,0,0,0,0,0,0};
    const uint8_t expected_ct[] = {0x8C,0xA6,0x4D,0xE9,0xC1,0xB1,0x23,0xA7};

    neverc_des_cipher_t c;
    neverc_des_init(&c, key);
    uint8_t ct[8];
    neverc_des_encrypt_block(&c, ct, pt);
    check_bytes("all zeros encrypt", ct, expected_ct, 8);

    uint8_t dec[8];
    neverc_des_decrypt_block(&c, dec, ct);
    check_bytes("all zeros decrypt", dec, pt, 8);
}

static void test_des_weak_keys(void) {
    printf("[DES weak key detection]\n");

    /* DES weak keys encrypt == decrypt (each subkey is the same) */
    const uint8_t weak_key[] = {0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01};
    const uint8_t pt[] = {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};

    neverc_des_cipher_t c;
    neverc_des_init(&c, weak_key);

    uint8_t ct[8], double_enc[8];
    neverc_des_encrypt_block(&c, ct, pt);
    neverc_des_encrypt_block(&c, double_enc, ct);

    check_bytes("weak key: E(E(pt)) == pt (involution)", double_enc, pt, 8);

    check_int("null key is not weak", neverc_des_is_weak_key(NULL), -1);
    check_int("01010101.. is weak", neverc_des_is_weak_key(weak_key), 1);

    const uint8_t zeros[8] = {0};
    check_int("all-zero is weak (parity-stripped 01..01)",
              neverc_des_is_weak_key(zeros), 1);

    const uint8_t nist[] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    check_int("NIST test key is not weak", neverc_des_is_weak_key(nist), 0);

    const uint8_t semi[] = {0x01,0xFE,0x01,0xFE,0x01,0xFE,0x01,0xFE};
    check_int("semi-weak 01FE.. is weak", neverc_des_is_weak_key(semi), 1);
    const uint8_t semi_parity[] = {0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF};
    check_int("semi-weak with flipped parity bits",
              neverc_des_is_weak_key(semi_parity), 1);

    check_int("null 3DES key", neverc_3des_is_weak_key(NULL), -1);
    const uint8_t tdes_ok[24] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
        0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01,0x23,
    };
    check_int("independent 3DES keys are not weak",
              neverc_3des_is_weak_key(tdes_ok), 0);

    const uint8_t tdes_k1k2[24] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01,0x23,
    };
    check_int("3DES K1==K2 is degenerate", neverc_3des_is_weak_key(tdes_k1k2), 1);

    const uint8_t tdes_two_key[24] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    };
    check_int("two-key 3DES (K1==K3) is not flagged weak",
              neverc_3des_is_weak_key(tdes_two_key), 0);

    uint8_t tdes_weak_k2[24];
    memcpy(tdes_weak_k2, tdes_ok, 24);
    memcpy(tdes_weak_k2 + 8, weak_key, 8);
    check_int("3DES with weak K2", neverc_3des_is_weak_key(tdes_weak_k2), 1);
}

static void test_des_different_keys(void) {
    printf("[DES different keys]\n");

    const uint8_t key1[] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    const uint8_t key2[] = {0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10};
    const uint8_t pt[]   = {0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48};

    neverc_des_cipher_t c1, c2;
    neverc_des_init(&c1, key1);
    neverc_des_init(&c2, key2);

    uint8_t ct1[8], ct2[8];
    neverc_des_encrypt_block(&c1, ct1, pt);
    neverc_des_encrypt_block(&c2, ct2, pt);

    check_true("different keys produce different ciphertext",
               memcmp(ct1, ct2, 8) != 0);
}

static void test_null_inputs(void) {
    printf("[DES null inputs]\n");
    neverc_des_cipher_t des;
    neverc_3des_cipher_t tdes;
    const uint8_t key8[8] = {0};
    const uint8_t key24[24] = {0};
    check_int("des null cipher", neverc_des_init(NULL, key8), -1);
    check_int("des null key", neverc_des_init(&des, NULL), -1);
    check_int("3des null cipher", neverc_3des_init(NULL, key24), -1);
    check_int("3des null key", neverc_3des_init(&tdes, NULL), -1);

    neverc_des_init(&des, key8);
    check_int("des null key after init fails", neverc_des_init(&des, NULL), -1);
    {
        int wiped = 1;
        for (int i = 0; i < 16; i++)
            if (des.subkeys[i] != 0) wiped = 0;
        check_true("des null key wipes subkeys", wiped);
    }

    neverc_3des_init(&tdes, key24);
    check_int("3des null key after init fails", neverc_3des_init(&tdes, NULL), -1);
    {
        int wiped = 1;
        for (int i = 0; i < 16; i++)
            if (tdes.c1.subkeys[i] || tdes.c2.subkeys[i] || tdes.c3.subkeys[i])
                wiped = 0;
        check_true("3des null key wipes subkeys", wiped);
    }

    {
        uint8_t out[8], pt[8], aa[8];
        memset(out, 0xAA, sizeof(out));
        memset(aa, 0xAA, sizeof(aa));
        memset(pt, 0x5A, sizeof(pt));
        neverc_des_encrypt_block(&des, out, NULL);
        check_true("des encrypt null src is a no-op", out[0] == 0xAA);
        neverc_3des_encrypt_block(&tdes, out, NULL);
        check_true("3des encrypt null src is a no-op", out[0] == 0xAA);

        /* All-zero subkeys are the valid schedule for the all-zero key, so a
         * wipe cannot be distinguished from that key. Encrypt after failed
         * init must still be a no-op rather than DES(0). */
        neverc_des_encrypt_block(&des, out, pt);
        check_bytes("des encrypt after failed re-init is a no-op", out, aa, 8);
        neverc_des_decrypt_block(&des, out, pt);
        check_bytes("des decrypt after failed re-init is a no-op", out, aa, 8);
        neverc_3des_encrypt_block(&tdes, out, pt);
        check_bytes("3des encrypt after failed re-init is a no-op", out, aa, 8);
        neverc_3des_decrypt_block(&tdes, out, pt);
        check_bytes("3des decrypt after failed re-init is a no-op", out, aa, 8);
    }

    {
        neverc_des_cipher_t zdes;
        neverc_3des_cipher_t ztdes;
        uint8_t out[8], pt[8], aa[8];
        memset(&zdes, 0, sizeof(zdes));
        memset(&ztdes, 0, sizeof(ztdes));
        memset(out, 0xAA, sizeof(out));
        memset(aa, 0xAA, sizeof(aa));
        memset(pt, 0x5A, sizeof(pt));
        neverc_des_encrypt_block(&zdes, out, pt);
        check_bytes("des encrypt before init is a no-op", out, aa, 8);
        neverc_3des_encrypt_block(&ztdes, out, pt);
        check_bytes("3des encrypt before init is a no-op", out, aa, 8);
    }
}

static void test_invalid_ready_marker(void) {
    printf("[DES invalid ready marker]\n");

    const uint8_t key8[8] = {0};
    const uint8_t key24[24] = {0};
    const uint8_t input[8] = {0};
    uint8_t out[8], expected[8];
    neverc_des_cipher_t des;
    neverc_3des_cipher_t tdes;

    memset(expected, 0xAA, sizeof(expected));

    neverc_des_init(&des, key8);
    des.subkeys[0] ^= UINT64_C(0x4000000000000000);
    memset(out, 0xAA, sizeof(out));
    neverc_des_encrypt_block(&des, out, input);
    check_bytes("des rejects corrupted ready marker", out, expected,
                sizeof(out));
    memset(out, 0xAA, sizeof(out));
    neverc_des_decrypt_block(&des, out, input);
    check_bytes("des decrypt rejects corrupted ready marker", out, expected,
                sizeof(out));

    neverc_3des_init(&tdes, key24);
    tdes.c2.subkeys[0] ^= UINT64_C(0x4000000000000000);
    memset(out, 0xAA, sizeof(out));
    neverc_3des_encrypt_block(&tdes, out, input);
    check_bytes("3des rejects corrupted ready marker", out, expected,
                sizeof(out));
    memset(out, 0xAA, sizeof(out));
    neverc_3des_decrypt_block(&tdes, out, input);
    check_bytes("3des decrypt rejects corrupted ready marker", out, expected,
                sizeof(out));
}

static void test_des_parity_ignored(void) {
    printf("[DES parity bits ignored]\n");

    /* FIPS 81 key has odd parity in each LSB. Flipping those bits must not
     * change the schedule: PC-1 drops them, matching Go crypto/des. */
    const uint8_t key[] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    uint8_t key_flip[8];
    for (int i = 0; i < 8; i++)
        key_flip[i] = (uint8_t)(key[i] ^ 0x01);
    const uint8_t pt[] = {0x4E,0x6F,0x77,0x20,0x69,0x73,0x20,0x74};
    const uint8_t expected_ct[] = {0x3F,0xA4,0x0E,0x8A,0x98,0x4D,0x48,0x15};

    neverc_des_cipher_t a, b;
    neverc_des_init(&a, key);
    neverc_des_init(&b, key_flip);

    uint8_t ct1[8], ct2[8];
    neverc_des_encrypt_block(&a, ct1, pt);
    neverc_des_encrypt_block(&b, ct2, pt);
    check_bytes("parity-flipped key same ciphertext", ct1, ct2, 8);
    check_bytes("parity-flipped still FIPS 81", ct2, expected_ct, 8);
}

static void test_3des_two_key(void) {
    printf("[3DES two-key (K1=K3)]\n");

    const uint8_t key[24] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
    };
    const uint8_t pt[] = {0x48,0x65,0x6C,0x6C,0x6F,0x2C,0x20,0x57};

    neverc_3des_cipher_t c;
    neverc_3des_init(&c, key);

    uint8_t ct[8], dec[8];
    neverc_3des_encrypt_block(&c, ct, pt);
    neverc_3des_decrypt_block(&c, dec, ct);
    check_bytes("two-key 3DES roundtrip", dec, pt, 8);
}

int main(void) {
    printf("=== NeverC DES Tests ===\n\n");
    test_des_nist();
    test_des_fips81();
    test_3des_consistency_with_des();
    test_3des_roundtrip_different_keys();
    test_3des_roundtrip();
    test_des_all_zeros();
    test_des_weak_keys();
    test_des_different_keys();
    test_null_inputs();
    test_invalid_ready_marker();
    test_des_parity_ignored();
    test_3des_two_key();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
