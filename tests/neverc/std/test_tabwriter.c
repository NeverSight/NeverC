#include "neverc/std/text/tabwriter.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

static void test_basic_alignment(neverc_tabwriter_t *w) {
    printf("[basic_alignment]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);

    const char *input = "a\tb\tc\naa\tbb\tcc\naaa\tbbb\tccc";
    neverc_tabwriter_write(w, input, strlen(input));
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (out && len > 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: no output\n"); }
}

static void test_single_column(neverc_tabwriter_t *w) {
    printf("[single_column]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);

    const char *input = "hello\nworld";
    neverc_tabwriter_write(w, input, strlen(input));
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    ASSERT_STR_EQ(out, "hello\nworld");
}

static void test_empty(neverc_tabwriter_t *w) {
    printf("[empty]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (len == 0 || (out && out[0] == '\0')) tests_passed++;
    else { tests_failed++; printf("  FAIL: expected empty output\n"); }
}

static void test_padding_char(neverc_tabwriter_t *w) {
    printf("[padding_char]\n");
    neverc_tabwriter_init(w, 1, 8, 1, '.', 0);

    const char *input = "a\tb\naa\tbb";
    neverc_tabwriter_write(w, input, strlen(input));
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (out && strchr(out, '.')) tests_passed++;
    else { tests_failed++; printf("  FAIL: expected dots in output, got: [%s]\n", out ? out : "(null)"); }
}

static void test_reset(neverc_tabwriter_t *w) {
    printf("[reset]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);

    const char *input = "hello\tworld";
    neverc_tabwriter_write(w, input, strlen(input));
    neverc_tabwriter_flush(w);

    neverc_tabwriter_reset(w);

    const char *input2 = "foo\tbar";
    neverc_tabwriter_write(w, input2, strlen(input2));
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (out && len > 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: no output after reset\n"); }
}

static void test_no_tabs(neverc_tabwriter_t *w) {
    printf("[no_tabs]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);

    const char *input = "just plain text";
    neverc_tabwriter_write(w, input, strlen(input));
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    ASSERT_STR_EQ(out, "just plain text");
}

static void test_multiple_lines(neverc_tabwriter_t *w) {
    printf("[multiple_lines]\n");
    neverc_tabwriter_init(w, 1, 8, 2, ' ', 0);

    const char *input = "name\tage\tcity\nalice\t30\tNY\nbob\t25\tLA";
    neverc_tabwriter_write(w, input, strlen(input));
    neverc_tabwriter_flush(w);

    size_t len;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (out && len > (size_t)strlen(input)) tests_passed++;
    else { tests_failed++; printf("  FAIL: expected padded output\n"); }
}

static void test_invalid_input(neverc_tabwriter_t *w) {
    printf("[invalid_input]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);
    neverc_tabwriter_write(w, NULL, 1);
    neverc_tabwriter_flush(w);
    size_t len = 123;
    const char *out = neverc_tabwriter_output(w, &len);
    ASSERT_INT_EQ(out == NULL, 1);
    ASSERT_INT_EQ((int)len, 0);
    neverc_tabwriter_reset(w);
}

static void test_max_buf_sets_failed(neverc_tabwriter_t *w) {
    printf("[max_buf_sets_failed]\n");
    neverc_tabwriter_init(w, 8, 0, 1, ' ', 0);
    size_t n = (size_t)NEVERC_TABWRITER_MAX_BUF + 2;
    char *big = (char *)malloc(n);
    ASSERT_INT_EQ(big != NULL, 1);
    if (!big) return;
    memset(big, 'a', n - 1);
    big[n - 1] = '\n';
    neverc_tabwriter_write(w, big, n);
    neverc_tabwriter_flush(w);
    ASSERT_INT_EQ(neverc_tabwriter_output(w, NULL) == NULL, 1);
    free(big);
}

static void test_many_cells(neverc_tabwriter_t *w) {
    printf("[many_cells]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);

    /* 100 lines x 3 cells used to overflow the 256-cell array and drop rows. */
    char line[32];
    for (int i = 0; i < 100; i++) {
        int n = snprintf(line, sizeof(line), "c%d\tv%d\tw%d\n", i, i, i);
        neverc_tabwriter_write(w, line, (size_t)n);
    }
    neverc_tabwriter_flush(w);

    size_t len = 0;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (out && strstr(out, "c0") && strstr(out, "c99") && strstr(out, "w99"))
        tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: 300-cell table missing rows (out=%s)\n",
               out ? "truncated" : "(null)");
    }
    neverc_tabwriter_reset(w);
}

static void test_minwidth(neverc_tabwriter_t *w) {
    printf("[minwidth]\n");
    neverc_tabwriter_init(w, 5, 0, 0, '.', 0);
    neverc_tabwriter_write(w, "a\tb\n", 4);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a....b\n");
}

static void test_space_pad_ignores_tabwidth(neverc_tabwriter_t *w) {
    printf("[space_pad_ignores_tabwidth]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);
    neverc_tabwriter_write(w, "a\tb\naa\tbb", 9);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a  b\naa bb");
}

static void test_trailing_htab_is_last_cell(neverc_tabwriter_t *w) {
    printf("[trailing_htab_is_last_cell]\n");
    /* Go tests 4a / 5c: a trailing htab does not invent an extra column. */
    neverc_tabwriter_init(w, 8, 0, 1, '.', 0);
    neverc_tabwriter_write(w, "\t", 1);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "");

    neverc_tabwriter_reset(w);
    neverc_tabwriter_write(w, "*\t*\t", 4);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "*.......*");
}

static void test_align_right_trailing_htab(neverc_tabwriter_t *w) {
    printf("[align_right_trailing_htab]\n");
    neverc_tabwriter_init(w, 8, 0, 1, '.', NEVERC_TABWRITER_ALIGN_RIGHT);
    neverc_tabwriter_write(w, "*\t*\t", 4);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), ".......**");
}

static void test_formfeed_breaks_columns(neverc_tabwriter_t *w) {
    printf("[formfeed_breaks_columns]\n");
    /* Go test 9c: '\f' flushes so the next table is formatted independently. */
    neverc_tabwriter_init(w, 1, 0, 0, '.', 0);
    const char *in = "1\t2\t3\t4\f11\t222\t3333\t44444\n";
    neverc_tabwriter_write(w, in, strlen(in));
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "1234\n11222333344444\n");
}

static void test_vertical_tab_ends_cell(neverc_tabwriter_t *w) {
    printf("[vertical_tab_ends_cell]\n");
    neverc_tabwriter_init(w, 1, 0, 1, '.', 0);
    neverc_tabwriter_write(w, "a\vb\n", 4);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a.b\n");
}

static void test_invalid_utf8_rune_width(neverc_tabwriter_t *w) {
    printf("[invalid_utf8_rune_width]\n");
    /* Go utf8.RuneCount: 0x96 is one rune, so "A\x96B" has width 3.
     * NeverC hex escapes consume following hex digits — split the literal. */
    neverc_tabwriter_init(w, 5, 0, 0, '.', 0);
    const char in[] = {'A', '\x96', 'B', '\t', 'x', '\n'};
    neverc_tabwriter_write(w, in, sizeof(in));
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "A" "\x96" "B..x\n");
}

static void test_flush_then_write_is_independent(neverc_tabwriter_t *w) {
    printf("[flush_then_write_is_independent]\n");
    neverc_tabwriter_init(w, 1, 0, 1, '.', 0);
    neverc_tabwriter_write(w, "a\tb\n", 4);
    neverc_tabwriter_flush(w);
    neverc_tabwriter_write(w, "ccc\tdd\n", 7);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a.b\nccc.dd\n");
}

static void test_elastic_tabstops_ragged(neverc_tabwriter_t *w) {
    printf("[elastic_tabstops_ragged]\n");
    /* A 1-cell line breaks the column block (Go elastic tabstops). */
    neverc_tabwriter_init(w, 1, 0, 1, ' ', 0);
    const char *in = "aaaa\tbbb\naa\tb\na\naa\tcccc";
    neverc_tabwriter_write(w, in, strlen(in));
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL),
                  "aaaa bbb\naa   b\na\naa cccc");
}

