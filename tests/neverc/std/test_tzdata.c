#include "neverc/std/time_tzdata.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got == NULL && expected == NULL) { tests_passed++; return; }
    if (got == NULL || expected == NULL) {
        tests_failed++;
        printf("  FAIL: %s: got %s, expected %s\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
        return;
    }
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

static void check_null(const char *name, const void *ptr) {
    tests_run++;
    if (!ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: expected NULL\n", name); }
}

/* ===== Concurrent first-use init ===== */

#define TZ_RACE_THREADS 8
#define TZ_RACE_ITERS   200

static int tz_race_body(void) {
    static const char *names[] = {
        "UTC", "America/New_York", "Europe/London", "Asia/Tokyo",
        "Australia/Sydney", "Pacific/Auckland", "Africa/Cairo", "GMT"
    };
    const int nnames = (int)(sizeof(names) / sizeof(names[0]));
    for (int i = 0; i < TZ_RACE_ITERS; i++) {
        const neverc_tzdata_zone_t *z = neverc_tzdata_lookup(names[i % nnames]);
        if (!z || !z->name || !z->abbrev) return 1;
        if (!neverc_tzdata_lookup_abbrev(z->abbrev)) return 1;
        (void)neverc_tzdata_offset_for_month(z, (i % 12) + 1);
        if (!neverc_tzdata_at(i % neverc_tzdata_count())) return 1;
        if (!neverc_tzdata_utc()) return 1;
    }
    return 0;
}

#if defined(_WIN32)
static DWORD WINAPI tz_race_worker(LPVOID arg) {
    (void)arg;
    return (DWORD)tz_race_body();
}
#else
static void *tz_race_worker(void *arg) {
    (void)arg;
    return (void *)(intptr_t)tz_race_body();
}
#endif

static void test_concurrent_init(void) {
    printf("[concurrent_init]\n");
    int failed = 0;
#if defined(_WIN32)
    HANDLE threads[TZ_RACE_THREADS];
    for (int i = 0; i < TZ_RACE_THREADS; i++)
        threads[i] = CreateThread(NULL, 0, tz_race_worker, NULL, 0, NULL);
    WaitForMultipleObjects(TZ_RACE_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < TZ_RACE_THREADS; i++) {
        DWORD code = 1;
        if (!threads[i] || !GetExitCodeThread(threads[i], &code) || code != 0)
            failed = 1;
        if (threads[i]) CloseHandle(threads[i]);
    }
#else
    pthread_t threads[TZ_RACE_THREADS];
    int started = 0;
    for (int i = 0; i < TZ_RACE_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, tz_race_worker, NULL) != 0) {
            failed = 1;
            break;
        }
        started++;
    }
    for (int i = 0; i < started; i++) {
        void *ret = (void *)(intptr_t)1;
        pthread_join(threads[i], &ret);
        if (ret) failed = 1;
    }
#endif
    check_int("concurrent lookup during init", failed, 0);
}

/* ===== Lookup ===== */

