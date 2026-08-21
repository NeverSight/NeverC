/*
 * ChaCha20-Poly1305 AEAD tests — RFC 8439 Section 2.8.2 official vectors.
 */
#include "neverc/std/crypto/chacha20poly1305.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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
    memset(dec, 0xAA, sizeof(dec));
    ct[5] ^= 0xFF;
    int ret = neverc_chacha20poly1305_open(
        dec, key, nonce, ct, 11 + 16, (void *)0, 0);
    check_int("tampered ct rejected", ret, -1);
    {
        uint8_t aa[11];
        memset(aa, 0xAA, sizeof(aa));
        check_bytes("auth failure leaves plaintext unmodified", dec, aa, 11);
    }

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

static void test_nonce_reuse_leaks_xor(void) {
    printf("[nonce reuse]\n");
    uint8_t key[32] = {1};
    uint8_t nonce[12] = {2};
    uint8_t pt1[32], pt2[32], ct1[32 + 16], ct2[32 + 16];
    for (int i = 0; i < 32; i++) {
        pt1[i] = (uint8_t)i;
        pt2[i] = (uint8_t)(0x80 + i);
    }
    neverc_chacha20poly1305_seal(ct1, key, nonce, pt1, 32, NULL, 0);
    neverc_chacha20poly1305_seal(ct2, key, nonce, pt2, 32, NULL, 0);
    int xor_leaks = 1;
    for (int i = 0; i < 32; i++) {
        if ((uint8_t)(ct1[i] ^ ct2[i]) != (uint8_t)(pt1[i] ^ pt2[i]))
            xor_leaks = 0;
    }
    check_int("reused nonce leaks plaintext XOR", xor_leaks, 1);
}

static void test_large_message_roundtrip(void) {
    printf("[large message]\n");

    const size_t plaintext_len = 4096;
    const size_t aad_len = 777;
    uint8_t key[32] = {0};
    uint8_t nonce[12] = {0};
    uint8_t *plaintext = (uint8_t *)malloc(plaintext_len);
    uint8_t *aad = (uint8_t *)malloc(aad_len);
    uint8_t *ciphertext = (uint8_t *)malloc(plaintext_len + 16);
    uint8_t *decrypted = (uint8_t *)malloc(plaintext_len);

    if (!plaintext || !aad || !ciphertext || !decrypted) {
        check_int("large message allocation", 0, 1);
        free(plaintext);
        free(aad);
        free(ciphertext);
        free(decrypted);
        return;
    }

    for (size_t i = 0; i < plaintext_len; i++)
        plaintext[i] = (uint8_t)(i * 13 + 7);
    for (size_t i = 0; i < aad_len; i++)
        aad[i] = (uint8_t)(i * 5 + 11);

    size_t sealed_len = neverc_chacha20poly1305_seal(
        ciphertext, key, nonce, plaintext, plaintext_len, aad, aad_len);
    check_int("large seal length", (int)sealed_len,
              (int)(plaintext_len + 16));

    int opened_len = neverc_chacha20poly1305_open(
        decrypted, key, nonce, ciphertext, sealed_len, aad, aad_len);
    check_int("large open length", opened_len, (int)plaintext_len);
    check_bytes("large roundtrip data", decrypted, plaintext,
                (int)plaintext_len);

    free(plaintext);
    free(aad);
    free(ciphertext);
    free(decrypted);
}

