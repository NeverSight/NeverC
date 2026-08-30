/*
 * AES-GCM test suite — NIST SP 800-38D test vectors.
 */
#include "neverc/std/crypto/gcm.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(neverc_aes_ctx_t) == 484,
               "neverc_aes_ctx_t v3389 ABI changed");
_Static_assert(sizeof(neverc_gcm_ctx) == 888,
               "neverc_gcm_ctx v3389 ABI changed");
_Static_assert(_Alignof(neverc_gcm_ctx) == 8,
               "neverc_gcm_ctx v3389 alignment changed");
_Static_assert(offsetof(neverc_gcm_ctx, aes) == 0,
               "neverc_gcm_ctx.aes v3389 offset changed");
_Static_assert(offsetof(neverc_gcm_ctx, h) == 484,
               "neverc_gcm_ctx.h v3389 offset changed");
_Static_assert(offsetof(neverc_gcm_ctx, htab) == 504,
               "neverc_gcm_ctx.htab v3389 offset changed");
_Static_assert(offsetof(neverc_gcm_ctx, rem4) == 760,
               "neverc_gcm_ctx.rem4 v3389 offset changed");

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

    uint8_t dec[60];
    int rc = neverc_gcm_open(&ctx, nonce, ct, 60, aad, 20, tag, dec);
    check_true("open OK", rc == 0);
    check_true("roundtrip", memcmp(dec, pt, 60) == 0);
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

    /* NIST SP 800-38D Test Case 14: 16-byte zero plaintext */
    uint8_t pt[16], ct[16];
    memset(pt, 0, 16);
    neverc_gcm_seal(&ctx, nonce, pt, 16, NULL, 0, ct, tag);
    check_bytes("AES-256-GCM ciphertext", ct,
        "cea7403d4d606b6e074ec5d3baf39d18", 16);
    check_bytes("AES-256-GCM 16B tag", tag,
        "d0d1c8a799996bf0265b98b5d48ab919", 16);
    uint8_t dec[16];
    check_true("AES-256-GCM open",
               neverc_gcm_open(&ctx, nonce, ct, 16, NULL, 0, tag, dec) == 0);
    check_true("AES-256-GCM roundtrip", memcmp(dec, pt, 16) == 0);
}

/* NIST SP 800-38D Test Case 7/8: AES-192-GCM */
static void test_aes192_gcm(void) {
    printf("[AES-192-GCM]\n");
    uint8_t key[24], nonce[12];
    memset(key, 0, 24);
    memset(nonce, 0, 12);

    neverc_gcm_ctx ctx;
    check_true("init-192", neverc_gcm_init(&ctx, key, 24) == 0);

    uint8_t tag[16];
    neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag);
    check_bytes("AES-192-GCM empty tag", tag, "cd33b28ac773f74ba00ed1f312572435", 16);

    uint8_t pt[16], ct[16], dec[16];
    memset(pt, 0, 16);
    neverc_gcm_seal(&ctx, nonce, pt, 16, NULL, 0, ct, tag);
    check_bytes("AES-192-GCM ciphertext", ct,
        "98e7247c07f0fe411c267e4384b0f600", 16);
    check_bytes("AES-192-GCM 16B tag", tag, "2ff58d80033927ab8ef4d4587514f0fb", 16);
    check_true("AES-192-GCM open",
               neverc_gcm_open(&ctx, nonce, ct, 16, NULL, 0, tag, dec) == 0);
    check_true("AES-192-GCM roundtrip", memcmp(dec, pt, 16) == 0);
}

