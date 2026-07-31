#include "neverc/std/encoding/pem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (strcmp(got, expected) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got '%s', expected '%s'\n", name, got, expected); }
}

static void check_mem(const char *name, const uint8_t *got, const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: memory mismatch\n", name); }
}

static void test_encode_decode_roundtrip(void) {
    printf("[pem encode/decode roundtrip]\n");

    uint8_t data[] = "Hello, World!";
    size_t data_len = 13;

    char pem_buf[1024];
    int enc_len = neverc_pem_encode(pem_buf, sizeof(pem_buf), "TEST DATA", data, data_len);
    check_int("encode returns >0", enc_len > 0, 1);

    /* Verify PEM structure */
    check_int("starts with BEGIN",
              strncmp(pem_buf, "-----BEGIN TEST DATA-----\n", 26) == 0, 1);
    check_int("contains END",
              strstr(pem_buf, "-----END TEST DATA-----\n") != NULL, 1);

    /* Decode it back */
    char type_buf[64];
    uint8_t out_buf[256];
    size_t bytes_written = 0;
    size_t rest_offset = 0;
    int rc = neverc_pem_decode(pem_buf, (size_t)enc_len,
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, &rest_offset);
    check_int("decode succeeds", rc, 0);
    check_str("type matches", type_buf, "TEST DATA");
    check_int("decoded length", (int)bytes_written, (int)data_len);
    check_mem("decoded data", out_buf, data, data_len);
}

static void test_known_pem(void) {
    printf("[pem decode known block]\n");

    const char *pem =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "SGVsbG8gV29ybGQ=\n"
        "-----END RSA PRIVATE KEY-----\n";

    char type_buf[64];
    uint8_t out_buf[256];
    size_t bytes_written = 0;
    size_t rest_offset = 0;

    int rc = neverc_pem_decode(pem, strlen(pem),
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, &rest_offset);
    check_int("decode known PEM", rc, 0);
    check_str("type RSA PRIVATE KEY", type_buf, "RSA PRIVATE KEY");
    check_int("decoded len 11", (int)bytes_written, 11);
    check_mem("decoded Hello World", out_buf, (const uint8_t *)"Hello World", 11);
}

static void test_empty_data(void) {
    printf("[pem empty data]\n");

    char pem_buf[512];
    uint8_t empty[] = "";
    int enc_len = neverc_pem_encode(pem_buf, sizeof(pem_buf), "EMPTY", (const uint8_t *)empty, 0);
    check_int("encode empty returns >0", enc_len > 0, 1);

    char type_buf[64];
    uint8_t out_buf[64];
    size_t bytes_written = 0;
    int rc = neverc_pem_decode(pem_buf, (size_t)enc_len,
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, NULL);
    check_int("decode empty", rc, 0);
    check_str("empty type", type_buf, "EMPTY");
    check_int("empty decoded len", (int)bytes_written, 0);
}

