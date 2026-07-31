#include "_net_buffer.h"
#include "_net_event_loop.h"
#include "_net_io_uring.h"
#include "_net_poller.h"
#include "_net_socket.h"
#include "_net_thread.h"
#include "_net_timer.h"

#include <stdio.h>

static volatile int task_ran;

static void run_task(void *arg) {
    int *value = (int *)arg;
    task_ran = *value;
}

int main(void) {
    nc_buf_t buf;
    nc_buf_init(&buf);
    if (nc_buf_append(&buf, "ok", 2) != 0 || buf.len != 2) {
        nc_buf_free(&buf);
        return 1;
    }
    nc_buf_free(&buf);

    nc_timer_wheel_t wheel;
    nc_timer_t timer;
    nc_tw_init(&wheel);
    nc_timer_init(&timer, NULL, NULL);
    nc_tw_add(&wheel, &timer, 1000);
    if (!timer.active) return 2;
    nc_tw_cancel(&wheel, &timer);
    if (timer.active) return 3;

    char host[32];
    uint16_t port = 0;
    if (nc_parse_addr("127.0.0.1:80", host, sizeof(host), &port) != 0 ||
        port != 80)
        return 4;

    nc_poller_t *poller = nc_poller_create();
    if (!poller) return 5;
    nc_poller_destroy(poller);

    nc_evloop_t *loop = nc_evloop_create();
    if (!loop) return 6;
    if (nc_evloop_stop(loop) != 0 ||
        nc_evloop_run(loop, NULL) != NC_EVLOOP_CANCELLED) {
        nc_evloop_destroy(loop);
        return 7;
    }
    nc_evloop_destroy(loop);

    nc_threadpool_t *pool = nc_threadpool_create(1);
    if (!pool) return 8;
    int expected = 7;
    task_ran = 0;
    if (nc_threadpool_submit(pool, run_task, &expected) != 0) {
        nc_threadpool_destroy(pool);
        return 9;
    }
    nc_threadpool_destroy(pool);
    if (task_ran != expected) return 10;

    puts("passed");
    return 0;
}
