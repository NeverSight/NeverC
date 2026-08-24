#include "neverc/std/time_tzdata.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

static void tz_yield(void) {
#if defined(_WIN32)
    Sleep(0);
#else
    sched_yield();
#endif
}

/* ======================================================================
 * Internal helpers
 * ====================================================================== */

static int nc_streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int nc_strpfx(const char *s, const char *prefix) {
    while (*prefix) { if (*s != *prefix) return 0; s++; prefix++; }
    return 1;
}

static int nc_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static size_t nc_slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Go time.LoadLocation: reject "..", a leading '/' or '\\'. Empty is not
 * UTC here — lookup documents NULL for unknown names. */
static int tz_iana_name_ok(const char *name) {
    if (!name || !name[0])
        return 0;
    if (name[0] == '/' || name[0] == '\\')
        return 0;
    for (const char *p = name; p[0] && p[1]; p++) {
        if (p[0] == '.' && p[1] == '.')
            return 0;
    }
    return 1;
}

/* TZif abbreviations are packed in a counted char array. A missing NUL
 * must fail closed: unbounded strlen would read past the file, and the
 * later memcpy of that length could overflow the heap copy. */
static int tz_chararray_strlen(const uint8_t *chars, uint32_t nchar,
                               uint8_t off, size_t *out_len) {
    if ((uint32_t)off >= nchar)
        return -1;
    uint32_t i = off;
    while (i < nchar && chars[i] != 0)
        i++;
    if (i >= nchar)
        return -1;
    *out_len = (size_t)(i - off);
    return 0;
}

/* ======================================================================
 * Timezone database — 100+ common IANA timezones
 *
 * Offsets in seconds from UTC.
 * has_dst=1 for zones that observe daylight saving time.
 * ====================================================================== */

/* Latitude categories for DST: N = northern hemisphere, S = southern */
#define N 1  /* DST Mar-Nov */
#define S 2  /* DST Oct-Apr */

typedef struct { const char *name; const char *abbr; const char *abbr_dst; int off; int off_dst; int hemi; } tz_entry_t;

static const tz_entry_t tz_table[] = {
    /* UTC / GMT */
    {"UTC",                     "UTC",  NULL,   0,      0,      0},
    {"GMT",                     "GMT",  NULL,   0,      0,      0},
    {"Etc/UTC",                 "UTC",  NULL,   0,      0,      0},
    {"Etc/GMT",                 "GMT",  NULL,   0,      0,      0},

    /* Go zoneinfo "factory" names: fixed offsets, no DST.
     * LoadLocation("EST") is not America/New_York. */
    {"EST",                     "EST",  NULL,   -18000, 0,      0},
    {"MST",                     "MST",  NULL,   -25200, 0,      0},
    {"HST",                     "HST",  NULL,   -36000, 0,      0},

    /* Americas */
    {"America/New_York",        "EST",  "EDT",  -18000, -14400, N},
    {"America/Chicago",         "CST",  "CDT",  -21600, -18000, N},
    {"America/Denver",          "MST",  "MDT",  -25200, -21600, N},
    {"America/Los_Angeles",     "PST",  "PDT",  -28800, -25200, N},
    {"America/Anchorage",       "AKST", "AKDT", -32400, -28800, N},
    {"America/Honolulu",        "HST",  NULL,   -36000, 0,      0},
    {"America/Phoenix",         "MST",  NULL,   -25200, 0,      0},
    {"America/Toronto",         "EST",  "EDT",  -18000, -14400, N},
    {"America/Vancouver",       "PST",  "PDT",  -28800, -25200, N},
    {"America/Winnipeg",        "CST",  "CDT",  -21600, -18000, N},
    {"America/Edmonton",        "MST",  "MDT",  -25200, -21600, N},
    {"America/Halifax",         "AST",  "ADT",  -14400, -10800, N},
    {"America/St_Johns",        "NST",  "NDT",  -12600, -9000,  N},
    {"America/Mexico_City",     "CST",  NULL,   -21600, 0,      0},
    {"America/Bogota",          "COT",  NULL,   -18000, 0,      0},
    {"America/Lima",            "PET",  NULL,   -18000, 0,      0},
    {"America/Santiago",        "CLT",  "CLST", -14400, -10800, S},
    {"America/Sao_Paulo",       "BRT",  NULL,   -10800, 0,      0},
    {"America/Argentina/Buenos_Aires","ART",NULL,-10800, 0,      0},
    {"America/Caracas",         "VET",  NULL,   -14400, 0,      0},

    /* Europe */
    {"Europe/London",           "GMT",  "BST",  0,      3600,   N},
    {"Europe/Paris",            "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Berlin",           "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Madrid",           "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Rome",             "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Amsterdam",        "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Brussels",         "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Zurich",           "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Vienna",           "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Stockholm",        "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Copenhagen",       "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Oslo",             "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Helsinki",         "EET",  "EEST", 7200,   10800,  N},
    {"Europe/Warsaw",           "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Prague",           "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Budapest",         "CET",  "CEST", 3600,   7200,   N},
    {"Europe/Bucharest",        "EET",  "EEST", 7200,   10800,  N},
    {"Europe/Athens",           "EET",  "EEST", 7200,   10800,  N},
    {"Europe/Istanbul",         "TRT",  NULL,   10800,  0,      0},
    {"Europe/Moscow",           "MSK",  NULL,   10800,  0,      0},
    {"Europe/Kiev",             "EET",  "EEST", 7200,   10800,  N},
    {"Europe/Kyiv",             "EET",  "EEST", 7200,   10800,  N},
    {"Europe/Dublin",           "GMT",  "IST",  0,      3600,   N},
    {"Europe/Lisbon",           "WET",  "WEST", 0,      3600,   N},

    /* Asia */
    {"Asia/Shanghai",           "CST",  NULL,   28800,  0,      0},
    {"Asia/Hong_Kong",          "HKT",  NULL,   28800,  0,      0},
    {"Asia/Taipei",             "CST",  NULL,   28800,  0,      0},
    {"Asia/Tokyo",              "JST",  NULL,   32400,  0,      0},
    {"Asia/Seoul",              "KST",  NULL,   32400,  0,      0},
    {"Asia/Singapore",          "SGT",  NULL,   28800,  0,      0},
    {"Asia/Kolkata",            "IST",  NULL,   19800,  0,      0},
    {"Asia/Calcutta",           "IST",  NULL,   19800,  0,      0},
    {"Asia/Mumbai",             "IST",  NULL,   19800,  0,      0},
    {"Asia/Dubai",              "GST",  NULL,   14400,  0,      0},
    {"Asia/Riyadh",             "AST",  NULL,   10800,  0,      0},
    {"Asia/Tehran",             "IRST", NULL,   12600,  0,      0},
    {"Asia/Bangkok",            "ICT",  NULL,   25200,  0,      0},
    {"Asia/Jakarta",            "WIB",  NULL,   25200,  0,      0},
    {"Asia/Ho_Chi_Minh",        "ICT",  NULL,   25200,  0,      0},
    {"Asia/Manila",             "PHT",  NULL,   28800,  0,      0},
    {"Asia/Kuala_Lumpur",       "MYT",  NULL,   28800,  0,      0},
    {"Asia/Karachi",            "PKT",  NULL,   18000,  0,      0},
    {"Asia/Dhaka",              "BST",  NULL,   21600,  0,      0},
    {"Asia/Colombo",            "IST",  NULL,   19800,  0,      0},
    {"Asia/Kathmandu",          "NPT",  NULL,   20700,  0,      0},
    {"Asia/Yangon",             "MMT",  NULL,   23400,  0,      0},
    {"Asia/Almaty",             "ALMT", NULL,   18000,  0,      0},
    {"Asia/Vladivostok",        "VLAT", NULL,   36000,  0,      0},
    {"Asia/Novosibirsk",        "NOVT", NULL,   25200,  0,      0},
    {"Asia/Yekaterinburg",      "YEKT", NULL,   18000,  0,      0},
    {"Asia/Tashkent",           "UZT",  NULL,   18000,  0,      0},
    {"Asia/Kabul",              "AFT",  NULL,   16200,  0,      0},
    {"Asia/Baghdad",            "AST",  NULL,   10800,  0,      0},
    {"Asia/Jerusalem",          "IST",  "IDT",  7200,   10800,  N},

    /* Oceania */
    {"Australia/Sydney",        "AEST", "AEDT", 36000,  39600,  S},
    {"Australia/Melbourne",     "AEST", "AEDT", 36000,  39600,  S},
    {"Australia/Brisbane",      "AEST", NULL,   36000,  0,      0},
    {"Australia/Perth",         "AWST", NULL,   28800,  0,      0},
    {"Australia/Adelaide",      "ACST", "ACDT", 34200,  37800,  S},
    {"Australia/Darwin",        "ACST", NULL,   34200,  0,      0},
    {"Australia/Hobart",        "AEST", "AEDT", 36000,  39600,  S},
    {"Pacific/Auckland",        "NZST", "NZDT", 43200,  46800,  S},
    {"Pacific/Fiji",            "FJT",  NULL,   43200,  0,      0},
    {"Pacific/Guam",            "ChST", NULL,   36000,  0,      0},
    {"Pacific/Chatham",         "CHAST","CHADT",45900,  49500,  S},
    {"Pacific/Tongatapu",       "TOT",  NULL,   46800,  0,      0},
    {"Pacific/Samoa",           "SST",  NULL,   -39600, 0,      0},
    {"Pacific/Midway",          "SST",  NULL,   -39600, 0,      0},

    /* Africa */
    {"Africa/Cairo",            "EET",  "EEST", 7200,   10800,  N},
    {"Africa/Lagos",            "WAT",  NULL,   3600,   0,      0},
    {"Africa/Johannesburg",     "SAST", NULL,   7200,   0,      0},
    {"Africa/Nairobi",          "EAT",  NULL,   10800,  0,      0},
    {"Africa/Casablanca",       "+01",  NULL,   3600,   0,      0},
    {"Africa/Algiers",          "CET",  NULL,   3600,   0,      0},
    {"Africa/Tunis",            "CET",  NULL,   3600,   0,      0},
    {"Africa/Addis_Ababa",      "EAT",  NULL,   10800,  0,      0},
    {"Africa/Accra",            "GMT",  NULL,   0,      0,      0},
    {"Africa/Dar_es_Salaam",    "EAT",  NULL,   10800,  0,      0},

    /* Atlantic / Indian */
    {"Atlantic/Reykjavik",      "GMT",  NULL,   0,      0,      0},
    {"Atlantic/Azores",         "AZOT", "AZOST",-3600,  0,      N},
    {"Indian/Maldives",         "MVT",  NULL,   18000,  0,      0},
    {"Indian/Mauritius",        "MUT",  NULL,   14400,  0,      0},
};

