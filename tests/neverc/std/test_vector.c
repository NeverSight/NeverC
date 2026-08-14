#include <neverc/std/container/vector.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        tests_run++;                                                   \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__);  \
            fflush(stderr);                                            \
        } else {                                                       \
            tests_passed++;                                            \
        }                                                              \
    } while (0)

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ========== Construction / Destruction ========== */

static void test_new_and_free(void) {
    printf("test_new_and_free:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    ASSERT(v != NULL, "new returns non-null");
    ASSERT(neverc_vector_size(v) == 0, "new vector size == 0");
    ASSERT(neverc_vector_empty(v), "new vector is empty");
    ASSERT(neverc_vector_elem_size(v) == sizeof(int), "elem_size == sizeof(int)");
    neverc_vector_free(v);
    neverc_vector_free(NULL);
}

static void test_new_with_capacity(void) {
    printf("test_new_with_capacity:\n");
    neverc_vector_t *v = neverc_vector_new_with_capacity(sizeof(int), 100);
    ASSERT(v != NULL, "new_with_capacity returns non-null");
    ASSERT(neverc_vector_capacity(v) >= 100, "capacity >= 100");
    ASSERT(neverc_vector_size(v) == 0, "size == 0");
    neverc_vector_free(v);

    v = neverc_vector_new_with_capacity(SIZE_MAX / 2U + 1U, 2U);
    ASSERT(v == NULL, "capacity byte-size overflow is rejected");
}

static void test_new_with_size(void) {
    printf("test_new_with_size:\n");
    int fill = 42;
    neverc_vector_t *v = neverc_vector_new_with_size(sizeof(int), 5, &fill);
    ASSERT(v != NULL, "new_with_size returns non-null");
    ASSERT(neverc_vector_size(v) == 5, "size == 5");
    for (int i = 0; i < 5; i++) {
        ASSERT(*(int *)neverc_vector_at(v, i) == 42,
               "filled elements == 42");
    }
    neverc_vector_free(v);

    neverc_vector_t *v2 = neverc_vector_new_with_size(sizeof(int), 3, NULL);
    ASSERT(v2 != NULL, "new_with_size(NULL fill) non-null");
    ASSERT(*(int *)neverc_vector_at(v2, 0) == 0, "zero-filled");
    neverc_vector_free(v2);
}

static void test_from_array(void) {
    printf("test_from_array:\n");
    int arr[] = {10, 20, 30, 40, 50};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    ASSERT(v != NULL, "from_array non-null");
    ASSERT(neverc_vector_size(v) == 5, "size == 5");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 10, "elem[0] == 10");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 50, "elem[4] == 50");
    neverc_vector_free(v);
}

static void test_copy(void) {
    printf("test_copy:\n");
    int arr[] = {1, 2, 3};
    neverc_vector_t *src = neverc_vector_from_array(arr, 3, sizeof(int));
    neverc_vector_t *dst = neverc_vector_copy(src);
    ASSERT(dst != NULL, "copy non-null");
    ASSERT(neverc_vector_size(dst) == 3, "copy size == 3");
    ASSERT(*(int *)neverc_vector_at(dst, 1) == 2, "copy elem[1] == 2");

    int val = 99;
    neverc_vector_set(dst, 0, &val);
    ASSERT(*(int *)neverc_vector_at(src, 0) == 1,
           "modifying copy doesn't affect source");

    neverc_vector_free(src);
    neverc_vector_free(dst);
    ASSERT(neverc_vector_copy(NULL) == NULL, "copy(NULL) == NULL");
}

static void test_init_destroy(void) {
    printf("test_init_destroy:\n");
    neverc_vector_t v;
    neverc_vector_init(&v, sizeof(double));
    ASSERT(neverc_vector_size(&v) == 0, "init size == 0");
    ASSERT(neverc_vector_elem_size(&v) == sizeof(double),
           "init elem_size");
    double d = 3.14;
    neverc_vector_push_back(&v, &d);
    ASSERT(neverc_vector_size(&v) == 1, "push after init");
    neverc_vector_destroy(&v);
    ASSERT(v.data == NULL, "destroy sets data NULL");
    ASSERT(v.size == 0, "destroy sets size 0");
}

/* ========== Element Access ========== */

static void test_element_access(void) {
    printf("test_element_access:\n");
    int arr[] = {100, 200, 300};
    neverc_vector_t *v = neverc_vector_from_array(arr, 3, sizeof(int));

    ASSERT(*(int *)neverc_vector_front(v) == 100, "front == 100");
    ASSERT(*(int *)neverc_vector_back(v) == 300, "back == 300");
    ASSERT(neverc_vector_data(v) != NULL, "data non-null");

    int out = 0;
    ASSERT(neverc_vector_get(v, 1, &out) && out == 200, "get(1) == 200");
    ASSERT(!neverc_vector_get(v, 10, &out), "get out-of-bounds returns false");

    int val = 999;
    ASSERT(neverc_vector_set(v, 2, &val), "set(2, 999)");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 999, "at(2) == 999 after set");
    ASSERT(!neverc_vector_set(v, 10, &val), "set out-of-bounds false");

    ASSERT(neverc_vector_at(v, 10) == NULL, "at out-of-bounds == NULL");
    ASSERT(neverc_vector_at(NULL, 0) == NULL, "at(NULL) == NULL");

    neverc_vector_free(v);

    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    ASSERT(neverc_vector_front(empty) == NULL, "front of empty == NULL");
    ASSERT(neverc_vector_back(empty) == NULL, "back of empty == NULL");
    neverc_vector_free(empty);
}

/* ========== Capacity ========== */

static void test_capacity(void) {
    printf("test_capacity:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    ASSERT(neverc_vector_empty(v), "empty on new");
    ASSERT(neverc_vector_size(v) == 0, "size 0");

    ASSERT(neverc_vector_reserve(v, 50), "reserve 50");
    ASSERT(neverc_vector_capacity(v) >= 50, "capacity >= 50");
    ASSERT(neverc_vector_size(v) == 0, "reserve doesn't change size");

    for (int i = 0; i < 100; i++)
        neverc_vector_push_back(v, &i);
    ASSERT(neverc_vector_size(v) == 100, "size after 100 pushes");
    ASSERT(neverc_vector_capacity(v) >= 100, "capacity >= 100");
    ASSERT(!neverc_vector_empty(v), "not empty");

    size_t old_cap = neverc_vector_capacity(v);
    ASSERT(neverc_vector_shrink_to_fit(v), "shrink_to_fit");
    ASSERT(neverc_vector_capacity(v) == 100, "capacity == size after shrink");
    ASSERT(neverc_vector_capacity(v) <= old_cap, "cap <= old_cap");
    (void)old_cap;

    neverc_vector_free(v);
}

/* ========== Push / Pop ========== */

static void test_push_pop(void) {
    printf("test_push_pop:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));

    for (int i = 0; i < 1000; i++)
        ASSERT(neverc_vector_push_back(v, &i), "push_back succeeds");
    ASSERT(neverc_vector_size(v) == 1000, "size == 1000 after pushes");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 0, "first == 0");
    ASSERT(*(int *)neverc_vector_at(v, 999) == 999, "last == 999");

    int popped = -1;
    ASSERT(neverc_vector_pop_back(v, &popped) && popped == 999,
           "pop_back == 999");
    ASSERT(neverc_vector_size(v) == 999, "size == 999 after pop");

    ASSERT(neverc_vector_pop_back(v, NULL), "pop_back(NULL out)");
    ASSERT(neverc_vector_size(v) == 998, "size == 998");

    neverc_vector_clear(v);
    ASSERT(!neverc_vector_pop_back(v, &popped), "pop empty vector fails");

    neverc_vector_free(v);
}

/* ========== Insert / Erase ========== */

static void test_insert_erase(void) {
    printf("test_insert_erase:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));

    int a = 1, b = 2, c = 3;
    neverc_vector_push_back(v, &a);
    neverc_vector_push_back(v, &c);

    ASSERT(neverc_vector_insert(v, 1, &b), "insert at index 1");
    ASSERT(neverc_vector_size(v) == 3, "size == 3");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 1, "[0]==1");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 2, "[1]==2");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 3, "[2]==3");

    int front_val = 0;
    ASSERT(neverc_vector_insert(v, 0, &front_val), "insert at front");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 0, "front insert [0]==0");
    ASSERT(neverc_vector_size(v) == 4, "size == 4");

    int back_val = 99;
    ASSERT(neverc_vector_insert(v, neverc_vector_size(v), &back_val),
           "insert at end");
    ASSERT(*(int *)neverc_vector_back(v) == 99, "back == 99");

    ASSERT(!neverc_vector_insert(v, 100, &a), "insert out-of-bounds fails");

    ASSERT(neverc_vector_erase(v, 0), "erase front");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 1, "[0]==1 after erase front");

    size_t sz = neverc_vector_size(v);
    ASSERT(neverc_vector_erase(v, sz - 1), "erase last");
    ASSERT(neverc_vector_size(v) == sz - 1, "size decreased");

    ASSERT(!neverc_vector_erase(v, 100), "erase out-of-bounds fails");

    neverc_vector_free(v);
}

static void test_insert_range(void) {
    printf("test_insert_range:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    int a = 1, d = 4;
    neverc_vector_push_back(v, &a);
    neverc_vector_push_back(v, &d);

    int mid[] = {2, 3};
    ASSERT(neverc_vector_insert_range(v, 1, mid, 2), "insert_range");
    ASSERT(neverc_vector_size(v) == 4, "size == 4");
    for (int i = 0; i < 4; i++)
        ASSERT(*(int *)neverc_vector_at(v, i) == i + 1, "sequential values");

    neverc_vector_free(v);
}

static void test_erase_range(void) {
    printf("test_erase_range:\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    ASSERT(neverc_vector_erase_range(v, 1, 2), "erase_range(1,2)");
    ASSERT(neverc_vector_size(v) == 3, "size == 3");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 1, "[0]==1");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 4, "[1]==4");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 5, "[2]==5");

    ASSERT(!neverc_vector_erase_range(v, 10, 1), "erase_range oob fails");
    ASSERT(neverc_vector_erase_range(v, 1, SIZE_MAX),
           "overflowing erase count is clamped to the suffix");
    ASSERT(neverc_vector_size(v) == 1, "overflowing erase leaves prefix");

    neverc_vector_free(v);
}

/* ========== Resize / Clear / Swap ========== */

static void test_resize_clear(void) {
    printf("test_resize_clear:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    int fill = 7;
    ASSERT(neverc_vector_resize(v, 10, &fill), "resize to 10");
    ASSERT(neverc_vector_size(v) == 10, "size == 10");
    for (size_t i = 0; i < 10; i++)
        ASSERT(*(int *)neverc_vector_at(v, i) == 7, "all filled with 7");

    ASSERT(neverc_vector_resize(v, 5, NULL), "resize down to 5");
    ASSERT(neverc_vector_size(v) == 5, "size == 5");

    ASSERT(neverc_vector_resize(v, 8, NULL), "resize up with zero fill");
    ASSERT(*(int *)neverc_vector_at(v, 7) == 0, "new elements zero");

    neverc_vector_clear(v);
    ASSERT(neverc_vector_size(v) == 0, "clear: size == 0");
    ASSERT(neverc_vector_capacity(v) > 0, "clear: capacity preserved");

    neverc_vector_free(v);
}

static void test_swap(void) {
    printf("test_swap:\n");
    int a[] = {1, 2, 3};
    int b[] = {10, 20};
    neverc_vector_t *va = neverc_vector_from_array(a, 3, sizeof(int));
    neverc_vector_t *vb = neverc_vector_from_array(b, 2, sizeof(int));

    neverc_vector_swap(va, vb);
    ASSERT(neverc_vector_size(va) == 2, "after swap: va size==2");
    ASSERT(neverc_vector_size(vb) == 3, "after swap: vb size==3");
    ASSERT(*(int *)neverc_vector_at(va, 0) == 10, "va[0]==10");
    ASSERT(*(int *)neverc_vector_at(vb, 0) == 1, "vb[0]==1");

    neverc_vector_free(va);
    neverc_vector_free(vb);
}

/* ========== Assign / Append ========== */

static void test_assign_append(void) {
    printf("test_assign_append:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    int arr[] = {5, 6, 7};
    ASSERT(neverc_vector_assign(v, arr, 3), "assign");
    ASSERT(neverc_vector_size(v) == 3, "assign size == 3");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 7, "assign [2]==7");

    int arr2[] = {8, 9};
    neverc_vector_t *other = neverc_vector_from_array(arr2, 2, sizeof(int));
    ASSERT(neverc_vector_append(v, other), "append");
    ASSERT(neverc_vector_size(v) == 5, "append size == 5");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 9, "[4]==9");

    ASSERT(neverc_vector_append(v, v), "self append");
    ASSERT(neverc_vector_size(v) == 10, "self append doubles size");
    ASSERT(*(int *)neverc_vector_at(v, 5) == 5, "self append copies first");
    ASSERT(*(int *)neverc_vector_at(v, 9) == 9, "self append copies last");

    neverc_vector_free(other);
    neverc_vector_free(v);
}

