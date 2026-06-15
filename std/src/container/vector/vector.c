#include <neverc/std/container/vector.h>
#include "../../sort/sort_impl.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VEC_INITIAL_CAP 8
#define VEC_GROWTH_FACTOR 2

static inline void *vec_elem_ptr(const neverc_vector_t *v, size_t index) {
    return (char *)v->data + index * v->elem_size;
}

static bool vec_grow(neverc_vector_t *v, size_t min_cap) {
    if (v->capacity >= min_cap)
        return true;
    size_t new_cap = v->capacity ? v->capacity : VEC_INITIAL_CAP;
    while (new_cap < min_cap) {
        size_t doubled = new_cap * VEC_GROWTH_FACTOR;
        if (doubled <= new_cap)
            return false;
        new_cap = doubled;
    }
    size_t alloc_size = new_cap * v->elem_size;
    if (v->elem_size != 0 && alloc_size / v->elem_size != new_cap)
        return false;
    void *new_data = realloc(v->data, alloc_size);
    if (!new_data)
        return false;
    v->data = new_data;
    v->capacity = new_cap;
    return true;
}

/* ===== Construction / Destruction ===== */

neverc_vector_t *neverc_vector_new(size_t elem_size) {
    return neverc_vector_new_with_capacity(elem_size, 0);
}

neverc_vector_t *neverc_vector_new_with_capacity(size_t elem_size, size_t cap) {
    if (elem_size == 0)
        return NULL;
    neverc_vector_t *v = (neverc_vector_t *)calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->elem_size = elem_size;
    if (cap > 0) {
        v->data = malloc(cap * elem_size);
        if (!v->data) {
            free(v);
            return NULL;
        }
        v->capacity = cap;
    }
    return v;
}

neverc_vector_t *neverc_vector_new_with_size(size_t elem_size, size_t count,
                                              const void *fill_value) {
    neverc_vector_t *v = neverc_vector_new_with_capacity(elem_size, count);
    if (!v)
        return NULL;
    if (count > 0) {
        if (fill_value) {
            for (size_t i = 0; i < count; i++)
                memcpy(vec_elem_ptr(v, i), fill_value, elem_size);
        } else {
            memset(v->data, 0, count * elem_size);
        }
        v->size = count;
    }
    return v;
}

neverc_vector_t *neverc_vector_copy(const neverc_vector_t *src) {
    if (!src)
        return NULL;
    neverc_vector_t *v = neverc_vector_new_with_capacity(src->elem_size,
                                                          src->size);
    if (!v)
        return NULL;
    if (src->size > 0) {
        memcpy(v->data, src->data, src->size * src->elem_size);
        size_t n = src->size;
        v->size = n;
    }
    return v;
}

neverc_vector_t *neverc_vector_from_array(const void *arr, size_t count,
                                           size_t elem_size) {
    if (!arr && count > 0)
        return NULL;
    neverc_vector_t *v = neverc_vector_new_with_capacity(elem_size, count);
    if (!v)
        return NULL;
    if (count > 0) {
        memcpy(v->data, arr, count * elem_size);
        v->size = count;
    }
    return v;
}

void neverc_vector_free(neverc_vector_t *v) {
    if (!v)
        return;
    free(v->data);
    free(v);
}

void neverc_vector_init(neverc_vector_t *v, size_t elem_size) {
    if (!v)
        return;
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->elem_size = elem_size;
}

