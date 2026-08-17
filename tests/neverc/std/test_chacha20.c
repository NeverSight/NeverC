#include "neverc/std/crypto/chacha20.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

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

static void test_rfc7539_block(void) {
    printf("[ChaCha20 RFC 7539 block test]\n");

    /* RFC 7539 Section 2.3.2 test vector */
    uint8_t key[32], nonce[12];
    hex_to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    hex_to_bytes("000000090000004a00000000", nonce, 12);

    uint32_t state[16];
    state[0]  = 0x61707865; state[1]  = 0x3320646e;
    state[2]  = 0x79622d32; state[3]  = 0x6b206574;
    for (int i = 0; i < 8; i++) {
        state[4+i] = (uint32_t)key[4*i] | ((uint32_t)key[4*i+1]<<8) |
                     ((uint32_t)key[4*i+2]<<16) | ((uint32_t)key[4*i+3]<<24);
    }
    state[12] = 1;
    state[13] = (uint32_t)nonce[0] | ((uint32_t)nonce[1]<<8) |
                ((uint32_t)nonce[2]<<16) | ((uint32_t)nonce[3]<<24);
    state[14] = (uint32_t)nonce[4] | ((uint32_t)nonce[5]<<8) |
                ((uint32_t)nonce[6]<<16) | ((uint32_t)nonce[7]<<24);
    state[15] = (uint32_t)nonce[8] | ((uint32_t)nonce[9]<<8) |
                ((uint32_t)nonce[10]<<16) | ((uint32_t)nonce[11]<<24);

    uint8_t out[64];
    neverc_chacha20_block(state, out);

    uint8_t expected[64];
    hex_to_bytes(
        "10f1e7e4d13b5915500fdd1fa32071c4"
        "c7d1f4c733c068030422aa9ac3d46c4e"
        "d2826446079faa0914c2d705d98b02a2"
        "b5129cd1de164eb9cbd083e8a2503c4e", expected, 64);

    check_true("RFC 7539 block output", memcmp(out, expected, 64) == 0);
}

static void test_rfc7539_encrypt(void) {
    printf("[ChaCha20 RFC 7539 encryption test]\n");

    /* RFC 7539 Section 2.4.2 */
    uint8_t key[32], nonce[12];
    hex_to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    hex_to_bytes("000000000000004a00000000", nonce, 12);

    const char *plaintext =
        "Ladies and Gentlemen of the class of '99: "
        "If I could offer you only one tip for the future, sunscreen would be it.";
    size_t pt_len = strlen(plaintext);

    uint8_t expected_ct[114];
    hex_to_bytes(
        "6e2e359a2568f98041ba0728dd0d6981"
        "e97e7aec1d4360c20a27afccfd9fae0b"
        "f91b65c5524733ab8f593dabcd62b357"
        "1639d624e65152ab8f530c359f0861d8"
        "07ca0dbf500d6a6156a38e088a22b65e"
        "52bc514d16ccf806818ce91ab7793736"
        "5af90bbf74a35be6b40b8eedf2785e42"
        "874d", expected_ct, 114);

    neverc_chacha20_ctx ctx;
    neverc_chacha20_init(&ctx, key, nonce, 1);

    uint8_t ciphertext[114];
    neverc_chacha20_xor(&ctx, ciphertext, (const uint8_t *)plaintext, pt_len);
    check_true("RFC 7539 ciphertext", memcmp(ciphertext, expected_ct, pt_len) == 0);

    /* Decrypt: XOR again should recover plaintext */
    neverc_chacha20_init(&ctx, key, nonce, 1);
    uint8_t recovered[114];
    neverc_chacha20_xor(&ctx, recovered, ciphertext, pt_len);
    check_true("RFC 7539 decrypt round-trip",
               memcmp(recovered, plaintext, pt_len) == 0);
}

static void test_round_trip(void) {
    printf("[ChaCha20 round-trip]\n");

    uint8_t key[32], nonce[12];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i * 3 + 7);
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(i * 5 + 13);

    uint8_t msg[200], enc[200], dec[200];
    for (int i = 0; i < 200; i++) msg[i] = (uint8_t)(i * 7 + 11);

    for (int len = 1; len <= 200; len += 17) {
        neverc_chacha20_ctx ctx1, ctx2;
        neverc_chacha20_init(&ctx1, key, nonce, 0);
        neverc_chacha20_xor(&ctx1, enc, msg, len);

        neverc_chacha20_init(&ctx2, key, nonce, 0);
        neverc_chacha20_xor(&ctx2, dec, enc, len);

        char buf[64];
        snprintf(buf, sizeof(buf), "round-trip len=%d", len);
        check_true(buf, memcmp(dec, msg, len) == 0);
    }
}

