#include "neverc/std/index/suffixarray.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * SA-IS: O(n) suffix array construction.
 * Based on Nong, Zhang & Chan (2009) "Two Efficient Algorithms for
 * Linear Time Suffix Array Construction".
 */

#define SAIS_TP(i) ((st[(i)>>3]>>((i)&7))&1)
#define SAIS_SET(i) (st[(i)>>3] |= (uint8_t)(1<<((i)&7)))
#define SAIS_LMS(i) ((i)>0 && SAIS_TP(i) && !SAIS_TP((i)-1))

static int sais_core(int32_t *T, int32_t *SA, int32_t n, int32_t K) {
    if (n <= 1) { if (n == 1) SA[0] = 0; return 0; }
    if (n == 2) {
        SA[0] = (T[0] < T[1]) ? 0 : 1;
        SA[1] = 1 - SA[0];
        return 0;
    }

    uint8_t *st = (uint8_t *)calloc(((size_t)n + 7u) >> 3, 1);
    int32_t *bkt = (int32_t *)calloc((size_t)K, sizeof(int32_t));
    int32_t *cur = (int32_t *)malloc((size_t)K * sizeof(int32_t));
    if (!st || !bkt || !cur) {
        free(st); free(bkt); free(cur);
        return -1;
    }
    SAIS_SET(n - 1);
    for (int32_t i = n - 2; i >= 0; i--)
        if (T[i] < T[i+1] || (T[i] == T[i+1] && SAIS_TP(i+1)))
            SAIS_SET(i);

    for (int32_t i = 0; i < n; i++) bkt[T[i]]++;

#define BUCKET_ENDS   { int32_t s=0; for(int32_t c=0;c<K;c++){s+=bkt[c];cur[c]=s;} }
#define BUCKET_STARTS { int32_t s=0; for(int32_t c=0;c<K;c++){cur[c]=s;s+=bkt[c];} }

    /* --- Step 1: sort LMS substrings via induced sort --- */
    for (int32_t i = 0; i < n; i++) SA[i] = -1;
    BUCKET_ENDS;
    for (int32_t i = n - 1; i >= 1; i--)
        if (SAIS_LMS(i)) SA[--cur[T[i]]] = i;

    BUCKET_STARTS;
    for (int32_t i = 0; i < n; i++) {
        if (SA[i] <= 0) continue;
        int32_t j = SA[i] - 1;
        if (!SAIS_TP(j)) SA[cur[T[j]]++] = j;
    }
    BUCKET_ENDS;
    for (int32_t i = n - 1; i >= 0; i--) {
        if (SA[i] <= 0) continue;
        int32_t j = SA[i] - 1;
        if (SAIS_TP(j)) SA[--cur[T[j]]] = j;
    }

    /* --- Step 2: name sorted LMS substrings --- */
    int32_t m = 0;
    for (int32_t i = 0; i < n; i++)
        if (SAIS_LMS(SA[i])) SA[m++] = SA[i];
    for (int32_t i = m; i < n; i++) SA[i] = -1;

    int32_t name = 0, prev = -1;
    for (int32_t i = 0; i < m; i++) {
        int32_t pos = SA[i];
        int diff = 1;
        if (prev >= 0) {
            diff = 0;
            for (int32_t d = 0; ; d++) {
                if (pos + d >= n || prev + d >= n ||
                    T[pos+d] != T[prev+d] || SAIS_TP(pos+d) != SAIS_TP(prev+d)) {
                    diff = 1; break;
                }
                if (d > 0 && (SAIS_LMS(pos+d) || SAIS_LMS(prev+d)))
                    break;
            }
        }
        if (diff) name++;
        prev = pos;
        SA[m + (pos >> 1)] = name - 1;
    }
    { int32_t j = n - 1;
      for (int32_t i = n - 1; i >= m; i--)
          if (SA[i] >= 0) SA[j--] = SA[i]; }

    /* --- Step 3: recurse or directly compute --- */
    int32_t *s1 = SA + n - m;
    if (name < m) {
        if (sais_core(s1, SA, m, name) != 0) {
            free(st); free(bkt); free(cur);
            return -1;
        }
    }
    else
        for (int32_t i = 0; i < m; i++) SA[s1[i]] = i;

    /* --- Step 4: final induced sort --- */
    { int32_t k = 0;
      for (int32_t i = 1; i < n; i++)
          if (SAIS_LMS(i)) s1[k++] = i; }
    for (int32_t i = 0; i < m; i++) SA[i] = s1[SA[i]];
    for (int32_t i = m; i < n; i++) SA[i] = -1;

    BUCKET_ENDS;
    for (int32_t i = m - 1; i >= 0; i--) {
        int32_t p = SA[i]; SA[i] = -1;
        SA[--cur[T[p]]] = p;
    }
    BUCKET_STARTS;
    for (int32_t i = 0; i < n; i++) {
        if (SA[i] <= 0) continue;
        int32_t j = SA[i] - 1;
        if (!SAIS_TP(j)) SA[cur[T[j]]++] = j;
    }
    BUCKET_ENDS;
    for (int32_t i = n - 1; i >= 0; i--) {
        if (SA[i] <= 0) continue;
        int32_t j = SA[i] - 1;
        if (SAIS_TP(j)) SA[--cur[T[j]]] = j;
    }

    free(st); free(bkt); free(cur);
#undef BUCKET_ENDS
#undef BUCKET_STARTS
    return 0;
}

