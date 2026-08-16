#include "neverc/std/time_tzdata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
              neverc_tzdata_offset_for_month(yyc, 1), -21600);
    const neverc_tzdata_zone_t *yvr = neverc_tzdata_lookup("America/Vancouver");
    check_int("Vancouver Jan (std)",
              neverc_tzdata_offset_for_month(yvr, 1), -25200);
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
    check_int("Edmonton offset -6h", z ? z->utc_offset : 0, -21600);
    check_int("Edmonton no dst", z ? z->has_dst : -1, 0);

    z = neverc_tzdata_lookup("America/Vancouver");
    check_int("Vancouver offset -7h", z ? z->utc_offset : 0, -25200);
    check_int("Vancouver no dst", z ? z->has_dst : -1, 0);
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

    if (had) tzdata_set_tz(saved);
    else tzdata_set_tz(NULL);
}

/* ===== Main ===== */

int main(void) {
    test_lookup();
    test_lookup_abbrev();
    test_count();
    test_list();
    test_utc();
    test_fixed_zone();
    test_dst_offset();
    test_offsets();
    test_edge_cases();
    test_local_tz();

    printf("\n--- time/tzdata: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
