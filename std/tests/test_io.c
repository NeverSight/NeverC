#include "neverc/io.h"
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
    test_pipe();
    test_nop_closer();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
