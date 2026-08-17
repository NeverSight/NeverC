#include "neverc/std/time.h"
#include "neverc/std/_platform.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int64_t wrapped_int64(uint64_t bits) {
    if (bits <= (uint64_t)INT64_MAX) return (int64_t)bits;
    return INT64_MIN + (int64_t)(bits - (UINT64_C(1) << 63));
}

static int64_t wrapping_add(int64_t a, int64_t b) {
    return wrapped_int64((uint64_t)a + (uint64_t)b);
}

static int64_t wrapping_scale_add(int64_t value, uint64_t scale,
                                  uint64_t extra) {
    return wrapped_int64((uint64_t)value * scale + extra);
}

static neverc_time_t normalize_time(neverc_time_t t) {
    int64_t whole = (int64_t)t.nsec / NEVERC_TIME_SECOND;
    int64_t remainder = (int64_t)t.nsec % NEVERC_TIME_SECOND;
    t.sec = wrapping_add(t.sec, whole);
    if (remainder < 0) {
        t.sec = wrapping_add(t.sec, -1);
        remainder += NEVERC_TIME_SECOND;
    }
    t.nsec = (int32_t)remainder;
    return t;
}

neverc_time_t neverc_time_now(void) {
    neverc_time_t t = {0, 0};
#if defined(NEVERC_PLATFORM_WINDOWS)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t u = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    const uint64_t epoch = 116444736000000000ULL;
    if (u >= epoch) {
        u -= epoch;
        t.sec = (int64_t)(u / 10000000ULL);
        t.nsec = (int32_t)((u % 10000000ULL) * 100ULL);
    } else {
        uint64_t delta = epoch - u;
        uint64_t seconds = delta / 10000000ULL;
        uint64_t remainder = delta % 10000000ULL;
        t.sec = -(int64_t)seconds;
        if (remainder != 0) {
            t.sec--;
            t.nsec = (int32_t)((10000000ULL - remainder) * 100ULL);
        }
    }
#else
    struct timespec ts = {0, 0};
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0 ||
        timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        t.sec = (int64_t)ts.tv_sec;
        t.nsec = (int32_t)ts.tv_nsec;
    }
#endif
    return t;
}

neverc_time_t neverc_time_unix(int64_t sec, int64_t nsec) {
    neverc_time_t t;
    int64_t whole = nsec / NEVERC_TIME_SECOND;
    int64_t remainder = nsec % NEVERC_TIME_SECOND;
    t.sec = wrapping_add(sec, whole);
    if (remainder < 0) {
        t.sec = wrapping_add(t.sec, -1);
        remainder += NEVERC_TIME_SECOND;
    }
    t.nsec = (int32_t)remainder;
    return t;
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

/* Inverse of days_from_civil. z is days since 1970-01-01. */
static void civil_from_days(int64_t z, int64_t *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                 /* [0, 146096] */
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
    int64_t year = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);          /* [0, 365]    */
    int64_t mp = (5 * doy + 2) / 153;                               /* [0, 11]     */
    int day = (int)(doy - (153 * mp + 2) / 5 + 1);                  /* [1, 31]     */
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);                   /* [1, 12]     */
    *y = year + (month <= 2);
    *m = month;
    *d = day;
}

/*
 * Portable UTC breakdown. libc gmtime_s/gmtime_r reject pre-1970 timestamps
 * on Windows, which made parse-layout cases such as "2:30PM" (year 0) and
 * "jun  5 69" (1969) return zeros from neverc_time_year/hour/….
 */
static int decompose(neverc_time_t t, struct tm *tm) {
    if (!tm) return 0;
    memset(tm, 0, sizeof(*tm));
    t = normalize_time(t);
    int64_t days = t.sec / 86400;
    int64_t sod = t.sec % 86400;
    if (sod < 0) {
        days--;
        sod += 86400;
    }
    int64_t year;
    int month, day;
    civil_from_days(days, &year, &month, &day);
    if (year < (int64_t)INT_MIN + 1900 || year > (int64_t)INT_MAX + 1900)
        return 0;
    tm->tm_year = (int)(year - 1900);
    tm->tm_mon = month - 1;
    tm->tm_mday = day;
    tm->tm_hour = (int)(sod / 3600);
    tm->tm_min = (int)((sod % 3600) / 60);
    tm->tm_sec = (int)(sod % 60);
    int64_t wday = (days + 4) % 7; /* 1970-01-01 was Thursday */
    if (wday < 0) wday += 7;
    tm->tm_wday = (int)wday;
    tm->tm_yday = (int)(days - days_from_civil(year, 1, 1));
    tm->tm_isdst = 0;
    return 1;
}

int neverc_time_year(neverc_time_t t)       { struct tm m; return decompose(t, &m) ? m.tm_year + 1900 : 0; }
int neverc_time_month(neverc_time_t t)      { struct tm m; return decompose(t, &m) ? m.tm_mon + 1 : 0; }
int neverc_time_day(neverc_time_t t)        { struct tm m; return decompose(t, &m) ? m.tm_mday : 0; }
int neverc_time_hour(neverc_time_t t)       { struct tm m; return decompose(t, &m) ? m.tm_hour : 0; }
int neverc_time_minute(neverc_time_t t)     { struct tm m; return decompose(t, &m) ? m.tm_min : 0; }
int neverc_time_second(neverc_time_t t)     { struct tm m; return decompose(t, &m) ? m.tm_sec : 0; }
int neverc_time_nanosecond(neverc_time_t t) { return normalize_time(t).nsec; }
int neverc_time_weekday(neverc_time_t t)    { struct tm m; return decompose(t, &m) ? m.tm_wday : 0; }
int neverc_time_yearday(neverc_time_t t)    { struct tm m; return decompose(t, &m) ? m.tm_yday + 1 : 0; }

