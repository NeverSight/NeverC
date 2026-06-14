#include "neverc/std/time.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#if defined(NEVERC_PLATFORM_WINDOWS)
static struct tm *gmtime_r(const time_t *timer, struct tm *result) {
    gmtime_s(result, timer);
    return result;
}
#endif

neverc_time_t neverc_time_now(void) {
    neverc_time_t t;
#if defined(NEVERC_PLATFORM_WINDOWS)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t u = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    u -= 116444736000000000ULL;
    t.sec = (int64_t)(u / 10000000ULL);
    t.nsec = (int32_t)((u % 10000000ULL) * 100);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    t.sec = ts.tv_sec;
    t.nsec = (int32_t)ts.tv_nsec;
#endif
    return t;
}

neverc_time_t neverc_time_unix(int64_t sec, int64_t nsec) {
    neverc_time_t t;
    t.sec = sec + nsec / 1000000000LL;
    t.nsec = (int32_t)(nsec % 1000000000LL);
    if (t.nsec < 0) { t.sec--; t.nsec += 1000000000; }
    return t;
}

static void decompose(neverc_time_t t, struct tm *tm) {
    time_t s = (time_t)t.sec;
    gmtime_r(&s, tm);
}

int neverc_time_year(neverc_time_t t)       { struct tm m; decompose(t, &m); return m.tm_year + 1900; }
int neverc_time_month(neverc_time_t t)      { struct tm m; decompose(t, &m); return m.tm_mon + 1; }
int neverc_time_day(neverc_time_t t)        { struct tm m; decompose(t, &m); return m.tm_mday; }
int neverc_time_hour(neverc_time_t t)       { struct tm m; decompose(t, &m); return m.tm_hour; }
int neverc_time_minute(neverc_time_t t)     { struct tm m; decompose(t, &m); return m.tm_min; }
int neverc_time_second(neverc_time_t t)     { struct tm m; decompose(t, &m); return m.tm_sec; }
int neverc_time_nanosecond(neverc_time_t t) { return t.nsec; }
int neverc_time_weekday(neverc_time_t t)    { struct tm m; decompose(t, &m); return m.tm_wday; }
int neverc_time_yearday(neverc_time_t t)    { struct tm m; decompose(t, &m); return m.tm_yday + 1; }

neverc_time_t neverc_time_add(neverc_time_t t, neverc_duration_t d) {
    int64_t total_nsec = (int64_t)t.nsec + d;
    t.sec += total_nsec / 1000000000LL;
    t.nsec = (int32_t)(total_nsec % 1000000000LL);
    if (t.nsec < 0) { t.sec--; t.nsec += 1000000000; }
    return t;
}

neverc_duration_t neverc_time_sub(neverc_time_t a, neverc_time_t b) {
    return (a.sec - b.sec) * 1000000000LL + (a.nsec - b.nsec);
}

neverc_duration_t neverc_time_since(neverc_time_t t) {
    return neverc_time_sub(neverc_time_now(), t);
}

neverc_duration_t neverc_time_until(neverc_time_t t) {
    return neverc_time_sub(t, neverc_time_now());
}

int neverc_time_before(neverc_time_t a, neverc_time_t b) {
    return a.sec < b.sec || (a.sec == b.sec && a.nsec < b.nsec);
}

int neverc_time_after(neverc_time_t a, neverc_time_t b) {
    return a.sec > b.sec || (a.sec == b.sec && a.nsec > b.nsec);
}

int neverc_time_equal(neverc_time_t a, neverc_time_t b) {
    return a.sec == b.sec && a.nsec == b.nsec;
}

int neverc_time_is_zero(neverc_time_t t) {
    return t.sec == 0 && t.nsec == 0;
}

int64_t neverc_time_unix_sec(neverc_time_t t)   { return t.sec; }
int64_t neverc_time_unix_milli(neverc_time_t t)  { return t.sec * 1000 + t.nsec / 1000000; }
int64_t neverc_time_unix_nano(neverc_time_t t)   { return t.sec * 1000000000LL + t.nsec; }

double  neverc_time_duration_seconds(neverc_duration_t d)       { return (double)d / 1e9; }
int64_t neverc_time_duration_milliseconds(neverc_duration_t d)  { return d / 1000000; }
int64_t neverc_time_duration_microseconds(neverc_duration_t d)  { return d / 1000; }
int64_t neverc_time_duration_nanoseconds(neverc_duration_t d)   { return d; }

