#ifndef NEVERC_NET_IO_URING_H
#define NEVERC_NET_IO_URING_H

#include "_net_platform.h"

/*
 * Raw io_uring syscall wrappers and ring management. This intentionally has
 * no liburing dependency so the standard library remains self-contained.
 */
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

#ifndef IORING_OFF_SQ_RING
#define IORING_OFF_SQ_RING 0ULL
#endif
#ifndef IORING_OFF_CQ_RING
#define IORING_OFF_CQ_RING 0x8000000ULL
#endif
#ifndef IORING_OFF_SQES
#define IORING_OFF_SQES 0x10000000ULL
#endif
#ifndef IORING_ENTER_GETEVENTS
#define IORING_ENTER_GETEVENTS (1U << 0)
#endif
#ifndef IORING_SETUP_CQSIZE
#define IORING_SETUP_CQSIZE (1U << 3)
#endif
#ifndef IORING_FEAT_SINGLE_MMAP
#define IORING_FEAT_SINGLE_MMAP (1U << 0)
#endif

#ifndef IORING_OP_NOP
#define IORING_OP_NOP 0
#endif
#ifndef IORING_OP_POLL_ADD
#define IORING_OP_POLL_ADD 6
#endif
#ifndef IORING_OP_POLL_REMOVE
#define IORING_OP_POLL_REMOVE 7
#endif
#ifndef IORING_OP_ACCEPT
#define IORING_OP_ACCEPT 13
#endif
#ifndef IORING_OP_CLOSE
#define IORING_OP_CLOSE 19
#endif
#ifndef IORING_OP_SEND
#define IORING_OP_SEND 26
#endif
#ifndef IORING_OP_RECV
#define IORING_OP_RECV 27
#endif
#ifndef IORING_OP_TIMEOUT
#define IORING_OP_TIMEOUT 11
#endif
#ifndef IORING_ACCEPT_MULTISHOT
#define IORING_ACCEPT_MULTISHOT (1U << 0)
#endif
#ifndef IORING_CQE_F_MORE
#define IORING_CQE_F_MORE (1U << 1)
#endif

#define nc_io_smp_store_release(ptr, value) \
    __atomic_store_n((ptr), (value), __ATOMIC_RELEASE)
#define nc_io_smp_load_acquire(ptr) \
    __atomic_load_n((ptr), __ATOMIC_ACQUIRE)

static inline int nc_io_uring_setup(unsigned entries,
                                    struct io_uring_params *params) {
    int result;
    do {
        result = (int)syscall(__NR_io_uring_setup, entries, params);
    } while (result < 0 && errno == EINTR);
    return result;
}

static inline int nc_io_uring_enter(int ring_fd, unsigned to_submit,
                                    unsigned min_complete, unsigned flags) {
    int result;
    do {
        result = (int)syscall(__NR_io_uring_enter, ring_fd, to_submit,
                              min_complete, flags, NULL, 0);
    } while (result < 0 && errno == EINTR);
    return result;
}

static inline int nc_io_uring_register(int ring_fd, unsigned opcode,
                                       void *arg, unsigned nr_args) {
    return (int)syscall(__NR_io_uring_register, ring_fd, opcode, arg, nr_args);
}

typedef struct {
    int ring_fd;

    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_mask;
    unsigned *sq_entries_ptr;
    unsigned *sq_flags;
    unsigned *sq_array;
    struct io_uring_sqe *sqes;

    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_mask;
    unsigned *cq_entries_ptr;
    struct io_uring_cqe *cqes;

    void *sq_ring_ptr;
    size_t sq_ring_sz;
    void *cq_ring_ptr;
    size_t cq_ring_sz;
    void *sqes_ptr;
    size_t sqes_sz;
    int single_mmap;

    unsigned sq_ring_entries;
    unsigned cq_ring_entries;
} nc_uring_t;

