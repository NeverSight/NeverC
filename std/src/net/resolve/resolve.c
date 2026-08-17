#include "neverc/std/net/resolve.h"
#include "neverc/std/net/netip.h"
#include "../idna_inc.h"
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
    if (!dst || dstsz == 0 || !src || !src[0]) return -1;
    size_t n = strlen(src);
    if (n >= dstsz) return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

static int idna_lookup_name(const char *host, char *out, size_t cap) {
    if (!host || !host[0] || !out || cap == 0) return -1;
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

/* res_query may return a size larger than anslen (only anslen bytes written)
 * or a UDP answer with the TC bit set. Either case is an incomplete RRset;
 * passing an oversized length to ns_initparse / dn_expand over-reads. */
static int posix_res_query(const char *name, int class, int type,
                           unsigned char *answer, int anslen) {
    pthread_mutex_lock(&g_resolv_lock);
    int len = res_query(name, class, type, answer, anslen);
    pthread_mutex_unlock(&g_resolv_lock);
    if (len < 0 || len > anslen)
        return -1;
    /* DNS header flags: QR Opcode AA TC RD in byte 2; TC is 0x02. */
    if (len >= 12 && (answer[2] & 0x02))
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

/* ======================================================================
 * DNS Lookup — LookupHost / LookupIP
 * Uses getaddrinfo for cross-platform portability.
 * ====================================================================== */

int neverc_net_lookup_host(const char *host, neverc_net_addrs_t *out) {
    return neverc_net_lookup_ip(NULL, host, out);
}

int neverc_net_lookup_ip(const char *network, const char *host,
                          neverc_net_addrs_t *out) {
    if (!host || !host[0] || !out) return -1;
    ensure_wsa_init();
    memset(out, 0, sizeof(*out));

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

    int rc = getaddrinfo(lookup, NULL, &hints, &result);
    if (rc != 0) return -1;

    for (rp = result; rp && out->count < NEVERC_NET_MAX_ADDRS; rp = rp->ai_next) {
        char buf[64] = {0};
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *in = (struct sockaddr_in *)rp->ai_addr;
            if (format_resolved_ip(AF_INET, &in->sin_addr, 0, buf,
                                   sizeof(buf)) != 0)
                continue;
        } else if (rp->ai_family == AF_INET6) {
            struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)rp->ai_addr;
            /* IPv4-mapped AAAA / literals must print as IPv4 so an ACL
             * that allows "127.0.0.1" is not bypassed by ::ffff:127.0.0.1. */
            if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
                if (format_resolved_ip(AF_INET,
                                       in6->sin6_addr.s6_addr + 12, 0, buf,
                                       sizeof(buf)) != 0)
                    continue;
            } else {
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
            if (copy_dns_name(out->addrs[out->count], 64, buf) != 0)
                continue;
            out->count++;
        }
    }

    int truncated = (rp != NULL);
    freeaddrinfo(result);
    if (truncated)
        return -1;
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

int neverc_net_lookup_port(const char *network, const char *service) {
    if (!service || !service[0]) return -1;
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
    if (!addr || !addr[0] || !out) return -1;
    ensure_wsa_init();
    memset(out, 0, sizeof(*out));

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
        if (IN6_IS_ADDR_V4MAPPED(&v6)) {
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
    if (!host || !host[0] || !buf || buflen == 0) return -1;
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
    return ok ? 0 : -1;
}

/* ======================================================================
 * LookupMX — MX records for a domain
 * ====================================================================== */

int neverc_net_lookup_mx(const char *name, neverc_net_mx_list_t *out) {
    if (!name || !name[0] || !out) return -1;
    memset(out, 0, sizeof(*out));

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
            return -1;
        }
        if (copy_dns_name(out->records[out->count].host, 256,
                          r->Data.MX.pNameExchange) != 0) {
            DnsRecordListFree(rec, DnsFreeRecordList);
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
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) return -1;
        if (ns_rr_type(rr) != T_MX) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) return -1;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 2) return -1;
        out->records[out->count].pref =
            (uint16_t)((rdata[0] << 8) | rdata[1]);

        char mxhost[256];
        if (dn_expand(answer, answer + len, rdata + 2, mxhost, sizeof(mxhost)) < 0)
            return -1;
        if (copy_dns_name(out->records[out->count].host, 256, mxhost) != 0)
            return -1;
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
#endif
}