neverc_time_t neverc_time_add(neverc_time_t t, neverc_duration_t d) {
    t = normalize_time(t);
    int64_t seconds = d / NEVERC_TIME_SECOND;
    int64_t nanos = d % NEVERC_TIME_SECOND;
    int64_t combined = (int64_t)t.nsec + nanos;
    if (combined >= NEVERC_TIME_SECOND) {
        seconds++;
        combined -= NEVERC_TIME_SECOND;
    } else if (combined < 0) {
        seconds--;
        combined += NEVERC_TIME_SECOND;
    }
    t.sec = wrapping_add(t.sec, seconds);
    t.nsec = (int32_t)combined;
    return t;
}

neverc_duration_t neverc_time_sub(neverc_time_t a, neverc_time_t b) {
    a = normalize_time(a);
    b = normalize_time(b);
    int negative = neverc_time_before(a, b);
    neverc_time_t later = negative ? b : a;
    neverc_time_t earlier = negative ? a : b;
    uint64_t seconds = (uint64_t)later.sec - (uint64_t)earlier.sec;
    int64_t nanos = (int64_t)later.nsec - earlier.nsec;
    if (nanos < 0) {
        seconds--;
        nanos += NEVERC_TIME_SECOND;
    }

    uint64_t limit = negative ? (UINT64_C(1) << 63) : (uint64_t)INT64_MAX;
    uint64_t magnitude;
    if (seconds > limit / (uint64_t)NEVERC_TIME_SECOND ||
        (seconds == limit / (uint64_t)NEVERC_TIME_SECOND &&
         (uint64_t)nanos > limit % (uint64_t)NEVERC_TIME_SECOND)) {
        magnitude = limit;
    } else {
        magnitude = seconds * (uint64_t)NEVERC_TIME_SECOND + (uint64_t)nanos;
    }
    if (!negative) return (int64_t)magnitude;
    if (magnitude == (UINT64_C(1) << 63)) return INT64_MIN;
    return -(int64_t)magnitude;
}

neverc_duration_t neverc_time_since(neverc_time_t t) {
    return neverc_time_sub(neverc_time_now(), t);
}

neverc_duration_t neverc_time_until(neverc_time_t t) {
    return neverc_time_sub(t, neverc_time_now());
}

int neverc_time_before(neverc_time_t a, neverc_time_t b) {
    a = normalize_time(a);
    b = normalize_time(b);
    return a.sec < b.sec || (a.sec == b.sec && a.nsec < b.nsec);
}

int neverc_time_after(neverc_time_t a, neverc_time_t b) {
    a = normalize_time(a);
    b = normalize_time(b);
    return a.sec > b.sec || (a.sec == b.sec && a.nsec > b.nsec);
}

int neverc_time_equal(neverc_time_t a, neverc_time_t b) {
    a = normalize_time(a);
    b = normalize_time(b);
    return a.sec == b.sec && a.nsec == b.nsec;
}

int neverc_time_is_zero(neverc_time_t t) {
    t = normalize_time(t);
    return t.sec == 0 && t.nsec == 0;
}

int64_t neverc_time_unix_sec(neverc_time_t t) {
    return normalize_time(t).sec;
}

int64_t neverc_time_unix_milli(neverc_time_t t) {
    t = normalize_time(t);
    return wrapping_scale_add(t.sec, 1000U, (uint32_t)t.nsec / 1000000U);
}

int64_t neverc_time_unix_nano(neverc_time_t t) {
    t = normalize_time(t);
    return wrapping_scale_add(t.sec, 1000000000ULL, (uint32_t)t.nsec);
}

double  neverc_time_duration_seconds(neverc_duration_t d)       { return (double)d / 1e9; }
int64_t neverc_time_duration_milliseconds(neverc_duration_t d)  { return d / 1000000; }
int64_t neverc_time_duration_microseconds(neverc_duration_t d)  { return d / 1000; }
int64_t neverc_time_duration_nanoseconds(neverc_duration_t d)   { return d; }

static void write_int(char *buf, size_t *pos, int val, int width) {
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
    t = normalize_time(t);
    struct tm m;
    if (!decompose(t, &m)) return NULL;
    int year = m.tm_year + 1900;
    if (year < 0 || year > 9999) return NULL;
    char *buf = (char *)malloc(32);
    if (!buf) return NULL;
    size_t p = 0;
    write_int(buf, &p, year, 4);             buf[p++] = '-';
    write_int(buf, &p, m.tm_mon + 1, 2);     buf[p++] = '-';
    write_int(buf, &p, m.tm_mday, 2);        buf[p++] = 'T';
    write_int(buf, &p, m.tm_hour, 2);        buf[p++] = ':';
    write_int(buf, &p, m.tm_min, 2);         buf[p++] = ':';
    write_int(buf, &p, m.tm_sec, 2);
    if (t.nsec != 0) {
        char fraction[9];
        int32_t value = t.nsec;
        for (int i = 8; i >= 0; i--) {
            fraction[i] = (char)('0' + value % 10);
            value /= 10;
        }
        int digits = 9;
        while (digits > 0 && fraction[digits - 1] == '0') digits--;
        buf[p++] = '.';
        memcpy(buf + p, fraction, (size_t)digits);
        p += digits;
    }
    buf[p++] = 'Z';
    buf[p] = '\0';
    return buf;
}