#undef SAIS_TP
#undef SAIS_SET
#undef SAIS_LMS

static int build_suffix_array(const unsigned char *data, size_t n, int32_t *sa) {
    if (n == 0) return 0;
    if (n == 1) { sa[0] = 0; return 0; }

    int32_t n1 = (int32_t)(n + 1);
    int32_t *T  = (int32_t *)malloc((size_t)n1 * sizeof(int32_t));
    int32_t *SA = (int32_t *)malloc((size_t)n1 * sizeof(int32_t));
    if (!T || !SA) {
        free(T); free(SA);
        return -1;
    }
    for (size_t i = 0; i < n; i++) T[i] = (int32_t)data[i] + 1;
    T[n] = 0;

    if (sais_core(T, SA, n1, 257) != 0) {
        free(T); free(SA);
        return -1;
    }

    for (int32_t i = 1; i < n1; i++) sa[i - 1] = SA[i];
    free(T);
    free(SA);
    return 0;
}

/* Recursively fill llcp[mid]/rlcp[mid] for every binary-search interval [lo,hi];
 * returns lcp(SA[lo], SA[hi]) using the adjacent-LCP array (lcp[i]=lcp(SA[i-1],SA[i])).
 * Depth is O(log n) and total work is O(n) — one visit per interval midpoint. */
static int32_t build_lcp_lr(const int32_t *lcp, int32_t lo, int32_t hi,
                            int32_t *llcp, int32_t *rlcp) {
    if (lo + 1 == hi) return lcp[hi];
    int32_t mid = lo + (hi - lo) / 2;
    int32_t L = build_lcp_lr(lcp, lo, mid, llcp, rlcp);
    int32_t R = build_lcp_lr(lcp, mid, hi, llcp, rlcp);
    llcp[mid] = L;
    rlcp[mid] = R;
    return L < R ? L : R;
}

/* Build the LCP-LR arrays for O(m + log n) search. Adjacent LCPs come from
 * Kasai's O(n) algorithm. On allocation failure leaves idx->llcp/rlcp NULL, so
 * queries transparently fall back to the plain (correct, slower) search. */
