/*
 * A/B benchmark + correctness check: time date<->timestamp + layout format.
 *
 *  - old_date / old_parse_days — the previous library code, reproduced
 *      verbatim: the days-since-epoch value was built with O(year) loops that
 *      added one term per year from 1970 plus one per month. The cost grew with
 *      how far the year is from 1970 (year 9999 => ~8000 loop iterations).
 *
 *  - neverc_time_date / neverc_time_parse_rfc3339 (library) — now use a closed
 *      form (Howard Hinnant's days_from_civil), O(1) regardless of year.
 *
 *  - old_format — the previous neverc_time_format body: it called six accessors
 *      (year/month/day/hour/minute/second), each doing its own gmtime_r, then
 *      formatted each field with snprintf.
 *
 *  - neverc_time_format (library) — decomposes once and uses a hand-written
 *      zero-padded integer writer.
 *
 * Every transformation is behavior-preserving, so outputs are asserted equal to
 * the old outputs across a full year sweep before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/time_bench \
 *      tests/neverc/std/time_bench.c std/src/time/time.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/time.h"

/* ============================================================
 * OLD implementations — verbatim reproduction
 * ============================================================ */
static int o_is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}
static int o_days_in_month(int y, int m) {
    static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && o_is_leap(y)) return 29;
    return dm[m - 1];
}

static int64_t old_days(int year, int month, int day) {
    int64_t days = 0;
    for (int y = 1970; y < year; y++)
        days += o_is_leap(y) ? 366 : 365;
    for (int y = year; y < 1970; y++)
        days -= o_is_leap(y) ? 366 : 365;
    for (int m = 1; m < month; m++)
        days += o_days_in_month(year, m);
    days += day - 1;
    return days;
}

static neverc_time_t old_date(int year, int month, int day,
                              int hour, int min, int sec, int nsec) {
    int64_t days = old_days(year, month, day);
    neverc_time_t t;
    t.sec = days * 86400 + hour * 3600 + min * 60 + sec;
    t.nsec = nsec;
    return t;
}

/* Previous neverc_time_format body: six accessors (each gmtime_r) + snprintf. */
static char *old_format(neverc_time_t t, const char *layout) {
    if (!layout) return NULL;
    int yr = neverc_time_year(t);
    int mo = neverc_time_month(t);
    int dy = neverc_time_day(t);
    int hr = neverc_time_hour(t);
    int mi = neverc_time_minute(t);
    int sc = neverc_time_second(t);

    char buf[256];
    size_t out = 0, llen = strlen(layout);
    for (size_t i = 0; i < llen && out < sizeof(buf) - 20;) {
        if (i + 4 <= llen && memcmp(layout + i, "2006", 4) == 0) {
            out += (size_t)snprintf(buf + out, sizeof(buf) - out, "%04d", yr); i += 4;
        } else if (i + 2 <= llen && memcmp(layout + i, "01", 2) == 0) {
            out += (size_t)snprintf(buf + out, sizeof(buf) - out, "%02d", mo); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "02", 2) == 0) {
            out += (size_t)snprintf(buf + out, sizeof(buf) - out, "%02d", dy); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "15", 2) == 0) {
            out += (size_t)snprintf(buf + out, sizeof(buf) - out, "%02d", hr); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "04", 2) == 0) {
            out += (size_t)snprintf(buf + out, sizeof(buf) - out, "%02d", mi); i += 2;
        } else if (i + 2 <= llen && memcmp(layout + i, "05", 2) == 0) {
            out += (size_t)snprintf(buf + out, sizeof(buf) - out, "%02d", sc); i += 2;
        } else {
            buf[out++] = layout[i++];
        }
    }
    buf[out] = '\0';
    char *result = (char *)malloc(out + 1);
    if (result) memcpy(result, buf, out + 1);
    return result;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int64_t sink;

/* ============================================================
 * Correctness sweeps
 * ============================================================ */
static int sweep_date(void) {
    int mism = 0;
    for (int year = 1; year <= 9999; year++) {
        for (int month = 1; month <= 12; month++) {
            int dlast = o_days_in_month(year, month);
            int days_try[4] = { 1, 15, 28, dlast };
            for (int k = 0; k < 4; k++) {
                int day = days_try[k];
                neverc_time_t o = old_date(year, month, day, 13, 7, 5, 123);
                neverc_time_t n = neverc_time_date(year, month, day, 13, 7, 5, 123);
                if (o.sec != n.sec || o.nsec != n.nsec) {
                    if (mism < 6)
                        printf("  DATE MISMATCH %04d-%02d-%02d old=%lld new=%lld\n",
                               year, month, day, (long long)o.sec, (long long)n.sec);
                    mism++;
                }
            }
        }
    }
    return mism;
}

/* parse_rfc3339 uses the same closed form internally; verify via round-trip
 * against the old day arithmetic for a spread of dates. */
