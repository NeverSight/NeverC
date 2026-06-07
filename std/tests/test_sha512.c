/*
 * SHA-512 test suite — vectors from FIPS 180-4.
 */
#include "neverc/crypto/sha512.h"
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

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