static void test_binary_data(void) {
    printf("[pem binary data]\n");

    uint8_t binary[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x80, 0x7F,
                        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    size_t binary_len = sizeof(binary);

    char pem_buf[1024];
    int enc_len = neverc_pem_encode(pem_buf, sizeof(pem_buf), "BINARY DATA", binary, binary_len);
    check_int("encode binary >0", enc_len > 0, 1);

    char type_buf[64];
    uint8_t out_buf[256];
    size_t bytes_written = 0;
    int rc = neverc_pem_decode(pem_buf, (size_t)enc_len,
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, NULL);
    check_int("decode binary", rc, 0);
    check_int("binary len", (int)bytes_written, (int)binary_len);
    check_mem("binary data", out_buf, binary, binary_len);
}

static void test_invalid_pem(void) {
    printf("[pem invalid input]\n");

    char type_buf[64];
    uint8_t out_buf[64];
    size_t bytes_written = 0;

    /* No PEM block */
    int rc = neverc_pem_decode("not a pem block", 15,
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, NULL);
    check_int("no PEM block", rc, -1);

    /* Missing END */
    const char *no_end = "-----BEGIN FOO-----\nYWJj\n";
    rc = neverc_pem_decode(no_end, strlen(no_end),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("missing END", rc, -1);

    /* Mismatched type */
    const char *mismatch = "-----BEGIN FOO-----\nYWJj\n-----END BAR-----\n";
    rc = neverc_pem_decode(mismatch, strlen(mismatch),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("mismatched type", rc, -1);

    const char *invalid_char =
        "-----BEGIN FOO-----\nYW!j\n-----END FOO-----\n";
    rc = neverc_pem_decode(invalid_char, strlen(invalid_char),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("invalid base64 character", rc, -1);

    const char *bad_padding =
        "-----BEGIN FOO-----\nY=Jj\n-----END FOO-----\n";
    rc = neverc_pem_decode(bad_padding, strlen(bad_padding),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("invalid base64 padding", rc, -1);

    const char *data_after_padding =
        "-----BEGIN FOO-----\nYQ==YQ==\n-----END FOO-----\n";
    rc = neverc_pem_decode(data_after_padding,
                            strlen(data_after_padding),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("base64 data after padding", rc, -1);

    const char *noncanonical =
        "-----BEGIN FOO-----\nYR==\n-----END FOO-----\n";
    rc = neverc_pem_decode(noncanonical, strlen(noncanonical),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("noncanonical base64 tail bits", rc, -1);

    const char *incomplete_quad =
        "-----BEGIN FOO-----\nYWI\n-----END FOO-----\n";
    rc = neverc_pem_decode(incomplete_quad, strlen(incomplete_quad),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("incomplete base64 quartet", rc, -1);

    const char *header_without_newline =
        "-----BEGIN FOO-----YWJj\n-----END FOO-----\n";
    rc = neverc_pem_decode(header_without_newline,
                            strlen(header_without_newline),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("header requires newline", rc, -1);
}

static void test_large_data(void) {
    printf("[pem large data]\n");

    uint8_t large[256];
    for (int i = 0; i < 256; i++) large[i] = (uint8_t)i;

    char pem_buf[4096];
    int enc_len = neverc_pem_encode(pem_buf, sizeof(pem_buf), "CERTIFICATE", large, 256);
    check_int("encode large >0", enc_len > 0, 1);

    char *exact = (char *)malloc((size_t)enc_len + 1);
    int exact_len = exact ? neverc_pem_encode(
        exact, (size_t)enc_len + 1, "CERTIFICATE", large, 256) : -1;
    check_int("encode with exact capacity", exact_len, enc_len);
    if (exact_len == enc_len)
        check_int("exact-capacity encoding matches",
                  memcmp(exact, pem_buf, (size_t)enc_len + 1) == 0, 1);
    free(exact);

    /* Verify line length: no base64 line should exceed 64 chars */
    const char *line_start = pem_buf;
    while (*line_start && strncmp(line_start, "-----END", 8) != 0) {
        const char *nl = strchr(line_start, '\n');
        if (!nl) break;
        /* Skip BEGIN line */
        if (strncmp(line_start, "-----BEGIN", 10) != 0) {
            int line_len = (int)(nl - line_start);
            if (line_len > 64) {
                tests_run++;
                tests_failed++;
                printf("  FAIL: line too long: %d chars\n", line_len);
                break;
            }
        }
        line_start = nl + 1;
    }

    char type_buf[64];
    uint8_t out_buf[512];
    size_t bytes_written = 0;
    int rc = neverc_pem_decode(pem_buf, (size_t)enc_len,
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, NULL);
    check_int("decode large", rc, 0);
    check_int("large len", (int)bytes_written, 256);
    check_mem("large data", out_buf, large, 256);
}

int main(void) {
    printf("=== NeverC PEM Library Tests ===\n\n");

    test_encode_decode_roundtrip();
    test_known_pem();
    test_empty_data();
    test_binary_data();
    test_invalid_pem();
    test_large_data();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
