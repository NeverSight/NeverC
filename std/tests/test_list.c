/*
 * NeverC container/list tests.
 * Tests doubly linked list operations — mirrors Go container/list test cases.
 */
#include "neverc/container/list.h"
#include <stdio.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d\n", #expr, _v, _e); } \
} while(0)

#define ASSERT_PTR_EQ(expr, expected) do { \
    void *_v = (void *)(expr); void *_e = (void *)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s != expected ptr\n", #expr); } \
} while(0)

#define ASSERT_NULL(expr) ASSERT_PTR_EQ(expr, NULL)
#define ASSERT_NOT_NULL(expr) do { tests_run++; \
    if ((expr) != NULL) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s is NULL\n", #expr); } } while(0)

#define INT_VAL(n) ((void *)(long)(n))
#define TO_INT(p) ((int)(long)(p))

static void test_basic(void) {
    printf("[basic]\n");
    neverc_list_t *l = neverc_list_new();
    ASSERT_INT_EQ(neverc_list_len(l), 0);
    ASSERT_NULL(neverc_list_front(l));
    ASSERT_NULL(neverc_list_back(l));

    neverc_list_push_back(l, INT_VAL(1));
    neverc_list_push_back(l, INT_VAL(2));
    neverc_list_push_back(l, INT_VAL(3));
    ASSERT_INT_EQ(neverc_list_len(l), 3);
    ASSERT_INT_EQ(TO_INT(neverc_list_front(l)->value), 1);
    ASSERT_INT_EQ(TO_INT(neverc_list_back(l)->value), 3);

    neverc_list_free(l);
    ASSERT_INT_EQ(neverc_list_len(l), 0);
    free(l);
}

static void test_push_front(void) {
    printf("[push_front]\n");
    neverc_list_t *l = neverc_list_new();
    neverc_list_push_front(l, INT_VAL(3));
    neverc_list_push_front(l, INT_VAL(2));
    neverc_list_push_front(l, INT_VAL(1));
    ASSERT_INT_EQ(neverc_list_len(l), 3);

    neverc_list_element_t *e = neverc_list_front(l);
    ASSERT_INT_EQ(TO_INT(e->value), 1);
    e = neverc_list_element_next(e);
    ASSERT_INT_EQ(TO_INT(e->value), 2);
    e = neverc_list_element_next(e);
    ASSERT_INT_EQ(TO_INT(e->value), 3);
    ASSERT_NULL(neverc_list_element_next(e));

    neverc_list_free(l);
    free(l);
}

static void test_remove(void) {
    printf("[remove]\n");
    neverc_list_t *l = neverc_list_new();
    neverc_list_element_t *e1 = neverc_list_push_back(l, INT_VAL(1));
    neverc_list_element_t *e2 = neverc_list_push_back(l, INT_VAL(2));
    neverc_list_element_t *e3 = neverc_list_push_back(l, INT_VAL(3));
    (void)e1; (void)e3;

    void *v = neverc_list_remove(l, e2);
    ASSERT_INT_EQ(TO_INT(v), 2);
    ASSERT_INT_EQ(neverc_list_len(l), 2);
    ASSERT_INT_EQ(TO_INT(neverc_list_front(l)->value), 1);
    ASSERT_INT_EQ(TO_INT(neverc_list_back(l)->value), 3);

    neverc_list_free(l);
    free(l);
}

static void test_insert_before_after(void) {
    printf("[insert_before_after]\n");
    neverc_list_t *l = neverc_list_new();
    neverc_list_element_t *e2 = neverc_list_push_back(l, INT_VAL(2));
    neverc_list_insert_before(l, INT_VAL(1), e2);
    neverc_list_insert_after(l, INT_VAL(3), e2);

    ASSERT_INT_EQ(neverc_list_len(l), 3);
    ASSERT_INT_EQ(TO_INT(neverc_list_front(l)->value), 1);
    ASSERT_INT_EQ(TO_INT(neverc_list_element_next(neverc_list_front(l))->value), 2);
    ASSERT_INT_EQ(TO_INT(neverc_list_back(l)->value), 3);

    neverc_list_free(l);
    free(l);
}

