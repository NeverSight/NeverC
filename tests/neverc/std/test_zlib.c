/*
 * NeverC compress/zlib tests.
 */
#include "neverc/std/compress/zlib.h"
#include "neverc/std/hash/adler32.h"
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

    /* Adler-32 of 0 is a real checksum, not a skip / fail-open. */
    memcpy(invalid, compressed, valid_compressed_len);
    invalid[valid_compressed_len - 4] = 0;
    invalid[valid_compressed_len - 3] = 0;
    invalid[valid_compressed_len - 2] = 0;
    invalid[valid_compressed_len - 1] = 0;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      invalid, valid_compressed_len, output, &output_len),
                  -1);

    memcpy(invalid, compressed, valid_compressed_len);
    invalid[1] |= 0x20; /* FDICT */
    for (unsigned flag = 0; flag < 32; flag++) {
        uint8_t flg = (uint8_t)(flag | 0x20U);
        if ((((unsigned)invalid[0] * 256U + flg) % 31U) == 0) {
            invalid[1] = flg;
            break;
        }
    }
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      invalid, valid_compressed_len, output, &output_len),
                  -1);

    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      compressed, valid_compressed_len - 1U, output, &output_len),
                  -1);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      compressed, 2U, output, &output_len),
                  -1);

    uint8_t extra_after[256];
    memcpy(extra_after, compressed, valid_compressed_len);
    extra_after[valid_compressed_len] = 0;
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(
                      extra_after, valid_compressed_len + 1U, output, &output_len),
                  -1);
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
    uint32_t rev = 0;
    while (dsym + 1U < 30U && base[dsym + 1U] <= distance)
        dsym++;
    for (unsigned i = 0; i < 5U; i++)
        rev |= ((dsym >> i) & 1U) << (4U - i);
    put_bits(buffer, bit_offset, rev, 5U);
    if (extra[dsym] > 0)
        put_bits(buffer, bit_offset, distance - base[dsym], extra[dsym]);
}

static int wrap_zlib(uint8_t *z, size_t *z_len, unsigned cinfo,
                     const uint8_t *raw, size_t raw_len,
                     const uint8_t *plain, size_t plain_len) {
    if (*z_len < raw_len + 6U) return -1;
    uint8_t cmf = (uint8_t)((cinfo << 4) | 8U);
    uint8_t flg = 0;
    for (unsigned flag = 0; flag < 32U; flag++) {
        if (flag & 0x20U) continue;
        if ((((unsigned)cmf * 256U + flag) % 31U) == 0) {
            flg = (uint8_t)flag;
            break;
        }
    }
    z[0] = cmf;
    z[1] = flg;
    memcpy(z + 2, raw, raw_len);
    uint32_t adler = neverc_adler32_checksum(plain, plain_len);
    z[2 + raw_len + 0] = (uint8_t)(adler >> 24);
    z[2 + raw_len + 1] = (uint8_t)(adler >> 16);
    z[2 + raw_len + 2] = (uint8_t)(adler >> 8);
    z[2 + raw_len + 3] = (uint8_t)adler;
    *z_len = raw_len + 6U;
    return 0;
}

static void test_window_bits(void) {
    printf("[zlib window bits]\n");
    /* 257 literals + a distance-257 match: legal in a 512-byte window,
     * illegal in the 256-byte window advertised by CINFO=0. */
    uint8_t raw[512] = {0};
    size_t bit_offset = 0;
    put_bits(raw, &bit_offset, 1U, 1U);
    put_bits(raw, &bit_offset, 1U, 2U);
    for (unsigned i = 0; i < 257U; i++)
        put_fixed_litlen(raw, &bit_offset, i & 0xFFU);
    put_fixed_litlen(raw, &bit_offset, 257U);
    put_fixed_distance(raw, &bit_offset, 257U);
    put_fixed_litlen(raw, &bit_offset, 256U);
    size_t raw_len = (bit_offset + 7U) / 8U;

    uint8_t plain[260];
    for (unsigned i = 0; i < 257U; i++)
        plain[i] = (uint8_t)(i & 0xFFU);
    plain[257] = 0;
    plain[258] = 1;
    plain[259] = 2;

    uint8_t z[520], output[300];
    size_t z_len = sizeof(z);
    ASSERT_INT_EQ(wrap_zlib(z, &z_len, 0, raw, raw_len, plain, 260), 0);
    size_t output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(z, z_len, output, &output_len), -1);

    z_len = sizeof(z);
    ASSERT_INT_EQ(wrap_zlib(z, &z_len, 1, raw, raw_len, plain, 260), 0);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(z, z_len, output, &output_len), 0);
    ASSERT_TRUE(output_len == 260 && memcmp(output, plain, 260) == 0);

    z_len = sizeof(z);
    ASSERT_INT_EQ(wrap_zlib(z, &z_len, 7, raw, raw_len, plain, 260), 0);
    output_len = sizeof(output);
    ASSERT_INT_EQ(neverc_zlib_decompress(z, z_len, output, &output_len), 0);
    ASSERT_TRUE(output_len == 260 && memcmp(output, plain, 260) == 0);
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
    test_window_bits();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
