/*
 * NeverC compress/flate tests.
 * Tests DEFLATE compression + decompression roundtrip.
 */
#include "neverc/std/compress/flate.h"
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
    printf("[%s level=%d]\n", label, level);
    uint8_t comp[131072], decomp[131072];
    size_t comp_len = sizeof(comp);
    size_t decomp_len = sizeof(decomp);

    int rc = neverc_flate_compress(data, len, comp, &comp_len, level);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(comp_len > 0);

    rc = neverc_flate_decompress(comp, comp_len, decomp, &decomp_len);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ((int)decomp_len, (int)len);

    tests_run++;
    if (len == 0 || memcmp(data, decomp, len) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: decompressed data mismatch\n"); }
}

static void test_empty(void) {
    test_roundtrip("empty", (uint8_t *)"", 0, 0);
    test_roundtrip("empty", (uint8_t *)"", 0, 6);
}

static void test_simple(void) {
    const uint8_t *data = (const uint8_t *)"Hello, World!";
    test_roundtrip("hello", data, 13, 0);
    test_roundtrip("hello", data, 13, 1);
    test_roundtrip("hello", data, 13, 6);
    test_roundtrip("hello", data, 13, 9);
}

static void test_repetitive(void) {
    uint8_t data[4096];
    memset(data, 'X', sizeof(data));
    test_roundtrip("repetitive", data, sizeof(data), 0);
    test_roundtrip("repetitive", data, sizeof(data), 1);
    test_roundtrip("repetitive", data, sizeof(data), 6);

    /* compressed should be smaller for level > 0 */
    uint8_t comp[8192];
    size_t comp_len = sizeof(comp);
    neverc_flate_compress(data, sizeof(data), comp, &comp_len, 6);
    tests_run++;
    if (comp_len < sizeof(data)) tests_passed++;
    else { tests_failed++; printf("  FAIL: repetitive not compressed\n"); }
}

static void test_sequential(void) {
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;
    test_roundtrip("sequential", data, 256, 1);
    test_roundtrip("sequential", data, 256, 6);
}

static void test_large(void) {
    uint8_t data[32768];
    for (int i = 0; i < 32768; i++)
        data[i] = (uint8_t)((i * 37 + i / 100) & 0xFF);
    test_roundtrip("large_32k", data, sizeof(data), 1);
    test_roundtrip("large_32k", data, sizeof(data), 6);
}

/* Exercise the inflate back-reference copy across every overlap shape:
 * distance == 1 (RLE/memset), small periodic distances (overlapping runs),
 * and large periods (disjoint memcpy), at lengths that span the short/long
 * thresholds inside copy_match. */
static void test_overlap_copy(void) {
    static uint8_t data[20000];
    for (int period = 1; period <= 259; period++) {
        size_t n = sizeof(data);
        for (size_t i = 0; i < n; i++)
            data[i] = (uint8_t)((i % period) * 31 + 7);
        char label[32];
        snprintf(label, sizeof(label), "overlap_p%d", period);
        test_roundtrip(label, data, n, 6);
        if (period <= 4 || period == 32 || period == 258)
            test_roundtrip(label, data, n, 9);
    }
    /* Pure single-byte run (distance 1, max-length matches). */
    memset(data, 0x5A, sizeof(data));
    test_roundtrip("single_byte_run", data, sizeof(data), 9);
}

