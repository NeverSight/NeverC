#include "neverc/std/net/tcp.h"
#include "../_net_internal.h"
#include <stdarg.h>
#include <stdint.h>

#ifndef _WIN32
#include <poll.h>
#endif

#ifndef NC_TCP_CALLOC
#define NC_TCP_CALLOC calloc
#endif
#ifndef NC_TCP_MALLOC
#define NC_TCP_MALLOC malloc
#endif

/* ======================================================================
 * Internal structures
 * ====================================================================== */

struct neverc_tcp_listener {
    nc_sock_t fd;
    struct sockaddr_storage addr;
    socklen_t addrlen;
};

struct neverc_tcp_conn {
    nc_sock_t fd;
    struct sockaddr_storage remote;
    socklen_t remote_len;
    struct sockaddr_storage local;
    socklen_t local_len;
    uint8_t *preload;
    size_t preload_len;
    size_t preload_pos;
    int64_t read_deadline_ms;
    int64_t write_deadline_ms;
    int read_timeout_ms;
    int write_timeout_ms;
    volatile int nonblocking;
    volatile int read_shutdown;
    volatile int write_shutdown;
};

/* ======================================================================
 * Helpers
 * ====================================================================== */

static void addr_to_string(const struct sockaddr *sa, socklen_t salen,
                            neverc_tcp_addr_t *out) {
    (void)salen;
    memset(out, 0, sizeof(*out));
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &in->sin_addr, out->addr, sizeof(out->addr));
        out->port = ntohs(in->sin_port);
        size_t len = strlen(out->addr);
        snprintf(out->addr + len, sizeof(out->addr) - len, ":%d", out->port);
    } else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)sa;
        /* Dual-stack accept() reports IPv4 peers as ::ffff:a.b.c.d.
         * Format them as IPv4 so ACLs matching "127.0.0.1" still work. */
        if (nc_in6_is_addr_v4mapped(&in6->sin6_addr)) {
            inet_ntop(AF_INET, in6->sin6_addr.s6_addr + 12, out->addr,
                      sizeof(out->addr));
            out->port = ntohs(in6->sin6_port);
            size_t len = strlen(out->addr);
            snprintf(out->addr + len, sizeof(out->addr) - len, ":%d",
                     out->port);
            return;
        }
        out->addr[0] = '[';
        inet_ntop(AF_INET6, &in6->sin6_addr, out->addr + 1,
                  sizeof(out->addr) - 2);
        size_t len = strlen(out->addr);
        if (in6->sin6_scope_id)
            snprintf(out->addr + len, sizeof(out->addr) - len, "%%%u]:%d",
                     (unsigned)in6->sin6_scope_id, ntohs(in6->sin6_port));
        else
            snprintf(out->addr + len, sizeof(out->addr) - len, "]:%d",
                     ntohs(in6->sin6_port));
        out->port = ntohs(in6->sin6_port);
    }
}

static neverc_net_result_t tcp_result(neverc_net_status_t status,
                                      int system_code,
                                      const char *operation,
                                      size_t transferred);
static int64_t tcp_realtime_ms(void);
static int tcp_deadline_expired(int64_t deadline_ms);
static int tcp_deadline_timeout(int64_t deadline_ms);
static int tcp_refresh_read_deadline(neverc_tcp_conn_t *conn);
static int tcp_refresh_write_deadline(neverc_tcp_conn_t *conn);

static int tcp_timeout_error(void) {
#ifdef _WIN32
    return WSAETIMEDOUT;
#else
    return ETIMEDOUT;
#endif
}

static int tcp_accept_would_block(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static int tcp_listener_wait(nc_sock_t fd) {
#ifdef _WIN32
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    int rc;
    do {
        rc = select(0, &readfds, NULL, NULL, NULL);
    } while (rc == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
    return rc == SOCKET_ERROR ? -1 : rc;
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, -1);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0 || (rc > 0 && (pfd.revents & POLLNVAL) != 0))
        return -1;
    return rc;
#endif
}

static neverc_tcp_conn_t *tcp_conn_from_accepted(
    nc_sock_t fd, const struct sockaddr_storage *remote,
    socklen_t remote_len, int nonblocking) {
    neverc_tcp_conn_t *conn = (neverc_tcp_conn_t *)NC_TCP_CALLOC(1, sizeof(*conn));
    if (!conn) return NULL;
    conn->fd = fd;
    conn->nonblocking = nonblocking;
    /* accept() may report a truncated address by returning addrlen larger
     * than the buffer we passed; copying that size overflows conn. */
    if (remote && remote_len > 0) {
        if ((size_t)remote_len > sizeof(conn->remote))
            remote_len = (socklen_t)sizeof(conn->remote);
        conn->remote_len = remote_len;
        memcpy(&conn->remote, remote, (size_t)remote_len);
    }
    conn->local_len = sizeof(conn->local);
    if (getsockname(fd, (struct sockaddr *)&conn->local,
                    &conn->local_len) != 0 ||
        (size_t)conn->local_len > sizeof(conn->local)) {
        memset(&conn->local, 0, sizeof(conn->local));
        conn->local_len = 0;
    }
    return conn;
}

