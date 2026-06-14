/*
 * A/B benchmark + correctness check: net/http request-line/header scans.
 *
 * parse_request() is a static function deep inside http.c, so this bench
 * reproduces the four byte-at-a-time scans it used to perform and the memchr
 * replacements now in the library, then fuzz-compares the two on random and
 * structured HTTP inputs (they must return identical offsets) before timing
 * a realistic full-request scan.
 *
 *   1. header terminator  "\r\n\r\n"
 *   2. request-line end   "\r\n"
 *   3. query '?' in the request target
 *   4. per-header-line    first colon + first "\r\n"
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/http_parse_bench \
 *      tests/neverc/std/http_parse_bench.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * OLD scans — verbatim reproduction of the previous parse_request
 * ============================================================ */
static long old_hdr_end(const char *raw, size_t rawlen) {
    for (size_t i = 0; i + 3 < rawlen; i++)
        if (raw[i] == '\r' && raw[i+1] == '\n' && raw[i+2] == '\r' && raw[i+3] == '\n')
            return (long)i;
    return -1;
}
static long old_eol(const char *raw, size_t rawlen) {
    for (size_t i = 0; i + 1 < rawlen; i++)
        if (raw[i] == '\r' && raw[i+1] == '\n')
            return (long)i;
    return -1;
}
static long old_qmark(const char *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (p[i] == '?') return (long)i;
    return -1;
}
static void old_line(const char *p, const char *end, long *colon_off, long *he_off) {
    const char *colon = NULL, *hline_end = NULL;
    for (const char *q = p; q + 1 < end; q++) {
        if (*q == ':' && !colon) colon = q;
        if (q[0] == '\r' && q[1] == '\n') { hline_end = q; break; }
    }
    *he_off = hline_end ? (long)(hline_end - p) : -1;
    *colon_off = colon ? (long)(colon - p) : -1;
}

/* ============================================================
 * NEW scans — memchr versions now in the library
 * ============================================================ */
