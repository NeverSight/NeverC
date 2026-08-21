#include "neverc/std/regexp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void section(const char *title) {
    printf("%s\n", title);
    fflush(stdout);
}

static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got %d, expected %d\n", name, got, expected);
        fflush(stdout);
    }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected);
        fflush(stdout);
    }
}
static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got %d, expected %d\n", name, got, expected);
        fflush(stdout);
    }
}

static void test_compile(void) {
    section("[compile]");
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
    section("[match]");
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
    check_bool("noncap group", neverc_regexp_match_string("(?:ab)+", "abab"), 1);
    check_bool("noncap group no", neverc_regexp_match_string("(?:ab)+", "abx"), 0);
}

static void test_character_classes(void) {
    section("[character classes]");
    check_bool("[a-z]", neverc_regexp_match_string("[a-z]+", "hello"), 1);
    check_bool("[a-z] no", neverc_regexp_match_string("[a-z]+", "HELLO"), 0);
    check_bool("[A-Za-z]", neverc_regexp_match_string("[A-Za-z]+", "Hello"), 1);
    check_bool("[^0-9]", neverc_regexp_match_string("[^0-9]+", "abc"), 1);
    check_bool("\\d", neverc_regexp_match_string("\\d+", "12345"), 1);
    check_bool("\\d no", neverc_regexp_match_string("\\d+", "abc"), 0);
    check_bool("\\w", neverc_regexp_match_string("\\w+", "hello_123"), 1);
    check_bool("\\s", neverc_regexp_match_string("\\s+", "  \t"), 1);
    check_bool("\\s formfeed", neverc_regexp_match_string("\\s", "\f"), 1);
    check_bool("\\s vtab", neverc_regexp_match_string("\\s", "\v"), 1);
    check_bool("[\\s] formfeed", neverc_regexp_match_string("[\\s]", "\f"), 1);
    check_bool("[\\s] vtab", neverc_regexp_match_string("[\\s]", "\v"), 1);
    check_bool("\\n newline", neverc_regexp_match_string("\\n", "\n"), 1);
    check_bool("[\\n] newline", neverc_regexp_match_string("[\\n]", "\n"), 1);
    check_bool("[\\t-\\n] tab", neverc_regexp_match_string("[\\t-\\n]", "\t"), 1);
    check_bool("[\\t-\\n] newline", neverc_regexp_match_string("[\\t-\\n]", "\n"), 1);
    check_bool("[\\t-\\n] not backslash",
               neverc_regexp_match_string("[\\t-\\n]", "\\"), 0);
    check_bool("\\n not letter n", neverc_regexp_match_string("\\n", "n"), 0);
    check_bool("[]] literal bracket", neverc_regexp_match_string("[]]", "]"), 1);
    check_bool("[\\.] escaped dot in class", neverc_regexp_match_string("[\\.]", "."), 1);
    check_bool("[\\.] not letter", neverc_regexp_match_string("[\\.]", "a"), 0);
    check_bool("[]a] a", neverc_regexp_match_string("[]a]", "a"), 1);
    check_bool("[]a] bracket", neverc_regexp_match_string("[]a]", "]"), 1);
    check_bool("[\\D] letter", neverc_regexp_match_string("[\\D]", "a"), 1);
    check_bool("[\\D] digit", neverc_regexp_match_string("[\\D]", "0"), 0);
    check_bool("{ literal", neverc_regexp_match_string("{", "{"), 1);
    check_bool("{3} literal braces", neverc_regexp_match_string("{3}", "{3}"), 1);
    check_bool("] literal", neverc_regexp_match_string("]", "]"), 1);
    check_bool("a] literal", neverc_regexp_match_string("a]", "a]"), 1);
    check_bool("\\x41 is A", neverc_regexp_match_string("\\x41", "A"), 1);
    check_bool("\\x61 is a", neverc_regexp_match_string("\\x61", "a"), 1);
    /* NeverC C hex is greedy: "\x96B" is one value, so split the text literal. */
    check_bool("\\x96B", neverc_regexp_match_string("\\x96B", "\x96" "B"), 1);
    check_bool("\\x96 not whole", neverc_regexp_match_string("\\x96", "\x96" "B"), 0);
    check_bool("\\x96 raw byte", neverc_regexp_match_string("\\x96", "\x96" ""), 1);
    check_bool("\\x{96} is UTF-8", neverc_regexp_match_string("\\x{96}", "\xC2\x96"), 1);
    check_bool("\\x{96} not raw", neverc_regexp_match_string("\\x{96}", "\x96" ""), 0);
    check_bool("\\x{41} is A", neverc_regexp_match_string("\\x{41}", "A"), 1);
    check_bool("[\\x41-\\x43] B", neverc_regexp_match_string("[\\x41-\\x43]", "B"), 1);
    check_bool("[\\x41-\\x43] D no", neverc_regexp_match_string("[\\x41-\\x43]", "D"), 0);
    check_bool("\\a bell", neverc_regexp_match_string("\\a", "\a"), 1);
}

static void test_find(void) {
    section("[find]");
    neverc_regexp_t *re = neverc_regexp_compile("[0-9]+", NULL);

    size_t mlen;
    const char *m = neverc_regexp_find(re, "abc 123 def", &mlen);
    check_bool("find not null", m != NULL, 1);
    if (m) {
        char buf[32];
        if (mlen > 31) mlen = 31;
        memcpy(buf, m, mlen); buf[mlen] = '\0';
        check_str("find match", buf, "123");
    }

    m = neverc_regexp_find(re, "no digits here", &mlen);
    check_bool("find null", m == NULL, 1);

    neverc_regexp_free(re);
}

