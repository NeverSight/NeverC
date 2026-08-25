#include "neverc/std/crypto/sha384.h"
#include "neverc/std/crypto/sha512.h"
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

    {
        uint8_t sha512[64];
        neverc_sha512_sum((const uint8_t *)"abc", 3, sha512);
        check_true("SHA-384 is 48 bytes and not truncated SHA-512",
                   memcmp(digest, sha512, 48) != 0);
        check_true("SHA-384 digest size constant",
                   NEVERC_SHA384_DIGEST_SIZE == 48 &&
                   NEVERC_SHA512_DIGEST_SIZE == 64);
    }

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
    neverc_sha384_update(&ctx, NULL, 0);
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

static void test_final_lifecycle(void) {
    printf("[SHA-384 final lifecycle]\n");
    neverc_sha384_ctx ctx;
    uint8_t d1[48], d2[48];

    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha384_final(&ctx, d1);
    neverc_sha384_update(&ctx, (const uint8_t *)"x", 1);
    neverc_sha384_final(&ctx, d2);
    check_true("update after final ignored", memcmp(d1, d2, 48) == 0);
}

static void test_128bit_length(void) {
    printf("[SHA-384 128-bit length at 2^61 bytes]\n");
    neverc_sha384_ctx ctx;
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha384_ctx long_ctx = ctx;
    uint8_t short_d[48], long_d[48], zeros[48] = {0};
    neverc_sha384_final(&ctx, short_d);
    long_ctx.count = (1ULL << 61) + 3;
    neverc_sha384_final(&long_ctx, long_d);
    check_true("short SHA-384(\"abc\")",
        digest_matches(short_d,
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
            "8086072ba1e7cc2358baeca134c825a7"));
    check_true("2^61+3 length must not collide with SHA-384(\"abc\")",
               memcmp(short_d, long_d, 48) != 0);
    check_true("2^61+3 is a valid 128-bit length, not fail-closed",
               memcmp(long_d, zeros, 48) != 0);
}

static void test_byte_count_wrap(void) {
    printf("[SHA-384 byte-count carry into high word]\n");
    neverc_sha384_ctx ctx;
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"abc", 3);
    ctx.count = UINT64_MAX - 2;
    neverc_sha384_update(&ctx, (const uint8_t *)"xxxxx", 5);
    check_true("wrapped byte count carries into high word",
               ctx.count == 2 && ctx.count_hi == 1 && !ctx.finalized);
    uint8_t digest[48];
    memset(digest, 0xa5, sizeof(digest));
    neverc_sha384_final(&ctx, digest);
    uint8_t zeros[48] = {0};
    check_true("carried byte count remains hashable",
               memcmp(digest, zeros, 48) != 0);
}

static void test_invalid_span(void) {
    printf("[SHA-384 invalid data span ignored]\n");
    neverc_sha384_ctx ctx;
    uint8_t d1[48], d2[48];
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha384_final(&ctx, d1);
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha384_update(&ctx, NULL, 5);
    neverc_sha384_final(&ctx, d2);
    check_true("invalid span ignored", memcmp(d1, d2, 48) == 0);
}

static void test_null_ctx_and_sum(void) {
    printf("[SHA-384 NULL ctx / invalid sum]\n");
    uint8_t digest[48], empty[48], zeros[48] = {0};
    memset(digest, 0xa5, sizeof(digest));
    neverc_sha384_final(NULL, digest);
    check_true("NULL ctx final fails closed", memcmp(digest, zeros, 48) == 0);

    memset(digest, 0xa5, sizeof(digest));
    neverc_sha384_sum(NULL, 5, digest);
    neverc_sha384_sum((const uint8_t *)"", 0, empty);
    check_true("sum(NULL, n) fails closed", memcmp(digest, zeros, 48) == 0);
    check_true("sum(NULL, n) != empty hash", memcmp(digest, empty, 48) != 0);
}

static void test_reset_and_clone(void) {
    printf("[SHA-384 reset / clone]\n");
    neverc_sha384_ctx ctx;
    uint8_t leftover[10];
    memset(leftover, 0x5a, sizeof(leftover));
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, leftover, sizeof(leftover));
    neverc_sha384_init(&ctx);
    int dirty = 0;
    for (size_t i = 0; i < sizeof(ctx.buf); i++)
        if (ctx.buf[i] != 0) dirty = 1;
    check_true("re-init wipes buf",
               !dirty && ctx.count == 0 && ctx.count_hi == 0 &&
               ctx.finalized == 0);

    neverc_sha384_ctx clone;
    uint8_t d1[48], d2[48], expected[48];
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, (const uint8_t *)"ab", 2);
    clone = ctx;
    neverc_sha384_update(&ctx, (const uint8_t *)"c", 1);
    neverc_sha384_update(&clone, (const uint8_t *)"c", 1);
    neverc_sha384_final(&ctx, d1);
    neverc_sha384_final(&clone, d2);
    neverc_sha384_sum((const uint8_t *)"abc", 3, expected);
    check_true("clone after update",
               memcmp(d1, d2, 48) == 0 && memcmp(d1, expected, 48) == 0);
}

int main(void) {
    printf("=== NeverC SHA-384 Tests ===\n\n");
    test_fips_vectors();
    test_empty();
    test_incremental();
    test_1m_a();
    test_final_lifecycle();
    test_128bit_length();
    test_byte_count_wrap();
    test_invalid_span();
    test_null_ctx_and_sum();
    test_reset_and_clone();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
