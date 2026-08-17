/*
 * NeverC unified substring search engine.
 *
 * Shared by bytes.c and cstring.c, which previously each carried their own copy
 * of plain Boyer-Moore-Horspool (O(n*m) worst case on periodic/adversarial
 * inputs). This engine keeps the introsort structure: a fast average-case
 * workhorse plus a worst-case-linear guard.
 *
 * Forward search (nci_ss_index / nci_ss_finder):
 *   1. memchr to the first occurrence of needle[0] — a rare or absent first
 *      byte collapses to a single SIMD scan (the big win on real text).
 *   2. Boyer-Moore-Horspool from there — tight last-byte-skip loop, fast on
 *      every alphabet (so no regression versus the old code).
 *   3. Introspective Two-Way (Crochemore-Perrin) fallback — when deep
 *      verifications exceed a work budget (periodic/adversarial needles), the
 *      remainder runs in guaranteed O(n+m), the way introsort drops from
 *      quicksort to heapsort.
 *
 * Reverse search (nci_ss_last_index): bounded brute force for short needles,
 * Two-Way over a virtually-reversed haystack for long ones (also O(n+m)).
 *
 * All functions are static so each translation unit gets its own copy with no
 * link-time conflicts (same pattern as sort_impl.h).
 */

#ifndef NEVERC_STD_STRSEARCH_H
#define NEVERC_STD_STRSEARCH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* Needles up to this length use the cache-friendly brute force path. Longer
 * needles use Two-Way so the worst case can never become quadratic. */
#define NCI_SS_SHORT     16
/* Haystacks at or below this size skip Two-Way preprocessing entirely. */
#define NCI_SS_SMALLHAY  64
/* Reversed-needle stack buffer for last_index; larger needles malloc. */
#define NCI_SS_RBUF      256

/* ------------------------------------------------------------------ *
 * Two-Way preprocessing
 * ------------------------------------------------------------------ */

typedef struct {
    size_t suffix;      /* critical factorization split point */
    size_t period;      /* period used for shifting during search */
    int    periodic;    /* needle[0,suffix) == needle[period,period+suffix) */
    size_t shift[256];  /* Boyer-Moore bad-character (last byte) shift table */
} nci_ss_pp_t;

/*
 * Critical factorization via the maximal-suffix computation under both the
 * '<' and '>' orderings (Crochemore-Perrin). Returns the split point
 * (suffix length) and writes the corresponding period.
 *
 * SIZE_MAX is used as the "-1" sentinel for the current maximal-suffix start;
 * unsigned wraparound makes x[SIZE_MAX + k] == x[k-1] and j - SIZE_MAX == j+1,
 * exactly the signed arithmetic the algorithm calls for.
 */
static size_t nci_ss_crit_fact(const uint8_t *x, size_t m, size_t *period) {
    size_t ms1, ms2, p1, p2, j, k, p;

    ms1 = SIZE_MAX; j = 0; k = 1; p = 1;
    while (j + k < m) {
        uint8_t a = x[j + k];
        uint8_t b = x[ms1 + k];
        if (a < b) { j += k; k = 1; p = j - ms1; }
        else if (a == b) { if (k != p) k++; else { j += p; k = 1; } }
        else { ms1 = j; j = ms1 + 1; k = 1; p = 1; }
    }
    p1 = p;

    ms2 = SIZE_MAX; j = 0; k = 1; p = 1;
    while (j + k < m) {
        uint8_t a = x[j + k];
        uint8_t b = x[ms2 + k];
        if (b < a) { j += k; k = 1; p = j - ms2; }
        else if (a == b) { if (k != p) k++; else { j += p; k = 1; } }
        else { ms2 = j; j = ms2 + 1; k = 1; p = 1; }
    }
    p2 = p;

    /* Compare as (index + 1) so the SIZE_MAX sentinel folds to 0. */
    if (ms2 + 1 < ms1 + 1) { *period = p1; return ms1 + 1; }
    *period = p2;
    return ms2 + 1;
}