static void test_stored_blocks(void) {
    /* level=0 uses stored blocks, test block splitting for >64K */
    uint8_t data[70000];
    for (int i = 0; i < 70000; i++)
        data[i] = (uint8_t)(i & 0xFF);

    uint8_t comp[75000], decomp[75000];
    size_t comp_len = sizeof(comp);
    size_t decomp_len = sizeof(decomp);

    printf("[stored_blocks_large]\n");
    int rc = neverc_flate_compress(data, sizeof(data), comp, &comp_len, 0);
    ASSERT_INT_EQ(rc, 0);

    rc = neverc_flate_decompress(comp, comp_len, decomp, &decomp_len);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ((int)decomp_len, (int)sizeof(data));

    tests_run++;
    if (memcmp(data, decomp, sizeof(data)) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: stored blocks large roundtrip\n"); }
}

static void put_bits(uint8_t *buffer, size_t *bit_offset,
                     unsigned value, unsigned count) {
    for (unsigned bit = 0; bit < count; bit++) {
        if ((value >> bit) & 1U)
            buffer[*bit_offset / 8U] |=
                (uint8_t)(1U << (*bit_offset % 8U));
        (*bit_offset)++;
    }
}

static uint32_t bit_reverse(uint32_t code, unsigned nbits) {
    uint32_t rev = 0;
    for (unsigned i = 0; i < nbits; i++)
        rev |= ((code >> i) & 1U) << (nbits - 1U - i);
    return rev;
}

static void put_fixed_litlen(uint8_t *buffer, size_t *bit_offset, unsigned sym) {
    uint32_t code;
    unsigned nbits;
    if (sym <= 143U) {
        code = sym + 0x30U;
        nbits = 8;
    } else if (sym <= 255U) {
        code = sym - 144U + 0x190U;
        nbits = 9;
    } else if (sym <= 279U) {
        code = sym - 256U;
        nbits = 7;
    } else {
        code = sym - 280U + 0xC0U;
        nbits = 8;
    }
    put_bits(buffer, bit_offset, bit_reverse(code, nbits), nbits);
}

static void put_fixed_dist_sym(uint8_t *buffer, size_t *bit_offset,
                               unsigned sym) {
    put_bits(buffer, bit_offset, bit_reverse(sym, 5U), 5U);
}

static void put_fixed_distance(uint8_t *buffer, size_t *bit_offset,
                               unsigned distance) {
    static const uint16_t base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const uint8_t extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    unsigned dsym = 0;
    while (dsym + 1U < 30U && base[dsym + 1U] <= distance)
        dsym++;
    put_fixed_dist_sym(buffer, bit_offset, dsym);
    if (extra[dsym] > 0)
        put_bits(buffer, bit_offset, distance - base[dsym], extra[dsym]);
}

static void test_invalid_streams(void) {
    printf("[invalid_streams]\n");

    /* Dynamic block whose four code-length symbols all have one-bit codes.
     * Four codes cannot fit in a one-bit code space and must be rejected. */
    uint8_t oversubscribed[8] = {0};
    size_t bit_offset = 0;
    put_bits(oversubscribed, &bit_offset, 1U, 1U); /* BFINAL */
    put_bits(oversubscribed, &bit_offset, 2U, 2U); /* dynamic block */
    put_bits(oversubscribed, &bit_offset, 0U, 5U); /* HLIT = 257 */
    put_bits(oversubscribed, &bit_offset, 0U, 5U); /* HDIST = 1 */
    put_bits(oversubscribed, &bit_offset, 0U, 4U); /* HCLEN = 4 */
    for (int i = 0; i < 4; i++)
        put_bits(oversubscribed, &bit_offset, 1U, 3U);
    uint8_t output[16] = {0};
    size_t output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      oversubscribed, (bit_offset + 7U) / 8U,
                      output, &output_len),
                  -1);

    uint8_t incomplete[8] = {0};
    bit_offset = 0;
    put_bits(incomplete, &bit_offset, 1U, 1U);
    put_bits(incomplete, &bit_offset, 2U, 2U);
    put_bits(incomplete, &bit_offset, 0U, 5U);
    put_bits(incomplete, &bit_offset, 0U, 5U);
    put_bits(incomplete, &bit_offset, 0U, 4U);
    put_bits(incomplete, &bit_offset, 1U, 3U);
    put_bits(incomplete, &bit_offset, 0U, 3U);
    put_bits(incomplete, &bit_offset, 0U, 3U);
    put_bits(incomplete, &bit_offset, 0U, 3U);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      incomplete, (bit_offset + 7U) / 8U,
                      output, &output_len),
                  -1);

    uint8_t reserved_counts[4] = {0};
    bit_offset = 0;
    put_bits(reserved_counts, &bit_offset, 1U, 1U);
    put_bits(reserved_counts, &bit_offset, 2U, 2U);
    put_bits(reserved_counts, &bit_offset, 31U, 5U); /* HLIT = 288 */
    put_bits(reserved_counts, &bit_offset, 0U, 5U);
    put_bits(reserved_counts, &bit_offset, 0U, 4U);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      reserved_counts, (bit_offset + 7U) / 8U,
                      output, &output_len),
                  -1);

    uint8_t stored[64];
    size_t stored_len = sizeof(stored);
    ASSERT_INT_EQ(neverc_flate_compress(
                      (const uint8_t *)"abc", 3U,
                      stored, &stored_len, 0),
                  0);
    uint8_t short_output[2] = {0xaa, 0xbb};
    output_len = sizeof(short_output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      stored, stored_len, short_output, &output_len),
                  -1);
    ASSERT_TRUE(short_output[0] == 0xaa && short_output[1] == 0xbb);

    ASSERT_INT_EQ(neverc_flate_decompress(
                      stored, stored_len, output, NULL),
                  -1);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      NULL, 1U, output, &output_len),
                  -1);

    size_t empty_len = sizeof(stored);
    ASSERT_INT_EQ(neverc_flate_compress(
                      (const uint8_t *)"", 0U,
                      stored, &empty_len, 0),
                  0);
    output_len = 0;
    ASSERT_INT_EQ(neverc_flate_decompress(
                      stored, empty_len, NULL, &output_len),
                  0);
    ASSERT_TRUE(output_len == 0);

    ASSERT_INT_EQ(neverc_flate_compress(
                      (const uint8_t *)"abc", 3U, stored, NULL, 1),
                  -1);
    stored_len = sizeof(stored);
    ASSERT_INT_EQ(neverc_flate_compress(
                      NULL, 1U, stored, &stored_len, 1),
                  -1);
    stored_len = 1;
    ASSERT_INT_EQ(neverc_flate_compress(
                      (const uint8_t *)"abc", 3U, NULL, &stored_len, 1),
                  -1);