#undef N
#undef S

static const int tz_count = sizeof(tz_table) / sizeof(tz_table[0]);

/* ======================================================================
 * Public API
 * ====================================================================== */

static void fill_zone(neverc_tzdata_zone_t *z, const tz_entry_t *e) {
    z->name = e->name;
    z->abbrev = e->abbr;
    z->abbrev_dst = e->abbr_dst;
    z->utc_offset = e->off;
    z->dst_offset = e->off_dst;
    z->has_dst = (e->hemi != 0) ? 1 : 0;
    z->dst_hemi = e->hemi;
}

/* Process-wide zone cache. g_zones_init: 0 = empty, 1 = filling, 2 = ready. */
static neverc_tzdata_zone_t g_zones[sizeof(tz_table) / sizeof(tz_table[0])];
static int g_zones_init = 0;

static void init_zones(void) {
    /* Only 2 means published. Returning on any non-zero let concurrent
     * first callers observe a half-filled g_zones while state was 1. */
    if (__atomic_load_n(&g_zones_init, __ATOMIC_ACQUIRE) == 2) return;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&g_zones_init, &expected, 1, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&g_zones_init, __ATOMIC_ACQUIRE) != 2)
            tz_yield();
        return;
    }
    for (int i = 0; i < tz_count; i++)
        fill_zone(&g_zones[i], &tz_table[i]);
    __atomic_store_n(&g_zones_init, 2, __ATOMIC_RELEASE);
}

const neverc_tzdata_zone_t *neverc_tzdata_lookup(const char *name) {
    if (!tz_iana_name_ok(name)) return NULL;
    init_zones();
    for (int i = 0; i < tz_count; i++) {
        if (nc_streq(tz_table[i].name, name))
            return &g_zones[i];
    }
    return NULL;
}

const neverc_tzdata_zone_t *neverc_tzdata_lookup_abbrev(const char *abbrev) {
    if (!abbrev) return NULL;
    init_zones();
    for (int i = 0; i < tz_count; i++) {
        if (nc_streq_ci(tz_table[i].abbr, abbrev))
            return &g_zones[i];
        if (tz_table[i].abbr_dst && nc_streq_ci(tz_table[i].abbr_dst, abbrev))
            return &g_zones[i];
    }
    return NULL;
}

