#ifndef NEVERC_NET_EVENT_LOOP_H
#define NEVERC_NET_EVENT_LOOP_H

#include "_net_poller.h"
#include "_net_socket.h"
#include "_net_thread.h"

#define NC_EVLOOP_MAX_EVENTS 256
#define NC_EVLOOP_INITIAL_PENDING 256
#define NC_EVLOOP_MAX_PENDING 65536

#define NC_EVLOOP_OK 0
#define NC_EVLOOP_CANCELLED 1
#define NC_EVLOOP_DEADLINE 2
#define NC_EVLOOP_ERROR (-1)

#ifndef NC_EVLOOP_CALLOC
#define NC_EVLOOP_CALLOC calloc
#endif

#ifndef NC_EVLOOP_REALLOC
#define NC_EVLOOP_REALLOC realloc
#endif

typedef void (*nc_evloop_cb_t)(void *data, int events);

typedef struct nc_evloop {
    nc_poller_t *poller;
    volatile int running;
    volatile int stop_requested;
    volatile int wakeup_pending;
    int wakeup_fds[2];
    char wakeup_marker;

    nc_task_t *pending;
    int pending_count;
    int pending_cap;
    nc_task_t *dispatch;
    int dispatch_cap;
    nc_mutex_t pending_lock;
} nc_evloop_t;

static inline void nc_evloop_destroy(nc_evloop_t *loop);

static inline nc_evloop_t *nc_evloop_create(void) {
    nc_evloop_t *loop =
        (nc_evloop_t *)NC_EVLOOP_CALLOC(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->wakeup_fds[0] = -1;
    loop->wakeup_fds[1] = -1;
    loop->pending_cap = NC_EVLOOP_INITIAL_PENDING;
    loop->dispatch_cap = NC_EVLOOP_INITIAL_PENDING;
    loop->pending = (nc_task_t *)NC_EVLOOP_CALLOC(
        (size_t)loop->pending_cap, sizeof(nc_task_t));
    loop->dispatch = (nc_task_t *)NC_EVLOOP_CALLOC(
        (size_t)loop->dispatch_cap, sizeof(nc_task_t));
    if (!loop->pending || !loop->dispatch) {
        free(loop->pending);
        free(loop->dispatch);
        free(loop);
        return NULL;
    }
    nc_mutex_init(&loop->pending_lock);

    loop->poller = nc_poller_create();
    if (!loop->poller) {
        nc_mutex_destroy(&loop->pending_lock);
        free(loop->pending);
        free(loop->dispatch);
        free(loop);
        return NULL;
    }

#ifndef _WIN32
    if (pipe(loop->wakeup_fds) != 0 ||
        nc_set_nonblocking(loop->wakeup_fds[0]) != 0 ||
        nc_set_nonblocking(loop->wakeup_fds[1]) != 0 ||
        nc_set_cloexec(loop->wakeup_fds[0]) != 0 ||
        nc_set_cloexec(loop->wakeup_fds[1]) != 0 ||
        nc_poller_add(loop->poller, loop->wakeup_fds[0], NC_EV_READ,
                      &loop->wakeup_marker) != 0) {
        nc_evloop_destroy(loop);
        return NULL;
    }
#endif
    return loop;
}

/*
 * The caller must stop and join the loop thread before destroying the loop.
 * Concurrent post/stop against destruction is not supported.
 */
static inline void nc_evloop_destroy(nc_evloop_t *loop) {
    if (!loop) return;
#ifndef _WIN32
    if (loop->wakeup_fds[0] >= 0) {
        if (loop->poller)
            nc_poller_del(loop->poller, loop->wakeup_fds[0]);
        close(loop->wakeup_fds[0]);
    }
    if (loop->wakeup_fds[1] >= 0)
        close(loop->wakeup_fds[1]);
#endif
    nc_poller_destroy(loop->poller);
    free(loop->pending);
    free(loop->dispatch);
    nc_mutex_destroy(&loop->pending_lock);
    free(loop);
}

/* The pending lock serializes wakeup delivery with the terminal drain. */
static inline int nc_evloop_wakeup_locked(nc_evloop_t *loop) {
    if (!loop || !loop->poller) return -1;
    if (!nc_atomic_cas(&loop->wakeup_pending, 0, 1))
        return 0;

#ifdef _WIN32
    if (!loop->poller->iocp ||
        !PostQueuedCompletionStatus(
            loop->poller->iocp, 0,
            (ULONG_PTR)&loop->wakeup_marker, NULL)) {
        nc_atomic_store(&loop->wakeup_pending, 0);
        return -1;
    }
#else
    if (loop->wakeup_fds[1] < 0) {
        nc_atomic_store(&loop->wakeup_pending, 0);
        return -1;
    }
    char byte = 'W';
    ssize_t written;
    do {
        written = write(loop->wakeup_fds[1], &byte, 1);
    } while (written < 0 && errno == EINTR);
    if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        nc_atomic_store(&loop->wakeup_pending, 0);
        return -1;
    }
#endif
    return 0;
}

