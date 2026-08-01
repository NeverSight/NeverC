#include "neverc/std/net/tcp.h"
#include "neverc/std/net/udp.h"
#include "neverc/std/thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#endif

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    neverc_tcp_listener_t *listener;
    int result;
} tcp_echo_task_t;

typedef struct {
    neverc_udp_conn_t *server;
    int result;
} udp_echo_task_t;

typedef struct {
    neverc_tcp_listener_t *listener;
    neverc_thread_channel_t *ready;
    neverc_thread_channel_t *release;
    int result;
} tcp_slow_reader_task_t;

typedef struct {
    neverc_udp_conn_t *receiver;
    int result;
} udp_truncated_read_task_t;

typedef struct {
    neverc_tcp_listener_t *listener;
    neverc_context_t *context;
    neverc_thread_channel_t *ready;
    neverc_thread_channel_t *start;
    neverc_net_status_t status;
} tcp_context_accept_task_t;

static int64_t transport_now_ms(void) {
#ifdef _WIN32
    FILETIME filetime;
    GetSystemTimeAsFileTime(&filetime);
    uint64_t ticks = ((uint64_t)filetime.dwHighDateTime << 32) |
                     filetime.dwLowDateTime;
    return (int64_t)(ticks / 10000U - 11644473600000ULL);
#else
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

static void transport_sleep_ms(int milliseconds) {
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec interval;
    interval.tv_sec = milliseconds / 1000;
    interval.tv_nsec = (milliseconds % 1000) * 1000 * 1000;
    while (nanosleep(&interval, &interval) != 0 && errno == EINTR) {
    }
#endif
}

static void tcp_echo_task(void *opaque) {
    tcp_echo_task_t *task = (tcp_echo_task_t *)opaque;
    const char *error = NULL;
    neverc_tcp_conn_t *conn =
        neverc_tcp_accept(task->listener, &error);
    if (!conn) {
        task->result = 1;
        return;
    }

    (void)neverc_tcp_set_timeout(conn, 2000);
    char buffer[16];
    int n = neverc_tcp_read(conn, buffer, sizeof(buffer));
    if (n != 4 || memcmp(buffer, "ping", 4) != 0 ||
        neverc_tcp_write(conn, "pong", 4) != 4) {
        task->result = 2;
    }
    neverc_tcp_close(conn);
}

static void udp_echo_task(void *opaque) {
    udp_echo_task_t *task = (udp_echo_task_t *)opaque;
    char buffer[16];
    neverc_udp_packet_info_t info;
    int n = neverc_udp_read_packet(
        task->server, buffer, sizeof(buffer), &info);
    if (n != 4 || memcmp(buffer, "ping", 4) != 0 ||
        neverc_udp_write_to(task->server, "pong", 4,
                            &info.source) != 4) {
        task->result = 1;
    }
}

static void tcp_disconnect_task(void *opaque) {
    tcp_echo_task_t *task = (tcp_echo_task_t *)opaque;
    const char *error = NULL;
    neverc_tcp_conn_t *conn =
        neverc_tcp_accept(task->listener, &error);
    if (!conn) {
        task->result = 1;
        return;
    }
    char byte;
    task->result = neverc_tcp_read(conn, &byte, 1) == 0 ? 0 : 2;
    neverc_tcp_close(conn);
}

static void tcp_slow_reader_task(void *opaque) {
    tcp_slow_reader_task_t *task = (tcp_slow_reader_task_t *)opaque;
    const char *error = NULL;
    neverc_tcp_conn_t *conn =
        neverc_tcp_accept(task->listener, &error);
    if (!conn) {
        task->result = 1;
        (void)neverc_thread_channel_send(task->ready, NULL);
        return;
    }
    if (neverc_tcp_set_read_buffer(conn, 4096) != 0)
        task->result = 2;
    if (neverc_thread_channel_send(task->ready, NULL) !=
        NEVERC_THREAD_OK) {
        task->result = 3;
        neverc_tcp_close(conn);
        return;
    }
    void *release = NULL;
    if (neverc_thread_channel_receive(task->release, &release) !=
        NEVERC_THREAD_OK) {
        task->result = 4;
    }
    neverc_tcp_close(conn);
}

static void udp_truncated_read_task(void *opaque) {
    udp_truncated_read_task_t *task =
        (udp_truncated_read_task_t *)opaque;
    char buffer[8];
    neverc_udp_packet_info_t info;
    int n = neverc_udp_read_packet(
        task->receiver, buffer, sizeof(buffer), &info);
    if (n != (int)sizeof(buffer) || !info.truncated ||
        info.datagram_len != 64) {
        task->result = 1;
    }
}

static void tcp_context_accept_task(void *opaque) {
    tcp_context_accept_task_t *task =
        (tcp_context_accept_task_t *)opaque;
    if (neverc_thread_channel_send(task->ready, NULL) !=
        NEVERC_THREAD_OK) {
        task->status = NEVERC_NET_SYSTEM;
        return;
    }
    void *start = NULL;
    if (neverc_thread_channel_receive(task->start, &start) !=
        NEVERC_THREAD_OK) {
        task->status = NEVERC_NET_SYSTEM;
        return;
    }

    neverc_tcp_conn_t *conn = NULL;
    neverc_net_result_t result = neverc_tcp_accept_context(
        task->listener, task->context, &conn);
    task->status = result.status;
    if (conn)
        neverc_tcp_close(conn);
}

static int run_tcp_echo(const char *listen_addr, const char *dial_host) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen(listen_addr, &error);
    CHECK(listener != NULL);
    CHECK(error == NULL);

    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);
    char dial_addr[96];
    snprintf(dial_addr, sizeof(dial_addr), dial_host, local.port);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    CHECK(executor != NULL);
    tcp_echo_task_t task = {listener, 0};
    CHECK(neverc_thread_executor_submit(
              executor, tcp_echo_task, &task) == NEVERC_THREAD_OK);

    neverc_tcp_conn_t *client = neverc_tcp_dial(dial_addr, &error);
    CHECK(client != NULL);
    CHECK(neverc_tcp_set_timeout(client, 2000) == 0);
    CHECK(neverc_tcp_write(client, "ping", 4) == 4);

    char buffer[16];
    CHECK(neverc_tcp_read(client, buffer, sizeof(buffer)) == 4);
    CHECK(memcmp(buffer, "pong", 4) == 0);
    neverc_tcp_close(client);

    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(task.result == 0);
    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(listener);
    return 0;
}

