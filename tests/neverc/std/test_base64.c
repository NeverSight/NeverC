#include "neverc/std/encoding/base64.h"
#include <limits.h>
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
    check_size("encoded_len overflow",
               neverc_base64_encoded_len(SIZE_MAX), SIZE_MAX);
    check_size("decoded_len(0)", neverc_base64_decoded_len(0), 0);
    check_size("decoded_len(2)", neverc_base64_decoded_len(2), 1);
    check_size("decoded_len(3)", neverc_base64_decoded_len(3), 2);
    check_size("decoded_len(4)", neverc_base64_decoded_len(4), 3);
    check_size("decoded_len(6)", neverc_base64_decoded_len(6), 4);
    check_size("decoded_len(7)", neverc_base64_decoded_len(7), 5);
    check_size("decoded_len(8)", neverc_base64_decoded_len(8), 6);
}

/* RFC 4648 test vectors */
static void test_encode(void) {
    printf("[encode]\n");
    char dst[64];
    size_t n;

    uint8_t byte = 0;
    check_size("encode rejects overflowing length",
               neverc_base64_encode(dst, &byte, SIZE_MAX), SIZE_MAX);
    check_size("encode rejects NULL source",
               neverc_base64_encode(dst, NULL, 1), SIZE_MAX);
    check_size("encode rejects NULL destination",
               neverc_base64_encode(NULL, &byte, 1), SIZE_MAX);

    char exact[5] = {'?', '?', '?', '?', 'X'};
    n = neverc_base64_encode(
        exact, (const uint8_t *)"f", 1);
    check_size("encode exact length", n, 4);
    check_mem("encode exact payload", (const uint8_t *)exact,
              (const uint8_t *)"Zg==", 4);
    check_int("encode does not append NUL", exact[4], 'X');

    n = neverc_base64_encode(dst, (const uint8_t *)"", 0);
    dst[n] = '\0';
    check_str("encode(empty)", dst, "");

    n = neverc_base64_encode(dst, (const uint8_t *)"f", 1);
    dst[n] = '\0';
    check_str("encode(f)", dst, "Zg==");

    n = neverc_base64_encode(dst, (const uint8_t *)"fo", 2);
    dst[n] = '\0';
    check_str("encode(fo)", dst, "Zm8=");

    n = neverc_base64_encode(dst, (const uint8_t *)"foo", 3);
    dst[n] = '\0';
    check_str("encode(foo)", dst, "Zm9v");

    n = neverc_base64_encode(dst, (const uint8_t *)"foob", 4);
    dst[n] = '\0';
    check_str("encode(foob)", dst, "Zm9vYg==");

    n = neverc_base64_encode(dst, (const uint8_t *)"fooba", 5);
    dst[n] = '\0';
    check_str("encode(fooba)", dst, "Zm9vYmE=");

    n = neverc_base64_encode(dst, (const uint8_t *)"foobar", 6);
    dst[n] = '\0';
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

    n = neverc_base64_decode(dst, "Zm9v\r\nYmFy", 10);
    check_int("decode ignores CRLF length", n, 6);
    check_mem("decode ignores CRLF value",
              dst, (const uint8_t *)"foobar", 6);

    n = neverc_base64_decode(dst, "Zg=\n=", 5);
    check_int("decode padding split by newline length", n, 1);
    check_mem("decode padding split by newline value",
              dst, (const uint8_t *)"f", 1);

    n = neverc_base64_decode(dst, "!!!", 3);
    check_int("decode(invalid)", n, -1);

    check_int("standard rejects URL alphabet",
              neverc_base64_decode(dst, "-__-", 4), -1);
    check_int("reject non-newline whitespace",
              neverc_base64_decode(dst, "Z g==", 5), -1);
    check_int("standard rejects comma",
              neverc_base64_decode(dst, "AAAA,AAA", 8), -1);
    check_int("reject excess padding",
              neverc_base64_decode(dst, "Zg===", 5), -1);
    check_int("reject incomplete padding",
              neverc_base64_decode(dst, "Zg=", 3), -1);
    check_int("reject interior padding",
              neverc_base64_decode(dst, "Z=g=", 4), -1);
    check_int("reject noncanonical padded tail",
              neverc_base64_decode(dst, "Zh==", 4), -1);
    check_int("reject noncanonical raw tail",
              neverc_base64_decode(dst, "Zh", 2), -1);
    n = neverc_base64_decode(dst, "Zg", 2);
    check_int("decode canonical raw input", n, 1);
    check_mem("decode canonical raw value",
              dst, (const uint8_t *)"f", 1);

    size_t max_encoded_for_int =
        ((size_t)INT_MAX / 3 + ((size_t)INT_MAX % 3 != 0)) * 4;
    check_int("decode rejects result larger than int",
              neverc_base64_decode(dst, "AA", max_encoded_for_int + 2), -1);
}

static void test_url_encode(void) {
    printf("[url_encode]\n");
    char dst[64];

    uint8_t data[] = {0xfb, 0xff, 0xfe};
    size_t nencoded = neverc_base64_url_encode(dst, data, 3);
    dst[nencoded] = '\0';
    check_str("url_encode(fbfffe)", dst, "-__-");

    nencoded = neverc_base64_encode(dst, data, 3);
    dst[nencoded] = '\0';
    check_str("std_encode(fbfffe)", dst, "+//+");

    uint8_t decoded[8];
    int n = neverc_base64_url_decode(decoded, "-__-", 4);
    check_int("url_decode.len", n, 3);
    check_mem("url_decode.val", decoded, data, sizeof(data));
    check_int("URL decoder rejects standard alphabet",
              neverc_base64_url_decode(decoded, "+//+", 4), -1);
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