/* ======================================================================
 * Listen / Accept
 * ====================================================================== */

neverc_tcp_listener_t *neverc_tcp_listen(const char *addr, const char **errp) {
    if (nc_net_init() != 0) {
        if (errp) *errp = "network initialization failed";
        return NULL;
    }

    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0) {
        if (errp) *errp = "invalid address format";
        return NULL;
    }

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int rc = getaddrinfo(host[0] ? host : NULL, portstr, &hints, &result);
    if (rc != 0) {
        if (errp) *errp = "getaddrinfo failed";
        return NULL;
    }

    nc_sock_t fd = NC_INVALID_SOCK;
    struct sockaddr_storage bound_addr;
    socklen_t bound_len = 0;
    for (rp = result; rp; rp = rp->ai_next) {
        if (rp->ai_addrlen > sizeof(bound_addr))
            continue;
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == NC_INVALID_SOCK)
            continue;

        (void)nc_set_reuseaddr(fd);
        if (rp->ai_family == AF_INET6) {
            const struct sockaddr_in6 *a6 =
                (const struct sockaddr_in6 *)rp->ai_addr;
            /* Unspecified :: (including ":port" and "[::]:port") is dual-stack. */
            if (nc_in6_is_addr_unspecified(&a6->sin6_addr)) {
                int v6only = 0;
#ifdef _WIN32
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                                 (const char *)&v6only, sizeof(v6only));
#else
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                                 &v6only, sizeof(v6only));
#endif
            }
        }

        if (bind(fd, rp->ai_addr, (int)rp->ai_addrlen) != NC_SOCK_ERR &&
            listen(fd, 512) != NC_SOCK_ERR &&
            nc_set_nonblocking(fd) == 0) {
            bound_len = (socklen_t)rp->ai_addrlen;
            memcpy(&bound_addr, rp->ai_addr, rp->ai_addrlen);
            break;
        }
        nc_sock_close(fd);
        fd = NC_INVALID_SOCK;
    }
    if (fd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        if (errp) *errp = "no address could be bound";
        return NULL;
    }

    neverc_tcp_listener_t *ln =
        (neverc_tcp_listener_t *)NC_TCP_CALLOC(1, sizeof(*ln));
    if (!ln) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    ln->fd = fd;
    ln->addrlen = bound_len;
    memcpy(&ln->addr, &bound_addr, bound_len);

    getsockname(fd, (struct sockaddr *)&ln->addr, &ln->addrlen);

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return ln;
}

neverc_tcp_conn_t *neverc_tcp_accept(neverc_tcp_listener_t *ln,
                                      const char **errp) {
    if (!ln) {
        if (errp) *errp = "nil listener";
        return NULL;
    }

    struct sockaddr_storage client_addr;
    socklen_t client_len;
    nc_sock_t cfd;
    for (;;) {
        client_len = sizeof(client_addr);
        cfd = accept(ln->fd, (struct sockaddr *)&client_addr, &client_len);
        if (cfd != NC_INVALID_SOCK)
            break;

        int error = nc_sock_errno;
#ifdef _WIN32
        if (error == WSAEINTR)
            continue;
#else
        if (error == EINTR)
            continue;
#endif
        if (!tcp_accept_would_block(error) ||
            tcp_listener_wait(ln->fd) < 0) {
            if (errp) *errp = "accept failed";
            return NULL;
        }
    }
    if (nc_set_blocking(cfd) != 0) {
        nc_sock_close(cfd);
        if (errp) *errp = "failed to configure accepted socket";
        return NULL;
    }

    neverc_tcp_conn_t *conn =
        tcp_conn_from_accepted(cfd, &client_addr, client_len, 0);
    if (!conn) {
        nc_sock_close(cfd);
        if (errp) *errp = "out of memory";
        return NULL;
    }

    if (errp) *errp = NULL;
    return conn;
}

neverc_net_result_t neverc_tcp_try_accept(neverc_tcp_listener_t *ln,
                                           neverc_tcp_conn_t **conn_out) {
    if (conn_out) *conn_out = NULL;
    if (!ln || !conn_out)
        return tcp_result(NEVERC_NET_INVALID, 0, "accept", 0);

    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    nc_sock_t cfd = nc_accept_nonblock(
        ln->fd, (struct sockaddr *)&client_addr, &client_len);
    if (cfd == NC_INVALID_SOCK) {
        int error = nc_sock_errno;
        if (tcp_accept_would_block(error))
            return tcp_result(NEVERC_NET_WOULD_BLOCK, error,
                              "accept", 0);
        return tcp_result(NEVERC_NET_SYSTEM, error, "accept", 0);
    }
    neverc_tcp_conn_t *conn =
        tcp_conn_from_accepted(cfd, &client_addr, client_len, 1);
    if (!conn) {
        nc_sock_close(cfd);
        return tcp_result(NEVERC_NET_NOMEM, 0, "accept", 0);
    }
    *conn_out = conn;
    return tcp_result(NEVERC_NET_OK, 0, "accept", 0);
}

