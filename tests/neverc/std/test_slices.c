#include "neverc/std/slices.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define ASSERT_INT_EQ(expr, expected) do { int _v=(int)(expr); int _e=(int)(expected); tests_run++; if(_v==_e)tests_passed++; else{tests_failed++; printf("  FAIL: %s=%d, expected %d (line %d)\n",#expr,_v,_e,__LINE__);}} while(0)
#define ASSERT_TRUE(expr) do { tests_run++; if(expr)tests_passed++; else{tests_failed++; printf("  FAIL: %s (line %d)\n",#expr,__LINE__);}} while(0)

static int cmp_int(const void *a, const void *b) {
    int va = *(const int *)a, vb = *(const int *)b;
    return (va > vb) - (va < vb);
}
static int eq_int(const void *a, const void *b) {
    return *(const int *)a == *(const int *)b;
}

static void test_equal(void) {
    printf("[equal]\n");
    int a[] = {1, 2, 3}, b[] = {1, 2, 3}, c[] = {1, 2, 4};
    ASSERT_TRUE(neverc_slices_equal_ints(a, 3, b, 3));
    ASSERT_TRUE(!neverc_slices_equal_ints(a, 3, c, 3));
    ASSERT_TRUE(!neverc_slices_equal_ints(a, 3, b, 2));
    ASSERT_TRUE(neverc_slices_equal_ints(NULL, 0, NULL, 0));
}

static void test_contains(void) {
    printf("[contains]\n");
    int arr[] = {10, 20, 30, 40};
    ASSERT_TRUE(neverc_slices_contains_int(arr, 4, 20));
    ASSERT_TRUE(!neverc_slices_contains_int(arr, 4, 50));
}

static void test_index(void) {
    printf("[index]\n");
    int arr[] = {5, 10, 15, 20};
    ASSERT_INT_EQ(neverc_slices_index_int(arr, 4, 15), 2);
    ASSERT_INT_EQ(neverc_slices_index_int(arr, 4, 99), -1);
}

static void test_reverse(void) {
    printf("[reverse]\n");
    int arr[] = {1, 2, 3, 4, 5};
    int exp[] = {5, 4, 3, 2, 1};
    neverc_slices_reverse_ints(arr, 5);
    ASSERT_TRUE(neverc_slices_equal_ints(arr, 5, exp, 5));
}

static void test_sort(void) {
    printf("[sort]\n");
    int arr[] = {5, 3, 1, 4, 2};
    int exp[] = {1, 2, 3, 4, 5};
    neverc_slices_sort_ints(arr, 5);
    ASSERT_TRUE(neverc_slices_equal_ints(arr, 5, exp, 5));
}

static void test_is_sorted(void) {
    printf("[is_sorted]\n");
    int sorted[] = {1, 2, 3, 4};
    int unsorted[] = {3, 1, 2};
    ASSERT_TRUE(neverc_slices_is_sorted(sorted, 4, sizeof(int), cmp_int));
    ASSERT_TRUE(!neverc_slices_is_sorted(unsorted, 3, sizeof(int), cmp_int));
    ASSERT_TRUE(neverc_slices_is_sorted_ints(sorted, 4));
    ASSERT_TRUE(!neverc_slices_is_sorted_ints(unsorted, 3));
}

static void test_binary_search(void) {
    printf("[binary_search]\n");
    int arr[] = {1, 3, 5, 7, 9};
    int found;
    ASSERT_INT_EQ(neverc_slices_binary_search(arr, 5, &(int){5}, sizeof(int), cmp_int, &found), 2);
    ASSERT_INT_EQ(found, 1);
    neverc_slices_binary_search(arr, 5, &(int){6}, sizeof(int), cmp_int, &found);
    ASSERT_INT_EQ(found, 0);
}

