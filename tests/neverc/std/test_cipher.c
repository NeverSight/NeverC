/*
 * AES-CBC and AES-CTR cipher mode tests.
 * Vectors from NIST SP 800-38A.
 */
#include "neverc/std/crypto/cipher.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void hex_to_bytes(const char *hex, uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int b;
        sscanf(hex + 2*i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

static void check_bytes(const char *name, const uint8_t *got,
                        const uint8_t *expected, int len) {
    tests_run++;
    if (memcmp(got, expected, (size_t)len) == 0) { tests_passed++; return; }
    tests_failed++;
    printf("  FAIL: %s\n    got: ", name);
    for (int i = 0; i < len; i++) printf("%02x", got[i]);
    printf("\n    exp: ");
    for (int i = 0; i < len; i++) printf("%02x", expected[i]);
    printf("\n");
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

/* NIST SP 800-38A F.2.1: AES-128 CBC Encrypt */
static void test_cbc_128(void) {
    printf("[AES-128 CBC — NIST SP 800-38A F.2.1]\n");
    uint8_t key[16], iv[16], pt[64], ct[64], expected_ct[64];

    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    hex_to_bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710", pt, 64);
    hex_to_bytes(
        "7649abac8119b246cee98e9b12e9197d"
        "5086cb9b507219ee95db113a917678b2"
        "73bed6b8e3c1743b7116e69e22229516"
        "3ff1caa1681fac09120eca307586e1a7", expected_ct, 64);

    int ret = neverc_cipher_cbc_encrypt(key, 16, iv, ct, pt, 64);
    check_int("CBC-128 encrypt return", ret, 0);
    check_bytes("CBC-128 ciphertext", ct, expected_ct, 64);

    /* Decrypt */
    uint8_t dec[64];
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    ret = neverc_cipher_cbc_decrypt(key, 16, iv, dec, expected_ct, 64);
    check_int("CBC-128 decrypt return", ret, 0);
    check_bytes("CBC-128 plaintext", dec, pt, 64);

    uint8_t in_place[64];
    memcpy(in_place, expected_ct, sizeof(in_place));
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    ret = neverc_cipher_cbc_decrypt(
        key, 16, iv, in_place, in_place, sizeof(in_place));
    check_int("CBC-128 in-place decrypt return", ret, 0);
    check_bytes("CBC-128 in-place plaintext", in_place, pt, 64);
}

/* NIST SP 800-38A F.2.3: AES-192 CBC */
static void test_cbc_192(void) {
    printf("[AES-192 CBC — NIST SP 800-38A F.2.3]\n");
    uint8_t key[24], iv[16], pt[64], ct[64], expected_ct[64], dec[64];

    hex_to_bytes("8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b", key, 24);
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    hex_to_bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710", pt, 64);
    hex_to_bytes(
        "4f021db243bc633d7178183a9fa071e8"
        "b4d9ada9ad7dedf4e5e738763f69145a"
        "571b242012fb7ae07fa9baac3df102e0"
        "08b0e27988598881d920a9e64f5615cd", expected_ct, 64);

    neverc_cipher_cbc_encrypt(key, 24, iv, ct, pt, 64);
    check_bytes("CBC-192 ciphertext", ct, expected_ct, 64);

    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    neverc_cipher_cbc_decrypt(key, 24, iv, dec, expected_ct, 64);
    check_bytes("CBC-192 roundtrip", dec, pt, 64);
}

/* NIST SP 800-38A F.2.5: AES-256 CBC */
static void test_cbc_256(void) {
    printf("[AES-256 CBC — NIST SP 800-38A F.2.5]\n");
    uint8_t key[32], iv[16], pt[64], ct[64], expected_ct[64], dec[64];

    hex_to_bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
                 key, 32);
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    hex_to_bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710", pt, 64);
    hex_to_bytes(
        "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
        "9cfc4e967edb808d679f777bc6702c7d"
        "39f23369a9d9bacfa530e26304231461"
        "b2eb05e2c39be9fcda6c19078c6a9d1b", expected_ct, 64);

    neverc_cipher_cbc_encrypt(key, 32, iv, ct, pt, 64);
    check_bytes("CBC-256 ciphertext", ct, expected_ct, 64);

    hex_to_bytes("000102030405060708090a0b0c0d0e0f", iv, 16);
    neverc_cipher_cbc_decrypt(key, 32, iv, dec, expected_ct, 64);
    check_bytes("CBC-256 roundtrip", dec, pt, 64);
}

/* NIST SP 800-38A F.5.1: AES-128 CTR */
static void test_ctr_128(void) {
    printf("[AES-128 CTR — NIST SP 800-38A F.5.1]\n");
    uint8_t key[16], iv[16], pt[64], ct[64], expected_ct[64];

    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex_to_bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv, 16);
    hex_to_bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710", pt, 64);
    hex_to_bytes(
        "874d6191b620e3261bef6864990db6ce"
        "9806f66b7970fdff8617187bb9fffdff"
        "5ae4df3edbd5d35e5b4f09020db03eab"
        "1e031dda2fbe03d1792170a0f3009cee", expected_ct, 64);

    neverc_cipher_ctr(key, 16, iv, ct, pt, 64);
    check_bytes("CTR-128 ciphertext", ct, expected_ct, 64);

    /* CTR is self-inverse: encrypt ciphertext = plaintext */
    uint8_t dec[64];
    hex_to_bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv, 16);
    neverc_cipher_ctr(key, 16, iv, dec, expected_ct, 64);
    check_bytes("CTR-128 decrypt", dec, pt, 64);
}