static void nci_ss_pp_init(nci_ss_pp_t *pp, const uint8_t *n, size_t nlen) {
    size_t period;
    size_t suffix = nci_ss_crit_fact(n, nlen, &period);
    pp->periodic = (suffix == 0) ||
        (period <= nlen && suffix <= nlen - period &&
         memcmp(n, n + period, suffix) == 0);
    if (!pp->periodic) {
        size_t tail = nlen - suffix;
        period = (suffix > tail ? suffix : tail) + 1;
    }
    pp->suffix = suffix;
    pp->period = period;
    for (int i = 0; i < 256; i++) pp->shift[i] = nlen;
    for (size_t i = 0; i < nlen; i++) pp->shift[n[i]] = nlen - 1 - i;
}

/* ------------------------------------------------------------------ *
 * Two-Way search (forward)
 * ------------------------------------------------------------------ */

static size_t nci_ss_two_way(const uint8_t *h, size_t hlen,
                             const uint8_t *n, size_t nlen,
                             const nci_ss_pp_t *pp) {
    size_t suffix = pp->suffix, period = pp->period;
    const size_t *shift = pp->shift;
    size_t last = hlen - nlen;
    size_t j = 0;

    if (pp->periodic) {
        size_t memory = 0;
        while (j <= last) {
            size_t s = shift[h[j + nlen - 1]];
            if (s) {
                if (memory && s < period) s = nlen - period;
                memory = 0;
                j += s;
                continue;
            }
            size_t i = (suffix > memory) ? suffix : memory;
            while (i < nlen && n[i] == h[j + i]) i++;
            if (i >= nlen) {
                size_t k = suffix;
                while (k > memory && n[k - 1] == h[j + k - 1]) k--;
                if (k <= memory) return j;
                j += period;
                memory = nlen - period;
            } else {
                j += i - suffix + 1;
                memory = 0;
            }
        }
    } else {
        while (j <= last) {
            size_t s = shift[h[j + nlen - 1]];
            if (s) { j += s; continue; }
            size_t i = suffix;
            while (i < nlen && n[i] == h[j + i]) i++;
            if (i >= nlen) {
                size_t k = suffix;
                while (k > 0 && n[k - 1] == h[j + k - 1]) k--;
                if (k == 0) return j;
                j += period;
            } else {
                j += i - suffix + 1;
            }
        }
    }
    return SIZE_MAX;
}

/* Two-Way over a virtually-reversed haystack: returns the rightmost match.
 * `rn` is the already-reversed needle; pp is built from rn. A virtual match at
 * virtual offset j maps back to real start hlen - j - nlen, so the leftmost
 * virtual match is the rightmost real match. */
static size_t nci_ss_two_way_rev(const uint8_t *h, size_t hlen,
                                 const uint8_t *rn, size_t nlen,
                                 const nci_ss_pp_t *pp) {
    size_t suffix = pp->suffix, period = pp->period;
    const size_t *shift = pp->shift;
    size_t last = hlen - nlen;
    size_t j = 0;
#define NCI_RH(idx) (h[hlen - 1 - (idx)])

    if (pp->periodic) {
        size_t memory = 0;
        while (j <= last) {
            size_t s = shift[NCI_RH(j + nlen - 1)];
            if (s) {
                if (memory && s < period) s = nlen - period;
                memory = 0;
                j += s;
                continue;
            }
            size_t i = (suffix > memory) ? suffix : memory;
            while (i < nlen && rn[i] == NCI_RH(j + i)) i++;
            if (i >= nlen) {
                size_t k = suffix;
                while (k > memory && rn[k - 1] == NCI_RH(j + k - 1)) k--;
                if (k <= memory) return hlen - j - nlen;
                j += period;
                memory = nlen - period;
            } else {
                j += i - suffix + 1;
                memory = 0;
            }
        }
    } else {
        while (j <= last) {
            size_t s = shift[NCI_RH(j + nlen - 1)];
            if (s) { j += s; continue; }
            size_t i = suffix;
            while (i < nlen && rn[i] == NCI_RH(j + i)) i++;
            if (i >= nlen) {
                size_t k = suffix;
                while (k > 0 && rn[k - 1] == NCI_RH(j + k - 1)) k--;
                if (k == 0) return hlen - j - nlen;
                j += period;
            } else {
                j += i - suffix + 1;
            }
        }
    }
#undef NCI_RH
    return SIZE_MAX;
}

