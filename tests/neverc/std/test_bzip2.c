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

/* "AAAA" compressed with block size 1 ("BZh1"). */
static const uint8_t bz2_aaaa[] = {
    0x42, 0x5a, 0x68, 0x31, 0x31, 0x41, 0x59, 0x26,
    0x53, 0x59, 0xe1, 0x6e, 0x65, 0x71, 0x00, 0x00,
    0x02, 0x44, 0x00, 0x40, 0x00, 0x20, 0x00, 0x20,
    0x00, 0x21, 0x00, 0x82, 0x0b, 0x17, 0x72, 0x45,
    0x38, 0x50, 0x90, 0xe1, 0x6e, 0x65, 0x71
};

/* A stream without blocks: header, end-of-stream magic, combined CRC 0. */
static const uint8_t bz2_empty[] = {
    0x42, 0x5a, 0x68, 0x39, 0x17, 0x72, 0x45, 0x38,
    0x50, 0x90, 0x00, 0x00, 0x00, 0x00
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

static void test_rle_run_requires_count_byte(void) {
    printf("[rle_run_requires_count_byte]\n");
    /* Four repeated bytes are followed by a mandatory count byte, even when
     * the count is zero. Both streams carry the correct CRC for "AAAA". */
    static const uint8_t missing_count[] = {
        0x42, 0x5a, 0x68, 0x31, 0x31, 0x41, 0x59, 0x26,
        0x53, 0x59, 0xe1, 0x6e, 0x65, 0x71, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x20, 0x00, 0x20, 0x00, 0x21,
        0x01, 0x04, 0x85, 0xdc, 0x91, 0x4e, 0x14, 0x24,
        0x38, 0x5b, 0x99, 0x5c, 0x40
    };
    uint8_t out[8];
    size_t out_len = sizeof(out);

    int rc = neverc_bzip2_decompress(bz2_aaaa, sizeof(bz2_aaaa), out, &out_len);
    ASSERT_INT_EQ(rc, 0);
    if (rc == 0) {
        ASSERT_INT_EQ((int)out_len, 4);
        ASSERT_TRUE(memcmp(out, "AAAA", 4) == 0);
    }

    out_len = sizeof(out);
    ASSERT_INT_EQ(neverc_bzip2_decompress(
                      missing_count, sizeof(missing_count), out, &out_len),
                  -1);
}

static unsigned bz2_get_bit(const uint8_t *buf, size_t bit) {
    return (buf[bit >> 3] >> (7u - (bit & 7u))) & 1u;
}

static void bz2_put_bit(uint8_t *buf, size_t *bit, unsigned value) {
    if (value) buf[*bit >> 3] |= (uint8_t)(0x80u >> (*bit & 7u));
    (*bit)++;
}

static uint32_t bz2_copy_bits(uint8_t *out, size_t *wr, size_t *rd,
                              unsigned count) {
    uint32_t value = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned bit = bz2_get_bit(bz2_hello, (*rd)++);
        value = (value << 1) | bit;
        bz2_put_bit(out, wr, bit);
    }
    return value;
}

/* Re-encode bz2_hello with its block declaring `selectors` selectors. The
 * surplus entries are MTF index 0 and lie past the last symbol group, so
 * they are never referenced. Returns the new length, 0 on failure. */
static size_t bz2_hello_with_selectors(uint8_t *out, size_t cap,
                                       unsigned selectors) {
    size_t end = 0;
    /* Payload ends with the 48-bit end-of-stream magic and the 32-bit
     * combined CRC; at most 7 padding bits follow. */
    for (unsigned pad = 0; pad < 8 && end == 0; pad++) {
        size_t at = sizeof(bz2_hello) * 8u - pad - 80u;
        uint64_t magic = 0;
        for (unsigned i = 0; i < 48u; i++)
            magic = (magic << 1) | bz2_get_bit(bz2_hello, at + i);
        if (magic == UINT64_C(0x177245385090)) end = at + 80u;
    }
    if (end == 0 || sizeof(bz2_hello) + selectors / 8u + 2u > cap) return 0;

    size_t rd = 0, wr = 0;
    memset(out, 0, cap);
    /* Stream header, block magic, block CRC, randomized bit, origPtr. */
    bz2_copy_bits(out, &wr, &rd, 32u + 48u + 32u + 1u + 24u);
    uint32_t ranges = bz2_copy_bits(out, &wr, &rd, 16u);
    for (unsigned i = 0; i < 16u; i++)
        if (ranges & (1u << i)) bz2_copy_bits(out, &wr, &rd, 16u);
    bz2_copy_bits(out, &wr, &rd, 3u); /* number of Huffman groups */

    uint32_t declared = 0;
    for (unsigned i = 0; i < 15u; i++)
        declared = (declared << 1) | bz2_get_bit(bz2_hello, rd++);
    for (unsigned i = 15u; i > 0; i--)
        bz2_put_bit(out, &wr, (selectors >> (i - 1u)) & 1u);
    for (uint32_t s = 0; s < declared; s++) {
        unsigned bit;
        do {
            bit = bz2_get_bit(bz2_hello, rd++);
            if (s < selectors) bz2_put_bit(out, &wr, bit);
        } while (bit);
    }
    for (uint32_t s = declared; s < selectors; s++)
        bz2_put_bit(out, &wr, 0);
    while (rd < end) bz2_put_bit(out, &wr, bz2_get_bit(bz2_hello, rd++));
    return (wr + 7u) / 8u;
}

