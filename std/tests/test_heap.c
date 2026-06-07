/*
 * NeverC container/heap tests.
 * Tests min-heap property with Init / Push / Pop / Remove / Fix.
 */
#include "neverc/container/heap.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d\n", #expr, _v, _e); } \
} while(0)

/* simple int heap backed by a fixed-size array */
#define MAX_HEAP 256
typedef struct {
    int arr[MAX_HEAP];
    int len;
} int_heap_t;

static int ih_len(void *d) { return ((int_heap_t *)d)->len; }
static int ih_less(void *d, int i, int j) {
    int_heap_t *h = (int_heap_t *)d;
    return h->arr[i] < h->arr[j];
}
static void ih_swap(void *d, int i, int j) {
    int_heap_t *h = (int_heap_t *)d;
    int tmp = h->arr[i]; h->arr[i] = h->arr[j]; h->arr[j] = tmp;
}
static void ih_push(void *d, const void *elem) {
    int_heap_t *h = (int_heap_t *)d;
    h->arr[h->len++] = *(const int *)elem;
}
static void ih_pop(void *d, void *out) {
    int_heap_t *h = (int_heap_t *)d;
    h->len--;
    *(int *)out = h->arr[h->len];
}

static neverc_heap_interface_t make_iface(int_heap_t *h) {
    neverc_heap_interface_t iface;
    iface.data = h;
    iface.len_fn = ih_len;
    iface.less_fn = ih_less;
    iface.swap_fn = ih_swap;
    iface.push_fn = ih_push;
    iface.pop_fn = ih_pop;
    return iface;
}

static int is_min_heap(int_heap_t *h) {
    for (int i = 0; i < h->len; i++) {
        int left = 2 * i + 1, right = 2 * i + 2;
        if (left < h->len && h->arr[i] > h->arr[left]) return 0;
        if (right < h->len && h->arr[i] > h->arr[right]) return 0;
    }
    return 1;
}

static void test_init(void) {
    printf("[init]\n");
    int_heap_t h = { .arr = {5, 3, 8, 1, 2, 7, 4, 6}, .len = 8 };
    neverc_heap_interface_t iface = make_iface(&h);
    neverc_heap_init(&iface);
    ASSERT_INT_EQ(is_min_heap(&h), 1);
    ASSERT_INT_EQ(h.arr[0], 1);
}

static void test_push_pop(void) {
    printf("[push_pop]\n");
    int_heap_t h = { .len = 0 };
    neverc_heap_interface_t iface = make_iface(&h);

    int vals[] = {20, 10, 30, 5, 15, 25, 1};
    for (int i = 0; i < 7; i++)
        neverc_heap_push(&iface, &vals[i]);

    ASSERT_INT_EQ(h.len, 7);
    ASSERT_INT_EQ(is_min_heap(&h), 1);

    int out;
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 1);
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 5);
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 10);
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 15);
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 20);
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 25);
    neverc_heap_pop(&iface, &out); ASSERT_INT_EQ(out, 30);
    ASSERT_INT_EQ(h.len, 0);
}

static void test_remove(void) {
    printf("[remove]\n");
    int_heap_t h = { .len = 0 };
    neverc_heap_interface_t iface = make_iface(&h);

    int vals[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++)
        neverc_heap_push(&iface, &vals[i]);

    int out;
    neverc_heap_remove(&iface, 2, &out);
    ASSERT_INT_EQ(h.len, 4);
    ASSERT_INT_EQ(is_min_heap(&h), 1);

    /* verify remaining elements by popping all */
    int remaining[4];
    for (int i = 0; i < 4; i++)
        neverc_heap_pop(&iface, &remaining[i]);
    ASSERT_INT_EQ(remaining[0], 10);
}

static void test_fix(void) {
    printf("[fix]\n");
    int_heap_t h = { .len = 0 };
    neverc_heap_interface_t iface = make_iface(&h);

    int vals[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++)
        neverc_heap_push(&iface, &vals[i]);

    /* change root to a large value and fix */
    h.arr[0] = 100;
    neverc_heap_fix(&iface, 0);
    ASSERT_INT_EQ(is_min_heap(&h), 1);
    ASSERT_INT_EQ(h.arr[0], 20);

    /* change a leaf to smallest value and fix */
    h.arr[h.len - 1] = 1;
    neverc_heap_fix(&iface, h.len - 1);
    ASSERT_INT_EQ(is_min_heap(&h), 1);
    ASSERT_INT_EQ(h.arr[0], 1);
}

static void test_empty(void) {
    printf("[empty]\n");
    int_heap_t h = { .len = 0 };
    neverc_heap_interface_t iface = make_iface(&h);
    neverc_heap_init(&iface);
    ASSERT_INT_EQ(h.len, 0);

    int v = 42;
    neverc_heap_push(&iface, &v);
    ASSERT_INT_EQ(h.len, 1);
    ASSERT_INT_EQ(h.arr[0], 42);

    int out;
    neverc_heap_pop(&iface, &out);
    ASSERT_INT_EQ(out, 42);
    ASSERT_INT_EQ(h.len, 0);
}

static void test_sorted_input(void) {
    printf("[sorted_input]\n");
    int_heap_t h = { .len = 0 };
    neverc_heap_interface_t iface = make_iface(&h);

    for (int i = 1; i <= 20; i++)
        neverc_heap_push(&iface, &i);
    ASSERT_INT_EQ(is_min_heap(&h), 1);

    int prev = -1;
    for (int i = 0; i < 20; i++) {
        int out;
        neverc_heap_pop(&iface, &out);
        tests_run++;
        if (out > prev) tests_passed++;
        else { tests_failed++; printf("  FAIL: not sorted %d <= %d\n", out, prev); }
        prev = out;
    }
}

static void test_reverse_input(void) {
    printf("[reverse_input]\n");
    int_heap_t h = { .len = 0 };
    neverc_heap_interface_t iface = make_iface(&h);

    for (int i = 20; i >= 1; i--)
        neverc_heap_push(&iface, &i);
    ASSERT_INT_EQ(is_min_heap(&h), 1);

    int out;
    neverc_heap_pop(&iface, &out);
    ASSERT_INT_EQ(out, 1);
}

int main(void) {
    printf("=== NeverC container/heap Tests ===\n");
    test_init();
    test_push_pop();
    test_remove();
    test_fix();
    test_empty();
    test_sorted_input();
    test_reverse_input();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
