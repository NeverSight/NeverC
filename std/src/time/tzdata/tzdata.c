#include "neverc/std/time_tzdata.h"
#include <stdlib.h>

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
    {"America/Sao_Paulo",       "BRT",  "BRST", -10800, -7200,  S},
    {"America/Argentina/Buenos_Aires","ART",NULL,-10800, 0,      0},
    {"America/Caracas",         "VET",  NULL,   -16200, 0,      0},

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
    {"Asia/Tehran",             "IRST", "IRDT", 12600,  16200,  N},
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
    {"Asia/Almaty",             "ALMT", NULL,   21600,  0,      0},
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
    {"Pacific/Fiji",            "FJT",  "FJST", 43200,  46800,  S},
    {"Pacific/Guam",            "ChST", NULL,   36000,  0,      0},
    {"Pacific/Chatham",         "CHAST","CHADT",45900,  49500,  S},
    {"Pacific/Tongatapu",       "TOT",  NULL,   46800,  0,      0},
    {"Pacific/Samoa",           "SST",  NULL,   -39600, 0,      0},
    {"Pacific/Midway",          "SST",  NULL,   -39600, 0,      0},

    /* Africa */
    {"Africa/Cairo",            "EET",  NULL,   7200,   0,      0},
    {"Africa/Lagos",            "WAT",  NULL,   3600,   0,      0},
    {"Africa/Johannesburg",     "SAST", NULL,   7200,   0,      0},
    {"Africa/Nairobi",          "EAT",  NULL,   10800,  0,      0},
    {"Africa/Casablanca",       "WET",  "WEST", 0,      3600,   N},
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

static const neverc_tzdata_zone_t *entry_to_zone(const tz_entry_t *e) {
    /* Cast is safe: neverc_tzdata_zone_t layout matches first fields */
    static neverc_tzdata_zone_t z;
    z.name = e->name;
    z.abbrev = e->abbr;
    z.abbrev_dst = e->abbr_dst;
    z.utc_offset = e->off;
    z.dst_offset = e->off_dst;
    z.has_dst = (e->hemi != 0) ? 1 : 0;
    return &z;
}

/* Thread-local storage for returned zone structs */
static neverc_tzdata_zone_t g_zones[sizeof(tz_table) / sizeof(tz_table[0])];
static int g_zones_init = 0;

static void init_zones(void) {
    if (g_zones_init) return;
    for (int i = 0; i < tz_count; i++) {
        g_zones[i].name = tz_table[i].name;
        g_zones[i].abbrev = tz_table[i].abbr;
        g_zones[i].abbrev_dst = tz_table[i].abbr_dst;
        g_zones[i].utc_offset = tz_table[i].off;
        g_zones[i].dst_offset = tz_table[i].off_dst;
        g_zones[i].has_dst = (tz_table[i].hemi != 0) ? 1 : 0;
    }
    g_zones_init = 1;
}

const neverc_tzdata_zone_t *neverc_tzdata_lookup(const char *name) {
    init_zones();
    for (int i = 0; i < tz_count; i++) {
        if (nc_streq(tz_table[i].name, name))
            return &g_zones[i];
    }
    return NULL;
}

const neverc_tzdata_zone_t *neverc_tzdata_lookup_abbrev(const char *abbrev) {
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
    neverc_tzdata_zone_t *z = (neverc_tzdata_zone_t *)calloc(1, sizeof(*z));
    if (!z) return NULL;
    size_t nlen = nc_slen(name);
    char *n = (char *)malloc(nlen + 1);
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

const neverc_tzdata_zone_t *neverc_tzdata_local(void) {
    /* Try TZ environment variable first */
    const char *tz = NULL;
#if defined(_MSC_VER)
    char buf[128];
    size_t len;
    if (_dupenv_s(&buf[0], &len, "TZ") == 0 && len > 0)
        tz = buf;
#else
    tz = getenv("TZ");
#endif
    if (tz && tz[0]) {
        const neverc_tzdata_zone_t *z = neverc_tzdata_lookup(tz);
        if (z) return z;
        z = neverc_tzdata_lookup_abbrev(tz);
        if (z) return z;
    }
    return neverc_tzdata_utc();
}

int neverc_tzdata_offset_for_month(const neverc_tzdata_zone_t *zone, int month) {
    if (!zone || !zone->has_dst) return zone ? zone->utc_offset : 0;

    /* Look up hemisphere from original table data */
    int hemi = 0;
    for (int i = 0; i < tz_count; i++) {
        if (nc_streq(tz_table[i].name, zone->name)) {
            hemi = tz_table[i].hemi;
            break;
        }
    }
    if (hemi == 0) return zone->utc_offset;

    int is_dst = 0;
    if (hemi == 1) {
        /* Northern hemisphere DST: March through November */
        is_dst = (month >= 3 && month <= 11);
    } else {
        /* Southern hemisphere DST: October through April (next year) */
        is_dst = (month >= 10 || month <= 4);
    }

    return is_dst ? zone->dst_offset : zone->utc_offset;
}
