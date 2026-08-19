#include "neverc/std/log.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

typedef struct {
    FILE *output;
    const char *prefix;
    int flags;
} log_entry_t;

static void logger_lock(neverc_log_logger_t *l) {
    while (__atomic_exchange_n(&l->state_lock, 1, __ATOMIC_ACQUIRE)) {}
}

static void logger_unlock(neverc_log_logger_t *l) {
    __atomic_store_n(&l->state_lock, 0, __ATOMIC_RELEASE);
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

static int begin_entry(neverc_log_logger_t *l, log_entry_t *entry) {
    if (!l || !entry) return 0;
    logger_lock(l);
    entry->output = l->output ? l->output : stderr;
    entry->prefix = l->prefix;
    entry->flags = l->flags;
    logger_unlock(l);
    output_lock(entry->output);
    return 1;
}

static void end_entry(log_entry_t *entry, int flush) {
    if (flush) fflush(entry->output);
    output_unlock(entry->output);
}

void neverc_log_init(neverc_log_logger_t *l, FILE *output,
                     const char *prefix, int flags) {
    if (!l) return;
    l->output = output ? output : stderr;
    l->prefix = prefix;
    l->flags = flags;
    __atomic_store_n(&l->state_lock, 0, __ATOMIC_RELEASE);
}

void neverc_log_set_output(neverc_log_logger_t *l, FILE *output) {
    if (!l) return;
    logger_lock(l);
    l->output = output ? output : stderr;
    logger_unlock(l);
}

void neverc_log_set_prefix(neverc_log_logger_t *l, const char *prefix) {
    if (!l) return;
    logger_lock(l);
    l->prefix = prefix;
    logger_unlock(l);
}

void neverc_log_set_flags(neverc_log_logger_t *l, int flags) {
    if (!l) return;
    logger_lock(l);
    l->flags = flags;
    logger_unlock(l);
}

/* timespec_get is C11 and an inline UCRT wrapper on Windows; clock_gettime /
 * FILETIME are the paths already proven by slog and neverc_time_now. */
static void log_wall_clock(time_t *sec, long *nsec) {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t u = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    const uint64_t epoch = 116444736000000000ULL;
    if (u >= epoch) {
        u -= epoch;
        *sec = (time_t)(u / 10000000ULL);
        *nsec = (long)((u % 10000000ULL) * 100ULL);
        return;
    }
    *sec = time(NULL);
    *nsec = 0;
#else
    struct timespec ts = {0, 0};
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        *sec = ts.tv_sec;
        *nsec = ts.tv_nsec;
        return;
    }
    *sec = time(NULL);
    *nsec = 0;
#endif
}