static inline int nc_evloop_stop(nc_evloop_t *loop) {
    if (!loop) return -1;
    int result = 0;
    nc_mutex_lock(&loop->pending_lock);
    nc_atomic_store(&loop->stop_requested, 1);
    if (nc_atomic_load(&loop->running))
        result = nc_evloop_wakeup_locked(loop);
    nc_mutex_unlock(&loop->pending_lock);
    return result;
}

static inline int nc_evloop_post(nc_evloop_t *loop, nc_task_func_t func,
                                 void *arg) {
    if (!loop || !func) return -1;

    nc_mutex_lock(&loop->pending_lock);
    if (nc_atomic_load(&loop->stop_requested) ||
        loop->pending_count >= NC_EVLOOP_MAX_PENDING) {
        nc_mutex_unlock(&loop->pending_lock);
        return -1;
    }
    if (loop->pending_count >= loop->pending_cap) {
        int new_cap =
            loop->pending_cap > NC_EVLOOP_MAX_PENDING / 2
                ? NC_EVLOOP_MAX_PENDING
                : loop->pending_cap * 2;
        nc_task_t *tasks = (nc_task_t *)NC_EVLOOP_REALLOC(
            loop->pending, (size_t)new_cap * sizeof(nc_task_t));
        if (!tasks) {
            nc_mutex_unlock(&loop->pending_lock);
            return -1;
        }
        loop->pending = tasks;
        loop->pending_cap = new_cap;
    }
    loop->pending[loop->pending_count].func = func;
    loop->pending[loop->pending_count].arg = arg;
    loop->pending_count++;

    /*
     * Ownership transfers when the task is enqueued. Wakeup failure cannot
     * safely roll that transfer back because the loop may already dispatch it.
     * Keep the lock through delivery so loop shutdown cannot drain first and
     * then leave a late wakeup behind.
     */
    (void)nc_evloop_wakeup_locked(loop);
    nc_mutex_unlock(&loop->pending_lock);
    return 0;
}

static inline int nc_evloop_dispatch_pending(nc_evloop_t *loop) {
    nc_mutex_lock(&loop->pending_lock);
    nc_task_t *tasks = loop->pending;
    int count = loop->pending_count;
    int tasks_cap = loop->pending_cap;
    loop->pending = loop->dispatch;
    loop->pending_count = 0;
    loop->pending_cap = loop->dispatch_cap;
    loop->dispatch = tasks;
    loop->dispatch_cap = tasks_cap;
    nc_mutex_unlock(&loop->pending_lock);

    for (int i = 0; i < count; i++)
        tasks[i].func(tasks[i].arg);
    return count;
}

static inline void nc_evloop_drain_wakeup(nc_evloop_t *loop) {
#ifndef _WIN32
    char bytes[256];
    for (;;) {
        ssize_t count = read(loop->wakeup_fds[0], bytes, sizeof(bytes));
        if (count > 0) continue;
        if (count < 0 && errno == EINTR) continue;
        break;
    }
#else
    (void)loop;
#endif
    nc_atomic_store(&loop->wakeup_pending, 0);
}

