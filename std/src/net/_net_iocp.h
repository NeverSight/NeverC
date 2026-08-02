#ifndef NEVERC_NET_IOCP_H
#define NEVERC_NET_IOCP_H

#include "_net_platform.h"

#if defined(NC_USE_IOCP)

#define NC_HAS_IOCP_COMPLETIONS 1
#define NC_IOCP_OP_IDLE 0
#define NC_IOCP_OP_PENDING 1
#define NC_IOCP_OP_COMPLETED 2
#define NC_IOCP_ACCEPT_ADDRESS_BYTES \
    (sizeof(struct sockaddr_storage) + 16)

#ifndef SO_UPDATE_ACCEPT_CONTEXT
#define SO_UPDATE_ACCEPT_CONTEXT 0x700B
#endif

typedef enum {
    NC_IOCP_OP_NONE = 0,
    NC_IOCP_OP_ACCEPT,
    NC_IOCP_OP_RECV,
    NC_IOCP_OP_SEND
} nc_iocp_op_kind_t;

#if defined(_M_IX86) || defined(__i386__)
#define NC_IOCP_ACCEPTEX_CALL WSAAPI
#else
#define NC_IOCP_ACCEPTEX_CALL
#endif

typedef BOOL(NC_IOCP_ACCEPTEX_CALL *nc_acceptex_fn_t)(
    SOCKET listen_socket, SOCKET accept_socket, PVOID output_buffer,
    DWORD receive_data_length, DWORD local_address_length,
    DWORD remote_address_length, LPDWORD bytes_received,
    LPOVERLAPPED overlapped);

#undef NC_IOCP_ACCEPTEX_CALL

typedef struct {
    nc_sock_t listener;
    int family;
    nc_acceptex_fn_t accept_ex;
} nc_iocp_acceptor_t;

/*
 * OVERLAPPED must remain the first field: completion packets only carry its
 * address, so the poller recovers the owning operation with a plain cast.
 * Callers own the operation and every referenced buffer until completion.
 * The socket must first be associated with the destination completion port
 * through nc_poller_associate_completion(). After cancellation, callers must
 * still dequeue the completion before cleaning up the operation or its buffer.
 */
typedef struct nc_iocp_op {
    OVERLAPPED overlapped;
    volatile LONG state;
    nc_iocp_op_kind_t kind;
    nc_sock_t fd;
    nc_sock_t accepted_fd;
    WSABUF buffer;
    DWORD flags;
    DWORD transferred;
    int error;
    void *data;
    unsigned char accept_addresses[2 * NC_IOCP_ACCEPT_ADDRESS_BYTES];
} nc_iocp_op_t;

_Static_assert(offsetof(nc_iocp_op_t, overlapped) == 0,
               "IOCP operation must begin with OVERLAPPED");

static inline void nc_iocp_op_init(nc_iocp_op_t *operation) {
    if (!operation) return;
    memset(operation, 0, sizeof(*operation));
    operation->fd = NC_INVALID_SOCK;
    operation->accepted_fd = NC_INVALID_SOCK;
}

static inline int nc_iocp_op_state(const nc_iocp_op_t *operation) {
    if (!operation) return NC_IOCP_OP_IDLE;
    return (int)InterlockedCompareExchange(
        (volatile LONG *)&operation->state, 0, 0);
}

static inline int nc_iocp_op_cleanup(nc_iocp_op_t *operation) {
    if (!operation) return 0;
    if (nc_iocp_op_state(operation) == NC_IOCP_OP_PENDING) {
        WSASetLastError(WSA_IO_PENDING);
        return -1;
    }
    if (operation->accepted_fd != NC_INVALID_SOCK)
        nc_sock_close(operation->accepted_fd);
    nc_iocp_op_init(operation);
    return 0;
}

static inline int nc_iocp_op_begin(nc_iocp_op_t *operation,
                                   nc_iocp_op_kind_t kind,
                                   nc_sock_t fd, void *data) {
    if (!operation || fd == NC_INVALID_SOCK ||
        InterlockedCompareExchange(&operation->state, NC_IOCP_OP_PENDING,
                                   NC_IOCP_OP_IDLE) != NC_IOCP_OP_IDLE) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }

    memset(&operation->overlapped, 0, sizeof(operation->overlapped));
    operation->kind = kind;
    operation->fd = fd;
    operation->accepted_fd = NC_INVALID_SOCK;
    operation->buffer.buf = NULL;
    operation->buffer.len = 0;
    operation->flags = 0;
    operation->transferred = 0;
    operation->error = 0;
    operation->data = data;
    return 0;
}

static inline int nc_iocp_op_start_failed(nc_iocp_op_t *operation,
                                          int error) {
    if (operation->accepted_fd != NC_INVALID_SOCK) {
        nc_sock_close(operation->accepted_fd);
        operation->accepted_fd = NC_INVALID_SOCK;
    }
    operation->error = error;
    InterlockedExchange(&operation->state, NC_IOCP_OP_IDLE);
    WSASetLastError(error);
    return -1;
}

static inline int nc_iocp_get_acceptex(
    nc_sock_t listener, nc_acceptex_fn_t *accept_ex) {
    static const GUID accept_ex_guid = {
        0xb5367df1, 0xcbac, 0x11cf,
        {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}};
    DWORD bytes = 0;
    *accept_ex = NULL;
    if (WSAIoctl(listener, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 (void *)&accept_ex_guid, sizeof(accept_ex_guid),
                 accept_ex, sizeof(*accept_ex),
                 &bytes, NULL, NULL) != 0 ||
        !*accept_ex)
        return -1;
    return 0;
}