static void build_search_index(neverc_suffixarray_t *idx) {
    size_t n = idx->sa_len;
    if (n < 2) return;                       /* trivial; plain path handles it */

    const unsigned char *data = idx->data;
    const int32_t *sa = idx->sa;
    int32_t *rank = (int32_t *)malloc(n * sizeof(int32_t));
    int32_t *lcp  = (int32_t *)calloc(n, sizeof(int32_t));
    int32_t *llcp = (int32_t *)calloc(n, sizeof(int32_t));
    int32_t *rlcp = (int32_t *)calloc(n, sizeof(int32_t));
    if (!rank || !lcp || !llcp || !rlcp) {
        free(rank); free(lcp); free(llcp); free(rlcp);
        return;
    }

    for (size_t i = 0; i < n; i++) rank[sa[i]] = (int32_t)i;

    /* Kasai: lcp[rank[i]] = lcp(suffix SA[rank[i]-1], suffix i). */
    size_t h = 0;
    for (size_t i = 0; i < n; i++) {
        if (rank[i] > 0) {
            size_t k = (size_t)sa[rank[i] - 1];
            while (i + h < n && k + h < n && data[i + h] == data[k + h]) h++;
            lcp[rank[i]] = (int32_t)h;
            if (h > 0) h--;
        } else {
            h = 0;
        }
    }
    lcp[0] = 0;

    build_lcp_lr(lcp, 0, (int32_t)n - 1, llcp, rlcp);
    free(rank);
    free(lcp);
    idx->llcp = llcp;
    idx->rlcp = rlcp;
}

int neverc_suffixarray_new(neverc_suffixarray_t *idx, const unsigned char *data, size_t len) {
    if (!idx) return -1;
    memset(idx, 0, sizeof(*idx));
    if (len == 0) return 0;
    if (!data || len > (size_t)INT32_MAX - 1U ||
        len > SIZE_MAX / sizeof(int32_t))
        return -1;

    idx->data = data;
    idx->data_len = len;
    idx->sa_len = len;
    idx->sa = (int32_t *)malloc(len * sizeof(int32_t));
    if (!idx->sa) return -1;

    if (build_suffix_array(data, len, idx->sa) != 0) {
        free(idx->sa);
        memset(idx, 0, sizeof(*idx));
        return -1;
    }
    build_search_index(idx);
    return 0;
}

void neverc_suffixarray_free(neverc_suffixarray_t *idx) {
    if (idx->sa)   { free(idx->sa);   idx->sa   = NULL; }
    if (idx->llcp) { free(idx->llcp); idx->llcp = NULL; }
    if (idx->rlcp) { free(idx->rlcp); idx->rlcp = NULL; }
    idx->data = NULL;
    idx->data_len = 0;
    idx->sa_len = 0;
}

/* ------------------------------------------------------------------ *
 * LCP-LR accelerated pattern search (Manber & Myers, 1993).
 *
 * Plain binary search re-compares the pattern from byte 0 at every probe, so
 * each of the log n probes costs up to m bytes -> O(m log n) (this is what Go's
 * suffixarray and the previous NeverC code do). The LCP-LR scheme precomputes,
 * for every binary-search midpoint M of interval [L,R], the values
 * llcp[M] = lcp(SA[L], SA[M]) and rlcp[M] = lcp(SA[M], SA[R]). During the search
 * we carry l = lcp(P, SA[L]) and r = lcp(P, SA[R]); comparing llcp[M]/rlcp[M]
 * against l/r either decides the branch with zero character comparisons or lets
 * us resume the comparison from max(l, r). The matched prefix only ever grows,
 * so the whole search costs O(m + log n) characters — and because it never
 * re-scans a shared prefix, it also wins on adversarial low-entropy inputs
 * (all-equal / periodic text) where a compare-from-zero search degrades.
 * ------------------------------------------------------------------ */

enum { SA_LESS = -1, SA_PREFIX = 0, SA_GREATER = 1 };

/* Compare suffix at sa_pos against P, assuming P[0..start) already matches.
 * Returns the suffix's relation to P and writes the matched length to *ml. */
static int sa_cmp(const unsigned char *data, size_t dlen, int32_t sa_pos,
                  const unsigned char *P, size_t m, size_t start, size_t *ml) {
    size_t sp = (size_t)sa_pos;
    size_t i = start;
    while (i < m && sp + i < dlen && data[sp + i] == P[i]) i++;
    *ml = i;
    if (i == m)        return SA_PREFIX;   /* P is a prefix of the suffix */
    if (sp + i == dlen) return SA_LESS;    /* suffix ended first -> suffix < P */
    return data[sp + i] < P[i] ? SA_LESS : SA_GREATER;
}

/* A suffix counts as "below the answer": strictly-less always; a P-prefix match
 * counts as below only when seeking the upper bound (first suffix strictly > P). */