/* NIST SP 800-38D Test Case 16: AES-256-GCM with AAD */
static void test_aes256_gcm_aad(void) {
    printf("[AES-256-GCM AAD]\n");
    uint8_t key[32], nonce[12], pt[60], aad[20];
    hex_to_bytes("feffe9928665731c6d6a8f9467308308"
                 "feffe9928665731c6d6a8f9467308308", key, 32);
    hex_to_bytes("cafebabefacedbaddecaf888", nonce, 12);
    hex_to_bytes(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
        pt, 60);
    hex_to_bytes("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, 20);

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 32);

    uint8_t ct[60], tag[16], dec[60];
    neverc_gcm_seal(&ctx, nonce, pt, 60, aad, 20, ct, tag);
    check_bytes("ciphertext", ct,
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
        "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662",
        60);
    check_bytes("tag", tag, "76fc6ece0f4e1768cddf8853bb2d551b", 16);
    check_true("open OK",
               neverc_gcm_open(&ctx, nonce, ct, 60, aad, 20, tag, dec) == 0);
    check_true("roundtrip", memcmp(dec, pt, 60) == 0);
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

    /* Tamper ciphertext — output buffer must stay untouched on auth failure */
    memset(dec, 0xAA, sizeof(dec));
    ct[0] ^= 0xFF;
    check_true("tamper ct", neverc_gcm_open(&ctx, nonce, ct, len, NULL, 0, tag, dec) == -1);
    {
        uint8_t aa[16];
        memset(aa, 0xAA, sizeof(aa));
        check_true("auth failure leaves plaintext unmodified",
                   memcmp(dec, aa, sizeof(aa)) == 0);
    }
    ct[0] ^= 0xFF;

    /* Tamper tag — in-place open must not decrypt on auth failure */
    tag[15] ^= 1;
    check_true("tamper tag", neverc_gcm_open(&ctx, nonce, ct, len, NULL, 0, tag, dec) == -1);
    {
        uint8_t inplace[16];
        memcpy(inplace, ct, len);
        check_true("in-place tamper tag",
                   neverc_gcm_open(&ctx, nonce, inplace, len, NULL, 0, tag, inplace) == -1);
        check_true("in-place auth failure leaves ciphertext",
                   memcmp(inplace, ct, len) == 0);
    }
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

static void test_nonce_reuse_leaks_xor(void) {
    printf("[nonce reuse]\n");
    uint8_t key[16] = {0x11};
    uint8_t nonce[12] = {0x22};
    uint8_t pt1[16], pt2[16], ct1[16], ct2[16], tag1[16], tag2[16];
    for (int i = 0; i < 16; i++) {
        pt1[i] = (uint8_t)i;
        pt2[i] = (uint8_t)(0xA0 + i);
    }

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);
    neverc_gcm_seal(&ctx, nonce, pt1, 16, NULL, 0, ct1, tag1);
    neverc_gcm_seal(&ctx, nonce, pt2, 16, NULL, 0, ct2, tag2);

    int xor_leaks = 1;
    for (int i = 0; i < 16; i++) {
        if ((uint8_t)(ct1[i] ^ ct2[i]) != (uint8_t)(pt1[i] ^ pt2[i]))
            xor_leaks = 0;
    }
    check_true("reused nonce leaks plaintext XOR", xor_leaks);
    check_true("reused nonce produces distinct tags",
               memcmp(tag1, tag2, 16) != 0);
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

static void test_uninit_and_failed_reinit(void) {
    printf("[uninitialized context and failed re-init]\n");
    uint8_t key[16] = {0x11};
    uint8_t nonce[12] = {0x22};
    uint8_t tag[16];
    neverc_gcm_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    check_true("seal rejects zeroed context",
               neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag) == -1);
    check_true("open rejects zeroed context",
               neverc_gcm_open(&ctx, nonce, NULL, 0, NULL, 0, tag, NULL) == -1);

    check_true("valid init", neverc_gcm_init(&ctx, key, 16) == 0);
    check_true("seal after init",
               neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag) == 0);

    check_true("re-init with null key fails",
               neverc_gcm_init(&ctx, NULL, 16) == -1);
    check_true("seal after failed re-init fails",
               neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag) == -1);
    check_true("open after failed re-init fails",
               neverc_gcm_open(&ctx, nonce, NULL, 0, NULL, 0, tag, NULL) == -1);

    check_true("re-init with bad key length fails",
               neverc_gcm_init(&ctx, key, 15) == -1);
    check_true("seal after bad key length fails",
               neverc_gcm_seal(&ctx, nonce, NULL, 0, NULL, 0, NULL, tag) == -1);
}

/* Empty plaintext, 20-byte AAD — GHASH of AAD || len(A)||len(C) only. */
static void test_aad_only(void) {
    printf("[AAD only, empty PT]\n");
    uint8_t key[16], nonce[12], aad[20], tag[16];
    memset(key, 0, 16);
    memset(nonce, 0, 12);
    for (int i = 0; i < 20; i++)
        aad[i] = (uint8_t)i;

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);
    check_true("aad-only seal",
               neverc_gcm_seal(&ctx, nonce, NULL, 0, aad, 20, NULL, tag) == 0);
    check_bytes("aad-only tag", tag, "6cb71d8230f1c75a6bbc9f23ad201c0b", 16);
    check_true("aad-only open",
               neverc_gcm_open(&ctx, nonce, NULL, 0, aad, 20, tag, NULL) == 0);
    aad[0] ^= 1;
    check_true("aad-only tampered AAD",
               neverc_gcm_open(&ctx, nonce, NULL, 0, aad, 20, tag, NULL) == -1);
}