static void write_int(char *buf, int *pos, int val, int width) {
    char tmp[16];
    int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    else {
        int v = val;
        while (v > 0) { tmp[len++] = '0' + v % 10; v /= 10; }
    }
    for (int i = len; i < width; i++) buf[(*pos)++] = '0';
    for (int i = len - 1; i >= 0; i--) buf[(*pos)++] = tmp[i];
}

char *neverc_time_format_rfc3339(neverc_time_t t) {
    struct tm m;
    decompose(t, &m);
    char *buf = (char *)malloc(32);
    int p = 0;
    write_int(buf, &p, m.tm_year + 1900, 4); buf[p++] = '-';
    write_int(buf, &p, m.tm_mon + 1, 2);     buf[p++] = '-';
    write_int(buf, &p, m.tm_mday, 2);        buf[p++] = 'T';
    write_int(buf, &p, m.tm_hour, 2);        buf[p++] = ':';
    write_int(buf, &p, m.tm_min, 2);         buf[p++] = ':';
    write_int(buf, &p, m.tm_sec, 2);         buf[p++] = 'Z';
    buf[p] = '\0';
    return buf;
}

char *neverc_time_format_unix_date(neverc_time_t t) {
    static const char *weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
    struct tm m;
    decompose(t, &m);
    char *buf = (char *)malloc(64);
    int p = 0;
    const char *wd = weekdays[m.tm_wday];
    while (*wd) buf[p++] = *wd++;
    buf[p++] = ' ';
    const char *mo = months[m.tm_mon];
    while (*mo) buf[p++] = *mo++;
    buf[p++] = ' ';
    write_int(buf, &p, m.tm_mday, 1); buf[p++] = ' ';
    write_int(buf, &p, m.tm_hour, 2); buf[p++] = ':';
    write_int(buf, &p, m.tm_min, 2);  buf[p++] = ':';
    write_int(buf, &p, m.tm_sec, 2);  buf[p++] = ' ';
    buf[p++] = 'U'; buf[p++] = 'T'; buf[p++] = 'C';
    buf[p++] = ' ';
    write_int(buf, &p, m.tm_year + 1900, 4);
    buf[p] = '\0';
    return buf;
}

static int parse_digits(const char **s, int count) {
    int val = 0;
    for (int i = 0; i < count; i++) {
        if (**s < '0' || **s > '9') return -1;
        val = val * 10 + (**s - '0');
        (*s)++;
    }
    return val;
}

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m) {
    static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && is_leap(y)) return 29;
    return dm[m - 1];
}

/*
 * Days since 1970-01-01 for a proleptic-Gregorian date, in O(1) (Howard
 * Hinnant's days_from_civil, as used by C++20 <chrono>). Replaces the previous
 * O(year) loops that summed one term per year and per month — those grew
 * without bound for far-off years. Precondition: m in [1,12], d a valid day.
 */
static int64_t days_from_civil(int64_t y, int m, int d) {
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                                    /* [0, 399]    */
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   /* [0, 365]    */
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

int neverc_time_parse_rfc3339(const char *s, neverc_time_t *out) {
    const char *p = s;
    int year = parse_digits(&p, 4); if (year < 0 || *p++ != '-') return -1;
    int month = parse_digits(&p, 2); if (month < 1 || month > 12 || *p++ != '-') return -1;
    int day = parse_digits(&p, 2);
    if (day < 1 || day > days_in_month(year, month)) return -1;
    if (*p != 'T' && *p != 't' && *p != ' ') return -1;
    p++;
    int hour = parse_digits(&p, 2); if (hour < 0 || hour > 23 || *p++ != ':') return -1;
    int min = parse_digits(&p, 2);  if (min < 0 || min > 59 || *p++ != ':') return -1;
    int sec = parse_digits(&p, 2);  if (sec < 0 || sec > 60) return -1;

    int32_t nsec = 0;
    if (*p == '.') {
        p++;
        int64_t frac = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 9) {
            frac = frac * 10 + (*p - '0');
            digits++;
            p++;
        }
        while (digits < 9) { frac *= 10; digits++; }
        nsec = (int32_t)frac;
        while (*p >= '0' && *p <= '9') p++;
    }

    int64_t tz_offset = 0;
    if (*p == 'Z' || *p == 'z') { p++; }
    else if (*p == '+' || *p == '-') {
        int sign = (*p == '-') ? -1 : 1;
        p++;
        int tzh = parse_digits(&p, 2); if (tzh < 0 || *p++ != ':') return -1;
        int tzm = parse_digits(&p, 2); if (tzm < 0) return -1;
        tz_offset = sign * (tzh * 3600 + tzm * 60);
    }

    /* Convert to Unix timestamp */
    int64_t days = days_from_civil(year, month, day);

    int64_t total_sec = days * 86400 + hour * 3600 + min * 60 + sec - tz_offset;
    out->sec = total_sec;
    out->nsec = nsec;
    return 0;
}

