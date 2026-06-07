#include "neverc/std/net/udp.h"
#include "../_net_internal.h"

/* ======================================================================
 * Internal
 * ====================================================================== */

struct neverc_udp_conn {
    nc_sock_t fd;
    struct sockaddr_storage local;
    socklen_t local_len;
    int connected;
};

static void sa_to_udp_addr(const struct sockaddr *sa, socklen_t salen,
                            neverc_udp_addr_t *out) {
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
    if (salen <= (socklen_t)sizeof(out->_sa)) {
        memcpy(out->_sa, sa, salen);
        out->_sa_len = (int)salen;
    }
}

/* ======================================================================
 * Listen / Dial
 * ====================================================================== */

neverc_udp_conn_t *neverc_udp_listen(const char *addr, const char **errp) {
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
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host[0] ? host : NULL, portstr, &hints, &result) != 0) {
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

    neverc_udp_conn_t *conn = (neverc_udp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = fd;
    conn->local_len = sizeof(conn->local);
    getsockname(fd, (struct sockaddr *)&conn->local, &conn->local_len);
    conn->connected = 0;

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return conn;
}

neverc_udp_conn_t *neverc_udp_dial(const char *addr, const char **errp) {
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
    hints.ai_socktype = SOCK_DGRAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &result) != 0) {
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

    neverc_udp_conn_t *conn = (neverc_udp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    conn->fd = fd;
    conn->local_len = sizeof(conn->local);
    getsockname(fd, (struct sockaddr *)&conn->local, &conn->local_len);
    conn->connected = 1;

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return conn;
}

/* ======================================================================
 * I/O
 * ====================================================================== */

int neverc_udp_read_from(neverc_udp_conn_t *conn, void *buf, size_t buflen,
                          neverc_udp_addr_t *from) {
    if (!conn) return -1;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    int n;
#ifdef _WIN32
    n = recvfrom(conn->fd, (char *)buf, (int)buflen, 0,
                 (struct sockaddr *)&sa, &salen);
#else
    n = (int)recvfrom(conn->fd, buf, buflen, 0,
                       (struct sockaddr *)&sa, &salen);
#endif
    if (n >= 0 && from)
        sa_to_udp_addr((struct sockaddr *)&sa, salen, from);
    return n;
}

int neverc_udp_write_to(neverc_udp_conn_t *conn, const void *data, size_t len,
                         const neverc_udp_addr_t *to) {
    if (!conn || !to || to->_sa_len == 0) return -1;
#ifdef _WIN32
    return sendto(conn->fd, (const char *)data, (int)len, 0,
                  (const struct sockaddr *)to->_sa, to->_sa_len);
#else
    return (int)sendto(conn->fd, data, len, 0,
                        (const struct sockaddr *)to->_sa,
                        (socklen_t)to->_sa_len);
#endif
}

int neverc_udp_write(neverc_udp_conn_t *conn, const void *data, size_t len) {
    if (!conn || !conn->connected) return -1;
#ifdef _WIN32
    return send(conn->fd, (const char *)data, (int)len, 0);
#else
    return (int)send(conn->fd, data, len, 0);
#endif
}

int neverc_udp_read(neverc_udp_conn_t *conn, void *buf, size_t buflen) {
    if (!conn) return -1;
#ifdef _WIN32
    return recv(conn->fd, (char *)buf, (int)buflen, 0);
#else
    return (int)recv(conn->fd, buf, buflen, 0);
#endif
}

void neverc_udp_close(neverc_udp_conn_t *conn) {
    if (!conn) return;
    nc_sock_close(conn->fd);
    free(conn);
}

int neverc_udp_local_addr(neverc_udp_conn_t *conn, neverc_udp_addr_t *addr) {
    if (!conn || !addr) return -1;
    sa_to_udp_addr((struct sockaddr *)&conn->local, conn->local_len, addr);
    return 0;
}

int neverc_udp_set_timeout(neverc_udp_conn_t *conn, int ms) {
    if (!conn) return -1;
#ifdef _WIN32
    DWORD tv = (DWORD)ms;
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    return setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

int neverc_udp_set_broadcast(neverc_udp_conn_t *conn, int enable) {
    if (!conn) return -1;
    int val = enable ? 1 : 0;
#ifdef _WIN32
    return setsockopt(conn->fd, SOL_SOCKET, SO_BROADCAST, (const char *)&val,
                      sizeof(val));
#else
    return setsockopt(conn->fd, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
#endif
}

int neverc_udp_resolve_addr(const char *addr_str, neverc_udp_addr_t *out) {
    if (!addr_str || !out) return -1;

    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr_str, host, sizeof(host), &port) != 0)
        return -1;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host[0] ? host : "0.0.0.0", portstr, &hints,
                    &result) != 0)
        return -1;

    sa_to_udp_addr(result->ai_addr, (socklen_t)result->ai_addrlen, out);
    freeaddrinfo(result);
    return 0;
}
