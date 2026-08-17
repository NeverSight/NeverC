#ifndef NEVERC_NET_NETIP_H
#define NEVERC_NET_NETIP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  addr[16];
    uint8_t  is_v4;
    uint8_t  valid;
    char     zone[64];
} neverc_netip_addr_t;

typedef struct {
    neverc_netip_addr_t addr;
    uint16_t            port;
} neverc_netip_addrport_t;

typedef struct {
    neverc_netip_addr_t addr;
    uint8_t             bits;
    uint8_t             valid;
} neverc_netip_prefix_t;

/* Parse/Create */
int  neverc_netip_parse_addr(const char *s, neverc_netip_addr_t *out);
int  neverc_netip_addr_from4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, neverc_netip_addr_t *out);
int  neverc_netip_addr_from16(const uint8_t addr[16], neverc_netip_addr_t *out);
int  neverc_netip_parse_addrport(const char *s, neverc_netip_addrport_t *out);
int  neverc_netip_parse_prefix(const char *s, neverc_netip_prefix_t *out);

/* Format to string with snprintf length semantics: full length excluding NUL
 * even when truncated. A zero capacity may use a NULL buffer. Returns -1
 * on invalid arguments. */
int  neverc_netip_addr_string(const neverc_netip_addr_t *addr, char *buf, size_t cap);
int  neverc_netip_addrport_string(const neverc_netip_addrport_t *ap, char *buf, size_t cap);
int  neverc_netip_prefix_string(const neverc_netip_prefix_t *pfx, char *buf, size_t cap);

/* Properties */
int  neverc_netip_addr_is_valid(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is4(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is6(const neverc_netip_addr_t *addr);
/* IPv4-mapped IPv6 (::ffff:0:0/96). Unmap copies addr and, when mapped,
 * returns the IPv4 form with the zone cleared. */
int  neverc_netip_addr_is4in6(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_unmap(const neverc_netip_addr_t *addr,
                             neverc_netip_addr_t *out);
int  neverc_netip_addr_is_loopback(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is_multicast(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is_private(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is_link_local_unicast(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is_link_local_multicast(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is_global_unicast(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_is_unspecified(const neverc_netip_addr_t *addr);
/* True for loopback, private, link-local, unspecified, multicast, IPv4
 * broadcast, or IPv4-mapped forms of those. Unmaps mapped addresses
 * before classifying. For SSRF / DNS-rebinding checks. */
int  neverc_netip_addr_is_internal(const neverc_netip_addr_t *addr);
int  neverc_netip_addr_bit_len(const neverc_netip_addr_t *addr);

/* Comparison */
int  neverc_netip_addr_compare(const neverc_netip_addr_t *a, const neverc_netip_addr_t *b);
int  neverc_netip_addr_equal(const neverc_netip_addr_t *a, const neverc_netip_addr_t *b);

/* Prefix operations */
int  neverc_netip_prefix_contains(const neverc_netip_prefix_t *pfx, const neverc_netip_addr_t *addr);
int  neverc_netip_prefix_masked(const neverc_netip_prefix_t *pfx, neverc_netip_addr_t *out);
int  neverc_netip_prefix_bits(const neverc_netip_prefix_t *pfx);

/* Well-known addresses */
void neverc_netip_addr_ipv4_unspecified(neverc_netip_addr_t *out);
void neverc_netip_addr_ipv6_unspecified(neverc_netip_addr_t *out);
void neverc_netip_addr_ipv6_loopback(neverc_netip_addr_t *out);

/* Raw bytes access (returns byte count: 4 or 16) */
int  neverc_netip_addr_as4(const neverc_netip_addr_t *addr, uint8_t out[4]);
int  neverc_netip_addr_as16(const neverc_netip_addr_t *addr, uint8_t out[16]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/net.h>
#endif


#endif
