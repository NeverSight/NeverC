/*
 * SHA-1 test suite — vectors from FIPS 180-4.
 */
#include "neverc/sha1.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void hex_encode(const uint8_t *data, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2*i] = hex[data[i] >> 4]; out[2*i+1] = hex[data[i] & 0xf];
    }
    out[2*len] = '\0';
}

static void check(const char *name, const char *input, const char *expected) {
    tests_run++;
    uint8_t digest[20]; char got[41];
    neverc_sha1_sum((const uint8_t *)input, strlen(input), digest);
    hex_encode(digest, 20, got);
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n    got: %s\n    exp: %s\n", name, got, expected); }
}

int main(void) {
    printf("=== NeverC SHA-1 Tests ===\n\n");

    printf("[FIPS 180-4 vectors]\n");
    check("empty", "", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check("abc", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    check("448-bit", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    printf("[common strings]\n");
    check("hello world", "hello world", "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
    check("a", "a", "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");

    printf("[incremental update]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"hello", 5);
        neverc_sha1_update(&ctx, (const uint8_t *)" ", 1);
        neverc_sha1_update(&ctx, (const uint8_t *)"world", 5);
        uint8_t digest[20]; char got[41];
        neverc_sha1_final(&ctx, digest);
        hex_encode(digest, 20, got);
        if (strcmp(got, "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed") == 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: incremental\n    got: %s\n", got); }
    }

    printf("[byte-by-byte]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        const char *msg = "abc";
        for (int i = 0; i < 3; i++)
            neverc_sha1_update(&ctx, (const uint8_t *)&msg[i], 1);
        uint8_t digest[20]; char got[41];
        neverc_sha1_final(&ctx, digest);
        hex_encode(digest, 20, got);
        if (strcmp(got, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: byte-by-byte\n    got: %s\n", got); }
    }

    printf("[1M 'a' characters]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        uint8_t buf[1000]; memset(buf, 'a', 1000);
        for (int i = 0; i < 1000; i++)
            neverc_sha1_update(&ctx, buf, 1000);
        uint8_t digest[20]; char got[41];
        neverc_sha1_final(&ctx, digest);
        hex_encode(digest, 20, got);
        if (strcmp(got, "34aa973cd4c4daa4f61eeb2bdbad27316534016f") == 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: 1M 'a'\n    got: %s\n", got); }
    }

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
