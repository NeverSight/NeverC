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

/* Close the listener. */
void neverc_tcp_listener_close(neverc_tcp_listener_t *ln);

/* Get the listener's local address. */
int neverc_tcp_listener_addr(neverc_tcp_listener_t *ln,
                              neverc_tcp_addr_t *addr);

/* --- Dial --- */

/* Connect to addr (e.g. "127.0.0.1:8080", "example.com:80").
 * Returns NULL on error. */
neverc_tcp_conn_t *neverc_tcp_dial(const char *addr, const char **errp);

/* Take ownership of an existing connected socket fd.
 * Optional preload bytes are returned by neverc_tcp_read before socket I/O. */
neverc_tcp_conn_t *neverc_tcp_adopt(int fd, const void *preload,
                                     size_t preload_len, const char **errp);

/* --- Connection I/O --- */

/* Write data. Returns bytes written or -1 on error. */
int neverc_tcp_write(neverc_tcp_conn_t *conn, const void *data, size_t len);

/* Read data. Returns bytes read, 0 on EOF, -1 on error. */
int neverc_tcp_read(neverc_tcp_conn_t *conn, void *buf, size_t buflen);

/* Close connection. */
void neverc_tcp_close(neverc_tcp_conn_t *conn);

/* Get remote address. */
int neverc_tcp_remote_addr(neverc_tcp_conn_t *conn, neverc_tcp_addr_t *addr);

/* Get local address. */
int neverc_tcp_local_addr(neverc_tcp_conn_t *conn, neverc_tcp_addr_t *addr);

/* Set read/write timeout in milliseconds (0 = no timeout). */
int neverc_tcp_set_timeout(neverc_tcp_conn_t *conn, int ms);

/* Set TCP_NODELAY (disable Nagle). */
int neverc_tcp_set_nodelay(neverc_tcp_conn_t *conn, int enable);

/* Set SO_REUSEADDR on listener. */
int neverc_tcp_set_reuseaddr(neverc_tcp_listener_t *ln, int enable);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_TCP_H */
