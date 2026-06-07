#include "neverc/regexp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected); }
}
static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_compile(void) {
    printf("[compile]\n");
    const char *err;
    neverc_regexp_t *re = neverc_regexp_compile("hello", &err);
    check_bool("compile ok", re != NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("[a-z]+", &err);
    check_bool("class ok", re != NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(a|b)*c+", &err);
    check_bool("complex ok", re != NULL, 1);
    neverc_regexp_free(re);
}

static void test_match(void) {
    printf("[match]\n");
    check_bool("literal match", neverc_regexp_match_string("hello", "hello"), 1);
    check_bool("literal no match", neverc_regexp_match_string("hello", "world"), 0);
    check_bool("dot match", neverc_regexp_match_string("h.llo", "hello"), 1);
    check_bool("star match", neverc_regexp_match_string("he*llo", "hllo"), 1);
    check_bool("star match 2", neverc_regexp_match_string("he*llo", "heello"), 1);
    check_bool("plus match", neverc_regexp_match_string("he+llo", "hello"), 1);
    check_bool("plus no match", neverc_regexp_match_string("he+llo", "hllo"), 0);
    check_bool("question match", neverc_regexp_match_string("he?llo", "hllo"), 1);
    check_bool("question match 2", neverc_regexp_match_string("he?llo", "hello"), 1);
    check_bool("alt match a", neverc_regexp_match_string("cat|dog", "cat"), 1);
    check_bool("alt match b", neverc_regexp_match_string("cat|dog", "dog"), 1);
    check_bool("alt no match", neverc_regexp_match_string("cat|dog", "bird"), 0);
    check_bool("group match", neverc_regexp_match_string("(ab)+", "abab"), 1);
}

static void test_character_classes(void) {
    printf("[character classes]\n");
    check_bool("[a-z]", neverc_regexp_match_string("[a-z]+", "hello"), 1);
    check_bool("[a-z] no", neverc_regexp_match_string("[a-z]+", "HELLO"), 0);
    check_bool("[A-Za-z]", neverc_regexp_match_string("[A-Za-z]+", "Hello"), 1);
    check_bool("[^0-9]", neverc_regexp_match_string("[^0-9]+", "abc"), 1);
    check_bool("\\d", neverc_regexp_match_string("\\d+", "12345"), 1);
    check_bool("\\d no", neverc_regexp_match_string("\\d+", "abc"), 0);
    check_bool("\\w", neverc_regexp_match_string("\\w+", "hello_123"), 1);
    check_bool("\\s", neverc_regexp_match_string("\\s+", "  \t"), 1);
}

static void test_find(void) {
    printf("[find]\n");
    neverc_regexp_t *re = neverc_regexp_compile("[0-9]+", NULL);

    size_t mlen;
    const char *m = neverc_regexp_find(re, "abc 123 def", &mlen);
    check_bool("find not null", m != NULL, 1);
    if (m) {
        char buf[32];
        memcpy(buf, m, mlen); buf[mlen] = '\0';
        check_str("find match", buf, "123");
    }

    m = neverc_regexp_find(re, "no digits here", &mlen);
    check_bool("find null", m == NULL, 1);

    neverc_regexp_free(re);
}

static void test_find_all(void) {
    printf("[find_all]\n");
    neverc_regexp_t *re = neverc_regexp_compile("[0-9]+", NULL);

    int count;
    char **matches = neverc_regexp_find_all(re, "abc 12 def 345 ghi 6", -1, &count);
    check_int("find_all count", count, 3);
    if (count >= 3) {
        check_str("match 0", matches[0], "12");
        check_str("match 1", matches[1], "345");
        check_str("match 2", matches[2], "6");
    }
    neverc_regexp_free_strings(matches, count);

    matches = neverc_regexp_find_all(re, "a1b2c3", 2, &count);
    check_int("find_all n=2", count, 2);
    neverc_regexp_free_strings(matches, count);

    neverc_regexp_free(re);
}

static void test_replace(void) {
    printf("[replace]\n");
    neverc_regexp_t *re = neverc_regexp_compile("[aeiou]", NULL);
    size_t outlen;
    char *result = neverc_regexp_replace_all(re, "hello world", "*", &outlen);
    check_str("replace vowels", result, "h*ll* w*rld");
    free(result);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("\\d+", NULL);
    result = neverc_regexp_replace_all(re, "abc 123 def 456", "NUM", &outlen);
    check_str("replace digits", result, "abc NUM def NUM");
    free(result);
    neverc_regexp_free(re);
}

static void test_anchors(void) {
    printf("[anchors]\n");
    check_bool("^hello match", neverc_regexp_match_string("^hello", "hello"), 1);
    check_bool("world$ match", neverc_regexp_match_string("world$", "world"), 1);
    check_bool("^hello$ full", neverc_regexp_match_string("^hello$", "hello"), 1);
    check_bool("^hello$ no", neverc_regexp_match_string("^hello$", "hello world"), 0);
}

static void test_empty_and_edge_cases(void) {
    printf("[edge cases]\n");
    check_bool("empty pattern", neverc_regexp_match_string("", ""), 1);
    check_bool("a*", neverc_regexp_match_string("a*", ""), 1);
    check_bool("a* aaa", neverc_regexp_match_string("a*", "aaa"), 1);
    check_bool("escaped dot", neverc_regexp_match_string("\\.", "."), 1);
    check_bool("escaped dot no", neverc_regexp_match_string("\\.", "a"), 0);
}

static void test_quote_meta(void) {
    printf("[quote_meta]\n");
    char *q = neverc_regexp_quote_meta("hello");
    check_bool("quote_meta plain", strcmp(q, "hello") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("a.b+c*d?e");
    check_bool("quote_meta special", strcmp(q, "a\\.b\\+c\\*d\\?e") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("[foo](bar){baz}");
    check_bool("quote_meta brackets", strcmp(q, "\\[foo\\]\\(bar\\)\\{baz\\}") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("^start|end$");
    check_bool("quote_meta anchors", strcmp(q, "\\^start\\|end\\$") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("");
    check_bool("quote_meta empty", strcmp(q, "") == 0, 1);
    free(q);

    /* Verify quoted pattern matches literally */
    q = neverc_regexp_quote_meta("a.b+c");
    neverc_regexp_t *re = neverc_regexp_must_compile(q);
    check_bool("quote_meta literal match", neverc_regexp_match(re, "a.b+c"), 1);
    check_bool("quote_meta no wild match", neverc_regexp_match(re, "axbbc"), 0);
    neverc_regexp_free(re);
    free(q);
}

static void test_must_compile(void) {
    printf("[must_compile]\n");
    neverc_regexp_t *re = neverc_regexp_must_compile("[a-z]+");
    check_bool("must_compile ok", re != NULL, 1);
    check_bool("must_compile match", neverc_regexp_match(re, "hello"), 1);
    neverc_regexp_free(re);
}

int main(void) {
    printf("=== NeverC Regexp Module Tests ===\n\n");
    test_compile();
    test_match();
    test_character_classes();
    test_find();
    test_find_all();
    test_replace();
    test_anchors();
    test_empty_and_edge_cases();
    test_quote_meta();
    test_must_compile();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
