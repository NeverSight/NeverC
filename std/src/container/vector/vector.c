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

static void vec_swap_chunked(neverc_vector_t *v, size_t i, size_t j) {
    nci_swap_chunked((char *)vec_elem_ptr(v, i),
                     (char *)vec_elem_ptr(v, j), v->elem_size);
}

static bool vec_prepare_input(const neverc_vector_t *v, const void *input,
                              size_t bytes, const void **stable,
                              void **owned_copy) {
    *stable = input;
    *owned_copy = NULL;
    if (bytes == 0 || !v->data) return true;
    if (v->elem_size == 0 || v->size > v->capacity ||
        v->capacity > SIZE_MAX / v->elem_size)
        return false;

    uintptr_t base = (uintptr_t)v->data;
    uintptr_t address = (uintptr_t)input;
    if (address < base) return true;
    size_t offset = (size_t)(address - base);
    size_t allocated_bytes = v->capacity * v->elem_size;
    /* The allocation is [base, base + allocated_bytes). A distinct malloc
     * object may begin exactly at the numeric end address, so treating that
     * address as an internal one-past pointer rejects valid adjacent blocks. */
    if (offset >= allocated_bytes) return true;

    size_t used_bytes = v->size * v->elem_size;
    if (offset > used_bytes || bytes > used_bytes - offset) return false;
    void *copy = malloc(bytes);
    if (!copy) return false;
    memcpy(copy, input, bytes);
    *stable = copy;
    *owned_copy = copy;
    return true;
}

static bool vec_grow(neverc_vector_t *v, size_t min_cap) {
    if (!v || v->elem_size == 0 || min_cap > SIZE_MAX / v->elem_size)
        return false;
    if (v->capacity >= min_cap)
        return true;
    size_t max_cap = SIZE_MAX / v->elem_size;
    size_t new_cap = v->capacity ? v->capacity : VEC_INITIAL_CAP;
    if (new_cap > max_cap)
        new_cap = max_cap;
    while (new_cap < min_cap) {
        if (new_cap > max_cap / VEC_GROWTH_FACTOR)
            new_cap = max_cap;
        else
            new_cap *= VEC_GROWTH_FACTOR;
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
    if (elem_size == 0 || cap > SIZE_MAX / elem_size)
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
    if (!src || src->elem_size == 0 || src->size > src->capacity ||
        (src->size != 0 && !src->data) ||
        src->size > SIZE_MAX / src->elem_size)
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
    memmove(out, vec_elem_ptr(v, index), v->elem_size);
    return true;
}

bool neverc_vector_set(neverc_vector_t *v, size_t index, const void *value) {
    if (!v || !value || index >= v->size)
        return false;
    memmove(vec_elem_ptr(v, index), value, v->elem_size);
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
    if (v->size == SIZE_MAX)
        return false;
    const void *stable;
    void *copy;
    if (!vec_prepare_input(v, value, v->elem_size, &stable, &copy))
        return false;
    if (!vec_grow(v, v->size + 1)) {
        free(copy);
        return false;
    }
    memcpy(vec_elem_ptr(v, v->size), stable, v->elem_size);
    free(copy);
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
    if (v->size == SIZE_MAX)
        return false;
    const void *stable;
    void *copy;
    if (!vec_prepare_input(v, value, v->elem_size, &stable, &copy))
        return false;
    if (!vec_grow(v, v->size + 1)) {
        free(copy);
        return false;
    }
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + 1), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    memcpy(vec_elem_ptr(v, index), stable, v->elem_size);
    free(copy);
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
    if (v->elem_size == 0 || count > SIZE_MAX / v->elem_size ||
        count > SIZE_MAX - v->size)
        return false;
    const void *stable;
    void *copy;
    if (!vec_prepare_input(v, values, count * v->elem_size, &stable, &copy))
        return false;
    if (!vec_grow(v, v->size + count)) {
        free(copy);
        return false;
    }
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + count), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    memcpy(vec_elem_ptr(v, index), stable, count * v->elem_size);
    free(copy);
    v->size += count;
    return true;
}