static void test_invalid_inputs_and_limits(void) {
    printf("[invalid inputs and limits]\n");
    uint8_t key[32] = {0};
    uint8_t nonce[12] = {0};
    uint8_t byte = 0;
    uint8_t sealed[16];

    check_int("seal rejects null output",
              (int)neverc_chacha20poly1305_seal(
                  NULL, key, nonce, NULL, 0, NULL, 0),
              0);
    check_int("seal rejects invalid plaintext span",
              (int)neverc_chacha20poly1305_seal(
                  sealed, key, nonce, NULL, 1, NULL, 0),
              0);
    check_int("seal rejects invalid AAD span",
              (int)neverc_chacha20poly1305_seal(
                  sealed, key, nonce, NULL, 0, NULL, 1),
              0);
    check_int("empty seal succeeds",
              (int)neverc_chacha20poly1305_seal(
                  sealed, key, nonce, NULL, 0, NULL, 0),
              16);
    check_int("empty open accepts null output",
              neverc_chacha20poly1305_open(
                  NULL, key, nonce, sealed, sizeof(sealed), &byte, 0),
              0);
    uint8_t nonempty_ciphertext[17] = {0};
    check_int("nonempty open rejects null output",
              neverc_chacha20poly1305_open(
                  NULL, key, nonce, nonempty_ciphertext,
                  sizeof(nonempty_ciphertext), NULL, 0),
              -1);
    check_int("open rejects null ciphertext",
              neverc_chacha20poly1305_open(
                  &byte, key, nonce, NULL, sizeof(sealed), NULL, 0),
              -1);
    check_int("open rejects invalid AAD span",
              neverc_chacha20poly1305_open(
                  &byte, key, nonce, sealed, sizeof(sealed), NULL, 1),
              -1);
    check_int("seal rejects null nonce",
              (int)neverc_chacha20poly1305_seal(
                  sealed, key, NULL, NULL, 0, NULL, 0),
              0);
    check_int("seal rejects null key",
              (int)neverc_chacha20poly1305_seal(
                  sealed, NULL, nonce, NULL, 0, NULL, 0),
              0);
    check_int("open rejects null nonce",
              neverc_chacha20poly1305_open(
                  NULL, key, NULL, sealed, sizeof(sealed), NULL, 0),
              -1);
    check_int("open rejects null key",
              neverc_chacha20poly1305_open(
                  NULL, NULL, nonce, sealed, sizeof(sealed), NULL, 0),
              -1);
#if SIZE_MAX > UINT32_MAX
    check_int("seal rejects counter-wrap length",
              (int)neverc_chacha20poly1305_seal(
                  sealed, key, nonce, &byte,
                  ((size_t)1 << 38) - 63, NULL, 0),
              0);
    check_int("seal rejects lengths open cannot return",
              (int)neverc_chacha20poly1305_seal(
                  sealed, key, nonce, &byte,
                  (size_t)INT_MAX + 1u, NULL, 0),
              0);
#endif
}

static void test_empty_aad_known_answer(void) {
    printf("[empty AAD known-answer]\n");

    /* BoringSSL chacha20_poly1305_tests.txt: empty PT and empty AAD. */
    uint8_t key[32], nonce[12], tag[16];
    hex_to_bytes("9a97f65b9b4c721b960a672145fca8d4"
                 "e32e67f9111ea979ce9c4826806aeee6", key, 32);
    hex_to_bytes("000000003de9c0da2bd7f91e", nonce, 12);
    hex_to_bytes("5a6e21f4ba6dbee57380e79e79c30def", tag, 16);

    uint8_t sealed[16];
    size_t n = neverc_chacha20poly1305_seal(
        sealed, key, nonce, NULL, 0, NULL, 0);
    check_int("empty AAD seal length", (int)n, 16);
    check_bytes("empty AAD tag", sealed, tag, 16);
    check_int("empty AAD open",
              neverc_chacha20poly1305_open(
                  NULL, key, nonce, sealed, 16, NULL, 0),
              0);

    /* 1-byte AAD (15 bytes of pad) from the same BoringSSL corpus. */
    hex_to_bytes("808182838485868788898a8b8c8d8e8f"
                 "909192939495969798999a9b9c9d9e9f", key, 32);
    hex_to_bytes("070000004041424344454647", nonce, 12);
    uint8_t pt[16], aad[1], expected[16 + 16];
    memcpy(pt, "123456789abcdef0", 16);
    aad[0] = '1';
    hex_to_bytes("ae49da6934cb77822c83ed9852e46c9e", expected, 16);
    hex_to_bytes("dac9c841c168379dcf8f2bb8e22d6da2", expected + 16, 16);
    uint8_t out[32];
    n = neverc_chacha20poly1305_seal(out, key, nonce, pt, 16, aad, 1);
    check_int("1-byte AAD seal length", (int)n, 32);
    check_bytes("1-byte AAD ciphertext+tag", out, expected, 32);
}

