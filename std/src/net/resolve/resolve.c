#include "neverc/std/net/resolve.h"
#include "neverc/std/net/netip.h"
#include "../idna_inc.h"
#include "../_net_platform.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windns.h>
  #include <iphlpapi.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "dnsapi.lib")
  #pragma comment(lib, "iphlpapi.lib")

  static void ensure_wsa_init(void) {
      static volatile LONG inited = 0;
      if (InterlockedCompareExchange(&inited, 0, 0)) return;
      static volatile LONG lock = 0;
      while (InterlockedCompareExchange(&lock, 1, 0) != 0) Sleep(0);
      if (!InterlockedCompareExchange(&inited, 0, 0)) {
          WSADATA wsa;
          WSAStartup(MAKEWORD(2, 2), &wsa);
          InterlockedExchange(&inited, 1);
      }
      InterlockedExchange(&lock, 0);
  }
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  #include <pthread.h>
  #include <arpa/nameser.h>
  #include <resolv.h>
  #include <net/if.h>
  #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    #include <arpa/nameser_compat.h>
  #endif

  static void ensure_wsa_init(void) { /* no-op on POSIX */ }
#endif

static int copy_dns_name(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0 || !src) return -1;
    /* POSIX dn_expand writes "" for the DNS root. RFC 7505 Null MX and
     * RFC 2782 "no service" SRV use "."; Go LookupMX/SRV return ".". */
    if (!src[0]) {
        if (dstsz < 2) return -1;
        dst[0] = '.';
        dst[1] = '\0';
        return 0;
    }
    size_t n = strlen(src);
    if (n >= dstsz) return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        /* CTL in a DNS name interpolates into URL/log/SMTP lines. */
        if (c <= 0x20 || c == 0x7f)
            return -1;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

/* IP literals and getaddrinfo results, including Windows zones with spaces
 * ("Ethernet 2"). Still reject CTL so a leftover Host cannot be injected. */
static int copy_ip_text(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0 || !src || !src[0]) return -1;
    size_t n = strlen(src);
    if (n >= dstsz) return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x20 || c == 0x7f)
            return -1;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

/* Query names/service labels handed to getaddrinfo / res_query / DnsQuery.
 * Reject C0 and DEL so a leftover Host/header cannot be injected through
 * the resolver. Space (0x20) is allowed: Windows IPv6 zones like
 * "Ethernet 2". DNS names still use copy_dns_name (space-rejected). */
static int dns_query_text_ok(const char *s) {
    if (!s || !s[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7f)
            return 0;
    }
    return 1;
}

static int idna_lookup_name(const char *host, char *out, size_t cap) {
    if (!host || !host[0] || !out || cap == 0) return -1;
    if (!dns_query_text_ok(host)) return -1;
    /* IPv6 literals (with or without zone) are not IDNA names. */
    if (strchr(host, ':')) {
        size_t n = strlen(host);
        if (n >= cap) return -1;
        memcpy(out, host, n + 1);
        return 0;
    }
    return neverc_idna_to_ascii(host, out, cap);
}

#ifndef _WIN32
static pthread_mutex_t g_resolv_lock = PTHREAD_MUTEX_INITIALIZER;

/* Expand a DNS name that starts inside this RR's RDATA. Compression
 * pointers may jump earlier in the message (RFC 1035), but the encoded
 * name must begin and consume bytes only within [rdata, rdata+rdlen). */
static int dns_expand_rdata_name(const unsigned char *msg, int msglen,
                                 const unsigned char *rdata, int rdlen,
                                 int name_off, char *dst, size_t dstlen) {
    if (!msg || msglen < 0 || !rdata || !dst || dstlen == 0 ||
        rdlen < 0 || name_off < 0 || name_off >= rdlen)
        return -1;
    int consumed = dn_expand(msg, msg + msglen, rdata + name_off,
                             dst, (int)dstlen);
    if (consumed < 0 || consumed > rdlen - name_off)
        return -1;
    /* RFC 1035 MX/NS/SRV RDATA is preference/header plus a single name.
     * Trailing bytes after the expanded name are not part of the RR. */
    if (name_off + consumed != rdlen)
        return -1;
    return 0;
}

/* res_query may return a size larger than anslen (only anslen bytes written)
 * or a UDP answer with the TC bit set. Either case is an incomplete RRset;
 * passing an oversized length to ns_initparse / dn_expand over-reads. */
static int posix_res_query(const char *name, int class, int type,
                           unsigned char *answer, int anslen) {
    pthread_mutex_lock(&g_resolv_lock);
    int len = res_query(name, class, type, answer, anslen);
    pthread_mutex_unlock(&g_resolv_lock);
    if (len < 12 || len > anslen)
        return -1;
    /* DNS header: QR Opcode AA TC RD in byte 2; RA Z RCODE in byte 3.
     * Accept only a complete QUERY response with RCODE=NOERROR. A
     * SERVFAIL/NXDOMAIN packet with leftover answers must not parse. */
    if ((answer[2] & 0x80) == 0)
        return -1;
    if ((answer[2] & 0x78) != 0)
        return -1;
    if (answer[2] & 0x02)
        return -1;
    if ((answer[3] & 0x0f) != 0)
        return -1;
    return len;
}
#endif