static int run_udp_echo(const char *listen_addr, const char *dial_host) {
    const char *error = NULL;
    neverc_udp_conn_t *server = neverc_udp_listen(listen_addr, &error);
    CHECK(server != NULL);
    CHECK(error == NULL);
    CHECK(neverc_udp_set_timeout(server, 2000) == 0);

    neverc_udp_addr_t local;
    CHECK(neverc_udp_local_addr(server, &local) == 0);
    char dial_addr[96];
    snprintf(dial_addr, sizeof(dial_addr), dial_host, local.port);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    CHECK(executor != NULL);
    udp_echo_task_t task = {server, 0};
    CHECK(neverc_thread_executor_submit(
              executor, udp_echo_task, &task) == NEVERC_THREAD_OK);

    neverc_udp_conn_t *client = neverc_udp_dial(dial_addr, &error);
    CHECK(client != NULL);
    CHECK(neverc_udp_set_timeout(client, 2000) == 0);
    CHECK(neverc_udp_write(client, "ping", 4) == 4);

    char buffer[16];
    CHECK(neverc_udp_read(client, buffer, sizeof(buffer)) == 4);
    CHECK(memcmp(buffer, "pong", 4) == 0);
    neverc_udp_close(client);

    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(task.result == 0);
    neverc_thread_executor_free(executor);
    neverc_udp_close(server);
    return 0;
}