static void test_lookup(void) {
    printf("[lookup]\n");
    const neverc_tzdata_zone_t *z;

    z = neverc_tzdata_lookup("UTC");
    check_not_null("UTC", z);
    check_str("UTC name", z ? z->name : NULL, "UTC");
    check_str("UTC abbrev", z ? z->abbrev : NULL, "UTC");
    check_int("UTC offset", z ? z->utc_offset : -1, 0);
    check_int("UTC no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("America/New_York");
    check_not_null("New_York", z);
    check_str("NY abbrev", z ? z->abbrev : NULL, "EST");
    check_str("NY dst abbrev", z ? z->abbrev_dst : NULL, "EDT");
    check_int("NY offset", z ? z->utc_offset : 0, -18000);
    check_int("NY dst offset", z ? z->dst_offset : 0, -14400);
    check_int("NY has dst", z ? z->has_dst : 0, 1);

    z = neverc_tzdata_lookup("Asia/Tokyo");
    check_not_null("Tokyo", z);
    check_int("Tokyo offset", z ? z->utc_offset : 0, 32400);
    check_int("Tokyo no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("Asia/Shanghai");
    check_not_null("Shanghai", z);
    check_int("Shanghai offset", z ? z->utc_offset : 0, 28800);

    z = neverc_tzdata_lookup("Europe/London");
    check_not_null("London", z);
    check_str("London abbrev", z ? z->abbrev : NULL, "GMT");
    check_str("London dst", z ? z->abbrev_dst : NULL, "BST");
    check_int("London has dst", z ? z->has_dst : 0, 1);

    z = neverc_tzdata_lookup("Europe/Kyiv");
    check_not_null("Kyiv", z);
    check_int("Kyiv offset", z ? z->utc_offset : 0, 7200);
    check_int("Kyiv dst hemi north", neverc_tzdata_dst_hemisphere(z), 1);

    z = neverc_tzdata_lookup("Europe/Kiev");
    check_not_null("Kiev alias", z);

    z = neverc_tzdata_lookup("Australia/Sydney");
    check_not_null("Sydney", z);
    check_int("Sydney offset", z ? z->utc_offset : 0, 36000);
    check_int("Sydney has dst", z ? z->has_dst : 0, 1);

    z = neverc_tzdata_lookup("Nonexistent/Zone");
    check_null("nonexistent", z);

    z = neverc_tzdata_lookup("EST");
    check_not_null("factory EST", z);
    check_str("factory EST name", z ? z->name : NULL, "EST");
    check_int("factory EST offset", z ? z->utc_offset : 0, -18000);
    check_int("factory EST no dst", z ? z->has_dst : 1, 0);
    check_int("factory EST summer",
              neverc_tzdata_offset_at(z, 1719835200LL), -18000);

    z = neverc_tzdata_lookup("MST");
    check_not_null("factory MST", z);
    check_str("factory MST name", z ? z->name : NULL, "MST");
    check_int("factory MST offset", z ? z->utc_offset : 0, -25200);
    check_int("factory MST no dst", z ? z->has_dst : 1, 0);
    check_int("factory MST summer",
              neverc_tzdata_offset_at(z, 1719835200LL), -25200);

    z = neverc_tzdata_lookup("HST");
    check_not_null("factory HST", z);
    check_str("factory HST name", z ? z->name : NULL, "HST");
    check_int("factory HST offset", z ? z->utc_offset : 0, -36000);
    check_int("factory HST no dst", z ? z->has_dst : 1, 0);

    /* Go LoadLocation rejects ".." and a leading slash or backslash. */
    check_null("lookup empty", neverc_tzdata_lookup(""));
    check_null("lookup dot-dot", neverc_tzdata_lookup("America/../UTC"));
    check_null("lookup leading slash", neverc_tzdata_lookup("/UTC"));
    check_null("lookup leading backslash", neverc_tzdata_lookup("\\UTC"));
    check_not_null("lookup UTC still works", neverc_tzdata_lookup("UTC"));
}

/* ===== Lookup by abbreviation ===== */

static void test_lookup_abbrev(void) {
    printf("[lookup_abbrev]\n");
    const neverc_tzdata_zone_t *z;

    z = neverc_tzdata_lookup_abbrev("PST");
    check_not_null("PST", z);
    check_str("PST name", z ? z->name : NULL, "America/Los_Angeles");

    z = neverc_tzdata_lookup_abbrev("JST");
    check_not_null("JST", z);

    z = neverc_tzdata_lookup_abbrev("EDT");
    check_not_null("EDT", z);

    z = neverc_tzdata_lookup_abbrev("XXXXX");
    check_null("unknown abbrev", z);
}

/* ===== Count / At ===== */

static void test_count(void) {
    printf("[count]\n");
    int cnt = neverc_tzdata_count();
    check_int("count > 100", cnt > 100, 1);

    const neverc_tzdata_zone_t *z = neverc_tzdata_at(0);
    check_not_null("at(0)", z);

    z = neverc_tzdata_at(cnt - 1);
    check_not_null("at(last)", z);

    z = neverc_tzdata_at(cnt);
    check_null("at(out of range)", z);

    z = neverc_tzdata_at(-1);
    check_null("at(-1)", z);
}

/* ===== List ===== */

static void test_list(void) {
    printf("[list]\n");
    const char *names[200];

    int cnt = neverc_tzdata_list("America/", names, 200);
    check_int("america count > 10", cnt > 10, 1);

    cnt = neverc_tzdata_list("Europe/", names, 200);
    check_int("europe count > 10", cnt > 10, 1);

    cnt = neverc_tzdata_list("Asia/", names, 200);
    check_int("asia count > 10", cnt > 10, 1);

    cnt = neverc_tzdata_list("Nonexistent/", names, 200);
    check_int("nonexistent 0", cnt, 0);

    cnt = neverc_tzdata_list("", names, 200);
    check_int("all == count", cnt, neverc_tzdata_count());
}

/* ===== UTC ===== */

static void test_utc(void) {
    printf("[utc]\n");
    const neverc_tzdata_zone_t *z = neverc_tzdata_utc();
    check_not_null("utc", z);
    check_str("utc name", z ? z->name : NULL, "UTC");
    check_int("utc offset", z ? z->utc_offset : -1, 0);
}

/* ===== Fixed zone ===== */

static void test_fixed_zone(void) {
    printf("[fixed_zone]\n");
    neverc_tzdata_zone_t *z = neverc_tzdata_fixed_zone("UTC+8", 28800);
    check_not_null("fixed", z);
    check_int("fixed offset", z ? z->utc_offset : 0, 28800);
    check_int("fixed no dst", z ? z->has_dst : -1, 0);
    if (z) { free((void *)z->name); free(z); }
}

/* ===== DST offset ===== */

static void test_dst_offset(void) {
    printf("[dst_offset]\n");
    const neverc_tzdata_zone_t *ny = neverc_tzdata_lookup("America/New_York");
    if (!ny) { printf("  SKIP: New_York not found\n"); return; }

    /* Northern hemisphere DST: Mar-Nov */
    check_int("NY Jan (std)", neverc_tzdata_offset_for_month(ny, 1), -18000);
    check_int("NY Jul (dst)", neverc_tzdata_offset_for_month(ny, 7), -14400);
    check_int("NY Dec (std)", neverc_tzdata_offset_for_month(ny, 12), -18000);

    const neverc_tzdata_zone_t *syd = neverc_tzdata_lookup("Australia/Sydney");
    if (!syd) { printf("  SKIP: Sydney not found\n"); return; }

    /* Southern hemisphere DST: Oct-Apr */
    check_int("Syd Jan (dst)", neverc_tzdata_offset_for_month(syd, 1), syd->dst_offset);
    check_int("Syd Jul (std)", neverc_tzdata_offset_for_month(syd, 7), syd->utc_offset);
    check_int("Syd Nov (dst)", neverc_tzdata_offset_for_month(syd, 11), syd->dst_offset);
    check_int("Syd invalid month 0",
              neverc_tzdata_offset_for_month(syd, 0), syd->utc_offset);
    check_int("Syd invalid month 13",
              neverc_tzdata_offset_for_month(syd, 13), syd->utc_offset);

    const neverc_tzdata_zone_t *utc = neverc_tzdata_utc();
    check_int("UTC no dst any month", neverc_tzdata_offset_for_month(utc, 6), 0);

    const neverc_tzdata_zone_t *sp = neverc_tzdata_lookup("America/Sao_Paulo");
    check_int("Sao_Paulo Jan (std)",
              neverc_tzdata_offset_for_month(sp, 1), -10800);
    check_int("Sao_Paulo Jul (std)",
              neverc_tzdata_offset_for_month(sp, 7), -10800);

    const neverc_tzdata_zone_t *cai = neverc_tzdata_lookup("Africa/Cairo");
    check_int("Cairo Jan (std)", neverc_tzdata_offset_for_month(cai, 1), 7200);
    check_int("Cairo Jul (dst)", neverc_tzdata_offset_for_month(cai, 7), 10800);

    const neverc_tzdata_zone_t *yyc = neverc_tzdata_lookup("America/Edmonton");
    check_int("Edmonton Jan (std)",
              neverc_tzdata_offset_for_month(yyc, 1), -25200);
    check_int("Edmonton Jul (dst)",
              neverc_tzdata_offset_for_month(yyc, 7), -21600);
    const neverc_tzdata_zone_t *yvr = neverc_tzdata_lookup("America/Vancouver");
    check_int("Vancouver Jan (std)",
              neverc_tzdata_offset_for_month(yvr, 1), -28800);
    check_int("Vancouver Jul (dst)",
              neverc_tzdata_offset_for_month(yvr, 7), -25200);
}

/* 2024-03-10 07:00:00 UTC = US spring-forward instant (2nd Sunday, 02:00 EST). */
#define NY_SPRING_2024 1710054000LL
/* 2024-11-03 06:00:00 UTC = US fall-back instant (1st Sunday, 02:00 EDT). */
#define NY_FALL_2024   1730613600LL
/* 2024-03-31 01:00:00 UTC = EU spring-forward. */
#define EU_SPRING_2024 1711846800LL
/* 2024-10-27 01:00:00 UTC = EU fall-back. */
#define EU_FALL_2024   1729990800LL
/* 2024-04-06 16:00:00 UTC = Sydney autumn-back (first Sunday April 03:00 AEDT). */
#define SYD_END_2024   1712419200LL
/* 2024-10-05 16:00:00 UTC = Sydney spring-forward (first Sunday October 02:00 AEST). */
#define SYD_START_2024 1728144000LL
/* 2024-04-06 14:00:00 UTC = NZ/Chatham autumn-back (first Sunday April). */
#define NZ_END_2024    1712412000LL
/* 2024-09-28 14:00:00 UTC = NZ/Chatham spring-forward (last Sunday September). */
#define NZ_START_2024  1727532000LL
/* 2025-03-09 07:00:00 UTC = US spring-forward after the test TZif's last tx. */
#define NY_SPRING_2025 1741503600LL
#define JUL_2025       1751328000LL
#define JAN_2024       1705320000LL
#define JUL_2024       1719835200LL
/* 2024-04-25 22:00:00 UTC = Egypt's last-Friday-in-April transition. */
#define CAIRO_START_2024 1714082400LL
/* 2024-10-31 21:00:00 UTC = Egypt's last-Thursday-at-24:00 transition. */
#define CAIRO_END_2024   1730408400LL
/* Morocco suspends +01 during Ramadan; transition instants are in UTC. */
#define CASA_START_2024  1710036000LL
#define CASA_END_2024    1713060000LL
#define CAIRO_START_2023 1682632800LL
#define CAIRO_END_2023   1698354000LL
#define CASA_START_2025  1740276000LL
#define CASA_END_2025    1743904800LL
#define CASA_UTC_2026    1789866000LL
#define EDMONTON_PERMANENT_2026 1793520000LL
#define VANCOUVER_PERMANENT_2026 1793523600LL
#define CANADA_WINTER_2027       1800014400LL

static void test_offset_at(void) {
    printf("[offset_at]\n");
    const neverc_tzdata_zone_t *ny = neverc_tzdata_lookup("America/New_York");
    const neverc_tzdata_zone_t *lon = neverc_tzdata_lookup("Europe/London");
    const neverc_tzdata_zone_t *syd = neverc_tzdata_lookup("Australia/Sydney");
    const neverc_tzdata_zone_t *mel = neverc_tzdata_lookup("Australia/Melbourne");
    const neverc_tzdata_zone_t *akl = neverc_tzdata_lookup("Pacific/Auckland");
    const neverc_tzdata_zone_t *cht = neverc_tzdata_lookup("Pacific/Chatham");
    const neverc_tzdata_zone_t *scl = neverc_tzdata_lookup("America/Santiago");
    const neverc_tzdata_zone_t *cai = neverc_tzdata_lookup("Africa/Cairo");
    const neverc_tzdata_zone_t *casa = neverc_tzdata_lookup("Africa/Casablanca");
    const neverc_tzdata_zone_t *edm = neverc_tzdata_lookup("America/Edmonton");
    const neverc_tzdata_zone_t *van = neverc_tzdata_lookup("America/Vancouver");
    const neverc_tzdata_zone_t *utc = neverc_tzdata_utc();
    if (!ny || !lon || !syd || !mel || !akl || !cht || !scl || !cai ||
        !casa || !edm || !van || !utc) {
        printf("  SKIP: required zone missing\n");
        return;
    }

    check_int("offset_at NULL", neverc_tzdata_offset_at(NULL, 0), 0);
    check_int("UTC offset_at winter", neverc_tzdata_offset_at(utc, NY_SPRING_2024), 0);
    check_int("UTC offset_at summer", neverc_tzdata_offset_at(utc, NY_FALL_2024), 0);

    check_int("NY before spring-forward EST",
              neverc_tzdata_offset_at(ny, NY_SPRING_2024 - 1), -18000);
    check_int("NY at spring-forward EDT",
              neverc_tzdata_offset_at(ny, NY_SPRING_2024), -14400);
    check_int("NY before fall-back EDT",
              neverc_tzdata_offset_at(ny, NY_FALL_2024 - 1), -14400);
    check_int("NY at fall-back EST",
              neverc_tzdata_offset_at(ny, NY_FALL_2024), -18000);

    check_int("London before EU start GMT",
              neverc_tzdata_offset_at(lon, EU_SPRING_2024 - 1), 0);
    check_int("London at EU start BST",
              neverc_tzdata_offset_at(lon, EU_SPRING_2024), 3600);
    check_int("London before EU end BST",
              neverc_tzdata_offset_at(lon, EU_FALL_2024 - 1), 3600);
    check_int("London at EU end GMT",
              neverc_tzdata_offset_at(lon, EU_FALL_2024), 0);

    check_int("Sydney before autumn-back AEDT",
              neverc_tzdata_offset_at(syd, SYD_END_2024 - 1), 39600);
    check_int("Sydney at autumn-back AEST",
              neverc_tzdata_offset_at(syd, SYD_END_2024), 36000);
    check_int("Sydney before spring-forward AEST",
              neverc_tzdata_offset_at(syd, SYD_START_2024 - 1), 36000);
    check_int("Sydney at spring-forward AEDT",
              neverc_tzdata_offset_at(syd, SYD_START_2024), 39600);

    /* All built-in Australian zones use the private table hemisphere metadata,
     * rather than a Sydney-only name special case. */
    check_int("Melbourne at autumn-back AEST",
              neverc_tzdata_offset_at(mel, SYD_END_2024), 36000);
    check_int("Melbourne before spring-forward AEST",
              neverc_tzdata_offset_at(mel, SYD_START_2024 - 1), 36000);

    check_int("Auckland before autumn-back NZDT",
              neverc_tzdata_offset_at(akl, NZ_END_2024 - 1), 46800);
    check_int("Auckland at autumn-back NZST",
              neverc_tzdata_offset_at(akl, NZ_END_2024), 43200);
    check_int("Auckland before spring-forward NZST",
              neverc_tzdata_offset_at(akl, NZ_START_2024 - 1), 43200);
    check_int("Auckland at spring-forward NZDT",
              neverc_tzdata_offset_at(akl, NZ_START_2024), 46800);

    /* Chatham is 45 minutes ahead of NZ, so the same UTC instants at 02:45/03:45. */
    check_int("Chatham 45m before NZ start still CHAST",
              neverc_tzdata_offset_at(cht, NZ_START_2024 - 2700), 45900);
    check_int("Chatham before spring-forward CHAST",
              neverc_tzdata_offset_at(cht, NZ_START_2024 - 1), 45900);
    check_int("Chatham at spring-forward CHADT",
              neverc_tzdata_offset_at(cht, NZ_START_2024), 49500);
    check_int("Chatham before autumn-back CHADT",
              neverc_tzdata_offset_at(cht, NZ_END_2024 - 1), 49500);
    check_int("Chatham at autumn-back CHAST",
              neverc_tzdata_offset_at(cht, NZ_END_2024), 45900);

    /* IANA America/Santiago: first Saturday 24:00 (Sunday 00:00), not Sat 00:00. */
    check_int("Santiago Sat before Sep Sunday 00:00 is CLT",
              neverc_tzdata_offset_at(scl, 1725685200), -14400);
    check_int("Santiago after Sep Sunday 00:00 is CLST",
              neverc_tzdata_offset_at(scl, 1725768000), -10800);
    check_int("Santiago Sat before Apr Sunday 00:00 is CLST",
              neverc_tzdata_offset_at(scl, 1712404800), -10800);
    check_int("Santiago after Apr Sunday 00:00 is CLT",
              neverc_tzdata_offset_at(scl, 1712458800), -14400);

    check_int("Cairo before 2024 DST start is EET",
              neverc_tzdata_offset_at(cai, CAIRO_START_2024 - 1), 7200);
    check_int("Cairo at 2024 DST start is EEST",
              neverc_tzdata_offset_at(cai, CAIRO_START_2024), 10800);
    check_int("Cairo before 2024 DST end is EEST",
              neverc_tzdata_offset_at(cai, CAIRO_END_2024 - 1), 10800);
    check_int("Cairo at 2024 DST end is EET",
              neverc_tzdata_offset_at(cai, CAIRO_END_2024), 7200);
    check_int("Cairo before 2023 DST start is EET",
              neverc_tzdata_offset_at(cai, CAIRO_START_2023 - 1), 7200);
    check_int("Cairo at 2023 DST start is EEST",
              neverc_tzdata_offset_at(cai, CAIRO_START_2023), 10800);
    check_int("Cairo before 2023 DST end is EEST",
              neverc_tzdata_offset_at(cai, CAIRO_END_2023 - 1), 10800);
    check_int("Cairo at 2023 DST end is EET",
              neverc_tzdata_offset_at(cai, CAIRO_END_2023), 7200);

    check_int("Casablanca before 2024 Ramadan is +01",
              neverc_tzdata_offset_at(casa, CASA_START_2024 - 1), 3600);
    check_int("Casablanca during 2024 Ramadan is UTC",
              neverc_tzdata_offset_at(casa, CASA_START_2024), 0);
    check_int("Casablanca before 2024 Ramadan end is UTC",
              neverc_tzdata_offset_at(casa, CASA_END_2024 - 1), 0);
    check_int("Casablanca after 2024 Ramadan is +01",
              neverc_tzdata_offset_at(casa, CASA_END_2024), 3600);
    check_int("Casablanca before 2025 Ramadan is +01",
              neverc_tzdata_offset_at(casa, CASA_START_2025 - 1), 3600);
    check_int("Casablanca during 2025 Ramadan is UTC",
              neverc_tzdata_offset_at(casa, CASA_START_2025), 0);
    check_int("Casablanca before 2025 Ramadan end is UTC",
              neverc_tzdata_offset_at(casa, CASA_END_2025 - 1), 0);
    check_int("Casablanca after 2025 Ramadan is +01",
              neverc_tzdata_offset_at(casa, CASA_END_2025), 3600);
    check_int("Casablanca before permanent UTC is +01",
              neverc_tzdata_offset_at(casa, CASA_UTC_2026 - 1), 3600);
    check_int("Casablanca at permanent UTC is UTC",
              neverc_tzdata_offset_at(casa, CASA_UTC_2026), 0);

    check_int("Edmonton before permanent UTC-06 is MDT",
              neverc_tzdata_offset_at(edm, EDMONTON_PERMANENT_2026 - 1),
              -21600);
    check_int("Edmonton at permanent UTC-06 remains MDT offset",
              neverc_tzdata_offset_at(edm, EDMONTON_PERMANENT_2026),
              -21600);
    check_int("Edmonton winter 2027 remains UTC-06",
              neverc_tzdata_offset_at(edm, CANADA_WINTER_2027), -21600);
    check_int("Vancouver before permanent UTC-07 is PDT",
              neverc_tzdata_offset_at(van, VANCOUVER_PERMANENT_2026 - 1),
              -25200);
    check_int("Vancouver at permanent UTC-07 remains PDT offset",
              neverc_tzdata_offset_at(van, VANCOUVER_PERMANENT_2026),
              -25200);
    check_int("Vancouver winter 2027 remains UTC-07",
              neverc_tzdata_offset_at(van, CANADA_WINTER_2027), -25200);

    neverc_tzdata_zone_t *same_name =
        neverc_tzdata_fixed_zone("Africa/Cairo", 1234);
    check_not_null("fixed Cairo-name zone", same_name);
    check_int("fixed Cairo-name zone ignores builtin rules",
              neverc_tzdata_offset_at(same_name, CAIRO_START_2024), 1234);
    neverc_tzdata_zone_free(same_name);
    same_name = neverc_tzdata_fixed_zone("Africa/Casablanca", 1234);
    check_not_null("fixed Casablanca-name zone", same_name);
    check_int("fixed Casablanca-name zone ignores builtin rules",
              neverc_tzdata_offset_at(same_name, CASA_START_2024), 1234);
    neverc_tzdata_zone_free(same_name);

    /* Extreme unix_sec must not overflow civil math. */
    check_int("NY offset_at INT64_MAX",
              neverc_tzdata_offset_at(ny, INT64_MAX) == -18000 ||
              neverc_tzdata_offset_at(ny, INT64_MAX) == -14400, 1);
    check_int("NY offset_at INT64_MIN",
              neverc_tzdata_offset_at(ny, INT64_MIN) == -18000 ||
              neverc_tzdata_offset_at(ny, INT64_MIN) == -14400, 1);
    check_int("Sydney offset_at INT64_MIN",
              neverc_tzdata_offset_at(syd, INT64_MIN) == 36000 ||
              neverc_tzdata_offset_at(syd, INT64_MIN) == 39600, 1);
}

/* ===== Offsets correctness ===== */

static void test_offsets(void) {
    printf("[offsets]\n");
    const neverc_tzdata_zone_t *z;

    z = neverc_tzdata_lookup("America/Los_Angeles");
    check_int("LA offset -8h", z ? z->utc_offset : 0, -28800);

    z = neverc_tzdata_lookup("America/Chicago");
    check_int("Chicago offset -6h", z ? z->utc_offset : 0, -21600);

    z = neverc_tzdata_lookup("Europe/Paris");
    check_int("Paris offset +1h", z ? z->utc_offset : 0, 3600);

    z = neverc_tzdata_lookup("Europe/Moscow");
    check_int("Moscow offset +3h", z ? z->utc_offset : 0, 10800);

    z = neverc_tzdata_lookup("Asia/Kolkata");
    check_int("Kolkata offset +5:30", z ? z->utc_offset : 0, 19800);

    z = neverc_tzdata_lookup("Asia/Kathmandu");
    check_int("Kathmandu offset +5:45", z ? z->utc_offset : 0, 20700);

    z = neverc_tzdata_lookup("Pacific/Auckland");
    check_int("Auckland offset +12h", z ? z->utc_offset : 0, 43200);

    z = neverc_tzdata_lookup("Pacific/Chatham");
    check_int("Chatham offset +12:45", z ? z->utc_offset : 0, 45900);

    z = neverc_tzdata_lookup("America/Caracas");
    check_int("Caracas offset -4h", z ? z->utc_offset : 0, -14400);

    /* IANA current rules (tzdata 2026c): completed offset/DST changes */
    z = neverc_tzdata_lookup("America/Sao_Paulo");
    check_int("Sao_Paulo offset -3h", z ? z->utc_offset : 0, -10800);
    check_int("Sao_Paulo no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("Asia/Tehran");
    check_int("Tehran offset +3:30", z ? z->utc_offset : 0, 12600);
    check_int("Tehran no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("Asia/Almaty");
    check_int("Almaty offset +5h", z ? z->utc_offset : 0, 18000);
    check_int("Almaty no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("Pacific/Fiji");
    check_int("Fiji offset +12h", z ? z->utc_offset : 0, 43200);
    check_int("Fiji no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("Africa/Cairo");
    check_int("Cairo offset +2h", z ? z->utc_offset : 0, 7200);
    check_int("Cairo dst offset +3h", z ? z->dst_offset : 0, 10800);
    check_int("Cairo has dst", z ? z->has_dst : 0, 1);

    z = neverc_tzdata_lookup("Africa/Casablanca");
    check_int("Casablanca offset +1h", z ? z->utc_offset : 0, 3600);
    check_int("Casablanca no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("America/Edmonton");
    check_int("Edmonton offset -7h", z ? z->utc_offset : 0, -25200);
    check_int("Edmonton dst offset -6h", z ? z->dst_offset : 0, -21600);
    check_int("Edmonton has dst", z ? z->has_dst : 0, 1);

    z = neverc_tzdata_lookup("America/Vancouver");
    check_int("Vancouver offset -8h", z ? z->utc_offset : 0, -28800);
    check_int("Vancouver dst offset -7h", z ? z->dst_offset : 0, -25200);
    check_int("Vancouver has dst", z ? z->has_dst : 0, 1);
}

/* ===== Edge cases ===== */

static void test_edge_cases(void) {
    printf("[edge_cases]\n");

    /* Lookup is case-sensitive for IANA names */
    check_null("case sensitive", neverc_tzdata_lookup("utc"));

    /* But abbreviation lookup is case-insensitive */
    const neverc_tzdata_zone_t *z = neverc_tzdata_lookup_abbrev("pst");
    check_not_null("abbrev case insensitive", z);

    /* GMT and UTC both work */
    check_not_null("GMT exists", neverc_tzdata_lookup("GMT"));
    check_not_null("Etc/UTC exists", neverc_tzdata_lookup("Etc/UTC"));
    check_not_null("Etc/GMT exists", neverc_tzdata_lookup("Etc/GMT"));

    /* All entries have valid data */
    int cnt = neverc_tzdata_count();
    int all_valid = 1;
    for (int i = 0; i < cnt; i++) {
        z = neverc_tzdata_at(i);
        if (!z || !z->name || !z->abbrev) { all_valid = 0; break; }
    }
    check_int("all entries valid", all_valid, 1);

    check_null("lookup NULL", neverc_tzdata_lookup(NULL));
    check_null("lookup_abbrev NULL", neverc_tzdata_lookup_abbrev(NULL));
    check_int("list NULL names", neverc_tzdata_list("America/", NULL, 10), 0);

    neverc_tzdata_zone_t *fixed = neverc_tzdata_fixed_zone(NULL, 3600);
    check_not_null("fixed NULL name", fixed);
    check_int("fixed NULL name offset", fixed ? fixed->utc_offset : 0, 3600);
    if (fixed) { free((void *)fixed->name); free(fixed); }
}

static void tzdata_set_tz(const char *value) {
#if defined(_WIN32)
    _putenv_s("TZ", value ? value : "");
#else
    if (value) setenv("TZ", value, 1);
    else unsetenv("TZ");
#endif
}

static void test_local_tz(void) {
    printf("[local_tz]\n");
    const char *old = getenv("TZ");
    char saved[256];
    int had = 0;
    if (old) {
        size_t n = strlen(old);
        if (n >= sizeof(saved)) n = sizeof(saved) - 1;
        memcpy(saved, old, n);
        saved[n] = '\0';
        had = 1;
    }

    tzdata_set_tz(":America/New_York");
    const neverc_tzdata_zone_t *z = neverc_tzdata_local();
    check_not_null("colon TZ", z);
    check_str("colon TZ name", z ? z->name : NULL, "America/New_York");

    tzdata_set_tz("/usr/share/zoneinfo/Asia/Tokyo");
    z = neverc_tzdata_local();
    check_not_null("zoneinfo path TZ", z);
    check_str("zoneinfo path name", z ? z->name : NULL, "Asia/Tokyo");

    tzdata_set_tz("");
    z = neverc_tzdata_local();
    check_not_null("empty TZ", z);
    check_str("empty TZ is UTC", z ? z->name : NULL, "UTC");

    tzdata_set_tz("NotAReal/Zone");
    z = neverc_tzdata_local();
    check_not_null("unknown TZ", z);
    check_str("unknown TZ is UTC", z ? z->name : NULL, "UTC");

    tzdata_set_tz("EST");
    z = neverc_tzdata_local();
    check_not_null("TZ=EST", z);
    check_str("TZ=EST name", z ? z->name : NULL, "EST");
    check_int("TZ=EST no dst", z ? z->has_dst : 1, 0);
    check_int("TZ=EST summer offset",
              neverc_tzdata_offset_at(z, JUL_2024), -18000);

    tzdata_set_tz("EST5EDT,M3.2.0,M11.1.0");
    const neverc_tzdata_zone_t *est = neverc_tzdata_local();
    z = est;
    check_not_null("posix TZ", z);
    check_int("posix EST offset", z ? z->utc_offset : 0, -18000);
    check_int("posix EDT offset", z ? z->dst_offset : 0, -14400);
    check_int("posix has dst", z ? z->has_dst : 0, 1);
    check_int("posix EST dst hemi north", neverc_tzdata_dst_hemisphere(z), 1);
    check_int("posix spring-forward",
              neverc_tzdata_offset_at(z, NY_SPRING_2024), -14400);
    check_int("posix before spring-forward",
              neverc_tzdata_offset_at(z, NY_SPRING_2024 - 1), -18000);

    tzdata_set_tz("NZST-12NZDT,M9.5.0,M4.1.0");
    z = neverc_tzdata_local();
    check_not_null("posix NZ", z);
    check_int("posix NZ offset", z ? z->utc_offset : 0, 43200);
    check_int("posix NZ dst offset", z ? z->dst_offset : 0, 46800);
    check_int("posix NZ dst hemi south", neverc_tzdata_dst_hemisphere(z), 2);
    check_int("posix NZ July month std",
              neverc_tzdata_offset_for_month(z, 7), 43200);
    check_int("posix NZ January month dst",
              neverc_tzdata_offset_for_month(z, 1), 46800);
    check_int("posix NZ spring-forward",
              neverc_tzdata_offset_at(z, NZ_START_2024), 46800);
    check_int("posix NZ before spring-forward",
              neverc_tzdata_offset_at(z, NZ_START_2024 - 1), 43200);
    check_int("posix EST immutable after NZ load",
              est ? est->utc_offset : 0, -18000);
    check_int("posix EST offset_at after NZ load",
              neverc_tzdata_offset_at(est, NY_SPRING_2024), -14400);

    tzdata_set_tz("UTC0");
    z = neverc_tzdata_local();
    check_not_null("posix UTC0", z);
    check_int("posix UTC0 offset", z ? z->utc_offset : -1, 0);
    check_int("posix UTC0 no dst", z ? z->has_dst : -1, 0);

    tzdata_set_tz("EST5EDT,J79,J355");
    z = neverc_tzdata_local();
    check_not_null("posix Julian TZ", z);
    check_str("posix Julian is not UTC", z && z->name ? z->name : "UTC",
              "EST5EDT,J79,J355");
    check_int("posix Julian January EST",
              neverc_tzdata_offset_at(z, JAN_2024), -18000);
    check_int("posix Julian July EDT",
              neverc_tzdata_offset_at(z, JUL_2024), -14400);

    tzdata_set_tz("IST-2IDT,M3.4.4/26,M10.5.0");
    z = neverc_tzdata_local();
    check_not_null("posix Israel 26h", z);
    check_str("posix Israel is not UTC", z && z->name ? z->name : "UTC",
              "IST-2IDT,M3.4.4/26,M10.5.0");
    check_int("posix Israel std offset", z ? z->utc_offset : 0, 7200);
    check_int("posix Israel dst offset", z ? z->dst_offset : 0, 10800);

    tzdata_set_tz("XXX168");
    z = neverc_tzdata_local();
    check_not_null("posix 168h offset", z);
    check_int("posix 168h offset val", z ? z->utc_offset : 1, -168 * 3600);

    tzdata_set_tz("XXX169");
    z = neverc_tzdata_local();
    check_str("posix 169h rejected", z && z->name ? z->name : NULL, "UTC");

    tzdata_set_tz("EST5EDT,M3.2.0/-1,M11.1.0");
    z = neverc_tzdata_local();
    check_not_null("posix negative rule time", z);
    check_str("posix negative rule not UTC", z && z->name ? z->name : "UTC",
              "EST5EDT,M3.2.0/-1,M11.1.0");

    tzdata_set_tz("EST5EDT,78,354");
    z = neverc_tzdata_local();
    check_not_null("posix zero-based Julian TZ", z);
    check_int("posix n-form is not UTC offset",
              z && z->utc_offset == -18000, 1);
    check_int("posix n-form July EDT",
              neverc_tzdata_offset_at(z, JUL_2024), -14400);

    /* Jn / n rules leave posix_rule_t.month=0, so start.month > end.month
     * cannot classify Sep→Apr wrap-around as southern. */
    tzdata_set_tz("NZST-12NZDT,J260,J90");
    z = neverc_tzdata_local();
    check_not_null("posix Julian wrap TZ", z);
    check_int("posix Julian wrap hemi south",
              neverc_tzdata_dst_hemisphere(z), 2);
    check_int("posix Julian wrap January DST month",
              neverc_tzdata_offset_for_month(z, 1), 46800);
    check_int("posix Julian wrap July STD month",
              neverc_tzdata_offset_for_month(z, 7), 43200);
    check_int("posix Julian wrap January DST at",
              neverc_tzdata_offset_at(z, JAN_2024), 46800);
    check_int("posix Julian wrap July STD at",
              neverc_tzdata_offset_at(z, JUL_2024), 43200);

    tzdata_set_tz("NZST-12NZDT,259,89");
    z = neverc_tzdata_local();
    check_not_null("posix n-form wrap TZ", z);
    check_int("posix n-form wrap hemi south",
              neverc_tzdata_dst_hemisphere(z), 2);
    check_int("posix n-form wrap January DST month",
              neverc_tzdata_offset_for_month(z, 1), 46800);

    if (had) tzdata_set_tz(saved);
    else tzdata_set_tz(NULL);
}

static void append_bytes(uint8_t *buf, size_t *n, size_t cap,
                         const void *p, size_t len) {
    if (*n + len > cap) return;
    memcpy(buf + *n, p, len);
    *n += len;
}

static void append_be32(uint8_t *buf, size_t *n, size_t cap, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 8), (uint8_t)v};
    append_bytes(buf, n, cap, b, 4);
}

static void append_be64(uint8_t *buf, size_t *n, size_t cap, uint64_t v) {
    append_be32(buf, n, cap, (uint32_t)(v >> 32));
    append_be32(buf, n, cap, (uint32_t)v);
}

static void append_le16(uint8_t *buf, size_t *n, size_t cap, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    append_bytes(buf, n, cap, b, 2);
}

static void append_le32(uint8_t *buf, size_t *n, size_t cap, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8),
                    (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    append_bytes(buf, n, cap, b, 4);
}

static uint32_t test_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void store_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void tzif_header(uint8_t *buf, size_t *n, size_t cap) {
    append_bytes(buf, n, cap, "TZif2", 5);
    uint8_t pad[15] = {0};
    append_bytes(buf, n, cap, pad, 15);
}

static void tzif_counts(uint8_t *buf, size_t *n, size_t cap,
                        uint32_t ntime, uint32_t nzone, uint32_t nchar) {
    append_be32(buf, n, cap, 0);
    append_be32(buf, n, cap, 0);
    append_be32(buf, n, cap, 0);
    append_be32(buf, n, cap, ntime);
    append_be32(buf, n, cap, nzone);
    append_be32(buf, n, cap, nchar);
}

static size_t build_tzif_utc(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    return n;
}

static size_t build_tzif_ny(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 3, 2, 8);
    append_be64(buf, &n, cap, 0);
    append_be64(buf, &n, cap, (uint64_t)NY_SPRING_2024);
    append_be64(buf, &n, cap, (uint64_t)NY_FALL_2024);
    uint8_t idx[3] = {0, 1, 0};
    append_bytes(buf, &n, cap, idx, 3);
    append_be32(buf, &n, cap, (uint32_t)-18000);
    uint8_t est[2] = {0, 0};
    append_bytes(buf, &n, cap, est, 2);
    append_be32(buf, &n, cap, (uint32_t)-14400);
    uint8_t edt[2] = {1, 4};
    append_bytes(buf, &n, cap, edt, 2);
    append_bytes(buf, &n, cap, "EST\0EDT\0", 8);
    return n;
}

static size_t build_tzif_ny_footer(uint8_t *buf, size_t cap) {
    size_t n = build_tzif_ny(buf, cap);
    append_bytes(buf, &n, cap, "\nEST5EDT,M3.2.0,M11.1.0\n", 24);
    return n;
}

/* Real TZif files commonly keep an old local-mean-time type first. */
static size_t build_tzif_ny_historical(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 3, 3, 12);
    append_be64(buf, &n, cap, 0);
    append_be64(buf, &n, cap, (uint64_t)NY_SPRING_2024);
    append_be64(buf, &n, cap, (uint64_t)NY_FALL_2024);
    {
        uint8_t idx[3] = {2, 1, 2};
        append_bytes(buf, &n, cap, idx, 3);
    }
    append_be32(buf, &n, cap, (uint32_t)-17762);
    {
        uint8_t lmt[2] = {0, 0};
        append_bytes(buf, &n, cap, lmt, 2);
    }
    append_be32(buf, &n, cap, (uint32_t)-14400);
    {
        uint8_t edt[2] = {1, 4};
        append_bytes(buf, &n, cap, edt, 2);
    }
    append_be32(buf, &n, cap, (uint32_t)-18000);
    {
        uint8_t est[2] = {0, 8};
        append_bytes(buf, &n, cap, est, 2);
    }
    append_bytes(buf, &n, cap, "LMT\0EDT\0EST\0", 12);
    return n;
}

static size_t build_tzif_ny_historical_footer(uint8_t *buf, size_t cap) {
    size_t n = build_tzif_ny_historical(buf, cap);
    append_bytes(buf, &n, cap, "\nEST5EDT,M3.2.0,M11.1.0\n", 24);
    return n;
}

/* Southern POSIX footer (Oct→Apr) so zip/tzif-loaded zones are not
 * forced onto the northern rule path. */
static size_t build_tzif_syd_footer(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 1, 2, 10);
    append_be64(buf, &n, cap, 0);
    uint8_t idx[1] = {0};
    append_bytes(buf, &n, cap, idx, 1);
    append_be32(buf, &n, cap, 36000);
    uint8_t aest[2] = {0, 0};
    append_bytes(buf, &n, cap, aest, 2);
    append_be32(buf, &n, cap, 39600);
    uint8_t aedt[2] = {1, 5};
    append_bytes(buf, &n, cap, aedt, 2);
    append_bytes(buf, &n, cap, "AEST\0AEDT\0", 10);
    append_bytes(buf, &n, cap, "\nAEST-10AEDT,M10.1.0,M4.1.0\n", 28);
    return n;
}

/* No transitions + POSIX footer. Go LoadLocationFromTZData synthesizes a
 * fake transition at alpha, so lookup applies the extend string. */
static size_t build_tzif_posix_no_tx(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 2, 8);
    append_be32(buf, &n, cap, (uint32_t)-18000);
    uint8_t est[2] = {0, 0};
    append_bytes(buf, &n, cap, est, 2);
    append_be32(buf, &n, cap, (uint32_t)-14400);
    uint8_t edt[2] = {1, 4};
    append_bytes(buf, &n, cap, edt, 2);
    append_bytes(buf, &n, cap, "EST\0EDT\0", 8);
    append_bytes(buf, &n, cap, "\nEST5EDT,M3.2.0,M11.1.0\n", 24);
    return n;
}

/* Julian wrap-around footer (day 260 → day 90). Hemisphere must not use
 * start.month, which is 0 for Jn rules. */
static size_t build_tzif_julian_south_footer(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 1, 2, 10);
    append_be64(buf, &n, cap, 0);
    uint8_t idx[1] = {0};
    append_bytes(buf, &n, cap, idx, 1);
    append_be32(buf, &n, cap, 43200);
    uint8_t nzst[2] = {0, 0};
    append_bytes(buf, &n, cap, nzst, 2);
    append_be32(buf, &n, cap, 46800);
    uint8_t nzdt[2] = {1, 5};
    append_bytes(buf, &n, cap, nzdt, 2);
    append_bytes(buf, &n, cap, "NZST\0NZDT\0", 10);
    append_bytes(buf, &n, cap, "\nNZST-12NZDT,J260,J90\n", 22);
    return n;
}