neverc_tzdata_zone_t *neverc_tzdata_fixed_zone(const char *name, int offset_sec) {
    if (!name) name = "";
    neverc_tzdata_zone_t *z = (neverc_tzdata_zone_t *)calloc(1, sizeof(*z));
    if (!z) return NULL;
    size_t nlen = nc_slen(name);
    char *n = (char *)malloc(nlen + 1);
    if (!n) {
        free(z);
        return NULL;
    }
    for (size_t i = 0; i < nlen; i++) n[i] = name[i];
    n[nlen] = '\0';
    z->name = n;
    z->abbrev = n;
    z->abbrev_dst = NULL;
    z->utc_offset = offset_sec;
    z->dst_offset = 0;
    z->has_dst = 0;
    return z;
}

int neverc_tzdata_count(void) {
    return tz_count;
}

const neverc_tzdata_zone_t *neverc_tzdata_at(int index) {
    if (index < 0 || index >= tz_count) return NULL;
    init_zones();
    return &g_zones[index];
}

int neverc_tzdata_list(const char *prefix, const char **names, int max_names) {
    if (!names || max_names <= 0) return 0;
    int count = 0;
    for (int i = 0; i < tz_count && count < max_names; i++) {
        if (!prefix || !prefix[0] || nc_strpfx(tz_table[i].name, prefix)) {
            names[count++] = tz_table[i].name;
        }
    }
    return count;
}

const neverc_tzdata_zone_t *neverc_tzdata_utc(void) {
    return neverc_tzdata_lookup("UTC");
}

/* Civil date helpers (Howard Hinnant days_from_civil). Kept local so
 * tzdata does not link against time.c. */
static int tz_is_leap(int64_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int64_t tz_days_from_civil(int64_t y, int m, int d) {
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void tz_civil_from_days(int64_t z, int64_t *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t year = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int day = (int)(doy - (153 * mp + 2) / 5 + 1);
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = year + (month <= 2);
    *m = month;
    *d = day;
}

static int64_t tz_add_sat(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b) return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b) return INT64_MIN;
    return a + b;
}

static int64_t tz_unix_civil(int64_t y, int m, int d, int h, int mi, int s) {
    int64_t days = tz_days_from_civil(y, m, d);
    int64_t sec;
    if (days > 0 && days > INT64_MAX / 86400)
        sec = INT64_MAX;
    else if (days < 0 && days < INT64_MIN / 86400)
        sec = INT64_MIN;
    else
        sec = days * 86400;
    int64_t tod = (int64_t)h * 3600 + (int64_t)mi * 60 + s;
    return tz_add_sat(sec, tod);
}

