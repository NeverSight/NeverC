#ifndef NEVERC_NET_RPC_SERVER_QUEUE_H
#define NEVERC_NET_RPC_SERVER_QUEUE_H

#include "neverc/std/context.h"
#include "neverc/std/thread.h"

typedef enum {
    NC_RPC_SERVER_QUEUE_STREAM = 0,
    NC_RPC_SERVER_QUEUE_CONTROL = 1,
    NC_RPC_SERVER_QUEUE_TERMINAL = 2
} nc_rpc_server_queue_mode_t;

typedef struct {
    int (*send_context)(neverc_thread_channel_t *, neverc_context_t *, void *);
    int (*try_send)(neverc_thread_channel_t *, void *);
    int (*send)(neverc_thread_channel_t *, void *);
} nc_rpc_server_queue_ops_t;

/* Stream DATA observes its request context. Opportunistic connection control
 * frames never block. Terminal stream outcomes block for FIFO ownership and
 * are released by connection shutdown closing the channel. */
static inline int nc_rpc_server_queue_send_with_ops(
    neverc_thread_channel_t *queue, neverc_context_t *context,
    nc_rpc_server_queue_mode_t mode, void *value,
    const nc_rpc_server_queue_ops_t *ops) {
    if (!ops || !ops->send_context || !ops->try_send || !ops->send)
        return NEVERC_THREAD_INVALID;
    switch (mode) {
    case NC_RPC_SERVER_QUEUE_STREAM:
        return context
            ? ops->send_context(queue, context, value)
            : NEVERC_THREAD_INVALID;
    case NC_RPC_SERVER_QUEUE_CONTROL:
        return ops->try_send(queue, value);
    case NC_RPC_SERVER_QUEUE_TERMINAL:
        return ops->send(queue, value);
    default:
        return NEVERC_THREAD_INVALID;
    }
}

static inline int nc_rpc_server_queue_send(
    neverc_thread_channel_t *queue, neverc_context_t *context,
    nc_rpc_server_queue_mode_t mode, void *value) {
    const nc_rpc_server_queue_ops_t ops = {
        neverc_thread_channel_send_context,
        neverc_thread_channel_try_send,
        neverc_thread_channel_send
    };
    return nc_rpc_server_queue_send_with_ops(
        queue, context, mode, value, &ops);
}

#endif
