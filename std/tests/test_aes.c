#include "neverc/crypto/aes.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void hex_to_bytes(const char *hex, uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int v;
        sscanf(hex + 2 * i, "%02x", &v);
        out[i] = (uint8_t)v;
    }
}

static void test_aes128(void) {
    printf("[AES-128 FIPS 197]\n");

    /* FIPS 197 Appendix B */
    uint8_t key[16], plain[16], cipher[16], result[16];
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex_to_bytes("3243f6a8885a308d313198a2e0370734", plain, 16);
    hex_to_bytes("3925841d02dc09fbdc118597196a0b32", cipher, 16);

    neverc_aes_ctx_t ctx;
    check_true("init AES-128", neverc_aes_init(&ctx, key, 16) == 0);

    neverc_aes_encrypt_block(&ctx, result, plain);
    check_true("AES-128 encrypt", memcmp(result, cipher, 16) == 0);

    neverc_aes_decrypt_block(&ctx, result, cipher);
    check_true("AES-128 decrypt", memcmp(result, plain, 16) == 0);

    /* NIST SP 800-38A F.1.1 — ECB-AES128.Encrypt */
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    neverc_aes_init(&ctx, key, 16);

    struct { const char *pt; const char *ct; } nist_128[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "3ad77bb40d7a3660a89ecaf32466ef97"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "f5d3d58503b9699de785895a96fdbaaf"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "43b1cd7f598ece23881b00e3ed030688"},
        {"f69f2445df4f9b17ad2b417be66c3710", "7b0c785e27e8ad3f8223207104725dd4"},
    };

    for (int i = 0; i < 4; i++) {
        hex_to_bytes(nist_128[i].pt, plain, 16);
        hex_to_bytes(nist_128[i].ct, cipher, 16);

        neverc_aes_encrypt_block(&ctx, result, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "NIST-128 encrypt block %d", i);
        check_true(buf, memcmp(result, cipher, 16) == 0);

        neverc_aes_decrypt_block(&ctx, result, cipher);
        snprintf(buf, sizeof(buf), "NIST-128 decrypt block %d", i);
        check_true(buf, memcmp(result, plain, 16) == 0);
    }
}

static void test_aes192(void) {
    printf("[AES-192 NIST SP 800-38A]\n");

    uint8_t key[24], plain[16], cipher[16], result[16];
    hex_to_bytes("8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b", key, 24);

    neverc_aes_ctx_t ctx;
    check_true("init AES-192", neverc_aes_init(&ctx, key, 24) == 0);

    struct { const char *pt; const char *ct; } nist_192[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "bd334f1d6e45f25ff712a214571fa5cc"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "974104846d0ad3ad7734ecb3ecee4eef"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "ef7afd2270e2e60adce0ba2face6444e"},
        {"f69f2445df4f9b17ad2b417be66c3710", "9a4b41ba738d6c72fb16691603c18e0e"},
    };

    for (int i = 0; i < 4; i++) {
        hex_to_bytes(nist_192[i].pt, plain, 16);
        hex_to_bytes(nist_192[i].ct, cipher, 16);

        neverc_aes_encrypt_block(&ctx, result, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "NIST-192 encrypt block %d", i);
        check_true(buf, memcmp(result, cipher, 16) == 0);

        neverc_aes_decrypt_block(&ctx, result, cipher);
        snprintf(buf, sizeof(buf), "NIST-192 decrypt block %d", i);
        check_true(buf, memcmp(result, plain, 16) == 0);
    }
}

static void test_aes256(void) {
    printf("[AES-256 NIST SP 800-38A]\n");

    uint8_t key[32], plain[16], cipher[16], result[16];
    hex_to_bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key, 32);

    neverc_aes_ctx_t ctx;
    check_true("init AES-256", neverc_aes_init(&ctx, key, 32) == 0);

    struct { const char *pt; const char *ct; } nist_256[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "f3eed1bdb5d2a03c064b5a7e3db181f8"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "591ccb10d410ed26dc5ba74a31362870"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "b6ed21b99ca6f4f9f153e7b1beafed1d"},
        {"f69f2445df4f9b17ad2b417be66c3710", "23304b7a39f9f3ff067d8d8f9e24ecc7"},
    };

    for (int i = 0; i < 4; i++) {
        hex_to_bytes(nist_256[i].pt, plain, 16);
        hex_to_bytes(nist_256[i].ct, cipher, 16);

        neverc_aes_encrypt_block(&ctx, result, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "NIST-256 encrypt block %d", i);
        check_true(buf, memcmp(result, cipher, 16) == 0);

        neverc_aes_decrypt_block(&ctx, result, cipher);
        snprintf(buf, sizeof(buf), "NIST-256 decrypt block %d", i);
        check_true(buf, memcmp(result, plain, 16) == 0);
    }
}

static void test_invalid_key(void) {
    printf("[AES invalid key]\n");
    neverc_aes_ctx_t ctx;
    uint8_t key[15] = {0};
    check_true("reject 15-byte key", neverc_aes_init(&ctx, key, 15) == -1);
    check_true("reject 0-byte key",  neverc_aes_init(&ctx, key, 0) == -1);
}

static void test_round_trip(void) {
    printf("[AES round-trip]\n");

    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i * 7 + 13);

    int key_sizes[] = {16, 24, 32};
    for (int k = 0; k < 3; k++) {
        neverc_aes_ctx_t ctx;
        neverc_aes_init(&ctx, key, key_sizes[k]);

        for (int t = 0; t < 16; t++) {
            uint8_t plain[16], enc[16], dec[16];
            for (int i = 0; i < 16; i++) plain[i] = (uint8_t)(t * 17 + i * 3);

            neverc_aes_encrypt_block(&ctx, enc, plain);
            neverc_aes_decrypt_block(&ctx, dec, enc);

            char buf[64];
            snprintf(buf, sizeof(buf), "AES-%d round-trip %d", key_sizes[k] * 8, t);
            check_true(buf, memcmp(dec, plain, 16) == 0);
        }
    }
}

int main(void) {
    printf("=== NeverC AES Tests ===\n\n");

    test_aes128();
    test_aes192();
    test_aes256();
    test_invalid_key();
    test_round_trip();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
