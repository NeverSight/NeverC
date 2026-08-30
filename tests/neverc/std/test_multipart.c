#include "neverc/std/mime/multipart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_EQ(a, b) do { int _a=(a), _b=(b); tests_run++; \
    if (_a==_b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s\n", __LINE__, #expr); } \
} while(0)

#define ASSERT_STREQ(a, b) do { tests_run++; \
    if (a && b && strcmp(a,b)==0) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: got \"%s\", expected \"%s\"\n", __LINE__, a?a:"(null)", b); } \
} while(0)

static void test_parse_basic(void) {
    printf("[parse basic]\n");
    const char *data =
        "--boundary\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Hello World\r\n"
        "--boundary\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<p>Hi</p>\r\n"
        "--boundary--\r\n";

    neverc_multipart_reader_t *reader = (neverc_multipart_reader_t *)calloc(1, sizeof(*reader));
    ASSERT_EQ(neverc_multipart_parse((const unsigned char*)data, strlen(data), "boundary", reader), 0);
    ASSERT_EQ(reader->part_count, 2);

    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "Content-Type"), "text/plain");
    ASSERT_EQ(reader->parts[0].body_len, 11);
    ASSERT_TRUE(memcmp(reader->parts[0].body, "Hello World", 11) == 0);

    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[1], "Content-Type"), "text/html");
    ASSERT_EQ(reader->parts[1].body_len, 9);
    ASSERT_TRUE(memcmp(reader->parts[1].body, "<p>Hi</p>", 9) == 0);
    free(reader);
}

static void test_parse_multiple_headers(void) {
    printf("[parse multiple headers]\n");
    const char *data =
        "--sep\r\n"
        "Content-Disposition: form-data; name=\"file\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "binary data here\r\n"
        "--sep--\r\n";

    neverc_multipart_reader_t *reader = (neverc_multipart_reader_t *)calloc(1, sizeof(*reader));
    ASSERT_EQ(neverc_multipart_parse((const unsigned char*)data, strlen(data), "sep", reader), 0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ(reader->parts[0].header_count, 2);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "content-disposition"),
                 "form-data; name=\"file\"");
    free(reader);
}

static void test_header_get_rejects_invalid_public_state(void) {
    printf("[header getter invalid public state]\n");
    neverc_multipart_part_t part;
    memset(&part, 0, sizeof(part));

    part.header_count = NEVERC_MULTIPART_MAX_HEADERS + 1;
    ASSERT_TRUE(neverc_multipart_part_header(&part, "X-Test") == NULL);
    part.header_count = -1;
    ASSERT_TRUE(neverc_multipart_part_header(&part, "X-Test") == NULL);

    part.header_count = 1;
    memset(part.headers[0].key, 'X', sizeof(part.headers[0].key));
    strcpy(part.headers[0].value, "value");
    ASSERT_TRUE(neverc_multipart_part_header(&part, "X-Test") == NULL);

    strcpy(part.headers[0].key, "X-Test");
    memset(part.headers[0].value, 'V', sizeof(part.headers[0].value));
    ASSERT_TRUE(neverc_multipart_part_header(&part, "X-Test") == NULL);
}

