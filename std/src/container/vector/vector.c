#include <neverc/std/container/vector.h>
#include "../../sort/sort_impl.h"
#include <stdlib.h>
#include <string.h>

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
    for (size_t i = 0; i < count; i++)
        memcpy(vec_elem_ptr(v, index + i), value, v->elem_size);
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
    size_t write = 1;
    for (size_t read = 1; read < v->size; read++) {
        if (cmp(vec_elem_ptr(v, read), vec_elem_ptr(v, write - 1)) != 0) {
            if (write != read)
                memcpy(vec_elem_ptr(v, write), vec_elem_ptr(v, read),
                       v->elem_size);
            write++;
        }
    }
    v->size = write;
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
    size_t write = 0;
    for (size_t read = 0; read < v->size; read++) {
        if (!pred(vec_elem_ptr(v, read))) {
            if (write != read)
                memcpy(vec_elem_ptr(v, write), vec_elem_ptr(v, read),
                       v->elem_size);
            write++;
        }
    }
    size_t removed = v->size - write;
    v->size = write;
    return removed;
}

void neverc_vector_fill(neverc_vector_t *v, const void *value) {
    if (!v || !value)
        return;
    for (size_t i = 0; i < v->size; i++)
        memcpy(vec_elem_ptr(v, i), value, v->elem_size);
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
    size_t lo = 0, hi = v->size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(vec_elem_ptr(v, mid), value);
        if (r == 0)
            return (long)mid;
        if (r < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

long neverc_vector_lower_bound(const neverc_vector_t *v,
                                const void *value,
                                neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp)
        return 0;
    size_t lo = 0, hi = v->size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp(vec_elem_ptr(v, mid), value) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (long)lo;
}

long neverc_vector_upper_bound(const neverc_vector_t *v,
                                const void *value,
                                neverc_vector_cmp_fn cmp) {
    if (!v || !value || !cmp)
        return 0;
    size_t lo = 0, hi = v->size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp(vec_elem_ptr(v, mid), value) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
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
    neverc_vector_t *result = neverc_vector_new_with_capacity(a->elem_size,
                                                               a->size + b->size);
    if (!result)
        return NULL;
    size_t i = 0, j = 0;
    while (i < a->size && j < b->size) {
        if (cmp(vec_elem_ptr(a, i), vec_elem_ptr(b, j)) <= 0) {
            neverc_vector_push_back(result, vec_elem_ptr(a, i));
            i++;
        } else {
            neverc_vector_push_back(result, vec_elem_ptr(b, j));
            j++;
        }
    }
    while (i < a->size) {
        neverc_vector_push_back(result, vec_elem_ptr(a, i));
        i++;
    }
    while (j < b->size) {
        neverc_vector_push_back(result, vec_elem_ptr(b, j));
        j++;
    }
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