static inline int nc_uring_init(nc_uring_t *ring, unsigned entries) {
    memset(ring, 0, sizeof(*ring));
    ring->ring_fd = -1;
    if (entries == 0 || entries > UINT_MAX / 4 ||
        (size_t)entries > SIZE_MAX / sizeof(struct io_uring_sqe))
        return -1;

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.cq_entries = entries * 4;
    params.flags = IORING_SETUP_CQSIZE;

    ring->ring_fd = nc_io_uring_setup(entries, &params);
    if (ring->ring_fd < 0) return -1;

    if (params.sq_entries == 0 || params.cq_entries == 0 ||
        params.sq_entries > SIZE_MAX / sizeof(struct io_uring_sqe) ||
        params.cq_entries > SIZE_MAX / sizeof(struct io_uring_cqe) ||
        params.sq_entries > SIZE_MAX / sizeof(unsigned)) {
        close(ring->ring_fd);
        ring->ring_fd = -1;
        return -1;
    }

    ring->sq_ring_entries = params.sq_entries;
    ring->cq_ring_entries = params.cq_entries;
    ring->single_mmap = !!(params.features & IORING_FEAT_SINGLE_MMAP);

    size_t sq_array_bytes = (size_t)params.sq_entries * sizeof(unsigned);
    size_t cqes_bytes =
        (size_t)params.cq_entries * sizeof(struct io_uring_cqe);
    if (params.sq_off.array > SIZE_MAX - sq_array_bytes ||
        params.cq_off.cqes > SIZE_MAX - cqes_bytes) {
        close(ring->ring_fd);
        ring->ring_fd = -1;
        return -1;
    }
    size_t sq_ring_sz = (size_t)params.sq_off.array + sq_array_bytes;
    size_t cq_ring_sz = (size_t)params.cq_off.cqes + cqes_bytes;
    if (ring->single_mmap && cq_ring_sz > sq_ring_sz)
        sq_ring_sz = cq_ring_sz;
    ring->sq_ring_sz = sq_ring_sz;
    ring->sq_ring_ptr =
        mmap(NULL, ring->sq_ring_sz, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_SQ_RING);
    if (ring->sq_ring_ptr == MAP_FAILED) {
        close(ring->ring_fd);
        ring->ring_fd = -1;
        return -1;
    }

    ring->sq_head =
        (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.head);
    ring->sq_tail =
        (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.tail);
    ring->sq_mask =
        (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.ring_mask);
    ring->sq_entries_ptr =
        (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.ring_entries);
    ring->sq_flags =
        (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.flags);
    ring->sq_array =
        (unsigned *)((char *)ring->sq_ring_ptr + params.sq_off.array);

    ring->sqes_sz = params.sq_entries * sizeof(struct io_uring_sqe);
    ring->sqes_ptr =
        mmap(NULL, ring->sqes_sz, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_POPULATE, ring->ring_fd, IORING_OFF_SQES);
    if (ring->sqes_ptr == MAP_FAILED) {
        munmap(ring->sq_ring_ptr, ring->sq_ring_sz);
        close(ring->ring_fd);
        ring->sq_ring_ptr = NULL;
        ring->ring_fd = -1;
        return -1;
    }
    ring->sqes = (struct io_uring_sqe *)ring->sqes_ptr;

    if (ring->single_mmap) {
        ring->cq_ring_ptr = ring->sq_ring_ptr;
        ring->cq_ring_sz = 0;
    } else {
        ring->cq_ring_sz = cq_ring_sz;
        ring->cq_ring_ptr =
            mmap(NULL, ring->cq_ring_sz, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_POPULATE, ring->ring_fd,
                 IORING_OFF_CQ_RING);
        if (ring->cq_ring_ptr == MAP_FAILED) {
            munmap(ring->sqes_ptr, ring->sqes_sz);
            munmap(ring->sq_ring_ptr, ring->sq_ring_sz);
            close(ring->ring_fd);
            ring->sqes_ptr = NULL;
            ring->sq_ring_ptr = NULL;
            ring->ring_fd = -1;
            return -1;
        }
    }

    ring->cq_head =
        (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.head);
    ring->cq_tail =
        (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.tail);
    ring->cq_mask =
        (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.ring_mask);
    ring->cq_entries_ptr =
        (unsigned *)((char *)ring->cq_ring_ptr + params.cq_off.ring_entries);
    ring->cqes =
        (struct io_uring_cqe *)((char *)ring->cq_ring_ptr +
                               params.cq_off.cqes);

    for (unsigned i = 0; i < params.sq_entries; i++)
        ring->sq_array[i] = i;
    return 0;
}