/* 256-byte plaintext hits ChaCha20's 4-block SIMD kernel (counter starts at 1). */
static void test_simd_known_answer(void) {
    printf("[256-byte SIMD known-answer]\n");
    uint8_t key[32], nonce[12], pt[256], aad[3], expected_ct[16], expected_tag[16];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)i;
    for (int i = 0; i < 256; i++) pt[i] = (uint8_t)(i * 7 + 3);
    memcpy(aad, "abc", 3);
    hex_to_bytes("8af11918363188748cc176a3cf436b0f", expected_ct, 16);
    hex_to_bytes("b57287e4729efa41ebdd955d3cc7485b", expected_tag, 16);

    uint8_t out[256 + 16];
    size_t n = neverc_chacha20poly1305_seal(out, key, nonce, pt, 256, aad, 3);
    check_int("256-byte seal length", (int)n, 272);
    check_bytes("256-byte ciphertext prefix", out, expected_ct, 16);
    check_bytes("256-byte tag", out + 256, expected_tag, 16);

    uint8_t dec[256];
    check_int("256-byte open",
              neverc_chacha20poly1305_open(dec, key, nonce, out, 272, aad, 3),
              256);
    check_bytes("256-byte plaintext", dec, pt, 256);
}

static void test_inplace_and_aad_overlap(void) {
    printf("[in-place and AAD overlap]\n");
    uint8_t key[32] = {0};
    uint8_t nonce[12] = {0};
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i + 1);
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(i + 50);

    uint8_t pt[32];
    for (int i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 9 + 2);
    uint8_t aad[12];
    for (int i = 0; i < 12; i++) aad[i] = (uint8_t)(0xA0 + i);

    uint8_t disjoint[32 + 16];
    size_t n = neverc_chacha20poly1305_seal(
        disjoint, key, nonce, pt, 32, aad, 12);
    check_int("disjoint seal length", (int)n, 48);

    uint8_t in_place[32 + 16];
    memcpy(in_place, pt, 32);
    n = neverc_chacha20poly1305_seal(
        in_place, key, nonce, in_place, 32, aad, 12);
    check_int("in-place seal length", (int)n, 48);
    check_bytes("in-place matches disjoint", in_place, disjoint, 48);

    uint8_t recovered[32];
    int opened = neverc_chacha20poly1305_open(
        recovered, key, nonce, in_place, 48, aad, 12);
    check_int("in-place open length", opened, 32);
    check_bytes("in-place open plaintext", recovered, pt, 32);

    opened = neverc_chacha20poly1305_open(
        in_place, key, nonce, in_place, 48, aad, 12);
    check_int("in-place decrypt length", opened, 32);
    check_bytes("in-place decrypt plaintext", in_place, pt, 32);

    /* AAD lives at the front of the output buffer. Encrypting first used to
     * overwrite AAD before the MAC was computed. */
    uint8_t overlap[12 + 32 + 16];
    memcpy(overlap, aad, 12);
    memcpy(overlap + 12, pt, 32);
    n = neverc_chacha20poly1305_seal(
        overlap, key, nonce, overlap + 12, 32, overlap, 12);
    check_int("AAD-overlap seal length", (int)n, 48);
    check_bytes("AAD-overlap ciphertext+tag", overlap, disjoint, 48);

    /* dest-after-src: ChaCha XOR used to clobber unread plaintext. */
    uint8_t wide[32 + 16 + 4];
    memcpy(wide, pt, 32);
    n = neverc_chacha20poly1305_seal(
        wide + 4, key, nonce, wide, 32, aad, 12);
    check_int("dst=src+4 seal length", (int)n, 48);
    check_bytes("dst=src+4 ciphertext+tag", wide + 4, disjoint, 48);

    memcpy(wide, disjoint, 48);
    check_int("dst=src+4 open length",
              neverc_chacha20poly1305_open(
                  wide + 4, key, nonce, wide, 48, aad, 12),
              32);
    check_bytes("dst=src+4 open plaintext", wide + 4, pt, 32);
}

int main(void) {
    printf("=== NeverC ChaCha20-Poly1305 AEAD Tests ===\n");
    test_rfc8439();
    test_roundtrip();
    test_auth_failure();
    test_nonce_reuse_leaks_xor();
    test_large_message_roundtrip();
    test_invalid_inputs_and_limits();
    test_empty_aad_known_answer();
    test_simd_known_answer();
    test_inplace_and_aad_overlap();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