void neverc_vector_destroy(neverc_vector_t *v) {
    if (!v)
        return;
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

/* ===== Element Access ===== */

void *neverc_vector_at(const neverc_vector_t *v, size_t index) {
    if (!v || index >= v->size)
        return NULL;
    return vec_elem_ptr(v, index);
}

void *neverc_vector_front(const neverc_vector_t *v) {
    return neverc_vector_at(v, 0);
}

void *neverc_vector_back(const neverc_vector_t *v) {
    if (!v || v->size == 0)
        return NULL;
    return vec_elem_ptr(v, v->size - 1);
}

void *neverc_vector_data(const neverc_vector_t *v) {
    return v ? v->data : NULL;
}

bool neverc_vector_get(const neverc_vector_t *v, size_t index, void *out) {
    if (!v || !out || index >= v->size)
        return false;
    memcpy(out, vec_elem_ptr(v, index), v->elem_size);
    return true;
}

bool neverc_vector_set(neverc_vector_t *v, size_t index, const void *value) {
    if (!v || !value || index >= v->size)
        return false;
    memcpy(vec_elem_ptr(v, index), value, v->elem_size);
    return true;
}

/* ===== Capacity ===== */

bool neverc_vector_empty(const neverc_vector_t *v) {
    return !v || v->size == 0;
}

size_t neverc_vector_size(const neverc_vector_t *v) {
    return v ? v->size : 0;
}

size_t neverc_vector_capacity(const neverc_vector_t *v) {
    return v ? v->capacity : 0;
}

size_t neverc_vector_elem_size(const neverc_vector_t *v) {
    return v ? v->elem_size : 0;
}

size_t neverc_vector_max_size(const neverc_vector_t *v) {
    if (!v || v->elem_size == 0)
        return 0;
    return (size_t)-1 / v->elem_size;
}

bool neverc_vector_reserve(neverc_vector_t *v, size_t new_cap) {
    if (!v)
        return false;
    return vec_grow(v, new_cap);
}

bool neverc_vector_shrink_to_fit(neverc_vector_t *v) {
    if (!v)
        return false;
    if (v->size == v->capacity)
        return true;
    if (v->size == 0) {
        free(v->data);
        v->data = NULL;
        v->capacity = 0;
        return true;
    }
    void *new_data = realloc(v->data, v->size * v->elem_size);
    if (!new_data)
        return false;
    v->data = new_data;
    v->capacity = v->size;
    return true;
}

/* ===== Modifiers ===== */

bool neverc_vector_push_back(neverc_vector_t *v, const void *value) {
    if (!v || !value)
        return false;
    if (!vec_grow(v, v->size + 1))
        return false;
    memcpy(vec_elem_ptr(v, v->size), value, v->elem_size);
    v->size++;
    return true;
}

bool neverc_vector_pop_back(neverc_vector_t *v, void *out) {
    if (!v || v->size == 0)
        return false;
    v->size--;
    if (out)
        memcpy(out, vec_elem_ptr(v, v->size), v->elem_size);
    return true;
}

bool neverc_vector_insert(neverc_vector_t *v, size_t index,
                           const void *value) {
    if (!v || !value || index > v->size)
        return false;
    if (!vec_grow(v, v->size + 1))
        return false;
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + 1), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    memcpy(vec_elem_ptr(v, index), value, v->elem_size);
    v->size++;
    return true;
}

bool neverc_vector_insert_range(neverc_vector_t *v, size_t index,
                                 const void *values, size_t count) {
    if (!v || index > v->size)
        return false;
    if (count == 0)
        return true;
    if (!values)
        return false;
    if (!vec_grow(v, v->size + count))
        return false;
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + count), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    memcpy(vec_elem_ptr(v, index), values, count * v->elem_size);
    v->size += count;
    return true;
}

bool neverc_vector_insert_fill(neverc_vector_t *v, size_t index,
                                size_t count, const void *value) {
    if (!v || index > v->size || !value)
        return false;
    if (count == 0)
        return true;
    if (!vec_grow(v, v->size + count))
        return false;
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + count), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    char *dst = (char *)vec_elem_ptr(v, index);
    size_t sz = v->elem_size;
    if (sz == 1) {
        memset(dst, *(const unsigned char *)value, count);
    } else {
        memcpy(dst, value, sz);
        for (size_t copied = 1; copied < count; ) {
            size_t chunk = count - copied;
            if (chunk > copied) chunk = copied;
            memcpy(dst + copied * sz, dst, chunk * sz);
            copied += chunk;
        }
    }
    v->size += count;
    return true;
}

bool neverc_vector_erase(neverc_vector_t *v, size_t index) {
    if (!v || index >= v->size)
        return false;
    if (index < v->size - 1) {
        memmove(vec_elem_ptr(v, index), vec_elem_ptr(v, index + 1),
                (v->size - index - 1) * v->elem_size);
    }
    v->size--;
    return true;
}

bool neverc_vector_erase_range(neverc_vector_t *v, size_t first, size_t count) {
    if (!v || first >= v->size || count == 0)
        return false;
    if (first + count > v->size)
        count = v->size - first;
    size_t after = v->size - first - count;
    if (after > 0) {
        memmove(vec_elem_ptr(v, first), vec_elem_ptr(v, first + count),
                after * v->elem_size);
    }
    v->size -= count;
    return true;
}

void neverc_vector_clear(neverc_vector_t *v) {
    if (v)
        v->size = 0;
}

bool neverc_vector_resize(neverc_vector_t *v, size_t new_size,
                           const void *fill_value) {
    if (!v)
        return false;
    if (new_size <= v->size) {
        v->size = new_size;
        return true;
    }
    if (!vec_grow(v, new_size))
        return false;
    if (fill_value) {
        for (size_t i = v->size; i < new_size; i++)
            memcpy(vec_elem_ptr(v, i), fill_value, v->elem_size);
    } else {
        memset(vec_elem_ptr(v, v->size), 0,
               (new_size - v->size) * v->elem_size);
    }
    v->size = new_size;
    return true;
}

void neverc_vector_swap(neverc_vector_t *a, neverc_vector_t *b) {
    if (!a || !b)
        return;
    neverc_vector_t tmp = *a;
    *a = *b;
    *b = tmp;
}

bool neverc_vector_assign(neverc_vector_t *v, const void *values,
                           size_t count) {
    if (!v)
        return false;
    v->size = 0;
    if (count == 0)
        return true;
    if (!values)
        return false;
    if (!vec_grow(v, count))
        return false;
    memcpy(v->data, values, count * v->elem_size);
    v->size = count;
    return true;
}

