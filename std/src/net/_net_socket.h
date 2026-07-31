#ifndef NEVERC_NET_SOCKET_H
#define NEVERC_NET_SOCKET_H

#include "_net_platform.h"

/* Process-level socket initialization. */
static volatile int nc_net_initialized;

static inline int nc_net_init(void) {
#ifdef _WIN32
    int state = nc_atomic_load(&nc_net_initialized);
    if (state != 0) return state > 0 ? 0 : -1;
    static volatile LONG nc_init_lock;
    while (InterlockedCompareExchange(&nc_init_lock, 1, 0) != 0) {
        Sleep(0);
    }
    state = nc_atomic_load(&nc_net_initialized);
    if (state == 0) {
        WSADATA wsa;
        state = WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 1 : -1;
        nc_atomic_store(&nc_net_initialized, state);
    }
    InterlockedExchange(&nc_init_lock, 0);
    return state > 0 ? 0 : -1;
#else
    int state = nc_atomic_load(&nc_net_initialized);
    if (state != 0) return state > 0 ? 0 : -1;
    static volatile int nc_init_lock;
    while (!__sync_bool_compare_and_swap(&nc_init_lock, 0, 1)) {
    }
    state = nc_atomic_load(&nc_net_initialized);
    if (state == 0) {
        state = signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 1;
        nc_atomic_store(&nc_net_initialized, state);
    }
    __sync_lock_release(&nc_init_lock);
    return state > 0 ? 0 : -1;
#endif
}

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

#ifndef _WIN32
static inline int nc_set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}
#endif

static inline int nc_set_reuseaddr(nc_sock_t fd) {
    int opt = 1;
#ifdef _WIN32
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
                      sizeof(opt));
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
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt,
                      sizeof(opt));
#else
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif
}

static inline int nc_set_keepalive(nc_sock_t fd) {
    int opt = 1;
#ifdef _WIN32
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt,
                      sizeof(opt));
#else
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
#endif
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static inline int nc_parse_port(const char *text, uint16_t *port) {
    if (!text || !text[0] || !port) return -1;
    unsigned value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10U + (unsigned)(*p - '0');
        if (value > UINT16_MAX) return -1;
    }
    *port = (uint16_t)value;
    return 0;
}

/* Parse host:port and [IPv6]:port spellings used by network APIs. */
static inline int nc_parse_addr(const char *addr, char *host, size_t hostlen,
                                uint16_t *port) {
    if (!addr || !addr[0] || !host || hostlen == 0 || !port)
        return -1;

    if (addr[0] == ':') {
        host[0] = '\0';
        return nc_parse_port(addr + 1, port);
    }

    if (addr[0] == '[') {
        const char *end = strchr(addr, ']');
        if (!end || end[1] != ':') return -1;
        size_t hlen = (size_t)(end - addr - 1);
        if (hlen == 0 || hlen >= hostlen) return -1;
        memcpy(host, addr + 1, hlen);
        host[hlen] = '\0';
        return nc_parse_port(end + 2, port);
    }

    const char *colon = NULL;
    for (const char *p = addr; *p; p++)
        if (*p == ':') {
            if (colon) return -1;
            colon = p;
        }
    if (!colon) return -1;

    size_t hlen = (size_t)(colon - addr);
    if (hlen >= hostlen) return -1;
    memcpy(host, addr, hlen);
    host[hlen] = '\0';
    return nc_parse_port(colon + 1, port);
}

/* Zero-copy file-to-socket transfer where the platform supports it. */
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
    return rc == 0 ? (ssize_t)sbytes
                   : (sbytes > 0 ? (ssize_t)sbytes : -1);
#endif
}
#define NC_HAS_SENDFILE 1
#else
#define NC_HAS_SENDFILE 0
#endif

#if defined(__linux__) && defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
extern int accept4(int, struct sockaddr *, socklen_t *, int);
#endif

static inline nc_sock_t nc_accept_nonblock(nc_sock_t listen_fd,
                                            struct sockaddr *addr,
                                            socklen_t *addrlen) {
#if defined(__linux__) && defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    nc_sock_t fd =
        accept4(listen_fd, addr, addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd != NC_INVALID_SOCK ||
        (errno != ENOSYS && errno != EINVAL))
        return fd;
#else
    nc_sock_t fd;
#endif
    fd = accept(listen_fd, addr, addrlen);
    if (fd != NC_INVALID_SOCK) {
        int setup_failed = nc_set_nonblocking(fd) != 0;
#ifndef _WIN32
        setup_failed = setup_failed || nc_set_cloexec(fd) != 0;
#endif
        if (setup_failed) {
            nc_sock_close(fd);
            return NC_INVALID_SOCK;
        }
    }
    return fd;
}

