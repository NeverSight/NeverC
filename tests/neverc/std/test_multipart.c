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

static void test_generate_boundary(void) {
    printf("[generate boundary]\n");
    char b1[64], b2[64];
    ASSERT_TRUE(neverc_multipart_generate_boundary(b1, sizeof(b1)) == 32);
    ASSERT_TRUE(neverc_multipart_generate_boundary(b2, sizeof(b2)) == 32);
    ASSERT_TRUE(strlen(b1) == 32);
    ASSERT_TRUE(strcmp(b1, b2) != 0);
}

static void test_rejects_malformed_input(void) {
    printf("[rejects malformed input]\n");
    neverc_multipart_reader_t reader;

    const char *truncated =
        "--b\r\n"
        "\r\n"
        "body\r\n";
    memset(&reader, 0xa5, sizeof(reader));
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)truncated, strlen(truncated),
                  "b", &reader),
              -1);
    ASSERT_EQ(reader.part_count, 0);

    const char *boundary_prefix =
        "--b\r\n"
        "\r\n"
        "alpha\r\n--bX\r\nomega\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)boundary_prefix,
                  strlen(boundary_prefix), "b", &reader),
              0);
    ASSERT_EQ(reader.part_count, 1);
    ASSERT_TRUE(reader.parts[0].body_len ==
                strlen("alpha\r\n--bX\r\nomega"));
    ASSERT_TRUE(memcmp(reader.parts[0].body, "alpha\r\n--bX\r\nomega",
                       reader.parts[0].body_len) == 0);

    char long_boundary[72];
    memset(long_boundary, 'a', sizeof(long_boundary) - 1);
    long_boundary[sizeof(long_boundary) - 1] = '\0';
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)boundary_prefix,
                  strlen(boundary_prefix), long_boundary, &reader),
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

    const char *folded =
        "--b\r\n"
        "Content-Type: text/plain;\r\n"
        " charset=utf-8\r\n"
        "\r\n"
        "hi\r\n"
        "--b--\r\n";
    ASSERT_EQ(neverc_multipart_parse(
                  (const unsigned char *)folded, strlen(folded), "b",
                  &reader),
              0);
    ASSERT_EQ(reader.part_count, 1);
    ASSERT_STREQ(neverc_multipart_part_header(&reader.parts[0], "Content-Type"),
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
}

int main(void) {
    printf("=== NeverC mime/multipart Tests ===\n");
    test_parse_basic();
    test_parse_multiple_headers();
    test_write_roundtrip();
    test_generate_boundary();
    test_rejects_malformed_input();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
