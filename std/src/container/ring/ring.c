/*
 * NeverC container/ring — circular doubly linked list.
 * Mirrors Go container/ring.
 *
 * NULL represents an empty ring. A single-element ring points to itself.
 * Nodes are heap-allocated (malloc). Call neverc_ring_free to release all.
 */

#include "neverc/std/container/ring.h"
#include <stdlib.h>

static neverc_ring_t *ring_init(neverc_ring_t *r) {
    r->next = r;
    r->prev = r;
    return r;
}

neverc_ring_t *neverc_ring_new(int n) {
    if (n <= 0) return NULL;

    neverc_ring_t *r = (neverc_ring_t *)malloc(sizeof(neverc_ring_t));
    if (!r) return NULL;
    r->value = NULL;

    neverc_ring_t *p = r;
    for (int i = 1; i < n; i++) {
        neverc_ring_t *node = (neverc_ring_t *)malloc(sizeof(neverc_ring_t));
        if (!node) {
            /* partial cleanup on OOM */
            r->prev = p;
            p->next = r;
            neverc_ring_free(r);
            return NULL;
        }
        node->value = NULL;
        node->prev = p;
        p->next = node;
        p = node;
    }
    p->next = r;
    r->prev = p;
    return r;
}

void neverc_ring_free(neverc_ring_t *r) {
    if (!r) return;
    neverc_ring_t *p = r->next;
    while (p != r) {
        neverc_ring_t *next = p->next;
        free(p);
        p = next;
    }
    free(r);
}

neverc_ring_t *neverc_ring_next(neverc_ring_t *r) {
    if (r->next == NULL) return ring_init(r);
    return r->next;
}

neverc_ring_t *neverc_ring_prev(neverc_ring_t *r) {
    if (r->next == NULL) return ring_init(r);
    return r->prev;
}

neverc_ring_t *neverc_ring_move(neverc_ring_t *r, int n) {
    if (r->next == NULL) ring_init(r);
    if (n < 0) {
        for (; n < 0; n++)
            r = r->prev;
    } else {
        for (; n > 0; n--)
            r = r->next;
    }
    return r;
}

neverc_ring_t *neverc_ring_link(neverc_ring_t *r, neverc_ring_t *s) {
    neverc_ring_t *n = neverc_ring_next(r);
    if (s != NULL) {
        neverc_ring_t *p = neverc_ring_prev(s);
        r->next = s;
        s->prev = r;
        n->prev = p;
        p->next = n;
    }
    return n;
}

neverc_ring_t *neverc_ring_unlink(neverc_ring_t *r, int n) {
    if (n <= 0) return NULL;
    return neverc_ring_link(r, neverc_ring_move(r, n + 1));
}

int neverc_ring_len(const neverc_ring_t *r) {
    int n = 0;
    if (r != NULL) {
        n = 1;
        const neverc_ring_t *p = r->next;
        while (p != r) {
            n++;
            p = p->next;
        }
    }
    return n;
}

void neverc_ring_do(neverc_ring_t *r,
                    void (*f)(void *value, void *ctx), void *ctx) {
    if (r == NULL) return;
    f(r->value, ctx);
    neverc_ring_t *p = r->next;
    while (p != r) {
        f(p->value, ctx);
        p = p->next;
    }
}
