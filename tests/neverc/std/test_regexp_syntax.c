#include "neverc/std/regexp_syntax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got == NULL && expected == NULL) { tests_passed++; return; }
    if (got == NULL || expected == NULL) {
        tests_failed++;
        printf("  FAIL: %s: got %s, expected %s\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
        return;
    }
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_op(const char *name, neverc_regexp_syntax_node_t *node,
                     neverc_regexp_op_t expected) {
    tests_run++;
    if (node && node->op == expected) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got op %s, expected %s\n", name,
               node ? neverc_regexp_syntax_op_string(node->op) : "NULL",
               neverc_regexp_syntax_op_string(expected));
    }
}

static void check_not_null(const char *name, void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

static void check_null(const char *name, void *ptr) {
    tests_run++;
    if (!ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: expected NULL\n", name); }
}

/* ===== Parse basic patterns ===== */

static void test_parse_literal(void) {
    printf("[parse_literal]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a", 0, &err);
    check_not_null("literal a", n);
    check_op("literal a op", n, NC_RE_OP_LITERAL);
    check_int("literal a rune", n ? n->nrunes : 0, 1);
    check_int("literal a val", (n && n->nrunes > 0) ? n->runes[0] : 0, 'a');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("abc", 0, &err);
    check_not_null("literal abc", n);
    check_op("literal abc op", n, NC_RE_OP_CONCAT);
    check_int("literal abc nsubs", n ? n->nsubs : 0, 3);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("", 0, &err);
    check_not_null("empty pattern", n);
    check_op("empty op", n, NC_RE_OP_EMPTY_MATCH);
    neverc_regexp_syntax_free(n);
}

static void test_parse_dot(void) {
    printf("[parse_dot]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse(".", 0, &err);
    check_not_null("dot", n);
    check_op("dot op", n, NC_RE_OP_ANY_CHAR_NOT_NL);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse(".", NC_RE_FLAG_DOT_NL, &err);
    check_not_null("dot s-flag", n);
    check_op("dot s-flag op", n, NC_RE_OP_ANY_CHAR);
    neverc_regexp_syntax_free(n);
}

static void test_parse_anchors(void) {
    printf("[parse_anchors]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;
    char *s;

    n = neverc_regexp_syntax_parse("^", 0, &err);
    check_not_null("caret", n);
    check_op("caret op", n, NC_RE_OP_BEGIN_TEXT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("^", NC_RE_FLAG_MULTI_LINE, &err);
    check_not_null("caret multi", n);
    check_op("caret multi op", n, NC_RE_OP_BEGIN_LINE);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("$", 0, &err);
    check_not_null("dollar", n);
    check_op("dollar op", n, NC_RE_OP_END_TEXT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("^", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string ^", s, "^");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\A", 0, &err);
    check_op("\\A op", n, NC_RE_OP_BEGIN_TEXT);
    s = neverc_regexp_syntax_string(n);
    check_str("string \\A", s, "\\A");
    free(s);
    neverc_regexp_syntax_free(n);
}

/* ===== Quantifiers ===== */

static void test_parse_star(void) {
    printf("[parse_star]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a*", 0, &err);
    check_not_null("a*", n);
    check_op("a* op", n, NC_RE_OP_STAR);
    check_int("a* nsubs", n ? n->nsubs : 0, 1);
    if (n && n->nsubs > 0)
        check_op("a* sub op", n->subs[0], NC_RE_OP_LITERAL);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a*?", 0, &err);
    check_not_null("a*?", n);
    check_op("a*? op", n, NC_RE_OP_STAR);
    check_int("a*? non-greedy", n ? (n->flags & NC_RE_FLAG_NON_GREEDY) != 0 : 0, 1);
    neverc_regexp_syntax_free(n);
}

static void test_parse_plus(void) {
    printf("[parse_plus]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a+", 0, &err);
    check_not_null("a+", n);
    check_op("a+ op", n, NC_RE_OP_PLUS);
    neverc_regexp_syntax_free(n);
}

static void test_parse_quest(void) {
    printf("[parse_quest]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a?", 0, &err);
    check_not_null("a?", n);
    check_op("a? op", n, NC_RE_OP_QUEST);
    neverc_regexp_syntax_free(n);
}

static void test_parse_repeat(void) {
    printf("[parse_repeat]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a{3}", 0, &err);
    check_not_null("a{3}", n);
    check_op("a{3} op", n, NC_RE_OP_REPEAT);
    check_int("a{3} min", n ? n->min : -1, 3);
    check_int("a{3} max", n ? n->max : -1, 3);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{2,5}", 0, &err);
    check_not_null("a{2,5}", n);
    check_int("a{2,5} min", n ? n->min : -1, 2);
    check_int("a{2,5} max", n ? n->max : -1, 5);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{2,}", 0, &err);
    check_not_null("a{2,}", n);
    check_int("a{2,} min", n ? n->min : -1, 2);
    check_int("a{2,} max", n ? n->max : -1, -1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{3}?", 0, &err);
    check_not_null("a{3}?", n);
    check_int("a{3}? non-greedy", n ? (n->flags & NC_RE_FLAG_NON_GREEDY) != 0 : 0, 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("{", 0, &err);
    check_not_null("literal {", n);
    check_op("literal { op", n, NC_RE_OP_LITERAL);
    check_int("literal { rune", (n && n->nrunes > 0) ? n->runes[0] : 0, '{');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{}", 0, &err);
    check_not_null("a{} literals", n);
    check_op("a{} op", n, NC_RE_OP_CONCAT);
    check_int("a{} nsubs", n ? n->nsubs : 0, 3);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("{3}", 0, &err);
    check_not_null("{3} literals", n);
    check_op("{3} op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);

    /* Go parseInt: leading zeros are not a repeat count. */
    n = neverc_regexp_syntax_parse("a{01}", 0, &err);
    check_not_null("a{01} literals", n);
    check_op("a{01} op", n, NC_RE_OP_CONCAT);
    check_int("a{01} nsubs", n ? n->nsubs : 0, 5);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{0,01}", 0, &err);
    check_not_null("a{0,01} literals", n);
    check_op("a{0,01} op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);
}

/* ===== Alternation / Groups ===== */

static void test_parse_alternate(void) {
    printf("[parse_alternate]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a|b", 0, &err);
    check_not_null("a|b", n);
    check_op("a|b op", n, NC_RE_OP_ALTERNATE);
    check_int("a|b nsubs", n ? n->nsubs : 0, 2);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a|b|c", 0, &err);
    check_not_null("a|b|c", n);
    check_int("a|b|c nsubs", n ? n->nsubs : 0, 3);
    neverc_regexp_syntax_free(n);
}

static void test_parse_group(void) {
    printf("[parse_group]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("(abc)", 0, &err);
    check_not_null("(abc)", n);
    check_op("(abc) op", n, NC_RE_OP_CAPTURE);
    check_int("(abc) cap", n ? n->cap : -1, 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?:abc)", 0, &err);
    check_not_null("(?:abc)", n);
    check_op("(?:abc) op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?P<name>abc)", 0, &err);
    check_not_null("named cap", n);
    check_op("named cap op", n, NC_RE_OP_CAPTURE);
    check_str("named cap name", n ? n->name : NULL, "name");
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?P<>abc)", 0, &err);
    check_null("empty named cap", n);
    n = neverc_regexp_syntax_parse("(?P<foo-bar>x)", 0, &err);
    check_null("invalid named cap chars", n);

    /* Go: `(?P` is a named capture only when '<' follows. Skipping 3 bytes
     * for any `(?P` accepted `(?Pname>x)` as a capture named "ame". */
    n = neverc_regexp_syntax_parse("(?Pname>x)", 0, &err);
    check_null("(?Pname>x) requires < after P", n);
    n = neverc_regexp_syntax_parse("(?P foo>x)", 0, &err);
    check_null("(?P space-name) rejected", n);
    n = neverc_regexp_syntax_parse("(?Px)", 0, &err);
    check_null("(?Px) rejected", n);

    n = neverc_regexp_syntax_parse("(?<name>abc)", 0, &err);
    check_not_null("(?<name>)", n);
    check_str("(?<name>) name", n ? n->name : NULL, "name");
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?'name'abc)", 0, &err);
    check_not_null("(?'name')", n);
    check_str("(?'name') name", n ? n->name : NULL, "name");
    neverc_regexp_syntax_free(n);
}

/* ===== Char class ===== */

static void test_parse_charclass(void) {
    printf("[parse_charclass]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("[abc]", 0, &err);
    check_not_null("[abc]", n);
    check_op("[abc] op", n, NC_RE_OP_CHAR_CLASS);
    check_int("[abc] nrunes", n ? n->nrunes : 0, 6);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[a-z]", 0, &err);
    check_not_null("[a-z]", n);
    check_int("[a-z] nrunes", n ? n->nrunes : 0, 2);
    check_int("[a-z] lo", (n && n->nrunes >= 2) ? n->runes[0] : 0, 'a');
    check_int("[a-z] hi", (n && n->nrunes >= 2) ? n->runes[1] : 0, 'z');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[^0-9]", 0, &err);
    check_not_null("[^0-9]", n);
    check_int("[^0-9] negated", n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 0, 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[\\D]", 0, &err);
    check_not_null("[\\D]", n);
    check_op("[\\D] op", n, NC_RE_OP_CHAR_CLASS);
    check_int("[\\D] nrunes", n ? n->nrunes : 0, 4);
    check_int("[\\D] lo0", (n && n->nrunes >= 2) ? n->runes[0] : -1, 0);
    check_int("[\\D] hi0", (n && n->nrunes >= 2) ? n->runes[1] : -1, '0' - 1);
    check_int("[\\D] lo1", (n && n->nrunes >= 4) ? n->runes[2] : -1, '9' + 1);
    check_int("[\\D] hi1", (n && n->nrunes >= 4) ? n->runes[3] : -1, 0x10FFFF);
    check_int("[\\D] not negated", n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 1, 0);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[a-\\d]", 0, &err);
    check_null("[a-\\d] class as range end", n);

    n = neverc_regexp_syntax_parse("[\\a]", 0, &err);
    check_not_null("[\\a]", n);
    check_int("[\\a] lo", (n && n->nrunes >= 1) ? n->runes[0] : -1, '\a');
    check_int("[\\a] hi", (n && n->nrunes >= 2) ? n->runes[1] : -1, '\a');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[\\b]", 0, &err);
    check_not_null("[\\b]", n);
    check_int("[\\b] lo", (n && n->nrunes >= 1) ? n->runes[0] : -1, '\b');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[[:digit:]]", 0, &err);
    check_not_null("[[:digit:]]", n);
    check_op("[[:digit:]] op", n, NC_RE_OP_CHAR_CLASS);
    check_int("[[:digit:]] nrunes", n ? n->nrunes : 0, 2);
    check_int("[[:digit:]] lo", (n && n->nrunes >= 2) ? n->runes[0] : -1, '0');
    check_int("[[:digit:]] hi", (n && n->nrunes >= 2) ? n->runes[1] : -1, '9');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[[:foo:]]", 0, &err);
    check_null("[[:foo:]] unknown", n);
}

/* ===== Escapes ===== */

static void test_parse_escapes(void) {
    printf("[parse_escapes]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("\\d", 0, &err);
    check_not_null("\\d", n);
    check_op("\\d op", n, NC_RE_OP_CHAR_CLASS);
    check_int("\\d nrunes", n ? n->nrunes : 0, 2);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\w", 0, &err);
    check_not_null("\\w", n);
    check_int("\\w nrunes", n ? n->nrunes : 0, 8);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\b", 0, &err);
    check_not_null("\\b", n);
    check_op("\\b op", n, NC_RE_OP_WORD_BOUNDARY);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\.", 0, &err);
    check_not_null("\\.", n);
    check_op("\\. op", n, NC_RE_OP_LITERAL);
    check_int("\\. rune", (n && n->nrunes > 0) ? n->runes[0] : 0, '.');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\n", 0, &err);
    check_not_null("\\n", n);
    check_int("\\n rune", (n && n->nrunes > 0) ? n->runes[0] : 0, '\n');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\x41", 0, &err);
    check_not_null("\\x41", n);
    check_op("\\x41 op", n, NC_RE_OP_LITERAL);
    check_int("\\x41 rune", (n && n->nrunes > 0) ? n->runes[0] : 0, 'A');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\x96B", 0, &err);
    check_not_null("\\x96B", n);
    check_op("\\x96B op", n, NC_RE_OP_CONCAT);
    check_int("\\x96B nsubs", n ? n->nsubs : 0, 2);
    check_int("\\x96B lo", (n && n->nsubs > 0 && n->subs[0]->nrunes > 0) ?
              n->subs[0]->runes[0] : -1, 0x96);
    check_int("\\x96B hi", (n && n->nsubs > 1 && n->subs[1]->nrunes > 0) ?
              n->subs[1]->runes[0] : -1, 'B');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\x{41}", 0, &err);
    check_not_null("\\x{41}", n);
    check_int("\\x{41} rune", (n && n->nrunes > 0) ? n->runes[0] : 0, 'A');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\x", 0, &err);
    check_null("\\x invalid", n);
    n = neverc_regexp_syntax_parse("\\x4", 0, &err);
    check_null("\\x4 invalid", n);
    n = neverc_regexp_syntax_parse("\\xGG", 0, &err);
    check_null("\\xGG invalid", n);

    n = neverc_regexp_syntax_parse("\\q", 0, &err);
    check_null("\\q unknown letter escape", n);
    n = neverc_regexp_syntax_parse("\\1", 0, &err);
    check_null("\\1 backreference rejected", n);
    n = neverc_regexp_syntax_parse("\\8", 0, &err);
    check_null("\\8 unknown digit escape", n);
    n = neverc_regexp_syntax_parse("[\\q]", 0, &err);
    check_null("[\\q] unknown class escape", n);
    n = neverc_regexp_syntax_parse("[a-\\q]", 0, &err);
    check_null("[a-\\q] unknown range-end escape", n);
    n = neverc_regexp_syntax_parse("[\\A]", 0, &err);
    check_null("[\\A] unknown class escape", n);
}

/* ===== String conversion ===== */

static void test_string(void) {
    printf("[string]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;
    char *s;

    n = neverc_regexp_syntax_parse("abc", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string abc", s, "abc");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a*", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string a*", s, "a*");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a|b", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string a|b", s, "a|b");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[a-z]", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string [a-z]", s, "[a-z]");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse(".", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string .", s, ".");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{2,5}", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string a{2,5}", s, "a{2,5}");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\x{96}", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string \\x{96}", s, "\\x{96}");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?:a|b)c", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string (?:a|b)c keeps grouping", s, "(?:a|b)c");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?:a|b)*", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string (?:a|b)* stays non-capturing", s, "(?:a|b)*");
    free(s);
    neverc_regexp_syntax_free(n);
}

/* ===== Equality ===== */

static void test_equal(void) {
    printf("[equal]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *a, *b;

    a = neverc_regexp_syntax_parse("a+b", 0, &err);
    b = neverc_regexp_syntax_parse("a+b", 0, &err);
    check_int("equal same", neverc_regexp_syntax_equal(a, b), 1);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("a+", 0, &err);
    b = neverc_regexp_syntax_parse("a*", 0, &err);
    check_int("equal diff op", neverc_regexp_syntax_equal(a, b), 0);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("(abc)", 0, &err);
    b = neverc_regexp_syntax_parse("(abc)", 0, &err);
    check_int("equal capture", neverc_regexp_syntax_equal(a, b), 1);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    check_int("equal null", neverc_regexp_syntax_equal(NULL, NULL), 1);
}

/* ===== Op string ===== */

static void test_op_string(void) {
    printf("[op_string]\n");
    check_str("op literal", neverc_regexp_syntax_op_string(NC_RE_OP_LITERAL), "Literal");
    check_str("op star", neverc_regexp_syntax_op_string(NC_RE_OP_STAR), "Star");
    check_str("op concat", neverc_regexp_syntax_op_string(NC_RE_OP_CONCAT), "Concat");
    check_str("op capture", neverc_regexp_syntax_op_string(NC_RE_OP_CAPTURE), "Capture");
}

/* ===== Node count ===== */

static void test_node_count(void) {
    printf("[node_count]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("a", 0, &err);
    check_int("count a", neverc_regexp_syntax_node_count(n), 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a+b", 0, &err);
    check_int("count a+b", neverc_regexp_syntax_node_count(n), 4);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(a|b)*", 0, &err);
    int cnt = neverc_regexp_syntax_node_count(n);
    check_int("count (a|b)* > 3", cnt > 3, 1);
    neverc_regexp_syntax_free(n);
}

/* ===== Error cases ===== */

static void test_errors(void) {
    printf("[errors]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("[abc", 0, &err);
    check_null("unclosed class", n);
    check_not_null("unclosed class err", (void *)(size_t)(err != NULL));

    n = neverc_regexp_syntax_parse("(abc", 0, &err);
    check_null("unclosed group", n);

    n = neverc_regexp_syntax_parse("a{3", 0, &err);
    check_null("bad repeat", n);

    n = neverc_regexp_syntax_parse("a{2}*", 0, &err);
    check_null("stacked a{2}*", n);

    n = neverc_regexp_syntax_parse("a{2}{3}", 0, &err);
    check_null("stacked a{2}{3}", n);

    n = neverc_regexp_syntax_parse("a{1001}", 0, &err);
    check_null("repeat over 1000", n);

    n = neverc_regexp_syntax_parse("a{2147483648}", 0, &err);
    check_null("repeat INT_MAX+1 overflow", n);
    check_not_null("repeat overflow err", (void *)(size_t)(err != NULL));

    n = neverc_regexp_syntax_parse("a{99999999999999999999}", 0, &err);
    check_null("repeat huge overflow", n);

    {
        char bad[] = { 'a', (char)0xFF, 'b', 0 };
        err = NULL;
        n = neverc_regexp_syntax_parse(bad, 0, &err);
        check_null("invalid UTF-8 pattern", n);
        check_not_null("invalid UTF-8 err", (void *)(size_t)(err != NULL));

        char trunc[] = { (char)0xC3, 0 };
        err = NULL;
        n = neverc_regexp_syntax_parse(trunc, 0, &err);
        check_null("truncated UTF-8 pattern", n);

        char overlong[] = { (char)0xC0, (char)0x80, 0 };
        err = NULL;
        n = neverc_regexp_syntax_parse(overlong, 0, &err);
        check_null("overlong UTF-8 pattern", n);
    }

    n = neverc_regexp_syntax_parse("\xC3\xA9", 0, &err);
    check_not_null("utf8 e-acute", n);
    check_op("utf8 e-acute op", n, NC_RE_OP_LITERAL);
    check_int("utf8 e-acute nrunes", n ? n->nrunes : 0, 1);
    check_int("utf8 e-acute rune", (n && n->nrunes > 0) ? n->runes[0] : 0, 0xE9);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[z-a]", 0, &err);
    check_null("inverted class range", n);
    check_not_null("inverted class range err", (void *)(size_t)(err != NULL));

    n = neverc_regexp_syntax_parse("(?Pname>x)", 0, &err);
    check_null("(?P without <)", n);
    check_not_null("(?P without <) err", (void *)(size_t)(err != NULL));

    n = neverc_regexp_syntax_parse("(?=a)", 0, &err);
    check_null("lookahead unsupported", n);

    char deep[902];
    for (int i = 0; i < 450; i++) deep[i] = '(';
    deep[450] = 'a';
    for (int i = 0; i < 450; i++) deep[451 + i] = ')';
    deep[901] = '\0';
    err = NULL;
    n = neverc_regexp_syntax_parse(deep, 0, &err);
    check_null("nested too deeply", n);
    check_not_null("nested too deeply err", (void *)(size_t)(err != NULL));
}

/* ===== Complex patterns ===== */

static void test_complex(void) {
    printf("[complex]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;
    char *s;

    n = neverc_regexp_syntax_parse("^[a-zA-Z_][a-zA-Z0-9_]*$", 0, &err);
    check_not_null("identifier pattern", n);
    s = neverc_regexp_syntax_string(n);
    check_not_null("identifier string", s);
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}", 0, &err);
    check_not_null("ip pattern", n);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?P<year>\\d{4})-(?P<month>\\d{2})-(?P<day>\\d{2})", 0, &err);
    check_not_null("date pattern", n);
    check_op("date op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[[:alpha:]]", 0, &err);
    check_not_null("posix class", n);
    neverc_regexp_syntax_free(n);
}

/* ===== Main ===== */

int main(void) {
    test_parse_literal();
    test_parse_dot();
    test_parse_anchors();
    test_parse_star();
    test_parse_plus();
    test_parse_quest();
    test_parse_repeat();
    test_parse_alternate();
    test_parse_group();
    test_parse_charclass();
    test_parse_escapes();
    test_string();
    test_equal();
    test_op_string();
    test_node_count();
    test_errors();
    test_complex();

    printf("\n--- regexp/syntax: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
