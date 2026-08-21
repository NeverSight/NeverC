#ifndef NEVERC_NET_POLLER_H
#define NEVERC_NET_POLLER_H

#include "_net_io_uring.h"
#include "_net_iocp.h"
#if defined(NC_USE_EPOLL)
#include <errno.h>
#endif

#define NC_EV_READ  1
#define NC_EV_WRITE 2
#define NC_EV_ERROR 4
#define NC_POLLER_MAX_BATCH 256

#ifndef NC_POLLER_CALLOC
#define NC_POLLER_CALLOC calloc
#endif

typedef struct {
    nc_sock_t fd;
    int events;
    void *data;
    /* Completion backends populate these; readiness backends leave them zero. */
    void *operation;
    size_t transferred;
    int error;
} nc_event_t;

typedef struct {
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_t ring;
    #define NC_URING_MAX_FDS 65536
    void **fd_data;
    int *fd_events;
    uint32_t *fd_generation;
    int fd_cap;
#elif defined(NC_USE_EPOLL)
    int epfd;
    void **fd_data;
    int *fd_events;
    int fd_cap;
#elif defined(NC_USE_KQUEUE)
    int kqfd;
    void **fd_data;
    int *fd_events;
    uint32_t *fd_generation;
    int fd_cap;
#elif defined(NC_USE_IOCP)
    HANDLE iocp;
    WSAPOLLFD *pfds;
    void **pdata;
    int npfds;
    int cap_pfds;
#else
    struct pollfd *pfds;
    void **pdata;
    int npfds;
    int cap_pfds;
#endif
} nc_poller_t;

static inline int nc_poller_supports_readiness(void) {
    return 1;
}

static inline int nc_poller_supports_completions(void) {
    return NC_HAS_IOCP_COMPLETIONS;
}

#if defined(NC_USE_IOCP)
/*
 * Completion I/O and readiness polling are independent on Windows.  Sockets
 * using AcceptEx/WSARecv/WSASend must opt into the completion port explicitly;
 * nc_poller_add() only registers a socket with the WSAPoll readiness set.
 */
static inline int nc_poller_associate_completion(nc_poller_t *poller,
                                                  nc_sock_t fd,
                                                  void *data) {
    if (!poller || fd == NC_INVALID_SOCK) return -1;
    HANDLE handle = CreateIoCompletionPort(
        (HANDLE)fd, poller->iocp, (ULONG_PTR)data, 0);
    return handle ? 0 : -1;
}
#endif

static inline nc_poller_t *nc_poller_create(void) {
    nc_poller_t *poller =
        (nc_poller_t *)NC_POLLER_CALLOC(1, sizeof(*poller));
    if (!poller) return NULL;

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (nc_uring_init(&poller->ring, 4096) != 0) {
        free(poller);
        return NULL;
    }
    poller->fd_cap = NC_URING_MAX_FDS;
    poller->fd_data =
        (void **)NC_POLLER_CALLOC((size_t)poller->fd_cap, sizeof(void *));
    poller->fd_events =
        (int *)NC_POLLER_CALLOC((size_t)poller->fd_cap, sizeof(int));
    poller->fd_generation = (uint32_t *)NC_POLLER_CALLOC(
        (size_t)poller->fd_cap, sizeof(uint32_t));
    if (!poller->fd_data || !poller->fd_events ||
        !poller->fd_generation) {
        free(poller->fd_data);
        free(poller->fd_events);
        free(poller->fd_generation);
        nc_uring_destroy(&poller->ring);
        free(poller);
        return NULL;
    }
#elif defined(NC_USE_EPOLL)
    poller->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (poller->epfd < 0) {
        free(poller);
        return NULL;
    }
    poller->fd_cap = 64;
    poller->fd_data = (void **)NC_POLLER_CALLOC(
        (size_t)poller->fd_cap, sizeof(void *));
    poller->fd_events = (int *)NC_POLLER_CALLOC(
        (size_t)poller->fd_cap, sizeof(int));
    if (!poller->fd_data || !poller->fd_events) {
        free(poller->fd_data);
        free(poller->fd_events);
        close(poller->epfd);
        free(poller);
        return NULL;
    }
#elif defined(NC_USE_KQUEUE)
    poller->kqfd = kqueue();
    if (poller->kqfd < 0) {
        free(poller);
        return NULL;
    }
    poller->fd_cap = 64;
    poller->fd_data = (void **)NC_POLLER_CALLOC(
        (size_t)poller->fd_cap, sizeof(void *));
    poller->fd_events = (int *)NC_POLLER_CALLOC(
        (size_t)poller->fd_cap, sizeof(int));
    poller->fd_generation = (uint32_t *)NC_POLLER_CALLOC(
        (size_t)poller->fd_cap, sizeof(uint32_t));
    if (!poller->fd_data || !poller->fd_events ||
        !poller->fd_generation) {
        free(poller->fd_data);
        free(poller->fd_events);
        free(poller->fd_generation);
        close(poller->kqfd);
        free(poller);
        return NULL;
    }
#elif defined(NC_USE_IOCP)
    poller->iocp =
        CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!poller->iocp) {
        free(poller);
        return NULL;
    }
    poller->cap_pfds = 64;
    poller->pfds = (WSAPOLLFD *)NC_POLLER_CALLOC(
        (size_t)poller->cap_pfds, sizeof(*poller->pfds));
    poller->pdata = (void **)NC_POLLER_CALLOC(
        (size_t)poller->cap_pfds, sizeof(*poller->pdata));
    if (!poller->pfds || !poller->pdata) {
        free(poller->pfds);
        free(poller->pdata);
        CloseHandle(poller->iocp);
        free(poller);
        return NULL;
    }
