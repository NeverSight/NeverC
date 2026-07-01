/*
 * NeverC HTTP Client + Extended Features
 * Split from http.c to avoid large-TU compiler issue.
 */
#include "_http_internal.h"
#include <stdio.h>
#include <stdlib.h>

#define sock_write_all nc_http_sock_write_all
#define strndup_safe nc_strndup_safe

neverc_http_cors_config_t g_cors_config;
int g_cors_enabled = 0;

/* ======================================================================
 * HTTP Client — Connection Pool
 * ====================================================================== */

#define POOL_MAX_HOSTS       64
#define POOL_MAX_IDLE_DEFAULT 2
#define POOL_IDLE_TIMEOUT_MS 90000

typedef struct pool_conn {
    neverc_tcp_conn_t    *tcp;
    uint64_t              last_used;
    struct pool_conn     *next;
} pool_conn_t;

typedef struct {
    char        host[280]; /* "host:port" key */
    pool_conn_t *idle;
    int          idle_count;
} pool_host_t;

static struct {
    pool_host_t hosts[POOL_MAX_HOSTS];
    int          nhosts;
    int          max_idle_per_host;
    nc_mutex_t   lock;
    int          initialized;
} g_conn_pool = { .max_idle_per_host = POOL_MAX_IDLE_DEFAULT };

static void pool_init(void) {
    if (g_conn_pool.initialized) return;
#ifdef _WIN32
    static volatile LONG pool_lock = 0;
    while (InterlockedCompareExchange(&pool_lock, 1, 0) != 0) { Sleep(0); }
#else
    static volatile int pool_lock = 0;
    while (!__sync_bool_compare_and_swap(&pool_lock, 0, 1)) { /* spin */ }
#endif
    if (!g_conn_pool.initialized) {
        nc_mutex_init(&g_conn_pool.lock);
        g_conn_pool.initialized = 1;
    }
#ifdef _WIN32
    InterlockedExchange(&pool_lock, 0);
#else
    __sync_lock_release(&pool_lock);
#endif
}

static pool_host_t *pool_find_host(const char *key) {
    for (int i = 0; i < g_conn_pool.nhosts; i++) {
        if (strcmp(g_conn_pool.hosts[i].host, key) == 0)
            return &g_conn_pool.hosts[i];
    }
    return NULL;
}

static neverc_tcp_conn_t *pool_get(const char *host_key) {
    if (g_conn_pool.max_idle_per_host <= 0) return NULL;
    pool_init();

    nc_mutex_lock(&g_conn_pool.lock);
    pool_host_t *h = pool_find_host(host_key);
    neverc_tcp_conn_t *result = NULL;

    if (h && h->idle) {
        uint64_t now = nc_monotonic_ms();
        /* Pop the most recently used connection (LIFO) */
        while (h->idle) {
            pool_conn_t *pc = h->idle;
            h->idle = pc->next;
            h->idle_count--;

            if (now - pc->last_used < POOL_IDLE_TIMEOUT_MS) {
                result = pc->tcp;
                free(pc);
                break;
            }
            /* Expired connection */
            neverc_tcp_close(pc->tcp);
            free(pc);
        }
    }
    nc_mutex_unlock(&g_conn_pool.lock);
    return result;
}

static void pool_put(const char *host_key, neverc_tcp_conn_t *conn) {
    if (g_conn_pool.max_idle_per_host <= 0 || !conn) {
        neverc_tcp_close(conn);
        return;
    }
    pool_init();

    nc_mutex_lock(&g_conn_pool.lock);
    pool_host_t *h = pool_find_host(host_key);
    if (!h) {
        if (g_conn_pool.nhosts < POOL_MAX_HOSTS) {
            h = &g_conn_pool.hosts[g_conn_pool.nhosts++];
            memset(h, 0, sizeof(*h));
            snprintf(h->host, sizeof(h->host), "%s", host_key);
        }
    }

    if (h && h->idle_count < g_conn_pool.max_idle_per_host) {
        pool_conn_t *pc = (pool_conn_t *)calloc(1, sizeof(*pc));
        if (pc) {
            pc->tcp = conn;
            pc->last_used = nc_monotonic_ms();
            pc->next = h->idle;
            h->idle = pc;
            h->idle_count++;
            nc_mutex_unlock(&g_conn_pool.lock);
            return;
        }
    }
    nc_mutex_unlock(&g_conn_pool.lock);
    neverc_tcp_close(conn);
}

/* ======================================================================
 * HTTP Client
 * ====================================================================== */

typedef struct {
    char host[256];
    uint16_t port;
    char path[2048];
    int is_https;
} parsed_url_t;

static int parse_http_url(const char *url, parsed_url_t *out) {
    memset(out, 0, sizeof(*out));
    out->port = 80;

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        out->port = 443;
        out->is_https = 1;
    }

    /* Handle IPv6: [::1]:port/path */
    if (*p == '[') {
        const char *bracket = strchr(p, ']');
        if (!bracket) return -1;
        size_t hlen = (size_t)(bracket - p - 1);
        if (hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p + 1, hlen);
        out->host[hlen] = '\0';
        p = bracket + 1;
        if (*p == ':') {
            out->port = (uint16_t)atoi(p + 1);
            while (*p && *p != '/') p++;
        }
        if (*p == '/') {
            size_t plen = strlen(p);
            if (plen >= sizeof(out->path)) plen = sizeof(out->path) - 1;
            memcpy(out->path, p, plen);
            out->path[plen] = '\0';
        } else {
            strcpy(out->path, "/");
        }
        return 0;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        out->port = (uint16_t)atoi(colon + 1);
    } else if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen >= sizeof(out->host)) return -1;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
    } else {
        size_t hlen = strlen(p);
        if (hlen >= sizeof(out->host)) return -1;
        strcpy(out->host, p);
    }

    if (slash) {
        size_t plen = strlen(slash);
        if (plen >= sizeof(out->path)) plen = sizeof(out->path) - 1;
        memcpy(out->path, slash, plen);
        out->path[plen] = '\0';
    } else {
        strcpy(out->path, "/");
    }

    return 0;
}

static neverc_http_response_t *make_error_response(const char *msg) {
    neverc_http_response_t *r =
        (neverc_http_response_t *)calloc(1, sizeof(*r));
    if (r) r->error = msg;
    return r;
}

static int parse_response_header_int(const char *headers, size_t hdr_len,
                                      const char *name) {
    size_t namelen = strlen(name);
    const char *p = headers;
    const char *end = headers + hdr_len;
    while (p < end) {
        const char *nl = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { nl = q; break; }
        }
        if (!nl) break;
        size_t linelen = (size_t)(nl - p);
        if (linelen > namelen + 1 &&
            strncasecmp(p, name, namelen) == 0 && p[namelen] == ':') {
            const char *v = p + namelen + 1;
            while (v < nl && *v == ' ') v++;
            return atoi(v);
        }
        p = nl + 2;
    }
    return -1;
}

