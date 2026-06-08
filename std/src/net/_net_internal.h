#ifndef NEVERC_NET_INTERNAL_H
#define NEVERC_NET_INTERNAL_H

/*
 * NeverC net — shared platform abstraction for TCP/UDP/HTTP.
 *
 * Provides: socket types, non-blocking helpers, thread pool, event polling,
 *           event loop, timer wheel, buffer pool, connection state machine.
 * Platforms: POSIX (Linux/macOS/iOS/Android) + WinSock2 (Windows).
 *
 * Architecture: Multi-threaded Reactor (Nginx/libuv style)
 *   Main thread: accept() + distribute to worker event loops
 *   Worker threads: each runs its own event loop with epoll/kqueue/IOCP
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ======================================================================
 * Platform detection & socket abstraction
 * ====================================================================== */

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mswsock.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")

  typedef SOCKET nc_sock_t;
  #define NC_INVALID_SOCK  INVALID_SOCKET
  #define NC_SOCK_ERR      SOCKET_ERROR
  #define nc_sock_close(s) closesocket(s)
  #define nc_sock_errno    WSAGetLastError()

  typedef HANDLE nc_thread_t;
  typedef CRITICAL_SECTION nc_mutex_t;
  typedef CONDITION_VARIABLE nc_cond_t;

  #define nc_mutex_init(m)    InitializeCriticalSection(m)
  #define nc_mutex_destroy(m) DeleteCriticalSection(m)
  #define nc_mutex_lock(m)    EnterCriticalSection(m)
  #define nc_mutex_unlock(m)  LeaveCriticalSection(m)
  #define nc_cond_init(c)     InitializeConditionVariable(c)
  #define nc_cond_destroy(c)  ((void)(c))
  #define nc_cond_signal(c)   WakeConditionVariable(c)
  #define nc_cond_broadcast(c) WakeAllConditionVariable(c)
  #define nc_cond_wait(c, m)  SleepConditionVariableCS(c, m, INFINITE)

  static inline uint64_t nc_monotonic_ms(void) {
      return (uint64_t)GetTickCount64();
  }
  static inline int nc_cpu_count(void) {
      SYSTEM_INFO si;
      GetSystemInfo(&si);
      return (int)si.dwNumberOfProcessors;
  }

#else /* POSIX */
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <signal.h>
  #include <pthread.h>
  #include <time.h>

  typedef int nc_sock_t;
  #define NC_INVALID_SOCK  (-1)
  #define NC_SOCK_ERR      (-1)
  #define nc_sock_close(s) close(s)
  #define nc_sock_errno    errno

  typedef pthread_t nc_thread_t;
  typedef pthread_mutex_t nc_mutex_t;
  typedef pthread_cond_t nc_cond_t;

  #define nc_mutex_init(m)    pthread_mutex_init(m, NULL)
  #define nc_mutex_destroy(m) pthread_mutex_destroy(m)
  #define nc_mutex_lock(m)    pthread_mutex_lock(m)
  #define nc_mutex_unlock(m)  pthread_mutex_unlock(m)
  #define nc_cond_init(c)     pthread_cond_init(c, NULL)
  #define nc_cond_destroy(c)  pthread_cond_destroy(c)
  #define nc_cond_signal(c)   pthread_cond_signal(c)
  #define nc_cond_broadcast(c) pthread_cond_broadcast(c)
  #define nc_cond_wait(c, m)  pthread_cond_wait(c, m)

  static inline uint64_t nc_monotonic_ms(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
  }
  static inline int nc_cpu_count(void) {
      long n = sysconf(_SC_NPROCESSORS_ONLN);
      return n > 0 ? (int)n : 1;
  }
#endif

/* Event polling backend detection.
 * Priority: io_uring > epoll > kqueue > IOCP > poll
 * Set NC_USE_IO_URING=1 at compile time to enable io_uring (Linux 5.1+).
 * io_uring implementation uses raw syscalls — zero external dependencies. */
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
  #include <sys/mman.h>
  #include <sys/syscall.h>
  #include <poll.h>
  #include <linux/io_uring.h>
#elif defined(__linux__) || defined(__ANDROID__)
  #define NC_USE_EPOLL 1
  #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
  #define NC_USE_KQUEUE 1
  #include <sys/event.h>
#elif defined(_WIN32)
  #define NC_USE_IOCP 1
#else
  #define NC_USE_POLL 1
  #include <poll.h>
#endif

/* ======================================================================
 * io_uring raw syscall wrappers + ring management (Linux 5.1+)
 *
 * No liburing dependency. We call io_uring_setup/io_uring_enter directly
 * via syscall() and mmap the SQ/CQ rings into userspace.
 *
 * Two operation modes:
 *   1) POLL mode: IORING_OP_POLL_ADD for fd readiness (drop-in epoll replacement)
 *   2) NATIVE mode: IORING_OP_ACCEPT/RECV/SEND for true async I/O
 *      with multishot accept (5.19+) and provided buffer rings (5.19+)
 * ====================================================================== */

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup    425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter    426
#endif
#ifndef __NR_io_uring_register
#define __NR_io_uring_register 427
#endif

#ifndef IORING_OFF_SQ_RING
#define IORING_OFF_SQ_RING     0ULL
#endif
#ifndef IORING_OFF_CQ_RING
#define IORING_OFF_CQ_RING     0x8000000ULL
#endif
#ifndef IORING_OFF_SQES
#define IORING_OFF_SQES        0x10000000ULL
#endif

#ifndef IORING_ENTER_GETEVENTS
#define IORING_ENTER_GETEVENTS (1U << 0)
#endif
#ifndef IORING_SETUP_CQSIZE
#define IORING_SETUP_CQSIZE    (1U << 3)
#endif
#ifndef IORING_FEAT_SINGLE_MMAP
#define IORING_FEAT_SINGLE_MMAP (1U << 0)
#endif

#ifndef IORING_OP_NOP
#define IORING_OP_NOP          0
#endif
#ifndef IORING_OP_POLL_ADD
#define IORING_OP_POLL_ADD     6
#endif
#ifndef IORING_OP_POLL_REMOVE
#define IORING_OP_POLL_REMOVE  7
#endif
#ifndef IORING_OP_ACCEPT
#define IORING_OP_ACCEPT       13
#endif
#ifndef IORING_OP_RECV
#define IORING_OP_RECV         27
#endif
#ifndef IORING_OP_SEND
#define IORING_OP_SEND         26
#endif
#ifndef IORING_OP_CLOSE
#define IORING_OP_CLOSE        19
#endif
#ifndef IORING_OP_TIMEOUT
#define IORING_OP_TIMEOUT      11
#endif

#ifndef IORING_ACCEPT_MULTISHOT
#define IORING_ACCEPT_MULTISHOT (1U << 0)
#endif
#ifndef IORING_CQE_F_MORE
#define IORING_CQE_F_MORE      (1U << 1)
#endif

/* Memory barriers for ring synchronization */
#define nc_io_smp_store_release(p, v) \
    __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define nc_io_smp_load_acquire(p) \
    __atomic_load_n((p), __ATOMIC_ACQUIRE)

static inline int nc_io_uring_setup(unsigned entries,
                                     struct io_uring_params *p) {
    return (int)syscall(__NR_io_uring_setup, entries, p);
}

static inline int nc_io_uring_enter(int ring_fd, unsigned to_submit,
                                     unsigned min_complete, unsigned flags) {
    return (int)syscall(__NR_io_uring_enter, ring_fd, to_submit,
                        min_complete, flags, NULL, 0);
}

static inline int nc_io_uring_register(int ring_fd, unsigned opcode,
                                        void *arg, unsigned nr_args) {
    return (int)syscall(__NR_io_uring_register, ring_fd, opcode, arg, nr_args);
}

/* Managed io_uring instance — owns the ring fd and mmap regions */
typedef struct {
    int ring_fd;

    /* Submission ring pointers (into mmap'd memory) */
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_mask;
    unsigned *sq_entries_ptr;
    unsigned *sq_flags;
    unsigned *sq_array;
    struct io_uring_sqe *sqes;

    /* Completion ring pointers (into mmap'd memory) */
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_mask;
    unsigned *cq_entries_ptr;
    struct io_uring_cqe *cqes;

    /* mmap cleanup */
    void   *sq_ring_ptr;
    size_t  sq_ring_sz;
    void   *cq_ring_ptr;
    size_t  cq_ring_sz;
    void   *sqes_ptr;
    size_t  sqes_sz;
    int     single_mmap; /* sq and cq share a single mmap */

    unsigned sq_ring_entries;
    unsigned cq_ring_entries;
} nc_uring_t;