static size_t build_tzif_pre_first(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 1, 2, 8);
    append_be64(buf, &n, cap, 1000);
    uint8_t idx[1] = {1};
    append_bytes(buf, &n, cap, idx, 1);
    append_be32(buf, &n, cap, (uint32_t)-18000);
    uint8_t est[2] = {0, 0};
    append_bytes(buf, &n, cap, est, 2);
    append_be32(buf, &n, cap, (uint32_t)-14400);
    uint8_t edt[2] = {1, 4};
    append_bytes(buf, &n, cap, edt, 2);
    append_bytes(buf, &n, cap, "EST\0EDT\0", 8);
    return n;
}

/* Zone 0 is DST and is used by the first transition; zone 1 is unused STD.
 * Go lookupFirstZone case 3: times before tx[0] use the first non-DST zone. */
static size_t build_tzif_used_dst_first(uint8_t *buf, size_t cap) {
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 0, 1, 4);
    append_be32(buf, &n, cap, 0);
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, "UTC\0", 4);
    tzif_header(buf, &n, cap);
    tzif_counts(buf, &n, cap, 1, 2, 8);
    append_be64(buf, &n, cap, 1000);
    uint8_t idx[1] = {0};
    append_bytes(buf, &n, cap, idx, 1);
    append_be32(buf, &n, cap, (uint32_t)-14400);
    uint8_t dst[2] = {1, 4};
    append_bytes(buf, &n, cap, dst, 2);
    append_be32(buf, &n, cap, (uint32_t)-18000);
    uint8_t stdz[2] = {0, 0};
    append_bytes(buf, &n, cap, stdz, 2);
    append_bytes(buf, &n, cap, "EST\0EDT\0", 8);
    return n;
}