static void test_find_submatch(void) {
    section("[find_submatch]");
    neverc_regexp_match_t m[4];
    memset(m, 0, sizeof(m));
    neverc_regexp_t *re = neverc_regexp_compile("(a+)(b+)", NULL);
    check_bool("(a+)(b+) compiles", re != NULL, 1);
    printf("  find (a+)(b+)\n");
    {
        size_t flen = 0;
        const char *f = re ? neverc_regexp_find(re, "xxaaabbcyy", &flen) : NULL;
        printf("  find returned %s len=%zu\n", f ? "hit" : "null", flen);
    }
    printf("  submatch (a+)(b+)\n");
    int n = neverc_regexp_find_submatch(re, "xxaaabbcyy", m, 3);
    printf("  submatch returned %d full=%zu g1=%zu g2=%zu\n",
           n, m[0].len, m[1].len, m[2].len);
    check_int("submatch found", n, 1);
    check_int("submatch full len", (int)m[0].len, 5);
    check_int("group1 len", (int)m[1].len, 3);
    check_int("group2 len", (int)m[2].len, 2);
    if (m[1].start && m[1].len == 3)
        check_int("group1 a", m[1].start[0] == 'a' && m[1].start[2] == 'a', 1);
    if (m[2].start && m[2].len == 2)
        check_int("group2 b", m[2].start[0] == 'b' && m[2].start[1] == 'b', 1);
    neverc_regexp_free(re);

    printf("  submatch (?:ab)(c)\n");
    re = neverc_regexp_compile("(?:ab)(c)", NULL);
    memset(m, 0, sizeof(m));
    n = neverc_regexp_find_submatch(re, "abc", m, 2);
    printf("  noncap returned %d g1=%zu\n", n, m[1].len);
    check_int("noncap submatch", n, 1);
    check_int("noncap group1 len", (int)m[1].len, 1);
    if (m[1].start) check_int("noncap group1 c", m[1].start[0] == 'c', 1);
    neverc_regexp_free(re);

    /* Capture index: max_matches=1 must not write past the caller's array. */
    re = neverc_regexp_compile("(a)(b)", NULL);
    {
        neverc_regexp_match_t slot[2];
        const char *sent = "SENTINEL";
        slot[1].start = sent;
        slot[1].len = 99;
        n = neverc_regexp_find_submatch(re, "ab", slot, 1);
        check_int("max_matches=1 found", n, 1);
        check_bool("max_matches=1 leaves [1]", slot[1].start == sent && slot[1].len == 99, 1);
        memset(slot, 0xAA, sizeof(slot));
        n = neverc_regexp_find_submatch(re, "ab", slot, 2);
        check_int("max_matches=2 found", n, 1);
        check_int("max_matches=2 g1", (int)slot[1].len, 1);
    }
    neverc_regexp_free(re);

    /* Empty-width: ()* must not loop; find stays non-empty so no match. */
    printf("  find_all ()*\n");
    re = neverc_regexp_compile("()*", NULL);
    check_bool("()* compiles", re != NULL, 1);
    int count = 0;
    char **all = neverc_regexp_find_all(re, "abc", -1, &count);
    printf("  find_all ()* count=%d\n", count);
    check_int("empty-width find_all count", count, 0);
    neverc_regexp_free_strings(all, count);

    all = neverc_regexp_find_all(re, "\xFF\xFF", -1, &count);
    check_int("empty-width on invalid UTF-8 text", count, 0);
    neverc_regexp_free_strings(all, count);
    neverc_regexp_free(re);

    /* Classic empty-width loop in backtracking engines: (a|)* */
    re = neverc_regexp_compile("(a|)*", NULL);
    check_bool("(a|)* compiles", re != NULL, 1);
    check_bool("(a|)* empty", neverc_regexp_match(re, ""), 1);
    check_bool("(a|)* aaa", neverc_regexp_match(re, "aaa"), 1);
    check_bool("(a|)* bbb no full", neverc_regexp_match(re, "bbb"), 0);
    all = neverc_regexp_find_all(re, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", -1, &count);
    check_int("(a|)* find_all non-empty", count, 0);
    neverc_regexp_free_strings(all, count);
    size_t outlen = 0;
    char *rep = neverc_regexp_replace_all(re, "xyz", "Z", &outlen);
    check_str("(a|)* replace no empty inserts", rep, "xyz");
    free(rep);
    int scount = 0;
    char **parts = neverc_regexp_split(re, "xyz", -1, &scount);
    check_int("(a|)* split count", scount, 1);
    if (scount >= 1) check_str("(a|)* split[0]", parts[0], "xyz");
    neverc_regexp_free_strings(parts, scount);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("()+", NULL);
    check_bool("()+ compiles", re != NULL, 1);
    all = neverc_regexp_find_all(re, "abc", -1, &count);
    check_int("()+ find_all", count, 0);
    neverc_regexp_free_strings(all, count);
    neverc_regexp_free(re);
}

static void test_find_all(void) {
    section("[find_all]");
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
    section("[replace]");
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
    section("[anchors]");
    check_bool("^hello match", neverc_regexp_match_string("^hello", "hello"), 1);
    check_bool("world$ match", neverc_regexp_match_string("world$", "world"), 1);
    check_bool("^hello$ full", neverc_regexp_match_string("^hello$", "hello"), 1);
    check_bool("^hello$ no", neverc_regexp_match_string("^hello$", "hello world"), 0);
}

static void test_empty_and_edge_cases(void) {
    section("[edge cases]");
    check_bool("empty pattern", neverc_regexp_match_string("", ""), 1);
    check_bool("a*", neverc_regexp_match_string("a*", ""), 1);
    check_bool("a* aaa", neverc_regexp_match_string("a*", "aaa"), 1);
    check_bool("escaped dot", neverc_regexp_match_string("\\.", "."), 1);
    check_bool("escaped dot no", neverc_regexp_match_string("\\.", "a"), 0);
}

static void test_quote_meta(void) {
    section("[quote_meta]");
    char *q = neverc_regexp_quote_meta("hello");
    check_bool("quote_meta plain", q && strcmp(q, "hello") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("a.b+c*d?e");
    check_bool("quote_meta special", q && strcmp(q, "a\\.b\\+c\\*d\\?e") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("[foo](bar){baz}");
    check_bool("quote_meta brackets", q && strcmp(q, "\\[foo\\]\\(bar\\)\\{baz\\}") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("^start|end$");
    check_bool("quote_meta anchors", q && strcmp(q, "\\^start\\|end\\$") == 0, 1);
    free(q);

    q = neverc_regexp_quote_meta("");
    check_bool("quote_meta empty", q && strcmp(q, "") == 0, 1);
    free(q);

    /* Verify quoted pattern matches literally */
    q = neverc_regexp_quote_meta("a.b+c");
    neverc_regexp_t *re = neverc_regexp_must_compile(q);
    check_bool("quote_meta literal match", neverc_regexp_match(re, "a.b+c"), 1);
    check_bool("quote_meta no wild match", neverc_regexp_match(re, "axbbc"), 0);
    neverc_regexp_free(re);
    free(q);
}

/* Anchored find/find_all/replace: not covered by the differential test (its
 * brute-force reference matches substrings, where ^/$ would mean the substring
 * edge rather than the whole-text edge). These pin the single-pass engine's
 * position-aware anchor handling to known-correct results. */
static const char *find_str(neverc_regexp_t *re, const char *s, char *buf) {
    size_t mlen;
    const char *m = neverc_regexp_find(re, s, &mlen);
    if (!m) return NULL;
    if (mlen > 63) mlen = 63;
    memcpy(buf, m, mlen); buf[mlen] = '\0';
    return buf;
}

static void test_find_anchors(void) {
    section("[find anchors]");
    char buf[64];
    size_t mlen;

    neverc_regexp_t *re = neverc_regexp_compile("^abc", NULL);
    check_str("^abc in abcx", find_str(re, "abcx", buf), "abc");
    check_bool("^abc in xabc", neverc_regexp_find(re, "xabc", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("abc$", NULL);
    check_str("abc$ in abcabc", find_str(re, "abcabc", buf), "abc");
    {   /* the match must be the trailing one (offset 3), not the leading one */
        const char *m = neverc_regexp_find(re, "abcabc", &mlen);
        check_int("abc$ offset", m ? (int)(m - "abcabc") : -1, 3);
    }
    check_bool("abc$ in abcx", neverc_regexp_find(re, "abcx", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("^abc$", NULL);
    check_str("^abc$ exact", find_str(re, "abc", buf), "abc");
    check_bool("^abc$ no trail", neverc_regexp_find(re, "abcd", &mlen) == NULL, 1);
    check_bool("^abc$ no lead", neverc_regexp_find(re, "xabc", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("a^b", NULL);   /* mid-pattern ^ can never match */
    check_bool("a^b impossible", neverc_regexp_find(re, "ab", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    /* find_all: ^a only matches once (at offset 0) */
    re = neverc_regexp_compile("^a", NULL);
    int count;
    char **ms = neverc_regexp_find_all(re, "aaa", -1, &count);
    check_int("^a find_all count", count, 1);
    if (count >= 1) check_str("^a find_all[0]", ms[0], "a");
    neverc_regexp_free_strings(ms, count);
    neverc_regexp_free(re);

    /* replace_all with trailing anchor only touches the final match */
    re = neverc_regexp_compile("a$", NULL);
    size_t outlen;
    char *r = neverc_regexp_replace_all(re, "xaa", "Z", &outlen);
    check_str("a$ replace", r, "xaZ");
    free(r);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("[0-9]+$", NULL);
    check_str("digits$ tail", find_str(re, "a1b22c333", buf), "333");
    neverc_regexp_free(re);

    /* RE2/Go: $ also matches before a final trailing newline */
    re = neverc_regexp_compile("a$", NULL);
    check_str("a$ before final NL", find_str(re, "a\n", buf), "a");
    check_bool("a$ not before mid NL",
               neverc_regexp_find(re, "a\nb", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    check_bool("$\\n matches NL", neverc_regexp_match_string("$\n", "\n"), 1);
    check_bool("a$\\n matches aNL", neverc_regexp_match_string("a$\n", "a\n"), 1);
    check_bool("a$ does not consume NL",
               neverc_regexp_match_string("a$", "a\n"), 0);
}

/* Bounded repetition {n}, {n,}, {n,m}: previously parsed but silently ignored
 * (a no-op), so these are all new behavior the engine must now honour. */
static void test_repeat_braces(void) {
    section("[repeat braces]");
    /* exact count */
    check_bool("a{3} aaa",  neverc_regexp_match_string("^a{3}$", "aaa"), 1);
    check_bool("a{3} aa",   neverc_regexp_match_string("^a{3}$", "aa"), 0);
    check_bool("a{3} aaaa", neverc_regexp_match_string("^a{3}$", "aaaa"), 0);
    /* range */
    check_bool("a{2,4} a",     neverc_regexp_match_string("^a{2,4}$", "a"), 0);
    check_bool("a{2,4} aa",    neverc_regexp_match_string("^a{2,4}$", "aa"), 1);
    check_bool("a{2,4} aaaa",  neverc_regexp_match_string("^a{2,4}$", "aaaa"), 1);
    check_bool("a{2,4} aaaaa", neverc_regexp_match_string("^a{2,4}$", "aaaaa"), 0);
    /* unbounded n, */
    check_bool("a{2,} a",      neverc_regexp_match_string("^a{2,}$", "a"), 0);
    check_bool("a{2,} aa",     neverc_regexp_match_string("^a{2,}$", "aa"), 1);
    check_bool("a{2,} a*6",    neverc_regexp_match_string("^a{2,}$", "aaaaaa"), 1);
    /* zero lower bound */
    check_bool("a{0,2} empty", neverc_regexp_match_string("^a{0,2}$", ""), 1);
    check_bool("a{0,2} aa",    neverc_regexp_match_string("^a{0,2}$", "aa"), 1);
    check_bool("a{0,2} aaa",   neverc_regexp_match_string("^a{0,2}$", "aaa"), 0);
    check_bool("a{0} empty",   neverc_regexp_match_string("^a{0}$", ""), 1);
    check_bool("a{0} a",       neverc_regexp_match_string("^a{0}$", "a"), 0);
    /* group and class as the repeated unit */
    check_bool("(ab){2} abab",   neverc_regexp_match_string("^(ab){2}$", "abab"), 1);
    check_bool("(ab){2} ab",     neverc_regexp_match_string("^(ab){2}$", "ab"), 0);
    check_bool("[0-9]{3} 123",   neverc_regexp_match_string("^[0-9]{3}$", "123"), 1);
    check_bool("[0-9]{3} 12",    neverc_regexp_match_string("^[0-9]{3}$", "12"), 0);
    /* nested bounded repeats */
    check_bool("(a{2}){2} aaaa", neverc_regexp_match_string("^(a{2}){2}$", "aaaa"), 1);
    check_bool("(a{2}){2} aaa",  neverc_regexp_match_string("^(a{2}){2}$", "aaa"), 0);

    /* Long optional chain: epsilon-closure used to recurse and SIGSEGV on
     * linux-arm64 neverc frames. Must stay iterative. */
    {
        neverc_regexp_t *long_re = neverc_regexp_compile("(a?){400}", NULL);
        check_bool("(a?){400} compiles", long_re != NULL, 1);
        check_bool("(a?){400} match aaa", neverc_regexp_match(long_re, "aaa"), 1);
        size_t flen = 0;
        const char *fm = neverc_regexp_find(long_re, "xxaaayy", &flen);
        check_bool("(a?){400} find", fm != NULL, 1);
        neverc_regexp_match_t sm[2];
        memset(sm, 0, sizeof(sm));
        check_int("(a?){400} submatch",
                  neverc_regexp_find_submatch(long_re, "xxaaayy", sm, 2), 1);
        neverc_regexp_free(long_re);
    }

    /* find returns the leftmost-longest bounded match */
    char buf[64];
    neverc_regexp_t *re = neverc_regexp_compile("a{2,3}", NULL);
    check_str("a{2,3} find", find_str(re, "baaaab", buf), "aaa");
    neverc_regexp_free(re);

    /* a count over the cap is rejected rather than silently expanded */
    const char *err = NULL;
    re = neverc_regexp_compile("a{5000}", &err);
    check_bool("a{5000} rejected", re == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("a{999999999999999999999999999999}", &err);
    check_bool("huge repeat rejected", re == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("a{4,2}", &err);
    check_bool("descending repeat rejected", re == NULL, 1);
    neverc_regexp_free(re);

    /* `{` not followed by a digit is a literal '{' (matches Go) */
    check_bool("literal {", neverc_regexp_match_string("^a{$", "a{"), 1);
    check_bool("a{} literal braces", neverc_regexp_match_string("^a{}$", "a{}"), 1);
    check_bool("a{*} matches a}", neverc_regexp_match_string("a{*}", "a}"), 1);
    check_bool("a{*} matches a{}", neverc_regexp_match_string("a{*}", "a{}"), 1);
    check_bool("a{*} does not match }", neverc_regexp_match_string("a{*}", "}"), 0);

    /* `{n` that starts a repeat but never closes is a literal (matches Go) */
    err = NULL;
    re = neverc_regexp_compile("a{3", &err);
    check_bool("a{3 unclosed compiles", re != NULL, 1);
    check_bool("a{3 unclosed matches literal",
               re && neverc_regexp_match(re, "a{3"), 1);
    neverc_regexp_free(re);

    err = NULL;
    re = neverc_regexp_compile("[a-\\d]", &err);
    check_bool("[a-\\d] range rejected", re == NULL, 1);
    neverc_regexp_free(re);

    err = NULL;
    re = neverc_regexp_compile("\\x", &err);
    check_bool("\\x rejected", re == NULL, 1);
    neverc_regexp_free(re);

    err = NULL;
    re = neverc_regexp_compile("\\x4", &err);
    check_bool("\\x4 rejected", re == NULL, 1);
    neverc_regexp_free(re);

    err = NULL;
    re = neverc_regexp_compile("\\xGG", &err);
    check_bool("\\xGG rejected", re == NULL, 1);
    neverc_regexp_free(re);
}

static void test_invalid_inputs(void) {
    section("[invalid inputs]");
    const char *err = NULL;
    neverc_regexp_t *re = neverc_regexp_compile(NULL, &err);
    check_bool("null pattern rejected", re == NULL && err != NULL, 1);

    static const char *invalid[] = {
        "[abc", "[]", "[z-a]", "a)", "\\", "a**",
        "\\q", "\\1", "\\8", "(?P<>x)", "(?P<foo-bar>x)", "(?Pname>x)",
        "(?P foo>x)", "[[:foo:]]",
        "[\\q]", "[\\1]", "[\\A]", "[a-\\q]"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        err = NULL;
        re = neverc_regexp_compile(invalid[i], &err);
        check_bool(invalid[i], re == NULL && err != NULL, 1);
        neverc_regexp_free(re);
    }

    /* Go regexp/syntax: invalid UTF-8 in the pattern is ErrInvalidUTF8. */
    {
        char raw_ff[] = { 'a', (char)0xFF, 'b', 0 };
        err = NULL;
        re = neverc_regexp_compile(raw_ff, &err);
        check_bool("raw 0xFF pattern rejected", re == NULL && err != NULL, 1);
        neverc_regexp_free(re);

        char trunc[] = { (char)0xC3, 0 };
        err = NULL;
        re = neverc_regexp_compile(trunc, &err);
        check_bool("truncated UTF-8 pattern rejected", re == NULL && err != NULL, 1);
        neverc_regexp_free(re);

        char overlong[] = { (char)0xC0, (char)0x80, 0 };
        err = NULL;
        re = neverc_regexp_compile(overlong, &err);
        check_bool("overlong UTF-8 pattern rejected", re == NULL && err != NULL, 1);
        neverc_regexp_free(re);
    }

    /* Go parseInt: leading zeros make `{` a literal, not a{1}. */
    check_bool("a{01} matches literal", neverc_regexp_match_string("a{01}", "a{01}"), 1);
    check_bool("a{01} is not a{1}", neverc_regexp_match_string("a{01}", "a"), 0);
    check_bool("a{00} matches literal", neverc_regexp_match_string("a{00}", "a{00}"), 1);
    check_bool("a{0} still empty", neverc_regexp_match_string("^a{0}$", ""), 1);
    check_bool("a{0,01} is literal", neverc_regexp_match_string("a{0,01}", "a{0,01}"), 1);

    size_t match_len = 99;
    check_bool("match null regexp", neverc_regexp_match(NULL, "x"), 0);
    re = neverc_regexp_compile("x", NULL);
    check_bool("match null text", neverc_regexp_match(re, NULL), 0);
    check_bool("find null regexp",
               neverc_regexp_find(NULL, "x", &match_len) == NULL && match_len == 0,
               1);
    match_len = 99;
    check_bool("find null text",
               neverc_regexp_find(re, NULL, &match_len) == NULL && match_len == 0,
               1);
    check_bool("find accepts null length",
               neverc_regexp_find(re, "x", NULL) != NULL, 1);
    check_bool("submatch null output",
               neverc_regexp_find_submatch(re, "x", NULL, 1), 1);
    neverc_regexp_free(re);

    check_bool("quote null", neverc_regexp_quote_meta(NULL) == NULL, 1);
}

static void test_must_compile(void) {
    section("[must_compile]");
    neverc_regexp_t *re = neverc_regexp_must_compile("[a-z]+");
    check_bool("must_compile ok", re != NULL, 1);
    check_bool("must_compile match", neverc_regexp_match(re, "hello"), 1);
    neverc_regexp_free(re);

    const char *err = NULL;
    re = neverc_regexp_compile_posix("[a-z]+", &err);
    check_bool("compile_posix ok", re != NULL, 1);
    check_bool("compile_posix match", neverc_regexp_match(re, "hello"), 1);
    neverc_regexp_free(re);

    re = neverc_regexp_must_compile_posix("[0-9]+");
    check_bool("must_compile_posix ok", re != NULL, 1);
    check_bool("must_compile_posix match", neverc_regexp_match(re, "123"), 1);
    neverc_regexp_free(re);
}

/* ---- differential test: optimized search vs brute-force reference ----
 * The reference finds the leftmost-longest non-empty match by testing, for each
 * (i, j), whether the pattern matches the substring s[i:j] exactly (via the
 * anchored match API). It is independent of find()'s scanning/first-byte logic,
 * so agreement over many random anchor-free patterns proves the optimization
 * (shared context + first-byte skip) preserves semantics. */

static uint64_t rrng = 0x243f6a8885a308d3ULL;
static unsigned rr(void) {
    rrng ^= rrng << 13; rrng ^= rrng >> 7; rrng ^= rrng << 17;
    return (unsigned)(rrng >> 32);
}

/* leftmost i, longest j>i such that pattern matches s[i:j] exactly. */
static int ref_find(neverc_regexp_t *re, const char *s, size_t *rs, size_t *rl) {
    size_t slen = strlen(s);
    char buf[64];
    for (size_t i = 0; i <= slen; i++) {
        for (size_t j = slen; j > i; j--) {
            size_t L = j - i;
            if (L >= sizeof(buf)) continue;
            memcpy(buf, s + i, L); buf[L] = '\0';
            if (neverc_regexp_match(re, buf)) { *rs = i; *rl = L; return 1; }
        }
    }
    return 0;
}

/* random anchor-free pattern over a small alphabet */
static void gen_pattern(char *out) {
    int p = 0;
    int units = 1 + (int)(rr() % 4);
    for (int u = 0; u < units; u++) {
        int atom = (int)(rr() % 6);
        if (atom < 3) {
            out[p++] = (char)('a' + (rr() % 4));
        } else if (atom == 3) {
            out[p++] = '.';
        } else if (atom == 4) {
            out[p++] = '[';
            if (rr() & 1) out[p++] = '^';
            int cls = 1 + (int)(rr() % 3);
            for (int k = 0; k < cls; k++) out[p++] = (char)('a' + (rr() % 4));
            out[p++] = ']';
        } else {
            out[p++] = '(';
            out[p++] = (char)('a' + (rr() % 4));
            out[p++] = '|';
            out[p++] = (char)('a' + (rr() % 4));
            out[p++] = ')';
        }
        int q = (int)(rr() % 4);            /* 0:none 1:* 2:+ 3:? */
        if (q == 1) out[p++] = '*';
        else if (q == 2) out[p++] = '+';
        else if (q == 3) out[p++] = '?';
    }
    out[p] = '\0';
}

static void test_submatch_and_hex(void) {
    section("[submatch_and_hex]");
    neverc_regexp_match_t m[3];
    neverc_regexp_t *re = neverc_regexp_compile("(\\x41+)(\\x42+)", NULL);
    check_bool("hex groups compile", re != NULL, 1);
    int n = neverc_regexp_find_submatch(re, "xxAAABBByy", m, 3);
    check_int("hex groups found", n, 1);
    check_int("hex full len", (int)m[0].len, 6);
    check_int("hex group1 A", (int)m[1].len, 3);
    check_int("hex group2 B", (int)m[2].len, 3);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(\\x{41})", NULL);
    memset(m, 0, sizeof(m));
    n = neverc_regexp_find_submatch(re, "A", m, 2);
    check_int("brace hex group", n, 1);
    check_int("brace hex len", (int)m[1].len, 1);
    if (m[1].start)
        check_int("brace hex A", m[1].start[0] == 'A', 1);
    neverc_regexp_free(re);
}

static void test_word_bounds_and_text_anchors(void) {
    section("[word bounds / \\\\A \\\\z]");
    check_bool("\\bfoo in foo", neverc_regexp_match_string("\\bfoo\\b", "foo"), 1);
    check_bool("\\bfoo in xfoo", neverc_regexp_match_string("\\bfoo\\b", "xfoo"), 0);
    /* Matching APIs require the whole string; `foo!` is a find, not a match. */
    check_bool("\\bfoo whole foo!", neverc_regexp_match_string("\\bfoo\\b", "foo!"), 0);
    check_bool("\\Boo whole foo", neverc_regexp_match_string("\\Boo", "foo"), 0);
    check_bool("\\Bfoo no", neverc_regexp_match_string("\\Bfoo", "foo"), 0);

    neverc_regexp_t *re = neverc_regexp_compile("\\bfoo\\b", NULL);
    char buf[64];
    check_str("\\bfoo find in foo!", find_str(re, "foo!", buf), "foo");
    check_str("\\bfoo find in bar foo baz", find_str(re, "bar foo baz", buf), "foo");
    size_t mlen = 99;
    check_bool("\\bfoo find in xfooy", neverc_regexp_find(re, "xfooy", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("\\Boo", NULL);
    check_str("\\Boo find in foo", find_str(re, "foo", buf), "oo");
    neverc_regexp_free(re);

    check_bool("\\Ahello", neverc_regexp_match_string("\\Ahello", "hello"), 1);
    re = neverc_regexp_compile("\\Aabc", NULL);
    check_bool("\\Aabc in xabc", neverc_regexp_find(re, "xabc", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("a\\z", NULL);
    check_str("a\\z exact", find_str(re, "a", buf), "a");
    check_bool("a\\z not before final NL",
               neverc_regexp_find(re, "a\n", &mlen) == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("a$", NULL);
    check_str("a$ still before final NL", find_str(re, "a\n", buf), "a");
    neverc_regexp_free(re);

    check_bool("[\\b] backspace", neverc_regexp_match_string("[\\b]", "\b"), 1);
    check_bool("[\\b] not letter b", neverc_regexp_match_string("[\\b]", "b"), 0);
}

static void test_posix_classes(void) {
    section("[posix classes]");
    check_bool("[[:digit:]] 5", neverc_regexp_match_string("[[:digit:]]", "5"), 1);
    check_bool("[[:digit:]] a no", neverc_regexp_match_string("[[:digit:]]", "a"), 0);
    check_bool("[[:digit:]] not colon-bracket",
               neverc_regexp_match_string("[[:digit:]]", ":]"), 0);
    check_bool("[[:alpha:]] Q", neverc_regexp_match_string("[[:alpha:]]", "Q"), 1);
    check_bool("[[:alpha:]] 9 no", neverc_regexp_match_string("[[:alpha:]]", "9"), 0);
    check_bool("[[:space:]] formfeed", neverc_regexp_match_string("[[:space:]]", "\f"), 1);
    check_bool("[[:word:]] _", neverc_regexp_match_string("[[:word:]]", "_"), 1);
    check_bool("[^[:digit:]] a", neverc_regexp_match_string("[^[:digit:]]", "a"), 1);
    check_bool("[^[:digit:]] 3 no", neverc_regexp_match_string("[^[:digit:]]", "3"), 0);
    check_bool("[[:xdigit:]] f", neverc_regexp_match_string("[[:xdigit:]]", "f"), 1);
    check_bool("[[:alnum:]_]+ ident",
               neverc_regexp_match_string("^[[:alnum:]_]+$", "foo_1"), 1);
    check_bool("[[:^digit:]] a", neverc_regexp_match_string("[[:^digit:]]", "a"), 1);
    check_bool("[[:^digit:]] 0 no", neverc_regexp_match_string("[[:^digit:]]", "0"), 0);
    check_bool("[[:^space:]] letter", neverc_regexp_match_string("[[:^space:]]", "x"), 1);
    check_bool("[[:^space:]] tab no", neverc_regexp_match_string("[[:^space:]]", "\t"), 0);
    check_bool("[[:^nope:]] rejected",
               neverc_regexp_compile("[[:^nope:]]", NULL) == NULL, 1);
    {
        neverc_regexp_t *posix_re = neverc_regexp_compile("[[:]", NULL);
        check_bool("incomplete POSIX class compiles", posix_re != NULL, 1);
        neverc_regexp_free(posix_re);
    }
    check_bool("incomplete POSIX matches bracket",
               neverc_regexp_match_string("[[:]", "["), 1);
    check_bool("incomplete POSIX matches colon",
               neverc_regexp_match_string("[[:]", ":"), 1);
    check_bool("incomplete POSIX rejects letter",
               neverc_regexp_match_string("[[:]", "a"), 0);
    {
        neverc_regexp_t *re;
        re = neverc_regexp_compile("a*?", NULL);
        check_bool("a*? compiles", re != NULL, 1);
        neverc_regexp_free(re);
        re = neverc_regexp_compile("a+?", NULL);
        check_bool("a+? compiles", re != NULL, 1);
        neverc_regexp_free(re);
        re = neverc_regexp_compile("a??", NULL);
        check_bool("a?? compiles", re != NULL, 1);
        neverc_regexp_free(re);
        re = neverc_regexp_compile("a{2}?", NULL);
        check_bool("a{2}? compiles", re != NULL, 1);
        neverc_regexp_free(re);
    }
    check_bool("a*? empty", neverc_regexp_match_string("a*?", ""), 1);
    check_bool("a*? aaa", neverc_regexp_match_string("^a*?$", "aaa"), 1);
}

static void test_named_groups_and_replace_expand(void) {
    section("[named groups / replace $n]");
    neverc_regexp_match_t m[4];
    neverc_regexp_t *re = neverc_regexp_compile("(?P<as>a+)(?P<bs>b+)", NULL);
    check_bool("named compile", re != NULL, 1);
    check_int("num_subexp", neverc_regexp_num_subexp(re), 2);
    check_str("name 1", neverc_regexp_subexp_name(re, 1), "as");
    check_str("name 2", neverc_regexp_subexp_name(re, 2), "bs");
    check_int("index as", neverc_regexp_subexp_index(re, "as"), 1);
    check_int("index bs", neverc_regexp_subexp_index(re, "bs"), 2);
    check_int("index missing", neverc_regexp_subexp_index(re, "nope"), -1);
    check_bool("name -1", neverc_regexp_subexp_name(re, -1) == NULL, 1);
    check_bool("name past ngroups", neverc_regexp_subexp_name(re, 100) == NULL, 1);
    check_bool("name INT_MAX", neverc_regexp_subexp_name(re, INT_MAX) == NULL, 1);
    memset(m, 0, sizeof(m));
    check_int("named submatch", neverc_regexp_find_submatch(re, "xxaaabbcyy", m, 3), 1);
    check_int("named g1", (int)m[1].len, 3);
    check_int("named g2", (int)m[2].len, 2);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(?<n>\\d+)", NULL);
    check_bool("?<n> compile", re != NULL, 1);
    check_int("?<n> index", neverc_regexp_subexp_index(re, "n"), 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(?'n'\\d+)", NULL);
    check_bool("?'n' compile", re != NULL, 1);
    check_int("?'n' index", neverc_regexp_subexp_index(re, "n"), 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(\\d+)", NULL);
    size_t outlen;
    char *r = neverc_regexp_replace_all(re, "a12b34", "[$1]", &outlen);
    check_str("replace $1", r, "a[12]b[34]");
    free(r);
    r = neverc_regexp_replace_all(re, "a12b34", "$$", &outlen);
    check_str("replace $$", r, "a$b$");
    free(r);
    r = neverc_regexp_replace_all(re, "a12", "[$0]", &outlen);
    check_str("replace $0", r, "a[12]");
    free(r);
    /* Go extract(): $01 is the name "01", not group 1. */
    r = neverc_regexp_replace_all(re, "12", "[$01]", &outlen);
    check_str("replace $01 is name", r, "[]");
    free(r);
    /* $1a is the name "1a", not group 1 plus a literal 'a'. */
    r = neverc_regexp_replace_all(re, "12", "[$1a]", &outlen);
    check_str("replace $1a is name", r, "[]");
    free(r);
    r = neverc_regexp_replace_all(re, "12", "[${1}a]", &outlen);
    check_str("replace ${1}a is group plus a", r, "[12a]");
    free(r);
    r = neverc_regexp_replace_all(re, "12", "[$0a]", &outlen);
    check_str("replace $0a is name", r, "[]");
    free(r);
    r = neverc_regexp_replace_all(re, "12", "${}", &outlen);
    check_str("replace empty ${}", r, "${}");
    free(r);
    r = neverc_regexp_replace_all(re, "12", "${x", &outlen);
    check_str("replace unclosed ${", r, "${x");
    free(r);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(?P<n>\\d+)", NULL);
    r = neverc_regexp_replace_all(re, "a12b34", "<${n}>", &outlen);
    check_str("replace ${n}", r, "a<12>b<34>");
    free(r);
    neverc_regexp_free(re);

    /* Last iteration of a repeated group; unset alternative. */
    re = neverc_regexp_compile("(a)+", NULL);
    memset(m, 0, sizeof(m));
    check_int("(a)+ found", neverc_regexp_find_submatch(re, "xaaay", m, 2), 1);
    check_int("(a)+ last cap len", (int)m[1].len, 1);
    if (m[1].start) check_int("(a)+ last cap a", m[1].start[0] == 'a', 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(a)|(b)", NULL);
    memset(m, 0, sizeof(m));
    check_int("(a)|(b) on b", neverc_regexp_find_submatch(re, "b", m, 3), 1);
    check_int("(a)|(b) g1 unset", m[1].start == NULL, 1);
    check_int("(a)|(b) g2 len", (int)m[2].len, 1);
    neverc_regexp_free(re);

    /* Both sides of `|` match the same text and join to one accept. The
     * left alternative must win: a LIFO epsilon walk used to keep the right
     * branch and leave group 1 unmatched (or set a group that never fired). */
    re = neverc_regexp_compile("(a)|a", NULL);
    memset(m, 0, sizeof(m));
    check_int("(a)|a found", neverc_regexp_find_submatch(re, "a", m, 2), 1);
    check_int("(a)|a g1 set", m[1].start != NULL && (int)m[1].len == 1, 1);
    if (m[1].start) check_int("(a)|a g1 a", m[1].start[0] == 'a', 1);
    r = neverc_regexp_replace_all(re, "a", "[$1]", &outlen);
    check_str("(a)|a replace $1", r, "[a]");
    free(r);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("a|(a)", NULL);
    memset(m, 0, sizeof(m));
    check_int("a|(a) found", neverc_regexp_find_submatch(re, "a", m, 2), 1);
    check_int("a|(a) g1 unset", m[1].start == NULL, 1);
    r = neverc_regexp_replace_all(re, "a", "[$1]", &outlen);
    check_str("a|(a) replace $1", r, "[]");
    free(r);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(a)|(a)", NULL);
    memset(m, 0, sizeof(m));
    check_int("(a)|(a) found", neverc_regexp_find_submatch(re, "a", m, 3), 1);
    check_int("(a)|(a) g1 set", m[1].start != NULL && (int)m[1].len == 1, 1);
    check_int("(a)|(a) g2 unset", m[2].start == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("((a)|a)", NULL);
    memset(m, 0, sizeof(m));
    check_int("((a)|a) found", neverc_regexp_find_submatch(re, "a", m, 3), 1);
    check_int("((a)|a) g1 set", m[1].start != NULL && (int)m[1].len == 1, 1);
    check_int("((a)|a) g2 set", m[2].start != NULL && (int)m[2].len == 1, 1);
    neverc_regexp_free(re);

    /* Optional group outside vs inside: unmatched vs matched-empty. */
    re = neverc_regexp_compile("(a)?b", NULL);
    memset(m, 0, sizeof(m));
    check_int("(a)?b on b", neverc_regexp_find_submatch(re, "b", m, 2), 1);
    check_int("(a)?b g1 unmatched", m[1].start == NULL, 1);
    neverc_regexp_free(re);

    re = neverc_regexp_compile("(a?)b", NULL);
    memset(m, 0, sizeof(m));
    check_int("(a?)b on b", neverc_regexp_find_submatch(re, "b", m, 2), 1);
    check_int("(a?)b g1 empty", m[1].start != NULL && (int)m[1].len == 0, 1);
    neverc_regexp_free(re);
}

static void test_utf8_class_and_nfa_bound(void) {
    section("[utf8 class / nfa bound]");
    check_bool("[\\x{96}] utf8", neverc_regexp_match_string("[\\x{96}]", "\xC2\x96"), 1);
    check_bool("[\\x{96}] not raw", neverc_regexp_match_string("[\\x{96}]", "\x96" ""), 0);
    check_bool("[a\\x{96}] a", neverc_regexp_match_string("[a\\x{96}]", "a"), 1);
    check_bool("[\\x{41}] A", neverc_regexp_match_string("[\\x{41}]", "A"), 1);
    check_bool("[é] matches é", neverc_regexp_match_string("[é]", "é"), 1);
    check_bool("[é] not lead byte",
               neverc_regexp_match_string("[é]", "\xC3"), 0);
    check_bool("[中] not 世", neverc_regexp_match_string("[中]", "世"), 0);
    check_bool("[中] matches 中", neverc_regexp_match_string("[中]", "中"), 1);

    /* Thompson NFA: (a+)+x on a long run of 'a's must not explode. */
    neverc_regexp_t *re = neverc_regexp_compile("(a+)+x", NULL);
    check_bool("(a+)+x compiles", re != NULL, 1);
    check_bool("(a+)+x no match", neverc_regexp_match(re, "aaaaaaaaaaaaaaaaaaaa"), 0);
    check_bool("(a+)+x match", neverc_regexp_match(re, "aaaaaaaaaaaaaaaaaaaax"), 1);
    neverc_regexp_free(re);

    /* Longer run: still linear, not exponential backtracking. */
    {
        char as[41];
        memset(as, 'a', 40);
        as[40] = '\0';
        re = neverc_regexp_compile("(a+)+b", NULL);
        check_bool("(a+)+b compiles", re != NULL, 1);
        check_bool("(a+)+b 40 a's no match", neverc_regexp_match(re, as), 0);
        neverc_regexp_free(re);
    }

    /* Invalid UTF-8 in the *text* is a byte; `.` matches it (no hang). */
    check_bool(". matches invalid UTF-8 byte", neverc_regexp_match_string(".", "\xFF"), 1);
    check_bool("utf8 literal e-acute",
               neverc_regexp_match_string("\xC3\xA9", "\xC3\xA9"), 1);
    check_bool("utf8 literal quantifier",
               neverc_regexp_match_string("中{2}", "中中"), 1);
    check_bool("utf8 literal quantifier not last byte",
               neverc_regexp_match_string("中{2}", "中\x96"), 0);
    check_bool("utf8 plus is the rune",
               neverc_regexp_match_string("^中+$", "中中"), 1);
    check_bool("utf8 e-acute plus",
               neverc_regexp_match_string("é+", "éé"), 1);

    const char *err = NULL;
    re = neverc_regexp_compile("(a{1000}){1000}", &err);
    check_bool("nested 1000x1000 rejected", re == NULL, 1);
    neverc_regexp_free(re);
}

static void test_find_differential(void) {
    section("[find_differential]");
    rrng = 0x9e3779b97f4a7c15ULL;
    int find_mis = 0, all_mis = 0, cases = 0;

    for (int it = 0; it < 4000; it++) {
        char pat[64];
        gen_pattern(pat);
        neverc_regexp_t *re = neverc_regexp_compile(pat, NULL);
        if (!re) continue;

        char text[20];
        int tlen = (int)(rr() % 16);
        for (int k = 0; k < tlen; k++) text[k] = (char)('a' + (rr() % 5));
        text[tlen] = '\0';
        cases++;

        size_t rs, rl;
        int rfound = ref_find(re, text, &rs, &rl);
        size_t mlen = 0;
        const char *m = neverc_regexp_find(re, text, &mlen);
        int nfound = (m != NULL);
        if (nfound != rfound ||
            (nfound && ((size_t)(m - text) != rs || mlen != rl))) {
            find_mis++;
            if (find_mis <= 3)
                printf("  find diff: pat=\"%s\" text=\"%s\" ref=(%d,%zu,%zu) new=(%d,%zu,%zu)\n",
                       pat, text, rfound, rs, rl, nfound,
                       nfound ? (size_t)(m - text) : 0, mlen);
        }

        /* compare full find_all against repeated ref_find advancing past each */
        int ncount = 0;
        char **got = neverc_regexp_find_all(re, text, -1, &ncount);
        int ri = 0, ok = 1;
        size_t pos = 0;
        while (pos <= strlen(text)) {
            size_t s2, l2;
            char sub[20];
            if (!ref_find(re, text + pos, &s2, &l2)) break;
            /* reconstruct expected match string */
            memcpy(sub, text + pos + s2, l2); sub[l2] = '\0';
            if (!got || ri >= ncount || !got[ri] || strcmp(got[ri], sub) != 0) {
                ok = 0;
                break;
            }
            ri++;
            pos = pos + s2 + l2;
        }
        if (ok && ri != ncount) ok = 0;
        if (!ok) all_mis++;
        neverc_regexp_free_strings(got, ncount);

        neverc_regexp_free(re);
    }
    printf("  (%d random cases)\n", cases);
    check_int("find differential mismatches", find_mis, 0);
    check_int("find_all differential mismatches", all_mis, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("=== NeverC Regexp Module Tests ===\n\n");
    test_compile();
    test_match();
    test_character_classes();
    test_find();
    test_find_submatch();
    test_find_all();
    test_replace();
    test_anchors();
    test_find_anchors();
    test_repeat_braces();
    test_invalid_inputs();
    test_empty_and_edge_cases();
    test_quote_meta();
    test_must_compile();
    test_submatch_and_hex();
    test_word_bounds_and_text_anchors();
    test_posix_classes();
    test_named_groups_and_replace_expand();
    test_utf8_class_and_nfa_bound();
    test_find_differential();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