/* ------------------------------------------------------------------ *
 * Boyer-Moore-Horspool with an introspective Two-Way fallback (forward)
 *
 * Structure mirrors introsort: a fast average-case workhorse (BMH, like
 * quicksort) plus a worst-case-linear guard (Two-Way, like heapsort). BMH's
 * tight last-byte-skip loop is fast on every alphabet (no regression vs the
 * old code), and a verification-work budget detects adversarial O(n*m) inputs
 * (periodic needles) and finishes them with Two-Way, bounding the total at
 * O(n+m). Callers first jump to the first occurrence of needle[0] with memchr
 * (SIMD), which turns a rare/absent first byte into a single linear scan.
 * ------------------------------------------------------------------ */

#ifndef NCI_SS_BUDGET_MULT
#define NCI_SS_BUDGET_MULT 4
#endif
#ifndef NCI_SS_BUDGET_BASE
#define NCI_SS_BUDGET_BASE 64
#endif

static void nci_ss_bmh_skip(size_t skip[256], const uint8_t *n, size_t nlen) {
    for (int c = 0; c < 256; c++) skip[c] = nlen;
    for (size_t i = 0; i < nlen - 1; i++) skip[n[i]] = nlen - 1 - i;
}

static size_t nci_ss_bmh(const uint8_t *h, size_t hlen,
                         const uint8_t *n, size_t nlen,
                         const size_t skip[256], size_t start) {
    size_t last = hlen - nlen;
    uint8_t lastc = n[nlen - 1];
    /* Scalar-probe a few bytes (cheap early-exit + accurate budgeting on random
     * text) then SIMD memcmp the tail (fast when the match runs deep). */
    size_t pr = (nlen - 1 < 16) ? (nlen - 1) : 16;
    size_t budget = (size_t)NCI_SS_BUDGET_MULT * hlen
                  + (size_t)NCI_SS_BUDGET_BASE * nlen;
    size_t pos = start;

    while (pos <= last) {
        uint8_t c = h[pos + nlen - 1];
        if (c == lastc) {
            size_t k = 0;
            while (k < pr && n[k] == h[pos + k]) k++;
            if (k == pr) {
                /* Probe matched; confirm the tail. Only these "deep" verifies
                 * can compound into O(n*m), so the budget is charged here only
                 * — shallow mismatches (k < pr, bounded by 16 each) keep the
                 * hot path at plain-BMH speed with no bookkeeping. */
                size_t rest = (nlen - 1) - pr;
                if (rest == 0 || memcmp(h + pos + pr, n + pr, rest) == 0)
                    return pos;
                if (budget <= nlen) {
                    /* Adversarial — switch to worst-case-linear Two-Way. */
                    nci_ss_pp_t pp;
                    nci_ss_pp_init(&pp, n, nlen);
                    size_t r = nci_ss_two_way(h + pos, hlen - pos, n, nlen, &pp);
                    return (r == SIZE_MAX) ? SIZE_MAX : pos + r;
                }
                budget -= nlen;
            }
        }
        pos += skip[c];
    }
    return SIZE_MAX;
}

static size_t nci_ss_brute_rev(const uint8_t *h, size_t hlen,
                               const uint8_t *n, size_t nlen) {
    uint8_t c0 = n[0];
    size_t i = hlen - nlen + 1;
    while (i > 0) {
        i--;
        if (h[i] == c0 && memcmp(h + i + 1, n + 1, nlen - 1) == 0) return i;
    }
    return SIZE_MAX;
}

