#include "neverc/std/flag.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else if (!got && !expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n",
               name, got ? got : "(null)", expected ? expected : "(null)"); }
}
static void check_double(const char *name, double got, double expected) {
    tests_run++;
    double diff = got - expected;
    if (diff < 0) diff = -diff;
    if (diff < 1e-9) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %f, expected %f\n", name, got, expected); }
}

static void test_basic(void) {
    printf("[basic]\n");
    neverc_flag_reset();

    const char *name = NULL;
    int port = 0;
    int verbose = 0;
    double rate = 0.0;

    neverc_flag_string("name", "default", "user name", &name);
    neverc_flag_int("port", 8080, "server port", &port);
    neverc_flag_bool("verbose", 0, "verbose mode", &verbose);
    neverc_flag_double("rate", 1.0, "rate limit", &rate);

    char *argv[] = {"prog", "-name", "Alice", "-port", "3000", "-verbose",
                    "-rate", "2.5"};
    int argc = 8;
    int err = neverc_flag_parse(argc, argv);

    check_int("parse ok", err, 0);
    check_str("name", name, "Alice");
    check_int("port", port, 3000);
    check_int("verbose", verbose, 1);
    check_double("rate", rate, 2.5);
}

static void test_defaults(void) {
    printf("[defaults]\n");
    neverc_flag_reset();

    const char *name = NULL;
    int port = 0;
    int verbose = 0;

    neverc_flag_string("name", "Bob", "user name", &name);
    neverc_flag_int("port", 8080, "server port", &port);
    neverc_flag_bool("verbose", 0, "verbose mode", &verbose);

    char *argv[] = {"prog"};
    neverc_flag_parse(1, argv);

    check_str("default name", name, "Bob");
    check_int("default port", port, 8080);
    check_int("default verbose", verbose, 0);
}

static void test_equals_syntax(void) {
    printf("[equals syntax]\n");
    neverc_flag_reset();

    const char *name = NULL;
    int port = 0;

    neverc_flag_string("name", "", "user name", &name);
    neverc_flag_int("port", 0, "port", &port);

    char *argv[] = {"prog", "-name=Charlie", "--port=9090"};
    neverc_flag_parse(3, argv);

    check_str("eq name", name, "Charlie");
    check_int("eq port", port, 9090);
}

static void test_remaining_args(void) {
    printf("[remaining args]\n");
    neverc_flag_reset();

    int verbose = 0;
    neverc_flag_bool("v", 0, "verbose", &verbose);

    char *argv[] = {"prog", "-v", "file1.txt", "file2.txt"};
    neverc_flag_parse(4, argv);

    check_int("verbose on", verbose, 1);
    check_int("positional parse marks parsed", neverc_flag_parsed(), 1);
    check_int("narg", neverc_flag_narg(), 2);
    check_str("arg0", neverc_flag_arg(0), "file1.txt");
    check_str("arg1", neverc_flag_arg(1), "file2.txt");
}

static void test_lone_dash(void) {
    printf("[lone dash]\n");
    neverc_flag_reset();

    int n = 0;
    neverc_flag_int("n", 0, "count", &n);

    char *argv[] = {"prog", "-", "file.txt"};
    check_int("lone dash parse ok", neverc_flag_parse(3, argv), 0);
    check_int("lone dash is positional", neverc_flag_narg(), 2);
    check_str("lone dash arg0", neverc_flag_arg(0), "-");
    check_str("lone dash arg1", neverc_flag_arg(1), "file.txt");
}

static void test_base_prefixes(void) {
    printf("[base prefixes]\n");
    neverc_flag_reset();

    int port = 0;
    long long big = 0;
    unsigned long long ubig = 0;
    neverc_flag_int("port", 0, "port", &port);
    neverc_flag_int64("big", 0, "big", &big);
    neverc_flag_uint64("ubig", 0, "ubig", &ubig);

    char *argv[] = {"prog", "-port", "0x10", "-big", "0b1000", "-ubig", "0o17"};
    check_int("base prefix parse ok", neverc_flag_parse(7, argv), 0);
    check_int("hex int", port, 16);
    check_int("bin int64", (int)big, 8);
    check_int("oct uint64", (int)ubig, 15);
}

static void test_double_dash(void) {
    printf("[double dash]\n");
    neverc_flag_reset();

    int n = 0;
    neverc_flag_int("n", 0, "count", &n);

    char *argv[] = {"prog", "-n", "5", "--", "-other", "stuff"};
    neverc_flag_parse(6, argv);

    check_int("n val", n, 5);
    check_int("narg after --", neverc_flag_narg(), 2);
    check_str("arg0 after --", neverc_flag_arg(0), "-other");
}

