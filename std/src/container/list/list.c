/*
 * NeverC container/list — doubly linked list.
 * Mirrors Go container/list: sentinel-root design for O(1) ends.
 *
 * Nodes are heap-allocated (malloc). Call neverc_list_free to release all.
 */

#include "neverc/std/container/list.h"
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>

static void lazy_init(neverc_list_t *l) {
    if (l->root.next == NULL)
        neverc_list_init(l);
}

static neverc_list_element_t *insert_elem(neverc_list_t *l,
                                           neverc_list_element_t *e,
                                           neverc_list_element_t *at) {
    e->prev = at;
    e->next = at->next;
    e->prev->next = e;
    e->next->prev = e;
    e->list = l;
    l->len++;
    return e;
}

static neverc_list_element_t *insert_value(neverc_list_t *l, void *v,
                                            neverc_list_element_t *at) {
    if (l->len == INT_MAX) return NULL;
    neverc_list_element_t *e =
        (neverc_list_element_t *)malloc(sizeof(neverc_list_element_t));
    if (!e) return NULL;
    e->value = v;
    e->next = NULL;
    e->prev = NULL;
    e->list = NULL;
    return insert_elem(l, e, at);
}

static void remove_elem(neverc_list_t *l, neverc_list_element_t *e) {
    e->prev->next = e->next;
    e->next->prev = e->prev;
    e->next = NULL;
    e->prev = NULL;
    e->list = NULL;
    l->len--;
}

static void move_elem(neverc_list_element_t *e, neverc_list_element_t *at) {
    if (e == at) return;
    e->prev->next = e->next;
    e->next->prev = e->prev;

    e->prev = at;
    e->next = at->next;
    e->prev->next = e;
    e->next->prev = e;
}

neverc_list_t *neverc_list_new(void) {
    neverc_list_t *l = (neverc_list_t *)malloc(sizeof(neverc_list_t));
    if (!l) return NULL;
    neverc_list_init(l);
    return l;
}

void neverc_list_init(neverc_list_t *l) {
    if (!l) return;
    l->root.next = &l->root;
    l->root.prev = &l->root;
    l->root.list = NULL;
    l->root.value = NULL;
    l->len = 0;
}

void neverc_list_free(neverc_list_t *l) {
    if (!l) return;
    if (!l->root.next) {
        neverc_list_init(l);
        return;
    }
    neverc_list_element_t *e = l->root.next;
    while (e != &l->root) {
        neverc_list_element_t *next = e->next;
        free(e);
        e = next;
    }
    neverc_list_init(l);
}

int neverc_list_len(const neverc_list_t *l) {
    return l ? l->len : 0;
}

neverc_list_element_t *neverc_list_front(const neverc_list_t *l) {
    if (!l || l->len <= 0 || !l->root.next) return NULL;
    return l->root.next;
}

neverc_list_element_t *neverc_list_back(const neverc_list_t *l) {
    if (!l || l->len <= 0 || !l->root.prev) return NULL;
    return l->root.prev;
}

neverc_list_element_t *neverc_list_push_front(neverc_list_t *l, void *value) {
    if (!l) return NULL;
    lazy_init(l);
    return insert_value(l, value, &l->root);
}

neverc_list_element_t *neverc_list_push_back(neverc_list_t *l, void *value) {
    if (!l) return NULL;
    lazy_init(l);
    return insert_value(l, value, l->root.prev);
}

void *neverc_list_remove(neverc_list_t *l, neverc_list_element_t *e) {
    if (!l || !e) return NULL;
    if (e->list != l) return NULL;
    void *v = e->value;
    remove_elem(l, e);
    free(e);
    return v;
}

neverc_list_element_t *neverc_list_insert_before(neverc_list_t *l, void *value,
                                                  neverc_list_element_t *mark) {
    if (!l || !mark) return NULL;
    if (mark->list != l) return NULL;
    return insert_value(l, value, mark->prev);
}

neverc_list_element_t *neverc_list_insert_after(neverc_list_t *l, void *value,
                                                 neverc_list_element_t *mark) {
    if (!l || !mark) return NULL;
    if (mark->list != l) return NULL;
    return insert_value(l, value, mark);
}

void neverc_list_move_to_front(neverc_list_t *l, neverc_list_element_t *e) {
    if (!l || !e) return;
    if (e->list != l || l->root.next == e) return;
    move_elem(e, &l->root);
}

void neverc_list_move_to_back(neverc_list_t *l, neverc_list_element_t *e) {
    if (!l || !e) return;
    if (e->list != l || l->root.prev == e) return;
    move_elem(e, l->root.prev);
}

void neverc_list_move_before(neverc_list_t *l, neverc_list_element_t *e,
                              neverc_list_element_t *mark) {
    if (!l || !e || !mark) return;
    if (e->list != l || e == mark || mark->list != l) return;
    move_elem(e, mark->prev);
}

void neverc_list_move_after(neverc_list_t *l, neverc_list_element_t *e,
                             neverc_list_element_t *mark) {
    if (!l || !e || !mark) return;
    if (e->list != l || e == mark || mark->list != l) return;
    move_elem(e, mark);
}

neverc_list_element_t *neverc_list_element_next(const neverc_list_element_t *e) {
    if (!e || !e->list) return NULL;
    neverc_list_element_t *p = e->next;
    if (p && p != &e->list->root)
        return p;
    return NULL;
}

neverc_list_element_t *neverc_list_element_prev(const neverc_list_element_t *e) {
    if (!e || !e->list) return NULL;
    neverc_list_element_t *p = e->prev;
    if (p && p != &e->list->root)
        return p;
    return NULL;
}

static void rollback_after(neverc_list_t *l, neverc_list_element_t *stop) {
    neverc_list_element_t *e = stop->next;
    while (e != &l->root) {
        neverc_list_element_t *next = e->next;
        remove_elem(l, e);
        free(e);
        e = next;
    }
}

static void rollback_before(neverc_list_t *l, neverc_list_element_t *orig_front) {
    neverc_list_element_t *e = l->root.next;
    while (e != orig_front && e != &l->root) {
        neverc_list_element_t *next = e->next;
        remove_elem(l, e);
        free(e);
        e = next;
    }
}

int neverc_list_push_back_list(neverc_list_t *l, const neverc_list_t *other) {
    if (!l || !other) return -1;
    lazy_init(l);
    int n = other->len;
    if (n <= 0) return 0;
    if (l->len < 0 || n > INT_MAX - l->len) return -1;
    neverc_list_element_t *stop = l->root.prev;
    neverc_list_element_t *e = neverc_list_front(other);
    int added = 0;
    for (int i = 0; i < n && e; i++) {
        if (!insert_value(l, e->value, l->root.prev)) {
            rollback_after(l, stop);
            return -1;
        }
        added++;
        e = neverc_list_element_next(e);
    }
    return added;
}

int neverc_list_push_front_list(neverc_list_t *l, const neverc_list_t *other) {
    if (!l || !other) return -1;
    lazy_init(l);
    int n = other->len;
    if (n <= 0) return 0;
    if (l->len < 0 || n > INT_MAX - l->len) return -1;
    neverc_list_element_t *orig_front = l->root.next;
    neverc_list_element_t *e = neverc_list_back(other);
    int added = 0;
    for (int i = 0; i < n && e; i++) {
        if (!insert_value(l, e->value, &l->root)) {
            rollback_before(l, orig_front);
            return -1;
        }
        added++;
        e = neverc_list_element_prev(e);
    }
    return added;
}