void neverc_time_sleep(neverc_duration_t d) {
    if (d <= 0) return;
#if defined(NEVERC_PLATFORM_WINDOWS)
    Sleep((DWORD)(d / 1000000LL));
#else
    struct timespec ts;
    ts.tv_sec = d / 1000000000LL;
    ts.tv_nsec = d % 1000000000LL;
    nanosleep(&ts, NULL);
#endif
}

neverc_time_t neverc_time_date(int year, int month, int day,
                                int hour, int min, int sec, int nsec) {
    int64_t days = days_from_civil(year, month, day);

    neverc_time_t t;
    t.sec = days * 86400 + hour * 3600 + min * 60 + sec;
    t.nsec = nsec;
    return t;
}

int64_t neverc_time_unix_micro(neverc_time_t t) {
    return t.sec * 1000000LL + t.nsec / 1000;
}

neverc_time_t neverc_time_unix_micro_to_time(int64_t usec) {
    neverc_time_t t;
    t.sec = usec / 1000000LL;
    t.nsec = (int32_t)((usec % 1000000LL) * 1000);
    if (t.nsec < 0) { t.sec--; t.nsec += 1000000000; }
    return t;
}

static int parse_dur_num(const char **p, double *val) {
    if (**p < '0' || **p > '9') return 0;
    *val = 0;
    while (**p >= '0' && **p <= '9') {
        *val = *val * 10 + (**p - '0');
        (*p)++;
    }
    if (**p == '.') {
        (*p)++;
        double frac = 0.1;
        while (**p >= '0' && **p <= '9') {
            *val += (**p - '0') * frac;
            frac *= 0.1;
            (*p)++;
        }
    }
    return 1;
}

int neverc_time_parse_duration(const char *s, neverc_duration_t *out) {
    if (!s || !out) return -1;
    const char *p = s;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    if (*p == '\0') return -1;

    int64_t total = 0;
    while (*p) {
        double val;
        if (!parse_dur_num(&p, &val)) return -1;

        int64_t unit = 0;
        if (*p == 'n' && *(p+1) == 's') { unit = 1; p += 2; }
        else if (*p == 'u' && *(p+1) == 's') { unit = 1000; p += 2; }
        else if (*p == '\xc2' && *(p+1) == '\xb5' && *(p+2) == 's') { unit = 1000; p += 3; }
        else if (*p == 'm' && *(p+1) == 's') { unit = 1000000; p += 2; }
        else if (*p == 's') { unit = 1000000000LL; p += 1; }
        else if (*p == 'm') { unit = 60000000000LL; p += 1; }
        else if (*p == 'h') { unit = 3600000000000LL; p += 1; }
        else return -1;

        total += (int64_t)(val * (double)unit);
    }

    *out = neg ? -total : total;
    return 0;
}

char *neverc_time_format_duration(neverc_duration_t d) {
    char buf[128];
    int pos = 0;
    if (d < 0) { buf[pos++] = '-'; d = -d; }

    if (d == 0) {
        buf[pos++] = '0'; buf[pos++] = 's'; buf[pos] = '\0';
    } else if (d < NEVERC_TIME_MICROSECOND) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%lldns", (long long)d);
    } else if (d < NEVERC_TIME_MILLISECOND) {
        double us = (double)d / 1000.0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%.3gus", us);
    } else if (d < NEVERC_TIME_SECOND) {
        double ms = (double)d / 1000000.0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%.3gms", ms);
    } else {
        int64_t h = d / NEVERC_TIME_HOUR;
        d %= NEVERC_TIME_HOUR;
        int64_t m = d / NEVERC_TIME_MINUTE;
        d %= NEVERC_TIME_MINUTE;
        double s = (double)d / 1e9;

        if (h > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "%lldh", (long long)h);
        if (m > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "%lldm", (long long)m);
        if (s > 0 || (h == 0 && m == 0))
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%.3gs", s);
    }

    char *result = (char *)malloc(pos + 1);
    for (int i = 0; i <= pos; i++) result[i] = buf[i];
    return result;
}

