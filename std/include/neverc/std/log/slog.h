#ifndef NEVERC_LOG_SLOG_H
#define NEVERC_LOG_SLOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_SLOG_DEBUG = -4,
    NEVERC_SLOG_INFO  =  0,
    NEVERC_SLOG_WARN  =  4,
    NEVERC_SLOG_ERROR =  8
} neverc_slog_level_t;

typedef enum {
    NEVERC_SLOG_ATTR_NONE,
    NEVERC_SLOG_ATTR_STRING,
    NEVERC_SLOG_ATTR_INT64,
    NEVERC_SLOG_ATTR_UINT64,
    NEVERC_SLOG_ATTR_FLOAT64,
    NEVERC_SLOG_ATTR_BOOL
} neverc_slog_attr_kind_t;

typedef struct {
    const char             *key;
    neverc_slog_attr_kind_t kind;
    union {
        const char *s;
        int64_t     i;
        uint64_t    u;
        double      f;
        int         b;
    } val;
} neverc_slog_attr_t;

typedef enum {
    NEVERC_SLOG_FORMAT_TEXT,
    NEVERC_SLOG_FORMAT_JSON
} neverc_slog_format_t;

typedef struct {
    FILE                *output;
    neverc_slog_level_t  level;
    neverc_slog_format_t format;
    int                  add_source;
} neverc_slog_handler_t;

neverc_slog_attr_t neverc_slog_string(const char *key, const char *val);
neverc_slog_attr_t neverc_slog_int64(const char *key, int64_t val);
neverc_slog_attr_t neverc_slog_uint64(const char *key, uint64_t val);
neverc_slog_attr_t neverc_slog_float64(const char *key, double val);
neverc_slog_attr_t neverc_slog_bool(const char *key, int val);

void neverc_slog_init(neverc_slog_handler_t *h, FILE *output,
                      neverc_slog_level_t level, neverc_slog_format_t format);
void neverc_slog_set_default(neverc_slog_handler_t *h);
neverc_slog_handler_t *neverc_slog_default(void);
void neverc_slog_set_level(neverc_slog_handler_t *h, neverc_slog_level_t level);

void neverc_slog_log(neverc_slog_handler_t *h, neverc_slog_level_t level,
                     const char *msg, const neverc_slog_attr_t *attrs, int nattrs);

void neverc_slog_debug(const char *msg, const neverc_slog_attr_t *attrs, int nattrs);
void neverc_slog_info(const char *msg, const neverc_slog_attr_t *attrs, int nattrs);
void neverc_slog_warn(const char *msg, const neverc_slog_attr_t *attrs, int nattrs);
void neverc_slog_error(const char *msg, const neverc_slog_attr_t *attrs, int nattrs);

const char *neverc_slog_level_name(neverc_slog_level_t level);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/log.h>
#endif


#endif