static inline int nc_iocp_acceptor_init(
    nc_iocp_acceptor_t *acceptor, nc_sock_t listener) {
    if (!acceptor || listener == NC_INVALID_SOCK) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }

    struct sockaddr_storage local_address;
    int local_length = (int)sizeof(local_address);
    memset(&local_address, 0, sizeof(local_address));
    if (getsockname(listener, (struct sockaddr *)&local_address,
                    &local_length) != 0)
        return -1;
    if (local_address.ss_family != AF_INET &&
        local_address.ss_family != AF_INET6) {
        WSASetLastError(WSAEAFNOSUPPORT);
        return -1;
    }

    nc_acceptex_fn_t accept_ex;
    if (nc_iocp_get_acceptex(listener, &accept_ex) != 0)
        return -1;
    acceptor->listener = listener;
    acceptor->family = local_address.ss_family;
    acceptor->accept_ex = accept_ex;
    return 0;
}

static inline int nc_iocp_accept_start(
    nc_iocp_acceptor_t *acceptor, nc_iocp_op_t *operation, void *data) {
    if (!acceptor || !acceptor->accept_ex ||
        acceptor->listener == NC_INVALID_SOCK) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    if (nc_iocp_op_begin(operation, NC_IOCP_OP_ACCEPT,
                         acceptor->listener, data) != 0)
        return -1;

    DWORD socket_flags = WSA_FLAG_OVERLAPPED;
#ifdef WSA_FLAG_NO_HANDLE_INHERIT
    socket_flags |= WSA_FLAG_NO_HANDLE_INHERIT;
#endif
    operation->accepted_fd = WSASocketW(
        acceptor->family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
        socket_flags);
    if (operation->accepted_fd == NC_INVALID_SOCK)
        return nc_iocp_op_start_failed(operation, WSAGetLastError());

    DWORD received = 0;
    BOOL accepted = acceptor->accept_ex(
        acceptor->listener, operation->accepted_fd,
        operation->accept_addresses, 0,
        (DWORD)NC_IOCP_ACCEPT_ADDRESS_BYTES,
        (DWORD)NC_IOCP_ACCEPT_ADDRESS_BYTES, &received,
        &operation->overlapped);
    if (!accepted) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
            return nc_iocp_op_start_failed(operation, error);
    }
    return 0;
}

static inline int nc_iocp_recv_start(nc_sock_t fd,
                                     nc_iocp_op_t *operation,
                                     void *buffer, size_t length,
                                     void *data) {
    if (!buffer || length == 0 || length > ULONG_MAX) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    if (nc_iocp_op_begin(operation, NC_IOCP_OP_RECV, fd, data) != 0)
        return -1;

    operation->buffer.buf = (char *)buffer;
    operation->buffer.len = (ULONG)length;
    DWORD received = 0;
    int result = WSARecv(fd, &operation->buffer, 1, &received,
                         &operation->flags, &operation->overlapped, NULL);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
            return nc_iocp_op_start_failed(operation, error);
    }
    return 0;
}

static inline int nc_iocp_send_start(nc_sock_t fd,
                                     nc_iocp_op_t *operation,
                                     const void *buffer, size_t length,
                                     void *data) {
    if (!buffer || length == 0 || length > ULONG_MAX) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    if (nc_iocp_op_begin(operation, NC_IOCP_OP_SEND, fd, data) != 0)
        return -1;

    operation->buffer.buf = (char *)buffer;
    operation->buffer.len = (ULONG)length;
    DWORD sent = 0;
    int result = WSASend(fd, &operation->buffer, 1, &sent, 0,
                         &operation->overlapped, NULL);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
            return nc_iocp_op_start_failed(operation, error);
    }
    return 0;
}

static inline int nc_iocp_cancel(nc_sock_t fd,
                                 nc_iocp_op_t *operation) {
    if (!operation || operation->fd != fd ||
        nc_iocp_op_state(operation) != NC_IOCP_OP_PENDING) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    if (CancelIoEx((HANDLE)fd, &operation->overlapped))
        return 0;
    DWORD error = GetLastError();
    if (error == ERROR_NOT_FOUND)
        return 0;
    WSASetLastError((int)error);
    return -1;
}

static inline void nc_iocp_op_complete(nc_iocp_op_t *operation,
                                       DWORD queued_bytes) {
    DWORD transferred = queued_bytes;
    DWORD flags = 0;
    int error = 0;

    if (!WSAGetOverlappedResult(operation->fd, &operation->overlapped,
                                &transferred, FALSE, &flags))
        error = WSAGetLastError();

    if (error == 0 && operation->kind == NC_IOCP_OP_ACCEPT &&
        setsockopt(operation->accepted_fd, SOL_SOCKET,
                   SO_UPDATE_ACCEPT_CONTEXT, (const char *)&operation->fd,
                   sizeof(operation->fd)) != 0)
        error = WSAGetLastError();

    if (error != 0 && operation->kind == NC_IOCP_OP_ACCEPT &&
        operation->accepted_fd != NC_INVALID_SOCK) {
        nc_sock_close(operation->accepted_fd);
        operation->accepted_fd = NC_INVALID_SOCK;
    }

    operation->transferred = transferred;
    operation->flags = flags;
    operation->error = error;
    InterlockedExchange(&operation->state, NC_IOCP_OP_COMPLETED);
}

static inline nc_sock_t nc_iocp_accept_take(
    nc_iocp_op_t *operation) {
    if (!operation || operation->kind != NC_IOCP_OP_ACCEPT ||
        nc_iocp_op_state(operation) != NC_IOCP_OP_COMPLETED ||
        operation->error != 0)
        return NC_INVALID_SOCK;
    nc_sock_t accepted = operation->accepted_fd;
    operation->accepted_fd = NC_INVALID_SOCK;
    return accepted;
}

#else

#define NC_HAS_IOCP_COMPLETIONS 0

#endif

#endif /* NEVERC_NET_IOCP_H */
