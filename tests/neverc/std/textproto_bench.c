/*
 * A/B benchmark + correctness check: net/textproto read_line.
 *
 *  - old_read_line — the previous library primitive, reproduced verbatim:
 *      it finds the '\n' terminator with a byte-at-a-time loop.
 *
 *  - neverc_textproto_read_line (library) — the new primitive: the terminator
 *      is located with memchr(), which libc vectorizes. read_mime_header and
 *      read_dot_lines call this for every line, so the scan cost compounds on
 *      large header/body blocks.
 *
 * The change is behavior-preserving, so every case asserts the new result
 * (return code, consumed count, emitted line bytes) is identical to the old
 * result before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/textproto_bench \
 *      tests/neverc/std/textproto_bench.c std/src/net/textproto/textproto.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/net/textproto.h"

/* ============================================================
 * OLD primitive — verbatim reproduction of the previous library
 * ============================================================ */
static int old_read_line(const char *data, size_t len,
                         char *line, size_t line_cap, size_t *consumed) {
    if (!data || len == 0) return -1;
    size_t i = 0;
    while (i < len && data[i] != '\n') i++;
    size_t line_len = i;
    if (line_len > 0 && data[line_len - 1] == '\r') line_len--;
    if (line_len >= line_cap) line_len = line_cap - 1;
    memcpy(line, data, line_len);
    line[line_len] = '\0';
    if (consumed) *consumed = (i < len) ? i + 1 : i;
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

static int line_match(const char *data, size_t len) {
    char ol[8192], nl[8192];
    size_t oc = 0, nc = 0;
    int orc = old_read_line(data, len, ol, sizeof(ol), &oc);
    int nrc = neverc_textproto_read_line(data, len, nl, sizeof(nl), &nc);
    if (orc != nrc) return 0;
    if (orc != 0) return 1;
    if (oc != nc) return 0;
    return strcmp(ol, nl) == 0;
}

static void bench_line(const char *label, const char *data, size_t len) {
    if (!line_match(data, len)) { printf("%-18s  CORRECTNESS FAIL\n", label); return; }

    char buf[8192];
    size_t consumed;
    int iters = (int)(400000000ULL / (len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_read_line(data, len, buf, sizeof(buf), &consumed); sink = consumed; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_textproto_read_line(data, len, buf, sizeof(buf), &consumed); sink = consumed; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (line %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, len);
}

/* Macro-bench: scan an entire header block line-by-line (no allocation),
 * exactly the scan read_mime_header performs. */
static void bench_block(const char *label, const char *block, size_t len) {
    char buf[8192];
    int iters = (int)(40000000ULL / (len + 1)); if (iters < 200) iters = 200;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int it = 0; it < iters; it++) {
            size_t pos = 0, acc = 0, ate;
            while (pos < len) {
                if (old_read_line(block + pos, len - pos, buf, sizeof(buf), &ate) != 0) break;
                pos += ate; acc += (size_t)buf[0];
                if (buf[0] == '\0') break;
            }
            sink = acc;
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int it = 0; it < iters; it++) {
            size_t pos = 0, acc = 0, ate;
            while (pos < len) {
                if (neverc_textproto_read_line(block + pos, len - pos, buf, sizeof(buf), &ate) != 0) break;
                pos += ate; acc += (size_t)buf[0];
                if (buf[0] == '\0') break;
            }
            sink = acc;
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (block %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, len);
}

static char *make_line(size_t body_len, int with_nl, size_t *out_len) {
    char *s = (char *)malloc(body_len + 4);
    for (size_t i = 0; i < body_len; i++) s[i] = (char)('a' + (i % 26));
    size_t n = body_len;
    if (with_nl) { s[n++] = '\r'; s[n++] = '\n'; }
    s[n] = '\0';
    if (out_len) *out_len = n;
    return s;
}

static int correctness(void) {
    const char *cases[] = {
        "", "\n", "\r\n", "x", "x\n", "x\r\n", "ab\rcd\n",
        "Content-Type: text/html\r\n",
        "no newline at all just a long bare line",
        "trailing-cr-only\r", "\r", "a\nb\nc",
        "key:val\r\nnext", "   spaced   \n",
    };
    int ok = 0, total = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        total++;
        if (line_match(cases[i], strlen(cases[i]))) ok++;
        else printf("  EDGE FAIL [%zu]\n", i);
    }
    /* line_cap truncation parity */
    {
        const char *s = "this is a fairly long line that will be truncated\n";
        for (size_t cap = 1; cap <= 20; cap++) {
            char a[64], b[64]; size_t ca = 0, cb = 0;
            int ra = old_read_line(s, strlen(s), a, cap, &ca);
            int rb = neverc_textproto_read_line(s, strlen(s), b, cap, &cb);
            total++;
            if (ra == rb && ca == cb && strcmp(a, b) == 0) ok++;
            else printf("  CAP FAIL cap=%zu\n", cap);
        }
    }
    /* lengths around the SIMD stride boundaries, with and without newline */
    for (size_t L = 0; L <= 300; L++) {
        for (int nl = 0; nl < 2; nl++) {
            size_t ln; char *s = make_line(L, nl, &ln);
            total++;
            if (line_match(s, ln)) ok++; else printf("  LEN FAIL L=%zu nl=%d\n", L, nl);
            free(s);
        }
    }
    printf("edge cases: %d/%d identical\n", ok, total);
    return ok == total;
}

int main(void) {
    printf("=== textproto read_line: memchr (new) vs byte-loop (old) ===\n");
#ifndef TP_NO_TIMING
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    size_t l1, l2, l3, l4;
    char *short_line = make_line(38, 1, &l1);     /* typical HTTP header */
    char *med_line   = make_line(200, 1, &l2);
    char *long_line  = make_line(4000, 1, &l3);
    char *bare_line  = make_line(4000, 0, &l4);    /* no terminator: scan to end */
    bench_line("rl_short_38B",  short_line, l1);
    bench_line("rl_medium_200B", med_line, l2);
    bench_line("rl_long_4KB",   long_line, l3);
    bench_line("rl_bare_4KB",   bare_line, l4);
    free(short_line); free(med_line); free(long_line); free(bare_line);

    /* Realistic HTTP header blocks. */
    const char *hdrs =
        "Host: www.example.com\r\n"
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-US,en;q=0.5\r\n"
        "Accept-Encoding: gzip, deflate, br\r\n"
        "Connection: keep-alive\r\n"
        "Cookie: session=abc123; theme=dark; lang=en; tracking=xyz789\r\n"
        "Cache-Control: max-age=0\r\n"
        "\r\n";
    bench_block("hdr_block",  hdrs, strlen(hdrs));

    printf("\n");
#endif
    int all_ok = correctness();
    printf("\n=== Done%s ===\n", all_ok ? "" : " (MISMATCH!)");
    return all_ok ? 0 : 1;
}