static int format_resolved_ip(int family, const void *bin, unsigned scope_id,
                              char *buf, size_t buflen) {
    if (!inet_ntop(family, bin, buf, (socklen_t)buflen))
        return -1;
    if (family != AF_INET6 || scope_id == 0)
        return 0;
    size_t used = strlen(buf);
    if (used >= buflen)
        return -1;
#ifndef _WIN32
    char ifname[IF_NAMESIZE];
    if (if_indextoname(scope_id, ifname)) {
        int n = snprintf(buf + used, buflen - used, "%%%s", ifname);
        if (n > 0 && (size_t)n < buflen - used)
            return 0;
        buf[used] = '\0';
    }
#endif
    int n = snprintf(buf + used, buflen - used, "%%%u", scope_id);
    return (n > 0 && (size_t)n < buflen - used) ? 0 : -1;
}

static int parse_ipv6_zone(const char *zone, unsigned *scope_id) {
    if (!zone || !zone[0] || !scope_id)
        return -1;
    unsigned value = 0;
    int digits = 1;
    for (const unsigned char *p = (const unsigned char *)zone; *p; p++) {
        if (*p < '0' || *p > '9') {
            digits = 0;
            break;
        }
        unsigned digit = (unsigned)(*p - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u)
            return -1;
        value = value * 10u + digit;
    }
    if (digits) {
        if (value == 0)
            return -1;
        *scope_id = value;
        return 0;
    }
    unsigned idx = if_nametoindex(zone);
    if (idx == 0)
        return -1;
    *scope_id = idx;
    return 0;
}

/* Go Resolver.LookupIP parses IP literals in-process (ipv4only / ipv6only /
 * To4). getaddrinfo(AF_INET, "::ffff:a.b.c.d") often fails even though Go
 * LookupIP("ip4", mapped) returns the embedded IPv4. Returns 1 if `lit`
 * is not an IP literal. */
static int lookup_ip_from_literal(int family, const char *lit,
                                  unsigned host_scope, const char *host_zone,
                                  neverc_net_addrs_t *out) {
    struct in_addr v4;
    struct in6_addr v6;
    char buf[64] = {0};

    if (inet_pton(AF_INET, lit, &v4) == 1) {
        if (host_zone || family == AF_INET6)
            return -1;
        if (format_resolved_ip(AF_INET, &v4, 0, buf, sizeof(buf)) != 0)
            return -1;
    } else if (inet_pton(AF_INET6, lit, &v6) == 1) {
        if (nc_in6_is_addr_v4mapped(&v6)) {
            /* Go ipv6only: To4() != nil is not a suitable address. */
            if (host_zone || family == AF_INET6)
                return -1;
            if (format_resolved_ip(AF_INET, v6.s6_addr + 12, 0, buf,
                                   sizeof(buf)) != 0)
                return -1;
        } else {
            if (family == AF_INET)
                return -1;
            if (format_resolved_ip(AF_INET6, &v6, host_scope, buf,
                                   sizeof(buf)) != 0)
                return -1;
            if (host_zone && !strchr(buf, '%')) {
                size_t used = strlen(buf);
                int n = snprintf(buf + used, sizeof(buf) - used, "%%%s",
                                 host_zone);
                if (n <= 0 || (size_t)n >= sizeof(buf) - used)
                    return -1;
            }
        }
    } else {
        return 1;
    }
    if (copy_ip_text(out->addrs[0], sizeof(out->addrs[0]), buf) != 0)
        return -1;
    out->count = 1;
    return 0;
}

/* ======================================================================
 * DNS Lookup — LookupHost / LookupIP
 * Uses getaddrinfo for cross-platform portability.
 * ====================================================================== */

int neverc_net_lookup_host(const char *host, neverc_net_addrs_t *out) {
    return neverc_net_lookup_ip(NULL, host, out);
}

