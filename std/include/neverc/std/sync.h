#ifndef NEVERC_SYNC_H
#define NEVERC_SYNC_H

#include <stdint.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
typedef struct { SRWLOCK srw; } neverc_mutex_t;
#else
typedef struct { pthread_mutex_t mu; } neverc_mutex_t;
#endif

void neverc_mutex_init(neverc_mutex_t *m);
void neverc_mutex_destroy(neverc_mutex_t *m);
void neverc_mutex_lock(neverc_mutex_t *m);
void neverc_mutex_unlock(neverc_mutex_t *m);
int  neverc_mutex_trylock(neverc_mutex_t *m);

#if defined(_WIN32)
typedef struct { SRWLOCK rw; } neverc_rwmutex_t;
#else
typedef struct { pthread_rwlock_t rw; } neverc_rwmutex_t;
#endif

void neverc_rwmutex_init(neverc_rwmutex_t *rw);
void neverc_rwmutex_destroy(neverc_rwmutex_t *rw);
void neverc_rwmutex_rlock(neverc_rwmutex_t *rw);
void neverc_rwmutex_runlock(neverc_rwmutex_t *rw);
void neverc_rwmutex_lock(neverc_rwmutex_t *rw);
void neverc_rwmutex_unlock(neverc_rwmutex_t *rw);

#if defined(_WIN32)
typedef struct {
    volatile int32_t counter;
    int32_t          target;
    CRITICAL_SECTION mu;
    CONDITION_VARIABLE cond;
} neverc_waitgroup_t;
#else
typedef struct {
    volatile int32_t counter;
    int32_t          target;
    pthread_mutex_t  mu;
    pthread_cond_t   cond;
} neverc_waitgroup_t;
#endif

void neverc_waitgroup_init(neverc_waitgroup_t *wg);
void neverc_waitgroup_destroy(neverc_waitgroup_t *wg);
void neverc_waitgroup_add(neverc_waitgroup_t *wg, int delta);
void neverc_waitgroup_done(neverc_waitgroup_t *wg);
void neverc_waitgroup_wait(neverc_waitgroup_t *wg);

#if defined(_WIN32)
typedef struct { volatile int32_t done; CRITICAL_SECTION mu; } neverc_once_t;
#else
typedef struct { volatile int32_t done; pthread_mutex_t mu; } neverc_once_t;
#endif

void neverc_once_init(neverc_once_t *o);
void neverc_once_destroy(neverc_once_t *o);
void neverc_once_do(neverc_once_t *o, void (*f)(void));

#if defined(_WIN32)
typedef struct { CONDITION_VARIABLE cond; SRWLOCK *srw; } neverc_cond_t;
#else
typedef struct { pthread_cond_t cond; pthread_mutex_t *mu; } neverc_cond_t;
#endif

void neverc_cond_init(neverc_cond_t *c, neverc_mutex_t *m);
void neverc_cond_destroy(neverc_cond_t *c);
void neverc_cond_wait(neverc_cond_t *c);
void neverc_cond_signal(neverc_cond_t *c);
void neverc_cond_broadcast(neverc_cond_t *c);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_atomic_t { char __tag; };

struct __neverc_std_sync_t {
    char __tag;
    struct __neverc_std_atomic_t atomic;
};
extern struct __neverc_std_sync_t __neverc_mod_sync;
extern struct __neverc_std_sync_t sync_mod;
#endif

#endif
