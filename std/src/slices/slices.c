/*
 * NeverC slices — generic array/slice manipulation.
 * C adaptation of Go slices package.
 */

#include "neverc/slices.h"
#include <stdlib.h>
#include <string.h>

int neverc_slices_equal(const void *s1, size_t len1, const void *s2, size_t len2, size_t elem_size) {
    if (len1 != len2) return 0;
    if (len1 == 0) return 1;
    return memcmp(s1, s2, len1 * elem_size) == 0;
}

int neverc_slices_compare(const void *s1, size_t len1, const void *s2, size_t len2,
                           size_t elem_size, neverc_cmp_func_t cmp) {
    size_t minlen = len1 < len2 ? len1 : len2;
    const char *p1 = (const char *)s1;
    const char *p2 = (const char *)s2;
    for (size_t i = 0; i < minlen; i++) {
        int c = cmp(p1 + i * elem_size, p2 + i * elem_size);
        if (c != 0) return c;
    }
    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

int neverc_slices_contains(const void *slice, size_t len, const void *elem,
                            size_t elem_size, neverc_eq_func_t eq) {
    return neverc_slices_index(slice, len, elem, elem_size, eq) >= 0;
}

int neverc_slices_index(const void *slice, size_t len, const void *elem,
                         size_t elem_size, neverc_eq_func_t eq) {
    const char *p = (const char *)slice;
    for (size_t i = 0; i < len; i++) {
        if (eq(p + i * elem_size, elem)) return (int)i;
    }
    return -1;
}

void neverc_slices_reverse(void *slice, size_t len, size_t elem_size) {
    if (len <= 1) return;
    char *p = (char *)slice;
    char *tmp = (char *)malloc(elem_size);
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        memcpy(tmp, p + i * elem_size, elem_size);
        memcpy(p + i * elem_size, p + j * elem_size, elem_size);
        memcpy(p + j * elem_size, tmp, elem_size);
    }
    free(tmp);
}

static neverc_cmp_func_t g_sort_cmp;
static int sort_wrapper(const void *a, const void *b) {
    return g_sort_cmp(a, b);
}

void neverc_slices_sort(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    g_sort_cmp = cmp;
    qsort(slice, len, elem_size, sort_wrapper);
}

int neverc_slices_is_sorted(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len <= 1) return 1;
    const char *p = (const char *)slice;
    for (size_t i = 1; i < len; i++) {
        if (cmp(p + (i - 1) * elem_size, p + i * elem_size) > 0) return 0;
    }
    return 1;
}

int neverc_slices_binary_search(const void *slice, size_t len, const void *target,
                                 size_t elem_size, neverc_cmp_func_t cmp, int *found) {
    int lo = 0, hi = (int)len;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const char *p = (const char *)slice + (size_t)mid * elem_size;
        int c = cmp(p, target);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    if (found) {
        if (lo < (int)len) {
            const char *p = (const char *)slice + (size_t)lo * elem_size;
            *found = (cmp(p, target) == 0);
        } else {
            *found = 0;
        }
    }
    return lo;
}

size_t neverc_slices_compact(void *slice, size_t len, size_t elem_size, neverc_eq_func_t eq) {
    if (len <= 1) return len;
    char *p = (char *)slice;
    size_t w = 1;
    for (size_t r = 1; r < len; r++) {
        if (!eq(p + (w - 1) * elem_size, p + r * elem_size)) {
            if (w != r) memcpy(p + w * elem_size, p + r * elem_size, elem_size);
            w++;
        }
    }
    return w;
}

void *neverc_slices_clone(const void *slice, size_t len, size_t elem_size) {
    if (len == 0) return NULL;
    void *out = malloc(len * elem_size);
    if (out) memcpy(out, slice, len * elem_size);
    return out;
}

int neverc_slices_min(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len == 0) return -1;
    const char *p = (const char *)slice;
    int mi = 0;
    for (size_t i = 1; i < len; i++) {
        if (cmp(p + i * elem_size, p + (size_t)mi * elem_size) < 0) mi = (int)i;
    }
    return mi;
}

int neverc_slices_max(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len == 0) return -1;
    const char *p = (const char *)slice;
    int mi = 0;
    for (size_t i = 1; i < len; i++) {
        if (cmp(p + i * elem_size, p + (size_t)mi * elem_size) > 0) mi = (int)i;
    }
    return mi;
}

/* Type-specific convenience */
int neverc_slices_equal_ints(const int *s1, size_t len1, const int *s2, size_t len2) {
    return neverc_slices_equal(s1, len1, s2, len2, sizeof(int));
}

int neverc_slices_contains_int(const int *slice, size_t len, int val) {
    for (size_t i = 0; i < len; i++)
        if (slice[i] == val) return 1;
    return 0;
}

int neverc_slices_index_int(const int *slice, size_t len, int val) {
    for (size_t i = 0; i < len; i++)
        if (slice[i] == val) return (int)i;
    return -1;
}

void neverc_slices_reverse_ints(int *slice, size_t len) {
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        int tmp = slice[i]; slice[i] = slice[j]; slice[j] = tmp;
    }
}

static int cmp_int(const void *a, const void *b) {
    int va = *(const int *)a, vb = *(const int *)b;
    return (va > vb) - (va < vb);
}

void neverc_slices_sort_ints(int *slice, size_t len) {
    neverc_slices_sort(slice, len, sizeof(int), cmp_int);
}