/* ======================================================================
 * LookupTXT — TXT records for a domain
 * ====================================================================== */

int neverc_net_lookup_txt(const char *name, neverc_net_txt_list_t *out) {
    if (!name || !name[0] || !out) return -1;
    memset(out, 0, sizeof(*out));

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
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) return -1;
        if (ns_rr_type(rr) != T_TXT) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) return -1;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 1) return -1;

        size_t assembled = 0;
        int offset = 0;
        while (offset < rdlen) {
            int chunk = rdata[offset];
            if (offset + 1 + chunk > rdlen)
                return -1;
            size_t room = 511 - assembled;
            if ((size_t)chunk > room)
                return -1;
            memcpy(out->records[out->count] + assembled,
                   rdata + offset + 1, (size_t)chunk);
            assembled += (size_t)chunk;
            offset += 1 + chunk;
        }
        out->records[out->count][assembled] = '\0';
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
#endif
}

/* ======================================================================
 * LookupNS — NS records for a domain
 * ====================================================================== */

int neverc_net_lookup_ns(const char *name, neverc_net_ns_list_t *out) {
    if (!name || !name[0] || !out) return -1;
    memset(out, 0, sizeof(*out));

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
            return -1;
        }
        if (copy_dns_name(out->records[out->count], 256,
                          r->Data.NS.pNameHost) != 0) {
            DnsRecordListFree(rec, DnsFreeRecordList);
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
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) return -1;
        if (ns_rr_type(rr) != T_NS) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) return -1;

        char nshost[256];
        if (dn_expand(answer, answer + len, ns_rr_rdata(rr),
                       nshost, sizeof(nshost)) < 0)
            return -1;
        if (copy_dns_name(out->records[out->count], 256, nshost) != 0)
            return -1;
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
#endif
}

/* ======================================================================
 * LookupSRV — SRV records
 * ====================================================================== */

int neverc_net_lookup_srv(const char *service, const char *proto,
                            const char *name, neverc_net_srv_list_t *out) {
    if (!name || !name[0] || !out) return -1;
    memset(out, 0, sizeof(*out));

    char ascii_name[256];
    if (idna_lookup_name(name, ascii_name, sizeof(ascii_name)) != 0)
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
            return -1;
        }
        if (copy_dns_name(out->records[out->count].target, 256,
                          r->Data.SRV.pNameTarget) != 0) {
            DnsRecordListFree(rec, DnsFreeRecordList);
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
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) return -1;
        if (ns_rr_type(rr) != T_SRV) continue;
        if (out->count >= NEVERC_NET_MAX_RECORDS) return -1;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 6) return -1;
        out->records[out->count].priority =
            (uint16_t)((rdata[0] << 8) | rdata[1]);
        out->records[out->count].weight =
            (uint16_t)((rdata[2] << 8) | rdata[3]);
        out->records[out->count].port =
            (uint16_t)((rdata[4] << 8) | rdata[5]);

        char target[256];
        if (dn_expand(answer, answer + len, rdata + 6,
                       target, sizeof(target)) < 0)
            return -1;
        if (copy_dns_name(out->records[out->count].target, 256, target) != 0)
            return -1;
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
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
        memcpy(host, s + 1, hlen);
        host[hlen] = '\0';
        /* RFC 4007 zone-id after '%' must be non-empty. */
        const char *zone = strchr(host, '%');
        if (zone && !zone[1]) return -1;

        if (end[1] != ':') return -1;
        size_t plen = strlen(end + 2);
        if (plen >= portlen) return -1;
        memcpy(port, end + 2, plen);
        port[plen] = '\0';
        if (!port_text_valid(port)) return -1;
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
    memcpy(host, s, hlen);
    host[hlen] = '\0';

    size_t plen = strlen(last_colon + 1);
    if (plen >= portlen) return -1;
    memcpy(port, last_colon + 1, plen);
    port[plen] = '\0';
    if (!port_text_valid(port)) return -1;

    return 0;
}

