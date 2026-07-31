#ifndef NEVERC_NET_POLLER_H
#define NEVERC_NET_POLLER_H

#include "_net_io_uring.h"

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
#elif defined(NC_USE_IOCP)
    HANDLE iocp;
#else
    struct pollfd *pfds;
    void **pdata;
    int npfds;
    int cap_pfds;
#endif
} nc_poller_t;

static inline int nc_poller_supports_readiness(void) {
#if defined(NC_USE_IOCP)
    return 0;
#else
    return 1;
#endif
}

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
#elif defined(NC_USE_IOCP)
    poller->iocp =
        CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!poller->iocp) {
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
#elif defined(NC_USE_IOCP)
    CloseHandle(poller->iocp);
#else
    free(poller->pfds);
    free(poller->pdata);
#endif
    free(poller);
}

#if !defined(NC_USE_IO_URING) && !defined(NC_USE_EPOLL) && \
    !defined(NC_USE_KQUEUE) && !defined(NC_USE_IOCP)
static inline int nc_poller_reserve(nc_poller_t *poller, int capacity) {
    if (capacity <= poller->cap_pfds) return 0;
    struct pollfd *pfds = (struct pollfd *)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(struct pollfd));
    void **pdata =
        (void **)NC_POLLER_CALLOC((size_t)capacity, sizeof(void *));
    if (!pfds || !pdata) {
        free(pfds);
        free(pdata);
        return -1;
    }
    if (poller->npfds > 0) {
        memcpy(pfds, poller->pfds,
               (size_t)poller->npfds * sizeof(struct pollfd));
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

#if defined(NC_USE_EPOLL)
static inline int nc_poller_epoll_reserve(nc_poller_t *poller, int fd) {
    if (fd < poller->fd_cap) return 0;
    int capacity = poller->fd_cap;
    while (capacity <= fd) {
        if (capacity > INT_MAX / 2) return -1;
        capacity *= 2;
    }
    void **fd_data = (void **)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(void *));
    int *fd_events = (int *)NC_POLLER_CALLOC(
        (size_t)capacity, sizeof(int));
    if (!fd_data || !fd_events) {
        free(fd_data);
        free(fd_events);
        return -1;
    }
    memcpy(fd_data, poller->fd_data,
           (size_t)poller->fd_cap * sizeof(void *));
    memcpy(fd_events, poller->fd_events,
           (size_t)poller->fd_cap * sizeof(int));
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
    if (fd < 0 || nc_poller_epoll_reserve(poller, fd) != 0 ||
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
    struct kevent changes[2];
    int count = 0;
    if (events & NC_EV_READ) {
        EV_SET(&changes[count++], (uintptr_t)fd, EVFILT_READ,
               EV_ADD | EV_CLEAR, 0, 0, data);
    }
    if (events & NC_EV_WRITE) {
        EV_SET(&changes[count++], (uintptr_t)fd, EVFILT_WRITE,
               EV_ADD | EV_CLEAR, 0, 0, data);
    }
    return kevent(poller->kqfd, changes, count, NULL, 0, NULL);
#elif defined(NC_USE_IOCP)
    (void)events;
    HANDLE handle =
        CreateIoCompletionPort((HANDLE)fd, poller->iocp,
                               (ULONG_PTR)data, 0);
    return handle ? 0 : -1;
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
    int result = nc_poller_kqueue_change(
        poller, fd, EVFILT_READ,
        (events & NC_EV_READ) ? (EV_ADD | EV_CLEAR) : EV_DELETE,
        data, !(events & NC_EV_READ));
    if (result != 0) return result;
    return nc_poller_kqueue_change(
        poller, fd, EVFILT_WRITE,
        (events & NC_EV_WRITE) ? (EV_ADD | EV_CLEAR) : EV_DELETE,
        data, !(events & NC_EV_WRITE));
#elif defined(NC_USE_IOCP)
    (void)fd;
    (void)events;
    (void)data;
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
    poller->fd_data[fd] = NULL;
    poller->fd_events[fd] = 0;
    struct io_uring_sqe *sqe = nc_uring_get_sqe(&poller->ring);
    if (!sqe) {
        if (nc_uring_submit(&poller->ring) < 0) return -1;
        sqe = nc_uring_get_sqe(&poller->ring);
        if (!sqe) return -1;
    }
    nc_uring_prep_poll_remove(
        sqe, nc_poller_uring_token(poller, fd));
    nc_uring_sq_advance(&poller->ring, 1);
    return nc_uring_submit(&poller->ring) < 0 ? -1 : 0;
#elif defined(NC_USE_EPOLL)
    if (fd < 0 || fd >= poller->fd_cap ||
        poller->fd_events[fd] == 0)
        return -1;
    int result = epoll_ctl(poller->epfd, EPOLL_CTL_DEL, fd, NULL);
    if (result == 0) {
        poller->fd_data[fd] = NULL;
        poller->fd_events[fd] = 0;
    }
    return result;
#elif defined(NC_USE_KQUEUE)
    int read_result = nc_poller_kqueue_change(
        poller, fd, EVFILT_READ, EV_DELETE, NULL, 1);
    int write_result = nc_poller_kqueue_change(
        poller, fd, EVFILT_WRITE, EV_DELETE, NULL, 1);
    return read_result != 0 ? read_result : write_result;
#elif defined(NC_USE_IOCP)
    (void)fd;
    return 0;
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

static inline int nc_poller_wait(nc_poller_t *poller, nc_event_t *out,
                                 int max_events, int timeout_ms) {
    if (!poller || !out || max_events <= 0 || timeout_ms < -1)
        return -1;
    if (max_events > NC_POLLER_MAX_BATCH)
        max_events = NC_POLLER_MAX_BATCH;

#if defined(NC_USE_IO_URING) && NC_USE_IO_URING && defined(__linux__)
    if (!nc_uring_peek_cqe(&poller->ring)) {
        if (nc_uring_submit(&poller->ring) < 0) return -1;
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
        out[count].events = 0;
        if (result < 0) {
            out[count].events = NC_EV_ERROR;
        } else {
            if (result & POLLIN) out[count].events |= NC_EV_READ;
            if (result & POLLOUT) out[count].events |= NC_EV_WRITE;
            if (result & (POLLERR | POLLHUP | POLLNVAL))
                out[count].events |= NC_EV_ERROR;
        }
        count++;

        if (result >= 0 && poller->fd_events[fd] != 0 &&
            rearm_count < NC_POLLER_MAX_BATCH) {
            unsigned mask = 0;
            if (poller->fd_events[fd] & NC_EV_READ) mask |= POLLIN;
            if (poller->fd_events[fd] & NC_EV_WRITE) mask |= POLLOUT;
            rearms[rearm_count].fd = fd;
            rearms[rearm_count++].poll_mask = mask;
        }
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
    return count;
#elif defined(NC_USE_EPOLL)
    struct epoll_event events[NC_POLLER_MAX_BATCH];
    int count =
        epoll_wait(poller->epfd, events, max_events, timeout_ms);
    if (count <= 0) return count;
    for (int i = 0; i < count; i++) {
        int fd = events[i].data.fd;
        out[i].fd = fd;
        out[i].data =
            fd >= 0 && fd < poller->fd_cap ? poller->fd_data[fd] : NULL;
        out[i].events = 0;
        if (events[i].events & EPOLLIN) out[i].events |= NC_EV_READ;
        if (events[i].events & EPOLLOUT) out[i].events |= NC_EV_WRITE;
        if (events[i].events & (EPOLLERR | EPOLLHUP))
            out[i].events |= NC_EV_ERROR;
    }
    return count;
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
    for (int i = 0; i < count; i++) {
        out[i].data = events[i].udata;
        out[i].fd = (nc_sock_t)events[i].ident;
        out[i].events = 0;
        if (events[i].filter == EVFILT_READ)
            out[i].events |= NC_EV_READ;
        if (events[i].filter == EVFILT_WRITE)
            out[i].events |= NC_EV_WRITE;
        if (events[i].flags & (EV_ERROR | EV_EOF))
            out[i].events |= NC_EV_ERROR;
    }
    return count;
#elif defined(NC_USE_IOCP)
    OVERLAPPED_ENTRY entries[NC_POLLER_MAX_BATCH];
    ULONG removed = 0;
    DWORD timeout =
        timeout_ms >= 0 ? (DWORD)timeout_ms : INFINITE;
    BOOL ok = GetQueuedCompletionStatusEx(
        poller->iocp, entries, (ULONG)max_events, &removed, timeout, FALSE);
    if (!ok) {
        DWORD error = GetLastError();
        return error == WAIT_TIMEOUT ? 0 : -1;
    }
    for (ULONG i = 0; i < removed; i++) {
        out[i].data = (void *)entries[i].lpCompletionKey;
        out[i].fd = NC_INVALID_SOCK;
        out[i].events =
            entries[i].lpOverlapped ? (NC_EV_READ | NC_EV_WRITE) : 0;
    }
    return (int)removed;
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
        out[count].events = 0;
        if (poller->pfds[i].revents & POLLIN)
            out[count].events |= NC_EV_READ;
        if (poller->pfds[i].revents & POLLOUT)
            out[count].events |= NC_EV_WRITE;
        if (poller->pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
            out[count].events |= NC_EV_ERROR;
        count++;
    }
    return count;
#endif
}

#endif /* NEVERC_NET_POLLER_H */