bool neverc_vector_append(neverc_vector_t *v, const neverc_vector_t *other) {
    if (!v || !other || other->size == 0)
        return v != NULL;
    if (v->elem_size != other->elem_size)
        return false;
    return neverc_vector_insert_range(v, v->size, other->data, other->size);
}

/* ===== Algorithms ===== */

void neverc_vector_sort(neverc_vector_t *v, neverc_vector_cmp_fn cmp) {
    if (!v || v->size < 2 || !cmp)
        return;
    nci_pdqsort(v->data, v->size, v->elem_size, (nci_cmp_fn)cmp);
}

void neverc_vector_reverse(neverc_vector_t *v) {
    if (!v || v->size < 2)
        return;
    char tmp[256];
    char *buf = v->elem_size <= sizeof(tmp) ? tmp :
                (char *)malloc(v->elem_size);
    if (!buf)
        return;
    size_t lo = 0, hi = v->size - 1;
    while (lo < hi) {
        void *a = vec_elem_ptr(v, lo);
        void *b = vec_elem_ptr(v, hi);
        memcpy(buf, a, v->elem_size);
        memcpy(a, b, v->elem_size);
        memcpy(b, buf, v->elem_size);
        lo++;
        hi--;
    }
    if (buf != tmp)
        free(buf);
}

void neverc_vector_unique(neverc_vector_t *v, neverc_vector_cmp_fn cmp) {
    if (!v || v->size < 2 || !cmp)
        return;
    size_t sz = v->elem_size;
    size_t w = 1, r = 1;
    while (r < v->size) {
        if (cmp(vec_elem_ptr(v, r), vec_elem_ptr(v, w - 1)) == 0) {
            r++;
            continue;
        }
        size_t run_start = r;
        r++;
        while (r < v->size && cmp(vec_elem_ptr(v, r), vec_elem_ptr(v, r - 1)) != 0)
            r++;
        size_t run_len = r - run_start;
        if (w != run_start)
            memmove(vec_elem_ptr(v, w), vec_elem_ptr(v, run_start), run_len * sz);
        w += run_len;
    }
    v->size = w;
}

long neverc_vector_find(const neverc_vector_t *v, const void *value,
                         neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp)
        return -1;
    for (size_t i = 0; i < v->size; i++) {
        if (cmp(vec_elem_ptr(v, i), value) == 0)
            return (long)i;
    }
    return -1;
}

long neverc_vector_find_if(const neverc_vector_t *v,
                            bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return -1;
    for (size_t i = 0; i < v->size; i++) {
        if (pred(vec_elem_ptr(v, i)))
            return (long)i;
    }
    return -1;
}

bool neverc_vector_contains(const neverc_vector_t *v, const void *value,
                             neverc_vector_cmp_fn cmp) {
    return neverc_vector_find(v, value, cmp) >= 0;
}

size_t neverc_vector_count(const neverc_vector_t *v, const void *value,
                            neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp)
        return 0;
    size_t c = 0;
    for (size_t i = 0; i < v->size; i++) {
        if (cmp(vec_elem_ptr(v, i), value) == 0)
            c++;
    }
    return c;
}

size_t neverc_vector_count_if(const neverc_vector_t *v,
                               bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return 0;
    size_t c = 0;
    for (size_t i = 0; i < v->size; i++) {
        if (pred(vec_elem_ptr(v, i)))
            c++;
    }
    return c;
}

void neverc_vector_foreach(const neverc_vector_t *v,
                            void (*fn)(void *elem, void *ctx), void *ctx) {
    if (!v || !fn)
        return;
    for (size_t i = 0; i < v->size; i++)
        fn(vec_elem_ptr(v, i), ctx);
}

bool neverc_vector_any(const neverc_vector_t *v,
                        bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return false;
    for (size_t i = 0; i < v->size; i++) {
        if (pred(vec_elem_ptr(v, i)))
            return true;
    }
    return false;
}

bool neverc_vector_all(const neverc_vector_t *v,
                        bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return false;
    for (size_t i = 0; i < v->size; i++) {
        if (!pred(vec_elem_ptr(v, i)))
            return false;
    }
    return true;
}

bool neverc_vector_none(const neverc_vector_t *v,
                         bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return true;
    for (size_t i = 0; i < v->size; i++) {
        if (pred(vec_elem_ptr(v, i)))
            return false;
    }
    return true;
}

/* ===== Emplace ===== */

void *neverc_vector_emplace_back(neverc_vector_t *v) {
    if (!v)
        return NULL;
    if (!vec_grow(v, v->size + 1))
        return NULL;
    void *ptr = vec_elem_ptr(v, v->size);
    memset(ptr, 0, v->elem_size);
    v->size++;
    return ptr;
}