static void test_write_roundtrip(void) {
    printf("[write roundtrip]\n");
    neverc_multipart_part_t parts[2];
    memset(parts, 0, sizeof(parts));

    strcpy(parts[0].headers[0].key, "Content-Type");
    strcpy(parts[0].headers[0].value, "text/plain");
    parts[0].header_count = 1;
    const char *body1 = "Hello";
    parts[0].body = (const unsigned char*)body1;
    parts[0].body_len = 5;

    strcpy(parts[1].headers[0].key, "Content-Type");
    strcpy(parts[1].headers[0].value, "text/html");
    parts[1].header_count = 1;
    const char *body2 = "<b>Hi</b>";
    parts[1].body = (const unsigned char*)body2;
    parts[1].body_len = 9;

    unsigned char out[4096];
    int n = neverc_multipart_write(parts, 2, "testbnd", out, sizeof(out));
    ASSERT_TRUE(n > 0);

    neverc_multipart_reader_t *reader = (neverc_multipart_reader_t *)calloc(1, sizeof(*reader));
    ASSERT_EQ(neverc_multipart_parse(out, (size_t)n, "testbnd", reader), 0);
    ASSERT_EQ(reader->part_count, 2);
    ASSERT_EQ(reader->parts[0].body_len, 5);
    ASSERT_TRUE(memcmp(reader->parts[0].body, "Hello", 5) == 0);
    ASSERT_EQ(reader->parts[1].body_len, 9);
    ASSERT_TRUE(memcmp(reader->parts[1].body, "<b>Hi</b>", 9) == 0);
    free(reader);
}

static void test_parse_empty_parts_and_preamble(void) {
    printf("[parse empty parts / preamble]\n");
    neverc_multipart_reader_t *reader =
        (neverc_multipart_reader_t *)calloc(1, sizeof(*reader));
    ASSERT_TRUE(reader != NULL);
    if (!reader) return;

    /* Go parseTests: "single empty part, --boundary" */
    const char *empty_close =
        "--abc\r\n"
        "Foo: bar\r\n"
        "\r\n"
        "--abc--";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)empty_close, strlen(empty_close),
                  "abc", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "Foo"), "bar");
    ASSERT_EQ((int)reader->parts[0].body_len, 0);

    /* Go: "single empty part, \\r\\n--boundary" */
    const char *empty_crlf_close =
        "--abc\r\n"
        "Foo: bar\r\n"
        "\r\n"
        "\r\n--abc--";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)empty_crlf_close,
                  strlen(empty_crlf_close), "abc", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 0);

    /* Go: "final part empty" — two empty bodies back-to-back */
    const char *two_empty =
        "--abc\r\n"
        "Foo: bar\r\n"
        "\r\n"
        "--abc\r\n"
        "Foo2: bar2\r\n"
        "\r\n"
        "--abc--";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)two_empty, strlen(two_empty),
                  "abc", reader),
              0);
    ASSERT_EQ(reader->part_count, 2);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "Foo"), "bar");
    ASSERT_EQ((int)reader->parts[0].body_len, 0);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[1], "Foo2"),
                 "bar2");
    ASSERT_EQ((int)reader->parts[1].body_len, 0);

    /* Go: "final part empty then lwsp" — close may omit the trailing CRLF */
    const char *empty_lwsp_eof =
        "--abc\r\n"
        "Foo: bar\r\n"
        "\r\n"
        "--abc-- \t";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)empty_lwsp_eof,
                  strlen(empty_lwsp_eof), "abc", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 0);

    /* Go: "leading line" / TestMultipart preamble is ignored */
    const char *preamble =
        "This is a multi-part message. This line is ignored.\r\n"
        "--MyBoundary\r\n"
        "foo: bar\r\n"
        "\r\n"
        "My value\r\nThe end.\r\n"
        "--MyBoundary--\r\n"
        "useless trailer\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)preamble, strlen(preamble),
                  "MyBoundary", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "foo"), "bar");
    ASSERT_EQ((int)reader->parts[0].body_len, (int)strlen("My value\r\nThe end."));
    ASSERT_TRUE(memcmp(reader->parts[0].body, "My value\r\nThe end.",
                       reader->parts[0].body_len) == 0);

    /* Mid-line "--boundary" in the preamble is not a delimiter */
    const char *preamble_embed =
        "ignore --MyBoundary here\r\n"
        "--MyBoundary\r\n"
        "\r\n"
        "ok\r\n"
        "--MyBoundary--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)preamble_embed,
                  strlen(preamble_embed), "MyBoundary", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 2);
    ASSERT_TRUE(memcmp(reader->parts[0].body, "ok", 2) == 0);

    /* LF-only preamble + empty part (Go TestMultipartOnlyNewlines) */
    const char *lf_preamble_empty =
        "ignored\n"
        "--MyBoundary\n"
        "name: x\n"
        "\n"
        "--MyBoundary--\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)lf_preamble_empty,
                  strlen(lf_preamble_empty), "MyBoundary", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "name"), "x");
    ASSERT_EQ((int)reader->parts[0].body_len, 0);

    /* A CR-only line is not a delimiter line start (Go / RFC 2046 CRLF).
     * The mid-line `--boundary` is ignored; the later close is a closer. */
    const char *cr_preamble =
        "preamble\r"
        "--MyBoundary\r\n"
        "\r\n"
        "ok\r\n"
        "--MyBoundary--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)cr_preamble,
                  strlen(cr_preamble), "MyBoundary", reader),
              0);
    ASSERT_EQ(reader->part_count, 0);

    free(reader);
}

