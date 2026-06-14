/*
 * A/B benchmark + correctness check: mime quoted-printable decode.
 *
 *  - old_qp_decode — the previous library decoder, reproduced verbatim: a
 *      per-byte loop that branches on every input byte (testing for '=' and
 *      hex/soft-break handling) and stores one output byte at a time.
 *
 *  - neverc_mime_qp_decode (library) — the new decoder: memchr finds the next
 *      '=' and memcpy copies the literal run in bulk, so the common literal
 *      stretches move at memory speed; only the actual '=' bytes (escape, soft
 *      break, or trailing literal) take the byte-at-a-time path.
 *
 * The decoding rules are unchanged, so every case asserts the new output is
 * byte-for-byte identical to the old output (same return value, same out_len,
 * same bytes) — including under a tight output cap — before timing.
 *
 * (The QP encoder is intentionally not changed: its "needs escaping" boundary
 * is a multi-byte class that cannot be scanned with a single SIMD memchr, so a
 * bulk-copy rewrite regresses escape-dense text. Only decode is optimized.)
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/mime_qp_bench \
 *      tests/neverc/std/mime_qp_bench.c std/src/mime/mime.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/mime.h"

/* ============================================================
 * OLD decoder — verbatim reproduction of the previous library
 * ============================================================ */
static int o_hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int old_qp_decode(const char *src, size_t src_len,
                         char *dst, size_t dst_cap, size_t *out_len) {
    size_t si = 0, di = 0;
    while (si < src_len && di < dst_cap) {
        if (src[si] == '=' && si + 2 < src_len) {
            if (src[si + 1] == '\r' && si + 3 <= src_len && src[si + 2] == '\n') {
                si += 3;
            } else if (src[si + 1] == '\n') {
                si += 2;
            } else {
                int h = o_hex_val(src[si + 1]);
                int l = o_hex_val(src[si + 2]);
                if (h >= 0 && l >= 0) {
                    dst[di++] = (char)((h << 4) | l);
                    si += 3;
                } else {
                    dst[di++] = src[si++];
                }
            }
        } else {
            dst[di++] = src[si++];
        }
    }
    if (out_len) *out_len = di;
    return 0;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

/* Build a ~target_len byte buffer by repeating `pattern` (plen bytes). */
static unsigned char *make_repeat(const unsigned char *pattern, size_t plen,
                                  size_t target_len, size_t *out_len) {
    unsigned char *s = (unsigned char *)malloc(target_len + plen);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    *out_len = i;
    return s;
}

typedef int (*qp_fn)(const char *, size_t, char *, size_t, size_t *);

static void bench_case(const char *label, const unsigned char *in, size_t in_len,
                       size_t out_cap, qp_fn old_fn, qp_fn new_fn) {
    char *ob = (char *)malloc(out_cap), *nb = (char *)malloc(out_cap);
    size_t on = 0, nn = 0;
    old_fn((const char *)in, in_len, ob, out_cap, &on);
    new_fn((const char *)in, in_len, nb, out_cap, &nn);
    if (on != nn || memcmp(ob, nb, on) != 0) {
        printf("%-18s  CORRECTNESS FAIL (old=%zu new=%zu)\n", label, on, nn);
        free(ob); free(nb); return;
    }

    int iters = (int)(150000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        size_t tmp;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_fn((const char *)in, in_len, ob, out_cap, &tmp); sink = (int)tmp; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { new_fn((const char *)in, in_len, nb, out_cap, &tmp); sink = (int)tmp; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, nn);
    free(ob); free(nb);
}

/* ============================================================
 * Edge-case correctness: old vs new must agree exactly (return,
 * out_len, and bytes) across a range of output caps.
 * ============================================================ */
static int qp_eq(const unsigned char *in, size_t in_len, size_t cap, const char *desc) {
    char ob[1024], nb[1024];
    memset(ob, 0xAA, sizeof(ob));
    memset(nb, 0xBB, sizeof(nb));
    size_t on = 12345, nn = 54321;
    int ro = old_qp_decode((const char *)in, in_len, ob, cap, &on);
    int rn = neverc_mime_qp_decode((const char *)in, in_len, nb, cap, &nn);
    int ok = (ro == rn) && (on == nn) && memcmp(ob, nb, on) == 0;
    if (!ok) printf("  EDGE FAIL: %s (cap=%zu old=%zu new=%zu)\n", desc, cap, on, nn);
    return ok;
}

static void correctness_extra(void) {
    static const char *dec_in[] = {
        "",
        "plain text no escapes",
        "Caf=C3=A9 =E2=98=83 r=C3=A9sum=C3=A9",
        "soft=\r\nbreak and soft=\nlf join",
        "trailing equals at end=",
        "=3D=3D=3D=3D",
        "=GZ invalid hex stays",
        "a=",                /* '=' with no room for two more chars */
        "==",                /* adjacent equals */
        "mix =41=42 normal =XY bad =\r\n end",
    };
    size_t caps[] = { 1024, 16, 8, 5, 4, 3, 2, 1 };
    int ok = 0, n = 0;
    for (size_t i = 0; i < sizeof(dec_in) / sizeof(dec_in[0]); i++)
        for (size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); c++) {
            n++; ok += qp_eq((const unsigned char *)dec_in[i], strlen(dec_in[i]),
                             caps[c], dec_in[i]);
        }
    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    size_t len;

    printf("=== mime qp_decode: memchr bulk-copy (new) vs per-byte (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    /* Literal-heavy: no '=' at all, one big bulk-copy run. */
    unsigned char *d_lit = make_repeat((const unsigned char *)"The quick brown fox jumps over the lazy dog. ", 45, 8192, &len);
    bench_case("dec_literal", d_lit, len, len + 16, old_qp_decode, neverc_mime_qp_decode);
    free(d_lit);

    /* Typical: accented UTF-8 text with sparse =XX escapes (short runs). */
    unsigned char *d_typ = make_repeat((const unsigned char *)"Caf=C3=A9 r=C3=A9sum=C3=A9 na=C3=AFve text here. ", 48, 8192, &len);
    bench_case("dec_typical", d_typ, len, len + 16, old_qp_decode, neverc_mime_qp_decode);
    free(d_typ);

    /* Escape-dense worst case: almost nothing to bulk-copy. */
    unsigned char *d_hvy = make_repeat((const unsigned char *)"=20=21=22=23=24=25=26=27", 24, 8192, &len);
    bench_case("dec_heavy", d_hvy, len, len + 16, old_qp_decode, neverc_mime_qp_decode);
    free(d_hvy);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
