#include "neverc/std/net/tcp.h"
#include "../_net_internal.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#endif

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static neverc_net_result_t tcp_context_result(neverc_net_status_t status,
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

static neverc_net_status_t tcp_context_status(neverc_context_t *ctx) {
    if (!ctx || !neverc_context_done(ctx))
        return NEVERC_NET_OK;
    const char *error = neverc_context_err(ctx);
    if (error && strstr(error, "deadline") != NULL)
        return NEVERC_NET_TIMEOUT;
    return NEVERC_NET_CANCELLED;
}

static void tcp_context_pause(void) {
#ifdef _WIN32
    Sleep(5);
#else
    struct timespec interval;
    interval.tv_sec = 0;
    interval.tv_nsec = 5 * 1000 * 1000;
    while (nanosleep(&interval, &interval) != 0 && errno == EINTR) {
    }
#endif
}

static int tcp_connect_wait(nc_sock_t fd, int timeout_ms) {
#ifdef _WIN32
    fd_set writefds;
    fd_set exceptfds;
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(fd, &writefds);
    FD_SET(fd, &exceptfds);
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    int rc = select(0, NULL, &writefds, &exceptfds, &timeout);
    return rc == SOCKET_ERROR ? -1 : (rc > 0 ? 1 : 0);
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    return rc < 0 ? -1 : (rc > 0 ? 1 : 0);
#endif
}

static int tcp_connect_in_progress(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
           error == WSAEALREADY;
#else
    return error == EINPROGRESS || error == EALREADY ||
           error == EWOULDBLOCK;
#endif
}

#define TCP_RESOLVER_MAX_WORKERS 32

typedef struct {
    char host[256];
    char service[8];
    struct addrinfo *result;
    int resolve_error;
    atomic_int done;
    atomic_int references;
} tcp_resolver_job_t;

static atomic_int tcp_resolver_workers = 0;

static void tcp_resolver_job_release(tcp_resolver_job_t *job) {
    if (!job) return;
    if (atomic_fetch_sub_explicit(
            &job->references, 1, memory_order_acq_rel) == 1) {
        if (job->result) freeaddrinfo(job->result);
        free(job);
    }
}

#ifdef _WIN32
static DWORD WINAPI tcp_resolver_worker(LPVOID argument) {
#else
static void *tcp_resolver_worker(void *argument) {
#endif
    tcp_resolver_job_t *job = (tcp_resolver_job_t *)argument;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
#ifdef AI_NUMERICSERV
    hints.ai_flags = AI_NUMERICSERV;
#endif
    job->resolve_error = getaddrinfo(
        job->host, job->service, &hints, &job->result);
    atomic_store_explicit(&job->done, 1, memory_order_release);
    atomic_fetch_sub_explicit(
        &tcp_resolver_workers, 1, memory_order_acq_rel);
    tcp_resolver_job_release(job);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int tcp_resolver_start(tcp_resolver_job_t *job) {
    int workers = atomic_fetch_add_explicit(
        &tcp_resolver_workers, 1, memory_order_acq_rel);
    if (workers >= TCP_RESOLVER_MAX_WORKERS) {
        atomic_fetch_sub_explicit(
            &tcp_resolver_workers, 1, memory_order_acq_rel);
        return -1;
    }

    atomic_fetch_add_explicit(
        &job->references, 1, memory_order_relaxed);
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, tcp_resolver_worker, job, 0, NULL);
    if (!thread) {
        atomic_fetch_sub_explicit(
            &tcp_resolver_workers, 1, memory_order_acq_rel);
        tcp_resolver_job_release(job);
        return -1;
    }
    (void)CloseHandle(thread);
#else
    pthread_t thread;
    if (pthread_create(
            &thread, NULL, tcp_resolver_worker, job) != 0) {
        atomic_fetch_sub_explicit(
            &tcp_resolver_workers, 1, memory_order_acq_rel);
        tcp_resolver_job_release(job);
        return -1;
    }
    (void)pthread_detach(thread);
#endif
    return 0;
}

neverc_net_result_t neverc_tcp_dial_context(const char *addr,
                                             neverc_context_t *ctx,
                                             neverc_tcp_conn_t **conn_out) {
    if (conn_out) *conn_out = NULL;
    if (!addr || !conn_out)
        return tcp_context_result(NEVERC_NET_INVALID, 0, "dial", 0);

    neverc_net_status_t context_status = tcp_context_status(ctx);
    if (context_status != NEVERC_NET_OK)
        return tcp_context_result(context_status, 0, "dial", 0);
    if (nc_net_init() != 0)
        return tcp_context_result(NEVERC_NET_SYSTEM, nc_sock_errno,
                                  "dial", 0);

    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0 ||
        host[0] == '\0')
        return tcp_context_result(NEVERC_NET_INVALID, 0, "dial", 0);

    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", port);
    hints.ai_flags = AI_NUMERICHOST;
#ifdef AI_NUMERICSERV
    hints.ai_flags |= AI_NUMERICSERV;
#endif
    int resolve_error = getaddrinfo(host, port_text, &hints, &result);
    if (resolve_error != 0 && ctx) {
        tcp_resolver_job_t *job =
            (tcp_resolver_job_t *)calloc(1, sizeof(*job));
        if (!job)
            return tcp_context_result(
                NEVERC_NET_NOMEM, 0, "dial", 0);
        memcpy(job->host, host, strlen(host) + 1U);
        memcpy(job->service, port_text, strlen(port_text) + 1U);
        atomic_init(&job->done, 0);
        atomic_init(&job->references, 1);
        if (tcp_resolver_start(job) != 0) {
            tcp_resolver_job_release(job);
            return tcp_context_result(
                NEVERC_NET_SYSTEM, 0, "dial", 0);
        }
        while (!atomic_load_explicit(
                   &job->done, memory_order_acquire)) {
            context_status = tcp_context_status(ctx);
            if (context_status != NEVERC_NET_OK) {
                tcp_resolver_job_release(job);
                return tcp_context_result(
                    context_status, 0, "dial", 0);
            }
            tcp_context_pause();
        }
        context_status = tcp_context_status(ctx);
        if (context_status != NEVERC_NET_OK) {
            tcp_resolver_job_release(job);
            return tcp_context_result(
                context_status, 0, "dial", 0);
        }
        result = job->result;
        job->result = NULL;
        resolve_error = job->resolve_error;
        tcp_resolver_job_release(job);
    } else if (resolve_error != 0) {
        hints.ai_flags = 0;
#ifdef AI_NUMERICSERV
        hints.ai_flags = AI_NUMERICSERV;
#endif
        resolve_error =
            getaddrinfo(host, port_text, &hints, &result);
    }
    if (resolve_error != 0) {
        if (result) freeaddrinfo(result);
        return tcp_context_result(NEVERC_NET_RESOLVE, resolve_error,
                                  "dial", 0);
    }

    int last_error = 0;
    for (struct addrinfo *candidate = result; candidate;
         candidate = candidate->ai_next) {
        context_status = tcp_context_status(ctx);
        if (context_status != NEVERC_NET_OK)
            break;

        nc_sock_t fd = socket(candidate->ai_family, candidate->ai_socktype,
                              candidate->ai_protocol);
        if (fd == NC_INVALID_SOCK) {
            last_error = nc_sock_errno;
            continue;
        }
        if (nc_set_nonblocking(fd) != 0) {
            last_error = nc_sock_errno;
            nc_sock_close(fd);
            continue;
        }

        int connected =
            connect(fd, candidate->ai_addr,
                    (int)candidate->ai_addrlen) != NC_SOCK_ERR;
        if (!connected) {
            last_error = nc_sock_errno;
            if (tcp_connect_in_progress(last_error)) {
                for (;;) {
                    context_status = tcp_context_status(ctx);
                    if (context_status != NEVERC_NET_OK)
                        break;
                    int wait_result = tcp_connect_wait(fd, 5);
                    if (wait_result < 0) {
                        last_error = nc_sock_errno;
                        break;
                    }
                    if (wait_result == 0)
                        continue;

                    int socket_error = 0;
                    socklen_t error_len = sizeof(socket_error);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
#ifdef _WIN32
                                   (char *)&socket_error,
#else
                                   &socket_error,
#endif
                                   &error_len) != 0) {
                        last_error = nc_sock_errno;
                    } else {
                        last_error = socket_error;
                        connected = socket_error == 0;
                    }
                    break;
                }
            }
        }

        if (connected) {
            context_status = tcp_context_status(ctx);
            if (context_status != NEVERC_NET_OK) {
                nc_sock_close(fd);
                break;
            }
            const char *adopt_error = NULL;
            neverc_tcp_conn_t *conn = neverc_tcp_adopt_handle(
                (uintptr_t)fd, NULL, 0, &adopt_error);
            if (conn) {
                *conn_out = conn;
                freeaddrinfo(result);
                return tcp_context_result(NEVERC_NET_OK, 0, "dial", 0);
            }
            (void)adopt_error;
            nc_sock_close(fd);
            freeaddrinfo(result);
            return tcp_context_result(NEVERC_NET_NOMEM, 0, "dial", 0);
        }
        nc_sock_close(fd);
        if (context_status != NEVERC_NET_OK)
            break;
    }

    freeaddrinfo(result);
    if (context_status != NEVERC_NET_OK)
        return tcp_context_result(context_status, 0, "dial", 0);
    return tcp_context_result(NEVERC_NET_SYSTEM, last_error, "dial", 0);
}

