#include "neverc/std/weak.h"
#include "neverc/std/_platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ctrl_block {
    volatile int32_t strong;
    volatile int32_t weak;
    void *data;
    void (*free_fn)(void *);
    int   owns_data;
    uintptr_t token;
    struct ctrl_block *next_active;
} ctrl_block_t;

static ctrl_block_t *g_active = NULL;
static uintptr_t g_next_token = 1;
static int g_tokens_exhausted = 0;

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

static uintptr_t next_token_locked(void) {
    uintptr_t token;
    if (g_tokens_exhausted) return 0;
    token = g_next_token;
    if (g_next_token == UINTPTR_MAX)
        g_tokens_exhausted = 1;
    else
        g_next_token++;
    return token;
}

static int ctrl_matches(const ctrl_block_t *cb, uintptr_t token) {
    return cb && token != 0 && cb->token == token;
}

static ctrl_block_t *ctrl_for_strong_locked(neverc_weak_strong_t s) {
    uintptr_t token = (uintptr_t)s._ctrl;
    if (token == 0) return NULL;
    for (ctrl_block_t *cb = g_active; cb; cb = cb->next_active) {
        if (cb->token == token)
            return cb->data == s.ptr ? cb : NULL;
    }
    return NULL;
}

static void ctrl_activate_locked(ctrl_block_t *cb) {
    cb->next_active = g_active;
    g_active = cb;
}

static void ctrl_deactivate_locked(ctrl_block_t *cb) {
    ctrl_block_t **link = &g_active;
    while (*link && *link != cb)
        link = &(*link)->next_active;
    if (*link == cb)
        *link = cb->next_active;
}

static void ctrl_retire_locked(ctrl_block_t *cb) {
    ctrl_deactivate_locked(cb);
    free(cb);
}

static void ctrl_publish(ctrl_block_t *cb, void *data, void (*free_fn)(void *),
                         int owns, uintptr_t token) {
    memset(cb, 0, sizeof(*cb));
    cb->token = token;
    cb->strong = 1;
    cb->weak = 1;
    cb->data = data;
    cb->free_fn = free_fn;
    cb->owns_data = owns;
    ctrl_activate_locked(cb);
}

static ctrl_block_t *ctrl_new(void *data, void (*free_fn)(void *), int owns) {
    ctrl_block_t *cb = (ctrl_block_t *)calloc(1, sizeof(*cb));
    uintptr_t token;
    if (!cb) return NULL;
    LOCK();
    token = next_token_locked();
    if (token == 0) {
        UNLOCK();
        free(cb);
        return NULL;
    }
    ctrl_publish(cb, data, free_fn, owns, token);
    UNLOCK();
    return cb;
}

static neverc_weak_strong_t strong_from_cb(ctrl_block_t *cb) {
    neverc_weak_strong_t s = {0};
    if (!cb) return s;
    s.ptr = cb->data;
    s._ctrl = (void *)cb->token;
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
    LOCK();
    ctrl_block_t *cb = ctrl_for_strong_locked(s);
    if (cb && retain_count(&cb->strong))
        retained = s;
    UNLOCK();
    return retained;
}

void neverc_weak_strong_release(neverc_weak_strong_t *s) {
    if (!s || !s->_ctrl) return;
    neverc_weak_strong_t value = *s;
    s->ptr = NULL;
    s->_ctrl = NULL;

    void *data = NULL;
    void (*free_fn)(void *) = NULL;
    int owns = 0;
    int drop_payload = 0;

    LOCK();
    ctrl_block_t *cb = ctrl_for_strong_locked(value);
    if (cb) {
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
    uintptr_t token;
    volatile int32_t refs;
};

neverc_weak_ref_t *neverc_weak_make(neverc_weak_strong_t s) {
    neverc_weak_ref_t *w = (neverc_weak_ref_t *)malloc(sizeof(*w));
    if (!w) return NULL;
    LOCK();
    ctrl_block_t *cb = ctrl_for_strong_locked(s);
    if (!cb ||
        NEVERC_ATOMIC_LOAD32(&cb->strong) <= 0 ||
        !retain_count(&cb->weak)) {
        UNLOCK();
        free(w);
        return NULL;
    }
    w->cb = cb;
    w->token = cb->token;
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
    if (!ctrl_matches(w->cb, w->token) ||
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
    uintptr_t token;
    int32_t refs;
    if (!w) return;
    LOCK();
    if (!w->cb) {
        UNLOCK();
        return;
    }
    cb = w->cb;
    token = w->token;
    refs = release_count(&w->refs);
    if (refs < 0) {
        UNLOCK();
        return;
    }
    if (refs == 0) {
        w->cb = NULL;
        w->token = 0;
    }
    if (ctrl_matches(cb, token) && release_count(&cb->weak) == 0)
        ctrl_retire_locked(cb);
    UNLOCK();
    if (refs == 0) free(w);
}

void *neverc_weak_value(neverc_weak_ref_t *w) {
    void *data = NULL;
    if (!w) return NULL;
    LOCK();
    if (ctrl_matches(w->cb, w->token) &&
        NEVERC_ATOMIC_LOAD32(&w->cb->strong) > 0)
        data = w->cb->data;
    UNLOCK();
    return data;
}

neverc_weak_strong_t neverc_weak_upgrade(neverc_weak_ref_t *w) {
    neverc_weak_strong_t s = {0};
    if (!w) return s;
    LOCK();
    if (ctrl_matches(w->cb, w->token) && retain_count(&w->cb->strong))
        s = strong_from_cb(w->cb);
    UNLOCK();
    return s;
}

int neverc_weak_ref_equal(const neverc_weak_ref_t *a, const neverc_weak_ref_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return a->cb == b->cb && a->token == b->token;
}

int neverc_weak_strong_count(neverc_weak_strong_t s) {
    int n = 0;
    LOCK();
    ctrl_block_t *cb = ctrl_for_strong_locked(s);
    if (cb)
        n = NEVERC_ATOMIC_LOAD32(&cb->strong);
    UNLOCK();
    return n;
}

int neverc_weak_ref_count(neverc_weak_ref_t *w) {
    int n = 0;
    if (!w) return 0;
    LOCK();
    if (ctrl_matches(w->cb, w->token))
        n = NEVERC_ATOMIC_LOAD32(&w->cb->weak);
    UNLOCK();
    return n;
}
