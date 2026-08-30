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

/* Bind only the timer-wheel inline functions in this translation unit to a
 * deterministic clock. The remaining network internals keep the platform
 * monotonic clock when _net_internal.h is parsed below. */
#include "../../std/src/net/_net_platform.h"
static uint64_t tw_fake_now_ms;
#define nc_monotonic_ms() tw_fake_now_ms
#include "../../std/src/net/_net_timer.h"
#undef nc_monotonic_ms

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

    tw_fake_now_ms = 1000;
    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t t1;
    nc_timer_init(&t1, tw_callback, (void *)0x42);

    tw_fire_count = 0;
    tw_fire_data = NULL;

    nc_tw_add(&tw, &t1, 0);
    check_int("timer active", t1.active, 1);

    tw_fake_now_ms = 1001;
    nc_tw_tick(&tw);
    check_int("timer fired", tw_fire_count, 1);
    check_true("timer data correct", tw_fire_data == (void *)0x42);
    check_int("timer inactive after fire", t1.active, 0);
}

static void test_timer_wheel_cancel(void) {
    printf("[timer_wheel_cancel]\n");

    tw_fake_now_ms = 2000;
    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t t1;
    nc_timer_init(&t1, tw_callback, NULL);

    tw_fire_count = 0;

    nc_tw_add(&tw, &t1, 10);
    check_int("timer active", t1.active, 1);

    nc_tw_cancel(&tw, &t1);
    check_int("timer canceled", t1.active, 0);

    tw_fake_now_ms = 2015;
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

    tw_fake_now_ms = 3000;
    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    #define N_TIMERS 100
    nc_timer_t timers[N_TIMERS];
    tw_multi_count = 0;

    for (int i = 0; i < N_TIMERS; i++) {
        nc_timer_init(&timers[i], tw_multi_cb, NULL);
        nc_tw_add(&tw, &timers[i], 0);
    }

    tw_fake_now_ms = 3001;
    nc_tw_tick(&tw);
    check_int("all timers fired", tw_multi_count, N_TIMERS);
    #undef N_TIMERS
}

static void test_timer_wheel_deadline_saturates(void) {
    printf("[timer_wheel_deadline_saturates]\n");

    tw_fake_now_ms = UINT64_MAX - 2;
    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t timer;
    nc_timer_init(&timer, tw_callback, NULL);
    tw_fire_count = 0;

    nc_tw_add(&tw, &timer, 4);
    check_true("overflowing deadline saturates",
               timer.expire_ms == UINT64_MAX);

    tw_fake_now_ms = UINT64_MAX;
    nc_tw_tick(&tw);
    check_int("saturated timer fired", tw_fire_count, 1);
    check_int("saturated timer inactive after fire", timer.active, 0);
}

static nc_timer_wheel_t *tw_readd_wheel;
static int tw_readd_count;

static void tw_readd_zero_cb(nc_timer_t *timer, void *data) {
    (void)data;
    tw_readd_count++;
    if (tw_readd_count == 1)
        nc_tw_add(tw_readd_wheel, timer, 0);
}

static void test_timer_wheel_callback_readd_zero(void) {
    printf("[timer_wheel_callback_readd_zero]\n");

    tw_fake_now_ms = 3500;
    nc_timer_wheel_t tw;
    nc_timer_t timer;
    nc_tw_init(&tw);
    nc_timer_init(&timer, tw_readd_zero_cb, NULL);
    tw_readd_wheel = &tw;
    tw_readd_count = 0;

    nc_tw_add(&tw, &timer, 1);
    tw_fake_now_ms = 3501;
    nc_tw_tick(&tw);
    check_int("callback re-add fired once", tw_readd_count, 1);
    check_int("callback re-add remains active", timer.active, 1);
    check_true("callback re-add is after processed cursor",
               timer.expire_ms > tw.last_ms);

    tw_fake_now_ms = 3502;
    nc_tw_tick(&tw);
    check_int("callback re-add fired on next tick", tw_readd_count, 2);
    check_int("callback re-add inactive after second fire", timer.active, 0);
    tw_readd_wheel = NULL;
}