static void test_append_invalid_source(void) {
    printf("test_append_invalid_source:\n");
    neverc_vector_t dst;
    neverc_vector_init(&dst, sizeof(int));
    int data[3] = {1, 2, 3};
    neverc_vector_t bad = {
        .data = data,
        .size = 100,
        .capacity = 3,
        .elem_size = sizeof(int),
    };
    ASSERT(!neverc_vector_append(&dst, &bad),
           "append rejects size > capacity");
    neverc_vector_destroy(&dst);
}

static void test_self_aliased_modifiers(void) {
    printf("test_self_aliased_modifiers:\n");
    int initial[] = {0, 1, 2, 3, 4, 5, 6, 7};

    neverc_vector_t *v = neverc_vector_from_array(initial, 8, sizeof(int));
    const int *value = (const int *)neverc_vector_at(v, 2);
    ASSERT(neverc_vector_push_back(v, value), "self-aliased push succeeds");
    ASSERT(*(int *)neverc_vector_back(v) == 2, "self-aliased push preserves value");
    neverc_vector_free(v);

    v = neverc_vector_from_array(initial, 8, sizeof(int));
    ASSERT(neverc_vector_reserve(v, 16), "reserve for aliased insert");
    value = (const int *)neverc_vector_at(v, 6);
    ASSERT(neverc_vector_insert(v, 1, value), "self-aliased insert succeeds");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 6,
           "self-aliased insert preserves moved value");
    neverc_vector_free(v);

    v = neverc_vector_from_array(initial, 8, sizeof(int));
    ASSERT(neverc_vector_reserve(v, 16), "reserve for aliased range insert");
    const int *range = (const int *)neverc_vector_at(v, 2);
    ASSERT(neverc_vector_insert_range(v, 1, range, 3),
           "self-aliased range insert succeeds");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 2 &&
           *(int *)neverc_vector_at(v, 2) == 3 &&
           *(int *)neverc_vector_at(v, 3) == 4,
           "self-aliased range insert preserves range");
    neverc_vector_free(v);

    v = neverc_vector_from_array(initial, 8, sizeof(int));
    ASSERT(neverc_vector_reserve(v, 16), "reserve for aliased fill insert");
    value = (const int *)neverc_vector_at(v, 6);
    ASSERT(neverc_vector_insert_fill(v, 1, 3, value),
           "self-aliased fill insert succeeds");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 6 &&
           *(int *)neverc_vector_at(v, 2) == 6 &&
           *(int *)neverc_vector_at(v, 3) == 6,
           "self-aliased fill insert preserves value");
    neverc_vector_free(v);

    v = neverc_vector_from_array(initial, 8, sizeof(int));
    value = (const int *)neverc_vector_at(v, 3);
    ASSERT(neverc_vector_resize(v, 12, value), "self-aliased resize succeeds");
    ASSERT(*(int *)neverc_vector_at(v, 8) == 3 &&
           *(int *)neverc_vector_at(v, 11) == 3,
           "self-aliased resize preserves fill value");
    neverc_vector_free(v);

    v = neverc_vector_from_array(initial, 8, sizeof(int));
    range = (const int *)neverc_vector_at(v, 2);
    ASSERT(neverc_vector_assign(v, range, 4), "self-aliased assign succeeds");
    ASSERT(neverc_vector_size(v) == 4 &&
           *(int *)neverc_vector_at(v, 0) == 2 &&
           *(int *)neverc_vector_at(v, 3) == 5,
           "self-aliased assign preserves range");
    neverc_vector_free(v);

    int adjacent_storage[] = {1, 0, 2};
    neverc_vector_t adjacent_target = {
        adjacent_storage, 1, 2, sizeof(adjacent_storage[0])
    };
    neverc_vector_t adjacent_source = {
        adjacent_storage + 2, 1, 1, sizeof(adjacent_storage[0])
    };
    ASSERT(neverc_vector_append(&adjacent_target, &adjacent_source),
           "object at target capacity end is treated as external");
    ASSERT(adjacent_target.size == 2 && adjacent_storage[1] == 2,
           "adjacent external object is appended correctly");
}

/* ========== Sort / Reverse / Unique ========== */

static void test_sort(void) {
    printf("test_sort:\n");
    int arr[] = {5, 3, 1, 4, 2};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    neverc_vector_sort(v, cmp_int);
    for (int i = 0; i < 5; i++)
        ASSERT(*(int *)neverc_vector_at(v, i) == i + 1, "sorted");
    neverc_vector_free(v);

    neverc_vector_t *v2 = neverc_vector_new(sizeof(int));
    for (int i = 999; i >= 0; i--)
        neverc_vector_push_back(v2, &i);
    neverc_vector_sort(v2, cmp_int);
    int sorted = 1;
    for (size_t i = 1; i < neverc_vector_size(v2); i++) {
        if (*(int *)neverc_vector_at(v2, i) <
            *(int *)neverc_vector_at(v2, i - 1)) {
            sorted = 0;
            break;
        }
    }
    ASSERT(sorted, "1000 elements sorted correctly");
    neverc_vector_free(v2);
}