static inline int sa_is_less(int rel, int want_upper) {
    if (rel == SA_LESS)    return 1;
    if (rel == SA_GREATER) return 0;
    return want_upper;                     /* SA_PREFIX */
}

/* Plain compare-from-zero binary search, used only when LCP-LR is unavailable
 * (n < 2, or the LCP-LR arrays could not be allocated). Insertion point in [0,n]. */
static int32_t sa_bound_plain(const neverc_suffixarray_t *idx,
                              const unsigned char *P, size_t m, int want_upper) {
    int32_t lo = 0, hi = (int32_t)idx->sa_len;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        size_t ml;
        int rel = sa_cmp(idx->data, idx->data_len, idx->sa[mid], P, m, 0, &ml);
        if (sa_is_less(rel, want_upper)) lo = mid + 1;
        else                             hi = mid;
    }
    return lo;
}

/* want_upper == 0 -> leftmost index whose suffix is >= P (a P-prefix counts).
 * want_upper == 1 -> leftmost index whose suffix is  > P. count = upper - lower. */
static int32_t sa_bound(const neverc_suffixarray_t *idx,
                        const unsigned char *P, size_t m, int want_upper) {
    int32_t n = (int32_t)idx->sa_len;
    if (n == 0) return 0;
    if (!idx->llcp || !idx->rlcp) return sa_bound_plain(idx, P, m, want_upper);

    const unsigned char *data = idx->data;
    size_t dlen = idx->data_len;
    const int32_t *llcp = idx->llcp, *rlcp = idx->rlcp;
    size_t l, r, ml;

    int rel0 = sa_cmp(data, dlen, idx->sa[0], P, m, 0, &l);
    if (!sa_is_less(rel0, want_upper)) return 0;          /* SA[0] already >= answer */
    int reln = sa_cmp(data, dlen, idx->sa[n - 1], P, m, 0, &r);
    if (sa_is_less(reln, want_upper)) return n;           /* every suffix is below */

    /* Invariant: SA[lo] is below the answer, SA[hi] is at/above it. */
    int32_t lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int32_t mid = lo + (hi - lo) / 2;
        int rel;
        if (l >= r) {
            if ((size_t)llcp[mid] >= l)
                rel = sa_cmp(data, dlen, idx->sa[mid], P, m, l, &ml);
            else { rel = SA_GREATER; ml = (size_t)llcp[mid]; }  /* SA[mid] > P, decided */
        } else {
            if ((size_t)rlcp[mid] >= r)
                rel = sa_cmp(data, dlen, idx->sa[mid], P, m, r, &ml);
            else { rel = SA_LESS; ml = (size_t)rlcp[mid]; }     /* SA[mid] < P, decided */
        }
        if (sa_is_less(rel, want_upper)) { lo = mid; l = ml; }
        else                             { hi = mid; r = ml; }
    }
    return hi;
}

int neverc_suffixarray_lookup(const neverc_suffixarray_t *idx,
                               const unsigned char *pattern, size_t pat_len,
                               int32_t *results, size_t max_results,
                               size_t *nresults) {
    if (!idx->sa || idx->sa_len == 0 || pat_len == 0) {
        if (nresults) *nresults = 0;
        return 0;
    }

    int32_t lo = sa_bound(idx, pattern, pat_len, 0);
    int32_t hi = sa_bound(idx, pattern, pat_len, 1);
    size_t count = (size_t)(hi - lo);
    size_t copy = count < max_results ? count : max_results;
    if (copy > 0 && !results) return -1;

    for (size_t i = 0; i < copy; i++)
        results[i] = idx->sa[lo + (int32_t)i];

    if (nresults) *nresults = count;
    return 0;
}

size_t neverc_suffixarray_count(const neverc_suffixarray_t *idx,
                                const unsigned char *pattern, size_t pat_len) {
    if (!idx->sa || idx->sa_len == 0 || pat_len == 0) return 0;
    int32_t lo = sa_bound(idx, pattern, pat_len, 0);
    int32_t hi = sa_bound(idx, pattern, pat_len, 1);
    return (size_t)(hi - lo);
}

int neverc_suffixarray_at(const neverc_suffixarray_t *idx, size_t i) {
    if (i >= idx->sa_len) return -1;
    return idx->sa[i];
}