static void test_timer_wheel_reschedule(void) {
    printf("[timer_wheel_reschedule]\n");

    tw_fake_now_ms = 4000;
    nc_timer_wheel_t tw;
    nc_tw_init(&tw);

    nc_timer_t t1;
    nc_timer_init(&t1, tw_callback, NULL);
    tw_fire_count = 0;

    nc_tw_add(&tw, &t1, 5);
    nc_tw_add(&tw, &t1, 10);
    check_int("timer still active", t1.active, 1);

    tw_fake_now_ms = 4006;
    nc_tw_tick(&tw);
    check_int("reschedule: not fired at old time", tw_fire_count, 0);
}

static nc_timer_wheel_t *tw_cancel_wheel;
static nc_timer_t *tw_cancel_target;
static int tw_cancel_target_fired;
static int tw_cancel_survivor_fired;

static void tw_cancel_target_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_cancel_target_fired++;
}

static void tw_cancel_survivor_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_cancel_survivor_fired++;
}

static void tw_cancel_sibling_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    nc_tw_cancel(tw_cancel_wheel, tw_cancel_target);
}

static void test_timer_wheel_callback_cancel(void) {
    printf("[timer_wheel_callback_cancel]\n");

    tw_fake_now_ms = 5000;
    nc_timer_wheel_t wheel;
    nc_timer_t target;
    nc_timer_t canceler;
    nc_timer_t survivor;
    nc_tw_init(&wheel);
    nc_timer_init(&target, tw_cancel_target_cb, NULL);
    nc_timer_init(&canceler, tw_cancel_sibling_cb, NULL);
    nc_timer_init(&survivor, tw_cancel_survivor_cb, NULL);
    tw_cancel_wheel = &wheel;
    tw_cancel_target = &target;
    tw_cancel_target_fired = 0;
    tw_cancel_survivor_fired = 0;

    nc_tw_add(&wheel, &survivor, 1);
    nc_tw_add(&wheel, &target, 1);
    nc_tw_add(&wheel, &canceler, 1);
    tw_fake_now_ms = 5001;
    nc_tw_tick(&wheel);

    check_int("callback-canceled timer inactive", target.active, 0);
    check_int("callback-canceled timer did not fire",
              tw_cancel_target_fired, 0);
    check_int("timer after callback-canceled sibling fired",
              tw_cancel_survivor_fired, 1);
    check_int("timer after callback-canceled sibling inactive",
              survivor.active, 0);
    tw_cancel_wheel = NULL;
    tw_cancel_target = NULL;
}

static nc_timer_wheel_t *tw_move_wheel;
static nc_timer_t *tw_move_target;
static int tw_move_target_fired;
static int tw_move_survivor_fired;

static void tw_move_target_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_move_target_fired++;
}

static void tw_move_survivor_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_move_survivor_fired++;
}

static void tw_move_sibling_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    nc_tw_add(tw_move_wheel, tw_move_target, 2);
}

