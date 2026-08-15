#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "neverc/std/net/udp.h"
#include "../_net_internal.h"

#ifndef _WIN32
#include <poll.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
#include <net/if_dl.h>
#endif

#ifdef _WIN32
typedef INT(WSAAPI *nc_udp_recv_msg_fn)(
    SOCKET, LPWSAMSG, LPDWORD, LPWSAOVERLAPPED,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE);

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

static const GUID nc_udp_recv_msg_guid = {
    0xf689d7c8, 0x6f1f, 0x436b,
    {0x8a, 0x53, 0xe5, 0x4f, 0xe3, 0x51, 0xc3, 0x22}};
#endif

/* ======================================================================
 * Internal
 * ====================================================================== */

struct neverc_udp_conn {
    nc_sock_t fd;
    struct sockaddr_storage local;
    socklen_t local_len;
    int connected;
    uint8_t *packet_scratch;
    int64_t read_deadline_ms;
    int64_t write_deadline_ms;
    int read_timeout_ms;
    int write_timeout_ms;
    volatile int nonblocking;
    nc_mutex_t read_lock;
#ifdef _WIN32
    nc_udp_recv_msg_fn recv_msg;
#endif
};

struct neverc_udp_queue {
    size_t capacity;
    size_t payload_capacity;
    size_t head;
    size_t length;
    uint8_t *storage;
    neverc_udp_recv_message_t *slots;
};

static int64_t udp_realtime_ms(void);
static int udp_deadline_expired(int64_t deadline_ms);
static int udp_deadline_timeout(int64_t deadline_ms);
static int udp_refresh_read_deadline(neverc_udp_conn_t *conn);
static int udp_refresh_write_deadline(neverc_udp_conn_t *conn);

static int udp_timeout_error(void) {
#ifdef _WIN32
    return WSAETIMEDOUT;
#else
    return ETIMEDOUT;
#endif
}

static int udp_read_lock_init(neverc_udp_conn_t *conn) {
#ifdef _WIN32
    nc_mutex_init(&conn->read_lock);
    return 0;
#else
    return nc_mutex_init(&conn->read_lock);
#endif
}

static void udp_enable_packet_info(nc_sock_t fd, int family) {
    int enabled = 1;
    if (family == AF_INET) {
#if defined(IP_PKTINFO)
#ifdef _WIN32
        (void)setsockopt(fd, IPPROTO_IP, IP_PKTINFO,
                         (const char *)&enabled, sizeof(enabled));
#else
        (void)setsockopt(fd, IPPROTO_IP, IP_PKTINFO,
                         &enabled, sizeof(enabled));
#endif
#endif
#if defined(IP_RECVDSTADDR) && !defined(_WIN32)
        (void)setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR,
                         &enabled, sizeof(enabled));
#endif
#if defined(IP_RECVIF) && !defined(_WIN32)
        (void)setsockopt(fd, IPPROTO_IP, IP_RECVIF,
                         &enabled, sizeof(enabled));
#endif
    } else if (family == AF_INET6) {
#if defined(IPV6_RECVPKTINFO)
#ifdef _WIN32
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
                         (const char *)&enabled, sizeof(enabled));
#else
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
                         &enabled, sizeof(enabled));
#endif
#elif defined(IPV6_PKTINFO)
#ifdef _WIN32
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_PKTINFO,
                         (const char *)&enabled, sizeof(enabled));
#else
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_PKTINFO,
                         &enabled, sizeof(enabled));
#endif
#endif
    }
}

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

#ifdef _WIN32
static nc_udp_recv_msg_fn udp_load_recv_msg(nc_sock_t fd) {
    nc_udp_recv_msg_fn recv_msg = NULL;
    DWORD bytes = 0;
    int rc = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                      (void *)&nc_udp_recv_msg_guid,
                      sizeof(nc_udp_recv_msg_guid),
                      &recv_msg, sizeof(recv_msg),
                      &bytes, NULL, NULL);
    return rc == 0 ? recv_msg : NULL;
}

static void udp_disable_connection_reset(nc_sock_t fd) {
    BOOL enabled = FALSE;
    DWORD bytes = 0;
    (void)WSAIoctl(fd, SIO_UDP_CONNRESET,
                   &enabled, sizeof(enabled),
                   NULL, 0, &bytes, NULL, NULL);
}

static void udp_parse_windows_packet_control(
    const neverc_udp_conn_t *conn, WSAMSG *msg,
    neverc_udp_packet_info_t *info) {
    struct sockaddr_storage destination = conn->local;
    socklen_t destination_len = conn->local_len;
    uint16_t port = 0;
    if (destination.ss_family == AF_INET)
        port = ((struct sockaddr_in *)&destination)->sin_port;
    else if (destination.ss_family == AF_INET6)
        port = ((struct sockaddr_in6 *)&destination)->sin6_port;

    for (WSACMSGHDR *cmsg = WSA_CMSG_FIRSTHDR(msg); cmsg != NULL;
         cmsg = WSA_CMSG_NXTHDR(msg, cmsg)) {
#if defined(IP_PKTINFO)
        if (cmsg->cmsg_level == IPPROTO_IP &&
            cmsg->cmsg_type == IP_PKTINFO &&
            cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(IN_PKTINFO))) {
            const IN_PKTINFO *packet =
                (const IN_PKTINFO *)WSA_CMSG_DATA(cmsg);
            struct sockaddr_in *dst =
                (struct sockaddr_in *)&destination;
            memset(dst, 0, sizeof(*dst));
            dst->sin_family = AF_INET;
            dst->sin_port = port;
            dst->sin_addr = packet->ipi_addr;
            destination_len = sizeof(*dst);
            info->interface_index = packet->ipi_ifindex;
            continue;
        }
#endif
#if defined(IPV6_PKTINFO)
        if (cmsg->cmsg_level == IPPROTO_IPV6 &&
            cmsg->cmsg_type == IPV6_PKTINFO &&
            cmsg->cmsg_len >= WSA_CMSG_LEN(sizeof(IN6_PKTINFO))) {
            const IN6_PKTINFO *packet =
                (const IN6_PKTINFO *)WSA_CMSG_DATA(cmsg);
            struct sockaddr_in6 *dst =
                (struct sockaddr_in6 *)&destination;
            memset(dst, 0, sizeof(*dst));
            dst->sin6_family = AF_INET6;
            dst->sin6_port = port;
            dst->sin6_addr = packet->ipi6_addr;
            dst->sin6_scope_id = packet->ipi6_ifindex;
            destination_len = sizeof(*dst);
            info->interface_index = packet->ipi6_ifindex;
            continue;
        }
#endif
    }
    sa_to_udp_addr((struct sockaddr *)&destination, destination_len,
                   &info->destination);
}
#endif