static int sweep_parse(void) {
    int mism = 0;
    char s[40];
    for (int year = 1; year <= 9999; year += 1) {
        for (int month = 1; month <= 12; month++) {
            int day = o_days_in_month(year, month);
            snprintf(s, sizeof s, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     year, month, day, 23, 59, 59);
            neverc_time_t got;
            int rc = neverc_time_parse_rfc3339(s, &got);
            int64_t want = old_days(year, month, day) * 86400 + 23*3600 + 59*60 + 59;
            if (rc != 0 || got.sec != want) {
                if (mism < 6)
                    printf("  PARSE MISMATCH \"%s\" rc=%d got=%lld want=%lld\n",
                           s, rc, (long long)got.sec, (long long)want);
                mism++;
            }
        }
    }
    return mism;
}

static int sweep_format(void) {
    int mism = 0;
    const char *layouts[] = {
        "2006-01-02T15:04:05",
        "2006/01/02 15:04:05",
        "15:04:05 on 2006-01-02",
        "Year 2006 Month 01 Day 02",
    };
    int nl = (int)(sizeof(layouts)/sizeof(layouts[0]));
    /* spread of timestamps across decades */
    for (int64_t base = -62135596800LL; base < 253402300799LL; base += 911617200LL) {
        neverc_time_t t; t.sec = base; t.nsec = 0;
        for (int j = 0; j < nl; j++) {
            char *o = old_format(t, layouts[j]);
            char *n = neverc_time_format(t, layouts[j]);
            if (!o || !n || strcmp(o, n) != 0) {
                if (mism < 6) printf("  FMT MISMATCH sec=%lld old=\"%s\" new=\"%s\"\n",
                                     (long long)base, o ? o : "(null)", n ? n : "(null)");
                mism++;
            }
            free(o); free(n);
        }
    }
    return mism;
}

/* ============================================================
 * Timing
 * ============================================================ */
static void bench_date(const char *label, int year, int iters) {
    neverc_time_t o = old_date(year, 6, 15, 12, 30, 45, 0);
    neverc_time_t n = neverc_time_date(year, 6, 15, 12, 30, 45, 0);
    if (o.sec != n.sec) { printf("%-22s CORRECTNESS FAIL\n", label); return; }

    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_date(year, 6, 15, 12, 30, 45, 0).sec; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = neverc_time_date(year, 6, 15, 12, 30, 45, 0).sec; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %7.2fx   (year %d, %d iters)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, year, iters);
}

static void bench_parse(const char *label, const char *s, int iters) {
    neverc_time_t tmp;
    if (neverc_time_parse_rfc3339(s, &tmp) != 0) { printf("%-22s PARSE FAIL\n", label); return; }
    double t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_time_t t2; neverc_time_parse_rfc3339(s, &t2); sink = t2.sec; }
        double e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8s     %8.1f ms       -      (\"%s\")\n", label, "-", t_new * 1000, s);
}

static void bench_format(const char *label, int iters) {
    neverc_time_t t = neverc_time_date(2026, 6, 14, 7, 32, 10, 0);
    const char *layout = "2006-01-02T15:04:05";
    char *o = old_format(t, layout);
    char *n = neverc_time_format(t, layout);
    int ok = o && n && strcmp(o, n) == 0;
    free(o); free(n);
    if (!ok) { printf("%-22s CORRECTNESS FAIL\n", label); return; }

    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *p = old_format(t, layout); sink = (int64_t)(size_t)p; free(p); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *p = neverc_time_format(t, layout); sink = (int64_t)(size_t)p; free(p); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %7.2fx   (6x gmtime_r -> 1x)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new);
}

int main(void) {
    printf("=== time: O(1) days_from_civil + single-decompose format (new) vs O(year) loops + 6x gmtime_r (old) ===\n\n");

    printf("--- correctness sweeps ---\n");
    int total = 0, m;
    m = sweep_date();   total += m; printf("  date  (yr 1..9999, 12 mo, 4 days): %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_parse();  total += m; printf("  parse (yr 1..9999, month ends)   : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_format(); total += m; printf("  format (timestamps x 4 layouts)  : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    printf("  => %s\n\n", total ? "CORRECTNESS FAILED" : "all identical");

    /* Timing iteration scale-down for sanitizer runs (-DBSCALE=1000); the
     * correctness sweeps above always run in full. */
#ifndef BSCALE
#define BSCALE 1
#endif
    printf("--- timing ---\n");
    printf("%-22s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    bench_date("date near (2026)", 2026, 3000000 / BSCALE);
    /* The old reference path is O(|year-1970|): at year 9999 every call loops
     * ~8000x, ~140x more work per call than 2026. Use 10x fewer iters here so
     * the row still yields a stable >1s old-path measurement without letting
     * this single line dominate wall-clock (the run harness kills >240s). */
    bench_date("date far (9999)",  9999, 300000 / BSCALE);
    bench_parse("parse far (9999)", "9999-12-31T23:59:59Z", 3000000 / BSCALE);
    bench_format("format layout", 2000000 / BSCALE);

    printf("\n=== Done ===\n");
    return total ? 1 : 0;
}