void neverc_tcp_listener_close(neverc_tcp_listener_t *ln) {
    if (!ln) return;
    nc_sock_close(ln->fd);
    free(ln);
}

int neverc_tcp_listener_addr(neverc_tcp_listener_t *ln,
                              neverc_tcp_addr_t *addr) {
    if (!ln || !addr) return -1;
    addr_to_string((struct sockaddr *)&ln->addr, ln->addrlen, addr);
    return 0;
}

uintptr_t neverc_tcp_listener_handle(neverc_tcp_listener_t *ln) {
    return ln ? (uintptr_t)ln->fd : NEVERC_NET_INVALID_HANDLE;
}

/* ======================================================================
 * Dial
 * ====================================================================== */

neverc_tcp_conn_t *neverc_tcp_dial(const char *addr, const char **errp) {
    if (nc_net_init() != 0) {
        if (errp) *errp = "network initialization failed";
        return NULL;
    }

    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0 ||
        host[0] == '\0') {
        if (errp) *errp = "invalid address format";
        return NULL;
    }

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int rc = getaddrinfo(host, portstr, &hints, &result);
    if (rc != 0) {
        if (errp) *errp = "getaddrinfo failed";
        return NULL;
    }

    nc_sock_t fd = NC_INVALID_SOCK;
    struct sockaddr_storage remote;
    socklen_t remote_len = 0;
    for (rp = result; rp; rp = rp->ai_next) {
        if (rp->ai_addrlen > sizeof(remote))
            continue;
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == NC_INVALID_SOCK)
            continue;
        if (connect(fd, rp->ai_addr, (int)rp->ai_addrlen) != NC_SOCK_ERR) {
            remote_len = (socklen_t)rp->ai_addrlen;
            memcpy(&remote, rp->ai_addr, rp->ai_addrlen);
            break;
        }
        nc_sock_close(fd);
        fd = NC_INVALID_SOCK;
    }
    if (fd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        if (errp) *errp = "all connection attempts failed";
        return NULL;
    }

    neverc_tcp_conn_t *conn = (neverc_tcp_conn_t *)NC_TCP_CALLOC(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = fd;
    conn->remote_len = remote_len;
    if ((size_t)conn->remote_len > sizeof(conn->remote))
        conn->remote_len = (socklen_t)sizeof(conn->remote);
    memcpy(&conn->remote, &remote, conn->remote_len);
    conn->local_len = sizeof(conn->local);
    if (getsockname(fd, (struct sockaddr *)&conn->local,
                    &conn->local_len) != 0 ||
        (size_t)conn->local_len > sizeof(conn->local)) {
        memset(&conn->local, 0, sizeof(conn->local));
        conn->local_len = 0;
    }

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return conn;
}

neverc_tcp_conn_t *neverc_tcp_adopt(int fd, const void *preload,
                                     size_t preload_len, const char **errp) {
    if (fd < 0) {
        if (errp) *errp = "invalid fd";
        return NULL;
    }
    return neverc_tcp_adopt_handle((uintptr_t)(unsigned int)fd, preload,
                                    preload_len, errp);
}