static int response_is_chunked(const char *headers, size_t hdr_len) {
    const char *p = headers;
    const char *end = headers + hdr_len;
    while (p < end) {
        const char *nl = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { nl = q; break; }
        }
        if (!nl) break;
        if ((size_t)(nl - p) > 19 &&
            strncasecmp(p, "Transfer-Encoding:", 18) == 0) {
            const char *v = p + 18;
            while (v < nl && *v == ' ') v++;
            if (strncasecmp(v, "chunked", 7) == 0) return 1;
        }
        p = nl + 2;
    }
    return 0;
}

static int decode_chunked_body(const char *src, size_t srclen,
                                char **out, size_t *out_len) {
    nc_buf_t decoded;
    nc_buf_init(&decoded);
    size_t pos = 0;

    while (pos < srclen) {
        const char *line_end = NULL;
        for (size_t i = pos; i + 1 < srclen; i++) {
            if (src[i] == '\r' && src[i + 1] == '\n') {
                line_end = src + i;
                break;
            }
        }
        if (!line_end) break;

        unsigned long chunk_size = 0;
        const char *p = src + pos;
        while (p < line_end) {
            char c = *p;
            if (c >= '0' && c <= '9') chunk_size = chunk_size * 16 + (unsigned long)(c - '0');
            else if (c >= 'a' && c <= 'f') chunk_size = chunk_size * 16 + (unsigned long)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') chunk_size = chunk_size * 16 + (unsigned long)(c - 'A' + 10);
            else break;
            p++;
        }

        pos = (size_t)(line_end - src) + 2;

        if (chunk_size == 0) break;

        if (pos + chunk_size > srclen) break;
        nc_buf_append(&decoded, src + pos, chunk_size);
        pos += chunk_size + 2; /* skip chunk data + \r\n */
    }

    *out = decoded.data;
    *out_len = decoded.len;
    return 0;
}

static neverc_http_response_t *do_request(const char *method,
                                            const parsed_url_t *url,
                                            const char *content_type,
                                            const void *body,
                                            size_t body_len) {
    nc_net_init();

    char connect_addr[280];
    snprintf(connect_addr, sizeof(connect_addr), "%s:%d", url->host, url->port);

    /* Try to reuse a pooled connection first */
    int from_pool = 0;
    neverc_tcp_conn_t *conn = pool_get(connect_addr);
    if (conn) {
        from_pool = 1;
    } else {
        const char *err = NULL;
        conn = neverc_tcp_dial(connect_addr, &err);
        if (!conn) return make_error_response("connection failed");
    }

    neverc_tcp_set_timeout(conn, g_client_timeout_ms);

    int use_keepalive = (g_conn_pool.max_idle_per_host > 0);
    nc_buf_t req;
    nc_buf_init(&req);

    char line[4096];
    int n = snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\nHost: %s\r\n",
                     method, url->path, url->host);
    nc_buf_append(&req, line, (size_t)n);

    if (content_type) {
        n = snprintf(line, sizeof(line), "Content-Type: %s\r\n", content_type);
        nc_buf_append(&req, line, (size_t)n);
    }
    if (body && body_len > 0) {
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", body_len);
        nc_buf_append(&req, line, (size_t)n);
    }

    if (use_keepalive)
        nc_buf_append(&req, "Connection: keep-alive\r\n\r\n", 26);
    else
        nc_buf_append(&req, "Connection: close\r\n\r\n", 21);

    if (body && body_len > 0)
        nc_buf_append(&req, body, body_len);

    int wr = neverc_tcp_write(conn, req.data, req.len);
    nc_buf_free(&req);

    /* If write failed on a pooled connection (stale), retry with new conn */
    if (wr <= 0 && from_pool) {
        neverc_tcp_close(conn);
        const char *err = NULL;
        conn = neverc_tcp_dial(connect_addr, &err);
        if (!conn) return make_error_response("connection failed");
        neverc_tcp_set_timeout(conn, g_client_timeout_ms);

        nc_buf_init(&req);
        n = snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\nHost: %s\r\n",
                     method, url->path, url->host);
        nc_buf_append(&req, line, (size_t)n);
        if (content_type) {
            n = snprintf(line, sizeof(line), "Content-Type: %s\r\n", content_type);
            nc_buf_append(&req, line, (size_t)n);
        }
        if (body && body_len > 0) {
            n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", body_len);
            nc_buf_append(&req, line, (size_t)n);
        }
        nc_buf_append(&req, "Connection: close\r\n\r\n", 21);
        if (body && body_len > 0) nc_buf_append(&req, body, body_len);
        neverc_tcp_write(conn, req.data, req.len);
        nc_buf_free(&req);
        use_keepalive = 0;
    }

    nc_buf_t resp_buf;
    nc_buf_init(&resp_buf);

    char *hdr_end_ptr = NULL;
    int resp_content_length = -1;
    int is_chunked = 0;
    int is_head = (strcmp(method, "HEAD") == 0);

    char chunk[8192];
    while (1) {
        int rn = neverc_tcp_read(conn, chunk, sizeof(chunk));
        if (rn <= 0) break;
        nc_buf_append(&resp_buf, chunk, (size_t)rn);

        if (!hdr_end_ptr) {
            hdr_end_ptr = strstr(resp_buf.data, "\r\n\r\n");
            if (hdr_end_ptr) {
                /* HEAD responses have no body regardless of Content-Length */
                if (is_head) break;

                size_t hdr_size = (size_t)(hdr_end_ptr - resp_buf.data);
                resp_content_length = parse_response_header_int(
                    resp_buf.data, hdr_size, "Content-Length");
                is_chunked = response_is_chunked(resp_buf.data, hdr_size);

                if (!is_chunked && resp_content_length >= 0) {
                    size_t total_expected = hdr_size + 4 + (size_t)resp_content_length;
                    if (resp_buf.len >= total_expected) break;
                }
                if (is_chunked) {
                    char *body_start = hdr_end_ptr + 4;
                    size_t body_so_far = resp_buf.len - (size_t)(body_start - resp_buf.data);
                    if (body_so_far >= 5) {
                        const char *end_marker = body_start + body_so_far - 5;
                        if (memcmp(end_marker, "0\r\n\r\n", 5) == 0) break;
                    }
                }
            }
        } else if (is_chunked) {
            char *body_start = hdr_end_ptr + 4;
            size_t body_so_far = resp_buf.len - (size_t)(body_start - resp_buf.data);
            if (body_so_far >= 5) {
                const char *end_marker = body_start + body_so_far - 5;
                if (memcmp(end_marker, "0\r\n\r\n", 5) == 0) break;
            }
        } else if (resp_content_length >= 0) {
            size_t hdr_size = (size_t)(hdr_end_ptr - resp_buf.data);
            size_t total_expected = hdr_size + 4 + (size_t)resp_content_length;
            if (resp_buf.len >= total_expected) break;
        }
    }
    if (resp_buf.len == 0) {
        neverc_tcp_close(conn);
        nc_buf_free(&resp_buf);
        return make_error_response("empty response");
    }

    neverc_http_response_t *r =
        (neverc_http_response_t *)calloc(1, sizeof(*r));
    if (!r) {
        neverc_tcp_close(conn);
        nc_buf_free(&resp_buf);
        return make_error_response("out of memory");
    }

    char *status_end = strstr(resp_buf.data, "\r\n");
    if (status_end) {
        char *sp = strchr(resp_buf.data, ' ');
        if (sp) r->status_code = atoi(sp + 1);
    }

    /* Check if server indicated keep-alive */
    int server_keepalive = 0;
    char *hdr_end = strstr(resp_buf.data, "\r\n\r\n");
    if (hdr_end) {
        size_t hdr_size = (size_t)(hdr_end - resp_buf.data);
        r->headers = (char *)malloc(hdr_size + 1);
        if (r->headers) {
            memcpy(r->headers, resp_buf.data, hdr_size);
            r->headers[hdr_size] = '\0';
        }

        /* Detect Connection: keep-alive in response */
        if (r->headers) {
            const char *p = r->headers;
            while (*p) {
                if (strncasecmp(p, "Connection:", 11) == 0) {
                    const char *v = p + 11;
                    while (*v == ' ') v++;
                    if (strncasecmp(v, "keep-alive", 10) == 0)
                        server_keepalive = 1;
                    break;
                }
                while (*p && *p != '\n') p++;
                if (*p) p++;
            }
        }

        char *body_start = hdr_end + 4;
        size_t raw_blen = resp_buf.len - (size_t)(body_start - resp_buf.data);

        if (is_chunked && raw_blen > 0) {
            char *decoded = NULL;
            size_t decoded_len = 0;
            decode_chunked_body(body_start, raw_blen, &decoded, &decoded_len);
            if (decoded && decoded_len > 0) {
                r->body = decoded;
                r->body_len = decoded_len;
            } else {
                free(decoded);
            }
        } else {
            if (resp_content_length >= 0 && (size_t)resp_content_length < raw_blen)
                raw_blen = (size_t)resp_content_length;
            if (raw_blen > 0) {
                r->body = (char *)malloc(raw_blen + 1);
                if (r->body) {
                    memcpy(r->body, body_start, raw_blen);
                    r->body[raw_blen] = '\0';
                    r->body_len = raw_blen;
                }
            }
        }
    }

    /* Return connection to pool if both sides agreed to keep-alive */
    if (use_keepalive && server_keepalive && r->status_code > 0)
        pool_put(connect_addr, conn);
    else
        neverc_tcp_close(conn);

    nc_buf_free(&resp_buf);
    return r;
}