bool neverc_vector_insert_fill(neverc_vector_t *v, size_t index,
                                size_t count, const void *value) {
    if (!v || index > v->size || !value)
        return false;
    if (count == 0)
        return true;
    if (v->elem_size == 0 || count > SIZE_MAX - v->size)
        return false;
    const void *stable;
    void *copy;
    if (!vec_prepare_input(v, value, v->elem_size, &stable, &copy))
        return false;
    if (!vec_grow(v, v->size + count)) {
        free(copy);
        return false;
    }
    if (index < v->size) {
        memmove(vec_elem_ptr(v, index + count), vec_elem_ptr(v, index),
                (v->size - index) * v->elem_size);
    }
    char *dst = (char *)vec_elem_ptr(v, index);
    size_t sz = v->elem_size;
    if (sz == 1) {
        memset(dst, *(const unsigned char *)stable, count);
    } else {
        memcpy(dst, stable, sz);
        for (size_t copied = 1; copied < count; ) {
            size_t chunk = count - copied;
            if (chunk > copied) chunk = copied;
            memcpy(dst + copied * sz, dst, chunk * sz);
            copied += chunk;
        }
    }
    free(copy);
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
    if (count > v->size - first)
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
    const void *stable = fill_value;
    void *copy = NULL;
    if (fill_value &&
        !vec_prepare_input(v, fill_value, v->elem_size, &stable, &copy))
        return false;
    if (!vec_grow(v, new_size)) {
        free(copy);
        return false;
    }
    if (stable) {
        for (size_t i = v->size; i < new_size; i++)
            memcpy(vec_elem_ptr(v, i), stable, v->elem_size);
    } else {
        memset(vec_elem_ptr(v, v->size), 0,
               (new_size - v->size) * v->elem_size);
    }
    free(copy);
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
    if (count == 0) {
        v->size = 0;
        return true;
    }
    if (!values || v->elem_size == 0 || count > SIZE_MAX / v->elem_size)
        return false;
    const void *stable;
    void *copy;
    if (!vec_prepare_input(v, values, count * v->elem_size, &stable, &copy))
        return false;
    if (!vec_grow(v, count)) {
        free(copy);
        return false;
    }
    memcpy(v->data, stable, count * v->elem_size);
    free(copy);
    v->size = count;
    return true;
}

bool neverc_vector_append(neverc_vector_t *v, const neverc_vector_t *other) {
    if (!v || !other || other->size == 0)
        return v != NULL;
    if (v->elem_size != other->elem_size)
        return false;
    if (other->size > other->capacity ||
        (other->size > 0 && !other->data))
        return false;
    if (v == other) {
        size_t old_size = v->size;
        if (old_size > SIZE_MAX - old_size ||
            !vec_grow(v, old_size + old_size))
            return false;
        memcpy(vec_elem_ptr(v, old_size), v->data,
               old_size * v->elem_size);
        v->size = old_size + old_size;
        return true;
    }
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
    size_t lo = 0, hi = v->size - 1;
    while (lo < hi) {
        vec_swap_chunked(v, lo, hi);
        lo++;
        hi--;
    }
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
    if (v->size == SIZE_MAX)
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
    if (v->size == SIZE_MAX)
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
    vec_swap_chunked(v, i, j);
}

