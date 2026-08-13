#include "neverc/std/unicode.h"
#include <stdio.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got U+%04X, expected U+%04X\n", name, got, expected); }
}

static void test_ascii_classification(void) {
    printf("[ASCII classification]\n");

    /* Letters */
    for (uint32_t c = 'A'; c <= 'Z'; c++) {
        check_int("is_upper A-Z", neverc_unicode_is_upper(c), 1);
        check_int("is_letter A-Z", neverc_unicode_is_letter(c), 1);
    }
    for (uint32_t c = 'a'; c <= 'z'; c++) {
        check_int("is_lower a-z", neverc_unicode_is_lower(c), 1);
        check_int("is_letter a-z", neverc_unicode_is_letter(c), 1);
    }

    /* Digits */
    for (uint32_t c = '0'; c <= '9'; c++) {
        check_int("is_digit 0-9", neverc_unicode_is_digit(c), 1);
    }

    /* Negatives */
    check_int("!is_upper('a')", neverc_unicode_is_upper('a'), 0);
    check_int("!is_lower('A')", neverc_unicode_is_lower('A'), 0);
    check_int("!is_digit('a')", neverc_unicode_is_digit('a'), 0);
    check_int("!is_letter('1')", neverc_unicode_is_letter('1'), 0);
}

static void test_space(void) {
    printf("[space]\n");
    check_int("space(' ')", neverc_unicode_is_space(' '), 1);
    check_int("space('\\t')", neverc_unicode_is_space('\t'), 1);
    check_int("space('\\n')", neverc_unicode_is_space('\n'), 1);
    check_int("space('\\r')", neverc_unicode_is_space('\r'), 1);
    check_int("space(NBSP)", neverc_unicode_is_space(0xA0), 1);
    check_int("space(IDEOGRAPHIC)", neverc_unicode_is_space(0x3000), 1);
    check_int("!space('a')", neverc_unicode_is_space('a'), 0);
    check_int("!space('0')", neverc_unicode_is_space('0'), 0);
}

static void test_punct(void) {
    printf("[punct]\n");
    check_int("punct('!')", neverc_unicode_is_punct('!'), 1);
    check_int("punct('.')", neverc_unicode_is_punct('.'), 1);
    check_int("punct(',')", neverc_unicode_is_punct(','), 1);
    check_int("punct(';')", neverc_unicode_is_punct(';'), 1);
    check_int("punct('(')", neverc_unicode_is_punct('('), 1);
    check_int("!punct('a')", neverc_unicode_is_punct('a'), 0);
    check_int("!punct('0')", neverc_unicode_is_punct('0'), 0);
}

static void test_control(void) {
    printf("[control]\n");
    check_int("control(0x00)", neverc_unicode_is_control(0x00), 1);
    check_int("control(0x1F)", neverc_unicode_is_control(0x1F), 1);
    check_int("control(0x7F)", neverc_unicode_is_control(0x7F), 1);
    check_int("control(0x9F)", neverc_unicode_is_control(0x9F), 1);
    check_int("!control(' ')", neverc_unicode_is_control(' '), 0);
    check_int("!control('A')", neverc_unicode_is_control('A'), 0);
}

static void test_case_conversion(void) {
    printf("[case conversion]\n");
    check_u32("to_upper('a')", neverc_unicode_to_upper('a'), 'A');
    check_u32("to_upper('z')", neverc_unicode_to_upper('z'), 'Z');
    check_u32("to_upper('A')", neverc_unicode_to_upper('A'), 'A');
    check_u32("to_upper('1')", neverc_unicode_to_upper('1'), '1');

    check_u32("to_lower('A')", neverc_unicode_to_lower('A'), 'a');
    check_u32("to_lower('Z')", neverc_unicode_to_lower('Z'), 'z');
    check_u32("to_lower('a')", neverc_unicode_to_lower('a'), 'a');
    check_u32("to_title('a')", neverc_unicode_to_title('a'), 'A');
    check_u32("to_title(Dz)", neverc_unicode_to_title(0x01C6), 0x01C5);

    /* Latin-1 */
    check_u32("to_upper(à)", neverc_unicode_to_upper(0xE0), 0xC0);
    check_u32("to_lower(À)", neverc_unicode_to_lower(0xC0), 0xE0);

    /* Greek */
    check_u32("to_upper(α)", neverc_unicode_to_upper(0x3B1), 0x391);
    check_u32("to_lower(Α)", neverc_unicode_to_lower(0x391), 0x3B1);

    /* Cyrillic */
    check_u32("to_upper(а)", neverc_unicode_to_upper(0x430), 0x410);
    check_u32("to_lower(А)", neverc_unicode_to_lower(0x410), 0x430);

    /* Round-trip for ASCII */
    for (uint32_t c = 'A'; c <= 'Z'; c++) {
        check_u32("lower(upper(x))==lower",
            neverc_unicode_to_lower(neverc_unicode_to_upper(c + 32)), c + 32);
    }
}