static void test_discard_empty_soft_columns(neverc_tabwriter_t *w) {
    printf("[discard_empty_soft_columns]\n");
    neverc_tabwriter_init(w, 4, 0, 0, '.', NEVERC_TABWRITER_DISCARD_EMPTY_COLS);
    neverc_tabwriter_write(w, "a\v\vb", 4);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a...b");
}

static void test_escape_hides_tab(neverc_tabwriter_t *w) {
    printf("[escape_hides_tab]\n");
    neverc_tabwriter_init(w, 8, 0, 1, '.', 0);
    const char in[] = { 'a', NEVERC_TABWRITER_ESCAPE, '\t', 'b',
                        NEVERC_TABWRITER_ESCAPE, '\t', 'c' };
    neverc_tabwriter_write(w, in, sizeof(in));
    neverc_tabwriter_flush(w);
    /* Escape pair width is 3 (a, tab, b); column minwidth 8 → 5 dots. */
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL),
                  "a" "\xff" "\t" "b" "\xff" ".....c");
}

static void test_strip_escape(neverc_tabwriter_t *w) {
    printf("[strip_escape]\n");
    neverc_tabwriter_init(w, 8, 0, 1, '.', NEVERC_TABWRITER_STRIP_ESCAPE);
    const char in[] = { 'a', NEVERC_TABWRITER_ESCAPE, '\t', 'b',
                        NEVERC_TABWRITER_ESCAPE, '\t', 'c' };
    neverc_tabwriter_write(w, in, sizeof(in));
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a\tb.....c");
}