char *neverc_time_format_unix_date(neverc_time_t t) {
    static const char *weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
    struct tm m;
    if (!decompose(t, &m) || m.tm_wday < 0 || m.tm_wday > 6 ||
        m.tm_mon < 0 || m.tm_mon > 11)
        return NULL;
    int year = m.tm_year + 1900;
    if (year < 0 || year > 9999) return NULL;
    char *buf = (char *)malloc(64);
    if (!buf) return NULL;
    size_t p = 0;
    const char *wd = weekdays[m.tm_wday];
    while (*wd) buf[p++] = *wd++;
    buf[p++] = ' ';
    const char *mo = months[m.tm_mon];
    while (*mo) buf[p++] = *mo++;
    buf[p++] = ' ';
    if (m.tm_mday < 10) buf[p++] = ' ';
    write_int(buf, &p, m.tm_mday, 1); buf[p++] = ' ';
    write_int(buf, &p, m.tm_hour, 2); buf[p++] = ':';
    write_int(buf, &p, m.tm_min, 2);  buf[p++] = ':';
    write_int(buf, &p, m.tm_sec, 2);  buf[p++] = ' ';
    buf[p++] = 'U'; buf[p++] = 'T'; buf[p++] = 'C';
    buf[p++] = ' ';
    write_int(buf, &p, year, 4);
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

int neverc_time_parse_rfc3339(const char *s, neverc_time_t *out) {
    if (!s || !out) return -1;
    const char *p = s;
    int year = parse_digits(&p, 4); if (year < 0 || *p++ != '-') return -1;
    int month = parse_digits(&p, 2); if (month < 1 || month > 12 || *p++ != '-') return -1;
    int day = parse_digits(&p, 2);
    if (day < 1 || day > days_in_month(year, month)) return -1;
    if (*p++ != 'T') return -1;
    int hour = parse_digits(&p, 2); if (hour < 0 || hour > 23 || *p++ != ':') return -1;
    int min = parse_digits(&p, 2);  if (min < 0 || min > 59 || *p++ != ':') return -1;
    int sec = parse_digits(&p, 2);  if (sec < 0 || sec > 60) return -1;
    /* RFC 3339 / Go: leap second 60 is accepted and clamped to 59. */
    if (sec == 60) sec = 59;

    int32_t nsec = 0;
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return -1;
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
    if (*p == 'Z') { p++; }
    else if (*p == '+' || *p == '-') {
        int sign = (*p == '-') ? -1 : 1;
        p++;
        int tzh = parse_digits(&p, 2);
        if (tzh < 0 || tzh > 14) return -1;
        int tzm = 0;
        if (*p == ':') {
            p++;
            tzm = parse_digits(&p, 2);
            if (tzm < 0 || tzm > 59) return -1;
        } else if (*p >= '0' && *p <= '9') {
            tzm = parse_digits(&p, 2);
            if (tzm < 0 || tzm > 59) return -1;
        }
        if (tzh == 14 && tzm > 0) return -1;
        tz_offset = sign * (tzh * 3600 + tzm * 60);
    } else return -1;
    if (*p != '\0') return -1;

    /* Convert to Unix timestamp */
    int64_t days = days_from_civil(year, month, day);

    int64_t total_sec = days * 86400 + hour * 3600 + min * 60 + sec - tz_offset;
    neverc_time_t parsed = {total_sec, nsec};
    *out = parsed;
    return 0;
}

void neverc_time_sleep(neverc_duration_t d) {
    if (d <= 0) return;
#if defined(NEVERC_PLATFORM_WINDOWS)
    uint64_t millis = (uint64_t)(d / NEVERC_TIME_MILLISECOND);
    if (d % NEVERC_TIME_MILLISECOND != 0) millis++;
    while (millis > UINT32_MAX) {
        Sleep(UINT32_MAX);
        millis -= UINT32_MAX;
    }
    Sleep((DWORD)millis);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(d / NEVERC_TIME_SECOND);
    ts.tv_nsec = (long)(d % NEVERC_TIME_SECOND);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
#endif
}

neverc_time_t neverc_time_date(int year, int month, int day,
                                int hour, int min, int sec, int nsec) {
    int64_t month_index = (int64_t)month - 1;
    int64_t normalized_year = (int64_t)year + month_index / 12;
    int normalized_month = (int)(month_index % 12);
    if (normalized_month < 0) {
        normalized_month += 12;
        normalized_year--;
    }
    normalized_month++;

    int64_t days = days_from_civil(normalized_year, normalized_month, 1) +
                   (int64_t)day - 1;
    int64_t total_sec = days * 86400 + (int64_t)hour * 3600 +
                        (int64_t)min * 60 + sec;
    return neverc_time_unix(total_sec, nsec);
}

int64_t neverc_time_unix_micro(neverc_time_t t) {
    t = normalize_time(t);
    return wrapping_scale_add(t.sec, 1000000ULL, (uint32_t)t.nsec / 1000U);
}

neverc_time_t neverc_time_unix_micro_to_time(int64_t usec) {
    neverc_time_t t;
    t.sec = usec / 1000000LL;
    t.nsec = (int32_t)((usec % 1000000LL) * 1000);
    if (t.nsec < 0) { t.sec--; t.nsec += 1000000000; }
    return t;
}

static int consume_duration_integer(const char **p, uint64_t *value,
                                    int *consumed) {
    const uint64_t limit = UINT64_C(1) << 63;
    uint64_t result = 0;
    int digits = 0;
    while (**p >= '0' && **p <= '9') {
        unsigned digit = (unsigned)(**p - '0');
        if (result > limit / 10U ||
            (result == limit / 10U && digit > limit % 10U))
            return -1;
        result = result * 10U + digit;
        (*p)++;
        digits++;
    }
    *value = result;
    *consumed = digits != 0;
    return 0;
}

static void consume_duration_fraction(const char **p, uint64_t *value,
                                      double *scale, int *consumed) {
    const uint64_t limit = UINT64_C(1) << 63;
    uint64_t result = 0;
    double divisor = 1.0;
    int overflow = 0;
    int digits = 0;
    while (**p >= '0' && **p <= '9') {
        unsigned digit = (unsigned)(**p - '0');
        if (!overflow) {
            if (result > (limit - 1U) / 10U) {
                overflow = 1;
            } else {
                uint64_t next = result * 10U + digit;
                if (next > limit)
                    overflow = 1;
                else {
                    result = next;
                    divisor *= 10.0;
                }
            }
        }
        (*p)++;
        digits++;
    }
    *value = result;
    *scale = divisor;
    *consumed = digits != 0;
}

static int consume_duration_unit(const char **p, uint64_t *unit) {
    const unsigned char *u = (const unsigned char *)*p;
    if (u[0] == 'n' && u[1] == 's') {
        *unit = 1U; *p += 2;
    } else if (u[0] == 'u' && u[1] == 's') {
        *unit = 1000U; *p += 2;
    } else if (((u[0] == 0xc2U && u[1] == 0xb5U) ||
                (u[0] == 0xceU && u[1] == 0xbcU)) && u[2] == 's') {
        *unit = 1000U; *p += 3;
    } else if (u[0] == 'm' && u[1] == 's') {
        *unit = 1000000U; *p += 2;
    } else if (u[0] == 's') {
        *unit = 1000000000ULL; *p += 1;
    } else if (u[0] == 'm') {
        *unit = 60000000000ULL; *p += 1;
    } else if (u[0] == 'h') {
        *unit = 3600000000000ULL; *p += 1;
    } else {
        return -1;
    }
    return 0;
}

int neverc_time_parse_duration(const char *s, neverc_duration_t *out) {
    if (!s || !out) return -1;
    const char *p = s;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    if (strcmp(p, "0") == 0) {
        *out = 0;
        return 0;
    }
    if (*p == '\0') return -1;

    const uint64_t negative_limit = UINT64_C(1) << 63;
    uint64_t total = 0;
    while (*p) {
        uint64_t integer, fraction = 0;
        double scale = 1.0;
        int before_decimal, after_decimal = 0;
        if (consume_duration_integer(&p, &integer, &before_decimal) != 0)
            return -1;
        if (*p == '.') {
            p++;
            consume_duration_fraction(&p, &fraction, &scale, &after_decimal);
        }
        if (!before_decimal && !after_decimal) return -1;

        uint64_t unit;
        if (consume_duration_unit(&p, &unit) != 0) return -1;
        if (integer > negative_limit / unit) return -1;
        uint64_t segment = integer * unit;
        if (fraction != 0) {
            uint64_t fractional_ns =
                (uint64_t)((double)fraction * ((double)unit / scale));
            if (fractional_ns > negative_limit - segment) return -1;
            segment += fractional_ns;
        }
        if (segment > negative_limit - total) return -1;
        total += segment;
    }

    if (!neg && total > (uint64_t)INT64_MAX) return -1;
    if (neg && total == negative_limit)
        *out = INT64_MIN;
    else
        *out = neg ? -(int64_t)total : (int64_t)total;
    return 0;
}

static void append_uint64(char *buf, size_t *pos, uint64_t value) {
    char reversed[20];
    int digits = 0;
    do {
        reversed[digits++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    while (digits > 0) buf[(*pos)++] = reversed[--digits];
}

static void append_duration_decimal(char *buf, size_t *pos, uint64_t value,
                                    uint64_t unit, int fraction_digits,
                                    const char *suffix) {
    append_uint64(buf, pos, value / unit);
    uint64_t fraction = value % unit;
    if (fraction != 0) {
        char digits[9];
        for (int i = fraction_digits - 1; i >= 0; i--) {
            digits[i] = (char)('0' + fraction % 10U);
            fraction /= 10U;
        }
        while (fraction_digits > 0 && digits[fraction_digits - 1] == '0')
            fraction_digits--;
        buf[(*pos)++] = '.';
        memcpy(buf + *pos, digits, (size_t)fraction_digits);
        *pos += (size_t)fraction_digits;
    }
    while (*suffix) buf[(*pos)++] = *suffix++;
}

char *neverc_time_format_duration(neverc_duration_t d) {
    char buf[128];
    size_t pos = 0;
    int negative = d < 0;
    uint64_t magnitude = negative
        ? (uint64_t)(-(d + 1)) + 1U
        : (uint64_t)d;
    if (negative) buf[pos++] = '-';

    if (magnitude == 0) {
        buf[pos++] = '0'; buf[pos++] = 's';
    } else if (magnitude < (uint64_t)NEVERC_TIME_MICROSECOND) {
        append_uint64(buf, &pos, magnitude);
        buf[pos++] = 'n'; buf[pos++] = 's';
    } else if (magnitude < (uint64_t)NEVERC_TIME_MILLISECOND) {
        append_duration_decimal(buf, &pos, magnitude, 1000U, 3, "us");
    } else if (magnitude < (uint64_t)NEVERC_TIME_SECOND) {
        append_duration_decimal(buf, &pos, magnitude, 1000000U, 6, "ms");
    } else {
        uint64_t hours = magnitude / (uint64_t)NEVERC_TIME_HOUR;
        magnitude %= (uint64_t)NEVERC_TIME_HOUR;
        uint64_t minutes = magnitude / (uint64_t)NEVERC_TIME_MINUTE;
        magnitude %= (uint64_t)NEVERC_TIME_MINUTE;
        if (hours != 0) {
            append_uint64(buf, &pos, hours);
            buf[pos++] = 'h';
        }
        if (minutes != 0) {
            append_uint64(buf, &pos, minutes);
            buf[pos++] = 'm';
        }
        if (magnitude != 0 || (hours == 0 && minutes == 0))
            append_duration_decimal(buf, &pos, magnitude,
                                    (uint64_t)NEVERC_TIME_SECOND, 9, "s");
    }

    buf[pos] = '\0';
    char *result = (char *)malloc(pos + 1U);
    if (!result) return NULL;
    memcpy(result, buf, pos + 1U);
    return result;
}

neverc_time_t neverc_time_unix_milli_to_time(int64_t msec) {
    neverc_time_t t;
    t.sec = msec / 1000;
    t.nsec = (int32_t)((msec % 1000) * 1000000);
    if (t.nsec < 0) { t.sec--; t.nsec += 1000000000; }
    return t;
}

static const char *const time_month_abbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const time_month_full[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};
static const char *const time_wday_abbr[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const time_wday_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"};

static int time_ascii_ieq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return 1;
}

static int time_match_name(const char *s, size_t slen, size_t *off,
                           const char *const *names, int n, int *out) {
    for (int i = 0; i < n; i++) {
        size_t nlen = strlen(names[i]);
        if (*off + nlen <= slen && time_ascii_ieq(s + *off, names[i], nlen)) {
            *off += nlen;
            *out = i;
            return 0;
        }
    }
    return -1;
}

static void time_write_name(char *buf, size_t *pos, const char *name) {
    while (*name) buf[(*pos)++] = *name++;
}

static void write_frac_sec(char *buf, size_t *pos, int ns, int digits, int trim,
                           char sep) {
    int scale = 1;
    for (int i = digits; i < 9; i++) scale *= 10;
    int frac = ns / scale;
    if (trim) {
        while (digits > 0 && frac % 10 == 0) {
            frac /= 10;
            digits--;
        }
        if (digits == 0) return;
    }
    buf[(*pos)++] = sep;
    char tmp[9];
    for (int i = digits - 1; i >= 0; i--) {
        tmp[i] = (char)('0' + frac % 10);
        frac /= 10;
    }
    for (int i = 0; i < digits; i++) buf[(*pos)++] = tmp[i];
}

char *neverc_time_format(neverc_time_t t, const char *layout) {
    if (!layout) return NULL;
    t = normalize_time(t);
    struct tm m;
    if (!decompose(t, &m) || m.tm_mon < 0 || m.tm_mon > 11 ||
        m.tm_wday < 0 || m.tm_wday > 6)
        return NULL;
    int yr = m.tm_year + 1900;
    if (yr < 0 || yr > 9999) return NULL;
    int mo = m.tm_mon + 1;
    int dy = m.tm_mday;
    int hr = m.tm_hour;
    int mi = m.tm_min;
    int sc = m.tm_sec;
    int wd = m.tm_wday;
    int ns = t.nsec;

    size_t llen = strlen(layout);
    if (llen > (SIZE_MAX - 16U) / 2U) return NULL;
    char *buf = (char *)malloc(llen * 2U + 16U);
    if (!buf) return NULL;
    size_t out = 0;
    int h12 = hr % 12;
    if (h12 == 0) h12 = 12;

    for (size_t i = 0; i < llen;) {
        if (i + 4 <= llen && memcmp(layout + i, "2006", 4) == 0) {
            write_int(buf, &out, yr, 4); i += 4;
        } else if (i + 7 <= llen && memcmp(layout + i, "January", 7) == 0) {
            time_write_name(buf, &out, time_month_full[mo - 1]); i += 7;
        } else if (i + 3 <= llen && memcmp(layout + i, "Jan", 3) == 0) {
            time_write_name(buf, &out, time_month_abbr[mo - 1]); i += 3;
        } else if (i + 6 <= llen && memcmp(layout + i, "Monday", 6) == 0) {
            time_write_name(buf, &out, time_wday_full[wd]); i += 6;
        } else if (i + 3 <= llen && memcmp(layout + i, "Mon", 3) == 0) {
            time_write_name(buf, &out, time_wday_abbr[wd]); i += 3;
        } else if (i + 3 <= llen && memcmp(layout + i, "MST", 3) == 0) {
            buf[out++] = 'U'; buf[out++] = 'T'; buf[out++] = 'C'; i += 3;
        } else if (i + 9 <= llen && memcmp(layout + i, "Z07:00:00", 9) == 0) {
            buf[out++] = 'Z'; i += 9;
        } else if (i + 9 <= llen && memcmp(layout + i, "-07:00:00", 9) == 0) {
            memcpy(buf + out, "+00:00:00", 9); out += 9; i += 9;
        } else if (i + 7 <= llen && memcmp(layout + i, "Z070000", 7) == 0) {
            buf[out++] = 'Z'; i += 7;
        } else if (i + 7 <= llen && memcmp(layout + i, "-070000", 7) == 0) {
            memcpy(buf + out, "+000000", 7); out += 7; i += 7;
        } else if (i + 6 <= llen && memcmp(layout + i, "Z07:00", 6) == 0) {
            buf[out++] = 'Z'; i += 6;
        } else if (i + 5 <= llen && memcmp(layout + i, "Z0700", 5) == 0) {
            buf[out++] = 'Z'; i += 5;
        } else if (i + 6 <= llen && memcmp(layout + i, "-07:00", 6) == 0) {
            memcpy(buf + out, "+00:00", 6); out += 6; i += 6;
        } else if (i + 5 <= llen && memcmp(layout + i, "-0700", 5) == 0) {
            memcpy(buf + out, "+0000", 5); out += 5; i += 5;
        } else if (i + 3 <= llen && memcmp(layout + i, "Z07", 3) == 0) {
            buf[out++] = 'Z'; i += 3;
        } else if (i + 3 <= llen && memcmp(layout + i, "-07", 3) == 0) {
            memcpy(buf + out, "+00", 3); out += 3; i += 3;
        } else if (i + 2 <= llen && memcmp(layout + i, "PM", 2) == 0) {
            buf[out++] = (hr >= 12) ? 'P' : 'A'; buf[out++] = 'M'; i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "pm", 2) == 0) {
            buf[out++] = (hr >= 12) ? 'p' : 'a'; buf[out++] = 'm'; i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "01", 2) == 0) {
            write_int(buf, &out, mo, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "02", 2) == 0) {
            write_int(buf, &out, dy, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "15", 2) == 0) {
            write_int(buf, &out, hr, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "03", 2) == 0) {
            write_int(buf, &out, h12, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "04", 2) == 0) {
            write_int(buf, &out, mi, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "05", 2) == 0) {
            write_int(buf, &out, sc, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "06", 2) == 0) {
            write_int(buf, &out, yr % 100, 2); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "_2", 2) == 0) {
            if (dy < 10) buf[out++] = ' ';
            else buf[out++] = (char)('0' + dy / 10);
            buf[out++] = (char)('0' + dy % 10);
            i += 2;
        } else if ((layout[i] == '.' || layout[i] == ',') && i + 1 < llen &&
                   (layout[i + 1] == '0' || layout[i + 1] == '9')) {
            int trim = layout[i + 1] == '9';
            int digits = 0;
            while (i + 1 + (size_t)digits < llen && digits < 9 &&
                   (layout[i + 1 + (size_t)digits] == '0' ||
                    layout[i + 1 + (size_t)digits] == '9'))
                digits++;
            write_frac_sec(buf, &out, ns, digits, trim, layout[i]);
            i += 1 + (size_t)digits;
        } else if (layout[i] == '3') {
            write_int(buf, &out, h12, h12 >= 10 ? 2 : 1); i += 1;
        } else if (layout[i] == '4') {
            write_int(buf, &out, mi, mi >= 10 ? 2 : 1); i += 1;
        } else if (layout[i] == '5') {
            write_int(buf, &out, sc, sc >= 10 ? 2 : 1); i += 1;
        } else if (layout[i] == '2') {
            write_int(buf, &out, dy, dy >= 10 ? 2 : 1); i += 1;
        } else if (layout[i] == '1') {
            write_int(buf, &out, mo, mo >= 10 ? 2 : 1); i += 1;
        } else {
            buf[out++] = layout[i++];
        }
    }
    buf[out] = '\0';
    return buf;
}