static void test_unicode_beyond_ascii(void) {
    printf("[beyond ASCII]\n");
    /* CJK */
    check_int("is_letter(中)", neverc_unicode_is_letter(0x4E2D), 1);
    check_int("is_letter(世)", neverc_unicode_is_letter(0x4E16), 1);
    /* Hiragana */
    check_int("is_letter(あ)", neverc_unicode_is_letter(0x3042), 1);
    /* Arabic */
    check_int("is_letter(ب)", neverc_unicode_is_letter(0x0628), 1);
    /* Full-width digit */
    check_int("is_digit(０)", neverc_unicode_is_digit(0xFF10), 1);
    /* Print */
    check_int("is_print(' ')", neverc_unicode_is_print(' '), 1);
    check_int("is_print('A')", neverc_unicode_is_print('A'), 1);
    check_int("!is_print(0x00)", neverc_unicode_is_print(0x00), 0);
    check_int("!is_print(0x7F)", neverc_unicode_is_print(0x7F), 0);
}

static void test_new_classification(void) {
    printf("[new classification]\n");
    check_int("is_number('0')", neverc_unicode_is_number('0'), 1);
    check_int("is_number('9')", neverc_unicode_is_number('9'), 1);
    check_int("is_number('A')", neverc_unicode_is_number('A'), 0);
    check_int("is_number(Arabic-0)", neverc_unicode_is_number(0x0660), 1);
    check_int("is_number(superscript2)", neverc_unicode_is_number(0x00B2), 1);

    check_int("is_symbol('$')", neverc_unicode_is_symbol('$'), 1);
    check_int("is_symbol('+')", neverc_unicode_is_symbol('+'), 1);
    check_int("is_symbol('A')", neverc_unicode_is_symbol('A'), 0);
    check_int("is_symbol(copyright)", neverc_unicode_is_symbol(0x00A9), 1);
    check_int("is_symbol(arrow)", neverc_unicode_is_symbol(0x2192), 1);

    check_int("is_title(Dz)", neverc_unicode_is_title(0x01C5), 1);
    check_int("is_title(A)", neverc_unicode_is_title('A'), 0);
    check_int("is_title(a)", neverc_unicode_is_title('a'), 0);

    check_int("is_mark(combining_acute)", neverc_unicode_is_mark(0x0301), 1);
    check_int("is_mark(A)", neverc_unicode_is_mark('A'), 0);
}

static void test_simple_fold(void) {
    printf("[simple_fold]\n");
    check_int("fold('A')==a", (int)neverc_unicode_simple_fold('A'), (int)'a');
    check_int("fold('a')==A", (int)neverc_unicode_simple_fold('a'), (int)'A');
    check_int("fold('0')==0", (int)neverc_unicode_simple_fold('0'), (int)'0');
}