static void test_filter_html_tag_width(neverc_tabwriter_t *w) {
    printf("[filter_html_tag_width]\n");
    neverc_tabwriter_init(w, 5, 0, 0, '.', NEVERC_TABWRITER_FILTER_HTML);
    neverc_tabwriter_write(w, "a<foo>\tb\n", 9);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a<foo>....b\n");
}

static void test_filter_html_tab_inside_tag(neverc_tabwriter_t *w) {
    printf("[filter_html_tab_inside_tag]\n");
    neverc_tabwriter_init(w, 1, 0, 1, '.', NEVERC_TABWRITER_FILTER_HTML);
    neverc_tabwriter_write(w, "a<b\tc>d\te", 9);
    neverc_tabwriter_flush(w);
    /* <b\tc> is one tag (width 0); first cell is a + tag + d, width 2. */
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a<b\tc>d.e");
}

static void test_filter_html_entity_width(neverc_tabwriter_t *w) {
    printf("[filter_html_entity_width]\n");
    neverc_tabwriter_init(w, 5, 0, 0, '.', NEVERC_TABWRITER_FILTER_HTML);
    neverc_tabwriter_write(w, "a&amp;\tb\n", 9);
    neverc_tabwriter_flush(w);
    ASSERT_STR_EQ(neverc_tabwriter_output(w, NULL), "a&amp;...b\n");
}

static void test_full_buf_tab_ends_cell(neverc_tabwriter_t *w) {
    printf("[full_buf_tab_ends_cell]\n");
    /* A cell of exactly MAX_BUF bytes followed by a tab used to skip
     * end_cell (append_run returned success on a full buffer). */
    neverc_tabwriter_init(w, 1, 0, 1, '.', 0);
    size_t n = (size_t)NEVERC_TABWRITER_MAX_BUF + 1U;
    char *buf = (char *)malloc(n);
    ASSERT_INT_EQ(buf != NULL, 1);
    if (!buf) return;
    memset(buf, 'x', (size_t)NEVERC_TABWRITER_MAX_BUF);
    buf[NEVERC_TABWRITER_MAX_BUF] = '\t';
    neverc_tabwriter_write(w, buf, n);
    neverc_tabwriter_flush(w);
    size_t len = 0;
    const char *out = neverc_tabwriter_output(w, &len);
    tests_run++;
    if (out && len == (size_t)NEVERC_TABWRITER_MAX_BUF &&
        out[0] == 'x' && out[NEVERC_TABWRITER_MAX_BUF - 1] == 'x')
        tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: exact-full cell + tab (out=%s len=%zu)\n",
               out ? "non-null" : "(null)", len);
    }
    free(buf);
    neverc_tabwriter_reset(w);
}

static void test_max_lines_sets_failed(neverc_tabwriter_t *w) {
    printf("[max_lines_sets_failed]\n");
    neverc_tabwriter_init(w, 1, 8, 1, ' ', 0);
    size_t n = (size_t)NEVERC_TABWRITER_MAX_LINES + 1U;
    char *buf = (char *)malloc(n);
    ASSERT_INT_EQ(buf != NULL, 1);
    if (!buf) return;
    memset(buf, '\n', n);
    neverc_tabwriter_write(w, buf, n);
    neverc_tabwriter_flush(w);
    ASSERT_INT_EQ(neverc_tabwriter_output(w, NULL) == NULL, 1);
    free(buf);
    neverc_tabwriter_reset(w);
}

