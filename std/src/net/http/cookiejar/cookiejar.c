#include "neverc/std/net/http/cookiejar.h"
#include "neverc/std/net/netip.h"
#include "neverc/std/net/url.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION jar_mutex_t;
#define jar_mutex_init(m)    InitializeCriticalSection(m)
#define jar_mutex_destroy(m) DeleteCriticalSection(m)
#define jar_mutex_lock(m)    EnterCriticalSection(m)
#define jar_mutex_unlock(m)  LeaveCriticalSection(m)
#else
#include <pthread.h>
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
    int         host_only;
    uint64_t    creation;
    struct jar_entry *next;
} jar_entry_t;

struct neverc_cookiejar {
    jar_entry_t *entries;
    jar_mutex_t  lock;
    int          count;
    uint64_t     next_creation;
};

typedef struct cookie_span {
    const char *data;
    size_t      length;
} cookie_span_t;

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

static int valid_cookie_name(const char *name) {
    if (!name || !name[0]) return 0;
    static const char separators[] = "()<>@,;:\\\"/[]?={} \t";
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7f || strchr(separators, *p)) return 0;
    }
    return 1;
}

static int valid_cookie_value(const char *value) {
    if (!value) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p != 0x21 && !(*p >= 0x23 && *p <= 0x2b) &&
            !(*p >= 0x2d && *p <= 0x3a) &&
            !(*p >= 0x3c && *p <= 0x5b) &&
            !(*p >= 0x5d && *p <= 0x7e))
            return 0;
    }
    return 1;
}

static cookie_span_t trim_cookie_ows(cookie_span_t span) {
    while (span.length > 0 &&
           (span.data[0] == ' ' || span.data[0] == '\t')) {
        span.data++;
        span.length--;
    }
    while (span.length > 0 &&
           (span.data[span.length - 1] == ' ' ||
            span.data[span.length - 1] == '\t'))
        span.length--;
    return span;
}

static int span_case_equal(cookie_span_t span, const char *literal) {
    size_t length = strlen(literal);
    if (span.length != length) return 0;
    for (size_t i = 0; i < length; i++) {
        if (tolower((unsigned char)span.data[i]) !=
            tolower((unsigned char)literal[i]))
            return 0;
    }
    return 1;
}

static int copy_cookie_span(cookie_span_t span, char *output,
                            size_t capacity) {
    if (!output || capacity == 0 || span.length >= capacity) return -1;
    memcpy(output, span.data, span.length);
    output[span.length] = '\0';
    return 0;
}

/* Parse Max-Age without signed overflow. Positive overflow saturates. */
static int parse_max_age(cookie_span_t span, int64_t *result) {
    if (!result || span.length == 0) return -1;

    size_t offset = 0;
    int negative = 0;
    if (span.data[0] == '-') {
        negative = 1;
        offset = 1;
    } else if (span.data[0] == '+') {
        return -1;
    }
    if (offset == span.length) return -1;

    uint64_t value = 0;
    for (; offset < span.length; offset++) {
        unsigned char c = (unsigned char)span.data[offset];
        if (!isdigit(c)) return -1;
        unsigned digit = (unsigned)(c - '0');
        if (value > ((uint64_t)INT64_MAX - digit) / 10U)
            value = (uint64_t)INT64_MAX;
        else
            value = value * 10U + digit;
    }

    if (negative || value == 0)
        *result = -1;
    else
        *result = (int64_t)value;
    return 0;
}

static int parse_decimal_token(const char *token, size_t length,
                               size_t min_length, size_t max_length,
                               int *result) {
    if (!result || length < min_length || length > max_length) return -1;
    int value = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)token[i];
        if (!isdigit(c)) return -1;
        value = value * 10 + (int)(c - '0');
    }
    *result = value;
    return 0;
}

