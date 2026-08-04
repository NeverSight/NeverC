#ifndef NEVERC_LOG_H
#define NEVERC_LOG_H

/*
 * NeverC log — logging (mirrors Go log package).
 *
 * Supports configurable prefix, date/time flags, and output writer.
 * Default output is stderr.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_LOG_LDATE     (1 << 0)
#define NEVERC_LOG_LTIME     (1 << 1)
#define NEVERC_LOG_LMICRO    (1 << 2)
/* Reserved for Go API compatibility. The C ABI does not carry call-site
 * metadata, so these flags currently do not emit a file location. */
#define NEVERC_LOG_LLONGFILE (1 << 3)
#define NEVERC_LOG_LSHORTFILE (1 << 4)
#define NEVERC_LOG_LUTC      (1 << 5)
#define NEVERC_LOG_LMSGPREFIX (1 << 6)
#define NEVERC_LOG_LSTD      (NEVERC_LOG_LDATE | NEVERC_LOG_LTIME)

typedef struct {
    FILE       *output;
    const char *prefix;
    int         flags;
    /* Internal synchronization state; initialize loggers with neverc_log_init. */
    int         state_lock;
} neverc_log_logger_t;

void neverc_log_init(neverc_log_logger_t *l, FILE *output,
                     const char *prefix, int flags);
void neverc_log_set_output(neverc_log_logger_t *l, FILE *output);
void neverc_log_set_prefix(neverc_log_logger_t *l, const char *prefix);
void neverc_log_set_flags(neverc_log_logger_t *l, int flags);

void neverc_log_print(neverc_log_logger_t *l, const char *msg);
void neverc_log_printf(neverc_log_logger_t *l, const char *fmt, ...);
void neverc_log_println(neverc_log_logger_t *l, const char *msg);

void neverc_log_fatal(neverc_log_logger_t *l, const char *msg);
void neverc_log_fatalf(neverc_log_logger_t *l, const char *fmt, ...);
void neverc_log_fatalln(neverc_log_logger_t *l, const char *msg);

void neverc_log_panic(neverc_log_logger_t *l, const char *msg);
void neverc_log_panicf(neverc_log_logger_t *l, const char *fmt, ...);
void neverc_log_panicln(neverc_log_logger_t *l, const char *msg);

int         neverc_log_flags(neverc_log_logger_t *l);
const char *neverc_log_prefix(neverc_log_logger_t *l);
FILE       *neverc_log_writer(neverc_log_logger_t *l);

/* Default logger (stderr, LSTD flags) */
void neverc_log_default_print(const char *msg);
void neverc_log_default_printf(const char *fmt, ...);
void neverc_log_default_println(const char *msg);
void neverc_log_default_fatal(const char *msg);
void neverc_log_default_fatalf(const char *fmt, ...);
void neverc_log_default_panic(const char *msg);
void neverc_log_default_panicf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_slog_t { char __tag; };
struct __neverc_std_syslog_t { char __tag; };

struct __neverc_std_log_t {
    char __tag;
    struct __neverc_std_slog_t slog;
    struct __neverc_std_syslog_t syslog;
};
extern struct __neverc_std_log_t __neverc_mod_log;
extern struct __neverc_std_log_t log_mod;
#endif

#endif /* NEVERC_LOG_H */