neverc_tcp_conn_t *neverc_tcp_adopt_handle(uintptr_t socket_handle,
                                            const void *preload,
                                            size_t preload_len,
                                            const char **errp) {
    nc_sock_t fd = (nc_sock_t)socket_handle;
#ifdef _WIN32
    if (fd == NC_INVALID_SOCK) {
#else
    if (socket_handle > (uintptr_t)INT_MAX || fd < 0) {
#endif
        if (errp) *errp = "invalid socket handle";
        return NULL;
    }
    if (!preload && preload_len > 0) {
        if (errp) *errp = "missing preload data";
        return NULL;
    }

    neverc_tcp_conn_t *conn = (neverc_tcp_conn_t *)NC_TCP_CALLOC(1, sizeof(*conn));
    if (!conn) {
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = fd;
    conn->remote_len = sizeof(conn->remote);
    if (getpeername(conn->fd, (struct sockaddr *)&conn->remote,
                    &conn->remote_len) != 0 ||
        (size_t)conn->remote_len > sizeof(conn->remote)) {
        memset(&conn->remote, 0, sizeof(conn->remote));
        conn->remote_len = 0;
    }
    conn->local_len = sizeof(conn->local);
    if (getsockname(conn->fd, (struct sockaddr *)&conn->local,
                    &conn->local_len) != 0 ||
        (size_t)conn->local_len > sizeof(conn->local)) {
        memset(&conn->local, 0, sizeof(conn->local));
        conn->local_len = 0;
    }

    if (preload && preload_len > 0) {
        conn->preload = (uint8_t *)NC_TCP_MALLOC(preload_len);
        if (!conn->preload) {
            free(conn);
            if (errp) *errp = "out of memory";
            return NULL;
        }
        memcpy(conn->preload, preload, preload_len);
        conn->preload_len = preload_len;
    }

    if (nc_set_blocking(conn->fd) != 0) {
        free(conn->preload);
        free(conn);
        if (errp) *errp = "failed to configure socket";
        return NULL;
    }

    if (errp) *errp = NULL;
    return conn;
}

/* ======================================================================
 * Connection I/O
 * ====================================================================== */

static neverc_net_result_t tcp_result(neverc_net_status_t status,
                                      int system_code,
                                      const char *operation,
                                      size_t transferred) {
    neverc_net_result_t result;
    result.status = status;
    result.system_code = system_code;
    result.operation = operation;
    result.transferred = transferred;
    return result;
}

static int tcp_error_would_block(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static int tcp_error_closed(int error) {
#ifdef _WIN32
    return error == WSAECONNABORTED || error == WSAECONNRESET ||
           error == WSAENETRESET || error == WSAESHUTDOWN ||
           error == WSAENOTCONN;
#else
    return error == ECONNABORTED || error == ECONNRESET ||
           error == EPIPE || error == ENOTCONN;
#endif
}

/* SO_RCVTIMEO / SO_SNDTIMEO on a blocking socket surfaces as EAGAIN
 * (POSIX) rather than ETIMEDOUT. Callers checking ETIMEDOUT would
 * treat a fired timeout as a generic I/O error. */
static int tcp_error_timeout_io(int error) {
#ifdef _WIN32
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
    return error == ETIMEDOUT || error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static void tcp_set_last_error(int error) {
#ifdef _WIN32
    WSASetLastError(error);
#else
    errno = error;
#endif
}

static void tcp_note_blocking_timeout(neverc_tcp_conn_t *conn, int write_side) {
    int armed = write_side
                    ? (conn->write_timeout_ms > 0 ||
                       conn->write_deadline_ms > 0)
                    : (conn->read_timeout_ms > 0 ||
                       conn->read_deadline_ms > 0);
    if (armed && tcp_error_timeout_io(nc_sock_errno))
        tcp_set_last_error(tcp_timeout_error());
}

static int tcp_ensure_nonblocking(neverc_tcp_conn_t *conn) {
    if (nc_atomic_load(&conn->nonblocking)) return 0;
    if (nc_set_nonblocking(conn->fd) != 0) return -1;
    nc_atomic_store(&conn->nonblocking, 1);
    return 0;
}

static int tcp_wait_io(nc_sock_t fd, int write_ready, int timeout_ms) {
#ifdef _WIN32
    for (;;) {
        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        if (write_ready)
            FD_SET(fd, &writefds);
        else
            FD_SET(fd, &readfds);
        struct timeval timeout;
        struct timeval *timeout_ptr = NULL;
        if (timeout_ms >= 0) {
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            timeout_ptr = &timeout;
        }
        int rc = select(0, write_ready ? NULL : &readfds,
                        write_ready ? &writefds : NULL, NULL, timeout_ptr);
        if (rc != SOCKET_ERROR || WSAGetLastError() != WSAEINTR)
            return rc == SOCKET_ERROR ? -1 : rc;
    }
#elif defined(NC_USE_KQUEUE)
    /* Darwin poll() can miss a TCP FIN. kqueue EVFILT_READ reports EV_EOF. */
    int kq = kqueue();
    if (kq < 0) return -1;
    struct kevent change;
    EV_SET(&change, (uintptr_t)fd,
           write_ready ? EVFILT_WRITE : EVFILT_READ,
           EV_ADD | EV_ONESHOT, 0, 0, NULL);
    struct timespec timeout;
    struct timespec *timeout_ptr = NULL;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }
    struct kevent event;
    int rc;
    do {
        rc = kevent(kq, &change, 1, &event, 1, timeout_ptr);
    } while (rc < 0 && errno == EINTR);
    int saved = errno;
    close(kq);
    if (rc < 0) {
        errno = saved;
        return -1;
    }
    if (rc == 0) return 0;
    if (event.flags & EV_ERROR) {
        errno = event.data ? (int)event.data : EIO;
        return -1;
    }
    return rc;
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = write_ready ? POLLOUT : POLLIN;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    if (rc > 0 && (pfd.revents & POLLNVAL) != 0) {
        errno = EBADF;
        return -1;
    }
    return rc;
#endif
}

static int tcp_wait_conn(neverc_tcp_conn_t *conn, int write_ready) {
    int64_t deadline_ms =
        write_ready ? conn->write_deadline_ms : conn->read_deadline_ms;
    int timeout_ms =
        write_ready ? conn->write_timeout_ms : conn->read_timeout_ms;
    if (deadline_ms > 0) {
        timeout_ms = tcp_deadline_timeout(deadline_ms);
        if (timeout_ms < 0) return -1;
    } else if (timeout_ms == 0) {
        timeout_ms = -1;
    }

    int ready = tcp_wait_io(conn->fd, write_ready, timeout_ms);
    if (ready == 0) {
        tcp_set_last_error(tcp_timeout_error());
        return -1;
    }
    return ready < 0 ? -1 : 0;
}

neverc_net_result_t neverc_tcp_try_read(neverc_tcp_conn_t *conn,
                                        void *buf, size_t buflen) {
    if (!conn || (!buf && buflen > 0))
        return tcp_result(NEVERC_NET_INVALID, 0, "read", 0);
    if (buflen == 0)
        return tcp_result(NEVERC_NET_OK, 0, "read", 0);

    if (conn->preload && conn->preload_pos < conn->preload_len) {
        size_t avail = conn->preload_len - conn->preload_pos;
        size_t n = buflen < avail ? buflen : avail;
        memcpy(buf, conn->preload + conn->preload_pos, n);
        conn->preload_pos += n;
        if (conn->preload_pos >= conn->preload_len) {
            free(conn->preload);
            conn->preload = NULL;
            conn->preload_len = 0;
            conn->preload_pos = 0;
        }
        return tcp_result(NEVERC_NET_OK, 0, "read", n);
    }
    if (nc_atomic_load(&conn->read_shutdown))
        return tcp_result(NEVERC_NET_EOF, 0, "read", 0);
    if (tcp_deadline_expired(conn->read_deadline_ms))
        return tcp_result(NEVERC_NET_TIMEOUT, tcp_timeout_error(),
                          "read", 0);
    if (tcp_ensure_nonblocking(conn) != 0)
        return tcp_result(NEVERC_NET_SYSTEM, nc_sock_errno, "read", 0);

#ifdef _WIN32
    if (buflen > (size_t)INT_MAX)
        buflen = INT_MAX;
    int n = recv(conn->fd, (char *)buf, (int)buflen, 0);
#else
    ssize_t n;
    do {
        n = recv(conn->fd, buf, buflen, MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR);
#endif
    if (n > 0)
        return tcp_result(NEVERC_NET_OK, 0, "read", (size_t)n);
    if (n == 0)
        return tcp_result(NEVERC_NET_EOF, 0, "read", 0);
    int error = nc_sock_errno;
    if (tcp_deadline_expired(conn->read_deadline_ms))
        return tcp_result(NEVERC_NET_TIMEOUT, tcp_timeout_error(),
                          "read", 0);
    if (tcp_error_would_block(error))
        return tcp_result(NEVERC_NET_WOULD_BLOCK, error, "read", 0);
    if (tcp_error_closed(error))
        return tcp_result(NEVERC_NET_CLOSED, error, "read", 0);
    return tcp_result(NEVERC_NET_SYSTEM, error, "read", 0);
}

neverc_net_result_t neverc_tcp_try_write(neverc_tcp_conn_t *conn,
                                         const void *data, size_t len) {
    if (!conn || (!data && len > 0))
        return tcp_result(NEVERC_NET_INVALID, 0, "write", 0);
    if (len == 0)
        return tcp_result(NEVERC_NET_OK, 0, "write", 0);
    if (nc_atomic_load(&conn->write_shutdown)) {
#ifdef _WIN32
        int error = WSAESHUTDOWN;
#else
        int error = EPIPE;
#endif
        return tcp_result(NEVERC_NET_CLOSED, error, "write", 0);
    }
    if (tcp_deadline_expired(conn->write_deadline_ms))
        return tcp_result(NEVERC_NET_TIMEOUT, tcp_timeout_error(),
                          "write", 0);
    if (tcp_ensure_nonblocking(conn) != 0)
        return tcp_result(NEVERC_NET_SYSTEM, nc_sock_errno, "write", 0);

#ifdef _WIN32
    if (len > (size_t)INT_MAX)
        len = INT_MAX;
    int n = send(conn->fd, (const char *)data, (int)len, 0);
#else
    ssize_t n;
    do {
        n = send(conn->fd, data, len, MSG_DONTWAIT | MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR);
#endif
    if (n >= 0)
        return tcp_result(NEVERC_NET_OK, 0, "write", (size_t)n);
    int error = nc_sock_errno;
    if (tcp_deadline_expired(conn->write_deadline_ms))
        return tcp_result(NEVERC_NET_TIMEOUT, tcp_timeout_error(),
                          "write", 0);
    if (tcp_error_would_block(error))
        return tcp_result(NEVERC_NET_WOULD_BLOCK, error, "write", 0);
    if (tcp_error_closed(error))
        return tcp_result(NEVERC_NET_CLOSED, error, "write", 0);
    return tcp_result(NEVERC_NET_SYSTEM, error, "write", 0);
}

int neverc_tcp_write(neverc_tcp_conn_t *conn, const void *data, size_t len) {
    if (!conn || (!data && len > 0) || len > (size_t)INT_MAX) return -1;
    if (len == 0) return 0;
    if (nc_atomic_load(&conn->write_shutdown)) {
#ifdef _WIN32
        WSASetLastError(WSAESHUTDOWN);
#else
        errno = EPIPE;
#endif
        return -1;
    }
    const char *p = (const char *)data;
    size_t total = 0;
    if (nc_atomic_load(&conn->nonblocking)) {
        while (total < len) {
            neverc_net_result_t result =
                neverc_tcp_try_write(conn, p + total, len - total);
            if (result.status == NEVERC_NET_OK &&
                result.transferred > 0) {
                total += result.transferred;
                continue;
            }
            if (result.status == NEVERC_NET_WOULD_BLOCK) {
                if (tcp_wait_conn(conn, 1) == 0)
                    continue;
            } else if (result.system_code != 0) {
                tcp_set_last_error(result.system_code);
            }
            return total > 0 ? (int)total : -1;
        }
        return (int)total;
    }
    while (total < len) {
        if (tcp_refresh_write_deadline(conn) != 0)
            return total > 0 ? (int)total : -1;
#ifdef _WIN32
        int n = send(conn->fd, p + total, (int)(len - total), 0);
#else
        ssize_t n = send(conn->fd, p + total, len - total, MSG_NOSIGNAL);
#endif
        if (n > 0) {
            total += (size_t)n;
            continue;
        }
        if (n == 0) break;
#ifdef _WIN32
        int e = WSAGetLastError();
        if (e == WSAEINTR) continue;
#else
        if (errno == EINTR) continue;
#endif
        tcp_note_blocking_timeout(conn, 1);
        if (total > 0) return (int)total;
        return -1;
    }
    return (int)total;
}

int neverc_tcp_read(neverc_tcp_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || (!buf && buflen > 0) ||
        buflen > (size_t)INT_MAX)
        return -1;
    if (buflen == 0) return 0;
    if (nc_atomic_load(&conn->nonblocking)) {
        for (;;) {
            neverc_net_result_t result =
                neverc_tcp_try_read(conn, buf, buflen);
            if (result.status == NEVERC_NET_OK)
                return (int)result.transferred;
            if (result.status == NEVERC_NET_EOF)
                return 0;
            if (result.status == NEVERC_NET_WOULD_BLOCK) {
                if (tcp_wait_conn(conn, 0) == 0)
                    continue;
            } else if (result.system_code != 0) {
                tcp_set_last_error(result.system_code);
            }
            return -1;
        }
    }
    if (conn->preload && conn->preload_pos < conn->preload_len) {
        size_t avail = conn->preload_len - conn->preload_pos;
        size_t n = buflen < avail ? buflen : avail;
        memcpy(buf, conn->preload + conn->preload_pos, n);
        conn->preload_pos += n;
        if (conn->preload_pos >= conn->preload_len) {
            free(conn->preload);
            conn->preload = NULL;
            conn->preload_len = 0;
            conn->preload_pos = 0;
        }
        return (int)n;
    }
    if (nc_atomic_load(&conn->read_shutdown))
        return 0;
    for (;;) {
        if (tcp_refresh_read_deadline(conn) != 0) return -1;
#ifdef _WIN32
        int n = recv(conn->fd, (char *)buf, (int)buflen, 0);
        if (n >= 0) return n;
        if (WSAGetLastError() == WSAEINTR) continue;
        tcp_note_blocking_timeout(conn, 0);
        return -1;
#else
        ssize_t n = recv(conn->fd, buf, buflen, 0);
        if (n >= 0) return (int)n;
        if (errno == EINTR) continue;
        tcp_note_blocking_timeout(conn, 0);
        return -1;
#endif
    }
}

int neverc_tcp_shutdown_read(neverc_tcp_conn_t *conn) {
    if (!conn) return -1;
#ifdef _WIN32
    int rc = shutdown(conn->fd, SD_RECEIVE) == 0 ? 0 : -1;
#else
    int rc = shutdown(conn->fd, SHUT_RD) == 0 ? 0 : -1;
#endif
    if (rc == 0)
        nc_atomic_store(&conn->read_shutdown, 1);
    return rc;
}

int neverc_tcp_shutdown_write(neverc_tcp_conn_t *conn) {
    if (!conn) return -1;
#ifdef _WIN32
    int rc = shutdown(conn->fd, SD_SEND) == 0 ? 0 : -1;
#else
    int rc = shutdown(conn->fd, SHUT_WR) == 0 ? 0 : -1;
#endif
    if (rc == 0)
        nc_atomic_store(&conn->write_shutdown, 1);
    return rc;
}

int neverc_tcp_conn_fd(neverc_tcp_conn_t *conn) {
    uintptr_t handle = neverc_tcp_conn_handle(conn);
    return handle == NEVERC_NET_INVALID_HANDLE || handle > (uintptr_t)INT_MAX
               ? -1
               : (int)handle;
}

uintptr_t neverc_tcp_conn_handle(neverc_tcp_conn_t *conn) {
    return conn ? (uintptr_t)conn->fd : NEVERC_NET_INVALID_HANDLE;
}

void neverc_tcp_close(neverc_tcp_conn_t *conn) {
    if (!conn) return;
    free(conn->preload);
    nc_sock_close(conn->fd);
    free(conn);
}

int neverc_tcp_remote_addr(neverc_tcp_conn_t *conn, neverc_tcp_addr_t *addr) {
    if (!conn || !addr) return -1;
    addr_to_string((struct sockaddr *)&conn->remote, conn->remote_len, addr);
    return 0;
}

int neverc_tcp_local_addr(neverc_tcp_conn_t *conn, neverc_tcp_addr_t *addr) {
    if (!conn || !addr) return -1;
    addr_to_string((struct sockaddr *)&conn->local, conn->local_len, addr);
    return 0;
}

static int tcp_set_socket_timeout(nc_sock_t fd, int option, int ms) {
    if (ms < 0) return -1;
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    return setsockopt(fd, SOL_SOCKET, option, (const char *)&tv,
                      sizeof(tv)) == 0 ? 0 : -1;
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, option, &tv, sizeof(tv)) == 0 ? 0 : -1;
#endif
}

int neverc_tcp_set_read_timeout(neverc_tcp_conn_t *conn, int ms) {
    if (!conn) return -1;
    int rc = tcp_set_socket_timeout(conn->fd, SO_RCVTIMEO, ms);
    if (rc == 0) {
        conn->read_timeout_ms = ms;
        conn->read_deadline_ms = 0;
    }
    return rc;
}

int neverc_tcp_set_write_timeout(neverc_tcp_conn_t *conn, int ms) {
    if (!conn) return -1;
    int rc = tcp_set_socket_timeout(conn->fd, SO_SNDTIMEO, ms);
    if (rc == 0) {
        conn->write_timeout_ms = ms;
        conn->write_deadline_ms = 0;
    }
    return rc;
}

static int64_t tcp_realtime_ms(void) {
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

static int tcp_deadline_timeout(int64_t deadline_ms) {
    if (deadline_ms < 0) return -1;
    if (deadline_ms == 0) return 0;
    int64_t now = tcp_realtime_ms();
    if (now < 0) return -1;
    int64_t remaining = deadline_ms - now;
    if (remaining <= 0) return 1;
    return remaining > INT_MAX ? INT_MAX : (int)remaining;
}

static int tcp_deadline_expired(int64_t deadline_ms) {
    if (deadline_ms <= 0) return 0;
    int64_t now = tcp_realtime_ms();
    return now >= 0 && now >= deadline_ms;
}

static int tcp_refresh_deadline(neverc_tcp_conn_t *conn,
                                int64_t deadline_ms, int option) {
    if (deadline_ms == 0) return 0;
    if (tcp_deadline_expired(deadline_ms)) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        return -1;
    }
    int timeout = tcp_deadline_timeout(deadline_ms);
    return timeout <= 0
               ? -1
               : tcp_set_socket_timeout(conn->fd, option, timeout);
}

static int tcp_refresh_read_deadline(neverc_tcp_conn_t *conn) {
    return tcp_refresh_deadline(conn, conn->read_deadline_ms,
                                SO_RCVTIMEO);
}

static int tcp_refresh_write_deadline(neverc_tcp_conn_t *conn) {
    return tcp_refresh_deadline(conn, conn->write_deadline_ms,
                                SO_SNDTIMEO);
}

int neverc_tcp_set_read_deadline(neverc_tcp_conn_t *conn,
                                  int64_t deadline_ms) {
    if (!conn) return -1;
    int timeout = tcp_deadline_timeout(deadline_ms);
    if (timeout < 0 ||
        tcp_set_socket_timeout(conn->fd, SO_RCVTIMEO, timeout) != 0)
        return -1;
    conn->read_timeout_ms = 0;
    conn->read_deadline_ms = deadline_ms;
    return 0;
}

int neverc_tcp_set_write_deadline(neverc_tcp_conn_t *conn,
                                   int64_t deadline_ms) {
    if (!conn) return -1;
    int timeout = tcp_deadline_timeout(deadline_ms);
    if (timeout < 0 ||
        tcp_set_socket_timeout(conn->fd, SO_SNDTIMEO, timeout) != 0)
        return -1;
    conn->write_timeout_ms = 0;
    conn->write_deadline_ms = deadline_ms;
    return 0;
}

int neverc_tcp_set_timeout(neverc_tcp_conn_t *conn, int ms) {
    if (!conn || ms < 0) return -1;
    int read_rc = neverc_tcp_set_read_timeout(conn, ms);
    int write_rc = neverc_tcp_set_write_timeout(conn, ms);
    return read_rc == 0 && write_rc == 0 ? 0 : -1;
}

int neverc_tcp_set_nodelay(neverc_tcp_conn_t *conn, int enable) {
    if (!conn) return -1;
    int val = enable ? 1 : 0;
#ifdef _WIN32
    return setsockopt(conn->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&val,
                      sizeof(val));
#else
    return setsockopt(conn->fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
#endif
}

int neverc_tcp_set_reuseaddr(neverc_tcp_listener_t *ln, int enable) {
    if (!ln) return -1;
    int val = enable ? 1 : 0;
#ifdef _WIN32
    return setsockopt(ln->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val,
                      sizeof(val));
#else
    return setsockopt(ln->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
#endif
}

int neverc_tcp_set_keepalive(neverc_tcp_conn_t *conn, int enable) {
    if (!conn) return -1;
    int val = enable ? 1 : 0;
#ifdef _WIN32
    return setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&val,
                      sizeof(val));
#else
    return setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
#endif
}

int neverc_tcp_set_read_buffer(neverc_tcp_conn_t *conn, int bytes) {
    if (!conn || bytes <= 0) return -1;
#ifdef _WIN32
    return setsockopt(conn->fd, SOL_SOCKET, SO_RCVBUF, (const char *)&bytes,
                      sizeof(bytes));
#else
    return setsockopt(conn->fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
#endif
}

int neverc_tcp_set_write_buffer(neverc_tcp_conn_t *conn, int bytes) {
    if (!conn || bytes <= 0) return -1;
#ifdef _WIN32
    return setsockopt(conn->fd, SOL_SOCKET, SO_SNDBUF, (const char *)&bytes,
                      sizeof(bytes));
#else
    return setsockopt(conn->fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));
#endif
}

/* --- Pipe (like Go net.Pipe) --- */

int neverc_tcp_pipe(neverc_tcp_conn_t **a, neverc_tcp_conn_t **b) {
    if (!a || !b) return -1;
    if (nc_net_init() != 0) return -1;

#ifdef _WIN32
    SOCKET sv[2];
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(listener, 1) == SOCKET_ERROR ||
        getsockname(listener, (struct sockaddr *)&addr, &addrlen) == SOCKET_ERROR) {
        closesocket(listener);
        return -1;
    }

    sv[0] = socket(AF_INET, SOCK_STREAM, 0);
    if (sv[0] == INVALID_SOCKET) { closesocket(listener); return -1; }
    if (connect(sv[0], (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sv[0]); closesocket(listener); return -1;
    }
    sv[1] = accept(listener, NULL, NULL);
    closesocket(listener);
    if (sv[1] == INVALID_SOCKET) { closesocket(sv[0]); return -1; }
    {
        struct sockaddr_in local0, peer1, local1, peer0;
        int l0 = (int)sizeof(local0), p1 = (int)sizeof(peer1);
        int l1 = (int)sizeof(local1), p0 = (int)sizeof(peer0);
        if (getsockname(sv[0], (struct sockaddr *)&local0, &l0) == SOCKET_ERROR ||
            getpeername(sv[1], (struct sockaddr *)&peer1, &p1) == SOCKET_ERROR ||
            getsockname(sv[1], (struct sockaddr *)&local1, &l1) == SOCKET_ERROR ||
            getpeername(sv[0], (struct sockaddr *)&peer0, &p0) == SOCKET_ERROR ||
            local0.sin_family != AF_INET || peer1.sin_family != AF_INET ||
            local1.sin_family != AF_INET || peer0.sin_family != AF_INET ||
            local0.sin_port != peer1.sin_port ||
            local0.sin_addr.s_addr != peer1.sin_addr.s_addr ||
            local1.sin_port != peer0.sin_port ||
            local1.sin_addr.s_addr != peer0.sin_addr.s_addr) {
            closesocket(sv[0]);
            closesocket(sv[1]);
            return -1;
        }
    }
#else
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return -1;
#endif

    const char *err = NULL;
    *a = neverc_tcp_adopt_handle((uintptr_t)sv[0], NULL, 0, &err);
    *b = neverc_tcp_adopt_handle((uintptr_t)sv[1], NULL, 0, &err);
    if (!*a || !*b) {
        if (*a) neverc_tcp_close(*a);
        else nc_sock_close(sv[0]);
        if (*b) neverc_tcp_close(*b);
        else nc_sock_close(sv[1]);
        *a = *b = NULL;
        return -1;
    }
    return 0;
}

/* DNS/address utilities are in resolve.h / resolve.c */