int neverc_net_lookup_ip(const char *network, const char *host,
                          neverc_net_addrs_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!host || !host[0] || !dns_query_text_ok(host)) return -1;
    ensure_wsa_init();

    /* getaddrinfo / inet_ntop often drop the zone; keep the input zone text
     * so a literal like fe80::1%lo0 still round-trips. Unknown, empty, or
     * zero zones must fail closed, matching lookup_addr. */
    const char *host_zone = NULL;
    unsigned host_scope = 0;
    {
        const char *pct = strchr(host, '%');
        if (pct) {
            if (!strchr(host, ':') ||
                parse_ipv6_zone(pct + 1, &host_scope) != 0)
                return -1;
            host_zone = pct + 1;
        }
    }

    char query_host[256];
    const char *lookup = host;
    if (!strchr(host, ':')) {
        if (neverc_idna_to_ascii(host, query_host, sizeof(query_host)) != 0)
            return -1;
        lookup = query_host;
    }

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    if (!network || strcmp(network, "ip") == 0)
        hints.ai_family = AF_UNSPEC;
    else if (strcmp(network, "ip4") == 0)
        hints.ai_family = AF_INET;
    else if (strcmp(network, "ip6") == 0)
        hints.ai_family = AF_INET6;
    else
        return -1;

    {
        const char *lit = lookup;
        char iponly[256];
        if (host_zone) {
            const char *pct = host_zone - 1;
            size_t n = (size_t)(pct - host);
            if (n == 0 || n >= sizeof(iponly))
                return -1;
            memcpy(iponly, host, n);
            iponly[n] = '\0';
            lit = iponly;
        }
        int lit_rc = lookup_ip_from_literal(hints.ai_family, lit, host_scope,
                                            host_zone, out);
        if (lit_rc <= 0)
            return lit_rc;
    }

    int rc = getaddrinfo(lookup, NULL, &hints, &result);
    if (rc != 0) return -1;

    for (rp = result; rp && out->count < NEVERC_NET_MAX_ADDRS; rp = rp->ai_next) {
        char buf[64] = {0};
        if (rp->ai_family == AF_INET) {
            /* Go LookupIP("ip6"): IPv4 (To4 != nil) is not a suitable address. */
            if (hints.ai_family == AF_INET6)
                continue;
            struct sockaddr_in *in = (struct sockaddr_in *)rp->ai_addr;
            if (format_resolved_ip(AF_INET, &in->sin_addr, 0, buf,
                                   sizeof(buf)) != 0)
                continue;
        } else if (rp->ai_family == AF_INET6) {
            struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)rp->ai_addr;
            /* IPv4-mapped AAAA / literals must print as IPv4 so an ACL
             * that allows "127.0.0.1" is not bypassed by ::ffff:127.0.0.1.
             * Go ipv6only also drops mapped addresses (To4 != nil). */
            if (nc_in6_is_addr_v4mapped(&in6->sin6_addr)) {
                if (hints.ai_family == AF_INET6)
                    continue;
                if (format_resolved_ip(AF_INET,
                                       in6->sin6_addr.s6_addr + 12, 0, buf,
                                       sizeof(buf)) != 0)
                    continue;
            } else {
                if (hints.ai_family == AF_INET)
                    continue;
                unsigned scope = in6->sin6_scope_id ? in6->sin6_scope_id
                                                    : host_scope;
                if (format_resolved_ip(AF_INET6, &in6->sin6_addr, scope, buf,
                                       sizeof(buf)) != 0)
                    continue;
                if (host_zone && !strchr(buf, '%')) {
                    size_t used = strlen(buf);
                    int n = snprintf(buf + used, sizeof(buf) - used, "%%%s",
                                     host_zone);
                    if (n <= 0 || (size_t)n >= sizeof(buf) - used)
                        continue;
                }
            }
        } else {
            continue;
        }
        if (!buf[0]) continue;

        /* deduplicate */
        int dup = 0;
        for (int i = 0; i < out->count; i++) {
            if (strcmp(out->addrs[i], buf) == 0) { dup = 1; break; }
        }
        if (!dup) {
            if (copy_ip_text(out->addrs[out->count], 64, buf) != 0)
                continue;
            out->count++;
        }
    }

    int truncated = (rp != NULL);
    freeaddrinfo(result);
    if (truncated) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    return out->count > 0 ? 0 : -1;
}

/* ======================================================================
 * LookupPort — service name to port number
 * ====================================================================== */

static int parse_port_text(const char *port, unsigned *out) {
    if (!port || !port[0]) return -1;
    unsigned long value = 0;
    for (const unsigned char *p = (const unsigned char *)port; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10UL + (unsigned long)(*p - '0');
        if (value > 65535UL) return -1;
    }
    if (out) *out = (unsigned)value;
    return 0;
}

static int port_text_valid(const char *port) {
    return parse_port_text(port, NULL) == 0;
}

/* Host bytes that would inject headers or break URL/log formatting.
 * Extra '[' / ']' mismatch Go net.SplitHostPort (`foo[bar]:80`, `]:80`). */
static int host_text_valid(const char *host) {
    if (!host) return 0;
    for (const unsigned char *p = (const unsigned char *)host; *p; p++) {
        if (*p < 0x20 || *p == 0x7f || *p == '[' || *p == ']')
            return 0;
    }
    return 1;
}

int neverc_net_lookup_port(const char *network, const char *service) {
    if (!service || !service[0] || !dns_query_text_ok(service)) return -1;
    if (network && strcmp(network, "tcp") != 0 && strcmp(network, "udp") != 0)
        return -1;
    ensure_wsa_init();

    unsigned port_value;
    if (parse_port_text(service, &port_value) == 0)
        return (int)port_value;

    /* Overflowed digit strings and signed/whitespace numerics are not names. */
    unsigned char lead = (unsigned char)service[0];
    if (lead == '+' || lead == '-' || lead == ' ' || lead == '\t')
        return -1;
    int digits = 1;
    for (const unsigned char *p = (const unsigned char *)service; *p; p++) {
        if (*p < '0' || *p > '9') { digits = 0; break; }
    }
    if (digits) return -1;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    if (!network || strcmp(network, "tcp") == 0)
        hints.ai_socktype = SOCK_STREAM;
    else if (strcmp(network, "udp") == 0)
        hints.ai_socktype = SOCK_DGRAM;
    else
        hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(NULL, service, &hints, &result);
    if (rc != 0) return -1;

    int port = -1;
    if (result && result->ai_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)result->ai_addr;
        port = ntohs(in->sin_port);
    }
    freeaddrinfo(result);
    return port;
}

/* ======================================================================
 * LookupAddr — reverse DNS (IP -> hostname)
 * ====================================================================== */

