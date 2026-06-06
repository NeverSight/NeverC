#include "neverc/utf16.h"
#include <stdio.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_i32(const char *name, int32_t got, int32_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got 0x%X, expected 0x%X\n", name, (unsigned)got, (unsigned)expected); }
}

static void test_is_surrogate(void) {
    printf("[is_surrogate]\n");

    check_int("U+0000", neverc_utf16_is_surrogate(0x0000), 0);
    check_int("U+0041 'A'", neverc_utf16_is_surrogate(0x0041), 0);
    check_int("U+D7FF", neverc_utf16_is_surrogate(0xD7FF), 0);
    check_int("U+D800 (high surr start)", neverc_utf16_is_surrogate(0xD800), 1);
    check_int("U+DBFF (high surr end)", neverc_utf16_is_surrogate(0xDBFF), 1);
    check_int("U+DC00 (low surr start)", neverc_utf16_is_surrogate(0xDC00), 1);
    check_int("U+DFFF (low surr end)", neverc_utf16_is_surrogate(0xDFFF), 1);
    check_int("U+E000", neverc_utf16_is_surrogate(0xE000), 0);
    check_int("U+FFFF", neverc_utf16_is_surrogate(0xFFFF), 0);
    check_int("U+10000", neverc_utf16_is_surrogate(0x10000), 0);
    check_int("U+10FFFF", neverc_utf16_is_surrogate(0x10FFFF), 0);
}

static void test_decode_rune(void) {
    printf("[decode_rune]\n");

    /* U+10000 LINEAR B SYLLABLE B008 A = D800 DC00 */
    check_i32("U+10000", neverc_utf16_decode_rune(0xD800, 0xDC00), 0x10000);

    /* U+10FFFF = DBFF DFFF */
    check_i32("U+10FFFF", neverc_utf16_decode_rune(0xDBFF, 0xDFFF), 0x10FFFF);

    /* U+1F600 (GRINNING FACE) = D83D DE00 */
    check_i32("U+1F600", neverc_utf16_decode_rune(0xD83D, 0xDE00), 0x1F600);

    /* U+1D11E (MUSICAL SYMBOL G CLEF) = D834 DD1E */
    check_i32("U+1D11E", neverc_utf16_decode_rune(0xD834, 0xDD1E), 0x1D11E);

    /* Invalid pairs → replacement */
    check_i32("invalid: both low", neverc_utf16_decode_rune(0xDC00, 0xDC00), NEVERC_UTF16_REPLACEMENT_CHAR);
    check_i32("invalid: both high", neverc_utf16_decode_rune(0xD800, 0xD800), NEVERC_UTF16_REPLACEMENT_CHAR);
    check_i32("invalid: reversed", neverc_utf16_decode_rune(0xDC00, 0xD800), NEVERC_UTF16_REPLACEMENT_CHAR);
    check_i32("invalid: non-surr", neverc_utf16_decode_rune(0x0041, 0xDC00), NEVERC_UTF16_REPLACEMENT_CHAR);
}

static void test_encode_rune(void) {
    printf("[encode_rune]\n");

    int32_t r1, r2;

    /* U+10000 */
    neverc_utf16_encode_rune(0x10000, &r1, &r2);
    check_i32("U+10000 high", r1, 0xD800);
    check_i32("U+10000 low", r2, 0xDC00);

    /* U+10FFFF */
    neverc_utf16_encode_rune(0x10FFFF, &r1, &r2);
    check_i32("U+10FFFF high", r1, 0xDBFF);
    check_i32("U+10FFFF low", r2, 0xDFFF);

    /* U+1F600 */
    neverc_utf16_encode_rune(0x1F600, &r1, &r2);
    check_i32("U+1F600 high", r1, 0xD83D);
    check_i32("U+1F600 low", r2, 0xDE00);

    /* BMP characters should return replacement */
    neverc_utf16_encode_rune(0x0041, &r1, &r2);
    check_i32("BMP 'A' r1", r1, NEVERC_UTF16_REPLACEMENT_CHAR);
    check_i32("BMP 'A' r2", r2, NEVERC_UTF16_REPLACEMENT_CHAR);

    /* Out of range */
    neverc_utf16_encode_rune(0x110000, &r1, &r2);
    check_i32("out of range r1", r1, NEVERC_UTF16_REPLACEMENT_CHAR);
    check_i32("out of range r2", r2, NEVERC_UTF16_REPLACEMENT_CHAR);

    /* Round-trip: encode then decode */
    neverc_utf16_encode_rune(0x1D11E, &r1, &r2);
    check_i32("roundtrip U+1D11E", neverc_utf16_decode_rune(r1, r2), 0x1D11E);
}

static void test_rune_len(void) {
    printf("[rune_len]\n");

    check_int("U+0000", neverc_utf16_rune_len(0x0000), 1);
    check_int("U+0041 'A'", neverc_utf16_rune_len(0x0041), 1);
    check_int("U+FFFD", neverc_utf16_rune_len(0xFFFD), 1);
    check_int("U+FFFF", neverc_utf16_rune_len(0xFFFF), 1);
    check_int("U+10000", neverc_utf16_rune_len(0x10000), 2);
    check_int("U+10FFFF", neverc_utf16_rune_len(0x10FFFF), 2);
    check_int("U+1F600", neverc_utf16_rune_len(0x1F600), 2);

    /* Surrogates are invalid → -1 */
    check_int("U+D800 (surr)", neverc_utf16_rune_len(0xD800), -1);
    check_int("U+DFFF (surr)", neverc_utf16_rune_len(0xDFFF), -1);

    /* Out of range */
    check_int("U+110000", neverc_utf16_rune_len(0x110000), -1);
    check_int("negative", neverc_utf16_rune_len(-1), -1);
}

int main(void) {
    printf("=== NeverC UTF-16 Library Tests ===\n\n");

    test_is_surrogate();
    test_decode_rune();
    test_encode_rune();
    test_rune_len();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
