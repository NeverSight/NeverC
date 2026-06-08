/*
 * NeverC net internals test suite.
 *
 * Tests: Timer Wheel, SO_REUSEPORT, Connection State Machine,
 *        Buffer Pool, Connection Limiter, Graceful Shutdown.
 *
 * Cross-platform: POSIX + Windows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "neverc/std/net/tcp.h"

/* Pull in _net_internal.h via tcp.c compilation — we test the internal APIs
 * by including the internal header directly. */
#include "../../std/src/net/_net_internal.h"

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/* ===== Timer Wheel Tests ===== */

static int tw_fire_count = 0;
static void *tw_fire_data = NULL;

static void tw_callback(nc_timer_t *timer, void *data) {
    (void)timer;
    tw_fire_count++;
    tw_fire_data = data;
}

static void test_timer_wheel_basic(void) {
    printf("[timer_wheel_basic]\n");

    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t t1;
    nc_timer_init(&t1, tw_callback, (void *)0x42);

    tw_fire_count = 0;
    tw_fire_data = NULL;

    nc_tw_add(&tw, &t1, 0);
    check_int("timer active", t1.active, 1);

    tw.last_ms = nc_monotonic_ms() - 2;
    nc_tw_tick(&tw);
    check_int("timer fired", tw_fire_count, 1);
    check_true("timer data correct", tw_fire_data == (void *)0x42);
    check_int("timer inactive after fire", t1.active, 0);
}

static void test_timer_wheel_cancel(void) {
    printf("[timer_wheel_cancel]\n");

    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t t1;
    nc_timer_init(&t1, tw_callback, NULL);

    tw_fire_count = 0;

    nc_tw_add(&tw, &t1, 10);
    check_int("timer active", t1.active, 1);

    nc_tw_cancel(&tw, &t1);
    check_int("timer canceled", t1.active, 0);

    tw.last_ms = nc_monotonic_ms() - 15;
    nc_tw_tick(&tw);
    check_int("canceled timer did not fire", tw_fire_count, 0);
}

static int tw_multi_count = 0;
static void tw_multi_cb(nc_timer_t *timer, void *data) {
    (void)timer; (void)data;
    tw_multi_count++;
}

static void test_timer_wheel_multiple(void) {
    printf("[timer_wheel_multiple]\n");

    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    #define N_TIMERS 100
    nc_timer_t timers[N_TIMERS];
    tw_multi_count = 0;

    for (int i = 0; i < N_TIMERS; i++) {
        nc_timer_init(&timers[i], tw_multi_cb, NULL);
        nc_tw_add(&tw, &timers[i], 0);
    }

    tw.last_ms = nc_monotonic_ms() - 2;
    nc_tw_tick(&tw);
    check_int("all timers fired", tw_multi_count, N_TIMERS);
    #undef N_TIMERS
}

static void test_timer_wheel_reschedule(void) {
    printf("[timer_wheel_reschedule]\n");

    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t t1;
    nc_timer_init(&t1, tw_callback, NULL);
    tw_fire_count = 0;

    nc_tw_add(&tw, &t1, 5);
    nc_tw_add(&tw, &t1, 10);
    check_int("timer still active", t1.active, 1);

    tw.last_ms = nc_monotonic_ms() - 6;
    nc_tw_tick(&tw);
    check_int("reschedule: not fired at old time", tw_fire_count, 0);
}

/* ===== Buffer Pool Tests ===== */

static void test_bufpool(void) {
    printf("[bufpool]\n");

    nc_bufpool_t pool;
    nc_bufpool_init(&pool, 128);

    void *b1 = nc_bufpool_pop(&pool);
    check_true("pop returns non-null", b1 != NULL);

    void *b2 = nc_bufpool_pop(&pool);
    check_true("pop again returns non-null", b2 != NULL);

    nc_bufpool_push(&pool, b1);
    nc_bufpool_push(&pool, b2);

    void *b3 = nc_bufpool_pop(&pool);
    check_true("recycled pop non-null", b3 != NULL);

    nc_bufpool_push(&pool, b3);
    nc_bufpool_destroy(&pool);
}