static inline void nc_evloop_finish_run(nc_evloop_t *loop) {
    /*
     * A task accepted before stop owns its callback and must be dispatched.
     * Once stop_requested is visible, post rejects new work, so draining the
     * queue outside the lock is safe and cannot race another accepted post.
     */
    for (;;) {
        nc_mutex_lock(&loop->pending_lock);
        if (nc_atomic_load(&loop->stop_requested) &&
            loop->pending_count > 0) {
            nc_mutex_unlock(&loop->pending_lock);
            nc_evloop_dispatch_pending(loop);
            continue;
        }

        /*
         * Stop/post hold pending_lock while delivering wakeups. Clear the
         * signal and publish running=0 together, so no producer can write
         * after the final drain and an inactive stop remains quiescent.
         */
        nc_evloop_drain_wakeup(loop);
        nc_atomic_store(&loop->running, 0);

        /*
         * A deadline/error exit may race a successful post. Preserve its wake
         * for a later run instead of draining the signal and stranding work.
         */
        if (!nc_atomic_load(&loop->stop_requested) &&
            loop->pending_count > 0)
            (void)nc_evloop_wakeup_locked(loop);
        nc_mutex_unlock(&loop->pending_lock);
        return;
    }
}

typedef void (*nc_evloop_event_handler_t)(nc_evloop_t *loop,
                                          nc_event_t *event);

static inline int nc_evloop_run_until(
    nc_evloop_t *loop, nc_evloop_event_handler_t handler,
    uint64_t deadline_ms) {
    if (!loop || !loop->poller ||
        !nc_atomic_cas(&loop->running, 0, 1))
        return NC_EVLOOP_ERROR;

    int result = NC_EVLOOP_OK;
    nc_event_t events[NC_EVLOOP_MAX_EVENTS];

    for (;;) {
        if (nc_atomic_load(&loop->stop_requested)) {
            nc_evloop_dispatch_pending(loop);
            result = NC_EVLOOP_CANCELLED;
            break;
        }

        int timeout_ms = -1;
        if (deadline_ms != 0) {
            uint64_t now = nc_monotonic_ms();
            if (now >= deadline_ms) {
                result = NC_EVLOOP_DEADLINE;
                break;
            }
            uint64_t remaining = deadline_ms - now;
            timeout_ms =
                remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
        }

        int count = nc_poller_wait(
            loop->poller, events, NC_EVLOOP_MAX_EVENTS, timeout_ms);
        if (count < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            result = NC_EVLOOP_ERROR;
            break;
        }
        if (count == 0 && deadline_ms != 0 &&
            nc_monotonic_ms() >= deadline_ms) {
            result = NC_EVLOOP_DEADLINE;
            break;
        }

        for (int i = 0; i < count; i++) {
            if (events[i].data == &loop->wakeup_marker) {
                nc_evloop_drain_wakeup(loop);
                events[i].data = NULL;
            }
        }

        nc_evloop_dispatch_pending(loop);
        if (nc_atomic_load(&loop->stop_requested)) {
            result = NC_EVLOOP_CANCELLED;
            break;
        }

        if (handler) {
            for (int i = 0; i < count; i++) {
                if (!events[i].data) continue;
                handler(loop, &events[i]);
                if (nc_atomic_load(&loop->stop_requested)) {
                    result = NC_EVLOOP_CANCELLED;
                    break;
                }
            }
            if (result == NC_EVLOOP_CANCELLED)
                break;
        }
    }

    nc_evloop_finish_run(loop);
    return result;
}

static inline int nc_evloop_run(nc_evloop_t *loop,
                                nc_evloop_event_handler_t handler) {
    return nc_evloop_run_until(loop, handler, 0);
}

static inline int nc_evloop_run_for(nc_evloop_t *loop,
                                    nc_evloop_event_handler_t handler,
                                    uint32_t timeout_ms) {
    uint64_t now = nc_monotonic_ms();
    uint64_t deadline = now + (uint64_t)timeout_ms;
    if (deadline < now) deadline = UINT64_MAX;
    return nc_evloop_run_until(loop, handler, deadline);
}

#endif /* NEVERC_NET_EVENT_LOOP_H */
