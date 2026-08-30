#ifndef NEVERC_NET_TCP_H
#define NEVERC_NET_TCP_H

/*
 * NeverC net/tcp — TCP client/server (mirrors Go net.Listen/Dial/Conn).
 *
 * Cross-platform: POSIX (mac/ios/linux/android) + WinSock (windows).
 * Go-style API:
 *   listener = neverc_tcp_listen(":8080");
 *   conn = neverc_tcp_accept(listener);
 *   neverc_tcp_write(conn, data, len);
 *   n = neverc_tcp_read(conn, buf, buflen);
 *   neverc_tcp_close(conn);
 */

#include <stddef.h>
#include <stdint.h>

#include "../context.h"
#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_tcp_listener neverc_tcp_listener_t;
typedef struct neverc_tcp_conn neverc_tcp_conn_t;

typedef struct {
    char     addr[64];     /* peer address "ip:port" */
    uint16_t port;
} neverc_tcp_addr_t;

/* --- Listen / Accept --- */

/* Listen on addr (e.g. ":8080", "0.0.0.0:3000", "127.0.0.1:9090").
 * Returns NULL on error; *errp set. */
neverc_tcp_listener_t *neverc_tcp_listen(const char *addr, const char **errp);

/* Accept a connection. Blocks until a client connects.
 * Returns NULL on error. */
neverc_tcp_conn_t *neverc_tcp_accept(neverc_tcp_listener_t *ln,
                                      const char **errp);

/* Non-blocking and context-aware accept variants. */
neverc_net_result_t neverc_tcp_try_accept(neverc_tcp_listener_t *ln,
                                           neverc_tcp_conn_t **conn_out);
neverc_net_result_t neverc_tcp_accept_context(neverc_tcp_listener_t *ln,
                                               neverc_context_t *ctx,
                                               neverc_tcp_conn_t **conn_out);

/* Close the listener. */
void neverc_tcp_listener_close(neverc_tcp_listener_t *ln);

/* Get the listener's local address. */
int neverc_tcp_listener_addr(neverc_tcp_listener_t *ln,
                              neverc_tcp_addr_t *addr);

/* Pointer-width-safe native listener handle for event-loop integration. */
uintptr_t neverc_tcp_listener_handle(neverc_tcp_listener_t *ln);

/* --- Dial --- */

/* Connect to addr (e.g. "127.0.0.1:8080", "example.com:80").
 * Returns NULL on error. */
neverc_tcp_conn_t *neverc_tcp_dial(const char *addr, const char **errp);

/* Connect with cancellation/deadline and a structured result. Hostname
 * resolution, address attempts, and socket connection all observe ctx. */
neverc_net_result_t neverc_tcp_dial_context(const char *addr,
                                             neverc_context_t *ctx,
                                             neverc_tcp_conn_t **conn_out);

/* Take ownership of an existing connected socket fd.
 * Optional preload bytes are returned by neverc_tcp_read before socket I/O. */
neverc_tcp_conn_t *neverc_tcp_adopt(int fd, const void *preload,
                                     size_t preload_len, const char **errp);

/* Pointer-width-safe socket adoption for Windows and event-loop adapters. */
neverc_tcp_conn_t *neverc_tcp_adopt_handle(uintptr_t socket_handle,
                                            const void *preload,
                                            size_t preload_len,
                                            const char **errp);

/* --- Connection I/O --- */

/* Write data. Returns bytes written or -1 on error. */
int neverc_tcp_write(neverc_tcp_conn_t *conn, const void *data, size_t len);

/* Read data. Returns bytes read, 0 on EOF, -1 on error. */
int neverc_tcp_read(neverc_tcp_conn_t *conn, void *buf, size_t buflen);

/* Single-attempt I/O for integration with an external event loop. */
neverc_net_result_t neverc_tcp_try_read(neverc_tcp_conn_t *conn,
                                        void *buf, size_t buflen);
neverc_net_result_t neverc_tcp_try_write(neverc_tcp_conn_t *conn,
                                         const void *data, size_t len);

/* Blocking I/O interrupted by context cancellation or deadline expiry. */
neverc_net_result_t neverc_tcp_read_context(neverc_tcp_conn_t *conn,
                                             neverc_context_t *ctx,
                                             void *buf, size_t buflen);
neverc_net_result_t neverc_tcp_write_context(neverc_tcp_conn_t *conn,
                                              neverc_context_t *ctx,
                                              const void *data, size_t len);

/* Half-close one direction while keeping the other direction usable. */
int neverc_tcp_shutdown_read(neverc_tcp_conn_t *conn);
int neverc_tcp_shutdown_write(neverc_tcp_conn_t *conn);

/* Return the underlying socket fd (-1 on error). For advanced integrations. */
int neverc_tcp_conn_fd(neverc_tcp_conn_t *conn);

/* Pointer-width-safe native connection handle. */
uintptr_t neverc_tcp_conn_handle(neverc_tcp_conn_t *conn);

/* Close connection. */
void neverc_tcp_close(neverc_tcp_conn_t *conn);

/* Get remote address. */
int neverc_tcp_remote_addr(neverc_tcp_conn_t *conn, neverc_tcp_addr_t *addr);

/* Get local address. */
int neverc_tcp_local_addr(neverc_tcp_conn_t *conn, neverc_tcp_addr_t *addr);

/* Set read and write timeouts independently (0 = no timeout). */
int neverc_tcp_set_read_timeout(neverc_tcp_conn_t *conn, int ms);
int neverc_tcp_set_write_timeout(neverc_tcp_conn_t *conn, int ms);

/* Set absolute Unix-millisecond deadlines (0 clears the deadline). */
int neverc_tcp_set_read_deadline(neverc_tcp_conn_t *conn,
                                  int64_t deadline_ms);
int neverc_tcp_set_write_deadline(neverc_tcp_conn_t *conn,
                                   int64_t deadline_ms);

/* Compatibility helper that sets both read and write timeouts. */
int neverc_tcp_set_timeout(neverc_tcp_conn_t *conn, int ms);

/* Set TCP_NODELAY (disable Nagle). */
int neverc_tcp_set_nodelay(neverc_tcp_conn_t *conn, int enable);

/* Set SO_REUSEADDR on listener. */
int neverc_tcp_set_reuseaddr(neverc_tcp_listener_t *ln, int enable);

/* Set SO_KEEPALIVE on connection. */
int neverc_tcp_set_keepalive(neverc_tcp_conn_t *conn, int enable);

/* Set socket read buffer size. */
int neverc_tcp_set_read_buffer(neverc_tcp_conn_t *conn, int bytes);

/* Set socket write buffer size. */
int neverc_tcp_set_write_buffer(neverc_tcp_conn_t *conn, int bytes);

/* --- Pipe (like Go net.Pipe) --- */

/* Create an in-memory synchronous full-duplex pipe using socketpair.
 * Returns 0 on success, writes to *a and *b.
 * Both connections must be freed with neverc_tcp_close(). */
int neverc_tcp_pipe(neverc_tcp_conn_t **a, neverc_tcp_conn_t **b);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_TCP_H */
