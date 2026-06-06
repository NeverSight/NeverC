#include "neverc/sha384.h"
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

static int digest_matches(const uint8_t *got, const char *expected_hex) {
    uint8_t expected[48];
    hex_to_bytes(expected_hex, expected, 48);
    return memcmp(got, expected, 48) == 0;
}

static void test_fips_vectors(void) {
    printf("[SHA-384 FIPS vectors]\n");
    uint8_t digest[48];

    /* FIPS 180-4 B.1: "abc" */
    neverc_sha384_sum((const uint8_t *)"abc", 3, digest);
    check_true("SHA-384(\"abc\")",
        digest_matches(digest,
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
            "8086072ba1e7cc2358baeca134c825a7"));

    /* FIPS 180-4 B.2: two-block message */
    const char *msg2 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                       "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    neverc_sha384_sum((const uint8_t *)msg2, strlen(msg2), digest);
    check_true("SHA-384(two-block)",
        digest_matches(digest,
            "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712"
            "fcc7c71a557e2db966c3e9fa91746039"));
}

static void test_empty(void) {
    printf("[SHA-384 empty]\n");
    uint8_t digest[48];
    neverc_sha384_sum((const uint8_t *)"", 0, digest);
    check_true("SHA-384(\"\")",
        digest_matches(digest,
            "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da"
            "274edebfe76f65fbd51ad2f14898b95b"));
}

static void test_incremental(void) {
    printf("[SHA-384 incremental]\n");
    uint8_t d1[48], d2[48];

    neverc_sha384_sum((const uint8_t *)"abcdef", 6, d1);

    neverc_sha384_ctx ctx;
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha384_update(&ctx, (const uint8_t *)"def", 3);
    neverc_sha384_final(&ctx, d2);

    check_true("incremental == one-shot", memcmp(d1, d2, 48) == 0);

    /* Byte-by-byte */
    neverc_sha384_init(&ctx);
    for (int i = 0; i < 6; i++)
        neverc_sha384_update(&ctx, (const uint8_t *)"abcdef" + i, 1);
    neverc_sha384_final(&ctx, d2);
    check_true("byte-by-byte == one-shot", memcmp(d1, d2, 48) == 0);
}

static void test_1m_a(void) {
    printf("[SHA-384 1M 'a']\n");
    neverc_sha384_ctx ctx;
    neverc_sha384_init(&ctx);
    uint8_t block[1000];
    memset(block, 'a', 1000);
    for (int i = 0; i < 1000; i++)
        neverc_sha384_update(&ctx, block, 1000);
    uint8_t digest[48];
    neverc_sha384_final(&ctx, digest);
    check_true("SHA-384(1M 'a')",
        digest_matches(digest,
            "9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b"
            "07b8b3dc38ecc4ebae97ddd87f3d8985"));
}

int main(void) {
    printf("=== NeverC SHA-384 Tests ===\n\n");
    test_fips_vectors();
    test_empty();
    test_incremental();
    test_1m_a();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
