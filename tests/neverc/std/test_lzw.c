/*
 * NeverC compress/lzw tests.
 * Tests LZW compression + decompression roundtrip.
 */
#include "neverc/std/compress/lzw.h"
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

static void test_roundtrip(const char *label, const uint8_t *data, size_t len,
                           int order, int lit_width) {
    printf("[%s]\n", label);
    uint8_t comp[65536], decomp[65536];
    size_t comp_len = sizeof(comp);
    size_t decomp_len = sizeof(decomp);

    int rc = neverc_lzw_compress(data, len, comp, &comp_len, order, lit_width);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(comp_len > 0);

    rc = neverc_lzw_decompress(comp, comp_len, decomp, &decomp_len, order, lit_width);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ((int)decomp_len, (int)len);

    tests_run++;
    if (memcmp(data, decomp, len) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: decompressed data mismatch\n"); }
}

static void test_empty(void) {
    uint8_t empty[] = {};
    test_roundtrip("empty_lsb", empty, 0, NEVERC_LZW_LSB, 8);
    test_roundtrip("empty_msb", empty, 0, NEVERC_LZW_MSB, 8);
}

static void test_simple(void) {
    const uint8_t data[] = "Hello, World!";
    test_roundtrip("hello_lsb", data, 13, NEVERC_LZW_LSB, 8);
    test_roundtrip("hello_msb", data, 13, NEVERC_LZW_MSB, 8);
}

static void test_repetitive(void) {
    uint8_t data[1024];
    memset(data, 'A', sizeof(data));
    test_roundtrip("repetitive_lsb", data, sizeof(data), NEVERC_LZW_LSB, 8);

    /* Should compress well */
    uint8_t comp[2048];
    size_t comp_len = sizeof(comp);
    neverc_lzw_compress(data, sizeof(data), comp, &comp_len, NEVERC_LZW_LSB, 8);
    tests_run++;
    if (comp_len < sizeof(data)) tests_passed++;
    else { tests_failed++; printf("  FAIL: repetitive not compressed (comp=%zu, orig=%zu)\n", comp_len, sizeof(data)); }
}

static void test_sequential(void) {
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;
    test_roundtrip("sequential_lsb", data, 256, NEVERC_LZW_LSB, 8);
    test_roundtrip("sequential_msb", data, 256, NEVERC_LZW_MSB, 8);
}

static void test_lit_width(void) {
    /* Test with smaller literal widths */
    uint8_t data[] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    test_roundtrip("litwidth2", data, sizeof(data), NEVERC_LZW_LSB, 2);
    /* litWidth=4: values must be < 16 */
    uint8_t d4[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    test_roundtrip("litwidth4", d4, sizeof(d4), NEVERC_LZW_LSB, 4);
}

static void test_large(void) {
    /* ~8KB of mixed data */
    uint8_t data[8192];
    for (int i = 0; i < 8192; i++)
        data[i] = (uint8_t)((i * 37 + i / 100) & 0xFF);
    test_roundtrip("large_lsb", data, sizeof(data), NEVERC_LZW_LSB, 8);
}

static void test_invalid_params(void) {
    printf("[invalid_params]\n");
    uint8_t buf[64];
    size_t len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_compress((uint8_t*)"a", 1, buf, &len, NEVERC_LZW_LSB, 1), -1);
    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_compress((uint8_t*)"a", 1, buf, &len, NEVERC_LZW_LSB, 9), -1);
    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_compress((uint8_t*)"a", 1, buf, &len, 5, 8), -1);

    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_compress(NULL, 1, buf, &len, NEVERC_LZW_LSB, 8), -1);
    ASSERT_INT_EQ(neverc_lzw_compress((uint8_t*)"a", 1, buf, NULL,
                                     NEVERC_LZW_LSB, 8), -1);
    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_compress((uint8_t*)"a", 1, NULL, &len,
                                     NEVERC_LZW_LSB, 8), -1);

    uint8_t invalid_literal[] = {0, 4};
    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_compress(invalid_literal, sizeof(invalid_literal),
                                     buf, &len, NEVERC_LZW_LSB, 2), -1);

    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_decompress(NULL, 1, buf, &len,
                                       NEVERC_LZW_LSB, 8), -1);
    ASSERT_INT_EQ(neverc_lzw_decompress(buf, 1, buf, NULL,
                                       NEVERC_LZW_LSB, 8), -1);
    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_decompress(buf, 1, NULL, &len,
                                       NEVERC_LZW_LSB, 8), -1);

    uint8_t empty_comp[64];
    size_t empty_comp_len = sizeof(empty_comp);
    ASSERT_INT_EQ(neverc_lzw_compress((const uint8_t *)"", 0, empty_comp,
                                     &empty_comp_len, NEVERC_LZW_LSB, 8), 0);
    size_t empty_out = 0;
    ASSERT_INT_EQ(neverc_lzw_decompress(empty_comp, empty_comp_len, NULL,
                                       &empty_out, NEVERC_LZW_LSB, 8), 0);
    ASSERT_TRUE(empty_out == 0);
}