int neverc_net_lookup_addr(const char *addr, neverc_net_addrs_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!addr || !addr[0] || !dns_query_text_ok(addr)) return -1;
    ensure_wsa_init();

    struct sockaddr_storage ss;
    socklen_t sslen = 0;
    memset(&ss, 0, sizeof(ss));

    struct in_addr v4;
    struct in6_addr v6;
    char iponly[128];
    const char *ip = addr;
    unsigned scope_id = 0;

    /* inet_pton rejects IPv6 zone IDs; strip `%name` / `%index` first. */
    if (strchr(addr, ':')) {
        const char *pct = strchr(addr, '%');
        if (pct) {
            size_t n = (size_t)(pct - addr);
            if (n == 0 || n >= sizeof(iponly) ||
                parse_ipv6_zone(pct + 1, &scope_id) != 0)
                return -1;
            memcpy(iponly, addr, n);
            iponly[n] = '\0';
            ip = iponly;
        }
    }

    if (inet_pton(AF_INET, ip, &v4) == 1) {
        if (scope_id != 0)
            return -1;
        struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
        sin->sin_family = AF_INET;
        sin->sin_addr = v4;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
        sin->sin_len = sizeof(*sin);
#endif
        sslen = sizeof(struct sockaddr_in);
    } else if (inet_pton(AF_INET6, ip, &v6) == 1) {
        if (nc_in6_is_addr_v4mapped(&v6)) {
            /* Reverse-lookup the embedded IPv4; ip6.arpa for mapped
             * addresses is empty, so ::ffff:127.0.0.1 would fail. */
            if (scope_id != 0)
                return -1;
            struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
            sin->sin_family = AF_INET;
            memcpy(&sin->sin_addr, v6.s6_addr + 12, 4);
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
            sin->sin_len = sizeof(*sin);
#endif
            sslen = sizeof(struct sockaddr_in);
        } else {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
            sin6->sin6_family = AF_INET6;
            sin6->sin6_addr = v6;
            sin6->sin6_scope_id = scope_id;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
            sin6->sin6_len = sizeof(*sin6);
#endif
            sslen = sizeof(struct sockaddr_in6);
        }
    } else {
        return -1;
    }

    char host[256];
    int rc = getnameinfo((struct sockaddr *)&ss, sslen,
                          host, sizeof(host), NULL, 0, NI_NAMEREQD);
    if (rc != 0) return -1;

    if (strlen(host) >= sizeof(out->addrs[0])) return -1;
    if (copy_dns_name(out->addrs[0], sizeof(out->addrs[0]), host) != 0)
        return -1;
    out->count = 1;
    return 0;
}

/* ======================================================================
 * LookupCNAME — canonical name for a host
 * ====================================================================== */

int neverc_net_lookup_cname(const char *host, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return -1;
    buf[0] = '\0';
    if (!host || !host[0]) return -1;
    ensure_wsa_init();

    char lookup[256];
    if (idna_lookup_name(host, lookup, sizeof(lookup)) != 0)
        return -1;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(lookup, NULL, &hints, &result);
    if (rc != 0) return -1;

    const char *cname = (result && result->ai_canonname)
        ? result->ai_canonname : lookup;
    int ok = copy_dns_name(buf, buflen, cname) == 0;
    freeaddrinfo(result);
    if (!ok) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

/* ======================================================================
 * LookupMX — MX records for a domain
 * ====================================================================== */

int neverc_net_lookup_mx(const char *name, neverc_net_mx_list_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!name || !name[0]) return -1;

    char lookup[256];
    if (idna_lookup_name(name, lookup, sizeof(lookup)) != 0)
        return -1;

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(lookup, DNS_TYPE_MX,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    DNS_RECORD *r;
    for (r = rec; r; r = r->pNext) {
        if (r->wType != DNS_TYPE_MX) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        if (copy_dns_name(out->records[out->count].host, 256,
                          r->Data.MX.pNameExchange) != 0) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        out->records[out->count].pref = r->Data.MX.wPreference;
        out->count++;
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = posix_res_query(lookup, C_IN, T_MX, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) goto mx_fail;
        if (ns_rr_type(rr) != T_MX) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) goto mx_fail;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 2) goto mx_fail;
        out->records[out->count].pref =
            (uint16_t)((rdata[0] << 8) | rdata[1]);

        char mxhost[256];
        if (dns_expand_rdata_name(answer, len, rdata, rdlen, 2,
                                  mxhost, sizeof(mxhost)) != 0)
            goto mx_fail;
        if (copy_dns_name(out->records[out->count].host, 256, mxhost) != 0)
            goto mx_fail;
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
mx_fail:
    memset(out, 0, sizeof(*out));
    return -1;
#endif
}

/* ======================================================================
 * LookupTXT — TXT records for a domain
 * ====================================================================== */

