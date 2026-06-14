/*
 * A/B benchmark + correctness check: net/http chunked transfer-decoding scans.
 *
 * The chunked decoder lives inside a large static parse path in http.c, so this
 * bench reproduces the decode loop with the two scans it performs, selectable
 * between the previous byte-at-a-time form and the memchr form now in the
 * library:
 *
 *   1. terminating "0\r\n\r\n" search over the whole body
 *   2. each chunk-size line's "\r\n"
 *
 * Both forms run the identical hex-size parsing and copy logic, so for every
 * input (valid chunked bodies and random fuzz) they must return the identical
 * decoded bytes, consumed count and status before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/http_chunked_bench \
 *      tests/neverc/std/http_chunked_bench.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Decoder reproduced from http.c; `use_memchr` selects new vs old scans.
 * ============================================================ */
static int decode_chunked(int use_memchr, const char *in, size_t len,
                          char **out, size_t *outlen, size_t *consumed) {
    const char *chunk_start = in;
    size_t chunk_avail = len;

    /* (1) terminator scan */
    const char *term = NULL;
    if (use_memchr) {
        if (chunk_avail >= 5) {
            const char *cur = chunk_start;
            const char *limit = chunk_start + (chunk_avail - 4);
            while (cur < limit) {
                const char *z = (const char *)memchr(cur, '0', (size_t)(limit - cur));
                if (!z) break;
                if (z[1] == '\r' && z[2] == '\n' && z[3] == '\r' && z[4] == '\n') { term = z; break; }
                cur = z + 1;
            }
        }
    } else {
        for (size_t ci = 0; ci + 4 < chunk_avail; ci++) {
            if (chunk_start[ci] == '0' &&
                chunk_start[ci+1] == '\r' && chunk_start[ci+2] == '\n' &&
                chunk_start[ci+3] == '\r' && chunk_start[ci+4] == '\n') { term = chunk_start + ci; break; }
        }
    }
    if (!term) { *out = NULL; *outlen = 0; *consumed = 0; return -1; }

    size_t cap = 256, dlen = 0;
    char *d = (char *)malloc(cap);
    size_t cpos = 0;
    while (cpos < chunk_avail) {
        /* (2) chunk-size line CRLF scan */
        const char *cline_end = NULL;
        if (use_memchr) {
            const char *cur = chunk_start + cpos;
            size_t avail = chunk_avail - cpos;
            while (avail >= 2) {
                const char *cr = (const char *)memchr(cur, '\r', avail - 1);
                if (!cr) break;
                if (cr[1] == '\n') { cline_end = cr; break; }
                size_t adv = (size_t)(cr - cur) + 1;
                cur += adv; avail -= adv;
            }
        } else {
            for (size_t ci = cpos; ci + 1 < chunk_avail; ci++) {
                if (chunk_start[ci] == '\r' && chunk_start[ci+1] == '\n') { cline_end = chunk_start + ci; break; }
            }
        }
        if (!cline_end) break;

        unsigned long csz = 0;
        const char *cp = chunk_start + cpos;
        while (cp < cline_end) {
            char cc = *cp;
            if (cc >= '0' && cc <= '9') csz = csz * 16 + (unsigned long)(cc - '0');
            else if (cc >= 'a' && cc <= 'f') csz = csz * 16 + (unsigned long)(cc - 'a' + 10);
            else if (cc >= 'A' && cc <= 'F') csz = csz * 16 + (unsigned long)(cc - 'A' + 10);
            else break;
            cp++;
        }
        cpos = (size_t)(cline_end - chunk_start) + 2;
        if (csz == 0) break;
        if (cpos + csz > chunk_avail) break;
        while (dlen + csz > cap) { cap *= 2; d = (char *)realloc(d, cap); }
        memcpy(d + dlen, chunk_start + cpos, (size_t)csz);
        dlen += csz;
        cpos += csz + 2;
    }

    *out = d; *outlen = dlen; *consumed = (size_t)(term + 5 - chunk_start);
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

static int dec_match(const char *in, size_t len) {
    char *od = NULL, *nd = NULL; size_t ol = 0, nl = 0, oc = 0, nc = 0;
    int ro = decode_chunked(0, in, len, &od, &ol, &oc);
    int rn = decode_chunked(1, in, len, &nd, &nl, &nc);
    int ok = (ro == rn) && (oc == nc) && (ol == nl) &&
             (ol == 0 || (od && nd && memcmp(od, nd, ol) == 0));
    free(od); free(nd);
    return ok;
}

static unsigned rng = 0x9E3779B9u;
static unsigned nextr(void) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }

/* Build a well-formed chunked body of ~payload bytes split into chunks of
 * about chunk_sz each, alphabet selects the payload bytes. */
