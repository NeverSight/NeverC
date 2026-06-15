/*
 * sort_impl.h — Unified internal sort engine for NeverC standard library.
 *
 * Unstable sort:  PDQSort (Pattern-Defeating Quicksort)
 *   - Median-of-three / Tukey's ninther pivot selection
 *   - Pre-sortedness detection (partial insertion sort probe)
 *   - Reverse-sorted data detection and reversal
 *   - Adversarial pattern breaking
 *   - Heapsort fallback on degenerate partitions
 *   - O(n log n) worst case, adaptive for nearly-sorted data
 *
 * Stable sort:  Timsort runs + PowerSort merge policy
 *   - Natural run detection (ascending & descending)
 *   - Binary insertion sort to extend short runs
 *   - Galloping (binary-search) merge that trims ordered prefixes/suffixes
 *   - PowerSort node-power merge order (Munro & Wild 2018; CPython 3.11+):
 *     a provably near-optimal merge tree that also replaces the fragile
 *     Timsort length invariant with a strictly-increasing power stack
 *   - O(n log n) worst case, O(n) for pre-sorted data
 *
 * All functions are static — include this header from each .c file that
 * needs sorting.  The compiler eliminates unused functions.
 */

#ifndef NEVERC_SORT_IMPL_H
#define NEVERC_SORT_IMPL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int (*nci_cmp_fn)(const void *, const void *);

#define NCI_ELEM(base, i, es) ((base) + (i) * (es))

/* ═══════════════════════════════════════════════════════════════════════════
 *  Utilities
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void nci_swap(char *a, char *b, size_t es, char *tmp) {
    if (a == b) return;
    memcpy(tmp, a, es);
    memcpy(a, b, es);
    memcpy(b, tmp, es);
}

static void nci_reverse(char *base, size_t n, size_t es, char *tmp) {
    if (n <= 1) return;
    for (size_t i = 0, j = n - 1; i < j; i++, j--)
        nci_swap(NCI_ELEM(base, i, es), NCI_ELEM(base, j, es), es, tmp);
}

static int nci_log2(size_t n) {
    int k = 0;
    while (n > 1) { n >>= 1; k++; }
    return k;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Insertion Sort  (shift-based, cache-friendly)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void nci_insertion_sort(char *base, size_t n, size_t es,
                               nci_cmp_fn cmp, char *tmp) {
    for (size_t i = 1; i < n; i++) {
        memcpy(tmp, NCI_ELEM(base, i, es), es);
        size_t j = i;
        while (j > 0 && cmp(NCI_ELEM(base, j - 1, es), tmp) > 0) {
            memcpy(NCI_ELEM(base, j, es), NCI_ELEM(base, j - 1, es), es);
            j--;
        }
        memcpy(NCI_ELEM(base, j, es), tmp, es);
    }
}

/*
 * Probe whether data is nearly sorted: attempt insertion sort but abort after
 * `limit` element moves.  Returns 1 if sort completed (data was nearly
 * sorted), 0 if aborted (too many moves — data left partially modified).
 */
static int nci_partial_insertion_sort(char *base, size_t n, size_t es,
                                      nci_cmp_fn cmp, char *tmp, int limit) {
    if (n <= 1) return 1;
    int moves = 0;
    for (size_t i = 1; i < n; i++) {
        memcpy(tmp, NCI_ELEM(base, i, es), es);
        size_t j = i;
        while (j > 0 && cmp(NCI_ELEM(base, j - 1, es), tmp) > 0) {
            memcpy(NCI_ELEM(base, j, es), NCI_ELEM(base, j - 1, es), es);
            j--;
            if (++moves > limit) {
                memcpy(NCI_ELEM(base, j, es), tmp, es);
                return 0;
            }
        }
        memcpy(NCI_ELEM(base, j, es), tmp, es);
    }
    return 1;
}

/*
 * Binary insertion sort for range [lo, hi) given [lo, start) is already
 * sorted.  Uses memmove for bulk shifting — faster than element-by-element
 * for larger elements.
 */