#ifndef _WIN32
static uint16_t udp_local_port(const neverc_udp_conn_t *conn) {
    if (conn->local.ss_family == AF_INET)
        return ((const struct sockaddr_in *)&conn->local)->sin_port;
    if (conn->local.ss_family == AF_INET6)
        return ((const struct sockaddr_in6 *)&conn->local)->sin6_port;
    return 0;
}

static void udp_parse_packet_control(const neverc_udp_conn_t *conn,
                                     const struct msghdr *msg,
                                     neverc_udp_packet_info_t *info) {
    struct sockaddr_storage destination = conn->local;
    socklen_t destination_len = conn->local_len;
    uint16_t port = udp_local_port(conn);

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR((struct msghdr *)msg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR((struct msghdr *)msg, cmsg)) {
#if defined(IP_PKTINFO) && defined(__linux__)
        if (cmsg->cmsg_level == IPPROTO_IP &&
            cmsg->cmsg_type == IP_PKTINFO &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(struct in_pktinfo))) {
            const struct in_pktinfo *packet =
                (const struct in_pktinfo *)CMSG_DATA(cmsg);
            struct sockaddr_in *dst = (struct sockaddr_in *)&destination;
            memset(dst, 0, sizeof(*dst));
            dst->sin_family = AF_INET;
            dst->sin_port = port;
            dst->sin_addr = packet->ipi_addr;
            destination_len = sizeof(*dst);
            info->interface_index = (uint32_t)packet->ipi_ifindex;
            continue;
        }
#endif
#if defined(IP_RECVDSTADDR)
        if (cmsg->cmsg_level == IPPROTO_IP &&
            cmsg->cmsg_type == IP_RECVDSTADDR &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(struct in_addr))) {
            const struct in_addr *packet =
                (const struct in_addr *)CMSG_DATA(cmsg);
            struct sockaddr_in *dst = (struct sockaddr_in *)&destination;
            memset(dst, 0, sizeof(*dst));
            dst->sin_family = AF_INET;
            dst->sin_port = port;
            dst->sin_addr = *packet;
            destination_len = sizeof(*dst);
            continue;
        }
#endif
#if defined(IP_RECVIF)
        if (cmsg->cmsg_level == IPPROTO_IP &&
            cmsg->cmsg_type == IP_RECVIF &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(struct sockaddr_dl))) {
            const struct sockaddr_dl *link =
                (const struct sockaddr_dl *)CMSG_DATA(cmsg);
            info->interface_index = (uint32_t)link->sdl_index;
            continue;
        }
#endif
#if defined(IPV6_PKTINFO)
        if (cmsg->cmsg_level == IPPROTO_IPV6 &&
            cmsg->cmsg_type == IPV6_PKTINFO &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(struct in6_pktinfo))) {
            const struct in6_pktinfo *packet =
                (const struct in6_pktinfo *)CMSG_DATA(cmsg);
            struct sockaddr_in6 *dst = (struct sockaddr_in6 *)&destination;
            memset(dst, 0, sizeof(*dst));
            dst->sin6_family = AF_INET6;
            dst->sin6_port = port;
            dst->sin6_addr = packet->ipi6_addr;
            dst->sin6_scope_id = packet->ipi6_ifindex;
            destination_len = sizeof(*dst);
            info->interface_index = (uint32_t)packet->ipi6_ifindex;
            continue;
        }
#endif
    }

    sa_to_udp_addr((struct sockaddr *)&destination, destination_len,
                   &info->destination);
}
#endif

/* ======================================================================
 * Listen / Dial
 * ====================================================================== */

neverc_udp_conn_t *neverc_udp_listen(const char *addr, const char **errp) {
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
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host[0] ? host : NULL, portstr, &hints, &result) != 0) {
        if (errp) *errp = "getaddrinfo failed";
        return NULL;
    }

    nc_sock_t fd = NC_INVALID_SOCK;
    for (rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == NC_INVALID_SOCK)
            continue;
        (void)nc_set_reuseaddr(fd);
        udp_enable_packet_info(fd, rp->ai_family);
        if (rp->ai_family == AF_INET6 && host[0] == '\0') {
            int v6only = 0;
#ifdef _WIN32
            (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                             (const char *)&v6only, sizeof(v6only));
#else
            (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                             &v6only, sizeof(v6only));
#endif
        }
        if (bind(fd, rp->ai_addr, (int)rp->ai_addrlen) != NC_SOCK_ERR)
            break;
        nc_sock_close(fd);
        fd = NC_INVALID_SOCK;
    }
    if (fd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        if (errp) *errp = "no address could be bound";
        return NULL;
    }

#ifdef _WIN32
    udp_disable_connection_reset(fd);
