/*
 * NeverC container/ring tests.
 * Tests circular list operations — mirrors Go container/ring test cases.
 */
#include "neverc/std/container/ring.h"
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d\n", #expr, _v, _e); } \
} while(0)

#define ASSERT_NULL(expr) do { tests_run++; \
    if ((expr) == NULL) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s not NULL\n", #expr); } } while(0)

#define ASSERT_NOT_NULL(expr) do { tests_run++; \
    if ((expr) != NULL) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s is NULL\n", #expr); } } while(0)

#define INT_VAL(n) ((void *)(intptr_t)(n))
#define TO_INT(p) ((int)(intptr_t)(p))

static void test_new(void) {
    printf("[new]\n");
    ASSERT_NULL(neverc_ring_new(0));
    ASSERT_NULL(neverc_ring_new(-1));

    neverc_ring_t *r = neverc_ring_new(1);
    ASSERT_NOT_NULL(r);
    ASSERT_INT_EQ(neverc_ring_len(r), 1);
    ASSERT_INT_EQ(neverc_ring_next(r) == r, 1);
    ASSERT_INT_EQ(neverc_ring_prev(r) == r, 1);
    neverc_ring_free(r);

    r = neverc_ring_new(5);
    ASSERT_INT_EQ(neverc_ring_len(r), 5);
    neverc_ring_free(r);
}

static void test_next_prev(void) {
    printf("[next_prev]\n");
    neverc_ring_t *r = neverc_ring_new(3);
    r->value = INT_VAL(1);
    neverc_ring_next(r)->value = INT_VAL(2);
    neverc_ring_next(neverc_ring_next(r))->value = INT_VAL(3);

    ASSERT_INT_EQ(TO_INT(r->value), 1);
    ASSERT_INT_EQ(TO_INT(neverc_ring_next(r)->value), 2);
    ASSERT_INT_EQ(TO_INT(neverc_ring_next(neverc_ring_next(r))->value), 3);
    /* circular */
    ASSERT_INT_EQ(neverc_ring_next(neverc_ring_next(neverc_ring_next(r))) == r, 1);
    /* prev is reverse */
    ASSERT_INT_EQ(TO_INT(neverc_ring_prev(r)->value), 3);

    neverc_ring_free(r);
}

static void test_move(void) {
    printf("[move]\n");
    neverc_ring_t *r = neverc_ring_new(5);
    for (int i = 0; i < 5; i++) {
        neverc_ring_move(r, i)->value = INT_VAL(i);
    }

    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, 0)->value), 0);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, 1)->value), 1);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, 2)->value), 2);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, 5)->value), 0);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, -1)->value), 4);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, -5)->value), 0);

    neverc_ring_free(r);
}

static void test_link(void) {
    printf("[link]\n");
    neverc_ring_t *r = neverc_ring_new(3);
    neverc_ring_t *s = neverc_ring_new(2);

    r->value = INT_VAL(1);
    neverc_ring_next(r)->value = INT_VAL(2);
    neverc_ring_next(neverc_ring_next(r))->value = INT_VAL(3);
    s->value = INT_VAL(10);
    neverc_ring_next(s)->value = INT_VAL(20);

    neverc_ring_link(r, s);
    /* after link: r(1) -> s(10) -> s.next(20) -> r.next(2) -> r.next.next(3) -> r */
    ASSERT_INT_EQ(neverc_ring_len(r), 5);
    ASSERT_INT_EQ(TO_INT(neverc_ring_next(r)->value), 10);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, 2)->value), 20);

    neverc_ring_free(r);
}

static void test_unlink(void) {
    printf("[unlink]\n");
    neverc_ring_t *r = neverc_ring_new(5);
    for (int i = 0; i < 5; i++)
        neverc_ring_move(r, i)->value = INT_VAL(i);

    neverc_ring_t *removed = neverc_ring_unlink(r, 2);
    ASSERT_INT_EQ(neverc_ring_len(r), 3);
    ASSERT_INT_EQ(neverc_ring_len(removed), 2);
    ASSERT_INT_EQ(TO_INT(removed->value), 1);

    neverc_ring_free(r);
    neverc_ring_free(removed);
}

