/*
 * A/B benchmark + correctness check: encoding/pem decode.
 *
 *  - pem_old_decode — the previous library decoder, reproduced verbatim:
 *      it locates the BEGIN/END markers by memcmp-probing every byte offset
 *      and decodes the base64 body one character at a time with a per-byte
 *      whitespace test.
 *
 *  - neverc_pem_decode (library) — the new decoder: BEGIN/END markers are
 *      found with memchr() on the leading '-' (absent from a standard base64
 *      body), and the body is decoded four aligned base64 chars at a time,
 *      dropping to the per-char path only across newlines / padding.
 *
 * The fast path is behavior-preserving, so every case asserts the new result
 * (return code, type, decoded bytes, bytes_written, rest_offset) is identical
 * to the old result before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/pem_bench \
 *      tests/neverc/std/pem_bench.c std/src/encoding/pem/pem.c \
 *      std/src/encoding/base64/base64.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/encoding/pem.h"
#include "neverc/std/encoding/base64.h"

/* ============================================================
 * OLD decoder — verbatim reproduction of the previous library
 * ============================================================ */
static const char *O_BEGIN_PREFIX = "-----BEGIN ";
static const char *O_END_PREFIX   = "-----END ";
static const char *O_DASHES       = "-----";

static int pem_old_decode(const char *pem_data, size_t pem_len,
                          char *type_buf, size_t type_cap,
                          uint8_t *out_buf, size_t out_cap,
                          size_t *bytes_written,
                          size_t *rest_offset)
{
    if (bytes_written) *bytes_written = 0;
    if (rest_offset) *rest_offset = pem_len;

    const char *begin = NULL;
    for (size_t i = 0; i + 11 <= pem_len; i++) {
        if (memcmp(pem_data + i, O_BEGIN_PREFIX, 11) == 0) {
            if (i == 0 || pem_data[i - 1] == '\n' || pem_data[i - 1] == '\r') {
                begin = pem_data + i;
                break;
            }
        }
    }
    if (!begin) return -1;

    const char *type_start = begin + 11;
    const char *dash_end = strstr(type_start, O_DASHES);
    if (!dash_end) return -1;

    size_t type_len = (size_t)(dash_end - type_start);
    if (type_len >= type_cap) return -1;
    memcpy(type_buf, type_start, type_len);
    type_buf[type_len] = '\0';

    const char *body_start = dash_end + 5;
    while (body_start < pem_data + pem_len && (*body_start == '\n' || *body_start == '\r'))
        body_start++;

    char end_marker[256];
    size_t em_len = 0;
    size_t ep_len = strlen(O_END_PREFIX);
    size_t ds_len = strlen(O_DASHES);
    if (ep_len + type_len + ds_len >= sizeof(end_marker)) return -1;
    memcpy(end_marker + em_len, O_END_PREFIX, ep_len); em_len += ep_len;
    memcpy(end_marker + em_len, type_buf, type_len); em_len += type_len;
    memcpy(end_marker + em_len, O_DASHES, ds_len); em_len += ds_len;
    end_marker[em_len] = '\0';

    const char *end = NULL;
    for (const char *s = body_start; s + em_len <= pem_data + pem_len; s++) {
        if (memcmp(s, end_marker, (size_t)em_len) == 0) {
            end = s;
            break;
        }
    }
    if (!end) return -1;

    size_t b64_raw_len = (size_t)(end - body_start);

    size_t clean_len = 0;
    for (size_t i = 0; i < b64_raw_len; i++) {
        char c = body_start[i];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t')
            clean_len++;
    }

    size_t decoded_max = neverc_base64_decoded_len(clean_len);
    if (decoded_max > out_cap) return -1;

    static const int8_t b64tab[256] = {
        ['A']=0, ['B']=1, ['C']=2, ['D']=3, ['E']=4, ['F']=5, ['G']=6, ['H']=7,
        ['I']=8, ['J']=9, ['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,
        ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
        ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
        ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
        ['y']=50,['z']=51,
        ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,
        ['8']=60,['9']=61,['+']=62,['/']=63,
    };

    uint8_t quad[4];
    int qi = 0;
    size_t out_i = 0;
    int pad = 0;
    for (size_t i = 0; i < b64_raw_len; i++) {
        unsigned char c = (unsigned char)body_start[i];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') { quad[qi++] = 0; pad++; }
        else quad[qi++] = (uint8_t)b64tab[c];
        if (qi == 4) {
            if (out_i < out_cap) out_buf[out_i++] = (uint8_t)((quad[0]<<2) | (quad[1]>>4));
            if (pad < 2 && out_i < out_cap) out_buf[out_i++] = (uint8_t)((quad[1]<<4) | (quad[2]>>2));
            if (pad < 1 && out_i < out_cap) out_buf[out_i++] = (uint8_t)((quad[2]<<6) | quad[3]);
            qi = 0; pad = 0;
        }
    }
    int decoded = (int)out_i;
    if (decoded < 0) return -1;

    if (bytes_written) *bytes_written = (size_t)decoded;

    const char *after_end = end + em_len;
    while (after_end < pem_data + pem_len && (*after_end == '-' || *after_end == '\n' || *after_end == '\r'))
        after_end++;
    if (rest_offset) *rest_offset = (size_t)(after_end - pem_data);

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

#define OUTCAP (1u << 21)
static uint8_t g_ob[OUTCAP], g_nb[OUTCAP];

/* Compare old vs new on a single PEM input across all observable outputs. */
static int results_match(const char *pem, size_t len, size_t out_cap) {
    char ot[256], nt[256];
    size_t onbw = 0, nnbw = 0, orest = 0, nrest = 0;
    memset(ot, 0, sizeof(ot)); memset(nt, 0, sizeof(nt));
    if (out_cap > OUTCAP) out_cap = OUTCAP;

    int orc = pem_old_decode(pem, len, ot, sizeof(ot), g_ob, out_cap, &onbw, &orest);
    int nrc = neverc_pem_decode(pem, len, nt, sizeof(nt), g_nb, out_cap, &nnbw, &nrest);

    if (orc != nrc) return 0;
    if (strcmp(ot, nt) != 0) return 0;
    if (orc == 0) {
        if (onbw != nnbw) return 0;
        if (orest != nrest) return 0;
        if (memcmp(g_ob, g_nb, onbw) != 0) return 0;
    }
    return 1;
}

/* Build a \r\n variant of a \n-delimited PEM string. */
static char *to_crlf(const char *s, size_t len, size_t *out_len) {
    char *r = (char *)malloc(len * 2 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') r[j++] = '\r';
        r[j++] = s[i];
    }
    r[j] = '\0';
    if (out_len) *out_len = j;
    return r;
}

static char *make_pem(const uint8_t *data, size_t n, const char *type, size_t *out_len) {
    size_t b64 = neverc_base64_encoded_len(n);
    size_t cap = b64 + b64 / 64 + 512 + strlen(type) * 2;  /* + per-line newlines */
    char *buf = (char *)malloc(cap);
    int len = neverc_pem_encode(buf, cap, type, data, n);
    if (len < 0) { free(buf); return NULL; }
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static void bench_case(const char *label, const uint8_t *data, size_t n) {
    size_t plen = 0;
    char *pem = make_pem(data, n, "CERTIFICATE", &plen);
    if (!pem) { printf("%-18s  ENCODE FAIL\n", label); return; }

    if (!results_match(pem, plen, OUTCAP)) {
        printf("%-18s  CORRECTNESS FAIL\n", label);
        free(pem);
        return;
    }

    int iters = (int)(200000000ULL / (plen + 1)); if (iters < 50) iters = 50;
    double t_old = 1e30, t_new = 1e30;
    char ot[256], nt[256];
    size_t bw = 0, rest = 0;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink = (size_t)pem_old_decode(pem, plen, ot, sizeof(ot), g_ob, OUTCAP, &bw, &rest);
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink = (size_t)neverc_pem_decode(pem, plen, nt, sizeof(nt), g_nb, OUTCAP, &bw, &rest);
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (pem %zu B -> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, plen, bw);
    free(pem);
}

/* ============================================================
 * Correctness: structured edge cases + randomized fuzz
 * ============================================================ */
static unsigned long rng = 0x9e3779b97f4a7c15UL;
static unsigned rnd(void) { rng = rng * 6364136223846793005UL + 1; return (unsigned)(rng >> 33); }

static int correctness(void) {
    int ok = 0, total = 0;

    /* Hand-crafted structural edge cases. */
    const char *cases[] = {
        "not a pem block",
        "-----BEGIN FOO-----\nYWJj\n",                         /* missing END */
        "-----BEGIN FOO-----\nYWJj\n-----END BAR-----\n",      /* type mismatch */
        "-----BEGIN X-----\n-----END X-----\n",                /* empty body */
        "-----BEGIN RSA PRIVATE KEY-----\nSGVsbG8gV29ybGQ=\n-----END RSA PRIVATE KEY-----\n",
        "preamble text\n-----BEGIN C-----\nQUJD\n-----END C-----\ntrailing junk",
        "-----BEGIN C-----\nQUJ=\n-----END C-----\n",          /* 1-pad quad */
        "-----BEGIN C-----\nQQ==\n-----END C-----\n",          /* 2-pad quad */
        "-----BEGIN C-----\nQ-Q=\n-----END C-----\n",          /* '-' inside body */
        "-----BEGIN C-----\n  QUJD  \n-----END C-----\n",      /* spaces around line */
        "-----BEGIN C-----\nQU\tJD\n-----END C-----\n",        /* embedded tab */
        "-----BEGIN C-----\nQUJDRUZH\nSUpLTE1O\n-----END C-----\n", /* two lines */
        "-----BEGIN C-----\nQ#J$\n-----END C-----\n",          /* stray invalid bytes */
        "garbage-----BEGIN C-----\nQUJD\n-----END C-----\n",   /* BEGIN not at line start */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        total++;
        if (results_match(cases[i], strlen(cases[i]), OUTCAP)) ok++;
        else printf("  EDGE FAIL [%zu]: \"%.40s\"\n", i, cases[i]);
    }

    /* CRLF line endings parity on a known block. */
    {
        const char *base = "-----BEGIN C-----\nQUJDRUZHSElKS0xNTk9Q\n-----END C-----\n";
        size_t cl; char *crlf = to_crlf(base, strlen(base), &cl);
        total++;
        if (results_match(crlf, cl, OUTCAP)) ok++; else printf("  CRLF FAIL\n");
        free(crlf);
    }

    /* Capacity-limit parity: force the truncation path at many out_cap values. */
    {
        uint8_t data[200];
        for (int i = 0; i < 200; i++) data[i] = (uint8_t)(i * 7 + 1);
        size_t pl; char *pem = make_pem(data, sizeof(data), "CERTIFICATE", &pl);
        for (size_t cap = 0; cap <= 210; cap++) {
            total++;
            if (results_match(pem, pl, cap)) ok++;
            else { printf("  CAP FAIL cap=%zu\n", cap); }
        }
        free(pem);
    }

    /* Randomized fuzz: random payload sizes, random types, CRLF/LF, pre/post junk. */
    for (int t = 0; t < 4000; t++) {
        size_t n = rnd() % 600;
        uint8_t *data = (uint8_t *)malloc(n + 1);
        for (size_t i = 0; i < n; i++) data[i] = (uint8_t)rnd();
        char type[16];
        int tl = (int)(rnd() % 8) + 1;
        for (int i = 0; i < tl; i++) type[i] = (char)('A' + (rnd() % 26));
        type[tl] = '\0';

        size_t pl; char *pem = make_pem(data, n, type, &pl);
        if (pem) {
            char *use = pem; size_t ul = pl; char *crlf = NULL;
            if (rnd() & 1) { crlf = to_crlf(pem, pl, &ul); use = crlf; }
            total++;
            size_t cap = (rnd() & 3) ? OUTCAP : (rnd() % (n + 4));
            if (results_match(use, ul, cap)) ok++;
            else printf("  FUZZ FAIL t=%d n=%zu type=%s cap=%zu\n", t, n, type, cap);
            free(crlf);
            free(pem);
        }
        free(data);
    }

    printf("edge cases: %d/%d identical\n", ok, total);
    return ok == total;
}

int main(void) {
    printf("=== pem_decode: memchr marker scan + bulk quad decode (new) vs per-byte (old) ===\n");
#ifndef PEM_NO_TIMING
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    static uint8_t data[64 * 1024];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 131 + 7);

    bench_case("cert_64B",   data, 64);
    bench_case("cert_1K",    data, 1024);
    bench_case("cert_8K",    data, 8192);
    bench_case("cert_64K",   data, 64 * 1024);

    printf("\n");
#endif
    int all_ok = correctness();
    printf("\n=== Done%s ===\n", all_ok ? "" : " (MISMATCH!)");
    return all_ok ? 0 : 1;
}