/* Partial block CTR: arbitrary length */
static void test_ctr_partial(void) {
    printf("[CTR partial block]\n");
    uint8_t key[16] = {0};
    uint8_t iv[16] = {0}, iv2[16] = {0};

    int sizes[] = {1, 7, 15, 17, 31, 33, 63};
    for (int si = 0; si < 7; si++) {
        int sz = sizes[si];
        uint8_t pt[64], ct[64], dec[64];
        for (int i = 0; i < sz; i++) pt[i] = (uint8_t)(i * 13 + 7);

        memset(iv, 0, 16);
        neverc_cipher_ctr(key, 16, iv, ct, pt, (size_t)sz);

        memset(iv2, 0, 16);
        neverc_cipher_ctr(key, 16, iv2, dec, ct, (size_t)sz);

        char buf[64];
        snprintf(buf, sizeof(buf), "CTR partial sz=%d", sz);
        check_bytes(buf, dec, pt, sz);
    }
}

/* CBC with invalid length */
static void test_cbc_invalid(void) {
    printf("[CBC invalid length]\n");
    uint8_t key[16] = {0}, iv[16] = {0}, buf[32] = {0};
    check_int("CBC encrypt len=7 fails",
              neverc_cipher_cbc_encrypt(key, 16, iv, buf, buf, 7), -1);
    check_int("CBC decrypt len=3 fails",
              neverc_cipher_cbc_decrypt(key, 16, iv, buf, buf, 3), -1);
    check_int("CBC encrypt len=0 ok",
              neverc_cipher_cbc_encrypt(key, 16, iv, buf, buf, 0), 0);
}

static void test_invalid_key_and_spans(void) {
    printf("[invalid key and spans]\n");
    uint8_t key[16] = {0}, iv[16] = {0}, src[16] = {0}, dst[16];
    memset(dst, 0xa5, sizeof(dst));

    check_int("CTR invalid key rejected",
              neverc_cipher_ctr_checked(
                  key, 15, iv, dst, src, sizeof(src)), -1);
    uint8_t unchanged[16];
    memset(unchanged, 0xa5, sizeof(unchanged));
    check_bytes("CTR invalid key leaves output", dst, unchanged, sizeof(dst));
    check_int("CTR invalid source span rejected",
              neverc_cipher_ctr_checked(key, 16, iv, dst, NULL, 1), -1);
    check_int("CBC invalid destination span rejected",
              neverc_cipher_cbc_encrypt(
                  key, 16, iv, NULL, src, sizeof(src)),
              -1);
    check_int("CTR empty null spans accepted",
              neverc_cipher_ctr_checked(
                  key, 16, iv, NULL, NULL, 0), 0);

    neverc_cipher_ctr(key, 15, iv, dst, src, sizeof(src));
    check_bytes("legacy CTR invalid key is a no-op",
                dst, unchanged, sizeof(dst));
}

static void test_ctr_low32_wrap_no_reuse(void) {
    printf("[CTR 32-bit counter wrap]\n");
    uint8_t key[16] = {0};
    uint8_t iv[16];
    memset(iv, 0, 16);
    iv[12] = iv[13] = iv[14] = iv[15] = 0xFF;

    uint8_t pt[32], ct[32];
    memset(pt, 0x5A, sizeof(pt));
    check_int("CTR wrap-around encrypt",
              neverc_cipher_ctr_checked(key, 16, iv, ct, pt, 32), 0);
    /* 128-bit increment carries into the nonce; the two blocks must differ. */
    check_int("wrapped counter does not reuse keystream",
              memcmp(ct, ct + 16, 16) != 0, 1);
}

int main(void) {
    printf("=== NeverC Cipher Mode Tests ===\n");
    test_cbc_128();
    test_cbc_192();
    test_cbc_256();
    test_ctr_128();
    test_ctr_partial();
    test_cbc_invalid();
    test_invalid_key_and_spans();
    test_ctr_low32_wrap_no_reuse();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
