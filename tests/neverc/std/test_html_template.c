#include "neverc/std/html/template.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

static void check(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else printf("  FAIL: %s\n", name);
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && strcmp(got, expected) == 0) tests_passed++;
    else { printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got ? got : "(null)", expected); }
}

static void test_html_escape(void) {
    printf("[html_escape]\n");
    char *e = neverc_html_escape("<script>alert('xss')</script>");
    check_str("script", e, "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;");
    free(e);

    e = neverc_html_escape("a & b < c > d \"e\"");
    check_str("mixed", e, "a &amp; b &lt; c &gt; d &#34;e&#34;");
    free(e);

    e = neverc_html_escape("safe text");
    check_str("safe", e, "safe text");
    free(e);

    e = neverc_html_escape("");
    check_str("empty", e, "");
    free(e);
}

static void test_js_escape(void) {
    printf("[js_escape]\n");
    char *e = neverc_html_js_escape("hello \"world\" <script>");
    check_str("js", e, "hello \\\"world\\\" \\u003cscript\\u003e");
    free(e);

    e = neverc_html_js_escape("line1\nline2");
    check_str("newline", e, "line1\\nline2");
    free(e);

    e = neverc_html_js_escape("`+$");
    check_str("template literal", e, "\\u0060+\\u0024");
    free(e);
}

static void test_css_escape(void) {
    printf("[css_escape]\n");
    char *e = neverc_html_css_escape("!B");
    check_str("terminate before hex digit", e, "\\21 B");
    free(e);

    e = neverc_html_css_escape("#fff");
    check_str("selector escape does not absorb hex", e, "\\23 fff");
    free(e);

    e = neverc_html_css_escape("!G");
    check_str("non-hex continuation needs no separator", e, "\\21G");
    free(e);
}

static void test_url_escape(void) {
    printf("[url_escape]\n");
    char *e = neverc_html_url_query_escape("hello world&foo=bar");
    check_str("url", e, "hello%20world%26foo%3Dbar");
    free(e);
}

static void test_template_basic(void) {
    printf("[template_basic]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Name", "Alice");
    neverc_html_template_data_set(&data, "Title", "Welcome");

    char *out = neverc_html_template_render("<h1>{{.Title}}</h1><p>Hello, {{.Name}}!</p>", &data);
    check_str("basic", out, "<h1>Welcome</h1><p>Hello, Alice!</p>");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_auto_escape(void) {
    printf("[template_auto_escape]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Input", "<script>alert(1)</script>");

    char *out = neverc_html_template_render("User input: {{.Input}}", &data);
    check_str("auto_escape", out,
              "User input: &lt;script&gt;alert(1)&lt;/script&gt;");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_url_and_script(void) {
    printf("[template_url_and_script]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    char *out = neverc_html_template_render("<a href=\"{{.Link}}\">x</a>", &data);
    check("js url neutralized", out && strstr(out, "javascript:") == NULL);
    check("js url becomes hash", out && strstr(out, "href=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Name", "\";alert(1);//");
    out = neverc_html_template_render("<script>var x=\"{{.Name}}\";</script>", &data);
    check("script uses js escape", out && strstr(out, "\\\"") != NULL);
    check("script no html entity quote", out && strstr(out, "&#34;") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<a href={{.Link}}>x</a>", &data);
    check("unquoted js url neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Name", "');alert(1);//");
    out = neverc_html_template_render("<img onclick=\"{{.Name}}\">", &data);
    check("onclick uses js escape", out && strstr(out, "\\'") != NULL);
    check("onclick no html entity quote", out && strstr(out, "&#39;") == NULL);
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_missing_var(void) {
    printf("[template_missing_var]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);

    char *out = neverc_html_template_render("Hello {{.Name}}!", &data);
    check_str("missing", out, "Hello !");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_if(void) {
    printf("[template_if]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Show", "1");

    char *out = neverc_html_template_render("{{if .Show}}visible{{end}}", &data);
    check_str("if_true", out, "visible");
    free(out);

    neverc_html_template_data_set(&data, "Show", "0");
    out = neverc_html_template_render("{{if .Show}}visible{{end}}", &data);
    check_str("if_false", out, "");
    free(out);

    neverc_html_template_data_free(&data);
}

static void test_template_if_else(void) {
    printf("[template_if_else]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Show", "1");

    char *out = neverc_html_template_render(
        "{{if .Show}}yes{{else}}no{{end}}", &data);
    check_str("if_else_true", out, "yes");
    free(out);

    neverc_html_template_data_set(&data, "Show", "0");
    out = neverc_html_template_render(
        "{{if .Show}}yes{{else}}no{{end}}", &data);
    check_str("if_else_false", out, "no");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_range_subset(void) {
    printf("[template_range_subset]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);

    char *out = neverc_html_template_render(
        "A{{range .Items}}B{{end}}C", &data);
    check_str("range_absent", out, "AC");
    free(out);

    neverc_html_template_data_set(&data, "Items", "present");
    out = neverc_html_template_render(
        "A{{range .Items}}B{{end}}C", &data);
    check_str("range_present_once", out, "ABC");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_parse_errors(void) {
    printf("[template_parse_errors]\n");
    const char *bad[] = {
        "{{.Name", "{{if .Show}}open", "{{else}}", "{{end}}",
        "{{if}}", "{{range}}"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        neverc_html_template_t *t = neverc_html_template_parse(bad[i]);
        check("invalid template rejected", t == NULL);
        neverc_html_template_free(t);
    }
    check("NULL template rejected", neverc_html_template_parse(NULL) == NULL);
    check("NULL render rejected", neverc_html_template_render(NULL, NULL) == NULL);
}

static void test_data_operations(void) {
    printf("[data_operations]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "key1", "val1");
    neverc_html_template_data_set(&data, "key2", "val2");
    check_str("get1", neverc_html_template_data_get(&data, "key1"), "val1");
    check_str("get2", neverc_html_template_data_get(&data, "key2"), "val2");
    check("get_missing", neverc_html_template_data_get(&data, "key3") == NULL);

    neverc_html_template_data_set(&data, "key1", "updated");
    check_str("update", neverc_html_template_data_get(&data, "key1"), "updated");
    neverc_html_template_data_free(&data);
}

int main(void) {
    test_html_escape();
    test_js_escape();
    test_css_escape();
    test_url_escape();
    test_template_basic();
    test_template_auto_escape();
    test_template_url_and_script();
    test_template_missing_var();
    test_template_if();
    test_template_if_else();
    test_template_range_subset();
    test_template_parse_errors();
    test_data_operations();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