int neverc_net_lookup_txt(const char *name, neverc_net_txt_list_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!name || !name[0]) return -1;

    char lookup[256];
    if (idna_lookup_name(name, lookup, sizeof(lookup)) != 0)
        return -1;

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(lookup, DNS_TYPE_TEXT,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r; r = r->pNext) {
        if (r->wType != DNS_TYPE_TEXT || r->Data.TXT.dwStringCount == 0)
            continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        size_t assembled = 0;
        DWORD nstr = r->Data.TXT.dwStringCount;
        int truncated = 0;
        for (DWORD i = 0; i < nstr; i++) {
            const char *chunk = r->Data.TXT.pStringArray[i];
            if (!chunk) continue;
            size_t clen = strlen(chunk);
            size_t room = 511 - assembled;
            if (clen > room) {
                truncated = 1;
                break;
            }
            memcpy(out->records[out->count] + assembled, chunk, clen);
            assembled += clen;
        }
        if (truncated) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        out->records[out->count][assembled] = '\0';
        out->count++;
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = posix_res_query(lookup, C_IN, T_TXT, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) goto txt_fail;
        if (ns_rr_type(rr) != T_TXT) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) goto txt_fail;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 1) goto txt_fail;

        size_t assembled = 0;
        int offset = 0;
        while (offset < rdlen) {
            int chunk = rdata[offset];
            if (offset + 1 + chunk > rdlen)
                goto txt_fail;
            size_t room = 511 - assembled;
            if ((size_t)chunk > room)
                goto txt_fail;
            memcpy(out->records[out->count] + assembled,
                   rdata + offset + 1, (size_t)chunk);
            assembled += (size_t)chunk;
            offset += 1 + chunk;
        }
        out->records[out->count][assembled] = '\0';
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
txt_fail:
    memset(out, 0, sizeof(*out));
    return -1;
#endif
}

/* ======================================================================
 * LookupNS — NS records for a domain
 * ====================================================================== */

int neverc_net_lookup_ns(const char *name, neverc_net_ns_list_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!name || !name[0]) return -1;

    char lookup[256];
    if (idna_lookup_name(name, lookup, sizeof(lookup)) != 0)
        return -1;

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(lookup, DNS_TYPE_NS,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r; r = r->pNext) {
        if (r->wType != DNS_TYPE_NS) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        if (copy_dns_name(out->records[out->count], 256,
                          r->Data.NS.pNameHost) != 0) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        out->count++;
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = posix_res_query(lookup, C_IN, T_NS, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) goto ns_fail;
        if (ns_rr_type(rr) != T_NS) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) goto ns_fail;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        char nshost[256];
        if (dns_expand_rdata_name(answer, len, rdata, rdlen, 0,
                                  nshost, sizeof(nshost)) != 0)
            goto ns_fail;
        if (copy_dns_name(out->records[out->count], 256, nshost) != 0)
            goto ns_fail;
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
ns_fail:
    memset(out, 0, sizeof(*out));
    return -1;
#endif
}

/* ======================================================================
 * LookupSRV — SRV records
 * ====================================================================== */

int neverc_net_lookup_srv(const char *service, const char *proto,
                            const char *name, neverc_net_srv_list_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!name || !name[0]) return -1;

    char ascii_name[256];
    if (idna_lookup_name(name, ascii_name, sizeof(ascii_name)) != 0)
        return -1;
    if (service && !dns_query_text_ok(service))
        return -1;
    if (proto && !dns_query_text_ok(proto))
        return -1;

    char qname[512];
    int qn;
    if (service && proto)
        qn = snprintf(qname, sizeof(qname), "_%s._%s.%s", service, proto,
                      ascii_name);
    else
        qn = snprintf(qname, sizeof(qname), "%s", ascii_name);
    if (qn < 0 || (size_t)qn >= sizeof(qname)) return -1;

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(qname, DNS_TYPE_SRV,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r; r = r->pNext) {
        if (r->wType != DNS_TYPE_SRV) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        if (copy_dns_name(out->records[out->count].target, 256,
                          r->Data.SRV.pNameTarget) != 0) {
            DnsRecordListFree(rec, DnsFreeRecordList);
            memset(out, 0, sizeof(*out));
            return -1;
        }
        out->records[out->count].port = r->Data.SRV.wPort;
        out->records[out->count].priority = r->Data.SRV.wPriority;
        out->records[out->count].weight = r->Data.SRV.wWeight;
        out->count++;
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = posix_res_query(qname, C_IN, T_SRV, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) goto srv_fail;
        if (ns_rr_type(rr) != T_SRV) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) goto srv_fail;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 6) goto srv_fail;
        out->records[out->count].priority =
            (uint16_t)((rdata[0] << 8) | rdata[1]);
        out->records[out->count].weight =
            (uint16_t)((rdata[2] << 8) | rdata[3]);
        out->records[out->count].port =
            (uint16_t)((rdata[4] << 8) | rdata[5]);

        char target[256];
        if (dns_expand_rdata_name(answer, len, rdata, rdlen, 6,
                                  target, sizeof(target)) != 0)
            goto srv_fail;
        if (copy_dns_name(out->records[out->count].target, 256, target) != 0)
            goto srv_fail;
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
srv_fail:
    memset(out, 0, sizeof(*out));
    return -1;
#endif
}

/* ======================================================================
 * SplitHostPort / JoinHostPort — like Go net.SplitHostPort
 * ====================================================================== */

