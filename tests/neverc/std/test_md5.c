/*
 * MD5 test suite — vectors from RFC 1321 and Go crypto/md5.
 */
#include "neverc/std/crypto/md5.h"
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

static void check_md5(const char *name, const char *input, const char *expected_hex) {
    tests_run++;
    uint8_t digest[16];
    neverc_md5_sum((const uint8_t *)input, strlen(input), digest);
    char got_hex[33];
    hex_encode(digest, 16, got_hex);
    if (strcmp(got_hex, expected_hex) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s\n    got:      %s\n    expected: %s\n", name, got_hex, expected_hex);
    }
}

int main(void) {
    printf("=== NeverC MD5 Tests ===\n\n");

    printf("[RFC 1321 test vectors]\n");
    check_md5("empty", "", "d41d8cd98f00b204e9800998ecf8427e");
    check_md5("a", "a", "0cc175b9c0f1b6a831c399e269772661");
    check_md5("abc", "abc", "900150983cd24fb0d6963f7d28e17f72");
    check_md5("message digest", "message digest", "f96b697d7cb7938d525a2f31aaf161d0");
    check_md5("a-z", "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
    check_md5("mixed case + digits",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        "d174ab98d277d9f5a5611c2c9f419d9f");
    check_md5("numeric string",
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
        "57edf4a22be3c955ac49da2e2107b67a");

    printf("[Go crypto/md5 test vectors]\n");
    check_md5("hello", "hello", "5d41402abc4b2a76b9719d911017c592");
    check_md5("hello world", "hello world", "5eb63bbbe01eeed093cb22bb8f5acdc3");

    printf("[incremental update]\n");
    {
        tests_run++;
        neverc_md5_ctx ctx;
        neverc_md5_init(&ctx);
        neverc_md5_update(&ctx, (const uint8_t *)"hello", 5);
        neverc_md5_update(&ctx, NULL, 0);
        neverc_md5_update(&ctx, (const uint8_t *)" ", 1);
        neverc_md5_update(&ctx, (const uint8_t *)"world", 5);
        uint8_t digest[16];
        neverc_md5_final(&ctx, digest);
        char got_hex[33];
        hex_encode(digest, 16, got_hex);
        if (strcmp(got_hex, "5eb63bbbe01eeed093cb22bb8f5acdc3") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: incremental 'hello world'\n    got: %s\n", got_hex);
        }
    }

    printf("[byte-by-byte update]\n");
    {
        tests_run++;
        neverc_md5_ctx ctx;
        neverc_md5_init(&ctx);
        const char *msg = "abc";
        for (int i = 0; i < 3; i++)
            neverc_md5_update(&ctx, (const uint8_t *)&msg[i], 1);
        uint8_t digest[16];
        neverc_md5_final(&ctx, digest);
        char got_hex[33];
        hex_encode(digest, 16, got_hex);
        if (strcmp(got_hex, "900150983cd24fb0d6963f7d28e17f72") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: byte-by-byte 'abc'\n    got: %s\n", got_hex);
        }
    }

    printf("[one million 'a' characters]\n");
    {
        tests_run++;
        neverc_md5_ctx ctx;
        neverc_md5_init(&ctx);
        uint8_t buf[1000];
        memset(buf, 'a', 1000);
        for (int i = 0; i < 1000; i++)
            neverc_md5_update(&ctx, buf, 1000);
        uint8_t digest[16];
        neverc_md5_final(&ctx, digest);
        char got_hex[33];
        hex_encode(digest, 16, got_hex);
        if (strcmp(got_hex, "7707d6ae4e027c70eea2a935c2296f21") == 0)
            tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: 1M 'a'\n    got: %s\n", got_hex);
        }
    }

    printf("[boundary: exactly 55 bytes]\n");
    check_md5("55 bytes", "1234567890123456789012345678901234567890123456789012345",
        "c9ccf168914a1bcfc3229f1948e67da0");

    printf("[boundary: exactly 56 bytes]\n");
    check_md5("56 bytes", "12345678901234567890123456789012345678901234567890123456",
        "49f193adce178490e34d1b3a4ec0064c");

    printf("[boundary: exactly 64 bytes]\n");
    check_md5("64 bytes", "1234567890123456789012345678901234567890123456789012345678901234",
        "eb6c4179c0a7c82cc2828c1e6338e165");

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
