#include "neverc/std/unicode/utf8.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got U+%04X, expected U+%04X\n", name, got, expected); }
}

static void test_rune_len(void) {
    printf("[rune_len]\n");
    check_int("rune_len('A')", neverc_utf8_rune_len('A'), 1);
    check_int("rune_len(0x7F)", neverc_utf8_rune_len(0x7F), 1);
    check_int("rune_len(0x80)", neverc_utf8_rune_len(0x80), 2);
    check_int("rune_len(0x7FF)", neverc_utf8_rune_len(0x7FF), 2);
    check_int("rune_len(0x800)", neverc_utf8_rune_len(0x800), 3);
    check_int("rune_len(0xFFFF)", neverc_utf8_rune_len(0xFFFF), 3);
    check_int("rune_len(0x10000)", neverc_utf8_rune_len(0x10000), 4);
    check_int("rune_len(0x10FFFF)", neverc_utf8_rune_len(0x10FFFF), 4);
    check_int("rune_len(0x110000)", neverc_utf8_rune_len(0x110000), -1);
    check_int("rune_len(surrogate)", neverc_utf8_rune_len(0xD800), -1);
}

static void test_encode_decode_roundtrip(void) {
    printf("[encode/decode roundtrip]\n");

    uint32_t test_runes[] = {
        'A', 'z', 0x00, 0x7F,
        0x80, 0xFF, 0x7FF,
        0x800, 0x4E16, 0xFFFF,
        0x10000, 0x1F600, 0x10FFFF,
    };
    int n = sizeof(test_runes) / sizeof(test_runes[0]);

    for (int i = 0; i < n; i++) {
        uint32_t r = test_runes[i];
        uint8_t buf[4];
        int enc_len = neverc_utf8_encode_rune(buf, r);

        /* Verify encode returns expected length */
        char name[64];
        snprintf(name, sizeof(name), "encode(U+%04X) len", r);
        check_int(name, enc_len, neverc_utf8_rune_len(r));

        /* Decode back */
        uint32_t decoded; int dec_len;
        neverc_utf8_decode_rune(buf, (size_t)enc_len, &decoded, &dec_len);
        snprintf(name, sizeof(name), "decode(encode(U+%04X)) rune", r);
        check_u32(name, decoded, r);
        snprintf(name, sizeof(name), "decode(encode(U+%04X)) len", r);
        check_int(name, dec_len, enc_len);
    }
}

