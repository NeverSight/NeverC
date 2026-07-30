#include "neverc/std/net/resolve.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #if defined(__has_include)
    #if __has_include(<windns.h>)
      #include <windns.h>
    #else
      #include "windns_compat.h"
    #endif
  #else
    #include <windns.h>
  #endif
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "dnsapi.lib")

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
  #if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    #include <arpa/nameser_compat.h>
  #endif

  static void ensure_wsa_init(void) { /* no-op on POSIX */ }
#endif

/* ======================================================================
 * DNS Lookup — LookupHost / LookupIP
 * Uses getaddrinfo for cross-platform portability.
 * ====================================================================== */

int neverc_net_lookup_host(const char *host, neverc_net_addrs_t *out) {
    return neverc_net_lookup_ip(NULL, host, out);
}

int neverc_net_lookup_ip(const char *network, const char *host,
                          neverc_net_addrs_t *out) {
    if (!host || !out) return -1;
    ensure_wsa_init();
    memset(out, 0, sizeof(*out));

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
        hints.ai_family = AF_UNSPEC;

    int rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0) return -1;

    for (rp = result; rp && out->count < NEVERC_NET_MAX_ADDRS; rp = rp->ai_next) {
        char buf[64] = {0};
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *in = (struct sockaddr_in *)rp->ai_addr;
            inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
        } else if (rp->ai_family == AF_INET6) {
            struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)rp->ai_addr;
            inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
        } else {
            continue;
        }

        /* deduplicate */
        int dup = 0;
        for (int i = 0; i < out->count; i++) {
            if (strcmp(out->addrs[i], buf) == 0) { dup = 1; break; }
        }
        if (!dup) {
            strncpy(out->addrs[out->count], buf, 63);
            out->count++;
        }
    }

    freeaddrinfo(result);
    return out->count > 0 ? 0 : -1;
}

/* ======================================================================
 * LookupPort — service name to port number
 * ====================================================================== */

int neverc_net_lookup_port(const char *network, const char *service) {
    if (!service) return -1;
    ensure_wsa_init();

    /* Try numeric first */
    char *end;
    long p = strtol(service, &end, 10);
    if (*end == '\0' && p >= 0 && p <= 65535)
        return (int)p;

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
    if (!addr || !out) return -1;
    ensure_wsa_init();
    memset(out, 0, sizeof(*out));

    struct sockaddr_storage ss;
    socklen_t sslen = 0;
    memset(&ss, 0, sizeof(ss));

    struct in_addr v4;
    struct in6_addr v6;

    if (inet_pton(AF_INET, addr, &v4) == 1) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
        sin->sin_family = AF_INET;
        sin->sin_addr = v4;
        sslen = sizeof(struct sockaddr_in);
    } else if (inet_pton(AF_INET6, addr, &v6) == 1) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_addr = v6;
        sslen = sizeof(struct sockaddr_in6);
    } else {
        return -1;
    }

    char host[256];
    int rc = getnameinfo((struct sockaddr *)&ss, sslen,
                          host, sizeof(host), NULL, 0, NI_NAMEREQD);
    if (rc != 0) return -1;

    strncpy(out->addrs[0], host, 63);
    out->count = 1;
    return 0;
}

/* ======================================================================
 * LookupCNAME — canonical name for a host
 * ====================================================================== */

int neverc_net_lookup_cname(const char *host, char *buf, size_t buflen) {
    if (!host || !buf || buflen == 0) return -1;

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(host, DNS_TYPE_CNAME,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0 || !rec) {
        /* If no CNAME, return the host itself (like Go) */
        strncpy(buf, host, buflen - 1);
        buf[buflen - 1] = '\0';
        return 0;
    }
    strncpy(buf, rec->Data.CNAME.pNameHost, buflen - 1);
    buf[buflen - 1] = '\0';
    DnsRecordListFree(rec, DnsFreeRecordList);
    return 0;
#else
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0) {
        strncpy(buf, host, buflen - 1);
        buf[buflen - 1] = '\0';
        return 0;
    }

    if (result->ai_canonname) {
        strncpy(buf, result->ai_canonname, buflen - 1);
        buf[buflen - 1] = '\0';
    } else {
        strncpy(buf, host, buflen - 1);
        buf[buflen - 1] = '\0';
    }
    freeaddrinfo(result);
    return 0;
#endif
}

/* ======================================================================
 * LookupMX — MX records for a domain
 * ====================================================================== */

