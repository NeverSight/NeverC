#ifndef NEVERC_SLICES_H
#define NEVERC_SLICES_H

/*
 * NeverC slices — generic array/slice manipulation.
 * C adaptation of Go slices package (uses void* + element size).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*neverc_cmp_func_t)(const void *a, const void *b);
typedef int (*neverc_eq_func_t)(const void *a, const void *b);

int  neverc_slices_equal(const void *s1, size_t len1, const void *s2, size_t len2, size_t elem_size);
int  neverc_slices_compare(const void *s1, size_t len1, const void *s2, size_t len2,
                            size_t elem_size, neverc_cmp_func_t cmp);
int  neverc_slices_contains(const void *slice, size_t len, const void *elem,
                             size_t elem_size, neverc_eq_func_t eq);
int  neverc_slices_index(const void *slice, size_t len, const void *elem,
                          size_t elem_size, neverc_eq_func_t eq);
void neverc_slices_reverse(void *slice, size_t len, size_t elem_size);
void neverc_slices_sort(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);
int  neverc_slices_is_sorted(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);
int  neverc_slices_binary_search(const void *slice, size_t len, const void *target,
                                  size_t elem_size, neverc_cmp_func_t cmp, int *found);
size_t neverc_slices_compact(void *slice, size_t len, size_t elem_size, neverc_eq_func_t eq);
void *neverc_slices_clone(const void *slice, size_t len, size_t elem_size);
int  neverc_slices_min(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);
int  neverc_slices_max(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);

/* Type-specific convenience */
int  neverc_slices_equal_ints(const int *s1, size_t len1, const int *s2, size_t len2);
int  neverc_slices_contains_int(const int *slice, size_t len, int val);
int  neverc_slices_index_int(const int *slice, size_t len, int val);
void neverc_slices_reverse_ints(int *slice, size_t len);
void neverc_slices_sort_ints(int *slice, size_t len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_slices_t { char __tag; };
extern struct __neverc_std_slices_t __neverc_mod_slices;
extern struct __neverc_std_slices_t slices;
#endif

#endif