static void write_header(log_entry_t *entry) {
    if (entry->prefix && !(entry->flags & NEVERC_LOG_LMSGPREFIX))
        fputs(entry->prefix, entry->output);

    if (entry->flags &
        (NEVERC_LOG_LDATE | NEVERC_LOG_LTIME | NEVERC_LOG_LMICRO)) {
        time_t now = 0;
        long nsec = 0;
        log_wall_clock(&now, &nsec);
        struct tm tm_buf;
        struct tm *tm = NULL;
#if defined(_WIN32)
        errno_t rc;
        if (entry->flags & NEVERC_LOG_LUTC)
            rc = gmtime_s(&tm_buf, &now);
        else
            rc = localtime_s(&tm_buf, &now);
        if (rc == 0) tm = &tm_buf;
#else
        if (entry->flags & NEVERC_LOG_LUTC)
            tm = gmtime_r(&now, &tm_buf);
        else
            tm = localtime_r(&now, &tm_buf);
#endif

        if (tm && (entry->flags & NEVERC_LOG_LDATE)) {
            fprintf(entry->output, "%04d/%02d/%02d ",
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
        }
        if (tm &&
            (entry->flags & (NEVERC_LOG_LTIME | NEVERC_LOG_LMICRO))) {
            fprintf(entry->output, "%02d:%02d:%02d",
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
            if (entry->flags & NEVERC_LOG_LMICRO)
                fprintf(entry->output, ".%06ld", nsec / 1000);
            fputc(' ', entry->output);
        }
    }

    if (entry->prefix && (entry->flags & NEVERC_LOG_LMSGPREFIX))
        fputs(entry->prefix, entry->output);
}

static void print_message(neverc_log_logger_t *l, const char *msg,
                          int newline, int flush) {
    if (!l || !msg) return;
    log_entry_t entry;
    if (!begin_entry(l, &entry)) return;
    write_header(&entry);
    fputs(msg, entry.output);
    if (newline) fputc('\n', entry.output);
    end_entry(&entry, flush);
}

static int log_needs_nl(const char *msg) {
    if (!msg || !msg[0]) return 1;
    return msg[strlen(msg) - 1] != '\n';
}

static void vprint_message_go(neverc_log_logger_t *l, const char *fmt,
                              va_list args, int flush) {
    /* NULL format is UB for vsnprintf; glibc often returns -1, Apple/UCRT
     * SIGSEGV. Go log.Printf is not invoked with a nil format string. */
    if (!l || !fmt) return;
    char stack[256];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(stack, sizeof(stack), fmt, copy);
    va_end(copy);
    if (n < 0) return;
    if ((size_t)n < sizeof(stack)) {
        print_message(l, stack, log_needs_nl(stack), flush);
        return;
    }
    char *heap = (char *)malloc((size_t)n + 1U);
    if (!heap) return;
    if (vsnprintf(heap, (size_t)n + 1U, fmt, args) != n) {
        free(heap);
        return;
    }
    print_message(l, heap, log_needs_nl(heap), flush);
    free(heap);
}

void neverc_log_print(neverc_log_logger_t *l, const char *msg) {
    print_message(l, msg, log_needs_nl(msg), 0);
}

void neverc_log_printf(neverc_log_logger_t *l, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprint_message_go(l, fmt, args, 0);
    va_end(args);
}

void neverc_log_println(neverc_log_logger_t *l, const char *msg) {
    print_message(l, msg, 1, 0);
}

void neverc_log_fatal(neverc_log_logger_t *l, const char *msg) {
    print_message(l, msg, log_needs_nl(msg), 1);
    exit(1);
}

void neverc_log_fatalf(neverc_log_logger_t *l, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprint_message_go(l, fmt, args, 1);
    va_end(args);
    exit(1);
}

void neverc_log_fatalln(neverc_log_logger_t *l, const char *msg) {
    print_message(l, msg, 1, 1);
    exit(1);
}

void neverc_log_panic(neverc_log_logger_t *l, const char *msg) {
    print_message(l, msg, log_needs_nl(msg), 1);
    abort();
}

void neverc_log_panicf(neverc_log_logger_t *l, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprint_message_go(l, fmt, args, 1);
    va_end(args);
    abort();
}

void neverc_log_panicln(neverc_log_logger_t *l, const char *msg) {
    print_message(l, msg, 1, 1);
    abort();
}

int neverc_log_flags(neverc_log_logger_t *l) {
    if (!l) return 0;
    logger_lock(l);
    int flags = l->flags;
    logger_unlock(l);
    return flags;
}

const char *neverc_log_prefix(neverc_log_logger_t *l) {
    if (!l) return NULL;
    logger_lock(l);
    const char *prefix = l->prefix;
    logger_unlock(l);
    return prefix;
}

FILE *neverc_log_writer(neverc_log_logger_t *l) {
    if (!l) return stderr;
    logger_lock(l);
    FILE *output = l->output ? l->output : stderr;
    logger_unlock(l);
    return output;
}

/* Default logger */
static neverc_log_logger_t default_logger = {
    NULL, "", NEVERC_LOG_LSTD, 0
};

static neverc_log_logger_t *get_default(void) {
    return &default_logger;
}

void neverc_log_default_print(const char *msg) {
    neverc_log_print(get_default(), msg);
}

void neverc_log_default_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprint_message_go(get_default(), fmt, args, 0);
    va_end(args);
}

void neverc_log_default_println(const char *msg) {
    neverc_log_println(get_default(), msg);
}

void neverc_log_default_fatal(const char *msg) {
    neverc_log_fatal(get_default(), msg);
}

void neverc_log_default_fatalf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprint_message_go(get_default(), fmt, args, 1);
    va_end(args);
    exit(1);
}

void neverc_log_default_panic(const char *msg) {
    neverc_log_panic(get_default(), msg);
}

void neverc_log_default_panicf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprint_message_go(get_default(), fmt, args, 1);
    va_end(args);
    abort();
}
