/*
 * SHA-224 tests — FIPS 180-4 official vectors + incremental + stress.
 */
#include "neverc/std/crypto/sha224.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_digest(const char *name, const uint8_t *got,
                         const uint8_t *expected, int len) {
    tests_run++;
    if (memcmp(got, expected, (size_t)len) == 0) { tests_passed++; return; }
    tests_failed++;
    printf("  FAIL: %s\n    got: ", name);
    for (int i = 0; i < len; i++) printf("%02x", got[i]);
    printf("\n    exp: ");
    for (int i = 0; i < len; i++) printf("%02x", expected[i]);
    printf("\n");
}

static void hex_to_bytes(const char *hex, uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int b;
        sscanf(hex + 2*i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

int main(void) {
    printf("=== NeverC SHA-224 Tests ===\n");
    uint8_t digest[28], expected[28];

    /* FIPS 180-4 Example 1: "abc" */
    {
        const uint8_t *msg = (const uint8_t *)"abc";
        hex_to_bytes("23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7", expected, 28);
        neverc_sha224_sum(msg, 3, digest);
        check_digest("SHA-224(\"abc\")", digest, expected, 28);
    }

    /* FIPS 180-4 Example 2: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" */
    {
        const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        hex_to_bytes("75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525", expected, 28);
        neverc_sha224_sum((const uint8_t *)msg, strlen(msg), digest);
        check_digest("SHA-224(448-bit msg)", digest, expected, 28);
    }

    /* Empty string */
    {
        hex_to_bytes("d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f", expected, 28);
        neverc_sha224_sum((const uint8_t *)"", 0, digest);
        check_digest("SHA-224(\"\")", digest, expected, 28);
    }

    /* Incremental: update byte-by-byte */
    {
        const uint8_t *msg = (const uint8_t *)"abc";
        neverc_sha224_ctx ctx;
        neverc_sha224_init(&ctx);
        for (size_t i = 0; i < 3; i++) {
            neverc_sha224_update(&ctx, msg + i, 1);
            neverc_sha224_update(&ctx, NULL, 0);
        }
        neverc_sha224_final(&ctx, digest);
        hex_to_bytes("23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7", expected, 28);
        check_digest("SHA-224(\"abc\") incremental", digest, expected, 28);
    }

    /* Boundary: 55 bytes (pad fits in one block) */
    {
        uint8_t buf[55];
        memset(buf, 'a', 55);
        hex_to_bytes("fb0bd626a70c28541dfa781bb5cc4d7d7f56622a58f01a0b1ddd646f", expected, 28);
        neverc_sha224_sum(buf, 55, digest);
        check_digest("SHA-224(55 'a')", digest, expected, 28);
    }

    /* Boundary: 56 bytes (pad needs extra block) */
    {
        uint8_t buf[56];
        memset(buf, 'a', 56);
        hex_to_bytes("d40854fc9caf172067136f2e29e1380b14626bf6f0dd06779f820dcd", expected, 28);
        neverc_sha224_sum(buf, 56, digest);
        check_digest("SHA-224(56 'a')", digest, expected, 28);
    }

    /* 1 million 'a' characters — FIPS 180-4 */
    {
        hex_to_bytes("20794655980c91d8bbb4c1ea97618a4bf03f42581948b2ee4ee7ad67", expected, 28);
        neverc_sha224_ctx ctx;
        neverc_sha224_init(&ctx);
        uint8_t buf[1000];
        memset(buf, 'a', 1000);
        for (int i = 0; i < 1000; i++)
            neverc_sha224_update(&ctx, buf, 1000);
        neverc_sha224_final(&ctx, digest);
        check_digest("SHA-224(1M 'a')", digest, expected, 28);
    }

    /* Verify SHA-224 differs from SHA-256 for same input */
    {
        uint8_t sha256_digest[32];
        neverc_sha256_sum((const uint8_t *)"abc", 3, sha256_digest);
        neverc_sha224_sum((const uint8_t *)"abc", 3, digest);
        tests_run++;
        if (memcmp(digest, sha256_digest, 28) != 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: SHA-224 == SHA-256 truncated\n"); }
    }

    /* Re-init after final must start a new hash; update after final is ignored. */
    {
        neverc_sha224_ctx ctx;
        uint8_t d1[28], d2[28], d3[28];
        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_sha224_final(&ctx, d1);
        neverc_sha224_update(&ctx, (const uint8_t *)"x", 1);
        neverc_sha224_final(&ctx, d2);
        check_digest("SHA-224 update after final ignored", d2, d1, 28);

        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, (const uint8_t *)"xyz", 3);
        neverc_sha224_final(&ctx, d3);
        tests_run++;
        if (memcmp(d1, d3, 28) != 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: SHA-224 re-init reused old digest\n"); }
    }

    /* SHA-224 uses SHA-256's 64-bit bit-length; wrap must not collide. */
    {
        neverc_sha224_ctx ctx;
        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, (const uint8_t *)"abc", 3);
        ctx.count = (1ULL << 61) + 3;
        uint8_t overflowed[28];
        memset(overflowed, 0xa5, sizeof(overflowed));
        neverc_sha224_final(&ctx, overflowed);
        uint8_t zeros[28] = {0};
        tests_run++;
        if (memcmp(overflowed, zeros, 28) == 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: overflow must not collide with SHA-224(\"abc\")\n"); }
    }

    {
        neverc_sha224_ctx ctx;
        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_sha224_update(&ctx, NULL, 5);
        neverc_sha224_final(&ctx, digest);
        hex_to_bytes("23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7", expected, 28);
        check_digest("SHA-224 invalid data span ignored", digest, expected, 28);
    }

    {
        neverc_sha224_ctx ctx;
        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, (const uint8_t *)"abc", 3);
        ctx.count = UINT64_MAX / 8 - 2;
        neverc_sha224_update(&ctx, (const uint8_t *)"xxxxx", 5);
        uint8_t overflowed[28];
        memset(overflowed, 0xa5, sizeof(overflowed));
        neverc_sha224_final(&ctx, overflowed);
        uint8_t zeros[28] = {0};
        tests_run++;
        if (memcmp(overflowed, zeros, 28) == 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: update wrap must not collide with SHA-224(\"abc\")\n"); }
    }

    {
        uint8_t overflowed[28];
        memset(overflowed, 0xa5, sizeof(overflowed));
        neverc_sha224_final(NULL, overflowed);
        uint8_t zeros[28] = {0};
        tests_run++;
        if (memcmp(overflowed, zeros, 28) == 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: NULL ctx final must not leave digest untouched\n"); }
    }

    {
        uint8_t overflowed[28], empty[28], zeros[28] = {0};
        memset(overflowed, 0xa5, sizeof(overflowed));
        neverc_sha224_sum(NULL, 5, overflowed);
        neverc_sha224_sum((const uint8_t *)"", 0, empty);
        tests_run++;
        if (memcmp(overflowed, zeros, 28) == 0 && memcmp(overflowed, empty, 28) != 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: sum(NULL, n) must not hash empty\n"); }
    }

    {
        neverc_sha224_ctx ctx;
        uint8_t leftover[10];
        memset(leftover, 0x5a, sizeof(leftover));
        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, leftover, sizeof(leftover));
        neverc_sha224_init(&ctx);
        int dirty = 0;
        for (size_t i = 0; i < sizeof(ctx.buf); i++)
            if (ctx.buf[i] != 0) dirty = 1;
        tests_run++;
        if (!dirty && ctx.count == 0 && ctx.finalized == 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: SHA-224 re-init must wipe buf\n"); }
    }

    {
        neverc_sha224_ctx ctx, clone;
        uint8_t d1[28], d2[28];
        neverc_sha224_init(&ctx);
        neverc_sha224_update(&ctx, (const uint8_t *)"ab", 2);
        clone = ctx;
        neverc_sha224_update(&ctx, (const uint8_t *)"c", 1);
        neverc_sha224_update(&clone, (const uint8_t *)"c", 1);
        neverc_sha224_final(&ctx, d1);
        neverc_sha224_final(&clone, d2);
        hex_to_bytes("23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7", expected, 28);
        check_digest("SHA-224 clone after update", d1, expected, 28);
        tests_run++;
        if (memcmp(d1, d2, 28) == 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: SHA-224 cloned ctx mismatch\n"); }
    }

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
