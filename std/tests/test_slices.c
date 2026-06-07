#include "neverc/std/slices.h"
#include <stdio.h>
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

static void test_min_max(void) {
    printf("[min_max]\n");
    int arr[] = {5, 3, 1, 4, 2};
    ASSERT_INT_EQ(neverc_slices_min(arr, 5, sizeof(int), cmp_int), 2);
    ASSERT_INT_EQ(neverc_slices_max(arr, 5, sizeof(int), cmp_int), 0);
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
    test_min_max();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
