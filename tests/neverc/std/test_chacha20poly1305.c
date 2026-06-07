/*
 * ChaCha20-Poly1305 AEAD tests — RFC 8439 Section 2.8.2 official vectors.
 */
#include "neverc/std/crypto/chacha20poly1305.h"
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
    for (int i = 0; i < len && i < 32; i++) printf("%02x", got[i]);
    if (len > 32) printf("...");
    printf("\n    exp: ");
    for (int i = 0; i < len && i < 32; i++) printf("%02x", expected[i]);
    if (len > 32) printf("...");
    printf("\n");
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

/* RFC 8439 Section 2.8.2 AEAD test vector */
static void test_rfc8439(void) {
    printf("[RFC 8439 Section 2.8.2]\n");

    uint8_t key[32], nonce[12], aad[12], plaintext[114], expected_ct[114+16];

    hex_to_bytes(
        "808182838485868788898a8b8c8d8e8f"
        "909192939495969798999a9b9c9d9e9f",
        key, 32);

    hex_to_bytes("070000004041424344454647", nonce, 12);

    hex_to_bytes("50515253c0c1c2c3c4c5c6c7", aad, 12);

    const char *pt_str =
        "4c616469657320616e642047656e746c"
        "656d656e206f662074686520636c6173"
        "73206f66202739393a20496620492063"
        "6f756c64206f6666657220796f75206f"
        "6e6c79206f6e652074697020666f7220"
        "746865206675747572652c2073756e73"
        "637265656e20776f756c642062652069"
        "742e";
    hex_to_bytes(pt_str, plaintext, 114);

    const char *ct_str =
        "d31a8d34648e60db7b86afbc53ef7ec2"
        "a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b"
        "1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58"
        "fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b"
        "6116";
    hex_to_bytes(ct_str, expected_ct, 114);

    /* Tag from RFC 8439 */
    const char *tag_str = "1ae10b594f09e26a7e902ecbd0600691";
    hex_to_bytes(tag_str, expected_ct + 114, 16);

    /* Seal */
    uint8_t output[114 + 16];
    size_t out_len = neverc_chacha20poly1305_seal(
        output, key, nonce, plaintext, 114, aad, 12);
    check_int("seal output length", (int)out_len, 114 + 16);
    check_bytes("seal ciphertext", output, expected_ct, 114);
    check_bytes("seal tag", output + 114, expected_ct + 114, 16);

    /* Open */
    uint8_t decrypted[114];
    int dec_len = neverc_chacha20poly1305_open(
        decrypted, key, nonce, output, 114 + 16, aad, 12);
    check_int("open return value", dec_len, 114);
    check_bytes("open plaintext", decrypted, plaintext, 114);
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");

    uint8_t key[32] = {0};
    uint8_t nonce[12] = {0};
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(i + 100);

    /* Various sizes */
    int sizes[] = {0, 1, 15, 16, 31, 32, 63, 64, 100};
    for (int si = 0; si < 9; si++) {
        int sz = sizes[si];
        uint8_t pt[128], ct[128 + 16], dec[128];
        for (int i = 0; i < sz; i++) pt[i] = (uint8_t)(i * 7 + 3);

        uint8_t aad[] = {0xAA, 0xBB, 0xCC};

        size_t ct_len = neverc_chacha20poly1305_seal(
            ct, key, nonce, pt, (size_t)sz, aad, 3);
        char buf[64];
        snprintf(buf, sizeof(buf), "roundtrip sz=%d seal_len", sz);
        check_int(buf, (int)ct_len, sz + 16);

        int dec_len = neverc_chacha20poly1305_open(
            dec, key, nonce, ct, ct_len, aad, 3);
        snprintf(buf, sizeof(buf), "roundtrip sz=%d open_len", sz);
        check_int(buf, dec_len, sz);

        if (sz > 0) {
            snprintf(buf, sizeof(buf), "roundtrip sz=%d data", sz);
            check_bytes(buf, dec, pt, sz);
        }
    }
}

static void test_auth_failure(void) {
    printf("[authentication failure]\n");

    uint8_t key[32] = {0};
    uint8_t nonce[12] = {0};
    uint8_t pt[] = "hello world";
    uint8_t ct[11 + 16], dec[11];

    neverc_chacha20poly1305_seal(ct, key, nonce, pt, 11, (void *)0, 0);

    /* Tamper with ciphertext */
    ct[5] ^= 0xFF;
    int ret = neverc_chacha20poly1305_open(
        dec, key, nonce, ct, 11 + 16, (void *)0, 0);
    check_int("tampered ct rejected", ret, -1);

    /* Restore ct, tamper with tag */
    ct[5] ^= 0xFF;
    ct[11 + 5] ^= 0x01;
    ret = neverc_chacha20poly1305_open(
        dec, key, nonce, ct, 11 + 16, (void *)0, 0);
    check_int("tampered tag rejected", ret, -1);

    /* Restore tag, wrong AAD */
    ct[11 + 5] ^= 0x01;
    uint8_t wrong_aad[] = {0x01};
    ret = neverc_chacha20poly1305_open(
        dec, key, nonce, ct, 11 + 16, wrong_aad, 1);
    check_int("wrong AAD rejected", ret, -1);

    /* Too short ciphertext */
    ret = neverc_chacha20poly1305_open(
        dec, key, nonce, ct, 15, (void *)0, 0);
    check_int("too short rejected", ret, -1);

    /* No AAD, correct decrypt */
    ret = neverc_chacha20poly1305_open(
        dec, key, nonce, ct, 11 + 16, (void *)0, 0);
    check_int("correct open succeeds", ret, 11);
    check_bytes("correct plaintext", dec, pt, 11);
}

int main(void) {
    printf("=== NeverC ChaCha20-Poly1305 AEAD Tests ===\n");
    test_rfc8439();
    test_roundtrip();
    test_auth_failure();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
