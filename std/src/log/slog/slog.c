#include "neverc/std/log/slog.h"
#include "neverc/std/strconv.h"
#include <string.h>
#include <time.h>

typedef struct {
    FILE                *output;
    neverc_slog_level_t  level;
    neverc_slog_format_t format;
    int                  add_source;
} slog_config_t;

static neverc_slog_handler_t g_default_handler = {
    NULL, NEVERC_SLOG_INFO, NEVERC_SLOG_FORMAT_TEXT, 0, 0
};

static void handler_lock(neverc_slog_handler_t *h) {
    while (__atomic_exchange_n(&h->state_lock, 1, __ATOMIC_ACQUIRE)) {}
}

static void handler_unlock(neverc_slog_handler_t *h) {
    __atomic_store_n(&h->state_lock, 0, __ATOMIC_RELEASE);
}

static void output_lock(FILE *output) {
#ifdef _WIN32
    _lock_file(output);
#else
    flockfile(output);
#endif
}

static void output_unlock(FILE *output) {
#ifdef _WIN32
    _unlock_file(output);
#else
    funlockfile(output);
#endif
}

static slog_config_t snapshot_handler(neverc_slog_handler_t *h) {
    slog_config_t config;
    handler_lock(h);
    config.output = h->output ? h->output : stderr;
    config.level = h->level;
    config.format = h->format;
    config.add_source = h->add_source;
    handler_unlock(h);
    return config;
}

static void assign_handler(neverc_slog_handler_t *h,
                           const slog_config_t *config) {
    handler_lock(h);
    h->output = config->output ? config->output : stderr;
    h->level = config->level;
    h->format = config->format;
    h->add_source = config->add_source;
    handler_unlock(h);
}

neverc_slog_attr_t neverc_slog_string(const char *key, const char *val) {
    neverc_slog_attr_t a;
    a.key = key;
    a.kind = NEVERC_SLOG_ATTR_STRING;
    a.val.s = val;
    return a;
}

neverc_slog_attr_t neverc_slog_int64(const char *key, int64_t val) {
    neverc_slog_attr_t a;
    a.key = key;
    a.kind = NEVERC_SLOG_ATTR_INT64;
    a.val.i = val;
    return a;
}

neverc_slog_attr_t neverc_slog_uint64(const char *key, uint64_t val) {
    neverc_slog_attr_t a;
    a.key = key;
    a.kind = NEVERC_SLOG_ATTR_UINT64;
    a.val.u = val;
    return a;
}

neverc_slog_attr_t neverc_slog_float64(const char *key, double val) {
    neverc_slog_attr_t a;
    a.key = key;
    a.kind = NEVERC_SLOG_ATTR_FLOAT64;
    a.val.f = val;
    return a;
}

neverc_slog_attr_t neverc_slog_bool(const char *key, int val) {
    neverc_slog_attr_t a;
    a.key = key;
    a.kind = NEVERC_SLOG_ATTR_BOOL;
    a.val.b = val;
    return a;
}

void neverc_slog_init(neverc_slog_handler_t *h, FILE *output,
                      neverc_slog_level_t level, neverc_slog_format_t format) {
    if (!h) return;
    h->output = output ? output : stderr;
    h->level = level;
    h->format = format;
    h->add_source = 0;
    __atomic_store_n(&h->state_lock, 0, __ATOMIC_RELEASE);
}

void neverc_slog_set_default(neverc_slog_handler_t *h) {
    if (!h || h == &g_default_handler) return;
    slog_config_t config = snapshot_handler(h);
    assign_handler(&g_default_handler, &config);
}

neverc_slog_handler_t *neverc_slog_default(void) {
    return &g_default_handler;
}

void neverc_slog_set_level(neverc_slog_handler_t *h, neverc_slog_level_t level) {
    if (!h) return;
    handler_lock(h);
    h->level = level;
    handler_unlock(h);
}

const char *neverc_slog_level_name(neverc_slog_level_t level) {
    if (level < NEVERC_SLOG_INFO)  return "DEBUG";
    if (level < NEVERC_SLOG_WARN)  return "INFO";
    if (level < NEVERC_SLOG_ERROR) return "WARN";
    return "ERROR";
}

