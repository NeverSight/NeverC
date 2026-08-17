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
typedef struct {
    SRWLOCK srw;
    DWORD owner; /* 0 = unlocked; GetCurrentThreadId() while held */
} neverc_mutex_t;
#else
typedef struct { pthread_mutex_t mu; } neverc_mutex_t;
#endif

/* Returns 0 on success, -1 if the mutex cannot be created. */
int  neverc_sync_mutex_init(neverc_mutex_t *m);
void neverc_sync_mutex_destroy(neverc_mutex_t *m);
void neverc_sync_mutex_lock(neverc_mutex_t *m);
/* Unlocking a mutex that is not held by the caller is a no-op. */
void neverc_sync_mutex_unlock(neverc_mutex_t *m);
int  neverc_sync_mutex_trylock(neverc_mutex_t *m);

#define neverc_mutex_init    neverc_sync_mutex_init
#define neverc_mutex_destroy neverc_sync_mutex_destroy
#define neverc_mutex_lock    neverc_sync_mutex_lock
#define neverc_mutex_unlock  neverc_sync_mutex_unlock
#define neverc_mutex_trylock neverc_sync_mutex_trylock

#if defined(_WIN32)
typedef struct { SRWLOCK rw; } neverc_rwmutex_t;
#else
typedef struct { pthread_rwlock_t rw; } neverc_rwmutex_t;
#endif

/* Returns 0 on success, -1 if the rwmutex cannot be created. */
int  neverc_sync_rwmutex_init(neverc_rwmutex_t *rw);
void neverc_sync_rwmutex_destroy(neverc_rwmutex_t *rw);
void neverc_sync_rwmutex_rlock(neverc_rwmutex_t *rw);
int  neverc_sync_rwmutex_tryrlock(neverc_rwmutex_t *rw);
void neverc_sync_rwmutex_runlock(neverc_rwmutex_t *rw);
void neverc_sync_rwmutex_lock(neverc_rwmutex_t *rw);
int  neverc_sync_rwmutex_trylock(neverc_rwmutex_t *rw);
void neverc_sync_rwmutex_unlock(neverc_rwmutex_t *rw);

#define neverc_rwmutex_init     neverc_sync_rwmutex_init
#define neverc_rwmutex_destroy  neverc_sync_rwmutex_destroy
#define neverc_rwmutex_rlock    neverc_sync_rwmutex_rlock
#define neverc_rwmutex_tryrlock neverc_sync_rwmutex_tryrlock
#define neverc_rwmutex_runlock  neverc_sync_rwmutex_runlock
#define neverc_rwmutex_lock     neverc_sync_rwmutex_lock
#define neverc_rwmutex_trylock  neverc_sync_rwmutex_trylock
#define neverc_rwmutex_unlock   neverc_sync_rwmutex_unlock

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

/* Returns 0 on success, -1 if the waitgroup cannot be created. */
int  neverc_sync_waitgroup_init(neverc_waitgroup_t *wg);
void neverc_sync_waitgroup_destroy(neverc_waitgroup_t *wg);
/* Legacy Add/Done calls leave the counter unchanged on invalid input. */
void neverc_sync_waitgroup_add(neverc_waitgroup_t *wg, int delta);
void neverc_sync_waitgroup_done(neverc_waitgroup_t *wg);
/* Checked variants return -1 if the counter would leave [0, INT32_MAX]. */
int  neverc_sync_waitgroup_add_checked(neverc_waitgroup_t *wg, int delta);
int  neverc_sync_waitgroup_done_checked(neverc_waitgroup_t *wg);
void neverc_sync_waitgroup_wait(neverc_waitgroup_t *wg);

#define neverc_waitgroup_init         neverc_sync_waitgroup_init
#define neverc_waitgroup_destroy      neverc_sync_waitgroup_destroy
#define neverc_waitgroup_add          neverc_sync_waitgroup_add
#define neverc_waitgroup_done         neverc_sync_waitgroup_done
#define neverc_waitgroup_add_checked  neverc_sync_waitgroup_add_checked
#define neverc_waitgroup_done_checked neverc_sync_waitgroup_done_checked
#define neverc_waitgroup_wait         neverc_sync_waitgroup_wait

