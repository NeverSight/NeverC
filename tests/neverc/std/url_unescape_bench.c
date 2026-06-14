/*
 * A/B benchmark + correctness check: net/url percent-decoding (unescape).
 *
 *  - old_percent_decode — the previous library decoder, reproduced verbatim: a
 *      per-byte loop that, for every input byte, tests for a '%' escape (with
 *      two hex-digit lookups), tests for '+', and otherwise stores one byte at
 *      a time.
 *
 *  - neverc_url_query_unescape (library) — the new decoder: strchr finds the
 *      next '%' and the run before it is copied in one auto-vectorizable pass
 *      that folds the '+'->' ' substitution into the copy, so escape-free
 *      stretches (including '+'-heavy ones) move at memory speed; only the
 *      actual '%' escapes take the byte-at-a-time path.
 *
 * The decoding rules are unchanged, so every case asserts the new output is
 * byte-for-byte identical to the old output (same return value and bytes,
 * including under a tight output cap) before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/url_unescape_bench \
 *      tests/neverc/std/url_unescape_bench.c std/src/net/url/url.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/net/url.h"

/* ============================================================
 * OLD decoder — verbatim reproduction of the previous library
 * ============================================================ */
static int o_hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int old_percent_decode(const char *s, char *buf, size_t cap) {
    size_t si = 0, di = 0;
    while (s[si] && di < cap - 1) {
        if (s[si] == '%' && s[si+1] && s[si+2]) {
            int h = o_hex_digit(s[si+1]);
            int l = o_hex_digit(s[si+2]);
            if (h >= 0 && l >= 0) {
                buf[di++] = (char)((h << 4) | l);
                si += 3;
                continue;
            }
        }
        if (s[si] == '+') {
            buf[di++] = ' ';
            si++;
        } else {
            buf[di++] = s[si++];
        }
    }
    buf[di] = '\0';
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

/* Build a ~target_len string by repeating `pattern`. Caller frees. */
static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

typedef int (*dec_fn)(const char *, char *, size_t);

static void bench_case(const char *label, const char *input,
                       dec_fn old_fn, dec_fn new_fn) {
    size_t in_len = strlen(input);
    size_t cap = in_len + 16;          /* decode never grows the input */
    char *ob = (char *)malloc(cap), *nb = (char *)malloc(cap);

    int ro = old_fn(input, ob, cap);
    int rn = new_fn(input, nb, cap);
    if (ro != rn || ro < 0 || memcmp(ob, nb, (size_t)ro) != 0) {
        printf("%-18s  CORRECTNESS FAIL (ro=%d rn=%d)\n", label, ro, rn);
        free(ob); free(nb); return;
    }

    int iters = (int)(400000000 / (in_len + 1)); if (iters < 1000) iters = 1000;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_fn(input, ob, cap); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = new_fn(input, nb, cap); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %d B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, rn);
    free(ob); free(nb);
}

/* ============================================================
 * Edge-case correctness: old vs new must agree exactly, including
 * the trailing NUL position, across a range of output caps.
 * ============================================================ */
static int dec_eq(const char *in, size_t cap, const char *desc) {
    char ob[512], nb[512];
    memset(ob, 0xAA, sizeof(ob));
    memset(nb, 0xBB, sizeof(nb));
    int ro = old_percent_decode(in, ob, cap);
    int rn = neverc_url_query_unescape(in, nb, cap);
    /* Compare return value, the produced bytes, and the terminating NUL. */
    int ok = (ro == rn);
    if (ok && ro >= 0) {
        if (memcmp(ob, nb, (size_t)ro) != 0) ok = 0;
        if (ob[ro] != nb[rn]) ok = 0;     /* both must be '\0' at the same spot */
    }
    if (!ok) {
        printf("  EDGE FAIL: %s (cap=%zu old=%d \"%.*s\" new=%d \"%.*s\")\n",
               desc, cap, ro, ro > 0 ? ro : 0, ob, rn, rn > 0 ? rn : 0, nb);
    }
    return ok;
}

static void correctness_extra(void) {
    static const char *inputs[] = {
        "",                                   /* empty */
        "plain",                              /* no specials */
        "hello+world",                        /* '+' to space */
        "a%20b",                              /* basic %XX */
        "%41%42%43",                          /* all escapes */
        "100%25",                             /* trailing escape */
        "%2",                                 /* truncated escape at end */
        "%",                                  /* lone percent at end */
        "%G0",                                /* invalid hex (high) */
        "%0G",                                /* invalid hex (low) */
        "%%41",                               /* percent then valid escape */
        "a+b%20c+d",                          /* mixed plus and percent */
        "key=value&x=%26y+z",                 /* realistic query fragment */
        "%e4%b8%ad",                          /* UTF-8 bytes via lowercase hex */
        "no+trailing%",                       /* plus run then lone percent */
    };
    size_t caps[] = { 512, 8, 5, 4, 3, 2, 1 };  /* full + several tight caps */
    int ok = 0, n = 0;
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        for (size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); c++) {
            n++; ok += dec_eq(inputs[i], caps[c], inputs[i]);
        }
    }
    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    printf("=== net/url percent-decode: strchr bulk-copy (new) vs per-byte (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    /* No escapes at all: entirely one bulk memcpy run. */
    char *plain = make_repeat("the_quick_brown_fox_jumps_over_the_lazy_dog.", 4096);
    bench_case("decode_plain", plain, old_percent_decode, neverc_url_query_unescape);

    /* Realistic query: mostly literals with sparse %20 and '+'. */
    char *typ = make_repeat("search=hello%20world&tag=c+programming&id=42&ref=home%2Fpage&", 4096);
    bench_case("decode_typical", typ, old_percent_decode, neverc_url_query_unescape);

    /* '+'-heavy (spaces encoded as plus): short literal runs. */
    char *plus = make_repeat("one+two+three+four+five+six+seven+eight+", 4096);
    bench_case("decode_plus", plus, old_percent_decode, neverc_url_query_unescape);

    /* Escape-dense worst case: almost nothing to bulk-copy. */
    char *heavy = make_repeat("%20%21%22%23%24%25%26%27", 4096);
    bench_case("decode_heavy", heavy, old_percent_decode, neverc_url_query_unescape);

    free(plain); free(typ); free(plus); free(heavy);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
