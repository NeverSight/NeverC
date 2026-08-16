#include "neverc/std/html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n",
               name, got ? got : "(null)", expected ? expected : "(null)");
    }
}

static void check_true(const char *name, int condition) {
    tests_run++;
    if (condition) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s\n", name);
    }
}

static void test_escape(void) {
    printf("[escape]\n");
    size_t outlen;

    char *r1 = neverc_html_escape_string("Hello, <World>!", &outlen);
    check_str("basic tags", r1, "Hello, &lt;World&gt;!");
    free(r1);

    char *r2 = neverc_html_escape_string("\"foo\" & 'bar'", &outlen);
    check_str("quotes and amp", r2, "&#34;foo&#34; &amp; &#39;bar&#39;");
    free(r2);

    char *r3 = neverc_html_escape_string("no special chars", &outlen);
    check_str("no escape needed", r3, "no special chars");
    free(r3);

    char *r4 = neverc_html_escape_string("", &outlen);
    check_str("empty", r4, "");
    free(r4);

    char *r5 = neverc_html_escape_string("<script>alert('xss')</script>", &outlen);
    check_str("xss", r5, "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;");
    free(r5);

    char *r6 = neverc_html_escape_string("a < b && c > d", &outlen);
    check_str("operators", r6, "a &lt; b &amp;&amp; c &gt; d");
    free(r6);

    outlen = 123;
    check_true("escape rejects NULL input",
               neverc_html_escape_string(NULL, &outlen) == NULL);
    check_true("escape clears length on failure", outlen == 0);
    check_true("escape rejects NULL length",
               neverc_html_escape_string("text", NULL) == NULL);
}

static void test_unescape(void) {
    printf("[unescape]\n");
    size_t outlen;

    char *r1 = neverc_html_unescape_string("&lt;b&gt;hello&lt;/b&gt;", &outlen);
    check_str("basic", r1, "<b>hello</b>");
    free(r1);

    char *r2 = neverc_html_unescape_string("&#34;foo&#34; &amp; &#39;bar&#39;", &outlen);
    check_str("entities", r2, "\"foo\" & 'bar'");
    free(r2);

    char *r3 = neverc_html_unescape_string("no entities", &outlen);
    check_str("passthrough", r3, "no entities");
    free(r3);

    char *r4 = neverc_html_unescape_string("&#65;&#66;&#67;", &outlen);
    check_str("decimal entities", r4, "ABC");
    free(r4);

    char *r5 = neverc_html_unescape_string("&#x41;&#x42;&#x43;", &outlen);
    check_str("hex entities", r5, "ABC");
    free(r5);

    char *r6 = neverc_html_unescape_string("&quot;hi&quot;", &outlen);
    check_str("quot entity", r6, "\"hi\"");
    free(r6);

    char *r7 = neverc_html_unescape_string("a&#;b", &outlen);
    check_str("empty numeric entity stays literal", r7, "a&#;b");
    check_true("empty numeric entity has no hidden NUL", outlen == 5);
    free(r7);

    char *r8 = neverc_html_unescape_string("a&#x;b", &outlen);
    check_str("empty hex entity stays literal", r8, "a&#x;b");
    free(r8);

    char *r9 = neverc_html_unescape_string("a&#0;b", &outlen);
    check_str("numeric NUL becomes replacement", r9, "a\xef\xbf\xbd" "b");
    check_true("numeric NUL length stays visible", outlen == 5);
    free(r9);

    char *r10 = neverc_html_unescape_string(
        "a&#184467440737095516160;b", &outlen);
    check_str("overflowing numeric entity becomes replacement", r10,
              "a\xef\xbf\xbd" "b");
    free(r10);

    char *r11 = neverc_html_unescape_string("caf&#233;", &outlen);
    check_str("latin-1 numeric", r11, "caf\xc3\xa9");
    free(r11);

    char *r12 = neverc_html_unescape_string("&#x1F600;", &outlen);
    check_str("emoji numeric", r12, "\xf0\x9f\x98\x80");
    free(r12);

    char *r13 = neverc_html_unescape_string("A&nbsp;B", &outlen);
    check_str("nbsp named", r13, "A\xc2\xa0" "B");
    free(r13);

    char *r14 = neverc_html_unescape_string("&#x80;", &outlen);
    check_str("win1252 euro", r14, "\xe2\x82\xac");
    free(r14);

    char *r15 = neverc_html_unescape_string("&notin;&not;", &outlen);
    check_str("named longest match", r15, "\xe2\x88\x89" "\xc2\xac");
    free(r15);

    char *r16 = neverc_html_unescape_string("a&amp b", &outlen);
    check_str("unterminated amp", r16, "a& b");
    free(r16);

    char *r17 = neverc_html_unescape_string("&amp", &outlen);
    check_str("unterminated amp eof", r17, "&");
    free(r17);

    char *r18 = neverc_html_unescape_string("&ampfoo", &outlen);
    check_str("amp prefix stays literal", r18, "&ampfoo");
    free(r18);

    char *r19 = neverc_html_unescape_string("A&nbsp B", &outlen);
    check_str("unterminated nbsp", r19, "A\xc2\xa0" " B");
    free(r19);

    char *r20 = neverc_html_unescape_string("&AMP;&LT;&GT;&QUOT;", &outlen);
    check_str("html5 uppercase aliases", r20, "&<>\"");
    free(r20);

    char *r21 = neverc_html_unescape_string("&COPY;&REG;&TRADE;", &outlen);
    check_str("html5 uppercase named", r21,
              "\xc2\xa9" "\xc2\xae" "\xe2\x84\xa2");
    free(r21);

    char *r22 = neverc_html_unescape_string("&AMPfoo", &outlen);
    check_str("AMP prefix stays literal", r22, "&AMPfoo");
    free(r22);

    char *r23 = neverc_html_unescape_string("&#xFFFD;&#65533;", &outlen);
    check_str("explicit replacement rune", r23,
              "\xef\xbf\xbd" "\xef\xbf\xbd");
    free(r23);

    char *r24 = neverc_html_unescape_string("&#x81;", &outlen);
    check_str("undefined win1252 is replacement", r24, "\xef\xbf\xbd");
    free(r24);

    outlen = 123;
    check_true("unescape rejects NULL input",
               neverc_html_unescape_string(NULL, &outlen) == NULL);
    check_true("unescape clears length on failure", outlen == 0);
    check_true("unescape rejects NULL length",
               neverc_html_unescape_string("text", NULL) == NULL);
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    size_t len1, len2;
    const char *originals[] = {
        "Hello, World!",
        "<script>alert('xss')</script>",
        "a < b && c > d",
        "\"quoted\" & 'single'",
        "normal text no escaping"
    };
    for (int i = 0; i < 5; i++) {
        char *escaped = neverc_html_escape_string(originals[i], &len1);
        char *unescaped = neverc_html_unescape_string(escaped, &len2);
        tests_run++;
        if (strcmp(unescaped, originals[i]) == 0) tests_passed++;
        else {
            tests_failed++;
            printf("  FAIL: roundtrip[%d]: \"%s\" -> \"%s\" -> \"%s\"\n",
                   i, originals[i], escaped, unescaped);
        }
        free(escaped);
        free(unescaped);
    }
}

int main(void) {
    printf("=== NeverC HTML Module Tests ===\n\n");
    test_escape();
    test_unescape();
    test_roundtrip();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
