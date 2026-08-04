#include "neverc/std/weak.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    volatile int32_t strong;
    volatile int32_t weak;
    void *data;
    void (*free_fn)(void *);
    int   owns_data;
} ctrl_block_t;

static int retain_count(volatile int32_t *count) {
    for (;;) {
        int32_t current = NEVERC_ATOMIC_LOAD32(count);
        if (current <= 0 || current == INT32_MAX) return 0;
        if (NEVERC_ATOMIC_CAS32(count, current, current + 1)) return 1;
    }
}

static int32_t release_count(volatile int32_t *count) {
    for (;;) {
        int32_t current = NEVERC_ATOMIC_LOAD32(count);
        if (current <= 0) return -1;
        if (NEVERC_ATOMIC_CAS32(count, current, current - 1))
            return current - 1;
    }
}

static ctrl_block_t *ctrl_new(void *data, void (*free_fn)(void *), int owns) {
    ctrl_block_t *cb = (ctrl_block_t *)calloc(1, sizeof(*cb));
    if (!cb) return NULL;
    cb->strong = 1;
    cb->weak = 1;
    cb->data = data;
    cb->free_fn = free_fn;
    cb->owns_data = owns;
    return cb;
}

static void ctrl_release_strong(ctrl_block_t *cb) {
    int32_t s = release_count(&cb->strong);
    if (s == 0) {
        if (cb->free_fn)
            cb->free_fn(cb->data);
        else if (cb->owns_data)
            free(cb->data);
        cb->data = NULL;

        int32_t w = release_count(&cb->weak);
        if (w == 0) free(cb);
    }
}

static void ctrl_release_weak(ctrl_block_t *cb) {
    int32_t w = release_count(&cb->weak);
    if (w == 0) free(cb);
}

neverc_weak_strong_t neverc_weak_new(void *data, size_t size) {
    neverc_weak_strong_t s = {NULL, NULL};
    if (!data || size == 0) return s;
    void *copy = malloc(size);
    if (!copy) return s;
    memcpy(copy, data, size);
    ctrl_block_t *cb = ctrl_new(copy, NULL, 1);
    if (!cb) { free(copy); return s; }
    s.ptr = copy;
    s._ctrl = cb;
    return s;
}

neverc_weak_strong_t neverc_weak_new_with_free(void *data, void (*free_fn)(void *)) {
    neverc_weak_strong_t s = {NULL, NULL};
    ctrl_block_t *cb = ctrl_new(data, free_fn, 0);
    if (!cb) return s;
    s.ptr = data;
    s._ctrl = cb;
    return s;
}

neverc_weak_strong_t neverc_weak_strong_retain(neverc_weak_strong_t s) {
    neverc_weak_strong_t retained = {NULL, NULL};
    if (s._ctrl && retain_count(&((ctrl_block_t *)s._ctrl)->strong))
        retained = s;
    return retained;
}

void neverc_weak_strong_release(neverc_weak_strong_t *s) {
    if (s && s->_ctrl) {
        ctrl_release_strong((ctrl_block_t *)s->_ctrl);
        s->ptr = NULL;
        s->_ctrl = NULL;
    }
}

struct neverc_weak_ref {
    ctrl_block_t *cb;
    volatile int32_t refs;
};

neverc_weak_ref_t *neverc_weak_make(neverc_weak_strong_t s) {
    ctrl_block_t *cb = (ctrl_block_t *)s._ctrl;
    if (!cb) return NULL;
    neverc_weak_ref_t *w = (neverc_weak_ref_t *)malloc(sizeof(*w));
    if (!w) return NULL;
    if (!retain_count(&cb->weak)) { free(w); return NULL; }
    w->cb = cb;
    w->refs = 1;
    return w;
}

neverc_weak_ref_t *neverc_weak_ref_retain(neverc_weak_ref_t *w) {
    if (!w || !w->cb || !retain_count(&w->refs)) return NULL;
    if (!retain_count(&w->cb->weak)) {
        (void)release_count(&w->refs);
        return NULL;
    }
    return w;
}

void neverc_weak_ref_release(neverc_weak_ref_t *w) {
    if (!w || !w->cb) return;
    ctrl_block_t *cb = w->cb;
    int32_t refs = release_count(&w->refs);
    if (refs < 0) return;
    if (refs == 0) w->cb = NULL;
    ctrl_release_weak(cb);
    if (refs == 0) free(w);
}

void *neverc_weak_value(neverc_weak_ref_t *w) {
    if (!w || !w->cb) return NULL;
    if (NEVERC_ATOMIC_LOAD32(&w->cb->strong) <= 0) return NULL;
    return w->cb->data;
}

neverc_weak_strong_t neverc_weak_upgrade(neverc_weak_ref_t *w) {
    neverc_weak_strong_t s = {NULL, NULL};
    if (!w || !w->cb) return s;

    for (;;) {
        int32_t cur = NEVERC_ATOMIC_LOAD32(&w->cb->strong);
        if (cur <= 0 || cur == INT32_MAX) return s;
        if (NEVERC_ATOMIC_CAS32(&w->cb->strong, cur, cur + 1)) {
            s.ptr = w->cb->data;
            s._ctrl = w->cb;
            return s;
        }
    }
}

int neverc_weak_ref_equal(const neverc_weak_ref_t *a, const neverc_weak_ref_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return a->cb == b->cb;
}

int neverc_weak_strong_count(neverc_weak_strong_t s) {
    if (!s._ctrl) return 0;
    return NEVERC_ATOMIC_LOAD32(&((ctrl_block_t *)s._ctrl)->strong);
}

int neverc_weak_ref_count(neverc_weak_ref_t *w) {
    if (!w || !w->cb) return 0;
    return NEVERC_ATOMIC_LOAD32(&w->cb->weak);
}
