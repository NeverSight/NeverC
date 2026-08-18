#include "neverc/std/io.h"
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

static void test_read_all(void) {
    printf("[read_all]\n");
    const char *data = "Hello, NeverC World!";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    size_t outlen;
    uint8_t *result = neverc_io_read_all(&r, &outlen);
    check_bytes("read_all content", result, outlen, data);
    free(result);
}

static void test_read_full(void) {
    printf("[read_full]\n");
    const char *data = "abcdefgh";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    uint8_t buf[4];
    int err = neverc_io_read_full(&r, buf, 4);
    check_int("read_full ok", err, 0);
    check_bytes("read_full content", buf, 4, "abcd");

    err = neverc_io_read_full(&r, buf, 4);
    check_int("read_full ok 2", err, 0);
    check_bytes("read_full content 2", buf, 4, "efgh");

    err = neverc_io_read_full(&r, buf, 4);
    check_int("read_full eof", err, NEVERC_IO_ERR_UNEXP);
}

static void test_copy(void) {
    printf("[copy]\n");
    const char *data = "copy this data across";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };

    int64_t n = neverc_io_copy(&w, &r);
    check_size("copy len", (size_t)n, strlen(data));
    check_bytes("copy content", mw.data, mw.len, data);
    neverc_io_mem_writer_free(&mw);
}

static void test_copy_n(void) {
    printf("[copy_n]\n");
    const char *data = "hello world 12345";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, strlen(data));
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };

    int64_t n = neverc_io_copy_n(&w, &r, 5);
    check_size("copy_n len", (size_t)n, 5);
    check_bytes("copy_n content", mw.data, mw.len, "hello");
    check_size("copy_n zero", (size_t)neverc_io_copy_n(&w, &r, 0), 0);
    check_size("copy_n negative", (size_t)neverc_io_copy_n(&w, &r, -3), 0);
    neverc_io_mem_writer_free(&mw);
}

static void test_write_string(void) {
    printf("[write_string]\n");
    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };

    size_t n;
    neverc_io_write_string(&w, "hello", &n);
    check_size("write_string n", n, 5);
    neverc_io_write_string(&w, " world", &n);
    check_bytes("write_string content", mw.data, mw.len, "hello world");
    neverc_io_mem_writer_free(&mw);
}

static void test_discard(void) {
    printf("[discard]\n");
    neverc_io_writer_t w;
    neverc_io_discard_init(&w);

    size_t n;
    uint8_t data[] = "throw this away";
    int err = w.write(w.ctx, data, sizeof(data) - 1, &n);
    check_int("discard err", err, 0);
    check_size("discard n", n, sizeof(data) - 1);
}

static void test_multi_reader(void) {
    printf("[multi_reader]\n");
    neverc_io_mem_reader_t mr1, mr2;
    neverc_io_mem_reader_init(&mr1, (const uint8_t *)"hello ", 6);
    neverc_io_mem_reader_init(&mr2, (const uint8_t *)"world", 5);

    neverc_io_reader_t readers[2] = {
        { &mr1, neverc_io_mem_reader_read },
        { &mr2, neverc_io_mem_reader_read }
    };

    neverc_io_multi_reader_t multi;
    neverc_io_multi_reader_init(&multi, readers, 2);

    size_t olen;
    uint8_t *data = neverc_io_read_all(&multi.reader, &olen);
    check_size("multi_reader len", olen, 11);
    check_int("multi_reader content", memcmp(data, "hello world", 11) == 0, 1);
    free(data);
}

static void test_multi_writer(void) {
    printf("[multi_writer]\n");
    neverc_io_mem_writer_t mw1, mw2;
    neverc_io_mem_writer_init(&mw1);
    neverc_io_mem_writer_init(&mw2);

    neverc_io_writer_t writers[2] = {
        { &mw1, neverc_io_mem_writer_write },
        { &mw2, neverc_io_mem_writer_write }
    };

    neverc_io_multi_writer_t multi;
    neverc_io_multi_writer_init(&multi, writers, 2);

    size_t n;
    multi.writer.write(multi.writer.ctx, (const uint8_t *)"test", 4, &n);
    check_size("multi_writer n", n, 4);
    check_size("multi_writer w1 len", mw1.len, 4);
    check_size("multi_writer w2 len", mw2.len, 4);
    check_int("multi_writer w1 data", memcmp(mw1.data, "test", 4) == 0, 1);
    check_int("multi_writer w2 data", memcmp(mw2.data, "test", 4) == 0, 1);
    neverc_io_mem_writer_free(&mw1);
    neverc_io_mem_writer_free(&mw2);
}

