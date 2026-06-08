#include <neverc/std/container/vector.h>
#include <stdio.h>
#include <string.h>

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

    neverc_vector_free(other);
    neverc_vector_free(v);
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

    printf("\n=== vector: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
