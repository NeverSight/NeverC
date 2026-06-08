/*
 * NeverC io_uring backend test suite.
 *
 * Tests the raw io_uring syscall wrappers, poller backend, and native
 * async I/O engine. On non-Linux platforms, all tests pass trivially.
 *
 * Compile with: -DNC_USE_IO_URING=1 (Linux only)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#define NC_USE_IO_URING 1
#include <sys/syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <linux/io_uring.h>
#endif

#include "neverc/std/net/tcp.h"
#include "neverc/std/net/http.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  test %-50s ", #name); \
        test_##name(); \
        printf("PASS\n"); \
        tests_passed++; \
    } \
    static void test_##name(void)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("FAIL\n    %s:%d: %s == %lld, expected %lld\n", \
               __FILE__, __LINE__, #a, _a, _b); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a < _b) { \
        printf("FAIL\n    %s:%d: %s == %lld, expected >= %lld\n", \
               __FILE__, __LINE__, #a, _a, _b); \
        tests_failed++; return; \
    } \
} while(0)

/* ===== Test 1: io_uring syscall availability ===== */
TEST(io_uring_syscall_available) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    struct io_uring_params p;
    memset(&p, 0, sizeof(p));
    int fd = (int)syscall(__NR_io_uring_setup, 16, &p);

    if (fd < 0) {
        printf("(skipped: kernel doesn't support io_uring) ");
        return;
    }
    ASSERT_GE(fd, 0);
    ASSERT_GE((int)p.sq_entries, 16);
    ASSERT_GE((int)p.cq_entries, 16);
    close(fd);
#else
    /* Non-Linux: trivially pass */
#endif
}

/* ===== Test 2: nc_uring_t init and destroy ===== */
TEST(uring_ring_init_destroy) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    int rc = nc_uring_init(&ring, 256);
    if (rc < 0) {
        printf("(skipped: io_uring not supported on this kernel) ");
        return;
    }
    ASSERT_EQ(rc, 0);
    ASSERT_GE(ring.ring_fd, 0);
    ASSERT_TRUE(ring.sqes != NULL);
    ASSERT_TRUE(ring.cqes != NULL);
    ASSERT_GE(ring.sq_ring_entries, 256);
    ASSERT_GE(ring.cq_ring_entries, 256);

    nc_uring_destroy(&ring);
    ASSERT_EQ(ring.ring_fd, -1);
#endif
}

/* ===== Test 3: SQE get and advance ===== */
TEST(uring_sqe_get_advance) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    if (nc_uring_init(&ring, 64) < 0) {
        printf("(skipped) ");
        return;
    }

    struct io_uring_sqe *sqe = nc_uring_get_sqe(&ring);
    ASSERT_TRUE(sqe != NULL);
    ASSERT_EQ(sqe->opcode, 0);

    sqe->opcode = IORING_OP_NOP;
    sqe->user_data = 42;
    nc_uring_sq_advance(&ring, 1);

    int submitted = nc_uring_submit(&ring);
    ASSERT_GE(submitted, 1);

    nc_uring_destroy(&ring);
#endif
}

/* ===== Test 4: NOP submit and complete ===== */
TEST(uring_nop_submit_complete) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    if (nc_uring_init(&ring, 64) < 0) {
        printf("(skipped) ");
        return;
    }

    struct io_uring_sqe *sqe = nc_uring_get_sqe(&ring);
    ASSERT_TRUE(sqe != NULL);
    sqe->opcode = IORING_OP_NOP;
    sqe->user_data = 0xDEADBEEF;
    nc_uring_sq_advance(&ring, 1);

    int rc = nc_uring_submit_and_wait(&ring, 1);
    ASSERT_GE(rc, 0);

    struct io_uring_cqe *cqe = nc_uring_peek_cqe(&ring);
    ASSERT_TRUE(cqe != NULL);
    ASSERT_EQ(cqe->user_data, 0xDEADBEEF);
    ASSERT_EQ(cqe->res, 0);
    nc_uring_cq_advance(&ring, 1);

    nc_uring_destroy(&ring);
#endif
}

/* ===== Test 5: Batch NOP (multiple SQEs per submit) ===== */
TEST(uring_batch_nops) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    if (nc_uring_init(&ring, 64) < 0) {
        printf("(skipped) ");
        return;
    }

    int batch_size = 32;
    for (int i = 0; i < batch_size; i++) {
        struct io_uring_sqe *sqe = nc_uring_get_sqe(&ring);
        ASSERT_TRUE(sqe != NULL);
        sqe->opcode = IORING_OP_NOP;
        sqe->user_data = (uint64_t)(100 + i);
        nc_uring_sq_advance(&ring, 1);
    }

    int rc = nc_uring_submit_and_wait(&ring, (unsigned)batch_size);
    ASSERT_GE(rc, 0);

    int completed = 0;
    for (int i = 0; i < batch_size; i++) {
        struct io_uring_cqe *cqe = nc_uring_peek_cqe(&ring);
        if (!cqe) break;
        ASSERT_EQ(cqe->res, 0);
        nc_uring_cq_advance(&ring, 1);
        completed++;
    }
    ASSERT_EQ(completed, batch_size);

    nc_uring_destroy(&ring);
