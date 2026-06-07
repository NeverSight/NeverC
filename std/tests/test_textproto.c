#include "neverc/std/net/textproto.h"
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

static void test_canonical_key(void) {
    printf("[canonical_key]\n");
    char *k = neverc_textproto_canonical_mime_header_key("content-type");
    check_str("basic", k, "Content-Type");
    free(k);

    k = neverc_textproto_canonical_mime_header_key("CONTENT-TYPE");
    check_str("upper", k, "Content-Type");
    free(k);

    k = neverc_textproto_canonical_mime_header_key("x-forwarded-for");
    check_str("xforward", k, "X-Forwarded-For");
    free(k);

    k = neverc_textproto_canonical_mime_header_key("Accept");
    check_str("already_canon", k, "Accept");
    free(k);
}

static void test_mime_header(void) {
    printf("[mime_header]\n");
    neverc_mime_header_t h;
    neverc_mime_header_init(&h);

    neverc_mime_header_set(&h, "content-type", "text/html");
    check_str("get_ci", neverc_mime_header_get(&h, "Content-Type"), "text/html");
    check("len_1", neverc_mime_header_len(&h) == 1);

    neverc_mime_header_add(&h, "Accept", "application/json");
    check("len_2", neverc_mime_header_len(&h) == 2);

    neverc_mime_header_set(&h, "content-type", "text/plain");
    check_str("updated", neverc_mime_header_get(&h, "Content-Type"), "text/plain");
    check("len_still_2", neverc_mime_header_len(&h) == 2);

    neverc_mime_header_del(&h, "Accept");
    check("del", neverc_mime_header_get(&h, "Accept") == NULL);
    check("len_1_after_del", neverc_mime_header_len(&h) == 1);

    neverc_mime_header_free(&h);
}

static void test_read_mime_header(void) {
    printf("[read_mime_header]\n");
    const char *data = "Content-Type: text/plain\r\n"
                       "Content-Length: 42\r\n"
                       "X-Custom: hello world\r\n"
                       "\r\n"
                       "body here";

    neverc_mime_header_t h;
    neverc_mime_header_init(&h);
    size_t consumed = 0;
    int rc = neverc_textproto_read_mime_header(data, strlen(data), &h, &consumed);
    check("parse_ok", rc == 0);
    check_str("ct", neverc_mime_header_get(&h, "Content-Type"), "text/plain");
    check_str("cl", neverc_mime_header_get(&h, "Content-Length"), "42");
    check_str("custom", neverc_mime_header_get(&h, "X-Custom"), "hello world");
    check("header_count", neverc_mime_header_len(&h) == 3);
    check("consumed_before_body", consumed < strlen(data));
    neverc_mime_header_free(&h);
}

static void test_read_line(void) {
    printf("[read_line]\n");
    char line[256];
    size_t consumed = 0;

    const char *data = "hello world\r\nfoo\nbar";
    int rc = neverc_textproto_read_line(data, strlen(data), line, sizeof(line), &consumed);
    check("line1_ok", rc == 0);
    check_str("line1", line, "hello world");

    rc = neverc_textproto_read_line(data + consumed, strlen(data) - consumed,
                                     line, sizeof(line), &consumed);
    check("line2_ok", rc == 0);
    check_str("line2", line, "foo");
}

static void test_read_code_line(void) {
    printf("[read_code_line]\n");
    int code;
    const char *msg;

    int rc = neverc_textproto_read_code_line("220 smtp.example.com ready", &code, &msg);
    check("code_ok", rc == 0);
    check("code_220", code == 220);
    check_str("msg", msg, "smtp.example.com ready");

    rc = neverc_textproto_read_code_line("250-First line", &code, &msg);
    check("multiline", rc == 1);
    check("code_250", code == 250);

    rc = neverc_textproto_read_code_line("404 Not Found", &code, &msg);
    check("code_404", code == 404);
}

static void test_dot_lines(void) {
    printf("[dot_lines]\n");
    const char *data = "line1\r\nline2\r\n..escaped\r\n.\r\n";
    char *lines[10];
    size_t nlines = 0, consumed = 0;

    int rc = neverc_textproto_read_dot_lines(data, strlen(data), lines, 10, &nlines, &consumed);
    check("dot_ok", rc == 0);
    check("dot_count", nlines == 3);
    if (nlines >= 1) check_str("dot_line1", lines[0], "line1");
    if (nlines >= 2) check_str("dot_line2", lines[1], "line2");
    if (nlines >= 3) check_str("dot_escaped", lines[2], ".escaped");
    for (size_t i = 0; i < nlines; i++) free(lines[i]);
}

static void test_trim(void) {
    printf("[trim]\n");
    char out[64];
    neverc_textproto_trim_string("  hello  ", out, sizeof(out));
    check_str("trim", out, "hello");
    neverc_textproto_trim_string("no_trim", out, sizeof(out));
    check_str("no_trim", out, "no_trim");
    neverc_textproto_trim_string("", out, sizeof(out));
    check_str("empty", out, "");
}

int main(void) {
    test_canonical_key();
    test_mime_header();
    test_read_mime_header();
    test_read_line();
    test_read_code_line();
    test_dot_lines();
    test_trim();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