static void test_bufpool_stress(void) {
    printf("[bufpool_stress]\n");

    nc_bufpool_t pool;
    nc_bufpool_init(&pool, 64);

    #define BPT_N 1000
    void *ptrs[BPT_N];

    for (int i = 0; i < BPT_N; i++)
        ptrs[i] = nc_bufpool_pop(&pool);

    int all_valid = 1;
    for (int i = 0; i < BPT_N; i++) {
        if (!ptrs[i]) { all_valid = 0; break; }
    }
    check_true("all 1000 pops succeeded", all_valid);

    for (int i = 0; i < BPT_N; i++)
        nc_bufpool_push(&pool, ptrs[i]);

    nc_bufpool_destroy(&pool);
    #undef BPT_N
}

/* ===== Connection Limiter Tests ===== */

static void test_conn_limiter(void) {
    printf("[conn_limiter]\n");

    nc_conn_limiter_t lim;
    nc_conn_limiter_init(&lim, 3);

    check_int("acquire 1", nc_conn_limiter_try_acquire(&lim), 1);
    check_int("acquire 2", nc_conn_limiter_try_acquire(&lim), 1);
    check_int("acquire 3", nc_conn_limiter_try_acquire(&lim), 1);
    check_int("acquire 4 rejected", nc_conn_limiter_try_acquire(&lim), 0);
    check_int("count=3", nc_conn_limiter_count(&lim), 3);

    nc_conn_limiter_release(&lim);
    check_int("count after release", nc_conn_limiter_count(&lim), 2);
    check_int("acquire after release", nc_conn_limiter_try_acquire(&lim), 1);
}

static void test_conn_limiter_unlimited(void) {
    printf("[conn_limiter_unlimited]\n");

    nc_conn_limiter_t lim;
    nc_conn_limiter_init(&lim, 0);

    for (int i = 0; i < 10000; i++) {
        check_true("unlimited acquire", nc_conn_limiter_try_acquire(&lim) == 1);
    }
}

/* ===== Dynamic Buffer Tests ===== */

static void test_buf(void) {
    printf("[dynamic_buf]\n");

    nc_buf_t buf;
    nc_buf_init(&buf);

    nc_buf_append(&buf, "hello", 5);
    check_int("buf len=5", (int)buf.len, 5);
    check_true("buf data correct", memcmp(buf.data, "hello", 5) == 0);

    nc_buf_append(&buf, " world", 6);
    check_int("buf len=11", (int)buf.len, 11);
    check_true("buf appended", memcmp(buf.data, "hello world", 11) == 0);

    nc_buf_consume(&buf, 6);
    check_int("buf after consume len=5", (int)buf.len, 5);
    check_true("buf consumed", memcmp(buf.data, "world", 5) == 0);

    nc_buf_reset(&buf);
    check_int("buf reset len=0", (int)buf.len, 0);

    nc_buf_free(&buf);
}

static void test_buf_grow(void) {
    printf("[buf_grow]\n");

    nc_buf_t buf;
    nc_buf_init(&buf);

    for (int i = 0; i < 10000; i++) {
        nc_buf_append(&buf, "AAAA", 4);
    }
    check_int("large buf len", (int)buf.len, 40000);
    check_true("buf cap >= len", buf.cap >= buf.len);

    nc_buf_free(&buf);
}

/* ===== Connection Context Tests ===== */

