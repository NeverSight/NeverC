#ifndef NEVERC_NET_RESOLVE_H
#define NEVERC_NET_RESOLVE_H

/*
 * NeverC net/resolve — DNS resolution and address utilities.
 * Mirrors Go's net.LookupHost, net.LookupIP, net.LookupPort, etc.
 *
 * Cross-platform: POSIX (Linux/macOS/iOS/Android) + Windows.
 * Uses getaddrinfo/getnameinfo for portability.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_NET_MAX_ADDRS    64
#define NEVERC_NET_MAX_RECORDS  32

/* --- IP Address result --- */
typedef struct {
    char    addrs[NEVERC_NET_MAX_ADDRS][64];
    int     count;
} neverc_net_addrs_t;

/* --- MX Record --- */
typedef struct {
    char     host[256];
    uint16_t pref;
} neverc_net_mx_t;

typedef struct {
    neverc_net_mx_t records[NEVERC_NET_MAX_RECORDS];
    int             count;
} neverc_net_mx_list_t;

/* --- SRV Record --- */
typedef struct {
    char     target[256];
    uint16_t port;
    uint16_t priority;
    uint16_t weight;
} neverc_net_srv_t;

typedef struct {
    neverc_net_srv_t records[NEVERC_NET_MAX_RECORDS];
    int              count;
} neverc_net_srv_list_t;

/* --- TXT Records --- */
typedef struct {
    char records[NEVERC_NET_MAX_RECORDS][512];
    int  count;
} neverc_net_txt_list_t;

/* --- NS Records --- */
typedef struct {
    char records[NEVERC_NET_MAX_RECORDS][256];
    int  count;
} neverc_net_ns_list_t;

/* --- DNS Lookup Functions (like Go net.LookupHost etc.) --- */

/* Resolve hostname to IP addresses.
 * Returns 0 on success, -1 on error. */
int neverc_net_lookup_host(const char *host, neverc_net_addrs_t *out);

/* Resolve hostname to IP addresses (like Go net.LookupIP).
 * network: "ip", "ip4", "ip6". NULL = "ip" (both). */
int neverc_net_lookup_ip(const char *network, const char *host,
                          neverc_net_addrs_t *out);

/* Look up the port for a service name (like Go net.LookupPort).
 * network: "tcp", "udp". service: "http", "https", "80", etc.
 * Returns port number or -1 on error. */
int neverc_net_lookup_port(const char *network, const char *service);

/* Reverse DNS: IP address to hostname (like Go net.LookupAddr).
 * Returns 0 on success, names filled in out. */
int neverc_net_lookup_addr(const char *addr, neverc_net_addrs_t *out);

/* Look up CNAME for a hostname.
 * Returns 0 on success, CNAME written to buf. */
int neverc_net_lookup_cname(const char *host, char *buf, size_t buflen);

/* Look up MX records for a domain (like Go net.LookupMX). */
int neverc_net_lookup_mx(const char *name, neverc_net_mx_list_t *out);

/* Look up TXT records for a domain (like Go net.LookupTXT). */
int neverc_net_lookup_txt(const char *name, neverc_net_txt_list_t *out);

/* Look up NS records for a domain (like Go net.LookupNS). */
int neverc_net_lookup_ns(const char *name, neverc_net_ns_list_t *out);

/* Look up SRV records (like Go net.LookupSRV).
 * service: "xmpp-client", proto: "tcp", name: "example.com" */
int neverc_net_lookup_srv(const char *service, const char *proto,
                            const char *name, neverc_net_srv_list_t *out);

/* --- Address Utilities (like Go net.SplitHostPort, JoinHostPort) --- */

/* Split "host:port" or "[host]:port" into host and port.
 * Returns 0 on success. */
int neverc_net_split_host_port(const char *hostport,
                                 char *host, size_t hostlen,
                                 char *port, size_t portlen);

/* Join host and port into "host:port" or "[IPv6]:port".
 * Returns length written or -1 on error. */
int neverc_net_join_host_port(const char *host, const char *port,
                                char *buf, size_t buflen);

/* DNS-rebinding / SSRF helpers. An address is internal if it is
 * loopback, private, link-local, unspecified, multicast, IPv4 broadcast,
 * localhost, or an IPv4-mapped form of those. Unparseable text is
 * treated as internal (fail closed). */
int neverc_net_addr_is_internal(const char *addr);
int neverc_net_addrs_any_internal(const neverc_net_addrs_t *addrs);

/* --- Pipe (like Go net.Pipe) — in-memory full-duplex connection --- */

typedef struct neverc_net_pipe neverc_net_pipe_t;

/* Create a pair of connected pipe endpoints (like Go net.Pipe).
 * Both sides support read/write. Thread-safe. */
int neverc_net_pipe(neverc_net_pipe_t **end1, neverc_net_pipe_t **end2);

/* Read from a pipe endpoint. Blocks until data available or closed. */
int neverc_net_pipe_read(neverc_net_pipe_t *p, void *buf, size_t len);

/* Write to a pipe endpoint. */
int neverc_net_pipe_write(neverc_net_pipe_t *p, const void *data, size_t len);

/* Close a pipe endpoint. */
void neverc_net_pipe_close(neverc_net_pipe_t *p);


#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */
#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_RESOLVE_H */