static int is_utf8_continuation(unsigned char c) {
    return (c & 0xc0U) == 0x80U;
}

static size_t valid_utf8_sequence(const unsigned char *p) {
    unsigned char c = p[0];
    if (c < 0x80U) return 1;
    if (c >= 0xc2U && c <= 0xdfU && is_utf8_continuation(p[1]))
        return 2;
    if (c == 0xe0U && p[1] >= 0xa0U && p[1] <= 0xbfU &&
        is_utf8_continuation(p[2]))
        return 3;
    if (((c >= 0xe1U && c <= 0xecU) || (c >= 0xeeU && c <= 0xefU)) &&
        is_utf8_continuation(p[1]) && is_utf8_continuation(p[2]))
        return 3;
    if (c == 0xedU && p[1] >= 0x80U && p[1] <= 0x9fU &&
        is_utf8_continuation(p[2]))
        return 3;
    if (c == 0xf0U && p[1] >= 0x90U && p[1] <= 0xbfU &&
        is_utf8_continuation(p[2]) && is_utf8_continuation(p[3]))
        return 4;
    if (c >= 0xf1U && c <= 0xf3U && is_utf8_continuation(p[1]) &&
        is_utf8_continuation(p[2]) && is_utf8_continuation(p[3]))
        return 4;
    if (c == 0xf4U && p[1] >= 0x80U && p[1] <= 0x8fU &&
        is_utf8_continuation(p[2]) && is_utf8_continuation(p[3]))
        return 4;
    return 0;
}

static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    if (s) {
        const unsigned char *p = (const unsigned char *)s;
        while (*p) {
            switch (*p) {
            case '"':  fputs("\\\"", f); p++; break;
            case '\\': fputs("\\\\", f); p++; break;
            case '\b': fputs("\\b", f);  p++; break;
            case '\f': fputs("\\f", f);  p++; break;
            case '\n': fputs("\\n", f);  p++; break;
            case '\r': fputs("\\r", f);  p++; break;
            case '\t': fputs("\\t", f);  p++; break;
            default:
                if (*p < 0x20U) {
                    fprintf(f, "\\u%04x", (unsigned)*p);
                    p++;
                } else if (*p < 0x80U) {
                    fputc(*p++, f);
                } else {
                    size_t length = valid_utf8_sequence(p);
                    if (length == 0) {
                        fputs("\\ufffd", f);
                        p++;
                    } else {
                        fwrite(p, 1, length, f);
                        p += length;
                    }
                }
            }
        }
    }
    fputc('"', f);
}

static void get_timestamp(char *buf, size_t sz) {
    if (!buf || sz == 0) return;
    buf[0] = '\0';
    time_t now = time(NULL);
    struct tm tm = {0};
    int converted = 0;
#if defined(_WIN32)
    converted = gmtime_s(&tm, &now) == 0;
#else
    converted = gmtime_r(&now, &tm) != NULL;
#endif
    if (!converted || strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &tm) == 0)
        snprintf(buf, sz, "%s", "0000-00-00T00:00:00Z");
}

static int text_needs_quotes(const char *s) {
    if (!s || !*s) return 1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p <= 0x20U || *p >= 0x7fU || *p == '"' || *p == '\\' ||
            *p == '=')
            return 1;
    }
    return 0;
}

static void write_text_token(FILE *f, const char *s) {
    if (text_needs_quotes(s))
        write_json_string(f, s ? s : "");
    else
        fputs(s, f);
}