static void test_unicode_conformance_edges(void) {
    printf("[Unicode conformance edges]\n");

    check_int("NBSP is not print", neverc_unicode_is_print(0x00A0), 0);
    check_int("soft hyphen is not print",
              neverc_unicode_is_print(0x00AD), 0);
    check_int("NBSP is graphic", neverc_unicode_is_graphic(0x00A0), 1);
    check_int("soft hyphen is not graphic",
              neverc_unicode_is_graphic(0x00AD), 0);
    check_int("feminine ordinal is letter",
              neverc_unicode_is_letter(0x00AA), 1);
    check_int("Devanagari sign is not letter",
              neverc_unicode_is_letter(0x0900), 0);
    check_int("Devanagari sign is mark",
              neverc_unicode_is_mark(0x0900), 1);
    check_int("Hiragana voiced mark is print",
              neverc_unicode_is_print(0x3099), 1);
    check_int("Katakana middle dot is print",
              neverc_unicode_is_print(0x30FB), 1);
    check_int("Thai character MAI HAN-AKAT is print",
              neverc_unicode_is_print(0x0E31), 1);
    check_int("Devanagari vocalic L mark is print",
              neverc_unicode_is_print(0x0962), 1);
    check_int("titlecase DZ is letter and print",
              neverc_unicode_is_letter(0x01C5) &&
              neverc_unicode_is_print(0x01C5), 1);

    check_u32("micro sign uppercase",
              neverc_unicode_to_upper(0x00B5), 0x039C);
    check_u32("capital I with dot lowercase",
              neverc_unicode_to_lower(0x0130), 0x0069);
    check_u32("dotless i uppercase",
              neverc_unicode_to_upper(0x0131), 0x0049);
    check_u32("final sigma uppercase",
              neverc_unicode_to_upper(0x03C2), 0x03A3);

    check_int("kra is not uppercase",
              neverc_unicode_is_upper(0x0138), 0);
    check_int("kra is lowercase",
              neverc_unicode_is_lower(0x0138), 1);
    check_int("K cedilla capital is uppercase",
              neverc_unicode_is_upper(0x0136), 1);
    check_int("K cedilla small is lowercase",
              neverc_unicode_is_lower(0x0137), 1);
    check_int("L acute capital is uppercase",
              neverc_unicode_is_upper(0x0139), 1);

    check_int("NKo zero is digit",
              neverc_unicode_is_digit(0x07C0), 1);
    check_int("NKo zero is number",
              neverc_unicode_is_number(0x07C0), 1);
    check_int("Bengali zero is digit",
              neverc_unicode_is_digit(0x09E6), 1);
    check_int("Thai zero is digit",
              neverc_unicode_is_digit(0x0E50), 1);
    check_int("Tibetan zero is digit",
              neverc_unicode_is_digit(0x0F20), 1);
    check_int("supported digits are numbers",
              neverc_unicode_is_digit(0x09E6) &&
              neverc_unicode_is_number(0x09E6) &&
              neverc_unicode_is_digit(0x0E50) &&
              neverc_unicode_is_number(0x0E50), 1);
    check_int("superscript i is not number",
              neverc_unicode_is_number(0x2071), 0);

    check_int("dollar is not punctuation",
              neverc_unicode_is_punct('$'), 0);
    check_int("dollar is symbol",
              neverc_unicode_is_symbol('$'), 1);
    check_int("section sign is punctuation",
              neverc_unicode_is_punct(0x00A7), 1);
    check_int("section sign is not symbol",
              neverc_unicode_is_symbol(0x00A7), 0);
    check_int("Devanagari avagraha is letter",
              neverc_unicode_is_letter(0x093D), 1);
    check_int("Devanagari avagraha is not mark",
              neverc_unicode_is_mark(0x093D), 0);

    check_u32("titlecase DZ capital",
              neverc_unicode_to_title(0x01C4), 0x01C5);
    check_u32("titlecase LJ capital",
              neverc_unicode_to_title(0x01C7), 0x01C8);
    check_u32("titlecase NJ capital",
              neverc_unicode_to_title(0x01CA), 0x01CB);
    check_u32("titlecase DZ acute capital",
              neverc_unicode_to_title(0x01F1), 0x01F2);
    check_int("BOM is not whitespace",
              neverc_unicode_is_space(0xFEFF), 0);

    check_u32("fold K to k", neverc_unicode_simple_fold('K'), 'k');
    check_u32("fold k to Kelvin",
              neverc_unicode_simple_fold('k'), 0x212A);
    check_u32("fold Kelvin to K",
              neverc_unicode_simple_fold(0x212A), 'K');
    check_u32("fold S to s", neverc_unicode_simple_fold('S'), 's');
    check_u32("fold s to long s",
              neverc_unicode_simple_fold('s'), 0x017F);
    check_u32("fold long s to S",
              neverc_unicode_simple_fold(0x017F), 'S');
    check_u32("fold micro sign to capital mu",
              neverc_unicode_simple_fold(0x00B5), 0x039C);
    check_u32("fold capital mu to small mu",
              neverc_unicode_simple_fold(0x039C), 0x03BC);
    check_u32("fold small mu to micro sign",
              neverc_unicode_simple_fold(0x03BC), 0x00B5);
    check_u32("fold capital sigma to final sigma",
              neverc_unicode_simple_fold(0x03A3), 0x03C2);
    check_u32("fold final sigma to small sigma",
              neverc_unicode_simple_fold(0x03C2), 0x03C3);
    check_u32("fold small sigma to capital sigma",
              neverc_unicode_simple_fold(0x03C3), 0x03A3);
    check_u32("fold capital I with dot to itself",
              neverc_unicode_simple_fold(0x0130), 0x0130);
    check_u32("fold dotless i to itself",
              neverc_unicode_simple_fold(0x0131), 0x0131);
}

int main(void) {
    printf("=== NeverC Unicode Library Tests ===\n\n");
    test_ascii_classification();
    test_space();
    test_punct();
    test_control();
    test_case_conversion();
    test_unicode_beyond_ascii();
    test_new_classification();
    test_simple_fold();
    test_unicode_conformance_edges();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
