/*
 * AES-GCM test suite — NIST SP 800-38D test vectors.
 */
#include "neverc/std/crypto/gcm.h"
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

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void check_bytes(const char *name, const uint8_t *got,
                        const char *expected_hex, int len) {
    uint8_t expected[256];
    hex_to_bytes(expected_hex, expected, len);
    tests_run++;
    if (memcmp(got, expected, (size_t)len) == 0) { tests_passed++; return; }
    tests_failed++;
    printf("  FAIL: %s\n    got: ", name);
    for (int i = 0; i < len; i++) printf("%02x", got[i]);
    printf("\n    exp: %s\n", expected_hex);
}

/* NIST SP 800-38D Test Case 1: AES-128-GCM, no plaintext, no AAD */
static void test_case_1(void) {
    printf("[Test Case 1: empty PT, empty AAD]\n");
    uint8_t key[16], nonce[12];
    memset(key, 0, 16);
    memset(nonce, 0, 12);

    neverc_gcm_ctx ctx;
    check_true("init", neverc_gcm_init(&ctx, key, 16) == 0);

    uint8_t tag[16];
    int rc = neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag);
    check_true("seal", rc == 0);
    check_bytes("tag", tag, "58e2fccefa7e3061367f1d57a4e7455a", 16);

    /* Open with correct tag */
    rc = neverc_gcm_open(&ctx, nonce, NULL, 0, NULL, 0, tag, NULL);
    check_true("open OK", rc == 0);

    /* Tamper tag */
    tag[0] ^= 1;
    rc = neverc_gcm_open(&ctx, nonce, NULL, 0, NULL, 0, tag, NULL);
    check_true("open tampered", rc == -1);
}

/* NIST SP 800-38D Test Case 2: AES-128-GCM, 16-byte PT, no AAD */
static void test_case_2(void) {
    printf("[Test Case 2: 16B PT, no AAD]\n");
    uint8_t key[16], nonce[12], pt[16];
    memset(key, 0, 16);
    memset(nonce, 0, 12);
    memset(pt, 0, 16);

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);

    uint8_t ct[16], tag[16];
    neverc_gcm_seal(&ctx, nonce, pt, 16, NULL, 0, ct, tag);
    check_bytes("ciphertext", ct, "0388dace60b6a392f328c2b971b2fe78", 16);
    check_bytes("tag", tag, "ab6e47d42cec13bdf53a67b21257bddf", 16);

    /* Round-trip */
    uint8_t dec[16];
    int rc = neverc_gcm_open(&ctx, nonce, ct, 16, NULL, 0, tag, dec);
    check_true("open OK", rc == 0);
    check_true("roundtrip", memcmp(dec, pt, 16) == 0);
}

/* NIST SP 800-38D Test Case 3: AES-128-GCM, 64-byte PT, no AAD */
static void test_case_3(void) {
    printf("[Test Case 3: 64B PT]\n");
    uint8_t key[16], nonce[12];
    hex_to_bytes("feffe9928665731c6d6a8f9467308308", key, 16);
    hex_to_bytes("cafebabefacedbaddecaf888", nonce, 12);

    uint8_t pt[64];
    hex_to_bytes(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
        pt, 64);

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);

    uint8_t ct[64], tag[16];
    neverc_gcm_seal(&ctx, nonce, pt, 64, NULL, 0, ct, tag);
    check_bytes("ciphertext", ct,
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
        "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985", 64);
    check_bytes("tag", tag, "4d5c2af327cd64a62cf35abd2ba6fab4", 16);

    /* Round-trip */
    uint8_t dec[64];
    int rc = neverc_gcm_open(&ctx, nonce, ct, 64, NULL, 0, tag, dec);
    check_true("open OK", rc == 0);
    check_true("roundtrip", memcmp(dec, pt, 64) == 0);
}

/* NIST SP 800-38D Test Case 4: AES-128-GCM, 60-byte PT + 20-byte AAD */
static void test_case_4(void) {
    printf("[Test Case 4: 60B PT + 20B AAD]\n");
    uint8_t key[16], nonce[12];
    hex_to_bytes("feffe9928665731c6d6a8f9467308308", key, 16);
    hex_to_bytes("cafebabefacedbaddecaf888", nonce, 12);

    uint8_t pt[60], aad[20];
    hex_to_bytes(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
        pt, 60);
    hex_to_bytes("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, 20);

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);

    uint8_t ct[60], tag[16];
    neverc_gcm_seal(&ctx, nonce, pt, 60, aad, 20, ct, tag);
    check_bytes("ciphertext", ct,
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
        "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
        60);
    check_bytes("tag", tag, "5bc94fbc3221a5db94fae95ae7121a47", 16);
}

/* AES-256-GCM: NIST Test Case 13 */
static void test_aes256_gcm(void) {
    printf("[AES-256-GCM]\n");
    uint8_t key[32], nonce[12];
    memset(key, 0, 32);
    memset(nonce, 0, 12);

    neverc_gcm_ctx ctx;
    check_true("init-256", neverc_gcm_init(&ctx, key, 32) == 0);

    uint8_t tag[16];
    neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag);
    check_bytes("AES-256-GCM tag", tag, "530f8afbc74536b9a963b4f1c4cb738b", 16);
}