static void test_surplus_selectors(void) {
    printf("[surplus_selectors]\n");
    /* The 15-bit selector count may exceed the 18002 groups a 900k block
     * can use; Go compress/bzip2 reads and ignores the surplus. */
    static uint8_t stream[sizeof(bz2_hello) + 32768u / 8u + 8u];
    static const unsigned counts[] = {18002u, 18003u, 20000u, 32767u};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        size_t len = bz2_hello_with_selectors(stream, sizeof(stream),
                                              counts[i]);
        ASSERT_TRUE(len > 0);
        uint8_t out[64];
        size_t out_len = sizeof(out);
        int rc = neverc_bzip2_decompress(stream, len, out, &out_len);
        ASSERT_INT_EQ(rc, 0);
        if (rc == 0) {
            ASSERT_INT_EQ((int)out_len, 13);
            ASSERT_TRUE(memcmp(out, "Hello, World!", 13) == 0);
        }
    }

    /* A block still needs at least one selector. */
    size_t len = bz2_hello_with_selectors(stream, sizeof(stream), 0);
    ASSERT_TRUE(len > 0);
    uint8_t out[64];
    size_t out_len = sizeof(out);
    ASSERT_INT_EQ(neverc_bzip2_decompress(stream, len, out, &out_len), -1);
}

static size_t bz2_cat(uint8_t *buf, size_t len, const void *p, size_t n) {
    memcpy(buf + len, p, n);
    return len + n;
}

/* Decodes `in` with the multi-stream API and checks the result against the
 * expected text, or expects -1 (leaving *dst_len alone) when want is NULL. */
static void bz2_check_multi(const uint8_t *in, size_t in_len,
                            const char *want) {
    uint8_t out[64];
    size_t out_len = sizeof(out);
    int rc = neverc_bzip2_decompress_multistream(in, in_len, out, &out_len);
    if (!want) {
        ASSERT_INT_EQ(rc, -1);
        ASSERT_INT_EQ((int)out_len, (int)sizeof(out));
        return;
    }
    ASSERT_INT_EQ(rc, 0);
    if (rc == 0) {
        ASSERT_INT_EQ((int)out_len, (int)strlen(want));
        ASSERT_TRUE(memcmp(out, want, strlen(want)) == 0);
    }
}

static void test_multistream(void) {
    printf("[multistream]\n");
    uint8_t in[256];
    size_t n;

    /* A single stream decodes as with neverc_bzip2_decompress. */
    bz2_check_multi(bz2_hello, sizeof(bz2_hello), "Hello, World!");
    bz2_check_multi(bz2_empty, sizeof(bz2_empty), "");

    /* Streams with different block sizes, in both orders (the second order
     * grows the block buffer), with an empty stream between, and repeated. */
    n = bz2_cat(in, 0, bz2_hello, sizeof(bz2_hello));
    n = bz2_cat(in, n, bz2_aaaa, sizeof(bz2_aaaa));
    bz2_check_multi(in, n, "Hello, World!AAAA");
    uint8_t out[64];
    size_t out_len = sizeof(out);
    ASSERT_INT_EQ(neverc_bzip2_decompress(in, n, out, &out_len), -1);

    n = bz2_cat(in, 0, bz2_aaaa, sizeof(bz2_aaaa));
    n = bz2_cat(in, n, bz2_hello, sizeof(bz2_hello));
    bz2_check_multi(in, n, "AAAAHello, World!");

    n = bz2_cat(in, 0, bz2_hello, sizeof(bz2_hello));
    n = bz2_cat(in, n, bz2_empty, sizeof(bz2_empty));
    n = bz2_cat(in, n, bz2_aaaa, sizeof(bz2_aaaa));
    n = bz2_cat(in, n, bz2_aaaa, sizeof(bz2_aaaa));
    bz2_check_multi(in, n, "Hello, World!AAAAAAAA");

    /* The output buffer bounds all streams together. */
    n = bz2_cat(in, 0, bz2_hello, sizeof(bz2_hello));
    n = bz2_cat(in, n, bz2_aaaa, sizeof(bz2_aaaa));
    out_len = 17;
    ASSERT_INT_EQ(neverc_bzip2_decompress_multistream(in, n, out, &out_len), 0);
    ASSERT_INT_EQ((int)out_len, 17);
    out_len = 16;
    ASSERT_INT_EQ(neverc_bzip2_decompress_multistream(in, n, out, &out_len), -1);
    ASSERT_INT_EQ((int)out_len, 16);

    /* Anything after a stream must be a complete stream. */
    static const struct { const char *bytes; size_t len; } junk[] = {
        {"\0", 1}, {"B", 1}, {"\0\0", 2}, {"BZ", 2}, {"BZh", 3},
        {"BZh0", 4}, {"BZh9", 4}, {"BZh9junkjunk", 12}
    };
    for (size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) {
        n = bz2_cat(in, 0, bz2_hello, sizeof(bz2_hello));
        n = bz2_cat(in, n, junk[i].bytes, junk[i].len);
        bz2_check_multi(in, n, NULL);
    }
    n = bz2_cat(in, 0, bz2_hello, sizeof(bz2_hello));
    n = bz2_cat(in, n, bz2_aaaa, sizeof(bz2_aaaa) - 1);
    bz2_check_multi(in, n, NULL);

    /* Every stream's block CRC and combined CRC are verified. */
    n = bz2_cat(in, 0, bz2_hello, sizeof(bz2_hello));
    n = bz2_cat(in, n, bz2_aaaa, sizeof(bz2_aaaa));
    in[n - 1] ^= 1;
    bz2_check_multi(in, n, NULL);
    in[n - 1] ^= 1;
    in[sizeof(bz2_hello) + 10] ^= 1;
    bz2_check_multi(in, n, NULL);

    bz2_check_multi(in, 0, NULL);
    ASSERT_TRUE(neverc_bzip2_decompress_multistream(
                    bz2_hello, sizeof(bz2_hello), out, NULL) != 0);
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
    test_rle_run_requires_count_byte();
    test_surplus_selectors();
    test_multistream();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
