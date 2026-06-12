/*
 * NeverC slices — generic array/slice manipulation.
 * C adaptation of Go slices package.
 */

#include "neverc/std/slices.h"
#include "../sort/sort_impl.h"
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
    char stack_buf[256];
    char *tmp = elem_size <= sizeof(stack_buf) ? stack_buf : (char *)malloc(elem_size);
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        memcpy(tmp, p + i * elem_size, elem_size);
        memcpy(p + i * elem_size, p + j * elem_size, elem_size);
        memcpy(p + j * elem_size, tmp, elem_size);
    }
    if (tmp != stack_buf) free(tmp);
}

void neverc_slices_sort(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    nci_pdqsort(slice, len, elem_size, (nci_cmp_fn)cmp);
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
    size_t r = 1;
    while (r < len) {
        if (eq(p + (w - 1) * elem_size, p + r * elem_size)) {
            r++;
            continue;
        }
        size_t run_start = r;
        r++;
        while (r < len && !eq(p + (r - 1) * elem_size, p + r * elem_size))
            r++;
        size_t run_len = r - run_start;
        if (w != run_start)
            memmove(p + w * elem_size, p + run_start * elem_size, run_len * elem_size);
        w += run_len;
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
    if (len <= 1) return;
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        int tmp = slice[i]; slice[i] = slice[j]; slice[j] = tmp;
    }
}

void neverc_slices_sort_ints(int *slice, size_t len) {
    nci_pdqsort_int(slice, len);
}

int neverc_slices_binary_search_int(const int *slice, size_t len, int target, int *found) {
    size_t lo = 0, hi = len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (slice[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (found) *found = (lo < len && slice[lo] == target);
    return (int)lo;
}

void neverc_slices_sort_stable(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    nci_timsort(slice, len, elem_size, (nci_cmp_fn)cmp);
}

size_t neverc_slices_delete(void *slice, size_t len, size_t elem_size, size_t i, size_t j) {
    if (i >= j || j > len) return len;
    char *p = (char *)slice;
    size_t tail = len - j;
    if (tail > 0) memmove(p + i * elem_size, p + j * elem_size, tail * elem_size);
    return len - (j - i);
}

size_t neverc_slices_insert(void *slice, size_t len, size_t elem_size,
                             size_t i, const void *elems, size_t count) {
    if (count == 0) return len;
    char *p = (char *)slice;
    size_t tail = len - i;
    if (tail > 0) memmove(p + (i + count) * elem_size, p + i * elem_size, tail * elem_size);
    memcpy(p + i * elem_size, elems, count * elem_size);
    return len + count;
}

size_t neverc_slices_replace(void *slice, size_t len, size_t elem_size,
                              size_t i, size_t j, const void *elems, size_t count) {
    size_t removed = j - i;
    char *p = (char *)slice;
    size_t tail = len - j;
    if (count != removed && tail > 0)
        memmove(p + (i + count) * elem_size, p + j * elem_size, tail * elem_size);
    if (count > 0) memcpy(p + i * elem_size, elems, count * elem_size);
    return len - removed + count;
}

void *neverc_slices_concat(const void *s1, size_t len1, const void *s2, size_t len2,
                            size_t elem_size) {
    size_t total = len1 + len2;
    if (total == 0) return NULL;
    void *out = malloc(total * elem_size);
    if (!out) return NULL;
    if (len1 > 0) memcpy(out, s1, len1 * elem_size);
    if (len2 > 0) memcpy((char *)out + len1 * elem_size, s2, len2 * elem_size);
    return out;
}

int neverc_slices_contains_func(const void *slice, size_t len, size_t elem_size,
                                 neverc_slices_pred_func_t f) {
    return neverc_slices_index_func(slice, len, elem_size, f) >= 0;
}

int neverc_slices_index_func(const void *slice, size_t len, size_t elem_size,
                              neverc_slices_pred_func_t f) {
    const char *p = (const char *)slice;
    for (size_t i = 0; i < len; i++)
        if (f(p + i * elem_size)) return (int)i;
    return -1;
}

size_t neverc_slices_delete_func(void *slice, size_t len, size_t elem_size,
                                  neverc_slices_pred_func_t f) {
    char *p = (char *)slice;
    size_t w = 0;
    for (size_t r = 0; r < len; r++) {
        if (!f(p + r * elem_size)) {
            if (w != r) memcpy(p + w * elem_size, p + r * elem_size, elem_size);
            w++;
        }
    }
    return w;
}