static void test_reverse(void) {
    printf("test_reverse:\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    neverc_vector_reverse(v);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 5, "[0]==5");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 1, "[4]==1");
    neverc_vector_free(v);

    int arr2[] = {1, 2};
    neverc_vector_t *v2 = neverc_vector_from_array(arr2, 2, sizeof(int));
    neverc_vector_reverse(v2);
    ASSERT(*(int *)neverc_vector_at(v2, 0) == 2, "2-elem reverse");
    neverc_vector_free(v2);
}

static void test_unique(void) {
    printf("test_unique:\n");
    int arr[] = {1, 1, 2, 2, 2, 3, 4, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 9, sizeof(int));
    neverc_vector_unique(v, cmp_int);
    ASSERT(neverc_vector_size(v) == 5, "unique size == 5");
    int expected[] = {1, 2, 3, 4, 5};
    for (int i = 0; i < 5; i++)
        ASSERT(*(int *)neverc_vector_at(v, i) == expected[i],
               "unique values");
    neverc_vector_free(v);
}

/* ========== Find / Contains / Count ========== */

static void test_find_contains_count(void) {
    printf("test_find_contains_count:\n");
    int arr[] = {10, 20, 30, 20, 40};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    int needle = 20;
    ASSERT(neverc_vector_find(v, &needle, cmp_int) == 1, "find 20 == index 1");
    int missing = 99;
    ASSERT(neverc_vector_find(v, &missing, cmp_int) == -1, "find 99 == -1");

    ASSERT(neverc_vector_contains(v, &needle, cmp_int), "contains 20");
    ASSERT(!neverc_vector_contains(v, &missing, cmp_int), "!contains 99");

    ASSERT(neverc_vector_count(v, &needle, cmp_int) == 2, "count 20 == 2");
    int single = 10;
    ASSERT(neverc_vector_count(v, &single, cmp_int) == 1, "count 10 == 1");

    neverc_vector_free(v);
}

/* ========== Foreach / Any / All ========== */

static void sum_callback(void *elem, void *ctx) {
    *(int *)ctx += *(int *)elem;
}

static bool is_positive(const void *elem) {
    return *(const int *)elem > 0;
}

static bool is_even(const void *elem) {
    return *(const int *)elem % 2 == 0;
}

static void test_foreach_any_all(void) {
    printf("test_foreach_any_all:\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    int sum = 0;
    neverc_vector_foreach(v, sum_callback, &sum);
    ASSERT(sum == 15, "foreach sum == 15");

    ASSERT(neverc_vector_any(v, is_even), "any even");
    ASSERT(neverc_vector_all(v, is_positive), "all positive");

    int arr2[] = {1, 3, 5};
    neverc_vector_t *v2 = neverc_vector_from_array(arr2, 3, sizeof(int));
    ASSERT(!neverc_vector_any(v2, is_even), "no even in odd array");

    neverc_vector_free(v);
    neverc_vector_free(v2);
}

/* ========== Iterators ========== */

static void test_iterators(void) {
    printf("test_iterators:\n");
    int arr[] = {10, 20, 30};
    neverc_vector_t *v = neverc_vector_from_array(arr, 3, sizeof(int));

    int sum = 0;
    NEVERC_VECTOR_FOR_EACH(v, int, p) {
        sum += *p;
    }
    ASSERT(sum == 60, "for_each macro sum == 60");

    void *b = neverc_vector_begin(v);
    void *e = neverc_vector_end(v);
    ASSERT((char *)e - (char *)b == 3 * (int)sizeof(int),
           "end - begin == 3 * sizeof(int)");

    neverc_vector_free(v);

    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    ASSERT(neverc_vector_begin(empty) == neverc_vector_end(empty)
           || neverc_vector_begin(empty) == NULL,
           "empty: begin==end or both null");
    neverc_vector_free(empty);
}

/* ========== Equality ========== */

static void test_equal(void) {
    printf("test_equal:\n");
    int a[] = {1, 2, 3};
    int b[] = {1, 2, 3};
    int c[] = {1, 2, 4};

    neverc_vector_t *va = neverc_vector_from_array(a, 3, sizeof(int));
    neverc_vector_t *vb = neverc_vector_from_array(b, 3, sizeof(int));
    neverc_vector_t *vc = neverc_vector_from_array(c, 3, sizeof(int));

    ASSERT(neverc_vector_equal(va, vb, cmp_int), "a == b");
    ASSERT(!neverc_vector_equal(va, vc, cmp_int), "a != c");
    ASSERT(neverc_vector_equal(va, va, cmp_int), "a == a (self)");
    ASSERT(neverc_vector_equal(va, vb, NULL), "memcmp equality");

    neverc_vector_free(va);
    neverc_vector_free(vb);
    neverc_vector_free(vc);
}

/* ========== Type-Safe Macros ========== */

static void test_macros(void) {
    printf("test_macros:\n");
    neverc_vector_t *v = NEVERC_VECTOR_OF(double);
    ASSERT(neverc_vector_elem_size(v) == sizeof(double), "VECTOR_OF(double)");

    NEVERC_VECTOR_PUSH(v, 1.5);
    NEVERC_VECTOR_PUSH(v, 2.5);
    NEVERC_VECTOR_PUSH(v, 3.5);
    ASSERT(neverc_vector_size(v) == 3, "3 pushes via macro");

    double val = NEVERC_VECTOR_GET_AS(v, 1, double);
    ASSERT(val == 2.5, "GET_AS(1) == 2.5");

    NEVERC_VECTOR_SET_VAL(v, 0, 9.9);
    ASSERT(NEVERC_VECTOR_GET_AS(v, 0, double) == 9.9, "SET_VAL(0, 9.9)");

    double popped;
    NEVERC_VECTOR_POP_AS(v, double, popped);
    ASSERT(popped == 3.5, "POP_AS == 3.5");

    neverc_vector_free(v);
}

/* ========== Double (different element size) ========== */

static void test_double_vector(void) {
    printf("test_double_vector:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(double));
    for (int i = 0; i < 50; i++) {
        double d = i * 0.1;
        neverc_vector_push_back(v, &d);
    }
    ASSERT(neverc_vector_size(v) == 50, "50 doubles");

    double *front = (double *)neverc_vector_front(v);
    ASSERT(*front >= -0.001 && *front <= 0.001, "front ~= 0.0");

    neverc_vector_sort(v, cmp_double);
    int sorted = 1;
    for (size_t i = 1; i < 50; i++) {
        if (*(double *)neverc_vector_at(v, i) <
            *(double *)neverc_vector_at(v, i - 1)) {
            sorted = 0;
            break;
        }
    }
    ASSERT(sorted, "doubles sorted");
    neverc_vector_free(v);
}

/* ========== Struct elements ========== */

typedef struct {
    int x, y;
    char name[16];
} point_t;

static int cmp_point(const void *a, const void *b) {
    const point_t *pa = (const point_t *)a;
    const point_t *pb = (const point_t *)b;
    if (pa->x != pb->x) return pa->x - pb->x;
    return pa->y - pb->y;
}

static void test_struct_vector(void) {
    printf("test_struct_vector:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(point_t));

    point_t p1 = {3, 4, "alpha"};
    point_t p2 = {1, 2, "beta"};
    point_t p3 = {5, 6, "gamma"};
    neverc_vector_push_back(v, &p1);
    neverc_vector_push_back(v, &p2);
    neverc_vector_push_back(v, &p3);

    ASSERT(neverc_vector_size(v) == 3, "3 structs");

    point_t *back = (point_t *)neverc_vector_back(v);
    ASSERT(back->x == 5 && back->y == 6, "back == gamma");
    ASSERT(strcmp(back->name, "gamma") == 0, "back name == gamma");

    neverc_vector_sort(v, cmp_point);
    ASSERT(((point_t *)neverc_vector_at(v, 0))->x == 1, "sorted by x");
    ASSERT(((point_t *)neverc_vector_at(v, 2))->x == 5, "sorted last x==5");

    neverc_vector_free(v);
}

/* ========== Edge Cases ========== */

static void test_edge_cases(void) {
    printf("test_edge_cases:\n");

    ASSERT(neverc_vector_size(NULL) == 0, "size(NULL) == 0");
    ASSERT(neverc_vector_capacity(NULL) == 0, "capacity(NULL) == 0");
    ASSERT(neverc_vector_empty(NULL), "empty(NULL)");
    ASSERT(neverc_vector_data(NULL) == NULL, "data(NULL)");
    ASSERT(neverc_vector_begin(NULL) == NULL, "begin(NULL)");
    ASSERT(neverc_vector_end(NULL) == NULL, "end(NULL)");

    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    neverc_vector_clear(v);
    ASSERT(neverc_vector_size(v) == 0, "clear empty vector");

    ASSERT(neverc_vector_shrink_to_fit(v), "shrink empty");
    ASSERT(neverc_vector_capacity(v) == 0, "shrink empty cap==0");

    ASSERT(neverc_vector_assign(v, NULL, 0), "assign empty");
    ASSERT(neverc_vector_insert_range(v, 0, NULL, 0), "insert_range 0");

    neverc_vector_free(v);
}

/* ========== Stress Test ========== */

static void test_stress(void) {
    printf("test_stress:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));

    for (int i = 0; i < 10000; i++)
        neverc_vector_push_back(v, &i);
    ASSERT(neverc_vector_size(v) == 10000, "10k pushes");

    for (int i = 0; i < 5000; i++)
        neverc_vector_pop_back(v, NULL);
    ASSERT(neverc_vector_size(v) == 5000, "5k pops -> 5k left");

    neverc_vector_sort(v, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 0, "stress sorted first");
    ASSERT(*(int *)neverc_vector_at(v, 4999) == 4999, "stress sorted last");

    neverc_vector_free(v);
}

/* ========== Emplace ========== */

static void test_emplace(void) {
    printf("test_emplace:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));

    int *p = (int *)neverc_vector_emplace_back(v);
    ASSERT(p != NULL, "emplace_back non-null");
    ASSERT(*p == 0, "emplace_back zero-initialized");
    *p = 42;
    ASSERT(neverc_vector_size(v) == 1, "emplace_back size==1");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 42, "emplace_back value==42");

    int val = 10;
    neverc_vector_push_back(v, &val);
    val = 30;
    neverc_vector_push_back(v, &val);

    int *mid = (int *)neverc_vector_emplace(v, 1);
    ASSERT(mid != NULL, "emplace(1) non-null");
    *mid = 20;
    ASSERT(neverc_vector_size(v) == 4, "emplace size==4");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 42, "emplace [0]==42");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 20, "emplace [1]==20");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 10, "emplace [2]==10");

    neverc_vector_free(v);
}

/* ========== Erase If ========== */

static bool is_negative(const void *elem) {
    return *(const int *)elem < 0;
}

static void test_erase_if(void) {
    printf("test_erase_if:\n");
    int arr[] = {-3, 1, -2, 4, -1, 5, 0};
    neverc_vector_t *v = neverc_vector_from_array(arr, 7, sizeof(int));

    size_t removed = neverc_vector_erase_if(v, is_negative);
    ASSERT(removed == 3, "erase_if removed 3");
    ASSERT(neverc_vector_size(v) == 4, "erase_if size==4");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 1, "[0]==1");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 4, "[1]==4");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 5, "[2]==5");
    ASSERT(*(int *)neverc_vector_at(v, 3) == 0, "[3]==0");

    removed = neverc_vector_erase_if(v, is_negative);
    ASSERT(removed == 0, "erase_if no negatives");

    neverc_vector_free(v);
}

/* ========== Fill ========== */

static void test_fill(void) {
    printf("test_fill:\n");
    int zero = 0;
    neverc_vector_t *v = neverc_vector_new_with_size(sizeof(int), 5, &zero);
    int fill_val = 77;
    neverc_vector_fill(v, &fill_val);
    for (size_t i = 0; i < 5; i++)
        ASSERT(*(int *)neverc_vector_at(v, i) == 77, "filled with 77");
    neverc_vector_free(v);
}

/* ========== Swap Elements ========== */

static void test_swap_elements(void) {
    printf("test_swap_elements:\n");
    int arr[] = {10, 20, 30};
    neverc_vector_t *v = neverc_vector_from_array(arr, 3, sizeof(int));
    neverc_vector_swap_elements(v, 0, 2);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 30, "swap [0]==30");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 10, "swap [2]==10");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 20, "swap [1]==20 unchanged");
    neverc_vector_free(v);
}

/* ========== Rotate ========== */

static void test_rotate(void) {
    printf("test_rotate:\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    neverc_vector_rotate(v, 2);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 3, "rotate [0]==3");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 4, "rotate [1]==4");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 5, "rotate [2]==5");
    ASSERT(*(int *)neverc_vector_at(v, 3) == 1, "rotate [3]==1");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 2, "rotate [4]==2");
    neverc_vector_free(v);
}

/* ========== Binary Search ========== */

static void test_binary_search(void) {
    printf("test_binary_search:\n");
    int arr[] = {10, 20, 30, 40, 50};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    int needle = 30;
    ASSERT(neverc_vector_binary_search(v, &needle, cmp_int) == 2,
           "bsearch(30)==2");
    needle = 10;
    ASSERT(neverc_vector_binary_search(v, &needle, cmp_int) == 0,
           "bsearch(10)==0");
    needle = 50;
    ASSERT(neverc_vector_binary_search(v, &needle, cmp_int) == 4,
           "bsearch(50)==4");
    needle = 25;
    ASSERT(neverc_vector_binary_search(v, &needle, cmp_int) == -1,
           "bsearch(25)==-1");
    neverc_vector_free(v);
}

/* ========== Lower/Upper Bound ========== */

static void test_lower_upper_bound(void) {
    printf("test_lower_upper_bound:\n");
    int arr[] = {10, 20, 20, 20, 30};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    int needle = 20;
    ASSERT(neverc_vector_lower_bound(v, &needle, cmp_int) == 1,
           "lower_bound(20)==1");
    ASSERT(neverc_vector_upper_bound(v, &needle, cmp_int) == 4,
           "upper_bound(20)==4");

    needle = 15;
    ASSERT(neverc_vector_lower_bound(v, &needle, cmp_int) == 1,
           "lower_bound(15)==1");
    ASSERT(neverc_vector_upper_bound(v, &needle, cmp_int) == 1,
           "upper_bound(15)==1");

    needle = 35;
    ASSERT(neverc_vector_lower_bound(v, &needle, cmp_int) == 5,
           "lower_bound(35)==5");
    neverc_vector_free(v);
}

/* ========== Is Sorted ========== */

static void test_is_sorted(void) {
    printf("test_is_sorted:\n");
    int sorted[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v1 = neverc_vector_from_array(sorted, 5, sizeof(int));
    ASSERT(neverc_vector_is_sorted(v1, cmp_int), "sorted array");
    neverc_vector_free(v1);

    int unsorted[] = {1, 3, 2};
    neverc_vector_t *v2 = neverc_vector_from_array(unsorted, 3, sizeof(int));
    ASSERT(!neverc_vector_is_sorted(v2, cmp_int), "unsorted array");
    neverc_vector_free(v2);

    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    ASSERT(neverc_vector_is_sorted(empty, cmp_int), "empty is sorted");
    neverc_vector_free(empty);
}

/* ========== Stable Sort ========== */

typedef struct { int key; int order; } stable_item_t;

static int cmp_stable_key(const void *a, const void *b) {
    return ((const stable_item_t *)a)->key - ((const stable_item_t *)b)->key;
}

static void test_stable_sort(void) {
    printf("test_stable_sort:\n");
    stable_item_t items[] = {
        {3, 0}, {1, 1}, {2, 2}, {1, 3}, {3, 4}, {2, 5}
    };
    neverc_vector_t *v = neverc_vector_from_array(items, 6,
                                                    sizeof(stable_item_t));
    neverc_vector_stable_sort(v, cmp_stable_key);

    stable_item_t *arr = (stable_item_t *)neverc_vector_data(v);
    ASSERT(arr[0].key == 1 && arr[0].order == 1, "stable[0] key=1 order=1");
    ASSERT(arr[1].key == 1 && arr[1].order == 3, "stable[1] key=1 order=3");
    ASSERT(arr[2].key == 2 && arr[2].order == 2, "stable[2] key=2 order=2");
    ASSERT(arr[3].key == 2 && arr[3].order == 5, "stable[3] key=2 order=5");
    ASSERT(arr[4].key == 3 && arr[4].order == 0, "stable[4] key=3 order=0");
    ASSERT(arr[5].key == 3 && arr[5].order == 4, "stable[5] key=3 order=4");
    neverc_vector_free(v);
}

/* ========== Partial Sort ========== */

static void test_partial_sort(void) {
    printf("test_partial_sort:\n");
    int arr[] = {50, 30, 10, 40, 20};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    neverc_vector_partial_sort(v, 3, cmp_int);

    ASSERT(*(int *)neverc_vector_at(v, 0) == 10, "partial[0]==10");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 20, "partial[1]==20");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 30, "partial[2]==30");
    neverc_vector_free(v);
}

/* ========== Nth Element (introselect) ========== */