static void test_timer_wheel_callback_reschedule_next(void) {
    printf("[timer_wheel_callback_reschedule_next]\n");

    tw_fake_now_ms = 6000;
    nc_timer_wheel_t wheel;
    nc_timer_t target;
    nc_timer_t mover;
    nc_timer_t survivor;
    nc_tw_init(&wheel);
    nc_timer_init(&target, tw_move_target_cb, NULL);
    nc_timer_init(&mover, tw_move_sibling_cb, NULL);
    nc_timer_init(&survivor, tw_move_survivor_cb, NULL);
    tw_move_wheel = &wheel;
    tw_move_target = &target;
    tw_move_target_fired = 0;
    tw_move_survivor_fired = 0;

    nc_tw_add(&wheel, &survivor, 1);
    nc_tw_add(&wheel, &target, 1);
    nc_tw_add(&wheel, &mover, 1);
    tw_fake_now_ms = 6001;
    nc_tw_tick(&wheel);

    check_int("timer after callback-rescheduled sibling fired",
              tw_move_survivor_fired, 1);
    check_int("timer after callback-rescheduled sibling inactive",
              survivor.active, 0);
    check_int("callback-rescheduled timer deferred", tw_move_target_fired, 0);
    check_int("callback-rescheduled timer remains active", target.active, 1);
    check_true("callback-rescheduled timer moved to a later slot",
               target.expire_ms == 6003);
    check_true("callback-rescheduled source slot empty",
               wheel.slots[6001 % NC_TW_SLOTS] == NULL);

    tw_fake_now_ms = 6003;
    nc_tw_tick(&wheel);
    check_int("callback-rescheduled timer fired later", tw_move_target_fired, 1);
    check_int("callback-rescheduled timer inactive", target.active, 0);
    tw_move_wheel = NULL;
    tw_move_target = NULL;
}

static nc_timer_wheel_t *tw_nested_wheel;
static nc_timer_t *tw_nested_target;
static int tw_nested_target_fired;
static int tw_nested_canceler_fired;
static int tw_nested_survivor_fired;

static void tw_nested_target_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_nested_target_fired++;
}

static void tw_nested_survivor_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_nested_survivor_fired++;
}

static void tw_nested_cancel_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_nested_canceler_fired++;
    nc_tw_cancel(tw_nested_wheel, tw_nested_target);
}

static void tw_nested_tick_cb(nc_timer_t *timer, void *data) {
    (void)timer;
    (void)data;
    tw_fake_now_ms = 7002;
    nc_tw_tick(tw_nested_wheel);
}

static void test_timer_wheel_callback_nested_tick(void) {
    printf("[timer_wheel_callback_nested_tick]\n");

    tw_fake_now_ms = 7000;
    nc_timer_wheel_t wheel;
    nc_timer_t target;
    nc_timer_t nested_canceler;
    nc_timer_t nested_trigger;
    nc_timer_t survivor;
    nc_tw_init(&wheel);
    nc_timer_init(&target, tw_nested_target_cb, NULL);
    nc_timer_init(&nested_canceler, tw_nested_cancel_cb, NULL);
    nc_timer_init(&nested_trigger, tw_nested_tick_cb, NULL);
    nc_timer_init(&survivor, tw_nested_survivor_cb, NULL);
    tw_nested_wheel = &wheel;
    tw_nested_target = &target;
    tw_nested_target_fired = 0;
    tw_nested_canceler_fired = 0;
    tw_nested_survivor_fired = 0;

    nc_tw_add(&wheel, &survivor, 1);
    nc_tw_add(&wheel, &target, 1);
    nc_tw_add(&wheel, &nested_trigger, 1);
    nc_tw_add(&wheel, &nested_canceler, 2);
    tw_fake_now_ms = 7001;
    nc_tw_tick(&wheel);

    check_int("nested canceler fired", tw_nested_canceler_fired, 1);
    check_int("nested canceler inactive", nested_canceler.active, 0);
    check_int("nested-canceled timer did not fire", tw_nested_target_fired, 0);
    check_int("nested-canceled timer inactive", target.active, 0);
    check_int("outer timer after nested cancellation fired",
              tw_nested_survivor_fired, 1);
    check_int("outer timer after nested cancellation inactive",
              survivor.active, 0);
    tw_nested_wheel = NULL;
    tw_nested_target = NULL;
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
    check_int("unlimited mode still tracks count",
              nc_conn_limiter_count(&lim), 10000);
    for (int i = 0; i < 10000; i++)
        nc_conn_limiter_release(&lim);
    check_int("unlimited releases balance count",
              nc_conn_limiter_count(&lim), 0);
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
    ctx.max_requests = 0;
    check_int("zero max means unlimited",
              nc_conn_ctx_max_reached(&ctx), 0);

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
    check_int("reject missing port",
              nc_parse_addr("localhost:", host, sizeof(host), &port), -1);
    check_int("reject nonnumeric port",
              nc_parse_addr("localhost:http", host, sizeof(host), &port), -1);
    check_int("reject overflowing port",
              nc_parse_addr("localhost:65536", host, sizeof(host), &port), -1);
    check_int("reject unbracketed IPv6",
              nc_parse_addr("::1:443", host, sizeof(host), &port), -1);
    check_int("reject truncated host",
              nc_parse_addr("localhost:80", host, 4, &port), -1);
    check_int("reject extra brackets",
              nc_parse_addr("foo[bar]:80", host, sizeof(host), &port), -1);
    check_int("reject stray close bracket",
              nc_parse_addr("]:80", host, sizeof(host), &port), -1);
    check_int("reject CTL in host",
              nc_parse_addr("host\nname:80", host, sizeof(host), &port), -1);
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
        check_int("event preserves fd", (int)events[0].fd, fds[0]);
        check_true("event preserves data", events[0].data == (void *)0x1234);
    }

