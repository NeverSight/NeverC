#include "neverc/sort.h"
#include <string.h>

/*
 * Introsort (quicksort + heapsort fallback) — no qsort dependency.
 * Based on Go's sort.pdqsort approach, simplified.
 */

static void swap_bytes(void *a, void *b, size_t size) {
    char *pa = (char *)a, *pb = (char *)b;
    for (size_t i = 0; i < size; i++) {
        char t = pa[i];
        pa[i] = pb[i];
        pb[i] = t;
    }
}

#define ELEM(base, i, sz) ((char *)(base) + (i) * (sz))

static void sift_down(void *base, size_t n, size_t elem_size,
                      neverc_sort_cmp_t cmp, size_t i) {
    while (1) {
        size_t child = 2 * i + 1;
        if (child >= n) break;
        if (child + 1 < n && cmp(ELEM(base, child, elem_size),
                                   ELEM(base, child+1, elem_size)) < 0)
            child++;
        if (cmp(ELEM(base, i, elem_size), ELEM(base, child, elem_size)) >= 0)
            break;
        swap_bytes(ELEM(base, i, elem_size), ELEM(base, child, elem_size), elem_size);
        i = child;
    }
}

static void heapsort_impl(void *base, size_t n, size_t elem_size,
                          neverc_sort_cmp_t cmp) {
    for (size_t i = n / 2; i > 0; i--)
        sift_down(base, n, elem_size, cmp, i - 1);
    for (size_t i = n - 1; i > 0; i--) {
        swap_bytes(ELEM(base, 0, elem_size), ELEM(base, i, elem_size), elem_size);
        sift_down(base, i, elem_size, cmp, 0);
    }
}

static size_t partition(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    size_t pivot = n / 2;
    swap_bytes(ELEM(base, pivot, elem_size), ELEM(base, n-1, elem_size), elem_size);

    size_t i = 0;
    for (size_t j = 0; j < n - 1; j++) {
        if (cmp(ELEM(base, j, elem_size), ELEM(base, n-1, elem_size)) < 0) {
            swap_bytes(ELEM(base, i, elem_size), ELEM(base, j, elem_size), elem_size);
            i++;
        }
    }
    swap_bytes(ELEM(base, i, elem_size), ELEM(base, n-1, elem_size), elem_size);
    return i;
}

static void introsort(void *base, size_t n, size_t elem_size,
                      neverc_sort_cmp_t cmp, int depth_limit) {
    while (n > 16) {
        if (depth_limit == 0) {
            heapsort_impl(base, n, elem_size, cmp);
            return;
        }
        depth_limit--;
        size_t p = partition(base, n, elem_size, cmp);
        introsort(ELEM(base, p+1, elem_size), n - p - 1, elem_size, cmp, depth_limit);
        n = p;
    }
    /* insertion sort for small arrays */
    for (size_t i = 1; i < n; i++) {
        size_t j = i;
        while (j > 0 && cmp(ELEM(base, j-1, elem_size),
                             ELEM(base, j, elem_size)) > 0) {
            swap_bytes(ELEM(base, j-1, elem_size), ELEM(base, j, elem_size), elem_size);
            j--;
        }
    }
}

static int depth_for(size_t n) {
    int d = 0;
    while (n > 1) { n >>= 1; d++; }
    return d * 2;
}

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
    if (n < 2) return;
    introsort(arr, n, sizeof(int), cmp_int, depth_for(n));
}

void neverc_sort_doubles(double *arr, size_t n) {
    if (n < 2) return;
    introsort(arr, n, sizeof(double), cmp_double, depth_for(n));
}

void neverc_sort_custom(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp) {
    if (n < 2) return;
    introsort(base, n, elem_size, cmp, depth_for(n));
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
