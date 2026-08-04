#ifndef NEVERC_CONTAINER_HEAP_H
#define NEVERC_CONTAINER_HEAP_H

/*
 * NeverC container/heap — binary min-heap on a contiguous array
 * (mirrors Go container/heap package).
 *
 * The heap operates on a user-provided contiguous array through an
 * interface struct carrying function pointers (Len / Less / Swap / Push / Pop).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_heap_interface {
    void *data;
    int  (*len_fn)(void *data);
    int  (*less_fn)(void *data, int i, int j);
    void (*swap_fn)(void *data, int i, int j);
    void (*push_fn)(void *data, const void *elem);
    void (*pop_fn)(void *data, void *out);
} neverc_heap_interface_t;

/* Invalid interfaces, empty heaps, and out-of-range indices are safe no-ops.
 * Push and pop/remove additionally require non-NULL elem/out pointers. */
void neverc_heap_init(neverc_heap_interface_t *h);
void neverc_heap_push(neverc_heap_interface_t *h, const void *elem);
void neverc_heap_pop(neverc_heap_interface_t *h, void *out);
void neverc_heap_remove(neverc_heap_interface_t *h, int i, void *out);
void neverc_heap_fix(neverc_heap_interface_t *h, int i);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/container.h>
#endif


#endif
