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
    check_int("Kyiv dst hemi north", z ? z->dst_hemi : 0, 1);

    z = neverc_tzdata_lookup("Europe/Kiev");
    check_not_null("Kiev alias", z);

    z = neverc_tzdata_lookup("Australia/Sydney");
    check_not_null("Sydney", z);
    check_int("Sydney offset", z ? z->utc_offset : 0, 36000);
    check_int("Sydney has dst", z ? z->has_dst : 0, 1);

    z = neverc_tzdata_lookup("Nonexistent/Zone");
    check_null("nonexistent", z);
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
#define SYD_START_2024 1728132000LL
/* 2024-04-06 14:00:00 UTC = NZ/Chatham autumn-back (first Sunday April). */
#define NZ_END_2024    1712412000LL
/* 2024-09-28 14:00:00 UTC = NZ/Chatham spring-forward (last Sunday September). */
#define NZ_START_2024  1727532000LL

static void test_offset_at(void) {
    printf("[offset_at]\n");
    const neverc_tzdata_zone_t *ny = neverc_tzdata_lookup("America/New_York");
    const neverc_tzdata_zone_t *lon = neverc_tzdata_lookup("Europe/London");
    const neverc_tzdata_zone_t *syd = neverc_tzdata_lookup("Australia/Sydney");
    const neverc_tzdata_zone_t *akl = neverc_tzdata_lookup("Pacific/Auckland");
    const neverc_tzdata_zone_t *cht = neverc_tzdata_lookup("Pacific/Chatham");
    const neverc_tzdata_zone_t *utc = neverc_tzdata_utc();
    if (!ny || !lon || !syd || !akl || !cht || !utc) {
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

    tzdata_set_tz("EST5EDT,M3.2.0,M11.1.0");
    z = neverc_tzdata_local();
    check_not_null("posix TZ", z);
    check_int("posix EST offset", z ? z->utc_offset : 0, -18000);
    check_int("posix EDT offset", z ? z->dst_offset : 0, -14400);
    check_int("posix has dst", z ? z->has_dst : 0, 1);
    check_int("posix EST dst hemi north", z ? z->dst_hemi : 0, 1);
    check_int("posix spring-forward",
              neverc_tzdata_offset_at(z, NY_SPRING_2024), -14400);
    check_int("posix before spring-forward",
              neverc_tzdata_offset_at(z, NY_SPRING_2024 - 1), -18000);

    tzdata_set_tz("NZST-12NZDT,M9.5.0,M4.1.0");
    z = neverc_tzdata_local();
    check_not_null("posix NZ", z);
    check_int("posix NZ offset", z ? z->utc_offset : 0, 43200);
    check_int("posix NZ dst offset", z ? z->dst_offset : 0, 46800);
    check_int("posix NZ dst hemi south", z ? z->dst_hemi : 0, 2);
    check_int("posix NZ July month std",
              neverc_tzdata_offset_for_month(z, 7), 43200);
    check_int("posix NZ January month dst",
              neverc_tzdata_offset_for_month(z, 1), 46800);
    check_int("posix NZ spring-forward",
              neverc_tzdata_offset_at(z, NZ_START_2024), 46800);
    check_int("posix NZ before spring-forward",
              neverc_tzdata_offset_at(z, NZ_START_2024 - 1), 43200);

    tzdata_set_tz("UTC0");
    z = neverc_tzdata_local();
    check_not_null("posix UTC0", z);
    check_int("posix UTC0 offset", z ? z->utc_offset : -1, 0);
    check_int("posix UTC0 no dst", z ? z->has_dst : -1, 0);

    if (had) tzdata_set_tz(saved);
    else tzdata_set_tz(NULL);
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

    printf("\n--- time/tzdata: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
