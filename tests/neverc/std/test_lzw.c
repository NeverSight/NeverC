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
    test_roundtrip("empty_tiff_msb", empty, 0, NEVERC_LZW_TIFF_MSB, 8);
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
    ASSERT_INT_EQ(neverc_lzw_compress((uint8_t*)"a", 1, buf, &len,
                                     NEVERC_LZW_TIFF_MSB, 7), -1);

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
    len = sizeof(buf);
    ASSERT_INT_EQ(neverc_lzw_decompress(buf, 1, buf, &len,
                                       NEVERC_LZW_TIFF_MSB, 7), -1);
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

/* A complete TIFF LZW strip produced by libtiff 4.7.1
 * (commit 5fe20d0e9aba49a6a350ed533459d1505203838f) from the 256 bytes
 * 0xff, 0xfe, ..., 0x00 with Predictor disabled. Every adjacent pair is
 * unique, so the strip crosses the TIFF 9-to-10-bit early-change boundary
 * without dictionary-code shortcuts. This fixture is deliberately external
 * to NeverC's encoder and catches the one-code-late Go compress/lzw profile. */
static const uint8_t tiff_9_to_10_reference[] = {
    0x80, 0x3f, 0xdf, 0xcf, 0xd7, 0xe3, 0xed, 0xf4, 0xf9, 0x7c, 0x3d, 0xde,
    0xcf, 0x57, 0xa3, 0xcd, 0xe4, 0xf1, 0x78, 0x3b, 0xdd, 0xce, 0xd7, 0x63,
    0xad, 0xd4, 0xe9, 0x74, 0x39, 0xdc, 0xce, 0x57, 0x23, 0x8d, 0xc4, 0xe1,
    0x70, 0x37, 0xdb, 0xcd, 0xd6, 0xe3, 0x6d, 0xb4, 0xd9, 0x6c, 0x35, 0xda,
    0xcd, 0x56, 0xa3, 0x4d, 0xa4, 0xd1, 0x68, 0x33, 0xd9, 0xcc, 0xd6, 0x63,
    0x2d, 0x94, 0xc9, 0x64, 0x31, 0xd8, 0xcc, 0x56, 0x23, 0x0d, 0x84, 0xc1,
    0x60, 0x2f, 0xd7, 0xcb, 0xd5, 0xe2, 0xed, 0x74, 0xb9, 0x5c, 0x2d, 0xd6,
    0xcb, 0x55, 0xa2, 0xcd, 0x64, 0xb1, 0x58, 0x2b, 0xd5, 0xca, 0xd5, 0x62,
    0xad, 0x54, 0xa9, 0x54, 0x29, 0xd4, 0xca, 0x55, 0x22, 0x8d, 0x44, 0xa1,
    0x50, 0x27, 0xd3, 0xc9, 0xd4, 0xe2, 0x6d, 0x34, 0x99, 0x4c, 0x25, 0xd2,
    0xc9, 0x54, 0xa2, 0x4d, 0x24, 0x91, 0x48, 0x23, 0xd1, 0xc8, 0xd4, 0x62,
    0x2d, 0x14, 0x89, 0x44, 0x21, 0xd0, 0xc8, 0x54, 0x22, 0x0d, 0x04, 0x81,
    0x40, 0x1f, 0xcf, 0xc7, 0xd3, 0xe1, 0xec, 0xf4, 0x79, 0x3c, 0x1d, 0xce,
    0xc7, 0x53, 0xa1, 0xcc, 0xe4, 0x71, 0x38, 0x1b, 0xcd, 0xc6, 0xd3, 0x61,
    0xac, 0xd4, 0x69, 0x34, 0x19, 0xcc, 0xc6, 0x53, 0x21, 0x8c, 0xc4, 0x61,
    0x30, 0x17, 0xcb, 0xc5, 0xd2, 0xe1, 0x6c, 0xb4, 0x59, 0x2c, 0x15, 0xca,
    0xc5, 0x52, 0xa1, 0x4c, 0xa4, 0x51, 0x28, 0x13, 0xc9, 0xc4, 0xd2, 0x61,
    0x2c, 0x94, 0x49, 0x24, 0x11, 0xc8, 0xc4, 0x52, 0x21, 0x0c, 0x84, 0x41,
    0x20, 0x0f, 0xc7, 0xc3, 0xd1, 0xe0, 0xec, 0x74, 0x39, 0x1c, 0x0d, 0xc6,
    0xc3, 0x51, 0xa0, 0xcc, 0x64, 0x31, 0x18, 0x0b, 0xc5, 0xc2, 0xd1, 0x60,
    0xac, 0x54, 0x29, 0x14, 0x09, 0xc4, 0xc2, 0x51, 0x20, 0x8c, 0x44, 0x21,
    0x10, 0x07, 0xc3, 0xc1, 0xd0, 0xe0, 0x6c, 0x34, 0x19, 0x0c, 0x05, 0xc2,
    0xc1, 0x50, 0xa0, 0x4c, 0x24, 0x11, 0x08, 0x03, 0xc1, 0xc0, 0xd0, 0x60,
    0x2c, 0x14, 0x09, 0x04, 0x01, 0xc0, 0xc0, 0x50, 0x20, 0x0c, 0x04, 0x00,
    0x80, 0x08, 0x08
};

