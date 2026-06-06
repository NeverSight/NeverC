#include "neverc/sort.h"
#include <stdlib.h>
#include <string.h>

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

void neverc_sort_ints(int *arr, size_t n) {
    qsort(arr, n, sizeof(int), cmp_int);
}

void neverc_sort_doubles(double *arr, size_t n) {
    qsort(arr, n, sizeof(double), cmp_double);
}

void neverc_sort_custom(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    qsort(base, n, elem_size, cmp);
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
        if (arr[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < n && arr[lo] == target)
        return (int)lo;
    return -1;
}

int neverc_sort_search_doubles(const double *arr, size_t n, double target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < n && arr[lo] == target)
        return (int)lo;
    return -1;
}