static void test_delimiter_requires_line_end(void) {
    printf("[delimiter requires line end]\n");
    neverc_multipart_reader_t *reader =
        (neverc_multipart_reader_t *)calloc(1, sizeof(*reader));
    ASSERT_TRUE(reader != NULL);
    if (!reader) return;

    /* RFC 2046: `--b\r` + more on the same line is body, not a delimiter. */
    const char *cr_not_eol =
        "--b\r\n"
        "\r\n"
        "hello\n"
        "--b\rnot-a-break\n"
        "world\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)cr_not_eol, strlen(cr_not_eol),
                  "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len,
              (int)strlen("hello\n--b\rnot-a-break\nworld"));
    ASSERT_TRUE(memcmp(reader->parts[0].body,
                       "hello\n--b\rnot-a-break\nworld",
                       reader->parts[0].body_len) == 0);

    /* Fake close `--b--\r` + more on the same line must not end the message. */
    const char *fake_close =
        "--b\r\n"
        "\r\n"
        "one\r\n"
        "--b--\rstill-the-body\r\n"
        "--b\r\n"
        "\r\n"
        "two\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)fake_close, strlen(fake_close),
                  "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 2);
    ASSERT_EQ((int)reader->parts[0].body_len,
              (int)strlen("one\r\n--b--\rstill-the-body"));
    ASSERT_TRUE(memcmp(reader->parts[0].body, "one\r\n--b--\rstill-the-body",
                       reader->parts[0].body_len) == 0);
    ASSERT_EQ((int)reader->parts[1].body_len, 3);
    ASSERT_TRUE(memcmp(reader->parts[1].body, "two", 3) == 0);

    /* A lone CR is neither RFC 2046 CRLF nor Go's accepted no-EOL close. */
    const char *close_cr_eof =
        "--b\r\n"
        "\r\n"
        "ok\r\n"
        "--b--\r";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)close_cr_eof, strlen(close_cr_eof),
                  "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    /* Reader.nl starts as CRLF. Before any ordinary boundary can switch it to
     * LF mode, an LF-only final boundary is not a valid empty multipart. The
     * same close with CRLF or with no final line ending is valid. */
    const char *first_lf_close = "--b--\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)first_lf_close,
                  strlen(first_lf_close), "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    const char *first_crlf_close = "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)first_crlf_close,
                  strlen(first_crlf_close), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 0);

    const char *first_eof_close = "--b--";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)first_eof_close,
                  strlen(first_eof_close), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 0);

    /* Go locks Reader.nl from the first ordinary boundary line. A CRLF
     * multipart cannot terminate on an LF-only delimiter (whether the
     * mismatch is before or after the marker), and vice versa. */
    const char *crlf_then_lf_prefix =
        "--b\r\n"
        "\r\n"
        "body\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)crlf_then_lf_prefix,
                  strlen(crlf_then_lf_prefix), "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    /* An LF-prefixed boundary-looking line is body text in locked CRLF mode.
     * A later CRLF-prefixed close still completes the part. */
    const char *crlf_fake_lf_then_real_close =
        "--b\r\n"
        "\r\n"
        "body\n"
        "--b--\r\n"
        "tail\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)crlf_fake_lf_then_real_close,
                  strlen(crlf_fake_lf_then_real_close), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len,
              (int)strlen("body\n--b--\r\ntail"));
    ASSERT_TRUE(memcmp(reader->parts[0].body, "body\n--b--\r\ntail",
                       reader->parts[0].body_len) == 0);

    /* Go's partReader has a total==0 exception: after an LF-only empty header
     * section, a closing boundary starts the body directly and remains valid
     * even though Reader.nl was locked to CRLF by the first boundary. */
    const char *crlf_with_lf_empty_headers =
        "--b\r\n"
        "\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)crlf_with_lf_empty_headers,
                  strlen(crlf_with_lf_empty_headers), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 0);

    const char *crlf_then_lf_suffix =
        "--b\r\n"
        "\r\n"
        "body\r\n"
        "--b--\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)crlf_then_lf_suffix,
                  strlen(crlf_then_lf_suffix), "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    /* A suffix mismatch after a correctly prefixed candidate is a hard error;
     * Go does not skip it as body and recover at a later valid close. */
    const char *crlf_bad_suffix_then_real_close =
        "--b\r\n"
        "\r\n"
        "body\r\n"
        "--b--\n"
        "tail\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)crlf_bad_suffix_then_real_close,
                  strlen(crlf_bad_suffix_then_real_close), "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    const char *lf_then_crlf_suffix =
        "--b\n"
        "\n"
        "body\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)lf_then_crlf_suffix,
                  strlen(lf_then_crlf_suffix), "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    /* In LF mode Go searches for "\n--boundary". A CR immediately before
     * that LF remains part of the body; only the recorded LF is stripped. */
    const char *lf_with_cr_body_tail =
        "--b\n"
        "\n"
        "body\r\n"
        "--b--\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)lf_with_cr_body_tail,
                  strlen(lf_with_cr_body_tail), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 5);
    ASSERT_TRUE(memcmp(reader->parts[0].body, "body\r", 5) == 0);

    free(reader);
}