static void test_unlink_zero(void) {
    printf("[unlink_zero]\n");
    neverc_ring_t *r = neverc_ring_new(3);
    ASSERT_NULL(neverc_ring_unlink(r, 0));
    ASSERT_NULL(neverc_ring_unlink(r, -1));
    ASSERT_INT_EQ(neverc_ring_len(r), 3);
    neverc_ring_free(r);
}

static int do_sum;
static void sum_fn(void *val, void *ctx) {
    (void)ctx;
    do_sum += TO_INT(val);
}

static void test_do_simple(void) {
    printf("[do_callback]\n");
    neverc_ring_t *r = neverc_ring_new(5);
    for (int i = 0; i < 5; i++)
        neverc_ring_move(r, i)->value = INT_VAL(i + 1);

    do_sum = 0;
    neverc_ring_do(r, sum_fn, NULL);
    ASSERT_INT_EQ(do_sum, 15);

    neverc_ring_free(r);
}

static void test_single_element(void) {
    printf("[single_element]\n");
    neverc_ring_t *r = neverc_ring_new(1);
    r->value = INT_VAL(42);
    ASSERT_INT_EQ(neverc_ring_len(r), 1);
    ASSERT_INT_EQ(neverc_ring_next(r) == r, 1);
    ASSERT_INT_EQ(neverc_ring_prev(r) == r, 1);
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, 100)->value), 42);
    neverc_ring_free(r);
}

static int zero_do_count;

static void count_fn(void *value, void *ctx) {
    (void)value;
    (void)ctx;
    zero_do_count++;
}

static void test_zero_value_and_null_input(void) {
    printf("[zero_value_and_null_input]\n");
    neverc_ring_t zero = {0};
    ASSERT_INT_EQ(neverc_ring_len(&zero), 1);
    ASSERT_INT_EQ(neverc_ring_next(&zero) == &zero, 1);
    ASSERT_INT_EQ(neverc_ring_prev(&zero) == &zero, 1);
    ASSERT_INT_EQ(neverc_ring_move(&zero, INT_MIN) == &zero, 1);
    zero_do_count = 0;
    neverc_ring_do(&zero, count_fn, NULL);
    ASSERT_INT_EQ(zero_do_count, 1);

    ASSERT_NULL(neverc_ring_next(NULL));
    ASSERT_NULL(neverc_ring_prev(NULL));
    ASSERT_NULL(neverc_ring_move(NULL, 1));
    ASSERT_NULL(neverc_ring_link(NULL, &zero));
    ASSERT_NULL(neverc_ring_unlink(NULL, 1));
    ASSERT_INT_EQ(neverc_ring_len(NULL), 0);
    neverc_ring_do(NULL, count_fn, NULL);
    neverc_ring_do(&zero, NULL, NULL);
    neverc_ring_free(NULL);
}

static void test_extreme_move_and_unlink(void) {
    printf("[extreme_move_and_unlink]\n");
    neverc_ring_t *r = neverc_ring_new(5);
    for (int i = 0; i < 5; i++)
        neverc_ring_move(r, i)->value = INT_VAL(i);

    int expected_backward = INT_MIN % 5;
    if (expected_backward < 0) expected_backward += 5;
    ASSERT_INT_EQ(TO_INT(neverc_ring_move(r, INT_MIN)->value),
                  expected_backward);

    int removed_count = INT_MAX % 5;
    neverc_ring_t *removed = neverc_ring_unlink(r, INT_MAX);
    ASSERT_NOT_NULL(removed);
    ASSERT_INT_EQ(neverc_ring_len(removed), removed_count);
    ASSERT_INT_EQ(neverc_ring_len(r), 5 - removed_count);
    neverc_ring_free(removed);
    neverc_ring_free(r);
}

int main(void) {
    printf("=== NeverC container/ring Tests ===\n");
    test_new();
    test_next_prev();
    test_move();
    test_link();
    test_unlink();
    test_unlink_zero();
    test_do_simple();
    test_single_element();
    test_zero_value_and_null_input();
    test_extreme_move_and_unlink();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