static int parse_n_digits(const char *value, size_t vlen, size_t *vi, int n,
                          int *out) {
    if (vlen - *vi < (size_t)n) return -1;
    int result = 0;
    for (int j = 0; j < n; j++) {
        if (value[*vi] < '0' || value[*vi] > '9') return -1;
        result = result * 10 + (value[*vi] - '0');
        (*vi)++;
    }
    *out = result;
    return 0;
}

static int parse_flex_digits(const char *value, size_t vlen, size_t *vi,
                             int maxv, int *out) {
    if (*vi >= vlen || value[*vi] < '0' || value[*vi] > '9') return -1;
    int v = value[(*vi)++] - '0';
    if (*vi < vlen && value[*vi] >= '0' && value[*vi] <= '9') {
        int two = v * 10 + (value[*vi] - '0');
        if (two <= maxv) {
            v = two;
            (*vi)++;
        }
    }
    *out = v;
    return 0;
}

static int parse_under_day(const char *value, size_t vlen, size_t *vi, int *out) {
    if (*vi >= vlen) return -1;
    if (value[*vi] == ' ') {
        (*vi)++;
        if (*vi >= vlen || value[*vi] < '0' || value[*vi] > '9') return -1;
        *out = value[(*vi)++] - '0';
        return 0;
    }
    return parse_n_digits(value, vlen, vi, 2, out);
}

