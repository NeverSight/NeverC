#include "neverc/text/template.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

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
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