static void test_aad_overlap_with_output(void) {
    printf("[AAD overlapping ciphertext output]\n");
    uint8_t key[16] = {0x42};
    uint8_t nonce[12] = {0x13};
    uint8_t pt[32];
    uint8_t aad[16];
    for (int i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 3 + 1);
    for (int i = 0; i < 16; i++) aad[i] = (uint8_t)(0xA0 + i);

    neverc_gcm_ctx ctx;
    neverc_gcm_init(&ctx, key, 16);

    uint8_t ct_ref[32], tag_ref[16];
    neverc_gcm_seal(&ctx, nonce, pt, 32, aad, 16, ct_ref, tag_ref);

    /* AAD lives in the output buffer and would be clobbered if GHASH ran
     * after CTR encrypt. */
    uint8_t buf[32], tag[16];
    memcpy(buf, aad, 16);
    neverc_gcm_seal(&ctx, nonce, pt, 32, buf, 16, buf, tag);
    check_true("overlap ciphertext matches", memcmp(buf, ct_ref, 32) == 0);
    check_true("overlap tag matches", memcmp(tag, tag_ref, 16) == 0);

    /* In-place seal with AAD == plaintext. */
    uint8_t inplace[16], tag_ip[16], ct_ip_ref[16], tag_ip_ref[16];
    memcpy(inplace, pt, 16);
    neverc_gcm_seal(&ctx, nonce, pt, 16, pt, 16, ct_ip_ref, tag_ip_ref);
    neverc_gcm_seal(&ctx, nonce, inplace, 16, inplace, 16, inplace, tag_ip);
    check_true("in-place AAD=PT ciphertext matches",
               memcmp(inplace, ct_ip_ref, 16) == 0);
    check_true("in-place AAD=PT tag matches",
               memcmp(tag_ip, tag_ip_ref, 16) == 0);

    uint8_t overlap_pt[48], ct_noaad[32], tag_noaad[16];
    neverc_gcm_seal(&ctx, nonce, pt, 32, NULL, 0, ct_noaad, tag_noaad);
    memcpy(overlap_pt, pt, 32);
    neverc_gcm_seal(&ctx, nonce, overlap_pt, 32, NULL, 0,
                    overlap_pt + 4, tag);
    check_true("dest=src+4 ciphertext matches",
               memcmp(overlap_pt + 4, ct_noaad, 32) == 0);
}

static void test_open_output_may_overlap_nonce(void) {
    printf("[open output overlapping nonce]\n");
    uint8_t key[16] = {0x42};
    uint8_t nonce[12];
    uint8_t pt[32];
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(0x30 + i);
    for (int i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 7 + 3);

    neverc_gcm_ctx ctx;
    check_true("overlapping-nonce init",
               neverc_gcm_init(&ctx, key, sizeof(key)) == 0);

    uint8_t ct[32], tag[16];
    check_true("overlapping-nonce seal fixture",
               neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt),
                               NULL, 0, ct, tag) == 0);

    /* The destination starts inside the ciphertext and extends across the
     * nonce. Open must retain the authenticated nonce before sliding the
     * ciphertext for its supported dest-after-src overlap. */
    uint8_t layout[48] = {0};
    memcpy(layout, ct, sizeof(ct));
    memcpy(layout + sizeof(ct), nonce, sizeof(nonce));
    int rc = neverc_gcm_open(&ctx, layout + sizeof(ct),
                             layout, sizeof(ct), NULL, 0, tag, layout + 4);
    check_true("valid open returns the authenticated plaintext when output overlaps nonce",
               rc == 0 && memcmp(layout + 4, pt, sizeof(pt)) == 0);
}

