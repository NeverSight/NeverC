#include "neverc/std/net/udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
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
        check_int("UDP handle valid",
                  neverc_udp_conn_handle(conn) != NEVERC_NET_INVALID_HANDLE,
                  1);
        printf("    bound to %s (port %d)\n", addr.addr, addr.port);
        neverc_udp_close(conn);
    }
}

/* ===== IPv6 listen ===== */

static void test_listen_ipv6(void) {
    printf("[listen_ipv6]\n");
    const char *err = NULL;
    neverc_udp_conn_t *conn = neverc_udp_listen("[::1]:0", &err);
    check_not_null("listen [::1]:0", conn);
    check_null("IPv6 listen no error", err);

    if (conn) {
        neverc_udp_addr_t addr;
        check_int("IPv6 local addr", neverc_udp_local_addr(conn, &addr), 0);
        check_int("IPv6 port > 0", addr.port > 0, 1);
        check_int("IPv6 addr bracketed", addr.addr[0] == '[', 1);
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

    rc = neverc_udp_resolve_addr("[::1]:23456", &addr);
    check_int("resolve IPv6 ok", rc, 0);
    check_int("resolve IPv6 port", addr.port, 23456);
    check_int("resolve IPv6 bracketed", addr.addr[0] == '[', 1);

    rc = neverc_udp_resolve_addr("[::ffff:127.0.0.1]:12345", &addr);
    check_int("resolve ipv4-mapped ok", rc, 0);
    check_int("resolve ipv4-mapped port", addr.port, 12345);
    check_int("resolve ipv4-mapped is ipv4 text",
              strncmp(addr.addr, "127.0.0.1:", 10) == 0, 1);
    check_int("resolve ipv4-mapped not ffff",
              strstr(addr.addr, "ffff") == NULL, 1);
}

/* ===== invalid address ===== */

static void test_invalid_addr(void) {
    printf("[invalid_addr]\n");
    const char *err = NULL;

    neverc_udp_conn_t *c = neverc_udp_listen("", &err);
    check_null("empty addr", c);

    c = neverc_udp_listen("no_colon", &err);
    check_null("no colon", c);

    c = neverc_udp_listen("127.0.0.1:65536", &err);
    check_null("listen port overflow", c);

    c = neverc_udp_listen("127.0.0.1:abc", &err);
    check_null("listen nonnumeric port", c);

    c = neverc_udp_dial(":80", &err);
    check_null("dial empty host", c);
    if (c) neverc_udp_close(c);

    neverc_udp_addr_t addr;
    check_int("resolve port overflow",
              neverc_udp_resolve_addr("127.0.0.1:65536", &addr), -1);
}

/* ===== null safety ===== */

static void test_null_safety(void) {
    printf("[null_safety]\n");
    check_int("write null", neverc_udp_write(NULL, "x", 1), -1);
    check_int("read null", neverc_udp_read(NULL, (void*)"x", 1), -1);
    check_int("local_addr null", neverc_udp_local_addr(NULL, NULL), -1);
    check_int("UDP handle null",
              neverc_udp_conn_handle(NULL) == NEVERC_NET_INVALID_HANDLE, 1);
    check_int("set_timeout null", neverc_udp_set_timeout(NULL, 100), -1);
    check_int("set_read_timeout null",
              neverc_udp_set_read_timeout(NULL, 100), -1);
    check_int("set_write_timeout null",
              neverc_udp_set_write_timeout(NULL, 100), -1);
    check_int("set_read_deadline null",
              neverc_udp_set_read_deadline(NULL, 1), -1);
    check_int("set_write_deadline null",
              neverc_udp_set_write_deadline(NULL, 1), -1);
    check_int("MTU info null", neverc_udp_get_mtu_info(NULL, NULL), -1);
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
        check_int("read timeout", neverc_udp_set_read_timeout(conn, 250), 0);
        check_int("write timeout", neverc_udp_set_write_timeout(conn, 500), 0);
        check_int("negative read timeout",
                  neverc_udp_set_read_timeout(conn, -1), -1);
        check_int("negative write timeout",
                  neverc_udp_set_write_timeout(conn, -1), -1);
        check_int("absolute read deadline",
                  neverc_udp_set_read_deadline(conn, 1), 0);
        check_int("absolute write deadline",
                  neverc_udp_set_write_deadline(conn, 1), 0);
        check_int("broadcast", neverc_udp_set_broadcast(conn, 1), 0);
        neverc_udp_close(conn);
    }

    conn = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("MTU info conn", conn);
    if (conn) {
        neverc_udp_mtu_info_t mtu;
        check_int("get MTU info", neverc_udp_get_mtu_info(conn, &mtu), 0);
        check_int("IPv4 protocol payload max",
                  mtu.protocol_max_payload == 65507, 1);
        check_int("path payload bounded",
                  mtu.path_max_payload > 0 &&
                  mtu.path_max_payload <= mtu.protocol_max_payload, 1);
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

/* IPv4-mapped IPv6 must format as a.b.c.d:port (ACL). Dual-stack
 * listen on [::]:0 must accept IPv4 and report the same. */
static void test_ipv4_mapped_addr(void) {
    printf("[ipv4_mapped_addr]\n");
    const char *err = NULL;

    neverc_udp_conn_t *server = neverc_udp_listen("[::]:0", &err);
    if (!server) {
        check_int("dual-stack listen unavailable", 1, 1);
        return;
    }

    neverc_udp_addr_t saddr;
    neverc_udp_local_addr(server, &saddr);
    neverc_udp_set_timeout(server, 2000);

    pid_t pid = fork();
    if (pid == 0) {
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "127.0.0.1:%d", saddr.port);
        neverc_udp_conn_t *client = neverc_udp_dial(addr_str, &err);
        if (!client) _exit(1);
        int n = neverc_udp_write(client, "v4", 2);
        neverc_udp_close(client);
        _exit(n == 2 ? 0 : 2);
    }

    neverc_udp_addr_t from;
    char buf[8];
    int n = neverc_udp_read_from(server, buf, sizeof(buf), &from);
    int status = 0;
    waitpid(pid, &status, 0);
    if (n == 2) {
        check_int("dual-stack ipv4 payload", memcmp(buf, "v4", 2) == 0, 1);
        check_int("dual-stack peer is ipv4 text",
                  strncmp(from.addr, "127.0.0.1:", 10) == 0, 1);
        check_int("dual-stack peer not ffff",
                  strstr(from.addr, "ffff") == NULL, 1);
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
        check_int("dual-stack ipv4 dial skipped", 1, 1);
    } else {
        check_int("dual-stack ipv4 datagram", 0, 1);
    }
    neverc_udp_close(server);
}

static void test_read_timeout_errno(void) {
    printf("[read_timeout_errno]\n");
    const char *err = NULL;
    neverc_udp_conn_t *conn = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("timeout conn", conn);
    if (!conn) return;
    check_int("set short read timeout",
              neverc_udp_set_read_timeout(conn, 50), 0);
    char buf[8];
    neverc_udp_addr_t from;
    int n = neverc_udp_read_from(conn, buf, sizeof(buf), &from);
    check_int("udp read timed out", n, -1);
    check_int("udp timeout errno", errno == ETIMEDOUT, 1);
    neverc_udp_close(conn);
}

/* ===== IPv6 datagram test ===== */

static void test_ipv6_datagram(void) {
    printf("[ipv6_datagram]\n");
    const char *err = NULL;

    neverc_udp_conn_t *server = neverc_udp_listen("[::1]:0", &err);
    check_not_null("IPv6 UDP server", server);
    if (!server) return;

    neverc_udp_addr_t saddr;
    neverc_udp_local_addr(server, &saddr);

    pid_t pid = fork();
    if (pid == 0) {
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "[::1]:%d", saddr.port);
        neverc_udp_conn_t *client = neverc_udp_dial(addr_str, &err);
        if (!client) _exit(1);
        int n = neverc_udp_write(client, "v6", 2);
        neverc_udp_close(client);
        _exit(n == 2 ? 0 : 2);
    }

    int status;
    waitpid(pid, &status, 0);
    int client_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    check_int("IPv6 UDP client sent", client_ok, 1);
    if (client_ok) {
        neverc_udp_addr_t from;
        char buf[8];
        int n = neverc_udp_read_from(server, buf, sizeof(buf), &from);
        check_int("IPv6 UDP read", n, 2);
        check_int("IPv6 sender bracketed", from.addr[0] == '[', 1);
    }

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

/* ===== truncation-aware packet reads ===== */

static void test_truncation_detection(void) {
    printf("[truncation_detection]\n");
    const char *err = NULL;

    neverc_udp_conn_t *sender = neverc_udp_listen("127.0.0.1:0", &err);
    neverc_udp_conn_t *receiver = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("truncation sender", sender);
    check_not_null("truncation receiver", receiver);
    if (!sender || !receiver) {
        neverc_udp_close(sender);
        neverc_udp_close(receiver);
        return;
    }

    neverc_udp_addr_t receiver_addr;
    neverc_udp_local_addr(receiver, &receiver_addr);

    char payload[64];
    memset(payload, 'T', sizeof(payload));
    check_int("send oversized datagram",
              neverc_udp_write_to(sender, payload, sizeof(payload),
                                  &receiver_addr),
              (int)sizeof(payload));

    char small[8];
    neverc_udp_packet_info_t info;
    int n = neverc_udp_read_packet(receiver, small, sizeof(small), &info);
    check_int("truncated bytes copied", n, (int)sizeof(small));
    check_int("truncation reported", info.truncated, 1);
    check_int("full datagram length reported",
              info.datagram_len == sizeof(payload), 1);
    check_int("packet source recorded", info.source.port > 0, 1);
    check_int("packet destination recorded",
              info.destination.port == receiver_addr.port, 1);
    check_int("packet interface recorded", info.interface_index > 0, 1);

    check_int("send second oversized datagram",
              neverc_udp_write_to(sender, payload, sizeof(payload),
                                  &receiver_addr),
              (int)sizeof(payload));
    neverc_udp_addr_t from;
    n = neverc_udp_read_from(receiver, small, sizeof(small), &from);
    check_int("read_from copies prefix not error", n, (int)sizeof(small));
    check_int("read_from source recorded", from.port > 0, 1);

    check_int("send third oversized datagram",
              neverc_udp_write_to(sender, payload, sizeof(payload),
                                  &receiver_addr),
              (int)sizeof(payload));
    n = neverc_udp_read(receiver, small, sizeof(small));
    check_int("read copies prefix not error", n, (int)sizeof(small));

    neverc_udp_close(sender);
    neverc_udp_close(receiver);
}

/* ===== bounded batch send/receive ===== */

static void test_batch_io(void) {
    printf("[batch_io]\n");
    const char *err = NULL;

    neverc_udp_conn_t *sender = neverc_udp_listen("127.0.0.1:0", &err);
    neverc_udp_conn_t *receiver = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("batch sender", sender);
    check_not_null("batch receiver", receiver);
    if (!sender || !receiver) {
        neverc_udp_close(sender);
        neverc_udp_close(receiver);
        return;
    }

    int timeout_status = neverc_udp_set_read_timeout(receiver, 2000);
    check_int("batch read timeout", timeout_status, 0);
    if (timeout_status != 0) {
        neverc_udp_close(sender);
        neverc_udp_close(receiver);
        return;
    }

    neverc_udp_addr_t receiver_addr;
    neverc_udp_local_addr(receiver, &receiver_addr);
    const char *payloads[] = {"one", "two", "three", "four"};
    neverc_udp_send_message_t outgoing[4];
    memset(outgoing, 0, sizeof(outgoing));
    for (int i = 0; i < 4; ++i) {
        outgoing[i].data = payloads[i];
        outgoing[i].len = strlen(payloads[i]);
        outgoing[i].destination = &receiver_addr;
    }
    check_int("batch sent",
              neverc_udp_write_batch(sender, outgoing, 4), 4);

    char storage[4][16];
    neverc_udp_recv_message_t incoming[4];
    memset(incoming, 0, sizeof(incoming));
    for (int i = 0; i < 4; ++i) {
        incoming[i].data = storage[i];
        incoming[i].capacity = sizeof(storage[i]);
    }
    int received = 0;
    int batch_received = 0;
    while (received < 4) {
        batch_received = neverc_udp_read_batch(
            receiver, &incoming[received], (size_t)(4 - received));
        if (batch_received <= 0) break;
        received += batch_received;
    }
    if (received < 4)
        printf("  batch receive stopped after %d packets (last result %d)\n",
               received, batch_received);
    check_int("batch received", received, 4);
    for (int i = 0; i < received; ++i) {
        storage[i][incoming[i].len] = '\0';
        check_int("batch payload",
                  strcmp(storage[i], payloads[i]) == 0, 1);
        check_int("batch not truncated", incoming[i].info.truncated, 0);
        check_int("batch source", incoming[i].info.source.port > 0, 1);
    }

    check_int("batch recv rejects zero",
              neverc_udp_read_batch(receiver, incoming, 0), -1);
    check_int("batch send rejects zero",
              neverc_udp_write_batch(sender, outgoing, 0), -1);

    neverc_udp_close(sender);
    neverc_udp_close(receiver);
}

/* ===== bounded receive queue ===== */

static void test_bounded_queue(void) {
    printf("[bounded_queue]\n");
    const char *err = NULL;

    check_null("queue rejects zero capacity",
               neverc_udp_queue_create(0, 8));
    check_null("queue rejects zero payload",
               neverc_udp_queue_create(1, 0));
    check_null("queue rejects oversized capacity",
               neverc_udp_queue_create(
                   (size_t)NEVERC_UDP_MAX_QUEUE_CAPACITY + 1, 8));
    check_null("queue rejects oversized payload",
               neverc_udp_queue_create(
                   1, (size_t)NEVERC_UDP_MAX_DATAGRAM_SIZE + 1));

    neverc_udp_conn_t *sender = neverc_udp_listen("127.0.0.1:0", &err);
    neverc_udp_conn_t *receiver = neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("queue sender", sender);
    check_not_null("queue receiver", receiver);
    if (!sender || !receiver) {
        neverc_udp_close(sender);
        neverc_udp_close(receiver);
        return;
    }

    neverc_udp_queue_t *queue = neverc_udp_queue_create(2, 8);
    check_not_null("queue create", queue);
    if (!queue) {
        neverc_udp_close(sender);
        neverc_udp_close(receiver);
        return;
    }
    check_int("queue capacity", (int)neverc_udp_queue_capacity(queue), 2);
    check_int("queue initially empty", (int)neverc_udp_queue_length(queue), 0);

    neverc_udp_addr_t destination;
    neverc_udp_local_addr(receiver, &destination);
    check_int("reject oversized datagram",
              neverc_udp_write_to(
                  sender, "x",
                  (size_t)NEVERC_UDP_MAX_DATAGRAM_SIZE + 1,
                  &destination),
              -1);
    neverc_udp_addr_t invalid_destination = destination;
    invalid_destination._sa_len =
        (int)sizeof(invalid_destination._sa) + 1;
    check_int("reject invalid destination size",
              neverc_udp_write_to(
                  sender, "x", 1, &invalid_destination),
              -1);
    const char *payloads[] = {"one", "two", "tri"};
    neverc_udp_send_message_t outgoing[3];
    memset(outgoing, 0, sizeof(outgoing));
    for (int i = 0; i < 3; ++i) {
        outgoing[i].data = payloads[i];
        outgoing[i].len = 3;
        outgoing[i].destination = &destination;
    }
    check_int("queue packets sent",
              neverc_udp_write_batch(sender, outgoing, 3), 3);

    check_int("queue fills only capacity",
              neverc_udp_queue_receive(receiver, queue, 3), 2);
    check_int("queue full length", (int)neverc_udp_queue_length(queue), 2);

    char data[8];
    neverc_udp_packet_info_t info;
    neverc_net_result_t result =
        neverc_udp_queue_pop(queue, data, sizeof(data), &info);
    check_int("queue first pop", result.status, NEVERC_NET_OK);
    check_int("queue first length", (int)result.transferred, 3);
    check_int("queue first order", memcmp(data, "one", 3) == 0, 1);

    result = neverc_udp_queue_pop(queue, data, 2, &info);
    check_int("queue small pop truncated",
              result.status, NEVERC_NET_TRUNCATED);
    check_int("queue small pop length", (int)result.transferred, 2);
    check_int("queue second order", memcmp(data, "tw", 2) == 0, 1);
    check_int("queue small pop metadata", info.truncated, 1);
    check_int("queue full datagram metadata",
              (int)info.datagram_len, 3);

    check_int("queue receives retained packet",
              neverc_udp_queue_receive(receiver, queue, 3), 1);
    result = neverc_udp_queue_pop(queue, data, sizeof(data), &info);
    check_int("queue third pop", result.status, NEVERC_NET_OK);
    check_int("queue third order", memcmp(data, "tri", 3) == 0, 1);
    result = neverc_udp_queue_pop(queue, data, sizeof(data), &info);
    check_int("queue empty status",
              result.status, NEVERC_NET_WOULD_BLOCK);

    neverc_udp_queue_free(queue);
    neverc_udp_close(sender);
    neverc_udp_close(receiver);
}

/* ===== structured non-blocking and context-aware I/O ===== */

static void test_controlled_io(void) {
    printf("[controlled_io]\n");
    const char *err = NULL;

    neverc_udp_conn_t *receiver =
        neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("controlled UDP receiver", receiver);
    if (!receiver) return;

    char buf[8];
    neverc_udp_packet_info_t info;
    neverc_net_result_t result =
        neverc_udp_try_read_packet(receiver, buf, sizeof(buf), &info);
    check_int("UDP try read would block",
              result.status, NEVERC_NET_WOULD_BLOCK);
    check_int("UDP try read operation",
              strcmp(result.operation, "read") == 0, 1);

    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_timeout_handle(NULL, 50, &cancel);
    check_not_null("UDP timeout context", ctx);
    check_not_null("UDP timeout cancel", cancel);
    if (ctx && cancel) {
        result = neverc_udp_read_packet_context(
            receiver, ctx, buf, sizeof(buf), &info);
        check_int("UDP context deadline",
                  result.status, NEVERC_NET_TIMEOUT);
    }
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);

    cancel = NULL;
    ctx = neverc_context_with_cancel_handle(NULL, &cancel);
    check_not_null("UDP cancel context", ctx);
    check_not_null("UDP cancel handle", cancel);
    if (ctx && cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        result = neverc_udp_read_packet_context(
            receiver, ctx, buf, sizeof(buf), &info);
        check_int("UDP explicit cancel",
                  result.status, NEVERC_NET_CANCELLED);
    }
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);

    neverc_udp_conn_t *sender =
        neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("controlled UDP sender", sender);
    if (sender) {
        neverc_udp_addr_t destination;
        neverc_udp_local_addr(receiver, &destination);
        result = neverc_udp_try_write(
            sender, "try", 3, &destination);
        check_int("UDP try write", result.status, NEVERC_NET_OK);
        check_int("UDP try write bytes", (int)result.transferred, 3);
        check_int("UDP try write delivered",
                  neverc_udp_read_packet(receiver, buf, sizeof(buf), &info), 3);

        result = neverc_udp_write_context(
            sender, NULL, "ctx", 3, &destination);
        check_int("UDP context write", result.status, NEVERC_NET_OK);
        check_int("UDP context write delivered",
                  neverc_udp_read_packet(receiver, buf, sizeof(buf), &info), 3);
        neverc_udp_close(sender);
    }

    neverc_udp_close(receiver);
}

