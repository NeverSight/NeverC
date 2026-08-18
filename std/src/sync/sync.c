#include "neverc/std/sync.h"
#include "neverc/std/_platform.h"

#ifndef NEVERC_SYNC_INIT_SHOULD_FAIL
#define NEVERC_SYNC_INIT_SHOULD_FAIL() 0
#endif
#ifndef NEVERC_SYNC_LOCK_SHOULD_FAIL
#define NEVERC_SYNC_LOCK_SHOULD_FAIL() 0
#endif

#if defined(NEVERC_PLATFORM_WINDOWS)

int neverc_mutex_init(neverc_mutex_t *m) {
    if (!m || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    InitializeSRWLock(&m->srw);
    m->owner = 0;
    return 0;
}
void neverc_mutex_destroy(neverc_mutex_t *m) { (void)m; }
void neverc_mutex_lock(neverc_mutex_t *m) {
    AcquireSRWLockExclusive(&m->srw);
    m->owner = GetCurrentThreadId();
}
void neverc_mutex_unlock(neverc_mutex_t *m) {
    if (!m || m->owner != GetCurrentThreadId())
        return;
    m->owner = 0;
    ReleaseSRWLockExclusive(&m->srw);
}
int neverc_mutex_trylock(neverc_mutex_t *m) {
    if (!TryAcquireSRWLockExclusive(&m->srw))
        return 0;
    m->owner = GetCurrentThreadId();
    return 1;
}

int neverc_rwmutex_init(neverc_rwmutex_t *rw) {
    if (!rw || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    InitializeSRWLock(&rw->rw);
    return 0;
}
void neverc_rwmutex_destroy(neverc_rwmutex_t *rw) { (void)rw; }
void neverc_rwmutex_rlock(neverc_rwmutex_t *rw) { AcquireSRWLockShared(&rw->rw); }
int  neverc_rwmutex_tryrlock(neverc_rwmutex_t *rw) { return TryAcquireSRWLockShared(&rw->rw); }
void neverc_rwmutex_runlock(neverc_rwmutex_t *rw) { ReleaseSRWLockShared(&rw->rw); }
void neverc_rwmutex_lock(neverc_rwmutex_t *rw) { AcquireSRWLockExclusive(&rw->rw); }
int  neverc_rwmutex_trylock(neverc_rwmutex_t *rw) { return TryAcquireSRWLockExclusive(&rw->rw); }
void neverc_rwmutex_unlock(neverc_rwmutex_t *rw) { ReleaseSRWLockExclusive(&rw->rw); }

int neverc_waitgroup_init(neverc_waitgroup_t *wg) {
    if (!wg || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    wg->counter = 0;
    wg->target = 0;
    if (!InitializeCriticalSectionAndSpinCount(&wg->mu, 4000))
        return -1;
    InitializeConditionVariable(&wg->cond);
    return 0;
}
void neverc_waitgroup_destroy(neverc_waitgroup_t *wg) { DeleteCriticalSection(&wg->mu); }
int neverc_waitgroup_add_checked(neverc_waitgroup_t *wg, int delta) {
    if (!wg) return -1;
    EnterCriticalSection(&wg->mu);
    int64_t next = (int64_t)wg->counter + (int64_t)delta;
    int result = 0;
    if (next < 0 || next > INT32_MAX) {
        result = -1;
    } else {
        wg->counter = (int32_t)next;
        if (wg->counter == 0) WakeAllConditionVariable(&wg->cond);
    }
    LeaveCriticalSection(&wg->mu);
    return result;
}
void neverc_waitgroup_add(neverc_waitgroup_t *wg, int delta) {
    (void)neverc_waitgroup_add_checked(wg, delta);
}
int neverc_waitgroup_done_checked(neverc_waitgroup_t *wg) {
    return neverc_waitgroup_add_checked(wg, -1);
}
void neverc_waitgroup_done(neverc_waitgroup_t *wg) {
    (void)neverc_waitgroup_done_checked(wg);
}
void neverc_waitgroup_wait(neverc_waitgroup_t *wg) {
    if (!wg) return;
    EnterCriticalSection(&wg->mu);
    while (wg->counter > 0) SleepConditionVariableCS(&wg->cond, &wg->mu, INFINITE);
    LeaveCriticalSection(&wg->mu);
}

int neverc_once_init(neverc_once_t *o) {
    if (!o || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    o->done = 0;
    /* SRWLOCK is non-recursive, matching Go Once.Do (re-entry deadlocks).
     * CRITICAL_SECTION is recursive and would run f twice. */
    InitializeSRWLock(&o->mu);
    return 0;
}
void neverc_once_destroy(neverc_once_t *o) { (void)o; }
void neverc_once_do(neverc_once_t *o, void (*f)(void)) {
    if (!o || !f) return;
    if (InterlockedCompareExchange((volatile long*)&o->done, 0, 0)) return;
    AcquireSRWLockExclusive(&o->mu);
    if (!InterlockedCompareExchange((volatile long*)&o->done, 0, 0)) {
        f();
        InterlockedExchange((volatile long*)&o->done, 1);
    }
    ReleaseSRWLockExclusive(&o->mu);
}

int neverc_cond_init(neverc_cond_t *c, neverc_mutex_t *m) {
    if (!c || !m || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    InitializeConditionVariable(&c->cond);
    c->m = m;
    return 0;
}
void neverc_cond_destroy(neverc_cond_t *c) { (void)c; }
void neverc_cond_wait(neverc_cond_t *c) {
    DWORD self = c->m->owner;
    c->m->owner = 0;
    SleepConditionVariableSRW(&c->cond, &c->m->srw, INFINITE, 0);
    c->m->owner = self;
}
void neverc_cond_signal(neverc_cond_t *c) { WakeConditionVariable(&c->cond); }
void neverc_cond_broadcast(neverc_cond_t *c) { WakeAllConditionVariable(&c->cond); }

#else /* POSIX */

#include <errno.h>

static int sync_posix_mutex_lock(pthread_mutex_t *mu) {
    for (;;) {
        /* A failed lock is not "initialization ran" / "wait completed". */
        if (NEVERC_SYNC_LOCK_SHOULD_FAIL())
            continue;
        int rc = pthread_mutex_lock(mu);
        if (rc == 0)
            return 0;
        if (rc == EDEADLK)
            return -1;
    }
}

int neverc_mutex_init(neverc_mutex_t *m) {
    if (!m || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) == 0) {
        (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        if (pthread_mutex_init(&m->mu, &attr) == 0) {
            pthread_mutexattr_destroy(&attr);
            return 0;
        }
        pthread_mutexattr_destroy(&attr);
    }
    return pthread_mutex_init(&m->mu, NULL) == 0 ? 0 : -1;
}
void neverc_mutex_destroy(neverc_mutex_t *m) {
    pthread_mutex_destroy(&m->mu);
}
void neverc_mutex_lock(neverc_mutex_t *m) {
    /* ERRORCHECK mutexes return EDEADLK on recursive lock. Ignoring that
     * would fail-open (the caller proceeds without holding the mutex).
     * Looping matches Go Mutex.Lock, which deadlocks on re-entry. */
    for (;;) {
        int rc = pthread_mutex_lock(&m->mu);
        if (rc == 0)
            return;
    }
}
void neverc_mutex_unlock(neverc_mutex_t *m) {
    if (!m)
        return;
    /* ERRORCHECK mutex: unlocking an unheld mutex returns EPERM instead of UB. */
    (void)pthread_mutex_unlock(&m->mu);
}
int neverc_mutex_trylock(neverc_mutex_t *m) {
    return pthread_mutex_trylock(&m->mu) == 0;
}

int neverc_rwmutex_init(neverc_rwmutex_t *rw) {
    if (!rw || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    return pthread_rwlock_init(&rw->rw, NULL) == 0 ? 0 : -1;
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

int neverc_waitgroup_init(neverc_waitgroup_t *wg) {
    if (!wg || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    wg->counter = 0;
    wg->target = 0;
    if (pthread_mutex_init(&wg->mu, NULL) != 0)
        return -1;
    if (pthread_cond_init(&wg->cond, NULL) != 0) {
        pthread_mutex_destroy(&wg->mu);
        return -1;
    }
    return 0;
}
void neverc_waitgroup_destroy(neverc_waitgroup_t *wg) {
    pthread_mutex_destroy(&wg->mu);
    pthread_cond_destroy(&wg->cond);
}
int neverc_waitgroup_add_checked(neverc_waitgroup_t *wg, int delta) {
    if (!wg) return -1;
    if (pthread_mutex_lock(&wg->mu) != 0)
        return -1;
    int64_t next = (int64_t)wg->counter + (int64_t)delta;
    int result = 0;
    if (next < 0 || next > INT32_MAX) {
        result = -1;
    } else {
        wg->counter = (int32_t)next;
        if (wg->counter == 0)
            pthread_cond_broadcast(&wg->cond);
    }
    pthread_mutex_unlock(&wg->mu);
    return result;
}
void neverc_waitgroup_add(neverc_waitgroup_t *wg, int delta) {
    (void)neverc_waitgroup_add_checked(wg, delta);
}
int neverc_waitgroup_done_checked(neverc_waitgroup_t *wg) {
    return neverc_waitgroup_add_checked(wg, -1);
}
void neverc_waitgroup_done(neverc_waitgroup_t *wg) {
    (void)neverc_waitgroup_done_checked(wg);
}
void neverc_waitgroup_wait(neverc_waitgroup_t *wg) {
    if (!wg) return;
    /* Do not return on lock failure: that would fail-open (waiters proceed
     * as if every Add had been matched by Done). */
    while (sync_posix_mutex_lock(&wg->mu) != 0)
        continue;
    while (wg->counter > 0)
        pthread_cond_wait(&wg->cond, &wg->mu);
    pthread_mutex_unlock(&wg->mu);
}

int neverc_once_init(neverc_once_t *o) {
    if (!o || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    o->done = 0;
    return pthread_mutex_init(&o->mu, NULL) == 0 ? 0 : -1;
}
void neverc_once_destroy(neverc_once_t *o) {
    pthread_mutex_destroy(&o->mu);
}
void neverc_once_do(neverc_once_t *o, void (*f)(void)) {
    if (!o || !f)
        return;
    if (NEVERC_ATOMIC_LOAD32(&o->done))
        return;
    /* Do not mark done, run f(), or return without the lock: that would
     * fail-open (callers skip initialization that never ran under exclusion). */
    if (sync_posix_mutex_lock(&o->mu) != 0)
        return;
    if (!NEVERC_ATOMIC_LOAD32(&o->done)) {
        f();
        NEVERC_ATOMIC_STORE32(&o->done, 1);
    }
    pthread_mutex_unlock(&o->mu);
}

int neverc_cond_init(neverc_cond_t *c, neverc_mutex_t *m) {
    if (!c || !m || NEVERC_SYNC_INIT_SHOULD_FAIL())
        return -1;
    if (pthread_cond_init(&c->cond, NULL) != 0)
        return -1;
    c->mu = &m->mu;
    return 0;
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

#ifndef NC_SYNC_MALLOC
#define NC_SYNC_MALLOC malloc
#endif

#ifndef NC_SYNC_CALLOC
#define NC_SYNC_CALLOC calloc
#endif

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
    if (neverc_mutex_init(&p->mu) != 0) {
        free(p);
        return NULL;
    }
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

/*
 * wyhash (final v3) for sync.Map keys — same family as maps.c / unique.c /
 * hash/maphash. Hash only picks the probe start; strcmp re-verifies every hit,
 * so swapping FNV-1a for wyhash cannot change observable map behavior.
 * memcpy reads and the nci_wymix portable 64x64 fallback match maps.c.
 */
static inline uint64_t smap_read8(const uint8_t *p) {
    uint64_t v; memcpy(&v, p, 8); return v;
}
static inline uint64_t smap_read4(const uint8_t *p) {
    uint32_t v; memcpy(&v, p, 4); return (uint64_t)v;
}
static inline uint64_t smap_wymix(uint64_t a, uint64_t b) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)a * b;
    return (uint64_t)r ^ (uint64_t)(r >> 64);
#else
    uint64_t ha = a >> 32, la = (uint32_t)a;
    uint64_t hb = b >> 32, lb = (uint32_t)b;
    uint64_t rh = ha * hb, rl = la * lb;
    uint64_t rm0 = ha * lb, rm1 = hb * la;
    uint64_t t = rl + (rm0 << 32), c = (t < rl);
    uint64_t lo = t + (rm1 << 32); c += (lo < t);
    return lo ^ (rh + (rm0 >> 32) + (rm1 >> 32) + c);
#endif
}

#define SMAP_WY_S0 0xa0761d6478bd642fULL
#define SMAP_WY_S1 0xe7037ed1a0b428dbULL
#define SMAP_WY_S2 0x8ebc6af09c88c6e3ULL

static uint64_t smap_hash(const char *key) {
    const uint8_t *p = (const uint8_t *)key;

    if (!p[0]) return SMAP_WY_S0;
    if (!p[1]) return p[0] * SMAP_WY_S1;
    if (!p[2]) return (((uint64_t)p[0] << 8) | p[1]) * SMAP_WY_S1 ^ SMAP_WY_S0;
    if (!p[3]) return (((uint64_t)p[0] << 16) | ((uint64_t)p[1] << 8) | p[2]) * SMAP_WY_S1 ^ SMAP_WY_S0;

    size_t len = 4 + strlen(key + 4);
    uint64_t seed = SMAP_WY_S0;
    uint64_t a, b;

    if (len <= 16) {
        a = (smap_read4(p) << 32) | smap_read4(p + ((len >> 3) << 2));
        b = (smap_read4(p + len - 4) << 32) | smap_read4(p + len - 4 - ((len >> 3) << 2));
    } else if (len <= 48) {
        size_t i = 0;
        for (; len - i >= 16; i += 16)
            seed = smap_wymix(smap_read8(p + i) ^ SMAP_WY_S1, smap_read8(p + i + 8) ^ seed);
        a = smap_read8(p + len - 16);
        b = smap_read8(p + len - 8);
    } else {
        uint64_t s1 = seed, s2 = seed;
        size_t i = 0;
        for (; len - i >= 48; i += 48) {
            seed = smap_wymix(smap_read8(p + i)      ^ SMAP_WY_S0, smap_read8(p + i + 8)  ^ seed);
            s1   = smap_wymix(smap_read8(p + i + 16) ^ SMAP_WY_S1, smap_read8(p + i + 24) ^ s1);
            s2   = smap_wymix(smap_read8(p + i + 32) ^ SMAP_WY_S2, smap_read8(p + i + 40) ^ s2);
        }
        seed ^= s1 ^ s2;
        for (; len - i >= 16; i += 16)
            seed = smap_wymix(smap_read8(p + i) ^ SMAP_WY_S1, smap_read8(p + i + 8) ^ seed);
        a = smap_read8(p + len - 16);
        b = smap_read8(p + len - 8);
    }
    return smap_wymix(SMAP_WY_S1 ^ len, smap_wymix(a ^ SMAP_WY_S1, b ^ seed));
}

neverc_sync_map_t *neverc_sync_map_new(void) {
    neverc_sync_map_t *m = (neverc_sync_map_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->cap = SMAP_INIT_CAP;
    m->buckets = (smap_entry_t *)calloc(m->cap, sizeof(smap_entry_t));
    if (!m->buckets) { free(m); return NULL; }
    m->count = 0;
    if (neverc_rwmutex_init(&m->rw) != 0) {
        free(m->buckets);
        free(m);
        return NULL;
    }
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
    if (m->cap > SIZE_MAX / 2)
        return -1;
    size_t new_cap = m->cap * 2;
    if (new_cap > SIZE_MAX / sizeof(smap_entry_t))
        return -1;
    smap_entry_t *new_buckets =
        (smap_entry_t *)NC_SYNC_CALLOC(new_cap, sizeof(smap_entry_t));
    if (!new_buckets) return -1;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->buckets[i].occupied == SMAP_OCCUPIED) {
            smap_entry_t *slot = smap_find_slot(new_buckets, new_cap, m->buckets[i].key);
            if (!slot) {
                free(new_buckets);
                return -1;
            }
            *slot = m->buckets[i];
        }
    }
    free(m->buckets);
    m->buckets = new_buckets;
    m->cap = new_cap;
    return 0;
}

static int smap_insert(smap_entry_t *slot, const char *key, void *value) {
    size_t klen = strlen(key);
    if (klen == SIZE_MAX)
        return -1;

    char *key_copy = (char *)NC_SYNC_MALLOC(klen + 1);
    if (!key_copy)
        return -1;
    memcpy(key_copy, key, klen + 1);

    slot->key = key_copy;
    slot->value = value;
    slot->occupied = SMAP_OCCUPIED;
    return 0;
}

int neverc_sync_map_store(neverc_sync_map_t *m, const char *key, void *value) {
    if (!m || !key)
        return -1;
    neverc_rwmutex_lock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    if (slot && slot->occupied == SMAP_OCCUPIED) {
        slot->value = value;
        neverc_rwmutex_unlock(&m->rw);
        return 0;
    }

    if (!slot || m->count >= m->cap - m->cap / 4) {
        if (smap_grow(m) < 0) {
            neverc_rwmutex_unlock(&m->rw);
            return -1;
        }
        slot = smap_find_slot(m->buckets, m->cap, key);
        if (slot && slot->occupied == SMAP_OCCUPIED) {
            slot->value = value;
            neverc_rwmutex_unlock(&m->rw);
            return 0;
        }
    }
    int stored = 0;
    if (slot && smap_insert(slot, key, value) == 0) {
        m->count++;
        stored = 1;
    }
    neverc_rwmutex_unlock(&m->rw);
    return stored ? 0 : -1;
}

void *neverc_sync_map_load(neverc_sync_map_t *m, const char *key, int *ok) {
    if (!m || !key) {
        if (ok) *ok = 0;
        return NULL;
    }
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
    if (!m || !key) {
        if (loaded) *loaded = 0;
        return NULL;
    }
    neverc_rwmutex_lock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    void *actual = NULL;
    int was_loaded = 0;
    if (slot && slot->occupied == SMAP_OCCUPIED) {
        actual = slot->value;
        was_loaded = 1;
    } else {
        if (!slot || m->count >= m->cap - m->cap / 4) {
            if (smap_grow(m) != 0) {
                neverc_rwmutex_unlock(&m->rw);
                if (loaded) *loaded = 0;
                return NULL;
            }
            slot = smap_find_slot(m->buckets, m->cap, key);
            if (slot && slot->occupied == SMAP_OCCUPIED) {
                actual = slot->value;
                was_loaded = 1;
                neverc_rwmutex_unlock(&m->rw);
                if (loaded) *loaded = was_loaded;
                return actual;
            }
        }
        if (slot && smap_insert(slot, key, value) == 0) {
            actual = value;
            m->count++;
        }
    }
    neverc_rwmutex_unlock(&m->rw);
    if (loaded) *loaded = was_loaded;
    return actual;
}

void *neverc_sync_map_load_and_delete(neverc_sync_map_t *m, const char *key, int *loaded) {
    if (!m || !key) {
        if (loaded) *loaded = 0;
        return NULL;
    }
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
    if (!m)
        return;
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
    if (!m || !f)
        return;

    typedef struct smap_range_item {
        char *key;
        void *value;
    } smap_range_item_t;

    neverc_rwmutex_rlock(&m->rw);
    if (m->count == 0 ||
        m->count > SIZE_MAX / sizeof(smap_range_item_t)) {
        neverc_rwmutex_runlock(&m->rw);
        return;
    }

    smap_range_item_t *items = (smap_range_item_t *)NC_SYNC_CALLOC(
        m->count, sizeof(*items));
    if (!items) {
        neverc_rwmutex_runlock(&m->rw);
        return;
    }

    size_t count = 0;
    int complete = 1;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->buckets[i].occupied == SMAP_OCCUPIED) {
            size_t key_len = strlen(m->buckets[i].key);
            if (key_len == SIZE_MAX) {
                complete = 0;
                break;
            }
            items[count].key = (char *)NC_SYNC_MALLOC(key_len + 1);
            if (!items[count].key) {
                complete = 0;
                break;
            }
            memcpy(items[count].key, m->buckets[i].key, key_len + 1);
            items[count].value = m->buckets[i].value;
            count++;
        }
    }
    neverc_rwmutex_runlock(&m->rw);

    if (complete) {
        for (size_t i = 0; i < count; i++) {
            if (!f(items[i].key, items[i].value, user))
                break;
        }
    }
    for (size_t i = 0; i < count; i++)
        free(items[i].key);
    free(items);
}

void *neverc_sync_map_swap(neverc_sync_map_t *m, const char *key, void *value, int *loaded) {
    if (!m || !key) {
        if (loaded) *loaded = 0;
        return NULL;
    }
    neverc_rwmutex_lock(&m->rw);
    smap_entry_t *slot = smap_find_slot(m->buckets, m->cap, key);
    void *previous = NULL;
    int was_loaded = 0;
    if (slot && slot->occupied == SMAP_OCCUPIED) {
        previous = slot->value;
        slot->value = value;
        was_loaded = 1;
    } else {
        if (!slot || m->count >= m->cap - m->cap / 4) {
            if (smap_grow(m) != 0) {
                neverc_rwmutex_unlock(&m->rw);
                if (loaded) *loaded = 0;
                return NULL;
            }
            slot = smap_find_slot(m->buckets, m->cap, key);
            if (slot && slot->occupied == SMAP_OCCUPIED) {
                previous = slot->value;
                slot->value = value;
                was_loaded = 1;
                neverc_rwmutex_unlock(&m->rw);
                if (loaded) *loaded = was_loaded;
                return previous;
            }
        }
        if (slot && smap_insert(slot, key, value) == 0)
            m->count++;
    }
    neverc_rwmutex_unlock(&m->rw);
    if (loaded) *loaded = was_loaded;
    return previous;
}

int neverc_sync_map_compare_and_swap(neverc_sync_map_t *m, const char *key, void *old_val, void *new_val) {
    if (!m || !key)
        return 0;
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
    if (!m || !key)
        return 0;
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
