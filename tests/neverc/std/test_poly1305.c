#include "neverc/std/crypto/poly1305.h"
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

static void test_rfc7539_a3_r_zero(void) {
    printf("[Poly1305 RFC 7539 A.3 #2 r=0]\n");
    /* r is all zeros so clamp is a no-op; tag must be the pad s. */
    uint8_t key[32];
    hex_to_bytes(
        "00000000000000000000000000000000"
        "36e5f6b5c5e06070f0efca96227a863e", key, 32);
    const char *msg =
        "Any submission to the IETF intended by the Contributor for publication"
        " as all or part of an IETF Internet-Draft or RFC and any statement made"
        " within the context of an IETF activity is considered an \"IETF"
        " Contribution\". Such statements include oral statements in IETF"
        " sessions, as well as written and electronic communications made at"
        " any time or place, which are addressed to";
    uint8_t tag[16];
    neverc_poly1305_auth(tag, (const uint8_t *)msg, strlen(msg), key);
    uint8_t expected[16];
    hex_to_bytes("36e5f6b5c5e06070f0efca96227a863e", expected, 16);
    check_true("r=0 tag is the pad", memcmp(tag, expected, 16) == 0);
}

static void test_clamp_ignores_forbidden_bits(void) {
    printf("[Poly1305 clamp]\n");
    /* RFC 7539: r &= 0x0ffffffc0ffffffc0ffffffc0fffffff. Two keys that
     * differ only in the cleared bits must authenticate identically. */
    uint8_t key[32], key_unclamped[32];
    hex_to_bytes(
        "85d6be7857556d337f4452fe42d506a8"
        "0103808afb0db2fd4abff6af4149f51b", key, 32);
    memcpy(key_unclamped, key, 32);
    key_unclamped[3]  |= 0xF0;
    key_unclamped[4]  |= 0x03;
    key_unclamped[7]  |= 0xF0;
    key_unclamped[8]  |= 0x03;
    key_unclamped[11] |= 0xF0;
    key_unclamped[12] |= 0x03;
    key_unclamped[15] |= 0xF0;

    const char *msg = "Cryptographic Forum Research Group";
    uint8_t tag[16], tag2[16];
    neverc_poly1305_auth(tag, (const uint8_t *)msg, strlen(msg), key);
    neverc_poly1305_auth(tag2, (const uint8_t *)msg, strlen(msg), key_unclamped);
    check_true("clamp ignores forbidden r bits", memcmp(tag, tag2, 16) == 0);
    uint8_t expected[16];
    hex_to_bytes("a8061dc1305136c6c22b8baf0c0127a9", expected, 16);
    check_true("clamped key still matches RFC 7539",
               memcmp(tag, expected, 16) == 0);
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

    /* RFC 7539: no blocks => h = 0, tag = s (the second half of the key). */
    check_true("empty msg tag is the pad",
               memcmp(tag, key + 16, 16) == 0);

    uint8_t rfc_key[32];
    hex_to_bytes(
        "85d6be7857556d337f4452fe42d506a8"
        "0103808afb0db2fd4abff6af4149f51b", rfc_key, 32);
    uint8_t rfc_tag[16];
    neverc_poly1305_auth(rfc_tag, NULL, 0, rfc_key);
    uint8_t rfc_expected[16];
    hex_to_bytes("0103808afb0db2fd4abff6af4149f51b", rfc_expected, 16);
    check_true("empty msg RFC 7539 key tag",
               memcmp(rfc_tag, rfc_expected, 16) == 0);
    check_true("empty msg RFC 7539 key verify",
               neverc_poly1305_verify(rfc_expected, NULL, 0, rfc_key));
    rfc_expected[0] ^= 1;
    check_true("empty msg rejects wrong tag",
               !neverc_poly1305_verify(rfc_expected, NULL, 0, rfc_key));

    uint8_t zero_key[32];
    memset(zero_key, 0, sizeof(zero_key));
    uint8_t zero_tag[16];
    neverc_poly1305_auth(zero_tag, (const uint8_t *)"", 0, zero_key);
    uint8_t expected_zero[16];
    memset(expected_zero, 0, sizeof(expected_zero));
    check_true("empty msg all-zero key tag",
               memcmp(zero_tag, expected_zero, 16) == 0);
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

    /* Partial last block after many full blocks. The old
     * `off += msg_len` increment overflowed size_t when msg_len > SIZE_MAX/2. */
    {
        uint8_t long_msg[4097];
        for (int i = 0; i < 4097; i++)
            long_msg[i] = (uint8_t)(i * 13 + 1);
        uint8_t tag[16];
        neverc_poly1305_auth(tag, long_msg, 4097, key);
        check_true("verify len=4097 partial tail",
                   neverc_poly1305_verify(tag, long_msg, 4097, key));
        uint8_t tampered[16];
        memcpy(tampered, tag, 16);
        tampered[15] ^= 1;
        check_true("reject tampered len=4097 tag",
                   !neverc_poly1305_verify(tampered, long_msg, 4097, key));
    }
}

static void test_null_inputs_fail_closed(void) {
    printf("[Poly1305 null inputs]\n");
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i + 1);
    const uint8_t msg[] = "abc";
    uint8_t tag[16], sentinel[16];
    memset(sentinel, 0xAA, sizeof(sentinel));
    memcpy(tag, sentinel, 16);

    neverc_poly1305_auth(tag, msg, sizeof(msg) - 1, NULL);
    check_true("null key leaves tag unmodified",
               memcmp(tag, sentinel, 16) == 0);
    neverc_poly1305_auth(NULL, msg, sizeof(msg) - 1, key);
    neverc_poly1305_auth(tag, NULL, 4, key);
    check_true("null msg leaves tag unmodified",
               memcmp(tag, sentinel, 16) == 0);

    check_true("verify null key",
               !neverc_poly1305_verify(sentinel, msg, sizeof(msg) - 1, NULL));
    check_true("verify null tag",
               !neverc_poly1305_verify(NULL, msg, sizeof(msg) - 1, key));
    check_true("verify null msg",
               !neverc_poly1305_verify(sentinel, NULL, 4, key));
}

int main(void) {
    printf("=== NeverC Poly1305 Tests ===\n\n");
    test_rfc7539();
    test_rfc7539_aead_mac();
    test_rfc7539_a3_r_zero();
    test_clamp_ignores_forbidden_bits();
    test_verify_tamper();
    test_empty();
    test_various_lengths();
    test_null_inputs_fail_closed();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