static size_t build_stored_zip(uint8_t *out, size_t cap, const char *name,
                               const uint8_t *data, size_t dlen) {
    size_t n = 0;
    size_t namelen = strlen(name);
    uint32_t crc = test_crc32(data, dlen);
    append_le32(out, &n, cap, 0x04034b50u);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le32(out, &n, cap, crc);
    append_le32(out, &n, cap, (uint32_t)dlen);
    append_le32(out, &n, cap, (uint32_t)dlen);
    append_le16(out, &n, cap, (uint16_t)namelen);
    append_le16(out, &n, cap, 0);
    append_bytes(out, &n, cap, name, namelen);
    append_bytes(out, &n, cap, data, dlen);
    size_t cd_off = n;
    append_le32(out, &n, cap, 0x02014b50u);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le32(out, &n, cap, crc);
    append_le32(out, &n, cap, (uint32_t)dlen);
    append_le32(out, &n, cap, (uint32_t)dlen);
    append_le16(out, &n, cap, (uint16_t)namelen);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le32(out, &n, cap, 0);
    append_le32(out, &n, cap, 0);
    append_bytes(out, &n, cap, name, namelen);
    size_t cd_size = n - cd_off;
    append_le32(out, &n, cap, 0x06054b50u);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 0);
    append_le16(out, &n, cap, 1);
    append_le16(out, &n, cap, 1);
    append_le32(out, &n, cap, (uint32_t)cd_size);
    append_le32(out, &n, cap, (uint32_t)cd_off);
    append_le16(out, &n, cap, 0);
    return n;
}