static int float_is_finite(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

static void write_float(FILE *f, double value, int json) {
    if (json && !float_is_finite(value)) {
        fputs("null", f);
        return;
    }
    char buffer[64];
    int length = neverc_strconv_format_float(value, 'g', -1,
                                              buffer, sizeof(buffer));
    if (length < 0)
        fputs(json ? "null" : "0", f);
    else
        fwrite(buffer, 1, (size_t)length, f);
}

static void write_attr_text(FILE *f, const neverc_slog_attr_t *a) {
    fputc(' ', f);
    write_text_token(f, a->key);
    fputc('=', f);
    switch (a->kind) {
    case NEVERC_SLOG_ATTR_STRING:
        write_text_token(f, a->val.s ? a->val.s : "");
        break;
    case NEVERC_SLOG_ATTR_INT64:
        fprintf(f, "%lld", (long long)a->val.i);
        break;
    case NEVERC_SLOG_ATTR_UINT64:
        fprintf(f, "%llu", (unsigned long long)a->val.u);
        break;
    case NEVERC_SLOG_ATTR_FLOAT64:
        write_float(f, a->val.f, 0);
        break;
    case NEVERC_SLOG_ATTR_BOOL:
        fprintf(f, "%s", a->val.b ? "true" : "false");
        break;
    default:
        fputs("null", f);
        break;
    }
}

static int slog_attr_emit(const neverc_slog_attr_t *a) {
    return a && a->key && a->key[0] && a->kind != NEVERC_SLOG_ATTR_NONE;
}

static void write_attr_json(FILE *f, const neverc_slog_attr_t *a) {
    fputc(',', f);
    write_json_string(f, a->key);
    fputc(':', f);
    switch (a->kind) {
    case NEVERC_SLOG_ATTR_STRING:
        write_json_string(f, a->val.s ? a->val.s : "");
        break;
    case NEVERC_SLOG_ATTR_INT64:
        fprintf(f, "%lld", (long long)a->val.i);
        break;
    case NEVERC_SLOG_ATTR_UINT64:
        fprintf(f, "%llu", (unsigned long long)a->val.u);
        break;
    case NEVERC_SLOG_ATTR_FLOAT64:
        write_float(f, a->val.f, 1);
        break;
    case NEVERC_SLOG_ATTR_BOOL:
        fprintf(f, "%s", a->val.b ? "true" : "false");
        break;
    default:
        fputs("null", f);
    }
}

void neverc_slog_log(neverc_slog_handler_t *h, neverc_slog_level_t level,
                     const char *msg, const neverc_slog_attr_t *attrs, int nattrs) {
    if (nattrs < 0 || (nattrs > 0 && !attrs)) return;
    if (!h) h = &g_default_handler;
    slog_config_t config = snapshot_handler(h);
    if (level < config.level) return;

    char ts[64];
    get_timestamp(ts, sizeof(ts));
    output_lock(config.output);

    if (config.format == NEVERC_SLOG_FORMAT_JSON) {
        fprintf(config.output, "{\"time\":\"%s\",\"level\":\"%s\",\"msg\":",
                ts, neverc_slog_level_name(level));
        write_json_string(config.output, msg);
        for (int i = 0; i < nattrs; i++) {
            if (!slog_attr_emit(&attrs[i])) continue;
            write_attr_json(config.output, &attrs[i]);
        }
        fputs("}\n", config.output);
    } else {
        fprintf(config.output, "time=%s level=%s msg=",
                ts, neverc_slog_level_name(level));
        write_json_string(config.output, msg);
        for (int i = 0; i < nattrs; i++) {
            if (!slog_attr_emit(&attrs[i])) continue;
            write_attr_text(config.output, &attrs[i]);
        }
        fputc('\n', config.output);
    }
    fflush(config.output);
    output_unlock(config.output);
}

void neverc_slog_debug(const char *msg, const neverc_slog_attr_t *attrs, int nattrs) {
    neverc_slog_log(NULL, NEVERC_SLOG_DEBUG, msg, attrs, nattrs);
}

void neverc_slog_info(const char *msg, const neverc_slog_attr_t *attrs, int nattrs) {
    neverc_slog_log(NULL, NEVERC_SLOG_INFO, msg, attrs, nattrs);
}

void neverc_slog_warn(const char *msg, const neverc_slog_attr_t *attrs, int nattrs) {
    neverc_slog_log(NULL, NEVERC_SLOG_WARN, msg, attrs, nattrs);
}

void neverc_slog_error(const char *msg, const neverc_slog_attr_t *attrs, int nattrs) {
    neverc_slog_log(NULL, NEVERC_SLOG_ERROR, msg, attrs, nattrs);
}