static void test_generate_boundary(void) {
    printf("[generate boundary]\n");
    char b1[64], b2[64];
    ASSERT_TRUE(neverc_multipart_generate_boundary(b1, sizeof(b1)) == 32);
    ASSERT_TRUE(neverc_multipart_generate_boundary(b2, sizeof(b2)) == 32);
    ASSERT_TRUE(strlen(b1) == 32);
    ASSERT_TRUE(strcmp(b1, b2) != 0);

    /* Sixteen random bytes render as 32 hex bytes plus the terminator. */
    char exact[33];
    char short_buf[32];
    ASSERT_EQ(neverc_multipart_generate_boundary(exact, sizeof(exact)), 32);
    ASSERT_EQ((int)strlen(exact), 32);
    ASSERT_EQ(neverc_multipart_generate_boundary(short_buf, sizeof(short_buf)),
              -1);
}

static void test_rejects_malformed_input(void) {
    printf("[rejects malformed input]\n");
    neverc_multipart_reader_t *reader =
        (neverc_multipart_reader_t *)malloc(sizeof(*reader));
    ASSERT_TRUE(reader != NULL);
    if (!reader) return;

    const char *truncated =
        "--b\r\n"
        "\r\n"
        "body\r\n";
    memset(reader, 0xa5, sizeof(*reader));
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)truncated, strlen(truncated),
                  "b", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    /* Go TestMultipartTruncated: preamble + part + "--boundary-" is not a
     * close and must not be treated as a complete message. */
    const char *truncated_dash =
        "This is a multi-part message. This line is ignored.\r\n"
        "--MyBoundary\r\n"
        "foo-bar: baz\r\n"
        "\r\n"
        "Oh no, premature EOF!\r\n"
        "--MyBoundary-";
    memset(reader, 0xa5, sizeof(*reader));
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)truncated_dash,
                  strlen(truncated_dash), "MyBoundary", reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    const char *boundary_prefix =
        "--b\r\n"
        "\r\n"
        "alpha\r\n--bX\r\nomega\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)boundary_prefix,
                  strlen(boundary_prefix), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_TRUE(reader->parts[0].body_len ==
                strlen("alpha\r\n--bX\r\nomega"));
    ASSERT_TRUE(memcmp(reader->parts[0].body, "alpha\r\n--bX\r\nomega",
                       reader->parts[0].body_len) == 0);

    char long_boundary[72];
    memset(long_boundary, 'a', sizeof(long_boundary) - 1);
    long_boundary[sizeof(long_boundary) - 1] = '\0';
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)boundary_prefix,
                  strlen(boundary_prefix), long_boundary, reader),
              -1);

    neverc_multipart_part_t part;
    unsigned char output[256];
    memset(&part, 0, sizeof(part));
    part.body_len = 1;
    ASSERT_EQ(neverc_multipart_write(
                  &part, 1, "b", output, sizeof(output)),
              -1);

    part.body_len = 0;
    part.header_count = 1;
    strcpy(part.headers[0].key, "X-Bad\nInjected");
    strcpy(part.headers[0].value, "value");
    ASSERT_EQ(neverc_multipart_write(
                  &part, 1, "b", output, sizeof(output)),
              -1);
    ASSERT_EQ(neverc_multipart_write(
                  &part, 1, long_boundary, output, sizeof(output)),
              -1);

    const char *bare_cr =
        "--b\r\n"
        "X-Foo: bar\rbaz\r\n"
        "\r\n"
        "hi\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)bare_cr, strlen(bare_cr), "b",
                  reader),
              -1);

    const char *folded =
        "--b\r\n"
        "Content-Type: text/plain;\r\n"
        " charset=utf-8\r\n"
        "\r\n"
        "hi\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)folded, strlen(folded), "b",
                  reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "Content-Type"),
                 "text/plain; charset=utf-8");

    neverc_multipart_part_t inject;
    memset(&inject, 0, sizeof(inject));
    inject.body = (const unsigned char *)"--inj\r\nowned";
    inject.body_len = 12;
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);
    inject.body = (const unsigned char *)"ok\r\n--inj\r\n";
    inject.body_len = 11;
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);
    /* The writer emits CRLF framing, so an LF-terminated delimiter candidate
     * at a valid CRLF line start would make its own parser reject the output. */
    inject.body = (const unsigned char *)"--inj\nowned";
    inject.body_len = strlen((const char *)inject.body);
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);
    inject.body = (const unsigned char *)"ok\r\n--inj\nowned";
    inject.body_len = strlen((const char *)inject.body);
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);
    /* Exact `--inj` at EOF of the body becomes `--inj\r\n` on the wire. */
    inject.body = (const unsigned char *)"--inj";
    inject.body_len = 5;
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);
    inject.body = (const unsigned char *)"ok\n--inj";
    inject.body_len = 8;
    int injected_len = neverc_multipart_write(
        &inject, 1, "inj", output, sizeof(output));
    ASSERT_TRUE(injected_len > 0);
    if (injected_len > 0) {
        ASSERT_EQ(neverc_multipart_parse(
                      output, (size_t)injected_len, "inj", reader),
                  0);
        ASSERT_EQ(reader->part_count, 1);
        ASSERT_EQ((int)reader->parts[0].body_len, 8);
        ASSERT_TRUE(memcmp(reader->parts[0].body, "ok\n--inj", 8) == 0);
    }
    inject.body = (const unsigned char *)"--inj ";
    inject.body_len = 6;
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);

    /* A prefix of the delimiter is body text: write must accept it and
     * parse must keep it (RFC 2046: `--b` is not `--bX`). */
    {
        neverc_multipart_part_t prefix_part;
        memset(&prefix_part, 0, sizeof(prefix_part));
        const char *prefix_body = "alpha\r\n--bX\r\nomega";
        prefix_part.body = (const unsigned char *)prefix_body;
        prefix_part.body_len = strlen(prefix_body);
        int wn = neverc_multipart_write(&prefix_part, 1, "b", output,
                                        sizeof(output));
        ASSERT_TRUE(wn > 0);
        ASSERT_EQ(neverc_multipart_parse(output, (size_t)wn, "b", reader), 0);
        ASSERT_EQ(reader->part_count, 1);
        ASSERT_EQ((int)reader->parts[0].body_len, (int)prefix_part.body_len);
        ASSERT_TRUE(memcmp(reader->parts[0].body, prefix_body,
                           prefix_part.body_len) == 0);

        const char *cr_body = "hello\n--b\rnot-a-break\nworld";
        prefix_part.body = (const unsigned char *)cr_body;
        prefix_part.body_len = strlen(cr_body);
        wn = neverc_multipart_write(&prefix_part, 1, "b", output,
                                    sizeof(output));
        ASSERT_TRUE(wn > 0);
        ASSERT_EQ(neverc_multipart_parse(output, (size_t)wn, "b", reader), 0);
        ASSERT_EQ(reader->part_count, 1);
        ASSERT_EQ((int)reader->parts[0].body_len, (int)prefix_part.body_len);
        ASSERT_TRUE(memcmp(reader->parts[0].body, cr_body,
                           prefix_part.body_len) == 0);

        const char *close_prefix = "--b--Xstill";
        prefix_part.body = (const unsigned char *)close_prefix;
        prefix_part.body_len = strlen(close_prefix);
        wn = neverc_multipart_write(&prefix_part, 1, "b", output,
                                    sizeof(output));
        ASSERT_TRUE(wn > 0);
        ASSERT_EQ(neverc_multipart_parse(output, (size_t)wn, "b", reader), 0);
        ASSERT_EQ(reader->part_count, 1);
        ASSERT_EQ((int)reader->parts[0].body_len, (int)prefix_part.body_len);
    }

    const char *lwsp =
        "--b \r\n"
        "\r\n"
        "hi\r\n"
        "--b-- \t\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)lwsp, strlen(lwsp), "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 2);
    ASSERT_TRUE(memcmp(reader->parts[0].body, "hi", 2) == 0);

    /* RFC 2046 allows a space inside the boundary, but not at the end. */
    const char *spaced =
        "--simple boundary\r\n"
        "\r\n"
        "ok\r\n"
        "--simple boundary--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)spaced, strlen(spaced),
                  "simple boundary", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 2);

    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)spaced, strlen(spaced),
                  "simple boundary ", reader),
              -1);
    free(reader);
}

