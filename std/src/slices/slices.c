/*
 * NeverC slices — generic array/slice manipulation.
 * C adaptation of Go slices package.
 */

#include "neverc/std/slices.h"
#include "../sort/sort_impl.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int slices_byte_size(size_t len, size_t elem_size, size_t *bytes) {
    if (elem_size == 0 || len > SIZE_MAX / elem_size) return 0;
    *bytes = len * elem_size;
    return 1;
}

/* Go slices.Compact/Delete/Replace clear discarded tail slots so leftover
 * values (including pointers) cannot be observed past the new length. */
static void slices_clear_tail(char *p, size_t new_len, size_t old_len,
                              size_t elem_size) {
    if (!p || new_len >= old_len) return;
    memset(p + new_len * elem_size, 0, (old_len - new_len) * elem_size);
}

static int slices_ranges_overlap(const void *a, size_t a_bytes,
                                 const void *b, size_t b_bytes) {
    if (a_bytes == 0 || b_bytes == 0) return 0;
    uintptr_t a_start = (uintptr_t)a;
    uintptr_t b_start = (uintptr_t)b;
    if (a_start <= b_start) return b_start - a_start < a_bytes;
    return a_start - b_start < b_bytes;
}

int neverc_slices_equal(const void *s1, size_t len1, const void *s2, size_t len2, size_t elem_size) {
    if (len1 != len2) return 0;
    if (len1 == 0) return 1;
    size_t bytes;
    if (!s1 || !s2 || !slices_byte_size(len1, elem_size, &bytes)) return 0;
    return memcmp(s1, s2, bytes) == 0;
}

int neverc_slices_compare(const void *s1, size_t len1, const void *s2, size_t len2,
                           size_t elem_size, neverc_cmp_func_t cmp) {
    /* Go slices.Compare == 0 iff Equal. Overflow / missing cmp / zero
     * elem_size cannot inspect elements: fail closed (not equal) except
     * for the empty-empty identity Equal already treats as true. */
    if (!cmp || elem_size == 0) {
        if (len1 == 0 && len2 == 0) return 0;
        return 1;
    }
    if ((len1 > 0 && len1 > SIZE_MAX / elem_size) ||
        (len2 > 0 && len2 > SIZE_MAX / elem_size))
        return 1;
    if (len1 > 0 && !s1) len1 = 0;
    if (len2 > 0 && !s2) len2 = 0;
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
    if (len == 0 || !slice || !elem || elem_size == 0 || !eq ||
        len > SIZE_MAX / elem_size || len > (size_t)INT_MAX)
        return -1;
    const char *p = (const char *)slice;
    for (size_t i = 0; i < len; i++) {
        if (eq(p + i * elem_size, elem)) return (int)i;
    }
    return -1;
}

void neverc_slices_reverse(void *slice, size_t len, size_t elem_size) {
    if (!slice || len <= 1 || elem_size == 0 || len > SIZE_MAX / elem_size)
        return;
    char *p = (char *)slice;
    char stack_buf[256];
    char *tmp = elem_size <= sizeof(stack_buf) ? stack_buf : (char *)malloc(elem_size);
    if (!tmp) return;
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        memcpy(tmp, p + i * elem_size, elem_size);
        memcpy(p + i * elem_size, p + j * elem_size, elem_size);
        memcpy(p + j * elem_size, tmp, elem_size);
    }
    if (tmp != stack_buf) free(tmp);
}

void neverc_slices_sort(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (!slice || len <= 1 || elem_size == 0 || !cmp ||
        len > SIZE_MAX / elem_size)
        return;
    nci_pdqsort(slice, len, elem_size, (nci_cmp_fn)cmp);
}

int neverc_slices_is_sorted(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len <= 1) return 1;
    if (!slice || elem_size == 0 || !cmp || len > SIZE_MAX / elem_size)
        return 0;
    const char *p = (const char *)slice;
    for (size_t i = 1; i < len; i++) {
        if (cmp(p + (i - 1) * elem_size, p + i * elem_size) > 0) return 0;
    }
    return 1;
}

