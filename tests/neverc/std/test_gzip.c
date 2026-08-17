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

    uint8_t junk_before_trailer[320];
    memcpy(junk_before_trailer, base, base_len - 8);
    junk_before_trailer[base_len - 8] = 0xaa;
    memcpy(junk_before_trailer + base_len - 7, base + base_len - 8, 8);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      junk_before_trailer, base_len + 1, output, &output_len),
                  -1);

    uint8_t missing_comment_terminator[18] = {
        0x1f, 0x8b, 0x08, 0x10, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1
    };
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      missing_comment_terminator,
                      sizeof(missing_comment_terminator), output, &output_len),
                  -1);

    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(base, 10, output, &output_len), -1);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(base, base_len - 4, output, &output_len),
                  -1);
}

static void test_concatenated_members(void) {
    printf("[gzip concatenated members]\n");
    uint8_t first[256], second[256], cat[512], output[64];
    size_t first_len = sizeof(first);
    size_t second_len = sizeof(second);
    ASSERT_INT_EQ(neverc_gzip_compress(
                      (const uint8_t *)"hello", 5, first, &first_len, 1),
                  0);
    ASSERT_INT_EQ(neverc_gzip_compress(
                      (const uint8_t *)" world", 6, second, &second_len, 1),
                  0);
    memcpy(cat, first, first_len);
    memcpy(cat + first_len, second, second_len);
    size_t cat_len = first_len + second_len;

    /* RFC 1952 files are a series of members. The last member's ISIZE (6)
     * must not be used as the inflate cap for the concatenated payload. */
    size_t output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(cat, cat_len, output, &output_len), 0);
    ASSERT_TRUE(output_len == 11 && memcmp(output, "hello world", 11) == 0);

    output_len = 5; /* enough for the first member only */
    ASSERT_INT_EQ(neverc_gzip_decompress(cat, cat_len, output, &output_len),
                  -1);

    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(cat, first_len + 4, output, &output_len),
                  -1);

    uint8_t empty_comp[256];
    size_t empty_len = sizeof(empty_comp);
    ASSERT_INT_EQ(neverc_gzip_compress(
                      (const uint8_t *)"", 0, empty_comp, &empty_len, 1),
                  0);
    memcpy(cat, empty_comp, empty_len);
    memcpy(cat + empty_len, first, first_len);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      cat, empty_len + first_len, output, &output_len),
                  0);
    ASSERT_TRUE(output_len == 5 && memcmp(output, "hello", 5) == 0);

    /* Second member carries FNAME+FHCRC; header CRC is relative to that member. */
    uint8_t extended[320];
    memcpy(extended, first, 10);
    extended[3] = 0x0a; /* FNAME | FHCRC */
    size_t pos = 10;
    extended[pos++] = 'n';
    extended[pos++] = '\0';
    uint16_t header_crc =
        (uint16_t)(neverc_crc32_ieee(extended, pos) & UINT32_C(0xffff));
    extended[pos++] = (uint8_t)header_crc;
    extended[pos++] = (uint8_t)(header_crc >> 8);
    memcpy(extended + pos, first + 10, first_len - 10);
    size_t extended_len = pos + first_len - 10;
    memcpy(cat, second, second_len);
    memcpy(cat + second_len, extended, extended_len);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_gzip_decompress(
                      cat, second_len + extended_len, output, &output_len),
                  0);
    ASSERT_TRUE(output_len == 11 && memcmp(output, " worldhello", 11) == 0);
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
    test_concatenated_members();

    {
        uint8_t comp[256], output[4096];
        size_t comp_len = sizeof(comp);
        ASSERT_INT_EQ(neverc_gzip_compress(
                          (const uint8_t *)"hello", 5, comp, &comp_len, 1),
                      0);
        uint8_t crc_corrupt[256];
        memcpy(crc_corrupt, comp, comp_len);
        crc_corrupt[comp_len - 8] ^= 1U;
        size_t output_len = sizeof(output);
        ASSERT_INT_EQ(neverc_gzip_decompress(
                          crc_corrupt, comp_len, output, &output_len),
                      -1);

        /* ISIZE is size mod 2^32 and must not be used as the inflate cap.
         * A wrong ISIZE is rejected after inflate, even when the destination
         * is larger than the forged size. */
        comp[comp_len - 4] = 1;
        comp[comp_len - 3] = 0;
        comp[comp_len - 2] = 0;
        comp[comp_len - 1] = 0;
        output_len = sizeof(output);
        ASSERT_INT_EQ(neverc_gzip_decompress(
                          comp, comp_len, output, &output_len),
                      -1);

        /* Forged ISIZE larger than the destination still must not be treated
         * as the inflate cap (the overflow direction of the same bug). */
        uint8_t large_isize[256];
        memcpy(large_isize, comp, comp_len);
        large_isize[comp_len - 4] = 0xff;
        large_isize[comp_len - 3] = 0xff;
        large_isize[comp_len - 2] = 0xff;
        large_isize[comp_len - 1] = 0xff;
        output_len = 16; /* bigger than the real 5-byte payload, far below ISIZE */
        ASSERT_INT_EQ(neverc_gzip_decompress(
                          large_isize, comp_len, output, &output_len),
                      -1);
    }

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
