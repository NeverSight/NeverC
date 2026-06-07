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

/* Event polling backend detection */
#if defined(__linux__) || defined(__ANDROID__)
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
#if defined(NC_USE_EPOLL)
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

#if defined(NC_USE_EPOLL)
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
#if defined(NC_USE_EPOLL)
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
#if defined(NC_USE_EPOLL)
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
#if defined(NC_USE_EPOLL)
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
#if defined(NC_USE_EPOLL)
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
#if defined(NC_USE_EPOLL)
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
 * Atomic operations (for lock-free counters)
 * ====================================================================== */

#if defined(__GNUC__) || defined(__clang__)
  #define nc_atomic_inc(ptr) __sync_add_and_fetch(ptr, 1)
  #define nc_atomic_dec(ptr) __sync_sub_and_fetch(ptr, 1)
  #define nc_atomic_load(ptr) __sync_add_and_fetch(ptr, 0)
  #define nc_atomic_store(ptr, val) __sync_lock_test_and_set(ptr, val)
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

#endif /* NEVERC_NET_INTERNAL_H */