#if SIZE_MAX > UINT32_MAX
    stored_len = sizeof(stored);
    ASSERT_INT_EQ(neverc_flate_compress(
                      (const uint8_t *)"x", (size_t)UINT32_MAX + 1U,
                      stored, &stored_len, 1),
                  -1);
#endif

    stored_len = sizeof(stored);
    ASSERT_INT_EQ(neverc_flate_compress(
                      (const uint8_t *)"abc", 3U,
                      stored, &stored_len, 0),
                  0);
    uint8_t padded[64];
    memcpy(padded, stored, stored_len);
    padded[stored_len] = 0;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      padded, stored_len + 1U, output, &output_len),
                  -1);

    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      stored, stored_len - 1U, output, &output_len),
                  -1);

    size_t used = 0;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress_consumed(
                      padded, stored_len + 1U, output, &output_len, &used),
                  0);
    ASSERT_TRUE(used == stored_len);
    ASSERT_TRUE(output_len == 3 && memcmp(output, "abc", 3) == 0);
    ASSERT_INT_EQ(neverc_flate_decompress_consumed(
                      stored, stored_len, output, &output_len, NULL),
                  -1);

    /* RFC 1951: BTYPE=3 is reserved and must fail closed (Go TestStreams). */
    uint8_t reserved_type[1] = {0x07}; /* BFINAL=1, BTYPE=3 */
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      reserved_type, sizeof(reserved_type), output, &output_len),
                  -1);

    /* Stored LEN/NLEN must be one's complements. */
    uint8_t bad_nlen[] = {0x01, 0x01, 0x00, 0xff, 0xff, 0x11};
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      bad_nlen, sizeof(bad_nlen), output, &output_len),
                  -1);

    /* Fixed Huffman reserved length symbol 287 (Go "33180700"). */
    uint8_t reserved_sym[] = {0x33, 0x18, 0x07, 0x00};
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      reserved_sym, sizeof(reserved_sym), output, &output_len),
                  -1);

    /* Go issue 11030: empty distance tree on a literal-only stream is valid. */
    uint8_t empty_hdist[] = {
        0x05, 0xc0, 0x07, 0x06, 0x00, 0x00, 0x00, 0x80,
        0x40, 0x0f, 0xff, 0x37, 0xa0, 0xca
    };
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      empty_hdist, sizeof(empty_hdist), output, &output_len),
                  0);
    ASSERT_TRUE(output_len == 0);
}