int neverc_slices_binary_search(const void *slice, size_t len, const void *target,
                                 size_t elem_size, neverc_cmp_func_t cmp, int *found) {
    if (!slice || !target || elem_size == 0 || !cmp ||
        (len > 0 && len > SIZE_MAX / elem_size) ||
        len > (size_t)INT_MAX) {
        if (found) *found = 0;
        return 0;
    }
    size_t lo = 0, n = len;
    while (n > 1) {
        size_t half = n >> 1;
        const char *p = (const char *)slice + (lo + half) * elem_size;
        lo += ((size_t)(cmp(p, target) < 0)) * half;
        n -= half;
    }
    if (n > 0) {
        const char *p = (const char *)slice + lo * elem_size;
        lo += (size_t)(cmp(p, target) < 0);
    }
    if (found) {
        if (lo < len) {
            const char *p = (const char *)slice + lo * elem_size;
            *found = (cmp(p, target) == 0);
        } else {
            *found = 0;
        }
    }
    return (int)lo;
}

size_t neverc_slices_compact(void *slice, size_t len, size_t elem_size, neverc_eq_func_t eq) {
    if (!slice || !eq || elem_size == 0) return len;
    if (len <= 1) return len;
    if (len > SIZE_MAX / elem_size) return len;
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
    slices_clear_tail(p, w, len, elem_size);
    return w;
}

void *neverc_slices_clone(const void *slice, size_t len, size_t elem_size) {
    if (len == 0) return NULL;
    size_t bytes;
    if (!slice || !slices_byte_size(len, elem_size, &bytes)) return NULL;
    void *out = malloc(bytes);
    if (out) memcpy(out, slice, bytes);
    return out;
}

int neverc_slices_min(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len == 0 || !slice || !cmp || elem_size == 0 ||
        len > SIZE_MAX / elem_size || len > (size_t)INT_MAX)
        return -1;
    const char *p = (const char *)slice;
    size_t mi = 0;
    for (size_t i = 1; i < len; i++) {
        if (cmp(p + i * elem_size, p + mi * elem_size) < 0) mi = i;
    }
    return (int)mi;
}

int neverc_slices_max(const void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (len == 0 || !slice || !cmp || elem_size == 0 ||
        len > SIZE_MAX / elem_size || len > (size_t)INT_MAX)
        return -1;
    const char *p = (const char *)slice;
    size_t mi = 0;
    for (size_t i = 1; i < len; i++) {
        if (cmp(p + i * elem_size, p + mi * elem_size) > 0) mi = i;
    }
    return (int)mi;
}

/* Type-specific convenience */
int neverc_slices_equal_ints(const int *s1, size_t len1, const int *s2, size_t len2) {
    return neverc_slices_equal(s1, len1, s2, len2, sizeof(int));
}

int neverc_slices_index_int(const int *slice, size_t len, int val) {
    if (!slice || len > (size_t)INT_MAX) return -1;
    for (size_t i = 0; i < len; i++)
        if (slice[i] == val) return (int)i;
    return -1;
}

int neverc_slices_contains_int(const int *slice, size_t len, int val) {
    return neverc_slices_index_int(slice, len, val) >= 0;
}

void neverc_slices_reverse_ints(int *slice, size_t len) {
    if (!slice || len <= 1) return;
    for (size_t i = 0, j = len - 1; i < j; i++, j--) {
        int tmp = slice[i]; slice[i] = slice[j]; slice[j] = tmp;
    }
}

void neverc_slices_sort_ints(int *slice, size_t len) {
    if (!slice || len <= 1) return;
    nci_pdqsort_int(slice, len);
}

