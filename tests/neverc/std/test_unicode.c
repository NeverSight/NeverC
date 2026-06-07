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
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