static int run_tcp_multi_client(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(4, 4);
    CHECK(executor != NULL);
    tcp_echo_task_t tasks[4];
    memset(tasks, 0, sizeof(tasks));
    for (int i = 0; i < 4; ++i) {
        tasks[i].listener = listener;
        CHECK(neverc_thread_executor_submit(
                  executor, tcp_echo_task, &tasks[i]) == NEVERC_THREAD_OK);
    }

    char dial_addr[64];
    snprintf(dial_addr, sizeof(dial_addr), "127.0.0.1:%u", local.port);
    for (int i = 0; i < 4; ++i) {
        neverc_tcp_conn_t *client = neverc_tcp_dial(dial_addr, &error);
        CHECK(client != NULL);
        CHECK(neverc_tcp_set_timeout(client, 2000) == 0);
        CHECK(neverc_tcp_write(client, "ping", 4) == 4);
        char buffer[8];
        CHECK(neverc_tcp_read(client, buffer, sizeof(buffer)) == 4);
        CHECK(memcmp(buffer, "pong", 4) == 0);
        neverc_tcp_close(client);
    }
    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    for (int i = 0; i < 4; ++i)
        CHECK(tasks[i].result == 0);

    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(listener);
    return 0;
}

static int run_tcp_disconnect(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    CHECK(executor != NULL);
    tcp_echo_task_t task = {listener, 0};
    CHECK(neverc_thread_executor_submit(
              executor, tcp_disconnect_task, &task) == NEVERC_THREAD_OK);

    char dial_addr[64];
    snprintf(dial_addr, sizeof(dial_addr), "127.0.0.1:%u", local.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(dial_addr, &error);
    CHECK(client != NULL);
    neverc_tcp_close(client);
    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(task.result == 0);

    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(listener);
    return 0;
}

static int run_tcp_slow_reader(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    neverc_thread_channel_t *ready =
        neverc_thread_channel_create(1);
    neverc_thread_channel_t *release =
        neverc_thread_channel_create(1);
    CHECK(executor != NULL);
    CHECK(ready != NULL);
    CHECK(release != NULL);
    tcp_slow_reader_task_t task = {listener, ready, release, 0};
    CHECK(neverc_thread_executor_submit(
              executor, tcp_slow_reader_task, &task) == NEVERC_THREAD_OK);

    char dial_addr[64];
    snprintf(dial_addr, sizeof(dial_addr), "127.0.0.1:%u", local.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(dial_addr, &error);
    CHECK(client != NULL);
    void *ready_signal = NULL;
    CHECK(neverc_thread_channel_receive(ready, &ready_signal) ==
          NEVERC_THREAD_OK);
    CHECK(neverc_tcp_set_write_buffer(client, 4096) == 0);
    CHECK(neverc_tcp_set_write_timeout(client, 50) == 0);

    const size_t payload_size = 16U * 1024U * 1024U;
    char *payload = (char *)malloc(payload_size);
    CHECK(payload != NULL);
    memset(payload, 's', payload_size);
    int written = neverc_tcp_write(client, payload, payload_size);
    free(payload);
    CHECK(written < (int)payload_size);
    neverc_tcp_close(client);

    CHECK(neverc_thread_channel_send(release, NULL) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(task.result == 0);

    neverc_thread_channel_free(ready);
    neverc_thread_channel_free(release);
    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(listener);
    return 0;
}

static int run_absolute_deadlines(void) {
    neverc_tcp_conn_t *tcp_a = NULL;
    neverc_tcp_conn_t *tcp_b = NULL;
    CHECK(neverc_tcp_pipe(&tcp_a, &tcp_b) == 0);
    CHECK(neverc_tcp_set_read_deadline(
              tcp_a, transport_now_ms() + 20) == 0);
    transport_sleep_ms(40);
    char buffer[8];
    neverc_net_result_t result =
        neverc_tcp_try_read(tcp_a, buffer, sizeof(buffer));
    CHECK(result.status == NEVERC_NET_TIMEOUT);
    CHECK(neverc_tcp_set_read_deadline(tcp_a, 0) == 0);
    result = neverc_tcp_try_read(tcp_a, buffer, sizeof(buffer));
    CHECK(result.status == NEVERC_NET_WOULD_BLOCK);
    CHECK(neverc_tcp_set_read_timeout(tcp_a, 20) == 0);
    int64_t started = transport_now_ms();
    CHECK(neverc_tcp_read(tcp_a, buffer, sizeof(buffer)) < 0);
    int64_t elapsed = transport_now_ms() - started;
    CHECK(elapsed >= 10 && elapsed < 1000);
    neverc_tcp_close(tcp_a);
    neverc_tcp_close(tcp_b);

    const char *error = NULL;
    neverc_udp_conn_t *udp =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(udp != NULL);
    CHECK(neverc_udp_set_read_deadline(
              udp, transport_now_ms() + 20) == 0);
    transport_sleep_ms(40);
    neverc_udp_packet_info_t info;
    result = neverc_udp_try_read_packet(
        udp, buffer, sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_TIMEOUT);
    CHECK(neverc_udp_set_read_deadline(udp, 0) == 0);
    result = neverc_udp_try_read_packet(
        udp, buffer, sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_WOULD_BLOCK);
#ifndef _WIN32
    int flags = fcntl((int)neverc_udp_conn_handle(udp), F_GETFL, 0);
    CHECK(flags >= 0);
    CHECK((flags & O_NONBLOCK) != 0);
#endif
    CHECK(neverc_udp_set_read_timeout(udp, 20) == 0);
    started = transport_now_ms();
    CHECK(neverc_udp_read_packet(
              udp, buffer, sizeof(buffer), &info) < 0);
    elapsed = transport_now_ms() - started;
    CHECK(elapsed >= 10 && elapsed < 1000);
    neverc_udp_close(udp);
    return 0;
}

static int run_udp_batch_queue(void) {
    const char *error = NULL;
    neverc_udp_conn_t *sender =
        neverc_udp_listen("127.0.0.1:0", &error);
    neverc_udp_conn_t *receiver =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(sender != NULL);
    CHECK(receiver != NULL);
    CHECK(neverc_udp_set_timeout(receiver, 2000) == 0);

    neverc_udp_addr_t destination;
    CHECK(neverc_udp_local_addr(receiver, &destination) == 0);
    const char *payloads[] = {"one", "two", "tri"};
    neverc_udp_send_message_t outgoing[3];
    memset(outgoing, 0, sizeof(outgoing));
    for (int i = 0; i < 3; ++i) {
        outgoing[i].data = payloads[i];
        outgoing[i].len = 3;
        outgoing[i].destination = &destination;
    }
    CHECK(neverc_udp_write_batch(sender, outgoing, 3) == 3);

    neverc_udp_queue_t *queue = neverc_udp_queue_create(2, 8);
    CHECK(queue != NULL);
    CHECK(neverc_udp_queue_receive(receiver, queue, 3) == 2);
    char buffer[8];
    neverc_udp_packet_info_t info;
    neverc_net_result_t result =
        neverc_udp_queue_pop(queue, buffer, sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_OK);
    CHECK(result.transferred == 3);
    CHECK(memcmp(buffer, "one", 3) == 0);
    result = neverc_udp_queue_pop(queue, buffer, sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_OK);
    CHECK(memcmp(buffer, "two", 3) == 0);
    CHECK(neverc_udp_queue_receive(receiver, queue, 3) == 1);
    result = neverc_udp_queue_pop(queue, buffer, sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_OK);
    CHECK(memcmp(buffer, "tri", 3) == 0);
    neverc_udp_queue_free(queue);

    char large[64];
    memset(large, 'x', sizeof(large));
    CHECK(neverc_udp_write_to(sender, large, sizeof(large),
                              &destination) == (int)sizeof(large));
    CHECK(neverc_udp_read_packet(receiver, buffer,
                                 sizeof(buffer), &info) ==
          (int)sizeof(buffer));
    CHECK(info.truncated == 1);
    CHECK(info.datagram_len == sizeof(large));

    neverc_udp_close(sender);
    neverc_udp_close(receiver);
    return 0;
}

static int run_udp_concurrent_truncation(void) {
    const char *error = NULL;
    neverc_udp_conn_t *sender =
        neverc_udp_listen("127.0.0.1:0", &error);
    neverc_udp_conn_t *receiver =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(sender != NULL);
    CHECK(receiver != NULL);
    neverc_udp_addr_t destination;
    CHECK(neverc_udp_local_addr(receiver, &destination) == 0);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2, 2);
    CHECK(executor != NULL);
    udp_truncated_read_task_t tasks[2];
    memset(tasks, 0, sizeof(tasks));
    for (int i = 0; i < 2; ++i) {
        tasks[i].receiver = receiver;
        CHECK(neverc_thread_executor_submit(
                  executor, udp_truncated_read_task, &tasks[i]) ==
              NEVERC_THREAD_OK);
    }

    char payload[64];
    memset(payload, 't', sizeof(payload));
    neverc_udp_send_message_t messages[2];
    memset(messages, 0, sizeof(messages));
    for (int i = 0; i < 2; ++i) {
        messages[i].data = payload;
        messages[i].len = sizeof(payload);
        messages[i].destination = &destination;
    }
    CHECK(neverc_udp_write_batch(sender, messages, 2) == 2);
    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(tasks[0].result == 0);
    CHECK(tasks[1].result == 0);

    neverc_thread_executor_free(executor);
    neverc_udp_close(sender);
    neverc_udp_close(receiver);
    return 0;
}

/*
 * Fault injection at the application boundary: the stack must preserve
 * datagram arrival order and identity (reorder/duplicate/gap), and must
 * fail closed under bounded-queue / connection pressure.
 */
static int run_udp_fault_delivery(void) {
    const char *error = NULL;
    neverc_udp_conn_t *sender =
        neverc_udp_listen("127.0.0.1:0", &error);
    neverc_udp_conn_t *receiver =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(sender != NULL);
    CHECK(receiver != NULL);
    CHECK(neverc_udp_set_timeout(receiver, 2000) == 0);

    neverc_udp_addr_t destination;
    CHECK(neverc_udp_local_addr(receiver, &destination) == 0);

    /* Wire order: 1, 3, 2, 3 — gap for seq 4, reorder, duplicate. */
    const char *wire[] = {"1", "3", "2", "3"};
    neverc_udp_send_message_t outgoing[4];
    memset(outgoing, 0, sizeof(outgoing));
    for (int i = 0; i < 4; ++i) {
        outgoing[i].data = wire[i];
        outgoing[i].len = 1;
        outgoing[i].destination = &destination;
    }
    CHECK(neverc_udp_write_batch(sender, outgoing, 4) == 4);

    char seen[4];
    for (int i = 0; i < 4; ++i) {
        neverc_udp_packet_info_t info;
        char buffer[8];
        int n = neverc_udp_read_packet(
            receiver, buffer, sizeof(buffer), &info);
        CHECK(n == 1);
        CHECK(!info.truncated);
        seen[i] = buffer[0];
    }
    CHECK(memcmp(seen, "1323", 4) == 0);

    /* Missing seq "4" must not invent a packet under timeout. */
    CHECK(neverc_udp_set_read_timeout(receiver, 30) == 0);
    neverc_udp_packet_info_t info;
    char buffer[8];
    CHECK(neverc_udp_read_packet(receiver, buffer, sizeof(buffer),
                                 &info) < 0);

    neverc_udp_close(sender);
    neverc_udp_close(receiver);
    return 0;
}

static int run_udp_queue_exhaustion(void) {
    CHECK(neverc_udp_queue_create(0, 8) == NULL);
    CHECK(neverc_udp_queue_create(8, 0) == NULL);
    CHECK(neverc_udp_queue_create(NEVERC_UDP_MAX_QUEUE_CAPACITY + 1,
                                  8) == NULL);
    CHECK(neverc_udp_queue_create(8, NEVERC_UDP_MAX_DATAGRAM_SIZE + 1) ==
          NULL);

    const char *error = NULL;
    neverc_udp_conn_t *sender =
        neverc_udp_listen("127.0.0.1:0", &error);
    neverc_udp_conn_t *receiver =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(sender != NULL);
    CHECK(receiver != NULL);
    CHECK(neverc_udp_set_timeout(receiver, 2000) == 0);

    neverc_udp_addr_t destination;
    CHECK(neverc_udp_local_addr(receiver, &destination) == 0);

    neverc_udp_queue_t *queue = neverc_udp_queue_create(4, 16);
    CHECK(queue != NULL);

    neverc_udp_send_message_t flood[16];
    char payloads[16][4];
    memset(flood, 0, sizeof(flood));
    for (int i = 0; i < 16; ++i) {
        payloads[i][0] = (char)('A' + i);
        payloads[i][1] = '\0';
        flood[i].data = payloads[i];
        flood[i].len = 1;
        flood[i].destination = &destination;
    }
    CHECK(neverc_udp_write_batch(sender, flood, 16) == 16);

    CHECK(neverc_udp_queue_receive(receiver, queue, 16) == 4);
    CHECK(neverc_udp_queue_length(queue) == 4);
    CHECK(neverc_udp_queue_capacity(queue) == 4);
    /* Full queue refuses additional receive until drained. */
    CHECK(neverc_udp_queue_receive(receiver, queue, 16) == 0);

    char buffer[8];
    neverc_udp_packet_info_t info;
    for (int i = 0; i < 4; ++i) {
        neverc_net_result_t result =
            neverc_udp_queue_pop(queue, buffer, sizeof(buffer), &info);
        CHECK(result.status == NEVERC_NET_OK);
        CHECK(result.transferred == 1);
        CHECK(buffer[0] == (char)('A' + i));
    }
    CHECK(neverc_udp_queue_pop(queue, buffer, sizeof(buffer), &info)
              .status == NEVERC_NET_WOULD_BLOCK);

    /* After drain, remaining socket datagrams can refill the queue. */
    int refilled = neverc_udp_queue_receive(receiver, queue, 16);
    CHECK(refilled >= 1);
    CHECK(refilled <= 4);
    neverc_net_result_t result =
        neverc_udp_queue_pop(queue, buffer, sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_OK);
    CHECK(result.transferred == 1);
    CHECK(buffer[0] >= 'A' && buffer[0] <= 'P');
    CHECK(buffer[0] != 'A'); /* First four were already drained. */

    neverc_udp_queue_free(queue);
    neverc_udp_close(sender);
    neverc_udp_close(receiver);
    return 0;
}

static int run_tcp_accept_pressure(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);

    char dial_addr[64];
    snprintf(dial_addr, sizeof(dial_addr), "127.0.0.1:%u", local.port);

    enum { CLIENTS = 32 };
    neverc_tcp_conn_t *clients[CLIENTS];
    memset(clients, 0, sizeof(clients));
    int connected = 0;
    for (int i = 0; i < CLIENTS; ++i) {
        clients[i] = neverc_tcp_dial(dial_addr, &error);
        if (clients[i])
            ++connected;
    }
    CHECK(connected >= 8);

    int accepted = 0;
    for (int i = 0; i < connected; ++i) {
        neverc_tcp_conn_t *server = NULL;
        neverc_net_result_t result =
            neverc_tcp_try_accept(listener, &server);
        if (result.status == NEVERC_NET_WOULD_BLOCK)
            break;
        CHECK(result.status == NEVERC_NET_OK);
        CHECK(server != NULL);
#ifndef _WIN32
        int flags = fcntl((int)neverc_tcp_conn_handle(server), F_GETFL, 0);
        CHECK(flags >= 0);
        CHECK((flags & O_NONBLOCK) != 0);
#endif
        neverc_tcp_close(server);
        ++accepted;
    }
    CHECK(accepted >= 1);

    for (int i = 0; i < CLIENTS; ++i) {
        if (clients[i])
            neverc_tcp_close(clients[i]);
    }
    neverc_tcp_listener_close(listener);
    return 0;
}

static int run_tcp_context_accept_contention(void) {
    enum { WORKERS = 16 };

    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);

    char dial_addr[64];
    snprintf(dial_addr, sizeof(dial_addr), "127.0.0.1:%u", local.port);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(WORKERS, WORKERS);
    neverc_thread_channel_t *ready =
        neverc_thread_channel_create(WORKERS);
    neverc_thread_channel_t *start =
        neverc_thread_channel_create(WORKERS);
    CHECK(executor != NULL);
    CHECK(ready != NULL);
    CHECK(start != NULL);

    tcp_context_accept_task_t tasks[WORKERS];
    memset(tasks, 0, sizeof(tasks));
    for (int i = 0; i < WORKERS; ++i) {
        tasks[i].listener = listener;
        tasks[i].ready = ready;
        tasks[i].start = start;
        tasks[i].status = NEVERC_NET_INVALID;
        CHECK(neverc_thread_executor_submit(
                  executor, tcp_context_accept_task, &tasks[i]) ==
              NEVERC_THREAD_OK);
    }

    void *ready_signal = NULL;
    for (int i = 0; i < WORKERS; ++i)
        CHECK(neverc_thread_channel_receive(ready, &ready_signal) ==
              NEVERC_THREAD_OK);

    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *context =
        neverc_context_with_timeout_handle(NULL, 500, &cancel);
    CHECK(context != NULL);
    CHECK(cancel != NULL);
    for (int i = 0; i < WORKERS; ++i)
        tasks[i].context = context;

    for (int i = 0; i < WORKERS; ++i)
        CHECK(neverc_thread_channel_send(start, NULL) == NEVERC_THREAD_OK);

    neverc_tcp_conn_t *initial =
        neverc_tcp_dial(dial_addr, &error);
    CHECK(initial != NULL);

    /*
     * Correct non-blocking accept lets one worker consume the queued
     * connection and the rest observe the context deadline. If try_accept
     * performs a blocking accept after a readiness probe, losing workers
     * remain stuck past the deadline and consume these rescue connections.
     */
    transport_sleep_ms(600);
    neverc_tcp_conn_t *rescue[WORKERS];
    memset(rescue, 0, sizeof(rescue));
    for (int i = 0; i < WORKERS; ++i) {
        rescue[i] = neverc_tcp_dial(dial_addr, &error);
        CHECK(rescue[i] != NULL);
    }

    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    int accepted = 0;
    int timed_out = 0;
    for (int i = 0; i < WORKERS; ++i) {
        accepted += tasks[i].status == NEVERC_NET_OK;
        timed_out += tasks[i].status == NEVERC_NET_TIMEOUT;
    }
    CHECK(accepted == 1);
    CHECK(timed_out == WORKERS - 1);

    neverc_tcp_close(initial);
    for (int i = 0; i < WORKERS; ++i)
        neverc_tcp_close(rescue[i]);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_thread_channel_free(ready);
    neverc_thread_channel_free(start);
    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(listener);
    return 0;
}