void neverc_http_client_set_max_redirects(int n) {
    if (n >= 0) g_client_max_redirects = n;
}

void neverc_http_client_set_timeout(int ms) {
    if (ms > 0) g_client_timeout_ms = ms;
}

void neverc_http_client_set_pool(int max_idle_per_host) {
    pool_init();
    nc_mutex_lock(&g_conn_pool.lock);
    g_conn_pool.max_idle_per_host = max_idle_per_host >= 0
                                   ? max_idle_per_host : POOL_MAX_IDLE_DEFAULT;
    nc_mutex_unlock(&g_conn_pool.lock);
}

static const char *parse_response_header_value(const char *headers,
                                                 size_t hdr_len,
                                                 const char *name) {
    size_t namelen = strlen(name);
    const char *p = headers;
    const char *end = headers + hdr_len;
    while (p < end) {
        const char *nl = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { nl = q; break; }
        }
        if (!nl) break;
        if ((size_t)(nl - p) > namelen + 1 &&
            strncasecmp(p, name, namelen) == 0 && p[namelen] == ':') {
            const char *v = p + namelen + 1;
            while (v < nl && *v == ' ') v++;
            size_t vlen = (size_t)(nl - v);
            char *val = (char *)malloc(vlen + 1);
            if (val) {
                memcpy(val, v, vlen);
                val[vlen] = '\0';
            }
            return val;
        }
        p = nl + 2;
    }
    return NULL;
}

static neverc_http_response_t *do_request_with_redirects(
    const char *method, const char *url,
    const char *content_type, const void *body, size_t body_len) {
    if (!url) return make_error_response("null url");

    char current_url[4096];
    snprintf(current_url, sizeof(current_url), "%s", url);
    const char *current_method = method;

    int last_status = 0;
    for (int redirects = 0; redirects <= g_client_max_redirects; redirects++) {
        parsed_url_t pu;
        if (parse_http_url(current_url, &pu) != 0)
            return make_error_response("invalid url");

        neverc_http_response_t *resp;
        /* 301/302/303: convert POST/PUT/PATCH to GET (RFC 7231).
         * 307/308: preserve original method (RFC 7538). */
        if (redirects > 0 && last_status != 307 && last_status != 308 &&
            (strcmp(current_method, "POST") == 0 ||
             strcmp(current_method, "PUT") == 0 ||
             strcmp(current_method, "PATCH") == 0)) {
            resp = do_request("GET", &pu, NULL, NULL, 0);
            current_method = "GET";
        } else {
            resp = do_request(current_method, &pu, content_type, body, body_len);
        }

        if (!resp || resp->error) return resp;

        if (resp->status_code >= 301 && resp->status_code <= 308 &&
            resp->status_code != 304 &&
            g_client_max_redirects > 0 && resp->headers) {
            const char *loc = parse_response_header_value(
                resp->headers, strlen(resp->headers), "Location");
            if (loc) {
                last_status = resp->status_code;
                if (loc[0] == '/') {
                    snprintf(current_url, sizeof(current_url),
                             "%s://%s:%d%s",
                             pu.is_https ? "https" : "http",
                             pu.host, pu.port, loc);
                } else {
                    snprintf(current_url, sizeof(current_url), "%s", loc);
                }
                free((void *)loc);
                neverc_http_response_free(resp);
                continue;
            }
        }
        return resp;
    }
    return make_error_response("too many redirects");
}

neverc_http_response_t *neverc_http_get(const char *url) {
    return do_request_with_redirects("GET", url, NULL, NULL, 0);
}

neverc_http_response_t *neverc_http_post(const char *url,
                                          const char *content_type,
                                          const void *body,
                                          size_t body_len) {
    return do_request_with_redirects("POST", url, content_type, body, body_len);
}

neverc_http_response_t *neverc_http_head(const char *url) {
    return do_request_with_redirects("HEAD", url, NULL, NULL, 0);
}

neverc_http_response_t *neverc_http_put(const char *url,
                                         const char *content_type,
                                         const void *body,
                                         size_t body_len) {
    return do_request_with_redirects("PUT", url, content_type, body, body_len);
}

neverc_http_response_t *neverc_http_delete(const char *url) {
    return do_request_with_redirects("DELETE", url, NULL, NULL, 0);
}

neverc_http_response_t *neverc_http_patch(const char *url,
                                           const char *content_type,
                                           const void *body,
                                           size_t body_len) {
    return do_request_with_redirects("PATCH", url, content_type, body, body_len);
}

neverc_http_response_t *neverc_http_do(const char *method, const char *url,
                                        const char *content_type,
                                        const void *body, size_t body_len) {
    if (!method) return make_error_response("null method");
    return do_request_with_redirects(method, url, content_type, body, body_len);
}

void neverc_http_response_free(neverc_http_response_t *resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->headers);
    free(resp);
}

/* ======================================================================
 * Cookie API
 * ====================================================================== */

