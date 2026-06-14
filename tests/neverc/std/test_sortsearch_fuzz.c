/*
 * Differential fuzz regression test for the two heavily hand-optimized
 * std engines, exercised through their public APIs:
 *
 *   - sort_impl.h  via  neverc_sort_custom / _ints / _doubles / _stable
 *   - strsearch.h  via  neverc_bytes_index / _last_index
 *
 * Every case is checked against an independent oracle (libc qsort for the
 * unstable sort key order, an index-tiebreak invariant for stable-sort
 * stability, and a brute-force scan for substring search) over many
 * randomized + adversarial inputs.  This is the class of test that catches
 * structural regressions like the Timsort merge-invariant overflow fixed in
 * f655e563a — example-based tests miss those.
 *
 * Deterministic (fixed-seed xorshift), pure C11, no platform APIs, so it runs
 * identically on every target the std test harness compiles for.
 */
#include "neverc/std/sort.h"
#include "neverc/std/bytes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_pass = 0;
static int g_fail = 0;

static void ok(int cond, const char *what) {
    if (cond) g_pass++;
    else { g_fail++; printf("FAIL: %s\n", what); }
}

/* ---- deterministic RNG (independent of std rand) ---- */
static uint64_t rng = 0x123456789abcdef0ULL;
static uint64_t xr(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static unsigned ru(unsigned m) { return m ? (unsigned)(xr() % m) : 0u; }

/* ---- oracles ---- */
static int cmp_i(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y);
}
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
/* arbitrary-size record whose leading 4 bytes hold the int sort key */
static int cmp_blob(const void *a, const void *b) {
    int x, y; memcpy(&x, a, sizeof x); memcpy(&y, b, sizeof y); return (x > y) - (x < y);
}
typedef struct { int key; int idx; } pair_t;
static int cmp_pair(const void *a, const void *b) {
    const pair_t *x = (const pair_t *)a, *y = (const pair_t *)b;
    return (x->key > y->key) - (x->key < y->key);
}

/* 0 random,1 sorted,2 reverse,3 few-unique,4 all-equal,5 pipe-organ,
 * 6 nearly-sorted,7 sawtooth — the classic pattern-defeating battery. */
static void fill_pattern(int *a, size_t n, int mode) {
    switch (mode) {
    case 0: for (size_t i = 0; i < n; i++) a[i] = (int)xr(); break;
    case 1: for (size_t i = 0; i < n; i++) a[i] = (int)i; break;
    case 2: for (size_t i = 0; i < n; i++) a[i] = (int)(n - i); break;
    case 3: for (size_t i = 0; i < n; i++) a[i] = (int)ru(4); break;
    case 4: for (size_t i = 0; i < n; i++) a[i] = 7; break;
    case 5: for (size_t i = 0; i < n; i++) a[i] = (int)(i < n / 2 ? i : n - i); break;
    case 6: for (size_t i = 0; i < n; i++) a[i] = (int)i;
            for (size_t i = 0; i < n / 20; i++) a[ru((unsigned)n)] = (int)xr(); break;
    case 7: for (size_t i = 0; i < n; i++) a[i] = (int)(i % 16); break;
    }
}

/* ---- generic pdqsort vs qsort, across element sizes ---- */
static void test_pdqsort_generic(size_t n, int mode, size_t es) {
    if (es < sizeof(int)) es = sizeof(int);
    char *a = (char *)calloc(n ? n : 1, es);
    char *b = (char *)calloc(n ? n : 1, es);
    int *keys = (int *)malloc((n ? n : 1) * sizeof(int));
    if (!a || !b || !keys) { ok(0, "oom pdqsort_generic"); free(a); free(b); free(keys); return; }
    fill_pattern(keys, n, mode);
    for (size_t i = 0; i < n; i++) {
        memcpy(a + i * es, &keys[i], sizeof(int));
        memcpy(b + i * es, &keys[i], sizeof(int));
    }
    neverc_sort_custom(a, n, es, cmp_blob);
    qsort(b, n, es, cmp_blob);
    int good = 1;
    for (size_t i = 0; i < n; i++)
        if (cmp_blob(a + i * es, b + i * es) != 0) { good = 0; break; }
    ok(good, "pdqsort matches qsort");
    free(a); free(b); free(keys);
}

