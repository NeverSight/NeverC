/*
 * SHA-256 test suite — vectors from FIPS 180-4 and Go crypto/sha256.
 */
#include "neverc/std/crypto/sha256.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void hex_encode(const uint8_t *data, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2*i]   = hex[data[i] >> 4];
        out[2*i+1] = hex[data[i] & 0xf];
    }
    out[2*len] = '\0';
}

static void check_sha256(const char *name, const char *input, const char *expected_hex) {
    tests_run++;
    uint8_t digest[32];
    neverc_sha256_sum((const uint8_t *)input, strlen(input), digest);
    char got_hex[65];
    hex_encode(digest, 32, got_hex);
    if (strcmp(got_hex, expected_hex) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s\n    got:      %s\n    expected: %s\n", name, got_hex, expected_hex);
    }
}

static void check_sha256_bytes(const char *name, const uint8_t *data, size_t len,
                                const char *expected_hex) {
    tests_run++;
    uint8_t digest[32];
    neverc_sha256_sum(data, len, digest);
    char got_hex[65];
    hex_encode(digest, 32, got_hex);
    if (strcmp(got_hex, expected_hex) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s\n    got:      %s\n    expected: %s\n", name, got_hex, expected_hex);
    }
}

int main(void) {
    printf("=== NeverC SHA-256 Tests ===\n\n");

    printf("[FIPS 180-4 test vectors]\n");
    check_sha256("empty string", "",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_sha256("abc", "abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_sha256("448-bit message",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    printf("[Go crypto/sha256 test vectors]\n");
    check_sha256("a", "a",
        "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
    check_sha256("ab", "ab",
        "fb8e20fc2e4c3f248c60c39bd652f3c1347298bb977b8b4d5903b85055620603");
    check_sha256("hello", "hello",
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    check_sha256("hello world", "hello world",
        "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");

    printf("[long messages]\n");
    check_sha256("50 bytes",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomno",
        "d395c7fc3597afec7c0a7618b078daac01fe62081d815d7bae0654197508b80c");

    printf("[incremental update]\n");
    {
        tests_run++;
        neverc_sha256_ctx ctx;
        neverc_sha256_init(&ctx);
        neverc_sha256_update(&ctx, (const uint8_t *)"hello", 5);
        neverc_sha256_update(&ctx, (const uint8_t *)" ", 1);
        neverc_sha256_update(&ctx, (const uint8_t *)"world", 5);
        uint8_t digest[32];
        neverc_sha256_final(&ctx, digest);
        char got_hex[65];
        hex_encode(digest, 32, got_hex);
        if (strcmp(got_hex, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: incremental 'hello world'\n    got: %s\n", got_hex);
        }
    }

    printf("[one million 'a' characters]\n");
    {
        tests_run++;
        neverc_sha256_ctx ctx;
        neverc_sha256_init(&ctx);
        uint8_t buf[1000];
        memset(buf, 'a', 1000);
        for (int i = 0; i < 1000; i++)
            neverc_sha256_update(&ctx, buf, 1000);
        uint8_t digest[32];
        neverc_sha256_final(&ctx, digest);
        char got_hex[65];
        hex_encode(digest, 32, got_hex);
        if (strcmp(got_hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: 1M 'a'\n    got: %s\n", got_hex);
        }
    }

    printf("[byte-by-byte update]\n");
    {
        tests_run++;
        neverc_sha256_ctx ctx;
        neverc_sha256_init(&ctx);
        const char *msg = "abc";
        for (int i = 0; i < 3; i++)
            neverc_sha256_update(&ctx, (const uint8_t *)&msg[i], 1);
        uint8_t digest[32];
        neverc_sha256_final(&ctx, digest);
        char got_hex[65];
        hex_encode(digest, 32, got_hex);
        if (strcmp(got_hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: byte-by-byte 'abc'\n    got: %s\n", got_hex);
        }
    }

    printf("[empty data]\n");
    check_sha256_bytes("zero-length", (const uint8_t *)"", 0,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    printf("[boundary: exactly 55 bytes (fits in one block with padding)]\n");
    check_sha256("55 bytes",
        "1234567890123456789012345678901234567890123456789012345",
        "03c3a70e99ed5eeccd80f73771fcf1ece643d939d9ecc76f25544b0233f708e9");

    printf("[boundary: exactly 56 bytes (needs extra block)]\n");
    check_sha256("56 bytes (boundary)",
        "12345678901234567890123456789012345678901234567890123456",
        "0be66ce72c2467e793202906000672306661791622e0ca9adf4a8955b2ed189c");

    printf("[boundary: exactly 64 bytes (one full block)]\n");
    check_sha256("64 bytes",
        "1234567890123456789012345678901234567890123456789012345678901234",
        "676491965ed3ec50cb7a63ee96315480a95c54426b0b72bca8a0d4ad1285ad55");

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
