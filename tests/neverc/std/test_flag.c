#include "neverc/std/flag.h"
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
    check_int("narg", neverc_flag_narg(), 2);
    check_str("arg0", neverc_flag_arg(0), "file1.txt");
    check_str("arg1", neverc_flag_arg(1), "file2.txt");
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
    test_double_dash();
    test_int64_uint64();
    test_parsed_nflag();
    test_set_lookup();
    test_visit();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
