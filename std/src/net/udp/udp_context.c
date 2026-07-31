#include "neverc/std/net/udp.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

#include <string.h>

static neverc_net_result_t udp_context_result(neverc_net_status_t status,
                                              const char *operation) {
    neverc_net_result_t result;
    result.status = status;
    result.system_code = 0;
    result.operation = operation;
    result.transferred = 0;
    return result;
}

static neverc_net_status_t udp_context_status(neverc_context_t *ctx) {
    if (!ctx || !neverc_context_done(ctx))
        return NEVERC_NET_OK;
    const char *error = neverc_context_err(ctx);
    if (error && strstr(error, "deadline") != NULL)
        return NEVERC_NET_TIMEOUT;
    return NEVERC_NET_CANCELLED;
}

static void udp_context_pause(void) {
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

neverc_net_result_t neverc_udp_read_packet_context(
    neverc_udp_conn_t *conn, neverc_context_t *ctx,
    void *buf, size_t buflen, neverc_udp_packet_info_t *info) {
    if (!conn || !info || (!buf && buflen > 0))
        return udp_context_result(NEVERC_NET_INVALID, "read");

    for (;;) {
        neverc_net_status_t context_status = udp_context_status(ctx);
        if (context_status != NEVERC_NET_OK)
            return udp_context_result(context_status, "read");

        neverc_net_result_t result =
            neverc_udp_try_read_packet(conn, buf, buflen, info);
        if (result.status != NEVERC_NET_WOULD_BLOCK)
            return result;
        udp_context_pause();
    }
}

neverc_net_result_t neverc_udp_write_context(
    neverc_udp_conn_t *conn, neverc_context_t *ctx,
    const void *data, size_t len,
    const neverc_udp_addr_t *destination) {
    if (!conn || (!data && len > 0))
        return udp_context_result(NEVERC_NET_INVALID, "write");

    for (;;) {
        neverc_net_status_t context_status = udp_context_status(ctx);
        if (context_status != NEVERC_NET_OK)
            return udp_context_result(context_status, "write");

        neverc_net_result_t result =
            neverc_udp_try_write(conn, data, len, destination);
        if (result.status != NEVERC_NET_WOULD_BLOCK)
            return result;
        udp_context_pause();
    }
}
