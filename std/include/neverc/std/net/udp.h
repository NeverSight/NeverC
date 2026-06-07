#ifndef NEVERC_NET_UDP_H
#define NEVERC_NET_UDP_H

/*
 * NeverC net/udp — UDP client/server (mirrors Go net.ListenPacket/DialUDP).
 *
 * Cross-platform: POSIX (mac/ios/linux/android) + WinSock (windows).
 * Go-style API:
 *   conn = neverc_udp_listen(":8080");
 *   n = neverc_udp_read_from(conn, buf, buflen, &addr);
 *   neverc_udp_write_to(conn, data, len, &addr);
 *   neverc_udp_close(conn);
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_udp_conn neverc_udp_conn_t;

typedef struct {
    char     addr[64];     /* "ip:port" string */
    uint16_t port;
    /* Internal storage for sockaddr */
    uint8_t  _sa[128];
    int      _sa_len;
} neverc_udp_addr_t;

/* --- Listen (server) --- */

/* Bind a UDP socket to addr (e.g. ":8080", "0.0.0.0:9090").
 * Returns NULL on error; *errp set. */
neverc_udp_conn_t *neverc_udp_listen(const char *addr, const char **errp);

/* --- Dial (client) --- */

/* Create a "connected" UDP socket to addr (sends go to that addr by default).
 * Returns NULL on error. */
neverc_udp_conn_t *neverc_udp_dial(const char *addr, const char **errp);

/* --- I/O --- */

/* Read a datagram. Fills sender addr. Returns bytes read or -1. */
int neverc_udp_read_from(neverc_udp_conn_t *conn, void *buf, size_t buflen,
                          neverc_udp_addr_t *from);

/* Write a datagram to the given address. Returns bytes sent or -1. */
int neverc_udp_write_to(neverc_udp_conn_t *conn, const void *data, size_t len,
                         const neverc_udp_addr_t *to);

/* Write to the connected address (only works after Dial). */
int neverc_udp_write(neverc_udp_conn_t *conn, const void *data, size_t len);

/* Read from the connected address (only works after Dial). */
int neverc_udp_read(neverc_udp_conn_t *conn, void *buf, size_t buflen);

/* Close the UDP connection. */
void neverc_udp_close(neverc_udp_conn_t *conn);

/* Get local bound address. */
int neverc_udp_local_addr(neverc_udp_conn_t *conn, neverc_udp_addr_t *addr);

/* Set read timeout in milliseconds (0 = no timeout). */
int neverc_udp_set_timeout(neverc_udp_conn_t *conn, int ms);

/* Set SO_BROADCAST. */
int neverc_udp_set_broadcast(neverc_udp_conn_t *conn, int enable);

/* Resolve an address string into a neverc_udp_addr_t. */
int neverc_udp_resolve_addr(const char *addr_str, neverc_udp_addr_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_UDP_H */