static void test_limit_reader_and_named_multi_write(void) {
    printf("[limit_reader / named multi_writer_write]\n");
    const char *data = "abcdef";
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)data, 6);
    neverc_io_reader_t inner = { &mr, neverc_io_mem_reader_read };

    neverc_io_limit_reader_t lr;
    neverc_io_limit_reader_init(&lr, &inner, 3);
    uint8_t buf[8];
    size_t n = 0;
    check_int("limit read rc",
              lr.reader.read(lr.reader.ctx, buf, sizeof(buf), &n), 0);
    check_size("limit read n", n, 3);
    check_int("limit data", memcmp(buf, "abc", 3) == 0, 1);
    n = 99;
    check_int("limit eof",
              lr.reader.read(lr.reader.ctx, buf, sizeof(buf), &n),
              NEVERC_IO_EOF);

    neverc_io_mem_writer_t mw1, mw2;
    neverc_io_mem_writer_init(&mw1);
    neverc_io_mem_writer_init(&mw2);
    neverc_io_writer_t writers[2] = {
        { &mw1, neverc_io_mem_writer_write },
        { &mw2, neverc_io_mem_writer_write }
    };
    neverc_io_multi_writer_t multi;
    neverc_io_multi_writer_init(&multi, writers, 2);
    n = 0;
    check_int("named multi write",
              neverc_io_multi_writer_write(&multi, (const uint8_t *)"xy", 2, &n),
              0);
    check_size("named multi n", n, 2);
    check_int("named multi w1", memcmp(mw1.data, "xy", 2) == 0, 1);
    check_int("named multi w2", memcmp(mw2.data, "xy", 2) == 0, 1);
    neverc_io_mem_writer_free(&mw1);
    neverc_io_mem_writer_free(&mw2);
}

static void test_pipe(void) {
    printf("[pipe]\n");
    neverc_io_pipe_t pipe_ctx;
    neverc_io_reader_t pr;
    neverc_io_writer_t pw;
    neverc_io_pipe(&pipe_ctx, &pr, &pw);

    size_t n;
    pw.write(pw.ctx, (const uint8_t *)"hello pipe", 10, &n);
    check_size("pipe write n", n, 10);

    uint8_t buf[32];
    size_t got;
    pr.read(pr.ctx, buf, sizeof(buf), &got);
    check_size("pipe read n", got, 10);
    check_int("pipe read data", memcmp(buf, "hello pipe", 10) == 0, 1);

    neverc_io_pipe_close(&pipe_ctx);
    int rc = pr.read(pr.ctx, buf, sizeof(buf), &got);
    check_int("pipe eof", rc, NEVERC_IO_EOF);

    neverc_io_pipe_free(&pipe_ctx);
}

static void test_nop_closer(void) {
    printf("[nop_closer]\n");
    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)"data", 4);
    neverc_io_reader_t r = { &mr, neverc_io_mem_reader_read };

    neverc_io_nop_closer_t nc;
    neverc_io_nop_closer_init(&nc, &r);

    int rc = nc.closer.close(nc.closer.ctx);
    check_int("nop_closer close", rc, 0);
}

static int no_progress_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    (void)ctx;
    (void)buf;
    (void)len;
    *n = 0;
    return 0;
}

typedef struct {
    int calls;
} delayed_reader_t;

static int delayed_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    delayed_reader_t *reader = (delayed_reader_t *)ctx;
    reader->calls++;
    *n = 0;
    if (reader->calls == 1) return 0;
    if (reader->calls == 2 && len > 0) {
        buf[0] = 'x';
        *n = 1;
    }
    return NEVERC_IO_EOF;
}