static inline void nc_uring_destroy(nc_uring_t *ring) {
    if (ring->sqes_ptr && ring->sqes_ptr != MAP_FAILED)
        munmap(ring->sqes_ptr, ring->sqes_sz);
    if (!ring->single_mmap && ring->cq_ring_ptr &&
        ring->cq_ring_ptr != MAP_FAILED)
        munmap(ring->cq_ring_ptr, ring->cq_ring_sz);
    if (ring->sq_ring_ptr && ring->sq_ring_ptr != MAP_FAILED)
        munmap(ring->sq_ring_ptr, ring->sq_ring_sz);
    if (ring->ring_fd >= 0) close(ring->ring_fd);
    memset(ring, 0, sizeof(*ring));
    ring->ring_fd = -1;
}

static inline struct io_uring_sqe *nc_uring_get_sqe(nc_uring_t *ring) {
    unsigned tail = *ring->sq_tail;
    unsigned head = nc_io_smp_load_acquire(ring->sq_head);
    if (tail - head >= ring->sq_ring_entries) return NULL;
    struct io_uring_sqe *sqe = &ring->sqes[tail & *ring->sq_mask];
    memset(sqe, 0, sizeof(*sqe));
    return sqe;
}

static inline void nc_uring_sq_advance(nc_uring_t *ring, unsigned count) {
    unsigned tail = *ring->sq_tail;
    nc_io_smp_store_release(ring->sq_tail, tail + count);
}

static inline int nc_uring_submit(nc_uring_t *ring) {
    unsigned submitted =
        *ring->sq_tail - nc_io_smp_load_acquire(ring->sq_head);
    if (submitted == 0) return 0;
    return nc_io_uring_enter(ring->ring_fd, submitted, 0, 0);
}

static inline int nc_uring_submit_and_wait(nc_uring_t *ring,
                                           unsigned min_complete) {
    unsigned submitted =
        *ring->sq_tail - nc_io_smp_load_acquire(ring->sq_head);
    return nc_io_uring_enter(ring->ring_fd, submitted, min_complete,
                             IORING_ENTER_GETEVENTS);
}

static inline struct io_uring_cqe *nc_uring_peek_cqe(nc_uring_t *ring) {
    unsigned head = nc_io_smp_load_acquire(ring->cq_head);
    unsigned tail = nc_io_smp_load_acquire(ring->cq_tail);
    if (head == tail) return NULL;
    return &ring->cqes[head & *ring->cq_mask];
}

static inline void nc_uring_cq_advance(nc_uring_t *ring, unsigned count) {
    unsigned head = *ring->cq_head;
    nc_io_smp_store_release(ring->cq_head, head + count);
}