neverc_net_result_t neverc_tcp_accept_context(neverc_tcp_listener_t *ln,
                                               neverc_context_t *ctx,
                                               neverc_tcp_conn_t **conn_out) {
    if (conn_out) *conn_out = NULL;
    if (!ln || !conn_out)
        return tcp_context_result(NEVERC_NET_INVALID, 0, "accept", 0);

    for (;;) {
        neverc_net_status_t context_status = tcp_context_status(ctx);
        if (context_status != NEVERC_NET_OK)
            return tcp_context_result(context_status, 0, "accept", 0);

        neverc_net_result_t result =
            neverc_tcp_try_accept(ln, conn_out);
        if (result.status == NEVERC_NET_OK) {
            /*
             * A worker may be descheduled after the pre-accept context
             * check. Do not return a connection accepted after cancellation
             * or the deadline merely because it became ready in that gap.
             */
            context_status = tcp_context_status(ctx);
            if (context_status != NEVERC_NET_OK) {
                neverc_tcp_close(*conn_out);
                *conn_out = NULL;
                return tcp_context_result(
                    context_status, 0, "accept", 0);
            }
        }
        if (result.status != NEVERC_NET_WOULD_BLOCK)
            return result;
        tcp_context_pause();
    }
}

neverc_net_result_t neverc_tcp_read_context(neverc_tcp_conn_t *conn,
                                             neverc_context_t *ctx,
                                             void *buf, size_t buflen) {
    if (!conn || (!buf && buflen > 0))
        return tcp_context_result(NEVERC_NET_INVALID, 0, "read", 0);

    for (;;) {
        neverc_net_status_t context_status = tcp_context_status(ctx);
        if (context_status != NEVERC_NET_OK)
            return tcp_context_result(context_status, 0, "read", 0);

        neverc_net_result_t result =
            neverc_tcp_try_read(conn, buf, buflen);
        if (result.status != NEVERC_NET_WOULD_BLOCK)
            return result;
        tcp_context_pause();
    }
}

neverc_net_result_t neverc_tcp_write_context(neverc_tcp_conn_t *conn,
                                              neverc_context_t *ctx,
                                              const void *data, size_t len) {
    if (!conn || (!data && len > 0))
        return tcp_context_result(NEVERC_NET_INVALID, 0, "write", 0);

    size_t total = 0;
    while (total < len) {
        neverc_net_status_t context_status = tcp_context_status(ctx);
        if (context_status != NEVERC_NET_OK)
            return tcp_context_result(context_status, 0, "write", total);

        neverc_net_result_t result =
            neverc_tcp_try_write(conn, (const char *)data + total,
                                 len - total);
        if (result.status == NEVERC_NET_OK) {
            if (result.transferred == 0)
                return tcp_context_result(NEVERC_NET_CLOSED, 0,
                                          "write", total);
            total += result.transferred;
            continue;
        }
        if (result.status != NEVERC_NET_WOULD_BLOCK) {
            result.transferred += total;
            return result;
        }
        tcp_context_pause();
    }
    return tcp_context_result(NEVERC_NET_OK, 0, "write", total);
}