void *neverc_vector_emplace(neverc_vector_t *v, size_t index) {
    if (!v || index > v->size)
        return NULL;
    if (!vec_grow(v, v->size + 1))
        return NULL;
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + 1), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    void *ptr = vec_elem_ptr(v, index);
    memset(ptr, 0, v->elem_size);
    v->size++;
    return ptr;
}

/* ===== Advanced Modifiers ===== */

size_t neverc_vector_erase_if(neverc_vector_t *v,
                               bool (*pred)(const void *elem)) {
    if (!v || !pred || v->size == 0)
        return 0;
    size_t sz = v->elem_size;
    size_t w = 0, r = 0;
    while (r < v->size) {
        if (pred(vec_elem_ptr(v, r))) {
            r++;
            continue;
        }
        size_t run_start = r;
        r++;
        while (r < v->size && !pred(vec_elem_ptr(v, r)))
            r++;
        size_t run_len = r - run_start;
        if (w != run_start)
            memmove(vec_elem_ptr(v, w), vec_elem_ptr(v, run_start), run_len * sz);
        w += run_len;
    }
    size_t removed = v->size - w;
    v->size = w;
    return removed;
}

void neverc_vector_fill(neverc_vector_t *v, const void *value) {
    if (!v || !value || v->size == 0)
        return;
    size_t sz = v->elem_size;
    if (sz == 1) {
        memset(v->data, *(const unsigned char *)value, v->size);
        return;
    }
    memcpy(v->data, value, sz);
    for (size_t copied = 1; copied < v->size; ) {
        size_t chunk = v->size - copied;
        if (chunk > copied) chunk = copied;
        memcpy((char *)v->data + copied * sz, v->data, chunk * sz);
        copied += chunk;
    }
}

void neverc_vector_swap_elements(neverc_vector_t *v, size_t i, size_t j) {
    if (!v || i >= v->size || j >= v->size || i == j)
        return;
    char tmp[256];
    char *buf = v->elem_size <= sizeof(tmp) ? tmp :
                (char *)malloc(v->elem_size);
    if (!buf)
        return;
    memcpy(buf, vec_elem_ptr(v, i), v->elem_size);
    memcpy(vec_elem_ptr(v, i), vec_elem_ptr(v, j), v->elem_size);
    memcpy(vec_elem_ptr(v, j), buf, v->elem_size);
    if (buf != tmp)
        free(buf);
}

static void vec_reverse_range(neverc_vector_t *v, size_t lo, size_t hi) {
    char tmp[256];
    char *buf = v->elem_size <= sizeof(tmp) ? tmp :
                (char *)malloc(v->elem_size);
    if (!buf)
        return;
    while (lo < hi) {
        memcpy(buf, vec_elem_ptr(v, lo), v->elem_size);
        memcpy(vec_elem_ptr(v, lo), vec_elem_ptr(v, hi), v->elem_size);
        memcpy(vec_elem_ptr(v, hi), buf, v->elem_size);
        lo++;
        hi--;
    }
    if (buf != tmp)
        free(buf);
}

void neverc_vector_rotate(neverc_vector_t *v, size_t mid) {
    if (!v || v->size < 2 || mid == 0 || mid >= v->size)
        return;
    vec_reverse_range(v, 0, mid - 1);
    vec_reverse_range(v, mid, v->size - 1);
    vec_reverse_range(v, 0, v->size - 1);
}

size_t neverc_vector_partition(neverc_vector_t *v,
                                bool (*pred)(const void *elem)) {
    if (!v || !pred || v->size == 0)
        return 0;
    char tmp[256];
    char *buf = v->elem_size <= sizeof(tmp) ? tmp :
                (char *)malloc(v->elem_size);
    if (!buf)
        return 0;
    size_t lo = 0, hi = v->size - 1;
    while (lo < hi) {
        while (lo < hi && pred(vec_elem_ptr(v, lo)))
            lo++;
        while (lo < hi && !pred(vec_elem_ptr(v, hi)))
            hi--;
        if (lo < hi) {
            memcpy(buf, vec_elem_ptr(v, lo), v->elem_size);
            memcpy(vec_elem_ptr(v, lo), vec_elem_ptr(v, hi), v->elem_size);
            memcpy(vec_elem_ptr(v, hi), buf, v->elem_size);
            lo++;
            hi--;
        }
    }
    if (buf != tmp)
        free(buf);
    size_t boundary = pred(vec_elem_ptr(v, lo)) ? lo + 1 : lo;
    return boundary;
}

void neverc_vector_generate(neverc_vector_t *v,
                             void (*gen)(void *elem, size_t index, void *ctx),
                             void *ctx) {
    if (!v || !gen)
        return;
    for (size_t i = 0; i < v->size; i++)
        gen(vec_elem_ptr(v, i), i, ctx);
}

/* ===== Sorted-Vector Operations ===== */