int neverc_slices_binary_search_int(const int *slice, size_t len, int target, int *found) {
    if (!slice || len > (size_t)INT_MAX) {
        if (found) *found = 0;
        return 0;
    }
    size_t lo = 0, n = len;
    while (n > 1) {
        size_t half = n >> 1;
        lo += ((size_t)(slice[lo + half] < target)) * half;
        n -= half;
    }
    if (n > 0) lo += (size_t)(slice[lo] < target);
    if (found) *found = (lo < len && slice[lo] == target);
    return (int)lo;
}

int neverc_slices_min_int(const int *slice, size_t len) {
    if (!slice || len == 0 || len > (size_t)INT_MAX) return -1;
    int mi = 0;
    int mv = slice[0];
    for (size_t i = 1; i < len; i++) {
        if (slice[i] < mv) { mv = slice[i]; mi = (int)i; }
    }
    return mi;
}

int neverc_slices_max_int(const int *slice, size_t len) {
    if (!slice || len == 0 || len > (size_t)INT_MAX) return -1;
    int mi = 0;
    int mv = slice[0];
    for (size_t i = 1; i < len; i++) {
        if (slice[i] > mv) { mv = slice[i]; mi = (int)i; }
    }
    return mi;
}

int neverc_slices_is_sorted_ints(const int *slice, size_t len) {
    if (len <= 1) return 1;
    if (!slice) return 0;
    for (size_t i = 1; i < len; i++) {
        if (slice[i - 1] > slice[i]) return 0;
    }
    return 1;
}

void neverc_slices_sort_stable(void *slice, size_t len, size_t elem_size, neverc_cmp_func_t cmp) {
    if (!slice || len <= 1 || elem_size == 0 || !cmp ||
        len > SIZE_MAX / elem_size)
        return;
    nci_timsort(slice, len, elem_size, (nci_cmp_fn)cmp);
}

size_t neverc_slices_delete(void *slice, size_t len, size_t elem_size, size_t i, size_t j) {
    if (!slice || elem_size == 0 || i >= j || j > len ||
        len > SIZE_MAX / elem_size)
        return len;
    char *p = (char *)slice;
    size_t tail = len - j;
    if (tail > 0) memmove(p + i * elem_size, p + j * elem_size, tail * elem_size);
    size_t new_len = len - (j - i);
    slices_clear_tail(p, new_len, len, elem_size);
    return new_len;
}

size_t neverc_slices_insert(void *slice, size_t len, size_t elem_size,
                             size_t i, const void *elems, size_t count) {
    if (count == 0) return len;
    if (!slice || !elems || elem_size == 0 || i > len ||
        len > SIZE_MAX - count || count > SIZE_MAX / elem_size)
        return len;
    size_t new_len = len + count;
    if (new_len > SIZE_MAX / elem_size) return len;

    char *p = (char *)slice;
    size_t input_bytes = count * elem_size;
    void *input_copy = NULL;
    if (slices_ranges_overlap(elems, input_bytes, slice,
                              new_len * elem_size)) {
        input_copy = malloc(input_bytes);
        if (!input_copy) return len;
        memcpy(input_copy, elems, input_bytes);
        elems = input_copy;
    }

    size_t tail = len - i;
    if (tail > 0)
        memmove(p + (i + count) * elem_size, p + i * elem_size,
                tail * elem_size);
    memcpy(p + i * elem_size, elems, input_bytes);
    free(input_copy);
    return new_len;
}

