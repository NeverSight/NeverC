#include "neverc/std/crypto/pbkdf2.h"
#include <stdint.h>
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

static void test_rfc7914_vectors(void) {
    printf("[PBKDF2-SHA256 RFC 7914 vectors]\n");

    /* RFC 7914 Section 11 — PBKDF2-HMAC-SHA256 test vectors */
    {
        const char *pass = "passwd";
        const char *salt = "salt";
        uint8_t dk[64];
        neverc_pbkdf2_sha256(dk, 64,
            (const uint8_t *)pass, strlen(pass),
            (const uint8_t *)salt, strlen(salt), 1);

        uint8_t expected[64];
        /* Full 64-byte OKM. RFC 7914's printed second half is truncated;
         * the complete T_1||T_2 value is the standard PBKDF2-HMAC-SHA-256
         * result (OpenSSL / Python hashlib). */
        hex_to_bytes(
            "55ac046e56e3089fec1691c22544b605"
            "f94185216dde0465e68b9d57c20dacbc"
            "49ca9cccf179b645991664b39d77ef31"
            "7c71b845b1e30bd509112041d3a19783",
            expected, 64);
        check_true("RFC7914 passwd/salt/1 (64 bytes)", memcmp(dk, expected, 64) == 0);
    }

    /* password="Password", salt="NaCl", c=80000 */
    {
        const char *pass = "Password";
        const char *salt = "NaCl";
        uint8_t dk[64];
        neverc_pbkdf2_sha256(dk, 64,
            (const uint8_t *)pass, strlen(pass),
            (const uint8_t *)salt, strlen(salt), 80000);

        uint8_t expected[64];
        hex_to_bytes(
            "4ddcd8f60b98be21830cee5ef22701f9"
            "641a4418d04c0414aeff08876b34ab56"
            "a1d425a1225833549adb841b51c9b317"
            "6a272bdebba1d078478f62b397f33c8d",
            expected, 64);
        check_true("RFC7914 Password/NaCl/80000", memcmp(dk, expected, 64) == 0);
    }
}

static void test_basic(void) {
    printf("[PBKDF2-SHA256 basic]\n");

    /* Same password+salt+iterations should give same result */
    uint8_t dk1[32], dk2[32];
    const char *pass = "testpassword";
    const char *salt = "testsalt";

    neverc_pbkdf2_sha256(dk1, 32, (const uint8_t *)pass, strlen(pass),
                         (const uint8_t *)salt, strlen(salt), 100);
    neverc_pbkdf2_sha256(dk2, 32, (const uint8_t *)pass, strlen(pass),
                         (const uint8_t *)salt, strlen(salt), 100);
    check_true("deterministic", memcmp(dk1, dk2, 32) == 0);

    /* Different password → different output */
    neverc_pbkdf2_sha256(dk2, 32, (const uint8_t *)"other", 5,
                         (const uint8_t *)salt, strlen(salt), 100);
    check_true("diff password", memcmp(dk1, dk2, 32) != 0);

    /* Different salt → different output */
    neverc_pbkdf2_sha256(dk2, 32, (const uint8_t *)pass, strlen(pass),
                         (const uint8_t *)"othersalt", 9, 100);
    check_true("diff salt", memcmp(dk1, dk2, 32) != 0);

    /* Different iterations → different output */
    neverc_pbkdf2_sha256(dk2, 32, (const uint8_t *)pass, strlen(pass),
                         (const uint8_t *)salt, strlen(salt), 200);
    check_true("diff iterations", memcmp(dk1, dk2, 32) != 0);
}

static void test_various_lengths(void) {
    printf("[PBKDF2-SHA256 various output lengths]\n");
    const char *pass = "key";
    const char *salt = "salt";

    for (int len = 1; len <= 96; len += 15) {
        uint8_t dk[96];
        int rc = neverc_pbkdf2_sha256(dk, len, (const uint8_t *)pass, 3,
                                      (const uint8_t *)salt, 4, 10);
        char buf[64];
        snprintf(buf, sizeof(buf), "len=%d ok", len);
        check_true(buf, rc == 0);
    }
}

static void test_invalid_inputs(void) {
    printf("[PBKDF2-SHA256 invalid inputs]\n");
    uint8_t byte = 0;
    uint8_t dk[32];

    check_true("null output rejected",
               neverc_pbkdf2_sha256(
                   NULL, sizeof(dk), NULL, 0, NULL, 0, 1) == -1);
    check_true("invalid password span rejected",
               neverc_pbkdf2_sha256(
                   dk, sizeof(dk), NULL, 1, &byte, 1, 1) == -1);
    check_true("invalid salt span rejected",
               neverc_pbkdf2_sha256(
                   dk, sizeof(dk), &byte, 1, NULL, 1, 1) == -1);
    check_true("empty null spans accepted",
               neverc_pbkdf2_sha256(
                   dk, sizeof(dk), NULL, 0, NULL, 0, 1) == 0);
    {
        uint8_t sentinel[32];
        memset(sentinel, 0xAA, sizeof(sentinel));
        memcpy(dk, sentinel, sizeof(dk));
        check_true("RFC 8018 rejects iterations=0",
                   neverc_pbkdf2_sha256(
                       dk, sizeof(dk), &byte, 1, &byte, 1, 0) == -1);
        check_true("iterations=0 leaves derived key unmodified",
                   memcmp(dk, sentinel, sizeof(dk)) == 0);
        check_true("rejects negative iterations",
                   neverc_pbkdf2_sha256(
                       dk, sizeof(dk), &byte, 1, &byte, 1, -1) == -1);
        check_true("rejects zero derived-key length",
                   neverc_pbkdf2_sha256(
                       dk, 0, &byte, 1, &byte, 1, 1) == -1);
    }
    {
        uint8_t long_salt[300];
        uint8_t dk_long[32];
        for (size_t i = 0; i < sizeof(long_salt); i++)
            long_salt[i] = (uint8_t)(i + 1);
        check_true("long salt accepted",
                   neverc_pbkdf2_sha256(
                       dk_long, sizeof(dk_long),
                       (const uint8_t *)"pw", 2,
                       long_salt, sizeof(long_salt), 2) == 0);
    }
#if SIZE_MAX > UINT32_MAX
    check_true("RFC derived-key limit enforced",
               neverc_pbkdf2_sha256(
                   dk, (size_t)UINT32_MAX * 32U + 1U,
                   &byte, 1, &byte, 1, 1) == -1);
#endif
#if SIZE_MAX > (UINT64_MAX / 8)
    check_true("wrapping password rejected",
               neverc_pbkdf2_sha256(
                   dk, sizeof(dk), &byte, SIZE_MAX, &byte, 1, 1) == -1);
    check_true("wrapping salt rejected",
               neverc_pbkdf2_sha256(
                   dk, sizeof(dk), &byte, 1, &byte,
                   (size_t)(UINT64_MAX / 8) - 67, 1) == -1);
#endif
}

int main(void) {
    printf("=== NeverC PBKDF2 Tests ===\n\n");
    test_rfc7914_vectors();
    test_basic();
    test_various_lengths();
    test_invalid_inputs();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
