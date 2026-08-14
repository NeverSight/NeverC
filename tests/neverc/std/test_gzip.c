/*
 * NeverC compress/gzip tests.
 */
#include "neverc/std/compress/gzip.h"
#include "neverc/std/hash/crc32.h"
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
    printf("[gzip %s level=%d]\n", label, level);
    uint8_t comp[131072], decomp[131072];
    size_t comp_len = sizeof(comp);
    size_t decomp_len = sizeof(decomp);

    int rc = neverc_gzip_compress(data, len, comp, &comp_len, level);
    ASSERT_INT_EQ(rc, 0);

    /* verify gzip magic */
    ASSERT_INT_EQ(comp[0], 0x1F);
    ASSERT_INT_EQ(comp[1], 0x8B);

    rc = neverc_gzip_decompress(comp, comp_len, decomp, &decomp_len);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ((int)decomp_len, (int)len);

    tests_run++;
    if (len == 0 || memcmp(data, decomp, len) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: roundtrip mismatch\n"); }
}

static void test_optional_headers_and_invalid_inputs(void) {
    printf("[gzip optional headers and invalid inputs]\n");
    uint8_t base[256], extended[320], output[64];
    size_t base_len = sizeof(base);
    ASSERT_INT_EQ(neverc_gzip_compress(
                      (const uint8_t *)"abc", 3, base, &base_len, 1),
                  0);

    memcpy(extended, base, 10);
    extended[3] = 0x1e; /* FEXTRA | FNAME | FCOMMENT | FHCRC */
    size_t pos = 10;
    extended[pos++] = 2;
    extended[pos++] = 0;
    extended[pos++] = 0xaa;
    extended[pos++] = 0x55;
    extended[pos++] = 'n';
    extended[pos++] = '\0';
    extended[pos++] = 'c';
    extended[pos++] = '\0';
    uint16_t header_crc =
        (uint16_t)(neverc_crc32_ieee(extended, pos) & UINT32_C(0xffff));
    size_t header_crc_pos = pos;
    extended[pos++] = (uint8_t)header_crc;
    extended[pos++] = (uint8_t)(header_crc >> 8);
    memcpy(extended + pos, base + 10, base_len - 10);
    size_t extended_len = pos + base_len - 10;

    size_t output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      extended, extended_len, output, &output_len),
                  0);
    ASSERT_TRUE(output_len == 3 && memcmp(output, "abc", 3) == 0);

    extended[header_crc_pos] ^= 1;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      extended, extended_len, output, &output_len),
                  -1);
    extended[header_crc_pos] ^= 1;

    uint8_t malformed[256];
    memcpy(malformed, base, base_len);
    malformed[3] = 0x04;
    malformed[10] = 0xff;
    malformed[11] = 0xff;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      malformed, base_len, output, &output_len),
                  -1);

    memcpy(malformed, base, base_len);
    malformed[3] = 0x20;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      malformed, base_len, output, &output_len),
                  -1);

    uint8_t missing_name_terminator[18] = {
        0x1f, 0x8b, 0x08, 0x08, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1
    };
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      missing_name_terminator,
                      sizeof(missing_name_terminator), output, &output_len),
                  -1);

    ASSERT_INT_EQ(neverc_gzip_decompress(
                      base, base_len, output, NULL),
                  -1);
    base_len = sizeof(base);
    ASSERT_INT_EQ(neverc_gzip_compress(
                      NULL, 1, base, &base_len, 1),
                  -1);
}

int main(void) {
    printf("=== NeverC compress/gzip Tests ===\n");
    test_roundtrip("empty", (uint8_t *)"", 0, 0);
    test_roundtrip("hello", (uint8_t *)"Hello, World!", 13, 0);
    test_roundtrip("hello", (uint8_t *)"Hello, World!", 13, 6);

    uint8_t data[4096];
    memset(data, 'A', sizeof(data));
    test_roundtrip("repetitive", data, sizeof(data), 6);

    for (int i = 0; i < 4096; i++) data[i] = (uint8_t)(i & 0xFF);
    test_roundtrip("mixed", data, sizeof(data), 1);
    test_optional_headers_and_invalid_inputs();

    {
        uint8_t comp[256], output[4096];
        size_t comp_len = sizeof(comp);
        ASSERT_INT_EQ(neverc_gzip_compress(
                          (const uint8_t *)"hello", 5, comp, &comp_len, 1),
                      0);
        comp[comp_len - 4] = 1;
        comp[comp_len - 3] = 0;
        comp[comp_len - 2] = 0;
        comp[comp_len - 1] = 0;
        size_t output_len = sizeof(output);
        ASSERT_INT_EQ(neverc_gzip_decompress(
                          comp, comp_len, output, &output_len),
                      -1);
    }

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