static void test_zip_tzif(void) {
    printf("[zip/tzif]\n");
    uint8_t tzif[256];
    size_t tlen = build_tzif_utc(tzif, sizeof(tzif));
    check_int("utc tzif built", tlen > 0, 1);

    neverc_tzdata_zone_t *z = neverc_tzdata_load_tzif("UTC", tzif, tlen);
    check_not_null("load utc tzif", z);
    check_str("tzif utc name", z ? z->name : NULL, "UTC");
    check_int("tzif utc offset", z ? z->utc_offset : -1, 0);
    check_int("tzif utc no dst", z ? z->has_dst : -1, 0);
    neverc_tzdata_zone_free(z);

    z = neverc_tzdata_load_tzif("Africa/Cairo", tzif, tlen);
    check_not_null("load Cairo-name UTC tzif", z);
    check_int("Cairo-name TZif overrides builtin rules",
              neverc_tzdata_offset_at(z, CAIRO_START_2024), 0);
    neverc_tzdata_zone_free(z);

    z = neverc_tzdata_load_tzif("America/Edmonton", tzif, tlen);
    check_not_null("load Edmonton-name UTC tzif", z);
    check_int("Edmonton-name TZif overrides builtin rules",
              neverc_tzdata_offset_at(z, CANADA_WINTER_2027), 0);
    neverc_tzdata_zone_free(z);

    z = neverc_tzdata_load_tzif("America/Vancouver", tzif, tlen);
    check_not_null("load Vancouver-name UTC tzif", z);
    check_int("Vancouver-name TZif overrides builtin rules",
              neverc_tzdata_offset_at(z, CANADA_WINTER_2027), 0);
    neverc_tzdata_zone_free(z);

    if (tlen > 4 && tlen <= sizeof(tzif)) {
        uint8_t tzif4[256];
        memcpy(tzif4, tzif, tlen);
        tzif4[4] = '4';
        z = neverc_tzdata_load_tzif("UTC", tzif4, tlen);
        check_not_null("load tzif v4", z);
        check_int("tzif v4 utc offset", z ? z->utc_offset : -1, 0);
        neverc_tzdata_zone_free(z);
    }

    uint8_t ny[512];
    size_t nlen = build_tzif_ny(ny, sizeof(ny));
    z = neverc_tzdata_load_tzif("America/New_York", ny, nlen);
    check_not_null("load ny tzif", z);
    check_int("ny tzif std", z ? z->utc_offset : 0, -18000);
    check_int("ny tzif dst", z ? z->dst_offset : 0, -14400);
    check_int("ny tzif has dst", z ? z->has_dst : 0, 1);
    check_int("ny tzif before spring",
              neverc_tzdata_offset_at(z, NY_SPRING_2024 - 1), -18000);
    check_int("ny tzif at spring",
              neverc_tzdata_offset_at(z, NY_SPRING_2024), -14400);
    check_int("ny tzif at fall",
              neverc_tzdata_offset_at(z, NY_FALL_2024), -18000);
    neverc_tzdata_zone_free(z);

    uint8_t nyh[512];
    size_t nhlen = build_tzif_ny_historical(nyh, sizeof(nyh));
    z = neverc_tzdata_load_tzif("America/New_York", nyh, nhlen);
    check_not_null("load historical ny tzif without footer", z);
    check_str("historical no-footer representative std abbrev",
              z ? z->abbrev : NULL, "EST");
    check_int("historical no-footer representative std offset",
              z ? z->utc_offset : 0, -18000);
    check_str("historical no-footer representative dst abbrev",
              z ? z->abbrev_dst : NULL, "EDT");
    check_int("historical no-footer representative dst offset",
              z ? z->dst_offset : 0, -14400);
    neverc_tzdata_zone_free(z);

    uint8_t nyf[512];
    size_t nflen = build_tzif_ny_footer(nyf, sizeof(nyf));
    z = neverc_tzdata_load_tzif("America/New_York", nyf, nflen);
    check_not_null("load ny tzif with footer", z);
    check_int("ny footer after last tx uses POSIX DST",
              neverc_tzdata_offset_at(z, NY_SPRING_2025), -14400);
    check_int("ny footer mid-summer 2025",
              neverc_tzdata_offset_at(z, JUL_2025), -14400);
    neverc_tzdata_zone_free(z);

    uint8_t nyhf[512];
    size_t nhflen = build_tzif_ny_historical_footer(nyhf, sizeof(nyhf));
    z = neverc_tzdata_load_tzif("America/New_York", nyhf, nhflen);
    check_not_null("load historical ny tzif with footer", z);
    check_str("historical ny representative std abbrev",
              z ? z->abbrev : NULL, "EST");
    check_int("historical ny representative std offset",
              z ? z->utc_offset : 0, -18000);
    check_str("historical ny representative dst abbrev",
              z ? z->abbrev_dst : NULL, "EDT");
    check_int("historical ny representative dst offset",
              z ? z->dst_offset : 0, -14400);
    check_int("historical ny January representative offset",
              neverc_tzdata_offset_for_month(z, 1), -18000);
    check_int("historical ny footer instant offset",
              neverc_tzdata_offset_at(z, NY_SPRING_2025), -14400);
    neverc_tzdata_zone_free(z);

    uint8_t sydf[512];
    size_t sflen = build_tzif_syd_footer(sydf, sizeof(sydf));
    z = neverc_tzdata_load_tzif("Custom/South", sydf, sflen);
    check_not_null("load southern tzif with footer", z);
    check_int("southern tzif dst hemi",
              neverc_tzdata_dst_hemisphere(z), 2);
    check_int("southern tzif July std",
              neverc_tzdata_offset_for_month(z, 7), 36000);
    check_int("southern tzif January dst",
              neverc_tzdata_offset_for_month(z, 1), 39600);
    neverc_tzdata_zone_free(z);

    uint8_t notx[512];
    size_t ntlen = build_tzif_posix_no_tx(notx, sizeof(notx));
    z = neverc_tzdata_load_tzif("Custom/NoTx", notx, ntlen);
    check_not_null("load no-tx tzif with posix footer", z);
    check_int("no-tx July uses POSIX DST",
              neverc_tzdata_offset_at(z, JUL_2024), -14400);
    check_int("no-tx January uses POSIX STD",
              neverc_tzdata_offset_at(z, JAN_2024), -18000);
    neverc_tzdata_zone_free(z);

    uint8_t jsf[512];
    size_t jslen = build_tzif_julian_south_footer(jsf, sizeof(jsf));
    z = neverc_tzdata_load_tzif("Custom/JulianSouth", jsf, jslen);
    check_not_null("load julian-south tzif with footer", z);
    check_int("julian-south tzif dst hemi",
              neverc_tzdata_dst_hemisphere(z), 2);
    check_int("julian-south tzif January dst",
              neverc_tzdata_offset_for_month(z, 1), 46800);
    check_int("julian-south tzif July std",
              neverc_tzdata_offset_for_month(z, 7), 43200);
    neverc_tzdata_zone_free(z);

    uint8_t pre[256];
    size_t plen = build_tzif_pre_first(pre, sizeof(pre));
    z = neverc_tzdata_load_tzif("PreFirst", pre, plen);
    check_not_null("load pre-first tzif", z);
    check_int("pre-first uses first ttinfo not first transition",
              neverc_tzdata_offset_at(z, 0), -18000);
    check_int("at first transition still DST",
              neverc_tzdata_offset_at(z, 1000), -14400);
    neverc_tzdata_zone_free(z);

    uint8_t used[256];
    size_t ulen = build_tzif_used_dst_first(used, sizeof(used));
    z = neverc_tzdata_load_tzif("UsedDstFirst", used, ulen);
    check_not_null("load used-dst-first tzif", z);
    check_int("used-dst-first pre-tx uses first non-DST zone",
              neverc_tzdata_offset_at(z, 0), -18000);
    check_int("used-dst-first at first tx is DST",
              neverc_tzdata_offset_at(z, 1000), -14400);
    neverc_tzdata_zone_free(z);

    uint8_t zip[1024];
    size_t zlen = build_stored_zip(zip, sizeof(zip), "America/New_York", ny, nlen);
    check_int("zip built", zlen > 22, 1);
    size_t extracted_len = 0;
    uint8_t *extracted = neverc_tzdata_zip_extract(zip, zlen, "America/New_York",
                                                   &extracted_len);
    check_not_null("zip extract", extracted);
    check_int("zip extract size", (int)extracted_len, (int)nlen);
    free(extracted);

    static const uint8_t comment[] = {
        'z', 0x50, 0x4b, 0x05, 0x06, 'f', 'a', 'k', 'e',
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        't', 'a', 'i', 'l'
    };
    uint8_t commented[sizeof(zip) + sizeof(comment)];
    memcpy(commented, zip, zlen);
    commented[zlen - 2] = (uint8_t)sizeof(comment);
    commented[zlen - 1] = 0;
    memcpy(commented + zlen, comment, sizeof(comment));
    size_t commented_len = zlen + sizeof(comment);
    extracted_len = 0;
    extracted = neverc_tzdata_zip_extract(commented, commented_len,
                                           "America/New_York",
                                           &extracted_len);
    check_not_null("zip extract with EOCD comment and fake signature", extracted);
    check_int("commented zip extract size", (int)extracted_len, (int)nlen);
    check_int("commented zip extract content",
              extracted && extracted_len == nlen &&
                  memcmp(extracted, ny, nlen) == 0,
              1);
    free(extracted);

    uint8_t empty_eocd_comment[22] = {0};
    empty_eocd_comment[0] = 0x50;
    empty_eocd_comment[1] = 0x4b;
    empty_eocd_comment[2] = 0x05;
    empty_eocd_comment[3] = 0x06;
    uint8_t nested_eocd[sizeof(zip) + sizeof(empty_eocd_comment)];
    memcpy(nested_eocd, zip, zlen);
    nested_eocd[zlen - 2] = (uint8_t)sizeof(empty_eocd_comment);
    nested_eocd[zlen - 1] = 0;
    memcpy(nested_eocd + zlen, empty_eocd_comment,
           sizeof(empty_eocd_comment));
    extracted_len = 0;
    extracted = neverc_tzdata_zip_extract(
        nested_eocd, zlen + sizeof(empty_eocd_comment),
        "America/New_York", &extracted_len);
    check_not_null("zip comment with structurally valid fake EOCD", extracted);
    check_int("nested EOCD zip extract size", (int)extracted_len, (int)nlen);
    check_int("nested EOCD zip extract content",
              extracted && extracted_len == nlen &&
                  memcmp(extracted, ny, nlen) == 0,
              1);
    free(extracted);

    uint8_t bad_comment[sizeof(commented)];
    memcpy(bad_comment, commented, commented_len);
    bad_comment[zlen - 2] = (uint8_t)(sizeof(comment) + 1);
    check_null("zip EOCD comment length mismatch rejected",
               neverc_tzdata_zip_extract(bad_comment, commented_len,
                                         "America/New_York", NULL));

    check_null("zip missing name",
               neverc_tzdata_zip_extract(zip, zlen, "Europe/Paris", NULL));
    check_null("zip truncated",
               neverc_tzdata_zip_extract(zip, zlen > 0 ? zlen - 1 : 0,
                                         "America/New_York", NULL));
    check_null("zip null", neverc_tzdata_zip_extract(NULL, zlen, "x", NULL));

    size_t namelen = strlen("America/New_York");
    size_t cd = 30 + namelen + nlen;

    uint8_t zip64[1024];
    memcpy(zip64, zip, zlen);
    /* EOCD number-of-entries zip64 sentinel. */
    zip64[zlen - 12] = 0xFF;
    zip64[zlen - 11] = 0xFF;
    check_null("zip64 rejected",
               neverc_tzdata_zip_extract(zip64, zlen, "America/New_York", NULL));

    uint8_t zip64_entry[sizeof(zip)];
    memcpy(zip64_entry, zip, zlen);
    store_le32(zip64_entry + cd + 20, 0xFFFFFFFFu);
    check_null("zip64 central entry sentinel rejected",
               neverc_tzdata_zip_extract(zip64_entry, zlen,
                                         "America/New_York", NULL));

    uint8_t multidisk_entry[sizeof(zip)];
    memcpy(multidisk_entry, zip, zlen);
    store_le16(multidisk_entry + cd + 34, 1);
    check_null("multi-disk central entry rejected",
               neverc_tzdata_zip_extract(multidisk_entry, zlen,
                                         "America/New_York", NULL));

    uint8_t mismatched_flags[sizeof(zip)];
    memcpy(mismatched_flags, zip, zlen);
    store_le16(mismatched_flags + 6, 0x0800);
    check_null("local and central flags mismatch rejected",
               neverc_tzdata_zip_extract(mismatched_flags, zlen,
                                         "America/New_York", NULL));

    uint8_t data_descriptor[sizeof(zip)];
    memcpy(data_descriptor, zip, zlen);
    store_le16(data_descriptor + 6, 0x0008);
    store_le16(data_descriptor + cd + 8, 0x0008);
    check_null("unsupported data descriptor rejected",
               neverc_tzdata_zip_extract(data_descriptor, zlen,
                                         "America/New_York", NULL));

    uint8_t mismatched_size[sizeof(zip)];
    memcpy(mismatched_size, zip, zlen);
    store_le32(mismatched_size + 22, (uint32_t)nlen + 1);
    check_null("local and central size mismatch rejected",
               neverc_tzdata_zip_extract(mismatched_size, zlen,
                                         "America/New_York", NULL));

    uint8_t overlaps_directory[sizeof(zip)];
    memcpy(overlaps_directory, zip, zlen);
    store_le32(overlaps_directory + 18, (uint32_t)nlen + 1);
    store_le32(overlaps_directory + 22, (uint32_t)nlen + 1);
    store_le32(overlaps_directory + cd + 20, (uint32_t)nlen + 1);
    store_le32(overlaps_directory + cd + 24, (uint32_t)nlen + 1);
    check_null("stored data overlapping central directory rejected",
               neverc_tzdata_zip_extract(overlaps_directory, zlen,
                                         "America/New_York", NULL));

    uint8_t bad_entry_count[sizeof(zip)];
    memcpy(bad_entry_count, zip, zlen);
    store_le16(bad_entry_count + zlen - 14, 2);
    store_le16(bad_entry_count + zlen - 12, 2);
    check_null("central directory entry count mismatch rejected",
               neverc_tzdata_zip_extract(bad_entry_count, zlen,
                                         "America/New_York", NULL));

    uint8_t bad_crc[sizeof(zip)];
    memcpy(bad_crc, zip, zlen);
    bad_crc[30 + namelen] ^= 1;
    check_null("stored entry CRC mismatch rejected",
               neverc_tzdata_zip_extract(bad_crc, zlen,
                                         "America/New_York", NULL));

    uint8_t compressed[1024];
    memcpy(compressed, zip, zlen);
    /* local method at offset 8, central method at cd+10. */
    compressed[8] = 8;
    compressed[8 + 1] = 0;
    /* central dir starts after local header 30 + namelen + data */
    compressed[cd + 10] = 8;
    check_null("compressed zip rejected",
               neverc_tzdata_zip_extract(compressed, zlen, "America/New_York",
                                         NULL));

    z = neverc_tzdata_load_from_zip(zip, zlen, "America/New_York");
    check_not_null("load from zip", z);
    check_int("zip-loaded spring",
              neverc_tzdata_offset_at(z, NY_SPRING_2024), -14400);
    neverc_tzdata_zone_free(z);

    check_null("load zip dot-dot name",
               neverc_tzdata_load_from_zip(zip, zlen, "../America/New_York"));
    check_null("load zip leading slash",
               neverc_tzdata_load_from_zip(zip, zlen, "/America/New_York"));

    uint8_t bad[] = "not-tzif-data-at-all";
    check_null("bad tzif magic",
               neverc_tzdata_load_tzif("x", bad, sizeof(bad)));
    check_null("empty tzif", neverc_tzdata_load_tzif("x", bad, 0));

    neverc_tzdata_zone_t *fixed = neverc_tzdata_fixed_zone("UTC+8", 28800);
    neverc_tzdata_zone_free(fixed);
    check_int("zone_free fixed does not crash", 1, 1);
    const neverc_tzdata_zone_t *builtin = neverc_tzdata_lookup("UTC");
    neverc_tzdata_zone_free((neverc_tzdata_zone_t *)builtin);
    check_int("zone_free built-in is a no-op",
              neverc_tzdata_lookup("UTC") == builtin, 1);
    neverc_tzdata_zone_free(NULL);
}

