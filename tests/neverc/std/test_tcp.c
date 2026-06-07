#include "neverc/std/net/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
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

/* ===== invalid address ===== */

static void test_invalid_addr(void) {
    printf("[invalid_addr]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("", &err);
    check_null("empty addr", ln);

    ln = neverc_tcp_listen("no_colon", &err);
    check_null("no colon", ln);
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
    check_int("listener_addr null", neverc_tcp_listener_addr(NULL, NULL), -1);
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

#endif /* _WIN32 */

int main(void) {
    test_listen_close();
    test_listen_explicit();
    test_invalid_addr();
    test_dial_fail();
    test_null_safety();
    test_options();
#ifndef _WIN32
    test_echo();
    test_multiple_clients();
    test_large_data();
    test_nodelay();
#endif

    printf("\n--- net/tcp: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