static void test_distance_too_far(void) {
    printf("[distance_too_far]\n");
    uint8_t stream[16] = {0};
    size_t bit_offset = 0;
    put_bits(stream, &bit_offset, 1U, 1U); /* BFINAL */
    put_bits(stream, &bit_offset, 1U, 2U); /* fixed Huffman */
    put_fixed_litlen(stream, &bit_offset, 257U); /* length 3 */
    put_fixed_dist_sym(stream, &bit_offset, 0U); /* distance 1 */
    put_fixed_litlen(stream, &bit_offset, 256U); /* EOB */
    uint8_t output[16];
    size_t output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      stream, (bit_offset + 7U) / 8U, output, &output_len),
                  -1);

    /* One literal, then a match that steps before the start of output. */
    memset(stream, 0, sizeof(stream));
    bit_offset = 0;
    put_bits(stream, &bit_offset, 1U, 1U);
    put_bits(stream, &bit_offset, 1U, 2U);
    put_fixed_litlen(stream, &bit_offset, (unsigned)'A');
    put_fixed_litlen(stream, &bit_offset, 257U);
    put_fixed_dist_sym(stream, &bit_offset, 1U); /* distance 2 */
    put_fixed_litlen(stream, &bit_offset, 256U);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      stream, (bit_offset + 7U) / 8U, output, &output_len),
                  -1);

    /* Distance == out_pos is legal (copy from the first emitted byte). */
    memset(stream, 0, sizeof(stream));
    bit_offset = 0;
    put_bits(stream, &bit_offset, 1U, 1U);
    put_bits(stream, &bit_offset, 1U, 2U);
    put_fixed_litlen(stream, &bit_offset, (unsigned)'A');
    put_fixed_litlen(stream, &bit_offset, 257U);
    put_fixed_dist_sym(stream, &bit_offset, 0U); /* distance 1 */
    put_fixed_litlen(stream, &bit_offset, 256U);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_flate_decompress(
                      stream, (bit_offset + 7U) / 8U, output, &output_len),
                  0);
    ASSERT_TRUE(output_len == 4 && output[0] == 'A' &&
                output[1] == 'A' && output[2] == 'A' && output[3] == 'A');

    /* 257 literals + distance 257 is legal at 32 KiB, not at a 256-byte window. */
    uint8_t long_stream[512] = {0};
    bit_offset = 0;
    put_bits(long_stream, &bit_offset, 1U, 1U);
    put_bits(long_stream, &bit_offset, 1U, 2U);
    for (unsigned i = 0; i < 257U; i++)
        put_fixed_litlen(long_stream, &bit_offset, i & 0xFFU);
    put_fixed_litlen(long_stream, &bit_offset, 257U);
    put_fixed_distance(long_stream, &bit_offset, 257U);
    put_fixed_litlen(long_stream, &bit_offset, 256U);
    size_t long_len = (bit_offset + 7U) / 8U;
    uint8_t long_out[300];
    size_t used = 0;
    output_len = sizeof(long_out);
    ASSERT_INT_EQ(neverc_flate_decompress_consumed_window(
                      long_stream, long_len, long_out, &output_len, &used, 256U),
                  -1);
    output_len = sizeof(long_out);
    used = 0;
    ASSERT_INT_EQ(neverc_flate_decompress_consumed_window(
                      long_stream, long_len, long_out, &output_len, &used, 512U),
                  0);
    ASSERT_TRUE(output_len == 260);
    output_len = sizeof(long_out);
    ASSERT_INT_EQ(neverc_flate_decompress_consumed_window(
                      long_stream, long_len, long_out, &output_len, &used, 255U),
                  -1);
}

int main(void) {
    printf("=== NeverC compress/flate Tests ===\n");
    test_empty();
    test_simple();
    test_repetitive();
    test_sequential();
    test_large();
    test_overlap_copy();
    test_stored_blocks();
    test_invalid_streams();
    test_distance_too_far();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
