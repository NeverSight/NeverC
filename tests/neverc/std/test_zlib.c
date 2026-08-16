/*
 * NeverC compress/zlib tests.
 */
#include "neverc/std/compress/zlib.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = %d, expected %d\n", __LINE__, #expr, _v, _e); } \
} while(0)

#define ASSERT_TRUE(expr) ASSERT_INT_EQ(!!(expr), 1)

static void test_roundtrip(const char *label, const uint8_t *data, size_t len, int level) {
    printf("[zlib %s level=%d]\n", label, level);
    uint8_t comp[131072], decomp[131072];
    size_t comp_len = sizeof(comp);
    size_t decomp_len = sizeof(decomp);

    int rc = neverc_zlib_compress(data, len, comp, &comp_len, level);
    ASSERT_INT_EQ(rc, 0);

    /* verify zlib magic (CMF=0x78) */
    ASSERT_INT_EQ(comp[0], 0x78);

    rc = neverc_zlib_decompress(comp, comp_len, decomp, &decomp_len);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ((int)decomp_len, (int)len);

    tests_run++;
    if (len == 0 || memcmp(data, decomp, len) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: roundtrip mismatch\n"); }
}

static void test_invalid_headers_and_spans(void) {
    printf("[zlib invalid headers and spans]\n");
    uint8_t compressed[256], output[64];
    size_t compressed_len = sizeof(compressed);
    ASSERT_INT_EQ(neverc_zlib_compress(
                      (const uint8_t *)"abc", 3, compressed,
                      &compressed_len, 1),
                  0);
    size_t valid_compressed_len = compressed_len;

    uint8_t invalid[256];
    memcpy(invalid, compressed, compressed_len);
    invalid[0] = 0x88; /* CM=8 but CINFO=8 exceeds the 32 KiB window. */
    for (unsigned flag = 0; flag < 32; flag++) {
        if ((((unsigned)invalid[0] << 8) + flag) % 31 == 0) {
            invalid[1] = (uint8_t)flag;
            break;
        }
    }
    size_t output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      invalid, compressed_len, output, &output_len),
                  -1);

    ASSERT_INT_EQ(neverc_zlib_decompress(
                      compressed, compressed_len, output, NULL),
                  -1);
    size_t invalid_capacity = sizeof(compressed);
    ASSERT_INT_EQ(neverc_zlib_compress(
                      NULL, 1, compressed, &invalid_capacity, 1),
                  -1);

    uint8_t junk_before_adler[256];
    memcpy(junk_before_adler, compressed, valid_compressed_len - 4);
    junk_before_adler[valid_compressed_len - 4] = 0xaa;
    memcpy(junk_before_adler + valid_compressed_len - 3,
           compressed + valid_compressed_len - 4, 4);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      junk_before_adler, valid_compressed_len + 1,
                      output, &output_len),
                  -1);

    memcpy(invalid, compressed, valid_compressed_len);
    invalid[valid_compressed_len - 1] ^= 1U;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      invalid, valid_compressed_len, output, &output_len),
                  -1);
}

int main(void) {
    printf("=== NeverC compress/zlib Tests ===\n");
    test_roundtrip("empty", (uint8_t *)"", 0, 0);
    test_roundtrip("hello", (uint8_t *)"Hello, World!", 13, 0);
    test_roundtrip("hello", (uint8_t *)"Hello, World!", 13, 6);

    uint8_t data[4096];
    memset(data, 'B', sizeof(data));
    test_roundtrip("repetitive", data, sizeof(data), 6);

    for (int i = 0; i < 4096; i++) data[i] = (uint8_t)((i * 7) & 0xFF);
    test_roundtrip("mixed", data, sizeof(data), 1);
    test_invalid_headers_and_spans();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
