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

int main(void) {
    printf("=== NeverC compress/flate Tests ===\n");
    test_empty();
    test_simple();
    test_repetitive();
    test_sequential();
    test_large();
    test_overlap_copy();
    test_stored_blocks();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
