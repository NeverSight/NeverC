/*
 * NeverC slices — generic array/slice manipulation.
 * C adaptation of Go slices package.
 */

#include "neverc/std/slices.h"
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

static void insertion_sort(char *base, size_t len, size_t es, neverc_cmp_func_t cmp, char *tmp) {
    for (size_t i = 1; i < len; i++) {
        memcpy(tmp, base + i * es, es);
        size_t j = i;
        while (j > 0 && cmp(base + (j - 1) * es, tmp) > 0) {
            memcpy(base + j * es, base + (j - 1) * es, es);
            j--;
        }
        memcpy(base + j * es, tmp, es);
    }
}

static void sift_down(char *base, size_t node, size_t len, size_t es,
                       neverc_cmp_func_t cmp, char *tmp) {
    while (1) {
        size_t child = 2 * node + 1;
        if (child >= len) break;
        if (child + 1 < len && cmp(base + child * es, base + (child + 1) * es) < 0)
            child++;
        if (cmp(base + node * es, base + child * es) >= 0) break;
        memcpy(tmp, base + node * es, es);
        memcpy(base + node * es, base + child * es, es);
        memcpy(base + child * es, tmp, es);
        node = child;
    }
}

static void heap_sort(char *base, size_t len, size_t es, neverc_cmp_func_t cmp, char *tmp) {
    if (len <= 1) return;
    for (size_t i = len / 2; i > 0; i--)
        sift_down(base, i - 1, len, es, cmp, tmp);
    for (size_t i = len - 1; i > 0; i--) {
        memcpy(tmp, base, es);
        memcpy(base, base + i * es, es);
        memcpy(base + i * es, tmp, es);
        sift_down(base, 0, i, es, cmp, tmp);
    }
}

static size_t partition(char *base, size_t len, size_t es, neverc_cmp_func_t cmp, char *tmp) {
    size_t mid = len / 2;
    if (len > 8) {
        if (cmp(base, base + mid * es) > 0) {
            memcpy(tmp, base, es); memcpy(base, base + mid * es, es); memcpy(base + mid * es, tmp, es);
        }
        if (cmp(base + mid * es, base + (len - 1) * es) > 0) {
            memcpy(tmp, base + mid * es, es); memcpy(base + mid * es, base + (len - 1) * es, es); memcpy(base + (len - 1) * es, tmp, es);
            if (cmp(base, base + mid * es) > 0) {
                memcpy(tmp, base, es); memcpy(base, base + mid * es, es); memcpy(base + mid * es, tmp, es);
            }
        }
    }
    memcpy(tmp, base + mid * es, es);
    memcpy(base + mid * es, base + (len - 1) * es, es);
    memcpy(base + (len - 1) * es, tmp, es);

    size_t i = 0;
    for (size_t j = 0; j < len - 1; j++) {
        if (cmp(base + j * es, base + (len - 1) * es) < 0) {
            memcpy(tmp, base + i * es, es);
            memcpy(base + i * es, base + j * es, es);
            memcpy(base + j * es, tmp, es);
            i++;
        }
    }
    memcpy(tmp, base + i * es, es);
    memcpy(base + i * es, base + (len - 1) * es, es);
    memcpy(base + (len - 1) * es, tmp, es);
    return i;
}

static void introsort_impl(char *base, size_t len, size_t es,
                            neverc_cmp_func_t cmp, int depth, char *tmp) {
    if (len <= 16) { insertion_sort(base, len, es, cmp, tmp); return; }
    if (depth == 0) { heap_sort(base, len, es, cmp, tmp); return; }
    size_t pivot = partition(base, len, es, cmp, tmp);
    introsort_impl(base, pivot, es, cmp, depth - 1, tmp);
    if (pivot + 1 < len)
        introsort_impl(base + (pivot + 1) * es, len - pivot - 1, es, cmp, depth - 1, tmp);
}

void neverc_slices_sort(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len <= 1) return;
    int depth = 0;
    for (size_t n = len; n > 0; n >>= 1) depth++;
    depth *= 2;
    char *tmp = (char *)malloc(elem_size);
    introsort_impl((char *)slice, len, elem_size, cmp, depth, tmp);
    free(tmp);
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

/* Stable sort: bottom-up merge sort */
void neverc_slices_sort_stable(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len <= 1) return;
    char *arr = (char *)slice;
    char *aux = (char *)malloc(len * elem_size);
    if (!aux) return;

    for (size_t width = 1; width < len; width *= 2) {
        for (size_t i = 0; i < len; i += 2 * width) {
            size_t mid = i + width;
            size_t end = i + 2 * width;
            if (mid > len) mid = len;
            if (end > len) end = len;
            size_t l = i, r = mid, k = i;
            while (l < mid && r < end) {
                if (cmp(arr + l * elem_size, arr + r * elem_size) <= 0)
                    memcpy(aux + k * elem_size, arr + (l++) * elem_size, elem_size);
                else
                    memcpy(aux + k * elem_size, arr + (r++) * elem_size, elem_size);
                k++;
            }
            while (l < mid) { memcpy(aux + k * elem_size, arr + (l++) * elem_size, elem_size); k++; }
            while (r < end) { memcpy(aux + k * elem_size, arr + (r++) * elem_size, elem_size); k++; }
        }
        memcpy(arr, aux, len * elem_size);
    }
    free(aux);
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