/* ------------------------------------------------------------------ *
 * One-shot public entry points
 * ------------------------------------------------------------------ */

static size_t nci_ss_index(const uint8_t *h, size_t hlen,
                           const uint8_t *n, size_t nlen) {
    if (nlen == 0) return 0;
    if (nlen > hlen || !h || !n) return SIZE_MAX;
    if (nlen == 1) {
        const uint8_t *p = (const uint8_t *)memchr(h, n[0], hlen);
        return p ? (size_t)(p - h) : SIZE_MAX;
    }
    if (nlen == hlen) return memcmp(h, n, hlen) == 0 ? 0 : SIZE_MAX;
    /* Jump to the first occurrence of needle[0]: a rare/absent first byte
     * collapses to a single SIMD memchr instead of a full BMH walk. */
    const uint8_t *p = (const uint8_t *)memchr(h, n[0], hlen - nlen + 1);
    if (!p) return SIZE_MAX;
    size_t skip[256];
    nci_ss_bmh_skip(skip, n, nlen);
    return nci_ss_bmh(h, hlen, n, nlen, skip, (size_t)(p - h));
}

static size_t nci_ss_last_index(const uint8_t *h, size_t hlen,
                                const uint8_t *n, size_t nlen) {
    if (nlen == 0) return hlen;
    if (nlen > hlen || !h || !n) return SIZE_MAX;
    if (nlen == 1) {
        size_t i = hlen;
        while (i > 0) { i--; if (h[i] == n[0]) return i; }
        return SIZE_MAX;
    }
    if (nlen == hlen) return memcmp(h, n, hlen) == 0 ? 0 : SIZE_MAX;
    if (nlen <= NCI_SS_SHORT || hlen <= NCI_SS_SMALLHAY)
        return nci_ss_brute_rev(h, hlen, n, nlen);

    uint8_t stackbuf[NCI_SS_RBUF];
    uint8_t *rn = (nlen <= NCI_SS_RBUF) ? stackbuf : (uint8_t *)malloc(nlen);
    if (!rn) return nci_ss_brute_rev(h, hlen, n, nlen);
    for (size_t i = 0; i < nlen; i++) rn[i] = n[nlen - 1 - i];
    nci_ss_pp_t pp;
    nci_ss_pp_init(&pp, rn, nlen);
    size_t res = nci_ss_two_way_rev(h, hlen, rn, nlen, &pp);
    if (rn != stackbuf) free(rn);
    return res;
}

/* ------------------------------------------------------------------ *
 * Reusable finder — preprocess once, search a haystack repeatedly.
 * Used by count / split / replace so the shift table and factorization are
 * built a single time across all matches.
 * ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *needle;
    size_t nlen;
    size_t skip[256];       /* BMH skip table, built once (nlen >= 2) */
} nci_ss_finder_t;

static void nci_ss_finder_init(nci_ss_finder_t *f,
                               const uint8_t *n, size_t nlen) {
    f->needle = n;
    f->nlen = nlen;
    if (nlen >= 2) nci_ss_bmh_skip(f->skip, n, nlen);
}

static size_t nci_ss_finder_next(const nci_ss_finder_t *f,
                                 const uint8_t *h, size_t hlen) {
    size_t nlen = f->nlen;
    if (nlen == 0) return 0;
    if (nlen > hlen || !h || !f->needle) return SIZE_MAX;
    if (nlen == 1) {
        const uint8_t *p = (const uint8_t *)memchr(h, f->needle[0], hlen);
        return p ? (size_t)(p - h) : SIZE_MAX;
    }
    const uint8_t *p = (const uint8_t *)memchr(h, f->needle[0], hlen - nlen + 1);
    if (!p) return SIZE_MAX;
    return nci_ss_bmh(h, hlen, f->needle, nlen, f->skip, (size_t)(p - h));
}

#endif /* NEVERC_STD_STRSEARCH_H */
