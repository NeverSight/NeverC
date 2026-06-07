#include "neverc/log/slog.h"
#include <string.h>
#include <time.h>

static neverc_slog_handler_t g_default_handler;
static int g_initialized = 0;

static void ensure_default(void) {
    if (!g_initialized) {
        neverc_slog_init(&g_default_handler, 0, NEVERC_SLOG_INFO, NEVERC_SLOG_FORMAT_TEXT);
        g_initialized = 1;
    }
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
    memset(h, 0, sizeof(*h));
    h->output = output ? output : stderr;
    h->level = level;
    h->format = format;
    h->add_source = 0;
}

void neverc_slog_set_default(neverc_slog_handler_t *h) {
    g_default_handler = *h;
    g_initialized = 1;
}

neverc_slog_handler_t *neverc_slog_default(void) {
    ensure_default();
    return &g_default_handler;
}

void neverc_slog_set_level(neverc_slog_handler_t *h, neverc_slog_level_t level) {
    h->level = level;
}

const char *neverc_slog_level_name(neverc_slog_level_t level) {
    if (level < NEVERC_SLOG_INFO)  return "DEBUG";
    if (level < NEVERC_SLOG_WARN)  return "INFO";
    if (level < NEVERC_SLOG_ERROR) return "WARN";
    return "ERROR";
}

static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    if (s) {
        for (const char *p = s; *p; p++) {
            switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f);  break;
            case '\r': fputs("\\r", f);  break;
            case '\t': fputs("\\t", f);  break;
            default:
                if ((unsigned char)*p < 0x20)
                    fprintf(f, "\\u%04x", (unsigned char)*p);
                else
                    fputc(*p, f);
            }
        }
    }
    fputc('"', f);
}

static void get_timestamp(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void write_attr_text(FILE *f, const neverc_slog_attr_t *a) {
    fprintf(f, " %s=", a->key);
    switch (a->kind) {
    case NEVERC_SLOG_ATTR_STRING:
        fprintf(f, "%s", a->val.s ? a->val.s : "");
        break;
    case NEVERC_SLOG_ATTR_INT64:
        fprintf(f, "%lld", (long long)a->val.i);
        break;
    case NEVERC_SLOG_ATTR_UINT64:
        fprintf(f, "%llu", (unsigned long long)a->val.u);
        break;
    case NEVERC_SLOG_ATTR_FLOAT64:
        fprintf(f, "%g", a->val.f);
        break;
    case NEVERC_SLOG_ATTR_BOOL:
        fprintf(f, "%s", a->val.b ? "true" : "false");
        break;
    default:
        break;
    }
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
        fprintf(f, "%g", a->val.f);
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
    if (!h) h = neverc_slog_default();
    if (level < h->level) return;

    char ts[64];
    get_timestamp(ts, sizeof(ts));

    if (h->format == NEVERC_SLOG_FORMAT_JSON) {
        fprintf(h->output, "{\"time\":\"%s\",\"level\":\"%s\",\"msg\":",
                ts, neverc_slog_level_name(level));
        write_json_string(h->output, msg);
        for (int i = 0; i < nattrs; i++)
            write_attr_json(h->output, &attrs[i]);
        fprintf(h->output, "}\n");
    } else {
        fprintf(h->output, "time=%s level=%s msg=\"%s\"",
                ts, neverc_slog_level_name(level), msg ? msg : "");
        for (int i = 0; i < nattrs; i++)
            write_attr_text(h->output, &attrs[i]);
        fputc('\n', h->output);
    }
    fflush(h->output);
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
