#include "neverc/std/time_tzdata.h"
#include <stdint.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <sched.h>
#include <unistd.h>
#endif

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
        while (__atomic_load_n(&g_zones_init, __ATOMIC_ACQUIRE) != 2) {
#if !defined(_WIN32)
            sched_yield();
#endif
        }
        return;
    }
    for (int i = 0; i < tz_count; i++)
        fill_zone(&g_zones[i], &tz_table[i]);
    __atomic_store_n(&g_zones_init, 2, __ATOMIC_RELEASE);
}

const neverc_tzdata_zone_t *neverc_tzdata_lookup(const char *name) {
    if (!name) return NULL;
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

static int64_t tz_unix_civil(int64_t y, int m, int d, int h, int mi, int s) {
    return tz_days_from_civil(y, m, d) * 86400 +
           (int64_t)h * 3600 + (int64_t)mi * 60 + s;
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

typedef struct {
    int month, week, wday, at_sec;
} posix_rule_t;

static neverc_tzdata_zone_t g_posix_zone;
static char g_posix_tzstr[96];
static char g_posix_std[16];
static char g_posix_dst[16];
static posix_rule_t g_posix_start, g_posix_end;
static int g_posix_has_rules;

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
    hh = *s++ - '0';
    if (*s >= '0' && *s <= '9') hh = hh * 10 + (*s++ - '0');
    if (hh > 24) return -1;
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

static int parse_posix_rule(const char **p, posix_rule_t *r) {
    if (**p != 'M') return -1;
    (*p)++;
    int month, week, wday;
    if (posix_uint(p, 2, &month) != 0 || month < 1 || month > 12) return -1;
    if (**p != '.') return -1;
    (*p)++;
    if (posix_uint(p, 1, &week) != 0 || week < 1 || week > 5) return -1;
    if (**p != '.') return -1;
    (*p)++;
    if (posix_uint(p, 1, &wday) != 0 || wday > 6) return -1;
    int at = 2 * 3600;
    if (**p == '/') {
        (*p)++;
        int hh = 0, mm = 0, ss = 0;
        if (posix_uint(p, 2, &hh) != 0) return -1;
        if (**p == ':') {
            (*p)++;
            if (posix_uint(p, 2, &mm) != 0) return -1;
            if (**p == ':') {
                (*p)++;
                if (posix_uint(p, 2, &ss) != 0) return -1;
            }
        }
        if (hh > 24 || mm > 59 || ss > 59) return -1;
        at = hh * 3600 + mm * 60 + ss;
    }
    r->month = month;
    r->week = week;
    r->wday = wday;
    r->at_sec = at;
    return 0;
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
    int dom = tz_nth_wday(year, r->month, r->wday, r->week);
    return tz_unix_civil(year, r->month, dom, 0, 0, 0) + r->at_sec - offset;
}

static const neverc_tzdata_zone_t *parse_posix_tz(const char *tz) {
    if (!tz || !tz[0] || tz[0] == ':' || tz[0] == '/') return NULL;
    const char *p = tz;
    char stdn[16], dstn[16];
    dstn[0] = '\0';
    int std_off = 0, dst_off = 0;
    if (parse_posix_tz_name(&p, stdn, sizeof(stdn)) != 0) return NULL;
    if (parse_posix_offset(&p, &std_off) != 0) return NULL;
    int has_dst = 0;
    posix_rule_t start = {0, 0, 0, 0}, end = {0, 0, 0, 0};
    int has_rules = 0;
    if (*p && *p != ',') {
        if (parse_posix_tz_name(&p, dstn, sizeof(dstn)) != 0) return NULL;
        has_dst = 1;
        dst_off = std_off + 3600;
        if (*p && *p != ',') {
            if (parse_posix_offset(&p, &dst_off) != 0) return NULL;
        }
    }
    if (*p == ',') {
        p++;
        if (parse_posix_rule(&p, &start) != 0) return NULL;
        if (*p != ',') return NULL;
        p++;
        if (parse_posix_rule(&p, &end) != 0) return NULL;
        has_rules = 1;
    }
    if (*p != '\0') return NULL;

    copy_cstr(g_posix_tzstr, sizeof(g_posix_tzstr), tz);
    copy_cstr(g_posix_std, sizeof(g_posix_std), stdn);
    copy_cstr(g_posix_dst, sizeof(g_posix_dst), dstn);
    g_posix_zone.name = g_posix_tzstr;
    g_posix_zone.abbrev = g_posix_std;
    g_posix_zone.abbrev_dst = has_dst ? g_posix_dst : NULL;
    g_posix_zone.utc_offset = std_off;
    g_posix_zone.dst_offset = has_dst ? dst_off : 0;
    g_posix_zone.has_dst = has_dst;
    g_posix_zone.dst_hemi = has_dst ? 1 : 0;
    g_posix_has_rules = has_rules;
    g_posix_start = start;
    g_posix_end = end;
    return &g_posix_zone;
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

    if (zone == &g_posix_zone && g_posix_has_rules) {
        int64_t start = posix_rule_unix(year, &g_posix_start, zone->utc_offset);
        int64_t end = posix_rule_unix(year, &g_posix_end, zone->dst_offset);
        return tz_in_span(unix_sec, start, end);
    }

    /* POSIX without rules: glibc uses US DST. */
    int us = (zone == &g_posix_zone) ||
             (zone->name && nc_strpfx(zone->name, "America/"));
    int nz = zone->name && (nc_streq(zone->name, "Pacific/Auckland") ||
                            nc_streq(zone->name, "Pacific/Chatham"));
    int cl = zone->name && nc_streq(zone->name, "America/Santiago");

    if (zone->dst_hemi == 2 || nz || cl) {
        int64_t start, end;
        if (nz) {
            start = tz_unix_civil(year, 9, tz_last_wday(year, 9, 0), 2, 0, 0) -
                    zone->utc_offset;
            end = tz_unix_civil(year, 4, tz_nth_wday(year, 4, 0, 1), 3, 0, 0) -
                  zone->dst_offset;
        } else if (cl) {
            start = tz_unix_civil(year, 9, tz_nth_wday(year, 9, 6, 1), 0, 0, 0) -
                    zone->utc_offset;
            end = tz_unix_civil(year, 4, tz_nth_wday(year, 4, 6, 1), 0, 0, 0) -
                  zone->dst_offset;
        } else {
            /* Australia: first Sunday October 02:00 std -> first Sunday April 03:00 dst */
            start = tz_unix_civil(year, 10, tz_nth_wday(year, 10, 0, 1), 2, 0, 0) -
                    zone->utc_offset;
            end = tz_unix_civil(year, 4, tz_nth_wday(year, 4, 0, 1), 3, 0, 0) -
                  zone->dst_offset;
        }
        return tz_in_span(unix_sec, start, end);
    }

    if (us) {
        int64_t start =
            tz_unix_civil(year, 3, tz_nth_wday(year, 3, 0, 2), 2, 0, 0) -
            zone->utc_offset;
        int64_t end =
            tz_unix_civil(year, 11, tz_nth_wday(year, 11, 0, 1), 2, 0, 0) -
            zone->dst_offset;
        return tz_in_span(unix_sec, start, end);
    }

    /* EU / default northern: last Sunday March 01:00 UTC to last Sunday October 01:00 UTC */
    int64_t start = tz_unix_civil(year, 3, tz_last_wday(year, 3, 0), 1, 0, 0);
    int64_t end = tz_unix_civil(year, 10, tz_last_wday(year, 10, 0), 1, 0, 0);
    return tz_in_span(unix_sec, start, end);
}

int neverc_tzdata_offset_at(const neverc_tzdata_zone_t *zone, int64_t unix_sec) {
    if (!zone) return 0;
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
    const neverc_tzdata_zone_t *z = neverc_tzdata_lookup(iana);
    if (z) return z;
    return neverc_tzdata_lookup_abbrev(iana);
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