int neverc_net_lookup_mx(const char *name, neverc_net_mx_list_t *out) {
    if (!name || !out) return -1;
    memset(out, 0, sizeof(*out));

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(name, DNS_TYPE_MX,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r && out->count < NEVERC_NET_MAX_RECORDS; r = r->pNext) {
        if (r->wType == DNS_TYPE_MX) {
            strncpy(out->records[out->count].host,
                    r->Data.MX.pNameExchange, 255);
            out->records[out->count].pref = r->Data.MX.wPreference;
            out->count++;
        }
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = res_query(name, C_IN, T_MX, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt && out->count < NEVERC_NET_MAX_RECORDS; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
        if (ns_rr_type(rr) != T_MX) continue;

        const unsigned char *rdata = ns_rr_rdata(rr);
        out->records[out->count].pref =
            (uint16_t)((rdata[0] << 8) | rdata[1]);

        char mxhost[256];
        if (dn_expand(answer, answer + len, rdata + 2, mxhost, sizeof(mxhost)) < 0)
            continue;
        strncpy(out->records[out->count].host, mxhost, 255);
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
#endif
}

/* ======================================================================
 * LookupTXT — TXT records for a domain
 * ====================================================================== */

int neverc_net_lookup_txt(const char *name, neverc_net_txt_list_t *out) {
    if (!name || !out) return -1;
    memset(out, 0, sizeof(*out));

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(name, DNS_TYPE_TEXT,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r && out->count < NEVERC_NET_MAX_RECORDS; r = r->pNext) {
        if (r->wType == DNS_TYPE_TEXT && r->Data.TXT.dwStringCount > 0) {
            strncpy(out->records[out->count],
                    r->Data.TXT.pStringArray[0], 511);
            out->count++;
        }
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = res_query(name, C_IN, T_TXT, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt && out->count < NEVERC_NET_MAX_RECORDS; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
        if (ns_rr_type(rr) != T_TXT) continue;

        const unsigned char *rdata = ns_rr_rdata(rr);
        int rdlen = ns_rr_rdlen(rr);
        if (rdlen < 1) continue;

        int txtlen = rdata[0];
        if (txtlen > rdlen - 1) txtlen = rdlen - 1;
        if (txtlen > 511) txtlen = 511;

        memcpy(out->records[out->count], rdata + 1, (size_t)txtlen);
        out->records[out->count][txtlen] = '\0';
        out->count++;
    }
    return out->count > 0 ? 0 : -1;
#endif
}

/* ======================================================================
 * LookupNS — NS records for a domain
 * ====================================================================== */

int neverc_net_lookup_ns(const char *name, neverc_net_ns_list_t *out) {
    if (!name || !out) return -1;
    memset(out, 0, sizeof(*out));

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(name, DNS_TYPE_NS,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r && out->count < NEVERC_NET_MAX_RECORDS; r = r->pNext) {
        if (r->wType == DNS_TYPE_NS) {
            strncpy(out->records[out->count],
                    r->Data.NS.pNameHost, 255);
            out->count++;
        }
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = res_query(name, C_IN, T_NS, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt && out->count < NEVERC_NET_MAX_RECORDS; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
        if (ns_rr_type(rr) != T_NS) continue;

        char nshost[256];
        if (dn_expand(answer, answer + len, ns_rr_rdata(rr),
                       nshost, sizeof(nshost)) < 0)
            continue;
        strncpy(out->records[out->count], nshost, 255);
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
    if (!name || !out) return -1;
    memset(out, 0, sizeof(*out));

    char qname[512];
    if (service && proto)
        snprintf(qname, sizeof(qname), "_%s._%s.%s", service, proto, name);
    else
        snprintf(qname, sizeof(qname), "%s", name);

#ifdef _WIN32
    DNS_RECORD *rec = NULL;
    DNS_STATUS status = DnsQuery_A(qname, DNS_TYPE_SRV,
                                    DNS_QUERY_STANDARD, NULL, &rec, NULL);
    if (status != 0) return -1;

    for (DNS_RECORD *r = rec; r && out->count < NEVERC_NET_MAX_RECORDS; r = r->pNext) {
        if (r->wType == DNS_TYPE_SRV) {
            strncpy(out->records[out->count].target,
                    r->Data.SRV.pNameTarget, 255);
            out->records[out->count].port = r->Data.SRV.wPort;
            out->records[out->count].priority = r->Data.SRV.wPriority;
            out->records[out->count].weight = r->Data.SRV.wWeight;
            out->count++;
        }
    }
    DnsRecordListFree(rec, DnsFreeRecordList);
    return out->count > 0 ? 0 : -1;
#else
    unsigned char answer[4096];
    int len = res_query(qname, C_IN, T_SRV, answer, sizeof(answer));
    if (len < 0) return -1;

    ns_msg msg;
    if (ns_initparse(answer, len, &msg) < 0) return -1;

    int cnt = ns_msg_count(msg, ns_s_an);
    for (int i = 0; i < cnt && out->count < NEVERC_NET_MAX_RECORDS; i++) {
        ns_rr rr;
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
        if (ns_rr_type(rr) != T_SRV) continue;

        const unsigned char *rdata = ns_rr_rdata(rr);
        out->records[out->count].priority =
            (uint16_t)((rdata[0] << 8) | rdata[1]);
        out->records[out->count].weight =
            (uint16_t)((rdata[2] << 8) | rdata[3]);
        out->records[out->count].port =
            (uint16_t)((rdata[4] << 8) | rdata[5]);

        char target[256];
        if (dn_expand(answer, answer + len, rdata + 6,
                       target, sizeof(target)) < 0)
            continue;
        strncpy(out->records[out->count].target, target, 255);
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
        if (hlen >= hostlen) hlen = hostlen - 1;
        memcpy(host, s + 1, hlen);
        host[hlen] = '\0';

        if (end[1] == ':') {
            size_t plen = strlen(end + 2);
            if (plen >= portlen) plen = portlen - 1;
            memcpy(port, end + 2, plen);
            port[plen] = '\0';
        } else if (end[1] != '\0') {
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
    if (hlen >= hostlen) hlen = hostlen - 1;
    memcpy(host, s, hlen);
    host[hlen] = '\0';

    size_t plen = strlen(last_colon + 1);
    if (plen >= portlen) plen = portlen - 1;
    memcpy(port, last_colon + 1, plen);
    port[plen] = '\0';

    return 0;
}

int neverc_net_join_host_port(const char *host, const char *port,
                                char *buf, size_t buflen) {
    if (!host || !port || !buf || buflen == 0) return -1;

    int need_brackets = (strchr(host, ':') != NULL);

    int n;
    if (need_brackets)
        n = snprintf(buf, buflen, "[%s]:%s", host, port);
    else
        n = snprintf(buf, buflen, "%s:%s", host, port);

    return (n > 0 && (size_t)n < buflen) ? n : -1;
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
  #define pipe_cond_signal(c)   WakeConditionVariable(c)
  #define pipe_cond_wait(c, m)  SleepConditionVariableCS(c, m, INFINITE)
#else
  typedef pthread_mutex_t pipe_mutex_t;
  typedef pthread_cond_t pipe_cond_t;
  #define pipe_mutex_init(m)    pthread_mutex_init(m, NULL)
  #define pipe_mutex_destroy(m) pthread_mutex_destroy(m)
  #define pipe_mutex_lock(m)    pthread_mutex_lock(m)
  #define pipe_mutex_unlock(m)  pthread_mutex_unlock(m)
  #define pipe_cond_init(c)     pthread_cond_init(c, NULL)
  #define pipe_cond_destroy(c)  pthread_cond_destroy(c)
  #define pipe_cond_signal(c)   pthread_cond_signal(c)
  #define pipe_cond_wait(c, m)  pthread_cond_wait(c, m)
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

struct neverc_net_pipe {
    pipe_ring_t *read_ring;   /* ring buffer this endpoint reads from */
    pipe_ring_t *write_ring;  /* ring buffer this endpoint writes to */
    struct neverc_net_pipe *peer;
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

    if (!r1 || !r2 || !p1 || !p2) {
        free(r1); free(r2); free(p1); free(p2);
        return -1;
    }

    ring_init(r1);
    ring_init(r2);

    /* p1 writes to r1, p2 reads from r1
     * p2 writes to r2, p1 reads from r2 */
    p1->write_ring = r1;
    p1->read_ring = r2;
    p1->peer = p2;

    p2->write_ring = r2;
    p2->read_ring = r1;
    p2->peer = p1;

    *end1 = p1;
    *end2 = p2;
    return 0;
}

int neverc_net_pipe_read(neverc_net_pipe_t *p, void *buf, size_t len) {
    if (!p || !buf || len == 0) return -1;

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
    if (!p || !data || len == 0) return -1;

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

    /* Close both directions — signal readers/writers */
    if (p->write_ring) {
        pipe_mutex_lock(&p->write_ring->lock);
        p->write_ring->closed = 1;
        pipe_cond_signal(&p->write_ring->not_empty);
        pipe_cond_signal(&p->write_ring->not_full);
        pipe_mutex_unlock(&p->write_ring->lock);
    }
    if (p->read_ring) {
        pipe_mutex_lock(&p->read_ring->lock);
        p->read_ring->closed = 1;
        pipe_cond_signal(&p->read_ring->not_empty);
        pipe_cond_signal(&p->read_ring->not_full);
        pipe_mutex_unlock(&p->read_ring->lock);
    }

    /* If peer is already closed, free the rings */
    if (p->peer == NULL) {
        if (p->write_ring) { ring_destroy(p->write_ring); free(p->write_ring); }
        if (p->read_ring)  { ring_destroy(p->read_ring);  free(p->read_ring); }
    } else {
        p->peer->peer = NULL;
    }
    free(p);
}
