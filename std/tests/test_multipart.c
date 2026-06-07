#include "neverc/mime/multipart.h"
#include <stdio.h>
#include <string.h>

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

    neverc_multipart_reader_t reader;
    ASSERT_EQ(neverc_multipart_parse((const unsigned char*)data, strlen(data), "boundary", &reader), 0);
    ASSERT_EQ(reader.part_count, 2);

    ASSERT_STREQ(neverc_multipart_part_header(&reader.parts[0], "Content-Type"), "text/plain");
    ASSERT_EQ(reader.parts[0].body_len, 11);
    ASSERT_TRUE(memcmp(reader.parts[0].body, "Hello World", 11) == 0);

    ASSERT_STREQ(neverc_multipart_part_header(&reader.parts[1], "Content-Type"), "text/html");
    ASSERT_EQ(reader.parts[1].body_len, 9);
    ASSERT_TRUE(memcmp(reader.parts[1].body, "<p>Hi</p>", 9) == 0);
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

    neverc_multipart_reader_t reader;
    ASSERT_EQ(neverc_multipart_parse((const unsigned char*)data, strlen(data), "sep", &reader), 0);
    ASSERT_EQ(reader.part_count, 1);
    ASSERT_EQ(reader.parts[0].header_count, 2);
    ASSERT_STREQ(neverc_multipart_part_header(&reader.parts[0], "content-disposition"),
                 "form-data; name=\"file\"");
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

    /* Parse it back */
    neverc_multipart_reader_t reader;
    ASSERT_EQ(neverc_multipart_parse(out, (size_t)n, "testbnd", &reader), 0);
    ASSERT_EQ(reader.part_count, 2);
    ASSERT_EQ(reader.parts[0].body_len, 5);
    ASSERT_TRUE(memcmp(reader.parts[0].body, "Hello", 5) == 0);
    ASSERT_EQ(reader.parts[1].body_len, 9);
    ASSERT_TRUE(memcmp(reader.parts[1].body, "<b>Hi</b>", 9) == 0);
}

static void test_generate_boundary(void) {
    printf("[generate boundary]\n");
    char b1[64], b2[64];
    ASSERT_TRUE(neverc_multipart_generate_boundary(b1, sizeof(b1)) == 32);
    ASSERT_TRUE(neverc_multipart_generate_boundary(b2, sizeof(b2)) == 32);
    ASSERT_TRUE(strlen(b1) == 32);
    ASSERT_TRUE(strcmp(b1, b2) != 0);
}

int main(void) {
    printf("=== NeverC mime/multipart Tests ===\n");
    test_parse_basic();
    test_parse_multiple_headers();
    test_write_roundtrip();
    test_generate_boundary();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
