#include "neverc/std/time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_int64(const char *name, int64_t got, int64_t expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %lld, expected %lld\n", name, (long long)got, (long long)expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected); }
}
static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_unix_epoch(void) {
    printf("[unix epoch]\n");
    /* 2024-01-15 12:30:45 UTC = 1705321845 */
    neverc_time_t t = neverc_time_unix(1705321845, 0);
    check_int("year", neverc_time_year(t), 2024);
    check_int("month", neverc_time_month(t), 1);
    check_int("day", neverc_time_day(t), 15);
    check_int("hour", neverc_time_hour(t), 12);
    check_int("minute", neverc_time_minute(t), 30);
    check_int("second", neverc_time_second(t), 45);
    check_int("weekday", neverc_time_weekday(t), 1); /* Monday */
}

static void test_zero(void) {
    printf("[zero]\n");
    neverc_time_t z = neverc_time_unix(0, 0);
    check_bool("is_zero", neverc_time_is_zero(z), 1);
    check_int("year 1970", neverc_time_year(z), 1970);
    check_int("month 1", neverc_time_month(z), 1);
    check_int("day 1", neverc_time_day(z), 1);
}

static void test_arithmetic(void) {
    printf("[arithmetic]\n");
    neverc_time_t t1 = neverc_time_unix(1000, 0);
    neverc_time_t t2 = neverc_time_add(t1, 5 * NEVERC_TIME_SECOND);
    check_int64("add 5s", neverc_time_unix_sec(t2), 1005);

    neverc_duration_t d = neverc_time_sub(t2, t1);
    check_int64("sub", d, 5 * NEVERC_TIME_SECOND);

    neverc_time_t t3 = neverc_time_add(t1, NEVERC_TIME_HOUR);
    check_int64("add 1h", neverc_time_unix_sec(t3), 4600);
}

static void test_comparison(void) {
    printf("[comparison]\n");
    neverc_time_t t1 = neverc_time_unix(1000, 0);
    neverc_time_t t2 = neverc_time_unix(2000, 0);
    neverc_time_t t3 = neverc_time_unix(1000, 0);

    check_bool("before", neverc_time_before(t1, t2), 1);
    check_bool("after", neverc_time_after(t2, t1), 1);
    check_bool("equal", neverc_time_equal(t1, t3), 1);
    check_bool("not equal", neverc_time_equal(t1, t2), 0);
}

static void test_epoch_conversions(void) {
    printf("[epoch conversions]\n");
    neverc_time_t t = neverc_time_unix(100, 500000000);
    check_int64("unix_sec", neverc_time_unix_sec(t), 100);
    check_int64("unix_milli", neverc_time_unix_milli(t), 100500);
    check_int64("unix_nano", neverc_time_unix_nano(t), 100500000000LL);
}

static void test_duration(void) {
    printf("[duration]\n");
    neverc_duration_t d = 2500 * NEVERC_TIME_MILLISECOND;
    check_int64("milliseconds", neverc_time_duration_milliseconds(d), 2500);
    check_int64("microseconds", neverc_time_duration_microseconds(d), 2500000);
    check_int64("nanoseconds", neverc_time_duration_nanoseconds(d), d);

    double sec = neverc_time_duration_seconds(d);
    tests_run++;
    if (sec > 2.499 && sec < 2.501) tests_passed++;
    else { tests_failed++; printf("  FAIL: seconds: got %f\n", sec); }
}

static void test_format_rfc3339(void) {
    printf("[format rfc3339]\n");
    neverc_time_t t = neverc_time_unix(1705321845, 0);
    char *s = neverc_time_format_rfc3339(t);
    check_str("rfc3339", s, "2024-01-15T12:30:45Z");
    free(s);

    neverc_time_t z = neverc_time_unix(0, 0);
    char *s2 = neverc_time_format_rfc3339(z);
    check_str("epoch rfc3339", s2, "1970-01-01T00:00:00Z");
    free(s2);
}

