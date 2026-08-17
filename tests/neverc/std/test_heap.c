/*
 * NeverC container/heap tests.
 * Tests min-heap property with Init / Push / Pop / Remove / Fix.
 */
#include "neverc/std/container/heap.h"
#include <limits.h>
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

typedef struct {
    int len;
    int callback_count;
    int invalid_index;
} guarded_heap_t;

static int gh_len(void *data) {
    return ((guarded_heap_t *)data)->len;
}

static int gh_less(void *data, int i, int j) {
    guarded_heap_t *heap = (guarded_heap_t *)data;
    heap->callback_count++;
    if (i < 0 || j < 0 || i >= heap->len || j >= heap->len)
        heap->invalid_index = 1;
    return 0;
}

static void gh_swap(void *data, int i, int j) {
    guarded_heap_t *heap = (guarded_heap_t *)data;
    heap->callback_count++;
    if (i < 0 || j < 0 || i >= heap->len || j >= heap->len)
        heap->invalid_index = 1;
}

static void gh_push(void *data, const void *elem) {
    guarded_heap_t *heap = (guarded_heap_t *)data;
    (void)elem;
    heap->callback_count++;
    if (heap->len < INT_MAX) heap->len++;
}

static void gh_pop(void *data, void *out) {
    guarded_heap_t *heap = (guarded_heap_t *)data;
    heap->callback_count++;
    if (heap->len > 0) heap->len--;
    *(int *)out = 123;
}

static neverc_heap_interface_t make_guarded_iface(guarded_heap_t *heap) {
    neverc_heap_interface_t iface = {
        .data = heap,
        .len_fn = gh_len,
        .less_fn = gh_less,
        .swap_fn = gh_swap,
        .push_fn = gh_push,
        .pop_fn = gh_pop,
    };
    return iface;
}

static void test_invalid_operations_are_noops(void) {
    printf("[invalid_operations_are_noops]\n");
    guarded_heap_t heap = {.len = 0};
    neverc_heap_interface_t iface = make_guarded_iface(&heap);
    int out = 77;

    neverc_heap_pop(&iface, &out);
    neverc_heap_remove(&iface, 0, &out);
    neverc_heap_remove(&iface, -1, &out);
    neverc_heap_fix(&iface, 0);
    neverc_heap_fix(&iface, -1);
    ASSERT_INT_EQ(out, 77);
    ASSERT_INT_EQ(heap.callback_count, 0);
    ASSERT_INT_EQ(heap.invalid_index, 0);

    neverc_heap_init(NULL);
    neverc_heap_push(NULL, NULL);
    neverc_heap_pop(NULL, NULL);
    neverc_heap_remove(NULL, 0, NULL);
    neverc_heap_fix(NULL, 0);

    neverc_heap_interface_t empty_iface = {0};
    neverc_heap_init(&empty_iface);
    neverc_heap_push(&empty_iface, NULL);
    neverc_heap_pop(&empty_iface, NULL);
    neverc_heap_remove(&empty_iface, 0, NULL);
    neverc_heap_fix(&empty_iface, 0);
    ASSERT_INT_EQ(heap.callback_count, 0);
}

static void test_large_leaf_index_does_not_overflow(void) {
    printf("[large_leaf_index_does_not_overflow]\n");
    guarded_heap_t heap = {.len = INT_MAX};
    neverc_heap_interface_t iface = make_guarded_iface(&heap);

    neverc_heap_fix(&iface, INT_MAX - 1);
    ASSERT_INT_EQ(heap.invalid_index, 0);
    ASSERT_INT_EQ(heap.callback_count, 1);
}

static void test_last_parent_index_does_not_overflow(void) {
    printf("[last_parent_index_does_not_overflow]\n");
    guarded_heap_t heap = {.len = INT_MAX};
    neverc_heap_interface_t iface = make_guarded_iface(&heap);

    neverc_heap_fix(&iface, INT_MAX / 2 - 1);
    ASSERT_INT_EQ(heap.invalid_index, 0);
}

static unsigned int random_state = 0x8f31a5c7U;

static unsigned int next_random(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static int reference_min(const int values[], int count) {
    int result = values[0];
    for (int i = 1; i < count; i++)
        if (values[i] < result) result = values[i];
    return result;
}

static void reference_remove_one(int values[], int *count, int value) {
    for (int i = 0; i < *count; i++) {
        if (values[i] == value) {
            values[i] = values[--*count];
            return;
        }
    }
    tests_run++;
    tests_failed++;
    printf("  FAIL: reference value %d not found\n", value);
}

static void test_randomized_operations(void) {
    printf("[randomized_operations]\n");
    int_heap_t heap = {.len = 0};
    neverc_heap_interface_t iface = make_iface(&heap);
    int reference[MAX_HEAP];
    int reference_count = 0;

    for (int round = 0; round < 10000; round++) {
        unsigned int choice = next_random() % 4U;
        if (reference_count == 0 ||
            (choice == 0 && reference_count < MAX_HEAP)) {
            int value = (int)(next_random() % 2001U) - 1000;
            neverc_heap_push(&iface, &value);
            reference[reference_count++] = value;
        } else if (choice == 1) {
            int expected = reference_min(reference, reference_count);
            int actual = 0;
            neverc_heap_pop(&iface, &actual);
            ASSERT_INT_EQ(actual, expected);
            reference_remove_one(reference, &reference_count, actual);
        } else if (choice == 2) {
            int index = (int)(next_random() % (unsigned int)heap.len);
            int expected = heap.arr[index];
            int actual = 0;
            neverc_heap_remove(&iface, index, &actual);
            ASSERT_INT_EQ(actual, expected);
            reference_remove_one(reference, &reference_count, actual);
        } else {
            int index = (int)(next_random() % (unsigned int)heap.len);
            int old_value = heap.arr[index];
            int new_value = (int)(next_random() % 2001U) - 1000;
            reference_remove_one(reference, &reference_count, old_value);
            heap.arr[index] = new_value;
            reference[reference_count++] = new_value;
            neverc_heap_fix(&iface, index);
        }

        ASSERT_INT_EQ(heap.len, reference_count);
        ASSERT_INT_EQ(is_min_heap(&heap), 1);
        if (reference_count > 0)
            ASSERT_INT_EQ(heap.arr[0], reference_min(reference,
                                                     reference_count));
    }
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
    test_invalid_operations_are_noops();
    test_large_leaf_index_does_not_overflow();
    test_last_parent_index_does_not_overflow();
    test_randomized_operations();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
