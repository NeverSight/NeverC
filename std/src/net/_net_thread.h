#ifndef NEVERC_NET_THREAD_H
#define NEVERC_NET_THREAD_H

#include "_net_platform.h"

#define NC_THREADPOOL_MAX_THREADS 256
#define NC_THREADPOOL_QUEUE_SIZE  65536

#ifndef NC_THREADPOOL_CALLOC
#define NC_THREADPOOL_CALLOC calloc
#endif

typedef void (*nc_task_func_t)(void *arg);

typedef struct {
    nc_task_func_t func;
    void *arg;
} nc_task_t;

typedef struct {
    nc_thread_t *threads;
    int nthreads;
    nc_task_t *queue;
    int queue_cap;
    int queue_head;
    int queue_tail;
    int queue_count;
    nc_mutex_t mutex;
    nc_cond_t not_empty;
    nc_cond_t not_full;
    volatile int shutdown;
} nc_threadpool_t;

#ifdef _WIN32
static DWORD WINAPI nc_threadpool_worker(LPVOID arg) {
#else
static void *nc_threadpool_worker(void *arg) {
#endif
    nc_threadpool_t *pool = (nc_threadpool_t *)arg;
    for (;;) {
        nc_mutex_lock(&pool->mutex);
        while (pool->queue_count == 0 && !pool->shutdown) {
            nc_cond_wait(&pool->not_empty, &pool->mutex);
        }
        if (pool->shutdown && pool->queue_count == 0) {
            nc_mutex_unlock(&pool->mutex);
            break;
        }
        nc_task_t task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_cap;
        pool->queue_count--;
        nc_cond_signal(&pool->not_full);
        nc_mutex_unlock(&pool->mutex);

        task.func(task.arg);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static inline nc_threadpool_t *nc_threadpool_create(int nthreads) {
    if (nthreads <= 0) nthreads = 4;
    if (nthreads > NC_THREADPOOL_MAX_THREADS)
        nthreads = NC_THREADPOOL_MAX_THREADS;

    nc_threadpool_t *pool =
        (nc_threadpool_t *)NC_THREADPOOL_CALLOC(1, sizeof(*pool));
    if (!pool) return NULL;

    pool->queue_cap = NC_THREADPOOL_QUEUE_SIZE;
    pool->queue =
        (nc_task_t *)NC_THREADPOOL_CALLOC((size_t)pool->queue_cap,
                                          sizeof(nc_task_t));
    pool->threads =
        (nc_thread_t *)NC_THREADPOOL_CALLOC((size_t)nthreads,
                                             sizeof(nc_thread_t));
    if (!pool->queue || !pool->threads) {
        free(pool->threads);
        free(pool->queue);
        free(pool);
        return NULL;
    }

    nc_mutex_init(&pool->mutex);
    nc_cond_init(&pool->not_empty);
    nc_cond_init(&pool->not_full);

    for (int i = 0; i < nthreads; i++) {
#ifdef _WIN32
        pool->threads[i] =
            CreateThread(NULL, 0, nc_threadpool_worker, pool, 0, NULL);
        if (!pool->threads[i])
            break;
#else
        if (pthread_create(&pool->threads[i], NULL, nc_threadpool_worker,
                           pool) != 0)
            break;
#endif
        pool->nthreads++;
    }

    if (pool->nthreads != nthreads) {
        nc_mutex_lock(&pool->mutex);
        pool->shutdown = 1;
        nc_cond_broadcast(&pool->not_empty);
        nc_mutex_unlock(&pool->mutex);
        for (int i = 0; i < pool->nthreads; i++) {
#ifdef _WIN32
            WaitForSingleObject(pool->threads[i], INFINITE);
            CloseHandle(pool->threads[i]);
#else
            pthread_join(pool->threads[i], NULL);
#endif
        }
        nc_mutex_destroy(&pool->mutex);
        nc_cond_destroy(&pool->not_empty);
        nc_cond_destroy(&pool->not_full);
        free(pool->threads);
        free(pool->queue);
        free(pool);
        return NULL;
    }
    return pool;
}

static inline int nc_threadpool_submit(nc_threadpool_t *pool,
                                        nc_task_func_t func, void *arg) {
    if (!pool || !func) return -1;
    nc_mutex_lock(&pool->mutex);
    while (pool->queue_count == pool->queue_cap && !pool->shutdown) {
        nc_cond_wait(&pool->not_full, &pool->mutex);
    }
    if (pool->shutdown) {
        nc_mutex_unlock(&pool->mutex);
        return -1;
    }
    pool->queue[pool->queue_tail].func = func;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_cap;
    pool->queue_count++;
    nc_cond_signal(&pool->not_empty);
    nc_mutex_unlock(&pool->mutex);
    return 0;
}

static inline void nc_threadpool_destroy(nc_threadpool_t *pool) {
    if (!pool) return;
    nc_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    nc_cond_broadcast(&pool->not_empty);
    nc_cond_broadcast(&pool->not_full);
    nc_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->nthreads; i++) {
#ifdef _WIN32
        WaitForSingleObject(pool->threads[i], INFINITE);
        CloseHandle(pool->threads[i]);
#else
        pthread_join(pool->threads[i], NULL);
#endif
    }
    free(pool->threads);
    free(pool->queue);
    nc_mutex_destroy(&pool->mutex);
    nc_cond_destroy(&pool->not_empty);
    nc_cond_destroy(&pool->not_full);
    free(pool);
}

typedef void *(*nc_thread_func_t)(void *);

static inline int nc_thread_create(nc_thread_t *t, nc_thread_func_t func,
                                   void *arg) {
#ifdef _WIN32
    *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return *t ? 0 : -1;
#else
    return pthread_create(t, NULL, func, arg);
#endif
}

static inline int nc_thread_join(nc_thread_t t) {
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
#else
    return pthread_join(t, NULL);
#endif
}

#endif /* NEVERC_NET_THREAD_H */
