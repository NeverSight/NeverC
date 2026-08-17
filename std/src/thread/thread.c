#include "neverc/std/thread.h"

#include <stdint.h>
#include <stdlib.h>

#ifndef NEVERC_THREAD_CALLOC
#define NEVERC_THREAD_CALLOC calloc
#endif

#ifndef NEVERC_THREAD_FREE
#define NEVERC_THREAD_FREE free
#endif

#ifndef NEVERC_THREAD_CREATE_SHOULD_FAIL
#define NEVERC_THREAD_CREATE_SHOULD_FAIL(worker_index) 0
#endif

#define NEVERC_THREAD_MAX_WORKERS 1024
#define NEVERC_THREAD_CONTEXT_POLL_MS 10

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef CRITICAL_SECTION neverc_thread_mutex_t;
typedef CONDITION_VARIABLE neverc_thread_cond_t;
typedef HANDLE neverc_thread_handle_t;

static int thread_mutex_init(neverc_thread_mutex_t *mutex) {
    InitializeCriticalSection(mutex);
    return 0;
}

static void thread_mutex_destroy(neverc_thread_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
}

static void thread_mutex_lock(neverc_thread_mutex_t *mutex) {
    EnterCriticalSection(mutex);
}

static void thread_mutex_unlock(neverc_thread_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
}

static int thread_cond_init(neverc_thread_cond_t *cond) {
    InitializeConditionVariable(cond);
    return 0;
}

static void thread_cond_destroy(neverc_thread_cond_t *cond) {
    (void)cond;
}

static void thread_cond_signal(neverc_thread_cond_t *cond) {
    WakeConditionVariable(cond);
}

static void thread_cond_broadcast(neverc_thread_cond_t *cond) {
    WakeAllConditionVariable(cond);
}

static int thread_cond_wait(neverc_thread_cond_t *cond,
                            neverc_thread_mutex_t *mutex,
                            neverc_context_t *ctx) {
    DWORD timeout = ctx ? NEVERC_THREAD_CONTEXT_POLL_MS : INFINITE;
    if (SleepConditionVariableCS(cond, mutex, timeout))
        return 0;
    return GetLastError() == ERROR_TIMEOUT ? 0 : -1;
}

#else

#include <errno.h>
#include <pthread.h>
#include <time.h>

typedef pthread_mutex_t neverc_thread_mutex_t;
typedef pthread_cond_t neverc_thread_cond_t;
typedef pthread_t neverc_thread_handle_t;

