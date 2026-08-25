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
#include <stdint.h>

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
/* std::equal_range: the half-open index range [*first,*last) of elements equal
 * to value in a sorted vector (i.e. [lower_bound, upper_bound)). Returns true
 * when the range is non-empty (value present). O(log n); either out-param may
 * be NULL. */
bool   neverc_vector_equal_range(const neverc_vector_t *v, const void *value,
                                  neverc_vector_cmp_fn cmp,
                                  size_t *first, size_t *last);
/* std::partition_point: index of the first element for which pred is false in a
 * vector already partitioned (every pred-true element precedes every pred-false
 * one). Equals the count of leading pred-true elements. O(log n). */
size_t neverc_vector_partition_point(const neverc_vector_t *v,
                                      bool (*pred)(const void *elem));

/* ===== Sorting Variants ===== */

void   neverc_vector_stable_sort(neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp);
void   neverc_vector_partial_sort(neverc_vector_t *v, size_t k,
                                   neverc_vector_cmp_fn cmp);
/* Introselect (std::nth_element): O(n) average rearrange so element k is the
 * k-th smallest, [0,k) <= v[k] <= (k,size). No-op if k is out of range. */
void   neverc_vector_nth_element(neverc_vector_t *v, size_t k,
                                  neverc_vector_cmp_fn cmp);
/* std::inplace_merge: merge the two consecutive sorted runs [0,mid) and
 * [mid,size) into one stable sorted run. O(n) using the Timsort gallop-merge
 * engine when a buffer is available, O(n log n) via rotations otherwise.
 * No-op if mid is 0 or >= size. */
void   neverc_vector_inplace_merge(neverc_vector_t *v, size_t mid,
                                    neverc_vector_cmp_fn cmp);

/* ===== Randomized & Partition Algorithms ===== */

/* Fisher-Yates shuffle driven by an unbiased splitmix64 generator (no 128-bit
 * math or platform intrinsics). `seed` makes the permutation reproducible. */
void   neverc_vector_shuffle(neverc_vector_t *v, uint64_t seed);

/* std::stable_partition: move every element satisfying pred to the front while
 * preserving the relative order within each group. Returns the partition point
 * (number of elements satisfying pred). O(n) with a buffer, O(n log n) in place
 * via rotations if allocation fails. */
size_t neverc_vector_stable_partition(neverc_vector_t *v,
                                       bool (*pred)(const void *elem));

/* std::sample: return a new vector holding k elements chosen uniformly at
 * random without replacement, preserving their original relative order (Knuth
 * Algorithm S). k is clamped to size; `seed` makes the choice reproducible.
 * Caller frees the result. */
neverc_vector_t *neverc_vector_sample(const neverc_vector_t *v, size_t k,
                                       uint64_t seed);

/* ===== Reduction / Transformation ===== */

void  *neverc_vector_min_element(const neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp);
void  *neverc_vector_max_element(const neverc_vector_t *v,
                                  neverc_vector_cmp_fn cmp);
/* std::minmax_element: locate a smallest and a largest element in a single
 * ~3n/2-comparison pass (vs ~2n for separate min + max). On ties *min_out is
 * the first smallest and *max_out is the last largest (the std::minmax_element
 * rule). Either out-param may be NULL; both are set to NULL for an empty vector. */
void   neverc_vector_minmax_element(const neverc_vector_t *v,
                                     neverc_vector_cmp_fn cmp,
                                     void **min_out, void **max_out);
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

/* ===== Sorted-Set Operations ===== */

/* Each mirrors the matching C++ <algorithm> set operation with multiset
 * (duplicate-preserving) semantics, runs in O(a->size + b->size), and returns a
 * new sorted vector the caller frees. Both inputs must share elem_size and be
 * sorted by cmp; each returns NULL on NULL/incompatible input or OOM.
 *
 * For a value occurring m times in a and n times in b, the result holds it:
 *   union                  -> max(m,n) times
 *   intersection           -> min(m,n) times
 *   difference (a \ b)     -> max(m-n,0) times
 *   symmetric_difference   -> |m-n| times
 */
neverc_vector_t *neverc_vector_set_union(const neverc_vector_t *a,
                                          const neverc_vector_t *b,
                                          neverc_vector_cmp_fn cmp);
neverc_vector_t *neverc_vector_set_intersection(const neverc_vector_t *a,
                                                 const neverc_vector_t *b,
                                                 neverc_vector_cmp_fn cmp);
neverc_vector_t *neverc_vector_set_difference(const neverc_vector_t *a,
                                               const neverc_vector_t *b,
                                               neverc_vector_cmp_fn cmp);
neverc_vector_t *neverc_vector_set_symmetric_difference(const neverc_vector_t *a,
                                                         const neverc_vector_t *b,
                                                         neverc_vector_cmp_fn cmp);
/* std::includes: true if sorted vector a contains every element of sorted vector
 * b honouring multiplicity (b is a sub-multiset of a). O(a->size + b->size). An
 * empty b is included in any a; returns false on NULL/incompatible input. */
bool   neverc_vector_includes(const neverc_vector_t *a, const neverc_vector_t *b,
                               neverc_vector_cmp_fn cmp);

/* ===== Permutations ===== */

/* std::next_permutation / std::prev_permutation: rearrange the vector in place
 * to the next (resp. previous) lexicographic permutation by cmp. Returns true
 * normally; returns false and resets to the last (resp. first) permutation when
 * the sequence was already the final (resp. first) one. O(n), no allocation. */
bool   neverc_vector_next_permutation(neverc_vector_t *v,
                                       neverc_vector_cmp_fn cmp);
bool   neverc_vector_prev_permutation(neverc_vector_t *v,
                                       neverc_vector_cmp_fn cmp);

/* ===== Iterators (pointer-based) =====
 *
 * rend is a NULL sentinel because C pointer arithmetic cannot form the
 * one-before-begin pointer used by a naive reverse loop. Stop at begin before
 * decrementing it, for example:
 *
 *   for (char *p = rbegin, *b = begin; p;
 *        p = p == b ? NULL : p - elem_size) { ... }
 */

void  *neverc_vector_begin(const neverc_vector_t *v);
void  *neverc_vector_end(const neverc_vector_t *v);
void  *neverc_vector_rbegin(const neverc_vector_t *v);
void  *neverc_vector_rend(const neverc_vector_t *v);

/* ===== Comparison ===== */

bool   neverc_vector_equal(const neverc_vector_t *a, const neverc_vector_t *b,
                            neverc_vector_cmp_fn cmp);
/* Lexicographic order. Vectors with different element widths are ordered by
 * elem_size before examining elements, so incompatible comparators are never
 * called and raw comparison cannot read past a narrower element. */
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
