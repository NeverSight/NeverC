/*
 * A/B benchmark + correctness check: mime/quotedprintable decode.
 *
 *  - old_decode — the previous library decoder, reproduced verbatim: it walks
 *      one byte at a time, copying each literal byte individually with a
 *      per-byte capacity check.
 *
 *  - neverc_qp_decode (library) — the new decoder: the '=' escape handling is
 *      unchanged, but runs of literal bytes are located with memchr and copied
 *      with a single memcpy. QP-encoded text is mostly literal, so this is the
 *      hot path.
 *
 * The fast path is behavior-preserving, so every case asserts the new output is
 * byte-for-byte identical to the old output before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/qp_bench \
 *      tests/neverc/std/qp_bench.c std/src/mime/quotedprintable/quotedprintable.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/mime/quotedprintable.h"

/* ============================================================
 * OLD decoder — verbatim reproduction of the previous library
 * ============================================================ */
static int o_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int old_decode(const char *src, size_t src_len,
                      unsigned char *out, size_t out_cap) {
    if (!src || !out) return -1;
    size_t si = 0, di = 0;

    while (si < src_len) {
        if (src[si] == '=') {
            if (si + 2 >= src_len) return -1;
            if (src[si+1] == '\r' && si + 2 < src_len && src[si+2] == '\n') {
                si += 3; continue;
            }
            if (src[si+1] == '\n') {
                si += 2; continue;
            }
            int hi = o_hex_digit(src[si+1]);
            int lo = o_hex_digit(src[si+2]);
            if (hi < 0 || lo < 0) return -1;
            if (di >= out_cap) return -1;
            out[di++] = (unsigned char)((hi << 4) | lo);
            si += 3;
        } else {
            if (di >= out_cap) return -1;
            out[di++] = (unsigned char)src[si++];
        }
    }
    return (int)di;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

static char *make_repeat(const char *pattern, size_t target_len, size_t *out_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    if (out_len) *out_len = i;
    return s;
}

static void bench_case(const char *label, const char *input, size_t in_len) {
    static unsigned char ob[1 << 20], nb[1 << 20];
    int o = old_decode(input, in_len, ob, sizeof(ob));
    int n = neverc_qp_decode(input, in_len, nb, sizeof(nb));
    if (o != n || o < 0 || memcmp(ob, nb, (size_t)o) != 0) {
        printf("%-18s  CORRECTNESS FAIL (old=%d new=%d)\n", label, o, n);
        return;
    }

    int iters = (int)(400000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_decode(input, in_len, ob, sizeof(ob)); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = neverc_qp_decode(input, in_len, nb, sizeof(nb)); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %d B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, n);
}

static void correctness_extra(void) {
    const char *cases[] = {
        "", "=", "==", "=2", "=2D", "a=2Db", "Hello=20World",
        "=48=65=6C=6C=6F", "line1\r\nline2", "Hello=\r\n World",
        "abc=\ndef", "no escapes here at all", "trailing=", "=XYbad",
        "tab\tand space then =3D end",
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int ok = 0;
    unsigned char ob[256], nb[256];
    for (int i = 0; i < n; i++) {
        int o = old_decode(cases[i], strlen(cases[i]), ob, sizeof(ob));
        int nw = neverc_qp_decode(cases[i], strlen(cases[i]), nb, sizeof(nb));
        int same = (o == nw) && (o < 0 || memcmp(ob, nb, (size_t)o) == 0);
        if (same) ok++;
        else printf("  EDGE FAIL [%d]: old=%d new=%d in=\"%s\"\n", i, o, nw, cases[i]);
    }
    /* capacity-limit parity: force the error path at various out_cap values */
    const char *cap_in = "Hello=20World and some more literal text here";
    for (size_t cap = 0; cap <= 24; cap++) {
        unsigned char a[64], b[64];
        int o = old_decode(cap_in, strlen(cap_in), a, cap);
        int nw = neverc_qp_decode(cap_in, strlen(cap_in), b, cap);
        int same = (o == nw) && (o < 0 || memcmp(a, b, (size_t)o) == 0);
        if (same) ok++; else printf("  CAP FAIL cap=%zu: old=%d new=%d\n", cap, o, nw);
    }
    printf("edge cases: %d/%d identical\n", ok, n + 25);
}

int main(void) {
    printf("=== qp_decode: memchr literal-run bulk-copy (new) vs per-byte (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    size_t l1, l2, l3;
    char *plain = make_repeat("The quick brown fox jumps over the lazy dog. ", 8192, &l1);
    bench_case("all_literal", plain, l1);

    char *sparse = make_repeat("Total is 50% off today =3D big sale, come visit. ", 8192, &l2);
    bench_case("literal_sparse", sparse, l2);

    char *heavy = make_repeat("=E4=B8=96=E7=95=8C", 8192, &l3);
    bench_case("escape_heavy", heavy, l3);

    free(plain); free(sparse); free(heavy);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