int neverc_net_split_host_port(const char *hostport,
                                 char *host, size_t hostlen,
                                 char *port, size_t portlen) {
    if (!hostport || !host || !port || hostlen == 0 || portlen == 0)
        return -1;

    host[0] = '\0';
    port[0] = '\0';

    const char *s = hostport;

    if (s[0] == '[') {
        /* IPv6: [host]:port */
        const char *end = strchr(s + 1, ']');
        if (!end) return -1;

        size_t hlen = (size_t)(end - s - 1);
        if (hlen >= hostlen) return -1;
        /* RFC 4007 zone-id after '%' must be non-empty. Validate before
         * copying so a failed split cannot leak CTL / leftover Host. */
        {
            const char *raw_host = s + 1;
            const char *zone = memchr(raw_host, '%', hlen);
            if (zone && (size_t)(zone - raw_host) + 1 >= hlen)
                return -1;
        }
        if (end[1] != ':') return -1;
        size_t plen = strlen(end + 2);
        if (plen >= portlen) return -1;
        if (!port_text_valid(end + 2)) return -1;
        memcpy(host, s + 1, hlen);
        host[hlen] = '\0';
        memcpy(port, end + 2, plen);
        port[plen] = '\0';
        if (!host_text_valid(host)) {
            host[0] = '\0';
            port[0] = '\0';
            return -1;
        }
        return 0;
    }

    /* Find last colon (to handle IPv6 without brackets correctly) */
    const char *last_colon = NULL;
    int colon_count = 0;
    for (const char *p = s; *p; p++) {
        if (*p == ':') { last_colon = p; colon_count++; }
    }

    if (!last_colon) return -1;  /* no port */

    if (colon_count > 1) {
        /* Multiple colons but no brackets = bare IPv6 address, error */
        return -1;
    }

    size_t hlen = (size_t)(last_colon - s);
    if (hlen >= hostlen) return -1;
    size_t plen = strlen(last_colon + 1);
    if (plen >= portlen) return -1;
    if (!port_text_valid(last_colon + 1)) return -1;

    memcpy(host, s, hlen);
    host[hlen] = '\0';
    memcpy(port, last_colon + 1, plen);
    port[plen] = '\0';
    if (!host_text_valid(host)) {
        host[0] = '\0';
        port[0] = '\0';
        return -1;
    }

    return 0;
}

int neverc_net_join_host_port(const char *host, const char *port,
                                char *buf, size_t buflen) {
    if (!host || !port || !buf || buflen == 0) return -1;
    if (!port_text_valid(port) || !host_text_valid(host)) return -1;

    int need_brackets = (strchr(host, ':') != NULL);

    int n;
    if (need_brackets)
        n = snprintf(buf, buflen, "[%s]:%s", host, port);
    else
        n = snprintf(buf, buflen, "%s:%s", host, port);

    return (n > 0 && (size_t)n < buflen) ? n : -1;
}

static int ascii_ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return 1;
}

static int name_is_localhost(const char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '.')
        n--;
    if (n == 9 && ascii_ieq_n(s, "localhost", 9))
        return 1;
    /* RFC 6761: *.localhost is loopback. */
    if (n > 10 && s[n - 10] == '.' &&
        ascii_ieq_n(s + n - 9, "localhost", 9))
        return 1;
    return 0;
}

int neverc_net_addr_is_internal(const char *addr) {
    neverc_netip_addr_t parsed, unmapped;
    if (!addr || !addr[0])
        return 1;
    if (name_is_localhost(addr))
        return 1;
    if (neverc_netip_parse_addr(addr, &parsed) != 0)
        return 1;
    if (neverc_netip_addr_unmap(&parsed, &unmapped) != 0)
        return 1;
    return neverc_netip_addr_is_internal(&unmapped);
}

int neverc_net_addrs_any_internal(const neverc_net_addrs_t *addrs) {
    if (!addrs || addrs->count <= 0)
        return 1;
    if (addrs->count > NEVERC_NET_MAX_ADDRS)
        return 1;
    for (int i = 0; i < addrs->count; i++) {
        if (neverc_net_addr_is_internal(addrs->addrs[i]))
            return 1;
    }
    return 0;
}

/* ======================================================================
 * Pipe — in-memory full-duplex connection (like Go net.Pipe)
 *
 * Implementation: two ring buffers + mutex + condvar for each direction.
 * Each endpoint owns one read-buffer and one write-buffer.
 * ====================================================================== */

#ifdef _WIN32
  typedef CRITICAL_SECTION pipe_mutex_t;
  typedef CONDITION_VARIABLE pipe_cond_t;
  #define pipe_mutex_init(m)    InitializeCriticalSection(m)
  #define pipe_mutex_destroy(m) DeleteCriticalSection(m)
  #define pipe_mutex_lock(m)    EnterCriticalSection(m)
  #define pipe_mutex_unlock(m)  LeaveCriticalSection(m)
  #define pipe_cond_init(c)     InitializeConditionVariable(c)
  #define pipe_cond_destroy(c)  ((void)(c))
  #define pipe_cond_signal(c)    WakeConditionVariable(c)
  #define pipe_cond_broadcast(c) WakeAllConditionVariable(c)
  #define pipe_cond_wait(c, m)   SleepConditionVariableCS(c, m, INFINITE)
#else
  typedef pthread_mutex_t pipe_mutex_t;
  typedef pthread_cond_t pipe_cond_t;
  #define pipe_mutex_init(m)    pthread_mutex_init(m, NULL)
  #define pipe_mutex_destroy(m) pthread_mutex_destroy(m)
  #define pipe_mutex_lock(m)    pthread_mutex_lock(m)
  #define pipe_mutex_unlock(m)  pthread_mutex_unlock(m)
  #define pipe_cond_init(c)     pthread_cond_init(c, NULL)
  #define pipe_cond_destroy(c)  pthread_cond_destroy(c)
  #define pipe_cond_signal(c)    pthread_cond_signal(c)
  #define pipe_cond_broadcast(c) pthread_cond_broadcast(c)
  #define pipe_cond_wait(c, m)   pthread_cond_wait(c, m)
