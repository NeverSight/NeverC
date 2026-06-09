#include "neverc/std/sync.h"
#include "neverc/std/_platform.h"

#if defined(NEVERC_PLATFORM_WINDOWS)

void neverc_mutex_init(neverc_mutex_t *m) { InitializeSRWLock(&m->srw); }
void neverc_mutex_destroy(neverc_mutex_t *m) { (void)m; }
void neverc_mutex_lock(neverc_mutex_t *m) { AcquireSRWLockExclusive(&m->srw); }
void neverc_mutex_unlock(neverc_mutex_t *m) { ReleaseSRWLockExclusive(&m->srw); }
int  neverc_mutex_trylock(neverc_mutex_t *m) { return TryAcquireSRWLockExclusive(&m->srw); }

void neverc_rwmutex_init(neverc_rwmutex_t *rw) { InitializeSRWLock(&rw->rw); }
void neverc_rwmutex_destroy(neverc_rwmutex_t *rw) { (void)rw; }
void neverc_rwmutex_rlock(neverc_rwmutex_t *rw) { AcquireSRWLockShared(&rw->rw); }
int  neverc_rwmutex_tryrlock(neverc_rwmutex_t *rw) { return TryAcquireSRWLockShared(&rw->rw); }
void neverc_rwmutex_runlock(neverc_rwmutex_t *rw) { ReleaseSRWLockShared(&rw->rw); }
void neverc_rwmutex_lock(neverc_rwmutex_t *rw) { AcquireSRWLockExclusive(&rw->rw); }
int  neverc_rwmutex_trylock(neverc_rwmutex_t *rw) { return TryAcquireSRWLockExclusive(&rw->rw); }
void neverc_rwmutex_unlock(neverc_rwmutex_t *rw) { ReleaseSRWLockExclusive(&rw->rw); }

void neverc_waitgroup_init(neverc_waitgroup_t *wg) {
    wg->counter = 0; wg->target = 0;
    InitializeCriticalSection(&wg->mu); InitializeConditionVariable(&wg->cond);
}
void neverc_waitgroup_destroy(neverc_waitgroup_t *wg) { DeleteCriticalSection(&wg->mu); }
void neverc_waitgroup_add(neverc_waitgroup_t *wg, int delta) {
    EnterCriticalSection(&wg->mu);
    wg->counter += delta;
    if (wg->counter <= 0) WakeAllConditionVariable(&wg->cond);
    LeaveCriticalSection(&wg->mu);
}
void neverc_waitgroup_done(neverc_waitgroup_t *wg) { neverc_waitgroup_add(wg, -1); }
void neverc_waitgroup_wait(neverc_waitgroup_t *wg) {
    EnterCriticalSection(&wg->mu);
    while (wg->counter > 0) SleepConditionVariableCS(&wg->cond, &wg->mu, INFINITE);
    LeaveCriticalSection(&wg->mu);
}

void neverc_once_init(neverc_once_t *o) { o->done = 0; InitializeCriticalSection(&o->mu); }
void neverc_once_destroy(neverc_once_t *o) { DeleteCriticalSection(&o->mu); }
void neverc_once_do(neverc_once_t *o, void (*f)(void)) {
    if (InterlockedCompareExchange((volatile long*)&o->done, 0, 0)) return;
    EnterCriticalSection(&o->mu);
    if (!o->done) { f(); InterlockedExchange((volatile long*)&o->done, 1); }
    LeaveCriticalSection(&o->mu);
}

void neverc_cond_init(neverc_cond_t *c, neverc_mutex_t *m) {
    InitializeConditionVariable(&c->cond); c->srw = &m->srw;
}
void neverc_cond_destroy(neverc_cond_t *c) { (void)c; }
void neverc_cond_wait(neverc_cond_t *c) { SleepConditionVariableSRW(&c->cond, c->srw, INFINITE, 0); }
void neverc_cond_signal(neverc_cond_t *c) { WakeConditionVariable(&c->cond); }
void neverc_cond_broadcast(neverc_cond_t *c) { WakeAllConditionVariable(&c->cond); }

#else /* POSIX */

void neverc_mutex_init(neverc_mutex_t *m) {
    pthread_mutex_init(&m->mu, NULL);
}
void neverc_mutex_destroy(neverc_mutex_t *m) {
    pthread_mutex_destroy(&m->mu);
}
void neverc_mutex_lock(neverc_mutex_t *m) {
    pthread_mutex_lock(&m->mu);
}
void neverc_mutex_unlock(neverc_mutex_t *m) {
    pthread_mutex_unlock(&m->mu);
}
int neverc_mutex_trylock(neverc_mutex_t *m) {
    return pthread_mutex_trylock(&m->mu) == 0;
}

