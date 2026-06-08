#include "neverc/std/net/http/cookiejar.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION jar_mutex_t;
#define jar_mutex_init(m)    InitializeCriticalSection(m)
#define jar_mutex_destroy(m) DeleteCriticalSection(m)
#define jar_mutex_lock(m)    EnterCriticalSection(m)
#define jar_mutex_unlock(m)  LeaveCriticalSection(m)
static int strncasecmp(const char *a, const char *b, size_t n) {
    return _strnicmp(a, b, n);
}
#else
#include <pthread.h>
#include <strings.h>
typedef pthread_mutex_t jar_mutex_t;
#define jar_mutex_init(m)    pthread_mutex_init(m, NULL)
#define jar_mutex_destroy(m) pthread_mutex_destroy(m)
#define jar_mutex_lock(m)    pthread_mutex_lock(m)
#define jar_mutex_unlock(m)  pthread_mutex_unlock(m)
#endif

typedef struct jar_entry {
    char       *name;
    char       *value;
    char       *domain;
    char       *path;
    int64_t     expires;
    int         secure;
    int         http_only;
    struct jar_entry *next;
} jar_entry_t;

struct neverc_cookiejar {
    jar_entry_t *entries;
    jar_mutex_t  lock;
    int          count;
};

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (d) { memcpy(d, s, len); d[len] = '\0'; }
    return d;
}

static void entry_free(jar_entry_t *e) {
    if (!e) return;
    free(e->name);
    free(e->value);
    free(e->domain);
    free(e->path);
    free(e);
}

static void str_tolower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Parse scheme, host, and path from a URL. */
static void parse_url_parts(const char *url, char *scheme, size_t slen,
                             char *host, size_t hlen,
                             char *path, size_t plen) {
    scheme[0] = host[0] = path[0] = '\0';

    const char *p = url;
    const char *colon_slash = strstr(p, "://");
    if (colon_slash) {
        size_t sl = (size_t)(colon_slash - p);
        if (sl < slen) { memcpy(scheme, p, sl); scheme[sl] = '\0'; }
        p = colon_slash + 3;
    }

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hl = (size_t)(slash - p);
        if (hl < hlen) { memcpy(host, p, hl); host[hl] = '\0'; }
        size_t pl = strlen(slash);
        if (pl < plen) { memcpy(path, slash, pl); path[pl] = '\0'; }
    } else {
        size_t hl = strlen(p);
        if (hl < hlen) { memcpy(host, p, hl); host[hl] = '\0'; }
        path[0] = '/'; path[1] = '\0';
    }

    /* Strip port from host */
    char *port_sep = strrchr(host, ':');
    if (port_sep) *port_sep = '\0';

    str_tolower(scheme);
    str_tolower(host);
}

/* RFC 6265 §5.1.3: domain matching.
 * "example.com" matches ".example.com" and "example.com".
 * "foo.example.com" matches ".example.com". */
static int domain_match(const char *cookie_domain, const char *request_host) {
    if (!cookie_domain || !request_host) return 0;
    if (strcmp(cookie_domain, request_host) == 0) return 1;

    /* cookie_domain starts with '.' → match any subdomain */
    if (cookie_domain[0] == '.') {
        size_t dlen = strlen(cookie_domain + 1);
        size_t hlen = strlen(request_host);
        if (hlen >= dlen &&
            strcmp(request_host + hlen - dlen, cookie_domain + 1) == 0) {
            if (hlen == dlen) return 1;
            if (request_host[hlen - dlen - 1] == '.') return 1;
        }
    }
    return 0;
}

/* RFC 6265 §5.1.4: path matching. */
static int path_match(const char *cookie_path, const char *request_path) {
    if (!cookie_path || !request_path) return 1;
    size_t cplen = strlen(cookie_path);
    if (cplen == 0) return 1;
    if (strncmp(request_path, cookie_path, cplen) == 0) {
        if (request_path[cplen] == '\0' || request_path[cplen] == '/')
            return 1;
        if (cookie_path[cplen - 1] == '/')
            return 1;
    }
    return 0;
}

static int64_t now_unix(void) {
    return (int64_t)time(NULL);
}

neverc_cookiejar_t *neverc_cookiejar_new(void) {
    neverc_cookiejar_t *jar =
        (neverc_cookiejar_t *)calloc(1, sizeof(*jar));
    if (jar) jar_mutex_init(&jar->lock);
    return jar;
}

void neverc_cookiejar_free(neverc_cookiejar_t *jar) {
    if (!jar) return;
    jar_entry_t *e = jar->entries;
    while (e) {
        jar_entry_t *next = e->next;
        entry_free(e);
        e = next;
    }
    jar_mutex_destroy(&jar->lock);
    free(jar);
}