static void test_seal_detached_output_boundaries(void) {
    printf("[seal detached output boundaries]\n");
    uint8_t key[16] = {0x42};
    uint8_t nonce[12] = {0x13};
    uint8_t pt[32];
    for (int i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 11 + 5);

    neverc_gcm_ctx ctx;
    check_true("tag-overlap init",
               neverc_gcm_init(&ctx, key, sizeof(key)) == 0);

    uint8_t output[48], unchanged[48];
    memset(output, 0xA5, sizeof(output));
    memcpy(unchanged, output, sizeof(output));
    int rc = neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt), NULL, 0,
                             output, output + 24);
    check_true("seal rejects tag overlapping ciphertext", rc == -1);
    check_true("rejected tag overlap leaves output untouched",
               memcmp(output, unchanged, sizeof(output)) == 0);

    memset(output, 0xA5, sizeof(output));
    rc = neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt), NULL, 0,
                         output + 8, output);
    check_true("seal rejects ciphertext overlapping tag", rc == -1);
    check_true("rejected reverse overlap leaves output untouched",
               memcmp(output, unchanged, sizeof(output)) == 0);

    uint8_t ct_ref[32], tag_ref[16];
    check_true("detached boundary reference",
               neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt), NULL, 0,
                               ct_ref, tag_ref) == 0);

    memset(output, 0, sizeof(output));
    rc = neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt), NULL, 0,
                         output, output + 32);
    check_true("tag immediately after ciphertext succeeds",
               rc == 0 && memcmp(output, ct_ref, sizeof(ct_ref)) == 0 &&
               memcmp(output + 32, tag_ref, sizeof(tag_ref)) == 0);

    memset(output, 0, sizeof(output));
    rc = neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt), NULL, 0,
                         output + 16, output);
    check_true("tag immediately before ciphertext succeeds",
               rc == 0 && memcmp(output + 16, ct_ref, sizeof(ct_ref)) == 0 &&
               memcmp(output, tag_ref, sizeof(tag_ref)) == 0);
}

static void test_seal_output_may_overlap_nonce(void) {
    printf("[seal output overlapping nonce]\n");
    uint8_t key[16] = {0x42};
    uint8_t nonce[12];
    uint8_t pt[32];
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(0x50 + i);
    for (int i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 13 + 7);

    neverc_gcm_ctx ctx;
    check_true("seal nonce-overlap init",
               neverc_gcm_init(&ctx, key, sizeof(key)) == 0);

    uint8_t ct_ref[32], tag_ref[16];
    check_true("seal nonce-overlap reference",
               neverc_gcm_seal(&ctx, nonce, pt, sizeof(pt), NULL, 0,
                               ct_ref, tag_ref) == 0);

    uint8_t layout[48] = {0};
    uint8_t tag[16];
    memcpy(layout + 32, nonce, sizeof(nonce));
    int rc = neverc_gcm_seal(&ctx, layout + 32, pt, sizeof(pt), NULL, 0,
                             layout + 4, tag);
    check_true("seal returns one consistent ciphertext/tag pair when output overlaps nonce",
               rc == 0 && memcmp(layout + 4, ct_ref, sizeof(ct_ref)) == 0 &&
               memcmp(tag, tag_ref, sizeof(tag_ref)) == 0);
}

static void test_null_nonce_rejected(void) {
    printf("[null nonce rejected]\n");
    uint8_t key[16] = {0};
    uint8_t tag[16] = {0};
    uint8_t byte = 0;
    neverc_gcm_ctx ctx;
    check_true("init", neverc_gcm_init(&ctx, key, 16) == 0);
    check_true("seal rejects null nonce",
               neverc_gcm_seal(&ctx, NULL, NULL, 0, NULL, 0, NULL, tag) == -1);
    check_true("open rejects null nonce",
               neverc_gcm_open(&ctx, NULL, NULL, 0, NULL, 0, tag, NULL) == -1);
    check_true("open rejects null tag",
               neverc_gcm_open(&ctx, (const uint8_t *)"0123456789ab",
                               NULL, 0, NULL, 0, NULL, NULL) == -1);
    memset(tag, 0xAA, sizeof(tag));
    check_true("null-nonce seal does not write a tag",
               neverc_gcm_seal(&ctx, NULL, &byte, 1, NULL, 0, &byte, tag) == -1);
    check_true("null-nonce seal leaves tag unmodified", tag[0] == 0xAA);
}

int main(void) {
    printf("=== NeverC AES-GCM Tests ===\n\n");
    test_case_1();
    test_case_2();
    test_case_3();
    test_case_4();
    test_aes256_gcm();
    test_aes192_gcm();
    test_aes256_gcm_aad();
    test_tamper();
    test_roundtrip_sizes();
    test_nonce_reuse_leaks_xor();
    test_invalid_inputs_and_limits();
    test_uninit_and_failed_reinit();
    test_aad_only();
    test_aad_overlap_with_output();
    test_open_output_may_overlap_nonce();
    test_seal_detached_output_boundaries();
    test_seal_output_may_overlap_nonce();
    test_null_nonce_rejected();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