void neverc_rwmutex_init(neverc_rwmutex_t *rw) {
    pthread_rwlock_init(&rw->rw, NULL);
}
void neverc_rwmutex_destroy(neverc_rwmutex_t *rw) {
    pthread_rwlock_destroy(&rw->rw);
}
void neverc_rwmutex_rlock(neverc_rwmutex_t *rw) {
    pthread_rwlock_rdlock(&rw->rw);
}
int neverc_rwmutex_tryrlock(neverc_rwmutex_t *rw) {
    return pthread_rwlock_tryrdlock(&rw->rw) == 0;
}
void neverc_rwmutex_runlock(neverc_rwmutex_t *rw) {
    pthread_rwlock_unlock(&rw->rw);
}
void neverc_rwmutex_lock(neverc_rwmutex_t *rw) {
    pthread_rwlock_wrlock(&rw->rw);
}
int neverc_rwmutex_trylock(neverc_rwmutex_t *rw) {
    return pthread_rwlock_trywrlock(&rw->rw) == 0;
}
void neverc_rwmutex_unlock(neverc_rwmutex_t *rw) {
    pthread_rwlock_unlock(&rw->rw);
}

void neverc_waitgroup_init(neverc_waitgroup_t *wg) {
    wg->counter = 0;
    wg->target = 0;
    pthread_mutex_init(&wg->mu, NULL);
    pthread_cond_init(&wg->cond, NULL);
}
void neverc_waitgroup_destroy(neverc_waitgroup_t *wg) {
    pthread_mutex_destroy(&wg->mu);
    pthread_cond_destroy(&wg->cond);
}
void neverc_waitgroup_add(neverc_waitgroup_t *wg, int delta) {
    pthread_mutex_lock(&wg->mu);
    wg->counter += delta;
    if (wg->counter <= 0)
        pthread_cond_broadcast(&wg->cond);
    pthread_mutex_unlock(&wg->mu);
}
void neverc_waitgroup_done(neverc_waitgroup_t *wg) {
    neverc_waitgroup_add(wg, -1);
}
void neverc_waitgroup_wait(neverc_waitgroup_t *wg) {
    pthread_mutex_lock(&wg->mu);
    while (wg->counter > 0)
        pthread_cond_wait(&wg->cond, &wg->mu);
    pthread_mutex_unlock(&wg->mu);
}

void neverc_once_init(neverc_once_t *o) {
    o->done = 0;
    pthread_mutex_init(&o->mu, NULL);
}
void neverc_once_destroy(neverc_once_t *o) {
    pthread_mutex_destroy(&o->mu);
}
void neverc_once_do(neverc_once_t *o, void (*f)(void)) {
    if (NEVERC_ATOMIC_LOAD32(&o->done))
        return;
    pthread_mutex_lock(&o->mu);
    if (!o->done) {
        f();
        NEVERC_ATOMIC_STORE32(&o->done, 1);
    }
    pthread_mutex_unlock(&o->mu);
}

void neverc_cond_init(neverc_cond_t *c, neverc_mutex_t *m) {
    pthread_cond_init(&c->cond, NULL);
    c->mu = &m->mu;
}
void neverc_cond_destroy(neverc_cond_t *c) {
    pthread_cond_destroy(&c->cond);
}
void neverc_cond_wait(neverc_cond_t *c) {
    pthread_cond_wait(&c->cond, c->mu);
}
void neverc_cond_signal(neverc_cond_t *c) {
    pthread_cond_signal(&c->cond);
}
void neverc_cond_broadcast(neverc_cond_t *c) {
    pthread_cond_broadcast(&c->cond);
}

#endif /* NEVERC_PLATFORM_WINDOWS */

/* ================================================================
 * sync.Pool — thread-safe object pool
 * ================================================================ */
#include <stdlib.h>
#include <string.h>

#define POOL_CAP 256

struct neverc_sync_pool {
    void *(*new_func)(void);
    void *items[POOL_CAP];
    int count;
    neverc_mutex_t mu;
};

