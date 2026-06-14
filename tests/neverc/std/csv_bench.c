/*
 * A/B benchmark + correctness check: encoding/csv write_record field quoting.
 *
 *  - old_write_record — the previous library routine, reproduced verbatim: a
 *      quoted field is emitted one byte at a time through a loop that bounds-
 *      checks every single byte and branches on '"'.
 *
 *  - neverc_csv_write_record (library) — the new routine: a quoted field copies
 *      the run up to each '"' with memchr + memcpy and only special-cases the
 *      doubled quote, so quote-free stretches (the common case) move in bulk.
 *
 * The quoting rules are unchanged, so every case asserts the new output is
 * byte-for-byte identical to the old output before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/csv_bench \
 *      tests/neverc/std/csv_bench.c std/src/encoding/csv/csv.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/encoding/csv.h"

/* ============================================================
 * OLD routine — verbatim reproduction of the previous library
 * ============================================================ */
static int old_needs_quoting(const char *s, char delim, int use_crlf) {
    (void)use_crlf;
    for (const char *p = s; *p; p++) {
        if (*p == delim || *p == '"' || *p == '\n' || *p == '\r')
            return 1;
    }
    return 0;
}

static int old_write_record(const char **fields, int nfields,
                            char *dst, size_t dst_len,
                            const neverc_csv_writer_opts_t *opts) {
    char delim = (opts && opts->delimiter) ? opts->delimiter : ',';
    int crlf = (opts && opts->use_crlf) ? 1 : 0;
    size_t pos = 0;

    for (int i = 0; i < nfields; i++) {
        if (i > 0) {
            if (pos >= dst_len) return -1;
            dst[pos++] = delim;
        }

        const char *f = fields[i];
        if (old_needs_quoting(f, delim, crlf)) {
            if (pos >= dst_len) return -1;
            dst[pos++] = '"';
            for (const char *p = f; *p; p++) {
                if (*p == '"') {
                    if (pos + 1 >= dst_len) return -1;
                    dst[pos++] = '"';
                    dst[pos++] = '"';
                } else {
                    if (pos >= dst_len) return -1;
                    dst[pos++] = *p;
                }
            }
            if (pos >= dst_len) return -1;
            dst[pos++] = '"';
        } else {
            size_t flen = strlen(f);
            if (pos + flen > dst_len) return -1;
            memcpy(dst + pos, f, flen);
            pos += flen;
        }
    }

    if (crlf) {
        if (pos + 2 > dst_len) return -1;
        dst[pos++] = '\r';
        dst[pos++] = '\n';
    } else {
        if (pos + 1 > dst_len) return -1;
        dst[pos++] = '\n';
    }

    return (int)pos;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

typedef int (*wfn)(const char **, int, char *, size_t,
                   const neverc_csv_writer_opts_t *);

static void bench_case(const char *label, const char *field, wfn oldf, wfn newf) {
    const char *fields[1] = { field };
    size_t in_len = strlen(field);
    size_t cap = in_len * 2 + 16;
    char *ob = (char *)malloc(cap), *nb = (char *)malloc(cap);

    int on = oldf(fields, 1, ob, cap, NULL);
    int nn = newf(fields, 1, nb, cap, NULL);
    if (on < 0 || nn < 0 || on != nn || memcmp(ob, nb, (size_t)on) != 0) {
        printf("%-18s  CORRECTNESS FAIL (old=%d new=%d)\n", label, on, nn);
        free(ob); free(nb); return;
    }
    size_t out_len = (size_t)nn;

    int iters = (int)(150000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = oldf(fields, 1, ob, cap, NULL); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = newf(fields, 1, nb, cap, NULL); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, out_len);
    free(ob); free(nb);
}

/* Extra edge-case correctness coverage with multi-field records and options. */
static int rec_eq(const char **fields, int nf, const neverc_csv_writer_opts_t *opts,
                  const char *desc) {
    char ob[512], nb[512];
    int on = old_write_record(fields, nf, ob, sizeof(ob), opts);
    int nn = neverc_csv_write_record(fields, nf, nb, sizeof(nb), opts);
    int ok = (on == nn) && (on < 0 || memcmp(ob, nb, (size_t)on) == 0);
    if (!ok) printf("  EDGE FAIL: %s (old=%d new=%d)\n", desc, on, nn);
    return ok;
}

static void correctness_extra(void) {
    neverc_csv_writer_opts_t crlf = { .delimiter = 0, .use_crlf = 1 };
    neverc_csv_writer_opts_t semi = { .delimiter = ';', .use_crlf = 0 };
    int ok = 0, n = 0;

    const char *r1[] = { "" };                       n++; ok += rec_eq(r1, 1, NULL, "empty");
    const char *r2[] = { "plain" };                  n++; ok += rec_eq(r2, 1, NULL, "plain");
    const char *r3[] = { "a,b" };                    n++; ok += rec_eq(r3, 1, NULL, "comma");
    const char *r4[] = { "\"" };                     n++; ok += rec_eq(r4, 1, NULL, "lone quote");
    const char *r5[] = { "\"\"\"\"" };               n++; ok += rec_eq(r5, 1, NULL, "all quotes");
    const char *r6[] = { "say \"hi\"" };             n++; ok += rec_eq(r6, 1, NULL, "embedded quotes");
    const char *r7[] = { "line1\nline2" };           n++; ok += rec_eq(r7, 1, NULL, "newline");
    const char *r8[] = { "a", "b", "c" };            n++; ok += rec_eq(r8, 3, NULL, "three plain");
    const char *r9[] = { "x,y", "z\"w", "plain" };   n++; ok += rec_eq(r9, 3, NULL, "mixed");
    const char *r10[] = { "a,b", "c" };              n++; ok += rec_eq(r10, 2, &crlf, "crlf");
    const char *r11[] = { "a;b", "c,d" };            n++; ok += rec_eq(r11, 2, &semi, "semicolon delim");
    const char *r12[] = { "\"start", "end\"" };      n++; ok += rec_eq(r12, 2, NULL, "quote at edges");

    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    printf("=== csv write_record: memchr quote bulk-copy (new) vs per-byte (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    /* Needs quoting (has commas) but contains no '"': one big bulk-copy run. */
    char *clean = make_repeat("The quick, brown fox, jumps over. ", 4096);
    bench_case("quoted_clean", clean, old_write_record, neverc_csv_write_record);

    /* Sparse embedded quotes: short runs separated by doubled quotes. */
    char *sparse = make_repeat("He said \"hi\" to her, then left. ", 4096);
    bench_case("quoted_sparse", sparse, old_write_record, neverc_csv_write_record);

    /* Quote-dense worst case: little to bulk-copy. */
    char *dense = make_repeat("\"a\",\"b\",", 4096);
    bench_case("quoted_dense", dense, old_write_record, neverc_csv_write_record);

    /* No quoting needed at all: exercises the unchanged memcpy fast path. */
    char *plain = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096);
    bench_case("plain", plain, old_write_record, neverc_csv_write_record);

    free(clean); free(sparse); free(dense); free(plain);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
