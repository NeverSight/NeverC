#include "neverc/std/log/syslog.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <stdint.h>
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

/* RFC 3164 records are newline-delimited. Replace CR/LF so a message
 * cannot inject a second "<PRI>..." line. Leave ':'/'<'/'>' in the
 * message body; those are tag delimiters, not record breaks. */
static void replace_record_breaks(char *s) {
    for (; s && *s; s++) {
        if (*s == '\n' || *s == '\r')
            *s = ' ';
    }
}

/* RFC 3164 TAG is alphanumeric (plus common ident punctuation). Space
 * would split the TAG field; ':' splits TAG from the message; '<' and
 * '>' can forge a second PRI; other controls are replaced too. */
static int tag_char_ok(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

static void sanitize_tag(char *s) {
    for (; s && *s; s++) {
        if (!tag_char_ok((unsigned char)*s))
            *s = '_';
    }
}

static void copy_tag(char *dst, size_t dstsz, const char *tag) {
    if (!tag || !*tag)
        tag = "neverc";
    size_t n = strlen(tag);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, tag, n);
    dst[n] = '\0';
    sanitize_tag(dst);
    if (dst[0] == '\0')
        memcpy(dst, "neverc", 7);
}

int neverc_syslog_pri(neverc_syslog_facility_t facility,
                      neverc_syslog_priority_t priority) {
    int sev = (int)priority;
    int fac = (int)facility;
    if (sev < 0 || sev > NEVERC_SYSLOG_DEBUG)
        return -1;
    if (fac < 0 || fac > (23 << 3) || (fac & 7) != 0)
        return -1;
    return fac | sev;
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

static char *sanitize_msg_copy(const char *msg) {
    if (!msg) return NULL;
    size_t len = strlen(msg);
    if (len == SIZE_MAX) return NULL;
    char *copy = (char *)malloc(len + 1U);
    if (!copy) return NULL;
    memcpy(copy, msg, len + 1U);
    replace_record_breaks(copy);
    return copy;
}

int neverc_syslog_format(neverc_syslog_t *log,
                         neverc_syslog_priority_t priority,
                         const char *msg, char *buf, size_t n) {
    if (!write_allowed(log, priority, msg) || !buf || n == 0)
        return -1;
    /* RFC 3164 / Go log/syslog: "<PRI>tag: message" with no extra fields. */
    int written = snprintf(buf, n, "<%d>%s: ",
                           neverc_syslog_pri(log->facility, priority),
                           log->tag);
    if (written < 0 || (size_t)written >= n)
        return -1;
    size_t prefix_len = (size_t)written;
    size_t msg_len = strlen(msg);
    if (msg_len >= n - prefix_len)
        return -1;
    memcpy(buf + prefix_len, msg, msg_len + 1U);
    replace_record_breaks(buf + prefix_len);
    return 0;
}

static int write_pri_fallback(neverc_syslog_t *log,
                              neverc_syslog_priority_t priority,
                              const char *msg) {
    char *clean = sanitize_msg_copy(msg);
    if (!clean)
        return -1;
    int written = fprintf(
        stderr, "<%d>%s: %s\n",
        neverc_syslog_pri(log->facility, priority), log->tag, clean);
    free(clean);
    return written < 0 ? -1 : 0;
}

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
#pragma comment(lib, "advapi32.lib")

neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    if (neverc_syslog_pri(facility, min_priority) < 0)
        return NULL;
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
    char *msgbuf = sanitize_msg_copy(msg);
    if (!msgbuf) return -1;
    /* RegisterEventSource without a message-table registration still treats
     * insertion specs (%1, %n, %%) in the payload. Neutralize them. */
    for (char *p = msgbuf; *p; p++)
        if (*p == '%')
            *p = '_';
    const char *msgs[1] = { msgbuf };
    if (!ReportEventA(log->event_log, etype, 0, 0, NULL, 1, 0, msgs, NULL)) {
        free(msgbuf);
        return -1;
    }
    free(msgbuf);
    return 0;
}

#elif defined(NEVERC_PLATFORM_APPLE) || defined(NEVERC_PLATFORM_LINUX) || \
      defined(NEVERC_PLATFORM_ANDROID) || defined(NEVERC_PLATFORM_BSD) || \
      defined(NEVERC_PLATFORM_POSIX)
#include <syslog.h>

#ifndef NCI_SYSLOG_OPEN
#define NCI_SYSLOG_OPEN openlog
#endif
#ifndef NCI_SYSLOG_WRITE
#define NCI_SYSLOG_WRITE syslog
#endif
#ifndef NCI_SYSLOG_CLOSE
#define NCI_SYSLOG_CLOSE closelog
#endif

/* POSIX openlog/closelog configure process-global state. This lock and owner
 * coordinate only NeverC handles; code that calls the libc syslog API directly
 * remains outside this boundary and can still change that global state. */
static int posix_syslog_state_lock;
static neverc_syslog_t *posix_syslog_owner;

static void posix_syslog_lock(void) {
    while (__atomic_exchange_n(&posix_syslog_state_lock, 1,
                               __ATOMIC_ACQUIRE)) {}
}

static void posix_syslog_unlock(void) {
    __atomic_store_n(&posix_syslog_state_lock, 0, __ATOMIC_RELEASE);
}

neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    if (neverc_syslog_pri(facility, min_priority) < 0)
        return NULL;
    neverc_syslog_t *log = (neverc_syslog_t*)calloc(1, sizeof(*log));
    if (!log) return NULL;
    copy_tag(log->tag, sizeof(log->tag), tag);
    log->facility = facility;
    log->min_priority = min_priority;
    /* Per-handle min_priority is enforced in write(); do not call setlogmask,
     * which is process-global and would leak across handles. */
    posix_syslog_lock();
    NCI_SYSLOG_OPEN(log->tag, LOG_PID | LOG_NDELAY, (int)facility);
    posix_syslog_owner = log;
    posix_syslog_unlock();
    return log;
}

void neverc_syslog_close(neverc_syslog_t *log) {
    if (!log) return;
    posix_syslog_lock();
    if (posix_syslog_owner == log) {
        NCI_SYSLOG_CLOSE();
        posix_syslog_owner = NULL;
    }
    posix_syslog_unlock();
    free(log);
}

int neverc_syslog_write(neverc_syslog_t *log,
                        neverc_syslog_priority_t priority,
                        const char *msg) {
    if (!write_allowed(log, priority, msg)) return -1;
    char *msgbuf = sanitize_msg_copy(msg);
    if (!msgbuf) return -1;
    /* Rebind the process-global ident for every record. Keeping this call and
     * syslog() in one NeverC critical section prevents two NeverC handles from
     * attributing each other's records. */
    posix_syslog_lock();
    NCI_SYSLOG_OPEN(log->tag, LOG_PID | LOG_NDELAY, (int)log->facility);
    posix_syslog_owner = log;
    NCI_SYSLOG_WRITE(neverc_syslog_pri(log->facility, priority), "%s", msgbuf);
    posix_syslog_unlock();
    free(msgbuf);
    return 0;
}

#else
neverc_syslog_t *neverc_syslog_open(const char *tag,
                                     neverc_syslog_facility_t facility,
                                     neverc_syslog_priority_t min_priority) {
    if (neverc_syslog_pri(facility, min_priority) < 0)
        return NULL;
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
