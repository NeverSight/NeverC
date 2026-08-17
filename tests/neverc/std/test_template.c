#include "neverc/std/text/template.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_true(const char *name, int condition) {
    tests_run++;
    if (condition) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected); }
}

static void test_simple_var(void) {
    printf("[simple var]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "Name", "World");

    size_t outlen;
    char *r = neverc_template_render("Hello, {{.Name}}!", &data, &outlen);
    check_str("simple var", r, "Hello, World!");
    free(r);

    neverc_template_data_set(&data, "Foo.Bar", "nested");
    r = neverc_template_render("{{.Foo.Bar}}", &data, &outlen);
    check_str("dotted key", r, "nested");
    free(r);

    neverc_template_data_free(&data);
}

static void test_multiple_vars(void) {
    printf("[multiple vars]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "First", "John");
    neverc_template_data_set(&data, "Last", "Doe");

    size_t outlen;
    char *r = neverc_template_render("{{.First}} {{.Last}}", &data, &outlen);
    check_str("multi var", r, "John Doe");
    free(r);

    neverc_template_data_free(&data);
}

static void test_missing_var(void) {
    printf("[missing var]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);

    size_t outlen;
    char *r = neverc_template_render("Hello, {{.Name}}!", &data, &outlen);
    check_str("missing var", r, "Hello, !");
    free(r);

    neverc_template_data_free(&data);
}

static void test_if_true(void) {
    printf("[if true]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "Show", "yes");

    size_t outlen;
    char *r = neverc_template_render("{{if .Show}}visible{{end}}", &data, &outlen);
    check_str("if true", r, "visible");
    free(r);

    neverc_template_data_free(&data);
}

static void test_if_false(void) {
    printf("[if false]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);

    size_t outlen;
    char *r = neverc_template_render("A{{if .Show}}B{{end}}C", &data, &outlen);
    check_str("if false", r, "AC");
    free(r);

    neverc_template_data_free(&data);
}

static void test_if_else(void) {
    printf("[if else]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);

    size_t outlen;
    char *r = neverc_template_render("{{if .Show}}yes{{else}}no{{end}}", &data, &outlen);
    check_str("if else false", r, "no");
    free(r);

    neverc_template_data_set(&data, "Show", "1");
    r = neverc_template_render("{{if .Show}}yes{{else}}no{{end}}", &data, &outlen);
    check_str("if else true", r, "yes");
    free(r);

    neverc_template_data_free(&data);
}

static void test_literal_text(void) {
    printf("[literal text]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);

    size_t outlen;
    char *r = neverc_template_render("no templates here", &data, &outlen);
    check_str("literal", r, "no templates here");
    free(r);

    neverc_template_data_free(&data);
}

static void test_complex(void) {
    printf("[complex]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "Title", "NeverC");
    neverc_template_data_set(&data, "Version", "0.1");
    neverc_template_data_set(&data, "Debug", "");

    size_t outlen;
    char *r = neverc_template_render(
        "# {{.Title}} v{{.Version}}\n"
        "{{if .Debug}}DEBUG MODE\n{{end}}"
        "Ready.",
        &data, &outlen);
    check_str("complex", r, "# NeverC v0.1\nReady.");
    free(r);

    neverc_template_data_free(&data);
}

static void test_falsy_values(void) {
    printf("[falsy values]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    size_t outlen;

    neverc_template_data_set(&data, "Zero", "0");
    char *r = neverc_template_render("{{if .Zero}}yes{{else}}no{{end}}", &data, &outlen);
    check_str("zero is falsy", r, "no");
    free(r);

    neverc_template_data_set(&data, "False", "false");
    r = neverc_template_render("{{if .False}}yes{{else}}no{{end}}", &data, &outlen);
    check_str("false is falsy", r, "no");
    free(r);

    neverc_template_data_free(&data);
}

static void test_range_subset(void) {
    printf("[range subset]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    size_t outlen = 0;

    char *r = neverc_template_render(
        "A{{range .Present}}B{{end}}C", &data, &outlen);
    check_str("range absent", r, "AC");
    free(r);

    neverc_template_data_set(&data, "Present", "value");
    r = neverc_template_render(
        "A{{range .Present}}B{{end}}C", &data, &outlen);
    check_str("range present once", r, "ABC");
    free(r);
    neverc_template_data_free(&data);
}

static void test_action_whitespace(void) {
    printf("[action whitespace]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "Show", "1");
    neverc_template_data_set(&data, "Present", "value");
    neverc_template_data_set(&data, "Name", "Ada");
    size_t outlen = 0;

    char *r = neverc_template_render("{{if\n.Show}}yes{{end}}", &data, &outlen);
    check_str("if newline", r, "yes");
    free(r);

    r = neverc_template_render("{{if\t.Show}}yes{{end}}", &data, &outlen);
    check_str("if tab", r, "yes");
    free(r);

    r = neverc_template_render("A{{range\t.Present}}B{{end}}C", &data, &outlen);
    check_str("range tab", r, "ABC");
    free(r);

    r = neverc_template_render("Hi {{.Name\n}}!", &data, &outlen);
    check_str("var trailing newline", r, "Hi Ada!");
    free(r);

    neverc_template_data_free(&data);
}

static void test_trim_markers(void) {
    printf("[trim markers]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "Name", "Ada");
    size_t outlen = 0;

    char *r = neverc_template_render("Hello, {{- .Name -}} !", &data, &outlen);
    check_str("trim both", r, "Hello,Ada!");
    free(r);

    r = neverc_template_render("A\n{{- if .Name -}}\nX\n{{- end -}}\nB",
                               &data, &outlen);
    check_str("trim if block", r, "AXB");
    free(r);

    r = neverc_template_render("{{ - .Name }}", &data, &outlen);
    check_true("space before dash is not trim", r == NULL);
    free(r);

    /* Go requires a space after "{{-" / before "-}}" so "{{-3}}" is a
     * number and "{{.Name-}}" is not a right-trim of .Name. */
    r = neverc_template_render("{{-.Name}}", &data, &outlen);
    check_true("dash without space is not left trim", r == NULL);
    free(r);

    r = neverc_template_render("{{.Name-}}", &data, &outlen);
    check_true("dash without space is not right trim", r == NULL);
    free(r);

    r = neverc_template_render("{{-end-}}", &data, &outlen);
    check_true("end without trim spaces rejected", r == NULL);
    free(r);

    neverc_template_data_free(&data);
}

static void test_parse_errors(void) {
    printf("[parse errors]\n");
    const char *bad[] = {
        "{{.Name",
        "{{if .Show}}unterminated",
        "{{if .Show}}yes{{else}}no",
        "{{end}}",
        "{{else}}",
        "{{if}}",
        "{{range}}",
        "{{.Name | html}}",
        "{{.Name|html}}",
        "{{if .Show | html}}yes{{end}}",
        "{{range .Present | html}}x{{end}}",
        "{{printf .Name}}",
        "{{html .Name}}",
        "{{.Name .Other}}",
        "{{if .Show extra}}yes{{end}}",
        "{{if Show}}yes{{end}}",
        "{{Name}}",
        "{{.Foo()}}",
        "{{.Foo[0]}}",
        "{{if .Show()}}yes{{end}}",
        "{{-.Name}}",
        "{{.Name-}}",
        "{{-end-}}",
        "{{.Name+.Other}}",
        "{{.Name-.Other}}",
        "{{.Foo.Bar.}}",
        "{{.Foo..Bar}}"
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const char *err = NULL;
        neverc_template_t *tmpl = neverc_template_parse(bad[i], &err);
        check_true("invalid template rejected", tmpl == NULL);
        check_true("invalid template reports error", err != NULL);
        neverc_template_free(tmpl);
    }
}

static void test_pipeline_rejected(void) {
    printf("[pipeline rejected]\n");
    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, "Name", "<script>alert(1)</script>");
    size_t outlen = 99;

    /* A ported Go template that asks for HTML escaping must not parse as
     * a lookup of the key "Name | html" and emit the raw value. */
    char *r = neverc_template_render("{{.Name | html}}", &data, &outlen);
    check_true("pipeline render rejected", r == NULL && outlen == 0);
    free(r);

    const char *err = NULL;
    neverc_template_t *tmpl = neverc_template_parse("{{.Name|html}}", &err);
    check_true("pipeline without spaces rejected", tmpl == NULL && err != NULL);
    neverc_template_free(tmpl);

    r = neverc_template_render("{{.Name}}", &data, &outlen);
    check_str("raw text substitution", r, "<script>alert(1)</script>");
    free(r);

    outlen = 99;
    r = neverc_template_render("{{.Name()}}", &data, &outlen);
    check_true("method-call selector rejected", r == NULL && outlen == 0);
    free(r);
    neverc_template_data_free(&data);
}

static void test_null_safety(void) {
    printf("[null safety]\n");
    size_t outlen = 99;
    char *r = neverc_template_render(NULL, NULL, &outlen);
    check_true("NULL render rejected", r == NULL && outlen == 0);

    outlen = 99;
    r = neverc_template_execute(NULL, NULL, &outlen);
    check_true("NULL execute rejected", r == NULL && outlen == 0);

    neverc_template_data_init(NULL);
    neverc_template_data_set(NULL, "key", "value");
    neverc_template_data_free(NULL);
    check_true("NULL data get", neverc_template_data_get(NULL, "key") == NULL);

    neverc_template_data_t data;
    neverc_template_data_init(&data);
    neverc_template_data_set(&data, NULL, "value");
    check_true("NULL key ignored", data.nvars == 0);
    r = neverc_template_render("ok", &data, NULL);
    check_str("optional output length", r, "ok");
    free(r);
    neverc_template_data_free(&data);
}

int main(void) {
    printf("=== NeverC Text/Template Module Tests ===\n\n");
    test_simple_var();
    test_multiple_vars();
    test_missing_var();
    test_if_true();
    test_if_false();
    test_if_else();
    test_literal_text();
    test_complex();
    test_falsy_values();
    test_range_subset();
    test_action_whitespace();
    test_trim_markers();
    test_parse_errors();
    test_pipeline_rejected();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
