#include "neverc/base64.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (strcmp(got, expected) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}

static void check_mem(const char *name, const uint8_t *got, const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: memory mismatch\n", name); }
}

static void test_lengths(void) {
    printf("[lengths]\n");
    check_size("encoded_len(0)", neverc_base64_encoded_len(0), 0);
    check_size("encoded_len(1)", neverc_base64_encoded_len(1), 4);
    check_size("encoded_len(2)", neverc_base64_encoded_len(2), 4);
    check_size("encoded_len(3)", neverc_base64_encoded_len(3), 4);
    check_size("encoded_len(4)", neverc_base64_encoded_len(4), 8);
    check_size("decoded_len(0)", neverc_base64_decoded_len(0), 0);
    check_size("decoded_len(4)", neverc_base64_decoded_len(4), 3);
    check_size("decoded_len(8)", neverc_base64_decoded_len(8), 6);
}

/* RFC 4648 test vectors */
static void test_encode(void) {
    printf("[encode]\n");
    char dst[64];

    neverc_base64_encode(dst, (const uint8_t *)"", 0);
    check_str("encode(empty)", dst, "");

    neverc_base64_encode(dst, (const uint8_t *)"f", 1);
    check_str("encode(f)", dst, "Zg==");

    neverc_base64_encode(dst, (const uint8_t *)"fo", 2);
    check_str("encode(fo)", dst, "Zm8=");

    neverc_base64_encode(dst, (const uint8_t *)"foo", 3);
    check_str("encode(foo)", dst, "Zm9v");

    neverc_base64_encode(dst, (const uint8_t *)"foob", 4);
    check_str("encode(foob)", dst, "Zm9vYg==");

    neverc_base64_encode(dst, (const uint8_t *)"fooba", 5);
    check_str("encode(fooba)", dst, "Zm9vYmE=");

    neverc_base64_encode(dst, (const uint8_t *)"foobar", 6);
    check_str("encode(foobar)", dst, "Zm9vYmFy");
}

static void test_decode(void) {
    printf("[decode]\n");
    uint8_t dst[64];

    int n = neverc_base64_decode(dst, "", 0);
    check_int("decode(empty)", n, 0);

    n = neverc_base64_decode(dst, "Zg==", 4);
    check_int("decode(Zg==).len", n, 1);
    check_mem("decode(Zg==).val", dst, (const uint8_t *)"f", 1);

    n = neverc_base64_decode(dst, "Zm8=", 4);
    check_int("decode(Zm8=).len", n, 2);
    check_mem("decode(Zm8=).val", dst, (const uint8_t *)"fo", 2);

    n = neverc_base64_decode(dst, "Zm9v", 4);
    check_int("decode(Zm9v).len", n, 3);
    check_mem("decode(Zm9v).val", dst, (const uint8_t *)"foo", 3);

    n = neverc_base64_decode(dst, "Zm9vYmFy", 8);
    check_int("decode(Zm9vYmFy).len", n, 6);
    check_mem("decode(Zm9vYmFy).val", dst, (const uint8_t *)"foobar", 6);

    n = neverc_base64_decode(dst, "!!!", 3);
    check_int("decode(invalid)", n, -1);
}

static void test_url_encode(void) {
    printf("[url_encode]\n");
    char dst[64];

    uint8_t data[] = {0xfb, 0xff, 0xfe};
    neverc_base64_url_encode(dst, data, 3);
    check_str("url_encode(fbfffe)", dst, "-__-");

    neverc_base64_encode(dst, data, 3);
    check_str("std_encode(fbfffe)", dst, "+//+");
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    const char *original = "Hello, World! This is a base64 roundtrip test.";
    size_t orig_len = strlen(original);

    char encoded[128];
    uint8_t decoded[128];

    size_t enc_len = neverc_base64_encode(encoded, (const uint8_t *)original, orig_len);
    int dec_len = neverc_base64_decode(decoded, encoded, enc_len);

    check_int("roundtrip.len", dec_len, (int)orig_len);
    check_mem("roundtrip.data", decoded, (const uint8_t *)original, orig_len);
}

int main(void) {
    printf("=== NeverC Base64 Library Tests ===\n\n");

    test_lengths();
    test_encode();
    test_decode();
    test_url_encode();
    test_roundtrip();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