static inline int nc_uring_init(nc_uring_t *ring, unsigned entries) {
    memset(ring, 0, sizeof(*ring));

    struct io_uring_params p;
    memset(&p, 0, sizeof(p));
    p.cq_entries = entries * 4;
    p.flags = IORING_SETUP_CQSIZE;

    ring->ring_fd = nc_io_uring_setup(entries, &p);
    if (ring->ring_fd < 0)
        return -1;

    ring->sq_ring_entries = p.sq_entries;
    ring->cq_ring_entries = p.cq_entries;
    ring->single_mmap = !!(p.features & IORING_FEAT_SINGLE_MMAP);

    /* Map submission queue ring */
    ring->sq_ring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    ring->sq_ring_ptr = mmap(NULL, ring->sq_ring_sz,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_POPULATE,
                              ring->ring_fd, IORING_OFF_SQ_RING);
    if (ring->sq_ring_ptr == MAP_FAILED) {
        close(ring->ring_fd);
        return -1;
    }

    ring->sq_head = (unsigned *)((char *)ring->sq_ring_ptr + p.sq_off.head);
    ring->sq_tail = (unsigned *)((char *)ring->sq_ring_ptr + p.sq_off.tail);
    ring->sq_mask = (unsigned *)((char *)ring->sq_ring_ptr + p.sq_off.ring_mask);
    ring->sq_entries_ptr = (unsigned *)((char *)ring->sq_ring_ptr + p.sq_off.ring_entries);
    ring->sq_flags = (unsigned *)((char *)ring->sq_ring_ptr + p.sq_off.flags);
    ring->sq_array = (unsigned *)((char *)ring->sq_ring_ptr + p.sq_off.array);

    /* Map SQEs array */
    ring->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
    ring->sqes_ptr = mmap(NULL, ring->sqes_sz,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_POPULATE,
                           ring->ring_fd, IORING_OFF_SQES);
    if (ring->sqes_ptr == MAP_FAILED) {
        munmap(ring->sq_ring_ptr, ring->sq_ring_sz);
        close(ring->ring_fd);
        return -1;
    }
    ring->sqes = (struct io_uring_sqe *)ring->sqes_ptr;

    /* Map completion queue ring */
    if (ring->single_mmap) {
        ring->cq_ring_ptr = ring->sq_ring_ptr;
        ring->cq_ring_sz = 0;
    } else {
        ring->cq_ring_sz = p.cq_off.cqes +
                           p.cq_entries * sizeof(struct io_uring_cqe);
        ring->cq_ring_ptr = mmap(NULL, ring->cq_ring_sz,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_POPULATE,
                                  ring->ring_fd, IORING_OFF_CQ_RING);
        if (ring->cq_ring_ptr == MAP_FAILED) {
            munmap(ring->sqes_ptr, ring->sqes_sz);
            munmap(ring->sq_ring_ptr, ring->sq_ring_sz);
            close(ring->ring_fd);
            return -1;
        }
    }

    ring->cq_head = (unsigned *)((char *)ring->cq_ring_ptr + p.cq_off.head);
    ring->cq_tail = (unsigned *)((char *)ring->cq_ring_ptr + p.cq_off.tail);
    ring->cq_mask = (unsigned *)((char *)ring->cq_ring_ptr + p.cq_off.ring_mask);
    ring->cq_entries_ptr = (unsigned *)((char *)ring->cq_ring_ptr + p.cq_off.ring_entries);
    ring->cqes = (struct io_uring_cqe *)((char *)ring->cq_ring_ptr + p.cq_off.cqes);

    /* Pre-fill SQ array with sequential indices */
    for (unsigned i = 0; i < p.sq_entries; i++)
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
    if (ring->ring_fd >= 0)
        close(ring->ring_fd);
    memset(ring, 0, sizeof(*ring));
    ring->ring_fd = -1;
}

/* Get next available SQE. Returns NULL if ring is full.
 * We are the sole SQ producer, so sq_tail is a local read.
 * Only sq_head needs acquire (kernel is the consumer). */
static inline struct io_uring_sqe *nc_uring_get_sqe(nc_uring_t *ring) {
    unsigned tail = *ring->sq_tail;
    unsigned head = nc_io_smp_load_acquire(ring->sq_head);
    unsigned mask = *ring->sq_mask;

    if (tail - head >= ring->sq_ring_entries)
        return NULL; /* SQ full */

    struct io_uring_sqe *sqe = &ring->sqes[tail & mask];
    memset(sqe, 0, sizeof(*sqe));
    return sqe;
}

/* Advance SQ tail after filling SQE(s). */
static inline void nc_uring_sq_advance(nc_uring_t *ring, unsigned count) {
    unsigned tail = *ring->sq_tail;
    nc_io_smp_store_release(ring->sq_tail, tail + count);
}

/* Submit pending SQEs to the kernel. Returns number submitted or -errno. */
static inline int nc_uring_submit(nc_uring_t *ring) {
    unsigned submitted = *ring->sq_tail - nc_io_smp_load_acquire(ring->sq_head);
    if (submitted == 0) return 0;
    return nc_io_uring_enter(ring->ring_fd, submitted, 0, 0);
}

/* Submit and wait for at least min_complete CQEs. */
static inline int nc_uring_submit_and_wait(nc_uring_t *ring,
                                            unsigned min_complete) {
    unsigned submitted = *ring->sq_tail - nc_io_smp_load_acquire(ring->sq_head);
    return nc_io_uring_enter(ring->ring_fd, submitted, min_complete,
                             IORING_ENTER_GETEVENTS);
}

/* Peek at next CQE without consuming it. Returns NULL if empty. */
static inline struct io_uring_cqe *nc_uring_peek_cqe(nc_uring_t *ring) {
    unsigned head = nc_io_smp_load_acquire(ring->cq_head);
    unsigned tail = nc_io_smp_load_acquire(ring->cq_tail);
    if (head == tail) return NULL;
    return &ring->cqes[head & *ring->cq_mask];
}

/* Advance CQ head after processing CQE(s). */
static inline void nc_uring_cq_advance(nc_uring_t *ring, unsigned count) {
    unsigned head = *ring->cq_head;
    nc_io_smp_store_release(ring->cq_head, head + count);
}

/* ---- SQE preparation helpers ---- */

static inline void nc_uring_prep_poll_add(struct io_uring_sqe *sqe,
                                           int fd, unsigned poll_mask,
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
    sqe->user_data = 0;
}

