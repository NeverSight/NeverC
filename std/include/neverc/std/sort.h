#ifndef NEVERC_SORT_H
#define NEVERC_SORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sorting and searching utilities (mirrors Go sort package).
 */

typedef int (*neverc_sort_cmp_t)(const void *a, const void *b);

void neverc_sort_ints(int *arr, size_t n);
void neverc_sort_doubles(double *arr, size_t n);

void neverc_sort_custom(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp);

int  neverc_sort_is_sorted(const void *base, size_t n, size_t elem_size,
                           neverc_sort_cmp_t cmp);

/* Binary search: returns the smallest index i in [0, n) for which f(i) is true.
 * Equivalent to Go's sort.Search. */
size_t neverc_sort_search(size_t n, int (*f)(size_t i));

int  neverc_sort_search_ints(const int *arr, size_t n, int target);
int  neverc_sort_search_doubles(const double *arr, size_t n, double target);

/* Stable sort (preserves order of equal elements) */
void neverc_sort_stable(void *base, size_t n, size_t elem_size,
                        neverc_sort_cmp_t cmp);

/* String array sort */
void neverc_sort_strings(const char **arr, size_t n);
int  neverc_sort_strings_are_sorted(const char **arr, size_t n);
int  neverc_sort_search_strings(const char **arr, size_t n, const char *target);

/* Reverse a sorted array in-place */
void neverc_sort_reverse(void *base, size_t n, size_t elem_size);

/* Type-specific sorted checks (Go: IntsAreSorted, Float64sAreSorted) */
int  neverc_sort_ints_are_sorted(const int *arr, size_t n);
int  neverc_sort_doubles_are_sorted(const double *arr, size_t n);

/* Find: binary search returning (index, found).
 * The cmp function compares the element at index i against the target.
 * Returns the index where target would be inserted; sets *found to 1 if exact match. */
size_t neverc_sort_find(size_t n, int (*cmp)(size_t i), int *found);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_sort_t { char __tag; };
extern struct __neverc_std_sort_t __neverc_mod_sort;
extern struct __neverc_std_sort_t sort;
#endif

#endif /* NEVERC_SORT_H */