void neverc_cookiejar_set_cookies(neverc_cookiejar_t *jar,
                                   const char *url,
                                   const neverc_cookiejar_entry_t *cookies,
                                   int count) {
    if (!jar || !url || !cookies || count <= 0) return;

    char scheme[16], host[256], path[1024];
    parse_url_parts(url, scheme, sizeof(scheme),
                    host, sizeof(host), path, sizeof(path));

    jar_mutex_lock(&jar->lock);

    for (int i = 0; i < count; i++) {
        const neverc_cookiejar_entry_t *c = &cookies[i];
        if (!c->name || !c->name[0]) continue;

        char domain[256];
        if (c->domain && c->domain[0]) {
            snprintf(domain, sizeof(domain), "%s", c->domain);
            str_tolower(domain);
        } else {
            snprintf(domain, sizeof(domain), "%s", host);
        }

        const char *cpath = (c->path && c->path[0]) ? c->path : "/";

        /* Check for existing entry with same name/domain/path → update */
        jar_entry_t *e = jar->entries;
        jar_entry_t *found = NULL;
        while (e) {
            if (strcmp(e->name, c->name) == 0 &&
                strcmp(e->domain, domain) == 0 &&
                strcmp(e->path, cpath) == 0) {
                found = e;
                break;
            }
            e = e->next;
        }

        if (found) {
            free(found->value);
            found->value = strdup_safe(c->value);
            found->expires = c->expires;
            found->secure = c->secure;
            found->http_only = c->http_only;
        } else {
            jar_entry_t *ne = (jar_entry_t *)calloc(1, sizeof(*ne));
            if (!ne) continue;
            ne->name = strdup_safe(c->name);
            ne->value = strdup_safe(c->value);
            ne->domain = strdup_safe(domain);
            ne->path = strdup_safe(cpath);
            ne->expires = c->expires;
            ne->secure = c->secure;
            ne->http_only = c->http_only;
            ne->next = jar->entries;
            jar->entries = ne;
            jar->count++;
        }
    }

    jar_mutex_unlock(&jar->lock);
}

int neverc_cookiejar_cookies(neverc_cookiejar_t *jar,
                              const char *url,
                              neverc_cookiejar_entry_t *out,
                              int max_out) {
    if (!jar || !url) return 0;

    char scheme[16], host[256], path[1024];
    parse_url_parts(url, scheme, sizeof(scheme),
                    host, sizeof(host), path, sizeof(path));

    int is_secure = (strcmp(scheme, "https") == 0);
    int64_t now = now_unix();

    jar_mutex_lock(&jar->lock);

    /* Prune expired cookies first */
    jar_entry_t **pp = &jar->entries;
    while (*pp) {
        jar_entry_t *e = *pp;
        if (e->expires > 0 && e->expires <= now) {
            *pp = e->next;
            entry_free(e);
            jar->count--;
        } else {
            pp = &(*pp)->next;
        }
    }

    int n = 0;
    jar_entry_t *e = jar->entries;
    while (e && n < max_out) {
        if (domain_match(e->domain, host) &&
            path_match(e->path, path) &&
            (!e->secure || is_secure)) {
            if (out) {
                out[n].name = e->name;
                out[n].value = e->value;
                out[n].domain = e->domain;
                out[n].path = e->path;
                out[n].expires = e->expires;
                out[n].secure = e->secure;
                out[n].http_only = e->http_only;
            }
            n++;
        }
        e = e->next;
    }

    jar_mutex_unlock(&jar->lock);
    return n;
}