static void test_nth_element(void) {
    printf("test_nth_element:\n");

    /* Median: v[2] becomes the 3rd smallest; left <= it <= right. */
    int arr[] = {50, 30, 10, 40, 20};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    neverc_vector_nth_element(v, 2, cmp_int);
    int med = *(int *)neverc_vector_at(v, 2);
    ASSERT(med == 30, "nth_element median == 30");
    ASSERT(*(int *)neverc_vector_at(v, 0) <= med &&
           *(int *)neverc_vector_at(v, 1) <= med, "nth_element left <= median");
    ASSERT(*(int *)neverc_vector_at(v, 3) >= med &&
           *(int *)neverc_vector_at(v, 4) >= med, "nth_element right >= median");
    neverc_vector_free(v);

    /* k=0 selects the minimum, k=size-1 the maximum. */
    int arr2[] = {7, 3, 9, 1, 5, 8, 2};
    v = neverc_vector_from_array(arr2, 7, sizeof(int));
    neverc_vector_nth_element(v, 0, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 1, "nth_element min == 1");
    neverc_vector_nth_element(v, 6, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 6) == 9, "nth_element max == 9");
    neverc_vector_free(v);

    /* Large array with many duplicates: for several k, v[k] must equal the
     * k-th order statistic and partition the array around it. */
    int big[200];
    for (int i = 0; i < 200; i++) big[i] = (i * 73 + 11) % 37;
    int ref[200];
    memcpy(ref, big, sizeof(big));
    neverc_vector_t *s = neverc_vector_from_array(ref, 200, sizeof(int));
    neverc_vector_sort(s, cmp_int);
    int all_ok = 1, all_match = 1;
    for (int k = 0; k < 200; k += 13) {
        v = neverc_vector_from_array(big, 200, sizeof(int));
        neverc_vector_nth_element(v, (size_t)k, cmp_int);
        int pivot = *(int *)neverc_vector_at(v, (size_t)k);
        for (int i = 0; i < k; i++)
            if (*(int *)neverc_vector_at(v, (size_t)i) > pivot) all_ok = 0;
        for (int i = k + 1; i < 200; i++)
            if (*(int *)neverc_vector_at(v, (size_t)i) < pivot) all_ok = 0;
        if (pivot != *(int *)neverc_vector_at(s, (size_t)k)) all_match = 0;
        neverc_vector_free(v);
    }
    ASSERT(all_ok, "nth_element partitions large dup array for every k");
    ASSERT(all_match, "nth_element matches sorted order statistic");
    neverc_vector_free(s);

    /* Already-sorted and reverse-sorted inputs (adversarial for naive pivots). */
    int sorted[64], rev[64];
    for (int i = 0; i < 64; i++) { sorted[i] = i; rev[i] = 63 - i; }
    v = neverc_vector_from_array(sorted, 64, sizeof(int));
    neverc_vector_nth_element(v, 32, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 32) == 32, "nth_element sorted input");
    neverc_vector_free(v);
    v = neverc_vector_from_array(rev, 64, sizeof(int));
    neverc_vector_nth_element(v, 32, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 32) == 32, "nth_element reverse input");
    neverc_vector_free(v);

    /* Edge: size < 2 and out-of-range k are safe no-ops. */
    int one[] = {42};
    v = neverc_vector_from_array(one, 1, sizeof(int));
    neverc_vector_nth_element(v, 0, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 42, "nth_element size-1 no-op");
    neverc_vector_nth_element(v, 5, cmp_int);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 42, "nth_element out-of-range no-op");
    neverc_vector_free(v);
}

/* ---- nth_element differential fuzz (introselect vs independent qsort) ----
 * Introselect is the only sort_impl.h engine without coverage in the shared
 * sort/search differential harness, so it is exercised here through the public
 * vector API. Across the pattern-defeating battery, several element sizes (incl.
 * the >256B path that mallocs the swap buffer), and many k, three invariants
 * must hold: v[k] is the k-th order statistic, [0,k) <= v[k] <= (k,n), and the
 * multiset is preserved. libc qsort is the independent oracle (it shares no
 * code with the engine, so a shared bug cannot hide). Deterministic xorshift,
 * pure C — runs identically on every target the std harness compiles for. */
static uint64_t nth_rng = 0x243f6a8885a308d3ULL;
static uint64_t nth_xr(void) {
    nth_rng ^= nth_rng << 13; nth_rng ^= nth_rng >> 7; nth_rng ^= nth_rng << 17;
    return nth_rng;
}
static unsigned nth_ru(unsigned m) { return m ? (unsigned)(nth_xr() % m) : 0u; }

static void nth_fill(int *a, size_t n, int mode) {
    switch (mode) {
    case 0: for (size_t i = 0; i < n; i++) a[i] = (int)nth_xr(); break;
    case 1: for (size_t i = 0; i < n; i++) a[i] = (int)i; break;
    case 2: for (size_t i = 0; i < n; i++) a[i] = (int)(n - i); break;
    case 3: for (size_t i = 0; i < n; i++) a[i] = (int)nth_ru(4); break;
    case 4: for (size_t i = 0; i < n; i++) a[i] = 7; break;
    case 5: for (size_t i = 0; i < n; i++) a[i] = (int)(i < n / 2 ? i : n - i); break;
    case 6: for (size_t i = 0; i < n; i++) a[i] = (int)i;
            for (size_t i = 0; i < n / 20; i++) a[nth_ru((unsigned)n)] = (int)nth_xr();
            break;
    case 7: for (size_t i = 0; i < n; i++) a[i] = (int)(i % 16); break;
    }
}

/* es is a multiple of 4 and the sort key lives in the leading 4 bytes, so
 * cmp_int reads it for any element size; padding is zeroed by calloc, hence
 * equal-key records are byte-identical and the multiset memcmp check is exact. */
static int nth_check(size_t n, int mode, size_t es, size_t k) {
    if (n == 0 || k >= n) return 1;
    if (es < sizeof(int)) es = sizeof(int);
    int  *keys = (int *)malloc(n * sizeof(int));
    char *raw  = (char *)calloc(n, es);
    char *srt  = (char *)malloc(n * es);
    char *out  = (char *)malloc(n * es);
    if (!keys || !raw || !srt || !out) {
        free(keys); free(raw); free(srt); free(out);
        return 1;   /* OOM: skip rather than report a spurious failure */
    }
    nth_fill(keys, n, mode);
    for (size_t i = 0; i < n; i++) memcpy(raw + i * es, &keys[i], sizeof(int));
    memcpy(srt, raw, n * es);
    qsort(srt, n, es, cmp_int);

    neverc_vector_t *v = neverc_vector_from_array(raw, n, es);
    neverc_vector_nth_element(v, k, cmp_int);

    int good = 1;
    if (cmp_int(neverc_vector_at(v, k), srt + k * es) != 0) good = 0;
    for (size_t i = 0; good && i < k; i++)
        if (cmp_int(neverc_vector_at(v, i), neverc_vector_at(v, k)) > 0) good = 0;
    for (size_t i = k + 1; good && i < n; i++)
        if (cmp_int(neverc_vector_at(v, i), neverc_vector_at(v, k)) < 0) good = 0;
    for (size_t i = 0; i < n; i++) memcpy(out + i * es, neverc_vector_at(v, i), es);
    qsort(out, n, es, cmp_int);
    if (memcmp(out, srt, n * es) != 0) good = 0;

    neverc_vector_free(v);
    free(keys); free(raw); free(srt); free(out);
    return good;
}

static void test_nth_element_fuzz(void) {
    printf("test_nth_element_fuzz:\n");
    size_t sizes[] = {1, 2, 3, 5, 16, 17, 24, 25, 33, 64,
                      127, 128, 129, 257, 1000, 1500};
    size_t esz[] = {4, 8, 24, 300};   /* 300 exercises the >256B malloc-tmp path */
    int ns = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int nes = (int)(sizeof(esz) / sizeof(esz[0]));
    int all = 1;
    for (int s = 0; s < ns; s++)
        for (int mode = 0; mode < 8; mode++)
            for (int e = 0; e < nes; e++) {
                size_t n = sizes[s];
                size_t ks[5];
                int kc = 0;
                ks[kc++] = 0; ks[kc++] = n - 1; ks[kc++] = n / 2;
                ks[kc++] = nth_ru((unsigned)n); ks[kc++] = nth_ru((unsigned)n);
                for (int t = 0; t < kc; t++)
                    if (!nth_check(n, mode, esz[e], ks[t])) all = 0;
            }
    ASSERT(all, "nth_element matches qsort order statistic "
                "(8 patterns x 16 sizes x 4 elem-sizes x 5 k)");
}

/* ========== Shuffle / Sample / Stable-Partition / Inplace-Merge ========== */

/* key+index record: idx records original position so stability can be checked;
 * vbig_t (elem size 308) forces the >256B malloc buffer paths. */
typedef struct { int key; int idx; } vpair_t;
typedef struct { int key; int idx; char pad[300]; } vbig_t;

static int cmp_pair_key(const void *a, const void *b) {
    int x = ((const vpair_t *)a)->key, y = ((const vpair_t *)b)->key;
    return (x > y) - (x < y);
}
static int cmp_pair_keyidx(const void *a, const void *b) {
    const vpair_t *x = (const vpair_t *)a, *y = (const vpair_t *)b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return (x->idx > y->idx) - (x->idx < y->idx);
}
static bool pred_pair_even(const void *e) { return ((const vpair_t *)e)->key % 2 == 0; }
static int stateful_predicate_calls;
static bool pred_first_call_only(const void *e) {
    (void)e;
    return stateful_predicate_calls++ == 0;
}
static int cmp_big_key(const void *a, const void *b) {
    int x = ((const vbig_t *)a)->key, y = ((const vbig_t *)b)->key;
    return (x > y) - (x < y);
}
static bool pred_big_even(const void *e) { return ((const vbig_t *)e)->key % 2 == 0; }

static void test_shuffle(void) {
    printf("test_shuffle:\n");
    int base[200];
    for (int i = 0; i < 200; i++) base[i] = i;

    /* same seed -> identical permutation */
    neverc_vector_t *v1 = neverc_vector_from_array(base, 200, sizeof(int));
    neverc_vector_t *v2 = neverc_vector_from_array(base, 200, sizeof(int));
    neverc_vector_shuffle(v1, 12345);
    neverc_vector_shuffle(v2, 12345);
    int same = 1;
    for (size_t i = 0; i < 200; i++)
        if (NEVERC_VECTOR_GET_AS(v1, i, int) != NEVERC_VECTOR_GET_AS(v2, i, int))
            same = 0;
    ASSERT(same, "shuffle deterministic for same seed");

    /* multiset preserved (sort back to 0..199) and not the identity */
    int moved = 0;
    for (size_t i = 0; i < 200; i++)
        if (NEVERC_VECTOR_GET_AS(v1, i, int) != (int)i) { moved = 1; break; }
    ASSERT(moved, "shuffle is not the identity");
    neverc_vector_sort(v1, cmp_int);
    int perm = 1;
    for (int i = 0; i < 200; i++)
        if (NEVERC_VECTOR_GET_AS(v1, (size_t)i, int) != i) perm = 0;
    ASSERT(perm, "shuffle preserves the multiset");
    neverc_vector_free(v1);
    neverc_vector_free(v2);

    /* different seeds -> different orders (overwhelmingly likely) */
    v1 = neverc_vector_from_array(base, 200, sizeof(int));
    v2 = neverc_vector_from_array(base, 200, sizeof(int));
    neverc_vector_shuffle(v1, 1);
    neverc_vector_shuffle(v2, 2);
    int diff = 0;
    for (size_t i = 0; i < 200; i++)
        if (NEVERC_VECTOR_GET_AS(v1, i, int) != NEVERC_VECTOR_GET_AS(v2, i, int)) {
            diff = 1; break;
        }
    ASSERT(diff, "shuffle differs across seeds");
    neverc_vector_free(v1);
    neverc_vector_free(v2);

    /* size < 2 is a no-op */
    int one[] = {7};
    v1 = neverc_vector_from_array(one, 1, sizeof(int));
    neverc_vector_shuffle(v1, 5);
    ASSERT(NEVERC_VECTOR_GET_AS(v1, 0, int) == 7, "shuffle size-1 no-op");
    neverc_vector_free(v1);

    /* distribution sanity: slot 0 reaches almost every value across seeds */
    int seen[50] = {0}, distinct = 0;
    for (int t = 0; t < 2000; t++) {
        neverc_vector_t *vv = neverc_vector_from_array(base, 50, sizeof(int));
        neverc_vector_shuffle(vv, (uint64_t)t * 2654435761u + 1u);
        int val = NEVERC_VECTOR_GET_AS(vv, 0, int);
        if (val >= 0 && val < 50 && !seen[val]) { seen[val] = 1; distinct++; }
        neverc_vector_free(vv);
    }
    ASSERT(distinct >= 45, "shuffle slot 0 reaches most values");
}

