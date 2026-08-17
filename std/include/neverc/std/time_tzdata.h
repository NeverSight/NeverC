#ifndef NEVERC_TIME_TZDATA_H
#define NEVERC_TIME_TZDATA_H

/*
 * NeverC time/tzdata — embedded timezone data (mirrors Go time/tzdata).
 *
 * Provides a built-in table of IANA timezone names, UTC offsets, and
 * abbreviations so programs don't require system tzdata files.
 * Covers 100+ common timezones across all regions.
 *
 * Cross-platform: mac/ios/linux/android/windows.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;       /* IANA name, e.g. "America/New_York" */
    const char *abbrev;     /* standard abbreviation, e.g. "EST" */
    const char *abbrev_dst; /* DST abbreviation, e.g. "EDT" (NULL if no DST) */
    int         utc_offset; /* standard offset from UTC in seconds */
    int         dst_offset; /* DST offset from UTC in seconds (0 if no DST) */
    int         has_dst;    /* 1 if timezone observes DST */
    int         dst_hemi;   /* 1 northern, 2 southern, 0 none */
} neverc_tzdata_zone_t;

/* Look up a timezone by IANA name. Returns NULL if not found. */
const neverc_tzdata_zone_t *neverc_tzdata_lookup(const char *name);

/* Look up a timezone by abbreviation (e.g. "PST"). Returns first match. */
const neverc_tzdata_zone_t *neverc_tzdata_lookup_abbrev(const char *abbrev);

/* Return a fixed-offset timezone (allocated, caller frees). */
neverc_tzdata_zone_t *neverc_tzdata_fixed_zone(const char *name, int offset_sec);

/* Get the total number of known timezones. */
int neverc_tzdata_count(void);

/* Get a timezone by index (0-based). Returns NULL if out of range. */
const neverc_tzdata_zone_t *neverc_tzdata_at(int index);

/* List all timezone names matching a prefix (e.g. "America/").
 * Returns count; fills names array up to max_names. */
int neverc_tzdata_list(const char *prefix, const char **names, int max_names);

/* Get UTC timezone. */
const neverc_tzdata_zone_t *neverc_tzdata_utc(void);

/* Get the local timezone (from TZ env var or system default). */
const neverc_tzdata_zone_t *neverc_tzdata_local(void);

/* Convert UTC seconds to local time offset for a given zone.
 * Accounts for DST based on month (simplified: DST Mar-Nov in northern,
 * Oct-Apr in southern hemisphere). */
int neverc_tzdata_offset_for_month(const neverc_tzdata_zone_t *zone, int month);

/* Offset from UTC in seconds at unix_sec. Uses US/EU/AU/NZ transition
 * rules (not a full tzif database). */
int neverc_tzdata_offset_at(const neverc_tzdata_zone_t *zone, int64_t unix_sec);

#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */
#ifdef __neverc__
struct __neverc_std_time_tzdata_t { char __tag; };
extern struct __neverc_std_time_tzdata_t __neverc_mod_tzdata;
#endif

#endif /* NEVERC_TIME_TZDATA_H */