static int parse_named_zone(const char *value, size_t vlen, size_t *vi) {
    size_t n = 0;
    while (*vi + n < vlen) {
        unsigned char c = (unsigned char)value[*vi + n];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) break;
        n++;
    }
    if (n < 3 || n > 5) return -1;
    *vi += n;
    return 0;
}

static int parse_num_zone(const char *value, size_t vlen, size_t *vi,
                          int allow_z, int colon, int with_min, int with_sec,
                          int *tz_sec) {
    if (*vi >= vlen) return -1;
    if (allow_z && (value[*vi] == 'Z' || value[*vi] == 'z')) {
        (*vi)++;
        *tz_sec = 0;
        return 0;
    }
    int sign = 1;
    if (value[*vi] == '+') sign = 1;
    else if (value[*vi] == '-') sign = -1;
    else return -1;
    (*vi)++;
    int hh = 0, mm = 0, ss = 0;
    if (parse_n_digits(value, vlen, vi, 2, &hh) != 0) return -1;
    if (with_min) {
        if (colon) {
            if (*vi >= vlen || value[*vi] != ':') return -1;
            (*vi)++;
        }
        if (parse_n_digits(value, vlen, vi, 2, &mm) != 0) return -1;
        if (with_sec) {
            if (colon) {
                if (*vi >= vlen || value[*vi] != ':') return -1;
                (*vi)++;
            }
            if (parse_n_digits(value, vlen, vi, 2, &ss) != 0) return -1;
        }
    }
    if (hh > 14 || mm > 59 || ss > 59 ||
        (hh == 14 && (mm != 0 || ss != 0)))
        return -1;
    *tz_sec = sign * (hh * 3600 + mm * 60 + ss);
    return 0;
}

