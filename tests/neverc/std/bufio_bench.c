/*
 * bufio_bench.c — A/B benchmark for the bufio.Scanner refill scan.
 *
 * Old behaviour rescanned the entire [start, buf_len) window with a byte loop
 * after every refill: a long line arriving in small chunks costs O(L^2/chunk).
 * New behaviour tracks the scan frontier (each byte examined once) and uses
 * memchr (SIMD) → O(L).
 *
 * Both scanners are reproduced here over an identical chunked mock reader so the
 * measurement isolates the scan-loop change.
 *
 * Build (from repo root):
 *   build-neverc/bin/neverc -Istd/include -O2 -fno-builtin-std \
 *     -o /tmp/bufio_bench tests/neverc/std/bufio_bench.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Mock reader: hands out `chunk` bytes per call from a fixed blob. */
typedef struct { const uint8_t *data; size_t len, pos, chunk; } reader_t;
static size_t rd(reader_t *r, uint8_t *dst, size_t cap) {
    size_t want = r->len - r->pos;
    if (want > r->chunk) want = r->chunk;
    if (want > cap) want = cap;
    memcpy(dst, r->data + r->pos, want);
    r->pos += want;
    return want;
}

/* ---- OLD: rescans [start, buf_len) each refill, byte-at-a-time ---- */
__attribute__((noinline))
static size_t scan_old(reader_t *r) {
    size_t cap = 4096, buf_len = 0, start = 0, lines = 0;
    uint8_t *buf = malloc(cap);
    int done = 0;
    while (!done) {
        int found = 0;
        for (;;) {
            size_t i;
            for (i = start; i < buf_len; i++) {
                if (buf[i] == '\n') { lines++; start = i + 1; found = 1; break; }
            }
            if (found) break;
            if (start > 0 && buf_len > start) {
                size_t rem = buf_len - start;
                for (size_t k = 0; k < rem; k++) buf[k] = buf[start + k];
                buf_len = rem; start = 0;
            } else if (start > 0) { buf_len = 0; start = 0; }
            if (buf_len >= cap) { cap *= 2; buf = realloc(buf, cap); }
            size_t nr = rd(r, buf + buf_len, cap - buf_len);
            buf_len += nr;
            if (nr == 0) { done = 1; break; }
        }
    }
    free(buf);
    return lines;
}

/* ---- NEW: scan frontier + memchr ---- */
__attribute__((noinline))
static size_t scan_new(reader_t *r) {
    size_t cap = 4096, buf_len = 0, start = 0, lines = 0;
    uint8_t *buf = malloc(cap);
    int done = 0;
    while (!done) {
        size_t scan_pos = start;
        for (;;) {
            if (scan_pos < buf_len) {
                const uint8_t *nl = memchr(buf + scan_pos, '\n', buf_len - scan_pos);
                if (nl) { lines++; start = (size_t)(nl - buf) + 1; break; }
                scan_pos = buf_len;
            }
            if (start > 0) {
                size_t rem = buf_len - start;
                if (rem > 0) memmove(buf, buf + start, rem);
                buf_len = rem; scan_pos = rem; start = 0;
            }
            if (buf_len >= cap) { cap *= 2; buf = realloc(buf, cap); }
            size_t nr = rd(r, buf + buf_len, cap - buf_len);
            buf_len += nr;
            if (nr == 0) { done = 1; break; }
        }
    }
    free(buf);
    return lines;
}

int main(void) {
    printf("=== bufio.Scanner refill scan: old (rescan, O(L^2/chunk)) vs new (frontier+memchr) ===\n");
    printf("%-28s  %12s  %12s  %8s  %s\n", "input", "old (ms)", "new (ms)", "speedup", "match");

    struct { const char *name; size_t total; size_t line; size_t chunk; } cases[] = {
        {"1 line of 4 MB, 4KB reads",  4u<<20, 4u<<20, 4096},
        {"few long lines, 1KB reads",  4u<<20, 256u<<10, 1024},
        {"many short lines, 64KB rd",  8u<<20, 100, 64u<<10},
    };

    for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
        size_t total = cases[c].total, line = cases[c].line, chunk = cases[c].chunk;
        uint8_t *blob = malloc(total);
        memset(blob, 'x', total);
        for (size_t i = line - 1; i < total; i += line) blob[i] = '\n';

        reader_t r1 = {blob, total, 0, chunk};
        reader_t r2 = {blob, total, 0, chunk};
        double t0 = now_ms(); size_t a = scan_old(&r1);
        double t1 = now_ms(); size_t b = scan_new(&r2);
        double t2 = now_ms();
        printf("%-28s  %12.2f  %12.2f  %7.2fx  %s\n", cases[c].name,
               t1 - t0, t2 - t1,
               (t2 - t1) > 0 ? (t1 - t0)/(t2 - t1) : 0.0,
               a == b ? "OK" : "MISMATCH");
        free(blob);
    }
    printf("\nBenchmark complete.\n");
    return 0;
}