static void test_no_progress_guards(void) {
    printf("[no progress guards]\n");

    neverc_io_reader_t reader = { NULL, no_progress_read };
    uint8_t byte = 0;
    check_int("read_full rejects no progress",
              neverc_io_read_full(&reader, &byte, 1),
              NEVERC_IO_ERR_UNEXP);

    neverc_io_writer_t discard;
    neverc_io_discard_init(&discard);
    check_size("copy_buffer rejects zero buffer",
               (size_t)neverc_io_copy_buffer(&discard, &reader, &byte, 0), 0);

    delayed_reader_t delayed = {0};
    neverc_io_reader_t delayed_io = { &delayed, delayed_read };
    neverc_io_multi_reader_t multi;
    neverc_io_multi_reader_init(&multi, &delayed_io, 1);
    size_t n = 99;
    check_int("multi_reader preserves temporarily idle reader",
              neverc_io_multi_reader_read(&multi, &byte, 1, &n), 0);
    check_size("multi_reader idle read reports no bytes", n, 0);
    check_int("multi_reader idle read does not advance", multi.current, 0);
    check_int("multi_reader resumes temporarily idle reader",
              neverc_io_multi_reader_read(&multi, &byte, 1, &n), 0);
    check_size("multi_reader resumed byte count", n, 1);
    check_int("multi_reader resumed content", byte, 'x');

    neverc_io_mem_reader_t memory;
    neverc_io_mem_reader_init(&memory, (const uint8_t *)"z", 1);
    neverc_io_reader_t memory_io = { &memory, neverc_io_mem_reader_read };
    neverc_io_multi_reader_init(&multi, &memory_io, 1);
    n = 99;
    check_int("multi_reader zero-length read succeeds",
              neverc_io_multi_reader_read(&multi, NULL, 0, &n), 0);
    check_size("multi_reader zero-length read reports no bytes", n, 0);
    check_int("multi_reader zero-length read does not advance", multi.current, 0);
    check_int("multi_reader data remains after zero-length read",
              neverc_io_multi_reader_read(&multi, &byte, 1, &n), 0);
    check_int("multi_reader retained content", byte, 'z');
    check_int("drained multi_reader zero-length is EOF",
              neverc_io_multi_reader_read(&multi, NULL, 0, &n), NEVERC_IO_EOF);

    neverc_io_multi_reader_init(&multi, NULL, 0);
    check_int("empty multi_reader zero-length is EOF",
              neverc_io_multi_reader_read(&multi, NULL, 0, &n), NEVERC_IO_EOF);

    delayed_reader_t delayed_copy = {0};
    neverc_io_reader_t delayed_src = { &delayed_copy, delayed_read };
    neverc_io_writer_t discard_copy;
    neverc_io_discard_init(&discard_copy);
    check_size("copy retries transient empty read",
               (size_t)neverc_io_copy(&discard_copy, &delayed_src), 1);

    delayed_copy.calls = 0;
    size_t outlen = 0;
    uint8_t *all = neverc_io_read_all(&delayed_src, &outlen);
    check_size("read_all retries transient empty read", outlen, 1);
    check_int("read_all delayed byte", all && all[0] == 'x', 1);
    free(all);

    delayed_copy.calls = 0;
    byte = 0;
    check_int("read_full retries transient empty read",
              neverc_io_read_full(&delayed_src, &byte, 1), 0);
    check_int("read_full delayed byte", byte, 'x');

    delayed_copy.calls = 0;
    check_size("copy_n retries transient empty read",
               (size_t)neverc_io_copy_n(&discard, &delayed_src, 1), 1);

    delayed_copy.calls = 0;
    uint8_t scratch = 0;
    check_size("copy_buffer retries transient empty read",
               (size_t)neverc_io_copy_buffer(&discard, &delayed_src, &scratch, 1),
               1);

    delayed_copy.calls = 0;
    n = 0;
    byte = 0;
    check_int("read_at_least retries transient empty read",
              neverc_io_read_at_least(&delayed_src, &byte, 1, 1, &n), 0);
    check_size("read_at_least delayed count", n, 1);
    check_int("read_at_least delayed byte", byte, 'x');
}

#if SIZE_MAX > INT64_MAX
static int enormous_count_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len;
    return NEVERC_IO_EOF;
}

