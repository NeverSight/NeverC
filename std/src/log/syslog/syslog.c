#include "neverc/std/log/syslog.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct neverc_syslog {
    char                      tag[128];
    neverc_syslog_facility_t  facility;
    neverc_syslog_priority_t  min_priority;
#if defined(NEVERC_PLATFORM_WINDOWS)
    void                     *event_log;
#endif
};

static void copy_tag(char *dst, size_t dstsz, const char *tag) {
    if (!tag || !*tag)
        tag = "neverc";
    size_t n = strlen(tag);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, tag, n);
    dst[n] = '\0';
}

int neverc_syslog_pri(neverc_syslog_facility_t facility,
                      neverc_syslog_priority_t priority) {
    if ((int)priority < 0 || (int)priority > NEVERC_SYSLOG_DEBUG)
        return -1;
    return ((int)facility & ~7) | ((int)priority & 7);
}

static int write_allowed(neverc_syslog_t *log,
                         neverc_syslog_priority_t priority,
                         const char *msg) {
    if (!log || !msg || neverc_syslog_pri(log->facility, priority) < 0)
        return 0;
    if (priority > log->min_priority)
        return 0;
    return 1;
}

static const char *priority_name(neverc_syslog_priority_t priority) {
    static const char *pnames[] = {
        "EMERG", "ALERT", "CRIT", "ERR", "WARN", "NOTICE", "INFO", "DEBUG"
    };
    return pnames[(int)priority];
}

static int write_pri_fallback(neverc_syslog_t *log,
                              neverc_syslog_priority_t priority,
                              const char *msg) {
    fprintf(stderr, "<%d>%s: [%s] %s\n",
            neverc_syslog_pri(log->facility, priority),
            log->tag, priority_name(priority), msg);
    return 0;
}

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
#pragma comment(lib, "advapi32.lib")

neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    copy_tag(log->tag, sizeof(log->tag), tag);
    log->facility = facility;
    log->min_priority = min_priority;
    log->event_log = RegisterEventSourceA(NULL, log->tag);
    return log;
}

void neverc_syslog_close(neverc_syslog_t *log) {
    if (!log) return;
    if (log->event_log) DeregisterEventSource(log->event_log);
    free(log);
}

int neverc_syslog_write(neverc_syslog_t *log,
                        neverc_syslog_priority_t priority,
                        const char *msg) {
    if (!write_allowed(log, priority, msg)) return -1;
    if (!log->event_log)
        return write_pri_fallback(log, priority, msg);
    WORD etype = EVENTLOG_INFORMATION_TYPE;
    if (priority <= NEVERC_SYSLOG_ERR) etype = EVENTLOG_ERROR_TYPE;
    else if (priority <= NEVERC_SYSLOG_WARNING) etype = EVENTLOG_WARNING_TYPE;
    const char *msgs[1] = { msg };
    ReportEventA(log->event_log, etype, 0, 0, NULL, 1, 0, msgs, NULL);
    return 0;
}

#elif defined(NEVERC_PLATFORM_APPLE) || defined(NEVERC_PLATFORM_LINUX) || \
      defined(NEVERC_PLATFORM_ANDROID) || defined(NEVERC_PLATFORM_BSD) || \
      defined(NEVERC_PLATFORM_POSIX)
#include <syslog.h>

neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    copy_tag(log->tag, sizeof(log->tag), tag);
    log->facility = facility;
    log->min_priority = min_priority;
    /* Per-handle min_priority is enforced in write(); do not call setlogmask,
     * which is process-global and would leak across handles. */
    openlog(log->tag, LOG_PID | LOG_NDELAY, (int)facility);
    return log;
}

void neverc_syslog_close(neverc_syslog_t *log) {
    if (!log) return;
    closelog();
    free(log);
}

int neverc_syslog_write(neverc_syslog_t *log,
                        neverc_syslog_priority_t priority,
                        const char *msg) {
    if (!write_allowed(log, priority, msg)) return -1;
    syslog(neverc_syslog_pri(log->facility, priority), "%s", msg);
    return 0;
}

#else
neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    copy_tag(log->tag, sizeof(log->tag), tag);
    log->facility = facility;
    log->min_priority = min_priority;
    return log;
}

void neverc_syslog_close(neverc_syslog_t *log) { free(log); }

int neverc_syslog_write(neverc_syslog_t *log,
                        neverc_syslog_priority_t priority,
                        const char *msg) {
    if (!write_allowed(log, priority, msg)) return -1;
    return write_pri_fallback(log, priority, msg);
}
#endif

int neverc_syslog_emerg(neverc_syslog_t *log, const char *msg)   { return neverc_syslog_write(log, NEVERC_SYSLOG_EMERG, msg); }
int neverc_syslog_alert(neverc_syslog_t *log, const char *msg)   { return neverc_syslog_write(log, NEVERC_SYSLOG_ALERT, msg); }
int neverc_syslog_crit(neverc_syslog_t *log, const char *msg)    { return neverc_syslog_write(log, NEVERC_SYSLOG_CRIT, msg); }
int neverc_syslog_err(neverc_syslog_t *log, const char *msg)     { return neverc_syslog_write(log, NEVERC_SYSLOG_ERR, msg); }
int neverc_syslog_warning(neverc_syslog_t *log, const char *msg) { return neverc_syslog_write(log, NEVERC_SYSLOG_WARNING, msg); }
int neverc_syslog_notice(neverc_syslog_t *log, const char *msg)  { return neverc_syslog_write(log, NEVERC_SYSLOG_NOTICE, msg); }
int neverc_syslog_info(neverc_syslog_t *log, const char *msg)    { return neverc_syslog_write(log, NEVERC_SYSLOG_INFO, msg); }
int neverc_syslog_debug(neverc_syslog_t *log, const char *msg)   { return neverc_syslog_write(log, NEVERC_SYSLOG_DEBUG, msg); }