size_t neverc_slices_replace(void *slice, size_t len, size_t elem_size,
                              size_t i, size_t j, const void *elems, size_t count) {
    if (!slice || elem_size == 0 || i > j || j > len ||
        len > SIZE_MAX / elem_size)
        return len;
    size_t removed = j - i;
    size_t retained = len - removed;
    if (count > SIZE_MAX - retained || count > SIZE_MAX / elem_size)
        return len;
    size_t new_len = retained + count;
    if (new_len > SIZE_MAX / elem_size || (count > 0 && !elems)) return len;

    char *p = (char *)slice;
    size_t input_bytes = count * elem_size;
    void *input_copy = NULL;
    size_t touched_len = len > new_len ? len : new_len;
    if (slices_ranges_overlap(elems, input_bytes, slice,
                              touched_len * elem_size)) {
        input_copy = malloc(input_bytes);
        if (!input_copy) return len;
        memcpy(input_copy, elems, input_bytes);
        elems = input_copy;
    }

    size_t tail = len - j;
    if (count != removed && tail > 0)
        memmove(p + (i + count) * elem_size, p + j * elem_size,
                tail * elem_size);
    if (count > 0) memcpy(p + i * elem_size, elems, input_bytes);
    slices_clear_tail(p, new_len, len, elem_size);
    free(input_copy);
    return new_len;
}

void *neverc_slices_concat(const void *s1, size_t len1, const void *s2, size_t len2,
                            size_t elem_size) {
    if (len1 > SIZE_MAX - len2) return NULL;
    size_t total = len1 + len2;
    if (total == 0) return NULL;
    size_t bytes;
    if ((len1 > 0 && !s1) || (len2 > 0 && !s2) ||
        !slices_byte_size(total, elem_size, &bytes))
        return NULL;
    void *out = malloc(bytes);
    if (!out) return NULL;
    if (len1 > 0) memcpy(out, s1, len1 * elem_size);
    if (len2 > 0) memcpy((char *)out + len1 * elem_size, s2, len2 * elem_size);
    return out;
}

int neverc_slices_clip(size_t len, size_t cap, size_t *out_cap) {
    if (!out_cap || cap < len) return 0;
    *out_cap = len;
    return 1;
}

void *neverc_slices_grow(void *slice, size_t len, size_t cap, size_t n,
                         size_t elem_size, size_t *out_cap) {
    if (!out_cap || elem_size == 0 || cap < len)
        return NULL;
    if ((len > 0 || cap > 0) && !slice)
        return NULL;
    /* leftover = cap - len; after Clip, leftover is 0 so n extra elements
     * require a new allocation of exactly len+n, not a wrapping cap+n. */
    if (n <= cap - len) {
        *out_cap = cap;
        return slice;
    }
    if (n > SIZE_MAX - len)
        return NULL;
    size_t new_cap = len + n;
    if (new_cap > SIZE_MAX / elem_size)
        return NULL;
    void *out = realloc(slice, new_cap * elem_size);
    if (!out)
        return NULL;
    *out_cap = new_cap;
    return out;
}

int neverc_slices_contains_func(const void *slice, size_t len, size_t elem_size,
                                 neverc_slices_pred_func_t f) {
    return neverc_slices_index_func(slice, len, elem_size, f) >= 0;
}

int neverc_slices_index_func(const void *slice, size_t len, size_t elem_size,
                              neverc_slices_pred_func_t f) {
    if (!slice || !f || elem_size == 0 || len == 0 ||
        len > SIZE_MAX / elem_size || len > (size_t)INT_MAX)
        return -1;
    const char *p = (const char *)slice;
    for (size_t i = 0; i < len; i++)
        if (f(p + i * elem_size)) return (int)i;
    return -1;
}

size_t neverc_slices_delete_func(void *slice, size_t len, size_t elem_size,
                                  neverc_slices_pred_func_t f) {
    if (!slice || !f || elem_size == 0) return len;
    if (len > 0 && len > SIZE_MAX / elem_size) return len;
    char *p = (char *)slice;
    size_t w = 0, r = 0;
    while (r < len) {
        if (f(p + r * elem_size)) {
            r++;
            continue;
        }
        size_t run_start = r;
        r++;
        while (r < len && !f(p + r * elem_size))
            r++;
        size_t run_len = r - run_start;
        if (w != run_start)
            memmove(p + w * elem_size, p + run_start * elem_size, run_len * elem_size);
        w += run_len;
    }
    slices_clear_tail(p, w, len, elem_size);
    return w;
}