static void test_max_cols_sets_failed(neverc_tabwriter_t *w) {
    printf("[max_cols_sets_failed]\n");
    neverc_tabwriter_init(w, 1, 0, 0, '.', 0);
    /* 258 tabs → 258 cells; the last is not a column, so 257 columns. */
    size_t n = (size_t)NEVERC_TABWRITER_MAX_COLS + 2U;
    char *buf = (char *)malloc(n);
    ASSERT_INT_EQ(buf != NULL, 1);
    if (!buf) return;
    memset(buf, '\t', n);
    neverc_tabwriter_write(w, buf, n);
    neverc_tabwriter_flush(w);
    ASSERT_INT_EQ(neverc_tabwriter_output(w, NULL) == NULL, 1);
    free(buf);
    neverc_tabwriter_reset(w);
}

static void test_huge_padding_fails_closed(neverc_tabwriter_t *w) {
    printf("[huge_padding_fails_closed]\n");
    neverc_tabwriter_init(w, 1, 8, INT_MAX, ' ', 0);
    neverc_tabwriter_write(w, "a\tb\n", 4);
    neverc_tabwriter_flush(w);
    ASSERT_INT_EQ(neverc_tabwriter_output(w, NULL) == NULL, 1);
    neverc_tabwriter_reset(w);
}

int main(void) {
    printf("=== NeverC text/tabwriter Tests ===\n");
    neverc_tabwriter_t *w =
        (neverc_tabwriter_t *)malloc(sizeof(*w));
    if (!w) {
        printf("  FAIL: tabwriter allocation failed\n");
        return 1;
    }
    test_basic_alignment(w);
    neverc_tabwriter_reset(w);
    test_single_column(w);
    neverc_tabwriter_reset(w);
    test_empty(w);
    neverc_tabwriter_reset(w);
    test_padding_char(w);
    neverc_tabwriter_reset(w);
    test_reset(w);
    neverc_tabwriter_reset(w);
    test_no_tabs(w);
    neverc_tabwriter_reset(w);
    test_multiple_lines(w);
    neverc_tabwriter_reset(w);
    test_invalid_input(w);
    neverc_tabwriter_reset(w);
    test_max_buf_sets_failed(w);
    neverc_tabwriter_reset(w);
    test_many_cells(w);
    neverc_tabwriter_reset(w);
    test_minwidth(w);
    neverc_tabwriter_reset(w);
    test_space_pad_ignores_tabwidth(w);
    neverc_tabwriter_reset(w);
    test_trailing_htab_is_last_cell(w);
    neverc_tabwriter_reset(w);
    test_align_right_trailing_htab(w);
    neverc_tabwriter_reset(w);
    test_formfeed_breaks_columns(w);
    neverc_tabwriter_reset(w);
    test_vertical_tab_ends_cell(w);
    neverc_tabwriter_reset(w);
    test_invalid_utf8_rune_width(w);
    neverc_tabwriter_reset(w);
    test_flush_then_write_is_independent(w);
    neverc_tabwriter_reset(w);
    test_elastic_tabstops_ragged(w);
    neverc_tabwriter_reset(w);
    test_discard_empty_soft_columns(w);
    neverc_tabwriter_reset(w);
    test_escape_hides_tab(w);
    neverc_tabwriter_reset(w);
    test_strip_escape(w);
    neverc_tabwriter_reset(w);
    test_filter_html_tag_width(w);
    neverc_tabwriter_reset(w);
    test_filter_html_tab_inside_tag(w);
    neverc_tabwriter_reset(w);
    test_filter_html_entity_width(w);
    neverc_tabwriter_reset(w);
    test_full_buf_tab_ends_cell(w);
    neverc_tabwriter_reset(w);
    test_max_lines_sets_failed(w);
    neverc_tabwriter_reset(w);
    test_max_cols_sets_failed(w);
    neverc_tabwriter_reset(w);
    test_huge_padding_fails_closed(w);
    free(w);
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