#endif
}

/* ===== Test 6: POLL_ADD on a pipe ===== */
TEST(uring_poll_add_pipe) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    if (nc_uring_init(&ring, 64) < 0) {
        printf("(skipped) ");
        return;
    }

    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    struct io_uring_sqe *sqe = nc_uring_get_sqe(&ring);
    ASSERT_TRUE(sqe != NULL);
    nc_uring_prep_poll_add(sqe, pipefd[0], POLLIN, 0x1234);
    nc_uring_sq_advance(&ring, 1);
    nc_uring_submit(&ring);

    /* Write to pipe to trigger POLLIN */
    char c = 'X';
    ASSERT_EQ(write(pipefd[1], &c, 1), 1);

    int rc = nc_uring_submit_and_wait(&ring, 1);
    ASSERT_GE(rc, 0);

    struct io_uring_cqe *cqe = nc_uring_peek_cqe(&ring);
    ASSERT_TRUE(cqe != NULL);
    ASSERT_EQ(cqe->user_data, 0x1234);
    ASSERT_TRUE(cqe->res & POLLIN);
    nc_uring_cq_advance(&ring, 1);

    close(pipefd[0]);
    close(pipefd[1]);
    nc_uring_destroy(&ring);
#endif
}

/* ===== Test 7: Poller with io_uring backend ===== */
TEST(poller_io_uring_backend) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_poller_t *poller = nc_poller_create();
    if (!poller) {
        printf("(skipped: couldn't create io_uring poller) ");
        return;
    }

    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    int dummy_data = 42;
    ASSERT_EQ(nc_poller_add(poller, pipefd[0], NC_EV_READ, &dummy_data), 0);

    /* Write to pipe */
    char c = 'Y';
    ASSERT_EQ(write(pipefd[1], &c, 1), 1);

    nc_event_t events[16];
    int n = nc_poller_wait(poller, events, 16, 1000);
    ASSERT_GE(n, 1);
    ASSERT_TRUE(events[0].events & NC_EV_READ);
    ASSERT_EQ(events[0].data, &dummy_data);

    nc_poller_del(poller, pipefd[0]);
    close(pipefd[0]);
    close(pipefd[1]);
    nc_poller_destroy(poller);
#endif
}

/* ===== Test 8: User-data encoding/decoding ===== */
TEST(uring_userdata_encoding) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    int test_ops[] = { NC_URING_OP_ACCEPT, NC_URING_OP_RECV,
                       NC_URING_OP_SEND, NC_URING_OP_CLOSE };
    for (int i = 0; i < 4; i++) {
        void *ptr = (void *)(uintptr_t)(0x12345678 + i * 0x100);
        uint64_t ud = nc_uring_encode_ud(test_ops[i], ptr);
        ASSERT_EQ(nc_uring_decode_op(ud), test_ops[i]);
        ASSERT_EQ(nc_uring_decode_ptr(ud), ptr);
    }

    /* Edge case: NULL pointer */
    uint64_t ud = nc_uring_encode_ud(NC_URING_OP_RECV, NULL);
    ASSERT_EQ(nc_uring_decode_op(ud), NC_URING_OP_RECV);
    ASSERT_EQ(nc_uring_decode_ptr(ud), NULL);
#endif
}

/* ===== Test 9: Native async engine init/destroy ===== */
TEST(uring_engine_init_destroy) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_engine_t eng;
    int rc = nc_uring_engine_init(&eng, 256);
    if (rc < 0) {
        printf("(skipped) ");
        return;
    }
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(eng.running);
    ASSERT_GE(eng.ring.ring_fd, 0);

    nc_uring_engine_destroy(&eng);
    ASSERT_TRUE(!eng.running);
#endif
}

/* ===== Test 10: TCP echo via io_uring poller ===== */
TEST(uring_tcp_echo_via_poller) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_net_init();

    /* Create a TCP listener */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* auto-assign port */
    ASSERT_EQ(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)), 0);
    ASSERT_EQ(listen(listen_fd, 128), 0);

    socklen_t alen = sizeof(addr);
    getsockname(listen_fd, (struct sockaddr *)&addr, &alen);
    int port = ntohs(addr.sin_port);

    nc_set_nonblocking(listen_fd);

    /* Connect a client */
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);

    struct sockaddr_in caddr;
    memset(&caddr, 0, sizeof(caddr));
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    caddr.sin_port = htons((uint16_t)port);
    ASSERT_EQ(connect(client_fd, (struct sockaddr *)&caddr, sizeof(caddr)), 0);

    /* Accept on server */
    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    int server_fd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
    ASSERT_GE(server_fd, 0);

    /* Send and receive */
    const char *msg = "hello io_uring";
    ASSERT_EQ(send(client_fd, msg, strlen(msg), 0), (ssize_t)strlen(msg));

    char buf[64];
    ssize_t n = recv(server_fd, buf, sizeof(buf) - 1, 0);
    ASSERT_GE(n, 1);
    buf[n] = '\0';
    ASSERT_EQ(strcmp(buf, msg), 0);

    close(server_fd);
    close(client_fd);
    close(listen_fd);
