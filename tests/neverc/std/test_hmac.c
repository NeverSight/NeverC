#include "neverc/std/crypto/hmac.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_hex(const char *name, const uint8_t *got, const char *expected_hex, size_t len) {
    tests_run++;
    char got_hex[256];
    for (size_t i = 0; i < len; i++)
        sprintf(got_hex + i * 2, "%02x", got[i]);
    got_hex[len * 2] = '\0';
    if (strcmp(got_hex, expected_hex) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s:\n    got  %s\n    want %s\n", name, got_hex, expected_hex);
    }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

/*
 * RFC 4231 — HMAC-SHA-256 test vectors.
 * These are the official IETF test cases.
 */
static void test_hmac_sha256_rfc4231(void) {
    printf("[hmac-sha256 RFC 4231 vectors]\n");

    uint8_t mac[32];

    /* Test Case 1 */
    {
        uint8_t key[20]; memset(key, 0x0b, 20);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha256(key, 20, data, 8, mac);
        check_hex("TC1 sha256",  mac,
            "b0344c61d8db38535ca8afceaf0bf12b"
            "881dc200c9833da726e9376c2e32cff7", 32);
    }

    /* Test Case 2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_sha256(key, 4, data, 28, mac);
        check_hex("TC2 sha256", mac,
            "5bdcc146bf60754e6a042426089575c7"
            "5a003f089d2739839dec58b964ec3843", 32);
    }

    /* Test Case 3 */
    {
        uint8_t key[20]; memset(key, 0xaa, 20);
        uint8_t data[50]; memset(data, 0xdd, 50);
        neverc_hmac_sha256(key, 20, data, 50, mac);
        check_hex("TC3 sha256", mac,
            "773ea91e36800e46854db8ebd09181a7"
            "2959098b3ef8c122d9635514ced565fe", 32);
    }

    /* Test Case 4 */
    {
        uint8_t key[25];
        for (int i = 0; i < 25; i++) key[i] = (uint8_t)(i + 1);
        uint8_t data[50]; memset(data, 0xcd, 50);
        neverc_hmac_sha256(key, 25, data, 50, mac);
        check_hex("TC4 sha256", mac,
            "82558a389a443c0ea4cc819899f2083a"
            "85f0faa3e578f8077a2e3ff46729665b", 32);
    }

    /* Test Case 6: key > block_size (131 bytes) */
    {
        uint8_t key[131]; memset(key, 0xaa, 131);
        const uint8_t *data = (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First";
        neverc_hmac_sha256(key, 131, data, 54, mac);
        check_hex("TC6 sha256 (long key)", mac,
            "60e431591ee0b67f0d8a26aacbf5b77f"
            "8e0bc6213728c5140546040f0ee37f54", 32);
    }

    /* Test Case 7: key > block_size + longer data */
    {
        uint8_t key[131]; memset(key, 0xaa, 131);
        const uint8_t *data = (const uint8_t *)
            "This is a test using a larger than block-size key and a "
            "larger than block-size data. The key needs to be hashed "
            "before being used by the HMAC algorithm.";
        neverc_hmac_sha256(key, 131, data, 152, mac);
        check_hex("TC7 sha256 (long key+data)", mac,
            "9b09ffa71b942fcb27635fbcd5b0e944"
            "bfdc63644f0713938a7f51535c3a35e2", 32);
    }
}

/*
 * RFC 2202 — HMAC-MD5 test vectors.
 */
static void test_hmac_md5_rfc2202(void) {
    printf("[hmac-md5 RFC 2202 vectors]\n");

    uint8_t mac[16];

    /* TC1 */
    {
        uint8_t key[16]; memset(key, 0x0b, 16);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_md5(key, 16, data, 8, mac);
        check_hex("TC1 md5", mac, "9294727a3638bb1c13f48ef8158bfc9d", 16);
    }

    /* TC2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_md5(key, 4, data, 28, mac);
        check_hex("TC2 md5", mac, "750c783e6ab0b503eaa86e310a5db738", 16);
    }

    /* TC3 */
    {
        uint8_t key[16]; memset(key, 0xaa, 16);
        uint8_t data[50]; memset(data, 0xdd, 50);
        neverc_hmac_md5(key, 16, data, 50, mac);
        check_hex("TC3 md5", mac, "56be34521d144c88dbb8c733f0e8b3f6", 16);
    }
}

/*
 * RFC 2202 — HMAC-SHA-1 test vectors.
 */
static void test_hmac_sha1_rfc2202(void) {
    printf("[hmac-sha1 RFC 2202 vectors]\n");

    uint8_t mac[20];

    /* TC1 */
    {
        uint8_t key[20]; memset(key, 0x0b, 20);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha1(key, 20, data, 8, mac);
        check_hex("TC1 sha1", mac, "b617318655057264e28bc0b6fb378c8ef146be00", 20);
    }

    /* TC2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_sha1(key, 4, data, 28, mac);
        check_hex("TC2 sha1", mac, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", 20);
    }

    /* TC3 */
    {
        uint8_t key[20]; memset(key, 0xaa, 20);
        uint8_t data[50]; memset(data, 0xdd, 50);
        neverc_hmac_sha1(key, 20, data, 50, mac);
        check_hex("TC3 sha1", mac, "125d7342b9ac11cd91a39af48aa17b4f63f175d3", 20);
    }
}

/*
 * HMAC-SHA-512 test from RFC 4231
 */
static void test_hmac_sha512_rfc4231(void) {
    printf("[hmac-sha512 RFC 4231 vectors]\n");

    uint8_t mac[64];

    /* TC1 */
    {
        uint8_t key[20]; memset(key, 0x0b, 20);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha512(key, 20, data, 8, mac);
        check_hex("TC1 sha512", mac,
            "87aa7cdea5ef619d4ff0b4241a1d6cb0"
            "2379f4e2ce4ec2787ad0b30545e17cde"
            "daa833b7d6b8a702038b274eaea3f4e4"
            "be9d914eeb61f1702e696c203a126854", 64);
    }

    /* TC2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_sha512(key, 4, data, 28, mac);
        check_hex("TC2 sha512", mac,
            "164b7a7bfcf819e2e395fbe73b56e0a3"
            "87bd64222e831fd610270cd7ea250554"
            "9758bf75c05a994a6d034f65f8f0e6fd"
            "caeab1a34d4a6b4b636e070a38bce737", 64);
    }
}

static void test_hmac_equal(void) {
    printf("[hmac_equal]\n");

    uint8_t a[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t b[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t c[] = {0xDE, 0xAD, 0xBE, 0x00};

    check_int("equal macs", neverc_hmac_equal(a, b, 4), 1);
    check_int("different macs", neverc_hmac_equal(a, c, 4), 0);
    check_int("zero-length equal", neverc_hmac_equal(a, c, 0), 1);
    check_int("null zero-length equal", neverc_hmac_equal(NULL, NULL, 0), 1);
}

static void test_empty_and_invalid_inputs(void) {
    printf("[empty and invalid inputs]\n");
    uint8_t byte = 0;
    uint8_t a[64], b[64], zeros[64] = {0};

    neverc_hmac_sha256(NULL, 0, NULL, 0, a);
    neverc_hmac_sha256(&byte, 0, &byte, 0, b);
    check_int("sha256 accepts null empty spans", memcmp(a, b, 32) == 0, 1);

    neverc_hmac_sha512(NULL, 0, NULL, 0, a);
    neverc_hmac_sha512(&byte, 0, &byte, 0, b);
    check_int("sha512 accepts null empty spans", memcmp(a, b, 64) == 0, 1);

    neverc_hmac_sha1(NULL, 0, NULL, 0, a);
    neverc_hmac_sha1(&byte, 0, &byte, 0, b);
    check_int("sha1 accepts null empty spans", memcmp(a, b, 20) == 0, 1);

    neverc_hmac_md5(NULL, 0, NULL, 0, a);
    neverc_hmac_md5(&byte, 0, &byte, 0, b);
    check_int("md5 accepts null empty spans", memcmp(a, b, 16) == 0, 1);

    memset(a, 0xa5, sizeof(a));
    neverc_hmac_sha256(NULL, 1, &byte, 0, a);
    check_int("invalid key span clears output",
              memcmp(a, zeros, 32) == 0, 1);
    memset(a, 0xa5, sizeof(a));
    neverc_hmac_sha512(&byte, 0, NULL, 1, a);
    check_int("invalid data span clears output",
              memcmp(a, zeros, 64) == 0, 1);

    neverc_hmac_sha256(NULL, 0, NULL, 0, NULL);
    check_int("null output is ignored", 1, 1);
}

int main(void) {
    printf("=== NeverC HMAC Library Tests ===\n");
    printf("(RFC 4231 / RFC 2202 official test vectors)\n\n");

    test_hmac_sha256_rfc4231();
    test_hmac_md5_rfc2202();
    test_hmac_sha1_rfc2202();
    test_hmac_sha512_rfc4231();
    test_hmac_equal();
    test_empty_and_invalid_inputs();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
