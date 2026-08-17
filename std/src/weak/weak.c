#include "neverc/std/weak.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>

typedef struct ctrl_block {
    volatile int32_t strong;
    volatile int32_t weak;
    void *data;
    void (*free_fn)(void *);
    int   owns_data;
    uint64_t epoch;
    struct ctrl_block *next_free;
} ctrl_block_t;

static ctrl_block_t *g_freelist = NULL;

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
static SRWLOCK g_lock = SRWLOCK_INIT;
#define LOCK()   AcquireSRWLockExclusive(&g_lock)
#define UNLOCK() ReleaseSRWLockExclusive(&g_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_lock)
#define UNLOCK() pthread_mutex_unlock(&g_lock)
#endif

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

static int ctrl_matches(const ctrl_block_t *cb, uint64_t epoch) {
    return cb && epoch != 0 && cb->epoch == epoch;
}

static void ctrl_retire(ctrl_block_t *cb) {
    LOCK();
    cb->epoch++;
    if (cb->epoch == 0) cb->epoch = 1;
    cb->strong = 0;
    cb->weak = 0;
    cb->data = NULL;
    cb->free_fn = NULL;
    cb->owns_data = 0;
    cb->next_free = g_freelist;
    g_freelist = cb;
    UNLOCK();
}

static ctrl_block_t *ctrl_new(void *data, void (*free_fn)(void *), int owns) {
    ctrl_block_t *cb = NULL;
    uint64_t epoch = 1;
    LOCK();
    if (g_freelist) {
        cb = g_freelist;
        g_freelist = cb->next_free;
        epoch = cb->epoch;
        UNLOCK();
        memset(cb, 0, sizeof(*cb));
        cb->epoch = epoch;
    } else {
        UNLOCK();
        cb = (ctrl_block_t *)calloc(1, sizeof(*cb));
        if (!cb) return NULL;
        cb->epoch = 1;
    }
    cb->strong = 1;
    cb->weak = 1;
    cb->data = data;
    cb->free_fn = free_fn;
    cb->owns_data = owns;
    cb->next_free = NULL;
    return cb;
}

static neverc_weak_strong_t strong_from_cb(ctrl_block_t *cb) {
    neverc_weak_strong_t s = {0};
    if (!cb) return s;
    s.ptr = cb->data;
    s._ctrl = cb;
    s._epoch = cb->epoch;
    return s;
}

static void ctrl_release_strong(ctrl_block_t *cb) {
    int32_t s = release_count(&cb->strong);
    if (s == 0) {
        void *data = cb->data;
        void (*free_fn)(void *) = cb->free_fn;
        int owns = cb->owns_data;
        cb->data = NULL;
        cb->free_fn = NULL;
        cb->owns_data = 0;
        if (free_fn)
            free_fn(data);
        else if (owns)
            free(data);

        int32_t w = release_count(&cb->weak);
        if (w == 0) ctrl_retire(cb);
    }
}

static void ctrl_release_weak(ctrl_block_t *cb) {
    int32_t w = release_count(&cb->weak);
    if (w == 0) ctrl_retire(cb);
}

neverc_weak_strong_t neverc_weak_new(void *data, size_t size) {
    neverc_weak_strong_t s = {0};
    if (!data || size == 0) return s;
    void *copy = malloc(size);
    if (!copy) return s;
    memcpy(copy, data, size);
    ctrl_block_t *cb = ctrl_new(copy, NULL, 1);
    if (!cb) { free(copy); return s; }
    return strong_from_cb(cb);
}

neverc_weak_strong_t neverc_weak_new_with_free(void *data, void (*free_fn)(void *)) {
    neverc_weak_strong_t s = {0};
    ctrl_block_t *cb = ctrl_new(data, free_fn, 0);
    if (!cb) return s;
    return strong_from_cb(cb);
}

neverc_weak_strong_t neverc_weak_strong_retain(neverc_weak_strong_t s) {
    neverc_weak_strong_t retained = {0};
    ctrl_block_t *cb = (ctrl_block_t *)s._ctrl;
    LOCK();
    if (ctrl_matches(cb, s._epoch) && retain_count(&cb->strong))
        retained = s;
    UNLOCK();
    return retained;
}

void neverc_weak_strong_release(neverc_weak_strong_t *s) {
    if (!s || !s->_ctrl) return;
    ctrl_block_t *cb = (ctrl_block_t *)s->_ctrl;
    uint64_t epoch = s->_epoch;
    s->ptr = NULL;
    s->_ctrl = NULL;
    s->_epoch = 0;
    LOCK();
    int live = ctrl_matches(cb, epoch);
    UNLOCK();
    if (live) ctrl_release_strong(cb);
}

struct neverc_weak_ref {
    ctrl_block_t *cb;
    volatile int32_t refs;
};

neverc_weak_ref_t *neverc_weak_make(neverc_weak_strong_t s) {
    ctrl_block_t *cb = (ctrl_block_t *)s._ctrl;
    neverc_weak_ref_t *w = (neverc_weak_ref_t *)malloc(sizeof(*w));
    if (!w) return NULL;
    LOCK();
    if (!ctrl_matches(cb, s._epoch) ||
        NEVERC_ATOMIC_LOAD32(&cb->strong) <= 0 ||
        !retain_count(&cb->weak)) {
        UNLOCK();
        free(w);
        return NULL;
    }
    w->cb = cb;
    w->refs = 1;
    UNLOCK();
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
    neverc_weak_strong_t s = {0};
    if (!w || !w->cb) return s;

    for (;;) {
        int32_t cur = NEVERC_ATOMIC_LOAD32(&w->cb->strong);
        if (cur <= 0 || cur == INT32_MAX) return s;
        if (NEVERC_ATOMIC_CAS32(&w->cb->strong, cur, cur + 1))
            return strong_from_cb(w->cb);
    }
}

int neverc_weak_ref_equal(const neverc_weak_ref_t *a, const neverc_weak_ref_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return a->cb == b->cb;
}

int neverc_weak_strong_count(neverc_weak_strong_t s) {
    ctrl_block_t *cb = (ctrl_block_t *)s._ctrl;
    int n = 0;
    LOCK();
    if (ctrl_matches(cb, s._epoch))
        n = NEVERC_ATOMIC_LOAD32(&cb->strong);
    UNLOCK();
    return n;
}

int neverc_weak_ref_count(neverc_weak_ref_t *w) {
    if (!w || !w->cb) return 0;
    return NEVERC_ATOMIC_LOAD32(&w->cb->weak);
}
