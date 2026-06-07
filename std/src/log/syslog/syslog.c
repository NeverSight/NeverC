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

#if defined(NEVERC_PLATFORM_WINDOWS)
/* Windows: use Event Log API */
#include <windows.h>
#pragma comment(lib, "advapi32.lib")

neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    if (tag) { strncpy(log->tag, tag, sizeof(log->tag)-1); }
    else { strcpy(log->tag, "neverc"); }
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
    if (!log || !msg || priority > log->min_priority) return -1;
    if (!log->event_log) return -1;
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
/* POSIX: use syslog(3) */
#include <syslog.h>

neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    if (tag) { strncpy(log->tag, tag, sizeof(log->tag)-1); }
    else { strcpy(log->tag, "neverc"); }
    log->facility = facility;
    log->min_priority = min_priority;
    openlog(log->tag, LOG_PID | LOG_NDELAY, (int)facility);
    setlogmask(LOG_UPTO((int)min_priority));
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
    if (!log || !msg || priority > log->min_priority) return -1;
    syslog((int)priority, "%s", msg);
    return 0;
}

#else
/* Fallback: write to stderr */
neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    if (tag) { strncpy(log->tag, tag, sizeof(log->tag)-1); }
    else { strcpy(log->tag, "neverc"); }
    log->facility = facility;
    log->min_priority = min_priority;
    return log;
}

void neverc_syslog_close(neverc_syslog_t *log) { free(log); }

int neverc_syslog_write(neverc_syslog_t *log,
                        neverc_syslog_priority_t priority,
                        const char *msg) {
    if (!log || !msg || priority > log->min_priority) return -1;
    static const char *pnames[] = {"EMERG","ALERT","CRIT","ERR","WARN","NOTICE","INFO","DEBUG"};
    fprintf(stderr, "%s: [%s] %s\n", log->tag, pnames[priority], msg);
    return 0;
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
