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
/* Consecutive equal runs only. Discarded tail slots are zeroed. */
size_t neverc_slices_compact(void *slice, size_t len, size_t elem_size, neverc_eq_func_t eq);
void *neverc_slices_clone(const void *slice, size_t len, size_t elem_size);
int  neverc_slices_min(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);
int  neverc_slices_max(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);

/* Stable sort */
void neverc_slices_sort_stable(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp);

/* Delete elements [i,j) in-place. Returns new length.
   Caller ensures 0 <= i <= j <= len. Discarded tail slots are zeroed. */
size_t neverc_slices_delete(void *slice, size_t len, size_t elem_size, size_t i, size_t j);

/* Insert 'count' elements from 'elems' at position 'i'.
   slice must have capacity >= (len + count) * elem_size.
   Returns new length. */
size_t neverc_slices_insert(void *slice, size_t len, size_t elem_size,
                             size_t i, const void *elems, size_t count);

/* Replace elements [i,j) with 'elems' (count elements).
   slice must have capacity >= (len - (j-i) + count) * elem_size.
   Returns new length. A shrinking replace zeroes discarded tail slots. */
size_t neverc_slices_replace(void *slice, size_t len, size_t elem_size,
                              size_t i, size_t j, const void *elems, size_t count);

/* Concat: allocate new slice = s1 + s2. Caller frees. */
void *neverc_slices_concat(const void *s1, size_t len1, const void *s2, size_t len2,
                            size_t elem_size);

/* Clip unused capacity like Go slices.Clip: *out_cap = len. Does not
 * reallocate. Returns 0 if cap < len or out_cap is NULL. */
int neverc_slices_clip(size_t len, size_t cap, size_t *out_cap);

/* Grow like Go slices.Grow: guarantee cap >= len+n. slice must be NULL
 * or malloc'd. Returns the (possibly moved) buffer and writes *out_cap.
 * On overflow or OOM returns NULL, leaves slice valid, and does not
 * write *out_cap. After Clip, leftover capacity is 0 so Grow(n) must
 * allocate len+n without wrapping cap+n. */
void *neverc_slices_grow(void *slice, size_t len, size_t cap, size_t n,
                         size_t elem_size, size_t *out_cap);

/* Func-based operations */
typedef int (*neverc_slices_pred_func_t)(const void *elem);

int  neverc_slices_contains_func(const void *slice, size_t len, size_t elem_size,
                                  neverc_slices_pred_func_t f);
int  neverc_slices_index_func(const void *slice, size_t len, size_t elem_size,
                               neverc_slices_pred_func_t f);
size_t neverc_slices_delete_func(void *slice, size_t len, size_t elem_size,
                                  neverc_slices_pred_func_t f);

/* Type-specific convenience */
int  neverc_slices_equal_ints(const int *s1, size_t len1, const int *s2, size_t len2);
int  neverc_slices_contains_int(const int *slice, size_t len, int val);
int  neverc_slices_index_int(const int *slice, size_t len, int val);
void neverc_slices_reverse_ints(int *slice, size_t len);
void neverc_slices_sort_ints(int *slice, size_t len);
int  neverc_slices_binary_search_int(const int *slice, size_t len, int target, int *found);
int  neverc_slices_min_int(const int *slice, size_t len);
int  neverc_slices_max_int(const int *slice, size_t len);
int  neverc_slices_is_sorted_ints(const int *slice, size_t len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_slices_t { char __tag; };
extern struct __neverc_std_slices_t __neverc_mod_slices;
extern struct __neverc_std_slices_t slices;
#endif

#endif
