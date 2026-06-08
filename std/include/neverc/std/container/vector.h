#ifndef NEVERC_CONTAINER_VECTOR_H
#define NEVERC_CONTAINER_VECTOR_H

/*
 * NeverC container/vector — dynamic array (C equivalent of std::vector)
 *
 * Elements are stored inline (contiguous memory, like C++ vector).
 * The vector tracks element size and manages memory automatically.
 * Growth factor: 2x (amortized O(1) push_back).
 *
 * Cross-platform: macOS, iOS, Linux, Android, Windows.
 */

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_vector {
    void   *data;
    size_t  size;
    size_t  capacity;
    size_t  elem_size;
} neverc_vector_t;

typedef int (*neverc_vector_cmp_fn)(const void *a, const void *b);

/* ===== Construction / Destruction ===== */

neverc_vector_t *neverc_vector_new(size_t elem_size);
neverc_vector_t *neverc_vector_new_with_capacity(size_t elem_size, size_t cap);
neverc_vector_t *neverc_vector_new_with_size(size_t elem_size, size_t count,
                                              const void *fill_value);
neverc_vector_t *neverc_vector_copy(const neverc_vector_t *src);
neverc_vector_t *neverc_vector_from_array(const void *arr, size_t count,
                                           size_t elem_size);
void neverc_vector_free(neverc_vector_t *v);

void neverc_vector_init(neverc_vector_t *v, size_t elem_size);
void neverc_vector_destroy(neverc_vector_t *v);

/* ===== Element Access ===== */

void       *neverc_vector_at(const neverc_vector_t *v, size_t index);
void       *neverc_vector_front(const neverc_vector_t *v);
void       *neverc_vector_back(const neverc_vector_t *v);
void       *neverc_vector_data(const neverc_vector_t *v);

bool        neverc_vector_get(const neverc_vector_t *v, size_t index,
                              void *out);
bool        neverc_vector_set(neverc_vector_t *v, size_t index,
                              const void *value);

/* ===== Capacity ===== */

bool   neverc_vector_empty(const neverc_vector_t *v);
size_t neverc_vector_size(const neverc_vector_t *v);
size_t neverc_vector_capacity(const neverc_vector_t *v);
size_t neverc_vector_elem_size(const neverc_vector_t *v);

size_t neverc_vector_max_size(const neverc_vector_t *v);

bool   neverc_vector_reserve(neverc_vector_t *v, size_t new_cap);
bool   neverc_vector_shrink_to_fit(neverc_vector_t *v);

/* ===== Modifiers ===== */

bool   neverc_vector_push_back(neverc_vector_t *v, const void *value);
bool   neverc_vector_pop_back(neverc_vector_t *v, void *out);
bool   neverc_vector_insert(neverc_vector_t *v, size_t index,
                             const void *value);
bool   neverc_vector_insert_range(neverc_vector_t *v, size_t index,
                                   const void *values, size_t count);
bool   neverc_vector_insert_fill(neverc_vector_t *v, size_t index,
                                  size_t count, const void *value);
bool   neverc_vector_erase(neverc_vector_t *v, size_t index);
bool   neverc_vector_erase_range(neverc_vector_t *v, size_t first,
                                  size_t count);
void   neverc_vector_clear(neverc_vector_t *v);
bool   neverc_vector_resize(neverc_vector_t *v, size_t new_size,
                             const void *fill_value);
void   neverc_vector_swap(neverc_vector_t *a, neverc_vector_t *b);

bool   neverc_vector_assign(neverc_vector_t *v, const void *values,
                             size_t count);
bool   neverc_vector_append(neverc_vector_t *v, const neverc_vector_t *other);

/* ===== Algorithms ===== */

void   neverc_vector_sort(neverc_vector_t *v, neverc_vector_cmp_fn cmp);
void   neverc_vector_reverse(neverc_vector_t *v);
void   neverc_vector_unique(neverc_vector_t *v, neverc_vector_cmp_fn cmp);

long   neverc_vector_find(const neverc_vector_t *v, const void *value,
                           neverc_vector_cmp_fn cmp);
long   neverc_vector_find_if(const neverc_vector_t *v,
                              bool (*pred)(const void *elem));
bool   neverc_vector_contains(const neverc_vector_t *v, const void *value,
                               neverc_vector_cmp_fn cmp);
size_t neverc_vector_count(const neverc_vector_t *v, const void *value,
                            neverc_vector_cmp_fn cmp);
size_t neverc_vector_count_if(const neverc_vector_t *v,
                               bool (*pred)(const void *elem));

void   neverc_vector_foreach(const neverc_vector_t *v,
                              void (*fn)(void *elem, void *ctx), void *ctx);
bool   neverc_vector_any(const neverc_vector_t *v,
                          bool (*pred)(const void *elem));