static void test_parse_rfc3339(void) {
    printf("[parse rfc3339]\n");
    neverc_time_t t;

    int err = neverc_time_parse_rfc3339("2024-01-15T12:30:45Z", &t);
    check_int("parse ok", err, 0);
    check_int64("parsed sec", t.sec, 1705321845);

    err = neverc_time_parse_rfc3339("1970-01-01T00:00:00Z", &t);
    check_int("parse epoch ok", err, 0);
    check_int64("epoch sec", t.sec, 0);

    err = neverc_time_parse_rfc3339("2024-01-15T12:30:45+08:00", &t);
    check_int("parse tz ok", err, 0);
    check_int64("tz adjusted", t.sec, 1705321845 - 8*3600);

    err = neverc_time_parse_rfc3339("2024-01-15T12:30:45.123456789Z", &t);
    check_int("parse nsec ok", err, 0);
    check_int("nsec", t.nsec, 123456789);

    err = neverc_time_parse_rfc3339("invalid", &t);
    check_int("parse invalid", err, -1);
}

static void test_now(void) {
    printf("[now]\n");
    neverc_time_t t = neverc_time_now();
    check_bool("now not zero", !neverc_time_is_zero(t), 1);
    check_bool("year >= 2024", neverc_time_year(t) >= 2024, 1);
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    neverc_time_t orig = neverc_time_unix(1705321845, 0);
    char *s = neverc_time_format_rfc3339(orig);
    neverc_time_t parsed;
    int err = neverc_time_parse_rfc3339(s, &parsed);
    check_int("roundtrip ok", err, 0);
    check_bool("roundtrip equal", neverc_time_equal(orig, parsed), 1);
    free(s);
}

static void test_date(void) {
    printf("[date]\n");
    neverc_time_t t = neverc_time_date(2024, 1, 15, 10, 30, 0, 0);
    check_int("date year", neverc_time_year(t), 2024);
    check_int("date month", neverc_time_month(t), 1);
    check_int("date day", neverc_time_day(t), 15);
    check_int("date hour", neverc_time_hour(t), 10);
    check_int("date minute", neverc_time_minute(t), 30);

    t = neverc_time_date(1970, 1, 1, 0, 0, 0, 0);
    check_bool("date epoch", neverc_time_unix_sec(t) == 0, 1);
}

static void test_unix_micro(void) {
    printf("[unix_micro]\n");
    neverc_time_t t = neverc_time_unix(1000, 500000000);
    int64_t usec = neverc_time_unix_micro(t);
    check_bool("unix_micro", usec == 1000500000LL, 1);

    neverc_time_t t2 = neverc_time_unix_micro_to_time(usec);
    check_bool("unix_micro roundtrip sec", t2.sec == 1000, 1);
    check_bool("unix_micro roundtrip nsec", t2.nsec == 500000000, 1);
}

static void test_parse_duration(void) {
    printf("[parse_duration]\n");
    neverc_duration_t d;

    check_int("parse 1s", neverc_time_parse_duration("1s", &d), 0);
    check_bool("1s value", d == NEVERC_TIME_SECOND, 1);

    check_int("parse 500ms", neverc_time_parse_duration("500ms", &d), 0);
    check_bool("500ms value", d == 500 * NEVERC_TIME_MILLISECOND, 1);

    check_int("parse 1h30m", neverc_time_parse_duration("1h30m", &d), 0);
    check_bool("1h30m value", d == NEVERC_TIME_HOUR + 30 * NEVERC_TIME_MINUTE, 1);

    check_int("parse -2s", neverc_time_parse_duration("-2s", &d), 0);
    check_bool("-2s value", d == -2 * NEVERC_TIME_SECOND, 1);

    check_int("parse 100ns", neverc_time_parse_duration("100ns", &d), 0);
    check_bool("100ns value", d == 100, 1);

    check_int("parse 50us", neverc_time_parse_duration("50us", &d), 0);
    check_bool("50us value", d == 50 * NEVERC_TIME_MICROSECOND, 1);

    check_int("parse invalid", neverc_time_parse_duration("abc", &d), -1);
    check_int("parse empty", neverc_time_parse_duration("", &d), -1);
}

