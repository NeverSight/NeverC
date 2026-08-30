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

#include "../context.h"
#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_UDP_MAX_DATAGRAM_SIZE 65535U
#define NEVERC_UDP_MAX_BATCH_SIZE 64U
#define NEVERC_UDP_MAX_QUEUE_CAPACITY 65536U

typedef struct neverc_udp_conn neverc_udp_conn_t;
typedef struct neverc_udp_queue neverc_udp_queue_t;

typedef struct {
    char     addr[64];     /* "ip:port" string */
    uint16_t port;
    /* Internal storage for sockaddr */
    uint8_t  _sa[128];
    int      _sa_len;
} neverc_udp_addr_t;

typedef struct {
    neverc_udp_addr_t source;
    neverc_udp_addr_t destination;
    size_t            datagram_len;   /* Full length before truncation. */
    uint32_t          interface_index;
    int               truncated;
} neverc_udp_packet_info_t;

typedef struct {
    size_t protocol_max_payload;
    size_t path_mtu;          /* Zero when the platform cannot report it. */
    size_t path_max_payload;  /* Safe payload for path_mtu, or protocol max. */
} neverc_udp_mtu_info_t;

typedef struct {
    void                     *data;
    size_t                    capacity;
    size_t                    len;
    neverc_udp_packet_info_t  info;
} neverc_udp_recv_message_t;

typedef struct {
    const void                *data;
    size_t                    len;
    const neverc_udp_addr_t   *destination; /* NULL for connected UDP. */
} neverc_udp_send_message_t;

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

/* Read one datagram with explicit truncation and packet metadata. */
int neverc_udp_read_packet(neverc_udp_conn_t *conn, void *buf, size_t buflen,
                            neverc_udp_packet_info_t *info);

/* Single-attempt read for integration with an external event loop. */
neverc_net_result_t neverc_udp_try_read_packet(
    neverc_udp_conn_t *conn, void *buf, size_t buflen,
    neverc_udp_packet_info_t *info);

/* Packet read interrupted by context cancellation or deadline expiry. */
neverc_net_result_t neverc_udp_read_packet_context(
    neverc_udp_conn_t *conn, neverc_context_t *ctx,
    void *buf, size_t buflen, neverc_udp_packet_info_t *info);

/* Non-blocking/context-aware datagram send. destination may be NULL only for
 * a connected UDP socket. */
neverc_net_result_t neverc_udp_try_write(
    neverc_udp_conn_t *conn, const void *data, size_t len,
    const neverc_udp_addr_t *destination);
neverc_net_result_t neverc_udp_write_context(
    neverc_udp_conn_t *conn, neverc_context_t *ctx,
    const void *data, size_t len,
    const neverc_udp_addr_t *destination);

/* Receive/send up to NEVERC_UDP_MAX_BATCH_SIZE datagrams per call.
 * Returns the number of completed datagrams, which may be less than count. */
int neverc_udp_read_batch(neverc_udp_conn_t *conn,
                           neverc_udp_recv_message_t *messages, size_t count);
int neverc_udp_write_batch(neverc_udp_conn_t *conn,
                            const neverc_udp_send_message_t *messages,
                            size_t count);

/* Caller-driven bounded receive queue. receive() waits for one datagram when
 * space is available, then drains readable datagrams into fixed storage. */
neverc_udp_queue_t *neverc_udp_queue_create(size_t capacity,
                                             size_t payload_capacity);
int neverc_udp_queue_receive(neverc_udp_conn_t *conn,
                              neverc_udp_queue_t *queue,
                              size_t max_messages);
neverc_net_result_t neverc_udp_queue_pop(
    neverc_udp_queue_t *queue, void *buf, size_t buflen,
    neverc_udp_packet_info_t *info);
size_t neverc_udp_queue_length(const neverc_udp_queue_t *queue);
size_t neverc_udp_queue_capacity(const neverc_udp_queue_t *queue);
void neverc_udp_queue_free(neverc_udp_queue_t *queue);

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

/* Pointer-width-safe native socket handle for event-loop integration. */
uintptr_t neverc_udp_conn_handle(neverc_udp_conn_t *conn);

/* Report protocol and currently known path MTU limits. */
int neverc_udp_get_mtu_info(neverc_udp_conn_t *conn,
                             neverc_udp_mtu_info_t *info);

/* Set read and write timeouts independently (0 = no timeout). */
int neverc_udp_set_read_timeout(neverc_udp_conn_t *conn, int ms);
int neverc_udp_set_write_timeout(neverc_udp_conn_t *conn, int ms);

/* Set absolute Unix-millisecond deadlines (0 clears the deadline). */
int neverc_udp_set_read_deadline(neverc_udp_conn_t *conn,
                                  int64_t deadline_ms);
int neverc_udp_set_write_deadline(neverc_udp_conn_t *conn,
                                   int64_t deadline_ms);

/* Compatibility helper that sets both read and write timeouts. */
int neverc_udp_set_timeout(neverc_udp_conn_t *conn, int ms);

/* Set SO_BROADCAST. */
int neverc_udp_set_broadcast(neverc_udp_conn_t *conn, int enable);

/* Resolve an address string into a neverc_udp_addr_t. */
int neverc_udp_resolve_addr(const char *addr_str, neverc_udp_addr_t *out);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_UDP_H */