static void test_value_cannot_be_terminator(void) {
    printf("[value cannot be terminator]\n");
    neverc_flag_reset();
    const char *name = "def";
    neverc_flag_string("name", "def", "user", &name);
    char *argv[] = {"prog", "-name", "--", "file.txt"};
    check_int("missing value before --", neverc_flag_parse(4, argv), -1);
    check_str("name unchanged", name, "def");
}

static void test_int64_uint64(void) {
    printf("[int64/uint64]\n");
    neverc_flag_reset();

    long long big = 0;
    unsigned long long ubig = 0;
    neverc_flag_int64("big", 0, "big number", &big);
    neverc_flag_uint64("ubig", 0, "unsigned big", &ubig);

    char *argv[] = {"prog", "-big", "999", "-ubig", "1234"};
    neverc_flag_parse(5, argv);

    check_int("big val", (int)big, 999);
    check_int("ubig val", (int)ubig, 1234);
}

static void test_numeric_limits(void) {
    printf("[numeric limits]\n");
    neverc_flag_reset();

    int integer = 0;
    long long signed_value = 0;
    unsigned long long unsigned_value = 0;
    double number = 0.0;
    neverc_flag_int("integer", 0, "integer", &integer);
    neverc_flag_int64("signed", 0, "signed", &signed_value);
    neverc_flag_uint64("unsigned", 0, "unsigned", &unsigned_value);
    neverc_flag_double("number", 0.0, "number", &number);

    char *argv[] = {
        "prog", "-integer=-2147483648", "-signed=9223372036854775807",
        "-unsigned=18446744073709551615", "-number=1e2"
    };
    check_int("limit parse succeeds", neverc_flag_parse(5, argv), 0);
    check_int("int minimum parses", integer == INT_MIN, 1);
    check_int("int64 maximum parses", signed_value == LLONG_MAX, 1);
    check_int("uint64 maximum parses", unsigned_value == ULLONG_MAX, 1);
    check_double("exponent parses", number, 100.0);
}

static void test_invalid_values(void) {
    printf("[invalid values]\n");

    neverc_flag_reset();
    int count = 7;
    neverc_flag_int("count", count, "count", &count);
    char *bad_int[] = {"prog", "-count=12x"};
    check_int("invalid integer is rejected", neverc_flag_parse(2, bad_int), -1);
    check_int("invalid integer preserves value", count, 7);
    check_int("invalid integer is not counted", neverc_flag_nflag(), 0);
    check_int("failed parse marks parsed", neverc_flag_parsed(), 1);

    neverc_flag_reset();
    count = 7;
    neverc_flag_int("count", count, "count", &count);
    char *overflow_int[] = {"prog", "-count=2147483648"};
    check_int("overflowing integer is rejected",
              neverc_flag_parse(2, overflow_int), -1);
    check_int("overflowing integer preserves value", count, 7);
    check_int("overflowing integer is not counted", neverc_flag_nflag(), 0);

    neverc_flag_reset();
    int enabled = 1;
    neverc_flag_bool("enabled", enabled, "enabled", &enabled);
    char *bad_bool[] = {"prog", "-enabled=maybe"};
    check_int("invalid boolean is rejected", neverc_flag_parse(2, bad_bool), -1);
    check_int("invalid boolean preserves value", enabled, 1);
    check_int("invalid boolean is not counted", neverc_flag_nflag(), 0);

    neverc_flag_reset();
    double rate = 2.5;
    neverc_flag_double("rate", rate, "rate", &rate);
    char *bad_double[] = {"prog", "-rate=1e"};
    check_int("invalid double is rejected",
              neverc_flag_parse(2, bad_double), -1);
    check_double("invalid double preserves value", rate, 2.5);
    check_int("invalid double is not counted", neverc_flag_nflag(), 0);

    neverc_flag_reset();
    count = 7;
    neverc_flag_int("count", count, "count", &count);
    char *missing[] = {"prog", "-count"};
    check_int("missing value is rejected", neverc_flag_parse(2, missing), -1);
    check_int("missing value preserves value", count, 7);
    check_int("missing value is not counted", neverc_flag_nflag(), 0);

    neverc_flag_reset();
    count = 7;
    neverc_flag_int("count", count, "count", &count);
    check_int("set rejects invalid integer",
              neverc_flag_set("count", "junk"), -1);
    check_int("invalid set preserves value", count, 7);
    check_int("invalid set is not counted", neverc_flag_nflag(), 0);
}