static inline void nc_uring_prep_poll_add(struct io_uring_sqe *sqe, int fd,
                                          unsigned poll_mask,
                                          uint64_t user_data) {
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = fd;
    sqe->poll32_events = poll_mask;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_poll_remove(struct io_uring_sqe *sqe,
                                             uint64_t user_data) {
    sqe->opcode = IORING_OP_POLL_REMOVE;
    sqe->addr = user_data;
    sqe->user_data = UINT64_MAX;
}

static inline void nc_uring_prep_accept(struct io_uring_sqe *sqe,
                                        int listen_fd,
                                        struct sockaddr *addr,
                                        socklen_t *addrlen, int flags,
                                        uint64_t user_data) {
    sqe->opcode = IORING_OP_ACCEPT;
    sqe->fd = listen_fd;
    sqe->addr = (unsigned long)addr;
    sqe->addr2 = (unsigned long)addrlen;
    sqe->accept_flags = (unsigned)flags;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_accept_multishot(
    struct io_uring_sqe *sqe, int listen_fd, struct sockaddr *addr,
    socklen_t *addrlen, int flags, uint64_t user_data) {
    nc_uring_prep_accept(sqe, listen_fd, addr, addrlen, flags, user_data);
    sqe->ioprio |= IORING_ACCEPT_MULTISHOT;
}

static inline void nc_uring_prep_recv(struct io_uring_sqe *sqe, int fd,
                                      void *buf, unsigned len, int flags,
                                      uint64_t user_data) {
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = fd;
    sqe->addr = (unsigned long)buf;
    sqe->len = len;
    sqe->msg_flags = (unsigned)flags;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_send(struct io_uring_sqe *sqe, int fd,
                                      const void *buf, unsigned len,
                                      int flags, uint64_t user_data) {
    sqe->opcode = IORING_OP_SEND;
    sqe->fd = fd;
    sqe->addr = (unsigned long)buf;
    sqe->len = len;
    sqe->msg_flags = (unsigned)flags;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_close(struct io_uring_sqe *sqe, int fd,
                                       uint64_t user_data) {
    sqe->opcode = IORING_OP_CLOSE;
    sqe->fd = fd;
    sqe->user_data = user_data;
}

/* Native async accept/recv/send engine used by Linux reactor workers. */
#define NC_URING_OP_ACCEPT 1
#define NC_URING_OP_RECV 2
#define NC_URING_OP_SEND 3
#define NC_URING_OP_CLOSE 4
#define NC_URING_OP_POLL 5
#define NC_URING_OP_TIMEOUT 6

static inline uint64_t nc_uring_encode_ud(int op, void *ptr) {
    return ((uint64_t)op << 56) |
           ((uint64_t)(uintptr_t)ptr & 0x00FFFFFFFFFFFFFFULL);
}

static inline int nc_uring_decode_op(uint64_t user_data) {
    return (int)(user_data >> 56);
}

static inline void *nc_uring_decode_ptr(uint64_t user_data) {
    return (void *)(uintptr_t)(user_data & 0x00FFFFFFFFFFFFFFULL);
}

typedef struct {
    nc_sock_t fd;
    char *buf;
    size_t buf_sz;
    void *user_ctx;
    void (*on_recv)(void *user_ctx, int nbytes, char *buf);
} nc_uring_recv_ctx_t;

typedef struct {
    nc_sock_t fd;
    const char *buf;
    size_t len;
    size_t sent;
    void *user_ctx;
    void (*on_send)(void *user_ctx, int result);
} nc_uring_send_ctx_t;

typedef struct {
    nc_uring_t ring;
    volatile int running;
    nc_sock_t listen_fd;
    int multishot_supported;
    struct sockaddr_storage accept_addr;
    socklen_t accept_addrlen;
    void *accept_ctx;
    void (*on_accept)(void *ctx, nc_sock_t client_fd,
                      struct sockaddr *addr, socklen_t addrlen);
} nc_uring_engine_t;

static inline int nc_uring_engine_init(nc_uring_engine_t *engine,
                                       unsigned ring_size) {
    memset(engine, 0, sizeof(*engine));
    engine->listen_fd = NC_INVALID_SOCK;
    engine->accept_addrlen = sizeof(engine->accept_addr);
    engine->multishot_supported = 1;
    if (nc_uring_init(&engine->ring, ring_size) != 0)
        return -1;
    nc_atomic_store(&engine->running, 1);
    return 0;
}

static inline void nc_uring_engine_destroy(nc_uring_engine_t *engine) {
    nc_atomic_store(&engine->running, 0);
    nc_uring_destroy(&engine->ring);
}

static inline int nc_uring_engine_accept(
    nc_uring_engine_t *engine, nc_sock_t listen_fd, void *ctx,
    void (*on_accept)(void *, nc_sock_t, struct sockaddr *, socklen_t)) {
    engine->listen_fd = listen_fd;
    engine->accept_ctx = ctx;
    engine->on_accept = on_accept;

    struct io_uring_sqe *sqe = nc_uring_get_sqe(&engine->ring);
    if (!sqe) return -1;
    engine->accept_addrlen = sizeof(engine->accept_addr);
    if (engine->multishot_supported) {
        nc_uring_prep_accept_multishot(
            sqe, listen_fd, (struct sockaddr *)&engine->accept_addr,
            &engine->accept_addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC,
            nc_uring_encode_ud(NC_URING_OP_ACCEPT, engine));
    } else {
        nc_uring_prep_accept(
            sqe, listen_fd, (struct sockaddr *)&engine->accept_addr,
            &engine->accept_addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC,
            nc_uring_encode_ud(NC_URING_OP_ACCEPT, engine));
    }
    nc_uring_sq_advance(&engine->ring, 1);
    return 0;
}

static inline int nc_uring_engine_recv(nc_uring_engine_t *engine,
                                       nc_uring_recv_ctx_t *ctx) {
    if (!engine || !ctx || !ctx->buf || ctx->buf_sz > UINT_MAX)
        return -1;
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&engine->ring);
    if (!sqe) {
        if (nc_uring_submit(&engine->ring) < 0) return -1;
        sqe = nc_uring_get_sqe(&engine->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_recv(
        sqe, ctx->fd, ctx->buf, (unsigned)ctx->buf_sz, 0,
        nc_uring_encode_ud(NC_URING_OP_RECV, ctx));
    nc_uring_sq_advance(&engine->ring, 1);
    return 0;
}

static inline int nc_uring_engine_send(nc_uring_engine_t *engine,
                                       nc_uring_send_ctx_t *ctx) {
    if (!engine || !ctx || !ctx->buf || ctx->sent > ctx->len)
        return -1;
    size_t remaining = ctx->len - ctx->sent;
    if (remaining > UINT_MAX) remaining = UINT_MAX;
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&engine->ring);
    if (!sqe) {
        if (nc_uring_submit(&engine->ring) < 0) return -1;
        sqe = nc_uring_get_sqe(&engine->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_send(
        sqe, ctx->fd, ctx->buf + ctx->sent, (unsigned)remaining,
        MSG_NOSIGNAL, nc_uring_encode_ud(NC_URING_OP_SEND, ctx));
    nc_uring_sq_advance(&engine->ring, 1);
    return 0;
}

static inline int nc_uring_engine_poll(nc_uring_engine_t *engine,
                                       int timeout_ms) {
    if (!engine || timeout_ms < -1) return -1;
    if (nc_uring_submit(&engine->ring) < 0) return -1;
    if (!nc_uring_peek_cqe(&engine->ring)) {
        struct pollfd ring_fd;
        memset(&ring_fd, 0, sizeof(ring_fd));
        ring_fd.fd = engine->ring.ring_fd;
        ring_fd.events = POLLIN;
        int ready;
        do {
            ready = poll(&ring_fd, 1, timeout_ms);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0) return ready;
    }

    int processed = 0;
    while (processed < 256) {
        struct io_uring_cqe *cqe = nc_uring_peek_cqe(&engine->ring);
        if (!cqe) break;
        uint64_t user_data = cqe->user_data;
        int completion = cqe->res;
        uint32_t flags = cqe->flags;
        nc_uring_cq_advance(&engine->ring, 1);
        processed++;

        int op = nc_uring_decode_op(user_data);
        void *ptr = nc_uring_decode_ptr(user_data);
        switch (op) {
        case NC_URING_OP_ACCEPT: {
            nc_uring_engine_t *accept_engine =
                (nc_uring_engine_t *)ptr;
            if (completion >= 0 && accept_engine->on_accept) {
                accept_engine->on_accept(
                    accept_engine->accept_ctx, (nc_sock_t)completion,
                    (struct sockaddr *)&accept_engine->accept_addr,
                    accept_engine->accept_addrlen);
            }
            if (!(flags & IORING_CQE_F_MORE) &&
                nc_atomic_load(&accept_engine->running)) {
                if (accept_engine->multishot_supported &&
                    completion == -EINVAL)
                    accept_engine->multishot_supported = 0;
                nc_uring_engine_accept(
                    accept_engine, accept_engine->listen_fd,
                    accept_engine->accept_ctx, accept_engine->on_accept);
            }
            break;
        }
        case NC_URING_OP_RECV: {
            nc_uring_recv_ctx_t *recv_ctx =
                (nc_uring_recv_ctx_t *)ptr;
            if (recv_ctx->on_recv)
                recv_ctx->on_recv(
                    recv_ctx->user_ctx, completion, recv_ctx->buf);
            break;
        }
        case NC_URING_OP_SEND: {
            nc_uring_send_ctx_t *send_ctx =
                (nc_uring_send_ctx_t *)ptr;
            if (completion > 0) {
                size_t n = (size_t)completion;
                if (send_ctx->sent > send_ctx->len ||
                    n > send_ctx->len - send_ctx->sent) {
                    if (send_ctx->on_send)
                        send_ctx->on_send(send_ctx->user_ctx, -1);
                    break;
                }
                send_ctx->sent += n;
                if (send_ctx->sent < send_ctx->len) {
                    nc_uring_engine_send(engine, send_ctx);
                    break;
                }
            }
            if (send_ctx->on_send) {
                send_ctx->on_send(
                    send_ctx->user_ctx,
                    send_ctx->sent >= send_ctx->len ? 0 : -1);
            }
            break;
        }
        default:
            break;
        }
    }
    return processed;
}

static inline void nc_uring_engine_run(nc_uring_engine_t *engine) {
    while (nc_atomic_load(&engine->running))
        nc_uring_engine_poll(engine, 100);
}

static inline void nc_uring_engine_stop(nc_uring_engine_t *engine) {
    if (engine) nc_atomic_store(&engine->running, 0);
}

#endif /* NC_USE_IO_URING */

#endif /* NEVERC_NET_IO_URING_H */