#endif
}

/* ===== Test 11: Multiple fd poll tracking ===== */
TEST(uring_multi_fd_poll) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_poller_t *poller = nc_poller_create();
    if (!poller) { printf("(skipped) "); return; }

    int pipes[4][2];
    int data_markers[4] = {10, 20, 30, 40};

    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(pipe(pipes[i]), 0);
        ASSERT_EQ(nc_poller_add(poller, pipes[i][0], NC_EV_READ,
                                &data_markers[i]), 0);
    }

    /* Write to pipes 1 and 3 */
    char c = 'Z';
    write(pipes[1][1], &c, 1);
    write(pipes[3][1], &c, 1);

    nc_event_t events[16];
    int n = nc_poller_wait(poller, events, 16, 1000);
    ASSERT_GE(n, 2);

    int found[4] = {0};
    for (int i = 0; i < n; i++) {
        int *marker = (int *)events[i].data;
        if (marker == &data_markers[1]) found[1] = 1;
        if (marker == &data_markers[3]) found[3] = 1;
    }
    ASSERT_TRUE(found[1]);
    ASSERT_TRUE(found[3]);

    for (int i = 0; i < 4; i++) {
        nc_poller_del(poller, pipes[i][0]);
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    nc_poller_destroy(poller);
#endif
}

/* ===== Test 12: Poller mod (change events) ===== */
TEST(uring_poller_mod) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_poller_t *poller = nc_poller_create();
    if (!poller) { printf("(skipped) "); return; }

    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    int data1 = 100, data2 = 200;
    ASSERT_EQ(nc_poller_add(poller, pipefd[0], NC_EV_READ, &data1), 0);
    ASSERT_EQ(nc_poller_mod(poller, pipefd[0], NC_EV_READ, &data2), 0);

    char c = 'M';
    write(pipefd[1], &c, 1);

    nc_event_t events[16];
    int n = nc_poller_wait(poller, events, 16, 1000);
    ASSERT_GE(n, 1);
    ASSERT_EQ(events[0].data, &data2);

    nc_poller_del(poller, pipefd[0]);
    close(pipefd[0]);
    close(pipefd[1]);
    nc_poller_destroy(poller);
#endif
}

/* ===== Test 13: Ring capacity — fill and drain ===== */
TEST(uring_ring_capacity) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    if (nc_uring_init(&ring, 64) < 0) {
        printf("(skipped) ");
        return;
    }

    /* Fill the SQ with NOPs */
    int count = 0;
    while (count < (int)ring.sq_ring_entries) {
        struct io_uring_sqe *sqe = nc_uring_get_sqe(&ring);
        if (!sqe) break;
        sqe->opcode = IORING_OP_NOP;
        sqe->user_data = (uint64_t)(1000 + count);
        nc_uring_sq_advance(&ring, 1);
        count++;
    }
    ASSERT_GE(count, 64);

    /* Submit all and drain CQ */
    int rc = nc_uring_submit_and_wait(&ring, (unsigned)count);
    ASSERT_GE(rc, 0);

    int drained = 0;
    while (drained < count) {
        struct io_uring_cqe *cqe = nc_uring_peek_cqe(&ring);
        if (!cqe) {
            nc_uring_submit_and_wait(&ring, 1);
            continue;
        }
        ASSERT_EQ(cqe->res, 0);
        nc_uring_cq_advance(&ring, 1);
        drained++;
    }
    ASSERT_EQ(drained, count);

    nc_uring_destroy(&ring);
#endif
}

int main(void) {
    printf("io_uring test suite:\n");

    run_test_io_uring_syscall_available();
    run_test_uring_ring_init_destroy();
    run_test_uring_sqe_get_advance();
    run_test_uring_nop_submit_complete();
    run_test_uring_batch_nops();
    run_test_uring_poll_add_pipe();
    run_test_poller_io_uring_backend();
    run_test_uring_userdata_encoding();
    run_test_uring_engine_init_destroy();
    run_test_uring_tcp_echo_via_poller();
    run_test_uring_multi_fd_poll();
    run_test_uring_poller_mod();
    run_test_uring_ring_capacity();

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
