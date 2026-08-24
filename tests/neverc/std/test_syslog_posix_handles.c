#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

int main(void) {
    puts("passed");
    return 0;
}

#else

typedef struct {
    int  connection_open;
    int  priority;
    char ident[128];
    char message[256];
} captured_syslog_record_t;

static const char *injected_ident;
static int injected_connection_open;
static int injected_close_calls;
static captured_syslog_record_t injected_records[8];
static size_t injected_record_count;

static void injected_syslog_open(const char *ident, int option, int facility);
static void injected_syslog_write(int priority, const char *format, ...);
static void injected_syslog_close(void);

#define NCI_SYSLOG_OPEN injected_syslog_open
#define NCI_SYSLOG_WRITE injected_syslog_write
#define NCI_SYSLOG_CLOSE injected_syslog_close
#include "../../../std/src/log/syslog/syslog.c"
#undef NCI_SYSLOG_CLOSE
#undef NCI_SYSLOG_WRITE
#undef NCI_SYSLOG_OPEN

static void injected_syslog_open(const char *ident, int option, int facility) {
    (void)option;
    (void)facility;
    injected_ident = ident;
    injected_connection_open = 1;
}

static void injected_syslog_write(int priority, const char *format, ...) {
    if (injected_record_count >=
        sizeof(injected_records) / sizeof(injected_records[0]))
        return;

    captured_syslog_record_t *record =
        &injected_records[injected_record_count++];
    record->connection_open = injected_connection_open;
    record->priority = priority;
    snprintf(record->ident, sizeof(record->ident), "%s",
             injected_ident ? injected_ident : "");

    va_list args;
    va_start(args, format);
    vsnprintf(record->message, sizeof(record->message), format, args);
    va_end(args);
}

static void injected_syslog_close(void) {
    injected_ident = NULL;
    injected_connection_open = 0;
    injected_close_calls++;
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    neverc_syslog_t *alpha =
        neverc_syslog_open("alpha", NEVERC_SYSLOG_LOCAL0,
                           NEVERC_SYSLOG_DEBUG);
    neverc_syslog_t *beta =
        neverc_syslog_open("beta", NEVERC_SYSLOG_LOCAL1,
                           NEVERC_SYSLOG_DEBUG);
    CHECK(alpha != NULL);
    CHECK(beta != NULL);

    CHECK(neverc_syslog_info(alpha, "from alpha") == 0);
    CHECK(neverc_syslog_warning(beta, "from beta") == 0);
    CHECK(injected_record_count == 2);
    CHECK(injected_records[0].connection_open);
    CHECK(strcmp(injected_records[0].ident, "alpha") == 0);
    CHECK(strcmp(injected_records[0].message, "from alpha") == 0);
    CHECK(injected_records[0].priority ==
          neverc_syslog_pri(NEVERC_SYSLOG_LOCAL0, NEVERC_SYSLOG_INFO));
    CHECK(injected_records[1].connection_open);
    CHECK(strcmp(injected_records[1].ident, "beta") == 0);
    CHECK(strcmp(injected_records[1].message, "from beta") == 0);
    CHECK(injected_records[1].priority ==
          neverc_syslog_pri(NEVERC_SYSLOG_LOCAL1,
                            NEVERC_SYSLOG_WARNING));

    /* beta owns the process-global libc state after the previous write.
     * Closing non-owner alpha must not close beta's connection or leave its
     * next record without the beta ident. */
    neverc_syslog_close(alpha);
    CHECK(injected_close_calls == 0);
    CHECK(neverc_syslog_err(beta, "beta remains") == 0);
    CHECK(injected_record_count == 3);
    CHECK(injected_records[2].connection_open);
    CHECK(strcmp(injected_records[2].ident, "beta") == 0);
    CHECK(strcmp(injected_records[2].message, "beta remains") == 0);

    neverc_syslog_close(beta);
    CHECK(injected_close_calls == 1);
    CHECK(!injected_connection_open);
    CHECK(injected_ident == NULL);

    puts("passed");
    return 0;
}

#endif