static void test_missing_close_and_epilogue(void) {
    printf("[missing close / epilogue]\n");
    neverc_multipart_reader_t *reader =
        (neverc_multipart_reader_t *)calloc(1, sizeof(*reader));
    ASSERT_TRUE(reader != NULL);
    if (!reader) return;

    /* A non-closing last delimiter is not a complete message. */
    const char *no_close =
        "--b\r\n"
        "\r\n"
        "body\r\n"
        "--b\r\n";
    memset(reader, 0xa5, sizeof(*reader));
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)no_close, strlen(no_close), "b",
                  reader),
              -1);
    ASSERT_EQ(reader->part_count, 0);

    /* Close delimiter ends the message; a following fake part is epilogue. */
    const char *epilogue_part =
        "--b\r\n"
        "\r\n"
        "ok\r\n"
        "--b--\r\n"
        "--b\r\n"
        "\r\n"
        "smuggled\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)epilogue_part, strlen(epilogue_part),
                  "b", reader),
              0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_EQ((int)reader->parts[0].body_len, 2);
    ASSERT_TRUE(memcmp(reader->parts[0].body, "ok", 2) == 0);

    char longkey[200];
    memset(longkey, 'A', sizeof(longkey) - 1);
    longkey[sizeof(longkey) - 1] = '\0';
    ASSERT_TRUE(neverc_multipart_part_header(&reader->parts[0], longkey) ==
                NULL);
    ASSERT_TRUE(neverc_multipart_part_header(&reader->parts[0],
                                             "Content-TypeExtra") == NULL);

    neverc_multipart_part_t inject;
    unsigned char output[256];
    memset(&inject, 0, sizeof(inject));
    inject.body = (const unsigned char *)"--inj--";
    inject.body_len = 7;
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);
    inject.body = (const unsigned char *)"--inj--\r\nowned";
    inject.body_len = 14;
    ASSERT_EQ(neverc_multipart_write(&inject, 1, "inj", output, sizeof(output)),
              -1);

    neverc_multipart_part_t ew;
    memset(&ew, 0, sizeof(ew));
    strcpy(ew.headers[0].key, "X-Name");
    strcpy(ew.headers[0].value, "=?utf-8?q?=0D=0AXed:_hidden?=");
    ew.header_count = 1;
    ASSERT_EQ(neverc_multipart_write(&ew, 1, "b", output, sizeof(output)), -1);

    const char *ew_part =
        "--b\r\n"
        "X-Name: =?utf-8?q?=0D=0AXed:_hidden?=\r\n"
        "\r\n"
        "hi\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)ew_part, strlen(ew_part), "b",
                  reader),
              -1);

    strcpy(ew.headers[0].value, "=?utf-8?q?ok?=");
    int ewn = neverc_multipart_write(&ew, 1, "b", output, sizeof(output));
    ASSERT_TRUE(ewn > 0);
    ASSERT_EQ(neverc_multipart_parse(output, (size_t)ewn, "b", reader), 0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "X-Name"),
                 "=?utf-8?q?ok?=");

    /* UTF-8 Å (C3 85): 0x85 is a continuation, not C1 NEL. */
    strcpy(ew.headers[0].value, "\xC3\x85ngstrom");
    ewn = neverc_multipart_write(&ew, 1, "b", output, sizeof(output));
    ASSERT_TRUE(ewn > 0);
    ASSERT_EQ(neverc_multipart_parse(output, (size_t)ewn, "b", reader), 0);
    ASSERT_EQ(reader->part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader->parts[0], "X-Name"),
                 "\xC3\x85ngstrom");

    strcpy(ew.headers[0].value, "x\x85y");
    ASSERT_EQ(neverc_multipart_write(&ew, 1, "b", output, sizeof(output)), -1);

    const char *nel_part =
        "--b\r\n"
        "X-Name: x\x85y\r\n"
        "\r\n"
        "hi\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)nel_part, strlen(nel_part), "b",
                  reader),
              -1);

    strcpy(ew.headers[0].value, "x\xC0\x8Ay");
    ASSERT_EQ(neverc_multipart_write(&ew, 1, "b", output, sizeof(output)), -1);

    const char *overlong_part =
        "--b\r\n"
        "X-Name: x\xC0\x8Ay\r\n"
        "\r\n"
        "hi\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)overlong_part, strlen(overlong_part),
                  "b", reader),
              -1);

    free(reader);
}

int main(void) {
    printf("=== NeverC mime/multipart Tests ===\n");
    test_parse_basic();
    test_parse_multiple_headers();
    test_header_get_rejects_invalid_public_state();
    test_parse_empty_parts_and_preamble();
    test_delimiter_requires_line_end();
    test_write_roundtrip();
    test_generate_boundary();
    test_rejects_malformed_input();
    test_missing_close_and_epilogue();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