/* ---- timsort: sortedness + stability invariant ---- */
static void test_timsort_stable(size_t n, int mode) {
    pair_t *a = (pair_t *)malloc((n ? n : 1) * sizeof(pair_t));
    int *keys = (int *)malloc((n ? n : 1) * sizeof(int));
    if (!a || !keys) { ok(0, "oom timsort"); free(a); free(keys); return; }
    fill_pattern(keys, n, mode);
    for (size_t i = 0; i < n; i++) { a[i].key = keys[i] % 50; a[i].idx = (int)i; }
    neverc_sort_stable(a, n, sizeof(pair_t), cmp_pair);
    int sorted = 1, stable = 1;
    for (size_t i = 1; i < n; i++) {
        if (a[i - 1].key > a[i].key) sorted = 0;
        if (a[i - 1].key == a[i].key && a[i - 1].idx > a[i].idx) stable = 0;
    }
    ok(sorted, "timsort sorted");
    ok(stable, "timsort stable");
    free(a); free(keys);
}

/* ---- typed int / double vs qsort ---- */
static void test_typed(size_t n, int mode) {
    int *ai = (int *)malloc((n ? n : 1) * sizeof(int));
    int *bi = (int *)malloc((n ? n : 1) * sizeof(int));
    if (!ai || !bi) { ok(0, "oom typed int"); free(ai); free(bi); return; }
    fill_pattern(ai, n, mode); memcpy(bi, ai, n * sizeof(int));
    neverc_sort_ints(ai, n); qsort(bi, n, sizeof(int), cmp_i);
    int gi = 1; for (size_t i = 0; i < n; i++) if (ai[i] != bi[i]) { gi = 0; break; }
    ok(gi, "typed int matches qsort");
    free(ai); free(bi);

    double *ad = (double *)malloc((n ? n : 1) * sizeof(double));
    double *bd = (double *)malloc((n ? n : 1) * sizeof(double));
    int *k = (int *)malloc((n ? n : 1) * sizeof(int));
    if (!ad || !bd || !k) { ok(0, "oom typed dbl"); free(ad); free(bd); free(k); return; }
    fill_pattern(k, n, mode);
    for (size_t i = 0; i < n; i++) ad[i] = k[i] * 0.5;
    memcpy(bd, ad, n * sizeof(double));
    neverc_sort_doubles(ad, n); qsort(bd, n, sizeof(double), cmp_d);
    int gd = 1; for (size_t i = 0; i < n; i++) if (ad[i] != bd[i]) { gd = 0; break; }
    ok(gd, "typed double matches qsort");
    free(ad); free(bd); free(k);
}

/* ---- string sort (multikey quicksort) vs qsort+strcmp ----
 * neverc_sort_strings is a 3-way radix quicksort whose correctness hinges on
 * NUL-vs-byte handling, unsigned-byte ordering, the iterative equal-group
 * descent and the introspective heapsort fallback. Diff it against the libc
 * oracle over shared-prefix, duplicate, empty and high-bit-byte inputs. */
static int cmp_strptr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Build a random NUL-terminated string into buf (cap incl. terminator).
 * mode picks a distribution that stresses a different part of the algorithm. */
static void gen_str(char *buf, size_t cap, int mode) {
    static const char *pre[] = {
        "/usr/local/bin/", "com.example.app.", "https://host/path/", "key_"
    };
    size_t len = 0;
    switch (mode) {
    case 0:                                   /* short, tiny alphabet -> dups */
        len = ru((unsigned)(cap > 6 ? 6 : cap - 1));
        for (size_t i = 0; i < len; i++) buf[i] = (char)('a' + ru(3));
        break;
    case 1: {                                 /* shared long prefix (paths) */
        const char *p = pre[ru(4)];
        size_t pl = strlen(p);
        if (pl >= cap) pl = cap - 1;
        memcpy(buf, p, pl);
        size_t extra = ru((unsigned)(cap - pl - 1 > 8 ? 8 : cap - pl - 1));
        for (size_t i = 0; i < extra; i++) buf[pl + i] = (char)('a' + ru(4));
        len = pl + extra;
        break;
    }
    case 2:                                   /* full-range incl. high bit */
        len = ru((unsigned)(cap > 20 ? 20 : cap - 1));
        for (size_t i = 0; i < len; i++) buf[i] = (char)(1 + ru(255));
        break;
    default:                                  /* often empty / prefix pairs */
        len = ru(3);
        for (size_t i = 0; i < len; i++) buf[i] = (char)('x' + ru(2));
        break;
    }
    buf[len] = '\0';
}

