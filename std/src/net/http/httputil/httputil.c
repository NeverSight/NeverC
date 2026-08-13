#include "neverc/std/net/http/httputil.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef _WIN32
#include <strings.h>
#else
static int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#endif

static int httputil_fixed_appendf(char *buf, size_t cap, size_t *length,
                                  const char *format, ...) {
    if (!buf || !length || !format || *length >= cap)
        return -1;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(
        buf + *length, cap - *length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= cap - *length)
        return -1;
    *length += (size_t)written;
    return 0;
}

static int httputil_dump_append(char **buf, size_t *length, size_t *capacity,
                                const void *data, size_t data_len) {
    if (!buf || !length || !capacity || (!data && data_len != 0) ||
        data_len == SIZE_MAX || *length > SIZE_MAX - data_len - 1U)
        return -1;
    size_t required = *length + data_len + 1U;
    if (required > *capacity) {
        size_t next = *capacity < 256U ? 256U : *capacity;
        while (next < required) {
            if (next > SIZE_MAX / 2U) {
                next = required;
                break;
            }
            next *= 2U;
        }
        char *grown = (char *)realloc(*buf, next);
        if (!grown)
            return -1;
        *buf = grown;
        *capacity = next;
    }
    if (data_len != 0)
        memcpy(*buf + *length, data, data_len);
    *length += data_len;
    (*buf)[*length] = '\0';
    return 0;
}

static int httputil_dump_append_string(char **buf, size_t *length,
                                       size_t *capacity, const char *value) {
    return value ? httputil_dump_append(
        buf, length, capacity, value, strlen(value)) : -1;
}

/* ======================================================================
 * Reverse Proxy
 * ====================================================================== */

struct neverc_httputil_reverse_proxy {
    char target_host[256];
    uint16_t target_port;
    char target_path[2048];
    int use_tls;
    int set_forwarded;

    neverc_httputil_rewrite_func_t rewrite_func;
    void *rewrite_data;

    neverc_httputil_error_handler_t error_handler;
    void *error_data;
};

#define MAX_PROXIES 64
static neverc_httputil_reverse_proxy_t *g_proxy_table[MAX_PROXIES];
static int g_proxy_count = 0;

static int parse_target_url(const char *url,
                             char *host, size_t hostlen,
                             uint16_t *port, char *path, size_t pathlen,
                             int *use_tls) {
    *use_tls = 0;
    const char *p = url;

    if (strncmp(p, "https://", 8) == 0) {
        *use_tls = 1;
        p += 8;
        *port = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = 80;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= hostlen) hlen = hostlen - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = (uint16_t)atoi(colon + 1);
        p = slash ? slash : p + strlen(p);
    } else if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen >= hostlen) hlen = hostlen - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        p = slash;
    } else {
        size_t hlen = strlen(p);
        if (hlen >= hostlen) hlen = hostlen - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        p = p + hlen;
    }

    if (*p) {
        size_t plen = strlen(p);
        if (plen >= pathlen) plen = pathlen - 1;
        memcpy(path, p, plen);
        path[plen] = '\0';
    } else {
        path[0] = '\0';
    }

    return 0;
}

neverc_httputil_reverse_proxy_t *neverc_httputil_new_single_host_reverse_proxy(
    const char *target_url) {
    if (!target_url) return NULL;

    neverc_httputil_reverse_proxy_t *rp =
        (neverc_httputil_reverse_proxy_t *)calloc(1, sizeof(*rp));
    if (!rp) return NULL;

    if (parse_target_url(target_url,
                          rp->target_host, sizeof(rp->target_host),
                          &rp->target_port,
                          rp->target_path, sizeof(rp->target_path),
                          &rp->use_tls) != 0) {
        free(rp);
        return NULL;
    }

    rp->set_forwarded = 1;

    if (g_proxy_count < MAX_PROXIES)
        g_proxy_table[g_proxy_count++] = rp;

    return rp;
}

void neverc_httputil_proxy_set_rewrite(neverc_httputil_reverse_proxy_t *rp,
                                        neverc_httputil_rewrite_func_t func,
                                        void *user_data) {
    if (!rp) return;
    rp->rewrite_func = func;
    rp->rewrite_data = user_data;
}

