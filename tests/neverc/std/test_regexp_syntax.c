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

static int class_contains(const neverc_regexp_syntax_node_t *node, int rune) {
    if (!node || node->op != NC_RE_OP_CHAR_CLASS) return 0;
    int contained = 0;
    for (int i = 0; i + 1 < node->nrunes; i += 2) {
        if (rune >= node->runes[i] && rune <= node->runes[i + 1]) {
            contained = 1;
            break;
        }
    }
    return (node->flags & NC_RE_FLAG_CLASS_NEGATED) ? !contained : contained;
}

static int node_round_trips(const neverc_regexp_syntax_node_t *node) {
    char *text = neverc_regexp_syntax_string(node);
    if (!text) return 0;
    const char *err = NULL;
    neverc_regexp_syntax_node_t *copy =
        neverc_regexp_syntax_parse(text, 0, &err);
    int equal = copy && neverc_regexp_syntax_equal(node, copy);
    free(text);
    neverc_regexp_syntax_free(copy);
    return equal;
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
    char *s;

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

    n = neverc_regexp_syntax_parse("a*", NC_RE_FLAG_NON_GREEDY, &err);
    check_not_null("a* default non-greedy", n);
    check_int("a* default non-greedy flag",
              n ? (n->flags & NC_RE_FLAG_NON_GREEDY) != 0 : 0, 1);
    s = neverc_regexp_syntax_string(n);
    check_str("a* default non-greedy string", s, "a*?");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a*?", NC_RE_FLAG_NON_GREEDY, &err);
    check_not_null("a*? flips default non-greedy", n);
    check_int("a*? default non-greedy becomes greedy",
              n ? (n->flags & NC_RE_FLAG_NON_GREEDY) != 0 : 1, 0);
    s = neverc_regexp_syntax_string(n);
    check_str("a*? default non-greedy string", s, "a*");
    free(s);
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

    n = neverc_regexp_syntax_parse(
        "a{1,2}", NC_RE_FLAG_NON_GREEDY, &err);
    check_not_null("a{1,2} default non-greedy", n);
    check_int("a{1,2} default non-greedy flag",
              n ? (n->flags & NC_RE_FLAG_NON_GREEDY) != 0 : 0, 1);
    {
        char *s = neverc_regexp_syntax_string(n);
        check_str("a{1,2} default non-greedy string", s, "a{1,2}?");
        free(s);
    }
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

    /* After a quantifier, `{01}` / unclosed `{3` are still literals (Go).
     * Treating `{`+digit as nested `{n}` rejected a{2}{01} and a*{01}. */
    n = neverc_regexp_syntax_parse("a{2}{01}", 0, &err);
    check_not_null("a{2}{01} literals after repeat", n);
    check_op("a{2}{01} op", n, NC_RE_OP_CONCAT);
    check_int("a{2}{01} nsubs", n ? n->nsubs : 0, 5);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a*{01}", 0, &err);
    check_not_null("a*{01} star plus literals", n);
    check_op("a*{01} op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{2}{3", 0, &err);
    check_not_null("a{2}{3 unclosed after repeat is literal", n);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{2}{3,", 0, &err);
    check_not_null("a{2}{3, unclosed after repeat is literal", n);
    neverc_regexp_syntax_free(n);

    /* Go parseRepeat: `{` is a literal unless `{min}`, `{min,}`, or
     * `{min,max}` is complete. `a{3,` / `a{3,x}` / unclosed overflow
     * used to error in syntax.c while regexp.c accepted them. */
    n = neverc_regexp_syntax_parse("a{3,", 0, &err);
    check_not_null("a{3, unclosed is literal", n);
    check_op("a{3, op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{3,x}", 0, &err);
    check_not_null("a{3,x} is literal", n);
    check_op("a{3,x} op", n, NC_RE_OP_CONCAT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("a{2147483648", 0, &err);
    check_not_null("unclosed overflow {n is literal", n);
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

    n = neverc_regexp_syntax_parse("a|ab", 0, &err);
    check_not_null("a|ab", n);
    check_op("a|ab op", n, NC_RE_OP_ALTERNATE);
    check_int("a|ab nsubs", n ? n->nsubs : 0, 2);
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

static void test_scoped_flags(void) {
    printf("[scoped_flags]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    /* All supported flags may be combined in any order. Their effects are
     * lexical: none may leak into the atoms following the scoped group. */
    n = neverc_regexp_syntax_parse("(?mis:a.$)b.^$", 0, &err);
    check_not_null("combined scoped flags", n);
    check_op("combined scope outer concat", n, NC_RE_OP_CONCAT);
    check_int("combined scope outer arity", n ? n->nsubs : 0, 5);
    neverc_regexp_syntax_node_t *inner =
        (n && n->nsubs == 5) ? n->subs[0] : NULL;
    check_op("combined scope inner concat", inner, NC_RE_OP_CONCAT);
    check_int("combined scope inner arity", inner ? inner->nsubs : 0, 3);
    check_int("combined scope folds literal",
              (inner && inner->nsubs == 3) ?
                  (inner->subs[0]->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 0,
              1);
    check_int("combined scope canonical literal",
              (inner && inner->nsubs == 3 && inner->subs[0]->nrunes == 1) ?
                  inner->subs[0]->runes[0] : -1,
              'A');
    check_op("combined scope dot matches newline",
             (inner && inner->nsubs == 3) ? inner->subs[1] : NULL,
             NC_RE_OP_ANY_CHAR);
    check_op("combined scope dollar is line anchor",
             (inner && inner->nsubs == 3) ? inner->subs[2] : NULL,
             NC_RE_OP_END_LINE);
    check_int("combined scope restores literal folding",
              (n && n->nsubs == 5) ?
                  (n->subs[1]->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 1,
              0);
    check_op("combined scope restores dot",
             (n && n->nsubs == 5) ? n->subs[2] : NULL,
             NC_RE_OP_ANY_CHAR_NOT_NL);
    check_op("combined scope restores caret",
             (n && n->nsubs == 5) ? n->subs[3] : NULL,
             NC_RE_OP_BEGIN_TEXT);
    check_op("combined scope restores dollar",
             (n && n->nsubs == 5) ? n->subs[4] : NULL,
             NC_RE_OP_END_TEXT);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?ii:a)", 0, &err);
    check_null("duplicate scoped flag rejected", n);
    n = neverc_regexp_syntax_parse("(?ix:a)", 0, &err);
    check_null("unknown scoped flag rejected", n);
    n = neverc_regexp_syntax_parse("(?x:a)", 0, &err);
    check_null("unknown initial scoped flag rejected", n);
    n = neverc_regexp_syntax_parse("(?i)", 0, &err);
    check_null("scoped flags require colon", n);
}

/* ===== Char class ===== */

static void test_parse_charclass(void) {
    printf("[parse_charclass]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n;

    n = neverc_regexp_syntax_parse("[abc]", 0, &err);
    check_not_null("[abc]", n);
    check_op("[abc] op", n, NC_RE_OP_CHAR_CLASS);
    /* Go cleanClass merges adjacent singletons, so [abc] canonicalises to
     * the single range a-c. */
    check_int("[abc] nrunes", n ? n->nrunes : 0, 2);
    check_int("[abc] lo", (n && n->nrunes >= 2) ? n->runes[0] : 0, 'a');
    check_int("[abc] hi", (n && n->nrunes >= 2) ? n->runes[1] : 0, 'c');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[b-da-c]", 0, &err);
    check_not_null("[b-da-c]", n);
    check_int("[b-da-c] merged nrunes", n ? n->nrunes : 0, 2);
    check_int("[b-da-c] lo", (n && n->nrunes >= 2) ? n->runes[0] : 0, 'a');
    check_int("[b-da-c] hi", (n && n->nrunes >= 2) ? n->runes[1] : 0, 'd');
    neverc_regexp_syntax_free(n);

    {
        neverc_regexp_syntax_node_t *a = neverc_regexp_syntax_parse(
            "[a-c]", 0, &err);
        neverc_regexp_syntax_node_t *b = neverc_regexp_syntax_parse(
            "[abc]", 0, &err);
        check_int("equivalent classes compare equal",
                  neverc_regexp_syntax_equal(a, b), 1);
        neverc_regexp_syntax_free(a);
        neverc_regexp_syntax_free(b);
    }

    /* Complementing a class only works on sorted, disjoint ranges, so every
     * class node must already be in that form. \W used to store '_' after
     * a-z, which put a-z and '_' inside the complement. */
    {
        static const char *patterns[] = {
            "\\w", "\\W", "\\s", "\\S", "\\d", "\\D",
            "[\\w-]", "[[:word:]]", "[z-ya-b_]",
        };
        for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
            neverc_regexp_syntax_node_t *c =
                neverc_regexp_syntax_parse(patterns[i], 0, &err);
            int canonical = c && c->op == NC_RE_OP_CHAR_CLASS &&
                            (c->nrunes % 2) == 0;
            for (int k = 0; canonical && k + 3 < c->nrunes; k += 2)
                if (c->runes[k] > c->runes[k + 1] ||
                    c->runes[k + 2] <= c->runes[k + 1] + 1)
                    canonical = 0;
            check_int("class ranges are canonical", canonical, 1);
            neverc_regexp_syntax_free(c);
        }
    }

    n = neverc_regexp_syntax_parse("\\W", 0, &err);
    check_not_null("\\W", n);
    check_int("\\W excludes lowercase", class_contains(n, 'a'), 0);
    check_int("\\W excludes underscore", class_contains(n, '_'), 0);
    check_int("\\W includes punctuation", class_contains(n, '!'), 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[a-z]", 0, &err);
    check_not_null("[a-z]", n);
    check_int("[a-z] nrunes", n ? n->nrunes : 0, 2);
    check_int("[a-z] lo", (n && n->nrunes >= 2) ? n->runes[0] : 0, 'a');
    check_int("[a-z] hi", (n && n->nrunes >= 2) ? n->runes[1] : 0, 'z');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[^0-9]", 0, &err);
    check_not_null("[^0-9]", n);
    check_int("[^0-9] negated",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 0, 1);
    check_int("[^0-9] is not folded",
              n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 1, 0);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[\\D]", 0, &err);
    check_not_null("[\\D]", n);
    check_op("[\\D] op", n, NC_RE_OP_CHAR_CLASS);
    check_int("[\\D] nrunes", n ? n->nrunes : 0, 4);
    check_int("[\\D] lo0", (n && n->nrunes >= 2) ? n->runes[0] : -1, 0);
    check_int("[\\D] hi0", (n && n->nrunes >= 2) ? n->runes[1] : -1, '0' - 1);
    check_int("[\\D] lo1", (n && n->nrunes >= 4) ? n->runes[2] : -1, '9' + 1);
    check_int("[\\D] hi1", (n && n->nrunes >= 4) ? n->runes[3] : -1, 0x10FFFF);
    check_int("[\\D] not outer-negated",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 1, 0);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[a-\\d]", 0, &err);
    check_null("[a-\\d] class as range end", n);

    /* Go: `[\d-a]` is the class `\d`, then literal '-' and 'a' (not a range). */
    n = neverc_regexp_syntax_parse("[\\d-a]", 0, &err);
    check_not_null("[\\d-a]", n);
    check_op("[\\d-a] op", n, NC_RE_OP_CHAR_CLASS);
    check_int("[\\d-a] nrunes", n ? n->nrunes : 0, 6);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[^é]", 0, &err);
    check_not_null("[^é] syntax accepts negated rune", n);
    check_int("[^é] negated",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 0, 1);
    check_int("[^é] nrunes", n ? n->nrunes : 0, 2);
    neverc_regexp_syntax_free(n);

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

    n = neverc_regexp_syntax_parse("[[:space:]]", 0, &err);
    check_not_null("[[:space:]]", n);
    check_int("[[:space:]] nrunes", n ? n->nrunes : 0, 4);
    check_int("[[:space:]] tab-cr lo",
              (n && n->nrunes >= 2) ? n->runes[0] : -1, '\t');
    check_int("[[:space:]] tab-cr hi includes vtab",
              (n && n->nrunes >= 2) ? n->runes[1] : -1, '\r');
    check_int("[[:space:]] space lo",
              (n && n->nrunes >= 4) ? n->runes[2] : -1, ' ');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[[:foo:]]", 0, &err);
    check_null("[[:foo:]] unknown", n);

    n = neverc_regexp_syntax_parse("[[:]", 0, &err);
    check_not_null("incomplete POSIX class", n);
    neverc_regexp_syntax_free(n);
}

static void test_fold_case(void) {
    printf("[fold_case]\n");
    const char *err = NULL;
    neverc_regexp_syntax_node_t *n, *a, *b;
    char *s;

    /* Preserve every previously published bit while assigning class
     * negation its own AST-only bit. */
    check_int("fold flag ABI value", NC_RE_FLAG_FOLD_CASE, 1 << 0);
    check_int("was-caret flag ABI value", NC_RE_FLAG_WAS_CARET, 1 << 7);
    check_int("class-negated flag value", NC_RE_FLAG_CLASS_NEGATED, 1 << 8);

    n = neverc_regexp_syntax_parse("[a]", NC_RE_FLAG_CLASS_NEGATED, &err);
    check_not_null("AST-only class flag is ignored as parse input", n);
    check_int("AST-only class flag does not negate input",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 1, 0);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?i:a)b", 0, &err);
    check_not_null("scoped fold group", n);
    check_op("scoped fold concat", n, NC_RE_OP_CONCAT);
    check_int("scoped fold first literal",
              (n && n->nsubs == 2) ?
                  (n->subs[0]->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 0,
              1);
    check_int("scoped fold restores following literal",
              (n && n->nsubs == 2) ?
                  (n->subs[1]->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 1,
              0);
    check_int("scoped fold following rune unchanged",
              (n && n->nsubs == 2 && n->subs[1]->nrunes == 1) ?
                  n->subs[1]->runes[0] : -1,
              'b');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?i:a", 0, &err);
    check_null("unclosed scoped fold group", n);

    n = neverc_regexp_syntax_parse("a", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold literal a", n);
    check_op("fold literal a op", n, NC_RE_OP_LITERAL);
    check_int("fold literal flag",
              n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 0, 1);
    check_int("fold literal canonical rune",
              (n && n->nrunes == 1) ? n->runes[0] : -1, 'A');
    s = neverc_regexp_syntax_string(n);
    check_str("fold literal string", s, "(?i:A)");
    free(s);
    neverc_regexp_syntax_free(n);

    /* Kelvin sign participates in the Unicode K/k simple-fold orbit. */
    n = neverc_regexp_syntax_parse("\xE2\x84\xAA", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold Kelvin literal", n);
    check_int("fold Kelvin canonical rune",
              (n && n->nrunes == 1) ? n->runes[0] : -1, 'K');
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\x{1e943}", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold highest Unicode orbit member", n);
    check_int("fold highest Unicode orbit canonical rune",
              (n && n->nrunes == 1) ? n->runes[0] : -1, 0x1E921);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[\\x{1e943}]", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold highest Unicode class orbit", n);
    check_int("fold highest Unicode class contains uppercase",
              class_contains(n, 0x1E921), 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[a-z]", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold ASCII range", n);
    check_int("fold range flag",
              n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 0, 1);
    check_int("fold range is not negated",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 1, 0);
    check_int("fold range contains A", class_contains(n, 'A'), 1);
    check_int("fold range contains z", class_contains(n, 'z'), 1);
    check_int("fold range contains long s", class_contains(n, 0x017F), 1);
    check_int("fold range contains Kelvin", class_contains(n, 0x212A), 1);
    check_int("fold range excludes digit", class_contains(n, '0'), 0);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[^k]", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold negated class", n);
    check_int("fold negated class fold flag",
              n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 0, 1);
    check_int("fold negated class marker",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 0, 1);
    check_int("fold negated class excludes K", class_contains(n, 'K'), 0);
    check_int("fold negated class excludes k", class_contains(n, 'k'), 0);
    check_int("fold negated class excludes Kelvin", class_contains(n, 0x212A), 0);
    check_int("fold negated class contains q", class_contains(n, 'q'), 1);
    s = neverc_regexp_syntax_string(n);
    check_str("fold negated class string", s, "(?i:[^Kk\\x{212a}])");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\w", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold \\w", n);
    check_int("fold \\w contains long s", class_contains(n, 0x017F), 1);
    check_int("fold \\w contains Kelvin", class_contains(n, 0x212A), 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("\\W", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold \\W", n);
    check_int("fold \\W is compactly negated",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 0, 1);
    check_int("fold \\W excludes K", class_contains(n, 'K'), 0);
    check_int("fold \\W excludes Kelvin", class_contains(n, 0x212A), 0);
    check_int("fold \\W contains punctuation", class_contains(n, '!'), 1);
    neverc_regexp_syntax_free(n);

    /* A complemented escape inside [] is a union member, not an outer class
     * negation. Fold the positive class before taking its complement. */
    n = neverc_regexp_syntax_parse("[\\W]", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold [\\W]", n);
    check_int("fold [\\W] is not outer-negated",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 1, 0);
    check_int("fold [\\W] excludes k", class_contains(n, 'k'), 0);
    check_int("fold [\\W] excludes Kelvin", class_contains(n, 0x212A), 0);
    check_int("fold [\\W] contains punctuation", class_contains(n, '!'), 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("[[:^lower:]]", NC_RE_FLAG_FOLD_CASE, &err);
    check_not_null("fold negated POSIX lower", n);
    check_int("fold negated POSIX lower excludes A", class_contains(n, 'A'), 0);
    check_int("fold negated POSIX lower excludes long s",
              class_contains(n, 0x017F), 0);
    check_int("fold negated POSIX lower excludes Kelvin",
              class_contains(n, 0x212A), 0);
    check_int("fold negated POSIX lower contains digit", class_contains(n, '0'), 1);
    neverc_regexp_syntax_free(n);

    a = neverc_regexp_syntax_parse("A", NC_RE_FLAG_FOLD_CASE, &err);
    b = neverc_regexp_syntax_parse("a", NC_RE_FLAG_FOLD_CASE, &err);
    check_int("fold-equivalent literals equal",
              neverc_regexp_syntax_equal(a, b), 1);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("A", NC_RE_FLAG_FOLD_CASE, &err);
    b = neverc_regexp_syntax_parse("A", 0, &err);
    check_int("folded and exact literals differ",
              neverc_regexp_syntax_equal(a, b), 0);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("[a]", NC_RE_FLAG_FOLD_CASE, &err);
    b = neverc_regexp_syntax_parse("[A]", NC_RE_FLAG_FOLD_CASE, &err);
    check_int("fold-equivalent classes equal",
              neverc_regexp_syntax_equal(a, b), 1);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("[a]", NC_RE_FLAG_FOLD_CASE, &err);
    b = neverc_regexp_syntax_parse("[^a]", NC_RE_FLAG_FOLD_CASE, &err);
    check_int("positive and negated folded classes differ",
              neverc_regexp_syntax_equal(a, b), 0);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("[^k]", NC_RE_FLAG_FOLD_CASE, &err);
    s = neverc_regexp_syntax_string(a);
    b = neverc_regexp_syntax_parse(s, 0, &err);
    check_not_null("folded class string reparses", b);
    check_int("folded class string round trip",
              neverc_regexp_syntax_equal(a, b), 1);
    free(s);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);

    a = neverc_regexp_syntax_parse("a", NC_RE_FLAG_FOLD_CASE, &err);
    s = neverc_regexp_syntax_string(a);
    b = neverc_regexp_syntax_parse(s, 0, &err);
    check_not_null("folded literal string reparses", b);
    check_int("folded literal string round trip",
              neverc_regexp_syntax_equal(a, b), 1);
    free(s);
    neverc_regexp_syntax_free(a);
    neverc_regexp_syntax_free(b);
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

    n = neverc_regexp_syntax_parse("\\D", 0, &err);
    check_not_null("\\D", n);
    check_int("\\D negated marker",
              n ? (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0 : 0, 1);
    check_int("\\D is not folded",
              n ? (n->flags & NC_RE_FLAG_FOLD_CASE) != 0 : 1, 0);
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
    n = neverc_regexp_syntax_parse("\\x{D800}", 0, &err);
    check_not_null("\\x{D800} surrogate hex (Go accepts)", n);
    neverc_regexp_syntax_free(n);
    n = neverc_regexp_syntax_parse("\\x{DFFF}", 0, &err);
    check_not_null("\\x{DFFF} surrogate hex (Go accepts)", n);
    neverc_regexp_syntax_free(n);
    n = neverc_regexp_syntax_parse("\\x{10FFFF}", 0, &err);
    check_not_null("\\x{10FFFF}", n);
    neverc_regexp_syntax_free(n);

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

    n = neverc_regexp_syntax_parse("[^0-9]", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string [^0-9] is not fold-scoped", s, "[^0-9]");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse(".", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string .", s, ".");
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse(".", NC_RE_FLAG_DOT_NL, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string dot-all is self-contained", s, "(?s:.)");
    check_int("dot-all string round trip", node_round_trips(n), 1);
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("^", NC_RE_FLAG_MULTI_LINE, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string begin-line is self-contained", s, "(?m:^)");
    check_int("begin-line string round trip", node_round_trips(n), 1);
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("$", NC_RE_FLAG_MULTI_LINE, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string end-line is self-contained", s, "(?m:$)");
    check_int("end-line string round trip", node_round_trips(n), 1);
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse(
        "(?i:a)(?s:.)(?m:^)(?m:$)b", 0, &err);
    check_not_null("mixed scoped nodes", n);
    check_int("mixed scoped nodes string round trip", node_round_trips(n), 1);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?:)*", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string repeated empty match", s, "(?:)*");
    check_int("repeated empty match string round trip",
              node_round_trips(n), 1);
    free(s);
    neverc_regexp_syntax_free(n);

    /* Go TestToStringEquivalentParse: a quantifier applied to a quantifier
     * must print with (?: ), or the text reparses to a different tree. */
    {
        static const struct {
            const char *pattern;
            const char *want;
        } quantified[] = {
            {"(?:a+)?", "(?:a+)?"},
            {"(?:a*)*", "(?:a*)*"},
            {"(?:a*)+", "(?:a*)+"},
            {"(?:a?){2}", "(?:a?){2}"},
            {"(?:a{2}){3}", "(?:a{2}){3}"},
            {"(?:a{2,})?", "(?:a{2,})?"},
            {"(a+)?", "(a+)?"},
            {"(?:ab)*", "(?:ab)*"},
        };
        for (size_t i = 0; i < sizeof(quantified) / sizeof(quantified[0]);
             i++) {
            neverc_regexp_syntax_node_t *q =
                neverc_regexp_syntax_parse(quantified[i].pattern, 0, &err);
            check_not_null("quantified quantifier parses", q);
            char *text = neverc_regexp_syntax_string(q);
            check_str("quantified quantifier string", text,
                      quantified[i].want);
            check_int("quantified quantifier round trip",
                      node_round_trips(q), 1);
            free(text);
            neverc_regexp_syntax_free(q);
        }
    }

    {
        neverc_regexp_syntax_node_t *quest =
            neverc_regexp_syntax_parse("(?:a+)?", 0, &err);
        neverc_regexp_syntax_node_t *lazy =
            neverc_regexp_syntax_parse("a+?", 0, &err);
        check_op("(?:a+)? top op is quest", quest, NC_RE_OP_QUEST);
        check_int("(?:a+)? is not a+?",
                  neverc_regexp_syntax_equal(quest, lazy), 0);
        neverc_regexp_syntax_free(quest);
        neverc_regexp_syntax_free(lazy);
    }

    n = neverc_regexp_syntax_parse("(?:ab)c", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string nested concat preserves node", s, "(?:ab)c");
    check_int("nested concat string round trip", node_round_trips(n), 1);
    free(s);
    neverc_regexp_syntax_free(n);

    n = neverc_regexp_syntax_parse("(?:a|b)|c", 0, &err);
    s = neverc_regexp_syntax_string(n);
    check_str("string nested alternate preserves node", s, "(?:a|b)|c");
    check_int("nested alternate string round trip", node_round_trips(n), 1);
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
    check_not_null("unclosed {n is literal", n);
    neverc_regexp_syntax_free(n);

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

    n = neverc_regexp_syntax_parse("[[:^digit:]]", 0, &err);
    check_not_null("posix complement class", n);
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
    test_scoped_flags();
    test_parse_charclass();
    test_parse_escapes();
    test_fold_case();
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