static void test_strsort(size_t n, int mode) {
    const char **a = (const char **)malloc((n ? n : 1) * sizeof(char *));
    const char **b = (const char **)malloc((n ? n : 1) * sizeof(char *));
    char *pool = (char *)malloc((n ? n : 1) * 40);
    if (!a || !b || !pool) { ok(0, "oom strsort"); free(a); free(b); free(pool); return; }
    for (size_t i = 0; i < n; i++) {
        char *s = pool + i * 40;
        gen_str(s, 40, mode);
        a[i] = s; b[i] = s;
    }
    neverc_sort_strings(a, n);
    qsort(b, n, sizeof(char *), cmp_strptr);
    int good = 1;
    for (size_t i = 0; i < n; i++)
        if (strcmp(a[i], b[i]) != 0) { good = 0; break; }
    ok(good, "string sort matches qsort+strcmp");
    free(a); free(b); free(pool);
}

/* ---- substring search vs brute force ---- */
static size_t ref_index(const uint8_t *h, size_t hl, const uint8_t *n, size_t nl) {
    if (nl == 0) return 0;
    if (nl > hl) return (size_t)-1;
    for (size_t i = 0; i + nl <= hl; i++) if (memcmp(h + i, n, nl) == 0) return i;
    return (size_t)-1;
}
static size_t ref_last(const uint8_t *h, size_t hl, const uint8_t *n, size_t nl) {
    if (nl == 0) return hl;
    if (nl > hl) return (size_t)-1;
    for (size_t i = hl - nl + 1; i > 0;) { i--; if (memcmp(h + i, n, nl) == 0) return i; }
    return (size_t)-1;
}
static void test_ss(const uint8_t *h, size_t hl, const uint8_t *n, size_t nl) {
    ok(neverc_bytes_index(h, hl, n, nl) == ref_index(h, hl, n, nl), "bytes_index matches brute");
    ok(neverc_bytes_last_index(h, hl, n, nl) == ref_last(h, hl, n, nl), "bytes_last_index matches brute");
}

static void fuzz_strsearch(void) {
    static uint8_t h[1400];
    static uint8_t n[320];
    int alpha[] = {2, 3, 4, 26};
    for (int it = 0; it < 30000; it++) {
        size_t hl = ru(1400);
        int al = alpha[ru(4)];
        for (size_t i = 0; i < hl; i++) h[i] = (uint8_t)('a' + ru((unsigned)al));
        size_t nl = ru(70);
        int mk = (int)ru(3);
        if (mk == 0) {                       /* random needle (mostly misses) */
            for (size_t i = 0; i < nl; i++) n[i] = (uint8_t)('a' + ru((unsigned)al));
        } else if (mk == 1 && hl > 0) {      /* real substring -> guaranteed hit */
            if (nl > hl) nl = hl;
            size_t off = ru((unsigned)(hl - nl + 1));
            memcpy(n, h + off, nl);
        } else {                             /* periodic/adversarial a^(nl-1)b */
            for (size_t i = 0; i < nl; i++) n[i] = 'a';
            if (nl > 1) n[nl - 1] = 'b';
        }
        test_ss(h, hl, n, nl);
    }
    /* textbook O(n*m) trap: a^k haystack, a^(m-1)b needle (BMH -> Two-Way) */
    memset(h, 'a', sizeof(h));
    for (int m = 2; m <= 300; m += 31) {
        memset(n, 'a', (size_t)m);
        n[m - 1] = 'b'; test_ss(h, sizeof(h), n, (size_t)m);   /* miss */
        n[m - 1] = 'a'; test_ss(h, sizeof(h), n, (size_t)m);   /* hit  */
    }
}

int main(void) {
    printf("=== sort/search differential fuzz ===\n");

    size_t sizes[] = {0, 1, 2, 3, 5, 16, 17, 24, 25, 31, 32, 33, 63, 64,
                      127, 128, 129, 255, 257, 1000, 5000};
    size_t esz[] = {4, 8, 24, 300};   /* 300 exercises the >256 malloc tmp path */
    int nsizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int nes = (int)(sizeof(esz) / sizeof(esz[0]));

    for (int rep = 0; rep < 4; rep++)
        for (int s = 0; s < nsizes; s++)
            for (int mode = 0; mode < 8; mode++) {
                test_typed(sizes[s], mode);
                test_timsort_stable(sizes[s], mode);
                for (int e = 0; e < nes; e++)
                    test_pdqsort_generic(sizes[s], mode, esz[e]);
            }
    printf("sort battery done\n");

    for (int rep = 0; rep < 6; rep++)
        for (int s = 0; s < nsizes; s++)
            for (int mode = 0; mode < 4; mode++)
                test_strsort(sizes[s], mode);
    printf("string-sort battery done\n");

    fuzz_strsearch();
    printf("strsearch battery done\n");

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
