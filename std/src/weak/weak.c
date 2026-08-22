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
static void weak_lock(void) {
    while (pthread_mutex_lock(&g_lock) != 0) {
    }
}
#define LOCK()   weak_lock()
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

static void ctrl_retire_locked(ctrl_block_t *cb) {
    cb->epoch++;
    if (cb->epoch == 0) cb->epoch = 1;
    cb->strong = 0;
    cb->weak = 0;
    cb->data = NULL;
    cb->free_fn = NULL;
    cb->owns_data = 0;
    cb->next_free = g_freelist;
    g_freelist = cb;
}

static void ctrl_publish(ctrl_block_t *cb, void *data, void (*free_fn)(void *),
                         int owns, uint64_t epoch) {
    memset(cb, 0, sizeof(*cb));
    cb->epoch = epoch;
    cb->strong = 1;
    cb->weak = 1;
    cb->data = data;
    cb->free_fn = free_fn;
    cb->owns_data = owns;
}

static ctrl_block_t *ctrl_new(void *data, void (*free_fn)(void *), int owns) {
    ctrl_block_t *cb;
    LOCK();
    if (g_freelist) {
        cb = g_freelist;
        g_freelist = cb->next_free;
        uint64_t epoch = cb->epoch;
        /* Publish epoch/strong/weak/data before dropping the lock so a
         * stale release that already observed the previous epoch cannot
         * decrement this new life. */
        ctrl_publish(cb, data, free_fn, owns, epoch);
        UNLOCK();
        return cb;
    }
    UNLOCK();
    cb = (ctrl_block_t *)calloc(1, sizeof(*cb));
    if (!cb) return NULL;
    ctrl_publish(cb, data, free_fn, owns, 1);
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

    void *data = NULL;
    void (*free_fn)(void *) = NULL;
    int owns = 0;
    int drop_payload = 0;

    LOCK();
    if (ctrl_matches(cb, epoch)) {
        int32_t remaining = release_count(&cb->strong);
        if (remaining == 0) {
            data = cb->data;
            free_fn = cb->free_fn;
            owns = cb->owns_data;
            cb->data = NULL;
            cb->free_fn = NULL;
            cb->owns_data = 0;
            drop_payload = 1;
            if (release_count(&cb->weak) == 0)
                ctrl_retire_locked(cb);
        }
    }
    UNLOCK();

    if (drop_payload) {
        if (free_fn)
            free_fn(data);
        else if (owns)
            free(data);
    }
}

struct neverc_weak_ref {
    ctrl_block_t *cb;
    uint64_t epoch;
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
    w->epoch = cb->epoch;
    w->refs = 1;
    UNLOCK();
    return w;
}

neverc_weak_ref_t *neverc_weak_ref_retain(neverc_weak_ref_t *w) {
    if (!w) return NULL;
    LOCK();
    /* Pin the control block first. Incrementing refs before weak left a
     * window where last-strong could retire (and recycle) the block while
     * this handle still had refs > 0. */
    if (!ctrl_matches(w->cb, w->epoch) ||
        NEVERC_ATOMIC_LOAD32(&w->refs) <= 0 ||
        !retain_count(&w->cb->weak)) {
        UNLOCK();
        return NULL;
    }
    if (!retain_count(&w->refs)) {
        if (release_count(&w->cb->weak) == 0)
            ctrl_retire_locked(w->cb);
        UNLOCK();
        return NULL;
    }
    UNLOCK();
    return w;
}

void neverc_weak_ref_release(neverc_weak_ref_t *w) {
    ctrl_block_t *cb;
    uint64_t epoch;
    int32_t refs;
    if (!w) return;
    LOCK();
    if (!w->cb) {
        UNLOCK();
        return;
    }
    cb = w->cb;
    epoch = w->epoch;
    refs = release_count(&w->refs);
    if (refs < 0) {
        UNLOCK();
        return;
    }
    if (refs == 0) {
        w->cb = NULL;
        w->epoch = 0;
    }
    if (ctrl_matches(cb, epoch) && release_count(&cb->weak) == 0)
        ctrl_retire_locked(cb);
    UNLOCK();
    if (refs == 0) free(w);
}

void *neverc_weak_value(neverc_weak_ref_t *w) {
    void *data = NULL;
    if (!w) return NULL;
    LOCK();
    if (ctrl_matches(w->cb, w->epoch) &&
        NEVERC_ATOMIC_LOAD32(&w->cb->strong) > 0)
        data = w->cb->data;
    UNLOCK();
    return data;
}

neverc_weak_strong_t neverc_weak_upgrade(neverc_weak_ref_t *w) {
    neverc_weak_strong_t s = {0};
    if (!w) return s;
    LOCK();
    if (ctrl_matches(w->cb, w->epoch) && retain_count(&w->cb->strong))
        s = strong_from_cb(w->cb);
    UNLOCK();
    return s;
}

int neverc_weak_ref_equal(const neverc_weak_ref_t *a, const neverc_weak_ref_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return a->cb == b->cb && a->epoch == b->epoch;
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
    int n = 0;
    if (!w) return 0;
    LOCK();
    if (ctrl_matches(w->cb, w->epoch))
        n = NEVERC_ATOMIC_LOAD32(&w->cb->weak);
    UNLOCK();
    return n;
}
