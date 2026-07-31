#ifndef NEVERC_NET_INTERNAL_H
#define NEVERC_NET_INTERNAL_H

/*
 * Compatibility umbrella for NeverC network internals. New code should
 * include the narrow module it uses.
 */
#include "_net_platform.h"
#include "_net_socket.h"
#include "_net_buffer.h"
#include "_net_thread.h"
#include "_net_timer.h"
#include "_net_io_uring.h"
#include "_net_poller.h"
#include "_net_event_loop.h"

typedef struct {
    volatile int current;
    int max_conns;
} nc_conn_limiter_t;

static inline void nc_conn_limiter_init(nc_conn_limiter_t *limiter,
                                        int max_connections) {
    limiter->current = 0;
    limiter->max_conns = max_connections;
}

static inline int nc_conn_limiter_try_acquire(
    nc_conn_limiter_t *limiter) {
    if (limiter->max_conns <= 0) {
        nc_atomic_inc(&limiter->current);
        return 1;
    }
    for (;;) {
        int current = nc_atomic_load(&limiter->current);
        if (current >= limiter->max_conns) return 0;
        if (nc_atomic_cas(&limiter->current, current, current + 1))
            return 1;
    }
}

static inline void nc_conn_limiter_release(
    nc_conn_limiter_t *limiter) {
    nc_atomic_dec(&limiter->current);
}

static inline int nc_conn_limiter_count(
    nc_conn_limiter_t *limiter) {
    return nc_atomic_load(&limiter->current);
}

typedef enum {
    NC_CONN_IDLE,
    NC_CONN_READING,
    NC_CONN_PROCESSING,
    NC_CONN_WRITING,
    NC_CONN_CLOSING
} nc_conn_state_t;

typedef struct {
    nc_sock_t fd;
    nc_conn_state_t state;
    int max_requests;
    int requests_served;
    uint64_t created_ms;
    uint64_t last_active_ms;
    nc_buf_t read_buf;
    nc_buf_t write_buf;
} nc_conn_ctx_t;

static inline void nc_conn_ctx_init(nc_conn_ctx_t *ctx, nc_sock_t fd,
                                    int max_requests) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = fd;
    ctx->state = NC_CONN_IDLE;
    ctx->max_requests = max_requests;
    ctx->created_ms = nc_monotonic_ms();
    ctx->last_active_ms = ctx->created_ms;
    nc_buf_init(&ctx->read_buf);
    nc_buf_init(&ctx->write_buf);
}

static inline void nc_conn_ctx_touch(nc_conn_ctx_t *ctx) {
    ctx->last_active_ms = nc_monotonic_ms();
}

static inline int nc_conn_ctx_expired(nc_conn_ctx_t *ctx,
                                      int timeout_ms) {
    if (timeout_ms <= 0) return 0;
    return nc_monotonic_ms() - ctx->last_active_ms >
           (uint64_t)timeout_ms;
}

static inline int nc_conn_ctx_max_reached(nc_conn_ctx_t *ctx) {
    return ctx->max_requests > 0 &&
           ctx->requests_served >= ctx->max_requests;
}

typedef struct {
    volatile int draining;
    volatile int active_conns;
    uint64_t deadline_ms;
} nc_shutdown_ctl_t;

static inline void nc_shutdown_init(nc_shutdown_ctl_t *controller) {
    memset(controller, 0, sizeof(*controller));
}

static inline int nc_shutdown_should_accept(
    nc_shutdown_ctl_t *controller) {
    return !nc_atomic_load(&controller->draining);
}

static inline int nc_shutdown_is_draining(
    nc_shutdown_ctl_t *controller) {
    return nc_atomic_load(&controller->draining);
}

static inline void nc_shutdown_begin(nc_shutdown_ctl_t *controller,
                                     int timeout_ms) {
    nc_atomic_store(&controller->draining, 1);
    uint64_t now = nc_monotonic_ms();
    controller->deadline_ms =
        timeout_ms > 0 ? now + (uint64_t)timeout_ms : now;
}

static inline void nc_shutdown_conn_add(
    nc_shutdown_ctl_t *controller) {
    nc_atomic_inc(&controller->active_conns);
}

static inline void nc_shutdown_conn_remove(
    nc_shutdown_ctl_t *controller) {
    nc_atomic_dec(&controller->active_conns);
}

static inline int nc_shutdown_complete(
    nc_shutdown_ctl_t *controller) {
    return nc_atomic_load(&controller->active_conns) <= 0;
}

static inline int nc_shutdown_deadline_reached(
    nc_shutdown_ctl_t *controller) {
    return nc_monotonic_ms() >= controller->deadline_ms;
}

#endif /* NEVERC_NET_INTERNAL_H */
