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
    check_int("text 1 is terminated",
              strcmp(neverc_bufio_scanner_text(&sc), "hello") == 0, 1);

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
    check_int("crlf text is terminated",
              strcmp(neverc_bufio_scanner_text(&sc), "line1") == 0, 1);

    check_int("scan 2", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("crlf 2", neverc_bufio_scanner_bytes(&sc, &len), len, "line2");

    neverc_bufio_scanner_free(&sc);
}

typedef struct {
    size_t remaining;
} final_eof_reader_t;

static int final_eof_reader_read(void *ctx, uint8_t *buf, size_t len,
                                 size_t *n) {
    final_eof_reader_t *reader = (final_eof_reader_t *)ctx;
    size_t take = reader->remaining < len ? reader->remaining : len;
    memset(buf, 'x', take);
    reader->remaining -= take;
    *n = take;
    return reader->remaining == 0 ? NEVERC_IO_EOF : 0;
}

static void test_scanner_full_buffer_with_eof(void) {
    printf("[scanner full buffer with eof]\n");

    final_eof_reader_t reader = { NEVERC_BUFIO_DEFAULT_SIZE };
    neverc_io_reader_t r = { &reader, final_eof_reader_read };
    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);

    check_int("full eof scan", neverc_bufio_scanner_scan(&sc), 1);
    size_t len = 0;
    const uint8_t *token = neverc_bufio_scanner_bytes(&sc, &len);
    check_size("full eof token length", len, NEVERC_BUFIO_DEFAULT_SIZE);
    check_int("full eof token first byte", token[0], 'x');
    check_int("full eof token last byte", token[len - 1], 'x');
    check_int("full eof text is terminated",
              neverc_bufio_scanner_text(&sc)[len], '\0');
    check_int("full eof stops", neverc_bufio_scanner_scan(&sc), 0);

    neverc_bufio_scanner_free(&sc);
}

typedef struct {
    const uint8_t *data;
    size_t len;
    int err;
    int used;
} data_error_reader_t;

static int data_error_reader_read(void *ctx, uint8_t *buf, size_t len,
                                  size_t *n) {
    data_error_reader_t *reader = (data_error_reader_t *)ctx;
    if (reader->used) {
        *n = 0;
        return reader->err;
    }
    size_t take = reader->len < len ? reader->len : len;
    memcpy(buf, reader->data, take);
    reader->used = 1;
    *n = take;
    return reader->err;
}

static void test_scanner_data_with_terminal_error(void) {
    printf("[scanner data with terminal error]\n");

    static const uint8_t eof_data[] = "first\nsecond";
    data_error_reader_t eof_reader = {
        eof_data, sizeof(eof_data) - 1, NEVERC_IO_EOF, 0
    };
    neverc_io_reader_t eof_io = { &eof_reader, data_error_reader_read };
    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, eof_io);

    size_t len = 0;
    check_int("data eof first scan", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("data eof first token",
                neverc_bufio_scanner_bytes(&sc, &len), len, "first");
    check_int("data eof second scan", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("data eof second token",
                neverc_bufio_scanner_bytes(&sc, &len), len, "second");
    check_int("data eof stops", neverc_bufio_scanner_scan(&sc), 0);
    check_int("eof is not scanner error", neverc_bufio_scanner_err(&sc), 0);
    neverc_bufio_scanner_free(&sc);

    static const uint8_t error_data[] = "tail";
    data_error_reader_t error_reader = {
        error_data, sizeof(error_data) - 1, NEVERC_IO_ERR_UNEXP, 0
    };
    neverc_io_reader_t error_io = { &error_reader, data_error_reader_read };
    neverc_bufio_scanner_init(&sc, error_io);
    check_int("data error scan", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("data error token",
                neverc_bufio_scanner_bytes(&sc, &len), len, "tail");
    check_int("data error stops", neverc_bufio_scanner_scan(&sc), 0);
    check_int("data error is retained", neverc_bufio_scanner_err(&sc),
              NEVERC_IO_ERR_UNEXP);
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

static int partial_error_write(void *ctx, const uint8_t *buf, size_t len,
                               size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len / 2;
    return NEVERC_IO_ERR_UNEXP;
}

static void test_partial_write_error(void) {
    printf("[partial write error]\n");

    neverc_io_writer_t w = { NULL, partial_error_write };
    neverc_bufio_writer_t bw;
    neverc_bufio_writer_init_size(&bw, w, 4);
    size_t n = 0;
    check_int("partial write returns error",
              neverc_bufio_writer_write(&bw, (const uint8_t *)"abcd", 4, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("partial write accepted input", n, 4);
    check_size("partial write keeps remainder", bw.n, 2);
    check_bytes("partial write remainder", bw.buf, bw.n, "cd");
    neverc_bufio_writer_free(&bw);
}

static void test_zero_size_buffers(void) {
    printf("[zero size buffers]\n");

    const char *input = "x";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)input, 1);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 0);
    uint8_t byte = 0;
    check_int("zero size reader read",
              neverc_bufio_reader_read_byte(&br, &byte), 0);
    check_int("zero size reader byte", byte, 'x');
    neverc_bufio_reader_free(&br);

    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };
    neverc_bufio_writer_t bw;
    neverc_bufio_writer_init_size(&bw, w, 0);
    size_t n = 0;
    check_int("zero size writer write",
              neverc_bufio_writer_write(&bw, (const uint8_t *)"y", 1, &n),
              0);
    check_size("zero size writer count", n, 1);
    check_int("zero size writer flush", neverc_bufio_writer_flush(&bw), 0);
    check_bytes("zero size writer data", mw.data, mw.len, "y");
    neverc_bufio_writer_free(&bw);
    neverc_io_mem_writer_free(&mw);
}

static int no_progress_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    (void)ctx;
    (void)buf;
    (void)len;
    *n = 0;
    return 0;
}

static void test_reader_no_progress(void) {
    printf("[reader no progress]\n");

    neverc_io_reader_t r = { NULL, no_progress_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t byte = 0;
    check_int("no progress peek",
              neverc_bufio_reader_peek(&br, &byte, 1), 0);
    check_int("no progress becomes EOF",
              neverc_bufio_reader_read_byte(&br, &byte), NEVERC_IO_EOF);
    neverc_bufio_reader_free(&br);
}

static void test_scanner_missing_reader(void) {
    printf("[scanner missing reader]\n");

    neverc_io_reader_t r = {0};
    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);
    check_int("missing reader stops scan",
              neverc_bufio_scanner_scan(&sc), 0);
    check_int("missing reader reports error",
              neverc_bufio_scanner_err(&sc), NEVERC_IO_ERR_UNEXP);
    neverc_bufio_scanner_free(&sc);
}

int main(void) {
    printf("=== NeverC Bufio Module Tests ===\n\n");
    test_scanner();
    test_scanner_no_trailing_newline();
    test_scanner_crlf();
    test_scanner_full_buffer_with_eof();
    test_scanner_data_with_terminal_error();
    test_buffered_reader();
    test_buffered_reader_readline();
    test_buffered_writer();
    test_partial_write_error();
    test_zero_size_buffers();
    test_reader_no_progress();
    test_scanner_missing_reader();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