neverc_time_t neverc_time_unix_milli_to_time(int64_t msec) {
    neverc_time_t t;
    t.sec = msec / 1000;
    t.nsec = (int32_t)((msec % 1000) * 1000000);
    if (t.nsec < 0) { t.sec--; t.nsec += 1000000000; }
    return t;
}

char *neverc_time_format(neverc_time_t t, const char *layout) {
    if (!layout) return NULL;
    /* Decompose once: the previous code called six accessors, each doing its
     * own gmtime_r, so a single format cost six broken-down-time conversions. */
    struct tm m;
    decompose(t, &m);
    int yr = m.tm_year + 1900;
    int mo = m.tm_mon + 1;
    int dy = m.tm_mday;
    int hr = m.tm_hour;
    int mi = m.tm_min;
    int sc = m.tm_sec;

    char buf[256];
    int out = 0;
    size_t llen = strlen(layout);

    for (size_t i = 0; i < llen && out < (int)sizeof(buf) - 20;) {
        if (i + 4 <= llen && memcmp(layout + i, "2006", 4) == 0) {
            write_int(buf, &out, yr, 4); i += 4;
        } else if (i + 2 <= llen && memcmp(layout + i, "01", 2) == 0) {
            write_int(buf, &out, mo, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "02", 2) == 0) {
            write_int(buf, &out, dy, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "15", 2) == 0) {
            write_int(buf, &out, hr, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "04", 2) == 0) {
            write_int(buf, &out, mi, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "05", 2) == 0) {
            write_int(buf, &out, sc, 2); i += 2;
        } else {
            buf[out++] = layout[i++];
        }
    }
    buf[out] = '\0';
    char *result = (char *)malloc((size_t)out + 1);
    if (result) memcpy(result, buf, (size_t)out + 1);
    return result;
}

static int parse_n_digits(const char *value, size_t vlen, size_t *vi, int n) {
    int result = 0;
    for (int j = 0; j < n && *vi < vlen && value[*vi] >= '0' && value[*vi] <= '9'; j++) {
        result = result * 10 + (value[*vi] - '0');
        (*vi)++;
    }
    return result;
}

int neverc_time_parse(const char *layout, const char *value, neverc_time_t *out) {
    if (!layout || !value || !out) return -1;
    int yr = 0, mo = 1, dy = 1, hr = 0, mi = 0, sc = 0;
    size_t li = 0, vi = 0;
    size_t llen = strlen(layout), vlen = strlen(value);

    while (li < llen && vi < vlen) {
        if (li + 4 <= llen && memcmp(layout + li, "2006", 4) == 0) {
            yr = parse_n_digits(value, vlen, &vi, 4);
            li += 4;
        } else if (li + 2 <= llen && memcmp(layout + li, "01", 2) == 0) {
            mo = parse_n_digits(value, vlen, &vi, 2);
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "02", 2) == 0) {
            dy = parse_n_digits(value, vlen, &vi, 2);
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "15", 2) == 0) {
            hr = parse_n_digits(value, vlen, &vi, 2);
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "04", 2) == 0) {
            mi = parse_n_digits(value, vlen, &vi, 2);
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "05", 2) == 0) {
            sc = parse_n_digits(value, vlen, &vi, 2);
            li += 2;
        } else {
            li++; vi++;
        }
    }
    *out = neverc_time_date(yr, mo, dy, hr, mi, sc, 0);
    return 0;
}

neverc_time_t neverc_time_truncate(neverc_time_t t, neverc_duration_t d) {
    if (d <= 0) return t;
    int64_t ns = t.sec * 1000000000LL + t.nsec;
    int64_t rem = ns % d;
    if (rem < 0) rem += d;
    ns -= rem;
    neverc_time_t result;
    result.sec = ns / 1000000000LL;
    result.nsec = (int32_t)(ns % 1000000000LL);
    return result;
}

neverc_time_t neverc_time_round(neverc_time_t t, neverc_duration_t d) {
    if (d <= 0) return t;
    int64_t ns = t.sec * 1000000000LL + t.nsec;
    int64_t rem = ns % d;
    if (rem < 0) rem += d;
    if (rem * 2 >= d) ns += d - rem;
    else ns -= rem;
    neverc_time_t result;
    result.sec = ns / 1000000000LL;
    result.nsec = (int32_t)(ns % 1000000000LL);
    return result;
}
