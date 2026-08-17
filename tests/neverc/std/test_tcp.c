#include "neverc/std/net/tcp.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got == NULL && expected == NULL) { tests_passed++; return; }
    if (got == NULL || expected == NULL) {
        tests_failed++;
        printf("  FAIL: %s: got %s, expected %s\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
        return;
    }
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_null(const char *name, const void *ptr) {
    tests_run++;
    if (!ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: expected NULL but got non-NULL\n", name); }
}

/* ===== listen + close ===== */

static void test_listen_close(void) {
    printf("[listen_close]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen(":0", &err);
    check_not_null("listen :0", ln);
    check_null("listen no error", err);

    if (ln) {
        neverc_tcp_addr_t addr;
        neverc_tcp_listener_addr(ln, &addr);
        check_int("port > 0", addr.port > 0, 1);
        check_int("listener handle valid",
                  neverc_tcp_listener_handle(ln) !=
                      NEVERC_NET_INVALID_HANDLE,
                  1);
        printf("    bound to %s (port %d)\n", addr.addr, addr.port);
        neverc_tcp_listener_close(ln);
    }
}

/* ===== listen on explicit addr ===== */

static void test_listen_explicit(void) {
    printf("[listen_explicit]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("listen 127.0.0.1:0", ln);
    check_null("no error", err);

    if (ln) {
        neverc_tcp_addr_t addr;
        neverc_tcp_listener_addr(ln, &addr);
        check_int("port > 0", addr.port > 0, 1);
        neverc_tcp_listener_close(ln);
    }
}

/* ===== IPv6 listen ===== */

static void test_listen_ipv6(void) {
    printf("[listen_ipv6]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("[::1]:0", &err);
    check_not_null("listen [::1]:0", ln);
    check_null("IPv6 listen no error", err);

    if (ln) {
        neverc_tcp_addr_t addr;
        check_int("IPv6 listener addr", neverc_tcp_listener_addr(ln, &addr), 0);
        check_int("IPv6 port > 0", addr.port > 0, 1);
        check_int("IPv6 addr bracketed", addr.addr[0] == '[', 1);
        neverc_tcp_listener_close(ln);
    }
}

/* ===== invalid address ===== */

static void test_invalid_addr(void) {
    printf("[invalid_addr]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("", &err);
    check_null("empty addr", ln);

    ln = neverc_tcp_listen("no_colon", &err);
    check_null("no colon", ln);

    ln = neverc_tcp_listen("127.0.0.1:65536", &err);
    check_null("listen port overflow", ln);

    ln = neverc_tcp_listen("127.0.0.1:abc", &err);
    check_null("listen nonnumeric port", ln);

    neverc_tcp_conn_t *conn = neverc_tcp_dial(":80", &err);
    check_null("dial empty host", conn);
    if (conn) neverc_tcp_close(conn);

    conn = neverc_tcp_dial("127.0.0.1:65536", &err);
    check_null("dial port overflow", conn);
    if (conn) neverc_tcp_close(conn);
}

/* ===== dial fail ===== */

static void test_dial_fail(void) {
    printf("[dial_fail]\n");
    const char *err = NULL;

    neverc_tcp_conn_t *conn = neverc_tcp_dial("127.0.0.1:1", &err);
    check_null("dial refused", conn);
    if (conn) neverc_tcp_close(conn);
}

/* ===== null safety ===== */

static void test_null_safety(void) {
    printf("[null_safety]\n");

    check_int("write null", neverc_tcp_write(NULL, "x", 1), -1);
    check_int("read null", neverc_tcp_read(NULL, (void*)"x", 1), -1);
    neverc_tcp_close(NULL);
    check_int("remote_addr null", neverc_tcp_remote_addr(NULL, NULL), -1);
    check_int("local_addr null", neverc_tcp_local_addr(NULL, NULL), -1);
    check_int("set_timeout null", neverc_tcp_set_timeout(NULL, 100), -1);
    check_int("set_nodelay null", neverc_tcp_set_nodelay(NULL, 1), -1);
    check_int("set_reuseaddr null", neverc_tcp_set_reuseaddr(NULL, 1), -1);
    check_int("accept null", neverc_tcp_accept(NULL, NULL) == NULL, 1);
    {
        neverc_tcp_conn_t *accepted = (neverc_tcp_conn_t *)(void *)1;
        neverc_net_result_t accept_rc =
            neverc_tcp_try_accept(NULL, &accepted);
        check_int("try accept null listener",
                  accept_rc.status, NEVERC_NET_INVALID);
        check_int("try accept null listener clears out",
                  accepted == NULL, 1);
        accept_rc = neverc_tcp_try_accept(NULL, NULL);
        check_int("try accept null out",
                  accept_rc.status, NEVERC_NET_INVALID);
    }
    check_int("listener_addr null", neverc_tcp_listener_addr(NULL, NULL), -1);
    check_int("listener handle null",
              neverc_tcp_listener_handle(NULL) ==
                  NEVERC_NET_INVALID_HANDLE,
              1);
    check_int("connection handle null",
              neverc_tcp_conn_handle(NULL) == NEVERC_NET_INVALID_HANDLE, 1);
    neverc_tcp_listener_close(NULL);
    tests_passed++; tests_run++;  /* survived without crash */
}

/* ===== socket options ===== */

static void test_options(void) {
    printf("[options]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen(":0", &err);
    check_not_null("options listen", ln);
    if (ln) {
        check_int("reuseaddr", neverc_tcp_set_reuseaddr(ln, 1), 0);
        neverc_tcp_listener_close(ln);
    }
}

#ifndef _WIN32

static long long test_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ===== echo test ===== */

static void test_echo(void) {
    printf("[echo]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("echo listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (!conn) _exit(1);

        const char *msg = "Hello, TCP!";
        neverc_tcp_write(conn, msg, strlen(msg));

        char buf[256];
        int n = neverc_tcp_read(conn, buf, sizeof(buf));
        buf[n > 0 ? n : 0] = '\0';

        neverc_tcp_close(conn);
        _exit(strcmp(buf, "ECHO:Hello, TCP!") == 0 ? 0 : 2);
    }

    neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
    check_not_null("echo accept", conn);

    if (conn) {
        char buf[256];
        int n = neverc_tcp_read(conn, buf, sizeof(buf));
        check_int("echo read > 0", n > 0, 1);
        buf[n > 0 ? n : 0] = '\0';

        char reply[512];
        snprintf(reply, sizeof(reply), "ECHO:%s", buf);
        neverc_tcp_write(conn, reply, strlen(reply));

        neverc_tcp_addr_t raddr;
        neverc_tcp_remote_addr(conn, &raddr);
        check_int("remote port > 0", raddr.port > 0, 1);

        neverc_tcp_local_addr(conn, &raddr);
        check_int("local port > 0", raddr.port > 0, 1);

        neverc_tcp_close(conn);
    }

    int status;
    waitpid(pid, &status, 0);
    check_int("client exit 0", WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);

    neverc_tcp_listener_close(ln);
}

/* ===== IPv6 echo test ===== */

static void test_ipv6_echo(void) {
    printf("[ipv6_echo]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("[::1]:0", &err);
    check_not_null("IPv6 echo listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        char addr[64];
        snprintf(addr, sizeof(addr), "[::1]:%d", laddr.port);
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (!conn) _exit(1);
        int n = neverc_tcp_write(conn, "v6", 2);
        neverc_tcp_close(conn);
        _exit(n == 2 ? 0 : 2);
    }

    int status;
    waitpid(pid, &status, 0);
    int client_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    check_int("IPv6 client connected", client_ok, 1);
    if (client_ok) {
        neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
        check_not_null("IPv6 echo accept", conn);
        if (conn) {
            char buf[8];
            int n = neverc_tcp_read(conn, buf, sizeof(buf));
            check_int("IPv6 echo read", n, 2);
            neverc_tcp_close(conn);
        }
    }

    neverc_tcp_listener_close(ln);
}

/* IPv4-mapped IPv6 must format as a.b.c.d:port (ACL/SSRF). Dual-stack
 * listen on [::]:0 must accept IPv4 and report the same. */
static void test_ipv4_mapped_addr(void) {
    printf("[ipv4_mapped_addr]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("mapped listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        char mapped[80];
        snprintf(mapped, sizeof(mapped), "[::ffff:127.0.0.1]:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(mapped, &err);
        if (!c) _exit(1);
        neverc_tcp_addr_t remote;
        int unmapped = neverc_tcp_remote_addr(c, &remote) == 0 &&
                       strncmp(remote.addr, "127.0.0.1:", 10) == 0 &&
                       strstr(remote.addr, "ffff") == NULL;
        neverc_tcp_write(c, "x", 1);
        neverc_tcp_close(c);
        _exit(unmapped ? 0 : 3);
    }

    neverc_tcp_conn_t *accepted = NULL;
    for (int i = 0; i < 100 && !accepted; i++) {
        neverc_net_result_t r = neverc_tcp_try_accept(ln, &accepted);
        if (r.status == NEVERC_NET_OK) break;
        accepted = NULL;
        usleep(10000);
    }
    if (accepted) {
        char buf[4];
        (void)neverc_tcp_read(accepted, buf, sizeof(buf));
        neverc_tcp_close(accepted);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
        check_int("mapped dial unavailable", 1, 1);
    } else {
        check_int("mapped dial remote is ipv4",
                  WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
    }
    neverc_tcp_listener_close(ln);

    ln = neverc_tcp_listen("[::]:0", &err);
    if (!ln) return;
    neverc_tcp_listener_addr(ln, &laddr);

    pid = fork();
    if (pid == 0) {
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);
        neverc_tcp_write(c, "x", 1);
        neverc_tcp_close(c);
        _exit(0);
    }

    accepted = NULL;
    for (int i = 0; i < 100 && !accepted; i++) {
        neverc_net_result_t r = neverc_tcp_try_accept(ln, &accepted);
        if (r.status == NEVERC_NET_OK) break;
        accepted = NULL;
        usleep(10000);
    }
    waitpid(pid, &status, 0);
    if (accepted) {
        neverc_tcp_addr_t remote;
        check_int("dual-stack accept",
                  neverc_tcp_remote_addr(accepted, &remote), 0);
        check_int("dual-stack peer is ipv4 text",
                  strncmp(remote.addr, "127.0.0.1:", 10) == 0, 1);
        check_int("dual-stack peer not ffff",
                  strstr(remote.addr, "ffff") == NULL, 1);
        neverc_tcp_close(accepted);
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
        check_int("dual-stack ipv4 dial skipped", 1, 1);
    } else {
        check_int("dual-stack ipv4 accept", 0, 1);
    }
    neverc_tcp_listener_close(ln);
}

/* ===== AF_UNSPEC address fallback ===== */

static void test_dial_address_fallback(void) {
    printf("[dial_address_fallback]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("fallback listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        char addr[64];
        snprintf(addr, sizeof(addr), "localhost:%d", laddr.port);
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (!conn) _exit(1);
        int n = neverc_tcp_write(conn, "fallback", 8);
        neverc_tcp_close(conn);
        _exit(n == 8 ? 0 : 2);
    }

    int status;
    waitpid(pid, &status, 0);
    int client_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    check_int("fallback client connected", client_ok, 1);
    if (client_ok) {
        neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
        check_not_null("fallback accept", conn);
        if (conn) {
            char buf[16];
            int n = neverc_tcp_read(conn, buf, sizeof(buf));
            check_int("fallback read", n, 8);
            neverc_tcp_close(conn);
        }
    }

    neverc_tcp_listener_close(ln);
}

/* ===== multiple clients ===== */

static void test_multiple_clients(void) {
    printf("[multiple_clients]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("multi listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    int nclient = 5;
    for (int i = 0; i < nclient; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            usleep(50000 + (unsigned)(i * 20000));
            char addr[64];
            snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
            neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
            if (!c) _exit(1);
            char msg[32];
            snprintf(msg, sizeof(msg), "client%d", i);
            neverc_tcp_write(c, msg, strlen(msg));
            neverc_tcp_close(c);
            _exit(0);
        }
    }

    int received = 0;
    for (int i = 0; i < nclient; i++) {
        neverc_tcp_conn_t *c = neverc_tcp_accept(ln, &err);
        if (c) {
            char buf[64];
            int n = neverc_tcp_read(c, buf, sizeof(buf));
            if (n > 0) received++;
            neverc_tcp_close(c);
        }
    }
    check_int("received all", received, nclient);

    for (int i = 0; i < nclient; i++) wait(NULL);

    neverc_tcp_listener_close(ln);
}

/* ===== large data transfer ===== */

static void test_large_data(void) {
    printf("[large_data]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("large listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    #define LARGE_SZ (64 * 1024)

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);

        char *data = (char *)malloc(LARGE_SZ);
        for (int i = 0; i < LARGE_SZ; i++) data[i] = (char)('A' + (i % 26));

        size_t sent = 0;
        while (sent < LARGE_SZ) {
            size_t chunk = LARGE_SZ - sent;
            if (chunk > 4096) chunk = 4096;
            int n = neverc_tcp_write(c, data + sent, chunk);
            if (n <= 0) break;
            sent += (size_t)n;
        }
        free(data);
        neverc_tcp_close(c);
        _exit(sent == LARGE_SZ ? 0 : 1);
    }

    neverc_tcp_conn_t *c = neverc_tcp_accept(ln, &err);
    check_not_null("large accept", c);

    if (c) {
        char *buf = (char *)malloc(LARGE_SZ + 1);
        size_t total = 0;
        while (total < LARGE_SZ) {
            int n = neverc_tcp_read(c, buf + total, LARGE_SZ - total);
            if (n <= 0) break;
            total += (size_t)n;
        }
        check_int("large recv total", (int)total, LARGE_SZ);

        int correct = 1;
        for (size_t i = 0; i < total; i++) {
            if (buf[i] != (char)('A' + (i % 26))) { correct = 0; break; }
        }
        check_int("large data correct", correct, 1);
        free(buf);
        neverc_tcp_close(c);
    }

    int status;
    waitpid(pid, &status, 0);
    check_int("large client ok", WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);

    neverc_tcp_listener_close(ln);
    #undef LARGE_SZ
}

/* ===== nodelay option ===== */

static void test_nodelay(void) {
    printf("[nodelay]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("nodelay listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);
        neverc_tcp_set_nodelay(c, 1);
        neverc_tcp_write(c, "ok", 2);
        neverc_tcp_close(c);
        _exit(0);
    }

    neverc_tcp_conn_t *c = neverc_tcp_accept(ln, &err);
    if (c) {
        check_int("nodelay set", neverc_tcp_set_nodelay(c, 1), 0);
        char buf[8];
        int n = neverc_tcp_read(c, buf, sizeof(buf));
        check_int("nodelay read", n, 2);
        neverc_tcp_close(c);
    }

    waitpid(pid, NULL, 0);
    neverc_tcp_listener_close(ln);
}

/* ===== Pipe test ===== */

static void test_pipe(void) {
    printf("[pipe]\n");

    neverc_tcp_conn_t *a = NULL, *b = NULL;
    int rc = neverc_tcp_pipe(&a, &b);
    check_int("pipe create", rc, 0);
    check_not_null("pipe a", a);
    check_not_null("pipe b", b);

    if (a && b) {
        char byte = 0;
        check_int("pipe rejects oversized write",
                  neverc_tcp_write(
                      a, &byte, (size_t)INT_MAX + 1), -1);
        check_int("pipe rejects oversized read",
                  neverc_tcp_read(
                      a, &byte, (size_t)INT_MAX + 1), -1);

        const char *msg = "hello pipe!";
        neverc_tcp_write(a, msg, strlen(msg));
        check_int("pipe shutdown write", neverc_tcp_shutdown_write(a), 0);
        check_int("pipe write after shutdown write",
                  neverc_tcp_write(a, "x", 1), -1);
        neverc_net_result_t shut_write =
            neverc_tcp_try_write(a, "x", 1);
        check_int("pipe try write after shutdown write",
                  shut_write.status, NEVERC_NET_CLOSED);

        char buf[64];
        int n = neverc_tcp_read(b, buf, sizeof(buf));
        check_int("pipe read len", n, (int)strlen(msg));
        buf[n] = '\0';
        check_str("pipe read data", buf, "hello pipe!");
        check_int("pipe read EOF after half-close",
                  neverc_tcp_read(b, buf, sizeof(buf)), 0);

        const char *reply = "pong";
        neverc_tcp_write(b, reply, strlen(reply));
        n = neverc_tcp_read(a, buf, sizeof(buf));
        check_int("pipe reply len", n, 4);
        buf[n] = '\0';
        check_str("pipe reply data", buf, "pong");
        check_int("pipe shutdown read", neverc_tcp_shutdown_read(a), 0);
        check_int("pipe read after shutdown read",
                  neverc_tcp_read(a, buf, sizeof(buf)), 0);
        neverc_net_result_t shut_read =
            neverc_tcp_try_read(a, buf, sizeof(buf));
        check_int("pipe try read after shutdown read",
                  shut_read.status, NEVERC_NET_EOF);

        neverc_tcp_close(a);
        neverc_tcp_close(b);
    }

    /* Null safety */
    check_int("pipe null a", neverc_tcp_pipe(NULL, &b), -1);
    check_int("pipe null b", neverc_tcp_pipe(&a, NULL), -1);
    check_int("shutdown read null", neverc_tcp_shutdown_read(NULL), -1);
    check_int("shutdown write null", neverc_tcp_shutdown_write(NULL), -1);
}

/* ===== independent read/write timeouts ===== */

static void test_independent_timeouts(void) {
    printf("[independent_timeouts]\n");

    neverc_tcp_conn_t *a = NULL, *b = NULL;
    check_int("timeout pipe create", neverc_tcp_pipe(&a, &b), 0);
    if (!a || !b) return;

    check_int("set read timeout", neverc_tcp_set_read_timeout(a, 50), 0);
    check_int("set write timeout", neverc_tcp_set_write_timeout(a, 500), 0);
    check_int("reject negative read timeout",
              neverc_tcp_set_read_timeout(a, -1), -1);
    check_int("reject negative write timeout",
              neverc_tcp_set_write_timeout(a, -1), -1);

    char buf[8];
    long long started = test_now_ms();
    int n = neverc_tcp_read(a, buf, sizeof(buf));
    long long elapsed = test_now_ms() - started;
    check_int("read timed out", n, -1);
    check_int("read timeout errno", errno == ETIMEDOUT, 1);
    check_int("read timeout bounded", elapsed >= 10 && elapsed < 1000, 1);

    started = test_now_ms();
    check_int("set absolute read deadline",
              neverc_tcp_set_read_deadline(a, started + 50), 0);
    n = neverc_tcp_read(a, buf, sizeof(buf));
    elapsed = test_now_ms() - started;
    check_int("absolute read deadline fired", n, -1);
    check_int("absolute deadline errno", errno == ETIMEDOUT, 1);
    check_int("absolute deadline bounded",
              elapsed >= 10 && elapsed < 1000, 1);
    check_int("set absolute write deadline",
              neverc_tcp_set_write_deadline(a, test_now_ms() + 500), 0);

    check_int("write remains usable", neverc_tcp_write(a, "ok", 2), 2);
    check_int("peer receives after read timeout",
              neverc_tcp_read(b, buf, sizeof(buf)), 2);

    neverc_tcp_close(a);
    neverc_tcp_close(b);

    check_int("read timeout null",
              neverc_tcp_set_read_timeout(NULL, 1), -1);
    check_int("write timeout null",
              neverc_tcp_set_write_timeout(NULL, 1), -1);
    check_int("read deadline null",
              neverc_tcp_set_read_deadline(NULL, 1), -1);
    check_int("write deadline null",
              neverc_tcp_set_write_deadline(NULL, 1), -1);
}

/* ===== structured non-blocking and context-aware I/O ===== */

static void test_controlled_io(void) {
    printf("[controlled_io]\n");

    neverc_tcp_conn_t *a = NULL, *b = NULL;
    check_int("controlled pipe", neverc_tcp_pipe(&a, &b), 0);
    if (!a || !b) return;

    char buf[8];
    neverc_net_result_t result =
        neverc_tcp_try_read(a, buf, sizeof(buf));
    check_int("try read would block",
              result.status, NEVERC_NET_WOULD_BLOCK);
    check_int("try read operation",
              strcmp(result.operation, "read") == 0, 1);

    check_int("controlled write", neverc_tcp_write(b, "ok", 2), 2);
    result = neverc_tcp_try_read(a, buf, sizeof(buf));
    check_int("try read success", result.status, NEVERC_NET_OK);
    check_int("try read bytes", (int)result.transferred, 2);

    result = neverc_tcp_write_context(b, NULL, "ctx", 3);
    check_int("context write success", result.status, NEVERC_NET_OK);
    check_int("context write bytes", (int)result.transferred, 3);
    check_int("context write delivered",
              neverc_tcp_read(a, buf, sizeof(buf)), 3);

    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_timeout_handle(NULL, 50, &cancel);
    check_not_null("controlled context", ctx);
    check_not_null("controlled cancel handle", cancel);
    if (ctx && cancel) {
        long long started = test_now_ms();
        result = neverc_tcp_read_context(a, ctx, buf, sizeof(buf));
        long long elapsed = test_now_ms() - started;
        check_int("context deadline status",
                  result.status, NEVERC_NET_TIMEOUT);
        check_int("context deadline bounded",
                  elapsed >= 10 && elapsed < 1000, 1);
    }
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);

    cancel = NULL;
    ctx = neverc_context_with_cancel_handle(NULL, &cancel);
    check_not_null("cancel context", ctx);
    check_not_null("cancel handle", cancel);
    if (ctx && cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        result = neverc_tcp_read_context(a, ctx, buf, sizeof(buf));
        check_int("explicit cancel status",
                  result.status, NEVERC_NET_CANCELLED);
    }
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);

    neverc_tcp_close(a);
    neverc_tcp_close(b);

    const char *err = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("controlled listener", listener);
    if (listener) {
        neverc_tcp_conn_t *accepted = NULL;
        result = neverc_tcp_try_accept(listener, &accepted);
        check_int("try accept would block",
                  result.status, NEVERC_NET_WOULD_BLOCK);
        check_null("try accept has no connection", accepted);

        cancel = NULL;
        ctx = neverc_context_with_timeout_handle(NULL, 50, &cancel);
        if (ctx && cancel) {
            result = neverc_tcp_accept_context(listener, ctx, &accepted);
            check_int("accept context deadline",
                      result.status, NEVERC_NET_TIMEOUT);
            check_null("timed out accept has no connection", accepted);
        }
        neverc_context_cancel_handle_free(cancel);
        neverc_context_free(ctx);

        neverc_tcp_addr_t listener_addr;
        neverc_tcp_listener_addr(listener, &listener_addr);
        char dial_addr[64];
        snprintf(dial_addr, sizeof(dial_addr), "127.0.0.1:%u",
                 listener_addr.port);

        cancel = NULL;
        ctx = neverc_context_with_timeout_handle(NULL, 500, &cancel);
        neverc_tcp_conn_t *dialed = NULL;
        if (ctx && cancel) {
            result = neverc_tcp_dial_context(dial_addr, ctx, &dialed);
            check_int("context dial success",
                      result.status, NEVERC_NET_OK);
            check_not_null("context dial connection", dialed);
        }
        neverc_context_cancel_handle_free(cancel);
        neverc_context_free(ctx);
        if (dialed) {
            neverc_tcp_conn_t *server_conn =
                neverc_tcp_accept(listener, &err);
            check_not_null("accept context dial", server_conn);
            neverc_tcp_close(server_conn);
            neverc_tcp_close(dialed);
        }

        cancel = NULL;
        ctx = neverc_context_with_cancel_handle(NULL, &cancel);
        dialed = NULL;
        if (ctx && cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            result = neverc_tcp_dial_context(dial_addr, ctx, &dialed);
            check_int("cancelled dial",
                      result.status, NEVERC_NET_CANCELLED);
            check_null("cancelled dial connection", dialed);
        }
        neverc_context_cancel_handle_free(cancel);
        neverc_context_free(ctx);
        neverc_tcp_listener_close(listener);
    }
}

/* Split/join and DNS are tested in test_resolve.c */

/* ===== Socket buffer size test ===== */

static void test_socket_options(void) {
    printf("[socket_options]\n");

    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("sockopts ln", ln);

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);
        neverc_tcp_write(c, "x", 1);
        neverc_tcp_close(c);
        _exit(0);
    }

    neverc_tcp_conn_t *c = neverc_tcp_accept(ln, &err);
    if (c) {
        check_int("set keepalive", neverc_tcp_set_keepalive(c, 1), 0);
        check_int("set read buf", neverc_tcp_set_read_buffer(c, 65536), 0);
        check_int("set write buf", neverc_tcp_set_write_buffer(c, 65536), 0);

        char buf[4];
        neverc_tcp_read(c, buf, sizeof(buf));
        neverc_tcp_close(c);
    }

    waitpid(pid, NULL, 0);
    neverc_tcp_listener_close(ln);

    /* Null safety */
    check_int("keepalive null", neverc_tcp_set_keepalive(NULL, 1), -1);
    check_int("readbuf null", neverc_tcp_set_read_buffer(NULL, 1024), -1);
    check_int("writebuf null", neverc_tcp_set_write_buffer(NULL, 1024), -1);
}

/* After try_read switches the socket to non-blocking, a peer FIN must still
 * surface as EOF. Darwin poll() can miss that; the waiter uses kqueue. */
static void test_fin_after_try_read(void) {
    printf("[fin_after_try_read]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("fin listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);
        if (neverc_tcp_write(c, "hi", 2) != 2) {
            neverc_tcp_close(c);
            _exit(2);
        }
        if (neverc_tcp_shutdown_write(c) != 0) {
            neverc_tcp_close(c);
            _exit(3);
        }
        usleep(200000);
        neverc_tcp_close(c);
        _exit(0);
    }

    neverc_tcp_conn_t *c = neverc_tcp_accept(ln, &err);
    check_not_null("fin accept", c);
    if (c) {
        check_int("fin read timeout", neverc_tcp_set_read_timeout(c, 2000), 0);
        char buf[8];
        neverc_net_result_t result = neverc_tcp_try_read(c, buf, sizeof(buf));
        if (result.status == NEVERC_NET_WOULD_BLOCK) {
            int n = neverc_tcp_read(c, buf, sizeof(buf));
            check_int("fin drained payload", n, 2);
            result = neverc_tcp_try_read(c, buf, sizeof(buf));
            if (result.status == NEVERC_NET_WOULD_BLOCK)
                result.status = neverc_tcp_read(c, buf, sizeof(buf)) == 0
                    ? NEVERC_NET_EOF : NEVERC_NET_SYSTEM;
        } else if (result.status == NEVERC_NET_OK) {
            check_int("fin first payload", (int)result.transferred, 2);
            int n = neverc_tcp_read(c, buf, sizeof(buf));
            result.status = n == 0 ? NEVERC_NET_EOF : NEVERC_NET_SYSTEM;
        }
        check_int("FIN after nonblocking read",
                  result.status, NEVERC_NET_EOF);
        neverc_tcp_close(c);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    check_int("fin client ok",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
    neverc_tcp_listener_close(ln);
}

#endif /* _WIN32 */

int main(void) {
    test_listen_close();
    test_listen_explicit();
    test_listen_ipv6();
    test_invalid_addr();
    test_dial_fail();
    test_null_safety();
    test_options();
#ifndef _WIN32
    test_echo();
    test_ipv6_echo();
    test_ipv4_mapped_addr();
    test_dial_address_fallback();
    test_multiple_clients();
    test_large_data();
    test_nodelay();
    test_pipe();
    test_independent_timeouts();
    test_controlled_io();
    test_socket_options();
    test_fin_after_try_read();
#endif

    printf("\n--- net/tcp: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
