/*
 * NeverC container/heap — binary min-heap on a contiguous array.
 * Mirrors Go container/heap: Init / Push / Pop / Remove / Fix.
 *
 * The user provides an interface struct with function pointers
 * (Len / Less / Swap / Push / Pop) — same abstraction as Go's heap.Interface.
 */

#include "neverc/container/heap.h"

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
    int i = i0;
    for (;;) {
        int j1 = 2 * i + 1;
        if (j1 >= n || j1 < 0)
            break;
        int j = j1;
        int j2 = j1 + 1;
        if (j2 < n && h->less_fn(h->data, j2, j1))
            j = j2;
        if (!h->less_fn(h->data, j, i))
            break;
        h->swap_fn(h->data, i, j);
        i = j;
    }
    return i > i0;
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