#if defined(_WIN32)
typedef struct { volatile int32_t done; CRITICAL_SECTION mu; } neverc_once_t;
#else
typedef struct { volatile int32_t done; pthread_mutex_t mu; } neverc_once_t;
#endif

/* Returns 0 on success, -1 if the once cannot be created. */
int  neverc_sync_once_init(neverc_once_t *o);
void neverc_sync_once_destroy(neverc_once_t *o);
void neverc_sync_once_do(neverc_once_t *o, void (*f)(void));

#define neverc_once_init    neverc_sync_once_init
#define neverc_once_destroy neverc_sync_once_destroy
#define neverc_once_do      neverc_sync_once_do

#if defined(_WIN32)
typedef struct { CONDITION_VARIABLE cond; neverc_mutex_t *m; } neverc_cond_t;
#else
typedef struct { pthread_cond_t cond; pthread_mutex_t *mu; } neverc_cond_t;
#endif

/* Returns 0 on success, -1 if the cond cannot be created. */
int  neverc_sync_cond_init(neverc_cond_t *c, neverc_mutex_t *m);
void neverc_sync_cond_destroy(neverc_cond_t *c);
void neverc_sync_cond_wait(neverc_cond_t *c);
void neverc_sync_cond_signal(neverc_cond_t *c);
void neverc_sync_cond_broadcast(neverc_cond_t *c);

#define neverc_cond_init      neverc_sync_cond_init
#define neverc_cond_destroy   neverc_sync_cond_destroy
#define neverc_cond_wait      neverc_sync_cond_wait
#define neverc_cond_signal    neverc_sync_cond_signal
#define neverc_cond_broadcast neverc_sync_cond_broadcast

/*
 * sync.Pool — thread-safe reusable object pool.
 * Mirrors Go sync.Pool: Put returns an object; Get retrieves or creates one.
 */
typedef struct neverc_sync_pool neverc_sync_pool_t;

neverc_sync_pool_t *neverc_sync_pool_new(void *(*new_func)(void));
void  neverc_sync_pool_free(neverc_sync_pool_t *p);
void  neverc_sync_pool_put(neverc_sync_pool_t *p, void *x);
void *neverc_sync_pool_get(neverc_sync_pool_t *p);

/*
 * sync.Map — concurrent-safe string-keyed map.
 * Mirrors Go sync.Map: thread-safe without external locking.
 */
typedef struct neverc_sync_map neverc_sync_map_t;

neverc_sync_map_t *neverc_sync_map_new(void);
void   neverc_sync_map_free(neverc_sync_map_t *m);
/* Returns 0 on success, -1 if the key cannot be stored (OOM / invalid). */
int    neverc_sync_map_store(neverc_sync_map_t *m, const char *key, void *value);
void  *neverc_sync_map_load(neverc_sync_map_t *m, const char *key, int *ok);
void  *neverc_sync_map_load_or_store(neverc_sync_map_t *m, const char *key, void *value, int *loaded);
void  *neverc_sync_map_load_and_delete(neverc_sync_map_t *m, const char *key, int *loaded);
void   neverc_sync_map_delete(neverc_sync_map_t *m, const char *key);
void   neverc_sync_map_clear(neverc_sync_map_t *m);
/* The callback runs without the map lock and may call methods on m.  key
 * remains valid for the duration of its callback invocation. */
void   neverc_sync_map_range(neverc_sync_map_t *m, int (*f)(const char *key, void *value, void *user), void *user);
void  *neverc_sync_map_swap(neverc_sync_map_t *m, const char *key, void *value, int *loaded);
int    neverc_sync_map_compare_and_swap(neverc_sync_map_t *m, const char *key, void *old_val, void *new_val);
int    neverc_sync_map_compare_and_delete(neverc_sync_map_t *m, const char *key, void *old_val);

#ifdef __cplusplus
}
#endif

#include "sync/atomic.h"

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