static void test_move(void) {
    printf("[move_to_front_back]\n");
    neverc_list_t *l = neverc_list_new();
    neverc_list_push_back(l, INT_VAL(1));
    neverc_list_push_back(l, INT_VAL(2));
    neverc_list_element_t *e3 = neverc_list_push_back(l, INT_VAL(3));

    neverc_list_move_to_front(l, e3);
    ASSERT_INT_EQ(TO_INT(neverc_list_front(l)->value), 3);
    ASSERT_INT_EQ(neverc_list_len(l), 3);

    neverc_list_element_t *e1 = neverc_list_element_next(neverc_list_front(l));
    neverc_list_move_to_back(l, e1);
    ASSERT_INT_EQ(TO_INT(neverc_list_back(l)->value), 1);

    neverc_list_free(l);
    free(l);
}

static void test_iteration(void) {
    printf("[iteration]\n");
    neverc_list_t *l = neverc_list_new();
    for (int i = 1; i <= 10; i++)
        neverc_list_push_back(l, INT_VAL(i));

    int sum = 0, count = 0;
    for (neverc_list_element_t *e = neverc_list_front(l); e != NULL;
         e = neverc_list_element_next(e)) {
        sum += TO_INT(e->value);
        count++;
    }
    ASSERT_INT_EQ(count, 10);
    ASSERT_INT_EQ(sum, 55);

    /* reverse iteration */
    sum = 0; count = 0;
    for (neverc_list_element_t *e = neverc_list_back(l); e != NULL;
         e = neverc_list_element_prev(e)) {
        sum += TO_INT(e->value);
        count++;
    }
    ASSERT_INT_EQ(count, 10);
    ASSERT_INT_EQ(sum, 55);

    neverc_list_free(l);
    free(l);
}

static void test_move_before_after(void) {
    printf("[move_before_after]\n");
    neverc_list_t *l = neverc_list_new();
    neverc_list_element_t *e1 = neverc_list_push_back(l, INT_VAL(1));
    neverc_list_element_t *e2 = neverc_list_push_back(l, INT_VAL(2));
    neverc_list_element_t *e3 = neverc_list_push_back(l, INT_VAL(3));
    neverc_list_element_t *e4 = neverc_list_push_back(l, INT_VAL(4));
    (void)e1;

    /* move e4 before e2: 1,4,2,3 */
    neverc_list_move_before(l, e4, e2);
    neverc_list_element_t *it = neverc_list_front(l);
    ASSERT_INT_EQ(TO_INT(it->value), 1);
    it = neverc_list_element_next(it);
    ASSERT_INT_EQ(TO_INT(it->value), 4);
    it = neverc_list_element_next(it);
    ASSERT_INT_EQ(TO_INT(it->value), 2);
    it = neverc_list_element_next(it);
    ASSERT_INT_EQ(TO_INT(it->value), 3);

    /* move e3 after e1: 1,3,4,2 */
    neverc_list_move_after(l, e3, neverc_list_front(l));
    it = neverc_list_front(l);
    ASSERT_INT_EQ(TO_INT(it->value), 1);
    it = neverc_list_element_next(it);
    ASSERT_INT_EQ(TO_INT(it->value), 3);

    neverc_list_free(l);
    free(l);
}

static void test_remove_all(void) {
    printf("[remove_all]\n");
    neverc_list_t *l = neverc_list_new();
    for (int i = 0; i < 5; i++)
        neverc_list_push_back(l, INT_VAL(i));

    while (neverc_list_len(l) > 0)
        neverc_list_remove(l, neverc_list_front(l));

    ASSERT_INT_EQ(neverc_list_len(l), 0);
    ASSERT_NULL(neverc_list_front(l));
    ASSERT_NULL(neverc_list_back(l));

    free(l);
}

int main(void) {
    printf("=== NeverC container/list Tests ===\n");
    test_basic();
    test_push_front();
    test_remove();
    test_insert_before_after();
    test_move();
    test_iteration();
    test_move_before_after();
    test_remove_all();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
