#ifndef NEVERC_NET_RPC_CLIENT_EPOCH_H
#define NEVERC_NET_RPC_CLIENT_EPOCH_H

#include "neverc/std/thread.h"
#include "../_net_platform.h"

#include <stddef.h>
#include <stdint.h>

/* A lease binds one outbound operation to a queue and generation. Retirement
 * first stops new leases, closes the queue to wake blocked senders, then waits
 * for every lease before clearing and freeing the queue. */
typedef struct {
    nc_mutex_t lock;
    nc_cond_t idle;
    neverc_thread_channel_t *send_queue;
    uint64_t generation;
    size_t leases;
    size_t waiters;
    int accepting;
} nc_rpc_client_epoch_t;

typedef struct {
    nc_rpc_client_epoch_t *epoch;
    neverc_thread_channel_t *send_queue;
    uint64_t generation;
    int held;
} nc_rpc_client_epoch_lease_t;

static inline void nc_rpc_client_epoch_init(
    nc_rpc_client_epoch_t *epoch) {
    nc_mutex_init(&epoch->lock);
    nc_cond_init(&epoch->idle);
    epoch->send_queue = NULL;
    epoch->generation = 0;
    epoch->leases = 0;
    epoch->waiters = 0;
    epoch->accepting = 0;
}

static inline int nc_rpc_client_epoch_prepare(
    nc_rpc_client_epoch_t *epoch, neverc_thread_channel_t *send_queue,
    uint64_t generation) {
    if (!epoch || !send_queue || generation == 0) return -1;
    nc_mutex_lock(&epoch->lock);
    if (epoch->send_queue || epoch->leases != 0 ||
        epoch->waiters != 0 || epoch->accepting) {
        nc_mutex_unlock(&epoch->lock);
        return -1;
    }
    epoch->send_queue = send_queue;
    epoch->generation = generation;
    epoch->accepting = 1;
    nc_mutex_unlock(&epoch->lock);
    return 0;
}

static inline int nc_rpc_client_epoch_pin(
    nc_rpc_client_epoch_t *epoch, uint64_t expected_generation,
    nc_rpc_client_epoch_lease_t *lease) {
    if (!epoch || !lease) return -1;
    lease->epoch = NULL;
    lease->send_queue = NULL;
    lease->generation = 0;
    lease->held = 0;
    nc_mutex_lock(&epoch->lock);
    if (!epoch->accepting || !epoch->send_queue ||
        epoch->leases == SIZE_MAX ||
        (expected_generation != 0 &&
         epoch->generation != expected_generation)) {
        nc_mutex_unlock(&epoch->lock);
        return -1;
    }
    epoch->leases++;
    lease->epoch = epoch;
    lease->send_queue = epoch->send_queue;
    lease->generation = epoch->generation;
    lease->held = 1;
    nc_mutex_unlock(&epoch->lock);
    return 0;
}

static inline void nc_rpc_client_epoch_unpin(
    nc_rpc_client_epoch_lease_t *lease) {
    if (!lease || !lease->held || !lease->epoch) return;
    nc_rpc_client_epoch_t *epoch = lease->epoch;
    nc_mutex_lock(&epoch->lock);
    if (epoch->leases > 0) {
        epoch->leases--;
        if (epoch->leases == 0)
            nc_cond_broadcast(&epoch->idle);
    }
    nc_mutex_unlock(&epoch->lock);
    lease->epoch = NULL;
    lease->send_queue = NULL;
    lease->generation = 0;
    lease->held = 0;
}

static inline neverc_thread_channel_t *nc_rpc_client_epoch_stop(
    nc_rpc_client_epoch_t *epoch) {
    if (!epoch) return NULL;
    nc_mutex_lock(&epoch->lock);
    epoch->accepting = 0;
    neverc_thread_channel_t *send_queue = epoch->send_queue;
    nc_mutex_unlock(&epoch->lock);
    return send_queue;
}

static inline int nc_rpc_client_epoch_is_active(
    nc_rpc_client_epoch_t *epoch) {
    if (!epoch) return 0;
    nc_mutex_lock(&epoch->lock);
    int active = epoch->accepting && epoch->send_queue;
    nc_mutex_unlock(&epoch->lock);
    return active;
}

static inline void nc_rpc_client_epoch_wait_idle(
    nc_rpc_client_epoch_t *epoch) {
    if (!epoch) return;
    nc_mutex_lock(&epoch->lock);
    if (epoch->leases != 0) {
        epoch->waiters++;
        nc_cond_broadcast(&epoch->idle);
        while (epoch->leases != 0)
            (void)nc_cond_wait(&epoch->idle, &epoch->lock);
        epoch->waiters--;
        nc_cond_broadcast(&epoch->idle);
    }
    nc_mutex_unlock(&epoch->lock);
}

static inline int nc_rpc_client_epoch_clear(
    nc_rpc_client_epoch_t *epoch,
    neverc_thread_channel_t **send_queue) {
    if (send_queue) *send_queue = NULL;
    if (!epoch) return -1;
    nc_mutex_lock(&epoch->lock);
    if (epoch->accepting || epoch->leases != 0 ||
        epoch->waiters != 0) {
        nc_mutex_unlock(&epoch->lock);
        return -1;
    }
    if (send_queue) *send_queue = epoch->send_queue;
    epoch->send_queue = NULL;
    epoch->generation = 0;
    nc_mutex_unlock(&epoch->lock);
    return 0;
}

static inline int nc_rpc_client_epoch_destroy(
    nc_rpc_client_epoch_t *epoch) {
    if (!epoch) return -1;
    nc_mutex_lock(&epoch->lock);
    int clear = !epoch->accepting && !epoch->send_queue &&
                epoch->generation == 0 && epoch->leases == 0 &&
                epoch->waiters == 0;
    nc_mutex_unlock(&epoch->lock);
    if (!clear) return -1;
    nc_cond_destroy(&epoch->idle);
    nc_mutex_destroy(&epoch->lock);
    return 0;
}

#endif