#endif

    neverc_udp_conn_t *conn = (neverc_udp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    if (udp_read_lock_init(conn) != 0) {
        nc_sock_close(fd);
        free(conn);
        freeaddrinfo(result);
        if (errp) *errp = "mutex initialization failed";
        return NULL;
    }
    conn->fd = fd;
    conn->local_len = sizeof(conn->local);
    getsockname(fd, (struct sockaddr *)&conn->local, &conn->local_len);
    conn->connected = 0;
#ifdef _WIN32
    conn->recv_msg = udp_load_recv_msg(fd);
#endif

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return conn;
}

neverc_udp_conn_t *neverc_udp_dial(const char *addr, const char **errp) {
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
    hints.ai_socktype = SOCK_DGRAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &result) != 0) {
        if (errp) *errp = "getaddrinfo failed";
        return NULL;
    }

    nc_sock_t fd = NC_INVALID_SOCK;
    for (rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == NC_INVALID_SOCK)
            continue;
        udp_enable_packet_info(fd, rp->ai_family);
        if (connect(fd, rp->ai_addr, (int)rp->ai_addrlen) != NC_SOCK_ERR)
            break;
        nc_sock_close(fd);
        fd = NC_INVALID_SOCK;
    }
    if (fd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        if (errp) *errp = "all connection attempts failed";
        return NULL;
    }

    neverc_udp_conn_t *conn = (neverc_udp_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        nc_sock_close(fd);
        freeaddrinfo(result);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    if (udp_read_lock_init(conn) != 0) {
        nc_sock_close(fd);
        free(conn);
        freeaddrinfo(result);
        if (errp) *errp = "mutex initialization failed";
        return NULL;
    }
    conn->fd = fd;
    conn->local_len = sizeof(conn->local);
    getsockname(fd, (struct sockaddr *)&conn->local, &conn->local_len);
    conn->connected = 1;
#ifdef _WIN32
    conn->recv_msg = udp_load_recv_msg(fd);
#endif

    freeaddrinfo(result);
    if (errp) *errp = NULL;
    return conn;
}

/* ======================================================================
 * I/O
 * ====================================================================== */

static int udp_error_would_block(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static void udp_set_last_error(int error) {
#ifdef _WIN32
    WSASetLastError(error);
#else
    errno = error;
#endif
}

static int udp_ensure_nonblocking(neverc_udp_conn_t *conn) {
    if (nc_atomic_load(&conn->nonblocking)) return 0;
    if (nc_set_nonblocking(conn->fd) != 0) return -1;
    nc_atomic_store(&conn->nonblocking, 1);
    return 0;
}

static int udp_wait_io(nc_sock_t fd, int write_ready, int timeout_ms) {
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

static int udp_wait_conn(neverc_udp_conn_t *conn, int write_ready) {
    int64_t deadline_ms =
        write_ready ? conn->write_deadline_ms : conn->read_deadline_ms;
    int timeout_ms =
        write_ready ? conn->write_timeout_ms : conn->read_timeout_ms;
    if (deadline_ms > 0) {
        timeout_ms = udp_deadline_timeout(deadline_ms);
        if (timeout_ms < 0) return -1;
    } else if (timeout_ms == 0) {
        timeout_ms = -1;
    }

    int ready = udp_wait_io(conn->fd, write_ready, timeout_ms);
    if (ready == 0) {
        udp_set_last_error(udp_timeout_error());
        return -1;
    }
    return ready < 0 ? -1 : 0;
}

static int udp_read_from_unlocked(neverc_udp_conn_t *conn, void *buf,
                                  size_t buflen,
                                  neverc_udp_addr_t *from) {
    if (!conn || (!buf && buflen > 0) ||
        buflen > NEVERC_UDP_MAX_DATAGRAM_SIZE)
        return -1;
    if (udp_refresh_read_deadline(conn) != 0) return -1;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    int n;
#ifdef _WIN32
    if (buflen > (size_t)INT_MAX) return -1;
    n = recvfrom(conn->fd, (char *)buf, (int)buflen, 0,
                 (struct sockaddr *)&sa, &salen);
#else
    do {
        n = (int)recvfrom(conn->fd, buf, buflen, 0,
                          (struct sockaddr *)&sa, &salen);
    } while (n < 0 && errno == EINTR);
#endif
    if (n >= 0 && from)
        sa_to_udp_addr((struct sockaddr *)&sa, salen, from);
    return n;
}

int neverc_udp_read_from(neverc_udp_conn_t *conn, void *buf, size_t buflen,
                          neverc_udp_addr_t *from) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->read_lock);
    int result;
    for (;;) {
        result = udp_read_from_unlocked(conn, buf, buflen, from);
        if (result >= 0 || !nc_atomic_load(&conn->nonblocking) ||
            !udp_error_would_block(nc_sock_errno) ||
            udp_wait_conn(conn, 0) != 0)
            break;
    }
    nc_mutex_unlock(&conn->read_lock);
    return result;
}