#else
    poller->cap_pfds = 64;
    poller->pfds = (struct pollfd *)NC_POLLER_CALLOC(
        (size_t)poller->cap_pfds, sizeof(struct pollfd));
    poller->pdata = (void **)NC_POLLER_CALLOC(
        (size_t)poller->cap_pfds, sizeof(void *));
    if (!poller->pfds || !poller->pdata) {
        free(poller->pfds);
        free(poller->pdata);
        free(poller);
        return NULL;
    }
#endif
    return poller;
}

/*
 * IOCP callers must cancel and dequeue every outstanding operation before
 * destroying the poller; closing the port does not make operation storage
 * safe to release.
 */
static inline void nc_poller_destroy(nc_poller_t *poller) {
    if (!poller) return;
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    nc_uring_destroy(&poller->ring);
    free(poller->fd_data);
    free(poller->fd_events);
    free(poller->fd_generation);
#elif defined(NC_USE_EPOLL)
    close(poller->epfd);
    free(poller->fd_data);
    free(poller->fd_events);
#elif defined(NC_USE_KQUEUE)
    close(poller->kqfd);
    free(poller->fd_data);
    free(poller->fd_events);
    free(poller->fd_generation);
#elif defined(NC_USE_IOCP)
    free(poller->pfds);
    free(poller->pdata);
    CloseHandle(poller->iocp);
#else
    free(poller->pfds);
    free(poller->pdata);
#endif
    free(poller);
}

#if !defined(NC_USE_IO_URING) && !defined(NC_USE_EPOLL) && \
    !defined(NC_USE_KQUEUE)
static inline int nc_poller_reserve(nc_poller_t *poller, int capacity) {
    if (!poller || capacity <= poller->cap_pfds) return 0;
    if (capacity <= 0) return -1;
#if defined(NC_USE_IOCP)
    if ((size_t)capacity > SIZE_MAX / sizeof(WSAPOLLFD) ||
        (size_t)capacity > SIZE_MAX / sizeof(void *))
        return -1;
    WSAPOLLFD *pfds = (WSAPOLLFD *)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(*pfds));
#else
    if ((size_t)capacity > SIZE_MAX / sizeof(struct pollfd) ||
        (size_t)capacity > SIZE_MAX / sizeof(void *))
        return -1;
    struct pollfd *pfds = (struct pollfd *)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(struct pollfd));
#endif
    void **pdata =
        (void **)NC_POLLER_CALLOC((size_t)capacity, sizeof(void *));
    if (!pfds || !pdata) {
        free(pfds);
        free(pdata);
        return -1;
    }
    if (poller->npfds > 0) {
        memcpy(pfds, poller->pfds,
               (size_t)poller->npfds * sizeof(*pfds));
        memcpy(pdata, poller->pdata,
               (size_t)poller->npfds * sizeof(void *));
    }
    free(poller->pfds);
    free(poller->pdata);
    poller->pfds = pfds;
    poller->pdata = pdata;
    poller->cap_pfds = capacity;
    return 0;
}
#endif

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
static inline uint64_t nc_poller_uring_token(nc_poller_t *poller, int fd) {
    return ((uint64_t)poller->fd_generation[fd] << 32) |
           (uint32_t)fd;
}
#endif

