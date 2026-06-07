#include "neverc/std/encoding/hex.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (strcmp(got, expected) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected);
    }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got %d, expected %d\n", name, got, expected);
    }
}

static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected);
    }
}

static void check_mem(const char *name, const uint8_t *got, const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: memory mismatch\n", name);
    }
}

static void test_encoded_len(void) {
    printf("[encoded_len]\n");
    check_size("encoded_len(0)", neverc_hex_encoded_len(0), 0);
    check_size("encoded_len(1)", neverc_hex_encoded_len(1), 2);
    check_size("encoded_len(5)", neverc_hex_encoded_len(5), 10);
    check_size("encoded_len(16)", neverc_hex_encoded_len(16), 32);
}

static void test_decoded_len(void) {
    printf("[decoded_len]\n");
    check_size("decoded_len(0)", neverc_hex_decoded_len(0), 0);
    check_size("decoded_len(2)", neverc_hex_decoded_len(2), 1);
    check_size("decoded_len(10)", neverc_hex_decoded_len(10), 5);
}

static void test_encode(void) {
    printf("[encode]\n");
    char dst[64];

    uint8_t empty[] = {};
    neverc_hex_encode(dst, empty, 0);
    check_str("encode(empty)", dst, "");

    uint8_t one[] = {0xab};
    neverc_hex_encode(dst, one, 1);
    check_str("encode(0xab)", dst, "ab");

    uint8_t hello[] = "Hello";
    neverc_hex_encode(dst, hello, 5);
    check_str("encode(Hello)", dst, "48656c6c6f");

    uint8_t bytes[] = {0x00, 0x01, 0x02, 0xff, 0xfe};
    neverc_hex_encode(dst, bytes, 5);
    check_str("encode(mixed)", dst, "000102fffe");

    uint8_t all_zeros[] = {0x00, 0x00, 0x00};
    neverc_hex_encode(dst, all_zeros, 3);
    check_str("encode(zeros)", dst, "000000");
}

static void test_decode(void) {
    printf("[decode]\n");
    uint8_t dst[64];

    int n = neverc_hex_decode(dst, "", 0);
    check_int("decode(empty)", n, 0);

    n = neverc_hex_decode(dst, "ab", 2);
    check_int("decode(ab).len", n, 1);
    check_int("decode(ab).val", dst[0], 0xab);

    n = neverc_hex_decode(dst, "48656c6c6f", 10);
    check_int("decode(Hello).len", n, 5);
    check_mem("decode(Hello).val", dst, (uint8_t *)"Hello", 5);

    n = neverc_hex_decode(dst, "AB", 2);
    check_int("decode(AB) uppercase", n, 1);
    check_int("decode(AB).val", dst[0], 0xab);

    n = neverc_hex_decode(dst, "aB", 2);
    check_int("decode(aB) mixed case", n, 1);
    check_int("decode(aB).val", dst[0], 0xab);

    n = neverc_hex_decode(dst, "a", 1);
    check_int("decode(odd len)", n, -1);

    n = neverc_hex_decode(dst, "zz", 2);
    check_int("decode(invalid)", n, -1);

    n = neverc_hex_decode(dst, "0g", 2);
    check_int("decode(0g invalid)", n, -1);
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    uint8_t data[] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x23, 0x45, 0x67};
    char hex_buf[32];
    uint8_t out[16];

    neverc_hex_encode(hex_buf, data, 8);
    check_str("roundtrip.hex", hex_buf, "deadbeef01234567");

    int n = neverc_hex_decode(out, hex_buf, 16);
    check_int("roundtrip.len", n, 8);
    check_mem("roundtrip.data", out, data, 8);
}

int main(void) {
    printf("=== NeverC Hex Library Tests ===\n\n");

    test_encoded_len();
    test_decoded_len();
    test_encode();
    test_decode();
    test_roundtrip();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
