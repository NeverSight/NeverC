#include "neverc/std/bufio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}
static void check_bytes(const char *name, const uint8_t *got, size_t glen,
                         const char *expected) {
    tests_run++;
    size_t elen = strlen(expected);
    if (glen == elen && memcmp(got, expected, glen) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: len %zu != %zu\n", name, glen, elen); }
}

static void test_scanner(void) {
    printf("[scanner]\n");
    const char *data = "hello\nworld\nfoo\n";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);

    check_int("scan 1", neverc_bufio_scanner_scan(&sc), 1);
    size_t len;
    const uint8_t *line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("line 1", line, len, "hello");

    check_int("scan 2", neverc_bufio_scanner_scan(&sc), 1);
    line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("line 2", line, len, "world");

    check_int("scan 3", neverc_bufio_scanner_scan(&sc), 1);
    line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("line 3", line, len, "foo");

    check_int("scan eof", neverc_bufio_scanner_scan(&sc), 0);

    neverc_bufio_scanner_free(&sc);
}

static void test_scanner_no_trailing_newline(void) {
    printf("[scanner no trailing newline]\n");
    const char *data = "abc\ndef";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);

    check_int("scan 1", neverc_bufio_scanner_scan(&sc), 1);
    size_t len;
    const uint8_t *line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("line 1", line, len, "abc");

    check_int("scan 2", neverc_bufio_scanner_scan(&sc), 1);
    line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("line 2", line, len, "def");

    check_int("scan eof", neverc_bufio_scanner_scan(&sc), 0);
    neverc_bufio_scanner_free(&sc);
}

static void test_scanner_crlf(void) {
    printf("[scanner crlf]\n");
    const char *data = "line1\r\nline2\r\n";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);

    check_int("scan 1", neverc_bufio_scanner_scan(&sc), 1);
    size_t len;
    check_bytes("crlf 1", neverc_bufio_scanner_bytes(&sc, &len), len, "line1");

    check_int("scan 2", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("crlf 2", neverc_bufio_scanner_bytes(&sc, &len), len, "line2");

    neverc_bufio_scanner_free(&sc);
}

static void test_buffered_reader(void) {
    printf("[buffered reader]\n");
    const char *data = "Hello, buffered world!";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);

    uint8_t b;
    check_int("read_byte H", neverc_bufio_reader_read_byte(&br, &b), 0);
    check_int("byte H", b, 'H');

    uint8_t buf[5];
    size_t n;
    neverc_bufio_reader_read(&br, buf, 5, &n);
    check_size("read 5", n, 5);
    check_bytes("buf content", buf, 5, "ello,");

    neverc_bufio_reader_free(&br);
}

static void test_buffered_reader_readline(void) {
    printf("[buffered reader readline]\n");
    const char *data = "first\nsecond\nthird";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_bufio_reader_t br;
    neverc_bufio_reader_init(&br, r);

    size_t len;
    uint8_t *line1 = neverc_bufio_reader_read_line(&br, &len);
    check_bytes("line 1", line1, len, "first");
    free(line1);

    uint8_t *line2 = neverc_bufio_reader_read_line(&br, &len);
    check_bytes("line 2", line2, len, "second");
    free(line2);

    uint8_t *line3 = neverc_bufio_reader_read_line(&br, &len);
    check_bytes("line 3", line3, len, "third");
    free(line3);

    uint8_t *line4 = neverc_bufio_reader_read_line(&br, &len);
    check_int("eof null", line4 == NULL, 1);

    neverc_bufio_reader_free(&br);
}

static void test_buffered_writer(void) {
    printf("[buffered writer]\n");
    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };

    neverc_bufio_writer_t bw;
    neverc_bufio_writer_init_size(&bw, w, 8);

    const char *data = "Hello";
    size_t n;
    neverc_bufio_writer_write(&bw, (const uint8_t *)data, 5, &n);
    check_size("write n", n, 5);
    check_size("not flushed yet", mw.len, 0);

    neverc_bufio_writer_flush(&bw);
    check_bytes("flushed", mw.data, mw.len, "Hello");

    neverc_bufio_writer_write_byte(&bw, '!');
    neverc_bufio_writer_flush(&bw);
    check_bytes("byte added", mw.data, mw.len, "Hello!");

    neverc_bufio_writer_free(&bw);
    neverc_io_mem_writer_free(&mw);
}

int main(void) {
    printf("=== NeverC Bufio Module Tests ===\n\n");
    test_scanner();
    test_scanner_no_trailing_newline();
    test_scanner_crlf();
    test_buffered_reader();
    test_buffered_reader_readline();
    test_buffered_writer();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
