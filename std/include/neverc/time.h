#ifndef NEVERC_TIME_H
#define NEVERC_TIME_H

/*
 * NeverC time — time operations (mirrors Go time package).
 *
 * Duration is int64_t nanoseconds. Time is seconds + nanoseconds since epoch.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Duration constants (nanoseconds) */
#define NEVERC_TIME_NANOSECOND   ((int64_t)1)
#define NEVERC_TIME_MICROSECOND  ((int64_t)1000)
#define NEVERC_TIME_MILLISECOND  ((int64_t)1000000)
#define NEVERC_TIME_SECOND       ((int64_t)1000000000)
#define NEVERC_TIME_MINUTE       ((int64_t)60000000000LL)
#define NEVERC_TIME_HOUR         ((int64_t)3600000000000LL)

typedef struct {
    int64_t sec;
    int32_t nsec;
} neverc_time_t;

typedef int64_t neverc_duration_t;

/* Current time */
neverc_time_t   neverc_time_now(void);
neverc_time_t   neverc_time_unix(int64_t sec, int64_t nsec);

/* Decomposition */
int  neverc_time_year(neverc_time_t t);
int  neverc_time_month(neverc_time_t t);
int  neverc_time_day(neverc_time_t t);
int  neverc_time_hour(neverc_time_t t);
int  neverc_time_minute(neverc_time_t t);
int  neverc_time_second(neverc_time_t t);
int  neverc_time_nanosecond(neverc_time_t t);
int  neverc_time_weekday(neverc_time_t t);
int  neverc_time_yearday(neverc_time_t t);

/* Arithmetic */
neverc_time_t     neverc_time_add(neverc_time_t t, neverc_duration_t d);
neverc_duration_t neverc_time_sub(neverc_time_t a, neverc_time_t b);
neverc_duration_t neverc_time_since(neverc_time_t t);
neverc_duration_t neverc_time_until(neverc_time_t t);

/* Comparison */
int  neverc_time_before(neverc_time_t a, neverc_time_t b);
int  neverc_time_after(neverc_time_t a, neverc_time_t b);
int  neverc_time_equal(neverc_time_t a, neverc_time_t b);
int  neverc_time_is_zero(neverc_time_t t);

/* Epoch conversions */
int64_t neverc_time_unix_sec(neverc_time_t t);
int64_t neverc_time_unix_milli(neverc_time_t t);
int64_t neverc_time_unix_nano(neverc_time_t t);

/* Duration helpers */
double  neverc_time_duration_seconds(neverc_duration_t d);
int64_t neverc_time_duration_milliseconds(neverc_duration_t d);
int64_t neverc_time_duration_microseconds(neverc_duration_t d);
int64_t neverc_time_duration_nanoseconds(neverc_duration_t d);

/* Format (returns malloc'd string, caller frees) */
char *neverc_time_format_rfc3339(neverc_time_t t);
char *neverc_time_format_unix_date(neverc_time_t t);

/* Parse RFC3339 */
int neverc_time_parse_rfc3339(const char *s, neverc_time_t *out);

/* Sleep (blocks current thread) */
void neverc_time_sleep(neverc_duration_t d);

/* Date constructor */
neverc_time_t neverc_time_date(int year, int month, int day,
                                int hour, int min, int sec, int nsec);

/* Unix from microseconds */
int64_t neverc_time_unix_micro(neverc_time_t t);
neverc_time_t neverc_time_unix_micro_to_time(int64_t usec);

/* Parse duration string (e.g. "1h30m", "500ms", "2s") */
int neverc_time_parse_duration(const char *s, neverc_duration_t *out);

/* Format duration to string (returns malloc'd string) */
char *neverc_time_format_duration(neverc_duration_t d);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_time_t { char __tag; };
extern struct __neverc_std_time_t time_mod;
#endif

#endif /* NEVERC_TIME_H */