static void test_decode_specific(void) {
    printf("[decode specific]\n");

    /* ASCII 'H' = 0x48 */
    {
        uint8_t b[] = { 0x48 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 1, &r, &sz);
        check_u32("decode 'H'", r, 'H');
        check_int("decode 'H' size", sz, 1);
    }

    /* U+00E9 (é) = C3 A9 */
    {
        uint8_t b[] = { 0xC3, 0xA9 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 2, &r, &sz);
        check_u32("decode U+00E9", r, 0x00E9);
        check_int("decode U+00E9 size", sz, 2);
    }

    /* U+4E16 (世) = E4 B8 96 */
    {
        uint8_t b[] = { 0xE4, 0xB8, 0x96 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 3, &r, &sz);
        check_u32("decode U+4E16", r, 0x4E16);
        check_int("decode U+4E16 size", sz, 3);
    }

    /* U+1F600 (😀) = F0 9F 98 80 */
    {
        uint8_t b[] = { 0xF0, 0x9F, 0x98, 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 4, &r, &sz);
        check_u32("decode U+1F600", r, 0x1F600);
        check_int("decode U+1F600 size", sz, 4);
    }

    /* Invalid: continuation byte alone */
    {
        uint8_t b[] = { 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 1, &r, &sz);
        check_u32("decode invalid 0x80", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode invalid 0x80 size", sz, 1);
    }

    /* Invalid: truncated 2-byte */
    {
        uint8_t b[] = { 0xC3 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 1, &r, &sz);
        check_u32("decode truncated 2b", r, NEVERC_UTF8_RUNE_ERROR);
    }

    /* Empty input */
    {
        uint32_t r; int sz;
        neverc_utf8_decode_rune((const uint8_t *)"", 0, &r, &sz);
        check_u32("decode empty", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode empty size", sz, 0);
    }
}

static void test_rune_count(void) {
    printf("[rune_count]\n");
    check_size("count ''", neverc_utf8_rune_count((const uint8_t *)"", 0), 0);
    check_size("count 'Hello'", neverc_utf8_rune_count((const uint8_t *)"Hello", 5), 5);

    /* "世界" = E4B896 E7958C = 6 bytes, 2 runes */
    uint8_t shijie[] = { 0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C };
    check_size("count '世界'", neverc_utf8_rune_count(shijie, 6), 2);

    /* "Hello, 世界!" = H,e,l,l,o,',',SP,世,界,! = 10 runes, 14 bytes */
    uint8_t mixed[] = { 'H','e','l','l','o',',',' ',
                        0xE4,0xB8,0x96, 0xE7,0x95,0x8C, '!' };
    check_size("count mixed", neverc_utf8_rune_count(mixed, 14), 10);
}

static void test_valid(void) {
    printf("[valid]\n");
    check_int("valid ''", neverc_utf8_valid((const uint8_t *)"", 0), 1);
    check_int("valid 'Hello'", neverc_utf8_valid((const uint8_t *)"Hello", 5), 1);

    uint8_t shijie[] = { 0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C };
    check_int("valid '世界'", neverc_utf8_valid(shijie, 6), 1);

    uint8_t emoji[] = { 0xF0, 0x9F, 0x98, 0x80 };
    check_int("valid emoji", neverc_utf8_valid(emoji, 4), 1);

    /* Invalid: lone continuation */
    uint8_t inv1[] = { 0x80 };
    check_int("invalid continuation", neverc_utf8_valid(inv1, 1), 0);

    /* Invalid: overlong encoding (C0 80 for U+0000) */
    uint8_t inv2[] = { 0xC0, 0x80 };
    check_int("invalid overlong", neverc_utf8_valid(inv2, 2), 0);

    /* Invalid: truncated */
    uint8_t inv3[] = { 0xE4, 0xB8 };
    check_int("invalid truncated", neverc_utf8_valid(inv3, 2), 0);

    /* Invalid: surrogate half (ED A0 80 = U+D800) */
    uint8_t inv4[] = { 0xED, 0xA0, 0x80 };
    check_int("invalid surrogate", neverc_utf8_valid(inv4, 3), 0);
}

static void test_rune_start(void) {
    printf("[rune_start]\n");
    check_int("start 'A'", neverc_utf8_rune_start('A'), 1);
    check_int("start 0x00", neverc_utf8_rune_start(0x00), 1);
    check_int("start 0xC0", neverc_utf8_rune_start(0xC0), 1);
    check_int("start 0xE0", neverc_utf8_rune_start(0xE0), 1);
    check_int("start 0xF0", neverc_utf8_rune_start(0xF0), 1);
    check_int("start 0x80 (continuation)", neverc_utf8_rune_start(0x80), 0);
    check_int("start 0xBF (continuation)", neverc_utf8_rune_start(0xBF), 0);
}

static void test_valid_rune(void) {
    printf("[valid_rune]\n");
    check_int("valid_rune 'A'", neverc_utf8_valid_rune('A'), 1);
    check_int("valid_rune 0", neverc_utf8_valid_rune(0), 1);
    check_int("valid_rune 0x10FFFF", neverc_utf8_valid_rune(0x10FFFF), 1);
    check_int("valid_rune 0x110000", neverc_utf8_valid_rune(0x110000), 0);
    check_int("valid_rune 0xD800", neverc_utf8_valid_rune(0xD800), 0);
    check_int("valid_rune 0xDFFF", neverc_utf8_valid_rune(0xDFFF), 0);
}

static void test_encode_surrogate(void) {
    printf("[encode surrogate]\n");
    /* Encoding a surrogate should produce RuneError (U+FFFD) */
    uint8_t buf[4];
    int len = neverc_utf8_encode_rune(buf, 0xD800);
    check_int("encode surrogate len", len, 3);
    uint32_t r; int sz;
    neverc_utf8_decode_rune(buf, (size_t)len, &r, &sz);
    check_u32("encode surrogate -> RuneError", r, NEVERC_UTF8_RUNE_ERROR);
}

int main(void) {
    printf("=== NeverC UTF-8 Library Tests ===\n\n");

    test_rune_len();
    test_encode_decode_roundtrip();
    test_decode_specific();
    test_rune_count();
    test_valid();
    test_rune_start();
    test_valid_rune();
    test_encode_surrogate();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
