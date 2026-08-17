#ifndef NEVERC_LOG_SYSLOG_H
#define NEVERC_LOG_SYSLOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_SYSLOG_EMERG   = 0,
    NEVERC_SYSLOG_ALERT   = 1,
    NEVERC_SYSLOG_CRIT    = 2,
    NEVERC_SYSLOG_ERR     = 3,
    NEVERC_SYSLOG_WARNING = 4,
    NEVERC_SYSLOG_NOTICE  = 5,
    NEVERC_SYSLOG_INFO    = 6,
    NEVERC_SYSLOG_DEBUG   = 7,
} neverc_syslog_priority_t;

typedef enum {
    NEVERC_SYSLOG_KERN     = 0 << 3,
    NEVERC_SYSLOG_USER     = 1 << 3,
    NEVERC_SYSLOG_MAIL     = 2 << 3,
    NEVERC_SYSLOG_DAEMON   = 3 << 3,
    NEVERC_SYSLOG_AUTH     = 4 << 3,
    NEVERC_SYSLOG_SYSLOG   = 5 << 3,
    NEVERC_SYSLOG_LPR      = 6 << 3,
    NEVERC_SYSLOG_NEWS     = 7 << 3,
    NEVERC_SYSLOG_UUCP     = 8 << 3,
    NEVERC_SYSLOG_CRON     = 9 << 3,
    NEVERC_SYSLOG_AUTHPRIV = 10 << 3,
    NEVERC_SYSLOG_FTP      = 11 << 3,
    NEVERC_SYSLOG_LOCAL0   = 16 << 3,
    NEVERC_SYSLOG_LOCAL1   = 17 << 3,
    NEVERC_SYSLOG_LOCAL2   = 18 << 3,
    NEVERC_SYSLOG_LOCAL3   = 19 << 3,
    NEVERC_SYSLOG_LOCAL4   = 20 << 3,
    NEVERC_SYSLOG_LOCAL5   = 21 << 3,
    NEVERC_SYSLOG_LOCAL6   = 22 << 3,
    NEVERC_SYSLOG_LOCAL7   = 23 << 3,
} neverc_syslog_facility_t;

typedef struct neverc_syslog neverc_syslog_t;

/* Open a syslog connection. tag is the program identifier.
 * Returns handle, or NULL on error. */
neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority);

/* Close syslog connection. */
void neverc_syslog_close(neverc_syslog_t *log);

/* RFC 5424 PRI = facility | severity. Returns -1 if severity is out of range. */
int neverc_syslog_pri(neverc_syslog_facility_t facility,
                      neverc_syslog_priority_t priority);

/* Write a message at the given priority. Returns 0 on success. */
int neverc_syslog_write(neverc_syslog_t *log,
                        neverc_syslog_priority_t priority,
                        const char *msg);

/* Convenience: write at specific priority levels */
int neverc_syslog_emerg(neverc_syslog_t *log, const char *msg);
int neverc_syslog_alert(neverc_syslog_t *log, const char *msg);
int neverc_syslog_crit(neverc_syslog_t *log, const char *msg);
int neverc_syslog_err(neverc_syslog_t *log, const char *msg);
int neverc_syslog_warning(neverc_syslog_t *log, const char *msg);
int neverc_syslog_notice(neverc_syslog_t *log, const char *msg);
int neverc_syslog_info(neverc_syslog_t *log, const char *msg);
int neverc_syslog_debug(neverc_syslog_t *log, const char *msg);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/log.h>
#endif


#endif
