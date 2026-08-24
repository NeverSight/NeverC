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
    const uint8_t *line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("crlf 1", line, len, "line1");
    check_int("crlf text is terminated",
              strcmp(neverc_bufio_scanner_text(&sc), "line1") == 0, 1);

    check_int("scan 2", neverc_bufio_scanner_scan(&sc), 1);
    line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("crlf 2", line, len, "line2");

    neverc_bufio_scanner_free(&sc);
}

static void test_scanner_empty_lines(void) {
    printf("[scanner empty lines]\n");
    const char *data = "a\n\nb";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);

    check_int("empty lines 1", neverc_bufio_scanner_scan(&sc), 1);
    size_t len;
    const uint8_t *line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("empty lines first", line, len, "a");

    check_int("empty lines 2", neverc_bufio_scanner_scan(&sc), 1);
    line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("empty lines blank", line, len, "");

    check_int("empty lines 3", neverc_bufio_scanner_scan(&sc), 1);
    line = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("empty lines last", line, len, "b");

    check_int("empty lines eof", neverc_bufio_scanner_scan(&sc), 0);
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
    const uint8_t *token = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("data eof first token", token, len, "first");
    check_int("data eof second scan", neverc_bufio_scanner_scan(&sc), 1);
    token = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("data eof second token", token, len, "second");
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
    token = neverc_bufio_scanner_bytes(&sc, &len);
    check_bytes("data error token", token, len, "tail");
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

static int one_byte_mem_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (len > 1) len = 1;
    return neverc_io_mem_reader_read(ctx, buf, len, n);
}

static void test_buffered_reader_short_read(void) {
    printf("[buffered reader short read]\n");
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)"abcdef", 6);
    neverc_io_reader_t r = { &mr, one_byte_mem_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t out[5];
    size_t n = 99;
    check_int("short read rc",
              neverc_bufio_reader_read(&br, out, sizeof(out), &n), 0);
    check_size("short read n", n, 1);
    check_int("short read byte", out[0], 'a');
    n = 99;
    check_int("short read second rc",
              neverc_bufio_reader_read(&br, out, sizeof(out), &n), 0);
    check_size("short read second n", n, 1);
    check_int("short read second byte", out[0], 'b');
    neverc_bufio_reader_free(&br);
}