#if defined(NC_USE_EPOLL) || defined(NC_USE_KQUEUE)
static inline int nc_poller_fd_table_reserve(nc_poller_t *poller, int fd) {
    if (!poller || fd < 0) return -1;
    if (fd < poller->fd_cap) return 0;
    int capacity = poller->fd_cap;
    while (capacity <= fd) {
        if (capacity <= 0 || capacity > INT_MAX / 2) return -1;
        capacity *= 2;
    }
    if ((size_t)capacity > SIZE_MAX / sizeof(void *) ||
        (size_t)capacity > SIZE_MAX / sizeof(int)
#if defined(NC_USE_KQUEUE)
        || (size_t)capacity > SIZE_MAX / sizeof(uint32_t)
#endif
        )
        return -1;
    void **fd_data = (void **)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(void *));
    int *fd_events = (int *)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(int));
#if defined(NC_USE_KQUEUE)
    uint32_t *fd_generation = (uint32_t *)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(uint32_t));
    if (!fd_data || !fd_events || !fd_generation) {
        free(fd_data);
        free(fd_events);
        free(fd_generation);
        return -1;
    }
#else
    if (!fd_data || !fd_events) {
        free(fd_data);
        free(fd_events);
        return -1;
    }
#endif
    memcpy(fd_data, poller->fd_data,
           (size_t)poller->fd_cap * sizeof(void *));
    memcpy(fd_events, poller->fd_events,
           (size_t)poller->fd_cap * sizeof(int));
#if defined(NC_USE_KQUEUE)
    memcpy(fd_generation, poller->fd_generation,
           (size_t)poller->fd_cap * sizeof(uint32_t));
    free(poller->fd_generation);
    poller->fd_generation = fd_generation;
#endif
    free(poller->fd_data);
    free(poller->fd_events);
    poller->fd_data = fd_data;
    poller->fd_events = fd_events;
    poller->fd_cap = capacity;
    return 0;
}
#endif

#if defined(NC_USE_KQUEUE)
static inline int nc_poller_kqueue_change(nc_poller_t *poller,
                                          nc_sock_t fd, int16_t filter,
                                          uint16_t flags, void *data,
                                          int ignore_missing) {
    struct kevent change;
    EV_SET(&change, (uintptr_t)fd, filter, flags, 0, 0, data);
    int result = kevent(poller->kqfd, &change, 1, NULL, 0, NULL);
    if (result < 0 && ignore_missing && errno == ENOENT)
        return 0;
    return result;
}
#endif

