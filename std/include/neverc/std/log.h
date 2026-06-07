#ifndef NEVERC_LOG_H
#define NEVERC_LOG_H

/*
 * NeverC log — logging (mirrors Go log package).
 *
 * Supports configurable prefix, flags (date/time/file), and output writer.
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
#define NEVERC_LOG_LLONGFILE (1 << 3)
#define NEVERC_LOG_LSHORTFILE (1 << 4)
#define NEVERC_LOG_LUTC      (1 << 5)
#define NEVERC_LOG_LMSGPREFIX (1 << 6)
#define NEVERC_LOG_LSTD      (NEVERC_LOG_LDATE | NEVERC_LOG_LTIME)

typedef struct {
    FILE       *output;
    const char *prefix;
    int         flags;
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

/* Default logger (stderr, LSTD flags) */
void neverc_log_default_print(const char *msg);
void neverc_log_default_printf(const char *fmt, ...);
void neverc_log_default_println(const char *msg);
void neverc_log_default_fatal(const char *msg);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_log_t { char __tag; };
extern struct __neverc_std_log_t __neverc_mod_log;
#endif

#endif /* NEVERC_LOG_H */