static int run_transport_soak(int duration_ms) {
    const char *error = NULL;
    neverc_tcp_listener_t *tcp_listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    neverc_udp_conn_t *udp_server =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(tcp_listener != NULL);
    CHECK(udp_server != NULL);
    CHECK(neverc_udp_set_timeout(udp_server, 1000) == 0);

    neverc_tcp_addr_t tcp_local;
    neverc_udp_addr_t udp_local;
    CHECK(neverc_tcp_listener_addr(tcp_listener, &tcp_local) == 0);
    CHECK(neverc_udp_local_addr(udp_server, &udp_local) == 0);

    char tcp_addr[64];
    char udp_addr[64];
    snprintf(tcp_addr, sizeof(tcp_addr), "127.0.0.1:%u", tcp_local.port);
    snprintf(udp_addr, sizeof(udp_addr), "127.0.0.1:%u", udp_local.port);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2, 4);
    CHECK(executor != NULL);

    int64_t deadline = transport_now_ms() + duration_ms;
    int tcp_rounds = 0;
    int udp_rounds = 0;
    while (transport_now_ms() < deadline) {
        tcp_echo_task_t tcp_task = {tcp_listener, 0};
        udp_echo_task_t udp_task = {udp_server, 0};
        CHECK(neverc_thread_executor_submit(
                  executor, tcp_echo_task, &tcp_task) == NEVERC_THREAD_OK);
        CHECK(neverc_thread_executor_submit(
                  executor, udp_echo_task, &udp_task) == NEVERC_THREAD_OK);

        neverc_tcp_conn_t *tcp_client =
            neverc_tcp_dial(tcp_addr, &error);
        neverc_udp_conn_t *udp_client =
            neverc_udp_dial(udp_addr, &error);
        CHECK(tcp_client != NULL);
        CHECK(udp_client != NULL);
        CHECK(neverc_tcp_set_timeout(tcp_client, 1000) == 0);
        CHECK(neverc_udp_set_timeout(udp_client, 1000) == 0);
        CHECK(neverc_tcp_write(tcp_client, "ping", 4) == 4);
        CHECK(neverc_udp_write(udp_client, "ping", 4) == 4);

        char buffer[8];
        CHECK(neverc_tcp_read(tcp_client, buffer, sizeof(buffer)) == 4);
        CHECK(memcmp(buffer, "pong", 4) == 0);
        CHECK(neverc_udp_read(udp_client, buffer, sizeof(buffer)) == 4);
        CHECK(memcmp(buffer, "pong", 4) == 0);
        neverc_tcp_close(tcp_client);
        neverc_udp_close(udp_client);

        CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
        CHECK(tcp_task.result == 0);
        CHECK(udp_task.result == 0);
        ++tcp_rounds;
        ++udp_rounds;
    }

    CHECK(tcp_rounds >= 1);
    CHECK(udp_rounds >= 1);
    fprintf(stderr, "soak %dms: tcp=%d udp=%d\n", duration_ms, tcp_rounds,
            udp_rounds);

    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(tcp_listener);
    neverc_udp_close(udp_server);
    return 0;
}