bool   neverc_vector_all(const neverc_vector_t *v,
                          bool (*pred)(const void *elem));
bool   neverc_vector_none(const neverc_vector_t *v,
                           bool (*pred)(const void *elem));

/* ===== Emplace (C equivalent: append zero-initialized, return pointer) ===== */

void  *neverc_vector_emplace_back(neverc_vector_t *v);
void  *neverc_vector_emplace(neverc_vector_t *v, size_t index);

/* ===== Advanced Modifiers ===== */

size_t neverc_vector_erase_if(neverc_vector_t *v,
                               bool (*pred)(const void *elem));
void   neverc_vector_fill(neverc_vector_t *v, const void *value);
void   neverc_vector_swap_elements(neverc_vector_t *v, size_t i, size_t j);
void   neverc_vector_rotate(neverc_vector_t *v, size_t mid);
size_t neverc_vector_partition(neverc_vector_t *v,
                                bool (*pred)(const void *elem));
void   neverc_vector_generate(neverc_vector_t *v,
                               void (*gen)(void *elem, size_t index,
                                           void *ctx),
                               void *ctx);

/* ===== Sorted-Vector Operations (require pre-sorted vector) ===== */

long   neverc_vector_binary_search(const neverc_vector_t *v,
                                    const void *value,
                                    neverc_vector_cmp_fn cmp);
long   neverc_vector_lower_bound(const neverc_vector_t *v,
                                  const void *value,
                                  neverc_vector_cmp_fn cmp);
long   neverc_vector_upper_bound(const neverc_vector_t *v,
                                  const void *value,
                                  neverc_vector_cmp_fn cmp);
bool   neverc_vector_is_sorted(const neverc_vector_t *v,
                                neverc_vector_cmp_fn cmp);

/* ===== Sorting Variants ===== */

void   neverc_vector_stable_sort(neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp);
void   neverc_vector_partial_sort(neverc_vector_t *v, size_t k,
                                   neverc_vector_cmp_fn cmp);

/* ===== Reduction / Transformation ===== */

void  *neverc_vector_min_element(const neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp);
void  *neverc_vector_max_element(const neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp);
void   neverc_vector_transform(neverc_vector_t *v,
                                void (*fn)(void *elem, void *ctx),
                                void *ctx);
bool   neverc_vector_reduce(const neverc_vector_t *v,
                             void *accumulator,
                             void (*fn)(void *acc, const void *elem));

/* ===== Slice / Subvector ===== */

neverc_vector_t *neverc_vector_slice(const neverc_vector_t *v,
                                      size_t start, size_t count);
neverc_vector_t *neverc_vector_filter(const neverc_vector_t *v,
                                       bool (*pred)(const void *elem));
neverc_vector_t *neverc_vector_map(const neverc_vector_t *v,
                                    size_t out_elem_size,
                                    void (*fn)(void *out, const void *in));
neverc_vector_t *neverc_vector_merge(const neverc_vector_t *a,
                                      const neverc_vector_t *b,
                                      neverc_vector_cmp_fn cmp);

/* ===== Iterators (pointer-based) ===== */

void  *neverc_vector_begin(const neverc_vector_t *v);
void  *neverc_vector_end(const neverc_vector_t *v);
void  *neverc_vector_rbegin(const neverc_vector_t *v);
void  *neverc_vector_rend(const neverc_vector_t *v);

/* ===== Comparison ===== */

bool   neverc_vector_equal(const neverc_vector_t *a, const neverc_vector_t *b,
                            neverc_vector_cmp_fn cmp);
int    neverc_vector_compare(const neverc_vector_t *a, const neverc_vector_t *b,
                              neverc_vector_cmp_fn cmp);

/* ===== Type-Safe Macros ===== */

#define NEVERC_VECTOR_OF(type) neverc_vector_new(sizeof(type))

#define NEVERC_VECTOR_PUSH(v, val) \
    do { __typeof__(val) _tmp = (val); neverc_vector_push_back((v), &_tmp); } while(0)

#define NEVERC_VECTOR_GET_AS(v, index, type) \
    (*(type *)neverc_vector_at((v), (index)))

#define NEVERC_VECTOR_SET_VAL(v, index, val) \
    do { __typeof__(val) _tmp = (val); neverc_vector_set((v), (index), &_tmp); } while(0)

#define NEVERC_VECTOR_POP_AS(v, type, out_var) \
    neverc_vector_pop_back((v), &(out_var))

#define NEVERC_VECTOR_FOR_EACH(v, type, var) \
    for (type *var = (type *)neverc_vector_begin(v); \
         (void *)(var) != neverc_vector_end(v); \
         ++(var))

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/container.h>
#endif

#endif