static void test_compact(void) {
    printf("[compact]\n");
    int arr[] = {1, 1, 2, 2, 3, 3, 3, 4};
    size_t new_len = neverc_slices_compact(arr, 8, sizeof(int), eq_int);
    ASSERT_INT_EQ((int)new_len, 4);
    ASSERT_INT_EQ(arr[0], 1);
    ASSERT_INT_EQ(arr[1], 2);
    ASSERT_INT_EQ(arr[2], 3);
    ASSERT_INT_EQ(arr[3], 4);
}

static void test_clone(void) {
    printf("[clone]\n");
    int arr[] = {10, 20, 30};
    int *c = (int *)neverc_slices_clone(arr, 3, sizeof(int));
    ASSERT_TRUE(c != NULL);
    ASSERT_TRUE(neverc_slices_equal_ints(arr, 3, c, 3));
    c[0] = 99;
    ASSERT_TRUE(!neverc_slices_equal_ints(arr, 3, c, 3));
    free(c);
}

static void test_size_overflow_rejected(void) {
    printf("[size overflow rejected]\n");
    unsigned char a = 1, b = 2;
    size_t overflowing_len = SIZE_MAX / 2 + 1;
    ASSERT_TRUE(!neverc_slices_equal(&a, overflowing_len, &b,
                                     overflowing_len, 2));
    ASSERT_TRUE(neverc_slices_clone(&a, overflowing_len, 2) == NULL);
}

static void test_min_max(void) {
    printf("[min_max]\n");
    int arr[] = {5, 3, 1, 4, 2};
    ASSERT_INT_EQ(neverc_slices_min(arr, 5, sizeof(int), cmp_int), 2);
    ASSERT_INT_EQ(neverc_slices_max(arr, 5, sizeof(int), cmp_int), 0);
    ASSERT_INT_EQ(neverc_slices_min_int(arr, 5), 2);
    ASSERT_INT_EQ(neverc_slices_max_int(arr, 5), 0);
}

static void test_stable_sort(void) {
    printf("[stable_sort]\n");
    int arr[] = {5, 3, 1, 4, 2};
    int exp[] = {1, 2, 3, 4, 5};
    neverc_slices_sort_stable(arr, 5, sizeof(int), cmp_int);
    ASSERT_TRUE(neverc_slices_equal_ints(arr, 5, exp, 5));
}

static void test_delete(void) {
    printf("[delete]\n");
    int arr[] = {1, 2, 3, 4, 5};
    size_t newlen = neverc_slices_delete(arr, 5, sizeof(int), 1, 3);
    ASSERT_INT_EQ((int)newlen, 3);
    ASSERT_INT_EQ(arr[0], 1);
    ASSERT_INT_EQ(arr[1], 4);
    ASSERT_INT_EQ(arr[2], 5);
}

static void test_insert(void) {
    printf("[insert]\n");
    int arr[10] = {1, 2, 5, 6};
    int ins[] = {3, 4};
    size_t newlen = neverc_slices_insert(arr, 4, sizeof(int), 2, ins, 2);
    ASSERT_INT_EQ((int)newlen, 6);
    ASSERT_INT_EQ(arr[0], 1);
    ASSERT_INT_EQ(arr[1], 2);
    ASSERT_INT_EQ(arr[2], 3);
    ASSERT_INT_EQ(arr[3], 4);
    ASSERT_INT_EQ(arr[4], 5);
    ASSERT_INT_EQ(arr[5], 6);
}

static void test_insert_overlapping_source(void) {
    printf("[insert overlapping source]\n");
    int arr[8] = {1, 2, 3, 4};
    size_t newlen = neverc_slices_insert(arr, 4, sizeof(int), 1, &arr[3],
                                          1);
    int expected[] = {1, 4, 2, 3, 4};
    ASSERT_INT_EQ((int)newlen, 5);
    ASSERT_TRUE(neverc_slices_equal_ints(arr, newlen, expected, 5));
}