void neverc_httputil_proxy_set_error_handler(
    neverc_httputil_reverse_proxy_t *rp,
    neverc_httputil_error_handler_t handler,
    void *user_data) {
    if (!rp) return;
    rp->error_handler = handler;
    rp->error_data = user_data;
}

void neverc_httputil_proxy_set_forwarded_headers(
    neverc_httputil_reverse_proxy_t *rp, int enable) {
    if (rp) rp->set_forwarded = enable;
}

static neverc_httputil_reverse_proxy_t *find_proxy_for_handler(
    neverc_http_handler_func_t handler);

static void reverse_proxy_handler(neverc_http_request_t *req,
                                    neverc_http_response_writer_t *w) {
    neverc_httputil_reverse_proxy_t *rp = NULL;
    for (int i = 0; i < g_proxy_count; i++) {
        rp = g_proxy_table[i];
        if (rp) break;
    }
    if (!rp) {
        neverc_http_error(w, "502 proxy not configured", 502);
        return;
    }

    /* Build the outbound request */
    char out_buf[65536];
    size_t n = 0;

    char full_path[4096];
    int path_len;
    if (rp->target_path[0]) {
        path_len = snprintf(full_path, sizeof(full_path), "%s%s",
                            rp->target_path,
                            req->path ? req->path : "/");
    } else {
        path_len = snprintf(full_path, sizeof(full_path), "%s",
                            req->path ? req->path : "/");
    }
    if (path_len < 0 || (size_t)path_len >= sizeof(full_path))
        goto request_too_large;

    if (req->query && req->query[0]) {
        if (httputil_fixed_appendf(
                out_buf, sizeof(out_buf), &n,
                "%s %s?%s HTTP/1.1\r\nHost: %s\r\n",
                req->method ? req->method : "GET",
                full_path, req->query, rp->target_host) != 0)
            goto request_too_large;
    } else {
        if (httputil_fixed_appendf(
                out_buf, sizeof(out_buf), &n,
                "%s %s HTTP/1.1\r\nHost: %s\r\n",
                req->method ? req->method : "GET",
                full_path, rp->target_host) != 0)
            goto request_too_large;
    }

    /* Forward original headers (skip hop-by-hop) */
    if (req->raw_headers) {
        const char *p = req->raw_headers;
        for (int i = 0; i < req->nheaders; i++) {
            const char *hname = p;
            while (*p) p++;
            p++;
            const char *hval = p;
            while (*p) p++;
            p++;

            /* Skip hop-by-hop headers */
            if (strcasecmp(hname, "Connection") == 0 ||
                strcasecmp(hname, "Keep-Alive") == 0 ||
                strcasecmp(hname, "Transfer-Encoding") == 0 ||
                strcasecmp(hname, "Proxy-Authorization") == 0 ||
                strcasecmp(hname, "TE") == 0 ||
                strcasecmp(hname, "Trailer") == 0 ||
                strcasecmp(hname, "Upgrade") == 0 ||
                strcasecmp(hname, "Host") == 0)
                continue;

            if (httputil_fixed_appendf(
                    out_buf, sizeof(out_buf), &n,
                    "%s: %s\r\n", hname, hval) != 0)
                goto request_too_large;
        }
    }

    /* Add X-Forwarded headers */
    if (rp->set_forwarded) {
        if (httputil_fixed_appendf(
                out_buf, sizeof(out_buf), &n,
                "X-Forwarded-Proto: %s\r\n",
                rp->use_tls ? "https" : "http") != 0)
            goto request_too_large;
        if (req->host &&
            httputil_fixed_appendf(
                out_buf, sizeof(out_buf), &n,
                "X-Forwarded-Host: %s\r\n", req->host) != 0)
            goto request_too_large;
    }

    if (httputil_fixed_appendf(
            out_buf, sizeof(out_buf), &n,
            "Connection: close\r\n") != 0)
        goto request_too_large;

    /* Body */
    if (req->body_len > 0) {
        if (!req->body ||
            httputil_fixed_appendf(
                out_buf, sizeof(out_buf), &n,
                "Content-Length: %zu\r\n\r\n", req->body_len) != 0 ||
            req->body_len > sizeof(out_buf) - n)
            goto request_too_large;
        memcpy(out_buf + n, req->body, req->body_len);
        n += req->body_len;
    } else {
        if (httputil_fixed_appendf(
                out_buf, sizeof(out_buf), &n, "\r\n") != 0)
            goto request_too_large;
    }

    char backend_addr[300];
    int addr_len = snprintf(
        backend_addr, sizeof(backend_addr), "%s:%u",
        rp->target_host, (unsigned)rp->target_port);
    if (addr_len < 0 || (size_t)addr_len >= sizeof(backend_addr))
        goto request_too_large;

    const char *err = NULL;
    neverc_tcp_conn_t *backend = neverc_tcp_dial(backend_addr, &err);
    if (!backend) {
        if (rp->error_handler) {
            rp->error_handler(w, req, err ? err : "connection failed",
                              rp->error_data);
        } else {
            neverc_http_error(w, "502 Bad Gateway", 502);
        }
        return;
    }

    neverc_tcp_set_timeout(backend, 30000);

    /* Send to backend */
    size_t sent = 0;
    while (sent < n) {
        int written = neverc_tcp_write(backend, out_buf + sent, n - sent);
        if (written <= 0) {
            neverc_tcp_close(backend);
            neverc_http_error(w, "502 Bad Gateway", 502);
            return;
        }
        sent += (size_t)written;
    }

    /* Read backend response */
    char resp_buf[65536];
    size_t resp_total = 0;
    int got_headers = 0;
    char *hdr_end_ptr = NULL;
    int backend_status = 502;
    size_t header_size = 0;
    int content_length = -1;

    while (resp_total < sizeof(resp_buf) - 1) {
        int nr = neverc_tcp_read(backend, resp_buf + resp_total,
                                  sizeof(resp_buf) - resp_total - 1);
        if (nr <= 0) break;
        resp_total += (size_t)nr;
        resp_buf[resp_total] = '\0';

        if (!got_headers) {
            hdr_end_ptr = strstr(resp_buf, "\r\n\r\n");
            if (hdr_end_ptr) {
                got_headers = 1;
                header_size = (size_t)(hdr_end_ptr + 4 - resp_buf);

                /* Parse status */
                if (strncmp(resp_buf, "HTTP/", 5) == 0) {
                    const char *sp = strchr(resp_buf, ' ');
                    if (sp) backend_status = atoi(sp + 1);
                }

                /* Parse Content-Length */
                char *cl = strstr(resp_buf, "Content-Length: ");
                if (!cl) cl = strstr(resp_buf, "content-length: ");
                if (cl && cl < hdr_end_ptr)
                    content_length = atoi(cl + 16);

                /* Check if we have the full body */
                if (content_length >= 0 &&
                    resp_total >= header_size + (size_t)content_length)
                    break;
            }
        } else {
            if (content_length >= 0 &&
                resp_total >= header_size + (size_t)content_length)
                break;
        }
    }

    neverc_tcp_close(backend);

    if (!got_headers) {
        neverc_http_error(w, "502 Bad Gateway", 502);
        return;
    }

    /* Forward backend response headers to client */
    neverc_http_set_status(w, backend_status);

    /* Parse and forward response headers */
    const char *hp = resp_buf;
    /* Skip status line */
    while (hp < hdr_end_ptr && !(hp[0] == '\r' && hp[1] == '\n')) hp++;
    if (hp < hdr_end_ptr) hp += 2;

    while (hp < hdr_end_ptr) {
        const char *line_end = NULL;
        for (const char *q = hp; q + 1 < hdr_end_ptr; q++) {
            if (q[0] == '\r' && q[1] == '\n') { line_end = q; break; }
        }
        if (!line_end) break;

        const char *colon = memchr(hp, ':', (size_t)(line_end - hp));
        if (colon) {
            char hname[256], hval[2048];
            size_t nlen = (size_t)(colon - hp);
            if (nlen >= sizeof(hname)) nlen = sizeof(hname) - 1;
            memcpy(hname, hp, nlen);
            hname[nlen] = '\0';

            const char *vp = colon + 1;
            while (vp < line_end && *vp == ' ') vp++;
            size_t vlen = (size_t)(line_end - vp);
            if (vlen >= sizeof(hval)) vlen = sizeof(hval) - 1;
            memcpy(hval, vp, vlen);
            hval[vlen] = '\0';

            /* Skip hop-by-hop headers */
            if (strcasecmp(hname, "Connection") != 0 &&
                strcasecmp(hname, "Transfer-Encoding") != 0 &&
                strcasecmp(hname, "Content-Length") != 0)
                neverc_http_set_header(w, hname, hval);
        }
        hp = line_end + 2;
    }

    /* Forward body */
    if (resp_total > header_size)
        neverc_http_write(w, resp_buf + header_size,
                           resp_total - header_size);
    return;

request_too_large:
    neverc_http_error(w, "502 proxy request too large", 502);
}