static int parse_frac_sec(const char *value, size_t vlen, size_t *vi,
                          int digits, int required, char sep, int *ns) {
    if (*vi < vlen && value[*vi] == sep) {
        (*vi)++;
        int got = 0;
        int val = 0;
        while (got < digits && *vi < vlen &&
               value[*vi] >= '0' && value[*vi] <= '9') {
            val = val * 10 + (value[(*vi)++] - '0');
            got++;
        }
        if (got == 0 || (required && got != digits)) return -1;
        while (got < 9) {
            val *= 10;
            got++;
        }
        *ns = val;
        return 0;
    }
    if (required) return -1;
    *ns = 0;
    return 0;
}

int neverc_time_parse(const char *layout, const char *value, neverc_time_t *out) {
    if (!layout || !value || !out) return -1;
    int yr = 0, mo = 1, dy = 1, hr = 0, mi = 0, sc = 0, ns = 0, wd = -1;
    int hour12 = 0, pm = -1, tz_sec = 0;
    size_t li = 0, vi = 0;
    size_t llen = strlen(layout), vlen = strlen(value);

    while (li < llen) {
        if (li + 4 <= llen && memcmp(layout + li, "2006", 4) == 0) {
            if (parse_n_digits(value, vlen, &vi, 4, &yr) != 0) return -1;
            li += 4;
        } else if (li + 7 <= llen && memcmp(layout + li, "January", 7) == 0) {
            int idx;
            if (time_match_name(value, vlen, &vi, time_month_full, 12, &idx) != 0)
                return -1;
            mo = idx + 1;
            li += 7;
        } else if (li + 3 <= llen && memcmp(layout + li, "Jan", 3) == 0) {
            int idx;
            if (time_match_name(value, vlen, &vi, time_month_abbr, 12, &idx) != 0)
                return -1;
            mo = idx + 1;
            li += 3;
        } else if (li + 6 <= llen && memcmp(layout + li, "Monday", 6) == 0) {
            if (time_match_name(value, vlen, &vi, time_wday_full, 7, &wd) != 0)
                return -1;
            li += 6;
        } else if (li + 3 <= llen && memcmp(layout + li, "Mon", 3) == 0) {
            if (time_match_name(value, vlen, &vi, time_wday_abbr, 7, &wd) != 0)
                return -1;
            li += 3;
        } else if (li + 3 <= llen && memcmp(layout + li, "MST", 3) == 0) {
            if (parse_named_zone(value, vlen, &vi) != 0) return -1;
            li += 3;
        } else if (li + 9 <= llen && memcmp(layout + li, "Z07:00:00", 9) == 0) {
            if (parse_num_zone(value, vlen, &vi, 1, 1, 1, 1, &tz_sec) != 0)
                return -1;
            li += 9;
        } else if (li + 9 <= llen && memcmp(layout + li, "-07:00:00", 9) == 0) {
            if (parse_num_zone(value, vlen, &vi, 0, 1, 1, 1, &tz_sec) != 0)
                return -1;
            li += 9;
        } else if (li + 7 <= llen && memcmp(layout + li, "Z070000", 7) == 0) {
            if (parse_num_zone(value, vlen, &vi, 1, 0, 1, 1, &tz_sec) != 0)
                return -1;
            li += 7;
        } else if (li + 7 <= llen && memcmp(layout + li, "-070000", 7) == 0) {
            if (parse_num_zone(value, vlen, &vi, 0, 0, 1, 1, &tz_sec) != 0)
                return -1;
            li += 7;
        } else if (li + 6 <= llen && memcmp(layout + li, "Z07:00", 6) == 0) {
            if (parse_num_zone(value, vlen, &vi, 1, 1, 1, 0, &tz_sec) != 0)
                return -1;
            li += 6;
        } else if (li + 5 <= llen && memcmp(layout + li, "Z0700", 5) == 0) {
            if (parse_num_zone(value, vlen, &vi, 1, 0, 1, 0, &tz_sec) != 0)
                return -1;
            li += 5;
        } else if (li + 6 <= llen && memcmp(layout + li, "-07:00", 6) == 0) {
            if (parse_num_zone(value, vlen, &vi, 0, 1, 1, 0, &tz_sec) != 0)
                return -1;
            li += 6;
        } else if (li + 5 <= llen && memcmp(layout + li, "-0700", 5) == 0) {
            if (parse_num_zone(value, vlen, &vi, 0, 0, 1, 0, &tz_sec) != 0)
                return -1;
            li += 5;
        } else if (li + 3 <= llen && memcmp(layout + li, "Z07", 3) == 0) {
            if (parse_num_zone(value, vlen, &vi, 1, 0, 0, 0, &tz_sec) != 0)
                return -1;
            li += 3;
        } else if (li + 3 <= llen && memcmp(layout + li, "-07", 3) == 0) {
            if (parse_num_zone(value, vlen, &vi, 0, 0, 0, 0, &tz_sec) != 0)
                return -1;
            li += 3;
        } else if (li + 2 <= llen && memcmp(layout + li, "01", 2) == 0) {
            if (parse_n_digits(value, vlen, &vi, 2, &mo) != 0) return -1;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "02", 2) == 0) {
            if (parse_n_digits(value, vlen, &vi, 2, &dy) != 0) return -1;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "15", 2) == 0) {
            if (parse_n_digits(value, vlen, &vi, 2, &hr) != 0) return -1;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "03", 2) == 0) {
            if (parse_n_digits(value, vlen, &vi, 2, &hr) != 0) return -1;
            hour12 = 1;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "PM", 2) == 0) {
            if (vi + 2 > vlen) return -1;
            if (value[vi] == 'P' && value[vi + 1] == 'M') pm = 1;
            else if (value[vi] == 'A' && value[vi + 1] == 'M') pm = 0;
            else return -1;
            vi += 2;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "pm", 2) == 0) {
            if (vi + 2 > vlen) return -1;
            if (value[vi] == 'p' && value[vi + 1] == 'm') pm = 1;
            else if (value[vi] == 'a' && value[vi + 1] == 'm') pm = 0;
            else return -1;
            vi += 2;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "04", 2) == 0) {
            if (parse_n_digits(value, vlen, &vi, 2, &mi) != 0) return -1;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "05", 2) == 0) {
            if (parse_n_digits(value, vlen, &vi, 2, &sc) != 0) return -1;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "06", 2) == 0) {
            int yy;
            if (parse_n_digits(value, vlen, &vi, 2, &yy) != 0) return -1;
            yr = 1900 + yy;
            if (yr < 1969) yr += 100;
            li += 2;
        } else if (li + 2 <= llen && memcmp(layout + li, "_2", 2) == 0) {
            if (parse_under_day(value, vlen, &vi, &dy) != 0) return -1;
            li += 2;
        } else if ((layout[li] == '.' || layout[li] == ',') && li + 1 < llen &&
                   (layout[li + 1] == '0' || layout[li + 1] == '9')) {
            int required = layout[li + 1] == '0';
            int digits = 0;
            while (li + 1 + (size_t)digits < llen && digits < 9 &&
                   (layout[li + 1 + (size_t)digits] == '0' ||
                    layout[li + 1 + (size_t)digits] == '9'))
                digits++;
            if (parse_frac_sec(value, vlen, &vi, digits, required, layout[li],
                               &ns) != 0)
                return -1;
            li += 1 + (size_t)digits;
        } else if (layout[li] == '3') {
            if (parse_flex_digits(value, vlen, &vi, 12, &hr) != 0) return -1;
            hour12 = 1;
            li += 1;
        } else if (layout[li] == '4') {
            if (parse_flex_digits(value, vlen, &vi, 59, &mi) != 0) return -1;
            li += 1;
        } else if (layout[li] == '5') {
            if (parse_flex_digits(value, vlen, &vi, 60, &sc) != 0) return -1;
            li += 1;
        } else if (layout[li] == '2') {
            if (parse_flex_digits(value, vlen, &vi, 31, &dy) != 0) return -1;
            li += 1;
        } else if (layout[li] == '1') {
            if (parse_flex_digits(value, vlen, &vi, 12, &mo) != 0) return -1;
            li += 1;
        } else {
            if (vi >= vlen || layout[li] != value[vi]) return -1;
            li++;
            vi++;
        }
    }
    if (hour12) {
        if (hr < 1 || hr > 12) return -1;
        if (pm == 1) hr = (hr == 12) ? 12 : hr + 12;
        else if (pm == 0) hr = (hr == 12) ? 0 : hr;
    }
    if (sc == 60) sc = 59;
    if (vi != vlen || mo < 1 || mo > 12 || dy < 1 ||
        dy > days_in_month(yr, mo) || hr < 0 || hr > 23 ||
        mi < 0 || mi > 59 || sc < 0 || sc > 59 ||
        ns < 0 || ns > 999999999)
        return -1;
    neverc_time_t local = neverc_time_date(yr, mo, dy, hr, mi, sc, ns);
    if (wd >= 0 && neverc_time_weekday(local) != wd)
        return -1;
    if (tz_sec != 0)
        local = neverc_time_add(local,
            -(neverc_duration_t)tz_sec * NEVERC_TIME_SECOND);
    *out = local;
    return 0;
}