static inline int nc_set_cork(nc_sock_t fd, int enable) {
#if defined(__linux__) || defined(__ANDROID__)
    return setsockopt(fd, IPPROTO_TCP, TCP_CORK, &enable, sizeof(enable));
#elif defined(TCP_NOPUSH)
    return setsockopt(fd, IPPROTO_TCP, TCP_NOPUSH, &enable, sizeof(enable));
#else
    (void)fd;
    (void)enable;
    return 0;
#endif
}

#ifdef _WIN32
static inline int nc_writev(nc_sock_t fd, const void *hdr, size_t hdr_len,
                            const void *body, size_t body_len) {
    if ((hdr_len > 0 && !hdr) || (body_len > 0 && !body) ||
        body_len > SIZE_MAX - hdr_len ||
        hdr_len + body_len > INT_MAX)
        return -1;
    size_t total = hdr_len + body_len;
    size_t sent = 0;
    while (sent < total) {
        DWORD n = 0;
        WSABUF bufs[2];
        int count = 0;
        size_t skip = sent;
        if (hdr && hdr_len > 0) {
            if (skip < hdr_len) {
                bufs[count].buf = (char *)hdr + skip;
                bufs[count].len = (ULONG)(hdr_len - skip);
                count++;
                skip = 0;
            } else {
                skip -= hdr_len;
            }
        }
        if (body && body_len > 0 && count < 2 && skip < body_len) {
            bufs[count].buf = (char *)body + skip;
            bufs[count].len = (ULONG)(body_len - skip);
            count++;
        }
        if (count == 0) break;
        if (WSASend(fd, bufs, count, &n, 0, NULL, NULL) != 0)
            return -1;
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}
#else
#include <sys/uio.h>
static inline int nc_writev(nc_sock_t fd, const void *hdr, size_t hdr_len,
                            const void *body, size_t body_len) {
    if ((hdr_len > 0 && !hdr) || (body_len > 0 && !body) ||
        body_len > SIZE_MAX - hdr_len ||
        hdr_len + body_len > INT_MAX)
        return -1;
    size_t total = hdr_len + body_len;
    size_t sent = 0;
    while (sent < total) {
        struct iovec iov[2];
        int count = 0;
        size_t skip = sent;
        if (hdr && hdr_len > 0) {
            if (skip < hdr_len) {
                iov[count].iov_base = (char *)hdr + skip;
                iov[count].iov_len = hdr_len - skip;
                count++;
                skip = 0;
            } else {
                skip -= hdr_len;
            }
        }
        if (body && body_len > 0 && count < 2 && skip < body_len) {
            iov[count].iov_base = (char *)body + skip;
            iov[count].iov_len = body_len - skip;
            count++;
        }
        if (count == 0) break;
        ssize_t n = writev(fd, iov, count);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}
#endif

static inline int nc_set_defer_accept(nc_sock_t fd) {
#if defined(__linux__) && defined(TCP_DEFER_ACCEPT)
    int timeout = 10;
    return setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &timeout,
                      sizeof(timeout));
#else
    (void)fd;
    return 0;
#endif
}

static inline int nc_set_quickack(nc_sock_t fd) {
#if defined(__linux__) && defined(TCP_QUICKACK)
    int opt = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &opt, sizeof(opt));
#else
    (void)fd;
    return 0;
#endif
}

#define NC_REUSEPORT_MAX 64

typedef struct {
    nc_sock_t fds[NC_REUSEPORT_MAX];
    int count;
    int max;
} nc_reuseport_group_t;

static inline int nc_reuseport_init(nc_reuseport_group_t *group, int max) {
    if (!group || max <= 0) return -1;
    memset(group, 0, sizeof(*group));
    group->max = max > NC_REUSEPORT_MAX ? NC_REUSEPORT_MAX : max;
    return 0;
}

static inline int nc_reuseport_listen(nc_reuseport_group_t *group,
                                      const char *host, uint16_t port,
                                      int backlog) {
    if (!group || group->count >= group->max) return -1;

    nc_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == NC_INVALID_SOCK) return -1;

    if (nc_set_reuseaddr(fd) != 0) {
        nc_sock_close(fd);
        return -1;
    }
#if !defined(_WIN32) && defined(SO_REUSEPORT)
    if (nc_set_reuseport(fd) != 0) {
        nc_sock_close(fd);
        return -1;
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host && host[0]) {
        if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
            nc_sock_close(fd);
            return -1;
        }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, backlog) != 0) {
        nc_sock_close(fd);
        return -1;
    }

    group->fds[group->count++] = fd;
    return 0;
}

static inline void nc_reuseport_close(nc_reuseport_group_t *group) {
    if (!group) return;
    for (int i = 0; i < group->count; i++)
        nc_sock_close(group->fds[i]);
    group->count = 0;
}

#endif /* NEVERC_NET_SOCKET_H */