static void test_conn_ctx(void) {
    printf("[conn_ctx]\n");

    nc_conn_ctx_t ctx;
    nc_conn_ctx_init(&ctx, NC_INVALID_SOCK, 100);

    check_int("initial state idle", ctx.state, NC_CONN_IDLE);
    check_int("max requests", ctx.max_requests, 100);
    check_int("requests served=0", ctx.requests_served, 0);
    check_true("created_ms > 0", ctx.created_ms > 0);
    check_int("not max reached", nc_conn_ctx_max_reached(&ctx), 0);

    ctx.requests_served = 100;
    check_int("max reached", nc_conn_ctx_max_reached(&ctx), 1);

    nc_conn_ctx_touch(&ctx);
    check_true("last_active updated", ctx.last_active_ms >= ctx.created_ms);
    check_int("not expired 60s", nc_conn_ctx_expired(&ctx, 60000), 0);

    nc_buf_free(&ctx.read_buf);
    nc_buf_free(&ctx.write_buf);
}

/* ===== Graceful Shutdown Controller Tests ===== */

static void test_shutdown_ctl(void) {
    printf("[shutdown_ctl]\n");

    nc_shutdown_ctl_t ctl;
    nc_shutdown_init(&ctl);

    check_int("should accept initially", nc_shutdown_should_accept(&ctl), 1);
    check_int("not draining initially", nc_shutdown_is_draining(&ctl), 0);

    nc_shutdown_conn_add(&ctl);
    nc_shutdown_conn_add(&ctl);

    nc_shutdown_begin(&ctl, 5000);
    check_int("stop accepting", nc_shutdown_should_accept(&ctl), 0);
    check_int("is draining", nc_shutdown_is_draining(&ctl), 1);
    check_int("not complete (2 conns)", nc_shutdown_complete(&ctl), 0);
    check_int("deadline not reached", nc_shutdown_deadline_reached(&ctl), 0);

    nc_shutdown_conn_remove(&ctl);
    nc_shutdown_conn_remove(&ctl);
    check_int("complete (0 conns)", nc_shutdown_complete(&ctl), 1);
}

/* ===== SO_REUSEPORT Tests ===== */

#ifndef _WIN32
static void test_reuseport(void) {
    printf("[reuseport]\n");

    nc_reuseport_group_t g;
    int rc = nc_reuseport_init(&g, 4);
    check_int("init ok", rc, 0);

    rc = nc_reuseport_listen(&g, "", 0, 128);
#if defined(SO_REUSEPORT)
    check_int("first listen ok", rc, 0);
    check_int("count=1", g.count, 1);
#else
    if (rc == 0) {
        check_int("listen ok (no reuseport)", rc, 0);
    }
#endif

    nc_reuseport_close(&g);
    check_int("after close count=0", g.count, 0);
}
#endif

/* ===== Address Parsing Tests ===== */

static void test_parse_addr(void) {
    printf("[parse_addr]\n");

    char host[256];
    uint16_t port;

    check_int("parse :8080", nc_parse_addr(":8080", host, sizeof(host), &port), 0);
    check_int("port 8080", port, 8080);
    check_true("empty host", host[0] == '\0');

    check_int("parse localhost:3000",
              nc_parse_addr("127.0.0.1:3000", host, sizeof(host), &port), 0);
    check_true("host=127.0.0.1", strcmp(host, "127.0.0.1") == 0);
    check_int("port 3000", port, 3000);

    check_int("parse [::1]:443",
              nc_parse_addr("[::1]:443", host, sizeof(host), &port), 0);
    check_true("host=::1", strcmp(host, "::1") == 0);
    check_int("port 443", port, 443);

    check_int("parse null", nc_parse_addr(NULL, host, sizeof(host), &port), -1);
    check_int("parse empty", nc_parse_addr("", host, sizeof(host), &port), -1);
    check_int("parse no colon", nc_parse_addr("localhost", host, sizeof(host), &port), -1);
}

/* ===== Non-blocking / Socket Options Tests ===== */

