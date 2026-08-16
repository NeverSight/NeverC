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

static void test_rfc1421_headers(void) {
    printf("[pem RFC 1421 encapsulated headers]\n");

    /* Encrypted OpenSSL-style PEM: headers, blank line, then payload. */
    const char *with_blank =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "Proc-Type: 4,ENCRYPTED\n"
        "DEK-Info: AES-256-CBC,0123456789ABCDEF0123456789ABCDEF\n"
        "\n"
        "SGVsbG8gV29ybGQ=\n"
        "-----END RSA PRIVATE KEY-----\n";

    char type_buf[64];
    uint8_t out_buf[256];
    size_t bytes_written = 0;
    int rc = neverc_pem_decode(with_blank, strlen(with_blank),
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, NULL);
    check_int("headers+blank decode", rc, 0);
    check_str("headers+blank type", type_buf, "RSA PRIVATE KEY");
    check_int("headers+blank len", (int)bytes_written, 11);
    check_mem("headers+blank data", out_buf,
              (const uint8_t *)"Hello World", 11);

    /* Header then body with no intervening blank line (Go pem.Decode). */
    const char *no_blank =
        "-----BEGIN FOO-----\n"
        "X-Custom: yes\n"
        "SGVsbG8gV29ybGQ=\n"
        "-----END FOO-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(no_blank, strlen(no_blank),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("header no-blank decode", rc, 0);
    check_str("header no-blank type", type_buf, "FOO");
    check_int("header no-blank len", (int)bytes_written, 11);
    check_mem("header no-blank data", out_buf,
              (const uint8_t *)"Hello World", 11);
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

    /* The bounded marker search must not form or inspect pointers beyond a
     * non-NUL-terminated input object when the only candidate is rejected. */
    const char bounded_no_marker[11] = {
        '-', '-', '-', '-', '-', 'B', 'E', 'G', 'I', 'N', 'X'
    };
    rc = neverc_pem_decode(bounded_no_marker, sizeof(bounded_no_marker),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("bounded input without marker", rc, -1);

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

    const char *unpadded_one =
        "-----BEGIN FOO-----\nZg\n-----END FOO-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(unpadded_one, strlen(unpadded_one),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("unpadded 2-char decode", rc, 0);
    check_int("unpadded 2-char len", (int)bytes_written, 1);
    check_mem("unpadded 2-char", out_buf, (const uint8_t *)"f", 1);

    const char *unpadded_two =
        "-----BEGIN FOO-----\nZm8\n-----END FOO-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(unpadded_two, strlen(unpadded_two),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("unpadded 3-char decode", rc, 0);
    check_int("unpadded 3-char len", (int)bytes_written, 2);
    check_mem("unpadded 3-char", out_buf, (const uint8_t *)"fo", 2);

    const char *unpadded_mixed =
        "-----BEGIN FOO-----\nZm9vYg\n-----END FOO-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(unpadded_mixed, strlen(unpadded_mixed),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("unpadded 6-char decode", rc, 0);
    check_int("unpadded 6-char len", (int)bytes_written, 4);
    check_mem("unpadded 6-char", out_buf, (const uint8_t *)"foob", 4);

    const char *incomplete_quad =
        "-----BEGIN FOO-----\nY\n-----END FOO-----\n";
    rc = neverc_pem_decode(incomplete_quad, strlen(incomplete_quad),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("incomplete base64 singleton", rc, -1);

    const char *header_without_newline =
        "-----BEGIN FOO-----YWJj\n-----END FOO-----\n";
    rc = neverc_pem_decode(header_without_newline,
                            strlen(header_without_newline),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("header requires newline", rc, -1);

    const char *end_junk =
        "-----BEGIN FOO-----\n"
        "dGVzdA==\n"
        "-----END FOO----- .\n";
    rc = neverc_pem_decode(end_junk, strlen(end_junk),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("END trailing non-whitespace", rc, -1);

    const char *bare_headers =
        "-----BEGIN INVALID HEADERS-----\n"
        "Header: 1\n"
        "-----END INVALID HEADERS-----\n";
    rc = neverc_pem_decode(bare_headers, strlen(bare_headers),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("headers without blank or body", rc, -1);
}

static void test_go_armor(void) {
    printf("[pem Go armor skip]\n");

    char type_buf[64];
    uint8_t out_buf[64];
    size_t bytes_written = 0;
    size_t rest_offset = 0;

    const char *invalid_then_valid =
        "-----BEGIN COMMENT-----\n"
        "foo foo foo\n"
        "-----END COMMENT-----\n"
        "-----BEGIN TEST BLOCK-----\n"
        "aGVsbG8=\n"
        "-----END TEST BLOCK-----\n";
    int rc = neverc_pem_decode(invalid_then_valid, strlen(invalid_then_valid),
                                type_buf, sizeof(type_buf),
                                out_buf, sizeof(out_buf),
                                &bytes_written, &rest_offset);
    check_int("skip invalid section", rc, 0);
    check_str("skip invalid type", type_buf, "TEST BLOCK");
    check_int("skip invalid len", (int)bytes_written, 5);
    check_mem("skip invalid data", out_buf, (const uint8_t *)"hello", 5);
    check_int("skip invalid rest at EOF",
              (int)rest_offset, (int)strlen(invalid_then_valid));

    const char *multi_begin =
        "-----BEGIN TEST BLOCK-----\n"
        "-----BEGIN TEST BLOCK-----\n"
        "-----BEGIN TEST BLOCK-----\n"
        "aGVsbG8=\n"
        "-----END TEST BLOCK-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(multi_begin, strlen(multi_begin),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("multiple BEGIN", rc, 0);
    check_str("multiple BEGIN type", type_buf, "TEST BLOCK");
    check_int("multiple BEGIN len", (int)bytes_written, 5);
    check_mem("multiple BEGIN data", out_buf, (const uint8_t *)"hello", 5);

    const char *leading_malformed =
        "-----BEGIN PUBLIC KEY\n"
        "aGVsbG8=\n"
        "-----END PUBLIC KEY-----\n"
        "-----BEGIN TEST BLOCK-----\n"
        "aGVsbG8=\n"
        "-----END TEST BLOCK-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(leading_malformed, strlen(leading_malformed),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("leading malformed BEGIN", rc, 0);
    check_str("leading malformed type", type_buf, "TEST BLOCK");
    check_int("leading malformed len", (int)bytes_written, 5);
    check_mem("leading malformed data", out_buf, (const uint8_t *)"hello", 5);

    const char *openssl_preamble =
        "verify return:0\n"
        "-----BEGIN CERTIFICATE-----\n"
        "sdlfkjskldfj\n"
        " -----BEGIN CERTIFICATE-----\n"
        "-----BEGIN CERTIFICATE-----\n"
        "aGVsbG8=\n"
        "-----END CERTIFICATE-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(openssl_preamble, strlen(openssl_preamble),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("openssl preamble", rc, 0);
    check_str("openssl preamble type", type_buf, "CERTIFICATE");
    check_int("openssl preamble len", (int)bytes_written, 5);
    check_mem("openssl preamble data", out_buf, (const uint8_t *)"hello", 5);

    const char *bare_then_valid =
        "-----BEGIN INVALID HEADERS-----\n"
        "Header: 1\n"
        "-----END INVALID HEADERS-----\n"
        "-----BEGIN TEST BLOCK-----\n"
        "aGVsbG8=\n"
        "-----END TEST BLOCK-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(bare_then_valid, strlen(bare_then_valid),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("skip bare headers", rc, 0);
    check_str("skip bare headers type", type_buf, "TEST BLOCK");
    check_int("skip bare headers len", (int)bytes_written, 5);

    const char *leading_garbage =
        "foo foo foo\n"
        "-----BEGIN TEST BLOCK-----\n"
        "aGVsbG8=\n"
        "-----END TEST BLOCK-----\n";
    bytes_written = 0;
    rc = neverc_pem_decode(leading_garbage, strlen(leading_garbage),
                            type_buf, sizeof(type_buf),
                            out_buf, sizeof(out_buf),
                            &bytes_written, NULL);
    check_int("leading garbage", rc, 0);
    check_str("leading garbage type", type_buf, "TEST BLOCK");
    check_int("leading garbage len", (int)bytes_written, 5);
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
    test_rfc1421_headers();
    test_invalid_pem();
    test_go_armor();
    test_large_data();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