static char *gen_chunked(size_t payload, size_t chunk_sz, int alphabet, size_t *out_len) {
    if (chunk_sz == 0) chunk_sz = 1;
    size_t cap = payload + payload / chunk_sz * 16 + 64;
    char *b = (char *)malloc(cap);
    size_t k = 0, done = 0;
    while (done < payload) {
        size_t cs = payload - done;
        if (cs > chunk_sz) cs = chunk_sz;
        k += (size_t)snprintf(b + k, cap - k, "%zx\r\n", cs);
        for (size_t j = 0; j < cs; j++) {
            char c;
            if (alphabet == 0) c = (char)('a' + (nextr() % 26));          /* text */
            else if (alphabet == 1) c = (char)(nextr() & 0xFF);            /* binary */
            else c = (char)('0' + (nextr() % 10));                          /* digit-heavy */
            b[k++] = c;
        }
        b[k++] = '\r'; b[k++] = '\n';
        done += cs;
    }
    memcpy(b + k, "0\r\n\r\n", 5); k += 5;
    *out_len = k;
    return b;
}

/* ============================================================
 * Correctness
 * ============================================================ */
static int correctness(void) {
    static const char *lits[] = {
        "", "0\r\n\r\n", "5\r\nhello\r\n0\r\n\r\n",
        "1\r\nA\r\n2\r\nBC\r\n0\r\n\r\n",
        "a\r\n0123456789\r\n0\r\n\r\n",
        "incomplete-no-term", "3\r\nabc", "ff\r\n",
        "4\r\nwxyz\r\n3\r\nabc\r\n0\r\n\r\n",
        "no crlf here at all just text 0 and stuff",
        "0\r\n\r\nextra-trailing-data",
    };
    int ok = 0, total = 0;
    for (size_t i = 0; i < sizeof(lits)/sizeof(lits[0]); i++) {
        total++;
        if (dec_match(lits[i], strlen(lits[i]))) ok++;
        else printf("  LIT FAIL [%zu]\n", i);
    }

    /* well-formed chunked bodies of many shapes */
    for (int t = 0; t < 500; t++) {
        size_t payload = (size_t)(nextr() % 6000);
        size_t cs = 1 + (size_t)(nextr() % 200);
        int ab = (int)(nextr() % 3);
        size_t L; char *s = gen_chunked(payload, cs, ab, &L);
        total++;
        if (dec_match(s, L)) ok++;
        else printf("  GEN FAIL t=%d payload=%zu cs=%zu ab=%d\n", t, payload, cs, ab);
        free(s);
    }

    /* raw random fuzz: both scans must agree even on garbage */
    for (int t = 0; t < 4000; t++) {
        size_t L = (size_t)(nextr() % 400);
        char *s = (char *)malloc(L + 1);
        for (size_t j = 0; j < L; j++) {
            unsigned r = nextr() % 10;
            s[j] = (r < 3) ? (char)"\r\n0"[r] : (char)(nextr() & 0xFF);  /* bias toward \r \n 0 */
        }
        total++;
        if (dec_match(s, L)) ok++;
        else printf("  FUZZ FAIL t=%d L=%zu\n", t, L);
        free(s);
    }

    printf("differential fuzz: %d/%d identical (old vs new)\n", ok, total);
    return ok == total;
}

/* ============================================================
 * Timing
 * ============================================================ */
static void bench(const char *label, const char *in, size_t len, const char *note) {
    if (!dec_match(in, len)) { printf("%-18s  CORRECTNESS FAIL\n", label); return; }
    int iters = (int)(400000000ULL / (len + 1));
    if (iters < 50) iters = 50;

    double t_old = 1e30, t_new = 1e30;
    char *out = NULL; size_t ol, oc;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { decode_chunked(0, in, len, &out, &ol, &oc); sink = ol; free(out); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { decode_chunked(1, in, len, &out, &ol, &oc); sink = ol; free(out); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %9.1f ms  %9.1f ms  %6.2fx   %s\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, note);
}

int main(void) {
    printf("=== net/http chunked decode: memchr scans (new) vs byte loops (old) ===\n");

    int all_ok = correctness();

#ifndef HC_NO_TIMING
    printf("\n%-18s  %9s  %9s  %8s\n", "case", "old", "new", "speedup");
    size_t L;
    char *t_big   = gen_chunked(131072, 16384, 0, &L);
    bench("text_16K_chunks",  t_big, L, "(128KB text, 16KB chunks)");
    free(t_big);

    char *t_small = gen_chunked(65536, 64, 0, &L);
    bench("text_64B_chunks",  t_small, L, "(64KB text, 64B chunks)");
    free(t_small);

    char *b_big   = gen_chunked(131072, 16384, 1, &L);
    bench("binary_16K",       b_big, L, "(128KB binary, 16KB chunks)");
    free(b_big);

    char *d_big   = gen_chunked(131072, 16384, 2, &L);
    bench("digit_heavy_16K",  d_big, L, "(128KB digits; '0'-dense)");
    free(d_big);
#endif

    printf("\n=== Done%s ===\n", all_ok ? "" : " (MISMATCH!)");
    return all_ok ? 0 : 1;
}