static void test_negative_and_bool_values(void) {
    printf("[negative/bool values]\n");
    neverc_flag_reset();
    int n = 0;
    int enabled = 1;
    neverc_flag_int("n", 0, "count", &n);
    neverc_flag_bool("enabled", 1, "enabled", &enabled);

    char *argv[] = {"prog", "-n", "-5", "-enabled=0"};
    check_int("negative int value parse ok", neverc_flag_parse(4, argv), 0);
    check_int("negative int as next arg", n, -5);
    check_int("bool equals false", enabled, 0);

    neverc_flag_reset();
    unsigned long long ubig = 9;
    neverc_flag_uint64("ubig", 9, "unsigned", &ubig);
    char *neg_uint[] = {"prog", "-ubig", "-1"};
    check_int("negative uint64 is rejected",
              neverc_flag_parse(2, neg_uint), -1);
    check_int("negative uint64 preserves value", (int)ubig, 9);
}

static void test_long_names(void) {
    printf("[long names]\n");
    neverc_flag_reset();

    char short_name[128];
    char long_name[129];
    memset(short_name, 'a', sizeof(short_name) - 1);
    short_name[sizeof(short_name) - 1] = '\0';
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    int short_value = 0, long_value = 0;
    neverc_flag_int(short_name, 0, "short", &short_value);
    neverc_flag_int(long_name, 0, "long", &long_value);

    char argument[133];
    argument[0] = '-';
    argument[1] = '-';
    memcpy(argument + 2, long_name, sizeof(long_name) - 1);
    argument[130] = '=';
    argument[131] = '7';
    argument[132] = '\0';
    char *argv[] = {"prog", argument};
    check_int("long name parse succeeds", neverc_flag_parse(2, argv), 0);
    check_int("long name does not match prefix", short_value, 0);
    check_int("long name matches exact flag", long_value, 7);
}

static void test_null_safety(void) {
    printf("[null safety]\n");
    neverc_flag_reset();

    int untouched = 44;
    neverc_flag_int(NULL, 1, "invalid", &untouched);
    check_int("null name registration is ignored", untouched, 44);
    check_int("null lookup is rejected", neverc_flag_lookup(NULL, NULL), -1);
    neverc_flag_visit(NULL, NULL);
    neverc_flag_visit_all(NULL, NULL);

    neverc_flag_reset();
    neverc_flag_int("invalid", 1, "invalid", NULL);
    check_int("null target registration is ignored",
              neverc_flag_lookup("invalid", NULL), -1);

    neverc_flag_reset();
    check_int("null argv is rejected", neverc_flag_parse(2, NULL), -1);
    check_int("null argv parse marks parsed", neverc_flag_parsed(), 1);
}

static void test_parsed_nflag(void) {
    printf("[parsed/nflag]\n");
    neverc_flag_reset();

    check_int("not parsed yet", neverc_flag_parsed(), 0);

    int x = 0;
    neverc_flag_int("x", 0, "test", &x);

    char *argv[] = {"prog", "-x", "42"};
    neverc_flag_parse(3, argv);

    check_int("parsed after parse", neverc_flag_parsed(), 1);
    check_int("nflag", neverc_flag_nflag(), 1);

    char *argv2[] = {"prog"};
    neverc_flag_parse(1, argv2);
    check_int("nflag after reparse", neverc_flag_nflag(), 0);
}

static void test_set_lookup(void) {
    printf("[set/lookup]\n");
    neverc_flag_reset();

    int val = 0;
    neverc_flag_int("count", 0, "item count", &val);

    check_int("set count", neverc_flag_set("count", "99"), 0);
    check_int("val after set", val, 99);

    const char *usage = NULL;
    check_int("lookup count", neverc_flag_lookup("count", &usage), 0);
    check_str("lookup usage", usage, "item count");

    check_int("lookup missing", neverc_flag_lookup("missing", &usage), -1);
}

static int visit_count_ctx;
static void visit_counter(const char *name, const char *usage, void *ctx) {
    (void)name; (void)usage; (void)ctx;
    visit_count_ctx++;
}

static void test_visit(void) {
    printf("[visit]\n");
    neverc_flag_reset();

    int a = 0, b = 0;
    neverc_flag_int("a", 0, "alpha", &a);
    neverc_flag_int("b", 0, "beta", &b);

    char *argv[] = {"prog", "-a", "1"};
    neverc_flag_parse(3, argv);

    visit_count_ctx = 0;
    neverc_flag_visit(visit_counter, NULL);
    check_int("visit set count", visit_count_ctx, 1);

    visit_count_ctx = 0;
    neverc_flag_visit_all(visit_counter, NULL);
    check_int("visit_all count", visit_count_ctx, 2);
}

int main(void) {
    printf("=== NeverC Flag Module Tests ===\n\n");
    test_basic();
    test_defaults();
    test_equals_syntax();
    test_remaining_args();
    test_lone_dash();
    test_base_prefixes();
    test_double_dash();
    test_value_cannot_be_terminator();
    test_int64_uint64();
    test_numeric_limits();
    test_invalid_values();
    test_negative_and_bool_values();
    test_long_names();
    test_parsed_nflag();
    test_set_lookup();
    test_visit();
    test_null_safety();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