int neverc_net_join_host_port(const char *host, const char *port,
                                char *buf, size_t buflen) {
    if (!host || !port || !buf || buflen == 0) return -1;
    if (!port_text_valid(port)) return -1;

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
    pipe_ring_t  *rings[2];
    pipe_mutex_t  lock;
    int           refs;
} pipe_shared_t;

struct neverc_net_pipe {
    pipe_ring_t *read_ring;   /* ring buffer this endpoint reads from */
    pipe_ring_t *write_ring;  /* ring buffer this endpoint writes to */
    pipe_shared_t *shared;
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

int neverc_net_pipe(neverc_net_pipe_t **end1, neverc_net_pipe_t **end2) {
    if (!end1 || !end2) return -1;

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
    shared->refs = 2;

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

    pipe_ring_t *r = p->read_ring;
    pipe_mutex_lock(&r->lock);

    while (r->count == 0 && !r->closed) {
        pipe_cond_wait(&r->not_empty, &r->lock);
    }

    if (r->count == 0 && r->closed) {
        pipe_mutex_unlock(&r->lock);
        return 0;  /* EOF */
    }

    size_t to_read = len < r->count ? len : r->count;
    size_t first = PIPE_BUF_SIZE - r->head;
    if (first > to_read) first = to_read;

    memcpy(buf, r->data + r->head, first);
    if (to_read > first)
        memcpy((char *)buf + first, r->data, to_read - first);

    r->head = (r->head + to_read) % PIPE_BUF_SIZE;
    r->count -= to_read;

    pipe_cond_signal(&r->not_full);
    pipe_mutex_unlock(&r->lock);

    return (int)to_read;
}

int neverc_net_pipe_write(neverc_net_pipe_t *p, const void *data, size_t len) {
    if (!p || (!data && len > 0) || len > (size_t)INT_MAX)
        return -1;
    if (len == 0) return 0;

    pipe_ring_t *r = p->write_ring;
    size_t written = 0;

    while (written < len) {
        pipe_mutex_lock(&r->lock);

        if (r->closed) {
            pipe_mutex_unlock(&r->lock);
            return -1;  /* broken pipe */
        }

        while (r->count == PIPE_BUF_SIZE && !r->closed) {
            pipe_cond_wait(&r->not_full, &r->lock);
        }

        if (r->closed) {
            pipe_mutex_unlock(&r->lock);
            return -1;
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

    return (int)written;
}

void neverc_net_pipe_close(neverc_net_pipe_t *p) {
    if (!p) return;

    /* Close both directions and wake every waiter. Signal would leave a
     * second blocked reader/writer parked after the first consumes EOF. */
    if (p->write_ring) {
        pipe_mutex_lock(&p->write_ring->lock);
        p->write_ring->closed = 1;
        pipe_cond_broadcast(&p->write_ring->not_empty);
        pipe_cond_broadcast(&p->write_ring->not_full);
        pipe_mutex_unlock(&p->write_ring->lock);
    }
    if (p->read_ring) {
        pipe_mutex_lock(&p->read_ring->lock);
        p->read_ring->closed = 1;
        pipe_cond_broadcast(&p->read_ring->not_empty);
        pipe_cond_broadcast(&p->read_ring->not_full);
        pipe_mutex_unlock(&p->read_ring->lock);
    }

    pipe_shared_t *shared = p->shared;
    int last = 0;
    if (shared) {
        pipe_mutex_lock(&shared->lock);
        if (shared->refs > 0) shared->refs--;
        last = (shared->refs == 0);
        pipe_mutex_unlock(&shared->lock);
        if (last) {
            if (shared->rings[0]) {
                ring_destroy(shared->rings[0]);
                free(shared->rings[0]);
            }
            if (shared->rings[1]) {
                ring_destroy(shared->rings[1]);
                free(shared->rings[1]);
            }
            pipe_mutex_destroy(&shared->lock);
            free(shared);
        }
    }
    free(p);
}