static void test_sample(void) {
    printf("test_sample:\n");
    int base[100];
    for (int i = 0; i < 100; i++) base[i] = i * 3;   /* distinct, increasing */
    neverc_vector_t *v = neverc_vector_from_array(base, 100, sizeof(int));

    /* k in range: k elements, strictly increasing subsequence of the source */
    neverc_vector_t *s = neverc_vector_sample(v, 20, 42);
    ASSERT(s && neverc_vector_size(s) == 20, "sample size == k");
    int ordered = 1, subset = 1;
    long prev = -1;
    for (size_t i = 0; i < neverc_vector_size(s); i++) {
        int val = NEVERC_VECTOR_GET_AS(s, i, int);
        if (val % 3 != 0 || val < 0 || val >= 300) subset = 0;
        if ((long)val <= prev) ordered = 0;
        prev = val;
    }
    ASSERT(subset, "sample draws only from the source");
    ASSERT(ordered, "sample preserves original order (no repeats)");
    neverc_vector_free(s);

    /* determinism */
    neverc_vector_t *s1 = neverc_vector_sample(v, 30, 7);
    neverc_vector_t *s2 = neverc_vector_sample(v, 30, 7);
    int det = 1;
    for (size_t i = 0; i < 30; i++)
        if (NEVERC_VECTOR_GET_AS(s1, i, int) != NEVERC_VECTOR_GET_AS(s2, i, int))
            det = 0;
    ASSERT(det, "sample deterministic for same seed");
    neverc_vector_free(s1);
    neverc_vector_free(s2);

    /* k == 0 -> empty; k >= size -> the whole vector in order */
    s = neverc_vector_sample(v, 0, 1);
    ASSERT(s && neverc_vector_size(s) == 0, "sample k=0 empty");
    neverc_vector_free(s);
    s = neverc_vector_sample(v, 1000, 1);
    ASSERT(s && neverc_vector_size(s) == 100, "sample k>=size clamps to size");
    int full = 1;
    for (int i = 0; i < 100; i++)
        if (NEVERC_VECTOR_GET_AS(s, (size_t)i, int) != i * 3) full = 0;
    ASSERT(full, "sample k>=size returns all in order");
    neverc_vector_free(s);
    neverc_vector_free(v);

    /* uniformity sanity: each index chosen ~ k/n of the time */
    int counts[40] = {0};
    for (int t = 0; t < 4000; t++) {
        neverc_vector_t *vv = neverc_vector_from_array(base, 40, sizeof(int));
        neverc_vector_t *ss = neverc_vector_sample(vv, 10, (uint64_t)t * 40503u + 1u);
        for (size_t i = 0; i < neverc_vector_size(ss); i++) {
            int idx = NEVERC_VECTOR_GET_AS(ss, i, int) / 3;
            if (idx >= 0 && idx < 40) counts[idx]++;
        }
        neverc_vector_free(ss);
        neverc_vector_free(vv);
    }
    int uniform = 1;                       /* expected ~1000 each */
    for (int i = 0; i < 40; i++)
        if (counts[i] < 800 || counts[i] > 1200) uniform = 0;
    ASSERT(uniform, "sample roughly uniform over indices");
}

static void test_stable_partition(void) {
    printf("test_stable_partition:\n");
    enum { N = 300 };
    vpair_t *arr = (vpair_t *)malloc(N * sizeof(vpair_t));
    for (int i = 0; i < N; i++) { arr[i].key = (i * 7 + 3) % 11; arr[i].idx = i; }
    neverc_vector_t *v = neverc_vector_from_array(arr, N, sizeof(vpair_t));
    size_t p = neverc_vector_stable_partition(v, pred_pair_even);
    size_t exp = 0;
    for (int i = 0; i < N; i++) if (arr[i].key % 2 == 0) exp++;
    ASSERT(p == exp, "stable_partition returns the true count");
    int groups = 1;
    for (size_t i = 0; i < p; i++)
        if (((vpair_t *)neverc_vector_at(v, i))->key % 2 != 0) groups = 0;
    for (size_t i = p; i < (size_t)N; i++)
        if (((vpair_t *)neverc_vector_at(v, i))->key % 2 == 0) groups = 0;
    ASSERT(groups, "stable_partition separates the groups");
    int stab = 1; long prev = -1;
    for (size_t i = 0; i < p; i++) {
        long id = ((vpair_t *)neverc_vector_at(v, i))->idx;
        if (id <= prev) stab = 0; prev = id;
    }
    prev = -1;
    for (size_t i = p; i < (size_t)N; i++) {
        long id = ((vpair_t *)neverc_vector_at(v, i))->idx;
        if (id <= prev) stab = 0; prev = id;
    }
    ASSERT(stab, "stable_partition preserves order within both groups");
    neverc_vector_free(v);
    free(arr);

    /* all-true / all-false edges */
    vpair_t at[4] = {{2, 0}, {4, 1}, {6, 2}, {8, 3}};
    v = neverc_vector_from_array(at, 4, sizeof(vpair_t));
    ASSERT(neverc_vector_stable_partition(v, pred_pair_even) == 4, "stable_partition all-true");
    neverc_vector_free(v);
    vpair_t af[3] = {{1, 0}, {3, 1}, {5, 2}};
    v = neverc_vector_from_array(af, 3, sizeof(vpair_t));
    ASSERT(neverc_vector_stable_partition(v, pred_pair_even) == 0, "stable_partition all-false");
    neverc_vector_free(v);

    /* Predicate results must be consumed once, not recounted into a staging
     * buffer whose capacity was computed from different calls. */
    vpair_t stateful[3] = {{1, 0}, {2, 1}, {3, 2}};
    v = neverc_vector_from_array(stateful, 3, sizeof(vpair_t));
    stateful_predicate_calls = 0;
    ASSERT(neverc_vector_stable_partition(v, pred_first_call_only) == 1,
           "stable_partition accepts stateful predicate safely");
    ASSERT(stateful_predicate_calls == 3,
           "stable_partition evaluates each element once");
    ASSERT(((vpair_t *)neverc_vector_at(v, 0))->idx == 0 &&
           ((vpair_t *)neverc_vector_at(v, 1))->idx == 1 &&
           ((vpair_t *)neverc_vector_at(v, 2))->idx == 2,
           "stable_partition preserves stateful classification order");
    neverc_vector_free(v);

    /* large element size (308B) exercises the malloc buffer path */
    int M = 120;
    vbig_t *big = (vbig_t *)calloc((size_t)M, sizeof(vbig_t));
    for (int i = 0; i < M; i++) { big[i].key = (i * 5) % 7; big[i].idx = i; }
    v = neverc_vector_from_array(big, (size_t)M, sizeof(vbig_t));
    size_t bp = neverc_vector_stable_partition(v, pred_big_even);
    int big_ok = 1, bstab = 1; long bprev = -1;
    for (size_t i = 0; i < bp; i++) {
        vbig_t *e = (vbig_t *)neverc_vector_at(v, i);
        if (e->key % 2 != 0) big_ok = 0;
        if ((long)e->idx <= bprev) bstab = 0; bprev = e->idx;
    }
    bprev = -1;
    for (size_t i = bp; i < (size_t)M; i++) {
        vbig_t *e = (vbig_t *)neverc_vector_at(v, i);
        if (e->key % 2 == 0) big_ok = 0;
        if ((long)e->idx <= bprev) bstab = 0; bprev = e->idx;
    }
    ASSERT(big_ok && bstab, "stable_partition large-element stable");
    neverc_vector_free(v);
    free(big);
}

static void test_inplace_merge(void) {
    printf("test_inplace_merge:\n");
    enum { N = 400 };
    int mid = 137;
    vpair_t *arr = (vpair_t *)malloc(N * sizeof(vpair_t));
    for (int i = 0; i < mid; i++) { arr[i].key = i; arr[i].idx = i; }
    for (int j = mid; j < N; j++) { arr[j].key = j - mid; arr[j].idx = j; }
    neverc_vector_t *v = neverc_vector_from_array(arr, N, sizeof(vpair_t));
    neverc_vector_inplace_merge(v, (size_t)mid, cmp_pair_key);
    int ok = 1; vpair_t prev = {-1, -1};
    for (size_t i = 0; i < N; i++) {
        vpair_t *e = (vpair_t *)neverc_vector_at(v, i);
        if (e->key < prev.key) ok = 0;
        else if (e->key == prev.key && e->idx <= prev.idx) ok = 0;
        prev = *e;
    }
    ASSERT(ok, "inplace_merge sorted and stable");
    neverc_vector_free(v);
    free(arr);

    /* large element size (308B) exercises the malloc buffer path */
    int M = 120, mmid = 50;
    vbig_t *big = (vbig_t *)calloc((size_t)M, sizeof(vbig_t));
    for (int i = 0; i < mmid; i++) { big[i].key = i; big[i].idx = i; }
    for (int j = mmid; j < M; j++) { big[j].key = j - mmid; big[j].idx = j; }
    v = neverc_vector_from_array(big, (size_t)M, sizeof(vbig_t));
    neverc_vector_inplace_merge(v, (size_t)mmid, cmp_big_key);
    int bok = 1; long bpk = -1, bpi = -1;
    for (size_t i = 0; i < (size_t)M; i++) {
        vbig_t *e = (vbig_t *)neverc_vector_at(v, i);
        if (e->key < bpk) bok = 0;
        else if (e->key == bpk && (long)e->idx <= bpi) bok = 0;
        bpk = e->key; bpi = e->idx;
    }
    ASSERT(bok, "inplace_merge large-element stable");
    neverc_vector_free(v);
    free(big);

    /* mid == 0 / mid >= size are no-ops */
    vpair_t two[2] = {{5, 0}, {1, 1}};
    v = neverc_vector_from_array(two, 2, sizeof(vpair_t));
    neverc_vector_inplace_merge(v, 0, cmp_pair_key);
    ASSERT(((vpair_t *)neverc_vector_at(v, 0))->key == 5, "inplace_merge mid=0 no-op");
    neverc_vector_inplace_merge(v, 2, cmp_pair_key);
    ASSERT(((vpair_t *)neverc_vector_at(v, 0))->key == 5, "inplace_merge mid>=size no-op");
    neverc_vector_free(v);
}

/* Differential fuzz: stable_partition vs a reference stable compaction, and
 * inplace_merge vs a reference stable merge, across sizes and split points.
 * Reuses the deterministic xorshift RNG declared above. */