#ifdef __APPLE__
    {
        char drain;
        (void)read(fds[0], &drain, 1);
        close(fds[1]);
        fds[1] = -1;
        n = nc_poller_wait(p, events, 16, 100);
        check_true("eof is readable", n >= 1 && (events[0].events & NC_EV_READ) != 0);
        check_true("eof is not error",
                   n >= 1 && (events[0].events & NC_EV_ERROR) == 0);
    }
#endif

    check_int("poller del", nc_poller_del(p, fds[0]), 0);
    close(fds[0]);
    if (fds[1] >= 0)
        close(fds[1]);
    nc_poller_destroy(p);

#if defined(NC_USE_KQUEUE)
    /* kqueue emits one kevent per filter. HTTP frees conn data on the first
     * ERROR, so READ+WRITE in the same batch must coalesce like epoll. */
    {
        nc_poller_t *kq = nc_poller_create();
        int sp[2] = {-1, -1};
        check_true("kqueue poller", kq != NULL);
        if (kq) {
        check_int("socketpair", socketpair(AF_UNIX, SOCK_STREAM, 0, sp), 0);
        nc_set_nonblocking(sp[0]);
        nc_set_nonblocking(sp[1]);
        check_int("add read+write",
                  nc_poller_add(kq, sp[0], NC_EV_READ | NC_EV_WRITE,
                                (void *)0xbeef),
                  0);
        check_int("peer write", (int)write(sp[1], "x", 1), 1);
        nc_event_t ev[8];
        int got = nc_poller_wait(kq, ev, 8, 100);
        check_int("kqueue coalesces dual filter", got, 1);
        if (got >= 1) {
            check_int("coalesced fd", (int)ev[0].fd, sp[0]);
            check_true("coalesced read", (ev[0].events & NC_EV_READ) != 0);
            check_true("coalesced write", (ev[0].events & NC_EV_WRITE) != 0);
            check_true("coalesced data", ev[0].data == (void *)0xbeef);
        }
        nc_poller_del(kq, sp[0]);
        close(sp[0]);
        close(sp[1]);
        nc_poller_destroy(kq);
        }
    }
#endif
}