static int parse_cookie_time(const char *token, size_t length,
                             int *hour, int *minute, int *second) {
    const char *first_colon = memchr(token, ':', length);
    if (!first_colon) return -1;
    size_t remaining = length - (size_t)(first_colon - token) - 1;
    const char *second_colon = memchr(first_colon + 1, ':', remaining);
    if (!second_colon ||
        memchr(second_colon + 1, ':',
               length - (size_t)(second_colon - token) - 1))
        return -1;

    size_t hour_length = (size_t)(first_colon - token);
    size_t minute_length = (size_t)(second_colon - first_colon - 1);
    size_t second_length = length - (size_t)(second_colon - token) - 1;
    if (parse_decimal_token(token, hour_length, 1, 2, hour) != 0 ||
        parse_decimal_token(first_colon + 1, minute_length, 1, 2,
                            minute) != 0 ||
        parse_decimal_token(second_colon + 1, second_length, 1, 2,
                            second) != 0)
        return -1;
    return 0;
}

static int cookie_month(const char *token, size_t length) {
    static const char *const months[] = {
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec",
    };
    if (length < 3) return 0;
    for (int month = 0; month < 12; month++) {
        int matches = 1;
        for (size_t i = 0; i < 3; i++) {
            if (tolower((unsigned char)token[i]) != months[month][i]) {
                matches = 0;
                break;
            }
        }
        if (matches) return month + 1;
    }
    return 0;
}

static int days_in_cookie_month(int year, int month) {
    static const int days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    int result = days[month - 1];
    if (month == 2 &&
        ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        result++;
    return result;
}

static int64_t cookie_days_from_civil(int year, unsigned month,
                                      unsigned day) {
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    unsigned adjusted_month = month > 2 ? month - 3U : month + 9U;
    unsigned day_of_year =
        (153U * adjusted_month + 2U) / 5U + day - 1U;
    unsigned day_of_era = year_of_era * 365U + year_of_era / 4U -
        year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

/* RFC 6265 section 5.1.1 cookie-date parser, interpreted as UTC. */
static int parse_cookie_date(cookie_span_t span, int64_t *result) {
    int hour = -1, minute = -1, second = -1;
    int day = -1, month = 0, year = -1;
    size_t offset = 0;

    while (offset < span.length) {
        while (offset < span.length) {
            unsigned char c = (unsigned char)span.data[offset];
            if (isalnum(c) || c == ':') break;
            offset++;
        }
        size_t start = offset;
        while (offset < span.length) {
            unsigned char c = (unsigned char)span.data[offset];
            if (!isalnum(c) && c != ':') break;
            offset++;
        }
        size_t length = offset - start;
        if (length == 0) continue;

        if (hour < 0 &&
            parse_cookie_time(span.data + start, length,
                              &hour, &minute, &second) == 0)
            continue;
        int number = 0;
        if (day < 0 &&
            parse_decimal_token(span.data + start, length, 1, 2,
                                &number) == 0) {
            day = number;
            continue;
        }
        if (month == 0) {
            int parsed_month = cookie_month(span.data + start, length);
            if (parsed_month != 0) {
                month = parsed_month;
                continue;
            }
        }
        if (year < 0 &&
            parse_decimal_token(span.data + start, length, 2, 4,
                                &number) == 0)
            year = number;
    }

    if (year >= 70 && year <= 99) year += 1900;
    else if (year >= 0 && year <= 69) year += 2000;
    if (!result || year < 1601 || month == 0 || day < 1 ||
        day > days_in_cookie_month(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
        return -1;

    int64_t days = cookie_days_from_civil(
        year, (unsigned)month, (unsigned)day);
    *result = days * 86400 + hour * 3600 + minute * 60 + second;
    return 0;
}

/* Parse scheme, host, and path from a URL. Host is unescaped so
 * percent-encoded names match the decoded request-host (RFC 6265 §5.1.2). */
static int parse_url_parts(const char *url, char *scheme, size_t slen,
                           char *host, size_t hlen,
                           char *path, size_t plen) {
    if (!url || !scheme || slen == 0 || !host || hlen == 0 ||
        !path || plen < 2)
        return -1;
    scheme[0] = host[0] = path[0] = '\0';

    neverc_url_t parsed;
    if (neverc_url_parse(&parsed, url) != 0 || !parsed.scheme[0] ||
        !parsed.host[0])
        return -1;

    size_t scheme_length = strlen(parsed.scheme);
    if (scheme_length >= slen) return -1;
    memcpy(scheme, parsed.scheme, scheme_length + 1);

    size_t host_length = strlen(parsed.host);
    if (host_length == 0 || host_length >= hlen) return -1;
    memcpy(host, parsed.host, host_length + 1);

    if (parsed.path[0]) {
        char decoded[sizeof(parsed.path)];
        int decoded_length = neverc_url_path_unescape(
            parsed.path, decoded, sizeof(decoded));
        if (decoded_length < 0 || (size_t)decoded_length >= plen ||
            (size_t)decoded_length >= sizeof(decoded))
            return -1;
        memcpy(path, decoded, (size_t)decoded_length + 1);
    } else {
        path[0] = '/';
        path[1] = '\0';
    }

    str_tolower(scheme);
    str_tolower(host);
    if (!strchr(host, ':') && host_length > 1 && host[host_length - 1] == '.')
        host[host_length - 1] = '\0';
    return host[0] ? 0 : -1;
}

static int host_is_ip_literal(const char *host) {
    neverc_netip_addr_t addr;
    return host && neverc_netip_parse_addr(host, &addr) == 0;
}

/* RFC 6265 section 5.1.3 domain matching for normalized domains. */
static int domain_match(const char *cookie_domain, const char *request_host) {
    if (!cookie_domain || !request_host) return 0;
    if (strcmp(cookie_domain, request_host) == 0) return 1;
    /* RFC 6265 §5.1.3 suffix matching applies only to host names, not IPs. */
    if (host_is_ip_literal(request_host) || host_is_ip_literal(cookie_domain))
        return 0;
    size_t domain_length = strlen(cookie_domain);
    size_t host_length = strlen(request_host);
    return host_length > domain_length &&
        request_host[host_length - domain_length - 1] == '.' &&
        strcmp(request_host + host_length - domain_length, cookie_domain) == 0;
}

static int normalize_cookie_domain(const char *input, char *domain,
                                   size_t capacity) {
    while (*input == '.') input++;
    size_t length = strlen(input);
    if (length >= 2 && input[0] == '[' && input[length - 1] == ']') {
        input++;
        length -= 2;
    }
    if (length == 0 || length >= capacity || input[length - 1] == '.')
        return -1;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c <= 0x20 || c >= 0x7f || c == '/' || c == '\\')
            return -1;
        domain[i] = (char)tolower(c);
    }
    domain[length] = '\0';
    return 0;
}

static int normalize_clear_domain(const char *input, char *domain,
                                  size_t capacity) {
    if (!input || !domain || capacity == 0) return -1;
    while (*input == '.') input++;
    size_t length = strlen(input);
    if (length > 1 && input[length - 1] == '.') length--;
    if (length >= 2 && input[0] == '[' && input[length - 1] == ']') {
        input++;
        length -= 2;
    }
    if (length == 0 || length >= capacity) return -1;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c <= 0x20 || c >= 0x7f || c == '/' || c == '\\') return -1;
        domain[i] = (char)tolower(c);
    }
    domain[length] = '\0';
    return 0;
}

