#include "neverc/std/net/url.h"
#include <string.h>
#include <stdio.h>

static int nc_isalnum(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static int nc_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int should_escape_path(unsigned char c) {
    if (nc_isalnum(c)) return 0;
    if (c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':' || c == '@') return 0;
    return 1;
}

static int should_escape_query(unsigned char c) {
    if (nc_isalnum(c)) return 0;
    if (c == '-' || c == '_' || c == '.' || c == '~') return 0;
    return 1;
}

static size_t safe_copy(char *dst, size_t cap, const char *src, size_t len) {
    size_t n = len < cap - 1 ? len : cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

int neverc_url_parse(neverc_url_t *u, const char *raw_url) {
    memset(u, 0, sizeof(*u));
    if (!raw_url || !*raw_url) return -1;

    const char *p = raw_url;

    const char *scheme_end = strstr(p, "://");
    if (scheme_end) {
        safe_copy(u->scheme, sizeof(u->scheme), p, (size_t)(scheme_end - p));
        for (size_t i = 0; u->scheme[i]; i++)
            u->scheme[i] = (char)nc_tolower((unsigned char)u->scheme[i]);
        p = scheme_end + 3;

        const char *at = NULL;
        const char *path_start = strchr(p, '/');
        const char *query_start = strchr(p, '?');
        const char *host_end = path_start ? path_start : (query_start ? query_start : p + strlen(p));

        for (const char *c = p; c < host_end; c++) {
            if (*c == '@') { at = c; break; }
        }

        if (at) {
            const char *colon = NULL;
            for (const char *c = p; c < at; c++)
                if (*c == ':') { colon = c; break; }

            if (colon) {
                safe_copy(u->user, sizeof(u->user), p, (size_t)(colon - p));
                safe_copy(u->password, sizeof(u->password), colon + 1, (size_t)(at - colon - 1));
            } else {
                safe_copy(u->user, sizeof(u->user), p, (size_t)(at - p));
            }
            p = at + 1;
        }

        host_end = strchr(p, '/');
        if (!host_end) host_end = strchr(p, '?');
        if (!host_end) host_end = strchr(p, '#');
        if (!host_end) host_end = p + strlen(p);

        if (*p == '[') {
            const char *bracket = strchr(p, ']');
            if (bracket && bracket < host_end) {
                safe_copy(u->host, sizeof(u->host), p + 1, (size_t)(bracket - p - 1));
                if (bracket + 1 < host_end && *(bracket + 1) == ':')
                    safe_copy(u->port, sizeof(u->port), bracket + 2, (size_t)(host_end - bracket - 2));
                p = host_end;
            }
        } else {
            const char *colon = NULL;
            for (const char *c = p; c < host_end; c++)
                if (*c == ':') colon = c;
            if (colon) {
                safe_copy(u->host, sizeof(u->host), p, (size_t)(colon - p));
                safe_copy(u->port, sizeof(u->port), colon + 1, (size_t)(host_end - colon - 1));
            } else {
                safe_copy(u->host, sizeof(u->host), p, (size_t)(host_end - p));
            }
            p = host_end;
        }
    }

    const char *query = strchr(p, '?');
    const char *frag = strchr(p, '#');

    if (frag) {
        safe_copy(u->fragment, sizeof(u->fragment), frag + 1, strlen(frag + 1));
    }

    const char *path_end = query ? query : (frag ? frag : p + strlen(p));
    safe_copy(u->path, sizeof(u->path), p, (size_t)(path_end - p));

    if (query) {
        const char *q_end = frag ? frag : query + strlen(query);
        safe_copy(u->raw_query, sizeof(u->raw_query), query + 1, (size_t)(q_end - query - 1));
    }

    return 0;
}

int neverc_url_string(const neverc_url_t *u, char *buf, size_t cap) {
    int n = 0;
    if (u->scheme[0])
        n = snprintf(buf, cap, "%s://", u->scheme);
    if (u->user[0]) {
        n += snprintf(buf + n, cap - (size_t)n, "%s", u->user);
        if (u->password[0])
            n += snprintf(buf + n, cap - (size_t)n, ":%s", u->password);
        n += snprintf(buf + n, cap - (size_t)n, "@");
    }
    if (u->host[0]) {
        n += snprintf(buf + n, cap - (size_t)n, "%s", u->host);
        if (u->port[0])
            n += snprintf(buf + n, cap - (size_t)n, ":%s", u->port);
    }
    if (u->path[0])
        n += snprintf(buf + n, cap - (size_t)n, "%s", u->path);
    if (u->raw_query[0])
        n += snprintf(buf + n, cap - (size_t)n, "?%s", u->raw_query);
    if (u->fragment[0])
        n += snprintf(buf + n, cap - (size_t)n, "#%s", u->fragment);
    return n;
}

int neverc_url_hostname(const neverc_url_t *u, char *buf, size_t cap) {
    return (int)safe_copy(buf, cap, u->host, strlen(u->host));
}

int neverc_url_request_uri(const neverc_url_t *u, char *buf, size_t cap) {
    int n = snprintf(buf, cap, "%s", u->path[0] ? u->path : "/");
    if (u->raw_query[0])
        n += snprintf(buf + n, cap - (size_t)n, "?%s", u->raw_query);
    return n;
}

int neverc_url_is_abs(const neverc_url_t *u) {
    return u->scheme[0] != '\0';
}

int neverc_url_values_parse(neverc_url_values_t *v, const char *query) {
    v->count = 0;
    if (!query || !*query) return 0;

    const char *p = query;
    while (*p && v->count < 64) {
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        const char *eq = NULL;
        for (const char *c = p; c < end; c++)
            if (*c == '=') { eq = c; break; }

        if (eq) {
            char key_buf[256], val_buf[1024];
            safe_copy(key_buf, sizeof(key_buf), p, (size_t)(eq - p));
            safe_copy(val_buf, sizeof(val_buf), eq + 1, (size_t)(end - eq - 1));

            neverc_url_query_unescape(key_buf, v->keys[v->count], sizeof(v->keys[0]));
            neverc_url_query_unescape(val_buf, v->vals[v->count], sizeof(v->vals[0]));
            v->count++;
        } else {
            char key_buf[256];
            safe_copy(key_buf, sizeof(key_buf), p, (size_t)(end - p));
            neverc_url_query_unescape(key_buf, v->keys[v->count], sizeof(v->keys[0]));
            v->vals[v->count][0] = '\0';
            v->count++;
        }

        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

const char *neverc_url_values_get(const neverc_url_values_t *v, const char *key) {
    for (int i = 0; i < v->count; i++)
        if (strcmp(v->keys[i], key) == 0)
            return v->vals[i];
    return NULL;
}

void neverc_url_values_set(neverc_url_values_t *v, const char *key, const char *val) {
    for (int i = 0; i < v->count; i++) {
        if (strcmp(v->keys[i], key) == 0) {
            safe_copy(v->vals[i], sizeof(v->vals[0]), val, strlen(val));
            return;
        }
    }
    if (v->count < 64) {
        safe_copy(v->keys[v->count], sizeof(v->keys[0]), key, strlen(key));
        safe_copy(v->vals[v->count], sizeof(v->vals[0]), val, strlen(val));
        v->count++;
    }
}

int neverc_url_values_encode(const neverc_url_values_t *v, char *buf, size_t cap) {
    size_t pos = 0;
    for (int i = 0; i < v->count; i++) {
        if (i > 0 && pos < cap) buf[pos++] = '&';
        char ek[512], ev[2048];
        neverc_url_query_escape(v->keys[i], ek, sizeof(ek));
        neverc_url_query_escape(v->vals[i], ev, sizeof(ev));
        int n = snprintf(buf + pos, cap - pos, "%s=%s", ek, ev);
        if (n > 0) pos += (size_t)n;
    }
    return (int)pos;
}

static int percent_encode(const char *s, char *buf, size_t cap,
                           int (*should_escape)(unsigned char)) {
    size_t si = 0, di = 0;
    while (s[si] && di < cap - 1) {
        unsigned char c = (unsigned char)s[si];
        if (should_escape(c)) {
            if (di + 3 > cap - 1) break;
            buf[di++] = '%';
            buf[di++] = "0123456789ABCDEF"[c >> 4];
            buf[di++] = "0123456789ABCDEF"[c & 0x0F];
        } else {
            buf[di++] = (char)c;
        }
        si++;
    }
    buf[di] = '\0';
    return (int)di;
}

static int percent_decode(const char *s, char *buf, size_t cap) {
    size_t si = 0, di = 0;
    while (s[si] && di < cap - 1) {
        if (s[si] == '%' && s[si+1] && s[si+2]) {
            int h = hex_digit(s[si+1]);
            int l = hex_digit(s[si+2]);
            if (h >= 0 && l >= 0) {
                buf[di++] = (char)((h << 4) | l);
                si += 3;
                continue;
            }
        }
        if (s[si] == '+') {
            buf[di++] = ' ';
            si++;
        } else {
            buf[di++] = s[si++];
        }
    }
    buf[di] = '\0';
    return (int)di;
}

int neverc_url_path_escape(const char *s, char *buf, size_t cap) {
    return percent_encode(s, buf, cap, should_escape_path);
}

int neverc_url_path_unescape(const char *s, char *buf, size_t cap) {
    return percent_decode(s, buf, cap);
}

int neverc_url_query_escape(const char *s, char *buf, size_t cap) {
    return percent_encode(s, buf, cap, should_escape_query);
}

int neverc_url_query_unescape(const char *s, char *buf, size_t cap) {
    return percent_decode(s, buf, cap);
}
