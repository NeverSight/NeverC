/*
 * NeverC container/heap — binary min-heap on a contiguous array.
 * Mirrors Go container/heap: Init / Push / Pop / Remove / Fix.
 *
 * The user provides an interface struct with function pointers
 * (Len / Less / Swap / Push / Pop) — same abstraction as Go's heap.Interface.
 *
 * Uses bottom-up sift-down (Floyd's optimization):
 * Phase 1 — push to leaf following smaller children (1 Less per level,
 *           no parent comparison, no data-dependent branch).
 * Phase 2 — sift back up from the leaf to the correct position
 *           (expected O(1) steps since most elements belong near bottom).
 * Net effect: ~50% fewer Less calls vs standard top-down sift-down,
 * and the hot loop in phase 1 is branch-free (always swaps), avoiding
 * pipeline stalls from mispredicted early-exit branches.
 */

#include "neverc/std/container/heap.h"
#include <limits.h>

static void heap_up(neverc_heap_interface_t *h, int j) {
    while (j > 0) {
        int i = (j - 1) / 2;
        if (!h->less_fn(h->data, j, i))
            break;
        h->swap_fn(h->data, i, j);
        j = i;
    }
}

static int heap_down(neverc_heap_interface_t *h, int i0, int n) {
    if (i0 < 0 || i0 >= n || n <= 1) return 0;

    unsigned int un = (unsigned int)n;
    unsigned int leaf = (unsigned int)i0;
    if (leaf >= un / 2u) return 0;

    while (leaf < un / 2u) {
        unsigned int j1 = 2u * leaf + 1u;
        unsigned int j = j1;
        unsigned int j2 = j1 + 1u;
        if (j2 < un && h->less_fn(h->data, (int)j2, (int)j1))
            j = j2;
        h->swap_fn(h->data, (int)leaf, (int)j);
        leaf = j;
    }

    while (leaf > (unsigned int)i0) {
        unsigned int parent = (leaf - 1u) / 2u;
        if (!h->less_fn(h->data, (int)leaf, (int)parent))
            break;
        h->swap_fn(h->data, (int)leaf, (int)parent);
        leaf = parent;
    }

    return leaf > (unsigned int)i0;
}

void neverc_heap_init(neverc_heap_interface_t *h) {
    if (!h || !h->len_fn || !h->less_fn || !h->swap_fn) return;
    int n = h->len_fn(h->data);
    if (n <= 1) return;
    for (int i = n / 2; i > 0;) {
        i--;
        heap_down(h, i, n);
    }
}

void neverc_heap_push(neverc_heap_interface_t *h, const void *elem) {
    if (!h || !elem || !h->len_fn || !h->less_fn || !h->swap_fn ||
        !h->push_fn)
        return;
    int before = h->len_fn(h->data);
    if (before < 0 || before == INT_MAX) return;
    h->push_fn(h->data, elem);
    int after = h->len_fn(h->data);
    if (after != before + 1) return;
    heap_up(h, after - 1);
}

void neverc_heap_pop(neverc_heap_interface_t *h, void *out) {
    if (!h || !out || !h->len_fn || !h->less_fn || !h->swap_fn ||
        !h->pop_fn)
        return;
    int length = h->len_fn(h->data);
    if (length <= 0) return;
    int n = length - 1;
    if (n != 0) h->swap_fn(h->data, 0, n);
    (void)heap_down(h, 0, n);
    h->pop_fn(h->data, out);
}

void neverc_heap_remove(neverc_heap_interface_t *h, int i, void *out) {
    if (!h || !out || !h->len_fn || !h->less_fn || !h->swap_fn ||
        !h->pop_fn)
        return;
    int length = h->len_fn(h->data);
    if (i < 0 || i >= length) return;
    int n = length - 1;
    if (n != i) {
        h->swap_fn(h->data, i, n);
        if (!heap_down(h, i, n))
            heap_up(h, i);
    }
    h->pop_fn(h->data, out);
}

void neverc_heap_fix(neverc_heap_interface_t *h, int i) {
    if (!h || !h->len_fn || !h->less_fn || !h->swap_fn) return;
    int n = h->len_fn(h->data);
    if (i < 0 || i >= n) return;
    if (!heap_down(h, i, n))
        heap_up(h, i);
}