neverc_sync_pool_t *neverc_sync_pool_new(void *(*new_func)(void)) {
    neverc_sync_pool_t *p = (neverc_sync_pool_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->new_func = new_func;
    p->count = 0;
    neverc_mutex_init(&p->mu);
    return p;
}

void neverc_sync_pool_free(neverc_sync_pool_t *p) {
    if (!p) return;
    neverc_mutex_destroy(&p->mu);
    free(p);
}

void neverc_sync_pool_put(neverc_sync_pool_t *p, void *x) {
    if (!p || !x) return;
    neverc_mutex_lock(&p->mu);
    if (p->count < POOL_CAP)
        p->items[p->count++] = x;
    neverc_mutex_unlock(&p->mu);
}

void *neverc_sync_pool_get(neverc_sync_pool_t *p) {
    if (!p) return NULL;
    neverc_mutex_lock(&p->mu);
    void *x = NULL;
    if (p->count > 0)
        x = p->items[--p->count];
    neverc_mutex_unlock(&p->mu);
    if (!x && p->new_func)
        x = p->new_func();
    return x;
}

/* ================================================================
 * sync.Map — concurrent hash map (RWLock-protected)
 * ================================================================ */

#define SMAP_INIT_CAP 16
#define SMAP_LOAD_FACTOR 0.75

typedef struct smap_entry {
    char *key;
    void *value;
    int   occupied;  /* 0 = empty, 1 = occupied, 2 = tombstone */
} smap_entry_t;

#define SMAP_EMPTY     0
#define SMAP_OCCUPIED  1
#define SMAP_TOMBSTONE 2

struct neverc_sync_map {
    smap_entry_t *buckets;
    size_t cap;
    size_t count;
    neverc_rwmutex_t rw;
};

static uint64_t smap_hash(const char *key) {
    uint64_t h = 14695981039346656037ULL;
    for (const char *p = key; *p; p++) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ULL;
    }
    return h;
}

neverc_sync_map_t *neverc_sync_map_new(void) {
    neverc_sync_map_t *m = (neverc_sync_map_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->cap = SMAP_INIT_CAP;
    m->buckets = (smap_entry_t *)calloc(m->cap, sizeof(smap_entry_t));
    if (!m->buckets) { free(m); return NULL; }
    m->count = 0;
    neverc_rwmutex_init(&m->rw);
    return m;
}

void neverc_sync_map_free(neverc_sync_map_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++)
        if (m->buckets[i].occupied == SMAP_OCCUPIED) free(m->buckets[i].key);
    free(m->buckets);
    neverc_rwmutex_destroy(&m->rw);
    free(m);
}

static smap_entry_t *smap_find_slot(smap_entry_t *buckets, size_t cap, const char *key) {
    uint64_t h = smap_hash(key);
    size_t idx = (size_t)(h % cap);
    smap_entry_t *first_tomb = NULL;
    for (size_t i = 0; i < cap; i++) {
        size_t slot = (idx + i) % cap;
        if (buckets[slot].occupied == SMAP_EMPTY) {
            return first_tomb ? first_tomb : &buckets[slot];
        }
        if (buckets[slot].occupied == SMAP_TOMBSTONE) {
            if (!first_tomb) first_tomb = &buckets[slot];
            continue;
        }
        if (strcmp(buckets[slot].key, key) == 0) return &buckets[slot];
    }
    return first_tomb;
}

static int smap_grow(neverc_sync_map_t *m) {
    size_t new_cap = m->cap * 2;
    smap_entry_t *new_buckets = (smap_entry_t *)calloc(new_cap, sizeof(smap_entry_t));
    if (!new_buckets) return -1;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->buckets[i].occupied == SMAP_OCCUPIED) {
            smap_entry_t *slot = smap_find_slot(new_buckets, new_cap, m->buckets[i].key);
            *slot = m->buckets[i];
        }
    }
    free(m->buckets);
    m->buckets = new_buckets;
    m->cap = new_cap;
    return 0;
}

void neverc_sync_map_store(neverc_sync_map_t *m, const char *key, void *value) {
    neverc_rwmutex_lock(&m->rw);
    if ((double)(m->count + 1) / (double)m->cap > SMAP_LOAD_FACTOR) {
        if (smap_grow(m) < 0) {
            neverc_rwmutex_unlock(&m->rw);
            return;
        }
    }
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    if (slot) {
        if (slot->occupied == SMAP_OCCUPIED) {
            slot->value = value;
        } else {
            size_t klen = strlen(key);
            slot->key = (char *)malloc(klen + 1);
            memcpy(slot->key, key, klen + 1);
            slot->value = value;
            slot->occupied = SMAP_OCCUPIED;
            m->count++;
        }
    }
    neverc_rwmutex_unlock(&m->rw);
}

void *neverc_sync_map_load(neverc_sync_map_t *m, const char *key, int *ok) {
    neverc_rwmutex_rlock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    void *val = NULL;
    int found = 0;
    if (slot && slot->occupied == SMAP_OCCUPIED) { val = slot->value; found = 1; }
    neverc_rwmutex_runlock(&m->rw);
    if (ok) *ok = found;
    return val;
}