static void test_replace(void) {
    printf("[replace]\n");
    int arr[10] = {1, 2, 3, 4, 5};
    int rep[] = {10, 20, 30};
    size_t newlen = neverc_slices_replace(arr, 5, sizeof(int), 1, 3, rep, 3);
    ASSERT_INT_EQ((int)newlen, 6);
    ASSERT_INT_EQ(arr[0], 1);
    ASSERT_INT_EQ(arr[1], 10);
    ASSERT_INT_EQ(arr[2], 20);
    ASSERT_INT_EQ(arr[3], 30);
    ASSERT_INT_EQ(arr[4], 4);
    ASSERT_INT_EQ(arr[5], 5);
}

static void test_replace_overlapping_source(void) {
    printf("[replace overlapping source]\n");
    int arr[10] = {1, 2, 3, 4, 5};
    size_t newlen = neverc_slices_replace(arr, 5, sizeof(int), 1, 2,
                                           &arr[3], 2);
    int expected[] = {1, 4, 5, 3, 4, 5};
    ASSERT_INT_EQ((int)newlen, 6);
    ASSERT_TRUE(neverc_slices_equal_ints(arr, newlen, expected, 6));
}

static void test_concat(void) {
    printf("[concat]\n");
    int a[] = {1, 2, 3}, b[] = {4, 5};
    int *c = (int *)neverc_slices_concat(a, 3, b, 2, sizeof(int));
    ASSERT_TRUE(c != NULL);
    ASSERT_INT_EQ(c[0], 1);
    ASSERT_INT_EQ(c[1], 2);
    ASSERT_INT_EQ(c[2], 3);
    ASSERT_INT_EQ(c[3], 4);
    ASSERT_INT_EQ(c[4], 5);
    free(c);
}

static void test_concat_size_overflow_rejected(void) {
    printf("[concat size overflow rejected]\n");
    unsigned char a[2] = {1, 2}, b[2] = {3, 4};
    size_t huge = SIZE_MAX / 2 + 1;
    void *result = neverc_slices_concat(a, huge, b, huge + 1, 2);
    ASSERT_TRUE(result == NULL);
    free(result);
}

static int is_even(const void *elem) { return (*(const int *)elem) % 2 == 0; }

static void test_func_ops(void) {
    printf("[func_ops]\n");
    int arr[] = {1, 2, 3, 4, 5};
    ASSERT_TRUE(neverc_slices_contains_func(arr, 5, sizeof(int), is_even));
    ASSERT_INT_EQ(neverc_slices_index_func(arr, 5, sizeof(int), is_even), 1);

    int arr2[] = {1, 2, 3, 4, 5, 6};
    size_t newlen = neverc_slices_delete_func(arr2, 6, sizeof(int), is_even);
    ASSERT_INT_EQ((int)newlen, 3);
    ASSERT_INT_EQ(arr2[0], 1);
    ASSERT_INT_EQ(arr2[1], 3);
    ASSERT_INT_EQ(arr2[2], 5);
}

static void test_null_guards(void) {
    printf("[null_guards]\n");
    int arr[] = {1, 2, 3};
    int v = 1;
    ASSERT_TRUE(neverc_slices_compare(NULL, 3, arr, 3, sizeof(int), cmp_int) != 0);
    ASSERT_INT_EQ(neverc_slices_index(NULL, 3, &v, sizeof(int), eq_int), -1);
    ASSERT_TRUE(!neverc_slices_contains(NULL, 3, &v, sizeof(int), eq_int));
    ASSERT_INT_EQ((int)neverc_slices_delete(NULL, 5, sizeof(int), 0, 1), 5);
}

int main(void) {
    printf("=== NeverC slices Tests ===\n");
    test_equal();
    test_contains();
    test_index();
    test_reverse();
    test_sort();
    test_is_sorted();
    test_binary_search();
    test_compact();
    test_clone();
    test_size_overflow_rejected();
    test_min_max();
    test_stable_sort();
    test_delete();
    test_insert();
    test_insert_overlapping_source();
    test_replace();
    test_replace_overlapping_source();
    test_concat();
    test_concat_size_overflow_rejected();
    test_func_ops();
    test_null_guards();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