static void test_poller_stale_completion(void) {
    printf("[poller_stale_completion]\n");

    nc_poller_t *p = nc_poller_create();
    check_true("poller created", p != NULL);
    if (!p) return;

    int fds[2];
    check_int("pipe created", pipe(fds), 0);

    char *tag = (char *)malloc(1);
    check_true("tag allocated", tag != NULL);
    if (!tag) {
        close(fds[0]);
        close(fds[1]);
        nc_poller_destroy(p);
        return;
    }
    *tag = 7;

    check_int("poller add", nc_poller_add(p, fds[0], NC_EV_READ, tag), 0);
    check_int("pipe write", (int)write(fds[1], "x", 1), 1);
    check_int("poller del", nc_poller_del(p, fds[0]), 0);

    void *freed = tag;
    free(tag);

    nc_event_t events[16];
    int n = nc_poller_wait(p, events, 16, 0);
    check_true("wait after del does not fail", n >= 0);
    int stale = 0;
    for (int i = 0; i < n; i++) {
        if (events[i].data == freed || events[i].fd == fds[0])
            stale = 1;
    }
    check_int("queued completion dropped after del", stale, 0);

    int live = 99;
    check_int("re-add after del",
              nc_poller_add(p, fds[0], NC_EV_READ, &live), 0);
    n = nc_poller_wait(p, events, 16, 100);
    check_true("re-add delivers live data",
               n >= 1 && events[0].data == &live && events[0].fd == fds[0]);
    check_int("re-add del", nc_poller_del(p, fds[0]), 0);

#if defined(NC_USE_EPOLL) || defined(NC_USE_KQUEUE) || \
    (defined(NC_USE_IO_URING) && NC_USE_IO_URING)
    check_int("fd table wrap rejected",
              nc_poller_add(p, INT_MAX, NC_EV_READ, NULL), -1);
#endif

    close(fds[0]);
    close(fds[1]);
    nc_poller_destroy(p);
}
#endif

/* ===== Event Loop Tests ===== */

static volatile int evloop_test_done = 0;
static volatile int evloop_task_count = 0;
static volatile int evloop_post_failed = 0;
static int evloop_run_result = NC_EVLOOP_ERROR;

static void evloop_stop_task(void *arg) {
    nc_evloop_t *loop = (nc_evloop_t *)arg;
    evloop_test_done = 1;
    nc_evloop_stop(loop);
}

static void *evloop_thread(void *arg) {
    nc_evloop_t *loop = (nc_evloop_t *)arg;
    evloop_run_result = nc_evloop_run(loop, NULL);
    return NULL;
}

static void evloop_count_task(void *arg) {
    (void)arg;
    evloop_task_count++;
}

static void evloop_post_then_stop_task(void *arg) {
    nc_evloop_t *loop = (nc_evloop_t *)arg;
    if (nc_evloop_post(loop, evloop_count_task, NULL) != 0)
        nc_atomic_store(&evloop_post_failed, 1);
    nc_evloop_stop(loop);
}

typedef struct {
    nc_evloop_t *loop;
    int count;
} evloop_producer_arg_t;

static void *evloop_producer(void *arg) {
    evloop_producer_arg_t *producer = (evloop_producer_arg_t *)arg;
    for (int i = 0; i < producer->count; i++) {
        if (nc_evloop_post(producer->loop, evloop_count_task, NULL) != 0)
            nc_atomic_store(&evloop_post_failed, 1);
    }
    return NULL;
}