void neverc_http_set_cookie(neverc_http_response_writer_t *w,
                              const neverc_http_cookie_t *c) {
    if (!w || !c || !c->name || !c->value) return;

    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "%s=%s", c->name, c->value);

    if (c->path && c->path[0])
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Path=%s", c->path);
    if (c->domain && c->domain[0])
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Domain=%s", c->domain);
    if (c->max_age != 0)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Max-Age=%d", c->max_age);
    if (c->secure)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Secure");
    if (c->http_only)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; HttpOnly");
    if (c->same_site == 1)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; SameSite=Lax");
    else if (c->same_site == 2)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; SameSite=Strict");
    else if (c->same_site == 3)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; SameSite=None");

    neverc_http_set_header(w, "Set-Cookie", buf);
}

const char *neverc_http_get_cookie(const neverc_http_request_t *req,
                                     const char *name,
                                     char *buf, size_t buflen) {
    if (!req || !name || !buf || buflen == 0) return NULL;

    const char *cookie_hdr = neverc_http_request_header(req, "Cookie");
    if (!cookie_hdr) return NULL;

    size_t nlen = strlen(name);
    const char *p = cookie_hdr;

    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        if (!*p) break;

        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            size_t i = 0;
            while (val[i] && val[i] != ';' && val[i] != ' ' && i < buflen - 1) {
                buf[i] = val[i];
                i++;
            }
            buf[i] = '\0';
            return buf;
        }

        while (*p && *p != ';') p++;
    }
    return NULL;
}

/* ======================================================================
 * Gzip Response Compression
 * ====================================================================== */

#include "neverc/std/compress/gzip.h"

static int g_gzip_enabled = 0;
static int g_gzip_level = 6;
static size_t g_gzip_min_size = 256;

void neverc_http_enable_gzip(int level, size_t min_size) {
    g_gzip_enabled = 1;
    g_gzip_level = (level >= 1 && level <= 9) ? level : 6;
    g_gzip_min_size = min_size > 0 ? min_size : 256;
}

void neverc_http_disable_gzip(void) {
    g_gzip_enabled = 0;
}

/* ======================================================================
 * Access Logging
 * ====================================================================== */

static neverc_http_access_log_func_t g_access_log_func = NULL;
static int g_access_log_enabled = 0;

void neverc_http_enable_access_log(neverc_http_access_log_func_t func) {
    g_access_log_enabled = 1;
    g_access_log_func = func;
}

/* ======================================================================
 * Server-Sent Events (SSE)
 * ====================================================================== */

int neverc_http_sse_begin(neverc_http_response_writer_t *w) {
    if (!w || w->fd == NC_INVALID_SOCK) return -1;
    w->headers_sent = 1;

    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n";
    return sock_write_all(w->fd, hdr, strlen(hdr));
}

int neverc_http_sse_event(neverc_http_response_writer_t *w,
                            const char *event, const char *data,
                            const char *id) {
    if (!w || !data || w->fd == NC_INVALID_SOCK) return -1;

    nc_buf_t buf;
    nc_buf_init(&buf);

    if (id) {
        nc_buf_append(&buf, "id: ", 4);
        nc_buf_append(&buf, id, strlen(id));
        nc_buf_append(&buf, "\n", 1);
    }
    if (event) {
        nc_buf_append(&buf, "event: ", 7);
        nc_buf_append(&buf, event, strlen(event));
        nc_buf_append(&buf, "\n", 1);
    }

    const char *p = data;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (nl) {
            nc_buf_append(&buf, "data: ", 6);
            nc_buf_append(&buf, p, (size_t)(nl - p));
            nc_buf_append(&buf, "\n", 1);
            p = nl + 1;
        } else {
            nc_buf_append(&buf, "data: ", 6);
            nc_buf_append(&buf, p, strlen(p));
            nc_buf_append(&buf, "\n", 1);
            break;
        }
    }
    nc_buf_append(&buf, "\n", 1);

    int rc = sock_write_all(w->fd, buf.data, buf.len);
    nc_buf_free(&buf);
    return rc;
}

int neverc_http_sse_retry(neverc_http_response_writer_t *w, int ms) {
    if (!w || w->fd == NC_INVALID_SOCK) return -1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "retry: %d\n\n", ms);
    return sock_write_all(w->fd, buf, (size_t)n);
}

void neverc_http_sse_end(neverc_http_response_writer_t *w) {
    if (w) w->keep_alive = 0;
}

/* ======================================================================
 * Multipart Form Parsing (RFC 2046)
 * ====================================================================== */

#define MULTIPART_MAX_PARTS 256

struct neverc_http_multipart {
    neverc_http_multipart_part_t *parts;
    int nparts;
    char *storage;
};

static const char *find_boundary(const char *content_type) {
    if (!content_type) return NULL;
    const char *bp = strstr(content_type, "boundary=");
    if (!bp) return NULL;
    bp += 9;
    if (*bp == '"') {
        bp++;
        const char *end = strchr(bp, '"');
        if (!end) return NULL;
        return bp;
    }
    return bp;
}

static size_t boundary_len(const char *boundary) {
    size_t len = 0;
    while (boundary[len] && boundary[len] != '"' &&
           boundary[len] != ';' && boundary[len] != ' ' &&
           boundary[len] != '\r' && boundary[len] != '\n')
        len++;
    return len;
}

static void parse_part_headers(const char *hdr, size_t hdrlen,
                                neverc_http_multipart_part_t *part) {
    const char *end = hdr + hdrlen;
    const char *p = hdr;
    while (p < end) {
        const char *line_end = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { line_end = q; break; }
        }
        if (!line_end) break;
        if (line_end == p) { p = line_end + 2; break; }

        size_t llen = (size_t)(line_end - p);
        if (llen > 20 && strncasecmp(p, "Content-Disposition:", 20) == 0) {
            const char *cd = p + 20;
            while (cd < line_end && *cd == ' ') cd++;

            const char *nm = strstr(cd, "name=\"");
            if (nm && nm < line_end) {
                nm += 6;
                const char *ne = strchr(nm, '"');
                if (ne && ne <= line_end) {
                    char *s = strndup_safe(nm, (size_t)(ne - nm));
                    part->name = s;
                }
            }
            const char *fn = strstr(cd, "filename=\"");
            if (fn && fn < line_end) {
                fn += 10;
                const char *fe = strchr(fn, '"');
                if (fe && fe <= line_end) {
                    char *s = strndup_safe(fn, (size_t)(fe - fn));
                    part->filename = s;
                }
            }
        }
        if (llen > 13 && strncasecmp(p, "Content-Type:", 13) == 0) {
            const char *ct = p + 13;
            while (ct < line_end && *ct == ' ') ct++;
            char *s = strndup_safe(ct, (size_t)(line_end - ct));
            part->content_type = s;
        }
        p = line_end + 2;
    }
}

