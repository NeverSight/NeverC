#include "neverc/std/net/tcp.h"
#include "../_net_internal.h"
#include <stdarg.h>

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
        out->addr[0] = '[';
        inet_ntop(AF_INET6, &in6->sin6_addr, out->addr + 1,
                  sizeof(out->addr) - 2);
        size_t len = strlen(out->addr);
        snprintf(out->addr + len, sizeof(out->addr) - len, "]:%d",
                 ntohs(in6->sin6_port));
        out->port = ntohs(in6->sin6_port);
    }
}

/* ======================================================================
 * Listen / Accept
 * ====================================================================== */

neverc_tcp_listener_t *neverc_tcp_listen(const char *addr, const char **errp) {
    nc_net_init();

    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0) {
        if (errp) *errp = "invalid address format";
        return NULL;
    }

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int rc = getaddrinfo(host[0] ? host : NULL, portstr, &hints, &result);
    if (rc != 0) {
        if (errp) *errp = "getaddrinfo failed";
        return NULL;
    }

    nc_sock_t fd = socket(result->ai_family, result->ai_socktype,
                          result->ai_protocol);
    if (fd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        if (errp) *errp = "socket creation failed";
        return NULL;
    }

    nc_set_reuseaddr(fd);

    if (bind(fd, result->ai_addr, (int)result->ai_addrlen) == NC_SOCK_ERR) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "bind failed";
        return NULL;
    }

    if (listen(fd, 512) == NC_SOCK_ERR) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "listen failed";
        return NULL;
    }

    neverc_tcp_listener_t *ln =
        (neverc_tcp_listener_t *)calloc(1, sizeof(*ln));
    if (!ln) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    ln->fd = fd;
    ln->addrlen = (socklen_t)result->ai_addrlen;
    memcpy(&ln->addr, result->ai_addr, result->ai_addrlen);

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
    socklen_t client_len = sizeof(client_addr);
    nc_sock_t cfd = accept(ln->fd, (struct sockaddr *)&client_addr,
                           &client_len);
    if (cfd == NC_INVALID_SOCK) {
        if (errp) *errp = "accept failed";
        return NULL;
    }

    neverc_tcp_conn_t *conn = (neverc_tcp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(cfd);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = cfd;
    conn->remote_len = client_len;
    memcpy(&conn->remote, &client_addr, client_len);
    conn->local_len = sizeof(conn->local);
    getsockname(cfd, (struct sockaddr *)&conn->local, &conn->local_len);

    if (errp) *errp = NULL;
    return conn;
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

/* ======================================================================
 * Dial
 * ====================================================================== */

neverc_tcp_conn_t *neverc_tcp_dial(const char *addr, const char **errp) {
    nc_net_init();

    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0) {
        if (errp) *errp = "invalid address format";
        return NULL;
    }

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int rc = getaddrinfo(host, portstr, &hints, &result);
    if (rc != 0) {
        if (errp) *errp = "getaddrinfo failed";
        return NULL;
    }

    nc_sock_t fd = socket(result->ai_family, result->ai_socktype,
                          result->ai_protocol);
    if (fd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        if (errp) *errp = "socket creation failed";
        return NULL;
    }

    if (connect(fd, result->ai_addr, (int)result->ai_addrlen) == NC_SOCK_ERR) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "connect failed";
        return NULL;
    }

    neverc_tcp_conn_t *conn = (neverc_tcp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = fd;
    conn->remote_len = (socklen_t)result->ai_addrlen;
    memcpy(&conn->remote, result->ai_addr, result->ai_addrlen);
    conn->local_len = sizeof(conn->local);
    getsockname(fd, (struct sockaddr *)&conn->local, &conn->local_len);

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return conn;
}

neverc_tcp_conn_t *neverc_tcp_adopt(int fd, const void *preload,
                                     size_t preload_len, const char **errp) {
    if (fd < 0 || fd == (int)NC_INVALID_SOCK) {
        if (errp) *errp = "invalid fd";
        return NULL;
    }

    neverc_tcp_conn_t *conn = (neverc_tcp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = (nc_sock_t)fd;
    conn->remote_len = sizeof(conn->remote);
    if (getpeername(conn->fd, (struct sockaddr *)&conn->remote,
                    &conn->remote_len) != 0) {
        memset(&conn->remote, 0, sizeof(conn->remote));
        conn->remote_len = 0;
    }
    conn->local_len = sizeof(conn->local);
    if (getsockname(conn->fd, (struct sockaddr *)&conn->local,
                    &conn->local_len) != 0) {
        memset(&conn->local, 0, sizeof(conn->local));
        conn->local_len = 0;
    }

    if (preload && preload_len > 0) {
        conn->preload = (uint8_t *)malloc(preload_len);
        if (!conn->preload) {
            free(conn);
            if (errp) *errp = "out of memory";
            return NULL;
        }
        memcpy(conn->preload, preload, preload_len);
        conn->preload_len = preload_len;
    }

    nc_set_blocking(conn->fd);

    if (errp) *errp = NULL;
    return conn;
}

/* ======================================================================
 * Connection I/O
 * ====================================================================== */

int neverc_tcp_write(neverc_tcp_conn_t *conn, const void *data, size_t len) {
    if (!conn) return -1;
    if (!data || len == 0) return 0;
    const char *p = (const char *)data;
    size_t total = 0;
    while (total < len) {
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
        if (total > 0) return (int)total;
        return -1;
    }
    return (int)total;
}

int neverc_tcp_read(neverc_tcp_conn_t *conn, void *buf, size_t buflen) {
    if (!conn) return -1;
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
    for (;;) {
#ifdef _WIN32
        int n = recv(conn->fd, (char *)buf, (int)buflen, 0);
        if (n >= 0) return n;
        if (WSAGetLastError() == WSAEINTR) continue;
        return -1;
#else
        ssize_t n = recv(conn->fd, buf, buflen, 0);
        if (n >= 0) return (int)n;
        if (errno == EINTR) continue;
        return -1;
#endif
    }
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

int neverc_tcp_set_timeout(neverc_tcp_conn_t *conn, int ms) {
    if (!conn) return -1;
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
               sizeof(tv));
    setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv,
               sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return 0;
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
    nc_net_init();

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
#else
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return -1;
#endif

    const char *err = NULL;
    *a = neverc_tcp_adopt(sv[0], NULL, 0, &err);
    *b = neverc_tcp_adopt(sv[1], NULL, 0, &err);
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
