#include "neverc/crypto/poly1305.h"
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

static void test_rfc7539(void) {
    printf("[Poly1305 RFC 7539 Section 2.5.2]\n");

    uint8_t key[32];
    hex_to_bytes(
        "85d6be7857556d337f4452fe42d506a8"
        "0103808afb0db2fd4abff6af4149f51b", key, 32);

    const char *msg = "Cryptographic Forum Research Group";
    uint8_t tag[16];
    neverc_poly1305_auth(tag, (const uint8_t *)msg, strlen(msg), key);

    uint8_t expected[16];
    hex_to_bytes("a8061dc1305136c6c22b8baf0c0127a9", expected, 16);
    check_true("RFC 7539 tag", memcmp(tag, expected, 16) == 0);
    check_true("RFC 7539 verify", neverc_poly1305_verify(expected, (const uint8_t *)msg, strlen(msg), key));
}

static void test_rfc7539_aead_mac(void) {
    printf("[Poly1305 RFC 7539 A.3 #1]\n");

    uint8_t key[32];
    hex_to_bytes(
        "00000000000000000000000000000000"
        "00000000000000000000000000000000", key, 32);

    uint8_t msg[64];
    memset(msg, 0, 64);

    uint8_t tag[16];
    neverc_poly1305_auth(tag, msg, 64, key);

    uint8_t expected[16];
    hex_to_bytes("00000000000000000000000000000000", expected, 16);
    check_true("all-zero tag", memcmp(tag, expected, 16) == 0);
}

static void test_verify_tamper(void) {
    printf("[Poly1305 tamper detection]\n");

    uint8_t key[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                       17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    const uint8_t msg[] = "Hello, Poly1305!";
    uint8_t tag[16];
    neverc_poly1305_auth(tag, msg, sizeof(msg) - 1, key);

    check_true("verify valid", neverc_poly1305_verify(tag, msg, sizeof(msg) - 1, key));

    uint8_t tampered[16];
    memcpy(tampered, tag, 16);
    tampered[0] ^= 1;
    check_true("reject tampered tag", !neverc_poly1305_verify(tampered, msg, sizeof(msg) - 1, key));

    uint8_t bad_msg[] = "Hello, Poly1306!";
    check_true("reject tampered msg", !neverc_poly1305_verify(tag, bad_msg, sizeof(bad_msg) - 1, key));
}

static void test_empty(void) {
    printf("[Poly1305 empty message]\n");
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i + 1);
    uint8_t tag[16];
    neverc_poly1305_auth(tag, (const uint8_t *)"", 0, key);
    check_true("empty msg verify", neverc_poly1305_verify(tag, (const uint8_t *)"", 0, key));
}

static void test_various_lengths(void) {
    printf("[Poly1305 various lengths]\n");
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i * 7 + 3);

    uint8_t msg[200];
    for (int i = 0; i < 200; i++) msg[i] = (uint8_t)(i * 11 + 5);

    for (int len = 1; len <= 200; len += 13) {
        uint8_t tag[16];
        neverc_poly1305_auth(tag, msg, len, key);
        char buf[64];
        snprintf(buf, sizeof(buf), "verify len=%d", len);
        check_true(buf, neverc_poly1305_verify(tag, msg, len, key));
    }
}

int main(void) {
    printf("=== NeverC Poly1305 Tests ===\n\n");
    test_rfc7539();
    test_rfc7539_aead_mac();
    test_verify_tamper();
    test_empty();
    test_various_lengths();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
