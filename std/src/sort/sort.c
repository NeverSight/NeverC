#include "neverc/std/sort.h"
#include "sort_impl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ─── Sorting ─── */

void neverc_sort_ints(int *arr, size_t n) {
    nci_pdqsort_int(arr, n);
}

void neverc_sort_doubles(double *arr, size_t n) {
    nci_pdqsort_double(arr, n);
}

void neverc_sort_custom(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    nci_pdqsort(base, n, elem_size, cmp);
}

int neverc_sort_is_sorted(const void *base, size_t n, size_t elem_size,
                          neverc_sort_cmp_t cmp) {
    const char *p = (const char *)base;
    for (size_t i = 1; i < n; i++) {
        if (cmp(p + (i - 1) * elem_size, p + i * elem_size) > 0)
            return 0;
    }
    return 1;
}

size_t neverc_sort_search(size_t n, int (*f)(size_t i)) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (!f(mid))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int neverc_sort_search_ints(const int *arr, size_t n, int target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && arr[lo] == target) return (int)lo;
    return -1;
}

int neverc_sort_search_doubles(const double *arr, size_t n, double target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && arr[lo] == target) return (int)lo;
    return -1;
}

/* ─── Stable sort (Timsort) ─── */

void neverc_sort_stable(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    nci_timsort(base, n, elem_size, cmp);
}

/* ─── String sorting ─── */

static int cmp_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void neverc_sort_strings(const char **arr, size_t n) {
    nci_pdqsort(arr, n, sizeof(const char *), cmp_strings);
}

int neverc_sort_strings_are_sorted(const char **arr, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (strcmp(arr[i-1], arr[i]) > 0) return 0;
    return 1;
}

int neverc_sort_search_strings(const char **arr, size_t n, const char *target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(arr[mid], target) < 0) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && strcmp(arr[lo], target) == 0) return (int)lo;
    return -1;
}

/* ─── Type-specific sorted checks ─── */

int neverc_sort_ints_are_sorted(const int *arr, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (arr[i-1] > arr[i]) return 0;
    return 1;
}

int neverc_sort_doubles_are_sorted(const double *arr, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (arr[i-1] > arr[i]) return 0;
    return 1;
}

/* ─── Find (Go sort.Find) ─── */

size_t neverc_sort_find(size_t n, int (*cmp)(size_t i), int *found) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(mid);
        if (c > 0) {
            lo = mid + 1;
        } else if (c < 0) {
            hi = mid;
        } else {
            if (found) *found = 1;
            hi = mid;
        }
    }
    if (found && lo < n && cmp(lo) == 0)
        *found = 1;
    return lo;
}

/* ─── Reverse ─── */

void neverc_sort_reverse(void *base, size_t n, size_t elem_size) {
    if (n <= 1) return;
    uint8_t *b = (uint8_t *)base;
    uint8_t tmp_buf[256];
    uint8_t *tmp = elem_size <= sizeof(tmp_buf) ? tmp_buf : (uint8_t *)malloc(elem_size);
    if (!tmp) return;
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        memcpy(tmp, b + i * elem_size, elem_size);
        memcpy(b + i * elem_size, b + j * elem_size, elem_size);
        memcpy(b + j * elem_size, tmp, elem_size);
    }
    if (tmp != tmp_buf) free(tmp);
}

void neverc_sort_slice(void *base, size_t n, size_t elem_size,
                       neverc_sort_cmp_t cmp) {
    nci_pdqsort(base, n, elem_size, cmp);
}

void neverc_sort_slice_stable(void *base, size_t n, size_t elem_size,
                              neverc_sort_cmp_t cmp) {
    nci_timsort(base, n, elem_size, cmp);
}

int neverc_sort_slice_is_sorted(const void *base, size_t n, size_t elem_size,
                                neverc_sort_cmp_t cmp) {
    return neverc_sort_is_sorted(base, n, elem_size, cmp);
}