static void test_format_duration(void) {
    printf("[format_duration]\n");
    char *s;

    s = neverc_time_format_duration(0);
    check_bool("0s", strcmp(s, "0s") == 0, 1);
    free(s);

    s = neverc_time_format_duration(NEVERC_TIME_SECOND);
    check_bool("1s", strcmp(s, "1s") == 0, 1);
    free(s);

    s = neverc_time_format_duration(500);
    check_bool("500ns", strcmp(s, "500ns") == 0, 1);
    free(s);

    s = neverc_time_format_duration(NEVERC_TIME_HOUR + 30 * NEVERC_TIME_MINUTE);
    check_bool("1h30m", strcmp(s, "1h30m") == 0, 1);
    free(s);

    s = neverc_time_format_duration(INT64_MIN);
    check_bool("minimum duration formats", s != NULL, 1);
    if (s) check_bool("minimum duration remains negative", s[0] == '-', 1);
    free(s);
}

static void test_format_layout(void) {
    printf("[format layout]\n");
    neverc_time_t t = neverc_time_date(2024, 3, 15, 14, 30, 45, 0);
    char *s = neverc_time_format(t, "2006-01-02 15:04:05");
    check_bool("format date", strcmp(s, "2024-03-15 14:30:45") == 0, 1);
    free(s);

    s = neverc_time_format(t, "2006/01/02");
    check_bool("format date only", strcmp(s, "2024/03/15") == 0, 1);
    free(s);
}

static void test_parse_layout(void) {
    printf("[parse layout]\n");
    neverc_time_t t;
    int ok = neverc_time_parse("2006-01-02", "2024-06-15", &t);
    check_int("parse ok", ok, 0);
    check_int("parse year", neverc_time_year(t), 2024);
    check_int("parse month", neverc_time_month(t), 6);
    check_int("parse day", neverc_time_day(t), 15);
}

static void test_truncate_round(void) {
    printf("[truncate/round]\n");
    neverc_time_t t = neverc_time_date(2024, 1, 1, 12, 34, 56, 789000000);
    neverc_time_t tr = neverc_time_truncate(t, NEVERC_TIME_SECOND);
    check_int("truncate sec nsec", tr.nsec, 0);
    check_int("truncate sec same sec", neverc_time_second(tr), 56);

    neverc_time_t t2 = neverc_time_date(2024, 1, 1, 12, 0, 30, 0);
    neverc_time_t rnd = neverc_time_round(t2, NEVERC_TIME_MINUTE);
    check_int("round up sec", neverc_time_second(rnd), 0);
    check_int("round up min", neverc_time_minute(rnd), 1);
}

static void test_unix_milli_to_time(void) {
    printf("[unix_milli_to_time]\n");
    neverc_time_t t = neverc_time_unix_milli_to_time(1000);
    check_bool("1000ms == 1s", t.sec == 1 && t.nsec == 0, 1);

    t = neverc_time_unix_milli_to_time(1500);
    check_bool("1500ms sec", t.sec == 1, 1);
    check_bool("1500ms nsec", t.nsec == 500000000, 1);
}

int main(void) {
    printf("=== NeverC Time Module Tests ===\n\n");
    test_unix_epoch();
    test_zero();
    test_arithmetic();
    test_comparison();
    test_epoch_conversions();
    test_duration();
    test_format_rfc3339();
    test_parse_rfc3339();
    test_now();
    test_roundtrip();
    test_date();
    test_unix_micro();
    test_parse_duration();
    test_format_duration();
    test_format_layout();
    test_parse_layout();
    test_truncate_round();
    test_unix_milli_to_time();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