#ifndef _WIN32
static void test_socket_helpers(void) {
    printf("[socket_helpers]\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    check_true("socket created", fd >= 0);

    check_int("set nonblocking", nc_set_nonblocking(fd), 0);
    check_int("set blocking", nc_set_blocking(fd), 0);
    check_int("set reuseaddr", nc_set_reuseaddr(fd), 0);
    check_int("set nodelay", nc_set_nodelay(fd), 0);
    check_int("set keepalive", nc_set_keepalive(fd), 0);
    check_int("set cork", nc_set_cork(fd, 1), 0);
    check_int("unset cork", nc_set_cork(fd, 0), 0);

    close(fd);
}
#endif

/* ===== Poller Tests ===== */

#ifndef _WIN32
static void test_poller(void) {
    printf("[poller]\n");

    nc_poller_t *p = nc_poller_create();
    check_true("poller created", p != NULL);

    int fds[2];
    int rc = pipe(fds);
    check_int("pipe created", rc, 0);

    nc_set_nonblocking(fds[0]);
    nc_set_nonblocking(fds[1]);

    rc = nc_poller_add(p, fds[0], NC_EV_READ, (void *)0x1234);
    check_int("poller add", rc, 0);

    write(fds[1], "x", 1);

    nc_event_t events[16];
    int n = nc_poller_wait(p, events, 16, 100);
    check_true("poller got event", n >= 1);
    if (n >= 1) {
        check_true("event has read", (events[0].events & NC_EV_READ) != 0);
    }

    nc_poller_del(p, fds[0]);
    close(fds[0]);
    close(fds[1]);
    nc_poller_destroy(p);
}
#endif

/* ===== Event Loop Tests ===== */

static volatile int evloop_test_done = 0;

static void evloop_stop_task(void *arg) {
    nc_evloop_t *loop = (nc_evloop_t *)arg;
    evloop_test_done = 1;
    nc_evloop_stop(loop);
}

#ifndef _WIN32
static void *evloop_thread(void *arg) {
    nc_evloop_t *loop = (nc_evloop_t *)arg;
    nc_evloop_run(loop, NULL);
    return NULL;
}

static void test_evloop(void) {
    printf("[evloop]\n");

    nc_evloop_t *loop = nc_evloop_create();
    check_true("evloop created", loop != NULL);

    evloop_test_done = 0;

    pthread_t th;
    pthread_create(&th, NULL, evloop_thread, loop);

    usleep(50000);
    nc_evloop_post(loop, evloop_stop_task, loop);

    pthread_join(th, NULL);
    check_int("evloop task executed", evloop_test_done, 1);

    nc_evloop_destroy(loop);
}
#endif

/* ===== Thread Pool Tests ===== */

static volatile int tp_counter = 0;

static void tp_increment(void *arg) {
    (void)arg;
    nc_atomic_inc(&tp_counter);
}

static void test_threadpool(void) {
    printf("[threadpool]\n");

    nc_threadpool_t *pool = nc_threadpool_create(4);
    check_true("pool created", pool != NULL);

    tp_counter = 0;
    for (int i = 0; i < 100; i++) {
        nc_threadpool_submit(pool, tp_increment, NULL);
    }

    nc_threadpool_destroy(pool);
    check_int("all tasks completed", tp_counter, 100);
}

/* ===== Main ===== */

int main(void) {
    printf("=== NeverC net internals tests ===\n");

    test_timer_wheel_basic();
    test_timer_wheel_cancel();
    test_timer_wheel_multiple();
    test_timer_wheel_reschedule();
    test_bufpool();
    test_bufpool_stress();
    test_conn_limiter();
    test_conn_limiter_unlimited();
    test_buf();
    test_buf_grow();
    test_conn_ctx();
    test_shutdown_ctl();
    test_parse_addr();
    test_threadpool();

#ifndef _WIN32
    test_reuseport();
    test_socket_helpers();
    test_poller();
    test_evloop();
#endif

    printf("\n--- net/internals: %d/%d passed ---\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("FAILED (%d failures)\n", tests_failed);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