neverc_http_handler_func_t neverc_httputil_proxy_handler(
    neverc_httputil_reverse_proxy_t *rp) {
    (void)rp;
    return reverse_proxy_handler;
}

void neverc_httputil_proxy_free(neverc_httputil_reverse_proxy_t *rp) {
    if (!rp) return;
    for (int i = 0; i < g_proxy_count; i++) {
        if (g_proxy_table[i] == rp) {
            g_proxy_table[i] = g_proxy_table[g_proxy_count - 1];
            g_proxy_table[g_proxy_count - 1] = NULL;
            g_proxy_count--;
            break;
        }
    }
    free(rp);
}

/* ======================================================================
 * Request Dumping
 * ====================================================================== */

char *neverc_httputil_dump_request(const neverc_http_request_t *req,
                                    int include_body) {
    if (!req) return NULL;

    size_t cap = 256;
    size_t n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    if (httputil_dump_append_string(
            &buf, &n, &cap, req->method ? req->method : "GET") != 0 ||
        httputil_dump_append_string(&buf, &n, &cap, " ") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap, req->path ? req->path : "/") != 0)
        goto fail;

    if (req->query && req->query[0] &&
        (httputil_dump_append_string(&buf, &n, &cap, "?") != 0 ||
         httputil_dump_append_string(&buf, &n, &cap, req->query) != 0))
        goto fail;

    if (httputil_dump_append_string(&buf, &n, &cap, " ") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap,
            req->http_version ? req->http_version : "HTTP/1.1") != 0 ||
        httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (req->host &&
        (httputil_dump_append_string(
             &buf, &n, &cap, "Host: ") != 0 ||
         httputil_dump_append_string(&buf, &n, &cap, req->host) != 0 ||
         httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0))
        goto fail;

    if (req->raw_headers) {
        const char *p = req->raw_headers;
        for (int i = 0; i < req->nheaders; i++) {
            const char *hname = p;
            while (*p) p++;
            p++;
            const char *hval = p;
            while (*p) p++;
            p++;

            if (strcasecmp(hname, "Host") == 0) continue;
            if (httputil_dump_append_string(
                    &buf, &n, &cap, hname) != 0 ||
                httputil_dump_append_string(
                    &buf, &n, &cap, ": ") != 0 ||
                httputil_dump_append_string(
                    &buf, &n, &cap, hval) != 0 ||
                httputil_dump_append_string(
                    &buf, &n, &cap, "\r\n") != 0)
                goto fail;
        }
    }

    if (httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (include_body && req->body && req->body_len > 0) {
        if (httputil_dump_append(
                &buf, &n, &cap, req->body, req->body_len) != 0)
            goto fail;
    }

    return buf;

fail:
    free(buf);
    return NULL;
}

char *neverc_httputil_dump_request_out(const char *method,
                                        const char *url,
                                        const char *headers,
                                        const char *body,
                                        size_t body_len) {
    if (!body && body_len != 0)
        return NULL;
    size_t cap = 256;
    size_t n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    if (httputil_dump_append_string(
            &buf, &n, &cap, method ? method : "GET") != 0 ||
        httputil_dump_append_string(&buf, &n, &cap, " ") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap, url ? url : "/") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap, " HTTP/1.1\r\n") != 0)
        goto fail;

    if (headers &&
        httputil_dump_append_string(&buf, &n, &cap, headers) != 0)
        goto fail;

    if (body_len > 0) {
        char content_length[64];
        int content_length_size = snprintf(
            content_length, sizeof(content_length),
            "Content-Length: %zu\r\n", body_len);
        if (content_length_size < 0 ||
            (size_t)content_length_size >= sizeof(content_length) ||
            httputil_dump_append(
                &buf, &n, &cap, content_length,
                (size_t)content_length_size) != 0)
            goto fail;
    }

    if (httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (body_len > 0 &&
        httputil_dump_append(
            &buf, &n, &cap, body, body_len) != 0)
        goto fail;

    return buf;

fail:
    free(buf);
    return NULL;
}