static inline int nc_poller_add(nc_poller_t *poller, nc_sock_t fd,
                                int events, void *data) {
    if (!poller || fd == NC_INVALID_SOCK || events == 0 ||
        (events & ~(NC_EV_READ | NC_EV_WRITE)) != 0)
        return -1;

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] != 0)
        return -1;
    unsigned poll_mask = 0;
    if (events & NC_EV_READ) poll_mask |= POLLIN;
    if (events & NC_EV_WRITE) poll_mask |= POLLOUT;
    poller->fd_generation[fd]++;
    if (poller->fd_generation[fd] == 0)
        poller->fd_generation[fd] = 1;
    uint64_t token = nc_poller_uring_token(poller, fd);
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&poller->ring);
    if (!sqe) {
        if (nc_uring_submit(&poller->ring) < 0) return -1;
        sqe = nc_uring_get_sqe(&poller->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_poll_add(sqe, fd, poll_mask, token);
    nc_uring_sq_advance(&poller->ring, 1);
    poller->fd_data[fd] = data;
    poller->fd_events[fd] = events;
    if (nc_uring_submit(&poller->ring) < 0) {
        poller->fd_data[fd] = NULL;
        poller->fd_events[fd] = 0;
        return -1;
    }
    return 0;
#elif defined(NC_USE_EPOLL)
    if (fd < 0 || nc_poller_fd_table_reserve(poller, fd) != 0 ||
        poller->fd_events[fd] != 0)
        return -1;
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    if (events & NC_EV_READ) event.events |= EPOLLIN;
    if (events & NC_EV_WRITE) event.events |= EPOLLOUT;
    event.events |= EPOLLET;
    int result = epoll_ctl(poller->epfd, EPOLL_CTL_ADD, fd, &event);
    if (result == 0) {
        poller->fd_data[fd] = data;
        poller->fd_events[fd] = events;
    }
    return result;
#elif defined(NC_USE_KQUEUE)
    if (fd < 0 || nc_poller_fd_table_reserve(poller, (int)fd) != 0 ||
        poller->fd_events[fd] != 0)
        return -1;
    poller->fd_generation[fd]++;
    if (poller->fd_generation[fd] == 0)
        poller->fd_generation[fd] = 1;
    void *udata = (void *)(uintptr_t)poller->fd_generation[fd];
    struct kevent changes[2];
    int count = 0;
    if (events & NC_EV_READ) {
        EV_SET(&changes[count++], (uintptr_t)fd, EVFILT_READ,
               EV_ADD | EV_CLEAR, 0, 0, udata);
    }
    if (events & NC_EV_WRITE) {
        EV_SET(&changes[count++], (uintptr_t)fd, EVFILT_WRITE,
               EV_ADD | EV_CLEAR, 0, 0, udata);
    }
    int result = kevent(poller->kqfd, changes, count, NULL, 0, NULL);
    if (result == 0) {
        poller->fd_data[fd] = data;
        poller->fd_events[fd] = events;
    }
    return result;
#elif defined(NC_USE_IOCP)
    for (int i = 0; i < poller->npfds; i++)
        if (poller->pfds[i].fd == fd) return -1;
    if (poller->npfds >= poller->cap_pfds) {
        if (poller->cap_pfds > INT_MAX / 2 ||
            nc_poller_reserve(poller, poller->cap_pfds * 2) != 0)
            return -1;
    }
    WSAPOLLFD *entry = &poller->pfds[poller->npfds];
    memset(entry, 0, sizeof(*entry));
    entry->fd = fd;
    if (events & NC_EV_READ) entry->events |= POLLRDNORM;
    if (events & NC_EV_WRITE) entry->events |= POLLWRNORM;
    poller->pdata[poller->npfds++] = data;
    return 0;
#else
    for (int i = 0; i < poller->npfds; i++)
        if (poller->pfds[i].fd == fd) return -1;
    if (poller->npfds >= poller->cap_pfds) {
        if (poller->cap_pfds > INT_MAX / 2 ||
            nc_poller_reserve(poller, poller->cap_pfds * 2) != 0)
            return -1;
    }
    struct pollfd *entry = &poller->pfds[poller->npfds];
    memset(entry, 0, sizeof(*entry));
    entry->fd = fd;
    if (events & NC_EV_READ) entry->events |= POLLIN;
    if (events & NC_EV_WRITE) entry->events |= POLLOUT;
    poller->pdata[poller->npfds++] = data;
    return 0;
#endif
}

static inline int nc_poller_mod(nc_poller_t *poller, nc_sock_t fd,
                                int events, void *data) {
    if (!poller || fd == NC_INVALID_SOCK || events == 0 ||
        (events & ~(NC_EV_READ | NC_EV_WRITE)) != 0)
        return -1;

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&poller->ring);
    if (!sqe) {
        if (nc_uring_submit(&poller->ring) < 0) return -1;
        sqe = nc_uring_get_sqe(&poller->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_poll_remove(
        sqe, nc_poller_uring_token(poller, fd));
    nc_uring_sq_advance(&poller->ring, 1);
    if (nc_uring_submit(&poller->ring) < 0) return -1;
    poller->fd_data[fd] = NULL;
    poller->fd_events[fd] = 0;
    return nc_poller_add(poller, fd, events, data);
#elif defined(NC_USE_EPOLL)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    if (events & NC_EV_READ) event.events |= EPOLLIN;
    if (events & NC_EV_WRITE) event.events |= EPOLLOUT;
    event.events |= EPOLLET;
    int result = epoll_ctl(poller->epfd, EPOLL_CTL_MOD, fd, &event);
    if (result == 0) {
        poller->fd_data[fd] = data;
        poller->fd_events[fd] = events;
    }
    return result;
#elif defined(NC_USE_KQUEUE)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    poller->fd_generation[fd]++;
    if (poller->fd_generation[fd] == 0)
        poller->fd_generation[fd] = 1;
    void *udata = (void *)(uintptr_t)poller->fd_generation[fd];
    int result = nc_poller_kqueue_change(
        poller, fd, EVFILT_READ,
        (events & NC_EV_READ) ? (EV_ADD | EV_CLEAR) : EV_DELETE,
        udata, !(events & NC_EV_READ));
    if (result != 0) return result;
    result = nc_poller_kqueue_change(
        poller, fd, EVFILT_WRITE,
        (events & NC_EV_WRITE) ? (EV_ADD | EV_CLEAR) : EV_DELETE,
        udata, !(events & NC_EV_WRITE));
    if (result == 0) {
        poller->fd_data[fd] = data;
        poller->fd_events[fd] = events;
    }
    return result;
#elif defined(NC_USE_IOCP)
    for (int i = 0; i < poller->npfds; i++) {
        if (poller->pfds[i].fd != fd) continue;
        poller->pfds[i].events = 0;
        if (events & NC_EV_READ)
            poller->pfds[i].events |= POLLRDNORM;
        if (events & NC_EV_WRITE)
            poller->pfds[i].events |= POLLWRNORM;
        poller->pdata[i] = data;
        return 0;
    }
    return -1;
#else
    for (int i = 0; i < poller->npfds; i++) {
        if (poller->pfds[i].fd != fd) continue;
        poller->pfds[i].events = 0;
        if (events & NC_EV_READ) poller->pfds[i].events |= POLLIN;
        if (events & NC_EV_WRITE) poller->pfds[i].events |= POLLOUT;
        poller->pdata[i] = data;
        return 0;
    }
    return -1;
#endif
}

static inline int nc_poller_del(nc_poller_t *poller, nc_sock_t fd) {
    if (!poller || fd == NC_INVALID_SOCK) return -1;
#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    uint64_t token = nc_poller_uring_token(poller, fd);
    poller->fd_generation[fd]++;
    if (poller->fd_generation[fd] == 0)
        poller->fd_generation[fd] = 1;
    poller->fd_data[fd] = NULL;
    poller->fd_events[fd] = 0;
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&poller->ring);
    if (!sqe) {
        if (nc_uring_submit(&poller->ring) < 0) return 0;
        sqe = nc_uring_get_sqe(&poller->ring);
        if (!sqe) return 0;
    }
    nc_uring_prep_poll_remove(sqe, token);
    nc_uring_sq_advance(&poller->ring, 1);
    (void)nc_uring_submit(&poller->ring);
    return 0;
#elif defined(NC_USE_EPOLL)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    int result = epoll_ctl(poller->epfd, EPOLL_CTL_DEL, fd, NULL);
    if (result != 0 && (errno == EBADF || errno == ENOENT))
        result = 0;
    if (result == 0) {
        poller->fd_data[fd] = NULL;
        poller->fd_events[fd] = 0;
    }
    return result;
#elif defined(NC_USE_KQUEUE)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    poller->fd_generation[fd]++;
    if (poller->fd_generation[fd] == 0)
        poller->fd_generation[fd] = 1;
    poller->fd_data[fd] = NULL;
    poller->fd_events[fd] = 0;
    int read_result = nc_poller_kqueue_change(
        poller, fd, EVFILT_READ, EV_DELETE, NULL, 1);
    int write_result = nc_poller_kqueue_change(
        poller, fd, EVFILT_WRITE, EV_DELETE, NULL, 1);
    return read_result != 0 ? read_result : write_result;
#elif defined(NC_USE_IOCP)
    for (int i = 0; i < poller->npfds; i++) {
        if (poller->pfds[i].fd != fd) continue;
        poller->pfds[i] = poller->pfds[poller->npfds - 1];
        poller->pdata[i] = poller->pdata[poller->npfds - 1];
        poller->npfds--;
        return 0;
    }
    return -1;
#else
    for (int i = 0; i < poller->npfds; i++) {
        if (poller->pfds[i].fd != fd) continue;
        poller->pfds[i] = poller->pfds[poller->npfds - 1];
        poller->pdata[i] = poller->pdata[poller->npfds - 1];
        poller->npfds--;
        return 0;
    }
    return -1;
#endif
}

#if defined(NC_USE_IOCP)
static inline int nc_poller_iocp_dequeue(nc_poller_t *poller,
                                         nc_event_t *out,
                                         int max_events,
                                         DWORD timeout) {
    OVERLAPPED_ENTRY entries[NC_POLLER_MAX_BATCH];
    ULONG removed = 0;
    if (max_events > NC_POLLER_MAX_BATCH)
        max_events = NC_POLLER_MAX_BATCH;
    if (max_events <= 0) return -1;
    BOOL ok = GetQueuedCompletionStatusEx(
        poller->iocp, entries, (ULONG)max_events, &removed, timeout, FALSE);
    if (!ok) {
        DWORD error = GetLastError();
        return error == WAIT_TIMEOUT ? 0 : -1;
    }
    for (ULONG i = 0; i < removed; i++) {
        nc_iocp_op_t *operation =
            (nc_iocp_op_t *)entries[i].lpOverlapped;
        out[i].operation = operation;
        out[i].transferred = 0;
        out[i].error = 0;
        out[i].fd = NC_INVALID_SOCK;
        out[i].events = 0;
        if (!operation) {
            out[i].data = (void *)entries[i].lpCompletionKey;
            continue;
        }

        nc_iocp_op_complete(
            operation, entries[i].dwNumberOfBytesTransferred);
        out[i].data = operation->data;
        out[i].fd = operation->fd;
        out[i].transferred = (size_t)operation->transferred;
        out[i].error = operation->error;
        if (operation->kind == NC_IOCP_OP_ACCEPT ||
            operation->kind == NC_IOCP_OP_RECV)
            out[i].events |= NC_EV_READ;
        else if (operation->kind == NC_IOCP_OP_SEND)
            out[i].events |= NC_EV_WRITE;
        if (operation->error != 0) out[i].events |= NC_EV_ERROR;
    }
    return (int)removed;
}
#endif

static inline int nc_poller_wait(nc_poller_t *poller, nc_event_t *out,
                                 int max_events, int timeout_ms) {
    if (!poller || !out || max_events <= 0 || timeout_ms < -1)
        return -1;
    if (max_events > NC_POLLER_MAX_BATCH)
        max_events = NC_POLLER_MAX_BATCH;

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    for (;;) {
        if (nc_uring_submit(&poller->ring) < 0) return -1;
        if (!nc_uring_peek_cqe(&poller->ring)) {
            struct pollfd ring_fd;
            memset(&ring_fd, 0, sizeof(ring_fd));
            ring_fd.fd = poller->ring.ring_fd;
            ring_fd.events = POLLIN;
            int ready;
            do {
                ready = poll(&ring_fd, 1, timeout_ms);
            } while (ready < 0 && errno == EINTR);
            if (ready <= 0) return ready;
        }

        int count = 0;
        int rearm_count = 0;
        struct {
            int fd;
            unsigned poll_mask;
        } rearms[NC_POLLER_MAX_BATCH];

        while (count < max_events) {
            struct io_uring_cqe *cqe = nc_uring_peek_cqe(&poller->ring);
            if (!cqe) break;
            uint64_t user_data = cqe->user_data;
            int result = cqe->res;
            nc_uring_cq_advance(&poller->ring, 1);

            if (user_data == UINT64_MAX) continue;
            int fd = (int)(uint32_t)user_data;
            uint32_t generation = (uint32_t)(user_data >> 32);
            if (fd < 0 || fd >= poller->fd_cap ||
                poller->fd_events[fd] == 0 ||
                poller->fd_generation[fd] != generation)
                continue;

            out[count].fd = fd;
            out[count].data = poller->fd_data[fd];
            out[count].operation = NULL;
            out[count].transferred = 0;
            out[count].error = 0;
            out[count].events = 0;
            if (result < 0) {
                out[count].events = NC_EV_ERROR;
            } else {
                if ((result & (POLLIN | POLLHUP)) &&
                    (poller->fd_events[fd] & NC_EV_READ))
                    out[count].events |= NC_EV_READ;
                if ((result & POLLOUT) && (poller->fd_events[fd] & NC_EV_WRITE))
                    out[count].events |= NC_EV_WRITE;
                if (result & (POLLERR | POLLNVAL))
                    out[count].events |= NC_EV_ERROR;
            }

            if (result >= 0 && poller->fd_events[fd] != 0 &&
                rearm_count < NC_POLLER_MAX_BATCH) {
                unsigned mask = 0;
                if (poller->fd_events[fd] & NC_EV_READ) mask |= POLLIN;
                if (poller->fd_events[fd] & NC_EV_WRITE) mask |= POLLOUT;
                rearms[rearm_count].fd = fd;
                rearms[rearm_count++].poll_mask = mask;
            }
            if (out[count].events == 0)
                continue;
            count++;
        }

        for (int i = 0; i < rearm_count; i++) {
            struct io_uring_sqe *sqe = nc_uring_get_sqe(&poller->ring);
            if (!sqe) {
                if (nc_uring_submit(&poller->ring) < 0) return -1;
                sqe = nc_uring_get_sqe(&poller->ring);
                if (!sqe) return -1;
            }
            nc_uring_prep_poll_add(
                sqe, rearms[i].fd, rearms[i].poll_mask,
                nc_poller_uring_token(poller, rearms[i].fd));
            nc_uring_sq_advance(&poller->ring, 1);
        }
        if (rearm_count > 0 && nc_uring_submit(&poller->ring) < 0)
            return -1;
        if (count > 0) return count;
        if (timeout_ms == 0) return 0;
        /* Only skippable CQEs (POLL_REMOVE / stale generation). Wait again
         * rather than returning a fake timeout. */
    }
#elif defined(NC_USE_EPOLL)
    struct epoll_event events[NC_POLLER_MAX_BATCH];
    int count =
        epoll_wait(poller->epfd, events, max_events, timeout_ms);
    if (count <= 0) return count;
    int delivered = 0;
    for (int i = 0; i < count; i++) {
        int fd = events[i].data.fd;
        if (fd < 0 || fd >= poller->fd_cap ||
            poller->fd_events[fd] == 0)
            continue;
        out[delivered].fd = fd;
        out[delivered].data = poller->fd_data[fd];
        out[delivered].operation = NULL;
        out[delivered].transferred = 0;
        out[delivered].error = 0;
        out[delivered].events = 0;
        if ((events[i].events & EPOLLIN) &&
            (poller->fd_events[fd] & NC_EV_READ))
            out[delivered].events |= NC_EV_READ;
        if ((events[i].events & EPOLLOUT) &&
            (poller->fd_events[fd] & NC_EV_WRITE))
            out[delivered].events |= NC_EV_WRITE;
        if (events[i].events & EPOLLHUP)
            out[delivered].events |= NC_EV_READ;
        if (events[i].events & EPOLLERR)
            out[delivered].events |= NC_EV_ERROR;
        if (out[delivered].events == 0)
            continue;
        delivered++;
    }
    return delivered;
#elif defined(NC_USE_KQUEUE)
    struct kevent events[NC_POLLER_MAX_BATCH];
    struct timespec timeout;
    struct timespec *timeout_ptr = NULL;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }
    int count = kevent(poller->kqfd, NULL, 0, events, max_events,
                       timeout_ptr);
    if (count <= 0) return count;
    int delivered = 0;
    for (int i = 0; i < count; i++) {
        if (events[i].ident > (uintptr_t)INT_MAX)
            continue;
        int fd = (int)events[i].ident;
        uint32_t generation = (uint32_t)(uintptr_t)events[i].udata;
        if (fd < 0 || fd >= poller->fd_cap ||
            poller->fd_events[fd] == 0 ||
            poller->fd_generation[fd] != generation)
            continue;
        uint32_t ev = 0;
        if (events[i].filter == EVFILT_READ) {
            if (!(poller->fd_events[fd] & NC_EV_READ))
                continue;
            ev |= NC_EV_READ;
        }
        if (events[i].filter == EVFILT_WRITE) {
            if (!(poller->fd_events[fd] & NC_EV_WRITE))
                continue;
            ev |= NC_EV_WRITE;
        }
        /* EV_EOF on a read filter is peer FIN, not an error. Mapping it to
         * NC_EV_ERROR made macOS treat a clean shutdown as a hard failure. */
        if (events[i].flags & EV_ERROR)
            ev |= NC_EV_ERROR;
        else if ((events[i].flags & EV_EOF) &&
                 events[i].filter != EVFILT_READ)
            ev |= NC_EV_ERROR;
        if (ev == 0)
            continue;
        /* epoll coalesces per fd; kqueue delivers one kevent per filter.
         * HTTP frees conn data on the first ERROR, so a second filter in
         * the same batch would UAF. Merge by fd like epoll. */
        int slot = -1;
        for (int j = 0; j < delivered; j++) {
            if (out[j].fd == fd) {
                slot = j;
                break;
            }
        }
        if (slot < 0) {
            if (delivered >= max_events)
                continue;
            slot = delivered++;
            out[slot].data = poller->fd_data[fd];
            out[slot].fd = fd;
            out[slot].operation = NULL;
            out[slot].transferred = 0;
            out[slot].error = 0;
            out[slot].events = 0;
        }
        out[slot].events |= ev;
    }
    return delivered;
#elif defined(NC_USE_IOCP)
    if (poller->npfds == 0) {
        DWORD timeout = timeout_ms >= 0 ? (DWORD)timeout_ms : INFINITE;
        return nc_poller_iocp_dequeue(poller, out, max_events, timeout);
    }

    uint64_t deadline = 0;
    if (timeout_ms >= 0) {
        uint64_t now = nc_monotonic_ms();
        deadline = now + (uint64_t)timeout_ms;
        if (deadline < now) deadline = UINT64_MAX;
    }
    for (;;) {
        int completed = nc_poller_iocp_dequeue(
            poller, out, max_events, 0);
        if (completed != 0) return completed;

        int slice_ms = 10;
        if (timeout_ms >= 0) {
            uint64_t now = nc_monotonic_ms();
            if (now >= deadline) slice_ms = 0;
            else if (deadline - now < (uint64_t)slice_ms)
                slice_ms = (int)(deadline - now);
        }
        int ready = WSAPoll(poller->pfds, (ULONG)poller->npfds, slice_ms);
        if (ready == SOCKET_ERROR) return -1;

        completed = nc_poller_iocp_dequeue(poller, out, max_events, 0);
        if (completed != 0) return completed;
        if (ready > 0) {
            int count = 0;
            for (int i = 0; i < poller->npfds && count < max_events; i++) {
                short revents = poller->pfds[i].revents;
                if (!revents) continue;
                out[count].fd = poller->pfds[i].fd;
                out[count].data = poller->pdata[i];
                out[count].operation = NULL;
                out[count].transferred = 0;
                out[count].error = 0;
                out[count].events = 0;
                if (revents & (POLLRDNORM | POLLIN | POLLHUP))
                    out[count].events |= NC_EV_READ;
                if (revents & (POLLWRNORM | POLLOUT))
                    out[count].events |= NC_EV_WRITE;
                if (revents & (POLLERR | POLLNVAL))
                    out[count].events |= NC_EV_ERROR;
                count++;
            }
            return count;
        }
        if (timeout_ms >= 0 && nc_monotonic_ms() >= deadline) return 0;
    }
#else
    int ready;
    do {
        ready = poll(poller->pfds, (nfds_t)poller->npfds, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready <= 0) return ready;
    int count = 0;
    for (int i = 0; i < poller->npfds && count < max_events; i++) {
        if (!poller->pfds[i].revents) continue;
        out[count].fd = poller->pfds[i].fd;
        out[count].data = poller->pdata[i];
        out[count].operation = NULL;
        out[count].transferred = 0;
        out[count].error = 0;
        out[count].events = 0;
        /* POLLHUP after a clean peer close is readable EOF, not a hard
         * error. Mapping it to NC_EV_ERROR made NC_FORCE_POLL treat a
         * drained pipe shutdown as failure (kqueue maps EV_EOF on read
         * to NC_EV_READ only). */
        if (poller->pfds[i].revents & (POLLIN | POLLHUP))
            out[count].events |= NC_EV_READ;
        if (poller->pfds[i].revents & POLLOUT)
            out[count].events |= NC_EV_WRITE;
        if (poller->pfds[i].revents & (POLLERR | POLLNVAL))
            out[count].events |= NC_EV_ERROR;
        count++;
    }
    return count;
#endif
}

#endif /* NEVERC_NET_POLLER_H */
