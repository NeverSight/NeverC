#include "_net_iocp.h"
#include "_net_poller.h"
#include "_net_socket.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32

static int wait_for_operation(nc_poller_t *poller, nc_iocp_op_t *operation,
                              void *data, int events, size_t transferred,
                              int expected_error) {
    uint64_t deadline = nc_monotonic_ms() + 2000U;
    while (nc_monotonic_ms() < deadline) {
        nc_event_t completed[4];
        int count = nc_poller_wait(poller, completed, 4, 50);
        if (count < 0) return -1;
        for (int i = 0; i < count; i++) {
            if (completed[i].operation != operation) continue;
            if (completed[i].data != data ||
                (completed[i].events & events) != events ||
                completed[i].transferred != transferred ||
                completed[i].error != expected_error)
                return -1;
            return 0;
        }
    }
    return -1;
}

static int wait_for_readiness(nc_poller_t *poller, nc_sock_t fd,
                              void *data, int events) {
    uint64_t deadline = nc_monotonic_ms() + 2000U;
    while (nc_monotonic_ms() < deadline) {
        nc_event_t ready[4];
        int count = nc_poller_wait(poller, ready, 4, 50);
        if (count < 0) return -1;
        for (int i = 0; i < count; i++)
            if (!ready[i].operation && ready[i].fd == fd &&
                ready[i].data == data &&
                (ready[i].events & events) == events)
                return 0;
    }
    return -1;
}

static int bind_listener(nc_sock_t *listener, struct sockaddr_in *bound) {
    *listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                           WSA_FLAG_OVERLAPPED);
    if (*listener == NC_INVALID_SOCK) return -1;

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(*listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(*listener, 16) != 0) {
        nc_sock_close(*listener);
        *listener = NC_INVALID_SOCK;
        return -1;
    }

    int length = (int)sizeof(*bound);
    if (getsockname(*listener, (struct sockaddr *)bound, &length) != 0) {
        nc_sock_close(*listener);
        *listener = NC_INVALID_SOCK;
        return -1;
    }
    return 0;
}

int main(void) {
    if (nc_net_init() != 0) return 1;
    if (!nc_poller_supports_completions() ||
        !nc_poller_supports_readiness())
        return 2;

    nc_sock_t listener = NC_INVALID_SOCK;
    nc_sock_t client = NC_INVALID_SOCK;
    nc_sock_t accepted = NC_INVALID_SOCK;
    nc_poller_t *poller = NULL;
    nc_iocp_acceptor_t acceptor;
    nc_iocp_op_t accept_op;
    nc_iocp_op_t recv_op;
    nc_iocp_op_t send_op;
    nc_iocp_op_t cancel_op;
    int accept_tag;
    int recv_tag;
    int send_tag;
    int cancel_tag;
    int result = 3;

    struct sockaddr_in bound;
    memset(&bound, 0, sizeof(bound));
    if (bind_listener(&listener, &bound) != 0) goto done;

    poller = nc_poller_create();
    if (!poller ||
        nc_poller_associate_completion(poller, listener, NULL) != 0 ||
        nc_poller_add(poller, listener, NC_EV_READ, NULL) != 0)
        goto done;

    nc_iocp_op_init(&accept_op);
    if (nc_iocp_acceptor_init(&acceptor, listener) != 0 ||
        nc_iocp_accept_start(&acceptor, &accept_op, &accept_tag) != 0)
        goto done;

    client = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                        WSA_FLAG_OVERLAPPED);
    if (client == NC_INVALID_SOCK ||
        connect(client, (struct sockaddr *)&bound, sizeof(bound)) != 0)
        goto done;

    if (wait_for_operation(poller, &accept_op, &accept_tag, NC_EV_READ,
                           0, 0) != 0)
        goto done;
    nc_iocp_op_complete(&accept_op, 0);
    if (nc_iocp_op_state(&accept_op) != NC_IOCP_OP_COMPLETED)
        goto done;
    accepted = nc_iocp_accept_take(&accept_op);
    if (accepted == NC_INVALID_SOCK ||
        nc_iocp_accept_take(&accept_op) != NC_INVALID_SOCK ||
        nc_poller_associate_completion(poller, accepted, NULL) != 0 ||
        nc_poller_add(poller, accepted, NC_EV_READ | NC_EV_WRITE, NULL) != 0)
        goto done;

    char input[16] = {0};
    nc_iocp_op_init(&recv_op);
    if (nc_iocp_recv_start(accepted, &recv_op, input, sizeof(input),
                           &recv_tag) != 0 ||
        nc_iocp_op_state(&recv_op) != NC_IOCP_OP_PENDING ||
        nc_iocp_op_cleanup(&recv_op) == 0 ||
        send(client, "ping", 4, 0) != 4 ||
        wait_for_operation(poller, &recv_op, &recv_tag, NC_EV_READ,
                           4, 0) != 0 ||
        nc_iocp_op_state(&recv_op) != NC_IOCP_OP_COMPLETED ||
        memcmp(input, "ping", 4) != 0)
        goto done;
    nc_iocp_op_complete(&recv_op, 4);
    if (nc_iocp_op_state(&recv_op) != NC_IOCP_OP_COMPLETED ||
        recv_op.accepted_fd != NC_INVALID_SOCK)
        goto done;

    nc_iocp_op_init(&send_op);
    if (nc_iocp_send_start(accepted, &send_op, "pong", 4, &send_tag) != 0 ||
        wait_for_operation(poller, &send_op, &send_tag, NC_EV_WRITE,
                           4, 0) != 0)
        goto done;

    char output[4];
    if (recv(client, output, sizeof(output), MSG_WAITALL) != 4 ||
        memcmp(output, "pong", 4) != 0)
        goto done;

    int readiness_tag;
    char readiness_byte;
    if (nc_poller_mod(poller, accepted, NC_EV_READ, &readiness_tag) != 0 ||
        send(client, "r", 1, 0) != 1 ||
        wait_for_readiness(poller, accepted, &readiness_tag, NC_EV_READ) != 0 ||
        recv(accepted, &readiness_byte, 1, 0) != 1 ||
        readiness_byte != 'r')
        goto done;

    char cancelled_buffer[1];
    nc_iocp_op_init(&cancel_op);
    if (nc_iocp_recv_start(accepted, &cancel_op, cancelled_buffer,
                           sizeof(cancelled_buffer), &cancel_tag) != 0 ||
        nc_iocp_cancel(accepted, &cancel_op) != 0 ||
        wait_for_operation(poller, &cancel_op, &cancel_tag,
                           NC_EV_READ | NC_EV_ERROR, 0,
                           WSA_OPERATION_ABORTED) != 0)
        goto done;

    if (nc_iocp_op_cleanup(&accept_op) != 0 ||
        nc_iocp_op_cleanup(&recv_op) != 0 ||
        nc_iocp_op_cleanup(&send_op) != 0 ||
        nc_iocp_op_cleanup(&cancel_op) != 0)
        goto done;

    result = 0;

done:
    if (accepted != NC_INVALID_SOCK) nc_sock_close(accepted);
    if (client != NC_INVALID_SOCK) nc_sock_close(client);
    if (listener != NC_INVALID_SOCK) nc_sock_close(listener);
    nc_poller_destroy(poller);
    if (result == 0) puts("passed");
    return result;
}

#else

int main(void) {
    if (nc_poller_supports_completions() ||
        !nc_poller_supports_readiness())
        return 1;
    puts("passed");
    return 0;
}

#endif
