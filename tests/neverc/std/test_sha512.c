/*
 * SHA-512 test suite — vectors from FIPS 180-4.
 */
#include "neverc/std/crypto/sha512.h"
#include "neverc/std/crypto/sha512_224.h"
#include "neverc/std/crypto/sha512_256.h"
#include "neverc/std/crypto/sha256.h"
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
    uint8_t digest[64]; char got[129];
    neverc_sha512_sum((const uint8_t *)input, strlen(input), digest);
    hex_encode(digest, 64, got);
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n    got: %s\n    exp: %s\n", name, got, expected); }
}

int main(void) {
    printf("=== NeverC SHA-512 Tests ===\n\n");

    printf("[FIPS 180-4 vectors]\n");
    check("empty", "",
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    check("abc", "abc",
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    check("896-bit",
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
        "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
        "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    printf("[common strings]\n");
    check("hello world", "hello world",
        "309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f"
        "989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f");

    printf("[incremental update]\n");
    {
        tests_run++;
        neverc_sha512_ctx ctx;
        neverc_sha512_init(&ctx);
        neverc_sha512_update(&ctx, (const uint8_t *)"hello", 5);
        neverc_sha512_update(&ctx, NULL, 0);
        neverc_sha512_update(&ctx, (const uint8_t *)" ", 1);
        neverc_sha512_update(&ctx, (const uint8_t *)"world", 5);
        uint8_t digest[64]; char got[129];
        neverc_sha512_final(&ctx, digest);
        hex_encode(digest, 64, got);
        if (strcmp(got,
            "309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f"
            "989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: incremental\n    got: %s\n", got); }
    }

    printf("[byte-by-byte]\n");
    {
        tests_run++;
        neverc_sha512_ctx ctx;
        neverc_sha512_init(&ctx);
        const char *msg = "abc";
        for (int i = 0; i < 3; i++)
            neverc_sha512_update(&ctx, (const uint8_t *)&msg[i], 1);
        uint8_t digest[64]; char got[129];
        neverc_sha512_final(&ctx, digest);
        hex_encode(digest, 64, got);
        if (strcmp(got,
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: byte-by-byte\n    got: %s\n", got); }
    }

    printf("[1M 'a' characters]\n");
    {
        tests_run++;
        neverc_sha512_ctx ctx;
        neverc_sha512_init(&ctx);
        uint8_t buf[1000]; memset(buf, 'a', 1000);
        for (int i = 0; i < 1000; i++)
            neverc_sha512_update(&ctx, buf, 1000);
        uint8_t digest[64]; char got[129];
        neverc_sha512_final(&ctx, digest);
        hex_encode(digest, 64, got);
        if (strcmp(got,
            "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
            "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: 1M 'a'\n    got: %s\n", got); }
    }

    printf("[SHA-512/224 and SHA-512/256 digest sizes]\n");
    {
        uint8_t d224[28], d256[32], d512[64], sha256[32];
        neverc_sha512_224_sum((const uint8_t *)"abc", 3, d224);
        neverc_sha512_256_sum((const uint8_t *)"abc", 3, d256);
        neverc_sha512_sum((const uint8_t *)"abc", 3, d512);
        neverc_sha256_sum((const uint8_t *)"abc", 3, sha256);

        tests_run++;
        char got[57];
        hex_encode(d224, 28, got);
        if (strcmp(got, "4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: SHA-512/224(\"abc\")\n    got: %s\n", got); }

        tests_run++;
        char got256[65];
        hex_encode(d256, 32, got256);
        if (strcmp(got256, "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: SHA-512/256(\"abc\")\n    got: %s\n", got256); }

        tests_run++;
        if (memcmp(d224, d512, 28) != 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: SHA-512/224 == truncated SHA-512\n"); }

        tests_run++;
        if (memcmp(d256, sha256, 32) != 0) tests_passed++;
        else { tests_failed++; printf("  FAIL: SHA-512/256 == SHA-256\n"); }

        tests_run++;
        if (NEVERC_SHA512_224_DIGEST_SIZE == 28 &&
            NEVERC_SHA512_256_DIGEST_SIZE == 32 &&
            NEVERC_SHA512_DIGEST_SIZE == 64)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: digest size constants\n"); }
    }

    printf("[padding boundaries 111/112/127/128]\n");
    {
        uint8_t buf[128];
        memset(buf, 'a', sizeof(buf));
        struct { size_t n; const char *hex; } cases[] = {
            {111,
             "fa9121c7b32b9e01733d034cfc78cbf67f926c7ed83e82200ef8681819692176"
             "0b4beff48404df811b953828274461673c68d04e297b0eb7b2b4d60fc6b566a2"},
            {112,
             "c01d080efd492776a1c43bd23dd99d0a2e626d481e16782e75d54c2503b5dc32"
             "bd05f0f1ba33e568b88fd2d970929b719ecbb152f58f130a407c8830604b70ca"},
            {127,
             "828613968b501dc00a97e08c73b118aa8876c26b8aac93df128502ab360f91ba"
             "b50a51e088769a5c1eff4782ace147dce3642554199876374291f5d921629502"},
            {128,
             "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a24"
             "3667807ed300314b95cacdd579f3e33abdfbe351909519a846d465c59582f321"},
        };
        for (int i = 0; i < 4; i++) {
            tests_run++;
            uint8_t digest[64];
            char got[129];
            neverc_sha512_sum(buf, cases[i].n, digest);
            hex_encode(digest, 64, got);
            if (strcmp(got, cases[i].hex) == 0)
                tests_passed++;
            else {
                tests_failed++;
                printf("  FAIL: %zu 'a'\n    got: %s\n", cases[i].n, got);
            }
        }
    }

    printf("[128-bit length field at 2^61 bytes]\n");
    {
        neverc_sha512_ctx ctx;
        neverc_sha512_init(&ctx);
        neverc_sha512_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_sha512_ctx long_ctx = ctx;
        uint8_t short_d[64], long_d[64];
        neverc_sha512_final(&ctx, short_d);
        long_ctx.count = (1ULL << 61) + 3;
        neverc_sha512_final(&long_ctx, long_d);

        tests_run++;
        char got[129];
        hex_encode(short_d, 64, got);
        if (strcmp(got,
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f") == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: short SHA-512(\"abc\")\n    got: %s\n", got); }

        tests_run++;
        hex_encode(long_d, 64, got);
        if (strcmp(got,
            "cc17042d5409a6e80bada6f9be28f5d624d29d776ac36999563d474c462d6ea0"
            "bc92a0876385d4d839d99521ae2ba79f758d509b105af7881ec4d63434713b38") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: SHA-512 length at 2^61+3 bytes\n    got: %s\n", got);
        }

        tests_run++;
        if (memcmp(short_d, long_d, 64) != 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: wrapped length collided with SHA-512(\"abc\")\n");
        }
    }

    printf("[byte-count wrap fails closed]\n");
    {
        tests_run++;
        neverc_sha512_ctx ctx;
        neverc_sha512_init(&ctx);
        neverc_sha512_update(&ctx, (const uint8_t *)"abc", 3);
        ctx.count = UINT64_MAX - 2;
        neverc_sha512_update(&ctx, (const uint8_t *)"xxxxx", 5);
        uint8_t digest[64];
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha512_final(&ctx, digest);
        uint8_t zeros[64] = {0};
        if (memcmp(digest, zeros, 64) == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: wrapped byte count must not collide with a short message\n"); }
    }

    printf("[update after final ignored]\n");
    {
        tests_run++;
        neverc_sha512_ctx ctx;
        neverc_sha512_init(&ctx);
        neverc_sha512_update(&ctx, (const uint8_t *)"abc", 3);
        uint8_t d1[64], d2[64];
        neverc_sha512_final(&ctx, d1);
        neverc_sha512_update(&ctx, (const uint8_t *)"x", 1);
        neverc_sha512_final(&ctx, d2);
        if (memcmp(d1, d2, 64) == 0)
            tests_passed++;
        else { tests_failed++; printf("  FAIL: update after final must not length-extend\n"); }
    }

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