#endif

#define PIPE_BUF_SIZE 65536

#ifdef NEVERC_NET_PIPE_TESTING
/* Defined by test_resolve.c only in dedicated lifecycle-test builds. The hook
 * runs with the ring mutex held immediately before a blocking wait. */
void neverc_net_pipe_test_waiting(int writing);
/* Runs after the ring mutex is released and immediately before the operation
 * drops its active-operation reference. It may block to make reclamation
 * tests deterministic, so it must never run with a pipe mutex held. */
void neverc_net_pipe_test_releasing(int writing);
#endif

typedef struct {
    char          data[PIPE_BUF_SIZE];
    size_t        head;
    size_t        tail;
    size_t        count;
    int           closed;
    pipe_mutex_t  lock;
    pipe_cond_t   not_empty;
    pipe_cond_t   not_full;
} pipe_ring_t;

typedef struct {
    pipe_ring_t        *rings[2];
    neverc_net_pipe_t  *endpoints[2];
    pipe_mutex_t        lock;
    size_t              open_endpoints;
    size_t              active_ops;
    int                 reaping;
} pipe_shared_t;

struct neverc_net_pipe {
    pipe_ring_t *read_ring;   /* ring buffer this endpoint reads from */
    pipe_ring_t *write_ring;  /* ring buffer this endpoint writes to */
    pipe_shared_t *shared;
    int closed;               /* guarded by shared->lock */
};

static void ring_init(pipe_ring_t *r) {
    memset(r, 0, sizeof(*r));
    pipe_mutex_init(&r->lock);
    pipe_cond_init(&r->not_empty);
    pipe_cond_init(&r->not_full);
}

static void ring_destroy(pipe_ring_t *r) {
    pipe_mutex_destroy(&r->lock);
    pipe_cond_destroy(&r->not_empty);
    pipe_cond_destroy(&r->not_full);
}

static void pipe_shared_reap(pipe_shared_t *shared) {
    ring_destroy(shared->rings[0]);
    ring_destroy(shared->rings[1]);
    free(shared->rings[0]);
    free(shared->rings[1]);
    free(shared->endpoints[0]);
    free(shared->endpoints[1]);
    pipe_mutex_destroy(&shared->lock);
    free(shared);
}

/* Called with shared->lock held. Exactly one closer or operation releaser
 * becomes the reaper after both endpoint owners and all registered I/O calls
 * are gone. */
static int pipe_shared_claim_reaper(pipe_shared_t *shared) {
    if (shared->open_endpoints != 0 || shared->active_ops != 0 ||
        shared->reaping)
        return 0;
    shared->reaping = 1;
    return 1;
}

/* Register the operation before reading either ring pointer. Close keeps the
 * endpoint and shared allocation alive until every registered operation has
 * released its ownership. Returns 1 when registered and 0 when closed. */
static int pipe_op_acquire(neverc_net_pipe_t *p, int writing,
                           pipe_shared_t **shared_out,
                           pipe_ring_t **ring_out) {
    pipe_shared_t *shared = p->shared;
    pipe_mutex_lock(&shared->lock);
    if (p->closed || shared->reaping) {
        pipe_mutex_unlock(&shared->lock);
        return 0;
    }
    shared->active_ops++;
    *shared_out = shared;
    *ring_out = writing ? p->write_ring : p->read_ring;
    pipe_mutex_unlock(&shared->lock);
    return 1;
}

static void pipe_op_release(pipe_shared_t *shared) {
    int reap;
    pipe_mutex_lock(&shared->lock);
    shared->active_ops--;
    reap = pipe_shared_claim_reaper(shared);
    pipe_mutex_unlock(&shared->lock);
    if (reap) {
        pipe_shared_reap(shared);
        /* shared, both rings, and both endpoints are invalid from here. */
    }
}

static void ring_close(pipe_ring_t *r) {
    pipe_mutex_lock(&r->lock);
    r->closed = 1;
    pipe_cond_broadcast(&r->not_empty);
    pipe_cond_broadcast(&r->not_full);
    pipe_mutex_unlock(&r->lock);
}

int neverc_net_pipe(neverc_net_pipe_t **end1, neverc_net_pipe_t **end2) {
    if (!end1 || !end2 || end1 == end2) return -1;

    pipe_ring_t *r1 = (pipe_ring_t *)calloc(1, sizeof(pipe_ring_t));
    pipe_ring_t *r2 = (pipe_ring_t *)calloc(1, sizeof(pipe_ring_t));
    neverc_net_pipe_t *p1 = (neverc_net_pipe_t *)calloc(1, sizeof(*p1));
    neverc_net_pipe_t *p2 = (neverc_net_pipe_t *)calloc(1, sizeof(*p2));
    pipe_shared_t *shared = (pipe_shared_t *)calloc(1, sizeof(*shared));

    if (!r1 || !r2 || !p1 || !p2 || !shared) {
        free(r1); free(r2); free(p1); free(p2); free(shared);
        return -1;
    }

    ring_init(r1);
    ring_init(r2);
    pipe_mutex_init(&shared->lock);
    shared->rings[0] = r1;
    shared->rings[1] = r2;
    shared->endpoints[0] = p1;
    shared->endpoints[1] = p2;
    shared->open_endpoints = 2;

    /* p1 writes to r1, p2 reads from r1
     * p2 writes to r2, p1 reads from r2 */
    p1->write_ring = r1;
    p1->read_ring = r2;
    p1->shared = shared;

    p2->write_ring = r2;
    p2->read_ring = r1;
    p2->shared = shared;

    *end1 = p1;
    *end2 = p2;
    return 0;
}