static void test_merge_partition_fuzz(void) {
    printf("test_merge_partition_fuzz:\n");
    size_t sizes[] = {1, 2, 3, 5, 16, 17, 33, 64, 128, 257, 1000};
    int ns = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int all_sp = 1, all_im = 1;
    for (int rep = 0; rep < 3; rep++)
        for (int s = 0; s < ns; s++) {
            size_t n = sizes[s];
            vpair_t *a = (vpair_t *)malloc(n * sizeof(vpair_t));
            vpair_t *ref = (vpair_t *)malloc(n * sizeof(vpair_t));
            if (!a || !ref) { free(a); free(ref); continue; }

            /* stable_partition vs reference compaction */
            for (size_t i = 0; i < n; i++) { a[i].key = (int)nth_ru(20); a[i].idx = (int)i; }
            neverc_vector_t *vp = neverc_vector_from_array(a, n, sizeof(vpair_t));
            size_t p = neverc_vector_stable_partition(vp, pred_pair_even);
            size_t w = 0;
            for (size_t i = 0; i < n; i++) if (a[i].key % 2 == 0) ref[w++] = a[i];
            size_t pexp = w;
            for (size_t i = 0; i < n; i++) if (a[i].key % 2 != 0) ref[w++] = a[i];
            int okp = (p == pexp);
            for (size_t i = 0; okp && i < n; i++) {
                vpair_t *e = (vpair_t *)neverc_vector_at(vp, i);
                if (e->key != ref[i].key || e->idx != ref[i].idx) okp = 0;
            }
            if (!okp) all_sp = 0;
            neverc_vector_free(vp);

            /* inplace_merge vs reference stable merge */
            for (size_t i = 0; i < n; i++) { a[i].key = (int)nth_ru(50); a[i].idx = (int)i; }
            size_t mid = nth_ru((unsigned)(n + 1));            /* 0..n */
            if (mid > 0) qsort(a, mid, sizeof(vpair_t), cmp_pair_keyidx);
            if (n > mid) qsort(a + mid, n - mid, sizeof(vpair_t), cmp_pair_keyidx);
            size_t ii = 0, jj = mid, w2 = 0;
            while (ii < mid && jj < n)
                ref[w2++] = (a[jj].key < a[ii].key) ? a[jj++] : a[ii++];
            while (ii < mid) ref[w2++] = a[ii++];
            while (jj < n) ref[w2++] = a[jj++];
            neverc_vector_t *vm = neverc_vector_from_array(a, n, sizeof(vpair_t));
            neverc_vector_inplace_merge(vm, mid, cmp_pair_key);
            int okm = 1;
            for (size_t i = 0; okm && i < n; i++) {
                vpair_t *e = (vpair_t *)neverc_vector_at(vm, i);
                if (e->key != ref[i].key || e->idx != ref[i].idx) okm = 0;
            }
            if (!okm) all_im = 0;
            neverc_vector_free(vm);

            free(a);
            free(ref);
        }
    ASSERT(all_sp, "stable_partition matches reference compaction (fuzz)");
    ASSERT(all_im, "inplace_merge matches reference stable merge (fuzz)");
}

/* ========== Min/Max Element ========== */

static void test_min_max_element(void) {
    printf("test_min_max_element:\n");
    int arr[] = {30, 10, 50, 20, 40};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    int *mn = (int *)neverc_vector_min_element(v, cmp_int);
    ASSERT(mn != NULL && *mn == 10, "min_element == 10");
    int *mx = (int *)neverc_vector_max_element(v, cmp_int);
    ASSERT(mx != NULL && *mx == 50, "max_element == 50");

    neverc_vector_free(v);

    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    ASSERT(neverc_vector_min_element(empty, cmp_int) == NULL,
           "min_element empty == NULL");
    neverc_vector_free(empty);
}

/* ========== Transform ========== */

static void double_value(void *elem, void *ctx) {
    (void)ctx;
    *(int *)elem *= 2;
}

static void test_transform(void) {
    printf("test_transform:\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    neverc_vector_transform(v, double_value, NULL);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 2, "transform [0]==2");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 6, "transform [2]==6");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 10, "transform [4]==10");
    neverc_vector_free(v);
}

/* ========== Reduce ========== */

static void sum_reduce(void *acc, const void *elem) {
    *(int *)acc += *(const int *)elem;
}

static void test_reduce(void) {
    printf("test_reduce:\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    int sum = 0;
    ASSERT(neverc_vector_reduce(v, &sum, sum_reduce), "reduce returns true");
    ASSERT(sum == 15, "reduce sum == 15");
    neverc_vector_free(v);

    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    sum = 0;
    ASSERT(!neverc_vector_reduce(empty, &sum, sum_reduce),
           "reduce empty returns false");
    neverc_vector_free(empty);
}

/* ========== Slice ========== */

static void test_slice(void) {
    printf("test_slice:\n");
    int arr[] = {10, 20, 30, 40, 50};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));

    neverc_vector_t *s = neverc_vector_slice(v, 1, 3);
    ASSERT(s != NULL, "slice non-null");
    ASSERT(neverc_vector_size(s) == 3, "slice size==3");
    ASSERT(*(int *)neverc_vector_at(s, 0) == 20, "slice [0]==20");
    ASSERT(*(int *)neverc_vector_at(s, 2) == 40, "slice [2]==40");
    neverc_vector_free(s);

    neverc_vector_t *s2 = neverc_vector_slice(v, 3, 100);
    ASSERT(neverc_vector_size(s2) == 2, "slice clamped size==2");
    neverc_vector_free(s2);

    neverc_vector_free(v);
}

/* ========== Filter ========== */

static void test_filter(void) {
    printf("test_filter:\n");
    int arr[] = {1, 2, 3, 4, 5, 6};
    neverc_vector_t *v = neverc_vector_from_array(arr, 6, sizeof(int));

    neverc_vector_t *evens = neverc_vector_filter(v, is_even);
    ASSERT(evens != NULL, "filter non-null");
    ASSERT(neverc_vector_size(evens) == 3, "filter size==3");
    ASSERT(*(int *)neverc_vector_at(evens, 0) == 2, "filter [0]==2");
    ASSERT(*(int *)neverc_vector_at(evens, 1) == 4, "filter [1]==4");
    ASSERT(*(int *)neverc_vector_at(evens, 2) == 6, "filter [2]==6");
    neverc_vector_free(evens);
    neverc_vector_free(v);
}

/* ========== Map ========== */

static void int_to_double(void *out, const void *in) {
    *(double *)out = (double)(*(const int *)in) * 1.5;
}

static void test_map(void) {
    printf("test_map:\n");
    int arr[] = {2, 4, 6};
    neverc_vector_t *v = neverc_vector_from_array(arr, 3, sizeof(int));

    neverc_vector_t *mapped = neverc_vector_map(v, sizeof(double),
                                                  int_to_double);
    ASSERT(mapped != NULL, "map non-null");
    ASSERT(neverc_vector_size(mapped) == 3, "map size==3");
    ASSERT(neverc_vector_elem_size(mapped) == sizeof(double),
           "map elem_size==double");
    double *d = (double *)neverc_vector_data(mapped);
    ASSERT(d[0] == 3.0, "map [0]==3.0");
    ASSERT(d[1] == 6.0, "map [1]==6.0");
    ASSERT(d[2] == 9.0, "map [2]==9.0");
    neverc_vector_free(mapped);
    neverc_vector_free(v);
}

/* ========== Reverse Iterators ========== */

static void test_rbegin_rend(void) {
    printf("test_rbegin_rend:\n");
    int arr[] = {1, 2, 3};
    neverc_vector_t *v = neverc_vector_from_array(arr, 3, sizeof(int));

    int *rb = (int *)neverc_vector_rbegin(v);
    ASSERT(rb != NULL && *rb == 3, "rbegin == 3");

    int *re = (int *)neverc_vector_rend(v);
    ASSERT(re != NULL, "rend non-null");

    int sum = 0;
    for (int *p = rb; p != re; p--)
        sum += *p;
    ASSERT(sum == 6, "reverse iteration sum == 6");

    neverc_vector_free(v);
}

/* ========== Compare ========== */

static void test_compare(void) {
    printf("test_compare:\n");
    int a[] = {1, 2, 3};
    int b[] = {1, 2, 4};
    int c[] = {1, 2};
    neverc_vector_t *va = neverc_vector_from_array(a, 3, sizeof(int));
    neverc_vector_t *vb = neverc_vector_from_array(b, 3, sizeof(int));
    neverc_vector_t *vc = neverc_vector_from_array(c, 2, sizeof(int));

    ASSERT(neverc_vector_compare(va, va, cmp_int) == 0, "compare a==a");
    ASSERT(neverc_vector_compare(va, vb, cmp_int) < 0, "compare a<b");
    ASSERT(neverc_vector_compare(vb, va, cmp_int) > 0, "compare b>a");
    ASSERT(neverc_vector_compare(va, vc, cmp_int) > 0,
           "compare a>c (longer)");
    ASSERT(neverc_vector_compare(vc, va, cmp_int) < 0,
           "compare c<a (shorter)");

    neverc_vector_free(va);
    neverc_vector_free(vb);
    neverc_vector_free(vc);

    struct wide_value { unsigned char bytes[32]; } wide = {{7}};
    unsigned char narrow = 7;
    neverc_vector_t *vw = neverc_vector_from_array(
        &wide, 1, sizeof(wide));
    neverc_vector_t *vn = neverc_vector_from_array(
        &narrow, 1, sizeof(narrow));
    ASSERT(neverc_vector_compare(vw, vn, NULL) > 0,
           "wider element vector sorts after narrower element vector");
    ASSERT(neverc_vector_compare(vn, vw, NULL) < 0,
           "narrower element vector sorts before wider element vector");
    neverc_vector_free(vw);
    neverc_vector_free(vn);
}

/* ========== Max Size ========== */

static void test_max_size(void) {
    printf("test_max_size:\n");
    neverc_vector_t *v = neverc_vector_new(sizeof(int));
    size_t ms = neverc_vector_max_size(v);
    ASSERT(ms > 0, "max_size > 0");
    ASSERT(ms >= 1000000, "max_size >= 1M for int");
    neverc_vector_free(v);
}

/* ========== Insert Fill ========== */

static void test_insert_fill(void) {
    printf("test_insert_fill:\n");
    int arr[] = {1, 5};
    neverc_vector_t *v = neverc_vector_from_array(arr, 2, sizeof(int));
    int fill = 3;
    ASSERT(neverc_vector_insert_fill(v, 1, 3, &fill), "insert_fill");
    ASSERT(neverc_vector_size(v) == 5, "size==5");
    ASSERT(*(int *)neverc_vector_at(v, 0) == 1, "[0]==1");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 3, "[1]==3");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 3, "[2]==3");
    ASSERT(*(int *)neverc_vector_at(v, 3) == 3, "[3]==3");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 5, "[4]==5");
    neverc_vector_free(v);
}

/* ========== Find If ========== */

static void test_find_if(void) {
    printf("test_find_if:\n");
    int arr[] = {1, 3, 4, 5, 7};
    neverc_vector_t *v = neverc_vector_from_array(arr, 5, sizeof(int));
    ASSERT(neverc_vector_find_if(v, is_even) == 2, "find_if even==2");

    int arr2[] = {1, 3, 5};
    neverc_vector_t *v2 = neverc_vector_from_array(arr2, 3, sizeof(int));
    ASSERT(neverc_vector_find_if(v2, is_even) == -1, "find_if no even==-1");

    neverc_vector_free(v);
    neverc_vector_free(v2);
}

/* ========== Count If ========== */

static void test_count_if(void) {
    printf("test_count_if:\n");
    int arr[] = {1, 2, 3, 4, 5, 6};
    neverc_vector_t *v = neverc_vector_from_array(arr, 6, sizeof(int));
    ASSERT(neverc_vector_count_if(v, is_even) == 3, "count_if even==3");
    ASSERT(neverc_vector_count_if(v, is_positive) == 6, "count_if pos==6");
    neverc_vector_free(v);
}

