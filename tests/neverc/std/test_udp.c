#include "neverc/std/net/udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
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
    else { tests_failed++; printf("  FAIL: %s: expected NULL\n", name); }
}

/* ===== listen + close ===== */

static void test_listen_close(void) {
    printf("[listen_close]\n");
    const char *err = NULL;
    neverc_udp_conn_t *conn = neverc_udp_listen(":0", &err);
    check_not_null("listen :0", conn);
    check_null("no error", err);

    if (conn) {
        neverc_udp_addr_t addr;
        neverc_udp_local_addr(conn, &addr);
        check_int("port > 0", addr.port > 0, 1);
        printf("    bound to %s (port %d)\n", addr.addr, addr.port);
        neverc_udp_close(conn);
    }
}

/* ===== resolve ===== */

static void test_resolve(void) {
    printf("[resolve]\n");
    neverc_udp_addr_t addr;
    int rc = neverc_udp_resolve_addr("127.0.0.1:12345", &addr);
    check_int("resolve ok", rc, 0);
    check_int("resolve port", addr.port, 12345);
}

/* ===== invalid address ===== */

static void test_invalid_addr(void) {
    printf("[invalid_addr]\n");
    const char *err = NULL;

    neverc_udp_conn_t *c = neverc_udp_listen("", &err);
    check_null("empty addr", c);

    c = neverc_udp_listen("no_colon", &err);
    check_null("no colon", c);
}

/* ===== null safety ===== */

static void test_null_safety(void) {
    printf("[null_safety]\n");
    check_int("write null", neverc_udp_write(NULL, "x", 1), -1);
    check_int("read null", neverc_udp_read(NULL, (void*)"x", 1), -1);
    check_int("local_addr null", neverc_udp_local_addr(NULL, NULL), -1);
    check_int("set_timeout null", neverc_udp_set_timeout(NULL, 100), -1);
    check_int("set_broadcast null", neverc_udp_set_broadcast(NULL, 1), -1);
    check_int("resolve null", neverc_udp_resolve_addr(NULL, NULL), -1);
    neverc_udp_close(NULL);
    tests_passed++; tests_run++;
}

/* ===== options ===== */

static void test_options(void) {
    printf("[options]\n");
    const char *err = NULL;
    neverc_udp_conn_t *conn = neverc_udp_listen(":0", &err);
    check_not_null("options conn", conn);
    if (conn) {
        check_int("timeout", neverc_udp_set_timeout(conn, 1000), 0);
        check_int("broadcast", neverc_udp_set_broadcast(conn, 1), 0);
        neverc_udp_close(conn);
    }
}

#ifndef _WIN32

/* ===== echo test ===== */

static void test_echo(void) {
    printf("[echo]\n");
    const char *err = NULL;

    neverc_udp_conn_t *server = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("echo server", server);
    if (!server) return;

    neverc_udp_addr_t saddr;
    neverc_udp_local_addr(server, &saddr);
    neverc_udp_set_timeout(server, 2000);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "127.0.0.1:%d", saddr.port);

        neverc_udp_conn_t *client = neverc_udp_dial(addr_str, &err);
        if (!client) _exit(1);

        neverc_udp_write(client, "ping", 4);

        char buf[64];
        int n = neverc_udp_read(client, buf, sizeof(buf));
        buf[n > 0 ? n : 0] = '\0';

        neverc_udp_close(client);
        _exit(strcmp(buf, "pong") == 0 ? 0 : 2);
    }

    neverc_udp_addr_t from;
    char buf[64];
    int n = neverc_udp_read_from(server, buf, sizeof(buf), &from);
    check_int("server read > 0", n > 0, 1);
    buf[n > 0 ? n : 0] = '\0';
    check_int("server got ping", strcmp(buf, "ping") == 0, 1);

    int sent = neverc_udp_write_to(server, "pong", 4, &from);
    check_int("server sent > 0", sent > 0, 1);

    int status;
    waitpid(pid, &status, 0);
    check_int("client exit 0", WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);

    neverc_udp_close(server);
}

/* ===== multiple datagrams ===== */

static void test_multiple_datagrams(void) {
    printf("[multiple_datagrams]\n");
    const char *err = NULL;

    neverc_udp_conn_t *server = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("multi server", server);
    if (!server) return;

    neverc_udp_addr_t saddr;
    neverc_udp_local_addr(server, &saddr);
    neverc_udp_set_timeout(server, 2000);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "127.0.0.1:%d", saddr.port);
        neverc_udp_conn_t *c = neverc_udp_dial(addr_str, &err);
        if (!c) _exit(1);
        for (int i = 0; i < 5; i++) {
            char msg[16];
            snprintf(msg, sizeof(msg), "msg%d", i);
            neverc_udp_write(c, msg, strlen(msg));
            usleep(10000);
        }
        neverc_udp_close(c);
        _exit(0);
    }

    int received = 0;
    for (int i = 0; i < 5; i++) {
        neverc_udp_addr_t from;
        char rbuf[64];
        int rn = neverc_udp_read_from(server, rbuf, sizeof(rbuf), &from);
        if (rn > 0) received++;
    }
    check_int("received 5", received, 5);

    waitpid(pid, NULL, 0);
    neverc_udp_close(server);
}

/* ===== loopback write_to / read_from ===== */

static void test_write_to_read_from(void) {
    printf("[write_to_read_from]\n");
    const char *err = NULL;

    neverc_udp_conn_t *a = neverc_udp_listen("127.0.0.1:0", &err);
    neverc_udp_conn_t *b = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("socket a", a);
    check_not_null("socket b", b);
    if (!a || !b) {
        if (a) neverc_udp_close(a);
        if (b) neverc_udp_close(b);
        return;
    }

    neverc_udp_set_timeout(b, 1000);

    neverc_udp_addr_t baddr;
    neverc_udp_local_addr(b, &baddr);

    neverc_udp_addr_t resolved;
    char addr_str[64];
    snprintf(addr_str, sizeof(addr_str), "127.0.0.1:%d", baddr.port);
    neverc_udp_resolve_addr(addr_str, &resolved);

    int sent = neverc_udp_write_to(a, "hello", 5, &resolved);
    check_int("sent > 0", sent > 0, 1);

    neverc_udp_addr_t from;
    char buf[64];
    int n = neverc_udp_read_from(b, buf, sizeof(buf), &from);
    check_int("recv 5", n, 5);
    buf[n > 0 ? n : 0] = '\0';
    check_int("got hello", strcmp(buf, "hello") == 0, 1);

    neverc_udp_close(a);
    neverc_udp_close(b);
}

#endif /* _WIN32 */

int main(void) {
    test_listen_close();
    test_resolve();
    test_invalid_addr();
    test_null_safety();
    test_options();
#ifndef _WIN32
    test_echo();
    test_multiple_datagrams();
    test_write_to_read_from();
#endif

    printf("\n--- net/udp: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