#endif /* _WIN32 */

#ifdef _WIN32
/* A send to a recently closed UDP peer produces an ICMP Port Unreachable.
 * Winsock reports that asynchronous error as WSAECONNRESET on the shared
 * listening socket unless SIO_UDP_CONNRESET is disabled.  The error must not
 * hide a subsequent datagram from a healthy peer. */
static void test_icmp_reset_does_not_poison_listener(void) {
    printf("[icmp_reset_does_not_poison_listener]\n");
    const char *err = NULL;
    neverc_udp_conn_t *server =
        neverc_udp_listen("127.0.0.1:0", &err);
    check_not_null("ICMP reset server", server);
    if (!server) return;

    neverc_udp_addr_t server_addr;
    check_int("ICMP reset server address",
              neverc_udp_local_addr(server, &server_addr), 0);
    char address[64];
    snprintf(address, sizeof(address), "127.0.0.1:%d", server_addr.port);
    neverc_udp_conn_t *stale = neverc_udp_dial(address, &err);
    neverc_udp_conn_t *healthy = neverc_udp_dial(address, &err);
    check_not_null("stale UDP peer", stale);
    check_not_null("healthy UDP peer", healthy);
    if (!stale || !healthy) {
        neverc_udp_close(stale);
        neverc_udp_close(healthy);
        neverc_udp_close(server);
        return;
    }

    check_int("stale peer seed send", neverc_udp_write(stale, "seed", 4), 4);
    neverc_udp_packet_info_t seed_info;
    char buffer[16];
    check_int("stale peer seed receive",
              neverc_udp_read_packet(server, buffer, sizeof(buffer),
                                     &seed_info),
              4);
    neverc_udp_addr_t stale_addr = seed_info.source;
    neverc_udp_close(stale);

    check_int("send to closed UDP peer",
              neverc_udp_write_to(server, "probe", 5, &stale_addr), 5);
    Sleep(100);
    check_int("healthy peer send",
              neverc_udp_write(healthy, "valid", 5), 5);
    neverc_udp_packet_info_t packet_info;
    int received = neverc_udp_read_packet(server, buffer, sizeof(buffer),
                                          &packet_info);
    if (received < 0)
        printf("  listener socket error after ICMP: %d\n",
               WSAGetLastError());
    check_int("listener survives ICMP reset", received, 5);
    check_int("listener receives healthy payload",
              received == 5 && memcmp(buffer, "valid", 5) == 0, 1);

    neverc_udp_close(healthy);
    neverc_udp_close(server);
}
#endif

int main(void) {
    test_listen_close();
    test_listen_ipv6();
    test_resolve();
    test_invalid_addr();
    test_null_safety();
    test_options();
#ifndef _WIN32
    test_echo();
    test_ipv4_mapped_addr();
    test_read_timeout_errno();
    test_ipv6_datagram();
    test_multiple_datagrams();
    test_write_to_read_from();
    test_truncation_detection();
    test_batch_io();
    test_bounded_queue();
    test_controlled_io();
#else
    test_icmp_reset_does_not_poison_listener();
#endif

    printf("\n--- net/udp: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