static long new_hdr_end(const char *raw, size_t rawlen) {
    const char *raw_end = raw + rawlen;
    for (const char *q = raw; q < raw_end; ) {
        const char *nl = (const char *)memchr(q, '\n', (size_t)(raw_end - q));
        if (!nl) break;
        size_t pidx = (size_t)(nl - raw);
        if (pidx >= 3 && raw[pidx-1] == '\r' && raw[pidx-2] == '\n' && raw[pidx-3] == '\r')
            return (long)(pidx - 3);
        q = nl + 1;
    }
    return -1;
}
static long new_eol(const char *raw, size_t rawlen) {
    const char *raw_end = raw + rawlen;
    for (const char *q = raw; q < raw_end; ) {
        const char *nl = (const char *)memchr(q, '\n', (size_t)(raw_end - q));
        if (!nl) break;
        if (nl > raw && nl[-1] == '\r') return (long)(nl - 1 - raw);
        q = nl + 1;
    }
    return -1;
}
static long new_qmark(const char *p, size_t n) {
    const char *q = (const char *)memchr(p, '?', n);
    return q ? (long)(q - p) : -1;
}
static void new_line(const char *p, const char *end, long *colon_off, long *he_off) {
    const char *hline_end = NULL;
    for (const char *q = p; q < end; ) {
        const char *nl = (const char *)memchr(q, '\n', (size_t)(end - q));
        if (!nl) break;
        if (nl > p && nl[-1] == '\r') { hline_end = nl - 1; break; }
        q = nl + 1;
    }
    if (!hline_end) { *he_off = -1; *colon_off = -1; return; }
    const char *colon = (const char *)memchr(p, ':', (size_t)(hline_end - p));
    *he_off = (long)(hline_end - p);
    *colon_off = colon ? (long)(colon - p) : -1;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile long sink;

static unsigned long rng = 0x243f6a8885a308d3UL;
static unsigned rnd(void) { rng = rng * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(rng >> 33); }

/* Compare all four scans on one buffer. */
static int scans_match(const char *buf, size_t len) {
    if (old_hdr_end(buf, len) != new_hdr_end(buf, len)) return 0;
    if (old_eol(buf, len) != new_eol(buf, len)) return 0;
    if (old_qmark(buf, len) != new_qmark(buf, len)) return 0;
    long oc, oe, nc, ne;
    old_line(buf, buf + len, &oc, &oe);
    new_line(buf, buf + len, &nc, &ne);
    if (oe != ne) return 0;
    if (oe != -1 && oc != nc) return 0;
    return 1;
}

static int correctness(void) {
    int ok = 0, total = 0;

    const char *cases[] = {
        "",
        "GET / HTTP/1.1\r\nHost: x\r\n\r\n",
        "POST /a?b=c HTTP/1.1\r\nH: v\r\n\r\nbody",
        "\r\n\r\n", "\r\n", "\n\n", "a\r\rb\r\n", "x\nyy:zz\r\n",
        "no-crlf-at-all key:val ? more",
        "K1:\r\nK2: v2\r\n\r\n",                  /* empty value */
        ":leadingcolon\r\n\r\n",
        "weird\rcontent\nwith:colon\r\nend\r\n\r\n",
        "GET /path/only HTTP/1.0\r\n\r\n",        /* no query */
        "A:1\nB:2\r\nC:3\r\n\r\n",                /* bare \n inside */
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        total++;
        if (scans_match(cases[i], strlen(cases[i]))) ok++;
        else printf("  EDGE FAIL [%zu]: \"%.40s\"\n", i, cases[i]);
    }

    /* Randomized fuzz heavily biased toward \r \n : ? so the branches fire. */
    static char buf[512];
    for (int t = 0; t < 200000; t++) {
        size_t len = rnd() % sizeof(buf);
        for (size_t i = 0; i < len; i++) {
            unsigned r = rnd() % 10;
            buf[i] = (r == 0) ? '\r' : (r == 1) ? '\n' : (r == 2) ? ':' :
                     (r == 3) ? '?' : (char)('a' + (rnd() % 26));
        }
        total++;
        if (scans_match(buf, len)) ok++;
        else { printf("  FUZZ FAIL t=%d len=%zu\n", t, len); if (total - ok > 5) break; }
    }

    printf("edge cases: %d/%d identical\n", ok, total);
    return ok == total;
}

/* Simulate the full set of scans parse_request performs on a request. */
static long scan_request_old(const char *raw, size_t len) {
    long acc = 0;
    long he = old_hdr_end(raw, len); acc += he;
    long e  = old_eol(raw, len);     acc += e;
    if (e > 0) acc += old_qmark(raw, (size_t)e);
    if (he >= 0) {
        const char *p = raw + e + 2;
        const char *end = raw + he + 2;
        while (p < end) {
            long c, h; old_line(p, end, &c, &h);
            if (h < 0) break;
            acc += c + h;
            p += h + 2;
        }
    }
    return acc;
}
static long scan_request_new(const char *raw, size_t len) {
    long acc = 0;
    long he = new_hdr_end(raw, len); acc += he;
    long e  = new_eol(raw, len);     acc += e;
    if (e > 0) acc += new_qmark(raw, (size_t)e);
    if (he >= 0) {
        const char *p = raw + e + 2;
        const char *end = raw + he + 2;
        while (p < end) {
            long c, h; new_line(p, end, &c, &h);
            if (h < 0) break;
            acc += c + h;
            p += h + 2;
        }
    }
    return acc;
}

static char *make_request(int nheaders, int value_len, size_t *out_len) {
    size_t cap = 256 + (size_t)nheaders * (64 + (size_t)value_len);
    char *b = (char *)malloc(cap);
    const char *line = "GET /api/v1/resource?id=42&sort=desc HTTP/1.1\r\n";
    size_t n = strlen(line);
    memcpy(b, line, n);
    const char *names[] = {"Host","User-Agent","Accept","Accept-Language",
                           "Accept-Encoding","Connection","Cookie","Cache-Control",
                           "Referer","X-Request-Id"};
    for (int i = 0; i < nheaders; i++) {
        const char *nm = names[i % 10];
        size_t nl = strlen(nm);
        memcpy(b + n, nm, nl); n += nl;
        b[n++] = ':'; b[n++] = ' ';
        for (int j = 0; j < value_len; j++) b[n++] = (char)('a' + (j % 26));
        b[n++] = '\r'; b[n++] = '\n';
    }
    b[n++] = '\r'; b[n++] = '\n';
    if (out_len) *out_len = n;
    return b;
}

static void bench_req(const char *label, int nheaders, int value_len) {
    size_t len; char *req = make_request(nheaders, value_len, &len);
    if (scan_request_old(req, len) != scan_request_new(req, len)) {
        printf("%-18s  CORRECTNESS FAIL\n", label); free(req); return;
    }
    int iters = (int)(200000000ULL / (len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) sink = scan_request_old(req, len);
        double el = now_sec() - t0; if (el < t_old) t_old = el;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) sink = scan_request_new(req, len);
        el = now_sec() - t0; if (el < t_new) t_new = el;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (req %zu B, %d hdrs)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, len, nheaders);
    free(req);
}

int main(void) {
    printf("=== http request scans: memchr (new) vs byte-loop (old) ===\n");
#ifndef HTTP_NO_TIMING
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    bench_req("small_8hdr",   8,  24);
    bench_req("typical_12hdr",12, 40);
    bench_req("big_24hdr",    24, 80);
    bench_req("long_values",  12, 600);
    printf("\n");
#endif
    int all_ok = correctness();
    printf("\n=== Done%s ===\n", all_ok ? "" : " (MISMATCH!)");
    return all_ok ? 0 : 1;
}