static void test_incremental(void) {
    printf("[ChaCha20 incremental vs one-shot]\n");

    uint8_t key[32], nonce[12], msg[100], out1[100], out2[100];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    for (int i = 0; i < 12; i++) nonce[i] = (uint8_t)(i + 32);
    for (int i = 0; i < 100; i++) msg[i] = (uint8_t)(i * 11);

    neverc_chacha20_ctx ctx;
    neverc_chacha20_init(&ctx, key, nonce, 0);
    neverc_chacha20_xor(&ctx, out1, msg, 100);

    neverc_chacha20_init(&ctx, key, nonce, 0);
    neverc_chacha20_xor(&ctx, out2, msg, 30);
    neverc_chacha20_xor(&ctx, out2 + 30, msg + 30, 70);
    check_true("incremental == one-shot", memcmp(out1, out2, 100) == 0);

    /* Byte-by-byte */
    neverc_chacha20_init(&ctx, key, nonce, 0);
    for (int i = 0; i < 100; i++)
        neverc_chacha20_xor(&ctx, out2 + i, msg + i, 1);
    check_true("byte-by-byte == one-shot", memcmp(out1, out2, 100) == 0);
}

static void test_counter_wrap(void) {
    printf("[ChaCha20 counter wrap]\n");
    uint8_t key[32] = {0}, nonce[12] = {0}, in[128] = {0}, out[128];
    memset(out, 0xAA, sizeof(out));
    neverc_chacha20_ctx ctx;
    neverc_chacha20_init(&ctx, key, nonce, 0xFFFFFFFFu);
    neverc_chacha20_xor(&ctx, out, in, 64);
    neverc_chacha20_xor(&ctx, out + 64, in + 64, 64);
    uint8_t aa[64];
    memset(aa, 0xAA, sizeof(aa));
    check_true("wrap does not reuse keystream", memcmp(out + 64, aa, 64) == 0);
    check_true("last block still emitted", memcmp(out, aa, 64) != 0);

    memset(out, 0xAA, sizeof(out));
    neverc_chacha20_init(&ctx, key, nonce, 0xFFFFFFFFu);
    neverc_chacha20_xor(&ctx, out, in, 128);
    check_true("oversize wrap request is all-or-nothing",
               memcmp(out, aa, 64) == 0 && memcmp(out + 64, aa, 64) == 0);
}

static void test_counter_wrap_leftover(void) {
    printf("[ChaCha20 last-block leftover]\n");
    uint8_t key[32] = {0}, nonce[12] = {0}, in[64] = {0};
    uint8_t one_shot[64], chunked[64], extra[16];
    neverc_chacha20_ctx ctx;

    neverc_chacha20_init(&ctx, key, nonce, 0xFFFFFFFFu);
    neverc_chacha20_xor(&ctx, one_shot, in, 64);

    neverc_chacha20_init(&ctx, key, nonce, 0xFFFFFFFFu);
    neverc_chacha20_xor(&ctx, chunked, in, 32);
    neverc_chacha20_xor(&ctx, chunked + 32, in + 32, 32);
    check_true("32+32 leftover matches 64-byte last block",
               memcmp(one_shot, chunked, 64) == 0);

    memset(extra, 0xAA, sizeof(extra));
    neverc_chacha20_xor(&ctx, extra, in, sizeof(extra));
    uint8_t aa[16];
    memset(aa, 0xAA, sizeof(aa));
    check_true("no keystream after last-block leftover is consumed",
               memcmp(extra, aa, sizeof(extra)) == 0);

    neverc_chacha20_init(&ctx, key, nonce, 0xFFFFFFFFu);
    neverc_chacha20_xor(&ctx, chunked, in, 1);
    neverc_chacha20_xor(&ctx, chunked + 1, in + 1, 63);
    check_true("1+63 leftover matches 64-byte last block",
               memcmp(one_shot, chunked, 64) == 0);
}

static void test_null_inputs(void) {
    printf("[ChaCha20 null inputs]\n");
    uint8_t key[32] = {0}, nonce[12] = {0};
    neverc_chacha20_ctx ctx;
    memset(&ctx, 0xAA, sizeof(ctx));
    neverc_chacha20_init(NULL, key, nonce, 0);
    neverc_chacha20_init(&ctx, NULL, nonce, 0);
    neverc_chacha20_init(&ctx, key, NULL, 0);
    check_true("null init leaves context unmodified",
               ctx.state[0] == 0xAAAAAAAAu);

    neverc_chacha20_init(&ctx, key, nonce, 0);
    uint8_t out[16];
    memset(out, 0xAA, sizeof(out));
    neverc_chacha20_xor(&ctx, NULL, out, sizeof(out));
    neverc_chacha20_xor(&ctx, out, NULL, sizeof(out));
    uint8_t aa[16];
    memset(aa, 0xAA, sizeof(aa));
    check_true("null xor leaves output unmodified",
               memcmp(out, aa, sizeof(out)) == 0);

    neverc_chacha20_ctx z;
    memset(&z, 0, sizeof(z));
    neverc_chacha20_init(&z, NULL, nonce, 0);
    uint8_t secret[32], leaked[32];
    memset(secret, 0x5a, sizeof(secret));
    memset(leaked, 0, sizeof(leaked));
    neverc_chacha20_xor(&z, leaked, secret, sizeof(secret));
    check_true("failed init does not copy plaintext",
               memcmp(leaked, secret, sizeof(secret)) != 0);
}

int main(void) {
    printf("=== NeverC ChaCha20 Tests ===\n\n");
    test_rfc7539_block();
    test_rfc7539_encrypt();
    test_round_trip();
    test_incremental();
    test_counter_wrap();
    test_counter_wrap_leftover();
    test_null_inputs();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
