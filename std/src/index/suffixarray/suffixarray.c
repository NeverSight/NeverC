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

static void sais_core(int32_t *T, int32_t *SA, int32_t n, int32_t K) {
    if (n <= 1) { if (n == 1) SA[0] = 0; return; }
    if (n == 2) {
        SA[0] = (T[0] < T[1]) ? 0 : 1;
        SA[1] = 1 - SA[0];
        return;
    }

    uint8_t *st = (uint8_t *)calloc(((size_t)n + 7u) >> 3, 1);
    SAIS_SET(n - 1);
    for (int32_t i = n - 2; i >= 0; i--)
        if (T[i] < T[i+1] || (T[i] == T[i+1] && SAIS_TP(i+1)))
            SAIS_SET(i);

    int32_t *bkt = (int32_t *)calloc((size_t)K, sizeof(int32_t));
    for (int32_t i = 0; i < n; i++) bkt[T[i]]++;
    int32_t *cur = (int32_t *)malloc((size_t)K * sizeof(int32_t));

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
    if (name < m)
        sais_core(s1, SA, m, name);
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
}

#undef SAIS_TP
#undef SAIS_SET
#undef SAIS_LMS

static void build_suffix_array(const unsigned char *data, size_t n, int32_t *sa) {
    if (n == 0) return;
    if (n == 1) { sa[0] = 0; return; }

    int32_t n1 = (int32_t)(n + 1);
    int32_t *T  = (int32_t *)malloc((size_t)n1 * sizeof(int32_t));
    int32_t *SA = (int32_t *)malloc((size_t)n1 * sizeof(int32_t));
    for (size_t i = 0; i < n; i++) T[i] = (int32_t)data[i] + 1;
    T[n] = 0;

    sais_core(T, SA, n1, 257);

    for (int32_t i = 1; i < n1; i++) sa[i - 1] = SA[i];
    free(T);
    free(SA);
}

int neverc_suffixarray_new(neverc_suffixarray_t *idx, const unsigned char *data, size_t len) {
    memset(idx, 0, sizeof(*idx));
    if (len == 0) return 0;

    idx->data = data;
    idx->data_len = len;
    idx->sa_len = len;
    idx->sa = (int32_t *)malloc(len * sizeof(int32_t));
    if (!idx->sa) return -1;

    build_suffix_array(data, len, idx->sa);
    return 0;
}

void neverc_suffixarray_free(neverc_suffixarray_t *idx) {
    if (idx->sa) { free(idx->sa); idx->sa = NULL; }
    idx->data = NULL;
    idx->data_len = 0;
    idx->sa_len = 0;
}

static int suffix_cmp(const unsigned char *data, size_t dlen, int32_t pos,
                       const unsigned char *pat, size_t plen) {
    size_t rem = dlen - (size_t)pos;
    size_t clen = rem < plen ? rem : plen;
    int r = memcmp(data + pos, pat, clen);
    if (r != 0) return r;
    if (clen < plen) return -1;
    return 0;
}

static int32_t lower_bound(const neverc_suffixarray_t *idx,
                            const unsigned char *pat, size_t plen) {
    int32_t lo = 0, hi = (int32_t)idx->sa_len;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (suffix_cmp(idx->data, idx->data_len, idx->sa[mid], pat, plen) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int32_t upper_bound(const neverc_suffixarray_t *idx,
                            const unsigned char *pat, size_t plen) {
    int32_t lo = 0, hi = (int32_t)idx->sa_len;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (suffix_cmp(idx->data, idx->data_len, idx->sa[mid], pat, plen) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int neverc_suffixarray_lookup(const neverc_suffixarray_t *idx,
                               const unsigned char *pattern, size_t pat_len,
                               int32_t *results, size_t max_results,
                               size_t *nresults) {
    if (!idx->sa || idx->sa_len == 0 || pat_len == 0) {
        if (nresults) *nresults = 0;
        return 0;
    }

    int32_t lo = lower_bound(idx, pattern, pat_len);
    int32_t hi = upper_bound(idx, pattern, pat_len);
    size_t count = (size_t)(hi - lo);
    size_t copy = count < max_results ? count : max_results;

    for (size_t i = 0; i < copy; i++)
        results[i] = idx->sa[lo + (int32_t)i];

    if (nresults) *nresults = count;
    return 0;
}

size_t neverc_suffixarray_count(const neverc_suffixarray_t *idx,
                                const unsigned char *pattern, size_t pat_len) {
    if (!idx->sa || idx->sa_len == 0 || pat_len == 0) return 0;
    int32_t lo = lower_bound(idx, pattern, pat_len);
    int32_t hi = upper_bound(idx, pattern, pat_len);
    return (size_t)(hi - lo);
}

int neverc_suffixarray_at(const neverc_suffixarray_t *idx, size_t i) {
    if (i >= idx->sa_len) return -1;
    return idx->sa[i];
}