static int udp_read_packet_unlocked(
    neverc_udp_conn_t *conn, void *buf, size_t buflen,
    neverc_udp_packet_info_t *info) {
    if (!conn || !info || (!buf && buflen > 0) ||
        buflen > NEVERC_UDP_MAX_DATAGRAM_SIZE)
        return -1;
    if (udp_refresh_read_deadline(conn) != 0) return -1;
    memset(info, 0, sizeof(*info));
    struct sockaddr_storage sa;
    memset(&sa, 0, sizeof(sa));
    socklen_t salen = sizeof(sa);
#ifdef _WIN32
    char probe;
    char *peek_buf = buflen > 0 ? (char *)buf : &probe;
    int peek_len = buflen > 0 ? (int)buflen : 1;
    int peeked;
    int peek_error = 0;
    do {
        peeked = recv(conn->fd, peek_buf, peek_len, MSG_PEEK);
        if (peeked < 0) peek_error = WSAGetLastError();
    } while (peeked < 0 && peek_error == WSAEINTR);
    int oversized = peeked < 0 && peek_error == WSAEMSGSIZE;
    if (peeked < 0 && !oversized) return -1;
    if (buflen == 0 && peeked > 0) oversized = 1;

    size_t receive_capacity = buflen;
    uint8_t *receive_buf = (uint8_t *)buf;
    if (oversized) {
        if (!conn->packet_scratch) {
            conn->packet_scratch =
                (uint8_t *)malloc(NEVERC_UDP_MAX_DATAGRAM_SIZE);
            if (!conn->packet_scratch) return -1;
        }
        receive_buf = conn->packet_scratch;
        receive_capacity = NEVERC_UDP_MAX_DATAGRAM_SIZE;
    }

    int n = -1;
    WSAMSG msg;
    memset(&msg, 0, sizeof(msg));
    if (conn->recv_msg) {
        WSABUF data_buf;
        data_buf.buf = (CHAR *)receive_buf;
        data_buf.len = (ULONG)receive_capacity;
        union {
            WSACMSGHDR align;
            CHAR data[256];
        } control;
        memset(&control, 0, sizeof(control));
        msg.name = (LPSOCKADDR)&sa;
        msg.namelen = sizeof(sa);
        msg.lpBuffers = &data_buf;
        msg.dwBufferCount = 1;
        msg.Control.buf = control.data;
        msg.Control.len = sizeof(control.data);
        DWORD received = 0;
        if (conn->recv_msg(conn->fd, &msg, &received, NULL, NULL) == 0) {
            n = (int)received;
            salen = (socklen_t)msg.namelen;
            udp_parse_windows_packet_control(conn, &msg, info);
        }
    } else {
        n = recvfrom(conn->fd, (char *)receive_buf,
                     (int)receive_capacity, 0,
                     (struct sockaddr *)&sa, &salen);
    }
    if (n < 0) {
        return -1;
    }
    size_t datagram_len = (size_t)n;
    size_t copied_size =
        datagram_len < buflen ? datagram_len : buflen;
    if (receive_buf != buf && copied_size > 0)
        memcpy(buf, receive_buf, copied_size);
#else
    uint8_t *receive_buf = (uint8_t *)buf;
    size_t receive_capacity = buflen;
#if !defined(__linux__)
    char probe;
    struct iovec peek_iov;
    peek_iov.iov_base = buflen > 0 ? buf : &probe;
    peek_iov.iov_len = buflen > 0 ? buflen : 1;
    struct msghdr peek_msg;
    memset(&peek_msg, 0, sizeof(peek_msg));
    peek_msg.msg_iov = &peek_iov;
    peek_msg.msg_iovlen = 1;
    ssize_t peeked;
    do {
        peeked = recvmsg(conn->fd, &peek_msg, MSG_PEEK);
    } while (peeked < 0 && errno == EINTR);
    if (peeked < 0) return -1;
    int oversized = (peek_msg.msg_flags & MSG_TRUNC) != 0 ||
                    (buflen == 0 && peeked > 0);
    if (oversized) {
        if (!conn->packet_scratch) {
            conn->packet_scratch =
                (uint8_t *)malloc(NEVERC_UDP_MAX_DATAGRAM_SIZE);
            if (!conn->packet_scratch) return -1;
        }
        receive_buf = conn->packet_scratch;
        receive_capacity = NEVERC_UDP_MAX_DATAGRAM_SIZE;
    }
#endif
    struct iovec iov;
    iov.iov_base = receive_buf;
    iov.iov_len = receive_capacity;

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    union {
        struct cmsghdr align;
        unsigned char data[256];
    } control;
    memset(&control, 0, sizeof(control));
    msg.msg_name = &sa;
    msg.msg_namelen = salen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data;
    msg.msg_controllen = sizeof(control.data);

    ssize_t n;
    do {
        n = recvmsg(conn->fd, &msg,
#if defined(__linux__)
                    MSG_TRUNC
#else
                    0
#endif
        );
    } while (n < 0 && errno == EINTR);
    salen = msg.msg_namelen;
    if (n < 0) {
        return -1;
    }
    size_t datagram_len = (size_t)n;
    size_t copied_size =
        datagram_len < buflen ? datagram_len : buflen;
    if (receive_buf != buf && copied_size > 0)
        memcpy(buf, receive_buf, copied_size);
    udp_parse_packet_control(conn, &msg, info);
#endif

    info->datagram_len = datagram_len;
    info->truncated = datagram_len > buflen;
    sa_to_udp_addr((struct sockaddr *)&sa, salen, &info->source);
#ifdef _WIN32
    if (info->destination._sa_len == 0)
        sa_to_udp_addr((struct sockaddr *)&conn->local, conn->local_len,
                       &info->destination);
#endif
    int copied = (int)copied_size;
    return copied;
}

int neverc_udp_read_packet(neverc_udp_conn_t *conn, void *buf, size_t buflen,
                            neverc_udp_packet_info_t *info) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->read_lock);
    int result;
    for (;;) {
        result = udp_read_packet_unlocked(conn, buf, buflen, info);
        if (result >= 0 || !nc_atomic_load(&conn->nonblocking) ||
            !udp_error_would_block(nc_sock_errno) ||
            udp_wait_conn(conn, 0) != 0)
            break;
    }
    nc_mutex_unlock(&conn->read_lock);
    return result;
}

static int udp_readable_now(nc_sock_t fd) {
#ifdef _WIN32
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timeval timeout = {0, 0};
    return select(0, &readfds, NULL, NULL, &timeout) > 0;
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, 0);
    } while (rc < 0 && errno == EINTR);
    return rc > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0;
#endif
}

static int udp_writable_now(nc_sock_t fd) {
#ifdef _WIN32
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);
    struct timeval timeout = {0, 0};
    return select(0, NULL, &writefds, NULL, &timeout) > 0;
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, 0);
    } while (rc < 0 && errno == EINTR);
    return rc > 0 && (pfd.revents & (POLLOUT | POLLERR | POLLHUP)) != 0;
