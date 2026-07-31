#ifndef NEVERC_NET_IO_H
#define NEVERC_NET_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_NET_INVALID = -1,
    NEVERC_NET_SYSTEM = -2,
    NEVERC_NET_NOMEM = -3,
    NEVERC_NET_RESOLVE = -4,
    NEVERC_NET_OK = 0,
    NEVERC_NET_EOF = 1,
    NEVERC_NET_WOULD_BLOCK = 2,
    NEVERC_NET_TIMEOUT = 3,
    NEVERC_NET_CANCELLED = 4,
    NEVERC_NET_CLOSED = 5,
    NEVERC_NET_TRUNCATED = 6
} neverc_net_status_t;

#define NEVERC_NET_INVALID_HANDLE UINTPTR_MAX

/*
 * Allocation-free operation result. operation points to a static string and
 * remains valid for the process lifetime. system_code is errno/WSAGetLastError
 * when available; context-originated cancellation/deadline results use zero.
 */
typedef struct {
    neverc_net_status_t status;
    int                 system_code;
    const char         *operation;
    size_t              transferred;
} neverc_net_result_t;

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_IO_H */
