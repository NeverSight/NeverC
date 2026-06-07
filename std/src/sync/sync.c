#include "neverc/sync.h"
#include "neverc/_platform.h"

#if defined(NEVERC_PLATFORM_WINDOWS)

void neverc_mutex_init(neverc_mutex_t *m) { InitializeCriticalSection(&m->cs); }
void neverc_mutex_destroy(neverc_mutex_t *m) { DeleteCriticalSection(&m->cs); }
void neverc_mutex_lock(neverc_mutex_t *m) { EnterCriticalSection(&m->cs); }
void neverc_mutex_unlock(neverc_mutex_t *m) { LeaveCriticalSection(&m->cs); }
int  neverc_mutex_trylock(neverc_mutex_t *m) { return TryEnterCriticalSection(&m->cs); }

void neverc_rwmutex_init(neverc_rwmutex_t *rw) { InitializeSRWLock(&rw->rw); }
void neverc_rwmutex_destroy(neverc_rwmutex_t *rw) { (void)rw; }
void neverc_rwmutex_rlock(neverc_rwmutex_t *rw) { AcquireSRWLockShared(&rw->rw); }
void neverc_rwmutex_runlock(neverc_rwmutex_t *rw) { ReleaseSRWLockShared(&rw->rw); }
void neverc_rwmutex_lock(neverc_rwmutex_t *rw) { AcquireSRWLockExclusive(&rw->rw); }
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
    InitializeConditionVariable(&c->cond); c->cs = &m->cs;
}
void neverc_cond_destroy(neverc_cond_t *c) { (void)c; }
void neverc_cond_wait(neverc_cond_t *c) { SleepConditionVariableCS(&c->cond, c->cs, INFINITE); }
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
void neverc_rwmutex_runlock(neverc_rwmutex_t *rw) {
    pthread_rwlock_unlock(&rw->rw);
}
void neverc_rwmutex_lock(neverc_rwmutex_t *rw) {
    pthread_rwlock_wrlock(&rw->rw);
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
    if (__atomic_load_n(&o->done, __ATOMIC_ACQUIRE))
        return;
    pthread_mutex_lock(&o->mu);
    if (!o->done) {
        f();
        __atomic_store_n(&o->done, 1, __ATOMIC_RELEASE);
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