static void test_evloop(void) {
    printf("[evloop]\n");

    nc_evloop_t *loop = nc_evloop_create();
    check_true("evloop created", loop != NULL);
    if (!loop) return;

    evloop_test_done = 0;
    evloop_task_count = 0;
    evloop_post_failed = 0;
    evloop_run_result = NC_EVLOOP_ERROR;

    nc_thread_t loop_thread;
    check_int("evloop thread started",
              nc_thread_create(&loop_thread, evloop_thread, loop), 0);

    #define EVLOOP_PRODUCERS 4
    #define EVLOOP_TASKS_PER_PRODUCER 500
    nc_thread_t producers[EVLOOP_PRODUCERS];
    evloop_producer_arg_t producer_args[EVLOOP_PRODUCERS];
    for (int i = 0; i < EVLOOP_PRODUCERS; i++) {
        producer_args[i].loop = loop;
        producer_args[i].count = EVLOOP_TASKS_PER_PRODUCER;
        check_int("producer started",
                  nc_thread_create(&producers[i], evloop_producer,
                                   &producer_args[i]), 0);
    }
    for (int i = 0; i < EVLOOP_PRODUCERS; i++)
        nc_thread_join(producers[i]);

    check_int("stop task posted",
              nc_evloop_post(loop, evloop_stop_task, loop), 0);
    nc_thread_join(loop_thread);
    check_int("evloop task executed", evloop_test_done, 1);
    check_int("all concurrent posts accepted", evloop_post_failed, 0);
    check_int("all concurrent tasks executed", evloop_task_count,
              EVLOOP_PRODUCERS * EVLOOP_TASKS_PER_PRODUCER);
    check_int("evloop cancellation result", evloop_run_result,
              NC_EVLOOP_CANCELLED);

    check_int("inactive evloop stop succeeds", nc_evloop_stop(loop), 0);
    check_int("inactive evloop stop stays quiescent",
              nc_atomic_load(&loop->wakeup_pending), 0);

#ifndef _WIN32
    char byte;
    errno = 0;
    ssize_t read_count = read(loop->wakeup_fds[0], &byte, 1);
    check_true("wakeup pipe fully drained",
               read_count < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK));
#endif
    nc_evloop_destroy(loop);

    loop = nc_evloop_create();
    check_true("evloop post-before-stop created", loop != NULL);
    if (loop) {
        evloop_task_count = 0;
        evloop_post_failed = 0;
        evloop_run_result = NC_EVLOOP_ERROR;
        check_int("evloop post-before-stop thread started",
                  nc_thread_create(&loop_thread, evloop_thread, loop), 0);
        check_int("post-before-stop task accepted",
                  nc_evloop_post(loop, evloop_post_then_stop_task, loop), 0);
        nc_thread_join(loop_thread);
        check_int("nested task accepted before stop", evloop_post_failed, 0);
        check_int("nested task drained before stop", evloop_task_count, 1);
        check_int("post-before-stop cancellation result", evloop_run_result,
                  NC_EVLOOP_CANCELLED);
        nc_evloop_destroy(loop);
    }

    loop = nc_evloop_create();
    check_true("evloop terminal-post created", loop != NULL);
    if (loop) {
        evloop_task_count = 0;
        nc_atomic_store(&loop->running, 1);
        check_int("terminal post accepted",
                  nc_evloop_post(loop, evloop_count_task, NULL), 0);
        nc_evloop_finish_run(loop);
        check_int("terminal post keeps wakeup armed",
                  nc_atomic_load(&loop->wakeup_pending), 1);
        check_int("terminal post rerun reaches deadline",
                  nc_evloop_run_for(loop, NULL, 20), NC_EVLOOP_DEADLINE);
        check_int("terminal post executes on rerun", evloop_task_count, 1);
        nc_evloop_destroy(loop);
    }

    loop = nc_evloop_create();
    check_true("deadline evloop created", loop != NULL);
    if (loop) {
        check_int("evloop deadline result",
                  nc_evloop_run_for(loop, NULL, 20),
                  NC_EVLOOP_DEADLINE);
        nc_evloop_destroy(loop);
    }

    #undef EVLOOP_PRODUCERS
    #undef EVLOOP_TASKS_PER_PRODUCER
}

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
    test_timer_wheel_deadline_saturates();
    test_timer_wheel_callback_readd_zero();
    test_timer_wheel_reschedule();
    test_timer_wheel_callback_cancel();
    test_timer_wheel_callback_reschedule_next();
    test_timer_wheel_callback_nested_tick();
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
    test_evloop();

#ifndef _WIN32
    test_reuseport();
    test_socket_helpers();
    test_poller();
    test_poller_stale_completion();
#endif

    printf("\n--- net/internals: %d/%d passed ---\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("FAILED (%d failures)\n", tests_failed);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
