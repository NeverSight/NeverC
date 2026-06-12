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

static void heap_up(neverc_heap_interface_t *h, int j) {
    for (;;) {
        int i = (j - 1) / 2;
        if (i == j || !h->less_fn(h->data, j, i))
            break;
        h->swap_fn(h->data, i, j);
        j = i;
    }
}

static int heap_down(neverc_heap_interface_t *h, int i0, int n) {
    int j1 = 2 * i0 + 1;
    if (j1 >= n || j1 < 0)
        return 0;

    int leaf = i0;
    for (;;) {
        j1 = 2 * leaf + 1;
        if (j1 >= n || j1 < 0) break;
        int j = j1;
        int j2 = j1 + 1;
        if (j2 < n && h->less_fn(h->data, j2, j1))
            j = j2;
        h->swap_fn(h->data, leaf, j);
        leaf = j;
    }

    while (leaf > i0) {
        int parent = (leaf - 1) / 2;
        if (!h->less_fn(h->data, leaf, parent))
            break;
        h->swap_fn(h->data, leaf, parent);
        leaf = parent;
    }

    return leaf > i0;
}

void neverc_heap_init(neverc_heap_interface_t *h) {
    int n = h->len_fn(h->data);
    for (int i = n / 2 - 1; i >= 0; i--)
        heap_down(h, i, n);
}

void neverc_heap_push(neverc_heap_interface_t *h, const void *elem) {
    h->push_fn(h->data, elem);
    heap_up(h, h->len_fn(h->data) - 1);
}

void neverc_heap_pop(neverc_heap_interface_t *h, void *out) {
    int n = h->len_fn(h->data) - 1;
    h->swap_fn(h->data, 0, n);
    heap_down(h, 0, n);
    h->pop_fn(h->data, out);
}

void neverc_heap_remove(neverc_heap_interface_t *h, int i, void *out) {
    int n = h->len_fn(h->data) - 1;
    if (n != i) {
        h->swap_fn(h->data, i, n);
        if (!heap_down(h, i, n))
            heap_up(h, i);
    }
    h->pop_fn(h->data, out);
}

void neverc_heap_fix(neverc_heap_interface_t *h, int i) {
    if (!heap_down(h, i, h->len_fn(h->data)))
        heap_up(h, i);
}