long neverc_vector_binary_search(const neverc_vector_t *v,
                                  const void *value,
                                  neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp || v->size == 0)
        return -1;
    size_t lo = 0, n = v->size;
    while (n > 1) {
        size_t half = n >> 1;
        lo += ((size_t)(cmp(vec_elem_ptr(v, lo + half), value) < 0)) * half;
        n -= half;
    }
    if (cmp(vec_elem_ptr(v, lo), value) < 0) lo++;
    if (lo < v->size && cmp(vec_elem_ptr(v, lo), value) == 0)
        return (long)lo;
    return -1;
}

long neverc_vector_lower_bound(const neverc_vector_t *v,
                                const void *value,
                                neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp)
        return 0;
    size_t lo = 0, n = v->size;
    while (n > 1) {
        size_t half = n >> 1;
        lo += ((size_t)(cmp(vec_elem_ptr(v, lo + half), value) < 0)) * half;
        n -= half;
    }
    if (n > 0 && cmp(vec_elem_ptr(v, lo), value) < 0) lo++;
    return (long)lo;
}

long neverc_vector_upper_bound(const neverc_vector_t *v,
                                const void *value,
                                neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp)
        return 0;
    size_t lo = 0, n = v->size;
    while (n > 1) {
        size_t half = n >> 1;
        lo += ((size_t)(cmp(vec_elem_ptr(v, lo + half), value) <= 0)) * half;
        n -= half;
    }
    if (n > 0 && cmp(vec_elem_ptr(v, lo), value) <= 0) lo++;
    return (long)lo;
}

bool neverc_vector_is_sorted(const neverc_vector_t *v,
                              neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size < 2)
        return true;
    for (size_t i = 1; i < v->size; i++) {
        if (cmp(vec_elem_ptr(v, i - 1), vec_elem_ptr(v, i)) > 0)
            return false;
    }
    return true;
}

/* ===== Sorting Variants ===== */

void neverc_vector_stable_sort(neverc_vector_t *v,
                                neverc_vector_cmp_fn cmp) {
    if (!v || v->size < 2 || !cmp)
        return;
    nci_timsort(v->data, v->size, v->elem_size, (nci_cmp_fn)cmp);
}

void neverc_vector_partial_sort(neverc_vector_t *v, size_t k,
                                 neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size < 2 || k == 0)
        return;
    if (k > v->size)
        k = v->size;
    char *arr = (char *)v->data;
    size_t sz = v->elem_size;
    char stack_tmp[256];
    char *buf = sz <= sizeof(stack_tmp) ? stack_tmp : (char *)malloc(sz);
    if (!buf)
        return;

    nci_cmp_fn icmp = (nci_cmp_fn)cmp;

    for (size_t i = k / 2; i > 0; i--)
        nci_sift_down(arr, k, sz, icmp, buf, i - 1);

    for (size_t i = k; i < v->size; i++) {
        if (cmp(arr + i * sz, arr) < 0) {
            memcpy(buf, arr, sz);
            memcpy(arr, arr + i * sz, sz);
            memcpy(arr + i * sz, buf, sz);
            nci_sift_down(arr, k, sz, icmp, buf, 0);
        }
    }

    nci_heapsort(arr, k, sz, icmp, buf);

    if (buf != stack_tmp)
        free(buf);
}

void neverc_vector_nth_element(neverc_vector_t *v, size_t k,
                               neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size < 2 || k >= v->size)
        return;
    nci_nth_element(v->data, v->size, v->elem_size, (nci_cmp_fn)cmp, k);
}

/* ===== Shared in-place rotation helpers (reused by merge / partition) ===== */

/* Reverse the half-open range [lo, hi) using a caller-provided element scratch
 * so a deep recursion never re-allocates per rotation. */
static void vec_reverse_buf(neverc_vector_t *v, size_t lo, size_t hi,
                            char *tmp) {
    if (hi <= lo)
        return;
    size_t es = v->elem_size;
    size_t i = lo, j = hi - 1;
    while (i < j) {
        memcpy(tmp, vec_elem_ptr(v, i), es);
        memcpy(vec_elem_ptr(v, i), vec_elem_ptr(v, j), es);
        memcpy(vec_elem_ptr(v, j), tmp, es);
        i++;
        j--;
    }
}

/* Rotate [lo, hi) left so the block starting at `mid` comes first
 * (reversal algorithm: O(n), no extra storage beyond one element). */
static void vec_rotate_buf(neverc_vector_t *v, size_t lo, size_t mid,
                           size_t hi, char *tmp) {
    if (mid <= lo || mid >= hi)
        return;
    vec_reverse_buf(v, lo, mid, tmp);
    vec_reverse_buf(v, mid, hi, tmp);
    vec_reverse_buf(v, lo, hi, tmp);
}

/* ===== inplace_merge ===== */

/* first index in [lo,hi) whose element is not < key (std lower_bound) */
static size_t vec_lower_bound_idx(neverc_vector_t *v, size_t lo, size_t hi,
                                  const void *key, nci_cmp_fn cmp) {
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (cmp(vec_elem_ptr(v, m), key) < 0) lo = m + 1;
        else hi = m;
    }
    return lo;
}