int main(void) {
    CHECK(run_tcp_echo("127.0.0.1:0", "127.0.0.1:%u") == 0);
    CHECK(run_tcp_echo("[::1]:0", "[::1]:%u") == 0);
    CHECK(run_tcp_multi_client() == 0);
    CHECK(run_tcp_disconnect() == 0);
    CHECK(run_tcp_slow_reader() == 0);
    CHECK(run_absolute_deadlines() == 0);
    CHECK(run_udp_echo("127.0.0.1:0", "127.0.0.1:%u") == 0);
    CHECK(run_udp_echo("[::1]:0", "[::1]:%u") == 0);
    CHECK(run_udp_batch_queue() == 0);
    CHECK(run_udp_concurrent_truncation() == 0);
    CHECK(run_udp_fault_delivery() == 0);
    CHECK(run_udp_queue_exhaustion() == 0);
    CHECK(run_tcp_accept_pressure() == 0);
    CHECK(run_tcp_context_accept_contention() == 0);

    const char *error = NULL;
    neverc_udp_conn_t *idle =
        neverc_udp_listen("127.0.0.1:0", &error);
    CHECK(idle != NULL);
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_timeout_handle(NULL, 20, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    char buffer[8];
    neverc_udp_packet_info_t info;
    neverc_net_result_t result =
        neverc_udp_read_packet_context(idle, ctx, buffer,
                                       sizeof(buffer), &info);
    CHECK(result.status == NEVERC_NET_TIMEOUT);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);
    neverc_udp_close(idle);

    /* Keep the default short so network-core gates stay CI-friendly.
     * Longer soak/benchmarks set NEVERC_NET_SOAK_MS explicitly. */
    int soak_ms = 250;
    const char *soak_env = getenv("NEVERC_NET_SOAK_MS");
    if (soak_env && soak_env[0]) {
        int parsed = atoi(soak_env);
        if (parsed > 0)
            soak_ms = parsed;
    }
    CHECK(run_transport_soak(soak_ms) == 0);

    puts("passed");
    return 0;
}
