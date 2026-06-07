#include "neverc/text/tabwriter.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d:\n  got:      [%s]\n  expected: [%s]\n", \
                  __LINE__, _v ? _v : "(null)", _e ? _e : "(null)"); } \
} while(0)

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

static void test_basic_alignment(void) {
    printf("[basic_alignment]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 1, ' ', 0);

    const char *input = "a\tb\tc\naa\tbb\tcc\naaa\tbbb\tccc";
    neverc_tabwriter_write(&w, input, strlen(input));
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    tests_run++;
    if (out && len > 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: no output\n"); }
}

static void test_single_column(void) {
    printf("[single_column]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 1, ' ', 0);

    const char *input = "hello\nworld";
    neverc_tabwriter_write(&w, input, strlen(input));
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    ASSERT_STR_EQ(out, "hello\nworld");
}

static void test_empty(void) {
    printf("[empty]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 1, ' ', 0);
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    tests_run++;
    if (len == 0 || (out && out[0] == '\0')) tests_passed++;
    else { tests_failed++; printf("  FAIL: expected empty output\n"); }
}

static void test_padding_char(void) {
    printf("[padding_char]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 1, '.', 0);

    const char *input = "a\tb\naa\tbb";
    neverc_tabwriter_write(&w, input, strlen(input));
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    tests_run++;
    if (out && strchr(out, '.')) tests_passed++;
    else { tests_failed++; printf("  FAIL: expected dots in output, got: [%s]\n", out ? out : "(null)"); }
}

static void test_reset(void) {
    printf("[reset]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 1, ' ', 0);

    const char *input = "hello\tworld";
    neverc_tabwriter_write(&w, input, strlen(input));
    neverc_tabwriter_flush(&w);

    neverc_tabwriter_reset(&w);

    const char *input2 = "foo\tbar";
    neverc_tabwriter_write(&w, input2, strlen(input2));
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    tests_run++;
    if (out && len > 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: no output after reset\n"); }
}

static void test_no_tabs(void) {
    printf("[no_tabs]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 1, ' ', 0);

    const char *input = "just plain text";
    neverc_tabwriter_write(&w, input, strlen(input));
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    ASSERT_STR_EQ(out, "just plain text");
}

static void test_multiple_lines(void) {
    printf("[multiple_lines]\n");
    neverc_tabwriter_t w;
    neverc_tabwriter_init(&w, 1, 8, 2, ' ', 0);

    const char *input = "name\tage\tcity\nalice\t30\tNY\nbob\t25\tLA";
    neverc_tabwriter_write(&w, input, strlen(input));
    neverc_tabwriter_flush(&w);

    size_t len;
    const char *out = neverc_tabwriter_output(&w, &len);
    tests_run++;
    if (out && len > (size_t)strlen(input)) tests_passed++;
    else { tests_failed++; printf("  FAIL: expected padded output\n"); }
}

int main(void) {
    printf("=== NeverC text/tabwriter Tests ===\n");
    test_basic_alignment();
    test_single_column();
    test_empty();
    test_padding_char();
    test_reset();
    test_no_tabs();
    test_multiple_lines();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
