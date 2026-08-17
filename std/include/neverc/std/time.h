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
/* Add wraps the seconds field on representational overflow. Sub saturates to
   INT64_MIN/INT64_MAX when the duration is out of range. */
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
/* Narrow epoch conversions use defined two's-complement wrapping when the
   mathematical result is outside int64_t. */
int64_t neverc_time_unix_sec(neverc_time_t t);
int64_t neverc_time_unix_milli(neverc_time_t t);
int64_t neverc_time_unix_nano(neverc_time_t t);

/* Duration helpers */
double  neverc_time_duration_seconds(neverc_duration_t d);
int64_t neverc_time_duration_milliseconds(neverc_duration_t d);
int64_t neverc_time_duration_microseconds(neverc_duration_t d);
int64_t neverc_time_duration_nanoseconds(neverc_duration_t d);
/* Multiply a duration by a signed integer. Returns 0 and writes *out, or
 * -1 on overflow and leaves *out unchanged. */
int neverc_time_duration_mul(neverc_duration_t d, int64_t n,
                             neverc_duration_t *out);

/* Format (returns malloc'd string, caller frees) */
char *neverc_time_format_rfc3339(neverc_time_t t);
char *neverc_time_format_unix_date(neverc_time_t t);

/* Parse RFC3339 */
int neverc_time_parse_rfc3339(const char *s, neverc_time_t *out);

/* Sleep (blocks current thread) */
void neverc_time_sleep(neverc_duration_t d);

/* Date constructor (UTC). */
neverc_time_t neverc_time_date(int year, int month, int day,
                                int hour, int min, int sec, int nsec);

/*
 * Location for ParseInLocation / DateInLocation (Go time.Location subset).
 * offset_at(unix, ctx) returns the UTC offset in seconds at that instant.
 * If offset_at is NULL, std_off is used as a fixed zone.
 */
typedef struct neverc_time_location {
    int         std_off;
    int         dst_off;
    const char *std_abbr;
    const char *dst_abbr;
    int       (*offset_at)(int64_t unix_sec, void *ctx);
    void       *ctx;
} neverc_time_location_t;

/* Date in a location. DST gap/overlap uses Go time.Date's lookup. */
neverc_time_t neverc_time_date_in_location(int year, int month, int day,
                                           int hour, int min, int sec, int nsec,
                                           const neverc_time_location_t *loc);

/* Unix from microseconds */
int64_t neverc_time_unix_micro(neverc_time_t t);
neverc_time_t neverc_time_unix_micro_to_time(int64_t usec);

/* Parse duration string (e.g. "1h30m", "500ms", "2s") */
int neverc_time_parse_duration(const char *s, neverc_duration_t *out);

/* Format duration to string (returns malloc'd string) */
char *neverc_time_format_duration(neverc_duration_t d);

/* Unix from milliseconds */
neverc_time_t neverc_time_unix_milli_to_time(int64_t msec);

/* Format: custom layout (Go-style: "2006-01-02 15:04:05").
   Returns malloc'd string, caller frees. */
char *neverc_time_format(neverc_time_t t, const char *layout);

/* Parse: parse time string with layout (UTC if no zone). Returns 0 on success. */
int neverc_time_parse(const char *layout, const char *value, neverc_time_t *out);

/* ParseInLocation: missing zone uses loc; named zones match loc abbrevs. */
int neverc_time_parse_in_location(const char *layout, const char *value,
                                  const neverc_time_location_t *loc,
                                  neverc_time_t *out);

/* Truncate / Round to duration boundary */
neverc_time_t neverc_time_truncate(neverc_time_t t, neverc_duration_t d);
neverc_time_t neverc_time_round(neverc_time_t t, neverc_duration_t d);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_time_t { char __tag; };
extern struct __neverc_std_time_t __neverc_mod_time_mod;
extern struct __neverc_std_time_t time_mod;
#endif

#endif /* NEVERC_TIME_H */