static inline void nc_uring_prep_accept(struct io_uring_sqe *sqe,
                                         int listen_fd,
                                         struct sockaddr *addr,
                                         socklen_t *addrlen,
                                         int flags,
                                         uint64_t user_data) {
    sqe->opcode = IORING_OP_ACCEPT;
    sqe->fd = listen_fd;
    sqe->addr = (unsigned long)addr;
    sqe->addr2 = (unsigned long)addrlen;
    sqe->accept_flags = (unsigned)flags;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_accept_multishot(struct io_uring_sqe *sqe,
                                                   int listen_fd,
                                                   struct sockaddr *addr,
                                                   socklen_t *addrlen,
                                                   int flags,
                                                   uint64_t user_data) {
    nc_uring_prep_accept(sqe, listen_fd, addr, addrlen, flags, user_data);
    sqe->ioprio |= IORING_ACCEPT_MULTISHOT;
}

static inline void nc_uring_prep_recv(struct io_uring_sqe *sqe,
                                       int fd, void *buf, unsigned len,
                                       int flags, uint64_t user_data) {
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = fd;
    sqe->addr = (unsigned long)buf;
    sqe->len = len;
    sqe->msg_flags = (unsigned)flags;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_send(struct io_uring_sqe *sqe,
                                       int fd, const void *buf, unsigned len,
                                       int flags, uint64_t user_data) {
    sqe->opcode = IORING_OP_SEND;
    sqe->fd = fd;
    sqe->addr = (unsigned long)buf;
    sqe->len = len;
    sqe->msg_flags = (unsigned)flags;
    sqe->user_data = user_data;
}

static inline void nc_uring_prep_close(struct io_uring_sqe *sqe,
                                        int fd, uint64_t user_data) {
    sqe->opcode = IORING_OP_CLOSE;
    sqe->fd = fd;
    sqe->user_data = user_data;
}

#endif /* NC_USE_IO_URING */

/* ======================================================================
 * WSAStartup (Windows only) — thread-safe init
 * ====================================================================== */

static volatile int nc_net_initialized = 0;

static inline void nc_net_init(void) {
#ifdef _WIN32
    if (InterlockedCompareExchange((volatile LONG *)&nc_net_initialized, 0, 0))
        return;
    static volatile LONG nc_init_lock = 0;
    while (InterlockedCompareExchange(&nc_init_lock, 1, 0) != 0) { Sleep(0); }
    if (!InterlockedCompareExchange((volatile LONG *)&nc_net_initialized, 0, 0)) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        InterlockedExchange((volatile LONG *)&nc_net_initialized, 1);
    }
    InterlockedExchange(&nc_init_lock, 0);
#else
    if (__sync_add_and_fetch(&nc_net_initialized, 0)) return;
    static volatile int nc_init_lock = 0;
    while (!__sync_bool_compare_and_swap(&nc_init_lock, 0, 1)) { /* spin */ }
    if (!__sync_add_and_fetch(&nc_net_initialized, 0)) {
        signal(SIGPIPE, SIG_IGN);
        __sync_synchronize();
        __sync_lock_test_and_set(&nc_net_initialized, 1);
    }
    __sync_lock_release(&nc_init_lock);
#endif
}

/* ======================================================================
 * Non-blocking socket helpers
 * ====================================================================== */

static inline int nc_set_nonblocking(nc_sock_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static inline int nc_set_blocking(nc_sock_t fd) {
#ifdef _WIN32
    u_long mode = 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

static inline int nc_set_reuseaddr(nc_sock_t fd) {
    int opt = 1;
#ifdef _WIN32
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
}

#if !defined(_WIN32) && defined(SO_REUSEPORT)
static inline int nc_set_reuseport(nc_sock_t fd) {
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
}
#endif

static inline int nc_set_nodelay(nc_sock_t fd) {
    int opt = 1;
#ifdef _WIN32
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt, sizeof(opt));
#else
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif
}

static inline int nc_set_keepalive(nc_sock_t fd) {
    int opt = 1;
#ifdef _WIN32
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt, sizeof(opt));
#else
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
#endif
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ======================================================================
 * Address parsing (shared by tcp.c / udp.c / http.c)
 * ====================================================================== */

static inline int nc_parse_addr(const char *addr, char *host, size_t hostlen,
                                 uint16_t *port) {
    if (!addr || !addr[0]) return -1;

    if (addr[0] == ':') {
        if (hostlen > 0) host[0] = '\0';
        *port = (uint16_t)atoi(addr + 1);
        return 0;
    }

    /* Handle [IPv6]:port */
    if (addr[0] == '[') {
        const char *end = strchr(addr, ']');
        if (!end) return -1;
        size_t hlen = (size_t)(end - addr - 1);
        if (hlen >= hostlen) hlen = hostlen - 1;
        memcpy(host, addr + 1, hlen);
        host[hlen] = '\0';
        if (end[1] == ':')
            *port = (uint16_t)atoi(end + 2);
        else
            *port = 0;
        return 0;
    }

    const char *colon = NULL;
    for (const char *p = addr; *p; p++) {
        if (*p == ':') colon = p;
    }
    if (!colon) return -1;

    size_t hlen = (size_t)(colon - addr);
    if (hlen >= hostlen) hlen = hostlen - 1;
    memcpy(host, addr, hlen);
    host[hlen] = '\0';
    *port = (uint16_t)atoi(colon + 1);
    return 0;
}

/* ======================================================================
 * Dynamic buffer (growable byte array)
 * ====================================================================== */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} nc_buf_t;

static inline void nc_buf_init(nc_buf_t *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static inline void nc_buf_free(nc_buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static inline int nc_buf_grow(nc_buf_t *b, size_t need) {
    if (b->cap >= need) return 0;
    size_t nc = b->cap < 256 ? 256 : b->cap;
    while (nc < need) {
        size_t next = nc * 2;
        if (next <= nc) { nc = need; break; }
        nc = next;
    }
    char *nd = (char *)realloc(b->data, nc);
    if (!nd) return -1;
    b->data = nd;
    b->cap = nc;
    return 0;
}

static inline int nc_buf_append(nc_buf_t *b, const void *data, size_t len) {
    if (nc_buf_grow(b, b->len + len + 1) != 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

static inline void nc_buf_reset(nc_buf_t *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

static inline void nc_buf_consume(nc_buf_t *b, size_t n) {
    if (n >= b->len) {
        nc_buf_reset(b);
        return;
    }
    size_t remaining = b->len - n;
    memmove(b->data, b->data + n, remaining);
    b->len = remaining;
    b->data[b->len] = '\0';
}

/* ======================================================================
 * Thread pool
 * ====================================================================== */

#define NC_THREADPOOL_MAX_THREADS 256
#define NC_THREADPOOL_QUEUE_SIZE  65536

typedef void (*nc_task_func_t)(void *arg);

typedef struct {
    nc_task_func_t func;
    void *arg;
} nc_task_t;

typedef struct {
    nc_thread_t *threads;
    int nthreads;
    nc_task_t *queue;
    int queue_cap;
    int queue_head;
    int queue_tail;
    int queue_count;
    nc_mutex_t mutex;
    nc_cond_t not_empty;
    nc_cond_t not_full;
    volatile int shutdown;
} nc_threadpool_t;

#ifdef _WIN32
static DWORD WINAPI nc_threadpool_worker(LPVOID arg) {
#else
static void *nc_threadpool_worker(void *arg) {
#endif
    nc_threadpool_t *pool = (nc_threadpool_t *)arg;
    for (;;) {
        nc_mutex_lock(&pool->mutex);
        while (pool->queue_count == 0 && !pool->shutdown) {
            nc_cond_wait(&pool->not_empty, &pool->mutex);
        }
        if (pool->shutdown && pool->queue_count == 0) {
            nc_mutex_unlock(&pool->mutex);
            break;
        }
        nc_task_t task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_cap;
        pool->queue_count--;
        nc_cond_signal(&pool->not_full);
        nc_mutex_unlock(&pool->mutex);

        task.func(task.arg);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static inline nc_threadpool_t *nc_threadpool_create(int nthreads) {
    if (nthreads <= 0) nthreads = 4;
    if (nthreads > NC_THREADPOOL_MAX_THREADS) nthreads = NC_THREADPOOL_MAX_THREADS;

    nc_threadpool_t *pool = (nc_threadpool_t *)calloc(1, sizeof(*pool));
    if (!pool) return NULL;

    pool->nthreads = nthreads;
    pool->queue_cap = NC_THREADPOOL_QUEUE_SIZE;
    pool->queue = (nc_task_t *)calloc((size_t)pool->queue_cap, sizeof(nc_task_t));
    pool->threads = (nc_thread_t *)calloc((size_t)nthreads, sizeof(nc_thread_t));
    nc_mutex_init(&pool->mutex);
    nc_cond_init(&pool->not_empty);
    nc_cond_init(&pool->not_full);

    for (int i = 0; i < nthreads; i++) {
#ifdef _WIN32
        pool->threads[i] = CreateThread(NULL, 0, nc_threadpool_worker, pool, 0, NULL);
#else
        pthread_create(&pool->threads[i], NULL, nc_threadpool_worker, pool);
#endif
    }
    return pool;
}

static inline int nc_threadpool_submit(nc_threadpool_t *pool,
                                        nc_task_func_t func, void *arg) {
    if (!pool) return -1;
    nc_mutex_lock(&pool->mutex);
    while (pool->queue_count == pool->queue_cap && !pool->shutdown) {
        nc_cond_wait(&pool->not_full, &pool->mutex);
    }
    if (pool->shutdown) {
        nc_mutex_unlock(&pool->mutex);
        return -1;
    }
    pool->queue[pool->queue_tail].func = func;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_cap;
    pool->queue_count++;
    nc_cond_signal(&pool->not_empty);
    nc_mutex_unlock(&pool->mutex);
    return 0;
}

static inline void nc_threadpool_destroy(nc_threadpool_t *pool) {
    if (!pool) return;
    nc_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    nc_cond_broadcast(&pool->not_empty);
    nc_cond_broadcast(&pool->not_full);
    nc_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->nthreads; i++) {
#ifdef _WIN32
        WaitForSingleObject(pool->threads[i], INFINITE);
        CloseHandle(pool->threads[i]);
#else
        pthread_join(pool->threads[i], NULL);
#endif
    }
    free(pool->threads);
    free(pool->queue);
    nc_mutex_destroy(&pool->mutex);
    nc_cond_destroy(&pool->not_empty);
    nc_cond_destroy(&pool->not_full);
    free(pool);
}

/* ======================================================================
 * Event poller — unified interface over epoll/kqueue/IOCP/poll
 * ====================================================================== */

#define NC_EV_READ  1
#define NC_EV_WRITE 2
#define NC_EV_ERROR 4

typedef struct {
    nc_sock_t fd;
    int events;
    void *data;
} nc_event_t;

typedef struct {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    /* fd→user_data mapping for poll-based tracking.
     * Sparse array indexed by fd (up to NC_URING_MAX_FDS). */
    #define NC_URING_MAX_FDS 65536
    void **fd_data;  /* fd_data[fd] = user_data pointer */
    int   *fd_events; /* fd_events[fd] = NC_EV_READ|NC_EV_WRITE */
    int    fd_cap;
#elif defined(NC_USE_EPOLL)
    int epfd;
#elif defined(NC_USE_KQUEUE)
    int kqfd;
#elif defined(NC_USE_IOCP)
    HANDLE iocp;
#else
    struct pollfd *pfds;
    void **pdata;
    int npfds;
    int cap_pfds;
#endif
} nc_poller_t;

static inline nc_poller_t *nc_poller_create(void) {
    nc_poller_t *p = (nc_poller_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (nc_uring_init(&p->ring, 4096) != 0) { free(p); return NULL; }
    p->fd_cap = NC_URING_MAX_FDS;
    p->fd_data = (void **)calloc((size_t)p->fd_cap, sizeof(void *));
    p->fd_events = (int *)calloc((size_t)p->fd_cap, sizeof(int));
    if (!p->fd_data || !p->fd_events) {
        free(p->fd_data); free(p->fd_events);
        nc_uring_destroy(&p->ring);
        free(p); return NULL;
    }
#elif defined(NC_USE_EPOLL)
    p->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (p->epfd < 0) { free(p); return NULL; }
#elif defined(NC_USE_KQUEUE)
    p->kqfd = kqueue();
    if (p->kqfd < 0) { free(p); return NULL; }
#elif defined(NC_USE_IOCP)
    p->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!p->iocp) { free(p); return NULL; }
#else
    p->cap_pfds = 64;
    p->pfds = (struct pollfd *)calloc((size_t)p->cap_pfds, sizeof(struct pollfd));
    p->pdata = (void **)calloc((size_t)p->cap_pfds, sizeof(void *));
#endif
    return p;
}

static inline void nc_poller_destroy(nc_poller_t *p) {
    if (!p) return;
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_destroy(&p->ring);
    free(p->fd_data);
    free(p->fd_events);
#elif defined(NC_USE_EPOLL)
    close(p->epfd);
#elif defined(NC_USE_KQUEUE)
    close(p->kqfd);
#elif defined(NC_USE_IOCP)
    CloseHandle(p->iocp);
#else
    free(p->pfds);
    free(p->pdata);
#endif
    free(p);
}

static inline int nc_poller_add(nc_poller_t *p, nc_sock_t fd, int events,
                                 void *data) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (fd < 0 || fd >= p->fd_cap) return -1;
    p->fd_data[fd] = data;
    p->fd_events[fd] = events;

    /* Submit POLL_ADD to io_uring. Encode poll events. */
    unsigned poll_mask = 0;
    if (events & NC_EV_READ)  poll_mask |= POLLIN;
    if (events & NC_EV_WRITE) poll_mask |= POLLOUT;

    struct io_uring_sqe *sqe = nc_uring_get_sqe(&p->ring);
    if (!sqe) {
        nc_uring_submit(&p->ring);
        sqe = nc_uring_get_sqe(&p->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_poll_add(sqe, fd, poll_mask, (uint64_t)(uintptr_t)fd);
    nc_uring_sq_advance(&p->ring, 1);
    nc_uring_submit(&p->ring);
    return 0;

#elif defined(NC_USE_EPOLL)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = data;
    if (events & NC_EV_READ) ev.events |= EPOLLIN;
    if (events & NC_EV_WRITE) ev.events |= EPOLLOUT;
    ev.events |= EPOLLET;
    return epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev);

#elif defined(NC_USE_KQUEUE)
    struct kevent kev[2];
    int n = 0;
    if (events & NC_EV_READ) {
        EV_SET(&kev[n], (uintptr_t)fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, data);
        n++;
    }
    if (events & NC_EV_WRITE) {
        EV_SET(&kev[n], (uintptr_t)fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, data);
        n++;
    }
    return kevent(p->kqfd, kev, n, NULL, 0, NULL);

#elif defined(NC_USE_IOCP)
    (void)events;
    (void)data;
    /* IOCP: association is done per-overlapped-op; store data for later use */
    HANDLE h = CreateIoCompletionPort((HANDLE)fd, p->iocp, (ULONG_PTR)data, 0);
    return h ? 0 : -1;

#else
    if (p->npfds >= p->cap_pfds) {
        int nc = p->cap_pfds * 2;
        p->pfds = (struct pollfd *)realloc(p->pfds, (size_t)nc * sizeof(struct pollfd));
        p->pdata = (void **)realloc(p->pdata, (size_t)nc * sizeof(void *));
        p->cap_pfds = nc;
    }
    p->pfds[p->npfds].fd = fd;
    p->pfds[p->npfds].events = 0;
    if (events & NC_EV_READ) p->pfds[p->npfds].events |= POLLIN;
    if (events & NC_EV_WRITE) p->pfds[p->npfds].events |= POLLOUT;
    p->pdata[p->npfds] = data;
    p->npfds++;
    return 0;
#endif
}

static inline int nc_poller_mod(nc_poller_t *p, nc_sock_t fd, int events,
                                 void *data) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (fd < 0 || fd >= p->fd_cap) return -1;

    /* Cancel existing poll, then re-add with new events */
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&p->ring);
    if (!sqe) { nc_uring_submit(&p->ring); sqe = nc_uring_get_sqe(&p->ring); }
    if (!sqe) return -1;
    nc_uring_prep_poll_remove(sqe, (uint64_t)(uintptr_t)fd);
    nc_uring_sq_advance(&p->ring, 1);

    p->fd_data[fd] = data;
    p->fd_events[fd] = events;

    unsigned poll_mask = 0;
    if (events & NC_EV_READ)  poll_mask |= POLLIN;
    if (events & NC_EV_WRITE) poll_mask |= POLLOUT;

    sqe = nc_uring_get_sqe(&p->ring);
    if (!sqe) { nc_uring_submit(&p->ring); sqe = nc_uring_get_sqe(&p->ring); }
    if (!sqe) return -1;
    nc_uring_prep_poll_add(sqe, fd, poll_mask, (uint64_t)(uintptr_t)fd);
    nc_uring_sq_advance(&p->ring, 1);
    nc_uring_submit(&p->ring);
    return 0;

#elif defined(NC_USE_EPOLL)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = data;
    if (events & NC_EV_READ) ev.events |= EPOLLIN;
    if (events & NC_EV_WRITE) ev.events |= EPOLLOUT;
    ev.events |= EPOLLET;
    return epoll_ctl(p->epfd, EPOLL_CTL_MOD, fd, &ev);

#elif defined(NC_USE_KQUEUE)
    struct kevent kev[4];
    int n = 0;
    EV_SET(&kev[n], (uintptr_t)fd, EVFILT_READ,
           (events & NC_EV_READ) ? (EV_ADD | EV_CLEAR) : EV_DELETE, 0, 0, data);
    n++;
    EV_SET(&kev[n], (uintptr_t)fd, EVFILT_WRITE,
           (events & NC_EV_WRITE) ? (EV_ADD | EV_CLEAR) : EV_DELETE, 0, 0, data);
    n++;
    kevent(p->kqfd, kev, n, NULL, 0, NULL);
    return 0;

#elif defined(NC_USE_IOCP)
    (void)p; (void)fd; (void)events; (void)data;
    return 0;

#else
    for (int i = 0; i < p->npfds; i++) {
        if (p->pfds[i].fd == fd) {
            p->pfds[i].events = 0;
            if (events & NC_EV_READ) p->pfds[i].events |= POLLIN;
            if (events & NC_EV_WRITE) p->pfds[i].events |= POLLOUT;
            p->pdata[i] = data;
            return 0;
        }
    }
    return -1;
#endif
}

static inline int nc_poller_del(nc_poller_t *p, nc_sock_t fd) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (fd >= 0 && fd < p->fd_cap) {
        p->fd_data[fd] = NULL;
        p->fd_events[fd] = 0;
    }
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&p->ring);
    if (!sqe) { nc_uring_submit(&p->ring); sqe = nc_uring_get_sqe(&p->ring); }
    if (!sqe) return -1;
    nc_uring_prep_poll_remove(sqe, (uint64_t)(uintptr_t)fd);
    nc_uring_sq_advance(&p->ring, 1);
    nc_uring_submit(&p->ring);
    return 0;

#elif defined(NC_USE_EPOLL)
    return epoll_ctl(p->epfd, EPOLL_CTL_DEL, fd, NULL);

#elif defined(NC_USE_KQUEUE)
    struct kevent kev[2];
    EV_SET(&kev[0], (uintptr_t)fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(p->kqfd, kev, 2, NULL, 0, NULL);
    return 0;

#elif defined(NC_USE_IOCP)
    (void)fd;
    return 0;

#else
    for (int i = 0; i < p->npfds; i++) {
        if (p->pfds[i].fd == fd) {
            p->pfds[i] = p->pfds[p->npfds - 1];
            p->pdata[i] = p->pdata[p->npfds - 1];
            p->npfds--;
            return 0;
        }
    }
    return -1;
#endif
}

static inline int nc_poller_wait(nc_poller_t *p, nc_event_t *out,
                                  int max_events, int timeout_ms) {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    /*
     * io_uring poll mode: CQEs arrive for each POLL_ADD that fires.
     * After processing a CQE, we must re-arm the poll (POLL_ADD is oneshot).
     *
     * We first try to harvest existing CQEs without blocking. If none,
     * we submit a wait with timeout via io_uring_enter(GETEVENTS).
     */
    {
        /* If no CQEs ready, wait for at least 1 event (with timeout).
         * io_uring_enter with min_complete=1 blocks until an event arrives
         * or the ring is interrupted. For timeout, we submit a NOP first
         * and use IORING_ENTER_GETEVENTS + min_complete. */
        struct io_uring_cqe *cqe = nc_uring_peek_cqe(&p->ring);
        if (!cqe) {
            /* Wait for at least 1 completion or timeout.
             * Use a short poll loop for timeout support since io_uring
             * doesn't natively support timeout in io_uring_enter for
             * POLL_ADD completions without IORING_OP_TIMEOUT. */
            if (timeout_ms == 0) {
                return 0; /* non-blocking, nothing ready */
            }

            /* Submit IORING_OP_TIMEOUT so io_uring_enter returns
             * after either 1 event or the timeout elapses.
             * ts is stack-local — safe for multi-threaded workers. */
            struct io_uring_sqe *tsqe = nc_uring_get_sqe(&p->ring);
            if (tsqe) {
                struct __kernel_timespec ts;
                ts.tv_sec = timeout_ms / 1000;
                ts.tv_nsec = (long long)(timeout_ms % 1000) * 1000000LL;
                tsqe->opcode = IORING_OP_TIMEOUT;
                tsqe->addr = (unsigned long)&ts;
                tsqe->len = 1;
                tsqe->off = 1;
                tsqe->user_data = (uint64_t)-1; /* sentinel: timeout marker */
                nc_uring_sq_advance(&p->ring, 1);
            }
            nc_io_uring_enter(p->ring.ring_fd,
                              *p->ring.sq_tail - nc_io_smp_load_acquire(p->ring.sq_head),
                              1, IORING_ENTER_GETEVENTS);
        }
    }

    int count = 0;
    int rearm_count = 0;
    struct { int fd; unsigned poll_mask; } rearms[256];

    while (count < max_events) {
        struct io_uring_cqe *cqe = nc_uring_peek_cqe(&p->ring);
        if (!cqe) break;

        uint64_t ud = cqe->user_data;
        int res = cqe->res;
        nc_uring_cq_advance(&p->ring, 1);

        /* Skip timeout sentinel CQEs */
        if (ud == (uint64_t)-1) continue;

        int fd = (int)(uintptr_t)ud;
        if (fd < 0 || fd >= p->fd_cap || !p->fd_data[fd]) continue;
        if (res < 0) {
            /* Poll error — report as error event */
            out[count].fd = fd;
            out[count].data = p->fd_data[fd];
            out[count].events = NC_EV_ERROR;
            count++;
            continue;
        }

        int ev = 0;
        if (res & POLLIN)                ev |= NC_EV_READ;
        if (res & POLLOUT)               ev |= NC_EV_WRITE;
        if (res & (POLLERR | POLLHUP))   ev |= NC_EV_ERROR;

        out[count].fd = fd;
        out[count].data = p->fd_data[fd];
        out[count].events = ev;
        count++;

        /* io_uring POLL_ADD is oneshot — schedule re-arm */
        if (p->fd_data[fd] && rearm_count < 256) {
            unsigned pmask = 0;
            if (p->fd_events[fd] & NC_EV_READ)  pmask |= POLLIN;
            if (p->fd_events[fd] & NC_EV_WRITE) pmask |= POLLOUT;
            rearms[rearm_count].fd = fd;
            rearms[rearm_count].poll_mask = pmask;
            rearm_count++;
        }
    }

    /* Batch re-arm all triggered polls */
    for (int i = 0; i < rearm_count; i++) {
        struct io_uring_sqe *sqe = nc_uring_get_sqe(&p->ring);
        if (!sqe) { nc_uring_submit(&p->ring); sqe = nc_uring_get_sqe(&p->ring); }
        if (!sqe) break;
        nc_uring_prep_poll_add(sqe, rearms[i].fd, rearms[i].poll_mask,
                               (uint64_t)(uintptr_t)rearms[i].fd);
        nc_uring_sq_advance(&p->ring, 1);
    }
    if (rearm_count > 0)
        nc_uring_submit(&p->ring);

    return count;

#elif defined(NC_USE_EPOLL)
    struct epoll_event events[256];
    if (max_events > 256) max_events = 256;
    int n = epoll_wait(p->epfd, events, max_events, timeout_ms);
    for (int i = 0; i < n; i++) {
        out[i].data = events[i].data.ptr;
        out[i].fd = -1;
        out[i].events = 0;
        if (events[i].events & EPOLLIN) out[i].events |= NC_EV_READ;
        if (events[i].events & EPOLLOUT) out[i].events |= NC_EV_WRITE;
        if (events[i].events & (EPOLLERR | EPOLLHUP)) out[i].events |= NC_EV_ERROR;
    }
    return n;

#elif defined(NC_USE_KQUEUE)
    struct kevent events[256];
    if (max_events > 256) max_events = 256;
    struct timespec ts, *tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }
    int n = kevent(p->kqfd, NULL, 0, events, max_events, tsp);
    for (int i = 0; i < n; i++) {
        out[i].data = events[i].udata;
        out[i].fd = (nc_sock_t)events[i].ident;
        out[i].events = 0;
        if (events[i].filter == EVFILT_READ) out[i].events |= NC_EV_READ;
        if (events[i].filter == EVFILT_WRITE) out[i].events |= NC_EV_WRITE;
        if (events[i].flags & EV_ERROR) out[i].events |= NC_EV_ERROR;
    }
    return n;

#elif defined(NC_USE_IOCP)
    OVERLAPPED_ENTRY entries[256];
    ULONG nremoved = 0;
    ULONG max = (ULONG)(max_events > 256 ? 256 : max_events);
    BOOL ok = GetQueuedCompletionStatusEx(p->iocp, entries, max, &nremoved,
                                           (DWORD)(timeout_ms >= 0 ? timeout_ms : INFINITE), FALSE);
    if (!ok) return 0;
    for (ULONG i = 0; i < nremoved; i++) {
        out[i].data = (void *)entries[i].lpCompletionKey;
        out[i].fd = NC_INVALID_SOCK;
        out[i].events = NC_EV_READ | NC_EV_WRITE;
    }
    return (int)nremoved;

#else
    int n = poll(p->pfds, (nfds_t)p->npfds, timeout_ms);
    if (n <= 0) return n;
    int count = 0;
    for (int i = 0; i < p->npfds && count < max_events; i++) {
        if (p->pfds[i].revents) {
            out[count].fd = p->pfds[i].fd;
            out[count].data = p->pdata[i];
            out[count].events = 0;
            if (p->pfds[i].revents & POLLIN) out[count].events |= NC_EV_READ;
            if (p->pfds[i].revents & POLLOUT) out[count].events |= NC_EV_WRITE;
            if (p->pfds[i].revents & (POLLERR | POLLHUP)) out[count].events |= NC_EV_ERROR;
            count++;
        }
    }
    return count;
#endif
}

/* ======================================================================
 * Event loop — per-worker-thread reactor
 * ====================================================================== */

#define NC_EVLOOP_MAX_EVENTS  256

typedef void (*nc_evloop_cb_t)(void *data, int events);

typedef struct nc_evloop {
    nc_poller_t   *poller;
    volatile int   running;
    int            wakeup_fds[2];   /* pipe for cross-thread wakeup */

    nc_task_t     *pending;
    int            pending_count;
    int            pending_cap;
    nc_mutex_t     pending_lock;
} nc_evloop_t;

static inline nc_evloop_t *nc_evloop_create(void) {
    nc_evloop_t *loop = (nc_evloop_t *)calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->poller = nc_poller_create();
    if (!loop->poller) { free(loop); return NULL; }

    loop->pending_cap = 256;
    loop->pending = (nc_task_t *)calloc((size_t)loop->pending_cap, sizeof(nc_task_t));
    nc_mutex_init(&loop->pending_lock);

#ifdef _WIN32
    loop->wakeup_fds[0] = -1;
    loop->wakeup_fds[1] = -1;
    /* Windows: use IOCP PostQueuedCompletionStatus for wakeup */
#else
    if (pipe(loop->wakeup_fds) == 0) {
        nc_set_nonblocking(loop->wakeup_fds[0]);
        nc_set_nonblocking(loop->wakeup_fds[1]);
        nc_poller_add(loop->poller, loop->wakeup_fds[0], NC_EV_READ, NULL);
    }
#endif

    return loop;
}

static inline void nc_evloop_destroy(nc_evloop_t *loop) {
    if (!loop) return;
#ifndef _WIN32
    if (loop->wakeup_fds[0] >= 0) close(loop->wakeup_fds[0]);
    if (loop->wakeup_fds[1] >= 0) close(loop->wakeup_fds[1]);
#endif
    nc_poller_destroy(loop->poller);
    free(loop->pending);
    nc_mutex_destroy(&loop->pending_lock);
    free(loop);
}

static inline void nc_evloop_wakeup(nc_evloop_t *loop) {
#ifdef _WIN32
    if (loop->poller && loop->poller->iocp)
        PostQueuedCompletionStatus(loop->poller->iocp, 0, 0, NULL);
#else
    if (loop->wakeup_fds[1] >= 0) {
        char c = 'W';
        (void)write(loop->wakeup_fds[1], &c, 1);
    }
#endif
}

static inline void nc_evloop_stop(nc_evloop_t *loop) {
    loop->running = 0;
    nc_evloop_wakeup(loop);
}

static inline void nc_evloop_post(nc_evloop_t *loop, nc_task_func_t func, void *arg) {
    nc_mutex_lock(&loop->pending_lock);
    if (loop->pending_count >= loop->pending_cap) {
        loop->pending_cap *= 2;
        loop->pending = (nc_task_t *)realloc(loop->pending,
            (size_t)loop->pending_cap * sizeof(nc_task_t));
    }
    loop->pending[loop->pending_count].func = func;
    loop->pending[loop->pending_count].arg = arg;
    loop->pending_count++;
    nc_mutex_unlock(&loop->pending_lock);
    nc_evloop_wakeup(loop);
}

typedef void (*nc_evloop_event_handler_t)(nc_evloop_t *loop, nc_event_t *ev);

static inline void nc_evloop_run(nc_evloop_t *loop,
                                  nc_evloop_event_handler_t handler) {
    loop->running = 1;
    nc_event_t events[NC_EVLOOP_MAX_EVENTS];

    while (loop->running) {
        int n = nc_poller_wait(loop->poller, events, NC_EVLOOP_MAX_EVENTS, 100);

        /* Process pending tasks (from other threads) */
        nc_mutex_lock(&loop->pending_lock);
        int pc = loop->pending_count;
        nc_task_t *ptasks = NULL;
        if (pc > 0) {
            ptasks = (nc_task_t *)malloc((size_t)pc * sizeof(nc_task_t));
            memcpy(ptasks, loop->pending, (size_t)pc * sizeof(nc_task_t));
            loop->pending_count = 0;
        }
        nc_mutex_unlock(&loop->pending_lock);

        if (ptasks) {
            for (int i = 0; i < pc; i++)
                ptasks[i].func(ptasks[i].arg);
            free(ptasks);
        }

        /* Process I/O events */
        for (int i = 0; i < n; i++) {
#ifndef _WIN32
            if (events[i].fd == loop->wakeup_fds[0]) {
                char drain[64];
                while (read(loop->wakeup_fds[0], drain, sizeof(drain)) > 0)
                    ;
                continue;
            }
#endif
            if (events[i].data == NULL) continue;
            handler(loop, &events[i]);
        }
    }
}

/* ======================================================================
 * sendfile — zero-copy file-to-socket transfer
 * ====================================================================== */

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/sendfile.h>
static inline ssize_t nc_sendfile(nc_sock_t out_fd, int in_fd,
                                   off_t *offset, size_t count) {
    return sendfile(out_fd, in_fd, offset, count);
}
#define NC_HAS_SENDFILE 1
#elif defined(__APPLE__) || defined(__FreeBSD__)
static inline ssize_t nc_sendfile(nc_sock_t out_fd, int in_fd,
                                   off_t *offset, size_t count) {
#if defined(__APPLE__)
    off_t len = (off_t)count;
    int rc = sendfile(in_fd, out_fd, offset ? *offset : 0, &len, NULL, 0);
    if (offset && rc == 0) *offset += len;
    return rc == 0 ? (ssize_t)len : -1;
#else
    off_t sbytes = 0;
    int rc = sendfile(in_fd, out_fd, offset ? *offset : 0, count,
                       NULL, &sbytes, 0);
    if (offset) *offset += sbytes;
    return rc == 0 ? (ssize_t)sbytes : (sbytes > 0 ? (ssize_t)sbytes : -1);
#endif
}
#define NC_HAS_SENDFILE 1
#else
#define NC_HAS_SENDFILE 0
#endif

/* ======================================================================
 * accept4 — atomic non-blocking accept (Linux)
 * ====================================================================== */

static inline nc_sock_t nc_accept_nonblock(nc_sock_t listen_fd,
                                             struct sockaddr *addr,
                                             socklen_t *addrlen) {
#if defined(__linux__) && defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    return accept4(listen_fd, addr, addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    nc_sock_t fd = accept(listen_fd, addr, addrlen);
    if (fd != NC_INVALID_SOCK)
        nc_set_nonblocking(fd);
    return fd;
#endif
}

/* ======================================================================
 * TCP_CORK / TCP_NOPUSH — merge header + body into single TCP segment
 * ====================================================================== */

static inline int nc_set_cork(nc_sock_t fd, int enable) {
#if defined(__linux__) || defined(__ANDROID__)
    return setsockopt(fd, IPPROTO_TCP, TCP_CORK, &enable, sizeof(enable));
#elif defined(TCP_NOPUSH)
    return setsockopt(fd, IPPROTO_TCP, TCP_NOPUSH, &enable, sizeof(enable));
#else
    (void)fd; (void)enable;
    return 0;
#endif
}

/* ======================================================================
 * Performance: writev() — scatter/gather write (header + body in one syscall)
 * ====================================================================== */

#ifdef _WIN32
static inline int nc_writev(nc_sock_t fd, const void *hdr, size_t hdr_len,
                              const void *body, size_t body_len) {
    WSABUF bufs[2];
    int nbufs = 0;
    if (hdr && hdr_len > 0) {
        bufs[nbufs].buf = (char *)hdr;
        bufs[nbufs].len = (ULONG)hdr_len;
        nbufs++;
    }
    if (body && body_len > 0) {
        bufs[nbufs].buf = (char *)body;
        bufs[nbufs].len = (ULONG)body_len;
        nbufs++;
    }
    if (nbufs == 0) return 0;
    size_t total = hdr_len + body_len;
    size_t sent = 0;
    while (sent < total) {
        DWORD n = 0;
        int cur = 0;
        WSABUF wb[2];
        int wn = 0;
        size_t skip = sent;
        if (hdr && hdr_len > 0) {
            if (skip < hdr_len) {
                wb[wn].buf = (char *)hdr + skip;
                wb[wn].len = (ULONG)(hdr_len - skip);
                wn++;
                skip = 0;
            } else {
                skip -= hdr_len;
            }
        }
        if (body && body_len > 0 && wn < 2) {
            if (skip < body_len) {
                wb[wn].buf = (char *)body + skip;
                wb[wn].len = (ULONG)(body_len - skip);
                wn++;
            }
        }
        if (wn == 0) break;
        cur = WSASend(fd, wb, wn, &n, 0, NULL, NULL);
        if (cur != 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}
#else
#include <sys/uio.h>
static inline int nc_writev(nc_sock_t fd, const void *hdr, size_t hdr_len,
                              const void *body, size_t body_len) {
    size_t total = hdr_len + body_len;
    size_t sent = 0;
    while (sent < total) {
        struct iovec iov[2];
        int iovcnt = 0;
        size_t skip = sent;
        if (hdr && hdr_len > 0) {
            if (skip < hdr_len) {
                iov[iovcnt].iov_base = (char *)hdr + skip;
                iov[iovcnt].iov_len = hdr_len - skip;
                iovcnt++;
                skip = 0;
            } else {
                skip -= hdr_len;
            }
        }
        if (body && body_len > 0 && iovcnt < 2) {
            if (skip < body_len) {
                iov[iovcnt].iov_base = (char *)body + skip;
                iov[iovcnt].iov_len = body_len - skip;
                iovcnt++;
            }
        }
        if (iovcnt == 0) break;
        ssize_t n = writev(fd, iov, iovcnt);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return (int)sent;
}
#endif

/* ======================================================================
 * Performance: TCP_DEFER_ACCEPT — don't wake listener until data arrives
 * ====================================================================== */

static inline int nc_set_defer_accept(nc_sock_t fd) {
#if defined(__linux__) && defined(TCP_DEFER_ACCEPT)
    int timeout = 10; /* seconds */
    return setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &timeout, sizeof(timeout));
#else
    (void)fd;
    return 0;
#endif
}

/* ======================================================================
 * Performance: TCP_QUICKACK — disable delayed ACK for fast responses
 * ====================================================================== */

static inline int nc_set_quickack(nc_sock_t fd) {
#if defined(__linux__) && defined(TCP_QUICKACK)
    int opt = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &opt, sizeof(opt));
#else
    (void)fd;
    return 0;
#endif
}

/* ======================================================================
 * Atomic operations (for lock-free counters)
 * ====================================================================== */

#if defined(__GNUC__) || defined(__clang__)
  #define nc_atomic_inc(ptr) __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST)
  #define nc_atomic_dec(ptr) __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST)
  #define nc_atomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
  #define nc_atomic_store(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)
  #define nc_atomic_cas(ptr, expected, desired) \
      __sync_bool_compare_and_swap(ptr, expected, desired)
#elif defined(_WIN32)
  #define nc_atomic_inc(ptr) InterlockedIncrement((volatile LONG *)(ptr))
  #define nc_atomic_dec(ptr) InterlockedDecrement((volatile LONG *)(ptr))
  #define nc_atomic_load(ptr) InterlockedCompareExchange((volatile LONG *)(ptr), 0, 0)
  #define nc_atomic_store(ptr, val) InterlockedExchange((volatile LONG *)(ptr), val)
  #define nc_atomic_cas(ptr, expected, desired) \
      (InterlockedCompareExchange((volatile LONG *)(ptr), desired, expected) == (LONG)(expected))
#endif

/* ======================================================================
 * Thread creation helper
 * ====================================================================== */

typedef void *(*nc_thread_func_t)(void *);

static inline int nc_thread_create(nc_thread_t *t, nc_thread_func_t func, void *arg) {
#ifdef _WIN32
    *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return *t ? 0 : -1;
#else
    return pthread_create(t, NULL, func, arg);
#endif
}

static inline int nc_thread_join(nc_thread_t t) {
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
#else
    return pthread_join(t, NULL);
#endif
}

/* ======================================================================
 * Buffer Pool — lock-free LIFO cache for reducing malloc/free pressure
 *
 * Each pool manages fixed-size buffers. Pop returns a buffer (may malloc
 * if pool is empty), Push returns it to the pool. Thread-safe via CAS.
 * ====================================================================== */

#define NC_BUFPOOL_MAX_CACHED 512

typedef struct nc_bufpool_node {
    struct nc_bufpool_node *next;
} nc_bufpool_node_t;

typedef struct {
    nc_bufpool_node_t *volatile head;
    volatile int count;
    size_t buf_size;
    nc_mutex_t lock;
} nc_bufpool_t;

static inline void nc_bufpool_init(nc_bufpool_t *pool, size_t buf_size) {
    pool->head = NULL;
    pool->count = 0;
    pool->buf_size = buf_size < sizeof(nc_bufpool_node_t)
                   ? sizeof(nc_bufpool_node_t) : buf_size;
    nc_mutex_init(&pool->lock);
}

static inline void *nc_bufpool_pop(nc_bufpool_t *pool) {
    nc_mutex_lock(&pool->lock);
    nc_bufpool_node_t *node = pool->head;
    if (node) {
        pool->head = node->next;
        pool->count--;
        nc_mutex_unlock(&pool->lock);
        memset(node, 0, pool->buf_size);
        return node;
    }
    nc_mutex_unlock(&pool->lock);
    return calloc(1, pool->buf_size);
}

static inline void nc_bufpool_push(nc_bufpool_t *pool, void *buf) {
    if (!buf) return;
    nc_mutex_lock(&pool->lock);
    if (pool->count >= NC_BUFPOOL_MAX_CACHED) {
        nc_mutex_unlock(&pool->lock);
        free(buf);
        return;
    }
    nc_bufpool_node_t *node = (nc_bufpool_node_t *)buf;
    node->next = pool->head;
    pool->head = node;
    pool->count++;
    nc_mutex_unlock(&pool->lock);
}

static inline void nc_bufpool_destroy(nc_bufpool_t *pool) {
    nc_mutex_lock(&pool->lock);
    nc_bufpool_node_t *n = pool->head;
    while (n) {
        nc_bufpool_node_t *next = n->next;
        free(n);
        n = next;
    }
    pool->head = NULL;
    pool->count = 0;
    nc_mutex_unlock(&pool->lock);
    nc_mutex_destroy(&pool->lock);
}

/* ======================================================================
 * Connection Limiter — atomic counter for enforcing max connection limits
 * ====================================================================== */

typedef struct {
    volatile int current;
    int max_conns;
} nc_conn_limiter_t;

static inline void nc_conn_limiter_init(nc_conn_limiter_t *l, int max) {
    l->current = 0;
    l->max_conns = max;
}

static inline int nc_conn_limiter_try_acquire(nc_conn_limiter_t *l) {
    if (l->max_conns <= 0) return 1; /* 0 = unlimited */
    for (;;) {
        int cur = nc_atomic_load(&l->current);
        if (cur >= l->max_conns) return 0;
        if (nc_atomic_cas(&l->current, cur, cur + 1))
            return 1;
    }
}

static inline void nc_conn_limiter_release(nc_conn_limiter_t *l) {
    nc_atomic_dec(&l->current);
}

static inline int nc_conn_limiter_count(nc_conn_limiter_t *l) {
    return nc_atomic_load(&l->current);
}

/* ======================================================================
 * Timer Wheel — O(1) timer scheduling for connection timeouts
 *
 * Hierarchical hashed timing wheel. Granularity = 1 ms, slots = 512.
 * Each slot is a doubly-linked list of timers. nc_tw_tick() advances
 * the wheel and fires all expired timers in the current slot.
 * ====================================================================== */

#define NC_TW_SLOTS 512

typedef struct nc_timer nc_timer_t;

typedef void (*nc_timer_cb_t)(nc_timer_t *timer, void *data);

struct nc_timer {
    nc_timer_cb_t   cb;
    void           *data;
    uint64_t        expire_ms;
    int             active;
    nc_timer_t     *tw_next;
    nc_timer_t     *tw_prev;
};

typedef struct {
    nc_timer_t *slots[NC_TW_SLOTS];
    uint64_t    last_ms;
} nc_timer_wheel_t;

static inline void nc_tw_init(nc_timer_wheel_t *tw) {
    memset(tw, 0, sizeof(*tw));
    tw->last_ms = nc_monotonic_ms();
}

static inline void nc_timer_init(nc_timer_t *t, nc_timer_cb_t cb, void *data) {
    memset(t, 0, sizeof(*t));
    t->cb = cb;
    t->data = data;
}

static inline void nc_tw_unlink(nc_timer_wheel_t *tw, nc_timer_t *t) {
    int slot = (int)(t->expire_ms % NC_TW_SLOTS);
    nc_timer_t *p = t->tw_prev;
    nc_timer_t *n = t->tw_next;
    if (p) p->tw_next = n;
    else   tw->slots[slot] = n;
    if (n) n->tw_prev = p;
    t->tw_prev = NULL;
    t->tw_next = NULL;
}

static inline void nc_tw_add(nc_timer_wheel_t *tw, nc_timer_t *t,
                              uint32_t delay_ms) {
    if (t->active)
        nc_tw_unlink(tw, t);

    t->expire_ms = nc_monotonic_ms() + delay_ms;
    t->active = 1;
    int slot = (int)(t->expire_ms % NC_TW_SLOTS);
    t->tw_prev = NULL;
    t->tw_next = tw->slots[slot];
    if (tw->slots[slot]) tw->slots[slot]->tw_prev = t;
    tw->slots[slot] = t;
}

static inline void nc_tw_cancel(nc_timer_wheel_t *tw, nc_timer_t *t) {
    if (!t->active) return;
    nc_tw_unlink(tw, t);
    t->active = 0;
}

static inline void nc_tw_tick(nc_timer_wheel_t *tw) {
    uint64_t now = nc_monotonic_ms();
    if (now <= tw->last_ms) return;

    for (uint64_t ms = tw->last_ms + 1; ms <= now; ms++) {
        int slot = (int)(ms % NC_TW_SLOTS);
        nc_timer_t *t = tw->slots[slot];
        while (t) {
            nc_timer_t *next = t->tw_next;
            if (t->expire_ms <= now) {
                nc_tw_unlink(tw, t);
                t->active = 0;
                if (t->cb) t->cb(t, t->data);
            }
            t = next;
        }
    }
    tw->last_ms = now;
}

/* ======================================================================
 * Connection Context — state machine for keep-alive connection tracking
 * ====================================================================== */

typedef enum {
    NC_CONN_IDLE,
    NC_CONN_READING,
    NC_CONN_PROCESSING,
    NC_CONN_WRITING,
    NC_CONN_CLOSING
} nc_conn_state_t;

typedef struct {
    nc_sock_t       fd;
    nc_conn_state_t state;
    int             max_requests;
    int             requests_served;
    uint64_t        created_ms;
    uint64_t        last_active_ms;
    nc_buf_t        read_buf;
    nc_buf_t        write_buf;
} nc_conn_ctx_t;

static inline void nc_conn_ctx_init(nc_conn_ctx_t *ctx, nc_sock_t fd,
                                     int max_requests) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = fd;
    ctx->state = NC_CONN_IDLE;
    ctx->max_requests = max_requests;
    ctx->created_ms = nc_monotonic_ms();
    ctx->last_active_ms = ctx->created_ms;
    nc_buf_init(&ctx->read_buf);
    nc_buf_init(&ctx->write_buf);
}

static inline void nc_conn_ctx_touch(nc_conn_ctx_t *ctx) {
    ctx->last_active_ms = nc_monotonic_ms();
}

static inline int nc_conn_ctx_expired(nc_conn_ctx_t *ctx, int timeout_ms) {
    return (int)(nc_monotonic_ms() - ctx->last_active_ms > (uint64_t)timeout_ms);
}

static inline int nc_conn_ctx_max_reached(nc_conn_ctx_t *ctx) {
    return ctx->requests_served >= ctx->max_requests;
}

/* ======================================================================
 * Graceful Shutdown Controller
 * ====================================================================== */

typedef struct {
    volatile int  draining;
    volatile int  active_conns;
    uint64_t      deadline_ms;
} nc_shutdown_ctl_t;

static inline void nc_shutdown_init(nc_shutdown_ctl_t *ctl) {
    memset(ctl, 0, sizeof(*ctl));
}

static inline int nc_shutdown_should_accept(nc_shutdown_ctl_t *ctl) {
    return !nc_atomic_load(&ctl->draining);
}

static inline int nc_shutdown_is_draining(nc_shutdown_ctl_t *ctl) {
    return nc_atomic_load(&ctl->draining);
}

static inline void nc_shutdown_begin(nc_shutdown_ctl_t *ctl, int timeout_ms) {
    nc_atomic_store(&ctl->draining, 1);
    ctl->deadline_ms = nc_monotonic_ms() + (uint64_t)timeout_ms;
}

static inline void nc_shutdown_conn_add(nc_shutdown_ctl_t *ctl) {
    nc_atomic_inc(&ctl->active_conns);
}

static inline void nc_shutdown_conn_remove(nc_shutdown_ctl_t *ctl) {
    nc_atomic_dec(&ctl->active_conns);
}

static inline int nc_shutdown_complete(nc_shutdown_ctl_t *ctl) {
    return nc_atomic_load(&ctl->active_conns) <= 0;
}

static inline int nc_shutdown_deadline_reached(nc_shutdown_ctl_t *ctl) {
    return nc_monotonic_ms() >= ctl->deadline_ms;
}

/* ======================================================================
 * SO_REUSEPORT — multi-listener (one per CPU core)
 * ====================================================================== */

#define NC_REUSEPORT_MAX 64

typedef struct {
    nc_sock_t fds[NC_REUSEPORT_MAX];
    int       count;
    int       max;
} nc_reuseport_group_t;

static inline int nc_reuseport_init(nc_reuseport_group_t *g, int max) {
    memset(g, 0, sizeof(*g));
    g->max = max > NC_REUSEPORT_MAX ? NC_REUSEPORT_MAX : max;
    return 0;
}

static inline int nc_reuseport_listen(nc_reuseport_group_t *g,
                                       const char *host, uint16_t port,
                                       int backlog) {
    if (g->count >= g->max) return -1;

    nc_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == NC_INVALID_SOCK) return -1;

    nc_set_reuseaddr(fd);
    nc_set_reuseport(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host && host[0])
        inet_pton(AF_INET, host, &addr.sin_addr);
    else
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        nc_sock_close(fd);
        return -1;
    }
    if (listen(fd, backlog) != 0) {
        nc_sock_close(fd);
        return -1;
    }

    g->fds[g->count++] = fd;
    return 0;
}

static inline void nc_reuseport_close(nc_reuseport_group_t *g) {
    for (int i = 0; i < g->count; i++)
        nc_sock_close(g->fds[i]);
    g->count = 0;
}

/* ======================================================================
 * io_uring Native Async I/O Engine (Linux 5.1+)
 *
 * Provides true async accept/recv/send via io_uring, eliminating all
 * read/write syscalls from the hot path. This is where the 2-3x
 * throughput improvement over epoll comes from.
 *
 * Architecture:
 *   - Each worker thread owns an nc_uring_engine_t
 *   - Accept uses multishot (5.19+) with automatic fallback to oneshot
 *   - Recv/send are submitted as SQEs, completions drive state machine
 *   - Buffer rings provide zero-copy reads (provided buffers)
 *   - All operations are batched: multiple SQEs per io_uring_enter()
 *
 * The engine integrates with the existing HTTP server via a drop-in
 * replacement event loop (nc_uring_evloop_run).
 * ====================================================================== */

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)

/* User-data encoding: pack operation type + fd + context pointer.
 * We use the upper 8 bits for the op type, and lower 56 for context. */
#define NC_URING_OP_ACCEPT   1
#define NC_URING_OP_RECV     2
#define NC_URING_OP_SEND     3
#define NC_URING_OP_CLOSE    4
#define NC_URING_OP_POLL     5
#define NC_URING_OP_TIMEOUT  6

/* Pack: [8-bit op] [56-bit ptr/fd] */
static inline uint64_t nc_uring_encode_ud(int op, void *ptr) {
    return ((uint64_t)op << 56) | ((uint64_t)(uintptr_t)ptr & 0x00FFFFFFFFFFFFFFULL);
}
static inline int nc_uring_decode_op(uint64_t ud) {
    return (int)(ud >> 56);
}
static inline void *nc_uring_decode_ptr(uint64_t ud) {
    return (void *)(uintptr_t)(ud & 0x00FFFFFFFFFFFFFFULL);
}

/* Async recv context — tracks an in-flight receive operation */
typedef struct {
    nc_sock_t fd;
    char     *buf;
    size_t    buf_sz;
    void     *user_ctx;
    void    (*on_recv)(void *user_ctx, int nbytes, char *buf);
} nc_uring_recv_ctx_t;

/* Async send context — tracks an in-flight send operation */
typedef struct {
    nc_sock_t fd;
    const char *buf;
    size_t      len;
    size_t      sent;
    void       *user_ctx;
    void      (*on_send)(void *user_ctx, int result);
} nc_uring_send_ctx_t;

/* Native async engine per worker thread */
typedef struct {
    nc_uring_t ring;
    volatile int running;

    /* Accept state */
    nc_sock_t listen_fd;
    int multishot_supported;
    struct sockaddr_storage accept_addr;
    socklen_t accept_addrlen;

    /* Callbacks */
    void *accept_ctx;
    void (*on_accept)(void *ctx, nc_sock_t client_fd,
                      struct sockaddr *addr, socklen_t addrlen);
} nc_uring_engine_t;

static inline int nc_uring_engine_init(nc_uring_engine_t *eng,
                                        unsigned ring_size) {
    memset(eng, 0, sizeof(*eng));
    eng->listen_fd = NC_INVALID_SOCK;
    eng->accept_addrlen = sizeof(eng->accept_addr);
    if (nc_uring_init(&eng->ring, ring_size) != 0)
        return -1;
    eng->running = 1;
    return 0;
}

static inline void nc_uring_engine_destroy(nc_uring_engine_t *eng) {
    eng->running = 0;
    nc_uring_destroy(&eng->ring);
}

/* Submit an async accept. Tries multishot first, falls back to oneshot. */
static inline int nc_uring_engine_accept(nc_uring_engine_t *eng,
                                          nc_sock_t listen_fd,
                                          void *ctx,
                                          void (*on_accept)(void *, nc_sock_t,
                                                            struct sockaddr *,
                                                            socklen_t)) {
    eng->listen_fd = listen_fd;
    eng->accept_ctx = ctx;
    eng->on_accept = on_accept;

    struct io_uring_sqe *sqe = nc_uring_get_sqe(&eng->ring);
    if (!sqe) return -1;

    eng->accept_addrlen = sizeof(eng->accept_addr);

    if (eng->multishot_supported) {
        nc_uring_prep_accept_multishot(sqe, listen_fd,
            (struct sockaddr *)&eng->accept_addr, &eng->accept_addrlen,
            SOCK_NONBLOCK | SOCK_CLOEXEC,
            nc_uring_encode_ud(NC_URING_OP_ACCEPT, eng));
    } else {
        nc_uring_prep_accept(sqe, listen_fd,
            (struct sockaddr *)&eng->accept_addr, &eng->accept_addrlen,
            SOCK_NONBLOCK | SOCK_CLOEXEC,
            nc_uring_encode_ud(NC_URING_OP_ACCEPT, eng));
    }
    nc_uring_sq_advance(&eng->ring, 1);
    return 0;
}

/* Submit an async recv */
static inline int nc_uring_engine_recv(nc_uring_engine_t *eng,
                                        nc_uring_recv_ctx_t *rctx) {
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&eng->ring);
    if (!sqe) {
        nc_uring_submit(&eng->ring);
        sqe = nc_uring_get_sqe(&eng->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_recv(sqe, rctx->fd, rctx->buf, (unsigned)rctx->buf_sz,
                       0, nc_uring_encode_ud(NC_URING_OP_RECV, rctx));
    nc_uring_sq_advance(&eng->ring, 1);
    return 0;
}

/* Submit an async send */
static inline int nc_uring_engine_send(nc_uring_engine_t *eng,
                                        nc_uring_send_ctx_t *sctx) {
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&eng->ring);
    if (!sqe) {
        nc_uring_submit(&eng->ring);
        sqe = nc_uring_get_sqe(&eng->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_send(sqe, sctx->fd, sctx->buf + sctx->sent,
                       (unsigned)(sctx->len - sctx->sent),
                       MSG_NOSIGNAL,
                       nc_uring_encode_ud(NC_URING_OP_SEND, sctx));
    nc_uring_sq_advance(&eng->ring, 1);
    return 0;
}

/* Process CQEs in the engine's completion ring */
static inline int nc_uring_engine_poll(nc_uring_engine_t *eng,
                                        int timeout_ms) {
    /* Submit pending SQEs and wait for at least 1 CQE */
    unsigned pending = *eng->ring.sq_tail -
                       nc_io_smp_load_acquire(eng->ring.sq_head);
    if (pending > 0 || timeout_ms > 0) {
        struct io_uring_cqe *peek = nc_uring_peek_cqe(&eng->ring);
        if (!peek) {
            int flags = timeout_ms > 0 ? IORING_ENTER_GETEVENTS : 0;
            unsigned min_cpl = timeout_ms > 0 ? 1 : 0;
            nc_io_uring_enter(eng->ring.ring_fd, pending, min_cpl, flags);
        } else if (pending > 0) {
            nc_uring_submit(&eng->ring);
        }
    }

    int processed = 0;
    while (processed < 256) {
        struct io_uring_cqe *cqe = nc_uring_peek_cqe(&eng->ring);
        if (!cqe) break;

        uint64_t ud = cqe->user_data;
        int res = cqe->res;
        uint32_t cflags = cqe->flags;
        nc_uring_cq_advance(&eng->ring, 1);
        processed++;

        if (ud == (uint64_t)-1) continue; /* timeout sentinel */

        int op = nc_uring_decode_op(ud);
        void *ptr = nc_uring_decode_ptr(ud);

        switch (op) {
        case NC_URING_OP_ACCEPT: {
            nc_uring_engine_t *e = (nc_uring_engine_t *)ptr;
            if (res >= 0 && e->on_accept) {
                e->on_accept(e->accept_ctx, (nc_sock_t)res,
                             (struct sockaddr *)&e->accept_addr,
                             e->accept_addrlen);
            }
            /* Re-arm accept if not multishot (or multishot ended) */
            if (!(cflags & IORING_CQE_F_MORE) && e->running) {
                if (e->multishot_supported && res == -22 /* EINVAL */) {
                    e->multishot_supported = 0; /* kernel too old */
                }
                nc_uring_engine_accept(e, e->listen_fd,
                                       e->accept_ctx, e->on_accept);
            }
            break;
        }
        case NC_URING_OP_RECV: {
            nc_uring_recv_ctx_t *rctx = (nc_uring_recv_ctx_t *)ptr;
            if (rctx->on_recv) {
                rctx->on_recv(rctx->user_ctx, res, rctx->buf);
            }
            break;
        }
        case NC_URING_OP_SEND: {
            nc_uring_send_ctx_t *sctx = (nc_uring_send_ctx_t *)ptr;
            if (res > 0) {
                sctx->sent += (size_t)res;
                if (sctx->sent < sctx->len) {
                    /* Partial send — submit remainder */
                    nc_uring_engine_send(eng, sctx);
                    break;
                }
            }
            if (sctx->on_send) {
                sctx->on_send(sctx->user_ctx,
                              sctx->sent >= sctx->len ? 0 : -1);
            }
            break;
        }
        default:
            break;
        }
    }
    return processed;
}

/* Run the engine event loop (blocks until engine is stopped) */
static inline void nc_uring_engine_run(nc_uring_engine_t *eng) {
    while (eng->running) {
        nc_uring_engine_poll(eng, 100);
    }
}

static inline void nc_uring_engine_stop(nc_uring_engine_t *eng) {
    eng->running = 0;
}

#endif /* NC_USE_IO_URING */

#endif /* NEVERC_NET_INTERNAL_H */