#endif
}

static neverc_net_result_t udp_result(neverc_net_status_t status,
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

static neverc_net_result_t udp_try_read_packet_unlocked(
    neverc_udp_conn_t *conn, void *buf, size_t buflen,
    neverc_udp_packet_info_t *info) {
    if (!conn || !info || (!buf && buflen > 0) ||
        buflen > NEVERC_UDP_MAX_DATAGRAM_SIZE)
        return udp_result(NEVERC_NET_INVALID, 0, "read", 0);
    if (udp_deadline_expired(conn->read_deadline_ms))
        return udp_result(NEVERC_NET_TIMEOUT, udp_timeout_error(),
                          "read", 0);
    if (udp_ensure_nonblocking(conn) != 0)
        return udp_result(NEVERC_NET_SYSTEM, nc_sock_errno, "read", 0);

    if (!udp_readable_now(conn->fd)) {
#ifdef _WIN32
        int error = WSAEWOULDBLOCK;
#else
        int error = EWOULDBLOCK;
#endif
        return udp_result(NEVERC_NET_WOULD_BLOCK, error, "read", 0);
    }

    int n = udp_read_packet_unlocked(conn, buf, buflen, info);
    if (n >= 0)
        return udp_result(info->truncated ? NEVERC_NET_TRUNCATED
                                         : NEVERC_NET_OK,
                          0, "read", (size_t)n);
    if (udp_deadline_expired(conn->read_deadline_ms))
        return udp_result(NEVERC_NET_TIMEOUT, udp_timeout_error(),
                          "read", 0);
    int error = nc_sock_errno;
    if (udp_error_would_block(error))
        return udp_result(NEVERC_NET_WOULD_BLOCK, error, "read", 0);
    return udp_result(NEVERC_NET_SYSTEM, error, "read", 0);
}

neverc_net_result_t neverc_udp_try_read_packet(
    neverc_udp_conn_t *conn, void *buf, size_t buflen,
    neverc_udp_packet_info_t *info) {
    if (!conn)
        return udp_result(NEVERC_NET_INVALID, 0, "read", 0);
    nc_mutex_lock(&conn->read_lock);
    neverc_net_result_t result =
        udp_try_read_packet_unlocked(conn, buf, buflen, info);
    nc_mutex_unlock(&conn->read_lock);
    return result;
}

neverc_net_result_t neverc_udp_try_write(
    neverc_udp_conn_t *conn, const void *data, size_t len,
    const neverc_udp_addr_t *destination) {
    if (!conn || (!data && len > 0) ||
        len > NEVERC_UDP_MAX_DATAGRAM_SIZE ||
        (!destination && !conn->connected) ||
        (destination &&
         (destination->_sa_len <= 0 ||
          destination->_sa_len > (int)sizeof(destination->_sa))))
        return udp_result(NEVERC_NET_INVALID, 0, "write", 0);
    if (udp_deadline_expired(conn->write_deadline_ms))
        return udp_result(NEVERC_NET_TIMEOUT, udp_timeout_error(),
                          "write", 0);
    if (udp_ensure_nonblocking(conn) != 0)
        return udp_result(NEVERC_NET_SYSTEM, nc_sock_errno, "write", 0);
    if (!udp_writable_now(conn->fd)) {
#ifdef _WIN32
        int error = WSAEWOULDBLOCK;
#else
        int error = EWOULDBLOCK;
#endif
        return udp_result(NEVERC_NET_WOULD_BLOCK, error, "write", 0);
    }

#ifdef _WIN32
    int n = destination
                ? sendto(conn->fd, (const char *)data, (int)len, 0,
                         (const struct sockaddr *)destination->_sa,
                         destination->_sa_len)
                : send(conn->fd, (const char *)data, (int)len, 0);
#else
    ssize_t n;
    do {
        n = destination
                ? sendto(conn->fd, data, len, MSG_DONTWAIT,
                         (const struct sockaddr *)destination->_sa,
                         (socklen_t)destination->_sa_len)
                : send(conn->fd, data, len, MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR);
#endif
    if (n >= 0)
        return udp_result(NEVERC_NET_OK, 0, "write", (size_t)n);
    int error = nc_sock_errno;
    if (udp_deadline_expired(conn->write_deadline_ms))
        return udp_result(NEVERC_NET_TIMEOUT, udp_timeout_error(),
                          "write", 0);
#ifdef _WIN32
    if (error == WSAEWOULDBLOCK)
#else
    if (error == EAGAIN || error == EWOULDBLOCK)
#endif
        return udp_result(NEVERC_NET_WOULD_BLOCK, error, "write", 0);
    return udp_result(NEVERC_NET_SYSTEM, error, "write", 0);
}

int neverc_udp_read_batch(neverc_udp_conn_t *conn,
                           neverc_udp_recv_message_t *messages, size_t count) {
    if (!conn || !messages || count == 0 ||
        count > NEVERC_UDP_MAX_BATCH_SIZE)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        if ((!messages[i].data && messages[i].capacity > 0) ||
            messages[i].capacity > NEVERC_UDP_MAX_DATAGRAM_SIZE)
            return -1;
        messages[i].len = 0;
        memset(&messages[i].info, 0, sizeof(messages[i].info));
    }

#if defined(__linux__)
    nc_mutex_lock(&conn->read_lock);
    struct mmsghdr msgvec[NEVERC_UDP_MAX_BATCH_SIZE];
    struct iovec iov[NEVERC_UDP_MAX_BATCH_SIZE];
    struct sockaddr_storage peers[NEVERC_UDP_MAX_BATCH_SIZE];
    union {
        struct cmsghdr align;
        unsigned char data[256];
    } controls[NEVERC_UDP_MAX_BATCH_SIZE];
    memset(msgvec, 0, sizeof(msgvec));
    memset(peers, 0, sizeof(peers));
    memset(controls, 0, sizeof(controls));

    for (size_t i = 0; i < count; ++i) {
        iov[i].iov_base = messages[i].data;
        iov[i].iov_len = messages[i].capacity;
        msgvec[i].msg_hdr.msg_name = &peers[i];
        msgvec[i].msg_hdr.msg_namelen = sizeof(peers[i]);
        msgvec[i].msg_hdr.msg_iov = &iov[i];
        msgvec[i].msg_hdr.msg_iovlen = 1;
        msgvec[i].msg_hdr.msg_control = controls[i].data;
        msgvec[i].msg_hdr.msg_controllen = sizeof(controls[i].data);
    }

    int received;
    for (;;) {
        if (udp_refresh_read_deadline(conn) != 0 ||
            (nc_atomic_load(&conn->nonblocking) &&
             udp_wait_conn(conn, 0) != 0)) {
            received = -1;
            break;
        }
        do {
            received = recvmmsg(conn->fd, msgvec, (unsigned int)count,
                                MSG_WAITFORONE | MSG_TRUNC, NULL);
        } while (received < 0 && errno == EINTR);
        if (received >= 0 ||
            !nc_atomic_load(&conn->nonblocking) ||
            !udp_error_would_block(errno))
            break;
    }
    if (received < 0) {
        nc_mutex_unlock(&conn->read_lock);
        return -1;
    }

    for (int i = 0; i < received; ++i) {
        size_t datagram_len = msgvec[i].msg_len;
        messages[i].len = datagram_len < messages[i].capacity
                              ? datagram_len
                              : messages[i].capacity;
        messages[i].info.datagram_len = datagram_len;
        messages[i].info.truncated =
            (msgvec[i].msg_hdr.msg_flags & MSG_TRUNC) != 0 ||
            datagram_len > messages[i].capacity;
        sa_to_udp_addr((struct sockaddr *)&peers[i],
                       msgvec[i].msg_hdr.msg_namelen,
                       &messages[i].info.source);
        udp_parse_packet_control(conn, &msgvec[i].msg_hdr,
                                 &messages[i].info);
    }
    nc_mutex_unlock(&conn->read_lock);
    return received;
#else
    int received = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i > 0 && !udp_readable_now(conn->fd))
            break;
        int n = neverc_udp_read_packet(conn, messages[i].data,
                                       messages[i].capacity,
                                       &messages[i].info);
        if (n < 0)
            return received > 0 ? received : -1;
        messages[i].len = (size_t)n;
        received++;
    }
    return received;