void neverc_cookiejar_set_cookie_header(neverc_cookiejar_t *jar,
                                         const char *url,
                                         const char *header) {
    if (!jar || !url || !header) return;

    neverc_cookiejar_entry_t cookie;
    memset(&cookie, 0, sizeof(cookie));

    char name[256] = {0}, value[4096] = {0};
    char domain[256] = {0}, cpath[1024] = {0};

    /* Parse "name=value" part */
    const char *eq = strchr(header, '=');
    if (!eq) return;

    size_t nlen = (size_t)(eq - header);
    while (nlen > 0 && header[nlen - 1] == ' ') nlen--;
    if (nlen == 0 || nlen >= sizeof(name)) return;
    memcpy(name, header, nlen);
    name[nlen] = '\0';

    const char *vstart = eq + 1;
    const char *semi = strchr(vstart, ';');
    size_t vlen = semi ? (size_t)(semi - vstart) : strlen(vstart);
    while (vlen > 0 && vstart[vlen - 1] == ' ') vlen--;
    if (vlen >= sizeof(value)) vlen = sizeof(value) - 1;
    memcpy(value, vstart, vlen);
    value[vlen] = '\0';

    cookie.name = name;
    cookie.value = value;

    /* Parse attributes */
    const char *p = semi ? semi + 1 : NULL;
    while (p && *p) {
        while (*p == ' ') p++;
        const char *attr_end = strchr(p, ';');
        size_t alen = attr_end ? (size_t)(attr_end - p) : strlen(p);

        char attr[512];
        if (alen >= sizeof(attr)) alen = sizeof(attr) - 1;
        memcpy(attr, p, alen);
        attr[alen] = '\0';

        char *aeq = strchr(attr, '=');
        char *aname = attr;
        char *aval = NULL;
        if (aeq) {
            *aeq = '\0';
            aval = aeq + 1;
            while (*aval == ' ') aval++;
        }
        while (*aname == ' ') aname++;
        size_t anlen = strlen(aname);
        while (anlen > 0 && aname[anlen - 1] == ' ') aname[--anlen] = '\0';

        if (anlen == 6 && strncasecmp(aname, "Domain", 6) == 0 && aval) {
            snprintf(domain, sizeof(domain), "%s", aval);
            str_tolower(domain);
            if (domain[0] != '.') {
                memmove(domain + 1, domain, strlen(domain) + 1);
                domain[0] = '.';
            }
        } else if (anlen == 4 && strncasecmp(aname, "Path", 4) == 0 && aval) {
            snprintf(cpath, sizeof(cpath), "%s", aval);
        } else if (anlen == 6 && strncasecmp(aname, "Secure", 6) == 0) {
            cookie.secure = 1;
        } else if (anlen == 8 && strncasecmp(aname, "HttpOnly", 8) == 0) {
            cookie.http_only = 1;
        } else if (anlen == 7 && strncasecmp(aname, "Max-Age", 7) == 0 && aval) {
            int ma = atoi(aval);
            if (ma > 0) cookie.expires = now_unix() + ma;
            else if (ma <= 0) cookie.expires = -1; /* delete */
        }

        p = attr_end ? attr_end + 1 : NULL;
    }

    if (cookie.expires == -1) {
        /* Max-Age=0 means delete the cookie */
        neverc_cookiejar_entry_t tmp;
        tmp.name = name;
        tmp.value = "";
        tmp.domain = domain[0] ? domain : NULL;
        tmp.path = cpath[0] ? cpath : "/";
        tmp.expires = 1; /* already expired */
        tmp.secure = 0;
        tmp.http_only = 0;
        neverc_cookiejar_set_cookies(jar, url, &tmp, 1);
        return;
    }

    cookie.domain = domain[0] ? domain : NULL;
    cookie.path = cpath[0] ? cpath : NULL;

    neverc_cookiejar_set_cookies(jar, url, &cookie, 1);
}

char *neverc_cookiejar_cookie_header(neverc_cookiejar_t *jar,
                                      const char *url) {
    if (!jar || !url) return NULL;

    neverc_cookiejar_entry_t matches[64];
    int n = neverc_cookiejar_cookies(jar, url, matches, 64);
    if (n <= 0) return NULL;

    size_t total = 0;
    for (int i = 0; i < n; i++)
        total += strlen(matches[i].name) + 1 + strlen(matches[i].value) + 2;

    char *buf = (char *)malloc(total + 1);
    if (!buf) return NULL;

    size_t off = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0) { buf[off++] = ';'; buf[off++] = ' '; }
        size_t nlen = strlen(matches[i].name);
        size_t vlen = strlen(matches[i].value);
        memcpy(buf + off, matches[i].name, nlen); off += nlen;
        buf[off++] = '=';
        memcpy(buf + off, matches[i].value, vlen); off += vlen;
    }
    buf[off] = '\0';
    return buf;
}

void neverc_cookiejar_clear_domain(neverc_cookiejar_t *jar,
                                     const char *domain) {
    if (!jar || !domain) return;

    char lower_domain[256];
    snprintf(lower_domain, sizeof(lower_domain), "%s", domain);
    str_tolower(lower_domain);

    jar_mutex_lock(&jar->lock);
    jar_entry_t **pp = &jar->entries;
    while (*pp) {
        jar_entry_t *e = *pp;
        if (domain_match(e->domain, lower_domain) ||
            strcmp(e->domain, lower_domain) == 0) {
            *pp = e->next;
            entry_free(e);
            jar->count--;
        } else {
            pp = &(*pp)->next;
        }
    }
    jar_mutex_unlock(&jar->lock);
}

void neverc_cookiejar_clear_all(neverc_cookiejar_t *jar) {
    if (!jar) return;
    jar_mutex_lock(&jar->lock);
    jar_entry_t *e = jar->entries;
    while (e) {
        jar_entry_t *next = e->next;
        entry_free(e);
        e = next;
    }
    jar->entries = NULL;
    jar->count = 0;
    jar_mutex_unlock(&jar->lock);
}

int neverc_cookiejar_count(neverc_cookiejar_t *jar) {
    if (!jar) return 0;
    jar_mutex_lock(&jar->lock);
    int n = jar->count;
    jar_mutex_unlock(&jar->lock);
    return n;
}
