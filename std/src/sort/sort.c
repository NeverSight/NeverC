#include "neverc/std/sort.h"
#include "sort_impl.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ─── Sorting ─── */

void neverc_sort_ints(int *arr, size_t n) {
    if (!arr || n <= 1) return;
    nci_pdqsort_int(arr, n);
}

void neverc_sort_doubles(double *arr, size_t n) {
    if (!arr || n <= 1) return;
    nci_pdqsort_double(arr, n);
}

void neverc_sort_custom(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    if (!base || !cmp || n <= 1 || elem_size == 0) return;
    nci_pdqsort(base, n, elem_size, cmp);
}

int neverc_sort_is_sorted(const void *base, size_t n, size_t elem_size,
                          neverc_sort_cmp_t cmp) {
    if (n <= 1) return 1;
    if (!base || !cmp || elem_size == 0 || n > SIZE_MAX / elem_size)
        return 0;
    const char *p = (const char *)base;
    for (size_t i = 1; i < n; i++) {
        if (cmp(p + (i - 1) * elem_size, p + i * elem_size) > 0)
            return 0;
    }
    return 1;
}

size_t neverc_sort_search(size_t n, int (*f)(size_t i)) {
    if (!f) return 0;
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
    if (!arr) return -1;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo > (size_t)INT_MAX) return -1;
    if (lo < n && arr[lo] == target) return (int)lo;
    return -1;
}

int neverc_sort_search_doubles(const double *arr, size_t n, double target) {
    if (!arr) return -1;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (nci_double_less(arr[mid], target)) lo = mid + 1;
        else hi = mid;
    }
    if (lo > (size_t)INT_MAX) return -1;
    if (lo < n && !nci_double_less(arr[lo], target) &&
        !nci_double_less(target, arr[lo]))
        return (int)lo;
    return -1;
}

/* ─── Stable sort (Timsort) ─── */

void neverc_sort_stable(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    nci_timsort(base, n, elem_size, cmp);
}

/* ─── String sorting ─── */

void neverc_sort_strings(const char **arr, size_t n) {
    if (!arr || n <= 1) return;
    nci_sort_strings(arr, n);
}

int neverc_sort_strings_are_sorted(const char **arr, size_t n) {
    if (n <= 1) return 1;
    if (!arr) return 0;
    for (size_t i = 1; i < n; i++)
        if (!arr[i - 1] || !arr[i] || strcmp(arr[i-1], arr[i]) > 0) return 0;
    return 1;
}

int neverc_sort_search_strings(const char **arr, size_t n, const char *target) {
    if (!arr || !target) return -1;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (!arr[mid]) return -1;
        if (strcmp(arr[mid], target) < 0) lo = mid + 1;
        else hi = mid;
    }
    if (lo > (size_t)INT_MAX) return -1;
    if (lo < n && arr[lo] && strcmp(arr[lo], target) == 0) return (int)lo;
    return -1;
}

/* ─── Type-specific sorted checks ─── */

int neverc_sort_ints_are_sorted(const int *arr, size_t n) {
    if (n <= 1) return 1;
    if (!arr) return 0;
    for (size_t i = 1; i < n; i++)
        if (arr[i-1] > arr[i]) return 0;
    return 1;
}

int neverc_sort_doubles_are_sorted(const double *arr, size_t n) {
    if (n <= 1) return 1;
    if (!arr) return 0;
    for (size_t i = 1; i < n; i++)
        if (nci_double_less(arr[i], arr[i - 1])) return 0;
    return 1;
}

/* ─── Find (Go sort.Find) ─── */

size_t neverc_sort_find(size_t n, int (*cmp)(size_t i), int *found) {
    if (found) *found = 0;
    if (!cmp) return 0;
    /* cmp(i) < 0 ⇒ entry i is less than the target (Go cmp.Compare). */
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp(mid) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (found && lo < n && cmp(lo) == 0)
        *found = 1;
    return lo;
}

/* ─── Reverse ─── */

void neverc_sort_reverse(void *base, size_t n, size_t elem_size) {
    if (!base || n <= 1 || elem_size == 0 || n > SIZE_MAX / elem_size) return;
    char *b = (char *)base;
    for (size_t i = 0, j = n - 1; i < j; i++, j--)
        nci_swap_chunked(b + i * elem_size, b + j * elem_size, elem_size);
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