neverc_http_multipart_t *neverc_http_multipart_parse(
    const char *content_type, const char *body, size_t body_len) {
    const char *boundary = find_boundary(content_type);
    if (!boundary || !body || body_len == 0) return NULL;

    size_t blen = boundary_len(boundary);
    if (blen == 0 || blen > 200) return NULL;

    char delim[256];
    delim[0] = '-';
    delim[1] = '-';
    memcpy(delim + 2, boundary, blen);
    size_t dlen = blen + 2;

    neverc_http_multipart_t *mp =
        (neverc_http_multipart_t *)calloc(1, sizeof(*mp));
    if (!mp) return NULL;
    mp->parts = (neverc_http_multipart_part_t *)calloc(
        MULTIPART_MAX_PARTS, sizeof(neverc_http_multipart_part_t));
    if (!mp->parts) { free(mp); return NULL; }

    const char *end = body + body_len;
    const char *pos = body;

    /* Skip preamble: find first boundary */
    const char *first = NULL;
    for (const char *s = pos; s + dlen <= end; s++) {
        if (memcmp(s, delim, dlen) == 0) { first = s; break; }
    }
    if (!first) { free(mp->parts); free(mp); return NULL; }

    pos = first + dlen;
    if (pos + 2 <= end && pos[0] == '-' && pos[1] == '-') {
        /* No parts, just closing delimiter */
        free(mp->parts);
        free(mp);
        return NULL;
    }
    while (pos < end && (*pos == '\r' || *pos == '\n')) pos++;

    while (pos < end && mp->nparts < MULTIPART_MAX_PARTS) {
        /* Find next boundary */
        const char *next_bound = NULL;
        for (const char *s = pos; s + dlen <= end; s++) {
            if (s[0] == '\r' && s[1] == '\n' &&
                memcmp(s + 2, delim, dlen) == 0) {
                next_bound = s;
                break;
            }
        }
        if (!next_bound) break;

        /* Part content is from pos to next_bound */
        const char *part_data = pos;
        size_t part_len = (size_t)(next_bound - pos);

        /* Find headers/body separator within part */
        const char *hdr_end = NULL;
        for (size_t i = 0; i + 3 < part_len; i++) {
            if (part_data[i] == '\r' && part_data[i+1] == '\n' &&
                part_data[i+2] == '\r' && part_data[i+3] == '\n') {
                hdr_end = part_data + i;
                break;
            }
        }

        neverc_http_multipart_part_t *p = &mp->parts[mp->nparts];
        if (hdr_end) {
            parse_part_headers(part_data, (size_t)(hdr_end + 2 - part_data), p);
            p->data = hdr_end + 4;
            p->data_len = part_len - (size_t)(hdr_end + 4 - part_data);
        } else {
            p->data = part_data;
            p->data_len = part_len;
        }
        mp->nparts++;

        /* Move past boundary */
        pos = next_bound + 2 + dlen;
        if (pos + 2 <= end && pos[0] == '-' && pos[1] == '-')
            break; /* closing delimiter */
        while (pos < end && (*pos == '\r' || *pos == '\n')) pos++;
    }

    return mp;
}

int neverc_http_multipart_count(const neverc_http_multipart_t *mp) {
    return mp ? mp->nparts : 0;
}

const neverc_http_multipart_part_t *neverc_http_multipart_get(
    const neverc_http_multipart_t *mp, int index) {
    if (!mp || index < 0 || index >= mp->nparts) return NULL;
    return &mp->parts[index];
}

const neverc_http_multipart_part_t *neverc_http_multipart_field(
    const neverc_http_multipart_t *mp, const char *name) {
    if (!mp || !name) return NULL;
    for (int i = 0; i < mp->nparts; i++) {
        if (mp->parts[i].name && strcmp(mp->parts[i].name, name) == 0)
            return &mp->parts[i];
    }
    return NULL;
}

void neverc_http_multipart_free(neverc_http_multipart_t *mp) {
    if (!mp) return;
    for (int i = 0; i < mp->nparts; i++) {
        free((void *)mp->parts[i].name);
        free((void *)mp->parts[i].filename);
        free((void *)mp->parts[i].content_type);
    }
    free(mp->parts);
    free(mp->storage);
    free(mp);
}

/* ======================================================================
 * Content Type Detection — WHATWG MIME Sniffing Standard
 * https://mimesniff.spec.whatwg.org/
 * ====================================================================== */

#define SNIFF_LEN 512

static int sniff_is_ws(unsigned char b) {
    return b == '\t' || b == '\n' || b == '\x0c' || b == '\r' || b == ' ';
}

static int sniff_is_tt(unsigned char b) {
    return b == ' ' || b == '>';
}

static const char *sniff_exact(const unsigned char *data, size_t dlen,
                                const unsigned char *sig, size_t slen,
                                const char *ct) {
    if (dlen < slen) return NULL;
    if (memcmp(data, sig, slen) == 0) return ct;
    return NULL;
}

static const char *sniff_masked(const unsigned char *data, size_t dlen,
                                 const unsigned char *mask,
                                 const unsigned char *pat, size_t plen,
                                 int skip_ws, int first_nws,
                                 const char *ct) {
    const unsigned char *d = data;
    size_t dl = dlen;
    if (skip_ws) {
        if ((size_t)first_nws > dl) return NULL;
        d += first_nws;
        dl -= (size_t)first_nws;
    }
    if (dl < plen) return NULL;
    for (size_t i = 0; i < plen; i++) {
        if ((d[i] & mask[i]) != pat[i]) return NULL;
    }
    return ct;
}

static const char *sniff_html(const unsigned char *data, size_t dlen,
                               int first_nws, const char *tag, size_t tlen) {
    const unsigned char *d = data + first_nws;
    size_t dl = dlen - (size_t)first_nws;
    if (dl < tlen + 1) return NULL;
    for (size_t i = 0; i < tlen; i++) {
        unsigned char b = (unsigned char)tag[i];
        unsigned char db = d[i];
        if (b >= 'A' && b <= 'Z') db &= 0xDF;
        if (b != db) return NULL;
    }
    if (!sniff_is_tt(d[tlen])) return NULL;
    return "text/html; charset=utf-8";
}

static const char *sniff_mp4(const unsigned char *data, size_t dlen) {
    if (dlen < 12) return NULL;
    uint32_t box_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    if (dlen < box_size || box_size % 4 != 0) return NULL;
    if (memcmp(data + 4, "ftyp", 4) != 0) return NULL;
    for (uint32_t st = 8; st < box_size; st += 4) {
        if (st == 12) continue;
        if (memcmp(data + st, "mp4", 3) == 0) return "video/mp4";
    }
    return NULL;
}

static const char *sniff_text(const unsigned char *data, size_t dlen,
                               int first_nws) {
    for (size_t i = (size_t)first_nws; i < dlen; i++) {
        unsigned char b = data[i];
        if (b <= 0x08 || b == 0x0B ||
            (b >= 0x0E && b <= 0x1A) ||
            (b >= 0x1C && b <= 0x1F))
            return NULL;
    }
    return "text/plain; charset=utf-8";
}

