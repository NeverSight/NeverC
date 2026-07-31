#ifndef NEVERC_THREAD_H
#define NEVERC_THREAD_H

/*
 * NeverC thread — bounded task execution and cross-thread message passing.
 *
 * Executor and channel queues are bounded deliberately: callers must handle
 * backpressure instead of allowing unbounded memory growth. Blocking APIs have
 * context-aware variants so cancellation and deadlines can interrupt waits.
 */

#include "context.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_thread_executor neverc_thread_executor_t;
typedef struct neverc_thread_channel neverc_thread_channel_t;
typedef void (*neverc_thread_task_func_t)(void *arg);

enum {
    NEVERC_THREAD_OK = 0,
    NEVERC_THREAD_CLOSED = 1,
    NEVERC_THREAD_WOULD_BLOCK = 2,
    NEVERC_THREAD_CANCELLED = 3,
    NEVERC_THREAD_INVALID = -1,
    NEVERC_THREAD_NOMEM = -2,
    NEVERC_THREAD_SYSTEM = -3
};

/*
 * Create a fixed-size executor with a bounded FIFO task queue.
 * worker_count and queue_capacity must both be greater than zero.
 */
neverc_thread_executor_t *neverc_thread_executor_create(
    size_t worker_count, size_t queue_capacity);

/* Submit blocks while the queue is full. The context-aware form returns
 * NEVERC_THREAD_CANCELLED if ctx is cancelled or its deadline expires. */
int neverc_thread_executor_submit(
    neverc_thread_executor_t *executor,
    neverc_thread_task_func_t function, void *arg);
int neverc_thread_executor_submit_context(
    neverc_thread_executor_t *executor, neverc_context_t *ctx,
    neverc_thread_task_func_t function, void *arg);

/* Non-blocking submission returns NEVERC_THREAD_WOULD_BLOCK when full. */
int neverc_thread_executor_try_submit(
    neverc_thread_executor_t *executor,
    neverc_thread_task_func_t function, void *arg);

/* Wait until all queued and running tasks complete. */
int neverc_thread_executor_wait(neverc_thread_executor_t *executor);
int neverc_thread_executor_wait_context(
    neverc_thread_executor_t *executor, neverc_context_t *ctx);

size_t neverc_thread_executor_pending(neverc_thread_executor_t *executor);
size_t neverc_thread_executor_active(neverc_thread_executor_t *executor);

/*
 * Stop accepting work, drain queued tasks, and join all workers. Shutdown is
 * idempotent but must not be called by a task running on the same executor.
 */
int neverc_thread_executor_shutdown(neverc_thread_executor_t *executor);

/* Free performs a draining shutdown if necessary. The caller must ensure that
 * no other thread is still using the executor when free begins. */
void neverc_thread_executor_free(neverc_thread_executor_t *executor);

/*
 * A bounded FIFO channel of non-owning void pointers. Capacity must be greater
 * than zero. Sending NULL is allowed; receive status distinguishes it from a
 * closed channel.
 */
neverc_thread_channel_t *neverc_thread_channel_create(size_t capacity);
int neverc_thread_channel_send(
    neverc_thread_channel_t *channel, void *value);
int neverc_thread_channel_send_context(
    neverc_thread_channel_t *channel, neverc_context_t *ctx, void *value);
int neverc_thread_channel_try_send(
    neverc_thread_channel_t *channel, void *value);

int neverc_thread_channel_receive(
    neverc_thread_channel_t *channel, void **value_out);
int neverc_thread_channel_receive_context(
    neverc_thread_channel_t *channel, neverc_context_t *ctx, void **value_out);
int neverc_thread_channel_try_receive(
    neverc_thread_channel_t *channel, void **value_out);

/* Close is idempotent. Receivers drain buffered values before observing
 * NEVERC_THREAD_CLOSED; blocked senders and receivers are awakened. */
int neverc_thread_channel_close(neverc_thread_channel_t *channel);
size_t neverc_thread_channel_length(neverc_thread_channel_t *channel);
size_t neverc_thread_channel_capacity(neverc_thread_channel_t *channel);

/* The caller must ensure no send or receive operation is in flight. */
void neverc_thread_channel_free(neverc_thread_channel_t *channel);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_thread_t { char __tag; };
extern struct __neverc_std_thread_t __neverc_mod_thread;
extern struct __neverc_std_thread_t thread;
#endif

#endif /* NEVERC_THREAD_H */