int neverc_net_pipe_read(neverc_net_pipe_t *p, void *buf, size_t len) {
    if (!p || (!buf && len > 0) || len > (size_t)INT_MAX) return -1;
    if (len == 0) return 0;

    pipe_shared_t *shared;
    pipe_ring_t *r;
    if (!pipe_op_acquire(p, 0, &shared, &r))
        return 0;
    pipe_mutex_lock(&r->lock);

#ifdef NEVERC_NET_PIPE_TESTING
    int wait_reported = 0;
#endif
    while (r->count == 0 && !r->closed) {
#ifdef NEVERC_NET_PIPE_TESTING
        if (!wait_reported) {
            wait_reported = 1;
            neverc_net_pipe_test_waiting(0);
        }
#endif
        pipe_cond_wait(&r->not_empty, &r->lock);
    }

    int result;
    if (r->count == 0 && r->closed) {
        result = 0;  /* EOF */
    } else {
        size_t to_read = len < r->count ? len : r->count;
        size_t first = PIPE_BUF_SIZE - r->head;
        if (first > to_read) first = to_read;

        memcpy(buf, r->data + r->head, first);
        if (to_read > first)
            memcpy((char *)buf + first, r->data, to_read - first);

        r->head = (r->head + to_read) % PIPE_BUF_SIZE;
        r->count -= to_read;

        pipe_cond_signal(&r->not_full);
        result = (int)to_read;
    }
    pipe_mutex_unlock(&r->lock);
    /* This release may reap p, r, and shared; it must be the final action. */
#ifdef NEVERC_NET_PIPE_TESTING
    neverc_net_pipe_test_releasing(0);
#endif
    pipe_op_release(shared);
    return result;
}

int neverc_net_pipe_write(neverc_net_pipe_t *p, const void *data, size_t len) {
    if (!p || (!data && len > 0) || len > (size_t)INT_MAX)
        return -1;
    if (len == 0) return 0;

    pipe_shared_t *shared;
    pipe_ring_t *r;
    if (!pipe_op_acquire(p, 1, &shared, &r))
        return -1;
    size_t written = 0;
    int result;
#ifdef NEVERC_NET_PIPE_TESTING
    int wait_reported = 0;
#endif

    while (written < len) {
        pipe_mutex_lock(&r->lock);

        if (r->closed) {
            pipe_mutex_unlock(&r->lock);
            result = written > 0 ? (int)written : -1;
            goto release;
        }

        while (r->count == PIPE_BUF_SIZE && !r->closed) {
#ifdef NEVERC_NET_PIPE_TESTING
            if (!wait_reported) {
                wait_reported = 1;
                neverc_net_pipe_test_waiting(1);
            }
#endif
            pipe_cond_wait(&r->not_full, &r->lock);
        }

        if (r->closed) {
            pipe_mutex_unlock(&r->lock);
            result = written > 0 ? (int)written : -1;
            goto release;
        }

        size_t space = PIPE_BUF_SIZE - r->count;
        size_t to_write = (len - written) < space ? (len - written) : space;

        size_t first = PIPE_BUF_SIZE - r->tail;
        if (first > to_write) first = to_write;

        memcpy(r->data + r->tail, (const char *)data + written, first);
        if (to_write > first)
            memcpy(r->data, (const char *)data + written + first,
                   to_write - first);

        r->tail = (r->tail + to_write) % PIPE_BUF_SIZE;
        r->count += to_write;
        written += to_write;

        pipe_cond_signal(&r->not_empty);
        pipe_mutex_unlock(&r->lock);
    }

    result = (int)written;
release:
    /* This release may reap p, r, and shared; it must be the final action. */
#ifdef NEVERC_NET_PIPE_TESTING
    neverc_net_pipe_test_releasing(1);
#endif
    pipe_op_release(shared);
    return result;
}

void neverc_net_pipe_close(neverc_net_pipe_t *p) {
    if (!p) return;

    pipe_shared_t *shared = p->shared;
    int reap;
    pipe_mutex_lock(&shared->lock);
    if (p->closed) {
        pipe_mutex_unlock(&shared->lock);
        return;
    }
    p->closed = 1;
    shared->open_endpoints--;

    /* Keep shared->lock while closing both rings so concurrent endpoint
     * closers cannot reap mutexes/condition variables before this closer has
     * finished using them. Broadcast is required for every blocked reader and
     * writer to leave its registered operation. */
    ring_close(p->write_ring);
    ring_close(p->read_ring);
    reap = pipe_shared_claim_reaper(shared);
    pipe_mutex_unlock(&shared->lock);

    if (reap) {
        pipe_shared_reap(shared);
        /* p and shared are invalid; return without touching either. */
    }
}
