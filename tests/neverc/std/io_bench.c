/*
 * A/B benchmark + correctness check: io memory reader/writer copy paths.
 *
 *  - old_* — the previous library code, reproduced verbatim: the in-memory
 *      reader/writer copied payloads one byte at a time in a scalar loop.
 *
 *  - neverc_io_mem_*_read/write (library) — now use memcpy (and memmove for the
 *      overlapping pipe compaction), letting the C library's vectorized copy
 *      run instead of a scalar byte loop.
 *
 * NOTE: at -O2 a compiler may already turn the simplest byte loops into memcpy
 * via loop-idiom recognition, so this bench reports the honest measured delta
 * for THIS compiler; the change additionally guarantees the fast path across
 * compilers/opt-levels and uses memmove where the ranges legitimately overlap.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/io_bench \
 *      tests/neverc/std/io_bench.c std/src/io/io.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/io.h"

/* ============================================================
 * OLD implementations — verbatim reproduction (scalar byte loop)
 * ============================================================ */
static int old_mem_writer_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    neverc_io_mem_writer_t *mw = (neverc_io_mem_writer_t *)ctx;
    while (mw->len + len > mw->cap) {
        mw->cap *= 2;
        mw->data = (uint8_t *)realloc(mw->data, mw->cap);
    }
    for (size_t i = 0; i < len; i++) mw->data[mw->len + i] = buf[i];
    mw->len += len;
    *n = len;
    return 0;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

static void bench_writer(size_t S) {
    uint8_t *src = (uint8_t *)malloc(S ? S : 1);
    for (size_t i = 0; i < S; i++) src[i] = (uint8_t)(i * 2654435761u >> 13);

    neverc_io_mem_writer_t mo, mn;
    neverc_io_mem_writer_init(&mo);
    neverc_io_mem_writer_init(&mn);
    /* pre-grow both so the timed loop only measures the copy, not realloc */
    mo.cap = mn.cap = S + 64;
    mo.data = (uint8_t *)realloc(mo.data, mo.cap);
    mn.data = (uint8_t *)realloc(mn.data, mn.cap);

    /* correctness */
    size_t no = 0, nn = 0;
    mo.len = 0; old_mem_writer_write(&mo, src, S, &no);
    mn.len = 0; neverc_io_mem_writer_write(&mn, src, S, &nn);
    int ok = (no == nn) && (memcmp(mo.data, mn.data, S) == 0);

#ifndef BSCALE
#define BSCALE 1
#endif
    int iters = (int)(600000000u / (S + 1)) / BSCALE; if (iters < 1) iters = 1;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { mo.len = 0; old_mem_writer_write(&mo, src, S, &no); sink = mo.len; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { mn.len = 0; neverc_io_mem_writer_write(&mn, src, S, &nn); sink = mn.len; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("mem_writer S=%-6zu  %8.1f ms  %8.1f ms  %6.2fx   %s\n",
           S, t_old * 1000, t_new * 1000, t_old / t_new, ok ? "" : "CORRECTNESS FAIL");

    neverc_io_mem_writer_free(&mo);
    neverc_io_mem_writer_free(&mn);
    free(src);
}

/* Functional check of the pipe path (memmove compaction on partial reads). */
static int check_pipe(void) {
    neverc_io_pipe_t p;
    neverc_io_reader_t r;
    neverc_io_writer_t w;
    neverc_io_pipe(&p, &r, &w);

    uint8_t msg[200];
    for (int i = 0; i < 200; i++) msg[i] = (uint8_t)(i * 7 + 1);
    size_t nw = 0;
    w.write(w.ctx, msg, sizeof msg, &nw);

    /* drain in small partial reads, forcing repeated overlapping compaction */
    uint8_t out[200]; size_t total = 0;
    for (;;) {
        size_t got = 0;
        int rc = r.read(r.ctx, out + total, 7, &got);
        total += got;
        if (got == 0 && (rc == NEVERC_IO_EOF || rc == 0)) break;
        if (total >= sizeof out) break;
    }
    int ok = (total == sizeof msg) && (memcmp(out, msg, sizeof msg) == 0);
    neverc_io_pipe_free(&p);
    return ok;
}

int main(void) {
    printf("=== io memcpy/memmove (new) vs scalar byte loop (old) ===\n\n");

    printf("pipe partial-read compaction (memmove): %s\n\n",
           check_pipe() ? "OK" : "FAIL");

    printf("(mem_reader kept as a scalar loop: its const source auto-vectorizes and\n");
    printf(" beat memcpy for small reads, so only the writer/pipe paths changed.)\n\n");

    printf("%-20s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    size_t sizes[] = { 16, 64, 256, 1024, 4096, 16384, 65536 };
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) bench_writer(sizes[i]);

    printf("\n=== Done ===\n");
    return 0;
}