/* first index in [lo,hi) whose element is > key (std upper_bound) */
static size_t vec_upper_bound_idx(neverc_vector_t *v, size_t lo, size_t hi,
                                  const void *key, nci_cmp_fn cmp) {
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (cmp(key, vec_elem_ptr(v, m)) < 0) hi = m;
        else lo = m + 1;
    }
    return lo;
}

/*
 * Kronrod's symmetric rotation merge of the sorted runs [lo,mid) and [mid,hi):
 * O(n log n), stable, and allocation-free (one element of scratch). Used as the
 * fallback when the O(n) buffer cannot be allocated. Stability mirrors
 * libstdc++ __merge_without_buffer: split the longer run at its midpoint and
 * locate the cut in the other run with lower_bound when the pivot comes from
 * the left run (equal right elements stay after it) and upper_bound when it
 * comes from the right run (equal left elements stay before it).
 */
static void vec_merge_rotate(neverc_vector_t *v, size_t lo, size_t mid,
                             size_t hi, nci_cmp_fn cmp, char *tmp) {
    size_t len1 = mid - lo, len2 = hi - mid;
    if (len1 == 0 || len2 == 0)
        return;
    if (len1 + len2 == 2) {
        if (cmp(vec_elem_ptr(v, mid), vec_elem_ptr(v, lo)) < 0) {
            size_t es = v->elem_size;
            memcpy(tmp, vec_elem_ptr(v, lo), es);
            memcpy(vec_elem_ptr(v, lo), vec_elem_ptr(v, mid), es);
            memcpy(vec_elem_ptr(v, mid), tmp, es);
        }
        return;
    }
    size_t mid1, mid2;
    if (len1 >= len2) {
        mid1 = lo + len1 / 2;
        mid2 = vec_lower_bound_idx(v, mid, hi, vec_elem_ptr(v, mid1), cmp);
    } else {
        mid2 = mid + len2 / 2;
        mid1 = vec_upper_bound_idx(v, lo, mid, vec_elem_ptr(v, mid2), cmp);
    }
    size_t newmid = mid1 + (mid2 - mid);
    vec_rotate_buf(v, mid1, mid, mid2, tmp);
    vec_merge_rotate(v, lo, mid1, newmid, cmp, tmp);
    vec_merge_rotate(v, newmid, mid2, hi, cmp, tmp);
}

void neverc_vector_inplace_merge(neverc_vector_t *v, size_t mid,
                                 neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || mid == 0 || mid >= v->size)
        return;
    size_t es = v->elem_size, n = v->size;
    size_t len1 = mid, len2 = n - mid;
    size_t bufn = len1 < len2 ? len1 : len2;   /* gallop-merge buffers the smaller run */

    char stack_buf[256];
    char *aux = (bufn * es <= sizeof(stack_buf)) ? stack_buf
                                                 : (char *)malloc(bufn * es);
    if (aux) {
        nci_tim_merge((char *)v->data, 0, len1, len2, es, (nci_cmp_fn)cmp, aux);
        if (aux != stack_buf)
            free(aux);
        return;
    }
    /* OOM: allocation-free rotation merge (still stable, O(n log n)). */
    char el_buf[256];
    char *tmp = es <= sizeof(el_buf) ? el_buf : (char *)malloc(es);
    if (!tmp)
        return;
    vec_merge_rotate(v, 0, mid, n, (nci_cmp_fn)cmp, tmp);
    if (tmp != el_buf)
        free(tmp);
}

/* ===== Randomized & Partition Algorithms ===== */

/* splitmix64: a high-quality, fully portable 64-bit generator — no 128-bit
 * math and no platform intrinsics, so shuffles/samples are reproducible and
 * identical on every target. */
static inline uint64_t vec_sm64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Unbiased uniform in [0, bound) by Lemire-style rejection using only 64-bit
 * arithmetic: discard the short biased tail, then take a modulo. bound > 0. */
static uint64_t vec_rand_bounded(uint64_t *s, uint64_t bound) {
    uint64_t threshold = (0ULL - bound) % bound;   /* == 2^64 mod bound */
    uint64_t r;
    do { r = vec_sm64(s); } while (r < threshold);
    return r % bound;
}

void neverc_vector_shuffle(neverc_vector_t *v, uint64_t seed) {
    if (!v || v->size < 2)
        return;
    size_t es = v->elem_size;
    char stack_buf[256];
    char *tmp = es <= sizeof(stack_buf) ? stack_buf : (char *)malloc(es);
    if (!tmp)
        return;
    uint64_t s = seed;
    for (size_t i = v->size - 1; i > 0; i--) {
        size_t j = (size_t)vec_rand_bounded(&s, (uint64_t)i + 1);
        if (j != i) {
            memcpy(tmp, vec_elem_ptr(v, i), es);
            memcpy(vec_elem_ptr(v, i), vec_elem_ptr(v, j), es);
            memcpy(vec_elem_ptr(v, j), tmp, es);
        }
    }
    if (tmp != stack_buf)
        free(tmp);
}

