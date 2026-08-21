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

    neverc_time_t past = neverc_time_unix(1, 0);
    check_bool("since past > 0", neverc_time_since(past) > 0, 1);
    neverc_time_t future = neverc_time_add(neverc_time_now(), NEVERC_TIME_HOUR);
    check_bool("until future > 0", neverc_time_until(future) > 0, 1);
    check_bool("since future < 0", neverc_time_since(future) < 0, 1);
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

    /* Windows gmtime_s rejects pre-1970 timestamps; accessors must not. */
    t = neverc_time_date(1969, 6, 5, 14, 30, 0, 0);
    check_int("pre-epoch year", neverc_time_year(t), 1969);
    check_int("pre-epoch month", neverc_time_month(t), 6);
    check_int("pre-epoch day", neverc_time_day(t), 5);
    check_int("pre-epoch hour", neverc_time_hour(t), 14);
    check_int("pre-epoch minute", neverc_time_minute(t), 30);
    check_int("pre-epoch weekday", neverc_time_weekday(t), 4); /* Thursday */
    check_int("pre-epoch yearday", neverc_time_yearday(t), 156);

    t = neverc_time_date(0, 1, 1, 2, 30, 0, 0);
    check_int("year-zero hour", neverc_time_hour(t), 2);
    check_int("year-zero minute", neverc_time_minute(t), 30);
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

