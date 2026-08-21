/*
 * NeverC compress/bzip2 tests.
 * Tests bzip2 decompression with known compressed data.
 * bzip2 is decompress-only (mirrors Go compress/bzip2).
 */
#include "neverc/std/compress/bzip2.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = %d, expected %d\n", __LINE__, #expr, _v, _e); } \
} while(0)

#define ASSERT_TRUE(expr) ASSERT_INT_EQ(!!(expr), 1)

/*
 * Pre-compressed bzip2 data for "Hello, World!\n"
 * Generated with: echo -n "Hello, World!" | bzip2 | xxd -i
 */
static const uint8_t bz2_hello[] = {
    0x42, 0x5a, 0x68, 0x39, 0x31, 0x41, 0x59, 0x26,
    0x53, 0x59, 0xe6, 0xd8, 0xfe, 0xdf, 0x00, 0x00,
    0x01, 0x97, 0x80, 0x60, 0x04, 0x00, 0x40, 0x00,
    0x80, 0x06, 0x04, 0x90, 0x00, 0x20, 0x00, 0x22,
    0x03, 0x23, 0x21, 0x00, 0x30, 0xb2, 0x80, 0x5a,
    0xde, 0x43, 0xef, 0x17, 0x72, 0x45, 0x38, 0x50,
    0x90, 0xe6, 0xd8, 0xfe, 0xdf
};

static void test_hello_decompress(void) {
    printf("[hello_decompress]\n");
    uint8_t out[256];
    size_t out_len = sizeof(out);

    int rc = neverc_bzip2_decompress(bz2_hello, sizeof(bz2_hello), out, &out_len);
    ASSERT_INT_EQ(rc, 0);
    if (rc == 0) {
        ASSERT_INT_EQ((int)out_len, 13);
        out[out_len] = '\0';
        tests_run++;
        if (memcmp(out, "Hello, World!", 13) == 0) tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: got \"%.*s\", expected \"Hello, World!\"\n",
                   (int)out_len, out);
        }
    }
}

static void test_invalid_magic(void) {
    printf("[invalid_magic]\n");
    uint8_t bad[] = {0x00, 0x01, 0x02, 0x03};
    uint8_t out[256];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(bad, sizeof(bad), out, &out_len) != 0);
}

static void test_truncated(void) {
    printf("[truncated]\n");
    uint8_t out[256];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(bz2_hello, 4, out, &out_len) != 0);
    out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    bz2_hello, sizeof(bz2_hello) - 1, out, &out_len) != 0);
}

static void test_bad_block_size(void) {
    printf("[bad_block_size]\n");
    uint8_t bad[4] = {'B', 'Z', 'h', '0'};
    uint8_t out[256];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(bad, sizeof(bad), out, &out_len) != 0);
}

static void test_empty_output(void) {
    printf("[empty_output]\n");
    uint8_t out[1];
    size_t out_len = 0;
    ASSERT_TRUE(neverc_bzip2_decompress(bz2_hello, sizeof(bz2_hello), out, &out_len) != 0);
}

static void test_crc_mismatch(void) {
    printf("[crc_mismatch]\n");
    uint8_t corrupted[sizeof(bz2_hello)];
    uint8_t out[256];
    size_t out_len;

    memcpy(corrupted, bz2_hello, sizeof(corrupted));
    corrupted[10] ^= 1; /* Stored block CRC follows the six-byte block marker. */
    out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    corrupted, sizeof(corrupted), out, &out_len) != 0);

    memcpy(corrupted, bz2_hello, sizeof(corrupted));
    corrupted[49] ^= 1; /* Stored combined CRC follows the end marker. */
    out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    corrupted, sizeof(corrupted), out, &out_len) != 0);
}

static void test_invalid_spans(void) {
    printf("[invalid_spans]\n");
    uint8_t out[16];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    bz2_hello, sizeof(bz2_hello), out, NULL) != 0);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    NULL, sizeof(bz2_hello), out, &out_len) != 0);
}

static void test_randomized_block(void) {
    printf("[randomized_block]\n");
    uint8_t randomized[sizeof(bz2_hello)];
    memcpy(randomized, bz2_hello, sizeof(randomized));
    /* After BZh9 + 1AY&SY + 32-bit block CRC, the next bit is RAND. */
    randomized[14] |= 0x80;
    uint8_t out[256];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    randomized, sizeof(randomized), out, &out_len) != 0);
}

static void test_leftover_bytes(void) {
    printf("[leftover_bytes]\n");
    uint8_t extra[sizeof(bz2_hello) + 1];
    memcpy(extra, bz2_hello, sizeof(bz2_hello));
    extra[sizeof(bz2_hello)] = 0x00;
    uint8_t out[64];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(neverc_bzip2_decompress(
                    extra, sizeof(extra), out, &out_len) != 0);
}

int main(void) {
    printf("=== NeverC bzip2 Tests ===\n");
    test_hello_decompress();
    test_invalid_magic();
    test_truncated();
    test_bad_block_size();
    test_empty_output();
    test_crc_mismatch();
    test_randomized_block();
    test_invalid_spans();
    test_leftover_bytes();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
