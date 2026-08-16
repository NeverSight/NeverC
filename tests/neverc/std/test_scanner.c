#include "neverc/std/text/scanner.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = \"%s\", expected \"%s\" (line %d)\n", \
                  #expr, _v ? _v : "(null)", _e ? _e : "(null)", __LINE__); } \
} while(0)

static void test_identifiers(void) {
    printf("[identifiers]\n");
    neverc_scanner_t s;
    const char *src = "hello world foo_bar _x123";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "hello");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "world");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "foo_bar");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "_x123");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_integers(void) {
    printf("[integers]\n");
    neverc_scanner_t s;
    const char *src = "42 0xFF 0b1010 0o77 0";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "42");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0xFF");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0b1010");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0o77");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_floats(void) {
    printf("[floats]\n");
    neverc_scanner_t s;
    const char *src = "3.14 1e10 2.5e-3 .5";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "3.14");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1e10");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "2.5e-3");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), ".5");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_strings(void) {
    printf("[strings]\n");
    neverc_scanner_t s;
    const char *src = "\"hello\" \"with\\nesc\" 'a'";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_STRING);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "\"hello\"");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_STRING);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "\"with\\nesc\"");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_CHAR);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "'a'");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_comments(void) {
    printf("[comments]\n");
    neverc_scanner_t s;
    const char *src = "a /* block */ b // line\nc";
    neverc_scanner_init(&s, src, strlen(src));
    neverc_scanner_set_mode(&s, NEVERC_SCAN_GO_TOKENS);

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "a");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "b");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "c");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_comments_visible(void) {
    printf("[comments_visible]\n");
    neverc_scanner_t s;
    const char *src = "a /* comment */ b";
    neverc_scanner_init(&s, src, strlen(src));
    neverc_scanner_set_mode(&s, NEVERC_SCAN_GO_TOKENS & ~NEVERC_SCAN_SKIP_COMMENTS);

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_COMMENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "/* comment */");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_operators(void) {
    printf("[operators]\n");
    neverc_scanner_t s;
    const char *src = "+-*/";
    neverc_scanner_init(&s, src, strlen(src));
    neverc_scanner_set_mode(&s, NEVERC_SCAN_IDENTS);

    ASSERT_INT_EQ(neverc_scanner_scan(&s), '+');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '-');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '*');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '/');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_position(void) {
    printf("[position]\n");
    neverc_scanner_t s;
    const char *src = "abc\ndef";
    neverc_scanner_init(&s, src, strlen(src));

    neverc_scanner_scan(&s);
    neverc_scanner_pos_t p = neverc_scanner_position(&s);
    ASSERT_INT_EQ(p.line, 1);
    ASSERT_INT_EQ(p.column, 1);

    neverc_scanner_scan(&s);
    p = neverc_scanner_position(&s);
    ASSERT_INT_EQ(p.line, 2);
    ASSERT_INT_EQ(p.column, 1);
}

static void test_peek(void) {
    printf("[peek]\n");
    neverc_scanner_t s;
    const char *src = "abc 123";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_peek(&s), 'a');
    neverc_scanner_scan(&s);
    ASSERT_INT_EQ(neverc_scanner_peek(&s), '1');
}

static void test_token_name(void) {
    printf("[token_name]\n");
    ASSERT_STR_EQ(neverc_scanner_token_name(NEVERC_SCANNER_EOF), "EOF");
    ASSERT_STR_EQ(neverc_scanner_token_name(NEVERC_SCANNER_IDENT), "Ident");
    ASSERT_STR_EQ(neverc_scanner_token_name(NEVERC_SCANNER_INT), "Int");
    ASSERT_STR_EQ(neverc_scanner_token_name(NEVERC_SCANNER_FLOAT), "Float");
    ASSERT_STR_EQ(neverc_scanner_token_name(NEVERC_SCANNER_STRING), "String");
}

