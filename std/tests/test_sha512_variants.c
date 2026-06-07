/*
 * SHA-512/224 and SHA-512/256 tests — FIPS 180-4 official vectors.
 */
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha512_224.h"
#include "neverc/std/crypto/sha512_256.h"
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

static void test_sha512_224(void) {
    printf("[SHA-512/224]\n");
    uint8_t digest[28], expected[28];

    /* FIPS 180-4: SHA-512/224("abc") */
    {
        hex_to_bytes("4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa", expected, 28);
        neverc_sha512_224_sum((const uint8_t *)"abc", 3, digest);
        check_digest("SHA-512/224(\"abc\")", digest, expected, 28);
    }

    /* Go golden vector: SHA-512/224("abcdefghij") */
    {
        hex_to_bytes("f809423cbb25e81a2a64aecee2cd5fdc7d91d5db583901fbf1db3116", expected, 28);
        neverc_sha512_224_sum((const uint8_t *)"abcdefghij", 10, digest);
        check_digest("SHA-512/224(\"abcdefghij\")", digest, expected, 28);
    }

    /* Go golden vector: SHA-512/224("a") */
    {
        hex_to_bytes("d5cdb9ccc769a5121d4175f2bfdd13d6310e0d3d361ea75d82108327", expected, 28);
        neverc_sha512_224_sum((const uint8_t *)"a", 1, digest);
        check_digest("SHA-512/224(\"a\")", digest, expected, 28);
    }

    /* Empty string */
    {
        hex_to_bytes("6ed0dd02806fa89e25de060c19d3ac86cabb87d6a0ddd05c333b84f4", expected, 28);
        neverc_sha512_224_sum((const uint8_t *)"", 0, digest);
        check_digest("SHA-512/224(\"\")", digest, expected, 28);
    }

    /* Incremental */
    {
        neverc_sha512_224_ctx ctx;
        neverc_sha512_224_init(&ctx);
        neverc_sha512_224_update(&ctx, (const uint8_t *)"a", 1);
        neverc_sha512_224_update(&ctx, (const uint8_t *)"bc", 2);
        neverc_sha512_224_final(&ctx, digest);
        hex_to_bytes("4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa", expected, 28);
        check_digest("SHA-512/224(\"abc\") incremental", digest, expected, 28);
    }
}

static void test_sha512_256(void) {
    printf("[SHA-512/256]\n");
    uint8_t digest[32], expected[32];

    /* FIPS 180-4: SHA-512/256("abc") */
    {
        hex_to_bytes("53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23", expected, 32);
        neverc_sha512_256_sum((const uint8_t *)"abc", 3, digest);
        check_digest("SHA-512/256(\"abc\")", digest, expected, 32);
    }

    /* FIPS 180-4: SHA-512/256(896-bit message) */
    {
        const char *msg = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
        hex_to_bytes("3928e184fb8690f840da3988121d31be65cb9d3ef83ee6146feac861e19b563a", expected, 32);
        neverc_sha512_256_sum((const uint8_t *)msg, strlen(msg), digest);
        check_digest("SHA-512/256(896-bit msg)", digest, expected, 32);
    }

    /* Empty string */
    {
        hex_to_bytes("c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a", expected, 32);
        neverc_sha512_256_sum((const uint8_t *)"", 0, digest);
        check_digest("SHA-512/256(\"\")", digest, expected, 32);
    }

    /* Incremental */
    {
        neverc_sha512_256_ctx ctx;
        neverc_sha512_256_init(&ctx);
        neverc_sha512_256_update(&ctx, (const uint8_t *)"a", 1);
        neverc_sha512_256_update(&ctx, (const uint8_t *)"bc", 2);
        neverc_sha512_256_final(&ctx, digest);
        hex_to_bytes("53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23", expected, 32);
        check_digest("SHA-512/256(\"abc\") incremental", digest, expected, 32);
    }

    /* SHA-512/256 must differ from SHA-256 on same input */
    {
        uint8_t sha256_d[32];
        neverc_sha256_sum((const uint8_t *)"abc", 3, sha256_d);
        neverc_sha512_256_sum((const uint8_t *)"abc", 3, digest);
        tests_run++;
        if (memcmp(digest, sha256_d, 32) != 0) { tests_passed++; }
        else { tests_failed++; printf("  FAIL: SHA-512/256 == SHA-256\n"); }
    }
}

int main(void) {
    printf("=== NeverC SHA-512 Variant Tests ===\n");
    test_sha512_224();
    test_sha512_256();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