/* Tamper detection */
static void test_tamper(void) {
    printf("[tamper detection]\n");
    uint8_t key[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t nonce[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
    const char *msg = "Hello, GCM!";
    size_t len = 11;

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);

    uint8_t ct[16], tag[16], dec[16];
    neverc_gcm_seal(&ctx, nonce, (const uint8_t *)msg, len, NULL, 0, ct, tag);

    /* Valid open */
    check_true("open valid", neverc_gcm_open(&ctx, nonce, ct, len, NULL, 0, tag, dec) == 0);
    check_true("decrypt match", memcmp(dec, msg, len) == 0);

    /* Tamper ciphertext */
    ct[0] ^= 0xFF;
    check_true("tamper ct", neverc_gcm_open(&ctx, nonce, ct, len, NULL, 0, tag, dec) == -1);
    ct[0] ^= 0xFF;

    /* Tamper tag */
    tag[15] ^= 1;
    check_true("tamper tag", neverc_gcm_open(&ctx, nonce, ct, len, NULL, 0, tag, dec) == -1);
    tag[15] ^= 1;

    /* Tamper AAD */
    uint8_t aad[4] = {1,2,3,4};
    uint8_t ct2[16], tag2[16];
    neverc_gcm_seal(&ctx, nonce, (const uint8_t *)msg, len, aad, 4, ct2, tag2);
    aad[0] ^= 1;
    check_true("tamper AAD", neverc_gcm_open(&ctx, nonce, ct2, len, aad, 4, tag2, dec) == -1);
}

/* Multi-size round-trip */
static void test_roundtrip_sizes(void) {
    printf("[roundtrip sizes]\n");
    uint8_t key[16] = {0x42};
    uint8_t nonce[12] = {0x13};
    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);

    int sizes[] = {0, 1, 15, 16, 17, 31, 32, 48, 64, 100, 256};
    uint8_t pt[256], ct[256], dec[256], tag[16];
    for (int k = 0; k < 11; k++) {
        int sz = sizes[k];
        for (int i = 0; i < sz; i++) pt[i] = (uint8_t)(i * 37 + k);

        neverc_gcm_seal(&ctx, nonce, pt, (size_t)sz, NULL, 0, ct, tag);
        int rc = neverc_gcm_open(&ctx, nonce, ct, (size_t)sz, NULL, 0, tag, dec);
        char name[64];
        snprintf(name, sizeof(name), "roundtrip %d bytes", sz);
        check_true(name, rc == 0 && (sz == 0 || memcmp(dec, pt, (size_t)sz) == 0));
    }
}

static void test_invalid_inputs_and_limits(void) {
    printf("[invalid inputs and limits]\n");
    uint8_t key[16] = {0};
    uint8_t nonce[12] = {0};
    uint8_t byte = 0;
    uint8_t tag[16] = {0};
    neverc_gcm_ctx ctx;

    check_true("init rejects null context",
               neverc_gcm_init(NULL, key, sizeof(key)) == -1);
    check_true("init rejects null key",
               neverc_gcm_init(&ctx, NULL, sizeof(key)) == -1);
    check_true("valid init",
               neverc_gcm_init(&ctx, key, sizeof(key)) == 0);
    check_true("seal rejects invalid plaintext span",
               neverc_gcm_seal(
                   &ctx, nonce, NULL, 1, NULL, 0, &byte, tag) == -1);
    check_true("seal rejects invalid ciphertext span",
               neverc_gcm_seal(
                   &ctx, nonce, &byte, 1, NULL, 0, NULL, tag) == -1);
    check_true("seal rejects invalid AAD span",
               neverc_gcm_seal(
                   &ctx, nonce, NULL, 0, NULL, 1, NULL, tag) == -1);
    check_true("open rejects invalid ciphertext span",
               neverc_gcm_open(
                   &ctx, nonce, NULL, 1, NULL, 0, tag, &byte) == -1);
    check_true("open rejects invalid plaintext span",
               neverc_gcm_open(
                   &ctx, nonce, &byte, 1, NULL, 0, tag, NULL) == -1);
#if SIZE_MAX > UINT32_MAX
    check_true("seal rejects counter-wrap length",
               neverc_gcm_seal(
                   &ctx, nonce, &byte,
                   ((size_t)1 << 36) - 31, NULL, 0, &byte, tag) == -1);
    check_true("seal rejects overflowing AAD bit length",
               neverc_gcm_seal(
                   &ctx, nonce, NULL, 0, &byte,
                   (size_t)1 << 61, NULL, tag) == -1);
#endif
}

int main(void) {
    printf("=== NeverC AES-GCM Tests ===\n\n");
    test_case_1();
    test_case_2();
    test_case_3();
    test_case_4();
    test_aes256_gcm();
    test_tamper();
    test_roundtrip_sizes();
    test_invalid_inputs_and_limits();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