static int thread_mutex_init(neverc_thread_mutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

static void thread_mutex_destroy(neverc_thread_mutex_t *mutex) {
    (void)pthread_mutex_destroy(mutex);
}

static void thread_mutex_lock(neverc_thread_mutex_t *mutex) {
    (void)pthread_mutex_lock(mutex);
}

static void thread_mutex_unlock(neverc_thread_mutex_t *mutex) {
    (void)pthread_mutex_unlock(mutex);
}

static int thread_cond_init(neverc_thread_cond_t *cond) {
    return pthread_cond_init(cond, NULL);
}

static void thread_cond_destroy(neverc_thread_cond_t *cond) {
    (void)pthread_cond_destroy(cond);
}

static void thread_cond_signal(neverc_thread_cond_t *cond) {
    (void)pthread_cond_signal(cond);
}

static void thread_cond_broadcast(neverc_thread_cond_t *cond) {
    (void)pthread_cond_broadcast(cond);
}

static int thread_cond_wait(neverc_thread_cond_t *cond,
                            neverc_thread_mutex_t *mutex,
                            neverc_context_t *ctx) {
    if (!ctx)
        return pthread_cond_wait(cond, mutex) == 0 ? 0 : -1;

    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return -1;
    deadline.tv_nsec += NEVERC_THREAD_CONTEXT_POLL_MS * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    int result = pthread_cond_timedwait(cond, mutex, &deadline);
    return result == 0 || result == ETIMEDOUT ? 0 : -1;
}

#endif

typedef struct {
    neverc_thread_task_func_t function;
    void *arg;
} neverc_thread_task_t;

struct neverc_thread_executor {
    neverc_thread_task_t *queue;
    neverc_thread_handle_t *threads;
#if defined(_WIN32)
    DWORD *thread_ids;
#endif
    size_t worker_count;
    size_t queue_capacity;
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
    size_t active_count;
    int accepting;
    int shutdown_started;
    int shutdown_complete;
    int failed;
    neverc_thread_mutex_t mutex;
    neverc_thread_cond_t not_empty;
    neverc_thread_cond_t not_full;
    neverc_thread_cond_t idle;
    neverc_thread_cond_t stopped;
};

struct neverc_thread_channel {
    void **queue;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int closed;
    neverc_thread_mutex_t mutex;
    neverc_thread_cond_t not_empty;
    neverc_thread_cond_t not_full;
};

static int context_is_done(neverc_context_t *ctx) {
    return ctx && neverc_context_done(ctx);
}

static int executor_sync_init(neverc_thread_executor_t *executor) {
    if (thread_mutex_init(&executor->mutex) != 0)
        return -1;
    if (thread_cond_init(&executor->not_empty) != 0) {
        thread_mutex_destroy(&executor->mutex);
        return -1;
    }
    if (thread_cond_init(&executor->not_full) != 0) {
        thread_cond_destroy(&executor->not_empty);
        thread_mutex_destroy(&executor->mutex);
        return -1;
    }
    if (thread_cond_init(&executor->idle) != 0) {
        thread_cond_destroy(&executor->not_full);
        thread_cond_destroy(&executor->not_empty);
        thread_mutex_destroy(&executor->mutex);
        return -1;
    }
    if (thread_cond_init(&executor->stopped) != 0) {
        thread_cond_destroy(&executor->idle);
        thread_cond_destroy(&executor->not_full);
        thread_cond_destroy(&executor->not_empty);
        thread_mutex_destroy(&executor->mutex);
        return -1;
    }
    return 0;
}

static void executor_sync_destroy(neverc_thread_executor_t *executor) {
    thread_cond_destroy(&executor->stopped);
    thread_cond_destroy(&executor->idle);
    thread_cond_destroy(&executor->not_full);
    thread_cond_destroy(&executor->not_empty);
    thread_mutex_destroy(&executor->mutex);
}

static void executor_fail_locked(neverc_thread_executor_t *executor) {
    executor->failed = 1;
    executor->accepting = 0;
    thread_cond_broadcast(&executor->not_empty);
    thread_cond_broadcast(&executor->not_full);
    thread_cond_broadcast(&executor->idle);
}

#if defined(_WIN32)
static DWORD WINAPI executor_worker(void *opaque) {
#else
static void *executor_worker(void *opaque) {
#endif
    neverc_thread_executor_t *executor =
        (neverc_thread_executor_t *)opaque;

    thread_mutex_lock(&executor->mutex);
    for (;;) {
        while (executor->queue_count == 0 && executor->accepting) {
            if (thread_cond_wait(&executor->not_empty, &executor->mutex,
                                 NULL) != 0) {
                executor_fail_locked(executor);
                break;
            }
        }

        if (executor->queue_count == 0 && !executor->accepting)
            break;

        neverc_thread_task_t task = executor->queue[executor->queue_head];
        executor->queue_head =
            (executor->queue_head + 1) % executor->queue_capacity;
        executor->queue_count--;
        executor->active_count++;
        thread_cond_signal(&executor->not_full);
        thread_mutex_unlock(&executor->mutex);

        task.function(task.arg);

        thread_mutex_lock(&executor->mutex);
        executor->active_count--;
        if (executor->queue_count == 0 && executor->active_count == 0)
            thread_cond_broadcast(&executor->idle);
    }
    thread_mutex_unlock(&executor->mutex);

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int executor_is_current_worker_locked(
    neverc_thread_executor_t *executor) {
    if (executor->shutdown_complete)
        return 0;
#if defined(_WIN32)
    DWORD current = GetCurrentThreadId();
    for (size_t i = 0; i < executor->worker_count; ++i) {
        if (executor->thread_ids[i] == current)
            return 1;
    }
#else
    pthread_t current = pthread_self();
    for (size_t i = 0; i < executor->worker_count; ++i) {
        if (pthread_equal(executor->threads[i], current))
            return 1;
    }
#endif
    return 0;
}

static int executor_is_current_worker(
    neverc_thread_executor_t *executor) {
    thread_mutex_lock(&executor->mutex);
    int is_worker = executor_is_current_worker_locked(executor);
    thread_mutex_unlock(&executor->mutex);
    return is_worker;
}

neverc_thread_executor_t *neverc_thread_executor_create(
    size_t worker_count, size_t queue_capacity) {
    if (worker_count == 0 || worker_count > NEVERC_THREAD_MAX_WORKERS ||
        queue_capacity == 0 ||
        queue_capacity > SIZE_MAX / sizeof(neverc_thread_task_t))
        return NULL;

    neverc_thread_executor_t *executor =
        (neverc_thread_executor_t *)NEVERC_THREAD_CALLOC(
            1, sizeof(*executor));
    if (!executor)
        return NULL;

    executor->queue =
        (neverc_thread_task_t *)NEVERC_THREAD_CALLOC(
            queue_capacity, sizeof(*executor->queue));
    executor->threads =
        (neverc_thread_handle_t *)NEVERC_THREAD_CALLOC(
            worker_count, sizeof(*executor->threads));
#if defined(_WIN32)
    executor->thread_ids =
        (DWORD *)NEVERC_THREAD_CALLOC(
            worker_count, sizeof(*executor->thread_ids));
#endif
    if (!executor->queue || !executor->threads
#if defined(_WIN32)
        || !executor->thread_ids
#endif
    ) {
#if defined(_WIN32)
        NEVERC_THREAD_FREE(executor->thread_ids);
#endif
        NEVERC_THREAD_FREE(executor->threads);
        NEVERC_THREAD_FREE(executor->queue);
        NEVERC_THREAD_FREE(executor);
        return NULL;
    }

    if (executor_sync_init(executor) != 0) {
#if defined(_WIN32)
        NEVERC_THREAD_FREE(executor->thread_ids);
#endif
        NEVERC_THREAD_FREE(executor->threads);
        NEVERC_THREAD_FREE(executor->queue);
        NEVERC_THREAD_FREE(executor);
        return NULL;
    }

    executor->worker_count = worker_count;
    executor->queue_capacity = queue_capacity;
    executor->accepting = 1;

    size_t created = 0;
    for (; created < worker_count; ++created) {
        if (NEVERC_THREAD_CREATE_SHOULD_FAIL(created))
            break;
#if defined(_WIN32)
        executor->threads[created] =
            CreateThread(NULL, 0, executor_worker, executor, 0,
                         &executor->thread_ids[created]);
        if (!executor->threads[created])
            break;
#else
        if (pthread_create(&executor->threads[created], NULL,
                           executor_worker, executor) != 0)
            break;
#endif
    }

    if (created != worker_count) {
        thread_mutex_lock(&executor->mutex);
        executor->accepting = 0;
        thread_cond_broadcast(&executor->not_empty);
        thread_mutex_unlock(&executor->mutex);

        for (size_t i = 0; i < created; ++i) {
#if defined(_WIN32)
            (void)WaitForSingleObject(executor->threads[i], INFINITE);
            CloseHandle(executor->threads[i]);
#else
            (void)pthread_join(executor->threads[i], NULL);
#endif
        }
        executor_sync_destroy(executor);
#if defined(_WIN32)
        NEVERC_THREAD_FREE(executor->thread_ids);
#endif
        NEVERC_THREAD_FREE(executor->threads);
        NEVERC_THREAD_FREE(executor->queue);
        NEVERC_THREAD_FREE(executor);
        return NULL;
    }

    return executor;
}

static int executor_submit_impl(
    neverc_thread_executor_t *executor, neverc_context_t *ctx,
    neverc_thread_task_func_t function, void *arg, int nonblocking) {
    if (!executor || !function)
        return NEVERC_THREAD_INVALID;
    if (context_is_done(ctx))
        return NEVERC_THREAD_CANCELLED;

    thread_mutex_lock(&executor->mutex);
    while (executor->queue_count == executor->queue_capacity &&
           executor->accepting && !executor->failed) {
        if (nonblocking) {
            thread_mutex_unlock(&executor->mutex);
            return NEVERC_THREAD_WOULD_BLOCK;
        }
        /* Every worker is inside a task. A blocking self-submit cannot
         * make progress: no worker remains to drain the queue. */
        if (executor->active_count == executor->worker_count &&
            executor_is_current_worker_locked(executor)) {
            thread_mutex_unlock(&executor->mutex);
            return NEVERC_THREAD_INVALID;
        }
        if (context_is_done(ctx)) {
            thread_mutex_unlock(&executor->mutex);
            return NEVERC_THREAD_CANCELLED;
        }
        if (thread_cond_wait(&executor->not_full, &executor->mutex,
                             ctx) != 0) {
            thread_mutex_unlock(&executor->mutex);
            return NEVERC_THREAD_SYSTEM;
        }
    }

    if (executor->failed) {
        thread_mutex_unlock(&executor->mutex);
        return NEVERC_THREAD_SYSTEM;
    }
    if (!executor->accepting) {
        thread_mutex_unlock(&executor->mutex);
        return NEVERC_THREAD_CLOSED;
    }
    if (context_is_done(ctx)) {
        thread_mutex_unlock(&executor->mutex);
        return NEVERC_THREAD_CANCELLED;
    }

    executor->queue[executor->queue_tail].function = function;
    executor->queue[executor->queue_tail].arg = arg;
    executor->queue_tail =
        (executor->queue_tail + 1) % executor->queue_capacity;
    executor->queue_count++;
    thread_cond_signal(&executor->not_empty);
    thread_mutex_unlock(&executor->mutex);
    return NEVERC_THREAD_OK;
}

int neverc_thread_executor_submit(
    neverc_thread_executor_t *executor,
    neverc_thread_task_func_t function, void *arg) {
    return executor_submit_impl(executor, NULL, function, arg, 0);
}

int neverc_thread_executor_submit_context(
    neverc_thread_executor_t *executor, neverc_context_t *ctx,
    neverc_thread_task_func_t function, void *arg) {
    return executor_submit_impl(executor, ctx, function, arg, 0);
}

int neverc_thread_executor_try_submit(
    neverc_thread_executor_t *executor,
    neverc_thread_task_func_t function, void *arg) {
    return executor_submit_impl(executor, NULL, function, arg, 1);
}

static int executor_wait_impl(
    neverc_thread_executor_t *executor, neverc_context_t *ctx) {
    if (!executor)
        return NEVERC_THREAD_INVALID;
    if (context_is_done(ctx))
        return NEVERC_THREAD_CANCELLED;
    if (executor_is_current_worker(executor))
        return NEVERC_THREAD_INVALID;

    thread_mutex_lock(&executor->mutex);
    while ((executor->queue_count != 0 || executor->active_count != 0) &&
           !executor->failed) {
        if (context_is_done(ctx)) {
            thread_mutex_unlock(&executor->mutex);
            return NEVERC_THREAD_CANCELLED;
        }
        if (thread_cond_wait(&executor->idle, &executor->mutex, ctx) != 0) {
            thread_mutex_unlock(&executor->mutex);
            return NEVERC_THREAD_SYSTEM;
        }
    }
    int result = executor->failed
                     ? NEVERC_THREAD_SYSTEM
                     : NEVERC_THREAD_OK;
    thread_mutex_unlock(&executor->mutex);
    return result;
}

int neverc_thread_executor_wait(neverc_thread_executor_t *executor) {
    return executor_wait_impl(executor, NULL);
}

int neverc_thread_executor_wait_context(
    neverc_thread_executor_t *executor, neverc_context_t *ctx) {
    return executor_wait_impl(executor, ctx);
}

size_t neverc_thread_executor_pending(
    neverc_thread_executor_t *executor) {
    if (!executor)
        return 0;
    thread_mutex_lock(&executor->mutex);
    size_t pending = executor->queue_count;
    thread_mutex_unlock(&executor->mutex);
    return pending;
}

size_t neverc_thread_executor_active(
    neverc_thread_executor_t *executor) {
    if (!executor)
        return 0;
    thread_mutex_lock(&executor->mutex);
    size_t active = executor->active_count;
    thread_mutex_unlock(&executor->mutex);
    return active;
}

int neverc_thread_executor_shutdown(
    neverc_thread_executor_t *executor) {
    if (!executor)
        return NEVERC_THREAD_INVALID;
    if (executor_is_current_worker(executor))
        return NEVERC_THREAD_INVALID;

    thread_mutex_lock(&executor->mutex);
    if (executor->shutdown_complete) {
        int result = executor->failed
                         ? NEVERC_THREAD_SYSTEM
                         : NEVERC_THREAD_OK;
        thread_mutex_unlock(&executor->mutex);
        return result;
    }
    if (executor->shutdown_started) {
        while (!executor->shutdown_complete) {
            if (thread_cond_wait(&executor->stopped, &executor->mutex,
                                 NULL) != 0) {
                thread_mutex_unlock(&executor->mutex);
                return NEVERC_THREAD_SYSTEM;
            }
        }
        int result = executor->failed
                         ? NEVERC_THREAD_SYSTEM
                         : NEVERC_THREAD_OK;
        thread_mutex_unlock(&executor->mutex);
        return result;
    }

    executor->shutdown_started = 1;
    executor->accepting = 0;
    thread_cond_broadcast(&executor->not_empty);
    thread_cond_broadcast(&executor->not_full);
    thread_mutex_unlock(&executor->mutex);

    int join_failed = 0;
    for (size_t i = 0; i < executor->worker_count; ++i) {
#if defined(_WIN32)
        if (WaitForSingleObject(executor->threads[i], INFINITE) !=
            WAIT_OBJECT_0)
            join_failed = 1;
        CloseHandle(executor->threads[i]);
#else
        if (pthread_join(executor->threads[i], NULL) != 0)
            join_failed = 1;
#endif
    }

    thread_mutex_lock(&executor->mutex);
    if (join_failed)
        executor->failed = 1;
#if defined(_WIN32)
    for (size_t i = 0; i < executor->worker_count; ++i) {
        executor->threads[i] = NULL;
        executor->thread_ids[i] = 0;
    }
#endif
    executor->shutdown_complete = 1;
    thread_cond_broadcast(&executor->stopped);
    thread_cond_broadcast(&executor->idle);
    thread_mutex_unlock(&executor->mutex);

    return executor->failed
               ? NEVERC_THREAD_SYSTEM
               : NEVERC_THREAD_OK;
}

void neverc_thread_executor_free(
    neverc_thread_executor_t *executor) {
    if (!executor || executor_is_current_worker(executor))
        return;
    (void)neverc_thread_executor_shutdown(executor);
    executor_sync_destroy(executor);
#if defined(_WIN32)
    NEVERC_THREAD_FREE(executor->thread_ids);
#endif
    NEVERC_THREAD_FREE(executor->threads);
    NEVERC_THREAD_FREE(executor->queue);
    NEVERC_THREAD_FREE(executor);
}

static int channel_sync_init(neverc_thread_channel_t *channel) {
    if (thread_mutex_init(&channel->mutex) != 0)
        return -1;
    if (thread_cond_init(&channel->not_empty) != 0) {
        thread_mutex_destroy(&channel->mutex);
        return -1;
    }
    if (thread_cond_init(&channel->not_full) != 0) {
        thread_cond_destroy(&channel->not_empty);
        thread_mutex_destroy(&channel->mutex);
        return -1;
    }
    return 0;
}

static void channel_sync_destroy(neverc_thread_channel_t *channel) {
    thread_cond_destroy(&channel->not_full);
    thread_cond_destroy(&channel->not_empty);
    thread_mutex_destroy(&channel->mutex);
}

neverc_thread_channel_t *neverc_thread_channel_create(size_t capacity) {
    if (capacity == 0 || capacity > SIZE_MAX / sizeof(void *))
        return NULL;

    neverc_thread_channel_t *channel =
        (neverc_thread_channel_t *)NEVERC_THREAD_CALLOC(
            1, sizeof(*channel));
    if (!channel)
        return NULL;
    channel->queue =
        (void **)NEVERC_THREAD_CALLOC(capacity, sizeof(*channel->queue));
    if (!channel->queue) {
        NEVERC_THREAD_FREE(channel);
        return NULL;
    }
    if (channel_sync_init(channel) != 0) {
        NEVERC_THREAD_FREE(channel->queue);
        NEVERC_THREAD_FREE(channel);
        return NULL;
    }
    channel->capacity = capacity;
    return channel;
}

static int channel_send_impl(
    neverc_thread_channel_t *channel, neverc_context_t *ctx,
    void *value, int nonblocking) {
    if (!channel)
        return NEVERC_THREAD_INVALID;
    if (context_is_done(ctx))
        return NEVERC_THREAD_CANCELLED;

    thread_mutex_lock(&channel->mutex);
    while (channel->count == channel->capacity && !channel->closed) {
        if (nonblocking) {
            thread_mutex_unlock(&channel->mutex);
            return NEVERC_THREAD_WOULD_BLOCK;
        }
        if (context_is_done(ctx)) {
            thread_mutex_unlock(&channel->mutex);
            return NEVERC_THREAD_CANCELLED;
        }
        if (thread_cond_wait(&channel->not_full, &channel->mutex,
                             ctx) != 0) {
            thread_mutex_unlock(&channel->mutex);
            return NEVERC_THREAD_SYSTEM;
        }
    }

    if (channel->closed) {
        thread_mutex_unlock(&channel->mutex);
        return NEVERC_THREAD_CLOSED;
    }
    if (context_is_done(ctx)) {
        thread_mutex_unlock(&channel->mutex);
        return NEVERC_THREAD_CANCELLED;
    }

    channel->queue[channel->tail] = value;
    channel->tail = (channel->tail + 1) % channel->capacity;
    channel->count++;
    thread_cond_signal(&channel->not_empty);
    thread_mutex_unlock(&channel->mutex);
    return NEVERC_THREAD_OK;
}

int neverc_thread_channel_send(
    neverc_thread_channel_t *channel, void *value) {
    return channel_send_impl(channel, NULL, value, 0);
}

int neverc_thread_channel_send_context(
    neverc_thread_channel_t *channel, neverc_context_t *ctx, void *value) {
    return channel_send_impl(channel, ctx, value, 0);
}

int neverc_thread_channel_try_send(
    neverc_thread_channel_t *channel, void *value) {
    return channel_send_impl(channel, NULL, value, 1);
}

static int channel_receive_impl(
    neverc_thread_channel_t *channel, neverc_context_t *ctx,
    void **value_out, int nonblocking) {
    if (value_out)
        *value_out = NULL;
    if (!channel || !value_out)
        return NEVERC_THREAD_INVALID;
    if (context_is_done(ctx))
        return NEVERC_THREAD_CANCELLED;

    thread_mutex_lock(&channel->mutex);
    while (channel->count == 0 && !channel->closed) {
        if (nonblocking) {
            thread_mutex_unlock(&channel->mutex);
            return NEVERC_THREAD_WOULD_BLOCK;
        }
        if (context_is_done(ctx)) {
            thread_mutex_unlock(&channel->mutex);
            return NEVERC_THREAD_CANCELLED;
        }
        if (thread_cond_wait(&channel->not_empty, &channel->mutex,
                             ctx) != 0) {
            thread_mutex_unlock(&channel->mutex);
            return NEVERC_THREAD_SYSTEM;
        }
    }

    if (channel->count == 0) {
        thread_mutex_unlock(&channel->mutex);
        return NEVERC_THREAD_CLOSED;
    }
    if (context_is_done(ctx)) {
        thread_mutex_unlock(&channel->mutex);
        return NEVERC_THREAD_CANCELLED;
    }

    *value_out = channel->queue[channel->head];
    channel->queue[channel->head] = NULL;
    channel->head = (channel->head + 1) % channel->capacity;
    channel->count--;
    thread_cond_signal(&channel->not_full);
    thread_mutex_unlock(&channel->mutex);
    return NEVERC_THREAD_OK;
}

int neverc_thread_channel_receive(
    neverc_thread_channel_t *channel, void **value_out) {
    return channel_receive_impl(channel, NULL, value_out, 0);
}

int neverc_thread_channel_receive_context(
    neverc_thread_channel_t *channel, neverc_context_t *ctx,
    void **value_out) {
    return channel_receive_impl(channel, ctx, value_out, 0);
}

int neverc_thread_channel_try_receive(
    neverc_thread_channel_t *channel, void **value_out) {
    return channel_receive_impl(channel, NULL, value_out, 1);
}

int neverc_thread_channel_close(neverc_thread_channel_t *channel) {
    if (!channel)
        return NEVERC_THREAD_INVALID;
    thread_mutex_lock(&channel->mutex);
    channel->closed = 1;
    thread_cond_broadcast(&channel->not_empty);
    thread_cond_broadcast(&channel->not_full);
    thread_mutex_unlock(&channel->mutex);
    return NEVERC_THREAD_OK;
}

size_t neverc_thread_channel_length(
    neverc_thread_channel_t *channel) {
    if (!channel)
        return 0;
    thread_mutex_lock(&channel->mutex);
    size_t length = channel->count;
    thread_mutex_unlock(&channel->mutex);
    return length;
}

size_t neverc_thread_channel_capacity(
    neverc_thread_channel_t *channel) {
    return channel ? channel->capacity : 0;
}

void neverc_thread_channel_free(neverc_thread_channel_t *channel) {
    if (!channel)
        return;
    (void)neverc_thread_channel_close(channel);
    channel_sync_destroy(channel);
    NEVERC_THREAD_FREE(channel->queue);
    NEVERC_THREAD_FREE(channel);
}
