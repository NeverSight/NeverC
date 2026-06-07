#include "neverc/std/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

void neverc_log_init(neverc_log_logger_t *l, FILE *output,
                     const char *prefix, int flags) {
    l->output = output ? output : stderr;
    l->prefix = prefix;
    l->flags = flags;
}

void neverc_log_set_output(neverc_log_logger_t *l, FILE *output) {
    l->output = output;
}

void neverc_log_set_prefix(neverc_log_logger_t *l, const char *prefix) {
    l->prefix = prefix;
}

void neverc_log_set_flags(neverc_log_logger_t *l, int flags) {
    l->flags = flags;
}

static void write_header(neverc_log_logger_t *l) {
    if (l->prefix && !(l->flags & NEVERC_LOG_LMSGPREFIX)) {
        fputs(l->prefix, l->output);
    }

    if (l->flags & (NEVERC_LOG_LDATE | NEVERC_LOG_LTIME | NEVERC_LOG_LMICRO)) {
        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm *tm;
#if defined(_WIN32)
        if (l->flags & NEVERC_LOG_LUTC)
            { gmtime_s(&tm_buf, &now); tm = &tm_buf; }
        else
            { localtime_s(&tm_buf, &now); tm = &tm_buf; }
#else
        if (l->flags & NEVERC_LOG_LUTC)
            tm = gmtime_r(&now, &tm_buf);
        else
            tm = localtime_r(&now, &tm_buf);
#endif

        if (l->flags & NEVERC_LOG_LDATE) {
            fprintf(l->output, "%04d/%02d/%02d ",
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
        }
        if (l->flags & NEVERC_LOG_LTIME) {
            fprintf(l->output, "%02d:%02d:%02d ",
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
        }
    }

    if (l->prefix && (l->flags & NEVERC_LOG_LMSGPREFIX)) {
        fputs(l->prefix, l->output);
    }
}

void neverc_log_print(neverc_log_logger_t *l, const char *msg) {
    write_header(l);
    fputs(msg, l->output);
}

void neverc_log_printf(neverc_log_logger_t *l, const char *fmt, ...) {
    write_header(l);
    va_list args;
    va_start(args, fmt);
    vfprintf(l->output, fmt, args);
    va_end(args);
}

void neverc_log_println(neverc_log_logger_t *l, const char *msg) {
    write_header(l);
    fputs(msg, l->output);
    fputc('\n', l->output);
}

void neverc_log_fatal(neverc_log_logger_t *l, const char *msg) {
    neverc_log_println(l, msg);
    exit(1);
}

void neverc_log_fatalf(neverc_log_logger_t *l, const char *fmt, ...) {
    write_header(l);
    va_list args;
    va_start(args, fmt);
    vfprintf(l->output, fmt, args);
    va_end(args);
    fputc('\n', l->output);
    exit(1);
}

/* Default logger */
static neverc_log_logger_t default_logger = { NULL, "", NEVERC_LOG_LSTD };

static neverc_log_logger_t *get_default(void) {
    if (!default_logger.output) default_logger.output = stderr;
    return &default_logger;
}

void neverc_log_default_print(const char *msg) {
    neverc_log_print(get_default(), msg);
}

void neverc_log_default_printf(const char *fmt, ...) {
    neverc_log_logger_t *l = get_default();
    write_header(l);
    va_list args;
    va_start(args, fmt);
    vfprintf(l->output, fmt, args);
    va_end(args);
}

void neverc_log_default_println(const char *msg) {
    neverc_log_println(get_default(), msg);
}

void neverc_log_default_fatal(const char *msg) {
    neverc_log_fatal(get_default(), msg);
}