static void test_raw_strings(void) {
    printf("[raw_strings]\n");
    neverc_scanner_t s;
    const char *src = "`hello\nworld`";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_RAWSTRING);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "`hello\nworld`");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_float_dot_and_exponent(void) {
    printf("[float_dot_and_exponent]\n");
    neverc_scanner_t s;
    const char *src = "1. 1.e10 1.foo 1if 1..2";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1.");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1.e10");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1.");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "foo");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "if");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1.");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), ".2");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_ints_do_not_consume_fraction(void) {
    printf("[ints_do_not_consume_fraction]\n");
    neverc_scanner_t s;
    const char *src = "3.14 1e10";
    neverc_scanner_init(&s, src, strlen(src));
    neverc_scanner_set_mode(&s, NEVERC_SCAN_INTS | NEVERC_SCAN_IDENTS);

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "3");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '.');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "14");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "e10");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_ints_do_not_consume_hex_float(void) {
    printf("[ints_do_not_consume_hex_float]\n");
    neverc_scanner_t s;
    const char *src = "0x1.0p1 0x2p3";
    neverc_scanner_init(&s, src, strlen(src));
    neverc_scanner_set_mode(&s, NEVERC_SCAN_INTS | NEVERC_SCAN_IDENTS);

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0x1");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '.');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "p1");

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0x2");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "p3");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_block_comments_are_not_nested(void) {
    printf("[block_comments_are_not_nested]\n");
    neverc_scanner_t s;
    const char *src = "a /* /* */ b";
    neverc_scanner_init(&s, src, strlen(src));
    neverc_scanner_set_mode(&s, NEVERC_SCAN_GO_TOKENS & ~NEVERC_SCAN_SKIP_COMMENTS);

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "a");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_COMMENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "/* /* */");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "b");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_null_source(void) {
    printf("[null_source]\n");
    neverc_scanner_t s;
    neverc_scanner_init(&s, NULL, 8);
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
    ASSERT_INT_EQ(neverc_scanner_peek(&s), NEVERC_SCANNER_EOF);
    neverc_scanner_init(NULL, "x", 1);
    neverc_scanner_set_mode(NULL, 0);
    ASSERT_INT_EQ(neverc_scanner_scan(NULL), NEVERC_SCANNER_EOF);
}

static void test_number_separators_and_prefixes(void) {
    printf("[number_separators_and_prefixes]\n");
    neverc_scanner_t s;
    const char *src = "1_000 0x_f00d 0b_10 0b0190 0o8123";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1_000");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0x_f00d");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0b_10");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0b0190");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0o8123");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_prefix_floats(void) {
    printf("[prefix_floats]\n");
    neverc_scanner_t s;
    const char *src = "0b1.0 0o1.2 0p0 1.0P-1";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0b1.0");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0o1.2");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "0p0");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "1.0P-1");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

static void test_mixed(void) {
    printf("[mixed]\n");
    neverc_scanner_t s;
    const char *src = "x = 42 + 3.14";
    neverc_scanner_init(&s, src, strlen(src));

    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_IDENT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "x");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '=');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_INT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "42");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), '+');
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_FLOAT);
    ASSERT_STR_EQ(neverc_scanner_token_text(&s, NULL), "3.14");
    ASSERT_INT_EQ(neverc_scanner_scan(&s), NEVERC_SCANNER_EOF);
}

int main(void) {
    printf("=== NeverC text/scanner Tests ===\n");
    test_identifiers();
    test_integers();
    test_floats();
    test_strings();
    test_comments();
    test_comments_visible();
    test_operators();
    test_position();
    test_peek();
    test_token_name();
    test_raw_strings();
    test_float_dot_and_exponent();
    test_ints_do_not_consume_fraction();
    test_ints_do_not_consume_hex_float();
    test_block_comments_are_not_nested();
    test_number_separators_and_prefixes();
    test_prefix_floats();
    test_null_source();
    test_mixed();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