/* ========== None Of ========== */

static void test_none(void) {
    printf("test_none:\n");
    int arr[] = {1, 3, 5, 7};
    neverc_vector_t *v = neverc_vector_from_array(arr, 4, sizeof(int));
    ASSERT(neverc_vector_none(v, is_even), "none even in odds");

    int arr2[] = {1, 2, 3};
    neverc_vector_t *v2 = neverc_vector_from_array(arr2, 3, sizeof(int));
    ASSERT(!neverc_vector_none(v2, is_even), "not none even");

    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    ASSERT(neverc_vector_none(empty, is_even), "none on empty");
    neverc_vector_free(empty);
    neverc_vector_free(v);
    neverc_vector_free(v2);
}

/* ========== Partition ========== */

static void test_partition(void) {
    printf("test_partition:\n");
    int arr[] = {1, 2, 3, 4, 5, 6};
    neverc_vector_t *v = neverc_vector_from_array(arr, 6, sizeof(int));
    size_t pivot = neverc_vector_partition(v, is_even);

    for (size_t i = 0; i < pivot; i++)
        ASSERT(*(int *)neverc_vector_at(v, i) % 2 == 0,
               "partition: left half even");
    for (size_t i = pivot; i < neverc_vector_size(v); i++)
        ASSERT(*(int *)neverc_vector_at(v, i) % 2 != 0,
               "partition: right half odd");

    neverc_vector_free(v);
}

/* ========== Generate ========== */

static void gen_squares(void *elem, size_t index, void *ctx) {
    (void)ctx;
    *(int *)elem = (int)(index * index);
}

static void test_generate(void) {
    printf("test_generate:\n");
    int zero = 0;
    neverc_vector_t *v = neverc_vector_new_with_size(sizeof(int), 5, &zero);
    neverc_vector_generate(v, gen_squares, NULL);
    ASSERT(*(int *)neverc_vector_at(v, 0) == 0, "gen [0]==0");
    ASSERT(*(int *)neverc_vector_at(v, 1) == 1, "gen [1]==1");
    ASSERT(*(int *)neverc_vector_at(v, 2) == 4, "gen [2]==4");
    ASSERT(*(int *)neverc_vector_at(v, 3) == 9, "gen [3]==9");
    ASSERT(*(int *)neverc_vector_at(v, 4) == 16, "gen [4]==16");
    neverc_vector_free(v);
}

/* ========== Merge ========== */

static void test_merge(void) {
    printf("test_merge:\n");
    int a[] = {1, 3, 5, 7};
    int b[] = {2, 4, 6};
    neverc_vector_t *va = neverc_vector_from_array(a, 4, sizeof(int));
    neverc_vector_t *vb = neverc_vector_from_array(b, 3, sizeof(int));

    neverc_vector_t *merged = neverc_vector_merge(va, vb, cmp_int);
    ASSERT(merged != NULL, "merge non-null");
    ASSERT(neverc_vector_size(merged) == 7, "merge size==7");
    for (int i = 0; i < 7; i++)
        ASSERT(*(int *)neverc_vector_at(merged, i) == i + 1,
               "merge sequential");

    neverc_vector_free(merged);

    neverc_vector_t *only_a = neverc_vector_merge(va, NULL, cmp_int);
    ASSERT(only_a != NULL, "merge(a, NULL) copies a");
    ASSERT(neverc_vector_size(only_a) == 4, "merge(a,NULL) size==4");
    neverc_vector_free(only_a);

    neverc_vector_t *only_b = neverc_vector_merge(NULL, vb, cmp_int);
    ASSERT(only_b != NULL, "merge(NULL, b) copies b");
    ASSERT(neverc_vector_size(only_b) == 3, "merge(NULL,b) size==3");
    neverc_vector_free(only_b);

    ASSERT(neverc_vector_merge(NULL, NULL, cmp_int) == NULL,
           "merge(NULL,NULL)==NULL");

    neverc_vector_free(va);
    neverc_vector_free(vb);
}

/* ========== minmax_element ========== */

static unsigned vt_rng = 0x1234abcdu;
static unsigned vt_next(void) {
    vt_rng = vt_rng * 1103515245u + 12345u;
    return vt_rng >> 8;
}

static void test_minmax_element(void) {
    printf("test_minmax_element:\n");

    int arr[] = {5, 2, 8, 1, 9, 3, 9, 1};
    neverc_vector_t *v = neverc_vector_from_array(arr, 8, sizeof(int));
    void *mn = (void *)1, *mx = (void *)1;
    neverc_vector_minmax_element(v, cmp_int, &mn, &mx);
    ASSERT(mn && *(int *)mn == 1, "minmax min == 1");
    ASSERT(mx && *(int *)mx == 9, "minmax max == 9");
    ASSERT(mn == neverc_vector_at(v, 3), "minmax min is FIRST smallest (idx 3)");
    ASSERT(mx == neverc_vector_at(v, 6), "minmax max is LAST largest (idx 6)");
    neverc_vector_free(v);

    /* empty -> both NULL; out-params may be NULL individually */
    v = neverc_vector_new(sizeof(int));
    mn = (void *)1; mx = (void *)1;
    neverc_vector_minmax_element(v, cmp_int, &mn, &mx);
    ASSERT(mn == NULL && mx == NULL, "minmax empty -> NULL,NULL");
    neverc_vector_minmax_element(v, cmp_int, NULL, NULL); /* must not crash */
    neverc_vector_free(v);

    /* single element -> min == max == that element */
    int one[] = {42};
    v = neverc_vector_from_array(one, 1, sizeof(int));
    neverc_vector_minmax_element(v, cmp_int, &mn, &mx);
    ASSERT(mn == neverc_vector_at(v, 0) && mx == neverc_vector_at(v, 0),
           "minmax size-1 min==max");
    neverc_vector_free(v);

    /* differential fuzz: first-min / last-max oracle, even & odd lengths */
    int allok = 1;
    for (int it = 0; it < 4000 && allok; it++) {
        size_t n = (size_t)(vt_next() % 64) + 1;
        neverc_vector_t *vv = neverc_vector_new(sizeof(int));
        int emn = 0, emx = 0;
        for (size_t i = 0; i < n; i++) {
            int x = (int)(vt_next() % 7);
            neverc_vector_push_back(vv, &x);
            if (i == 0) { emn = emx = 0; }
            else {
                int cur = *(int *)neverc_vector_at(vv, i);
                if (cur < *(int *)neverc_vector_at(vv, (size_t)emn)) emn = (int)i;
                if (cur >= *(int *)neverc_vector_at(vv, (size_t)emx)) emx = (int)i;
            }
        }
        neverc_vector_minmax_element(vv, cmp_int, &mn, &mx);
        if (mn != neverc_vector_at(vv, (size_t)emn) ||
            mx != neverc_vector_at(vv, (size_t)emx))
            allok = 0;
        neverc_vector_free(vv);
    }
    ASSERT(allok, "minmax matches first-min/last-max oracle (fuzz)");
}

/* ========== equal_range / partition_point ========== */

static int vt_thresh;
static bool vt_pred_lt(const void *e) { return *(const int *)e < vt_thresh; }

static void test_equal_range(void) {
    printf("test_equal_range:\n");

    int arr[] = {1, 2, 2, 2, 4, 4, 7};
    neverc_vector_t *v = neverc_vector_from_array(arr, 7, sizeof(int));
    size_t lo = 99, hi = 99;
    int key = 2;
    ASSERT(neverc_vector_equal_range(v, &key, cmp_int, &lo, &hi),
           "equal_range(2) found");
    ASSERT(lo == 1 && hi == 4, "equal_range(2) == [1,4)");
    key = 5;
    ASSERT(!neverc_vector_equal_range(v, &key, cmp_int, &lo, &hi),
           "equal_range(5) absent");
    ASSERT(lo == hi && lo == 6, "equal_range(5) empty at insertion point 6");
    key = 0;
    neverc_vector_equal_range(v, &key, cmp_int, &lo, &hi);
    ASSERT(lo == 0 && hi == 0, "equal_range(0) == [0,0)");
    key = 7;
    neverc_vector_equal_range(v, &key, cmp_int, &lo, &hi);
    ASSERT(lo == 6 && hi == 7, "equal_range(7) == [6,7)");
    /* NULL out-params must be tolerated */
    key = 4;
    ASSERT(neverc_vector_equal_range(v, &key, cmp_int, NULL, NULL),
           "equal_range NULL out-params, returns presence");
    neverc_vector_free(v);

    /* differential fuzz: equal_range and partition_point vs linear scan */
    int allok = 1;
    for (int it = 0; it < 6000 && allok; it++) {
        size_t n = (size_t)(vt_next() % 40);
        int mod = (int)(vt_next() % 8) + 1;
        int buf[40];
        for (size_t i = 0; i < n; i++) buf[i] = (int)(vt_next() % (unsigned)mod);
        qsort(buf, n, sizeof(int), cmp_int);
        neverc_vector_t *vv = neverc_vector_from_array(buf, n, sizeof(int));

        int k = (int)(vt_next() % (unsigned)(mod + 1));
        neverc_vector_equal_range(vv, &k, cmp_int, &lo, &hi);
        size_t elo = n, ehi = n, found_lo = 0;
        for (size_t i = 0; i < n; i++) if (buf[i] >= k) { elo = i; found_lo = 1; break; }
        if (!found_lo) elo = n;
        ehi = n;
        for (size_t i = 0; i < n; i++) if (buf[i] > k) { ehi = i; break; }
        if (lo != elo || hi != ehi) allok = 0;

        vt_thresh = (int)(vt_next() % (unsigned)(mod + 1));
        size_t pp = neverc_vector_partition_point(vv, vt_pred_lt);
        size_t epp = 0;
        while (epp < n && buf[epp] < vt_thresh) epp++;
        if (pp != epp) allok = 0;

        neverc_vector_free(vv);
    }
    ASSERT(allok, "equal_range/partition_point match linear oracle (fuzz)");
}

/* ========== Sorted-set operations ========== */

/* op: 0=union 1=intersect 2=diff 3=symdiff — reference multiset semantics. */
static size_t vt_ref_setop(int op, const int *a, size_t na,
                           const int *b, size_t nb, int *out) {
    size_t i = 0, j = 0, w = 0;
    while (i < na && j < nb) {
        if (a[i] < b[j]) { if (op != 1) out[w++] = a[i]; i++; }
        else if (a[i] > b[j]) { if (op == 0 || op == 3) out[w++] = b[j]; j++; }
        else { if (op == 0 || op == 1) out[w++] = a[i]; i++; j++; }
    }
    if (op != 1) while (i < na) out[w++] = a[i++];
    if (op == 0 || op == 3) while (j < nb) out[w++] = b[j++];
    return w;
}

static int vt_vec_eq_ref(const neverc_vector_t *v, const int *ref, size_t rn) {
    if (neverc_vector_size(v) != rn) return 0;
    for (size_t i = 0; i < rn; i++)
        if (*(int *)neverc_vector_at((neverc_vector_t *)v, i) != ref[i]) return 0;
    return 1;
}