static void nci_binary_insertion_sort(char *base, size_t lo, size_t hi,
                                      size_t start, size_t es,
                                      nci_cmp_fn cmp, char *tmp) {
    if (start <= lo) start = lo + 1;
    for (size_t i = start; i < hi; i++) {
        memcpy(tmp, NCI_ELEM(base, i, es), es);
        size_t left = lo, right = i;
        while (left < right) {
            size_t mid = left + (right - left) / 2;
            if (cmp(tmp, NCI_ELEM(base, mid, es)) < 0)
                right = mid;
            else
                left = mid + 1;
        }
        if (left < i)
            memmove(NCI_ELEM(base, left + 1, es), NCI_ELEM(base, left, es),
                    (i - left) * es);
        memcpy(NCI_ELEM(base, left, es), tmp, es);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Heap Sort
 * ═══════════════════════════════════════════════════════════════════════════ */

static void nci_sift_down(char *base, size_t n, size_t es,
                           nci_cmp_fn cmp, char *tmp, size_t node) {
    while (1) {
        size_t child = 2 * node + 1;
        if (child >= n) break;
        if (child + 1 < n &&
            cmp(NCI_ELEM(base, child, es), NCI_ELEM(base, child + 1, es)) < 0)
            child++;
        if (cmp(NCI_ELEM(base, node, es), NCI_ELEM(base, child, es)) >= 0)
            break;
        nci_swap(NCI_ELEM(base, node, es), NCI_ELEM(base, child, es), es, tmp);
        node = child;
    }
}

static void nci_heapsort(char *base, size_t n, size_t es,
                          nci_cmp_fn cmp, char *tmp) {
    if (n <= 1) return;
    for (size_t i = n / 2; i > 0; i--)
        nci_sift_down(base, n, es, cmp, tmp, i - 1);
    for (size_t i = n - 1; i > 0; i--) {
        nci_swap(base, NCI_ELEM(base, i, es), es, tmp);
        nci_sift_down(base, i, es, cmp, tmp, 0);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  PDQSort — Pattern-Defeating Quicksort
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NCI_PDQ_THRESHOLD     24   /* switch to insertion sort */
#define NCI_PDQ_NINTHER       128  /* use Tukey's ninther pivot */
#define NCI_PDQ_PARTIAL_LIMIT 8    /* partial insertion sort move budget */

/* --- Sort 3 elements in-place, count swaps as a sortedness signal --- */

static void nci_sort3(char *base, size_t a, size_t b, size_t c,
                      size_t es, nci_cmp_fn cmp, char *tmp, int *swaps) {
    if (cmp(NCI_ELEM(base, b, es), NCI_ELEM(base, a, es)) < 0) {
        nci_swap(NCI_ELEM(base, a, es), NCI_ELEM(base, b, es), es, tmp);
        (*swaps)++;
    }
    if (cmp(NCI_ELEM(base, c, es), NCI_ELEM(base, b, es)) < 0) {
        nci_swap(NCI_ELEM(base, b, es), NCI_ELEM(base, c, es), es, tmp);
        (*swaps)++;
        if (cmp(NCI_ELEM(base, b, es), NCI_ELEM(base, a, es)) < 0) {
            nci_swap(NCI_ELEM(base, a, es), NCI_ELEM(base, b, es), es, tmp);
            (*swaps)++;
        }
    }
}

#define NCI_HINT_INCREASING 0
#define NCI_HINT_DECREASING 1
#define NCI_HINT_UNKNOWN    2

/*
 * Choose pivot via median-of-three (or ninther for large arrays).
 * Returns pivot index and a *hint* about pre-sortedness:
 *   INCREASING → data looks already sorted
 *   DECREASING → data looks reverse-sorted
 *   UNKNOWN    → no clear pattern
 */
static size_t nci_choose_pivot(char *base, size_t n, size_t es,
                               nci_cmp_fn cmp, char *tmp, int *hint) {
    size_t q1 = n / 4, q2 = n / 2, q3 = q1 * 3;
    int swaps = 0;

    if (n >= NCI_PDQ_NINTHER) {
        nci_sort3(base, q1 - 1, q1, q1 + 1, es, cmp, tmp, &swaps);
        nci_sort3(base, q2 - 1, q2, q2 + 1, es, cmp, tmp, &swaps);
        nci_sort3(base, q3 - 1, q3, q3 + 1, es, cmp, tmp, &swaps);
        nci_sort3(base, q1, q2, q3, es, cmp, tmp, &swaps);
    } else {
        nci_sort3(base, q1, q2, q3, es, cmp, tmp, &swaps);
    }

    if (swaps == 0)
        *hint = NCI_HINT_INCREASING;
    else if ((n >= NCI_PDQ_NINTHER && swaps >= 9) ||
             (n <  NCI_PDQ_NINTHER && swaps >= 3))
        *hint = NCI_HINT_DECREASING;
    else
        *hint = NCI_HINT_UNKNOWN;

    return q2;
}

/*
 * Block-partitioned Hoare partition.
 *
 * Scans elements in cache-line-sized blocks (NCI_BLK) from each end,
 * collecting the indices of misplaced elements into small offset buffers,
 * then swaps them in bulk.  This keeps both scan directions sequential
 * within a block, avoiding the L2 thrashing that naive Hoare suffers
 * on large arrays.
 *
 * Pivot is moved to base[0]; on return elements in [0, pivot_pos) are
 * < pivot and [pivot_pos+1, n) are >= pivot.
 */

#define NCI_BLK 64

static size_t nci_partition(char *base, size_t n, size_t es,
                            nci_cmp_fn cmp, char *tmp,
                            size_t pivot_idx, int *already_partitioned) {
    nci_swap(base, NCI_ELEM(base, pivot_idx, es), es, tmp);

    size_t first = 1, last = n - 1;

    while (first <= last && cmp(NCI_ELEM(base, first, es), base) < 0) first++;
    while (first <= last && cmp(base, NCI_ELEM(base, last, es)) < 0)  last--;

    *already_partitioned = (first > last);

    /* ── Block partition phase ────────────────────────────────────────── */
    unsigned char offs_l[NCI_BLK], offs_r[NCI_BLK];
    size_t num_l = 0, num_r = 0;
    size_t scan_l = 0, scan_r = 0;
    size_t orig_l = first, orig_r = last;

    while (first + NCI_BLK - 1 < last) {
        if (num_l == 0) {
            orig_l = first;
            scan_l = 0;
            for (size_t k = 0; k < NCI_BLK; k++) {
                offs_l[num_l] = (unsigned char)k;
                num_l += (size_t)(cmp(NCI_ELEM(base, first + k, es), base) >= 0);
            }
            first += NCI_BLK;
        }
        if (num_r == 0) {
            if (first + NCI_BLK - 1 > last && num_l == 0) break;
            orig_r = last;
            scan_r = 0;
            size_t blk = last - first + 1;
            if (blk > NCI_BLK) blk = NCI_BLK;
            if (blk == 0) break;
            for (size_t k = 0; k < blk; k++) {
                offs_r[num_r] = (unsigned char)k;
                num_r += (size_t)(cmp(base, NCI_ELEM(base, last - k, es)) >= 0);
            }
            last -= blk;
        }

        size_t sw = num_l < num_r ? num_l : num_r;
        for (size_t k = 0; k < sw; k++) {
            size_t li = orig_l + offs_l[scan_l + k];
            size_t ri = orig_r - offs_r[scan_r + k];
            memcpy(tmp, NCI_ELEM(base, li, es), es);
            memcpy(NCI_ELEM(base, li, es), NCI_ELEM(base, ri, es), es);
            memcpy(NCI_ELEM(base, ri, es), tmp, es);
        }
        num_l -= sw; scan_l += sw;
        num_r -= sw; scan_r += sw;
    }

    /* ── Cleanup: re-scan the partially consumed tails with naive Hoare ─ */
    if (num_l > 0) first = orig_l;
    if (num_r > 0) last  = orig_r;

    while (first <= last) {
        while (first <= last && cmp(NCI_ELEM(base, first, es), base) < 0) first++;
        while (first <= last && cmp(base, NCI_ELEM(base, last, es)) < 0)  last--;
        if (first > last) break;
        memcpy(tmp, NCI_ELEM(base, first, es), es);
        memcpy(NCI_ELEM(base, first, es), NCI_ELEM(base, last, es), es);
        memcpy(NCI_ELEM(base, last, es), tmp, es);
        first++; last--;
    }

    if (last > 0)
        nci_swap(base, NCI_ELEM(base, last, es), es, tmp);
    return last;
}

/*
 * Deterministic shuffle around the middle to break adversarial patterns.
 * Called only after an unbalanced partition.
 */
static void nci_break_patterns(char *base, size_t n, size_t es, char *tmp) {
    if (n < 8) return;
    size_t modulus = 1;
    while (modulus < n) modulus <<= 1;
    uint64_t r = (uint64_t)n;
    size_t mid = n / 2;
    for (size_t k = 0; k < 3; k++) {
        size_t idx = mid - 1 + k;
        r = r * UINT64_C(0xd1342543de82ef95) + 1;
        size_t other = (size_t)(r & (uint64_t)(modulus - 1));
        if (other >= n) other -= n;
        nci_swap(NCI_ELEM(base, idx, es), NCI_ELEM(base, other, es), es, tmp);
    }
}

/* --- Main PDQSort loop --- */

static void nci_pdqsort_loop(char *base, size_t n, size_t es,
                              nci_cmp_fn cmp, char *tmp, int limit) {
    int was_balanced = 1;
    int was_partitioned = 1;

    while (1) {
        if (n <= NCI_PDQ_THRESHOLD) {
            nci_insertion_sort(base, n, es, cmp, tmp);
            return;
        }
        if (limit == 0) {
            nci_heapsort(base, n, es, cmp, tmp);
            return;
        }

        if (!was_balanced) {
            nci_break_patterns(base, n, es, tmp);
            limit--;
        }

        int hint;
        size_t pivot_idx = nci_choose_pivot(base, n, es, cmp, tmp, &hint);

        if (hint == NCI_HINT_DECREASING) {
            nci_reverse(base, n, es, tmp);
            pivot_idx = n - 1 - pivot_idx;
            hint = NCI_HINT_INCREASING;
        }

        if (was_partitioned && hint == NCI_HINT_INCREASING) {
            if (nci_partial_insertion_sort(base, n, es, cmp, tmp,
                                           NCI_PDQ_PARTIAL_LIMIT))
                return;
        }

        int already_partitioned;
        size_t p = nci_partition(base, n, es, cmp, tmp,
                                 pivot_idx, &already_partitioned);

        was_partitioned = already_partitioned;

        size_t l_size = p;
        size_t r_size = (p + 1 < n) ? n - p - 1 : 0;
        was_balanced = (l_size >= n / 8) && (r_size >= n / 8);

        /* Recurse on the smaller partition, iterate on the larger.
         * This bounds recursion depth to O(log n). */
        if (l_size < r_size) {
            nci_pdqsort_loop(base, l_size, es, cmp, tmp, limit);
            base = NCI_ELEM(base, p + 1, es);
            n = r_size;
        } else {
            nci_pdqsort_loop(NCI_ELEM(base, p + 1, es), r_size, es, cmp,
                              tmp, limit);
            n = l_size;
        }
    }
}

/* --- Public PDQSort entry --- */

static void nci_pdqsort(void *base, size_t n, size_t es, nci_cmp_fn cmp) {
    if (n <= 1 || es == 0) return;
    char stack_tmp[256];
    char *tmp = es <= sizeof(stack_tmp) ? stack_tmp : (char *)malloc(es);
    if (!tmp) return;
    nci_pdqsort_loop((char *)base, n, es, cmp, tmp, nci_log2(n) + 1);
    if (tmp != stack_tmp) free(tmp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Introselect — quickselect built on the PDQSort partition engine
 *
 *  Rearranges base[] so base[nth] holds the value that would occupy that slot
 *  in a fully sorted array, with every element in [0, nth) <= base[nth] and
 *  every element in (nth, n) >= base[nth] (the std::nth_element contract).
 *
 *  It reuses PDQSort's median-of-three / ninther pivot, cache-friendly block
 *  partition, and deterministic pattern breaking, but descends into only the
 *  side that contains `nth` — so the expected cost is O(n), not O(n log n).
 *  An introspective depth limit falls back to heapsort on a degenerate run of
 *  partitions, bounding the worst case at O(n log n); the same guard PDQSort
 *  uses, so adversarial input can never make selection go quadratic.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void nci_nth_element_loop(char *base, size_t n, size_t es,
                                 nci_cmp_fn cmp, char *tmp,
                                 size_t nth, int limit) {
    int was_balanced = 1;
    while (n > NCI_PDQ_THRESHOLD) {
        if (limit == 0) {
            nci_heapsort(base, n, es, cmp, tmp);   /* worst-case guard */
            return;
        }
        if (!was_balanced) {
            nci_break_patterns(base, n, es, tmp);
            limit--;
        }

        int hint;
        size_t pivot_idx = nci_choose_pivot(base, n, es, cmp, tmp, &hint);
        int already_partitioned;
        size_t p = nci_partition(base, n, es, cmp, tmp,
                                 pivot_idx, &already_partitioned);
        if (p == nth) return;          /* pivot landed exactly on the target */

        size_t l_size = p;
        size_t r_size = n - p - 1;
        was_balanced = (l_size >= n / 8) && (r_size >= n / 8);

        if (nth < p) {
            n = l_size;                /* target is in the left partition */
        } else {
            base = NCI_ELEM(base, p + 1, es);
            nth -= p + 1;              /* target is in the right partition */
            n = r_size;
        }
    }
    /* Small window: a full sort places base[nth] correctly, and the partition
     * invariant already orders everything outside the window around it. */
    nci_insertion_sort(base, n, es, cmp, tmp);
}

static void nci_nth_element(void *base, size_t n, size_t es,
                            nci_cmp_fn cmp, size_t nth) {
    if (n <= 1 || es == 0 || nth >= n) return;
    char stack_tmp[256];
    char *tmp = es <= sizeof(stack_tmp) ? stack_tmp : (char *)malloc(es);
    if (!tmp) return;
    nci_nth_element_loop((char *)base, n, es, cmp, tmp, nth, nci_log2(n) + 1);
    if (tmp != stack_tmp) free(tmp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Timsort — Adaptive Stable Merge Sort
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NCI_TIM_MIN_MERGE 32
#define NCI_TIM_MAX_STACK 85   /* sufficient for arrays up to 2^64 */

/* `power` is the PowerSort node power of the boundary on this run's right edge
 * (Munro & Wild 2018; the merge policy CPython adopted in 3.11). It is filled
 * in lazily when the next run is discovered. */
typedef struct { size_t start; size_t len; int power; } nci_tim_run;

/*
 * Compute the minimum run length so that n / min_run is close to a power
 * of 2.  This gives the best merge tree shape.
 */
static size_t nci_tim_min_run(size_t n) {
    size_t r = 0;
    while (n >= NCI_TIM_MIN_MERGE) {
        r |= (n & 1);
        n >>= 1;
    }
    return n + r;
}

/*
 * Find the length of the natural run starting at base[lo].
 * If the run is strictly descending, reverse it in-place.
 * Returns the run length (always >= 1).
 */
static size_t nci_tim_count_run(char *base, size_t lo, size_t hi,
                                size_t es, nci_cmp_fn cmp, char *tmp) {
    if (hi - lo <= 1) return hi - lo;
    size_t run_hi = lo + 1;
    if (cmp(NCI_ELEM(base, run_hi, es), NCI_ELEM(base, lo, es)) < 0) {
        while (run_hi < hi &&
               cmp(NCI_ELEM(base, run_hi, es),
                   NCI_ELEM(base, run_hi - 1, es)) < 0)
            run_hi++;
        /* reverse the descending run */
        for (size_t i = lo, j = run_hi - 1; i < j; i++, j--)
            nci_swap(NCI_ELEM(base, i, es), NCI_ELEM(base, j, es), es, tmp);
    } else {
        while (run_hi < hi &&
               cmp(NCI_ELEM(base, run_hi, es),
                   NCI_ELEM(base, run_hi - 1, es)) >= 0)
            run_hi++;
    }
    return run_hi - lo;
}

/*
 * Gallop search: binary search in a sorted range.
 *
 * gallop_right: find first p where arr[p] > key  (arr[p-1] <= key < arr[p]).
 * gallop_left:  find first p where arr[p] >= key (arr[p-1] < key <= arr[p]).
 *
 * The distinction matters for stability: gallop_right keeps equal elements
 * from the left run before the key, gallop_left keeps them after.
 */
static size_t nci_gallop_right(const char *key, const char *arr, size_t n,
                                size_t es, nci_cmp_fn cmp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp(key, arr + mid * es) < 0) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

static size_t nci_gallop_left(const char *key, const char *arr, size_t n,
                               size_t es, nci_cmp_fn cmp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp(arr + mid * es, key) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/*
 * Merge two adjacent runs:  base[lo1 .. lo1+len1)  and  base[lo2 .. lo2+len2)
 * where lo2 == lo1 + len1.  Uses `aux` as temporary storage.
 *
 * Before merging, gallop search trims already-ordered prefixes and suffixes:
 *   - left elements  <= right[0]    are already in place (skip prefix)
 *   - right elements >= left[last]  are already in place (skip suffix)
 * This makes the merge O(n) for sorted data and reduces work on nearly-sorted data.
 *
 * Stability: equal elements from the left run come first (cmp <= 0).
 */
static void nci_tim_merge(char *base, size_t lo1, size_t len1,
                           size_t len2, size_t es,
                           nci_cmp_fn cmp, char *aux) {
    size_t lo2 = lo1 + len1;

    /* Trim prefix: skip left elements already <= right[0] */
    size_t skip = nci_gallop_right(NCI_ELEM(base, lo2, es),
                                    NCI_ELEM(base, lo1, es), len1, es, cmp);
    lo1  += skip;
    len1 -= skip;
    if (len1 == 0) return;

    /* Trim suffix: keep only right elements < left[last] */
    len2 = nci_gallop_left(NCI_ELEM(base, lo1 + len1 - 1, es),
                            NCI_ELEM(base, lo2, es), len2, es, cmp);
    if (len2 == 0) return;

    lo2 = lo1 + len1;

    if (len1 <= len2) {
        /* merge-lo: copy left run to aux, merge forwards */
        memcpy(aux, NCI_ELEM(base, lo1, es), len1 * es);
        size_t ci = 0, cj = 0, dest = lo1;
        while (ci < len1 && cj < len2) {
            if (cmp(aux + ci * es, NCI_ELEM(base, lo2 + cj, es)) <= 0) {
                memcpy(NCI_ELEM(base, dest, es), aux + ci * es, es);
                ci++;
            } else {
                memcpy(NCI_ELEM(base, dest, es),
                       NCI_ELEM(base, lo2 + cj, es), es);
                cj++;
            }
            dest++;
        }
        if (ci < len1)
            memcpy(NCI_ELEM(base, dest, es), aux + ci * es, (len1 - ci) * es);
    } else {
        /* merge-hi: copy right run to aux, merge backwards */
        memcpy(aux, NCI_ELEM(base, lo2, es), len2 * es);
        size_t cursor1 = lo1 + len1 - 1;
        size_t cursor2 = len2 - 1;
        size_t dest = lo2 + len2 - 1;

        while (1) {
            if (cmp(NCI_ELEM(base, cursor1, es), aux + cursor2 * es) > 0) {
                memcpy(NCI_ELEM(base, dest, es),
                       NCI_ELEM(base, cursor1, es), es);
                dest--;
                if (cursor1 == lo1) {
                    memcpy(NCI_ELEM(base, lo1, es), aux,
                           (cursor2 + 1) * es);
                    break;
                }
                cursor1--;
            } else {
                memcpy(NCI_ELEM(base, dest, es), aux + cursor2 * es, es);
                dest--;
                if (cursor2 == 0) break;
                cursor2--;
            }
        }
    }
}

/*
 * PowerSort node power (Munro & Wild, "Nearly-Optimal Mergesorts", 2018;
 * the policy CPython adopted in 3.11). Returns the "power" of the boundary
 * between run 1 ([s1, s1+n1)) and the adjacent run 2 ([s1+n1, s1+n1+n2)) in an
 * array of total length n: the depth at which the scaled midpoints of the two
 * runs first differ in their binary expansion. Higher power = a boundary that
 * must be merged sooner. This drives a merge tree provably within a small
 * additive term of optimal, and the stack of strictly-increasing powers can
 * never exceed ~log2(n) entries — so it also removes the fragile Timsort
 * merge_collapse length invariant (the de Gouw et al. 2015 bug class).
 *
 * Branch-free of floats: a = 2*s1+n1 and b = a+n1+n2 are the run midpoints
 * scaled by 2n; comparing them against n bit by bit yields the power. All
 * subtractions are guarded (a -= n only when a >= n, and then b >= a >= n too),
 * so the unsigned arithmetic never underflows.
 */
static int nci_tim_power(size_t s1, size_t n1, size_t n2, size_t n) {
    int power = 0;
    size_t a = 2 * s1 + n1;
    size_t b = a + n1 + n2;
    for (;;) {
        power++;
        if (a >= n) { a -= n; b -= n; }
        else if (b >= n) break;
        a <<= 1;
        b <<= 1;
    }
    return power;
}

/* Merge the top two runs on the stack (PowerSort merge_at(top-1)). */
static void nci_tim_merge_top2(char *base, size_t es, nci_cmp_fn cmp,
                               nci_tim_run *stack, int *stack_size, char *aux) {
    int top = *stack_size - 1;
    nci_tim_merge(base, stack[top - 1].start, stack[top - 1].len,
                  stack[top].len, es, cmp, aux);
    stack[top - 1].len += stack[top].len;
    (*stack_size)--;
}

/*
 * On discovering a new run of length n2 (about to be pushed), resolve every
 * pending boundary whose power exceeds the new boundary's power, then record
 * the new boundary's power on the current top run. Mirrors CPython's
 * found_new_run: it only ever merges the top two runs, keeping the policy
 * simple and the stack powers strictly increasing toward the top.
 */
static void nci_tim_found_new_run(char *base, size_t es, nci_cmp_fn cmp,
                                  nci_tim_run *stack, int *stack_size,
                                  char *aux, size_t n2, size_t total) {
    if (*stack_size == 0) return;
    int top = *stack_size - 1;
    int power = nci_tim_power(stack[top].start, stack[top].len, n2, total);
    while (*stack_size > 1 && stack[*stack_size - 2].power > power)
        nci_tim_merge_top2(base, es, cmp, stack, stack_size, aux);
    stack[*stack_size - 1].power = power;
}

/* Force-merge all remaining runs on the stack. */
static void nci_tim_merge_force(char *base, size_t es, nci_cmp_fn cmp,
                                 nci_tim_run *stack, int *stack_size,
                                 char *aux) {
    while (*stack_size > 1) {
        int top = *stack_size - 1;
        if (*stack_size >= 3 && stack[top - 2].len < stack[top].len) {
            nci_tim_merge(base, stack[top - 2].start,
                          stack[top - 2].len, stack[top - 1].len,
                          es, cmp, aux);
            stack[top - 2].len += stack[top - 1].len;
            stack[top - 1] = stack[top];
        } else {
            nci_tim_merge(base, stack[top - 1].start,
                          stack[top - 1].len, stack[top].len,
                          es, cmp, aux);
            stack[top - 1].len += stack[top].len;
        }
        (*stack_size)--;
    }
}

/* --- Public Timsort entry --- */

static void nci_timsort(void *base_, size_t n, size_t es, nci_cmp_fn cmp) {
    if (n <= 1 || es == 0) return;

    char *base = (char *)base_;

    /* Small arrays: binary insertion sort, no allocation needed. */
    if (n <= NCI_TIM_MIN_MERGE) {
        char stack_tmp[256];
        char *tmp = es <= sizeof(stack_tmp) ? stack_tmp : (char *)malloc(es);
        if (!tmp) return;
        size_t run = nci_tim_count_run(base, 0, n, es, cmp, tmp);
        nci_binary_insertion_sort(base, 0, n, run, es, cmp, tmp);
        if (tmp != stack_tmp) free(tmp);
        return;
    }

    /* Allocate merge buffer.  merge-lo / merge-hi only ever copy the smaller of
     * the two adjacent runs, and adjacent runs sum to <= n, so a single merge
     * touches at most floor(n/2) elements — half of what a naive full-n buffer
     * reserves (the same bound Java/CPython Timsort use).  Halving the peak
     * scratch matters most on memory-constrained targets (Android / iOS) and
     * for large stable sorts.  The +1 keeps room for the 1-element swap buffer.
     * On failure, fall back to PDQSort (unstable). */
    char *aux = (char *)malloc((n / 2 + 1) * es);
    if (!aux) {
        nci_pdqsort(base_, n, es, cmp);
        return;
    }
    char *tmp = aux; /* first es bytes double as swap buffer */

    size_t min_run = nci_tim_min_run(n);

    nci_tim_run stack[NCI_TIM_MAX_STACK];
    int stack_size = 0;

    size_t lo = 0;
    while (lo < n) {
        size_t run_len = nci_tim_count_run(base, lo, n, es, cmp, tmp);

        if (run_len < min_run) {
            size_t force = n - lo;
            if (force > min_run) force = min_run;
            nci_binary_insertion_sort(base, lo, lo + force, lo + run_len,
                                      es, cmp, tmp);
            run_len = force;
        }

        /* PowerSort: resolve higher-power boundaries before pushing this run. */
        nci_tim_found_new_run(base, es, cmp, stack, &stack_size, aux,
                              run_len, n);

        stack[stack_size].start = lo;
        stack[stack_size].len = run_len;
        stack[stack_size].power = 0;
        stack_size++;

        lo += run_len;
    }

    nci_tim_merge_force(base, es, cmp, stack, &stack_size, aux);

    free(aux);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Type-Specialized Sort — inline comparisons, no function-pointer overhead
 *
 *  For int and double, the comparison is a trivial subtract/compare that
 *  the compiler can turn into a single CMP instruction.  Eliminating the
 *  indirect call to cmp() saves ~5 ns per comparison on typical hardware,
 *  which adds up to a 2-3× speedup for small–medium arrays.  The partition
 *  is the same branchless block partition the generic engine uses, so large
 *  random / few-unique inputs avoid the unpredictable partition branch too
 *  (a further ~2× on random, ~3× on few-unique versus a branchy Hoare scan).
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NCI_TYPED_ISORT_THRESHOLD 24

#define NCI_DEFINE_TYPED_SORT(NAME, TYPE, LESS)                              \
static void NAME##_isort_(TYPE *a, size_t n) {                               \
    for (size_t i = 1; i < n; i++) {                                         \
        TYPE t = a[i];                                                       \
        size_t j = i;                                                        \
        while (j > 0 && LESS(t, a[j-1])) { a[j] = a[j-1]; j--; }           \
        a[j] = t;                                                            \
    }                                                                        \
}                                                                            \
static void NAME##_sift_(TYPE *a, size_t n, size_t node) {                   \
    while (1) {                                                              \
        size_t c = 2*node+1;                                                 \
        if (c >= n) break;                                                   \
        if (c+1 < n && LESS(a[c], a[c+1])) c++;                             \
        if (!LESS(a[node], a[c])) break;                                     \
        TYPE t = a[node]; a[node] = a[c]; a[c] = t;                         \
        node = c;                                                            \
    }                                                                        \
}                                                                            \
static void NAME##_heap_(TYPE *a, size_t n) {                                \
    for (size_t i = n/2; i > 0; i--) NAME##_sift_(a, n, i-1);               \
    for (size_t i = n-1; i > 0; i--) {                                       \
        TYPE t = a[0]; a[0] = a[i]; a[i] = t;                               \
        NAME##_sift_(a, i, 0);                                               \
    }                                                                        \
}                                                                            \
static void NAME##_sort3_(TYPE *a, size_t x, size_t y, size_t z, int *sw) {  \
    if (LESS(a[y], a[x])) { TYPE t=a[x]; a[x]=a[y]; a[y]=t; (*sw)++; }     \
    if (LESS(a[z], a[y])) { TYPE t=a[y]; a[y]=a[z]; a[z]=t; (*sw)++;        \
        if (LESS(a[y], a[x])) { TYPE t2=a[x]; a[x]=a[y]; a[y]=t2; (*sw)++;}\
    }                                                                        \
}                                                                            \
static size_t NAME##_pivot_(TYPE *a, size_t n, int *hint) {                  \
    size_t q1=n/4, q2=n/2, q3=q1*3; int sw=0;                               \
    if (n >= NCI_PDQ_NINTHER) {                                              \
        NAME##_sort3_(a,q1-1,q1,q1+1,&sw);                                  \
        NAME##_sort3_(a,q2-1,q2,q2+1,&sw);                                  \
        NAME##_sort3_(a,q3-1,q3,q3+1,&sw);                                  \
        NAME##_sort3_(a,q1,q2,q3,&sw);                                      \
    } else NAME##_sort3_(a,q1,q2,q3,&sw);                                   \
    *hint = sw==0?0:(n>=NCI_PDQ_NINTHER&&sw>=9)||(n<NCI_PDQ_NINTHER&&sw>=3)?1:2;\
    return q2;                                                               \
}                                                                            \
static size_t NAME##_part_(TYPE *a, size_t n, size_t pi, int *ap) {          \
    { TYPE t=a[0]; a[0]=a[pi]; a[pi]=t; }                                   \
    TYPE pv = a[0];                                                          \
    size_t first=1, last=n-1;                                                \
    while (first<=last && LESS(a[first],pv)) first++;                        \
    while (first<=last && LESS(pv,a[last]))  last--;                         \
    *ap = (first > last);                                                    \
    /* Branchless block partition (pdqsort / Rust sort_unstable): collect the \
     * offsets of misplaced elements per cache-line block with a predicate    \
     * add (no data-dependent branch), then swap in bulk. Eliminates the      \
     * ~50%-unpredictable partition branch that dominates random/few-unique    \
     * inputs, the same win the generic nci_partition already takes. */        \
    unsigned char ol_[NCI_BLK], or_[NCI_BLK];                               \
    size_t nl_=0, nr_=0, sl_=0, sr_=0, gl_=first, gr_=last;                 \
    while (first + NCI_BLK - 1 < last) {                                     \
        if (nl_==0) { gl_=first; sl_=0;                                      \
            for (size_t k=0;k<NCI_BLK;k++) { ol_[nl_]=(unsigned char)k;      \
                nl_ += (size_t)!LESS(a[first+k],pv); }                       \
            first += NCI_BLK; }                                              \
        if (nr_==0) { if (first+NCI_BLK-1>last && nl_==0) break;             \
            gr_=last; sr_=0; size_t b_=last-first+1;                         \
            if (b_>NCI_BLK) b_=NCI_BLK;                                      \
            if (b_==0) break;                                                \
            for (size_t k=0;k<b_;k++) { or_[nr_]=(unsigned char)k;           \
                nr_ += (size_t)!LESS(pv,a[last-k]); }                        \
            last -= b_; }                                                    \
        size_t sw_=nl_<nr_?nl_:nr_;                                          \
        for (size_t k=0;k<sw_;k++) { size_t li=gl_+ol_[sl_+k];              \
            size_t ri=gr_-or_[sr_+k];                                        \
            TYPE t=a[li]; a[li]=a[ri]; a[ri]=t; }                            \
        nl_-=sw_; sl_+=sw_; nr_-=sw_; sr_+=sw_;                              \
    }                                                                        \
    if (nl_>0) first=gl_;                                                    \
    if (nr_>0) last=gr_;                                                     \
    while (first <= last) {                                                  \
        while (first<=last && LESS(a[first],pv)) first++;                    \
        while (first<=last && LESS(pv,a[last]))  last--;                     \
        if (first>last) break;                                               \
        TYPE t=a[first]; a[first]=a[last]; a[last]=t;                        \
        first++; last--;                                                     \
    }                                                                        \
    if (last > 0) { TYPE t=a[0]; a[0]=a[last]; a[last]=t; }                 \
    return last;                                                             \
}                                                                            \
static void NAME##_loop_(TYPE *a, size_t n, int limit) {                     \
    int wb=1, wp=1;                                                          \
    while (1) {                                                              \
        if (n <= NCI_TYPED_ISORT_THRESHOLD) { NAME##_isort_(a,n); return; }  \
        if (limit==0) { NAME##_heap_(a,n); return; }                         \
        if (!wb) {                                                           \
            if (n >= 8) {                                                    \
                size_t md_=1; while(md_<n)md_<<=1;                           \
                uint64_t rr_=(uint64_t)n; size_t mm_=n/2;                    \
                for(size_t kk_=0;kk_<3;kk_++){                              \
                    size_t ix_=mm_-1+kk_;                                    \
                    rr_=rr_*UINT64_C(0xd1342543de82ef95)+1;                  \
                    size_t oo_=(size_t)(rr_&(uint64_t)(md_-1));              \
                    if(oo_>=n)oo_-=n;                                        \
                    TYPE tt_=a[ix_];a[ix_]=a[oo_];a[oo_]=tt_;               \
                }                                                            \
            }                                                                \
            limit--;                                                         \
        }                                                                    \
        int hint;                                                            \
        size_t pi = NAME##_pivot_(a, n, &hint);                              \
        if (hint==1) {                                                       \
            for (size_t i=0,j=n-1;i<j;i++,j--) {TYPE t=a[i];a[i]=a[j];a[j]=t;}\
            pi=n-1-pi; hint=0;                                              \
        }                                                                    \
        if (wp && hint==0) {                                                 \
            int mv_=0, ok_=1;                                                \
            for(size_t i=1;i<n&&ok_;i++){                                    \
                TYPE t=a[i]; size_t j=i;                                     \
                while(j>0&&LESS(t,a[j-1])){a[j]=a[j-1];j--;                 \
                    if(++mv_>NCI_PDQ_PARTIAL_LIMIT){a[j]=t;ok_=0;break;}}   \
                if(ok_) a[j]=t;                                              \
            }                                                                \
            if (ok_) return;                                                 \
        }                                                                    \
        int ap; size_t p = NAME##_part_(a, n, pi, &ap);                      \
        wp = ap;                                                             \
        size_t ls=p, rs=p+1<n?n-p-1:0;                                      \
        wb = (ls>=n/8)&&(rs>=n/8);                                           \
        if (ls<rs) { NAME##_loop_(a,ls,limit); a+=p+1; n=rs; }              \
        else       { NAME##_loop_(a+p+1,rs,limit); n=ls; }                  \
    }                                                                        \
}                                                                            \
static void NAME(TYPE *a, size_t n) {                                        \
    if (n <= 1) return;                                                      \
    NAME##_loop_(a, n, nci_log2(n)+1);                                       \
}

#define NCI_INT_LESS(a, b) ((a) < (b))
#define NCI_DBL_LESS(a, b) ((a) < (b))

NCI_DEFINE_TYPED_SORT(nci_pdqsort_int,    int,    NCI_INT_LESS)
NCI_DEFINE_TYPED_SORT(nci_pdqsort_double, double, NCI_DBL_LESS)

/* ═══════════════════════════════════════════════════════════════════════════
 *  String Array Sort — 3-way radix quicksort (multikey quicksort)
 *
 *  Sorts an array of NUL-terminated C strings into unsigned-byte lexicographic
 *  order (identical to strcmp ordering, so it is a drop-in for a comparison
 *  sort). A comparison sort calls strcmp on every comparison and therefore
 *  re-reads shared prefixes O(n log n) times; multikey quicksort (Bentley &
 *  Sedgewick, "Fast Algorithms for Sorting and Searching Strings", 1997)
 *  partitions on a single byte position at a time and descends to the next
 *  position only for the equal group, so each byte of each string is examined
 *  about once. The win grows with the length of common prefixes — exactly the
 *  shape of real string data (paths, URLs, identifiers, sorted keys).
 *
 *  Robustness (no worst-case regression versus the previous pdqsort):
 *   - median-of-three pivot keeps the </> recursion balanced, including on
 *     already-sorted input;
 *   - an introspective depth limit falls back to heapsort-by-strcmp, so the
 *     worst case stays O(n log n) comparisons;
 *   - the equal group is *iterated* (d -> d+1) rather than recursed, so an
 *     arbitrarily long shared prefix can never grow the call stack.
 *
 *  Safety of the s[d] read: a string only reaches depth d through equal-group
 *  descents whose pivot byte was non-zero, so it has no NUL in [0, d); thus
 *  s[d] is always within [0, strlen(s)] and never reads past the terminator.
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NCI_STR_ISORT_THRESHOLD 16
#define NCI_STR_B(s, d) ((int)(unsigned char)(s)[d])

static int nci_cmp_strptr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Insertion sort a[lo..hi] (inclusive) by full strcmp — the small-range base
 * case, where re-scanning whole strings is cheaper than partition bookkeeping. */
static void nci_str_isort(const char **a, size_t lo, size_t hi) {
    for (size_t i = lo + 1; i <= hi; i++) {
        const char *t = a[i];
        size_t j = i;
        while (j > lo && strcmp(a[j - 1], t) > 0) { a[j] = a[j - 1]; j--; }
        a[j] = t;
    }
}

static void nci_sort_strings_rec(const char **a, size_t lo, size_t hi,
                                 size_t d, int limit) {
    if (limit < 0) {                      /* adversarial </> recursion: bail out */
        char tmp[sizeof(const char *)];
        nci_heapsort((char *)(a + lo), hi - lo + 1, sizeof(const char *),
                     nci_cmp_strptr, tmp);
        return;
    }
    while (hi - lo > NCI_STR_ISORT_THRESHOLD) {
        /* median-of-three pivot byte at depth d; move the median to a[lo] so the
         * equal group is never empty (keeps the partition cursors in bounds). */
        size_t mid = lo + (hi - lo) / 2;
        int blo = NCI_STR_B(a[lo], d);
        int bmid = NCI_STR_B(a[mid], d);
        int bhi = NCI_STR_B(a[hi], d);
        size_t medi;
        if (blo <= bmid)
            medi = (bmid <= bhi) ? mid : (blo <= bhi ? hi : lo);
        else
            medi = (blo <= bhi) ? lo : (bmid <= bhi ? hi : mid);
        if (medi != lo) { const char *t = a[lo]; a[lo] = a[medi]; a[medi] = t; }
        int pv = NCI_STR_B(a[lo], d);

        /* Sedgewick 3-way partition: [lo,lt) < pv, [lt,gt] == pv, (gt,hi] > pv.
         * The pivot a[lo] is itself == pv, so the equal band always holds at
         * least one element and gt never underflows below lo. */
        size_t lt = lo, gt = hi, i = lo + 1;
        while (i <= gt) {
            int c = NCI_STR_B(a[i], d);
            if (c < pv) {
                const char *t = a[lt]; a[lt] = a[i]; a[i] = t; lt++; i++;
            } else if (c > pv) {
                const char *t = a[i]; a[i] = a[gt]; a[gt] = t; gt--;
            } else {
                i++;
            }
        }

        /* Recurse on the smaller-alphabet </> groups (same depth); the limit
         * bounds only this quicksort-style recursion. */
        if (lt > lo) nci_sort_strings_rec(a, lo, lt - 1, d, limit - 1);
        if (gt < hi) nci_sort_strings_rec(a, gt + 1, hi, d, limit - 1);

        if (pv == 0) return;              /* equal group are completed strings */
        lo = lt; hi = gt; d++;            /* descend the equal group iteratively */
    }
    nci_str_isort(a, lo, hi);
}

static void nci_sort_strings(const char **a, size_t n) {
    if (n <= 1) return;
    nci_sort_strings_rec(a, 0, n - 1, 0, 2 * nci_log2(n) + 3);
}

#endif /* NEVERC_SORT_IMPL_H */