/* 0 = Sunday. 1970-01-01 was Thursday. */
static int tz_wday(int64_t y, int m, int d) {
    int64_t w = (tz_days_from_civil(y, m, d) + 4) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

static int tz_last_wday(int64_t y, int m, int wday) {
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int last = dim[m - 1];
    if (m == 2 && tz_is_leap(y)) last = 29;
    int wd = tz_wday(y, m, last);
    int delta = (wd - wday + 7) % 7;
    return last - delta;
}

/* nth: 1-4, or 5 for last. */
static int tz_nth_wday(int64_t y, int m, int wday, int nth) {
    if (nth >= 5) return tz_last_wday(y, m, wday);
    int wd = tz_wday(y, m, 1);
    return 1 + (wday - wd + 7) % 7 + (nth - 1) * 7;
}

static int tz_in_span(int64_t unix_sec, int64_t start, int64_t end) {
    if (start < end) return unix_sec >= start && unix_sec < end;
    return unix_sec >= start || unix_sec < end;
}

/* kind: 0 = Mm.w.d, 1 = Jn (1-365, no leap day), 2 = n (0-365, leap counted). */
typedef struct {
    int kind, month, week, wday, yday, at_sec;
} posix_rule_t;

typedef struct {
    int std_off, dst_off, has_dst, has_rules;
    posix_rule_t start, end;
} posix_tz_parsed_t;

typedef struct posix_extra {
    neverc_tzdata_zone_t zone;
    char std[16];
    char dst[16];
    posix_tz_parsed_t parsed;
    char *tzstr;
    struct posix_extra *next;
} posix_extra_t;

static posix_extra_t *g_posix_list;
static volatile int32_t g_posix_lock;

static void posix_lock(void) {
    int expected = 0;
    while (!__atomic_compare_exchange_n(&g_posix_lock, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        expected = 0;
        tz_yield();
    }
}

static void posix_unlock(void) {
    __atomic_store_n(&g_posix_lock, 0, __ATOMIC_RELEASE);
}

static int posix_uint(const char **p, int max_digits, int *out) {
    if (**p < '0' || **p > '9') return -1;
    int v = 0, n = 0;
    while (**p >= '0' && **p <= '9' && n < max_digits) {
        v = v * 10 + (*(*p)++ - '0');
        n++;
    }
    *out = v;
    return 0;
}

/* Go tzsetNum(..., 0, 24*7): variable-length hours, max 168. */
static int parse_posix_hours(const char **p, int *hh) {
    if (**p < '0' || **p > '9') return -1;
    int v = 0, n = 0;
    while (**p >= '0' && **p <= '9') {
        if (n >= 4) return -1;
        v = v * 10 + (*(*p)++ - '0');
        n++;
    }
    if (v > 24 * 7) return -1;
    *hh = v;
    return 0;
}

static int posix_is_rule_sep(int c) {
    return c == ',' || c == ';';
}

static int parse_posix_tz_name(const char **p, char *buf, size_t cap) {
    const char *s = *p;
    size_t n = 0;
    if (*s == '<') {
        s++;
        while (*s && *s != '>' && n + 1 < cap) buf[n++] = *s++;
        if (*s != '>' || n == 0) return -1;
        s++;
    } else {
        while (((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) &&
               n + 1 < cap)
            buf[n++] = *s++;
        if (n < 3) return -1;
    }
    buf[n] = '\0';
    *p = s;
    return 0;
}

/* POSIX offset: positive is west of UTC, so utc_offset = -parsed. */
static int parse_posix_offset(const char **p, int *utc_sec) {
    const char *s = *p;
    int sign = 1;
    if (*s == '+') { sign = 1; s++; }
    else if (*s == '-') { sign = -1; s++; }
    if (*s < '0' || *s > '9') return -1;
    int hh = 0, mm = 0, ss = 0;
    if (parse_posix_hours(&s, &hh) != 0) return -1;
    if (*s == ':') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        mm = (*s++ - '0') * 10;
        if (*s < '0' || *s > '9') return -1;
        mm += *s++ - '0';
        if (mm > 59) return -1;
        if (*s == ':') {
            s++;
            if (*s < '0' || *s > '9') return -1;
            ss = (*s++ - '0') * 10;
            if (*s < '0' || *s > '9') return -1;
            ss += *s++ - '0';
            if (ss > 59) return -1;
        }
    }
    *utc_sec = -sign * (hh * 3600 + mm * 60 + ss);
    *p = s;
    return 0;
}

static int parse_posix_rule_time(const char **p, int *at) {
    *at = 2 * 3600;
    if (**p != '/') return 0;
    (*p)++;
    int sign = 1;
    if (**p == '+') { (*p)++; }
    else if (**p == '-') { sign = -1; (*p)++; }
    int hh = 0, mm = 0, ss = 0;
    if (parse_posix_hours(p, &hh) != 0) return -1;
    if (**p == ':') {
        (*p)++;
        if (posix_uint(p, 2, &mm) != 0) return -1;
        if (**p == ':') {
            (*p)++;
            if (posix_uint(p, 2, &ss) != 0) return -1;
        }
    }
    if (mm > 59 || ss > 59) return -1;
    *at = sign * (hh * 3600 + mm * 60 + ss);
    return 0;
}

static int parse_posix_rule(const char **p, posix_rule_t *r) {
    memset(r, 0, sizeof(*r));
    if (**p == 'M') {
        (*p)++;
        int month, week, wday;
        if (posix_uint(p, 2, &month) != 0 || month < 1 || month > 12) return -1;
        if (**p != '.') return -1;
        (*p)++;
        if (posix_uint(p, 1, &week) != 0 || week < 1 || week > 5) return -1;
        if (**p != '.') return -1;
        (*p)++;
        if (posix_uint(p, 1, &wday) != 0 || wday > 6) return -1;
        if (parse_posix_rule_time(p, &r->at_sec) != 0) return -1;
        r->kind = 0;
        r->month = month;
        r->week = week;
        r->wday = wday;
        return 0;
    }
    if (**p == 'J') {
        (*p)++;
        int n;
        if (posix_uint(p, 3, &n) != 0 || n < 1 || n > 365) return -1;
        if (parse_posix_rule_time(p, &r->at_sec) != 0) return -1;
        r->kind = 1;
        r->yday = n;
        return 0;
    }
    if (**p >= '0' && **p <= '9') {
        int n;
        if (posix_uint(p, 3, &n) != 0 || n > 365) return -1;
        if (parse_posix_rule_time(p, &r->at_sec) != 0) return -1;
        r->kind = 2;
        r->yday = n;
        return 0;
    }
    return -1;
}

static void copy_cstr(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    while (src[n] && n + 1 < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static int64_t posix_rule_unix(int64_t year, const posix_rule_t *r, int offset) {
    int64_t base;
    if (r->kind == 0) {
        int dom = tz_nth_wday(year, r->month, r->wday, r->week);
        base = tz_unix_civil(year, r->month, dom, 0, 0, 0);
    } else {
        /* Go time.tzruleTime: Jn skips Feb 29; n counts it. */
        base = tz_unix_civil(year, 1, 1, 0, 0, 0);
        int64_t days = (r->kind == 1) ? (int64_t)r->yday - 1 : (int64_t)r->yday;
        if (r->kind == 1 && tz_is_leap(year) && r->yday >= 60)
            days++;
        if (days > 0 && days > INT64_MAX / 86400)
            base = INT64_MAX;
        else if (days < 0 && days < INT64_MIN / 86400)
            base = INT64_MIN;
        else
            base = tz_add_sat(base, days * 86400);
    }
    return tz_add_sat(tz_add_sat(base, r->at_sec), -(int64_t)offset);
}

/* Southern DST wraps the year (Sep→Apr, J260→J90, …). Mm.w.d stores
 * months, but Jn / n rules leave month=0, so start.month > end.month
 * cannot see those wrap-arounds. Compare rule instants in a leap year. */
static int posix_dst_wraps(const posix_tz_parsed_t *parsed) {
    if (!parsed || !parsed->has_dst || !parsed->has_rules)
        return 0;
    int64_t start = posix_rule_unix(2000, &parsed->start, parsed->std_off);
    int64_t end = posix_rule_unix(2000, &parsed->end, parsed->dst_off);
    return start > end;
}

static int parse_posix_tz_fields(const char *tz, posix_tz_parsed_t *out,
                                 char *stdn, size_t stdn_cap,
                                 char *dstn, size_t dstn_cap) {
    if (!tz || !tz[0] || tz[0] == ':' || tz[0] == '/' || !out)
        return -1;
    const char *p = tz;
    if (dstn && dstn_cap)
        dstn[0] = '\0';
    memset(out, 0, sizeof(*out));
    if (parse_posix_tz_name(&p, stdn, stdn_cap) != 0) return -1;
    if (parse_posix_offset(&p, &out->std_off) != 0) return -1;
    if (*p && !posix_is_rule_sep(*p)) {
        if (parse_posix_tz_name(&p, dstn, dstn_cap) != 0) return -1;
        out->has_dst = 1;
        out->dst_off = out->std_off + 3600;
        if (*p && !posix_is_rule_sep(*p)) {
            if (parse_posix_offset(&p, &out->dst_off) != 0) return -1;
        }
    }
    if (posix_is_rule_sep(*p)) {
        p++;
        if (parse_posix_rule(&p, &out->start) != 0) return -1;
        if (!posix_is_rule_sep(*p)) return -1;
        p++;
        if (parse_posix_rule(&p, &out->end) != 0) return -1;
        out->has_rules = 1;
    }
    if (*p != '\0') return -1;
    return 0;
}

static int posix_rules_dst_active(const posix_tz_parsed_t *parsed, int64_t unix_sec) {
    if (!parsed || !parsed->has_dst) return 0;
    int64_t days = unix_sec / 86400;
    int64_t sod = unix_sec % 86400;
    if (sod < 0) {
        days--;
        sod += 86400;
    }
    int64_t year;
    int month, day;
    tz_civil_from_days(days, &year, &month, &day);
    if (parsed->has_rules) {
        int64_t start = posix_rule_unix(year, &parsed->start, parsed->std_off);
        int64_t end = posix_rule_unix(year, &parsed->end, parsed->dst_off);
        return tz_in_span(unix_sec, start, end);
    }
    int64_t start = tz_add_sat(
        tz_unix_civil(year, 3, tz_nth_wday(year, 3, 0, 2), 2, 0, 0),
        -(int64_t)parsed->std_off);
    int64_t end = tz_add_sat(
        tz_unix_civil(year, 11, tz_nth_wday(year, 11, 0, 1), 2, 0, 0),
        -(int64_t)parsed->dst_off);
    return tz_in_span(unix_sec, start, end);
}

static int posix_rules_offset_at(const posix_tz_parsed_t *parsed, int64_t unix_sec) {
    if (!parsed) return 0;
    if (!parsed->has_dst) return parsed->std_off;
    return posix_rules_dst_active(parsed, unix_sec) ? parsed->dst_off
                                                    : parsed->std_off;
}

static posix_extra_t *posix_find_locked(const neverc_tzdata_zone_t *zone) {
    posix_extra_t *e;
    for (e = g_posix_list; e; e = e->next)
        if (&e->zone == zone) return e;
    return NULL;
}

static posix_extra_t *posix_find_tz_locked(const char *tz) {
    posix_extra_t *e;
    for (e = g_posix_list; e; e = e->next)
        if (e->tzstr && nc_streq(e->tzstr, tz)) return e;
    return NULL;
}

static void posix_extra_free(posix_extra_t *e) {
    if (!e) return;
    free(e->tzstr);
    free(e);
}

static const neverc_tzdata_zone_t *parse_posix_tz(const char *tz) {
    char stdn[16], dstn[16];
    posix_tz_parsed_t parsed;
    if (parse_posix_tz_fields(tz, &parsed, stdn, sizeof(stdn),
                              dstn, sizeof(dstn)) != 0)
        return NULL;
    int std_off = parsed.std_off;
    int dst_off = parsed.dst_off;
    int has_dst = parsed.has_dst;

    posix_lock();
    posix_extra_t *exist = posix_find_tz_locked(tz);
    if (exist) {
        posix_unlock();
        return &exist->zone;
    }
    posix_unlock();

    posix_extra_t *e = (posix_extra_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    size_t n = nc_slen(tz);
    e->tzstr = (char *)malloc(n + 1);
    if (!e->tzstr) {
        free(e);
        return NULL;
    }
    memcpy(e->tzstr, tz, n + 1);
    copy_cstr(e->std, sizeof(e->std), stdn);
    copy_cstr(e->dst, sizeof(e->dst), dstn);
    e->parsed = parsed;
    e->zone.name = e->tzstr;
    e->zone.abbrev = e->std;
    e->zone.abbrev_dst = has_dst ? e->dst : NULL;
    e->zone.utc_offset = std_off;
    e->zone.dst_offset = has_dst ? dst_off : 0;
    e->zone.has_dst = has_dst;
    e->zone.dst_hemi = !has_dst ? 0 : posix_dst_wraps(&parsed) ? 2 : 1;

    posix_lock();
    exist = posix_find_tz_locked(tz);
    if (exist) {
        posix_unlock();
        posix_extra_free(e);
        return &exist->zone;
    }
    e->next = g_posix_list;
    g_posix_list = e;
    posix_unlock();
    return &e->zone;
}

static int tz_dst_active(const neverc_tzdata_zone_t *zone, int64_t unix_sec) {
    if (!zone || !zone->has_dst) return 0;

    int64_t days = unix_sec / 86400;
    int64_t sod = unix_sec % 86400;
    if (sod < 0) {
        days--;
        sod += 86400;
    }
    int64_t year;
    int month, day;
    tz_civil_from_days(days, &year, &month, &day);

    int posix_has_rules = 0;
    posix_rule_t posix_start = {0}, posix_end = {0};
    int utc_off = 0, dst_off = 0;
    posix_extra_t *pe = NULL;
    posix_lock();
    pe = posix_find_locked(zone);
    if (pe) {
        posix_has_rules = pe->parsed.has_rules;
        posix_start = pe->parsed.start;
        posix_end = pe->parsed.end;
        utc_off = pe->zone.utc_offset;
        dst_off = pe->zone.dst_offset;
    }
    posix_unlock();
    if (pe && posix_has_rules) {
        int64_t start = posix_rule_unix(year, &posix_start, utc_off);
        int64_t end = posix_rule_unix(year, &posix_end, dst_off);
        return tz_in_span(unix_sec, start, end);
    }

    /* POSIX without rules: glibc uses US DST. */
    int us = (pe != NULL) ||
             (zone->name && nc_strpfx(zone->name, "America/"));
    int nz = zone->name && (nc_streq(zone->name, "Pacific/Auckland") ||
                            nc_streq(zone->name, "Pacific/Chatham"));
    int cl = zone->name && nc_streq(zone->name, "America/Santiago");

    if (zone->dst_hemi == 2 || nz || cl) {
        int64_t start, end;
        if (nz) {
            /* Auckland 02:00/03:00; Chatham is 45 minutes ahead, so 02:45/03:45. */
            int start_min = 0, end_min = 0;
            if (nc_streq(zone->name, "Pacific/Chatham")) {
                start_min = 45;
                end_min = 45;
            }
            start = tz_add_sat(
                tz_unix_civil(year, 9, tz_last_wday(year, 9, 0), 2, start_min, 0),
                -(int64_t)zone->utc_offset);
            end = tz_add_sat(
                tz_unix_civil(year, 4, tz_nth_wday(year, 4, 0, 1), 3, end_min, 0),
                -(int64_t)zone->dst_offset);
        } else if (cl) {
            /* IANA America/Santiago: first Saturday 24:00 (Sunday 00:00). */
            start = tz_add_sat(
                tz_unix_civil(year, 9, tz_nth_wday(year, 9, 6, 1) + 1, 0, 0, 0),
                -(int64_t)zone->utc_offset);
            end = tz_add_sat(
                tz_unix_civil(year, 4, tz_nth_wday(year, 4, 6, 1) + 1, 0, 0, 0),
                -(int64_t)zone->dst_offset);
        } else {
            /* Australia: first Sunday October 02:00 std -> first Sunday April 03:00 dst */
            start = tz_add_sat(
                tz_unix_civil(year, 10, tz_nth_wday(year, 10, 0, 1), 2, 0, 0),
                -(int64_t)zone->utc_offset);
            end = tz_add_sat(
                tz_unix_civil(year, 4, tz_nth_wday(year, 4, 0, 1), 3, 0, 0),
                -(int64_t)zone->dst_offset);
        }
        return tz_in_span(unix_sec, start, end);
    }

    if (us) {
        int64_t start = tz_add_sat(
            tz_unix_civil(year, 3, tz_nth_wday(year, 3, 0, 2), 2, 0, 0),
            -(int64_t)zone->utc_offset);
        int64_t end = tz_add_sat(
            tz_unix_civil(year, 11, tz_nth_wday(year, 11, 0, 1), 2, 0, 0),
            -(int64_t)zone->dst_offset);
        return tz_in_span(unix_sec, start, end);
    }

    /* EU / default northern: last Sunday March 01:00 UTC to last Sunday October 01:00 UTC */
    int64_t start = tz_unix_civil(year, 3, tz_last_wday(year, 3, 0), 1, 0, 0);
    int64_t end = tz_unix_civil(year, 10, tz_last_wday(year, 10, 0), 1, 0, 0);
    return tz_in_span(unix_sec, start, end);
}

typedef struct tzif_extra {
    neverc_tzdata_zone_t zone;
    int ntx;
    int64_t *when;
    int *off;
    int pre_off;
    int has_posix;
    posix_tz_parsed_t posix;
    char *storage;
    struct tzif_extra *next;
} tzif_extra_t;

static tzif_extra_t *g_tzif_list;
static volatile int32_t g_tzif_lock;

static void tzif_lock(void) {
    int expected = 0;
    while (!__atomic_compare_exchange_n(&g_tzif_lock, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        expected = 0;
        tz_yield();
    }
}

static void tzif_unlock(void) {
    __atomic_store_n(&g_tzif_lock, 0, __ATOMIC_RELEASE);
}

static tzif_extra_t *tzif_find(const neverc_tzdata_zone_t *zone) {
    tzif_extra_t *e;
    tzif_lock();
    for (e = g_tzif_list; e; e = e->next) {
        if (&e->zone == zone) {
            tzif_unlock();
            return e;
        }
    }
    tzif_unlock();
    return NULL;
}

static int tzif_offset_at(const tzif_extra_t *e, int64_t unix_sec) {
    if (!e) return 0;
    /* Go LoadLocationFromTZData synthesizes tx={alpha, index:0} when the
     * file has no transitions, so lookup treats every real instant as
     * "after the last transition" and applies the POSIX extend string. */
    if (e->ntx <= 0 || !e->when || !e->off) {
        if (e->has_posix)
            return posix_rules_offset_at(&e->posix, unix_sec);
        return e->pre_off;
    }
    if (unix_sec < e->when[0])
        return e->pre_off;
    /* Go: extend applies when lo == len(tx)-1, i.e. sec >= last tx. */
    if (e->has_posix && unix_sec >= e->when[e->ntx - 1])
        return posix_rules_offset_at(&e->posix, unix_sec);
    int lo = 0, hi = e->ntx;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (e->when[mid] <= unix_sec)
            lo = mid + 1;
        else
            hi = mid;
    }
    return e->off[lo - 1];
}

int neverc_tzdata_offset_at(const neverc_tzdata_zone_t *zone, int64_t unix_sec) {
    if (!zone) return 0;
    tzif_extra_t *e = tzif_find(zone);
    if (e)
        return tzif_offset_at(e, unix_sec);
    if (!zone->has_dst) return zone->utc_offset;
    return tz_dst_active(zone, unix_sec) ? zone->dst_offset : zone->utc_offset;
}

int neverc_tzdata_offset_for_month(const neverc_tzdata_zone_t *zone, int month) {
    if (!zone) return 0;
    if (!zone->has_dst || month < 1 || month > 12) return zone->utc_offset;

    int hemi = zone->dst_hemi;
    if (hemi == 0) {
        for (int i = 0; i < tz_count; i++) {
            if (zone->name && nc_streq(tz_table[i].name, zone->name)) {
                hemi = tz_table[i].hemi;
                break;
            }
        }
    }
    if (hemi == 0) hemi = 1; /* POSIX / unknown: northern */

    int is_dst = 0;
    if (hemi == 1)
        is_dst = (month >= 3 && month <= 11);
    else
        is_dst = (month >= 10 || month <= 4);

    return is_dst ? zone->dst_offset : zone->utc_offset;
}

/* POSIX TZ may be ":America/New_York" or a zoneinfo path. */
static const char *iana_from_tz_string(const char *tz) {
    if (!tz || !tz[0]) return NULL;
    if (tz[0] == ':') tz++;
    if (!tz[0]) return NULL;
    const char *iana = tz;
    for (const char *p = tz; *p; p++) {
        if (nc_strpfx(p, "zoneinfo/"))
            iana = p + 9;
    }
    return iana[0] ? iana : NULL;
}

static const neverc_tzdata_zone_t *lookup_tz_string(const char *tz) {
    const char *iana = iana_from_tz_string(tz);
    if (!iana) return NULL;
    return neverc_tzdata_lookup(iana);
}

#if !defined(_WIN32)
static const neverc_tzdata_zone_t *lookup_tz_symlink(const char *path) {
    char link[256];
    ssize_t n = readlink(path, link, sizeof(link) - 1);
    if (n <= 0) return NULL;
    link[n] = '\0';
    return lookup_tz_string(link);
}
#endif

const neverc_tzdata_zone_t *neverc_tzdata_local(void) {
    const char *tz = NULL;
#if defined(_MSC_VER)
    char *dup = NULL;
    size_t len = 0;
    if (_dupenv_s(&dup, &len, "TZ") == 0 && dup)
        tz = dup;
#else
    tz = getenv("TZ");
#endif
    const neverc_tzdata_zone_t *z = NULL;
    int tz_set = (tz != NULL);
    if (tz_set && !tz[0]) {
        z = neverc_tzdata_utc();
    } else if (tz_set) {
        z = lookup_tz_string(tz);
#if !defined(_WIN32)
        if (!z) {
            const char *path = (tz[0] == ':') ? tz + 1 : tz;
            if (path[0] == '/')
                z = lookup_tz_symlink(path);
        }
#endif
        if (!z) z = parse_posix_tz(tz);
        /* Set but unrecognized: UTC, do not ignore TZ and use /etc/localtime. */
        if (!z) z = neverc_tzdata_utc();
    }
#if defined(_MSC_VER)
    free(dup);
#endif
    if (z) return z;
#if !defined(_WIN32)
    z = lookup_tz_symlink("/etc/localtime");
    if (z) return z;
#endif
    return neverc_tzdata_utc();
}

static uint32_t tz_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t tz_le16(const uint8_t *p) {
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static uint32_t tz_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int64_t tz_be64(const uint8_t *p) {
    return (int64_t)(((uint64_t)tz_be32(p) << 32) | (uint64_t)tz_be32(p + 4));
}

uint8_t *neverc_tzdata_zip_extract(const uint8_t *zip, size_t zip_len,
                                   const char *name, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!zip || !name || zip_len < 22) return NULL;

    const uint8_t *eocd = zip + zip_len - 22;
    if (tz_le32(eocd) != 0x06054b50u) return NULL;
    uint32_t n = tz_le16(eocd + 10);
    uint32_t cd_size = tz_le32(eocd + 12);
    uint32_t cd_off = tz_le32(eocd + 16);
    /* Zip64 sentinels: fail closed, matching Go's 32-bit zip reader. */
    if (n == 0xFFFFu || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu)
        return NULL;
    if ((size_t)cd_off > zip_len || (size_t)cd_size > zip_len - (size_t)cd_off)
        return NULL;

    const uint8_t *cd = zip + cd_off;
    size_t remain = cd_size;
    size_t want = nc_slen(name);
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (remain < 46) return NULL;
        if (tz_le32(cd) != 0x02014b50u) return NULL;
        uint32_t meth = tz_le16(cd + 10);
        uint32_t uncsize = tz_le32(cd + 24);
        uint32_t namelen = tz_le16(cd + 28);
        uint32_t xlen = tz_le16(cd + 30);
        uint32_t fclen = tz_le16(cd + 32);
        uint32_t off = tz_le32(cd + 42);
        if (namelen > remain - 46 || xlen > remain - 46 - namelen ||
            fclen > remain - 46 - namelen - xlen)
            return NULL;
        const uint8_t *zname = cd + 46;
        cd += 46 + namelen + xlen + fclen;
        remain -= 46 + namelen + xlen + fclen;
        if (namelen != want || memcmp(zname, name, namelen) != 0)
            continue;
        if (meth != 0) return NULL;
        if ((size_t)off > zip_len || zip_len - (size_t)off < 30)
            return NULL;
        const uint8_t *lh = zip + off;
        if (tz_le32(lh) != 0x04034b50u) return NULL;
        if (tz_le16(lh + 8) != meth) return NULL;
        uint32_t lname = tz_le16(lh + 26);
        uint32_t lxlen = tz_le16(lh + 28);
        if (lname != namelen) return NULL;
        if (lname > zip_len - (size_t)off - 30 ||
            lxlen > zip_len - (size_t)off - 30 - lname)
            return NULL;
        if (memcmp(lh + 30, name, namelen) != 0) return NULL;
        size_t data_off = (size_t)off + 30 + lname + lxlen;
        if (data_off > zip_len || (size_t)uncsize > zip_len - data_off)
            return NULL;
        uint8_t *buf = (uint8_t *)malloc(uncsize ? uncsize : 1);
        if (!buf) return NULL;
        if (uncsize)
            memcpy(buf, zip + data_off, uncsize);
        if (out_len) *out_len = uncsize;
        return buf;
    }
    return NULL;
}

typedef struct {
    const uint8_t *p;
    size_t n;
    int err;
} tz_cur_t;

static const uint8_t *tz_cur_read(tz_cur_t *c, size_t n) {
    if (c->err || c->n < n) {
        c->err = 1;
        return NULL;
    }
    const uint8_t *p = c->p;
    c->p += n;
    c->n -= n;
    return p;
}

static int tz_cur_counts(tz_cur_t *c, uint32_t n[6]) {
    int i;
    for (i = 0; i < 6; i++) {
        const uint8_t *p = tz_cur_read(c, 4);
        if (!p) return -1;
        n[i] = tz_be32(p);
    }
    return 0;
}

/* Go Location.lookupFirstZone / tzcode localtime.c: offset before tx[0]. */
static int tzif_lookup_first_zone(const uint8_t *zdata, uint32_t nzone,
                                  const uint8_t *txidx, uint32_t ntime) {
    int first_used = 0;
    uint32_t ti;
    for (ti = 0; ti < ntime; ti++) {
        if (txidx[ti] == 0) {
            first_used = 1;
            break;
        }
    }
    if (!first_used)
        return 0;

    /* First zone is used. If the first transition is to DST, pick the
     * closest preceding non-DST zone. */
    if (ntime > 0) {
        if ((uint32_t)txidx[0] >= nzone)
            return 0;
        if (zdata[(size_t)txidx[0] * 6 + 4] != 0) {
            int zi;
            for (zi = (int)txidx[0] - 1; zi >= 0; zi--) {
                if (zdata[(size_t)zi * 6 + 4] == 0)
                    return zi;
            }
        }
    }

    uint32_t zi;
    for (zi = 0; zi < nzone; zi++) {
        if (zdata[(size_t)zi * 6 + 4] == 0)
            return (int)zi;
    }
    return 0;
}

neverc_tzdata_zone_t *neverc_tzdata_load_tzif(const char *name,
                                              const uint8_t *data, size_t len) {
    if (!data || len < 20) return NULL;
    tz_cur_t c = {data, len, 0};
    const uint8_t *magic = tz_cur_read(&c, 4);
    if (!magic || memcmp(magic, "TZif", 4) != 0) return NULL;
    const uint8_t *verpad = tz_cur_read(&c, 16);
    if (!verpad) return NULL;
    int version = 1;
    if (verpad[0] == '2' || verpad[0] == '3' || verpad[0] == '4') version = 2;
    else if (verpad[0] != 0) return NULL;

    uint32_t n[6];
    if (tz_cur_counts(&c, n) != 0) return NULL;
    /* NUTCLocal, NStdWall, NLeap, NTime, NZone, NChar */
    uint32_t ntime = n[3], nzone = n[4], nchar = n[5];
    uint32_t nleap = n[2], nstd = n[1], nutc = n[0];
    if (nzone == 0 || ntime > (uint32_t)INT_MAX) return NULL;

    if (version > 1) {
        size_t skip = 20;
        if ((size_t)ntime > (SIZE_MAX - skip) / 5) return NULL;
        skip += (size_t)ntime * 5;
        if ((size_t)nzone > (SIZE_MAX - skip) / 6) return NULL;
        skip += (size_t)nzone * 6;
        if ((size_t)nleap > (SIZE_MAX - skip) / 8) return NULL;
        skip += (size_t)nleap * 8;
        if ((size_t)nchar > SIZE_MAX - skip ||
            (size_t)nstd > SIZE_MAX - skip - (size_t)nchar ||
            (size_t)nutc > SIZE_MAX - skip - (size_t)nchar - (size_t)nstd)
            return NULL;
        skip += (size_t)nchar + (size_t)nstd + (size_t)nutc;
        if (!tz_cur_read(&c, skip)) return NULL;
        if (tz_cur_counts(&c, n) != 0) return NULL;
        ntime = n[3];
        nzone = n[4];
        nchar = n[5];
        nleap = n[2];
        nstd = n[1];
        nutc = n[0];
        if (nzone == 0 || ntime > (uint32_t)INT_MAX) return NULL;
    }

    int is64 = version > 1;
    size_t tsize = is64 ? 8 : 4;
    if ((size_t)ntime > SIZE_MAX / tsize ||
        (size_t)nzone > SIZE_MAX / 6 ||
        (size_t)nleap > SIZE_MAX / (tsize + 4))
        return NULL;

    const uint8_t *times = tz_cur_read(&c, (size_t)ntime * tsize);
    const uint8_t *txidx = tz_cur_read(&c, ntime);
    const uint8_t *zdata = tz_cur_read(&c, (size_t)nzone * 6);
    const uint8_t *abbrev = tz_cur_read(&c, nchar);
    if (c.err) return NULL;
    size_t leap_sz = (size_t)nleap * (tsize + 4);
    tz_cur_read(&c, leap_sz);
    tz_cur_read(&c, nstd);
    tz_cur_read(&c, nutc);
    if (c.err) return NULL;

    posix_tz_parsed_t footer;
    int has_footer = 0;
    memset(&footer, 0, sizeof(footer));
    if (version > 1 && c.n >= 2 && c.p[0] == '\n') {
        const uint8_t *fs = c.p + 1;
        size_t rem = c.n - 1;
        const uint8_t *nl = (const uint8_t *)memchr(fs, '\n', rem);
        if (nl && nl > fs && (size_t)(nl - fs) < 96) {
            char tz[96];
            memcpy(tz, fs, (size_t)(nl - fs));
            tz[nl - fs] = '\0';
            {
                char stdn[16], dstn[16];
                if (parse_posix_tz_fields(tz, &footer, stdn, sizeof(stdn),
                                          dstn, sizeof(dstn)) == 0)
                    has_footer = 1;
            }
        }
    }

    int std_off = 0, dst_off = 0, has_dst = 0, got_std = 0, got_dst = 0;
    const char *std_ab = "UTC", *dst_ab = NULL;
    size_t slen = 3, dlen = 0;
    uint32_t zi;
    for (zi = 0; zi < nzone; zi++) {
        int off = (int)(int32_t)tz_be32(zdata + (size_t)zi * 6);
        int isdst = zdata[(size_t)zi * 6 + 4] != 0;
        uint8_t ab = zdata[(size_t)zi * 6 + 5];
        size_t alen = 0;
        if (tz_chararray_strlen(abbrev, nchar, ab, &alen) != 0)
            return NULL;
        const char *an = (const char *)(abbrev + ab);
        if (!isdst && !got_std) {
            std_off = off;
            std_ab = an;
            slen = alen;
            got_std = 1;
        } else if (isdst && !got_dst) {
            dst_off = off;
            dst_ab = an;
            dlen = alen;
            got_dst = 1;
            has_dst = 1;
        }
    }
    if (!got_std) {
        std_off = (int)(int32_t)tz_be32(zdata);
        uint8_t ab = zdata[5];
        if (tz_chararray_strlen(abbrev, nchar, ab, &slen) != 0)
            return NULL;
        std_ab = (const char *)(abbrev + ab);
    }

    tzif_extra_t *e = (tzif_extra_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    {
        int first = tzif_lookup_first_zone(zdata, nzone, txidx, ntime);
        e->pre_off = (int)(int32_t)tz_be32(zdata + (size_t)first * 6);
    }
    if (has_footer) {
        e->has_posix = 1;
        e->posix = footer;
    }
    if (ntime > 0) {
        /* v1 times are 4 bytes, but we store int64_t + int arrays. The
         * earlier tsize check is not enough on 32-bit hosts. */
        if ((size_t)ntime > SIZE_MAX / sizeof(int64_t) ||
            (size_t)ntime > SIZE_MAX / sizeof(int)) {
            free(e);
            return NULL;
        }
        e->when = (int64_t *)malloc((size_t)ntime * sizeof(int64_t));
        e->off = (int *)malloc((size_t)ntime * sizeof(int));
        if (!e->when || !e->off) {
            free(e->when);
            free(e->off);
            free(e);
            return NULL;
        }
        uint32_t ti;
        for (ti = 0; ti < ntime; ti++) {
            int64_t when;
            if (is64)
                when = tz_be64(times + (size_t)ti * 8);
            else
                when = (int64_t)(int32_t)tz_be32(times + (size_t)ti * 4);
            uint8_t idx = txidx[ti];
            if ((uint32_t)idx >= nzone) {
                free(e->when);
                free(e->off);
                free(e);
                return NULL;
            }
            e->when[ti] = when;
            e->off[ti] = (int)(int32_t)tz_be32(zdata + (size_t)idx * 6);
        }
        e->ntx = (int)ntime;
    }

    size_t nlen = name ? nc_slen(name) : 0;
    if (!dst_ab)
        dlen = 0;
    if (nlen > SIZE_MAX - slen - dlen - 3) {
        free(e->when);
        free(e->off);
        free(e);
        return NULL;
    }
    e->storage = (char *)malloc(nlen + slen + dlen + 3);
    if (!e->storage) {
        free(e->when);
        free(e->off);
        free(e);
        return NULL;
    }
    char *p = e->storage;
    if (name)
        memcpy(p, name, nlen);
    p[nlen] = '\0';
    e->zone.name = p;
    p += nlen + 1;
    memcpy(p, std_ab, slen);
    p[slen] = '\0';
    e->zone.abbrev = p;
    p += slen + 1;
    if (dst_ab) {
        memcpy(p, dst_ab, dlen);
        p[dlen] = '\0';
        e->zone.abbrev_dst = p;
    }
    e->zone.utc_offset = std_off;
    e->zone.dst_offset = has_dst ? dst_off : 0;
    e->zone.has_dst = has_dst;
    /* Footer wrap-around rules (Oct→Apr, J260→J90) are southern. With no
     * footer, leave hemi 0 so offset_for_month can fill from the named table. */
    if (!has_dst)
        e->zone.dst_hemi = 0;
    else if (has_footer && posix_dst_wraps(&footer))
        e->zone.dst_hemi = 2;
    else if (has_footer)
        e->zone.dst_hemi = 1;
    else
        e->zone.dst_hemi = 0;

    tzif_lock();
    e->next = g_tzif_list;
    g_tzif_list = e;
    tzif_unlock();
    return &e->zone;
}

neverc_tzdata_zone_t *neverc_tzdata_load_from_zip(const uint8_t *zip,
                                                  size_t zip_len,
                                                  const char *name) {
    if (!tz_iana_name_ok(name))
        return NULL;
    size_t n = 0;
    uint8_t *data = neverc_tzdata_zip_extract(zip, zip_len, name, &n);
    if (!data) return NULL;
    neverc_tzdata_zone_t *z = neverc_tzdata_load_tzif(name, data, n);
    free(data);
    return z;
}

void neverc_tzdata_zone_free(neverc_tzdata_zone_t *zone) {
    if (!zone) return;
    for (int i = 0; i < tz_count; i++) {
        if (zone == &g_zones[i]) return;
    }
    posix_lock();
    if (posix_find_locked(zone)) {
        posix_unlock();
        return;
    }
    posix_unlock();

    tzif_lock();
    tzif_extra_t **pp = &g_tzif_list;
    while (*pp) {
        if (&(*pp)->zone == zone) {
            tzif_extra_t *e = *pp;
            *pp = e->next;
            tzif_unlock();
            free(e->when);
            free(e->off);
            free(e->storage);
            free(e);
            return;
        }
        pp = &(*pp)->next;
    }
    tzif_unlock();
    free((void *)zone->name);
    free(zone);
}