#endif
}

int neverc_udp_write_batch(neverc_udp_conn_t *conn,
                            const neverc_udp_send_message_t *messages,
                            size_t count) {
    if (!conn || !messages || count == 0 ||
        count > NEVERC_UDP_MAX_BATCH_SIZE)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        if ((!messages[i].data && messages[i].len > 0) ||
            messages[i].len > NEVERC_UDP_MAX_DATAGRAM_SIZE ||
            (!messages[i].destination && !conn->connected) ||
            (messages[i].destination &&
             (messages[i].destination->_sa_len <= 0 ||
              messages[i].destination->_sa_len >
                  (int)sizeof(messages[i].destination->_sa))))
            return -1;
    }
    if (udp_refresh_write_deadline(conn) != 0) return -1;

#if defined(__linux__)
    struct mmsghdr msgvec[NEVERC_UDP_MAX_BATCH_SIZE];
    struct iovec iov[NEVERC_UDP_MAX_BATCH_SIZE];
    memset(msgvec, 0, sizeof(msgvec));

    for (size_t i = 0; i < count; ++i) {
        iov[i].iov_base = (void *)messages[i].data;
        iov[i].iov_len = messages[i].len;
        msgvec[i].msg_hdr.msg_iov = &iov[i];
        msgvec[i].msg_hdr.msg_iovlen = 1;
        if (messages[i].destination) {
            msgvec[i].msg_hdr.msg_name =
                (void *)messages[i].destination->_sa;
            msgvec[i].msg_hdr.msg_namelen =
                (socklen_t)messages[i].destination->_sa_len;
        }
    }

    int sent;
    for (;;) {
        if (nc_atomic_load(&conn->nonblocking) &&
            udp_wait_conn(conn, 1) != 0)
            return -1;
        do {
            sent = sendmmsg(conn->fd, msgvec, (unsigned int)count, 0);
        } while (sent < 0 && errno == EINTR);
        if (sent >= 0 || !nc_atomic_load(&conn->nonblocking) ||
            !udp_error_would_block(errno))
            break;
    }
    return sent;
#else
    int sent = 0;
    for (size_t i = 0; i < count; ++i) {
        int n = messages[i].destination
                    ? neverc_udp_write_to(conn, messages[i].data,
                                          messages[i].len,
                                          messages[i].destination)
                    : neverc_udp_write(conn, messages[i].data,
                                       messages[i].len);
        if (n < 0 || (size_t)n != messages[i].len)
            return sent > 0 ? sent : -1;
        sent++;
    }
    return sent;
#endif
}

neverc_udp_queue_t *neverc_udp_queue_create(size_t capacity,
                                             size_t payload_capacity) {
    if (capacity == 0 || capacity > NEVERC_UDP_MAX_QUEUE_CAPACITY ||
        payload_capacity == 0 ||
        payload_capacity > NEVERC_UDP_MAX_DATAGRAM_SIZE ||
        capacity > SIZE_MAX / payload_capacity)
        return NULL;

    neverc_udp_queue_t *queue =
        (neverc_udp_queue_t *)calloc(1, sizeof(*queue));
    if (!queue) return NULL;
    queue->slots = (neverc_udp_recv_message_t *)calloc(
        capacity, sizeof(*queue->slots));
    queue->storage = (uint8_t *)malloc(capacity * payload_capacity);
    if (!queue->slots || !queue->storage) {
        free(queue->storage);
        free(queue->slots);
        free(queue);
        return NULL;
    }

    queue->capacity = capacity;
    queue->payload_capacity = payload_capacity;
    for (size_t i = 0; i < capacity; ++i) {
        queue->slots[i].data = queue->storage + i * payload_capacity;
        queue->slots[i].capacity = payload_capacity;
    }
    return queue;
}