static uint64_t add_mod(uint64_t a, uint64_t b, uint64_t modulus) {
    return a >= modulus - b ? a - (modulus - b) : a + b;
}

static uint64_t mul_mod(uint64_t a, uint64_t b, uint64_t modulus) {
    uint64_t result = 0;
    while (b != 0) {
        if (b & 1U) result = add_mod(result, a, modulus);
        b >>= 1;
        if (b != 0) a = add_mod(a, a, modulus);
    }
    return result;
}

static uint64_t timestamp_remainder(neverc_time_t t, neverc_duration_t d) {
    t = normalize_time(t);
    uint64_t modulus = (uint64_t)d;
    int64_t signed_sec_rem = t.sec % d;
    uint64_t sec_rem = signed_sec_rem < 0
        ? modulus - (uint64_t)(-signed_sec_rem)
        : (uint64_t)signed_sec_rem;
    uint64_t result = mul_mod(sec_rem,
                              (uint64_t)NEVERC_TIME_SECOND % modulus,
                              modulus);
    return add_mod(result, (uint32_t)t.nsec % modulus, modulus);
}

neverc_time_t neverc_time_truncate(neverc_time_t t, neverc_duration_t d) {
    if (d <= 0) return t;
    uint64_t remainder = timestamp_remainder(t, d);
    return neverc_time_add(t, -(int64_t)remainder);
}

neverc_time_t neverc_time_round(neverc_time_t t, neverc_duration_t d) {
    if (d <= 0) return t;
    uint64_t remainder = timestamp_remainder(t, d);
    uint64_t upper = (uint64_t)d - remainder;
    if (remainder < upper)
        return neverc_time_add(t, -(int64_t)remainder);
    return neverc_time_add(t, (int64_t)upper);
}