static void test_buffered_reader_preserves_terminal_error(void) {
    printf("[buffered reader terminal error]\n");

    data_error_reader_t empty_error = {
        (const uint8_t *)"", 0, NEVERC_IO_ERR_UNEXP, 0
    };
    neverc_io_reader_t r = { &empty_error, data_error_reader_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t byte = 0;
    check_int("empty terminal error preserved",
              neverc_bufio_reader_read_byte(&br, &byte),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_reader_free(&br);

    static const uint8_t one_byte[] = "x";
    data_error_reader_t data_error = {
        one_byte, 1, NEVERC_IO_ERR_UNEXP, 0
    };
    r.ctx = &data_error;
    neverc_bufio_reader_init_size(&br, r, 8);
    check_int("terminal error data read succeeds",
              neverc_bufio_reader_read_byte(&br, &byte), 0);
    check_int("terminal error data value", byte, 'x');
    check_int("terminal error follows buffered data",
              neverc_bufio_reader_read_byte(&br, &byte),
              NEVERC_IO_ERR_UNEXP);
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

static void test_buffered_writer_default_size(void) {
    printf("[buffered writer default size]\n");
    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };

    neverc_bufio_writer_t bw;
    neverc_bufio_writer_init(&bw, w);
    check_size("default cap", bw.buf_cap, NEVERC_BUFIO_DEFAULT_SIZE);

    size_t n;
    neverc_bufio_writer_write(&bw, (const uint8_t *)"ok", 2, &n);
    check_size("default write n", n, 2);
    neverc_bufio_writer_flush(&bw);
    check_bytes("default flushed", mw.data, mw.len, "ok");

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

static int partial_success_write(void *ctx, const uint8_t *buf, size_t len,
                                 size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len / 2;
    return 0;
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
    size_t leftover = bw.n;
    size_t n2 = 99;
    check_int("sticky write returns prior error",
              neverc_bufio_writer_write(&bw, (const uint8_t *)"xy", 2, &n2),
              NEVERC_IO_ERR_UNEXP);
    check_size("sticky write accepts nothing", n2, 0);
    check_size("sticky write keeps remainder", bw.n, leftover);
    check_int("sticky write_byte returns prior error",
              neverc_bufio_writer_write_byte(&bw, 'z'),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_writer_free(&bw);
}

static void test_partial_write_without_error(void) {
    printf("[partial write without error]\n");

    neverc_io_writer_t w = { NULL, partial_success_write };
    neverc_bufio_writer_t bw;
    neverc_bufio_writer_init_size(&bw, w, 4);
    size_t n = 0;
    check_int("partial success buffers input",
              neverc_bufio_writer_write(
                  &bw, (const uint8_t *)"ab", 2, &n), 0);
    check_size("partial success accepted input", n, 2);
    check_int("partial success flush reports short write",
              neverc_bufio_writer_flush(&bw), NEVERC_IO_ERR_SHORT);
    check_size("partial success keeps remainder", bw.n, 1);
    check_bytes("partial success remainder", bw.buf, bw.n, "b");
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

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t position;
    unsigned empty_reads;
} transient_empty_reader_t;

static int transient_empty_read(void *ctx, uint8_t *buf, size_t len,
                                size_t *n) {
    transient_empty_reader_t *reader = (transient_empty_reader_t *)ctx;
    if (reader->empty_reads > 0) {
        reader->empty_reads--;
        *n = 0;
        return 0;
    }
    size_t remaining = reader->len - reader->position;
    size_t take = remaining < len ? remaining : len;
    if (take > 0)
        memcpy(buf, reader->data + reader->position, take);
    reader->position += take;
    *n = take;
    return reader->position == reader->len ? NEVERC_IO_EOF : 0;
}

static int overreporting_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len + 1;
    return 0;
}

static void test_reader_rejects_invalid_count(void) {
    printf("[reader invalid count]\n");

    neverc_io_reader_t r = { NULL, overreporting_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t output[16] = {0};
    size_t n = 99;
    check_int("invalid count becomes unexpected error",
              neverc_bufio_reader_read(&br, output, 9, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("invalid count copies nothing", n, 0);
    neverc_bufio_reader_free(&br);
}

static void test_reader_no_progress(void) {
    printf("[reader no progress]\n");

    neverc_io_reader_t r = { NULL, no_progress_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t byte = 0;
    check_int("no progress peek",
              neverc_bufio_reader_peek(&br, &byte, 1), 0);
    check_int("persistent no progress becomes error",
              neverc_bufio_reader_read_byte(&br, &byte),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_reader_free(&br);

    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t direct[8] = {0};
    size_t n = 99;
    check_int("direct persistent no progress becomes error",
              neverc_bufio_reader_read(&br, direct, sizeof(direct), &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("direct persistent no progress count", n, 0);
    neverc_bufio_reader_free(&br);

    neverc_bufio_scanner_t scanner;
    neverc_bufio_scanner_init(&scanner, r);
    check_int("scanner persistent no progress stops",
              neverc_bufio_scanner_scan(&scanner), 0);
    check_int("scanner persistent no progress reports error",
              neverc_bufio_scanner_err(&scanner), NEVERC_IO_ERR_UNEXP);
    neverc_bufio_scanner_free(&scanner);
}

static void test_reader_transient_no_progress(void) {
    printf("[reader transient no progress]\n");

    static const uint8_t byte_data[] = "z";
    transient_empty_reader_t byte_reader = {
        byte_data, sizeof(byte_data) - 1, 0, 1
    };
    neverc_io_reader_t r = { &byte_reader, transient_empty_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t byte = 0;
    check_int("reader retries transient empty read",
              neverc_bufio_reader_read_byte(&br, &byte), 0);
    check_int("reader gets data after transient empty read", byte, 'z');
    neverc_bufio_reader_free(&br);

    transient_empty_reader_t direct_reader = {
        byte_data, sizeof(byte_data) - 1, 0, 1
    };
    r.ctx = &direct_reader;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t direct[8] = {0};
    size_t direct_n = 0;
    check_int("direct read retries transient empty read",
              neverc_bufio_reader_read(&br, direct, sizeof(direct), &direct_n),
              NEVERC_IO_EOF);
    check_size("direct read gets data count", direct_n, 1);
    check_int("direct read gets data after transient empty read", direct[0], 'z');
    neverc_bufio_reader_free(&br);

    static const uint8_t line_data[] = "line\n";
    transient_empty_reader_t line_reader = {
        line_data, sizeof(line_data) - 1, 0, 1
    };
    r.ctx = &line_reader;
    neverc_bufio_scanner_t scanner;
    neverc_bufio_scanner_init(&scanner, r);
    check_int("scanner retries transient empty read",
              neverc_bufio_scanner_scan(&scanner), 1);
    size_t len = 0;
    const uint8_t *token = neverc_bufio_scanner_bytes(&scanner, &len);
    check_bytes("scanner data after transient empty read",
                token, len, "line");
    check_int("scanner transient read has no error",
              neverc_bufio_scanner_err(&scanner), 0);
    neverc_bufio_scanner_free(&scanner);
}

static void test_reader_peek_larger_than_buffer(void) {
    printf("[reader peek larger than buffer]\n");

    static const uint8_t input[] = "abcdefghijklmnop";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, input, sizeof(input) - 1);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t output[9] = {0};
    check_int("oversized peek returns buffered bytes",
              neverc_bufio_reader_peek(&br, output, sizeof(output)), 8);
    check_int("oversized peek preserves data",
              memcmp(output, "abcdefgh", 8) == 0, 1);
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

static void test_scanner_token_too_long(void) {
    printf("[scanner token too long]\n");

    size_t n = (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE + 2;
    uint8_t *data = (uint8_t *)malloc(n);
    memset(data, 'x', n - 1);
    data[n - 1] = '\n';
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, data, n);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);

    check_int("too long scan fails closed",
              neverc_bufio_scanner_scan(&sc), 0);
    check_int("too long err", neverc_bufio_scanner_err(&sc),
              NEVERC_BUFIO_ERR_TOO_LONG);
    size_t len = 99;
    check_int("too long token cleared",
              neverc_bufio_scanner_bytes(&sc, &len) == NULL, 1);
    check_size("too long token len", len, 0);
    check_int("too long stays failed", neverc_bufio_scanner_scan(&sc), 0);
    neverc_bufio_scanner_free(&sc);
    free(data);

    /* Go: 65536 bytes plus a newline fills MaxScanTokenSize with no
     * delimiter in-buffer, then Scan reports ErrTooLong. A probe byte
     * past the limit used to accept the line. */
    n = (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE + 1;
    data = (uint8_t *)malloc(n);
    memset(data, 'x', n - 1);
    data[n - 1] = '\n';
    neverc_io_mem_reader_init(&mr, data, n);
    r.ctx = &mr;
    neverc_bufio_scanner_init(&sc, r);
    check_int("max line plus newline fails closed",
              neverc_bufio_scanner_scan(&sc), 0);
    check_int("max line plus newline is too long",
              neverc_bufio_scanner_err(&sc), NEVERC_BUFIO_ERR_TOO_LONG);
    neverc_bufio_scanner_free(&sc);
    free(data);

    data = (uint8_t *)malloc((size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE);
    memset(data, 'y', (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE);
    neverc_io_mem_reader_init(&mr, data,
                              (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE);
    r.ctx = &mr;
    neverc_bufio_scanner_init(&sc, r);
    check_int("max token at eof is allowed",
              neverc_bufio_scanner_scan(&sc), 1);
    const uint8_t *token = neverc_bufio_scanner_bytes(&sc, &len);
    check_size("max token length", len,
               (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE);
    check_int("max token first", token && token[0] == 'y', 1);
    check_int("max token last", token && token[len - 1] == 'y', 1);
    check_int("max token stops", neverc_bufio_scanner_scan(&sc), 0);
    neverc_bufio_scanner_free(&sc);
    free(data);
}

/* Go bufio.ScanBytes: only the (atEOF && len==0) guard. Indexing data[0]
 * is implied by returning data[0:1]. Calling this with empty !atEOF must
 * not happen. */
static int go_scan_bytes(const uint8_t *data, size_t data_len, int at_eof,
                         size_t *advance, const uint8_t **token,
                         size_t *token_len, int *err) {
    if (advance) *advance = 0;
    if (token) *token = NULL;
    if (token_len) *token_len = 0;
    if (err) *err = 0;
    if (at_eof && data_len == 0) return 0;
    if (!data || data_len == 0) {
        if (err) *err = NEVERC_IO_ERR_UNEXP;
        return -1;
    }
    *advance = 1;
    *token = data;
    *token_len = 1;
    return 1;
}

static int g_split_empty_non_eof;

static int contract_scan_lines(const uint8_t *data, size_t data_len, int at_eof,
                               size_t *advance, const uint8_t **token,
                               size_t *token_len, int *err) {
    if (!at_eof && data_len == 0)
        g_split_empty_non_eof++;
    return neverc_bufio_scan_lines(data, data_len, at_eof, advance, token,
                                   token_len, err);
}

static void test_scanner_split_empty_contract(void) {
    printf("[scanner split empty contract]\n");

    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)"ab", 2);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);
    neverc_bufio_scanner_split(&sc, go_scan_bytes);

    size_t len = 0;
    check_int("go ScanBytes first", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("go ScanBytes a", neverc_bufio_scanner_bytes(&sc, &len),
                len, "a");
    check_int("go ScanBytes second", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("go ScanBytes b", neverc_bufio_scanner_bytes(&sc, &len),
                len, "b");
    check_int("go ScanBytes eof", neverc_bufio_scanner_scan(&sc), 0);
    check_int("go ScanBytes no err", neverc_bufio_scanner_err(&sc), 0);
    neverc_bufio_scanner_free(&sc);

    neverc_io_mem_reader_init(&mr, (const uint8_t *)"", 0);
    r.ctx = &mr;
    neverc_bufio_scanner_init(&sc, r);
    neverc_bufio_scanner_split(&sc, go_scan_bytes);
    check_int("go ScanBytes empty file", neverc_bufio_scanner_scan(&sc), 0);
    check_int("go ScanBytes empty file no err",
              neverc_bufio_scanner_err(&sc), 0);
    neverc_bufio_scanner_free(&sc);

    g_split_empty_non_eof = 0;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)"x\ny\n", 4);
    r.ctx = &mr;
    neverc_bufio_scanner_init(&sc, r);
    neverc_bufio_scanner_split(&sc, contract_scan_lines);
    check_int("contract scan 1", neverc_bufio_scanner_scan(&sc), 1);
    check_int("contract scan 2", neverc_bufio_scanner_scan(&sc), 1);
    check_int("contract scan eof", neverc_bufio_scanner_scan(&sc), 0);
    check_int("split not called with empty !atEOF", g_split_empty_non_eof, 0);
    neverc_bufio_scanner_free(&sc);
}

static int huge_token_split(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err) {
    (void)at_eof;
    if (err) *err = 0;
    if (advance) *advance = 0;
    if (token) *token = NULL;
    if (token_len) *token_len = 0;
    if (data_len == 0) return 0;
    *advance = 1;
    *token = data;
    *token_len = (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE + 1;
    return 1;
}

static void test_scanner_split_func(void) {
    printf("[scanner split func]\n");

    const char *words = "  hello  world\tfoo\n";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)words, strlen(words));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_scanner_t sc;
    neverc_bufio_scanner_init(&sc, r);
    neverc_bufio_scanner_split(&sc, neverc_bufio_scan_words);

    size_t len = 0;
    check_int("words 1", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("word hello", neverc_bufio_scanner_bytes(&sc, &len),
                len, "hello");
    check_int("words 2", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("word world", neverc_bufio_scanner_bytes(&sc, &len),
                len, "world");
    check_int("words 3", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("word foo", neverc_bufio_scanner_bytes(&sc, &len),
                len, "foo");
    check_int("words eof", neverc_bufio_scanner_scan(&sc), 0);
    neverc_bufio_scanner_free(&sc);

    {
        /* Go ScanWords: 0x85/0xA0 are U+0085/U+00A0 as runes, not as
         * UTF-8 continuation bytes. "Å" is C3 85 and is one token. */
        const char *utf8_words = "\xC3\x85 \xC3\xA0";
        neverc_io_mem_reader_init(&mr, (const uint8_t *)utf8_words,
                                  strlen(utf8_words));
        r.ctx = &mr;
        neverc_bufio_scanner_init(&sc, r);
        neverc_bufio_scanner_split(&sc, neverc_bufio_scan_words);
        check_int("utf8 words 1", neverc_bufio_scanner_scan(&sc), 1);
        check_bytes("word A-ring", neverc_bufio_scanner_bytes(&sc, &len),
                    len, "\xC3\x85");
        check_int("utf8 words 2", neverc_bufio_scanner_scan(&sc), 1);
        check_bytes("word a-grave", neverc_bufio_scanner_bytes(&sc, &len),
                    len, "\xC3\xA0");
        check_int("utf8 words eof", neverc_bufio_scanner_scan(&sc), 0);
        neverc_bufio_scanner_free(&sc);
    }

    {
        const char *ideo = "a\xE3\x80\x80" "b"; /* U+3000 ideographic space */
        neverc_io_mem_reader_init(&mr, (const uint8_t *)ideo, strlen(ideo));
        r.ctx = &mr;
        neverc_bufio_scanner_init(&sc, r);
        neverc_bufio_scanner_split(&sc, neverc_bufio_scan_words);
        check_int("ideo space 1", neverc_bufio_scanner_scan(&sc), 1);
        check_bytes("word a", neverc_bufio_scanner_bytes(&sc, &len), len, "a");
        check_int("ideo space 2", neverc_bufio_scanner_scan(&sc), 1);
        check_bytes("word b", neverc_bufio_scanner_bytes(&sc, &len), len, "b");
        neverc_bufio_scanner_free(&sc);
    }

    {
        /* Go FullRune: 0xE3 0x20 is a finished error rune + space, not an
         * incomplete 3-byte sequence that should stall ScanWords. */
        const uint8_t bad_prefix[] = {0xE3, 0x20};
        size_t advance = 0;
        const uint8_t *token = NULL;
        size_t token_len = 0;
        int split_err = 0;
        check_int("invalid utf8 prefix splits",
                  neverc_bufio_scan_words(bad_prefix, sizeof(bad_prefix), 0,
                                          &advance, &token, &token_len,
                                          &split_err),
                  1);
        check_int("invalid utf8 prefix token len", token_len, 1);
        check_int("invalid utf8 prefix token byte",
                  token && token[0] == 0xE3, 1);
        check_int("invalid utf8 prefix advance", advance, 2);
    }

    neverc_io_mem_reader_init(&mr, (const uint8_t *)"ab", 2);
    r.ctx = &mr;
    neverc_bufio_scanner_init(&sc, r);
    neverc_bufio_scanner_split(&sc, neverc_bufio_scan_bytes);
    check_int("bytes 1", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("byte a", neverc_bufio_scanner_bytes(&sc, &len), len, "a");
    check_int("byte a text terminated",
              neverc_bufio_scanner_text(&sc) &&
                  strcmp(neverc_bufio_scanner_text(&sc), "a") == 0, 1);
    check_int("bytes 2", neverc_bufio_scanner_scan(&sc), 1);
    check_bytes("byte b", neverc_bufio_scanner_bytes(&sc, &len), len, "b");
    check_int("byte b text terminated",
              neverc_bufio_scanner_text(&sc) &&
                  strcmp(neverc_bufio_scanner_text(&sc), "b") == 0, 1);
    check_int("bytes eof", neverc_bufio_scanner_scan(&sc), 0);
    neverc_bufio_scanner_free(&sc);

    neverc_io_mem_reader_init(&mr, (const uint8_t *)"x", 1);
    r.ctx = &mr;
    neverc_bufio_scanner_init(&sc, r);
    neverc_bufio_scanner_split(&sc, huge_token_split);
    check_int("split oversized token fails closed",
              neverc_bufio_scanner_scan(&sc), 0);
    check_int("split oversized token err",
              neverc_bufio_scanner_err(&sc), NEVERC_BUFIO_ERR_TOO_LONG);
    neverc_bufio_scanner_free(&sc);
}

static void test_peek_after_unread(void) {
    printf("[peek after unread]\n");

    const char *input = "abcdef";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)input, 6);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);

    uint8_t peeked[4] = {0};
    check_int("peek before read",
              neverc_bufio_reader_peek(&br, peeked, 3), 3);
    check_int("peek does not consume",
              memcmp(peeked, "abc", 3) == 0, 1);

    uint8_t byte = 0;
    check_int("read after peek",
              neverc_bufio_reader_read_byte(&br, &byte), 0);
    check_int("read after peek byte", byte, 'a');
    check_int("unread last byte",
              neverc_bufio_reader_unread_byte(&br), 0);

    memset(peeked, 0, sizeof(peeked));
    check_int("peek after unread",
              neverc_bufio_reader_peek(&br, peeked, 3), 3);
    check_int("peek after unread sees unread byte",
              memcmp(peeked, "abc", 3) == 0, 1);

    check_int("read after unread peek",
              neverc_bufio_reader_read_byte(&br, &byte), 0);
    check_int("read after unread peek byte", byte, 'a');

    uint8_t rest[2];
    size_t n = 0;
    check_int("read rest after unread peek",
              neverc_bufio_reader_read(&br, rest, 2, &n), 0);
    check_size("read rest n", n, 2);
    check_int("read rest content", memcmp(rest, "bc", 2) == 0, 1);

    memset(peeked, 0, sizeof(peeked));
    check_int("peek after later read",
              neverc_bufio_reader_peek(&br, peeked, 1), 1);
    check_int("peek after later read byte", peeked[0], 'd');
    check_int("peek invalidates unread",
              neverc_bufio_reader_unread_byte(&br), NEVERC_IO_ERR_UNEXP);
    neverc_bufio_reader_free(&br);
}

static void test_invalid_arguments(void) {
    printf("[invalid arguments]\n");

    neverc_io_reader_t reader = {0};
    neverc_bufio_scanner_init(NULL, reader);
    size_t len = 99;
    check_int("nil scanner bytes", neverc_bufio_scanner_bytes(
                  NULL, &len) == NULL, 1);
    check_size("nil scanner bytes length", len, 0);
    check_int("nil scanner bytes length output",
              neverc_bufio_scanner_bytes(NULL, NULL) == NULL, 1);
    check_int("nil scanner text",
              neverc_bufio_scanner_text(NULL) == NULL, 1);
    check_int("nil scanner error", neverc_bufio_scanner_err(NULL),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_scanner_free(NULL);

    neverc_bufio_reader_init_size(NULL, reader, 8);
    uint8_t byte = 0;
    check_int("nil buffered reader byte",
              neverc_bufio_reader_read_byte(NULL, &byte),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_reader_t br = {0};
    check_int("nil buffered reader byte output",
              neverc_bufio_reader_read_byte(&br, NULL),
              NEVERC_IO_ERR_UNEXP);
    size_t n = 99;
    check_int("nil buffered reader",
              neverc_bufio_reader_read(NULL, &byte, 1, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("nil buffered reader count", n, 0);
    check_int("nil buffered reader count output",
              neverc_bufio_reader_read(&br, &byte, 1, NULL),
              NEVERC_IO_ERR_UNEXP);
    check_int("nil read line length output",
              neverc_bufio_reader_read_line(&br, NULL) == NULL, 1);
    check_int("nil read line reader",
              neverc_bufio_reader_read_line(NULL, &len) == NULL, 1);
    check_size("nil read line length", len, 0);
    check_int("nil peek reader",
              neverc_bufio_reader_peek(NULL, &byte, 1),
              NEVERC_IO_ERR_UNEXP);
    check_int("nil peek output",
              neverc_bufio_reader_peek(&br, NULL, 1),
              NEVERC_IO_ERR_UNEXP);
    check_int("nil unread",
              neverc_bufio_reader_unread_byte(NULL),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_scanner_split(NULL, neverc_bufio_scan_words);
    neverc_bufio_reader_free(NULL);

    neverc_io_writer_t writer = {0};
    neverc_bufio_writer_init_size(NULL, writer, 8);
    check_int("nil buffered writer flush",
              neverc_bufio_writer_flush(NULL), NEVERC_IO_ERR_UNEXP);
    n = 99;
    check_int("nil buffered writer",
              neverc_bufio_writer_write(NULL, &byte, 1, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("nil buffered writer count", n, 0);
    neverc_bufio_writer_t bw = {0};
    check_int("nil buffered writer count output",
              neverc_bufio_writer_write(&bw, &byte, 1, NULL),
              NEVERC_IO_ERR_UNEXP);
    check_int("nil buffered writer byte",
              neverc_bufio_writer_write_byte(NULL, byte),
              NEVERC_IO_ERR_UNEXP);
    neverc_bufio_writer_free(NULL);
}

static void test_zero_length_read_eof(void) {
    printf("[zero-length read eof]\n");
    const char *data = "z";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, 1);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };
    neverc_bufio_reader_t br;
    neverc_bufio_reader_init_size(&br, r, 8);

    uint8_t byte = 0;
    size_t n = 99;
    check_int("read one byte", neverc_bufio_reader_read(&br, &byte, 1, &n), 0);
    check_size("read one byte count", n, 1);

    n = 99;
    check_int("next read hits EOF",
              neverc_bufio_reader_read(&br, &byte, 1, &n), NEVERC_IO_EOF);
    check_size("eof read count", n, 0);
    n = 99;
    check_int("drained zero-length is EOF",
              neverc_bufio_reader_read(&br, NULL, 0, &n), NEVERC_IO_EOF);
    check_size("drained zero-length count", n, 0);
    neverc_bufio_reader_free(&br);

    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, 1);
    neverc_bufio_reader_init_size(&br, r, 8);
    uint8_t peek[3];
    check_int("peek leftover", neverc_bufio_reader_peek(&br, peek, 1), 1);
    n = 99;
    check_int("buffered zero-length is success",
              neverc_bufio_reader_read(&br, NULL, 0, &n), 0);
    check_size("buffered zero-length count", n, 0);
    neverc_bufio_reader_free(&br);
}

int main(void) {
    printf("=== NeverC Bufio Module Tests ===\n\n");
    test_scanner();
    test_scanner_no_trailing_newline();
    test_scanner_crlf();
    test_scanner_empty_lines();
    test_scanner_full_buffer_with_eof();
    test_scanner_data_with_terminal_error();
    test_buffered_reader();
    test_buffered_reader_short_read();
    test_buffered_reader_preserves_terminal_error();
    test_buffered_reader_readline();
    test_buffered_writer();
    test_buffered_writer_default_size();
    test_partial_write_error();
    test_partial_write_without_error();
    test_zero_size_buffers();
    test_reader_no_progress();
    test_reader_transient_no_progress();
    test_reader_peek_larger_than_buffer();
    test_reader_rejects_invalid_count();
    test_scanner_missing_reader();
    test_scanner_token_too_long();
    test_scanner_split_empty_contract();
    test_scanner_split_func();
    test_peek_after_unread();
    test_invalid_arguments();
    test_zero_length_read_eof();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