static void test_format_unix_date(void) {
    printf("[format unix date]\n");
    /* 2024-01-05 00:00:00 UTC — Go UnixDate space-pads the day. */
    neverc_time_t t = neverc_time_unix(1704412800, 0);
    char *s = neverc_time_format_unix_date(t);
    check_str("unix date space-padded day", s, "Fri Jan  5 00:00:00 UTC 2024");
    free(s);

    t = neverc_time_unix(1705321845, 0);
    s = neverc_time_format_unix_date(t);
    check_str("unix date two-digit day", s, "Mon Jan 15 12:30:45 UTC 2024");
    free(s);

    s = neverc_time_format_unix_date(neverc_time_date(10000, 1, 1, 0, 0, 0, 0));
    check_bool("unix date rejects year 10000", s == NULL, 1);
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

    s = neverc_time_format(t, "Mon Jan 02 2006");
    check_bool("format jan mon", strcmp(s, "Fri Mar 15 2024") == 0, 1);
    free(s);

    s = neverc_time_format(t, "Monday January 02");
    check_bool("format full names", strcmp(s, "Friday March 15") == 0, 1);
    free(s);

    neverc_time_t sept = neverc_time_date(2024, 9, 4, 0, 0, 0, 0);
    s = neverc_time_format(sept, "January");
    check_bool("format january expands", strcmp(s, "September") == 0, 1);
    free(s);

    s = neverc_time_format(t, "3:04PM");
    check_bool("format 12h pm", strcmp(s, "2:30PM") == 0, 1);
    free(s);

    neverc_time_t morning = neverc_time_date(2024, 3, 15, 0, 5, 0, 0);
    s = neverc_time_format(morning, "3:04pm");
    check_bool("format 12h am", strcmp(s, "12:05am") == 0, 1);
    free(s);

    neverc_time_t fifth = neverc_time_date(2024, 1, 5, 0, 0, 0, 123456789);
    s = neverc_time_format(fifth, "Jan _2 06 MST");
    check_bool("format space day and zone", strcmp(s, "Jan  5 24 UTC") == 0, 1);
    free(s);

    s = neverc_time_format(fifth, "1/2/2006");
    check_bool("format unpadded", strcmp(s, "1/5/2024") == 0, 1);
    free(s);

    s = neverc_time_format(neverc_time_date(2024, 1, 5, 0, 5, 7, 0), "4:5");
    check_bool("format unpadded min sec", strcmp(s, "5:7") == 0, 1);
    free(s);

    s = neverc_time_format(fifth, "15:04:05.000");
    check_bool("format exact frac", strcmp(s, "00:00:00.123") == 0, 1);
    free(s);

    s = neverc_time_format(fifth, ".0001");
    check_bool("format mixed frac is literal", strcmp(s, ".0001") == 0, 1);
    free(s);

    s = neverc_time_format(neverc_time_date(2024, 1, 5, 0, 0, 0, 123456789),
                           ".0000000000");
    check_bool("format 10-zero frac is 9 digits",
               strcmp(s, ".123456789") == 0, 1);
    free(s);

    s = neverc_time_format(fifth, "15:04:05.999");
    check_bool("format trim frac", strcmp(s, "00:00:00.123") == 0, 1);
    free(s);

    s = neverc_time_format(neverc_time_date(2024, 1, 5, 0, 0, 0, 0),
                          "15:04:05.999");
    check_bool("format omit zero frac", strcmp(s, "00:00:00") == 0, 1);
    free(s);

    s = neverc_time_format(t, "2006-01-02T15:04:05Z07:00");
    check_bool("format rfc3339 layout",
               strcmp(s, "2024-03-15T14:30:45Z") == 0, 1);
    free(s);

    s = neverc_time_format(t, "2006-01-02T15:04:05Z07:00:00");
    check_bool("format Z07:00:00 is Z not Z:00",
               strcmp(s, "2024-03-15T14:30:45Z") == 0, 1);
    free(s);

    s = neverc_time_format(t, "2006-01-02T15:04:05Z07");
    check_bool("format Z07", strcmp(s, "2024-03-15T14:30:45Z") == 0, 1);
    free(s);

    s = neverc_time_format(neverc_time_date(2024, 1, 5, 0, 0, 0, 123000000),
                           "15:04:05,000");
    check_bool("format comma frac", strcmp(s, "00:00:00,123") == 0, 1);
    free(s);

    s = neverc_time_format(neverc_time_date(2024, 6, 15, 12, 0, 0, 0),
                           "2006-01-02T15:04:05Z0700");
    check_bool("format Z0700", strcmp(s, "2024-06-15T12:00:00Z") == 0, 1);
    free(s);

    s = neverc_time_format(neverc_time_date(2024, 6, 15, 12, 0, 0, 0),
                           "2006-01-02T15:04:05-07");
    check_bool("format hour-only zone",
               strcmp(s, "2024-06-15T12:00:00+00") == 0, 1);
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

    ok = neverc_time_parse("Jan 02 2006", "Jun 15 2024", &t);
    check_int("parse jan ok", ok, 0);
    check_int("parse jan month", neverc_time_month(t), 6);
    check_int("parse jan day", neverc_time_day(t), 15);

    ok = neverc_time_parse("Monday January 02 2006", "Saturday June 15 2024", &t);
    check_int("parse full names", ok, 0);
    check_int("parse full month", neverc_time_month(t), 6);

    /* Go: weekday names are syntax-checked, then ignored. */
    ok = neverc_time_parse("Mon 2006-01-02", "Sun 2024-06-15", &t);
    check_int("parse weekday mismatch is ignored", ok, 0);
    check_int("parse weekday mismatch year", neverc_time_year(t), 2024);
    check_int("parse weekday mismatch month", neverc_time_month(t), 6);
    check_int("parse weekday mismatch day", neverc_time_day(t), 15);
    check_int("parse bad month name",
              neverc_time_parse("Jan 02", "Xxx 15", &t), -1);

    ok = neverc_time_parse("2006-01-02 15:04:05", "2024-06-15  12:00:00", &t);
    check_int("parse extra value spaces", ok, 0);
    check_int("parse extra value spaces hour", neverc_time_hour(t), 12);
    ok = neverc_time_parse("2006-01-02  15:04:05", "2024-06-15 12:00:00", &t);
    check_int("parse extra layout spaces", ok, 0);
    check_int("parse extra layout spaces hour", neverc_time_hour(t), 12);

    ok = neverc_time_parse("3:04PM", "2:30PM", &t);
    check_int("parse 12h pm", ok, 0);
    check_int("parse 12h hour", neverc_time_hour(t), 14);
    check_int("parse 12h min", neverc_time_minute(t), 30);

    ok = neverc_time_parse("3:04PM", "12:05AM", &t);
    check_int("parse 12h midnight", ok, 0);
    check_int("parse 12h midnight hour", neverc_time_hour(t), 0);

    ok = neverc_time_parse("Jan _2 06", "jun  5 69", &t);
    check_int("parse space day and 2-digit year", ok, 0);
    check_int("parse 2-digit year 69", neverc_time_year(t), 1969);
    check_int("parse case-insensitive month", neverc_time_month(t), 6);
    check_int("parse space day", neverc_time_day(t), 5);

    ok = neverc_time_parse("1/2/2006", "6/5/2024", &t);
    check_int("parse unpadded", ok, 0);
    check_int("parse unpadded month", neverc_time_month(t), 6);
    check_int("parse unpadded day", neverc_time_day(t), 5);

    ok = neverc_time_parse("15:04", "9:05", &t);
    check_int("parse unpadded hour 15", ok, 0);
    check_int("parse unpadded hour val", neverc_time_hour(t), 9);
    check_int("parse unpadded minute val", neverc_time_minute(t), 5);

    ok = neverc_time_parse(".0001", ".0001", &t);
    check_int("parse mixed frac as literals", ok, 0);

    ok = neverc_time_parse(".0000000000", ".1234567890", &t);
    check_int("parse 10-zero frac", ok, 0);
    check_int("parse 10-zero nsec", neverc_time_nanosecond(t), 123456789);

    ok = neverc_time_parse("15:4:5", "12:5:7", &t);
    check_int("parse unpadded min sec", ok, 0);
    check_int("parse unpadded min", neverc_time_minute(t), 5);
    check_int("parse unpadded sec", neverc_time_second(t), 7);

    ok = neverc_time_parse("15:04:05.000", "12:30:45.123", &t);
    check_int("parse exact frac", ok, 0);
    check_int("parse frac nsec", neverc_time_nanosecond(t), 123000000);

    ok = neverc_time_parse("2006-01-02T15:04:05Z07:00",
                           "2024-06-15T12:00:00+08:00", &t);
    check_int("parse numeric zone", ok, 0);
    check_int("parse zone hour utc", neverc_time_hour(t), 4);

    ok = neverc_time_parse("2006-01-02T15:04:05Z0700",
                           "2024-06-15T12:00:00+0800", &t);
    check_int("parse Z0700", ok, 0);
    check_int("parse Z0700 hour utc", neverc_time_hour(t), 4);

    ok = neverc_time_parse("2006-01-02T15:04:05-07",
                           "2024-06-15T12:00:00+08", &t);
    check_int("parse hour-only zone", ok, 0);
    check_int("parse hour-only zone utc", neverc_time_hour(t), 4);

    check_int("parse missing exact frac",
              neverc_time_parse("15:04:05.000", "12:30:45", &t), -1);

    check_int("parse leap second rejected",
              neverc_time_parse("15:04:05", "23:59:60", &t), -1);

    ok = neverc_time_parse("2006-01-02T15:04:05Z07:00:00",
                           "2024-06-15T12:00:00Z", &t);
    check_int("parse Z07:00:00 with Z", ok, 0);
    check_int("parse Z07:00:00 Z hour", neverc_time_hour(t), 12);

    ok = neverc_time_parse("2006-01-02T15:04:05Z07:00:00",
                           "2024-06-15T12:00:00+08:00:00", &t);
    check_int("parse offset with seconds", ok, 0);
    check_int("parse offset-seconds hour utc", neverc_time_hour(t), 4);

    ok = neverc_time_parse("15:04:05,000", "12:30:45,123", &t);
    check_int("parse comma frac", ok, 0);
    check_int("parse comma frac nsec", neverc_time_nanosecond(t), 123000000);

    ok = neverc_time_parse("15:04:05.000", "12:30:45,123", &t);
    check_int("parse comma value with dot layout", ok, 0);
    check_int("parse comma value with dot layout nsec",
              neverc_time_nanosecond(t), 123000000);
    ok = neverc_time_parse("15:04:05,000", "12:30:45.123", &t);
    check_int("parse dot value with comma layout", ok, 0);
    check_int("parse dot value with comma layout nsec",
              neverc_time_nanosecond(t), 123000000);

    ok = neverc_time_parse("2006-01-02T15:04:05Z07:00",
                           "2024-01-15T12:00:00+15:00", &t);
    check_int("parse +15:00 layout offset", ok, 0);
    check_int("parse +15:00 hour utc", neverc_time_hour(t), 21);
    check_int("parse +15:00 day utc", neverc_time_day(t), 14);

    check_int("parse lowercase z rejected",
              neverc_time_parse("2006-01-02T15:04:05Z07:00",
                                "2024-06-15T12:00:00z", &t), -1);

    ok = neverc_time_parse("15:04:05.999", "12:30:45.123456789", &t);
    check_int("parse 9s extra frac digits", ok, 0);
    check_int("parse 9s extra frac nsec", neverc_time_nanosecond(t), 123456789);

    ok = neverc_time_parse("15:04:05.9", "12:30:45.123456789012", &t);
    check_int("parse 9s truncates past ns", ok, 0);
    check_int("parse 9s truncated nsec", neverc_time_nanosecond(t), 123456789);

    ok = neverc_time_parse("15:04:05", "12:30:45.123", &t);
    check_int("parse implied frac after seconds", ok, 0);
    check_int("parse implied frac nsec", neverc_time_nanosecond(t), 123000000);

    ok = neverc_time_parse("15:4:5", "12:5:7.5", &t);
    check_int("parse implied frac unpadded sec", ok, 0);
    check_int("parse implied frac unpadded nsec", neverc_time_nanosecond(t), 500000000);

    /* Go stdUnderDay: a single digit day without a pad space is valid. */
    ok = neverc_time_parse("Jan _2 06", "Jan 5 69", &t);
    check_int("parse _2 single-digit day", ok, 0);
    check_int("parse _2 single-digit year", neverc_time_year(t), 1969);
    check_int("parse _2 single-digit month", neverc_time_month(t), 1);
    check_int("parse _2 single-digit value", neverc_time_day(t), 5);

    /* Go getnum always takes two digits when both are present. */
    check_int("parse concatenated month-day rejects 21",
              neverc_time_parse("12", "215", &t), -1);
    check_int("parse concatenated min-sec rejects 60",
              neverc_time_parse("45", "605", &t), -1);

    ok = neverc_time_parse("03:04PM", "00:30AM", &t);
    check_int("parse 00 AM", ok, 0);
    check_int("parse 00 AM hour", neverc_time_hour(t), 0);
    check_int("parse 00 AM min", neverc_time_minute(t), 30);

    ok = neverc_time_parse("15:04PM", "02:30PM", &t);
    check_int("parse 24h hour with PM", ok, 0);
    check_int("parse 24h hour with PM hour", neverc_time_hour(t), 14);
    /* Go: stdHour 14 + PM leaves 14 (PM only adds 12 when hour < 12). */
    ok = neverc_time_parse("15:04PM", "14:30PM", &t);
    check_int("parse 14:30PM with 15 stays 14", ok, 0);
    check_int("parse 14:30PM hour", neverc_time_hour(t), 14);
    check_int("parse 14:30PM min", neverc_time_minute(t), 30);
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

static int64_t int64_from_bits(uint64_t bits) {
    int64_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t time_test_random_state = 0x9e3779b97f4a7c15ULL;

static uint64_t time_test_random(void) {
    uint64_t x = time_test_random_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    time_test_random_state = x;
    return x * 0x2545f4914f6cdd1dULL;
}

static void test_overflow_safety(void) {
    printf("[overflow safety]\n");
    neverc_time_t t = neverc_time_unix(INT64_MAX, NEVERC_TIME_SECOND);
    check_int64("unix wraps positive sec", t.sec, INT64_MIN);
    check_int("unix wraps positive nsec", t.nsec, 0);
    t = neverc_time_unix(INT64_MIN, -NEVERC_TIME_SECOND);
    check_int64("unix wraps negative sec", t.sec, INT64_MAX);
    check_int("unix wraps negative nsec", t.nsec, 0);

    t.sec = INT64_MAX;
    t.nsec = 999999999;
    neverc_time_t added = neverc_time_add(t, 1);
    check_int64("add wraps sec", added.sec, INT64_MIN);
    check_int("add normalizes nsec", added.nsec, 0);

    neverc_time_t lo = {INT64_MIN, 0};
    neverc_time_t hi = {INT64_MAX, 999999999};
    check_int64("sub saturates positive", neverc_time_sub(hi, lo), INT64_MAX);
    check_int64("sub saturates negative", neverc_time_sub(lo, hi), INT64_MIN);

    check_int64("unix nano wraps", neverc_time_unix_nano(hi),
                int64_from_bits((uint64_t)INT64_MAX * 1000000000ULL +
                                999999999ULL));
    check_int64("unix milli wraps", neverc_time_unix_milli(hi),
                int64_from_bits((uint64_t)INT64_MAX * 1000ULL + 999ULL));
    check_int64("unix micro wraps", neverc_time_unix_micro(hi),
                int64_from_bits((uint64_t)INT64_MAX * 1000000ULL + 999999ULL));

    neverc_time_t far = {INT64_MAX, 123456789};
    neverc_time_t truncated = neverc_time_truncate(far, NEVERC_TIME_SECOND);
    check_int64("far truncate sec", truncated.sec, INT64_MAX);
    check_int("far truncate nsec", truncated.nsec, 0);

    neverc_time_t round_input = neverc_time_unix(5000000000LL, 0);
    neverc_time_t rounded = neverc_time_round(round_input, INT64_MAX);
    check_int64("large duration round sec", rounded.sec, 9223372036LL);
    check_int("large duration round nsec", rounded.nsec, 854775807);
}

static void test_strict_rfc3339(void) {
    printf("[strict rfc3339]\n");
    static const char *invalid[] = {
        "2024-01-15T12:30:45",
        "2024-01-15T12:30:45Zjunk",
        "2024-01-15T12:30:45.Z",
        "2024-01-15T12:30:45+25:00",
        "2024-01-15T12:30:45+08:61",
        "2024-01-15 12:30:45Z",
        "2024-01-15t12:30:45z",
        "2024-01-15T12:30:61Z",
        "2024-01-15T12:30:60Z"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        neverc_time_t out = {123, 456};
        check_int("reject malformed rfc3339",
                  neverc_time_parse_rfc3339(invalid[i], &out), -1);
        check_bool("malformed rfc3339 is atomic",
                   out.sec == 123 && out.nsec == 456, 1);
    }

    neverc_time_t out;
    check_int("accept +14:00",
              neverc_time_parse_rfc3339("2024-01-15T12:00:00+14:00", &out), 0);
    check_int("accept -14:00",
              neverc_time_parse_rfc3339("2024-01-15T12:00:00-14:00", &out), 0);
    /* Go parseRFC3339 allows hour 0-23; Parse(RFC3339) then accepts 24/60
     * via the layout fallback (hr > 24 / mm > 60). */
    check_int("accept +23:59",
              neverc_time_parse_rfc3339("2024-01-15T12:30:45+23:59", &out), 0);
    check_int64("utc of +23:59", out.sec, 1705321845LL - (23 * 3600 + 59 * 60));
    check_int("accept +14:01",
              neverc_time_parse_rfc3339("2024-01-15T12:00:00+14:01", &out), 0);
    check_int64("utc of +14:01", out.sec, 1705320000LL - (14 * 3600 + 60));
    check_int("accept -14:01",
              neverc_time_parse_rfc3339("2024-01-15T12:00:00-14:01", &out), 0);
    check_int64("utc of -14:01", out.sec, 1705320000LL + (14 * 3600 + 60));
    check_int("accept +24:00",
              neverc_time_parse_rfc3339("2024-01-15T12:30:45+24:00", &out), 0);
    check_int64("utc of +24:00", out.sec, 1705321845LL - 24 * 3600);
    check_int("accept +08:60",
              neverc_time_parse_rfc3339("2024-01-15T12:30:45+08:60", &out), 0);
    check_int64("utc of +08:60", out.sec, 1705321845LL - (8 * 3600 + 60 * 60));
    {
        neverc_time_t layout_out;
        check_int("layout parse +23:59",
                  neverc_time_parse("2006-01-02T15:04:05Z07:00",
                                    "2024-01-15T12:30:45+23:59",
                                    &layout_out), 0);
        check_int("rfc3339 +23:59 matches layout",
                  neverc_time_parse_rfc3339("2024-01-15T12:30:45+23:59",
                                            &out), 0);
        check_bool("rfc3339/layout +23:59 same instant",
                   out.sec == layout_out.sec && out.nsec == layout_out.nsec, 1);
    }
    check_int("accept compact +0800",
              neverc_time_parse_rfc3339("2024-01-15T12:30:45+0800", &out), 0);
    {
        neverc_time_t hour_only, with_minutes;
        check_int("accept hour-only +08",
                  neverc_time_parse_rfc3339("2024-01-15T12:00:00+08",
                                            &hour_only), 0);
        check_int("hour-only matches +08:00",
                  neverc_time_parse_rfc3339("2024-01-15T12:00:00+08:00",
                                            &with_minutes), 0);
        check_bool("hour-only offset equals +08:00",
                   hour_only.sec == with_minutes.sec &&
                   hour_only.nsec == with_minutes.nsec, 1);
    }
    check_int("reject leap second 60",
              neverc_time_parse_rfc3339("2024-01-15T12:30:60Z", &out), -1);
    check_int("null rfc3339 input", neverc_time_parse_rfc3339(NULL, &out), -1);
    check_int("null rfc3339 output",
              neverc_time_parse_rfc3339("1970-01-01T00:00:00Z", NULL), -1);

    neverc_time_t fractional = neverc_time_unix(0, 123400000);
    char *formatted = neverc_time_format_rfc3339(fractional);
    check_str("fractional rfc3339", formatted,
              "1970-01-01T00:00:00.1234Z");
    free(formatted);
}

static void test_duration_boundaries(void) {
    printf("[duration boundaries]\n");
    neverc_duration_t d = 17;
    check_int("parse unitless zero", neverc_time_parse_duration("0", &d), 0);
    check_int64("unitless zero value", d, 0);
    check_int("parse leading fraction", neverc_time_parse_duration(".5s", &d), 0);
    check_int64("leading fraction value", d, 500000000);
    check_int("parse negative leading fraction",
              neverc_time_parse_duration("-.5s", &d), 0);
    check_int64("negative leading fraction value", d, -500000000);
    check_int("parse Greek mu", neverc_time_parse_duration("2\xce\xbc" "s", &d), 0);
    check_int64("Greek mu value", d, 2000);
    check_int("parse micro sign", neverc_time_parse_duration("2\xc2\xb5" "s", &d), 0);
    check_int64("micro sign value", d, 2000);

    check_int("parse trailing-dot seconds", neverc_time_parse_duration("5.s", &d), 0);
    check_int64("trailing-dot seconds value", d, 5 * NEVERC_TIME_SECOND);
    check_int("parse leading-dot hours",
              neverc_time_parse_duration("0.3333333333333333333h", &d), 0);
    check_int64("long fraction hour is 20m", d, 20 * NEVERC_TIME_MINUTE);
    check_int("parse mixed fraction then minutes",
              neverc_time_parse_duration("10.5s4m", &d), 0);
    check_int64("mixed fraction then minutes value", d,
                4 * NEVERC_TIME_MINUTE + 10 * NEVERC_TIME_SECOND +
                    500 * NEVERC_TIME_MILLISECOND);

    d = 99;
    check_int("reject missing unit", neverc_time_parse_duration("1", &d), -1);
    check_int64("missing unit is atomic", d, 99);
    check_int("reject unknown unit", neverc_time_parse_duration("1d", &d), -1);
    check_int("reject dot without digits", neverc_time_parse_duration(".s", &d), -1);
    check_int("reject unitless sign", neverc_time_parse_duration("+", &d), -1);

    check_int("parse min composite duration",
              neverc_time_parse_duration("-2562047h47m16.854775808s", &d), 0);
    check_int64("min composite duration value", d, INT64_MIN);

    check_int("parse max duration",
              neverc_time_parse_duration("9223372036854775807ns", &d), 0);
    check_int64("max duration value", d, INT64_MAX);
    check_int("parse min duration",
              neverc_time_parse_duration("-9223372036854775808ns", &d), 0);
    check_int64("min duration value", d, INT64_MIN);

    d = 99;
    check_int("reject positive duration overflow",
              neverc_time_parse_duration("9223372036854775808ns", &d), -1);
    check_int64("duration overflow is atomic", d, 99);
    check_int("reject accumulated duration overflow",
              neverc_time_parse_duration("2562047h47m16.854775808s", &d), -1);
    check_int("reject huge duration",
              neverc_time_parse_duration("999999999999999999999999999h", &d), -1);

    {
        neverc_duration_t out = 99;
        check_int("duration mul by 0",
                  neverc_time_duration_mul(INT64_MAX, 0, &out), 0);
        check_int64("duration mul 0 value", out, 0);
        out = 99;
        check_int("duration mul by 1",
                  neverc_time_duration_mul(123, 1, &out), 0);
        check_int64("duration mul 1 value", out, 123);
        check_int("duration mul by -1",
                  neverc_time_duration_mul(123, -1, &out), 0);
        check_int64("duration mul -1 value", out, -123);
        out = 99;
        check_int("duration mul overflow",
                  neverc_time_duration_mul(INT64_MAX, 2, &out), -1);
        check_int64("duration mul overflow is atomic", out, 99);
        check_int("duration mul INT64_MIN * -1 overflow",
                  neverc_time_duration_mul(INT64_MIN, -1, &out), -1);
        check_int64("duration mul min overflow is atomic", out, 99);
        check_int("duration mul null out",
                  neverc_time_duration_mul(1, 1, NULL), -1);
    }

    static const neverc_duration_t values[] = {
        INT64_MIN, INT64_MAX, -1234567890123456LL, -1001, -1,
        0, 1, 1001, 1234567890123456LL
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        char *text = neverc_time_format_duration(values[i]);
        neverc_duration_t parsed = 0;
        check_bool("duration format allocation", text != NULL, 1);
        if (text) {
            check_int("formatted duration parses",
                      neverc_time_parse_duration(text, &parsed), 0);
            check_int64("duration format roundtrip", parsed, values[i]);
        }
        free(text);
    }
}

static void test_strict_layout_and_date_normalization(void) {
    printf("[strict layout/date normalization]\n");
    neverc_time_t out = {77, 88};
    static const char *invalid_values[] = {
        "2024/06/15", "2024-6-15", "2024-06-15junk",
        "2024-13-01", "2024-02-30"
    };
    for (size_t i = 0; i < sizeof(invalid_values) / sizeof(invalid_values[0]); i++) {
        out.sec = 77;
        out.nsec = 88;
        check_int("reject malformed layout value",
                  neverc_time_parse("2006-01-02", invalid_values[i], &out), -1);
        check_bool("layout failure is atomic", out.sec == 77 && out.nsec == 88, 1);
    }
    check_int("null layout output",
              neverc_time_parse("2006", "2024", NULL), -1);

    neverc_time_t normalized = neverc_time_date(2024, 1, 1, 24, 0, 0, -1);
    check_int("normalized date year", neverc_time_year(normalized), 2024);
    check_int("normalized date month", neverc_time_month(normalized), 1);
    check_int("normalized date day", neverc_time_day(normalized), 1);
    check_int("normalized date hour", neverc_time_hour(normalized), 23);
    check_int("normalized date minute", neverc_time_minute(normalized), 59);
    check_int("normalized date second", neverc_time_second(normalized), 59);
    check_int("normalized date nsec", normalized.nsec, 999999999);

    char long_layout[401];
    memset(long_layout, 'x', sizeof(long_layout) - 1);
    long_layout[sizeof(long_layout) - 1] = '\0';
    char *formatted = neverc_time_format(neverc_time_unix(0, 0), long_layout);
    check_bool("long layout allocation", formatted != NULL, 1);
    if (formatted)
        check_int64("long layout is not truncated", (int64_t)strlen(formatted), 400);
    free(formatted);
}

static void test_randomized_arithmetic(void) {
    printf("[randomized arithmetic]\n");
    for (int i = 0; i < 500; i++) {
        int64_t sec = (int64_t)(time_test_random() % 2000000001ULL) -
                      1000000000LL;
        int32_t nsec = (int32_t)(time_test_random() % 1000000000ULL);
        neverc_time_t t = {sec, nsec};
        neverc_duration_t d =
            (neverc_duration_t)(time_test_random() % 1000000000000000ULL) + 1;
        int64_t total = sec * NEVERC_TIME_SECOND + nsec;
        int64_t remainder = total % d;
        if (remainder < 0) remainder += d;
        int64_t truncated = total - remainder;
        int64_t rounded = remainder < d - remainder
            ? truncated
            : total + (d - remainder);

        check_int64("random truncate",
                    neverc_time_unix_nano(neverc_time_truncate(t, d)),
                    truncated);
        check_int64("random round",
                    neverc_time_unix_nano(neverc_time_round(t, d)),
                    rounded);

        neverc_duration_t delta =
            (neverc_duration_t)(time_test_random() % 2000000000000001ULL) -
            1000000000000000LL;
        neverc_time_t added = neverc_time_add(t, delta);
        check_int64("random add/sub", neverc_time_sub(added, t), delta);
    }

    for (int i = 0; i < 500; i++) {
        neverc_duration_t value = int64_from_bits(time_test_random());
        char *text = neverc_time_format_duration(value);
        neverc_duration_t parsed = 0;
        check_bool("random duration format", text != NULL, 1);
        if (text) {
            check_int("random duration parse",
                      neverc_time_parse_duration(text, &parsed), 0);
            check_int64("random duration roundtrip", parsed, value);
        }
        free(text);
    }
}

/* 2024-03-10 07:00:00 UTC = US spring-forward. */
#define NY_SPRING_2024 1710054000LL
/* 2024-11-03 06:00:00 UTC = US fall-back. */
#define NY_FALL_2024   1730613600LL

static int ny_offset_at(int64_t unix_sec, void *ctx) {
    (void)ctx;
    if (unix_sec >= NY_SPRING_2024 && unix_sec < NY_FALL_2024)
        return -14400;
    return -18000;
}

static void test_format_layout_go_tokens(void) {
    printf("[format layout go tokens]\n");
    neverc_time_t jan5 = neverc_time_date(2024, 1, 5, 0, 0, 0, 0);
    char *s = neverc_time_format(jan5, "002");
    check_str("format 002 yearday", s, "005");
    free(s);

    s = neverc_time_format(jan5, "__2");
    check_str("format __2 yearday", s, "  5");
    free(s);

    neverc_time_t day100 = neverc_time_date(2024, 4, 9, 0, 0, 0, 0);
    check_int("april 9 yearday", neverc_time_yearday(day100), 100);
    s = neverc_time_format(day100, "002");
    check_str("format 002 day 100", s, "100");
    free(s);
    s = neverc_time_format(day100, "__2");
    check_str("format __2 day 100", s, "100");
    free(s);

    s = neverc_time_format(jan5, "_2006");
    check_str("format _2006 is literal underscore year", s, "_2024");
    free(s);

    s = neverc_time_format(jan5, "Janitor");
    check_str("format Janitor is literal", s, "Janitor");
    free(s);

    s = neverc_time_format(jan5, "Money");
    check_str("format Money is literal", s, "Money");
    free(s);

    s = neverc_time_format(jan5, "Januaryish");
    check_str("format Januaryish keeps suffix", s, "Januaryish");
    free(s);
}

static void test_parse_layout_go_tokens(void) {
    printf("[parse layout go tokens]\n");
    neverc_time_t t;
    int ok = neverc_time_parse("002 2006", "005 2024", &t);
    check_int("parse 002 ok", ok, 0);
    check_int("parse 002 month", neverc_time_month(t), 1);
    check_int("parse 002 day", neverc_time_day(t), 5);

    ok = neverc_time_parse("__2 2006", "  5 2024", &t);
    check_int("parse __2 ok", ok, 0);
    check_int("parse __2 day", neverc_time_day(t), 5);

    ok = neverc_time_parse("002 2006", "167 2024", &t);
    check_int("parse 002 june", ok, 0);
    check_int("parse 002 june month", neverc_time_month(t), 6);
    check_int("parse 002 june day", neverc_time_day(t), 15);

    ok = neverc_time_parse("002 2006", "366 2024", &t);
    check_int("parse 002 leap 366", ok, 0);
    check_int("parse 002 leap month", neverc_time_month(t), 12);
    check_int("parse 002 leap day", neverc_time_day(t), 31);

    check_int("parse 002 non-leap 366",
              neverc_time_parse("002 2006", "366 2023", &t), -1);
    check_int("parse 002 yday 0",
              neverc_time_parse("002 2006", "000 2024", &t), -1);

    ok = neverc_time_parse("_2006", "_2024", &t);
    check_int("parse _2006 ok", ok, 0);
    check_int("parse _2006 year", neverc_time_year(t), 2024);

    check_int("parse Janitor is literal",
              neverc_time_parse("Janitor 2006", "Janitor 2024", &t), 0);
    check_int("parse Janitor year", neverc_time_year(t), 2024);

    ok = neverc_time_parse("MST", "GMT+8", &t);
    check_int("parse GMT+8", ok, 0);
    /* 0000-01-01 00:00 GMT+8 → previous day 16:00 UTC */
    check_int("parse GMT+8 hour utc", neverc_time_hour(t), 16);

    check_int("parse lowercase zone rejected",
              neverc_time_parse("MST", "est", &t), -1);

    ok = neverc_time_parse("MST", "WITA", &t);
    check_int("parse WITA", ok, 0);
    ok = neverc_time_parse("MST", "ChST", &t);
    check_int("parse ChST", ok, 0);
    check_int("parse MeST", neverc_time_parse("MST", "MeST", &t), 0);
}

static void test_parse_in_location_dst(void) {
    printf("[parse in location dst]\n");
    neverc_time_location_t ny = {
        -18000, -14400, "EST", "EDT", ny_offset_at, NULL
    };
    neverc_time_t t;

    int ok = neverc_time_parse_in_location("2006-01-02 15:04:05",
                                           "2024-01-15 12:00:00", &ny, &t);
    check_int("NY winter parse", ok, 0);
    check_int64("NY winter utc", t.sec, 1705320000LL + 18000);

    ok = neverc_time_parse_in_location("2006-01-02 15:04:05",
                                       "2024-07-15 12:00:00", &ny, &t);
    check_int("NY summer parse", ok, 0);
    /* 2024-07-15 12:00 EDT = 16:00 UTC */
    check_int("NY summer hour utc", neverc_time_hour(t), 16);

    ok = neverc_time_parse_in_location("2006-01-02 15:04:05",
                                       "2024-03-10 02:30:00", &ny, &t);
    check_int("NY gap parse", ok, 0);
    check_int64("NY gap uses later-zone offset", t.sec, 1710052200LL);

    ok = neverc_time_parse_in_location("2006-01-02 15:04:05",
                                       "2024-11-03 01:30:00", &ny, &t);
    check_int("NY overlap parse", ok, 0);
    check_int64("NY overlap prefers first (EDT)", t.sec, 1730611800LL);

    ok = neverc_time_parse_in_location("2006-01-02 15:04:05 MST",
                                       "2024-11-03 01:30:00 EST", &ny, &t);
    check_int("NY overlap EST name", ok, 0);
    check_int64("NY overlap EST instant", t.sec, 1730615400LL);

    ok = neverc_time_parse_in_location("2006-01-02 15:04:05 MST",
                                       "2024-11-03 01:30:00 EDT", &ny, &t);
    check_int("NY overlap EDT name", ok, 0);
    check_int64("NY overlap EDT instant", t.sec, 1730611800LL);

    neverc_time_t d = neverc_time_date_in_location(2024, 3, 10, 2, 30, 0, 0, &ny);
    check_int64("DateInLocation gap", d.sec, 1710052200LL);
}

static void test_negative_duration_ops(void) {
    printf("[negative duration ops]\n");
    neverc_time_t t = neverc_time_unix(1000, 0);
    neverc_time_t back = neverc_time_add(t, -5 * NEVERC_TIME_SECOND);
    check_int64("add negative", neverc_time_unix_sec(back), 995);
    check_int64("sub negative", neverc_time_sub(back, t),
                -5 * NEVERC_TIME_SECOND);

    neverc_duration_t d;
    check_int("parse -1h30m", neverc_time_parse_duration("-1h30m", &d), 0);
    check_int64("parse -1h30m value", d,
                -(NEVERC_TIME_HOUR + 30 * NEVERC_TIME_MINUTE));
    char *s = neverc_time_format_duration(d);
    check_str("format -1h30m", s, "-1h30m");
    free(s);

    neverc_time_t neg = neverc_time_unix(-2, 500000000);
    neverc_time_t tr = neverc_time_truncate(neg, NEVERC_TIME_SECOND);
    check_int64("truncate toward -inf sec", tr.sec, -2);
    check_int("truncate toward -inf nsec", tr.nsec, 0);

    neverc_time_t halfway = neverc_time_unix(0, -500000000);
    neverc_time_t rnd = neverc_time_round(halfway, NEVERC_TIME_SECOND);
    check_int64("round halfway up toward +inf", rnd.sec, 0);
    check_int("round halfway nsec", rnd.nsec, 0);
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
    test_format_unix_date();
    test_format_layout();
    test_parse_layout();
    test_truncate_round();
    test_unix_milli_to_time();
    test_overflow_safety();
    test_strict_rfc3339();
    test_duration_boundaries();
    test_strict_layout_and_date_normalization();
    test_randomized_arithmetic();
    test_format_layout_go_tokens();
    test_parse_layout_go_tokens();
    test_parse_in_location_dst();
    test_negative_duration_ops();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