static void vec_reverse_range(neverc_vector_t *v, size_t lo, size_t hi) {
    while (lo < hi) {
        vec_swap_chunked(v, lo, hi);
        lo++;
        hi--;
    }
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
    size_t lo = 0, hi = v->size - 1;
    while (lo < hi) {
        while (lo < hi && pred(vec_elem_ptr(v, lo)))
            lo++;
        while (lo < hi && !pred(vec_elem_ptr(v, hi)))
            hi--;
        if (lo < hi) {
            vec_swap_chunked(v, lo, hi);
            lo++;
            hi--;
        }
    }
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

bool neverc_vector_equal_range(const neverc_vector_t *v, const void *value,
                               neverc_vector_cmp_fn cmp,
                               size_t *first, size_t *last) {
    size_t lo = 0, hi = 0;
    if (v && value && cmp) {
        lo = (size_t)neverc_vector_lower_bound(v, value, cmp);
        hi = (size_t)neverc_vector_upper_bound(v, value, cmp);
    }
    if (first) *first = lo;
    if (last)  *last  = hi;
    return hi > lo;
}

size_t neverc_vector_partition_point(const neverc_vector_t *v,
                                     bool (*pred)(const void *elem)) {
    if (!v || !pred)
        return 0;
    size_t lo = 0, n = v->size;
    while (n > 0) {                       /* binary search for the false region */
        size_t half = n / 2;
        size_t mid = lo + half;
        if (pred(vec_elem_ptr(v, mid))) {
            lo = mid + 1;
            n -= half + 1;
        } else {
            n = half;
        }
    }
    return lo;
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
    if (!buf) {
        nci_heapsort_noalloc(v->data, v->size, sz, (nci_cmp_fn)cmp);
        return;
    }

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

/* Reverse the half-open range [lo, hi) without allocating an element-sized
 * scratch buffer. */
static void vec_reverse_buf(neverc_vector_t *v, size_t lo, size_t hi) {
    if (hi <= lo)
        return;
    size_t i = lo, j = hi - 1;
    while (i < j) {
        vec_swap_chunked(v, i, j);
        i++;
        j--;
    }
}

/* Rotate [lo, hi) left so the block starting at `mid` comes first
 * (reversal algorithm: O(n), fixed-size stack scratch only). */
static void vec_rotate_buf(neverc_vector_t *v, size_t lo, size_t mid,
                           size_t hi) {
    if (mid <= lo || mid >= hi)
        return;
    vec_reverse_buf(v, lo, mid);
    vec_reverse_buf(v, mid, hi);
    vec_reverse_buf(v, lo, hi);
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
 * O(n log n), stable, and allocation-free (fixed-size stack scratch). Used as
 * the fallback when the O(n) buffer cannot be allocated. Stability mirrors
 * libstdc++ __merge_without_buffer: split the longer run at its midpoint and
 * locate the cut in the other run with lower_bound when the pivot comes from
 * the left run (equal right elements stay after it) and upper_bound when it
 * comes from the right run (equal left elements stay before it).
 */
static void vec_merge_rotate(neverc_vector_t *v, size_t lo, size_t mid,
                             size_t hi, nci_cmp_fn cmp) {
    size_t len1 = mid - lo, len2 = hi - mid;
    if (len1 == 0 || len2 == 0)
        return;
    if (len1 + len2 == 2) {
        if (cmp(vec_elem_ptr(v, mid), vec_elem_ptr(v, lo)) < 0)
            vec_swap_chunked(v, lo, mid);
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
    vec_rotate_buf(v, mid1, mid, mid2);
    vec_merge_rotate(v, lo, mid1, newmid, cmp);
    vec_merge_rotate(v, newmid, mid2, hi, cmp);
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
    vec_merge_rotate(v, 0, mid, n, (nci_cmp_fn)cmp);
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
    uint64_t s = seed;
    for (size_t i = v->size - 1; i > 0; i--) {
        size_t j = (size_t)vec_rand_bounded(&s, (uint64_t)i + 1);
        if (j != i)
            vec_swap_chunked(v, i, j);
    }
}

/* Allocation-free stable partition, O(n log n): partition each half, then
 * rotate the second half's true-group in front of the first half's
 * false-group. Used when the O(n) buffer cannot be allocated. */
static size_t vec_stable_part_rotate(neverc_vector_t *v, size_t lo, size_t hi,
                                     bool (*pred)(const void *)) {
    size_t n = hi - lo;
    if (n == 0)
        return lo;
    if (n == 1)
        return pred(vec_elem_ptr(v, lo)) ? hi : lo;
    size_t mid = lo + n / 2;
    size_t left = vec_stable_part_rotate(v, lo, mid, pred);
    size_t right = vec_stable_part_rotate(v, mid, hi, pred);
    vec_rotate_buf(v, left, mid, right);
    return left + (right - mid);
}

size_t neverc_vector_stable_partition(neverc_vector_t *v,
                                      bool (*pred)(const void *elem)) {
    if (!v || !pred || v->size == 0)
        return 0;
    size_t es = v->elem_size, n = v->size;

    /* Cache predicate results and stage the complete output in one allocation.
     * Besides evaluating stateful predicates once, this avoids sizing one
     * group from a first pass and then trusting potentially different results
     * during a second pass. */
    if (es == SIZE_MAX || n > SIZE_MAX / (es + 1U))
        return vec_stable_part_rotate(v, 0, n, pred);
    unsigned char *scratch =
        (unsigned char *)malloc(n * (es + 1U));
    if (!scratch)
        return vec_stable_part_rotate(v, 0, n, pred);
    unsigned char *selected = scratch;
    char *staged = (char *)scratch + n;

    size_t ntrue = 0;
    for (size_t i = 0; i < n; i++) {
        selected[i] = pred(vec_elem_ptr(v, i)) ? 1U : 0U;
        if (selected[i])
            ntrue++;
    }
    if (ntrue == 0 || ntrue == n) {
        free(scratch);
        return ntrue;
    }

    size_t true_pos = 0;
    size_t false_pos = ntrue;
    for (size_t i = 0; i < n; i++) {
        size_t output = selected[i] ? true_pos++ : false_pos++;
        memcpy(staged + output * es, vec_elem_ptr(v, i), es);
    }
    memcpy(v->data, staged, n * es);
    free(scratch);
    return ntrue;
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

void neverc_vector_minmax_element(const neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp,
                                  void **min_out, void **max_out) {
    if (min_out) *min_out = NULL;
    if (max_out) *max_out = NULL;
    if (!v || !cmp || v->size == 0)
        return;
    size_t n = v->size, mn, mx, i;
    /* Seed from the first element (odd n) or first pair (even n). Pairing keeps
     * the total at the comparison-optimal ~3n/2 (1 intra-pair + 2 vs running
     * extrema per pair) instead of the 2n a separate min and max would cost. */
    if (n & 1) {
        mn = mx = 0;
        i = 1;
    } else {
        if (cmp(vec_elem_ptr(v, 1), vec_elem_ptr(v, 0)) < 0) { mn = 1; mx = 0; }
        else                                                 { mn = 0; mx = 1; }
        i = 2;
    }
    for (; i + 1 < n; i += 2) {
        size_t a = i, b = i + 1;
        if (cmp(vec_elem_ptr(v, b), vec_elem_ptr(v, a)) < 0) {   /* b < a */
            if (cmp(vec_elem_ptr(v, b), vec_elem_ptr(v, mn)) < 0) mn = b;
            if (!(cmp(vec_elem_ptr(v, a), vec_elem_ptr(v, mx)) < 0)) mx = a;
        } else {                                                 /* a <= b */
            if (cmp(vec_elem_ptr(v, a), vec_elem_ptr(v, mn)) < 0) mn = a;
            if (!(cmp(vec_elem_ptr(v, b), vec_elem_ptr(v, mx)) < 0)) mx = b;
        }
    }
    if (i < n) {                                                 /* odd leftover */
        if (cmp(vec_elem_ptr(v, i), vec_elem_ptr(v, mn)) < 0) mn = i;
        else if (!(cmp(vec_elem_ptr(v, i), vec_elem_ptr(v, mx)) < 0)) mx = i;
    }
    if (min_out) *min_out = vec_elem_ptr(v, mn);
    if (max_out) *max_out = vec_elem_ptr(v, mx);
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
    if (count > v->size - start)
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
        if (pred(vec_elem_ptr(v, i)) &&
            !neverc_vector_push_back(result, vec_elem_ptr(v, i))) {
            neverc_vector_free(result);
            return NULL;
        }
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
    if (a->elem_size == 0 || a->elem_size != b->elem_size || !cmp ||
        a->size > a->capacity || b->size > b->capacity ||
        (a->size != 0 && !a->data) || (b->size != 0 && !b->data))
        return NULL;
    if (a->size > SIZE_MAX - b->size)
        return NULL;
    size_t total = a->size + b->size;
    size_t sz = a->elem_size;
    if (total == 0) return neverc_vector_new(sz);
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

/* ===== Sorted-Set Operations ===== */

enum { VEC_SET_UNION, VEC_SET_INTERSECT, VEC_SET_DIFF, VEC_SET_SYMDIFF };

/* One linear merge pass drives all four set operations; `op` selects which of
 * the a-only / b-only / common elements are emitted. Multiset semantics fall
 * out naturally: the equal branch advances both cursors once, so a value kept
 * by `op` lands min(m,n) times and any surplus is drained by the tail copies. */
static neverc_vector_t *vec_set_op(const neverc_vector_t *a,
                                   const neverc_vector_t *b,
                                   neverc_vector_cmp_fn cmp, int op) {
    if (!a || !b || !cmp || a->elem_size == 0 ||
        a->elem_size != b->elem_size || a->size > a->capacity ||
        b->size > b->capacity || (a->size != 0 && !a->data) ||
        (b->size != 0 && !b->data))
        return NULL;
    size_t sz = a->elem_size;
    size_t cap;
    if (op == VEC_SET_INTERSECT)   cap = a->size < b->size ? a->size : b->size;
    else if (op == VEC_SET_DIFF)   cap = a->size;
    else {
        if (a->size > SIZE_MAX - b->size)
            return NULL;
        cap = a->size + b->size;
    }
    if (cap == 0) return neverc_vector_new(sz);
    neverc_vector_t *r = neverc_vector_new_with_capacity(sz, cap);
    if (!r)
        return NULL;
    char *out = (char *)r->data;
    size_t w = 0, i = 0, j = 0;
    while (i < a->size && j < b->size) {
        int c = cmp(vec_elem_ptr(a, i), vec_elem_ptr(b, j));
        if (c < 0) {                                   /* a[i] only */
            if (op != VEC_SET_INTERSECT) {
                memcpy(out + w * sz, vec_elem_ptr(a, i), sz);
                w++;
            }
            i++;
        } else if (c > 0) {                            /* b[j] only */
            if (op == VEC_SET_UNION || op == VEC_SET_SYMDIFF) {
                memcpy(out + w * sz, vec_elem_ptr(b, j), sz);
                w++;
            }
            j++;
        } else {                                       /* common value */
            if (op == VEC_SET_UNION || op == VEC_SET_INTERSECT) {
                memcpy(out + w * sz, vec_elem_ptr(a, i), sz);
                w++;
            }
            i++;
            j++;
        }
    }
    if (op != VEC_SET_INTERSECT && i < a->size) {      /* drain a's tail */
        size_t rem = a->size - i;
        memcpy(out + w * sz, vec_elem_ptr(a, i), rem * sz);
        w += rem;
    }
    if ((op == VEC_SET_UNION || op == VEC_SET_SYMDIFF) && j < b->size) {
        size_t rem = b->size - j;                      /* drain b's tail */
        memcpy(out + w * sz, vec_elem_ptr(b, j), rem * sz);
        w += rem;
    }
    r->size = w;
    return r;
}

neverc_vector_t *neverc_vector_set_union(const neverc_vector_t *a,
                                          const neverc_vector_t *b,
                                          neverc_vector_cmp_fn cmp) {
    return vec_set_op(a, b, cmp, VEC_SET_UNION);
}

neverc_vector_t *neverc_vector_set_intersection(const neverc_vector_t *a,
                                                 const neverc_vector_t *b,
                                                 neverc_vector_cmp_fn cmp) {
    return vec_set_op(a, b, cmp, VEC_SET_INTERSECT);
}

neverc_vector_t *neverc_vector_set_difference(const neverc_vector_t *a,
                                               const neverc_vector_t *b,
                                               neverc_vector_cmp_fn cmp) {
    return vec_set_op(a, b, cmp, VEC_SET_DIFF);
}

neverc_vector_t *neverc_vector_set_symmetric_difference(const neverc_vector_t *a,
                                                         const neverc_vector_t *b,
                                                         neverc_vector_cmp_fn cmp) {
    return vec_set_op(a, b, cmp, VEC_SET_SYMDIFF);
}

bool neverc_vector_includes(const neverc_vector_t *a, const neverc_vector_t *b,
                            neverc_vector_cmp_fn cmp) {
    if (!a || !b || !cmp || a->elem_size != b->elem_size)
        return false;
    size_t i = 0, j = 0;
    while (j < b->size) {
        if (i >= a->size)
            return false;                  /* a exhausted, b still has elements */
        int c = cmp(vec_elem_ptr(a, i), vec_elem_ptr(b, j));
        if (c < 0) i++;                     /* a[i] < b[j]: skip a[i] */
        else if (c > 0) return false;       /* a[i] > b[j]: b[j] missing from a */
        else { i++; j++; }                  /* equal: consume one of each */
    }
    return true;
}

/* ===== Permutations ===== */

bool neverc_vector_next_permutation(neverc_vector_t *v,
                                    neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size < 2)
        return false;
    size_t n = v->size, i = n - 1;
    for (;;) {
        size_t j = i;
        i--;
        if (cmp(vec_elem_ptr(v, i), vec_elem_ptr(v, j)) < 0) {  /* v[i] < v[j] */
            size_t k = n - 1;
            while (!(cmp(vec_elem_ptr(v, i), vec_elem_ptr(v, k)) < 0))
                k--;                        /* rightmost k with v[i] < v[k] */
            neverc_vector_swap_elements(v, i, k);
            vec_reverse_range(v, j, n - 1); /* make the suffix ascending */
            return true;
        }
        if (i == 0) {                       /* whole range non-increasing: wrap */
            vec_reverse_range(v, 0, n - 1);
            return false;
        }
    }
}

bool neverc_vector_prev_permutation(neverc_vector_t *v,
                                    neverc_vector_cmp_fn cmp) {
    if (!v || !cmp || v->size < 2)
        return false;
    size_t n = v->size, i = n - 1;
    for (;;) {
        size_t j = i;
        i--;
        if (cmp(vec_elem_ptr(v, j), vec_elem_ptr(v, i)) < 0) {  /* v[j] < v[i] */
            size_t k = n - 1;
            while (!(cmp(vec_elem_ptr(v, k), vec_elem_ptr(v, i)) < 0))
                k--;                        /* rightmost k with v[k] < v[i] */
            neverc_vector_swap_elements(v, i, k);
            vec_reverse_range(v, j, n - 1); /* make the suffix descending */
            return true;
        }
        if (i == 0) {                       /* whole range non-decreasing: wrap */
            vec_reverse_range(v, 0, n - 1);
            return false;
        }
    }
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
    if (a->elem_size < b->elem_size)
        return -1;
    if (a->elem_size > b->elem_size)
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
