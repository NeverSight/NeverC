#ifndef NEVERC_CONTAINER_RING_H
#define NEVERC_CONTAINER_RING_H

/*
 * NeverC container/ring — circular doubly linked list
 * (mirrors Go container/ring package).
 *
 * A ring has no beginning or end; any element is a reference to the whole ring.
 * NULL represents an empty ring.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_ring neverc_ring_t;

struct neverc_ring {
    neverc_ring_t *next;
    neverc_ring_t *prev;
    void          *value;
};

neverc_ring_t *neverc_ring_new(int n);
void           neverc_ring_free(neverc_ring_t *r);

neverc_ring_t *neverc_ring_next(neverc_ring_t *r);
neverc_ring_t *neverc_ring_prev(neverc_ring_t *r);
neverc_ring_t *neverc_ring_move(neverc_ring_t *r, int n);
neverc_ring_t *neverc_ring_link(neverc_ring_t *r, neverc_ring_t *s);
neverc_ring_t *neverc_ring_unlink(neverc_ring_t *r, int n);
int            neverc_ring_len(const neverc_ring_t *r);
void           neverc_ring_do(neverc_ring_t *r, void (*f)(void *value, void *ctx),
                              void *ctx);

#ifdef __cplusplus
}
#endif

#endif
