#include "neverc/std/errors.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else if (!got && !expected) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n",
               name, got ? got : "(null)", expected ? expected : "(null)");
    }
}

static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_new_and_message(void) {
    printf("[new/message]\n");
    neverc_error_t *e = neverc_errors_new("something went wrong");
    check_str("new msg", neverc_errors_message(e), "something went wrong");
    check_bool("unwrap null", neverc_errors_unwrap(e) == NULL, 1);
    neverc_errors_free(e);

    check_str("null msg", neverc_errors_message(NULL), NULL);
}

static void test_wrap_unwrap(void) {
    printf("[wrap/unwrap]\n");
    neverc_error_t *cause = neverc_errors_new("file not found");
    neverc_error_t *wrapped = neverc_errors_wrap("open config", cause);

    check_str("wrap msg", neverc_errors_message(wrapped), "open config: file not found");
    check_bool("unwrap ptr", neverc_errors_unwrap(wrapped) == cause, 1);
    check_str("unwrap msg", neverc_errors_message(neverc_errors_unwrap(wrapped)),
              "file not found");

    neverc_errors_free(wrapped);
}

static void test_is(void) {
    printf("[is]\n");
    neverc_error_t *base = neverc_errors_new("not found");
    neverc_error_t *mid  = neverc_errors_wrap("read file", base);
    neverc_error_t *top  = neverc_errors_wrap("process", mid);

    check_bool("is self", neverc_errors_is(top, top), 1);
    check_bool("is mid", neverc_errors_is(top, mid), 1);
    check_bool("is base", neverc_errors_is(top, base), 1);

    neverc_error_t *other = neverc_errors_new("timeout");
    check_bool("is other", neverc_errors_is(top, other), 0);

    neverc_error_t *same_msg = neverc_errors_new("not found");
    check_bool("is same msg", neverc_errors_is(top, same_msg), 1);

    neverc_errors_free(top);
    neverc_errors_free(other);
    neverc_errors_free(same_msg);
}

static void test_join(void) {
    printf("[join]\n");
    neverc_error_t *e1 = neverc_errors_new("err1");
    neverc_error_t *e2 = neverc_errors_new("err2");
    neverc_error_t *e3 = neverc_errors_new("err3");
    neverc_error_t *errs[] = {e1, NULL, e2, e3};

    neverc_error_t *joined = neverc_errors_join(errs, 4);
    check_str("join", neverc_errors_message(joined), "err1\nerr2\nerr3");
    neverc_errors_free(joined);

    neverc_error_t *nulls[] = {NULL, NULL};
    neverc_error_t *jnull = neverc_errors_join(nulls, 2);
    check_bool("join all null", jnull == NULL, 1);

    neverc_errors_free(e1);
    neverc_errors_free(e2);
    neverc_errors_free(e3);
}

int main(void) {
    printf("=== NeverC Errors Module Tests ===\n\n");
    test_new_and_message();
    test_wrap_unwrap();
    test_is();
    test_join();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
