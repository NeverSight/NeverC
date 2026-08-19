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

    k = neverc_textproto_canonical_mime_header_key("MIME-Version");
    check_str("mime_version", k, "MIME-Version");
    free(k);
    k = neverc_textproto_canonical_mime_header_key("mime-version");
    check_str("mime_version_lower", k, "MIME-Version");
    free(k);
    k = neverc_textproto_canonical_mime_header_key("MIME-VERSION");
    check_str("mime_version_upper", k, "MIME-Version");
    free(k);

    k = neverc_textproto_canonical_mime_header_key("X Name");
    check_str("invalid_unmodified", k, "X Name");
    free(k);
    k = neverc_textproto_canonical_mime_header_key("not:a-token");
    check_str("colon_unmodified", k, "not:a-token");
    free(k);
    k = neverc_textproto_canonical_mime_header_key("X\r\nBcc: hidden");
    check("crlf_key_rejected", k == NULL);
    k = neverc_textproto_canonical_mime_header_key("X\nInjected");
    check("lf_key_rejected", k == NULL);
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

    neverc_mime_header_add(&h, "X-Dup", "one");
    neverc_mime_header_add(&h, "X-Dup", "two");
    neverc_mime_header_add(&h, "X-Keep", "ok");
    check("dup_len", neverc_mime_header_len(&h) == 4);
    neverc_mime_header_set(&h, "x-dup", "only");
    check_str("set_replaces_all", neverc_mime_header_get(&h, "X-Dup"), "only");
    check("set_drops_extra_values", neverc_mime_header_len(&h) == 3);
    neverc_mime_header_add(&h, "X-Dup", "again");
    neverc_mime_header_del(&h, "X-DUP");
    check("del_all_values", neverc_mime_header_get(&h, "X-Dup") == NULL);
    check_str("del_leaves_other_keys", neverc_mime_header_get(&h, "X-Keep"),
              "ok");
    check("len_after_del_all", neverc_mime_header_len(&h) == 2);

    neverc_mime_header_add(&h, "MIME-Version", "1.0");
    check_str("get_mime_version", neverc_mime_header_get(&h, "mime-version"),
              "1.0");
    check("stored_mime_version",
          h.count >= 1 && h.keys[h.count - 1] &&
              strcmp(h.keys[h.count - 1], "MIME-Version") == 0);

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

    const char *folded = "X-Long: part1\r\n part2\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(folded, strlen(folded), &h, &consumed);
    check("folded_ok", rc == 0);
    check_str("folded_value", neverc_mime_header_get(&h, "X-Long"), "part1 part2");
    neverc_mime_header_free(&h);

    const char *fold_empty_first = "Subject:\r\n Hello\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(
        fold_empty_first, strlen(fold_empty_first), &h, &consumed);
    check("fold_empty_first_ok", rc == 0);
    check_str("fold_empty_first_value",
              neverc_mime_header_get(&h, "Subject"), "Hello");
    neverc_mime_header_free(&h);

    const char *fold_tab = "X-Long: part1\r\n\tpart2\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(fold_tab, strlen(fold_tab), &h,
                                          &consumed);
    check("fold_tab_ok", rc == 0);
    check_str("fold_tab_value", neverc_mime_header_get(&h, "X-Long"),
              "part1 part2");
    neverc_mime_header_free(&h);

    const char *fold_bcc = "From: user@x.com\r\n Bcc: hidden@x.com\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(fold_bcc, strlen(fold_bcc), &h,
                                          &consumed);
    check("fold_bcc_ok", rc == 0);
    check("fold_bcc_not_a_header", neverc_mime_header_get(&h, "Bcc") == NULL);
    check_str("fold_bcc_from_value", neverc_mime_header_get(&h, "From"),
              "user@x.com Bcc: hidden@x.com");
    neverc_mime_header_free(&h);

    const char *fold_tab_bcc =
        "From: user@x.com\r\n\tBcc: hidden@x.com\r\nTo: vis@x.com\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(fold_tab_bcc, strlen(fold_tab_bcc),
                                          &h, &consumed);
    check("fold_tab_bcc_ok", rc == 0);
    check("fold_tab_bcc_not_a_header",
          neverc_mime_header_get(&h, "Bcc") == NULL);
    check_str("fold_tab_bcc_from", neverc_mime_header_get(&h, "From"),
              "user@x.com Bcc: hidden@x.com");
    check_str("fold_tab_bcc_to", neverc_mime_header_get(&h, "To"), "vis@x.com");
    neverc_mime_header_free(&h);

    const char *empty_fold_then_bcc =
        "From: user@x.com\r\n"
        "\t \r\n"
        "Bcc: hidden@x.com\r\n"
        "\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(
        empty_fold_then_bcc, strlen(empty_fold_then_bcc), &h, &consumed);
    check("empty_fold_then_bcc_ok", rc == 0);
    check_str("empty_fold_keeps_from", neverc_mime_header_get(&h, "From"),
              "user@x.com");
    check_str("bcc_after_empty_fold_is_header",
              neverc_mime_header_get(&h, "Bcc"), "hidden@x.com");
    neverc_mime_header_free(&h);

    const char *same_line_bcc = "From: Bcc: hidden@x.com\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(same_line_bcc, strlen(same_line_bcc),
                                          &h, &consumed);
    check("same_line_bcc_ok", rc == 0);
    check("same_line_bcc_not_a_header",
          neverc_mime_header_get(&h, "Bcc") == NULL);
    check_str("same_line_bcc_is_from_value",
              neverc_mime_header_get(&h, "From"), "Bcc: hidden@x.com");
    neverc_mime_header_free(&h);

    const char *trail_ows = "X-Name: bar \r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(trail_ows, strlen(trail_ows), &h,
                                          &consumed);
    check("trailing_ows_ok", rc == 0);
    check_str("trailing_ows_stripped", neverc_mime_header_get(&h, "X-Name"),
              "bar");
    neverc_mime_header_free(&h);

    const char *fold_trail = "X-Long: part1 \r\n part2\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(fold_trail, strlen(fold_trail), &h,
                                          &consumed);
    check("fold_trailing_ows_ok", rc == 0);
    check_str("fold_trailing_ows_value",
              neverc_mime_header_get(&h, "X-Long"), "part1 part2");
    neverc_mime_header_free(&h);

    const char *empty_name = ": nosuch\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(
        empty_name, strlen(empty_name), &h, &consumed);
    check("empty header name rejected", rc == -1);
    neverc_mime_header_free(&h);

    char *huge = (char *)malloc(5000);
    if (huge) {
        memset(huge, 'A', 4200);
        memcpy(huge, "X-Long: ", 8);
        huge[4200] = '\r';
        huge[4201] = '\n';
        huge[4202] = '\r';
        huge[4203] = '\n';
        huge[4204] = '\0';
        neverc_mime_header_init(&h);
        consumed = 0;
        rc = neverc_textproto_read_mime_header(huge, 4204, &h, &consumed);
        check("oversize header line rejected", rc == -1);
        neverc_mime_header_free(&h);
        free(huge);
    }

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        "Foo: bar\r\n", strlen("Foo: bar\r\n"), &h, &consumed);
    check("header without blank line rejected", rc == -1);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        " folded\r\n\r\n", strlen(" folded\r\n\r\n"), &h, &consumed);
    check("orphan fold rejected", rc == -1);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        "NotAHeader\r\n\r\n", strlen("NotAHeader\r\n\r\n"), &h, &consumed);
    check("line without colon rejected", rc == -1);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        "X-Name: ok\rInjected: evil\r\n\r\n",
        strlen("X-Name: ok\rInjected: evil\r\n\r\n"), &h, &consumed);
    check("CR in header value rejected", rc == -1);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        "X-Name\rInjected: ok\r\n\r\n",
        strlen("X-Name\rInjected: ok\r\n\r\n"), &h, &consumed);
    check("CR in header name rejected", rc == -1);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        "X Name: ok\r\n\r\n", strlen("X Name: ok\r\n\r\n"), &h, &consumed);
    check("space in header name rejected", rc == -1);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(
        "X-Name: ok\x01more\r\n\r\n",
        strlen("X-Name: ok\x01more\r\n\r\n"), &h, &consumed);
    check("CTL in header value rejected", rc == -1);
    neverc_mime_header_free(&h);

    char nul_hdr[] = "X-Name: ok\0Y-Injected: evil\r\n\r\n";
    neverc_mime_header_init(&h);
    rc = neverc_textproto_read_mime_header(nul_hdr, sizeof(nul_hdr) - 1, &h,
                                          &consumed);
    check("embedded NUL in header line rejected", rc == -1);
    neverc_mime_header_free(&h);

    const char *tab_val = "X-Name: a\tb\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(tab_val, strlen(tab_val), &h,
                                          &consumed);
    check("tab in value ok", rc == 0);
    check_str("tab value", neverc_mime_header_get(&h, "X-Name"), "a\tb");
    neverc_mime_header_free(&h);

    const char *mv = "MIME-Version: 1.0\r\n\r\n";
    neverc_mime_header_init(&h);
    consumed = 0;
    rc = neverc_textproto_read_mime_header(mv, strlen(mv), &h, &consumed);
    check("mime_version_header_ok", rc == 0);
    check_str("mime_version_value",
              neverc_mime_header_get(&h, "MIME-Version"), "1.0");
    check("mime_version_stored",
          h.count == 1 && h.keys[0] &&
              strcmp(h.keys[0], "MIME-Version") == 0);
    neverc_mime_header_free(&h);

    neverc_mime_header_init(&h);
    neverc_mime_header_add(&h, "X-Bad\rName", "v");
    check("add rejects CR in name",
          neverc_mime_header_get(&h, "X-Bad\rName") == NULL);
    neverc_mime_header_add(&h, "X-Ok", "v\r\nInjected: x");
    check("add rejects CR in value", neverc_mime_header_get(&h, "X-Ok") == NULL);
    neverc_mime_header_set(&h, "X-Ok", "safe");
    neverc_mime_header_set(&h, "X-Ok", "v\rInjected");
    check("set rejects CR in value",
          neverc_mime_header_get(&h, "X-Ok") &&
              strcmp(neverc_mime_header_get(&h, "X-Ok"), "safe") == 0);
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

    rc = neverc_textproto_read_line(data, strlen(data), line, 0, &consumed);
    check("zero line capacity rejected", rc == -1);
    rc = neverc_textproto_read_line(
        data, strlen(data), NULL, sizeof(line), &consumed);
    check("NULL line buffer rejected", rc == -1);

    char tiny[4];
    rc = neverc_textproto_read_line(
        "hello world\r\n", 13, tiny, sizeof(tiny), &consumed);
    check("oversize line rejected", rc == -1);

    rc = neverc_textproto_read_line("no newline", 10, line, sizeof(line),
                                     &consumed);
    check("incomplete line rejected", rc == -1);

    char nul_line[] = "hello\0world\r\n";
    rc = neverc_textproto_read_line(nul_line, sizeof(nul_line) - 1, line,
                                    sizeof(line), &consumed);
    check("embedded NUL in line rejected", rc == -1);

    rc = neverc_textproto_read_line("hello\rworld\n", 12, line, sizeof(line),
                                    &consumed);
    check("embedded CR in line rejected", rc == -1);
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

    rc = neverc_textproto_read_code_line("220 foo\r\nBcc: hidden", &code, &msg);
    check("code_crlf_rejected", rc == -1);
    rc = neverc_textproto_read_code_line("220 foo\nInjected", &code, &msg);
    check("code_lf_rejected", rc == -1);
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

    rc = neverc_textproto_read_dot_lines(
        "line1\r\nline2\r\n", strlen("line1\r\nline2\r\n"),
        lines, 10, &nlines, &consumed);
    check("dot block without terminator rejected", rc == -1);
    check("failed dot parse clears lines", nlines == 0);

    rc = neverc_textproto_read_dot_lines(
        ".foo\r\n.\r\n", strlen(".foo\r\n.\r\n"),
        lines, 10, &nlines, &consumed);
    check("leading-dot destuff", rc == 0 && nlines == 1);
    if (nlines >= 1) check_str("destuffed .foo", lines[0], "foo");
    for (size_t i = 0; i < nlines; i++) free(lines[i]);

    rc = neverc_textproto_read_dot_lines(
        "a\r\nb\r\n.\r\n", strlen("a\r\nb\r\n.\r\n"),
        lines, 2, &nlines, &consumed);
    check("exact max_lines with terminator", rc == 0 && nlines == 2);
    if (nlines >= 1) check_str("exact_line1", lines[0], "a");
    if (nlines >= 2) check_str("exact_line2", lines[1], "b");
    for (size_t i = 0; i < nlines; i++) free(lines[i]);

    rc = neverc_textproto_read_dot_lines(
        "a\r\nb\r\nc\r\n.\r\n", strlen("a\r\nb\r\nc\r\n.\r\n"),
        lines, 2, &nlines, &consumed);
    check("over max_lines rejected", rc == -1);
    check("over max_lines clears", nlines == 0);

    rc = neverc_textproto_read_dot_lines(
        ".\r\n", strlen(".\r\n"), lines, 0, &nlines, &consumed);
    check("empty dot block max_lines 0", rc == 0 && nlines == 0);
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
    check("zero trim capacity rejected",
          neverc_textproto_trim_string("x", out, 0) == -1);
}

static void test_null_safety(void) {
    printf("[null_safety]\n");
    neverc_mime_header_init(NULL);
    neverc_mime_header_free(NULL);
    neverc_mime_header_add(NULL, "Key", "Value");
    neverc_mime_header_set(NULL, "Key", "Value");
    neverc_mime_header_del(NULL, "Key");
    check("NULL header get", neverc_mime_header_get(NULL, "Key") == NULL);
    check("NULL header len", neverc_mime_header_len(NULL) == 0);
    check("NULL canonical key",
          neverc_textproto_canonical_mime_header_key(NULL) == NULL);
    check("NULL code output rejected",
          neverc_textproto_read_code_line("200 ok", NULL, NULL) == -1);
}

int main(void) {
    test_canonical_key();
    test_mime_header();
    test_read_mime_header();
    test_read_line();
    test_read_code_line();
    test_dot_lines();
    test_trim();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_passed == tests_run) puts("passed");
    return tests_passed == tests_run ? 0 : 1;
}