static int enormous_count_write(void *ctx, const uint8_t *buf, size_t len,
                                size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len;
    return 0;
}

static void test_copy_count_saturation(void) {
    printf("[copy count saturation]\n");

    neverc_io_reader_t reader = { NULL, enormous_count_read };
    neverc_io_writer_t writer = { NULL, enormous_count_write };
    uint8_t scratch = 0;
    check_int("copy_buffer saturates enormous callback count",
              neverc_io_copy_buffer(&writer, &reader, &scratch, SIZE_MAX) ==
                  INT64_MAX,
              1);
}
#else
static void test_copy_count_saturation(void) {}
#endif

static int oversized_count_read(void *ctx, uint8_t *buf, size_t len,
                                size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len + 1;
    return NEVERC_IO_EOF;
}

static int one_byte_eof_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    int *used = (int *)ctx;
    if (*used || len == 0) {
        *n = 0;
        return NEVERC_IO_EOF;
    }
    buf[0] = 'x';
    *used = 1;
    *n = 1;
    return NEVERC_IO_EOF;
}

static int one_byte_error_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    int rc = one_byte_eof_read(ctx, buf, len, n);
    return rc == NEVERC_IO_EOF ? NEVERC_IO_ERR_UNEXP : rc;
}

static void test_invalid_reader_counts(void) {
    printf("[invalid reader counts]\n");

    neverc_io_reader_t reader = { NULL, oversized_count_read };
    size_t len = 99;
    uint8_t *all = neverc_io_read_all(&reader, &len);
    check_int("read_all rejects oversized count", all == NULL, 1);
    check_size("read_all clears oversized count length", len, 0);
    free(all);

    uint8_t byte = 0;
    check_int("read_full rejects oversized count",
              neverc_io_read_full(&reader, &byte, 1),
              NEVERC_IO_ERR_UNEXP);
    size_t n = 99;
    check_int("read_at_least rejects oversized count",
              neverc_io_read_at_least(&reader, &byte, 1, 1, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("read_at_least clears invalid count", n, 0);

    int used = 0;
    neverc_io_reader_t final_reader = { &used, one_byte_eof_read };
    n = 0;
    check_int("read_at_least accepts enough data with eof",
              neverc_io_read_at_least(&final_reader, &byte, 1, 1, &n), 0);
    check_size("read_at_least reports final data", n, 1);

    used = 0;
    neverc_io_reader_t error_reader = { &used, one_byte_error_read };
    check_int("read_full accepts enough data with terminal error",
              neverc_io_read_full(&error_reader, &byte, 1), 0);

    used = 0;
    len = 99;
    all = neverc_io_read_all(&error_reader, &len);
    check_int("read_all rejects terminal error", all == NULL, 1);
    check_size("read_all clears length on error", len, 0);
    free(all);
}

static void test_capacity_overflow_guards(void) {
    printf("[capacity overflow guards]\n");

    neverc_io_mem_writer_t mw = {
        (uint8_t *)malloc(1), SIZE_MAX, SIZE_MAX
    };
    size_t n = 99;
    check_int("memory writer rejects capacity overflow",
              neverc_io_mem_writer_write(&mw, (const uint8_t *)"x", 1, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("memory writer overflow writes nothing", n, 0);
    neverc_io_mem_writer_free(&mw);

    neverc_io_pipe_t pipe_ctx;
    neverc_io_reader_t pipe_reader;
    neverc_io_writer_t pipe_writer;
    neverc_io_pipe(&pipe_ctx, &pipe_reader, &pipe_writer);
    pipe_ctx.buf = (uint8_t *)malloc(1);
    pipe_ctx.len = SIZE_MAX;
    pipe_ctx.cap = SIZE_MAX;
    n = 99;
    check_int("pipe rejects capacity overflow",
              pipe_writer.write(pipe_writer.ctx, (const uint8_t *)"x", 1, &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("pipe overflow writes nothing", n, 0);
    neverc_io_pipe_free(&pipe_ctx);
}

static int chunked_mem_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (len > 2) len = 2;
    return neverc_io_mem_reader_read(ctx, buf, len, n);
}

static int short_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len > 0 ? len - 1 : 0;
    return 0;
}

static int rejecting_write(void *ctx, const uint8_t *buf, size_t len,
                           size_t *n) {
    (void)ctx;
    (void)buf;
    (void)len;
    *n = 0;
    return NEVERC_IO_ERR_UNEXP;
}

typedef struct {
    const uint8_t *data;
    size_t len;
    int used;
} eof_chunk_reader_t;

static int eof_chunk_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    eof_chunk_reader_t *reader = (eof_chunk_reader_t *)ctx;
    if (reader->used) {
        *n = 0;
        return NEVERC_IO_EOF;
    }
    size_t take = reader->len < len ? reader->len : len;
    if (take > 0) memcpy(buf, reader->data, take);
    reader->used = 1;
    *n = take;
    return NEVERC_IO_EOF;
}

static int limit_overreport_read(void *ctx, uint8_t *buf, size_t len,
                                 size_t *n) {
    (void)ctx;
    (void)buf;
    *n = len + 10;
    return 0;
}

static void test_multi_reader_eof_then_next(void) {
    printf("[multi_reader eof then next]\n");

    eof_chunk_reader_t first = { (const uint8_t *)"ab", 2, 0 };
    neverc_io_mem_reader_t second;
    neverc_io_mem_reader_init(&second, (const uint8_t *)"cd", 2);
    neverc_io_reader_t readers[2] = {
        { &first, eof_chunk_read },
        { &second, neverc_io_mem_reader_read }
    };
    neverc_io_multi_reader_t multi;
    neverc_io_multi_reader_init(&multi, readers, 2);

    size_t olen = 0;
    uint8_t *data = neverc_io_read_all(&multi.reader, &olen);
    check_size("multi data+eof then next len", olen, 4);
    check_int("multi data+eof then next content",
              data && memcmp(data, "abcd", 4) == 0, 1);
    free(data);

    neverc_io_mem_reader_t empty;
    neverc_io_mem_reader_init(&empty, (const uint8_t *)"", 0);
    neverc_io_mem_reader_t tail;
    neverc_io_mem_reader_init(&tail, (const uint8_t *)"z", 1);
    neverc_io_reader_t prefix[2] = {
        { &empty, neverc_io_mem_reader_read },
        { &tail, neverc_io_mem_reader_read }
    };
    neverc_io_multi_reader_init(&multi, prefix, 2);
    uint8_t byte = 0;
    size_t n = 99;
    check_int("multi empty prefix then data",
              neverc_io_multi_reader_read(&multi, &byte, 1, &n), 0);
    check_size("multi empty prefix n", n, 1);
    check_int("multi empty prefix byte", byte, 'z');
    n = 99;
    check_int("multi exhausted is eof",
              neverc_io_multi_reader_read(&multi, &byte, 1, &n),
              NEVERC_IO_EOF);
    check_size("multi exhausted n", n, 0);
}

static void test_limit_reader_wrap(void) {
    printf("[limit_reader wrap]\n");

    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)"abcdef", 6);
    neverc_io_reader_t inner = { &mr, neverc_io_mem_reader_read };
    neverc_io_limit_reader_t lr;
    neverc_io_limit_reader_init(&lr, &inner, -7);
    uint8_t buf[8];
    size_t n = 99;
    check_int("negative limit is eof",
              lr.reader.read(lr.reader.ctx, buf, sizeof(buf), &n),
              NEVERC_IO_EOF);
    check_size("negative limit writes nothing", n, 0);
    check_int("negative limit remaining stays non-negative",
              lr.remaining >= 0, 1);

    neverc_io_mem_reader_init(&mr, (const uint8_t *)"abcdef", 6);
    neverc_io_limit_reader_init(&lr, &inner, 3);
    neverc_io_mem_writer_t mw;
    neverc_io_mem_writer_init(&mw);
    neverc_io_writer_t w = { &mw, neverc_io_mem_writer_write };
    check_size("copy honors limit",
               (size_t)neverc_io_copy(&w, &lr.reader), 3);
    check_bytes("copy limit content", mw.data, mw.len, "abc");
    neverc_io_mem_writer_free(&mw);

    neverc_io_reader_t over = { NULL, limit_overreport_read };
    neverc_io_limit_reader_init(&lr, &over, 5);
    n = 99;
    check_int("over-report does not wrap remaining",
              lr.reader.read(lr.reader.ctx, buf, sizeof(buf), &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("over-report n cleared", n, 0);
    check_int("remaining unchanged after over-report", lr.remaining == 5, 1);
}

static void test_tee_reader_success(void) {
    printf("[tee_reader success]\n");

    neverc_io_mem_reader_t mr;
    neverc_io_mem_reader_init(&mr, (const uint8_t *)"hello", 5);
    neverc_io_reader_t inner = { &mr, neverc_io_mem_reader_read };
    neverc_io_mem_writer_t tee_w;
    neverc_io_mem_writer_init(&tee_w);
    neverc_io_writer_t w = { &tee_w, neverc_io_mem_writer_write };
    neverc_io_tee_reader_t tee;
    neverc_io_tee_reader_init(&tee, &inner, &w);

    size_t olen = 0;
    uint8_t *data = neverc_io_read_all(&tee.reader, &olen);
    check_size("tee read len", olen, 5);
    check_int("tee read content", data && memcmp(data, "hello", 5) == 0, 1);
    check_bytes("tee writer content", tee_w.data, tee_w.len, "hello");
    free(data);
    neverc_io_mem_writer_free(&tee_w);

    eof_chunk_reader_t chunk = { (const uint8_t *)"xy", 2, 0 };
    neverc_io_reader_t eof_inner = { &chunk, eof_chunk_read };
    neverc_io_mem_writer_init(&tee_w);
    w.ctx = &tee_w;
    neverc_io_tee_reader_init(&tee, &eof_inner, &w);
    uint8_t buf[4];
    size_t n = 99;
    check_int("tee data+eof rc",
              tee.reader.read(tee.reader.ctx, buf, sizeof(buf), &n),
              NEVERC_IO_EOF);
    check_size("tee data+eof n", n, 2);
    check_int("tee data+eof bytes", memcmp(buf, "xy", 2) == 0, 1);
    check_bytes("tee data+eof writer", tee_w.data, tee_w.len, "xy");
    neverc_io_mem_writer_free(&tee_w);
}

static void test_partial_writer_propagation(void) {
    printf("[partial writer propagation]\n");

    neverc_io_mem_reader_t source;
    neverc_io_mem_reader_init(&source, (const uint8_t *)"abcd", 4);
    neverc_io_reader_t reader = { &source, chunked_mem_read };
    neverc_io_writer_t writer = { NULL, short_write };
    check_size("copy_n stops at first short write",
               (size_t)neverc_io_copy_n(&writer, &reader, 4), 1);
    check_size("copy_n stops consuming after short write", source.pos, 2);

    size_t n = 99;
    check_int("write_string reports short write",
              neverc_io_write_string(&writer, "x", &n), NEVERC_IO_ERR_SHORT);
    check_size("write_string reports accepted bytes", n, 0);

    neverc_io_mem_reader_init(&source, (const uint8_t *)"tail", 4);
    neverc_io_reader_t inner = { &source, neverc_io_mem_reader_read };
    neverc_io_writer_t rejecting = { NULL, rejecting_write };
    neverc_io_tee_reader_t tee;
    neverc_io_tee_reader_init(&tee, &inner, &rejecting);
    uint8_t buf[4];
    n = 99;
    check_int("tee propagates writer error",
              tee.reader.read(tee.reader.ctx, buf, sizeof(buf), &n),
              NEVERC_IO_ERR_UNEXP);
    check_size("tee reports bytes accepted by writer", n, 0);
}

int main(void) {
    printf("=== NeverC IO Module Tests ===\n\n");
    test_read_all();
    test_read_full();
    test_copy();
    test_copy_n();
    test_write_string();
    test_discard();
    test_multi_reader();
    test_multi_writer();
    test_limit_reader_and_named_multi_write();
    test_pipe();
    test_nop_closer();
    test_no_progress_guards();
    test_copy_count_saturation();
    test_capacity_overflow_guards();
    test_invalid_reader_counts();
    test_multi_reader_eof_then_next();
    test_limit_reader_wrap();
    test_tee_reader_success();
    test_partial_writer_propagation();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