int neverc_udp_queue_receive(neverc_udp_conn_t *conn,
                              neverc_udp_queue_t *queue,
                              size_t max_messages) {
    if (!conn || !queue || max_messages == 0) return -1;
    size_t available = queue->capacity - queue->length;
    if (max_messages < available) available = max_messages;
    int total = 0;
    int first = 1;

    while (available > 0 &&
           (first || udp_readable_now(conn->fd))) {
        first = 0;
        size_t tail = (queue->head + queue->length) % queue->capacity;
        size_t contiguous = queue->capacity - tail;
        if (contiguous > available) contiguous = available;
        if (contiguous > NEVERC_UDP_MAX_BATCH_SIZE)
            contiguous = NEVERC_UDP_MAX_BATCH_SIZE;

        int received = neverc_udp_read_batch(
            conn, &queue->slots[tail], contiguous);
        if (received < 0)
            return total > 0 ? total : -1;
        if (received == 0)
            break;
        queue->length += (size_t)received;
        available -= (size_t)received;
        total += received;
        if ((size_t)received < contiguous)
            break;
    }
    return total;
}

neverc_net_result_t neverc_udp_queue_pop(
    neverc_udp_queue_t *queue, void *buf, size_t buflen,
    neverc_udp_packet_info_t *info) {
    if (!queue || (!buf && buflen > 0))
        return udp_result(NEVERC_NET_INVALID, 0, "queue_pop", 0);
    if (queue->length == 0)
        return udp_result(NEVERC_NET_WOULD_BLOCK, 0, "queue_pop", 0);

    neverc_udp_recv_message_t *message = &queue->slots[queue->head];
    size_t copied = message->len < buflen ? message->len : buflen;
    if (copied > 0)
        memcpy(buf, message->data, copied);

    int truncated = message->info.truncated || message->len > buflen;
    if (info) {
        *info = message->info;
        info->truncated = truncated;
    }
    message->len = 0;
    memset(&message->info, 0, sizeof(message->info));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->length--;
    return udp_result(truncated ? NEVERC_NET_TRUNCATED : NEVERC_NET_OK,
                      0, "queue_pop", copied);
}

size_t neverc_udp_queue_length(const neverc_udp_queue_t *queue) {
    return queue ? queue->length : 0;
}

size_t neverc_udp_queue_capacity(const neverc_udp_queue_t *queue) {
    return queue ? queue->capacity : 0;
}

void neverc_udp_queue_free(neverc_udp_queue_t *queue) {
    if (!queue) return;
    free(queue->storage);
    free(queue->slots);
    free(queue);
}

int neverc_udp_write_to(neverc_udp_conn_t *conn, const void *data, size_t len,
                         const neverc_udp_addr_t *to) {
    if (!conn || !to || (!data && len > 0) ||
        len > NEVERC_UDP_MAX_DATAGRAM_SIZE ||
        to->_sa_len <= 0 || to->_sa_len > (int)sizeof(to->_sa))
        return -1;
    if (nc_atomic_load(&conn->nonblocking)) {
        for (;;) {
            neverc_net_result_t result =
                neverc_udp_try_write(conn, data, len, to);
            if (result.status == NEVERC_NET_OK)
                return (int)result.transferred;
            if (result.status == NEVERC_NET_WOULD_BLOCK &&
                udp_wait_conn(conn, 1) == 0)
                continue;
            if (result.system_code != 0)
                udp_set_last_error(result.system_code);
            return -1;
        }
    }
    if (udp_refresh_write_deadline(conn) != 0) return -1;
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
    if (!conn || !conn->connected || (!data && len > 0) ||
        len > NEVERC_UDP_MAX_DATAGRAM_SIZE)
        return -1;
    if (nc_atomic_load(&conn->nonblocking)) {
        for (;;) {
            neverc_net_result_t result =
                neverc_udp_try_write(conn, data, len, NULL);
            if (result.status == NEVERC_NET_OK)
                return (int)result.transferred;
            if (result.status == NEVERC_NET_WOULD_BLOCK &&
                udp_wait_conn(conn, 1) == 0)
                continue;
            if (result.system_code != 0)
                udp_set_last_error(result.system_code);
            return -1;
        }
    }
    if (udp_refresh_write_deadline(conn) != 0) return -1;
#ifdef _WIN32
    return send(conn->fd, (const char *)data, (int)len, 0);
#else
    return (int)send(conn->fd, data, len, 0);
#endif
}

int neverc_udp_read(neverc_udp_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || (!buf && buflen > 0) ||
        buflen > NEVERC_UDP_MAX_DATAGRAM_SIZE)
        return -1;
    nc_mutex_lock(&conn->read_lock);
    int result;
    for (;;) {
        if (udp_refresh_read_deadline(conn) != 0) {
            result = -1;
            break;
        }
#ifdef _WIN32
        result = recv(conn->fd, (char *)buf, (int)buflen, 0);
#else
        do {
            result = (int)recv(conn->fd, buf, buflen, 0);
        } while (result < 0 && errno == EINTR);
#endif
        if (result >= 0 || !nc_atomic_load(&conn->nonblocking) ||
            !udp_error_would_block(nc_sock_errno) ||
            udp_wait_conn(conn, 0) != 0)
            break;
    }
    nc_mutex_unlock(&conn->read_lock);
    return result;
}

void neverc_udp_close(neverc_udp_conn_t *conn) {
    if (!conn) return;
    nc_sock_close(conn->fd);
    nc_mutex_destroy(&conn->read_lock);
    free(conn->packet_scratch);
    free(conn);
}