static void test_set_operations(void) {
    printf("test_set_operations:\n");

    int a[] = {1, 2, 2, 3, 5};
    int b[] = {2, 3, 3, 6};
    neverc_vector_t *va = neverc_vector_from_array(a, 5, sizeof(int));
    neverc_vector_t *vb = neverc_vector_from_array(b, 4, sizeof(int));

    int u[]  = {1, 2, 2, 3, 3, 5, 6};   /* union: max counts */
    int in[] = {2, 3};                  /* intersection: min counts */
    int df[] = {1, 2, 5};               /* a\b: max(m-n,0) */
    int sd[] = {1, 2, 3, 5, 6};         /* symmetric diff: |m-n| */

    neverc_vector_t *r = neverc_vector_set_union(va, vb, cmp_int);
    ASSERT(r && vt_vec_eq_ref(r, u, 7), "set_union multiset semantics");
    neverc_vector_free(r);
    r = neverc_vector_set_intersection(va, vb, cmp_int);
    ASSERT(r && vt_vec_eq_ref(r, in, 2), "set_intersection multiset semantics");
    neverc_vector_free(r);
    r = neverc_vector_set_difference(va, vb, cmp_int);
    ASSERT(r && vt_vec_eq_ref(r, df, 3), "set_difference multiset semantics");
    neverc_vector_free(r);
    r = neverc_vector_set_symmetric_difference(va, vb, cmp_int);
    ASSERT(r && vt_vec_eq_ref(r, sd, 5), "set_symmetric_difference semantics");
    neverc_vector_free(r);

    /* empty inputs and NULL handling */
    neverc_vector_t *empty = neverc_vector_new(sizeof(int));
    r = neverc_vector_set_union(va, empty, cmp_int);
    ASSERT(r && neverc_vector_size(r) == 5, "union with empty == a");
    neverc_vector_free(r);
    r = neverc_vector_set_intersection(va, empty, cmp_int);
    ASSERT(r && neverc_vector_size(r) == 0, "intersection with empty == empty");
    neverc_vector_free(r);
    ASSERT(neverc_vector_set_union(NULL, vb, cmp_int) == NULL,
           "set op NULL input -> NULL");
    neverc_vector_free(empty);
    neverc_vector_free(va);
    neverc_vector_free(vb);

    /* differential fuzz vs reference multiset oracle */
    int allok = 1;
    for (int it = 0; it < 5000 && allok; it++) {
        size_t na = (size_t)(vt_next() % 24), nb = (size_t)(vt_next() % 24);
        int ba[24], bb[24], ref[48];
        int mod = (int)(vt_next() % 12) + 1;
        for (size_t i = 0; i < na; i++) ba[i] = (int)(vt_next() % (unsigned)mod);
        for (size_t i = 0; i < nb; i++) bb[i] = (int)(vt_next() % (unsigned)mod);
        qsort(ba, na, sizeof(int), cmp_int);
        qsort(bb, nb, sizeof(int), cmp_int);
        neverc_vector_t *xa = neverc_vector_from_array(ba, na, sizeof(int));
        neverc_vector_t *xb = neverc_vector_from_array(bb, nb, sizeof(int));
        neverc_vector_t *(*ops[4])(const neverc_vector_t *, const neverc_vector_t *,
                                   neverc_vector_cmp_fn) = {
            neverc_vector_set_union, neverc_vector_set_intersection,
            neverc_vector_set_difference, neverc_vector_set_symmetric_difference};
        for (int op = 0; op < 4 && allok; op++) {
            size_t rn = vt_ref_setop(op, ba, na, bb, nb, ref);
            neverc_vector_t *got = ops[op](xa, xb, cmp_int);
            if (!got || !vt_vec_eq_ref(got, ref, rn)) allok = 0;
            neverc_vector_free(got);
        }
        neverc_vector_free(xa);
        neverc_vector_free(xb);
    }
    ASSERT(allok, "set operations match reference multiset oracle (fuzz)");
}

/* ========== includes ========== */

static void test_includes(void) {
    printf("test_includes:\n");
    int a[] = {1, 2, 2, 3, 4};
    int sub1[] = {2, 2, 3};
    int sub2[] = {2, 2, 2};   /* more 2s than a has */
    int sub3[] = {5};
    neverc_vector_t *va = neverc_vector_from_array(a, 5, sizeof(int));
    neverc_vector_t *vs1 = neverc_vector_from_array(sub1, 3, sizeof(int));
    neverc_vector_t *vs2 = neverc_vector_from_array(sub2, 3, sizeof(int));
    neverc_vector_t *vs3 = neverc_vector_from_array(sub3, 1, sizeof(int));
    neverc_vector_t *vempty = neverc_vector_new(sizeof(int));

    ASSERT(neverc_vector_includes(va, vs1, cmp_int), "includes multiset subset");
    ASSERT(!neverc_vector_includes(va, vs2, cmp_int), "includes respects multiplicity");
    ASSERT(!neverc_vector_includes(va, vs3, cmp_int), "includes missing element false");
    ASSERT(neverc_vector_includes(va, vempty, cmp_int), "includes empty -> true");
    ASSERT(neverc_vector_includes(va, va, cmp_int), "includes self -> true");
    ASSERT(!neverc_vector_includes(vempty, vs1, cmp_int), "empty includes non-empty -> false");
    ASSERT(!neverc_vector_includes(NULL, vs1, cmp_int), "includes NULL -> false");

    neverc_vector_free(va); neverc_vector_free(vs1); neverc_vector_free(vs2);
    neverc_vector_free(vs3); neverc_vector_free(vempty);

    /* differential fuzz: includes(a,b) iff set_difference(b,a) is empty */
    int allok = 1;
    for (int it = 0; it < 5000 && allok; it++) {
        size_t na = (size_t)(vt_next() % 20), nb = (size_t)(vt_next() % 12);
        int ba[20], bb[12];
        int mod = (int)(vt_next() % 8) + 1;
        for (size_t i = 0; i < na; i++) ba[i] = (int)(vt_next() % (unsigned)mod);
        for (size_t i = 0; i < nb; i++) bb[i] = (int)(vt_next() % (unsigned)mod);
        qsort(ba, na, sizeof(int), cmp_int);
        qsort(bb, nb, sizeof(int), cmp_int);
        neverc_vector_t *xa = neverc_vector_from_array(ba, na, sizeof(int));
        neverc_vector_t *xb = neverc_vector_from_array(bb, nb, sizeof(int));
        bool inc = neverc_vector_includes(xa, xb, cmp_int);
        neverc_vector_t *bdiffa = neverc_vector_set_difference(xb, xa, cmp_int);
        bool ref = (neverc_vector_size(bdiffa) == 0);
        if (inc != ref) allok = 0;
        neverc_vector_free(bdiffa);
        neverc_vector_free(xa); neverc_vector_free(xb);
    }
    ASSERT(allok, "includes(a,b) == (b\\a empty) (fuzz)");
}

/* ========== Permutations ========== */

static unsigned long vt_fact(int n) {
    unsigned long f = 1;
    for (int i = 2; i <= n; i++) f *= (unsigned long)i;
    return f;
}

static void test_permutations(void) {
    printf("test_permutations:\n");

    /* enumerate all permutations of {1,2,3,4}: exactly 4! in strict lexicographic
     * order, then a final false that wraps back to the sorted sequence */
    int base[] = {1, 2, 3, 4};
    neverc_vector_t *v = neverc_vector_from_array(base, 4, sizeof(int));
    unsigned long count = 1;          /* the initial (sorted) permutation */
    int strict_inc = 1, ok_more = 1;
    int prev[4]; for (int i = 0; i < 4; i++) prev[i] = base[i];
    while (neverc_vector_next_permutation(v, cmp_int)) {
        count++;
        /* lexicographically greater than the previous */
        int cur[4], greater = 0;
        for (int i = 0; i < 4; i++) cur[i] = *(int *)neverc_vector_at(v, (size_t)i);
        for (int i = 0; i < 4; i++) {
            if (cur[i] != prev[i]) { greater = cur[i] > prev[i]; break; }
        }
        if (!greater) strict_inc = 0;
        for (int i = 0; i < 4; i++) prev[i] = cur[i];
        if (count > 100) { ok_more = 0; break; }   /* runaway guard */
    }
    ASSERT(ok_more, "next_permutation terminates");
    ASSERT(count == vt_fact(4), "next_permutation yields 4! permutations");
    ASSERT(strict_inc, "next_permutation strictly lexicographically increasing");
    /* after returning false it is back to the sorted order */
    int wrapped = 1;
    for (int i = 0; i < 4; i++)
        if (*(int *)neverc_vector_at(v, (size_t)i) != base[i]) wrapped = 0;
    ASSERT(wrapped, "next_permutation wraps to first");
    neverc_vector_free(v);

    /* prev_permutation is the exact inverse: from the largest, enumerate down */
    int desc[] = {4, 3, 2, 1};
    v = neverc_vector_from_array(desc, 4, sizeof(int));
    unsigned long dcount = 1;
    while (neverc_vector_prev_permutation(v, cmp_int)) {
        dcount++;
        if (dcount > 100) break;
    }
    ASSERT(dcount == vt_fact(4), "prev_permutation yields 4! permutations");
    int dwrapped = 1;
    for (int i = 0; i < 4; i++)
        if (*(int *)neverc_vector_at(v, (size_t)i) != desc[i]) dwrapped = 0;
    ASSERT(dwrapped, "prev_permutation wraps to last");
    neverc_vector_free(v);

    /* round trip: next then prev restores the original (non-extremal) order */
    int mid[] = {2, 4, 1, 3};
    v = neverc_vector_from_array(mid, 4, sizeof(int));
    ASSERT(neverc_vector_next_permutation(v, cmp_int), "next on interior perm");
    ASSERT(neverc_vector_prev_permutation(v, cmp_int), "prev on interior perm");
    int restored = 1;
    for (int i = 0; i < 4; i++)
        if (*(int *)neverc_vector_at(v, (size_t)i) != mid[i]) restored = 0;
    ASSERT(restored, "next then prev round-trips");
    neverc_vector_free(v);

    /* duplicates: {1,1,2} has 3 distinct permutations (3!/2!), non-decreasing */
    int dup[] = {1, 1, 2};
    v = neverc_vector_from_array(dup, 3, sizeof(int));
    unsigned long dc = 1;
    while (neverc_vector_next_permutation(v, cmp_int)) { dc++; if (dc > 50) break; }
    ASSERT(dc == 3, "next_permutation counts distinct perms with duplicates");
    neverc_vector_free(v);

    /* size < 2 is always false */
    int one[] = {7};
    v = neverc_vector_from_array(one, 1, sizeof(int));
    ASSERT(!neverc_vector_next_permutation(v, cmp_int), "next size-1 false");
    ASSERT(!neverc_vector_prev_permutation(v, cmp_int), "prev size-1 false");
    neverc_vector_free(v);
}

/* ========== Main ========== */

int main(void) {
    test_new_and_free();
    test_new_with_capacity();
    test_new_with_size();
    test_from_array();
    test_copy();
    test_init_destroy();
    test_element_access();
    test_capacity();
    test_push_pop();
    test_insert_erase();
    test_insert_range();
    test_erase_range();
    test_resize_clear();
    test_swap();
    test_assign_append();
    test_append_invalid_source();
    test_self_aliased_modifiers();
    test_sort();
    test_reverse();
    test_unique();
    test_find_contains_count();
    test_foreach_any_all();
    test_iterators();
    test_equal();
    test_macros();
    test_double_vector();
    test_struct_vector();
    test_edge_cases();
    test_stress();
    test_emplace();
    test_erase_if();
    test_fill();
    test_swap_elements();
    test_rotate();
    test_binary_search();
    test_lower_upper_bound();
    test_is_sorted();
    test_stable_sort();
    test_partial_sort();
    test_nth_element();
    test_nth_element_fuzz();
    test_shuffle();
    test_sample();
    test_stable_partition();
    test_inplace_merge();
    test_merge_partition_fuzz();
    test_min_max_element();
    test_transform();
    test_reduce();
    test_slice();
    test_filter();
    test_map();
    test_rbegin_rend();
    test_compare();
    test_max_size();
    test_insert_fill();
    test_find_if();
    test_count_if();
    test_none();
    test_partition();
    test_generate();
    test_merge();
    test_minmax_element();
    test_equal_range();
    test_set_operations();
    test_includes();
    test_permutations();

    printf("\n=== vector: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