/* Allocation-free stable partition, O(n log n): partition each half, then
 * rotate the second half's true-group in front of the first half's
 * false-group. Used when the O(n) buffer cannot be allocated. */
static size_t vec_stable_part_rotate(neverc_vector_t *v, size_t lo, size_t hi,
                                     bool (*pred)(const void *), char *tmp) {
    size_t n = hi - lo;
    if (n == 0)
        return lo;
    if (n == 1)
        return pred(vec_elem_ptr(v, lo)) ? hi : lo;
    size_t mid = lo + n / 2;
    size_t left = vec_stable_part_rotate(v, lo, mid, pred, tmp);
    size_t right = vec_stable_part_rotate(v, mid, hi, pred, tmp);
    vec_rotate_buf(v, left, mid, right, tmp);
    return left + (right - mid);
}

size_t neverc_vector_stable_partition(neverc_vector_t *v,
                                      bool (*pred)(const void *elem)) {
    if (!v || !pred || v->size == 0)
        return 0;
    size_t es = v->elem_size, n = v->size;

    size_t ntrue = 0;
    for (size_t i = 0; i < n; i++)
        if (pred(vec_elem_ptr(v, i)))
            ntrue++;
    if (ntrue == 0 || ntrue == n)
        return ntrue;

    /* O(n) fast path: compact the true group to the front in place while
     * staging the false group in a buffer, then append the buffer. Stable
     * because both groups are visited left to right. */
    size_t nfalse = n - ntrue;
    char *buf = (char *)malloc(nfalse * es);
    if (buf) {
        size_t w = 0, f = 0;
        for (size_t i = 0; i < n; i++) {
            const void *e = vec_elem_ptr(v, i);
            if (pred(e)) {
                if (w != i)
                    memcpy(vec_elem_ptr(v, w), e, es);
                w++;
            } else {
                memcpy(buf + f * es, e, es);
                f++;
            }
        }
        memcpy(vec_elem_ptr(v, w), buf, nfalse * es);
        free(buf);
        return ntrue;
    }
    /* OOM: allocation-free rotation partition (one element of scratch). */
    char el_buf[256];
    char *tmp = es <= sizeof(el_buf) ? el_buf : (char *)malloc(es);
    if (!tmp)
        return ntrue;   /* cannot reorder; report the count so callers still know */
    size_t p = vec_stable_part_rotate(v, 0, n, pred, tmp);
    if (tmp != el_buf)
        free(tmp);
    return p;
}

neverc_vector_t *neverc_vector_sample(const neverc_vector_t *v, size_t k,
                                      uint64_t seed) {
    if (!v)
        return NULL;
    size_t es = v->elem_size, n = v->size;
    if (k > n)
        k = n;
    neverc_vector_t *out = neverc_vector_new_with_capacity(es, k);
    if (!out)
        return NULL;
    if (k == 0)
        return out;
    /* Knuth Algorithm S: keep element i with probability need/remain. Produces
     * a uniform k-subset in original order, O(n), no scratch buffer. */
    uint64_t s = seed;
    size_t need = k, remain = n;
    for (size_t i = 0; i < n && need > 0; i++) {
        if (vec_rand_bounded(&s, (uint64_t)remain) < (uint64_t)need) {
            memcpy(vec_elem_ptr(out, out->size), vec_elem_ptr(v, i), es);
            out->size++;
            need--;
        }
        remain--;
    }
    return out;
}

/* ===== Reduction / Transformation ===== */

void *neverc_vector_min_element(const neverc_vector_t *v,
                                 neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size == 0)
        return NULL;
    size_t min_idx = 0;
    for (size_t i = 1; i < v->size; i++) {
        if (cmp(vec_elem_ptr(v, i), vec_elem_ptr(v, min_idx)) < 0)
            min_idx = i;
    }
    return vec_elem_ptr(v, min_idx);
}

void *neverc_vector_max_element(const neverc_vector_t *v,
                                 neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size == 0)
        return NULL;
    size_t max_idx = 0;
    for (size_t i = 1; i < v->size; i++) {
        if (cmp(vec_elem_ptr(v, i), vec_elem_ptr(v, max_idx)) > 0)
            max_idx = i;
    }
    return vec_elem_ptr(v, max_idx);
}

void neverc_vector_transform(neverc_vector_t *v,
                              void (*fn)(void *elem, void *ctx),
                              void *ctx) {
    if (!v || !fn)
        return;
    for (size_t i = 0; i < v->size; i++)
        fn(vec_elem_ptr(v, i), ctx);
}

bool neverc_vector_reduce(const neverc_vector_t *v,
                           void *accumulator,
                           void (*fn)(void *acc, const void *elem)) {
    if (!v || !accumulator || !fn || v->size == 0)
        return false;
    for (size_t i = 0; i < v->size; i++)
        fn(accumulator, vec_elem_ptr(v, i));
    return true;
}

