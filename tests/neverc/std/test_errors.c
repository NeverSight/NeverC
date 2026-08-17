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
    check_bool("new null text", neverc_errors_new(NULL) == NULL, 1);
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

    cause = neverc_errors_new("still owned");
    wrapped = neverc_errors_wrap(NULL, cause);
    check_bool("wrap null text", wrapped == NULL, 1);
    check_str("failed wrap preserves cause",
              neverc_errors_message(cause), "still owned");
    neverc_errors_free(cause);
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
    check_bool("non-null is not nil", neverc_errors_is(top, NULL), 0);
    check_bool("nil is nil", neverc_errors_is(NULL, NULL), 1);

    neverc_error_t *found = NULL;
    check_bool("as base", neverc_errors_as(top, base, &found), 1);
    check_bool("as base node", found == base, 1);
    check_bool("as same msg", neverc_errors_as(top, same_msg, &found), 1);
    check_str("as same msg node", neverc_errors_message(found), "not found");
    check_bool("as other", neverc_errors_as(top, other, &found), 0);
    check_bool("as nil target", neverc_errors_as(top, NULL, &found), 0);
    check_bool("nil as nil", neverc_errors_as(NULL, NULL, &found), 1);
    check_bool("nil as nil out", found == NULL, 1);

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
    check_bool("join is e1", neverc_errors_is(joined, e1), 1);
    check_bool("join is e2", neverc_errors_is(joined, e2), 1);
    check_bool("join is e3", neverc_errors_is(joined, e3), 1);
    check_str("join unwrap first",
              neverc_errors_message(neverc_errors_unwrap(joined)), "err1");
    neverc_error_t *other = neverc_errors_new("timeout");
    check_bool("join is other", neverc_errors_is(joined, other), 0);
    neverc_errors_free(other);
    neverc_errors_free(joined);

    neverc_error_t *nulls[] = {NULL, NULL};
    neverc_error_t *jnull = neverc_errors_join(nulls, 2);
    check_bool("join all null", jnull == NULL, 1);
    check_bool("join null empty", neverc_errors_join(NULL, 0) == NULL, 1);
    check_bool("join null non-empty",
               neverc_errors_join(NULL, 1) == NULL, 1);

    neverc_errors_free(e1);
    neverc_errors_free(e2);
    neverc_errors_free(e3);

    /* Join must keep wrap chains so errors_is can see a wrapped cause. */
    neverc_error_t *cause = neverc_errors_new("file not found");
    neverc_error_t *wrapped = neverc_errors_wrap("open config", cause);
    neverc_error_t *extra = neverc_errors_new("timeout");
    neverc_error_t *mixed[] = {wrapped, extra};
    neverc_error_t *joined_wrap = neverc_errors_join(mixed, 2);
    check_str("join wrap msg", neverc_errors_message(joined_wrap),
              "open config: file not found\ntimeout");
    check_bool("join is wrapped", neverc_errors_is(joined_wrap, wrapped), 1);
    check_bool("join is cause", neverc_errors_is(joined_wrap, cause), 1);
    check_bool("join is extra", neverc_errors_is(joined_wrap, extra), 1);
    neverc_error_t *as_cause = NULL;
    check_bool("join as cause", neverc_errors_as(joined_wrap, cause, &as_cause), 1);
    check_str("join as cause msg", neverc_errors_message(as_cause),
              "file not found");
    check_bool("join as cause is clone", as_cause != cause, 1);
    neverc_error_t *unrelated = neverc_errors_new("permission denied");
    check_bool("join is unrelated", neverc_errors_is(joined_wrap, unrelated), 0);
    neverc_errors_free(unrelated);
    neverc_errors_free(joined_wrap);
    neverc_errors_free(wrapped);
    neverc_errors_free(extra);
}

static void test_cycle(void) {
    printf("[cycle]\n");
    neverc_error_t *a = neverc_errors_new("a");
    neverc_error_t *b = neverc_errors_new("b");
    a->wrapped = b;
    b->wrapped = a;

    check_bool("is finds peer in cycle", neverc_errors_is(a, b), 1);
    neverc_error_t *other = neverc_errors_new("c");
    check_bool("is other survives cycle", neverc_errors_is(a, other), 0);
    neverc_error_t *found = NULL;
    check_bool("as finds peer in cycle", neverc_errors_as(a, b, &found), 1);
    check_bool("as peer node", found == b, 1);
    neverc_errors_free(other);
    neverc_errors_free(a);

    a = neverc_errors_new("a");
    b = neverc_errors_new("b");
    a->wrapped = b;
    b->wrapped = a;
    neverc_error_t *cyclic[] = {a};
    neverc_error_t *joined = neverc_errors_join(cyclic, 1);
    check_bool("join cyclic succeeds", joined != NULL, 1);
    check_bool("join cyclic is a", neverc_errors_is(joined, a), 1);
    neverc_error_t *unrelated = neverc_errors_new("z");
    check_bool("join cyclic is not unrelated",
               neverc_errors_is(joined, unrelated), 0);
    neverc_errors_free(unrelated);
    neverc_errors_free(joined);
    neverc_errors_free(a);

    neverc_error_t *c = neverc_errors_new("c");
    neverc_error_t *d = neverc_errors_new("d");
    c->wrapped = d;
    d->wrapped = c;
    neverc_error_t *outer = neverc_errors_wrap("outer", c);
    check_bool("wrap cyclic succeeds", outer != NULL, 1);
    check_bool("wrap cyclic is c", neverc_errors_is(outer, c), 1);
    neverc_errors_free(outer);
}

int main(void) {
    printf("=== NeverC Errors Module Tests ===\n\n");
    test_new_and_message();
    test_wrap_unwrap();
    test_is();
    test_join();
    test_cycle();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
