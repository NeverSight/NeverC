#include <stddef.h>
#include <stdlib.h>

static int allocation_count;
static int calloc_fail_at;
static int realloc_should_fail;

static void *fault_calloc(size_t count, size_t size) {
    allocation_count++;
    if (calloc_fail_at > 0 && allocation_count == calloc_fail_at)
        return NULL;
    return calloc(count, size);
}

static void *fault_realloc(void *ptr, size_t size) {
    if (realloc_should_fail) return NULL;
    return realloc(ptr, size);
}

#define NC_POLLER_CALLOC fault_calloc
#define NC_EVLOOP_CALLOC fault_calloc
#define NC_EVLOOP_REALLOC fault_realloc
#include "_net_event_loop.h"

#include <stdio.h>

static int task_count;

static void count_task(void *arg) {
    (void)arg;
    task_count++;
}

int main(void) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    const int create_allocations = 7;
#elif defined(NC_USE_KQUEUE)
    const int create_allocations = 7;
#elif defined(NC_USE_EPOLL) || defined(NC_USE_IOCP)
    const int create_allocations = 6;
#else
    const int create_allocations = 6;
#endif
    for (int fail_at = 1; fail_at <= create_allocations; fail_at++) {
        allocation_count = 0;
        calloc_fail_at = fail_at;
        nc_evloop_t *loop = nc_evloop_create();
        if (loop) {
            nc_evloop_destroy(loop);
            return fail_at;
        }
    }

    allocation_count = 0;
    calloc_fail_at = 1;
    nc_poller_t *poller = nc_poller_create();
    if (poller) {
        nc_poller_destroy(poller);
        return 5;
    }

    allocation_count = 0;
    calloc_fail_at = 0;
    nc_evloop_t *loop = nc_evloop_create();
    if (!loop) return 6;

    calloc_fail_at = allocation_count + 1;
    int grow_rejected = 0;
#if defined(NC_USE_KQUEUE) || defined(NC_USE_EPOLL)
    grow_rejected = nc_poller_add(loop->poller, 64, NC_EV_READ, NULL) != 0;
#elif !(defined(NC_USE_IO_URING) && NC_USE_IO_URING)
    for (int fd = 32; fd < 256; fd++) {
        if (nc_poller_add(loop->poller, fd, NC_EV_READ, NULL) != 0) {
            grow_rejected = 1;
            break;
        }
    }
#else
    grow_rejected = 1;
#endif
    calloc_fail_at = 0;
    if (!grow_rejected) {
        nc_evloop_destroy(loop);
        return 10;
    }

    task_count = 0;
    for (int i = 0; i < NC_EVLOOP_INITIAL_PENDING; i++) {
        if (nc_evloop_post(loop, count_task, NULL) != 0) {
            nc_evloop_destroy(loop);
            return 7;
        }
    }

    realloc_should_fail = 1;
    if (nc_evloop_post(loop, count_task, NULL) == 0 ||
        loop->pending_count != NC_EVLOOP_INITIAL_PENDING) {
        nc_evloop_destroy(loop);
        return 8;
    }
    realloc_should_fail = 0;

    if (nc_evloop_stop(loop) != 0 ||
        nc_evloop_run(loop, NULL) != NC_EVLOOP_CANCELLED ||
        task_count != NC_EVLOOP_INITIAL_PENDING ||
        nc_evloop_post(loop, count_task, NULL) == 0) {
        nc_evloop_destroy(loop);
        return 9;
    }

    nc_evloop_destroy(loop);
    puts("passed");
    return 0;
}