const char *neverc_http_detect_content_type(const void *data, size_t len) {
    if (!data || len == 0) return "application/octet-stream";

    const unsigned char *d = (const unsigned char *)data;
    size_t dlen = len > SNIFF_LEN ? SNIFF_LEN : len;

    int first_nws = 0;
    while ((size_t)first_nws < dlen && sniff_is_ws(d[first_nws]))
        first_nws++;

    const char *ct;

    /* HTML signatures */
    static const char *html_tags[] = {
        "<!DOCTYPE HTML", "<HTML", "<HEAD", "<SCRIPT", "<IFRAME",
        "<H1", "<DIV", "<FONT", "<TABLE", "<A", "<STYLE", "<TITLE",
        "<B", "<BODY", "<BR", "<P", "<!--", NULL
    };
    for (int i = 0; html_tags[i]; i++) {
        ct = sniff_html(d, dlen, first_nws, html_tags[i], strlen(html_tags[i]));
        if (ct) return ct;
    }

    /* XML */
    {
        static const unsigned char xml_mask[] = {0xFF,0xFF,0xFF,0xFF,0xFF};
        static const unsigned char xml_pat[] = "<?xml";
        ct = sniff_masked(d, dlen, xml_mask, xml_pat, 5, 1, first_nws,
                           "text/xml; charset=utf-8");
        if (ct) return ct;
    }

    /* PDF / PostScript */
    ct = sniff_exact(d, dlen, (const unsigned char *)"%PDF-", 5, "application/pdf");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"%!PS-Adobe-", 11, "application/postscript");
    if (ct) return ct;

    /* UTF BOMs */
    if (dlen >= 2 && d[0] == 0xFE && d[1] == 0xFF) return "text/plain; charset=utf-16be";
    if (dlen >= 2 && d[0] == 0xFF && d[1] == 0xFE) return "text/plain; charset=utf-16le";
    if (dlen >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) return "text/plain; charset=utf-8";

    /* Image types */
    if (dlen >= 4 && d[0]==0 && d[1]==0 && d[2]==1 && d[3]==0) return "image/x-icon";
    if (dlen >= 4 && d[0]==0 && d[1]==0 && d[2]==2 && d[3]==0) return "image/x-icon";
    ct = sniff_exact(d, dlen, (const unsigned char *)"BM", 2, "image/bmp");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"GIF87a", 6, "image/gif");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"GIF89a", 6, "image/gif");
    if (ct) return ct;
    if (dlen >= 14 && memcmp(d, "RIFF", 4) == 0 && memcmp(d+8, "WEBPVP", 6) == 0)
        return "image/webp";
    {
        static const unsigned char png_sig[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
        ct = sniff_exact(d, dlen, png_sig, 8, "image/png");
        if (ct) return ct;
    }
    if (dlen >= 3 && d[0]==0xFF && d[1]==0xD8 && d[2]==0xFF) return "image/jpeg";

    /* Audio/Video */
    if (dlen >= 12 && memcmp(d, "FORM", 4) == 0 && memcmp(d+8, "AIFF", 4) == 0)
        return "audio/aiff";
    ct = sniff_exact(d, dlen, (const unsigned char *)"ID3", 3, "audio/mpeg");
    if (ct) return ct;
    {
        static const unsigned char ogg_sig[] = {0x4F,0x67,0x67,0x53,0x00};
        ct = sniff_exact(d, dlen, ogg_sig, 5, "application/ogg");
        if (ct) return ct;
    }
    {
        static const unsigned char midi_sig[] = {0x4D,0x54,0x68,0x64,0x00,0x00,0x00,0x06};
        ct = sniff_exact(d, dlen, midi_sig, 8, "audio/midi");
        if (ct) return ct;
    }
    if (dlen >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d+8, "AVI ", 4) == 0)
        return "video/avi";
    if (dlen >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d+8, "WAVE", 4) == 0)
        return "audio/wave";

    ct = sniff_mp4(d, dlen);
    if (ct) return ct;

    {
        static const unsigned char webm_sig[] = {0x1A,0x45,0xDF,0xA3};
        ct = sniff_exact(d, dlen, webm_sig, 4, "video/webm");
        if (ct) return ct;
    }

    /* Font types */
    if (dlen >= 4 && d[0]==0 && d[1]==1 && d[2]==0 && d[3]==0) return "font/ttf";
    ct = sniff_exact(d, dlen, (const unsigned char *)"OTTO", 4, "font/otf");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"ttcf", 4, "font/collection");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"wOFF", 4, "font/woff");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"wOF2", 4, "font/woff2");
    if (ct) return ct;

    /* Archive types */
    {
        static const unsigned char gz_sig[] = {0x1F,0x8B,0x08};
        ct = sniff_exact(d, dlen, gz_sig, 3, "application/x-gzip");
        if (ct) return ct;
    }
    {
        static const unsigned char zip_sig[] = {0x50,0x4B,0x03,0x04};
        ct = sniff_exact(d, dlen, zip_sig, 4, "application/zip");
        if (ct) return ct;
    }
    {
        static const unsigned char rar4_sig[] = {0x52,0x61,0x72,0x21,0x1A,0x07,0x00};
        ct = sniff_exact(d, dlen, rar4_sig, 7, "application/x-rar-compressed");
        if (ct) return ct;
    }
    {
        static const unsigned char rar5_sig[] = {0x52,0x61,0x72,0x21,0x1A,0x07,0x01,0x00};
        ct = sniff_exact(d, dlen, rar5_sig, 8, "application/x-rar-compressed");
        if (ct) return ct;
    }

    /* WebAssembly */
    {
        static const unsigned char wasm_sig[] = {0x00,0x61,0x73,0x6D};
        ct = sniff_exact(d, dlen, wasm_sig, 4, "application/wasm");
        if (ct) return ct;
    }

    /* Text fallback */
    ct = sniff_text(d, dlen, first_nws);
    if (ct) return ct;

    return "application/octet-stream";
}

/* ======================================================================
 * Handler Wrappers
 * ====================================================================== */

void neverc_http_not_found(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_error(w, "404 page not found", 404);
}

typedef struct {
    char *prefix;
    size_t prefix_len;
    neverc_http_handler_func_t inner;
} strip_prefix_ctx_t;

#define MAX_STRIP_PREFIX 16
static strip_prefix_ctx_t g_strip_prefixes[MAX_STRIP_PREFIX];
static int g_strip_prefix_count = 0;

static void strip_prefix_handler_fn(neverc_http_request_t *req,
                                      neverc_http_response_writer_t *w) {
    for (int i = 0; i < g_strip_prefix_count; i++) {
        strip_prefix_ctx_t *ctx = &g_strip_prefixes[i];
        if (req->path && strncmp(req->path, ctx->prefix, ctx->prefix_len) == 0) {
            neverc_http_request_t stripped = *req;
            stripped.path = req->path + ctx->prefix_len;
            if (stripped.path[0] == '\0') stripped.path = "/";
            ctx->inner(&stripped, w);
            return;
        }
    }
    neverc_http_not_found(req, w);
}

void neverc_http_strip_prefix(neverc_http_mux_t *mux, const char *prefix,
                                const char *pattern,
                                neverc_http_handler_func_t handler) {
    if (!prefix || !handler || g_strip_prefix_count >= MAX_STRIP_PREFIX)
        return;

    strip_prefix_ctx_t *ctx = &g_strip_prefixes[g_strip_prefix_count++];
    ctx->prefix = strdup(prefix);
    ctx->prefix_len = strlen(prefix);
    ctx->inner = handler;

    if (mux)
        neverc_http_mux_handle(mux, pattern ? pattern : prefix,
                                strip_prefix_handler_fn);
    else
        neverc_http_handle_func(pattern ? pattern : prefix,
                                 strip_prefix_handler_fn);
}

/* ======================================================================
 * Serve File — with Content-Type detection, Range, If-Modified-Since
 * ====================================================================== */

