#include "neverc/flag.h"
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

int main(void) {
    printf("=== NeverC Flag Module Tests ===\n\n");
    test_basic();
    test_defaults();
    test_equals_syntax();
    test_remaining_args();
    test_double_dash();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