static uint64_t fnv1a64(const uint8_t *data, size_t len) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static void test_tiff_reference_boundary(void) {
    printf("[tiff_reference_9_to_10]\n");
    uint8_t expected[256], decoded[256], encoded[sizeof(tiff_9_to_10_reference)];
    for (int i = 0; i < 256; i++) expected[i] = (uint8_t)(255 - i);

    size_t decoded_len = sizeof(decoded);
    ASSERT_INT_EQ(neverc_lzw_decompress(tiff_9_to_10_reference,
                                        sizeof(tiff_9_to_10_reference),
                                        decoded, &decoded_len,
                                        NEVERC_LZW_TIFF_MSB, 8), 0);
    ASSERT_TRUE(decoded_len == sizeof(expected));
    ASSERT_TRUE(memcmp(decoded, expected, sizeof(expected)) == 0);

    size_t encoded_len = sizeof(encoded);
    ASSERT_INT_EQ(neverc_lzw_compress(expected, sizeof(expected),
                                      encoded, &encoded_len,
                                      NEVERC_LZW_TIFF_MSB, 8), 0);
    ASSERT_TRUE(encoded_len == sizeof(tiff_9_to_10_reference));
    ASSERT_TRUE(memcmp(encoded, tiff_9_to_10_reference, encoded_len) == 0);

    /* The legacy MSB profile must remain late-change and therefore cannot
     * silently accept a TIFF early-change stream at the width boundary. */
    decoded_len = sizeof(decoded);
    ASSERT_TRUE(neverc_lzw_decompress(tiff_9_to_10_reference,
                                      sizeof(tiff_9_to_10_reference),
                                      decoded, &decoded_len,
                                      NEVERC_LZW_MSB, 8) != 0);
}

static void test_tiff_libtiff_full_table(void) {
    printf("[tiff_libtiff_full_table]\n");
    uint8_t input[8192], encoded[16384], decoded[8192];
    uint32_t state = UINT32_C(0x12345678);
    for (size_t i = 0; i < sizeof(input); i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        input[i] = (uint8_t)state;
    }

    /* These independent fingerprints come from the same pinned libtiff 4.7.1
     * build with Predictor disabled. The 11,146-byte strip crosses 9->10,
     * 10->11, and 11->12 bits,
     * then emits two full-table clear codes. Checking the exact compressed
     * fingerprint before decoding prevents an encoder/decoder roundtrip from
     * hiding a shared transition bug. */
    ASSERT_TRUE(fnv1a64(input, sizeof(input)) ==
                UINT64_C(0xf7c01728342d75c1));
    size_t encoded_len = sizeof(encoded);
    ASSERT_INT_EQ(neverc_lzw_compress(input, sizeof(input), encoded,
                                      &encoded_len, NEVERC_LZW_TIFF_MSB, 8), 0);
    ASSERT_TRUE(encoded_len == 11146);
    ASSERT_TRUE(fnv1a64(encoded, encoded_len) ==
                UINT64_C(0x5d6ff370c0028d8e));

    size_t decoded_len = sizeof(decoded);
    ASSERT_INT_EQ(neverc_lzw_decompress(encoded, encoded_len, decoded,
                                        &decoded_len,
                                        NEVERC_LZW_TIFF_MSB, 8), 0);
    ASSERT_TRUE(decoded_len == sizeof(input));
    ASSERT_TRUE(memcmp(decoded, input, sizeof(input)) == 0);

    /* The first 3,946 source bytes end exactly when libtiff advances its final
     * pending code to the full-table threshold. LZWPostEncode emits CLEAR and
     * then the 9-bit EOI; this fingerprint catches an encoder that writes EOI
     * directly with the stale 12-bit width. */
    encoded_len = sizeof(encoded);
    ASSERT_INT_EQ(neverc_lzw_compress(input, 3946, encoded, &encoded_len,
                                      NEVERC_LZW_TIFF_MSB, 8), 0);
    ASSERT_TRUE(encoded_len == 5407);
    ASSERT_TRUE(fnv1a64(encoded, encoded_len) ==
                UINT64_C(0x5c0ac006b1e99e7e));
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
    test_tiff_reference_boundary();
    test_tiff_libtiff_full_table();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