static size_t build_tzif_v1_abbrev(uint8_t *buf, size_t cap,
                                   const uint8_t *abbrev, uint32_t nchar) {
    size_t n = 0;
    append_bytes(buf, &n, cap, "TZif", 4);
    uint8_t pad[16] = {0};
    append_bytes(buf, &n, cap, pad, 16);
    tzif_counts(buf, &n, cap, 0, 1, nchar);
    append_be32(buf, &n, cap, 0);
    uint8_t zmeta[2] = {0, 0};
    append_bytes(buf, &n, cap, zmeta, 2);
    append_bytes(buf, &n, cap, abbrev, nchar);
    return n;
}

static void test_tzif_abbrev_bounds(void) {
    printf("[tzif abbrev bounds]\n");
    uint8_t ok_ab[4] = {'E', 'S', 'T', 0};
    uint8_t ok[64];
    size_t ok_len = build_tzif_v1_abbrev(ok, sizeof(ok), ok_ab, 4);
    neverc_tzdata_zone_t *z = neverc_tzdata_load_tzif("EST", ok, ok_len);
    check_not_null("v1 tzif nul-terminated abbrev", z);
    check_str("v1 tzif abbrev", z ? z->abbrev : NULL, "EST");
    neverc_tzdata_zone_free(z);

    /* Counted char array has no NUL. Padding after `len` would make an
     * unbounded strlen succeed and copy heap/stack as the abbreviation. */
    uint8_t padded[128];
    memset(padded, 'A', sizeof(padded));
    uint8_t bad_ab[3] = {'E', 'S', 'T'};
    size_t bad_len = build_tzif_v1_abbrev(padded, sizeof(padded), bad_ab, 3);
    memset(padded + bad_len, 'A', sizeof(padded) - bad_len);
    padded[sizeof(padded) - 1] = '\0';
    check_null("v1 tzif unterminated abbrev rejected",
               neverc_tzdata_load_tzif("x", padded, bad_len));

    uint8_t v2[128];
    memset(v2, 'A', sizeof(v2));
    size_t n = 0;
    uint8_t zmeta[2] = {0, 0};
    tzif_header(v2, &n, sizeof(v2));
    tzif_counts(v2, &n, sizeof(v2), 0, 1, 3);
    append_be32(v2, &n, sizeof(v2), 0);
    append_bytes(v2, &n, sizeof(v2), zmeta, 2);
    append_bytes(v2, &n, sizeof(v2), "EST", 3);
    tzif_header(v2, &n, sizeof(v2));
    tzif_counts(v2, &n, sizeof(v2), 0, 1, 3);
    append_be32(v2, &n, sizeof(v2), 0);
    append_bytes(v2, &n, sizeof(v2), zmeta, 2);
    append_bytes(v2, &n, sizeof(v2), "EST", 3);
    memset(v2 + n, 'A', sizeof(v2) - n);
    v2[sizeof(v2) - 1] = '\0';
    check_null("v2 tzif unterminated abbrev rejected",
               neverc_tzdata_load_tzif("x", v2, n));
}

/* ===== Main ===== */

int main(void) {
    /* Run before any other lookup so the first-use init race is live. */
    test_concurrent_init();
    test_lookup();
    test_lookup_abbrev();
    test_count();
    test_list();
    test_utc();
    test_fixed_zone();
    test_dst_offset();
    test_offset_at();
    test_offsets();
    test_edge_cases();
    test_local_tz();
    test_zip_tzif();
    test_tzif_abbrev_bounds();

    printf("\n--- time/tzdata: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