void *neverc_sync_map_load_or_store(neverc_sync_map_t *m, const char *key, void *value, int *loaded) {
    neverc_rwmutex_lock(&m->rw);
    if ((double)(m->count + 1) / (double)m->cap > SMAP_LOAD_FACTOR)
        smap_grow(m);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    void *actual = value;
    int was_loaded = 0;
    if (slot) {
        if (slot->occupied == SMAP_OCCUPIED) {
            actual = slot->value;
            was_loaded = 1;
        } else {
            size_t klen = strlen(key);
            slot->key = (char *)malloc(klen + 1);
            memcpy(slot->key, key, klen + 1);
            slot->value = value;
            slot->occupied = SMAP_OCCUPIED;
            m->count++;
        }
    }
    neverc_rwmutex_unlock(&m->rw);
    if (loaded) *loaded = was_loaded;
    return actual;
}

void *neverc_sync_map_load_and_delete(neverc_sync_map_t *m, const char *key, int *loaded) {
    neverc_rwmutex_lock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    void *val = NULL;
    int found = 0;
    if (slot && slot->occupied == SMAP_OCCUPIED) {
        val = slot->value;
        found = 1;
        free(slot->key);
        slot->key = NULL;
        slot->value = NULL;
        slot->occupied = SMAP_TOMBSTONE;
        m->count--;
    }
    neverc_rwmutex_unlock(&m->rw);
    if (loaded) *loaded = found;
    return val;
}

void neverc_sync_map_delete(neverc_sync_map_t *m, const char *key) {
    neverc_sync_map_load_and_delete(m, key, NULL);
}

void neverc_sync_map_clear(neverc_sync_map_t *m) {
    neverc_rwmutex_lock(&m->rw);
    for (size_t i = 0; i < m->cap; i++) {
        if (m->buckets[i].occupied == SMAP_OCCUPIED)
            free(m->buckets[i].key);
        m->buckets[i].key = NULL;
        m->buckets[i].value = NULL;
        m->buckets[i].occupied = SMAP_EMPTY;
    }
    m->count = 0;
    neverc_rwmutex_unlock(&m->rw);
}

void neverc_sync_map_range(neverc_sync_map_t *m, int (*f)(const char *key, void *value, void *user), void *user) {
    neverc_rwmutex_rlock(&m->rw);
    for (size_t i = 0; i < m->cap; i++) {
        if (m->buckets[i].occupied == SMAP_OCCUPIED) {
            if (!f(m->buckets[i].key, m->buckets[i].value, user))
                break;
        }
    }
    neverc_rwmutex_runlock(&m->rw);
}

void *neverc_sync_map_swap(neverc_sync_map_t *m, const char *key, void *value, int *loaded) {
    neverc_rwmutex_lock(&m->rw);
    if ((double)(m->count + 1) / (double)m->cap > SMAP_LOAD_FACTOR)
        smap_grow(m);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    void *previous = NULL;
    int was_loaded = 0;
    if (slot) {
        if (slot->occupied == SMAP_OCCUPIED) {
            previous = slot->value;
            slot->value = value;
            was_loaded = 1;
        } else {
            size_t klen = strlen(key);
            slot->key = (char *)malloc(klen + 1);
            memcpy(slot->key, key, klen + 1);
            slot->value = value;
            slot->occupied = SMAP_OCCUPIED;
            m->count++;
        }
    }
    neverc_rwmutex_unlock(&m->rw);
    if (loaded) *loaded = was_loaded;
    return previous;
}

int neverc_sync_map_compare_and_swap(neverc_sync_map_t *m, const char *key, void *old_val, void *new_val) {
    neverc_rwmutex_lock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    int swapped = 0;
    if (slot && slot->occupied == SMAP_OCCUPIED && slot->value == old_val) {
        slot->value = new_val;
        swapped = 1;
    }
    neverc_rwmutex_unlock(&m->rw);
    return swapped;
}

int neverc_sync_map_compare_and_delete(neverc_sync_map_t *m, const char *key, void *old_val) {
    neverc_rwmutex_lock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    int deleted = 0;
    if (slot && slot->occupied == SMAP_OCCUPIED && slot->value == old_val) {
        free(slot->key);
        slot->key = NULL;
        slot->value = NULL;
        slot->occupied = SMAP_TOMBSTONE;
        m->count--;
        deleted = 1;
    }
    neverc_rwmutex_unlock(&m->rw);
    return deleted;
}
