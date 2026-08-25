/*
 * SHA-1 test suite — vectors from FIPS 180-4.
 */
#include "neverc/std/crypto/sha1.h"
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
    check("896-bit",
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
        "a49b2446a02c645bf419f995b67091253a04a259");

    printf("[padding boundaries 55/63/64]\n");
    check("55 bytes",
        "1234567890123456789012345678901234567890123456789012345",
        "827a683fdfdbef225a2421078b7789b134c7eafa");
    check("63 bytes",
        "123456789012345678901234567890123456789012345678901234567890123",
        "98b4b1764ea88d6c3fa63b70799dbd0c03372d1a");
    check("64 bytes",
        "1234567890123456789012345678901234567890123456789012345678901234",
        "c71490fc24aa3d19e11282da77032dd9cdb33103");

    printf("[common strings]\n");
    check("hello world", "hello world", "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
    check("a", "a", "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");

    printf("[incremental update]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"hello", 5);
        neverc_sha1_update(&ctx, NULL, 0);
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

    printf("[update after final ignored]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"abc", 3);
        uint8_t d1[20], d2[20];
        neverc_sha1_final(&ctx, d1);
        neverc_sha1_update(&ctx, (const uint8_t *)"x", 1);
        neverc_sha1_final(&ctx, d2);
        char got[41];
        hex_encode(d1, 20, got);
        if (memcmp(d1, d2, 20) == 0 &&
            strcmp(got, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: update after final must not length-extend\n"); }
    }

    printf("[invalid data span ignored]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_sha1_update(&ctx, NULL, 5);
        uint8_t digest[20]; char got[41];
        neverc_sha1_final(&ctx, digest);
        hex_encode(digest, 20, got);
        if (strcmp(got, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: invalid span ignored\n    got: %s\n", got); }
    }

    printf("[counter overflow fails closed]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"abc", 3);
        ctx.count = (1ULL << 61) + 3;
        uint8_t digest[20];
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha1_final(&ctx, digest);
        uint8_t zeros[20] = {0};
        if (memcmp(digest, zeros, 20) == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: overflow must not collide with SHA-1(\"abc\")\n"); }
    }

    printf("[update-path 64-bit wrap fails closed]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"abc", 3);
        ctx.count = UINT64_MAX / 8 - 2;
        neverc_sha1_update(&ctx, (const uint8_t *)"xxxxx", 5);
        uint8_t digest[20];
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha1_final(&ctx, digest);
        uint8_t zeros[20] = {0};
        if (memcmp(digest, zeros, 20) == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: update wrap must not collide with SHA-1(\"abc\")\n"); }
    }

    printf("[NULL ctx final fails closed]\n");
    {
        tests_run++;
        uint8_t digest[20];
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha1_final(NULL, digest);
        uint8_t zeros[20] = {0};
        if (memcmp(digest, zeros, 20) == 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: NULL ctx final must not leave digest untouched\n"); }
    }

    printf("[one-shot invalid data span fails closed]\n");
    {
        tests_run++;
        uint8_t digest[20], empty[20], zeros[20] = {0};
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha1_sum(NULL, 5, digest);
        neverc_sha1_sum((const uint8_t *)"", 0, empty);
        if (memcmp(digest, zeros, 20) == 0 && memcmp(digest, empty, 20) != 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: sum(NULL, n) must not hash empty\n"); }
    }

    printf("[reset wipes leftover buf]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx;
        uint8_t leftover[10];
        memset(leftover, 0x5a, sizeof(leftover));
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, leftover, sizeof(leftover));
        neverc_sha1_init(&ctx);
        int dirty = 0;
        for (size_t i = 0; i < sizeof(ctx.buf); i++)
            if (ctx.buf[i] != 0) dirty = 1;
        if (!dirty && ctx.count == 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: re-init must wipe buf\n"); }
    }

    printf("[clone after update]\n");
    {
        tests_run++;
        neverc_sha1_ctx ctx, clone;
        uint8_t d1[20], d2[20], expected[20];
        neverc_sha1_init(&ctx);
        neverc_sha1_update(&ctx, (const uint8_t *)"ab", 2);
        clone = ctx;
        neverc_sha1_update(&ctx, (const uint8_t *)"c", 1);
        neverc_sha1_update(&clone, (const uint8_t *)"c", 1);
        neverc_sha1_final(&ctx, d1);
        neverc_sha1_final(&clone, d2);
        neverc_sha1_sum((const uint8_t *)"abc", 3, expected);
        if (memcmp(d1, d2, 20) == 0 && memcmp(d1, expected, 20) == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: cloned ctx must hash independently\n"); }
    }

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