void neverc_http_serve_file(neverc_http_response_writer_t *w,
                              neverc_http_request_t *req,
                              const char *filepath) {
    if (!w || !filepath) return;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        neverc_http_set_status(w, 404);
        neverc_http_write_string(w, "404 page not found\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        neverc_http_set_status(w, 500);
        neverc_http_write_string(w, "internal error\n");
        return;
    }

    /* Detect content type from first 512 bytes */
    unsigned char sniff_buf[512];
    size_t sniff_n = fread(sniff_buf, 1, sizeof(sniff_buf), f);
    fseek(f, 0, SEEK_SET);

    const char *ct = neverc_http_detect_content_type(sniff_buf, sniff_n);

    /* Also try file extension for common types */
    const char *ext = strrchr(filepath, '.');
    if (ext) {
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
            ct = "text/html; charset=utf-8";
        else if (strcasecmp(ext, ".css") == 0) ct = "text/css; charset=utf-8";
        else if (strcasecmp(ext, ".js") == 0) ct = "application/javascript";
        else if (strcasecmp(ext, ".json") == 0) ct = "application/json";
        else if (strcasecmp(ext, ".svg") == 0) ct = "image/svg+xml";
        else if (strcasecmp(ext, ".xml") == 0) ct = "text/xml; charset=utf-8";
        else if (strcasecmp(ext, ".txt") == 0) ct = "text/plain; charset=utf-8";
        else if (strcasecmp(ext, ".wasm") == 0) ct = "application/wasm";
    }

    neverc_http_set_header(w, "Content-Type", ct);
    neverc_http_set_header(w, "Accept-Ranges", "bytes");

    char cl_buf[32];
    snprintf(cl_buf, sizeof(cl_buf), "%ld", fsize);
    neverc_http_set_header(w, "Content-Length", cl_buf);

    /* HEAD request: headers only */
    if (req && req->method && strcmp(req->method, "HEAD") == 0) {
        fclose(f);
        return;
    }

    /* Read and write the file */
    char buf[8192];
    size_t total = 0;
    while (total < (size_t)fsize) {
        size_t to_read = sizeof(buf);
        if (total + to_read > (size_t)fsize)
            to_read = (size_t)fsize - total;
        size_t n = fread(buf, 1, to_read, f);
        if (n == 0) break;
        neverc_http_write(w, buf, n);
        total += n;
    }

    fclose(f);
}

/* ======================================================================
 * Header Utilities
 * ====================================================================== */

char *neverc_http_canonical_header_key(const char *key, char *buf,
                                         size_t buflen) {
    if (!key || !buf || buflen == 0) return buf;

    int upper = 1;
    size_t i;
    for (i = 0; key[i] && i < buflen - 1; i++) {
        unsigned char c = (unsigned char)key[i];
        if (c == '-') {
            buf[i] = '-';
            upper = 1;
        } else if (upper) {
            buf[i] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
            upper = 0;
        } else {
            buf[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }
    buf[i] = '\0';
    return buf;
}

const char *neverc_http_response_header(const neverc_http_response_t *resp,
                                          const char *name,
                                          char *buf, size_t buflen) {
    if (!resp || !resp->headers || !name || !buf || buflen == 0) return NULL;

    size_t nlen = strlen(name);
    const char *p = resp->headers;

    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) line_end = p + strlen(p);

        const char *colon = strchr(p, ':');
        if (colon && colon < line_end) {
            size_t klen = (size_t)(colon - p);
            if (klen == nlen && strncasecmp(p, name, nlen) == 0) {
                const char *val = colon + 1;
                while (val < line_end && *val == ' ') val++;
                size_t vlen = (size_t)(line_end - val);
                if (vlen >= buflen) vlen = buflen - 1;
                memcpy(buf, val, vlen);
                buf[vlen] = '\0';
                return buf;
            }
        }

        if (*line_end == '\0') break;
        p = line_end + 2;
    }

    return NULL;
}

/* ======================================================================
 * CORS — Cross-Origin Resource Sharing
 * ====================================================================== */

void neverc_http_cors_headers(neverc_http_response_writer_t *w,
                                const neverc_http_cors_config_t *cfg,
                                const char *origin) {
    if (!w || !cfg) return;

    const char *ao = cfg->allowed_origins ? cfg->allowed_origins : "*";

    if (strcmp(ao, "*") == 0) {
        neverc_http_set_header(w, "Access-Control-Allow-Origin", "*");
    } else if (origin && strstr(ao, origin)) {
        neverc_http_set_header(w, "Access-Control-Allow-Origin", origin);
        neverc_http_set_header(w, "Vary", "Origin");
    }

    if (cfg->allow_credentials)
        neverc_http_set_header(w, "Access-Control-Allow-Credentials", "true");

    if (cfg->exposed_headers)
        neverc_http_set_header(w, "Access-Control-Expose-Headers",
                                cfg->exposed_headers);
}

static void cors_preflight_handler(neverc_http_request_t *req,
                                     neverc_http_response_writer_t *w) {
    if (!req || !w) return;

    const char *origin = neverc_http_request_header(req, "Origin");
    neverc_http_cors_headers(w, &g_cors_config, origin);

    const char *methods = g_cors_config.allowed_methods
        ? g_cors_config.allowed_methods
        : "GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD";
    neverc_http_set_header(w, "Access-Control-Allow-Methods", methods);

    const char *headers = g_cors_config.allowed_headers
        ? g_cors_config.allowed_headers
        : "Content-Type, Authorization, Accept, X-Requested-With";
    neverc_http_set_header(w, "Access-Control-Allow-Headers", headers);

    char max_age_buf[16];
    int ma = g_cors_config.max_age > 0 ? g_cors_config.max_age : 86400;
    snprintf(max_age_buf, sizeof(max_age_buf), "%d", ma);
    neverc_http_set_header(w, "Access-Control-Max-Age", max_age_buf);

    neverc_http_set_status(w, 204);
}

void neverc_http_enable_cors(neverc_http_mux_t *mux,
                               const neverc_http_cors_config_t *config) {
    if (config) {
        g_cors_config = *config;
    } else {
        memset(&g_cors_config, 0, sizeof(g_cors_config));
        g_cors_config.allowed_origins = "*";
        g_cors_config.allowed_methods = "GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD";
        g_cors_config.allowed_headers = "Content-Type, Authorization, Accept, X-Requested-With";
        g_cors_config.max_age = 86400;
    }
    g_cors_enabled = 1;

    /* Register an OPTIONS handler for preflight requests */
    if (mux)
        neverc_http_mux_handle(mux, "OPTIONS /", cors_preflight_handler);
    else
        neverc_http_handle_func("OPTIONS /", cors_preflight_handler);
}

/* ======================================================================
 * JSON Request Helpers
 * ====================================================================== */