static int lzw_lsb_emit(uint8_t *buf, size_t cap, size_t *pos,
                        uint32_t *acc, unsigned *nbits,
                        uint32_t code, unsigned width) {
    *acc |= code << *nbits;
    *nbits += width;
    while (*nbits >= 8U) {
        if (*pos >= cap) return -1;
        buf[(*pos)++] = (uint8_t)*acc;
        *acc >>= 8;
        *nbits -= 8U;
    }
    return 0;
}

static int lzw_lsb_flush(uint8_t *buf, size_t cap, size_t *pos,
                         uint32_t *acc, unsigned *nbits) {
    if (*nbits == 0) return 0;
    if (*pos >= cap) return -1;
    buf[(*pos)++] = (uint8_t)*acc;
    *acc = 0;
    *nbits = 0;
    return 0;
}

static void test_clear_code(void) {
    printf("[clear_code]\n");
    uint8_t stream[16];
    uint8_t out[16];
    size_t pos, out_len;
    uint32_t acc;
    unsigned nbits;

    /* clear, 'A', 'B' defines code 258 as "AB". A second clear must drop it. */
    memset(stream, 0, sizeof(stream));
    pos = 0;
    acc = 0;
    nbits = 0;
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               256, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               65, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               66, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               256, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               258, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               257, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_flush(stream, sizeof(stream), &pos, &acc, &nbits), 0);
    out_len = sizeof(out);
    ASSERT_INT_EQ(neverc_lzw_decompress(stream, pos, out, &out_len,
                                        NEVERC_LZW_LSB, 8), -1);

    memset(stream, 0, sizeof(stream));
    pos = 0;
    acc = 0;
    nbits = 0;
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               256, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               65, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               66, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               256, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               65, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               66, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_emit(stream, sizeof(stream), &pos, &acc, &nbits,
                               257, 9), 0);
    ASSERT_INT_EQ(lzw_lsb_flush(stream, sizeof(stream), &pos, &acc, &nbits), 0);
    out_len = sizeof(out);
    ASSERT_INT_EQ(neverc_lzw_decompress(stream, pos, out, &out_len,
                                        NEVERC_LZW_LSB, 8), 0);
    ASSERT_TRUE(out_len == 4 && memcmp(out, "ABAB", 4) == 0);
}

static void test_leftover_bytes(void) {
    printf("[leftover_bytes]\n");
    const uint8_t data[] = "Hello, World!";
    uint8_t comp[256];
    uint8_t junk[257];
    uint8_t decomp[64];
    int orders[] = { NEVERC_LZW_LSB, NEVERC_LZW_MSB };
    for (int i = 0; i < 2; i++) {
        size_t comp_len = sizeof(comp);
        ASSERT_INT_EQ(neverc_lzw_compress(data, 13, comp, &comp_len,
                                          orders[i], 8), 0);
        memcpy(junk, comp, comp_len);
        junk[comp_len] = 0x5A;
        size_t decomp_len = sizeof(decomp);
        ASSERT_TRUE(neverc_lzw_decompress(junk, comp_len + 1, decomp,
                                          &decomp_len, orders[i], 8) != 0);
        decomp_len = sizeof(decomp);
        ASSERT_TRUE(neverc_lzw_decompress(comp, comp_len - 1, decomp,
                                          &decomp_len, orders[i], 8) != 0);
    }
}

int main(void) {
    printf("=== NeverC compress/lzw Tests ===\n");
    test_empty();
    test_simple();
    test_repetitive();
    test_sequential();
    test_lit_width();
    test_large();
    test_invalid_params();
    test_clear_code();
    test_leftover_bytes();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
