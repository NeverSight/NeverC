#include "neverc/index/suffixarray.h"
#include <stdlib.h>
#include <string.h>

/*
 * Simple O(n log^2 n) suffix array construction using prefix doubling.
 * Not as fast as SA-IS but much simpler and more robust.
 */

typedef struct {
    int32_t idx;
    int32_t rank[2];
} suffix_pair_t;

static int pair_less(const suffix_pair_t *a, const suffix_pair_t *b) {
    if (a->rank[0] != b->rank[0]) return a->rank[0] < b->rank[0];
    return a->rank[1] < b->rank[1];
}

static void pair_swap(suffix_pair_t *a, suffix_pair_t *b) {
    suffix_pair_t t = *a; *a = *b; *b = t;
}

static void insertion_sort_pairs(suffix_pair_t *arr, size_t n) {
    for (size_t i = 1; i < n; i++) {
        suffix_pair_t key = arr[i];
        size_t j = i;
        while (j > 0 && pair_less(&key, &arr[j - 1])) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = key;
    }
}

static void heapsort_pairs(suffix_pair_t *arr, size_t n) {
    if (n < 2) return;
    for (size_t i = n / 2; i > 0; i--) {
        size_t p = i - 1;
        for (;;) {
            size_t c = 2 * p + 1;
            if (c >= n) break;
            if (c + 1 < n && pair_less(&arr[c], &arr[c + 1])) c++;
            if (!pair_less(&arr[p], &arr[c])) break;
            pair_swap(&arr[p], &arr[c]);
            p = c;
        }
    }
    for (size_t i = n - 1; i > 0; i--) {
        pair_swap(&arr[0], &arr[i]);
        size_t p = 0;
        for (;;) {
            size_t c = 2 * p + 1;
            if (c >= i) break;
            if (c + 1 < i && pair_less(&arr[c], &arr[c + 1])) c++;
            if (!pair_less(&arr[p], &arr[c])) break;
            pair_swap(&arr[p], &arr[c]);
            p = c;
        }
    }
}

static void sort_pairs(suffix_pair_t *arr, size_t n, int depth) {
    while (n > 16) {
        if (depth == 0) { heapsort_pairs(arr, n); return; }
        depth--;
        size_t mid = n / 2;
        if (pair_less(&arr[mid], &arr[0])) pair_swap(&arr[0], &arr[mid]);
        if (pair_less(&arr[n-1], &arr[0])) pair_swap(&arr[0], &arr[n-1]);
        if (pair_less(&arr[mid], &arr[0])) pair_swap(&arr[0], &arr[mid]);
        suffix_pair_t pivot = arr[mid];
        size_t i = 0, j = n;
        for (;;) {
            while (++i < n && pair_less(&arr[i], &pivot));
            while (--j > 0 && pair_less(&pivot, &arr[j]));
            if (i >= j) break;
            pair_swap(&arr[i], &arr[j]);
        }
        pair_swap(&arr[0], &arr[j]);
        sort_pairs(arr, j, depth);
        arr += j + 1;
        n -= j + 1;
    }
    insertion_sort_pairs(arr, n);
}

static void build_suffix_array(const unsigned char *data, size_t n, int32_t *sa) {
    if (n == 0) return;
    if (n == 1) { sa[0] = 0; return; }

    suffix_pair_t *pairs = (suffix_pair_t *)malloc(n * sizeof(suffix_pair_t));
    int32_t *rank = (int32_t *)malloc(n * sizeof(int32_t));
    int32_t *tmp = (int32_t *)malloc(n * sizeof(int32_t));

    for (size_t i = 0; i < n; i++) {
        rank[i] = data[i];
        pairs[i].idx = (int32_t)i;
    }

    for (int32_t gap = 1; ; gap *= 2) {
        for (size_t i = 0; i < n; i++) {
            pairs[i].rank[0] = rank[i];
            size_t j = (size_t)((int32_t)i + gap);
            pairs[i].rank[1] = (j < n) ? rank[j] : -1;
            pairs[i].idx = (int32_t)i;
        }

        {
            int depth = 0;
            for (size_t nn = n; nn > 1; nn >>= 1) depth++;
            depth *= 2;
            sort_pairs(pairs, n, depth);
        }

        tmp[pairs[0].idx] = 0;
        for (size_t i = 1; i < n; i++) {
            tmp[pairs[i].idx] = tmp[pairs[i - 1].idx];
            if (pairs[i].rank[0] != pairs[i - 1].rank[0] ||
                pairs[i].rank[1] != pairs[i - 1].rank[1])
                tmp[pairs[i].idx]++;
        }
        memcpy(rank, tmp, n * sizeof(int32_t));

        if (rank[pairs[n - 1].idx] == (int32_t)(n - 1))
            break;
    }

    for (size_t i = 0; i < n; i++)
        sa[i] = pairs[i].idx;

    free(pairs);
    free(rank);
    free(tmp);
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