/* ===== Slice / Subvector ===== */

neverc_vector_t *neverc_vector_slice(const neverc_vector_t *v,
                                      size_t start, size_t count) {
    if (!v || start >= v->size)
        return neverc_vector_new(v ? v->elem_size : 1);
    if (start + count > v->size)
        count = v->size - start;
    return neverc_vector_from_array(vec_elem_ptr(v, start), count,
                                    v->elem_size);
}

neverc_vector_t *neverc_vector_filter(const neverc_vector_t *v,
                                       bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return NULL;
    neverc_vector_t *result = neverc_vector_new(v->elem_size);
    if (!result)
        return NULL;
    for (size_t i = 0; i < v->size; i++) {
        if (pred(vec_elem_ptr(v, i)))
            neverc_vector_push_back(result, vec_elem_ptr(v, i));
    }
    return result;
}

neverc_vector_t *neverc_vector_map(const neverc_vector_t *v,
                                    size_t out_elem_size,
                                    void (*fn)(void *out, const void *in)) {
    if (!v || !fn || out_elem_size == 0)
        return NULL;
    neverc_vector_t *result = neverc_vector_new_with_capacity(out_elem_size,
                                                               v->size);
    if (!result)
        return NULL;
    for (size_t i = 0; i < v->size; i++) {
        void *slot = neverc_vector_emplace_back(result);
        if (!slot) {
            neverc_vector_free(result);
            return NULL;
        }
        fn(slot, vec_elem_ptr(v, i));
    }
    return result;
}

neverc_vector_t *neverc_vector_merge(const neverc_vector_t *a,
                                      const neverc_vector_t *b,
                                      neverc_vector_cmp_fn cmp) {
    if (!a && !b)
        return NULL;
    if (!a)
        return neverc_vector_copy(b);
    if (!b)
        return neverc_vector_copy(a);
    if (a->elem_size != b->elem_size || !cmp)
        return NULL;
    size_t total = a->size + b->size;
    size_t sz = a->elem_size;
    neverc_vector_t *result = neverc_vector_new_with_capacity(sz, total);
    if (!result)
        return NULL;
    char *out = (char *)result->data;
    size_t w = 0, i = 0, j = 0;
    while (i < a->size && j < b->size) {
        if (cmp(vec_elem_ptr(a, i), vec_elem_ptr(b, j)) <= 0) {
            memcpy(out + w * sz, vec_elem_ptr(a, i), sz);
            i++;
        } else {
            memcpy(out + w * sz, vec_elem_ptr(b, j), sz);
            j++;
        }
        w++;
    }
    if (i < a->size) {
        size_t rem = a->size - i;
        memcpy(out + w * sz, vec_elem_ptr(a, i), rem * sz);
        w += rem;
    }
    if (j < b->size) {
        size_t rem = b->size - j;
        memcpy(out + w * sz, vec_elem_ptr(b, j), rem * sz);
        w += rem;
    }
    result->size = w;
    return result;
}

/* ===== Iterators ===== */

void *neverc_vector_begin(const neverc_vector_t *v) {
    return v ? v->data : NULL;
}

void *neverc_vector_end(const neverc_vector_t *v) {
    if (!v || !v->data)
        return NULL;
    return (char *)v->data + v->size * v->elem_size;
}

void *neverc_vector_rbegin(const neverc_vector_t *v) {
    if (!v || v->size == 0)
        return NULL;
    return vec_elem_ptr(v, v->size - 1);
}

void *neverc_vector_rend(const neverc_vector_t *v) {
    if (!v || !v->data)
        return NULL;
    return (char *)v->data - v->elem_size;
}

/* ===== Comparison ===== */

bool neverc_vector_equal(const neverc_vector_t *a, const neverc_vector_t *b,
                          neverc_vector_cmp_fn cmp) {
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->size != b->size || a->elem_size != b->elem_size)
        return false;
    if (!cmp)
        return memcmp(a->data, b->data, a->size * a->elem_size) == 0;
    for (size_t i = 0; i < a->size; i++) {
        if (cmp(vec_elem_ptr(a, i), vec_elem_ptr(b, i)) != 0)
            return false;
    }
    return true;
}

int neverc_vector_compare(const neverc_vector_t *a, const neverc_vector_t *b,
                           neverc_vector_cmp_fn cmp) {
    if (a == b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;
    size_t min_sz = a->size < b->size ? a->size : b->size;
    for (size_t i = 0; i < min_sz; i++) {
        int r = cmp ? cmp(vec_elem_ptr(a, i), vec_elem_ptr(b, i))
                     : memcmp(vec_elem_ptr(a, i), vec_elem_ptr(b, i),
                              a->elem_size);
        if (r != 0)
            return r;
    }
    if (a->size < b->size) return -1;
    if (a->size > b->size) return 1;
    return 0;
}