int neverc_udp_local_addr(neverc_udp_conn_t *conn, neverc_udp_addr_t *addr) {
    if (!conn || !addr) return -1;
    sa_to_udp_addr((struct sockaddr *)&conn->local, conn->local_len, addr);
    return 0;
}

uintptr_t neverc_udp_conn_handle(neverc_udp_conn_t *conn) {
    return conn ? (uintptr_t)conn->fd : NEVERC_NET_INVALID_HANDLE;
}

int neverc_udp_get_mtu_info(neverc_udp_conn_t *conn,
                             neverc_udp_mtu_info_t *info) {
    if (!conn || !info) return -1;
    memset(info, 0, sizeof(*info));

    int family = conn->local.ss_family;
    size_t overhead;
    if (family == AF_INET6) {
        info->protocol_max_payload = 65527;
        overhead = 48;
    } else if (family == AF_INET) {
        info->protocol_max_payload = 65507;
        overhead = 28;
    } else {
        return -1;
    }

    int mtu = 0;
    socklen_t mtu_len = sizeof(mtu);
    (void)mtu_len;
    int rc = -1;
#if defined(IP_MTU)
    if (family == AF_INET)
        rc = getsockopt(conn->fd, IPPROTO_IP, IP_MTU,
#ifdef _WIN32
                        (char *)&mtu,
#else
                        &mtu,
#endif
                        &mtu_len);
#endif
#if defined(IPV6_MTU)
    if (family == AF_INET6)
        rc = getsockopt(conn->fd, IPPROTO_IPV6, IPV6_MTU,
#ifdef _WIN32
                        (char *)&mtu,
#else
                        &mtu,
#endif
                        &mtu_len);
#endif
    if (rc == 0 && mtu > 0) {
        info->path_mtu = (size_t)mtu;
        if ((size_t)mtu > overhead) {
            info->path_max_payload = (size_t)mtu - overhead;
            if (info->path_max_payload > info->protocol_max_payload)
                info->path_max_payload = info->protocol_max_payload;
        }
    } else {
        info->path_max_payload = info->protocol_max_payload;
    }
    return 0;
}

static int udp_set_socket_timeout(nc_sock_t fd, int option, int ms) {
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

int neverc_udp_set_read_timeout(neverc_udp_conn_t *conn, int ms) {
    if (!conn) return -1;
    int rc = udp_set_socket_timeout(conn->fd, SO_RCVTIMEO, ms);
    if (rc == 0) {
        conn->read_timeout_ms = ms;
        conn->read_deadline_ms = 0;
    }
    return rc;
}

int neverc_udp_set_write_timeout(neverc_udp_conn_t *conn, int ms) {
    if (!conn) return -1;
    int rc = udp_set_socket_timeout(conn->fd, SO_SNDTIMEO, ms);
    if (rc == 0) {
        conn->write_timeout_ms = ms;
        conn->write_deadline_ms = 0;
    }
    return rc;
}

static int64_t udp_realtime_ms(void) {
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

static int udp_deadline_timeout(int64_t deadline_ms) {
    if (deadline_ms < 0) return -1;
    if (deadline_ms == 0) return 0;
    int64_t now = udp_realtime_ms();
    if (now < 0) return -1;
    int64_t remaining = deadline_ms - now;
    if (remaining <= 0) return 1;
    return remaining > INT_MAX ? INT_MAX : (int)remaining;
}

static int udp_deadline_expired(int64_t deadline_ms) {
    if (deadline_ms <= 0) return 0;
    int64_t now = udp_realtime_ms();
    return now >= 0 && now >= deadline_ms;
}

static int udp_refresh_deadline(neverc_udp_conn_t *conn,
                                int64_t deadline_ms, int option) {
    if (deadline_ms == 0) return 0;
    if (udp_deadline_expired(deadline_ms)) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        return -1;
    }
    int timeout = udp_deadline_timeout(deadline_ms);
    return timeout <= 0
               ? -1
               : udp_set_socket_timeout(conn->fd, option, timeout);
}

static int udp_refresh_read_deadline(neverc_udp_conn_t *conn) {
    return udp_refresh_deadline(conn, conn->read_deadline_ms,
                                SO_RCVTIMEO);
}

static int udp_refresh_write_deadline(neverc_udp_conn_t *conn) {
    return udp_refresh_deadline(conn, conn->write_deadline_ms,
                                SO_SNDTIMEO);
}

int neverc_udp_set_read_deadline(neverc_udp_conn_t *conn,
                                  int64_t deadline_ms) {
    if (!conn) return -1;
    int timeout = udp_deadline_timeout(deadline_ms);
    if (timeout < 0 ||
        udp_set_socket_timeout(conn->fd, SO_RCVTIMEO, timeout) != 0)
        return -1;
    conn->read_timeout_ms = 0;
    conn->read_deadline_ms = deadline_ms;
    return 0;
}

int neverc_udp_set_write_deadline(neverc_udp_conn_t *conn,
                                   int64_t deadline_ms) {
    if (!conn) return -1;
    int timeout = udp_deadline_timeout(deadline_ms);
    if (timeout < 0 ||
        udp_set_socket_timeout(conn->fd, SO_SNDTIMEO, timeout) != 0)
        return -1;
    conn->write_timeout_ms = 0;
    conn->write_deadline_ms = deadline_ms;
    return 0;
}

int neverc_udp_set_timeout(neverc_udp_conn_t *conn, int ms) {
    if (!conn || ms < 0) return -1;
    int read_rc = neverc_udp_set_read_timeout(conn, ms);
    int write_rc = neverc_udp_set_write_timeout(conn, ms);
    return read_rc == 0 && write_rc == 0 ? 0 : -1;
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
    hints.ai_family = AF_UNSPEC;
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