const char *neverc_http_json_get(const neverc_http_request_t *req,
                                   const char *key, char *buf, size_t buflen) {
    if (!req || !req->body || req->body_len == 0 || !key || !buf || buflen == 0)
        return NULL;

    size_t klen = strlen(key);
    const char *p = req->body;
    const char *end = req->body + req->body_len;

    /* Search for "key": or "key" : */
    while (p < end) {
        const char *quote = memchr(p, '"', (size_t)(end - p));
        if (!quote) break;

        const char *kstart = quote + 1;
        if (kstart + klen >= end) break;

        if (memcmp(kstart, key, klen) == 0 && kstart[klen] == '"') {
            /* Found key. Skip to colon and value. */
            const char *after_key = kstart + klen + 1;
            while (after_key < end && (*after_key == ' ' || *after_key == ':'))
                after_key++;

            if (after_key >= end) break;

            if (*after_key == '"') {
                /* String value */
                const char *vstart = after_key + 1;
                const char *vend = vstart;
                while (vend < end && *vend != '"') {
                    if (*vend == '\\' && vend + 1 < end) vend++; /* skip escape */
                    vend++;
                }
                size_t vlen = (size_t)(vend - vstart);
                if (vlen >= buflen) vlen = buflen - 1;
                memcpy(buf, vstart, vlen);
                buf[vlen] = '\0';
                return buf;
            }

            /* Number, boolean, null */
            const char *vstart = after_key;
            const char *vend = vstart;
            while (vend < end && *vend != ',' && *vend != '}' &&
                   *vend != ' ' && *vend != '\n' && *vend != '\r')
                vend++;
            size_t vlen = (size_t)(vend - vstart);
            if (vlen >= buflen) vlen = buflen - 1;
            memcpy(buf, vstart, vlen);
            buf[vlen] = '\0';
            return buf;
        }

        p = kstart + 1;
    }
    return NULL;
}

int neverc_http_json_error(neverc_http_response_writer_t *w,
                             int code, const char *message) {
    if (!w) return 0;
    neverc_http_set_status(w, code);
    neverc_http_set_header(w, "Content-Type", "application/json; charset=utf-8");
    return neverc_http_writef(w, "{\"error\":\"%s\",\"code\":%d}",
                              message ? message : "error", code);
}

/* ======================================================================
 * Server-Sent Events (SSE) Implementation
 *
 * SSE (RFC 8895 / W3C EventSource) enables server-to-client push over
 * a long-lived HTTP connection. Protocol:
 *   - Response: Content-Type: text/event-stream
 *   - Events: "event: <type>\ndata: <payload>\nid: <id>\n\n"
 *   - Keep-alive: ": comment\n\n"
 *   - Retry: "retry: <ms>\n\n"
 * ====================================================================== */

struct neverc_sse {
    nc_sock_t fd;
    int       closed;
};

neverc_sse_t *neverc_sse_start(neverc_http_response_writer_t *w) {
    if (!w) return NULL;

    neverc_http_set_status(w, 200);
    neverc_http_set_header(w, "Content-Type", "text/event-stream");
    neverc_http_set_header(w, "Cache-Control", "no-cache");
    neverc_http_set_header(w, "Connection", "keep-alive");
    neverc_http_set_header(w, "X-Accel-Buffering", "no");

    w->headers_sent = 0;
    w->chunked = 0;
    w->keep_alive = 1;

    nc_buf_t hdr;
    nc_buf_init(&hdr);
    nc_buf_append(&hdr, "HTTP/1.1 200 OK\r\n", 17);
    nc_buf_append(&hdr, "Content-Type: text/event-stream\r\n", 33);
    nc_buf_append(&hdr, "Cache-Control: no-cache\r\n", 25);
    nc_buf_append(&hdr, "Connection: keep-alive\r\n", 24);
    nc_buf_append(&hdr, "X-Accel-Buffering: no\r\n", 23);
    nc_buf_append(&hdr, "Transfer-Encoding: chunked\r\n", 28);
    nc_buf_append(&hdr, "\r\n", 2);

    if (w->fd != NC_INVALID_SOCK)
        sock_write_all(w->fd, hdr.data, hdr.len);
    nc_buf_free(&hdr);

    neverc_sse_t *sse = (neverc_sse_t *)malloc(sizeof(*sse));
    if (!sse) return NULL;
    sse->fd = w->fd;
    sse->closed = 0;

    w->hijacked = 1;
    return sse;
}

static int sse_write_chunk(neverc_sse_t *sse, const char *data, size_t len) {
    if (!sse || sse->closed || sse->fd == NC_INVALID_SOCK) return -1;

    char size_buf[32];
    int slen = snprintf(size_buf, sizeof(size_buf), "%zx\r\n", len);
    if (sock_write_all(sse->fd, size_buf, (size_t)slen) != 0) {
        sse->closed = 1;
        return -1;
    }
    if (sock_write_all(sse->fd, data, len) != 0) {
        sse->closed = 1;
        return -1;
    }
    if (sock_write_all(sse->fd, "\r\n", 2) != 0) {
        sse->closed = 1;
        return -1;
    }
    return 0;
}

int neverc_sse_send(neverc_sse_t *sse, const char *event_type,
                     const char *data, const char *id) {
    if (!sse || sse->closed) return -1;

    nc_buf_t ev;
    nc_buf_init(&ev);

    if (id && id[0]) {
        nc_buf_append(&ev, "id: ", 4);
        nc_buf_append(&ev, id, strlen(id));
        nc_buf_append(&ev, "\n", 1);
    }
    if (event_type && event_type[0]) {
        nc_buf_append(&ev, "event: ", 7);
        nc_buf_append(&ev, event_type, strlen(event_type));
        nc_buf_append(&ev, "\n", 1);
    }
    if (data) {
        const char *p = data;
        while (*p) {
            const char *nl = strchr(p, '\n');
            nc_buf_append(&ev, "data: ", 6);
            if (nl) {
                nc_buf_append(&ev, p, (size_t)(nl - p));
                nc_buf_append(&ev, "\n", 1);
                p = nl + 1;
            } else {
                nc_buf_append(&ev, p, strlen(p));
                nc_buf_append(&ev, "\n", 1);
                break;
            }
        }
    } else {
        nc_buf_append(&ev, "data: \n", 7);
    }
    nc_buf_append(&ev, "\n", 1);

    int rc = sse_write_chunk(sse, ev.data, ev.len);
    nc_buf_free(&ev);
    return rc;
}

int neverc_sse_send_id(neverc_sse_t *sse, const char *event_type,
                        const char *data, const char *id) {
    return neverc_sse_send(sse, event_type, data, id);
}

int neverc_sse_retry(neverc_sse_t *sse, int retry_ms) {
    if (!sse || sse->closed) return -1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "retry: %d\n\n", retry_ms);
    return sse_write_chunk(sse, buf, (size_t)n);
}

int neverc_sse_comment(neverc_sse_t *sse, const char *text) {
    if (!sse || sse->closed) return -1;

    nc_buf_t ev;
    nc_buf_init(&ev);
    nc_buf_append(&ev, ": ", 2);
    if (text) nc_buf_append(&ev, text, strlen(text));
    nc_buf_append(&ev, "\n\n", 2);

    int rc = sse_write_chunk(sse, ev.data, ev.len);
    nc_buf_free(&ev);
    return rc;
}

void neverc_sse_close(neverc_sse_t *sse) {
    if (!sse) return;
    if (!sse->closed && sse->fd != NC_INVALID_SOCK) {
        sock_write_all(sse->fd, "0\r\n\r\n", 5);
    }
    free(sse);
}