static void default_cookie_path(const char *request_path, char *path,
                                size_t capacity) {
    const char *last_slash = strrchr(request_path, '/');
    if (!last_slash || last_slash == request_path) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    size_t length = (size_t)(last_slash - request_path);
    if (length >= capacity) length = capacity - 1;
    memcpy(path, request_path, length);
    path[length] = '\0';
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

static int domains_overlap(const char *left, const char *right) {
    return domain_match(left, right) || domain_match(right, left);
}

static int paths_overlap(const char *left, const char *right) {
    return path_match(left, right) || path_match(right, left);
}

static int64_t now_unix(void) {
    return (int64_t)time(NULL);
}

static void prune_expired_locked(neverc_cookiejar_t *jar, int64_t now) {
    jar_entry_t **link = &jar->entries;
    while (*link) {
        jar_entry_t *entry = *link;
        if (entry->expires != 0 && entry->expires <= now) {
            *link = entry->next;
            entry_free(entry);
            jar->count--;
        } else {
            link = &entry->next;
        }
    }
}

static int entry_matches_request(const jar_entry_t *entry,
                                 const char *host, const char *path,
                                 int is_secure) {
    return (entry->host_only ? strcmp(entry->domain, host) == 0
                             : domain_match(entry->domain, host)) &&
        path_match(entry->path, path) &&
        (!entry->secure || is_secure);
}

static int entry_precedes(const jar_entry_t *left,
                          const jar_entry_t *right) {
    size_t left_path_length = strlen(left->path);
    size_t right_path_length = strlen(right->path);
    if (left_path_length != right_path_length)
        return left_path_length > right_path_length;
    return left->creation < right->creation;
}

static jar_entry_t *next_matching_entry_locked(
    neverc_cookiejar_t *jar, const char *host, const char *path,
    int is_secure, const jar_entry_t *previous) {
    jar_entry_t *best = NULL;
    for (jar_entry_t *entry = jar->entries; entry; entry = entry->next) {
        if (!entry_matches_request(entry, host, path, is_secure)) continue;
        if (previous && !entry_precedes(previous, entry)) continue;
        if (!best || entry_precedes(entry, best)) best = entry;
    }
    return best;
}

static int checked_size_add(size_t *value, size_t increment) {
    if (increment > SIZE_MAX - *value) return -1;
    *value += increment;
    return 0;
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
    if (parse_url_parts(url, scheme, sizeof(scheme), host, sizeof(host), path,
                        sizeof(path)) != 0)
        return;

    int source_is_secure = strcmp(scheme, "https") == 0;
    int64_t now = now_unix();
    jar_mutex_lock(&jar->lock);
    prune_expired_locked(jar, now);

    for (int i = 0; i < count; i++) {
        const neverc_cookiejar_entry_t *c = &cookies[i];
        if (!valid_cookie_name(c->name) || !valid_cookie_value(c->value))
            continue;
        if (c->secure && !source_is_secure) continue;

        char domain[256];
        int host_only = 1;
        if (c->domain && c->domain[0]) {
            if (normalize_cookie_domain(c->domain, domain, sizeof(domain)) != 0 ||
                !domain_match(domain, host))
                continue;
            host_only = 0;
            /* Reject obvious public-suffix attributes such as Domain=com. */
            if (!host_is_ip_literal(host) && !strchr(domain, '.')) {
                if (strcmp(domain, host) != 0) continue;
                host_only = 1;
            }
        } else {
            memcpy(domain, host, strlen(host) + 1);
        }

        char cookie_path[1024];
        if (c->path && c->path[0] == '/') {
            size_t path_length = strlen(c->path);
            if (path_length >= sizeof(cookie_path)) continue;
            memcpy(cookie_path, c->path, path_length + 1);
        } else {
            default_cookie_path(path, cookie_path, sizeof(cookie_path));
        }
        const char *cpath = cookie_path;

        /* An insecure origin cannot overlay a matching Secure cookie. */
        int overlays_secure = 0;
        if (!source_is_secure && !c->secure) {
            for (jar_entry_t *entry = jar->entries; entry;
                 entry = entry->next) {
                if (entry->secure && strcmp(entry->name, c->name) == 0 &&
                    domains_overlap(entry->domain, domain) &&
                    paths_overlap(entry->path, cpath)) {
                    overlays_secure = 1;
                    break;
                }
            }
        }
        if (overlays_secure) continue;

        jar_entry_t **found_link = &jar->entries;
        while (*found_link) {
            jar_entry_t *entry = *found_link;
            if (strcmp(entry->name, c->name) == 0 &&
                strcmp(entry->domain, domain) == 0 &&
                strcmp(entry->path, cpath) == 0)
                break;
            found_link = &entry->next;
        }
        jar_entry_t *found = *found_link;

        if (c->expires != 0 && c->expires <= now) {
            if (found) {
                *found_link = found->next;
                entry_free(found);
                jar->count--;
            }
            continue;
        }

        if (found) {
            char *value_copy = strdup_safe(c->value);
            if (!value_copy) continue;
            free(found->value);
            found->value = value_copy;
            found->expires = c->expires;
            found->secure = c->secure;
            found->http_only = c->http_only;
            found->host_only = host_only;
        } else {
            jar_entry_t *ne = (jar_entry_t *)calloc(1, sizeof(*ne));
            if (!ne) continue;
            ne->name = strdup_safe(c->name);
            ne->value = strdup_safe(c->value);
            ne->domain = strdup_safe(domain);
            ne->path = strdup_safe(cpath);
            if (!ne->name || !ne->value || !ne->domain || !ne->path) {
                entry_free(ne);
                continue;
            }
            ne->expires = c->expires;
            ne->secure = c->secure;
            ne->http_only = c->http_only;
            ne->host_only = host_only;
            ne->creation = jar->next_creation++;
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
    if (parse_url_parts(url, scheme, sizeof(scheme), host, sizeof(host), path,
                        sizeof(path)) != 0)
        return 0;

    int is_secure = (strcmp(scheme, "https") == 0);
    int64_t now = now_unix();

    jar_mutex_lock(&jar->lock);
    prune_expired_locked(jar, now);

    int n = 0;
    jar_entry_t *previous = NULL;
    while (n < max_out) {
        jar_entry_t *entry = next_matching_entry_locked(
            jar, host, path, is_secure, previous);
        if (!entry) break;
        if (out) {
            out[n].name = entry->name;
            out[n].value = entry->value;
            out[n].domain = entry->domain;
            out[n].path = entry->path;
            out[n].expires = entry->expires;
            out[n].secure = entry->secure;
            out[n].http_only = entry->http_only;
        }
        previous = entry;
        n++;
    }

    jar_mutex_unlock(&jar->lock);
    return n;
}

void neverc_cookiejar_set_cookie_header(neverc_cookiejar_t *jar,
                                         const char *url,
                                         const char *header) {
    if (!jar || !url || !header) return;

    neverc_cookiejar_entry_t cookie = {0};
    char name[256] = {0}, value[4096] = {0};
    char domain[256] = {0}, cpath[1024] = {0};
    int have_max_age = 0;
    int64_t max_age = 0;
    int have_expires = 0;
    int64_t expires = 0;

    const char *first_semi = strchr(header, ';');
    const char *eq = strchr(header, '=');
    if (!eq || (first_semi && eq > first_semi)) return;

    cookie_span_t name_span = {
        header, (size_t)(eq - header),
    };
    name_span = trim_cookie_ows(name_span);
    const char *value_start = eq + 1;
    const char *semi = strchr(value_start, ';');
    cookie_span_t value_span = {
        value_start,
        semi ? (size_t)(semi - value_start) : strlen(value_start),
    };
    value_span = trim_cookie_ows(value_span);
    if (value_span.length > 0 &&
        (value_span.data[0] == '"' ||
         value_span.data[value_span.length - 1] == '"')) {
        if (value_span.length < 2 || value_span.data[0] != '"' ||
            value_span.data[value_span.length - 1] != '"')
            return;
        value_span.data++;
        value_span.length -= 2;
    }
    if (copy_cookie_span(name_span, name, sizeof(name)) != 0 ||
        copy_cookie_span(value_span, value, sizeof(value)) != 0 ||
        !valid_cookie_name(name) || !valid_cookie_value(value))
        return;

    cookie.name = name;
    cookie.value = value;

    const char *p = semi ? semi + 1 : NULL;
    while (p && *p) {
        const char *attr_end = strchr(p, ';');
        cookie_span_t attribute = {
            p, attr_end ? (size_t)(attr_end - p) : strlen(p),
        };
        const char *attribute_eq = memchr(
            attribute.data, '=', attribute.length);
        cookie_span_t attribute_name = attribute;
        cookie_span_t attribute_value = {NULL, 0};
        int have_value = attribute_eq != NULL;
        if (have_value) {
            attribute_name.length =
                (size_t)(attribute_eq - attribute.data);
            attribute_value.data = attribute_eq + 1;
            attribute_value.length = attribute.length -
                (size_t)(attribute_eq - attribute.data) - 1;
            attribute_value = trim_cookie_ows(attribute_value);
        }
        attribute_name = trim_cookie_ows(attribute_name);

        if (span_case_equal(attribute_name, "Domain") && have_value) {
            if (copy_cookie_span(attribute_value, domain,
                                 sizeof(domain)) != 0)
                return;
        } else if (span_case_equal(attribute_name, "Path") && have_value) {
            if (copy_cookie_span(attribute_value, cpath,
                                 sizeof(cpath)) != 0)
                return;
        } else if (span_case_equal(attribute_name, "Secure")) {
            cookie.secure = 1;
        } else if (span_case_equal(attribute_name, "HttpOnly")) {
            cookie.http_only = 1;
        } else if (span_case_equal(attribute_name, "Max-Age") &&
                   have_value) {
            int64_t parsed = 0;
            if (parse_max_age(attribute_value, &parsed) == 0) {
                have_max_age = 1;
                max_age = parsed;
            }
        } else if (span_case_equal(attribute_name, "Expires") &&
                   have_value) {
            int64_t parsed = 0;
            if (parse_cookie_date(attribute_value, &parsed) == 0) {
                have_expires = 1;
                expires = parsed;
            }
        }

        p = attr_end ? attr_end + 1 : NULL;
    }

    if (have_max_age) {
        if (max_age <= 0) {
            cookie.expires = -1;
        } else {
            int64_t now = now_unix();
            cookie.expires = max_age > INT64_MAX - now
                ? INT64_MAX : now + max_age;
        }
    } else if (have_expires) {
        cookie.expires = expires <= 0 ? -1 : expires;
    }

    cookie.domain = domain[0] ? domain : NULL;
    cookie.path = cpath[0] ? cpath : NULL;

    neverc_cookiejar_set_cookies(jar, url, &cookie, 1);
}

char *neverc_cookiejar_cookie_header(neverc_cookiejar_t *jar,
                                      const char *url) {
    if (!jar || !url) return NULL;

    char scheme[16], host[256], path[1024];
    if (parse_url_parts(url, scheme, sizeof(scheme), host, sizeof(host), path,
                        sizeof(path)) != 0)
        return NULL;
    int is_secure = strcmp(scheme, "https") == 0;

    size_t total = 0;
    int match_count = 0;
    jar_mutex_lock(&jar->lock);
    prune_expired_locked(jar, now_unix());
    for (jar_entry_t *entry = jar->entries; entry; entry = entry->next) {
        if (!entry_matches_request(entry, host, path, is_secure)) continue;
        if ((match_count > 0 && checked_size_add(&total, 2) != 0) ||
            checked_size_add(&total, strlen(entry->name)) != 0 ||
            checked_size_add(&total, 1) != 0 ||
            checked_size_add(&total, strlen(entry->value)) != 0) {
            jar_mutex_unlock(&jar->lock);
            return NULL;
        }
        match_count++;
    }
    if (match_count == 0 || total == SIZE_MAX) {
        jar_mutex_unlock(&jar->lock);
        return NULL;
    }

    char *buf = (char *)malloc(total + 1);
    if (!buf) {
        jar_mutex_unlock(&jar->lock);
        return NULL;
    }

    size_t off = 0;
    jar_entry_t *previous = NULL;
    for (int i = 0; i < match_count; i++) {
        jar_entry_t *entry = next_matching_entry_locked(
            jar, host, path, is_secure, previous);
        if (!entry) {
            free(buf);
            jar_mutex_unlock(&jar->lock);
            return NULL;
        }
        if (i > 0) { buf[off++] = ';'; buf[off++] = ' '; }
        size_t nlen = strlen(entry->name);
        size_t vlen = strlen(entry->value);
        memcpy(buf + off, entry->name, nlen); off += nlen;
        buf[off++] = '=';
        memcpy(buf + off, entry->value, vlen); off += vlen;
        previous = entry;
    }
    buf[off] = '\0';
    jar_mutex_unlock(&jar->lock);
    return buf;
}

void neverc_cookiejar_clear_domain(neverc_cookiejar_t *jar,
                                     const char *domain) {
    if (!jar || !domain) return;

    char lower_domain[256];
    if (normalize_clear_domain(domain, lower_domain,
                               sizeof(lower_domain)) != 0)
        return;

    jar_mutex_lock(&jar->lock);
    jar_entry_t **pp = &jar->entries;
    while (*pp) {
        jar_entry_t *e = *pp;
        if (domains_overlap(e->domain, lower_domain)) {
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
